#include <Geode/Geode.hpp>
#include <Geode/modify/CCMenu.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <box2d/box2d.h>
#include <random>
#include <vector>

using namespace geode::prelude;

class ModCreatorLayer;

constexpr float PTM_RATIO = 32.0f; // pixel to meter

struct ButtonPhysics {
    CCNode *node = nullptr;
    b2Body *body = nullptr;
};

class TouchPhysicsHandler : public CCLayer {
  public:
    ModCreatorLayer *m_creatorLayer = nullptr;

    static TouchPhysicsHandler *create(ModCreatorLayer *layer);
    virtual bool ccTouchBegan(CCTouch *touch, CCEvent *event) override;
    virtual void ccTouchMoved(CCTouch *touch, CCEvent *event) override;
    virtual void ccTouchEnded(CCTouch *touch, CCEvent *event) override;
    virtual void registerWithTouchDispatcher() override;
    virtual void onExit() override;
};

class $modify(ModCreatorLayer, CreatorLayer) {
    struct Fields {
        std::vector<ButtonPhysics> m_physicsButtons;
        bool m_buttonsCaptured = false;
        bool m_physicsDraggingEnabled = false;

        b2World *m_world = nullptr;
        b2Body *m_groundBody = nullptr;
        b2MouseJoint *m_mouseJoint = nullptr;

        ~Fields() {
            if (m_world) {
                delete m_world;
                m_world = nullptr;
            }
        }
    };

    bool init() {
        if (!CreatorLayer::init())
            return false;

        if (!Mod::get()->getSettingValue<bool>("enable-physics")) {
            return true;
        }

        m_fields->m_buttonsCaptured = false;
        m_fields->m_physicsButtons.clear();
        m_fields->m_mouseJoint = nullptr;
        m_fields->m_physicsDraggingEnabled = false;

        float gravityY = static_cast<float>(
            Mod::get()->getSettingValue<double>("physics-gravity"));

        b2Vec2 gravity(0.0f, gravityY);
        m_fields->m_world = new b2World(gravity);
        m_fields->m_world->SetAllowSleeping(false);

        b2BodyDef groundBodyDef;
        m_fields->m_groundBody = m_fields->m_world->CreateBody(&groundBodyDef);

        auto menu =
            typeinfo_cast<CCMenu *>(this->getChildByID("creator-buttons-menu"));
        if (menu) {
            menu->schedule(
                schedule_selector(ModCreatorLayer::customPhysicsUpdate));
            menu->setTouchEnabled(true);

            auto touchOverlay = TouchPhysicsHandler::create(this);
            if (touchOverlay) {
                this->addChild(touchOverlay, 999);
                touchOverlay->setTouchEnabled(true);
            }

            auto exitMenu = this->getChildByID("exit-menu");
            if (exitMenu) {
                auto toggler = CCMenuItemToggler::createWithStandardSprites(
                    this, menu_selector(ModCreatorLayer::onTogglePhysics),
                    0.7f);
                toggler->setID("physics-toggler"_spr);
                toggler->toggle(m_fields->m_physicsDraggingEnabled);
                exitMenu->addChild(toggler);

                auto label = CCLabelBMFont::create("Drag", "bigFont.fnt");
                label->setScale(0.35f);
                label->setPosition({17, 0});
                toggler->addChild(label);

                if (auto layout =
                        typeinfo_cast<AxisLayout *>(exitMenu->getLayout())) {
                    layout->setAxisReverse(true);
                    exitMenu->updateLayout();
                }
            }
        }
        return true;
    }

    void onTogglePhysics(CCObject *sender) {
        auto toggler = typeinfo_cast<CCMenuItemToggler *>(sender);
        m_fields->m_physicsDraggingEnabled = !toggler->isToggled();

        if (auto menu = typeinfo_cast<CCMenu *>(
                this->getChildByID("creator-buttons-menu"))) {
            menu->setTouchEnabled(!m_fields->m_physicsDraggingEnabled);
        }
    }

    void customPhysicsUpdate(float dt) {
        auto menu = static_cast<CCMenu *>(static_cast<CCObject *>(this));
        if (!menu)
            return;

        auto creatorLayer = static_cast<ModCreatorLayer *>(menu->getParent());
        if (!creatorLayer || !creatorLayer->m_fields->m_world)
            return;

        if (dt <= 0.0f)
            dt = 1.0f / 60.0f;

        if (!creatorLayer->m_fields->m_buttonsCaptured) {
            menu->updateLayout();
            CCSize menuSize = menu->getContentSize();
            auto winSize = CCDirector::sharedDirector()->getWinSize();

            CCPoint botLeft = menu->convertToNodeSpace(CCPoint{0.0f, 0.0f});
            CCPoint topRight = menu->convertToNodeSpace(
                CCPoint{winSize.width, winSize.height});

            b2BodyDef wallBodyDef;
            wallBodyDef.position.Set(0, 0);
            b2Body *staticWalls =
                creatorLayer->m_fields->m_world->CreateBody(&wallBodyDef);

            b2EdgeShape wallShape;
            b2FixtureDef wallFixture;
            wallFixture.shape = &wallShape;
            wallFixture.friction = 0.5f;
            wallFixture.restitution = 0.3f;

            float infiniteHeightY = topRight.y * 100.0f;

            wallShape.SetTwoSided(
                b2Vec2(botLeft.x / PTM_RATIO, botLeft.y / PTM_RATIO),
                b2Vec2(topRight.x / PTM_RATIO, botLeft.y / PTM_RATIO));
            staticWalls->CreateFixture(&wallFixture);

            bool removeCeiling =
                Mod::get()->getSettingValue<bool>("remove-ceiling");
            if (!removeCeiling) {
                wallShape.SetTwoSided(
                    b2Vec2(botLeft.x / PTM_RATIO, topRight.y / PTM_RATIO),
                    b2Vec2(topRight.x / PTM_RATIO, topRight.y / PTM_RATIO));
                staticWalls->CreateFixture(&wallFixture);
            }

            wallShape.SetTwoSided(
                b2Vec2(botLeft.x / PTM_RATIO, botLeft.y / PTM_RATIO),
                b2Vec2(botLeft.x / PTM_RATIO, infiniteHeightY / PTM_RATIO));
            staticWalls->CreateFixture(&wallFixture);

            wallShape.SetTwoSided(
                b2Vec2(topRight.x / PTM_RATIO, botLeft.y / PTM_RATIO),
                b2Vec2(topRight.x / PTM_RATIO, infiniteHeightY / PTM_RATIO));
            staticWalls->CreateFixture(&wallFixture);

            auto children = menu->getChildren();
            if (children && children->count() > 0) {
                std::random_device rd;
                std::mt19937 gen(rd());

                std::uniform_real_distribution<float> forceX(-5.0f, 5.0f);
                std::uniform_real_distribution<float> forceY(2.0f, 10.0f);
                std::uniform_real_distribution<float> torque(-2.0f, 2.0f);

                float customFriction = static_cast<float>(
                    Mod::get()->getSettingValue<double>("physics-friction"));
                float customBounciness = static_cast<float>(
                    Mod::get()->getSettingValue<double>("physics-bounciness"));

                for (int i = 0; i < children->count(); ++i) {
                    auto btn =
                        typeinfo_cast<CCNode *>(children->objectAtIndex(i));
                    if (btn) {
                        CCPoint pos = btn->getPosition();

                        float hitboxScale = 0.887f;
                        float w =
                            btn->getScaledContentSize().width * hitboxScale;
                        float h =
                            btn->getScaledContentSize().height * hitboxScale;

                        b2BodyDef bodyDef;
                        bodyDef.type = b2_dynamicBody;
                        bodyDef.position.Set(pos.x / PTM_RATIO,
                                             pos.y / PTM_RATIO);
                        bodyDef.angularDamping = 0.8f;
                        bodyDef.linearDamping = 0.1f;

                        b2Body *body =
                            creatorLayer->m_fields->m_world->CreateBody(
                                &bodyDef);

                        b2PolygonShape boxShape;
                        boxShape.SetAsBox((w * 0.5f) / PTM_RATIO,
                                          (h * 0.5f) / PTM_RATIO);

                        b2FixtureDef fixtureDef;
                        fixtureDef.shape = &boxShape;
                        fixtureDef.density = 1.0f;

                        fixtureDef.friction = customFriction;
                        fixtureDef.restitution = customBounciness;

                        body->CreateFixture(&fixtureDef);

                        float mass = body->GetMass();
                        b2Vec2 randomImpulse(forceX(gen) * mass,
                                             forceY(gen) * mass);

                        body->ApplyLinearImpulseToCenter(randomImpulse, true);
                        body->ApplyAngularImpulse(torque(gen) * mass, true);

                        ButtonPhysics bp;
                        bp.node = btn;
                        bp.body = body;
                        creatorLayer->m_fields->m_physicsButtons.push_back(bp);
                    }
                }
                menu->setLayout(nullptr);
                menu->setContentSize(menuSize);
                creatorLayer->m_fields->m_buttonsCaptured = true;
            }
            return;
        }

        creatorLayer->m_fields->m_world->Step(dt, 8, 3);

        for (auto &bp : creatorLayer->m_fields->m_physicsButtons) {
            if (!bp.node || !bp.body)
                continue;

            b2Vec2 b2Pos = bp.body->GetPosition();
            float angle = bp.body->GetAngle();

            bp.node->setPosition(
                CCPoint{b2Pos.x * PTM_RATIO, b2Pos.y * PTM_RATIO});
            bp.node->setRotation(-1.0f * CC_RADIANS_TO_DEGREES(angle));
        }
    }
};

TouchPhysicsHandler *TouchPhysicsHandler::create(ModCreatorLayer *layer) {
    auto ret = new TouchPhysicsHandler();
    if (ret && ret->init()) {
        ret->m_creatorLayer = layer;
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TouchPhysicsHandler::ccTouchBegan(CCTouch *touch, CCEvent *event) {
    if (!m_creatorLayer || !m_creatorLayer->m_fields->m_world ||
        !m_creatorLayer->m_fields->m_buttonsCaptured)
        return false;

    if (!m_creatorLayer->m_fields->m_physicsDraggingEnabled)
        return false;

    auto menu = typeinfo_cast<CCMenu *>(
        m_creatorLayer->getChildByID("creator-buttons-menu"));
    if (!menu)
        return false;

    CCPoint localClickPos = menu->convertToNodeSpace(touch->getLocation());
    b2Vec2 physicsTargetPos(localClickPos.x / PTM_RATIO,
                            localClickPos.y / PTM_RATIO);

    for (auto &bp : m_creatorLayer->m_fields->m_physicsButtons) {
        if (!bp.body)
            continue;

        for (b2Fixture *f = bp.body->GetFixtureList(); f; f = f->GetNext()) {
            if (f->TestPoint(physicsTargetPos)) {
                b2MouseJointDef md;
                md.bodyA = m_creatorLayer->m_fields->m_groundBody;
                md.bodyB = bp.body;
                md.target = physicsTargetPos;
                md.maxForce = 2000.0f * bp.body->GetMass();

                float frequencyHz = 5.0f;
                float dampingRatio = 0.7f;
                b2LinearStiffness(md.stiffness, md.damping, frequencyHz,
                                  dampingRatio, md.bodyA, md.bodyB);

                m_creatorLayer->m_fields->m_mouseJoint =
                    static_cast<b2MouseJoint *>(
                        m_creatorLayer->m_fields->m_world->CreateJoint(&md));
                bp.body->SetAwake(true);
                return true;
            }
        }
    }
    return false;
}

void TouchPhysicsHandler::ccTouchMoved(CCTouch *touch, CCEvent *event) {
    bool isDragging =
        (m_creatorLayer && m_creatorLayer->m_fields->m_mouseJoint);
    if (!isDragging)
        return;

    auto menu = typeinfo_cast<CCMenu *>(
        m_creatorLayer->getChildByID("creator-buttons-menu"));
    if (menu) {
        CCPoint localClickPos = menu->convertToNodeSpace(touch->getLocation());
        b2Vec2 physicsTargetPos(localClickPos.x / PTM_RATIO,
                                localClickPos.y / PTM_RATIO);
        m_creatorLayer->m_fields->m_mouseJoint->SetTarget(physicsTargetPos);
    }
}

void TouchPhysicsHandler::ccTouchEnded(CCTouch *touch, CCEvent *event) {
    if (!m_creatorLayer)
        return;
    if (m_creatorLayer->m_fields->m_mouseJoint) {
        m_creatorLayer->m_fields->m_world->DestroyJoint(
            m_creatorLayer->m_fields->m_mouseJoint);
        m_creatorLayer->m_fields->m_mouseJoint = nullptr;
    }
}

void TouchPhysicsHandler::registerWithTouchDispatcher() {
    auto dispatcher = CCDirector::sharedDirector()->getTouchDispatcher();
    dispatcher->addTargetedDelegate(this, -129, true);
}

void TouchPhysicsHandler::onExit() {
    auto dispatcher = CCDirector::sharedDirector()->getTouchDispatcher();
    dispatcher->removeDelegate(this);
    CCLayer::onExit();
}