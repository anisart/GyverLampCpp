#include "Settings.h"
#include "EffectsManager.h"
#include "MyMatrix.h"

#include <EEPROM.h>
#if defined(ESP32)
#include <SPIFFS.h>
#else
#include <FS.h>
#endif
#include <ArduinoJson.h>

namespace {

Settings *instance = nullptr;

size_t eepromSize = 0;

const uint8_t alarmsCount = 7;
bool settingsChanged = false;
uint32_t eepromTimer = 0;
uint32_t eepromSaveInterval = 3000;

const char* settingsFileName = "/settings.json";

} // namespace

Settings *Settings::Instance()
{
    return instance;
}

void Settings::Initialize(const uint8_t eepromInitialization, uint32_t saveInterval)
{
    if (instance) {
        return;
    }

    instance = new Settings(eepromInitialization, saveInterval);
}

void Settings::Process()
{
    if (settingsChanged && (millis() - eepromTimer) > eepromSaveInterval) {
        settingsChanged = false;
        eepromTimer = millis();
        Save();
    }
}

void Settings::SaveLater()
{
    settingsChanged = true;
    eepromTimer = millis();
}

void Settings::Save()
{
    DynamicJsonDocument doc(4096);
    JsonArray effects = doc.createNestedArray("effects");
    for (uint8_t i = 0; i < EffectsManager::Count(); i++) {
        Serial.printf("Write effect %s, speed: %u, scale: %u, brightness: %u\n",
                      EffectsManager::EffectName(i).c_str(),
                      effectsSettings[i].effectSpeed,
                      effectsSettings[i].effectScale,
                      effectsSettings[i].effectBrightness);
        JsonObject effect = effects.createNestedObject();
        effect["name"] = EffectsManager::EffectName(i);
        effect["speed"] = effectsSettings[i].effectSpeed;
        effect["scale"] = effectsSettings[i].effectScale;
        effect["brightness"] = effectsSettings[i].effectBrightness;
    }
    doc["currentEffect"] = currentEffect;
    doc["currentEffectName"] = EffectsManager::EffectName(currentEffect);

    Serial.printf("Effects settings count: %zu\n", effects.size());

    File file = SPIFFS.open(settingsFileName, FILE_WRITE);
    if (!file) {
        Serial.println("Error opening settings file from SPIFFS!");
        return;
    }

    if (serializeJson(doc, file) == 0) {
        Serial.println(F("Failed to write to file"));
    }

    if (file) {
        file.close();
    }
}

String Settings::GetCurrentConfig()
{
    DynamicJsonDocument doc(4096);

    doc["working"] = masterSwitch;
    doc["activeEffect"] = currentEffect;

    JsonArray effects = doc.createNestedArray("effects");

    for (uint8_t index = 0; index < EffectsManager::Count(); index++) {
        const EffectSettings settings = effectsSettings[index];

        JsonObject effect = effects.createNestedObject();
        effect["name"] = EffectsManager::EffectName(index);
        effect["speed"] = settings.effectSpeed;
        effect["scale"] = settings.effectScale;
        effect["brightness"] = settings.effectBrightness;

    }

    JsonArray alarms = doc.createNestedArray("alarms");
//    alarms.add(48.756080);
//    alarms.add(2.302038);

    String output;
    serializeJson(doc, output);

    Serial.print(">> ");
    Serial.println(output);
    return output;
}

void Settings::ApplyConfig(const String &message)
{
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, message);

    const String event = doc["event"];
    if (event == "WORKING") {
        const bool working = doc["data"];

        Serial.printf("working: %s\n", working ? "true" : "false");
        masterSwitch = working;
        if (!masterSwitch) {
            myMatrix->clear();
            myMatrix->show();
        }
    } else if (event == "ACTIVE_EFFECT") {
        const int index = doc["data"];
        EffectsManager::ChangeEffect(static_cast<uint8_t>(index));
    } else if (event == "EFFECTS_CHANGED") {
        const JsonObject effect = doc["data"];
        const int speed = effect["speed"];
        const int scale = effect["scale"];
        const int brightness = effect["brightness"];
        CurrentEffectSettings()->effectSpeed = static_cast<uint8_t>(speed);
        CurrentEffectSettings()->effectScale = static_cast<uint8_t>(scale);
        CurrentEffectSettings()->effectBrightness = static_cast<uint8_t>(brightness);
        myMatrix->setBrightness(brightness);
        SaveLater();
    } else if (event == "ALARMS_CHANGED") {

    }
}

Settings::EffectSettings* Settings::CurrentEffectSettings()
{
    return &effectsSettings[currentEffect];
}

Settings::Settings(const uint8_t eepromInitialization, uint32_t saveInterval)
{
    eepromSaveInterval = saveInterval;

    alarmSettings = new AlarmSettings[alarmsCount]();
    effectsSettings = new EffectSettings[EffectsManager::Count()]();

    bool settingsExists = SPIFFS.exists(settingsFileName);
    Serial.printf("SPIFFS Settings file exists: %s\n", settingsExists ? "true" : "false");
    if (!settingsExists) {
        eepromSize = sizeof(uint8_t) // initializationFlag
                   + sizeof(AlarmSettings) * alarmsCount // alarmSettings
                   + sizeof(uint8_t) // currentEffect
                   + sizeof(EffectSettings) * EffectsManager::Count() // effectsSettings
                   + sizeof(uint8_t); // dawnMode
        Serial.printf("EEPROM size used: %zu\n", eepromSize);

        int address = 0;

    #if defined(ESP32)
        const bool eepromReady =
    #endif
         EEPROM.begin(eepromSize);

    #if defined(ESP32)
        Serial.printf("EEPROM ready: %s\n", eepromReady ? "true" : "false");
    #endif

        initializationFlag = EEPROM.read(address);
        Serial.printf("initialization read: %u, expected: %u\n", initializationFlag, eepromInitialization);
        if (eepromInitialization != initializationFlag) {
            initializationFlag = eepromInitialization;
            Serial.println("Erasing EEPROM");
            Save();
            return;
        }

        address += sizeof(uint8_t);

        for (int i = 0; i < alarmsCount; i++) {
            EEPROM.get(address, alarmSettings[i]);
            address += sizeof(AlarmSettings);
        }

        currentEffect = EEPROM.read(address);
        address += sizeof(uint8_t);

        for (uint8_t i = 0; i < EffectsManager::Count(); i++) {
            EEPROM.get(address, effectsSettings[i]);
            Serial.printf("EEPROM Read effect %s, speed: %u, scale: %u, brightness: %u\n",
                          EffectsManager::EffectName(i).c_str(),
                          effectsSettings[i].effectSpeed,
                          effectsSettings[i].effectScale,
                          effectsSettings[i].effectBrightness);
            address += sizeof(EffectSettings);
            delay(10);
        }

        dawnMode = EEPROM.read(address);
        address += sizeof(uint8_t);

        Save();
    } else {
        File settings = SPIFFS.open(settingsFileName);
        if (!settings) {
            Serial.println("SPIFFS Error reading settings file");
            return;
        }

        StaticJsonDocument<4096> doc;
        DeserializationError err = deserializeJson(doc, settings);
        if (err) {
            Serial.print("SPIFFS Error parsing json file: ");
            Serial.println(err.c_str());
            return;
        }

        JsonObject root = doc.as<JsonObject>();
        if (!root.containsKey("effects")) {
            Serial.println("JSON contains no effects!");
            return;
        }

        if (!root["effects"].is<JsonArray>()) {
            Serial.println("JSON effects is not array!");
            return;
        }

        JsonArray effects = root["effects"];
        for (JsonVariant effectVariant : effects) {
            if (!effectVariant.is<JsonObject>()) {
                Serial.println("JSON effect is not object!");
                continue;
            }
            JsonObject effect = effectVariant.as<JsonObject>();
            String effectName = effect["name"];
            uint8_t effectSpeed = effect["speed"];
            uint8_t effectScale = effect["scale"];
            uint8_t effectBrightness = effect["brightness"];
            Serial.printf("SPIFFS Read effect %s, speed: %u, scale: %u, brightness: %u\n",
                          effectName.c_str(),
                          effectSpeed,
                          effectScale,
                          effectBrightness);
        }

        for (uint8_t i = 0; i < EffectsManager::Count(); i++) {
            if (i >= effects.size()) {
                continue;
            }

            JsonObject effect = effects[i];
            String effectName = effect["name"];
            uint8_t effectSpeed = effect["speed"];
            uint8_t effectScale = effect["scale"];
            uint8_t effectBrightness = effect["brightness"];
            Serial.printf("SPIFFS Read effect %s, speed: %u, scale: %u, brightness: %u\n",
                          effectName.c_str(),
                          effectSpeed,
                          effectScale,
                          effectBrightness);

            effectsSettings[i].effectSpeed = effectSpeed;
            effectsSettings[i].effectScale = effectScale;
            effectsSettings[i].effectBrightness = effectBrightness;
        }

        if (root.containsKey("currentEffect")) {
            currentEffect = root["currentEffect"];
        }
    }
}
