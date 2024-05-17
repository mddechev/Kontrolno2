#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <cstring>
#include <new>
#include <stdexcept>

class Appliance;

struct PowerSource
{
    virtual void consumptionChanged() = 0;
    virtual float getCurrentConsumption() const = 0;
    virtual float getMaxConsumption() const = 0;
    virtual void setSource(Appliance& app);
    virtual void clearSource(Appliance& app) const;
    virtual ~PowerSource() {}
};

enum class Type : uint64_t
{
    Heater  = 1,
    TV      = Heater << 1,
    Fridge  = TV << 1
};

class Appliance
{
public:
    Appliance(Type t, const char* br, const char* mod, const char* sn, float power)
        : type(t), isOn(false), source(nullptr)
    {
        if ((!br || !br[0]) || (!mod || !mod[0]) || (!sn || !sn[0]) || power <= 0) {
            throw std::invalid_argument("Bad arguments for creating an appliance!");
        }

        consumption = power;

        brand = new(std::nothrow)char[strlen(br) + 1];
        model = new(std::nothrow)char[strlen(mod) + 1];
        SN = new(std::nothrow)char[strlen(sn) + 1];
        if (!brand || !model || !SN) {
            delete[] brand;
            delete[] model;
            delete[] SN;
            throw std::bad_alloc{};
        }

        strcpy(brand, br);
        strcpy(model, mod);
        strcpy(SN, sn);
    }

    Appliance(const Appliance& app)
        : consumption(app.consumption), isOn(false), source(nullptr), type(app.type)
    {
        brand = new(std::nothrow)char[strlen(app.brand) + 1];
        model = new(std::nothrow)char[strlen(app.model) + 1];
        SN = new(std::nothrow)char[strlen(app.SN) + 1];
        if (!brand || !model || !SN) {
            delete[] brand;
            delete[] model;
            delete[] SN;
            throw std::bad_alloc{};
        }

        strcpy(brand, app.brand);
        strcpy(model, app.model);
        strcpy(SN, app.SN);
    }
    Appliance& operator=(const Appliance& app) = delete;
    virtual ~Appliance()
    {
        delete[] model;
        delete[] brand;
        delete[] SN;
    }
    virtual Appliance* clone() const = 0;

    void print() const
    {
        std::cout << "Model: " << model
            << ", brand: " << brand
            << ", type: " << (uint64_t)type
            << ", Serial num: " << SN
            << " is " << (isOn ? "on" : "off")
            << " power consumptiuon: " << getPower() << " KW.\n";
    }
    Type getType() const
    {
        return type;
    }
    const char* getBrand() const
    {
        return brand;
    }
    const char* getModel() const
    {
        return model;
    }
    const char* getSerial() const
    {
        return SN;
    }
    virtual float getPower()const
    {
        return isOn ? consumption : 0;
    }

    bool isON() const
    {
        return isOn;
    }
    bool turnOn()
    {
        if (isOn || !source) return false;

        float current = source->getCurrentConsumption() - getPower();
        float max = source->getMaxConsumption();

        isOn = true;
        if (current + getPower() > max) {
            isOn = false;
            return false;
        }
        source->consumptionChanged();
        return true;
    }
    bool turnOff()
    {
        if (!isOn) return false;
        
        isOn = false;
        source->consumptionChanged();
        return true;
    }

protected:
    char* brand;
    char* model;
    char* SN;

    float consumption;
    bool isOn;
    PowerSource* source;

private:
    const Type type;

    friend struct PowerSource;
    void setSource(PowerSource* src)
    {
        if (src) {
            if (source)
                throw std::logic_error("Can not plug a plugged device!");
            source = src;
            src->consumptionChanged();
        }
        else {
            isOn = false;
            if (source)
                source->consumptionChanged();
            source = nullptr;
        }
    }
};

class Heater : public Appliance
{
public:
    Heater(const char* br, const char* mod, const char* sn,
        float power) : Appliance(Type::Heater, br, mod, sn, power)
    {}

    virtual Appliance* clone() const override
    {
        return new Heater(*this);
    }
};

class TV : public Appliance
{
public:
    TV(const char* br, const char* mod, const char* sn,
        float power, unsigned initialBrightness) : Appliance(Type::TV, br, mod, sn, power)
    {
        if (initialBrightness > 100) 
            throw std::invalid_argument("Bad value for brightness!");
        brightness = initialBrightness;
    }
    virtual Appliance* clone() const override
    {
        return new TV(*this);
    }

    virtual float getPower()const override
    {
        return isOn ? (consumption * brightness / 100) : 0.05f;
    }

    unsigned getBrightness()const
    {
        return brightness;
    }
    void setBrightness(unsigned br)
    {
        if (br <= 100) {
            brightness = br;
            if (source) {
                source->consumptionChanged();
            }
        }
    }

protected:
    unsigned brightness;
};

class Fridge : public Appliance
{
public:
    Fridge(const char* br, const char* mod, const char* sn,
        float power, unsigned comp) : Appliance(Type::Fridge, br, mod, sn, power)
    {
        compressors = comp;
    }
    virtual Appliance* clone() const override
    {
        return new Fridge(*this);
    }

    virtual float getPower()const override
    {
        return isOn ? compressors * consumption : 0;
    }

    unsigned getCompressors() const
    {
        return compressors;
    }

protected:
    unsigned compressors;
};

void PowerSource::setSource(Appliance& app)
{ app.setSource(this); }
void PowerSource::clearSource(Appliance& app) const
{ app.setSource(nullptr); }

class Room : public PowerSource
{
public:
    Room(const char* name, unsigned sockets, float maxPower)
        : sockets(nullptr), socketCnt(0), maxSockets(sockets)
        , maxPower(0), powerDown(false)
        , forbidden(0)
    {
        if (maxPower <= 0) throw std::invalid_argument("Invalid value for room power!");
        this->maxPower = maxPower;
        setName(name);
        this->sockets = new Appliance * [maxSockets];
    }
    Room(const Room& other)
        : sockets(nullptr), socketCnt(0), maxSockets(other.maxSockets)
        , maxPower(other.maxPower), powerDown(other.powerDown), forbidden(other.forbidden)
    {
        strcpy(name, other.name);

        if (maxSockets) {
            try {
                copyAppliances(other.sockets, other.socketCnt);
            }
            catch (...) {
                removeAllAppliances();
                throw;
            }
        }
    }
    Room& operator=(const Room& other)
    {
        if (this != &other) {
            Room copy(other);
            swap(copy);
        }
        return *this;
    }
    ~Room()
    {
        removeAllAppliances();
    }

    void addForbidden(Type t)
    {
        forbidden |= (uint64_t)t;
    }
    void clearForbidden(Type t)
    {
        forbidden &= ~(uint64_t)t;
    }

    Room& operator+= (const Appliance& app)
    {
        if (!powerDown && socketCnt < maxSockets && 
            !(forbidden & (uint64_t)app.getType())) {
            Appliance* toAdd = app.clone();
            setSource(*toAdd);
            sockets[socketCnt++] = toAdd;
        }
        return *this;
    }
    Room operator+(const Appliance& app)const
    {
        return Room(*this) += app;
    }
    Room& operator-= (const char* appSerial)
    {
        unsigned pos = findApp(appSerial);
        if (pos < socketCnt) {
            Appliance* toRemove = sockets[pos];
            sockets[pos] = sockets[--socketCnt];
            clearSource(*toRemove);
            delete toRemove;
        }
        return *this;
    }
    Room operator-(const char* appSerial)const
    {
        return Room(*this) -= appSerial;
    }
    const Appliance* operator[](const char* sn) const
    {
        unsigned pos = findApp(sn);
        if (pos < socketCnt) {
            return sockets[pos];
        }
        return nullptr;
    }
    Appliance* operator[](const char* sn)
    {
        return const_cast<Appliance*>((*(const Room*)this)[sn]);
    }

    virtual void consumptionChanged() override
    {
        float power = getCurrentConsumption();
        if (power > maxPower) {
            removeAllAppliances();
            powerDown = true;
            std::cerr << "Power down!!!\n";
        }
    }
    virtual float getCurrentConsumption() const override
    {
        float total = 0;
        for (unsigned i = 0; i < socketCnt; ++i) {
            total += sockets[i]->getPower();
        }
        return total;
    }
    virtual float getMaxConsumption() const override
    {
        return maxPower;
    }

    void setName(const char* name)
    {
        if (!name) name = "";
        strncpy(this->name, name, 30);
        this->name[30] = '\0';
    }
    const char* getName()const
    {
        return name;
    }
    void print()const
    {
        std::cout << "\t----\tRoom " << name << "\t----\t\n";
        std::cout << "Max power: " << maxPower << "; current consumption: " << getCurrentConsumption() << "\n";
        std::cout << "Power state: " << (powerDown ? "Down!" : "OK") << "\n";
        std::cout << "Total plugged devices: " << socketCnt << "\n";
        std::cout << "Total sockets: " << maxSockets << "\n";
        for (unsigned i = 0; i < socketCnt; ++i) {
            sockets[i]->print();
        }
        std::cout << std::endl;
    }
protected:
    void removeAllAppliances()
    {
        for (unsigned i = 0; i < socketCnt; ++i) {
            delete sockets[i];
        }
        delete[] sockets;
        sockets = nullptr;
        socketCnt = 0;
        maxSockets = 0;
    }
    void copyAppliances(Appliance** app, unsigned cnt)
    {
        socketCnt = 0;
        sockets = new Appliance * [maxSockets];
        for (unsigned i = 0; i < cnt; ++i) {
            *this += *app[i];
            if (app[i]->isON()) {
                this->sockets[i]->turnOn();
            }
        }
    }
    void swap(Room& other) noexcept
    {
        std::swap(sockets, other.sockets);
        std::swap(socketCnt, other.socketCnt);
        std::swap(maxPower, other.maxPower);

        strcpy(name, other.name);
        powerDown = other.powerDown;
        forbidden = other.forbidden;
    }
    
    unsigned findApp(const char* sn) const
    {
        if (sn) {
            for (unsigned i = 0; i < socketCnt; ++i) {
                if (!strcmp(sockets[i]->getSerial(), sn))
                    return i;
            }
        }
        return maxSockets;
    }

protected:
    char name[32];
    
    Appliance** sockets;
    unsigned socketCnt, maxSockets;
    float maxPower;
    bool powerDown;

    uint64_t forbidden;
};

int main()
{
    Heater heater("Peshy", "Mega heat", "P MH140-7765d", 2);
    TV tv("Sony", "Mony", "SN123", 0.25f, 100);

    Room bedroom("Bedroom", 5, 2.1f);
    bedroom.addForbidden(Type::Fridge);
    bedroom.addForbidden(Type::TV);
    bedroom.print();

    bedroom += heater;
    bedroom["P MH140-7765d"]->turnOn();
    bedroom.print();

    Room guestroom(bedroom);
    guestroom.setName("Guestroom");
    guestroom.clearForbidden(Type::TV);
    guestroom.print();

    guestroom += tv;
    std::cout << "On ? " << guestroom["SN123"]->turnOn() << std::endl;
    guestroom.print();

    guestroom -= "P MH140-7765d";
    guestroom["SN123"]->turnOn();
    guestroom.print();
    
    ((TV*)guestroom["SN123"])->setBrightness(20);
    guestroom["SN123"]->turnOn();
    guestroom.print();

    guestroom += heater;
    guestroom[heater.getSerial()]->turnOn();
    guestroom.print();

    bedroom = guestroom;

    ((TV*)guestroom["SN123"])->setBrightness(100);
    guestroom.print();

    bedroom.print();
    
    return 0;
}
