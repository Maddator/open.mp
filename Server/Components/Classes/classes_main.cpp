#include <Server/Components/Classes/classes.hpp>
#include <netcode.hpp>

static const struct DefaultClass final : public PlayerClass {
    DefaultClass() {
        team = 255;
        skin = 0;
        spawn = Vector3(0.0f, 0.0f, 3.1279f);
        angle = 0.f;
        weapons.fill(WeaponSlotData{ 0, 0 });
    }
} defClass;

struct PlayerClassData final : IPlayerClassData {
    IPlayer& player;
    PlayerClass cls;
    bool& inClassRequest;
    bool& skipDefaultClassRequest;

    PlayerClassData(IPlayer& player, bool& inClassRequest, bool& skipDefaultClassRequest) :
        player(player),
        cls(defClass),
        inClassRequest(inClassRequest),
        skipDefaultClassRequest(skipDefaultClassRequest)
    {}

	const PlayerClass& getClass() override {
		return cls;
	}

	void setSpawnInfo(const PlayerClass& info) override {
		cls.team = info.team;
		cls.skin = info.skin;
		cls.spawn = info.spawn;
		cls.angle = info.angle;
		cls.weapons = info.weapons;

        if (inClassRequest) {
            skipDefaultClassRequest = true;
        }
        else {
            const WeaponSlots& weapons = info.weapons;
            StaticArray<uint32_t, 3> weaponIDsArray = { weapons[0].id, weapons[1].id, weapons[2].id };
            StaticArray<uint32_t, 3> weaponAmmoArray = { weapons[0].ammo, weapons[1].ammo, weapons[2].ammo };
            NetCode::RPC::SetSpawnInfo setSpawnInfoRPC;
            setSpawnInfoRPC.TeamID = info.team;
            setSpawnInfoRPC.ModelID = info.skin;
            setSpawnInfoRPC.Spawn = info.spawn;
            setSpawnInfoRPC.ZAngle = info.angle;
            setSpawnInfoRPC.Weapons = NetworkArray<uint32_t>(weaponIDsArray);
            setSpawnInfoRPC.Ammos = NetworkArray<uint32_t>(weaponAmmoArray);

            player.sendRPC(setSpawnInfoRPC);
        }
	}

	void free() override {
		delete this;
	}
};

struct ClassesComponent final : public IClassesComponent, public PlayerEventHandler {
    MarkedPoolStorage<PlayerClass, PlayerClass, IClassesComponent::Capacity> storage;
    DefaultEventDispatcher<ClassEventHandler> eventDispatcher;
    bool inClassRequest;
    bool skipDefaultClassRequest;
	ICore* core;

    struct PlayerRequestClassHandler : public SingleNetworkInOutEventHandler {
        ClassesComponent& self;
        PlayerRequestClassHandler(ClassesComponent& self) : self(self) {}
	        bool received(IPlayer& peer, INetworkBitStream& bs) override {
            NetCode::RPC::PlayerRequestClass playerRequestClassPacket;
            if (!playerRequestClassPacket.read(bs)) {
                return false;
            }

            self.inClassRequest = true;
            self.skipDefaultClassRequest = false;
            if (self.eventDispatcher.stopAtFalse(
                [&peer, &playerRequestClassPacket](ClassEventHandler* handler) {
                    return handler->onPlayerRequestClass(peer, playerRequestClassPacket.Classid);
                }
            )) {
                if (self.skipDefaultClassRequest) {
                    IPlayerClassData* clsData = peer.queryData<IPlayerClassData>();
                    if (clsData) {
                        const PlayerClass& cls = clsData->getClass();
                        const WeaponSlots& weapons = cls.weapons;
                        StaticArray<uint32_t, 3> weaponIDsArray = { weapons[0].id, weapons[1].id, weapons[2].id };
                        StaticArray<uint32_t, 3> weaponAmmoArray = { weapons[0].ammo, weapons[1].ammo, weapons[2].ammo };
                        NetCode::RPC::PlayerRequestClassResponse playerRequestClassResponse(cls.team, cls.skin, cls.spawn, cls.angle);
                        playerRequestClassResponse.Selectable = true;
                        playerRequestClassResponse.Unknown1 = 0;
                        playerRequestClassResponse.Weapons = NetworkArray<uint32_t>(weaponIDsArray);
                        playerRequestClassResponse.Ammos = NetworkArray<uint32_t>(weaponAmmoArray);

                        peer.sendRPC(playerRequestClassResponse);
                    }
                }
                else if (self.storage.valid(playerRequestClassPacket.Classid)) {
                    const PlayerClass& cls = self.storage.get(playerRequestClassPacket.Classid);
                    IPlayerClassData* clsData = peer.queryData<IPlayerClassData>();
                    if (clsData) {
                        PlayerClassData* clsDataCast = static_cast<PlayerClassData*>(clsData);
                        clsDataCast->cls = cls;
                    }
                    const WeaponSlots& weapons = cls.weapons;
                    StaticArray<uint32_t, 3> weaponIDsArray = { weapons[0].id, weapons[1].id, weapons[2].id };
                    StaticArray<uint32_t, 3> weaponAmmoArray = { weapons[0].ammo, weapons[1].ammo, weapons[2].ammo };
                    NetCode::RPC::PlayerRequestClassResponse playerRequestClassResponse(cls.team, cls.skin, cls.spawn, cls.angle);
                    playerRequestClassResponse.Selectable = true;
                    playerRequestClassResponse.Unknown1 = 0;
                    playerRequestClassResponse.Weapons = NetworkArray<uint32_t>(weaponIDsArray);
                    playerRequestClassResponse.Ammos = NetworkArray<uint32_t>(weaponAmmoArray);

                    peer.sendRPC(playerRequestClassResponse);
                }
                else {
                    const WeaponSlots& weapons = defClass.weapons;
                    StaticArray<uint32_t, 3> weaponIDsArray = { weapons[0].id, weapons[1].id, weapons[2].id };
                    StaticArray<uint32_t, 3> weaponAmmoArray = { weapons[0].ammo, weapons[1].ammo, weapons[2].ammo };
                    NetCode::RPC::PlayerRequestClassResponse playerRequestClassResponse(defClass.team, defClass.skin, defClass.spawn, defClass.angle);
                    playerRequestClassResponse.Selectable = true;
                    playerRequestClassResponse.Unknown1 = 0;
                    playerRequestClassResponse.Weapons = NetworkArray<uint32_t>(weaponIDsArray);
                    playerRequestClassResponse.Ammos = NetworkArray<uint32_t>(weaponAmmoArray);

                    peer.sendRPC(playerRequestClassResponse);
                }
            }
            else {
                StaticArray<uint32_t, 3> weaponIDsArray = { 0, 0, 0 };
                StaticArray<uint32_t, 3> weaponAmmoArray = { 0, 0, 0 };
                NetCode::RPC::PlayerRequestClassResponse playerRequestClassResponseNotAllowed;
                playerRequestClassResponseNotAllowed.Selectable = false;
                playerRequestClassResponseNotAllowed.Weapons = NetworkArray<uint32_t>(weaponIDsArray);
                playerRequestClassResponseNotAllowed.Ammos = NetworkArray<uint32_t>(weaponAmmoArray);

                peer.sendRPC(playerRequestClassResponseNotAllowed);
            }

            self.inClassRequest = false;
            return true;
        }
    } onPlayerRequestClassHandler;

	ClassesComponent() :
        onPlayerRequestClassHandler(*this)
	{
	}

    void onLoad(ICore* c) override {
        core = c;
        core->addPerRPCEventHandler<NetCode::RPC::PlayerRequestClass>(&onPlayerRequestClassHandler);
        core->getPlayers().getEventDispatcher().addEventHandler(this);
    }

    IEventDispatcher<ClassEventHandler>& getEventDispatcher() override {
        return eventDispatcher;
    }

    StringView componentName() const override {
		return "Classes";
	}

    SemanticVersion componentVersion() const override {
        return SemanticVersion(0, 0, 0, BUILD_NUMBER);
    }

    PlayerClass* create(int skin, int team, Vector3 spawn, float angle, const WeaponSlots& weapons) override {
        int freeIdx = storage.findFreeIndex();
        if (freeIdx == -1) {
            // No free index
            return nullptr;
        }

        int cid = storage.claim(freeIdx);
        if (cid == -1) {
            // No free index
            return nullptr;
        }

        PlayerClass& cls = storage.get(cid);
        cls.skin = skin;
        cls.team = team;
        cls.spawn = spawn;
        cls.angle = angle;
        cls.weapons = weapons;
        return &cls;
    }

	IPlayerData* onPlayerDataRequest(IPlayer& player) override {
		return new PlayerClassData(player, inClassRequest, skipDefaultClassRequest);
	}

	void free() override {
		delete this;
	}

    int findFreeIndex() override {
        return storage.findFreeIndex();
    }

    int claim() override {
        int res = storage.claim();
        return res;
    }

    int claim(int hint) override {
        int res = storage.claim(hint);
        return res;
    }

    bool valid(int index) const override {
        return storage.valid(index);
    }

    PlayerClass& get(int index) override {
        return storage.get(index);
    }

    void release(int index) override {
        storage.release(index, false);
    }

    void lock(int index) override {
        storage.lock(index);
    }

    void unlock(int index) override {
        storage.unlock(index);
    }

    const FlatPtrHashSet<PlayerClass>& entries() override {
        return storage.entries();
    }

	~ClassesComponent() {
        core->removePerRPCEventHandler<NetCode::RPC::PlayerRequestClass>(&onPlayerRequestClassHandler);
		core->getPlayers().getEventDispatcher().removeEventHandler(this);
	}
};

COMPONENT_ENTRY_POINT() {
	return new ClassesComponent();
}
