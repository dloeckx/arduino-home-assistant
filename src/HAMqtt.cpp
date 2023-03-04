#include "HAMqtt.h"

#ifndef ARDUINOHA_TEST
#include <MQTT.h>
#endif

#include "HADevice.h"
#include "device-types/HABaseDeviceType.h"
#include "mocks/PubSubClientMock.h"

#define HAMQTT_INIT \
    _device(device), \
    _messageCallback(nullptr), \
    _connectedCallback(nullptr), \
    _initialized(false), \
    _discoveryPrefix(DefaultDiscoveryPrefix), \
    _dataPrefix(DefaultDataPrefix), \
    _username(nullptr), \
    _password(nullptr), \
    _lastConnectionAttemptAt(0), \
    _devicesTypesNb(0), \
    _maxDevicesTypesNb(maxDevicesTypesNb), \
    _devicesTypes(new HABaseDeviceType*[maxDevicesTypesNb]), \
    _lastWillTopic(nullptr), \
    _lastWillMessage(nullptr), \
    _lastWillRetain(false), \
    _payloadPtr(NULL), \
    _payloadStart(NULL), \
    _payloadRemaining(0), \
    _client(&netClient), \
    _qos(qos)

static const char* DefaultDiscoveryPrefix = "homeassistant";
static const char* DefaultDataPrefix = "aha";

HAMqtt* HAMqtt::_instance = nullptr;

void onMessageReceived(MQTTClient *client, char* topic, char* payload, int length)
{
    if (HAMqtt::instance() == nullptr || length > UINT16_MAX) {
        return;
    }

    HAMqtt::instance()->processMessage(topic, (const uint8_t*)payload, static_cast<uint16_t>(length));
}

#ifdef ARDUINOHA_TEST
HAMqtt::HAMqtt(
    PubSubClientMock* pubSub,
    HADevice& device,
    uint8_t maxDevicesTypesNb
) :
    _mqtt(pubSub),
    HAMQTT_INIT
{
    _instance = this;
}
#else
HAMqtt::HAMqtt(
    Client& netClient,
    HADevice& device,
    uint8_t maxDevicesTypesNb,
    int qos
) :
    _mqtt(new MQTTClient(512)),
    HAMQTT_INIT
{
    _instance = this;
}
#endif

HAMqtt::~HAMqtt()
{
    delete[] _devicesTypes;

    if (_mqtt) {
        delete _mqtt;
    }

    _instance = nullptr;
}

bool HAMqtt::begin(
    const IPAddress serverIp,
    const uint16_t serverPort,
    const char* username,
    const char* password
)
{
    ARDUINOHA_DEBUG_PRINT(F("AHA: init server "))
    ARDUINOHA_DEBUG_PRINT(serverIp)
    ARDUINOHA_DEBUG_PRINT(F(":"))
    ARDUINOHA_DEBUG_PRINTLN(serverPort)

    if (_device.getUniqueId() == nullptr) {
        ARDUINOHA_DEBUG_PRINTLN(F("AHA: init failed. Missing device unique ID"))
        return false;
    }

    if (_initialized) {
        ARDUINOHA_DEBUG_PRINTLN(F("AHA: already initialized"))
        return false;
    }

    _username = username;
    _password = password;
    _initialized = true;

    _mqtt->begin(serverIp, serverPort, *_client);
    _mqtt->onMessageAdvanced(onMessageReceived);

    return true;
}

bool HAMqtt::begin(
    const IPAddress serverIp,
    const char* username,
    const char* password
)
{
    return begin(serverIp, HAMQTT_DEFAULT_PORT, username, password);
}

bool HAMqtt::begin(
    const char* serverHostname,
    const uint16_t serverPort,
    const char* username,
    const char* password
)
{
    ARDUINOHA_DEBUG_PRINT(F("AHA: init server "))
    ARDUINOHA_DEBUG_PRINT(serverHostname)
    ARDUINOHA_DEBUG_PRINT(F(":"))
    ARDUINOHA_DEBUG_PRINTLN(serverPort)

    if (_device.getUniqueId() == nullptr) {
        ARDUINOHA_DEBUG_PRINTLN(F("AHA: init failed. Missing device unique ID"))
        return false;
    }

    if (_initialized) {
        ARDUINOHA_DEBUG_PRINTLN(F("AHA: already initialized"))
        return false;
    }

    _username = username;
    _password = password;
    _initialized = true;

    _mqtt->begin(serverHostname, serverPort, *_client);
    _mqtt->onMessageAdvanced(onMessageReceived);

    return true;
}

bool HAMqtt::begin(
    const char* serverHostname,
    const char* username,
    const char* password
)
{
    return begin(serverHostname, HAMQTT_DEFAULT_PORT, username, password);
}

bool HAMqtt::disconnect()
{
    if (!_initialized) {
        return false;
    }

    ARDUINOHA_DEBUG_PRINTLN(F("AHA: disconnecting"))

    _initialized = false;
    _lastConnectionAttemptAt = 0;
    _mqtt->disconnect();

    return true;
}

void HAMqtt::loop()
{
    if (_initialized && !_mqtt->loop()) {
        connectToServer();
    }
}

bool HAMqtt::isConnected() const
{
    return _mqtt->connected();
}

void HAMqtt::addDeviceType(HABaseDeviceType* deviceType)
{
    if (_devicesTypesNb + 1 >= _maxDevicesTypesNb) {
        return;
    }

    _devicesTypes[_devicesTypesNb++] = deviceType;
}

bool HAMqtt::publish(const char* topic, const char* payload, bool retained)
{
    if (!isConnected()) {
        return false;
    }

    ARDUINOHA_DEBUG_PRINT(F("AHA: publishing "))
    ARDUINOHA_DEBUG_PRINT(topic)
    ARDUINOHA_DEBUG_PRINT(F(", len: "))
    ARDUINOHA_DEBUG_PRINT(strlen(payload))
    ARDUINOHA_DEBUG_PRINT(F(", qos: "))
    ARDUINOHA_DEBUG_PRINTLN(_qos)

    bool success = _mqtt->publish(topic, payload, retained, _qos);

#ifdef ARDUINOHA_DEBUG
    if (!success) {
        ARDUINOHA_DEBUG_PRINT("Publish error: ")
        ARDUINOHA_DEBUG_PRINTLN(_mqtt->lastError())
    }
#endif

    return success;
}

const int HAMqtt::lastError()
{
       return _mqtt->lastError();
}

void HAMqtt::writePayload(const char* data, const uint16_t length)
{
    if (length < _payloadRemaining) {
        strncpy(_payloadPtr, data, length);
        _payloadPtr += length;
        _payloadRemaining -= length;
        *_payloadPtr = char(0);
    }
}

void HAMqtt::writePayload(const uint8_t* data, const uint16_t length)
{
    writePayload((const char *)data, length);
}

void HAMqtt::writePayload(const __FlashStringHelper* src)
{
    const uint16_t length = strlen_P((const char *)src);
    if (length + 1 < _payloadRemaining) {
        strncpy_P(_payloadPtr, (const char *)src, length);
        _payloadPtr += length;
        _payloadRemaining -= length;
        *_payloadPtr = char(0);
    }
}

bool HAMqtt::subscribe(const char* topic)
{
    ARDUINOHA_DEBUG_PRINT(F("AHA: subscribing "))
    ARDUINOHA_DEBUG_PRINTLN(topic)

    return _mqtt->subscribe(topic, _qos);
}

void HAMqtt::processMessage(const char* topic, const uint8_t* payload, uint16_t length)
{
    ARDUINOHA_DEBUG_PRINT(F("AHA: received call "))
    ARDUINOHA_DEBUG_PRINT(topic)
    ARDUINOHA_DEBUG_PRINT(F(", len: "))
    ARDUINOHA_DEBUG_PRINTLN(length)

    if (_messageCallback) {
        _messageCallback(topic, payload, length);
    }

    for (uint8_t i = 0; i < _devicesTypesNb; i++) {
        _devicesTypes[i]->onMqttMessage(topic, payload, length);
    }
}

void HAMqtt::connectToServer()
{
    if (_lastConnectionAttemptAt > 0 &&
            (millis() - _lastConnectionAttemptAt) < ReconnectInterval) {
        return;
    }

    _lastConnectionAttemptAt = millis();

    ARDUINOHA_DEBUG_PRINT(F("AHA: connecting, client ID: "))
    ARDUINOHA_DEBUG_PRINTLN(_device.getUniqueId())

    _mqtt->connect(
        _device.getUniqueId(),
        _username,
        _password
    );

    if (isConnected()) {
        ARDUINOHA_DEBUG_PRINTLN(F("AHA: connected"))
        onConnectedLogic();
    } else {
        ARDUINOHA_DEBUG_PRINTLN(F("AHA: failed to connect"))
    }
}

void HAMqtt::onConnectedLogic()
{
    if (_connectedCallback) {
        _connectedCallback();
    }

    _mqtt->setWill(
        _lastWillTopic,
        _lastWillMessage,
        _lastWillRetain,
        0
    );

    _device.publishAvailability();

    for (uint8_t i = 0; i < _devicesTypesNb; i++) {
        _devicesTypes[i]->onMqttConnected();
    }
}
