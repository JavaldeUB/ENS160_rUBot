#include <Arduino.h>
#include <Wire.h>
#include <ScioSense_ENS16x.h>

// --- Configuration ---
#define I2C_ADDRESS 0x52    // I2C address of the ENS16x sensor
#define TCA_ADDR 0x70       // I2C address of the TCA9548A multiplexer
#define EN_PIN A6           // Enable pin for the sensor system
#define BAUD_USB 9600       // Baud rate for USB serial communication
const int SENSOR_COUNT = 6; // Total number of connected ENS16x sensors
const int tcaChannels[SENSOR_COUNT] = {0, 1, 2, 3, 4, 5}; // Physical multiplexer channels in use

// --- Global Variables ---
ENS160 mySensors[SENSOR_COUNT];             // Array of ENS160 objects, one for each sensor
bool sensorInitialized[SENSOR_COUNT] = { false }; // Initialization status of each sensor
uint8_t channelMask = 0;                    // Bitmask to indicate which channels (sensors) are active for reading

bool streaming = false;                     // Flag to control if data reading is in streaming mode
uint32_t sampleInterval = 2000;             // Time interval in ms between readings in streaming mode
uint32_t lastSampleTime = 0;                // Timestamp of the last reading in streaming mode

// --- Function Prototypes ---
void handleCommand(const String &cmd);
void showHelp();
void showChannels();
void selectTCAChannel(uint8_t channel);
void setChannelsFromList(const String &list);
void setSensorsMode(const String &modeStr, const String &channelsStr);
void initializeAllSensors();
void readAllChannels();
void sampleAllChannels();
void sendCurrentStatus();
void softResetSensor(uint8_t channel);
void viewMode();

void getHotplateResistances(int channel, uint16_t* resistances) {
  const uint8_t hp_l_registers[] = {0x32, 0x34, 0x36, 0x38};

  selectTCAChannel(tcaChannels[channel]); // Use channel mapping

  for (int i = 0; i < 4; i++) {
    uint16_t resistance = 0;
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(hp_l_registers[i]);
    uint8_t status = Wire.endTransmission(false);

    if (status == 0) {
      Wire.requestFrom(I2C_ADDRESS, 2);

      if (Wire.available() == 2) {
        uint8_t lsb = Wire.read();
        uint8_t msb = Wire.read();
        resistance = (msb << 8) | lsb;
      } else {
        resistance = 0;
      }
    } else {
      resistance = 0;
    }
    resistances[i] = resistance;
  }
}

void selectTCAChannel(uint8_t channel) {
    if (channel > 7) return;
    Wire.beginTransmission(TCA_ADDR);
    Wire.write(1 << channel);
    if (Wire.endTransmission() != 0) {
        Serial.print("Error switching to channel ");
        Serial.println(channel);
    }
    delay(10);
}

void setup() {
    Serial.begin(BAUD_USB);
    while (!Serial);

    pinMode(EN_PIN, OUTPUT);
    digitalWrite(EN_PIN, HIGH);
    delay(1000);

    Wire.begin();

    Serial.println("Initializing all sensors...");
    initializeAllSensors();

    uint8_t initialMask = 0;
    for(int i=0; i<SENSOR_COUNT; i++) {
        if(sensorInitialized[i]) {
            initialMask |= (1 << i);
        }
    }
    channelMask = initialMask;

    // ---- Activar streaming automáticamente ----
    streaming = true;           // streaming activado
    sampleInterval = 1000;      // intervalo 1000 ms
    lastSampleTime = millis();

    Serial.print("Auto STREAM ON, interval ");
    Serial.print(sampleInterval);
    Serial.println(" ms");

    Serial.println("\n>> Sensors initialized. Type HELP for commands.\n");
}

void loop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        cmd.toUpperCase();

        while (Serial.available()) {
            Serial.read();
        }

        if (cmd.length() > 0) {
            handleCommand(cmd);
        }
    }

    if (streaming && (millis() - lastSampleTime >= sampleInterval)) {
        lastSampleTime = millis();
        sampleAllChannels();
    }
}

void handleCommand(const String &cmd) {
    streaming = false;
    Serial.println("---");

    if (cmd == "HELP") {
        showHelp();
    } else if (cmd == "STATUS") {
        sendCurrentStatus();
    } else if (cmd == "RESET") {
        Serial.println("Restarting...");
        NVIC_SystemReset();
    } else if (cmd.startsWith("STREAM_START")) {
        String param = cmd.substring(12);
        if (param.toInt() > 0) sampleInterval = param.toInt();
        streaming = true;
        lastSampleTime = millis();
        Serial.print("STREAM ON, interval "); Serial.print(sampleInterval); Serial.println(" ms");
    } else if (cmd == "STREAM_STOP") {
        Serial.println("STREAM OFF");
    } else if (cmd.startsWith("SET_CHANNELS")) {
        String params = cmd.substring(12);
        params.trim();
        setChannelsFromList(params);
        Serial.println("Reading channels configured.");
    } else if (cmd.startsWith("SET_MODE")) {
        String params = cmd.substring(8);
        params.trim();
        int spaceIndex = params.indexOf(' ');
        if (spaceIndex > 0) {
            String modeStr = params.substring(0, spaceIndex);
            String channelsStr = params.substring(spaceIndex + 1);
            setSensorsMode(modeStr, channelsStr);
        } else {
            Serial.println("Error: Incorrect format. Usage: SET_MODE <MODE> <channels|ALL>\n");
        }
    } else if (cmd == "VIEW_MODE") {
        viewMode();
    } else if (cmd == "CLEAR_CHANNELS") {
        channelMask = 0;
        Serial.println("All reading channels deactivated.\n");
    } else if (cmd == "ALL_CHANNELS") {
        channelMask = 0;
        for(int i=0; i<SENSOR_COUNT; i++) {
            if(sensorInitialized[i]) channelMask |= (1 << i);
        }
        Serial.println("All reading channels activated.\n");
    } else if (cmd == "INITIALIZE") {
        initializeAllSensors();
        Serial.println("Reinitialization complete.");
    } else if (cmd.startsWith("READ_ALL")) {
        readAllChannels();
    } else if (cmd.startsWith("SAMPLE")) {
        sampleAllChannels();
    } else if (cmd == "SHOW_CHANNELS") {
        showChannels();
    } else if (cmd == "SHOW_HELP") {
        showHelp();
    } else if (cmd.startsWith("SOFT_RESET")) {
        String param = cmd.substring(10);
        param.trim();
        int channelToReset = param.toInt();

        if (channelToReset >= 0 && channelToReset < SENSOR_COUNT) {
            softResetSensor(channelToReset);
        } else {
            Serial.println("Error: Invalid channel for SOFT_RESET. Usage: SOFT_RESET <channel_number>\n");
        }
    } else {
        Serial.print("Command NOT recognized: ");
        Serial.println(cmd);
    }
    Serial.println("---");

    if (cmd != "STATUS" && cmd != "RESET" && cmd != "READ_ALL" && !cmd.startsWith("SAMPLE")) {
        sendCurrentStatus();
    }
}


void showHelp() {
    Serial.println(F(
        "Available Commands:\n"
        "GENERAL:\n"
        "\n"
        "     HELP\n"
        "     STATUS\n"
        "     RESET\n"
        "     \n"
        "SENSOR MODE (ENS160):\n"
        "\n"
        "     SET_MODE <MODE> <0,1,2..|ALL> (MODE: STANDARD, IDLE, SLEEP)\n"
        "     SOFT_RESET <0,1,2...>\n" 
        "     \n"
        "READING / STREAMING:\n"
        "\n"
        "     READ_ALL\n"
        "     STREAM_START [ms]\n"
        "     STREAM_STOP"
    ));
}

void showChannels() {
    Serial.print("Active channels for reading:\n");
    for (int i = 0; i < SENSOR_COUNT; i++) {
        Serial.print(" (CH"); Serial.print(tcaChannels[i]); Serial.print("): ");
        Serial.println((channelMask & (1 << i)) ? "ON" : "OFF");
    }
}

void setChannelsFromList(const String &list) {
    uint8_t newMask = 0;

    if (list == "ALL") {
        for(int i=0; i<SENSOR_COUNT; i++) {
            if(sensorInitialized[i]) newMask |= (1 << i);
        }
    } else {
        char tempStr[list.length() + 1];
        list.toCharArray(tempStr, sizeof(tempStr));

        char* token = strtok(tempStr, ",");
        while (token != NULL) {
            String chStr = String(token);
            chStr.trim();
            if (chStr.length() > 0) {
                int ch = chStr.toInt();
                if (ch >= 0 && ch < SENSOR_COUNT && sensorInitialized[ch]) {
                    newMask |= (1 << ch);
                }
            }
            token = strtok(NULL, ",");
        }
    }
    channelMask = newMask;
}

void setSensorsMode(const String &modeStr, const String &channelsStr) {
    uint8_t modeByte;

    if (modeStr == "STANDARD") {
        modeByte = 0x02;
    } else if (modeStr == "IDLE") {
        modeByte = 0x01;
    } else if (modeStr == "SLEEP") {
        modeByte = 0x00;
    } else {
        Serial.println("Error: Invalid mode. Use STANDARD, IDLE, or SLEEP.\n");
        return;
    }

    Serial.print("Writing byte 0x"); Serial.print(modeByte, HEX);
    Serial.print(" ("); Serial.print(modeStr); Serial.print(")");
    Serial.print(" to channels: "); Serial.println(channelsStr);

    if (channelsStr == "ALL") {
        for (int i = 0; i < SENSOR_COUNT; i++) {
            if (sensorInitialized[i]) {
                selectTCAChannel(tcaChannels[i]); // Use channel mapping
                Wire.beginTransmission(I2C_ADDRESS);
                Wire.write(0x10);
                Wire.write(modeByte);
                if (Wire.endTransmission() == 0) {
                    Serial.print("     CH"); Serial.print(tcaChannels[i]); Serial.println(" -> OK");
                } else {
                    Serial.print("     CH"); Serial.print(tcaChannels[i]); Serial.println(" -> FAIL");
                }
            }
        }
    } else {
        int idx = 0;
        while (idx < channelsStr.length()) {
            int comma = channelsStr.indexOf(',', idx);
            if (comma < 0) comma = channelsStr.length();
            uint8_t ch = channelsStr.substring(idx, comma).toInt();
            if (ch < SENSOR_COUNT && sensorInitialized[ch]) {
                selectTCAChannel(tcaChannels[ch]); // Use channel mapping
                Wire.beginTransmission(I2C_ADDRESS);
                Wire.write(0x10);
                Wire.write(modeByte);
                if (Wire.endTransmission() == 0) {
                    Serial.print("     CH"); Serial.print(tcaChannels[ch]); Serial.println(" -> OK");
                } else {
                    Serial.print("     CH"); Serial.print(tcaChannels[ch]); Serial.println(" -> FAIL");
                }
            }
            idx = comma + 1;
        }
    }
    Serial.println("Mode command completed.");
}

void initializeAllSensors() {
    for (int i = 0; i < SENSOR_COUNT; i++) {
        selectTCAChannel(tcaChannels[i]); // Use channel mapping
        Serial.print("Init CH"); Serial.print(tcaChannels[i]);

        mySensors[i].begin(&Wire, I2C_ADDRESS); 
        if (mySensors[i].init()) {
            sensorInitialized[i] = true;
            Wire.beginTransmission(I2C_ADDRESS);
            Wire.write(0x10);
            Wire.write(0x02);
            Wire.endTransmission();
            Serial.println(" -> OK (STANDARD Mode)");
        } else {
            sensorInitialized[i] = false;
            Serial.println(" -> FAIL");
        }
        delay(50);
    }
}

void readAllChannels() {
    Serial.println("Starting reading of all active channels (with resistances)...");
    bool anyPrinted = false;
    uint16_t resistances[4];

    for (int ch = 0; ch < SENSOR_COUNT; ch++) {
        if ((channelMask & (1 << ch))) {
            if (!sensorInitialized[ch]) {
                Serial.print("Channel "); Serial.print(ch); Serial.println(" inactive (not initialized).");
                continue;
            }

            selectTCAChannel(tcaChannels[ch]); // Use channel mapping
            if (mySensors[ch].update() == RESULT_OK && mySensors[ch].hasNewData()) {
                anyPrinted = true;
                Serial.print(millis()); Serial.print(",CH"); Serial.print(tcaChannels[ch]);
                Serial.print(",eCO2="); Serial.print(mySensors[ch].getEco2());
                Serial.print(",TVOC="); Serial.print(mySensors[ch].getTvoc());
                Serial.print(",AQI=");  Serial.print(mySensors[ch].getAirQualityIndex_UBA());

                //getHotplateResistances(ch, &resistances);
                Serial.print(",R0="); Serial.print(mySensors[ch].getRs0());
                Serial.print(",R1="); Serial.print(mySensors[ch].getRs1());
                Serial.print(",R2="); Serial.print(mySensors[ch].getRs2());
                Serial.print(",R3="); Serial.println(mySensors[ch].getRs3());
            } else {
                Serial.print("     CH"); Serial.print(tcaChannels[ch]); Serial.println(": Could not read new data (may be in SLEEP/IDLE mode).");
            }
        }
    }
    if (!anyPrinted) {
        Serial.println("No data read from any channel.\n");
    }
}

void sampleAllChannels() {
    uint16_t resistances[4];

    for (int ch = 0; ch < SENSOR_COUNT; ch++) {
        if ((channelMask & (1 << ch)) && sensorInitialized[ch]) {
            selectTCAChannel(tcaChannels[ch]); // Use channel mapping
            if (mySensors[ch].update() == RESULT_OK && mySensors[ch].hasNewData()) {
                Serial.print(millis()); Serial.print(",CH"); Serial.print(tcaChannels[ch]);
                Serial.print(",eCO2="); Serial.print(mySensors[ch].getEco2());
                Serial.print(",TVOC="); Serial.print(mySensors[ch].getTvoc());
                Serial.print(",AQI=");  Serial.print(mySensors[ch].getAirQualityIndex_UBA());

                //getHotplateResistances(ch, &resistances);
                Serial.print(",R0="); Serial.print(mySensors[ch].getRs0());
                Serial.print(",R1="); Serial.print(mySensors[ch].getRs1());
                Serial.print(",R2="); Serial.print(mySensors[ch].getRs2());
                Serial.print(",R3="); Serial.println(mySensors[ch].getRs3());
            }
        }
    }
}

void sendCurrentStatus() {
    uint8_t initMask = 0;
    for (int i = 0; i < SENSOR_COUNT; i++)
        if (sensorInitialized[i]) initMask |= (1 << i);

    Serial.print("STATUS: streaming="); Serial.print(streaming ? "ON" : "OFF");
    Serial.print(", interval="); Serial.print(sampleInterval); Serial.print(" ms");
    Serial.print(", inited_mask=0b"); Serial.println(channelMask, BIN);
    showChannels();
}

void viewMode() {
    Serial.println("Current sensor modes:");
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (sensorInitialized[i]) {
            selectTCAChannel(tcaChannels[i]); // Use channel mapping
            Wire.beginTransmission(I2C_ADDRESS);
            Wire.write(0x10);
            Wire.endTransmission(false);
            Wire.requestFrom(I2C_ADDRESS, 1);
            if (Wire.available()) {
                uint8_t mode = Wire.read();
                Serial.print("CH"); Serial.print(tcaChannels[i]); Serial.print(": ");
                if (mode == 0x02) {
                    Serial.println("STANDARD");
                } else if (mode == 0x01) {
                    Serial.println("IDLE");
                } else if (mode == 0x00) {
                    Serial.println("SLEEP");
                } else {
                    Serial.println("UNKNOWN");
                }
            } else {
                Serial.print("CH"); Serial.print(tcaChannels[i]); Serial.println(": Error reading mode.");
            }
        } else {
            Serial.print("Channel "); Serial.print(i); Serial.println(": Not initialized.");
        }
    }
}

void softResetSensor(uint8_t channel) {
    if (channel >= SENSOR_COUNT) {
        Serial.print("Error: Channel "); Serial.print(channel); Serial.println(" out of range for SOFT_RESET.");
        return;
    }
    if (!sensorInitialized[channel]) {
        Serial.print("Error: Sensor at channel "); Serial.print(channel); Serial.println(" not initialized. Cannot reset.");
        return;
    }

    Serial.print("Performing SOFT_RESET on CH"); Serial.print(tcaChannels[channel]); Serial.print("... ");
    selectTCAChannel(tcaChannels[channel]); // Use channel mapping

    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(0x10);
    Wire.write(0x00);
    if (Wire.endTransmission() == 0) {
        Serial.println("Switched to DEEP SLEEP mode.");
        delay(100); 
    } else {
        Serial.println("Error switching to DEEP SLEEP (I2C error).");
        return; 
    }

    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(0x10); 
    Wire.write(0x01);
    if (Wire.endTransmission() == 0) {
        Serial.println("Switched to IDLE mode.");
        delay(100); 
    } else {
        Serial.println("Error switching to IDLE (I2C error).");
        return; 
    }

    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(0x10); 
    Wire.write(0x02); 
    if (Wire.endTransmission() == 0) {
        Serial.println("Switched to STANDARD mode. SOFT_RESET complete.");
        delay(500);
    } else {
        Serial.println("Error switching to STANDARD Mode (I2C error). SOFT_RESET failed.");
        return; 
    }

    selectTCAChannel(tcaChannels[channel]); 
    if (mySensors[channel].init()) {
        Serial.println("CH" + String(tcaChannels[channel]) + " re-initialized correctly after SOFT_RESET.");
    } else {
        Serial.println("ERROR: ENS160 re-initialization failed for CH" + String(tcaChannels[channel]) + " after SOFT_RESET.");
    }
}