// Final, complete sketch with all sensors for Home Assistant MQTT Discovery.
// This version combines the working APIs from all official examples provided.

#include "Arduino.h"
#include "AZ3166WiFi.h"
#include "IoT_DevKit_HW.h"      // For Sensors and Screen
#include "MQTTClient.h"         // For generic MQTT
#include "MQTTNetwork.h"        // For MQTT networking

// =================== CONFIGURATION ===================
// --- Replace with your Wi-Fi details (needed for the direct connection method) ---
char ssid[] = "<SSID>";
char pass[] = "<password>";

// --- Replace with the IP address of your Home Assistant device running the MQTT broker ---
static const char* mqtt_broker_host = "192.168.x.x";
const int mqtt_port = 1883;
// ================= END CONFIGURATION =================

// Create objects for the MQTT connection
MQTTNetwork mqttNetwork;
// Create the MQTT client object and increase the buffer size to 1024 bytes
MQTT::Client<MQTTNetwork, Countdown, 1024> mqttClient = MQTT::Client<MQTTNetwork, Countdown, 1024>(mqttNetwork);

// Forward declarations
void setup_ha_mqtt_discovery();
void publish_telemetry();

void setup() {
    Serial.begin(115200);

    // Initialize the hardware (sensors, screen, etc.) WITHOUT connecting to Wi-Fi
    initIoTDevKit(0); 
    
    // --- Manual Wi-Fi Connection (from ConnectWithWPA example) ---
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    textOutDevKitScreen(0, "WiFi...", 1);

    if (WiFi.begin(ssid, pass) != WL_CONNECTED) {
        Serial.println("Couldn't get a wifi connection");
        textOutDevKitScreen(0, "WiFi Fail", 1);
        while(true); // Stop here
    }

    Serial.println("WiFi connected");
    IPAddress ip = WiFi.localIP();
    Serial.printf("IP Address: %s\r\n", ip.get_address());
    delay(2000);

    // --- Manual MQTT Connection (from MQTTClient example) ---
    Serial.printf("Connecting to MQTT broker at %s...\r\n", mqtt_broker_host);
    textOutDevKitScreen(0, "MQTT...", 1);
    if (mqttNetwork.connect(mqtt_broker_host, mqtt_port) != 0) {
        Serial.println("MQTT TCP connect failed.");
        textOutDevKitScreen(0, "MQTT TCP Fail", 1);
        return;
    }

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = (char*)"AZ3166Client";
    
    if (mqttClient.connect(data) != 0) {
        Serial.println("MQTT connect failed.");
        textOutDevKitScreen(0, "MQTT Conn Fail", 1);
        return;
    }
    
    Serial.println("MQTT connected successfully.");
    textOutDevKitScreen(0, "MQTT OK", 1);
    setup_ha_mqtt_discovery();
}

void loop() {
    if (!mqttClient.isConnected()) {
        Serial.println("MQTT disconnected. Attempting reconnect...");
        mqttNetwork.disconnect();
        if (mqttNetwork.connect(mqtt_broker_host, mqtt_port) == 0) {
            MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
            data.MQTTVersion = 3;
            data.clientID.cstring = (char*)"AZ3166Client";
            if (mqttClient.connect(data) == 0) {
                Serial.println("MQTT Reconnected.");
                setup_ha_mqtt_discovery();
            }
        }
        delay(5000); // Wait 5 seconds before retrying
        return;
    }
    
    static unsigned long last_telemetry_sent_ms = 0;
    if (millis() - last_telemetry_sent_ms > 30000) {
        publish_telemetry();
        last_telemetry_sent_ms = millis();
    }
    
    mqttClient.yield(100);
}

void publish_telemetry() {
    if (!mqttClient.isConnected()) {
      return;
    }
    
    char telemetry_payload[512];
    int accel_x, accel_y, accel_z;
    int gyro_x, gyro_y, gyro_z;
    int mag_x, mag_y, mag_z;

    // Use the correct, global functions to get all sensor data
    getDevKitAcceleratorValue(&accel_x, &accel_y, &accel_z);
    getDevKitGyroscopeValue(&gyro_x, &gyro_y, &gyro_z);
    getDevKitMagnetometerValue(&mag_x, &mag_y, &mag_z);

    // Create the full JSON payload
    snprintf(telemetry_payload, sizeof(telemetry_payload),
             "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,\"accelerometer_x\":%d,\"accelerometer_y\":%d,\"accelerometer_z\":%d,\"gyroscope_x\":%d,\"gyroscope_y\":%d,\"gyroscope_z\":%d,\"magnetometer_x\":%d,\"magnetometer_y\":%d,\"magnetometer_z\":%d}",
             getDevKitTemperatureValue(0), getDevKitHumidityValue(), getDevKitPressureValue(),
             accel_x, accel_y, accel_z,
             gyro_x, gyro_y, gyro_z,
             mag_x, mag_y, mag_z);

    Serial.print("Publishing message: ");
    Serial.println(telemetry_payload);

    // Publish the payload to the shared state topic
    MQTT::Message message;
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.payload = (void*)telemetry_payload;
    message.payloadlen = strlen(telemetry_payload);
    
    mqttClient.publish("homeassistant/sensor/az3166/state", message);
}

void setup_ha_mqtt_discovery() {
    if (!mqttClient.isConnected()) {
      return;
    }
    Serial.println("Sending Home Assistant discovery messages for all sensors...");
  
    char discovery_topic[255];
    char discovery_payload[1024];
    const char* device_json = "\"device\":{\"identifiers\":[\"az3166\"],\"name\":\"MXChip AZ3166\",\"model\":\"AZ3166\",\"manufacturer\":\"Microsoft\"}";

    MQTT::Message message;
    message.qos = MQTT::QOS0;
    message.retained = true; // Discovery messages must be retained

    // Temperature
    snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/sensor/az3166/temperature/config");
    snprintf(discovery_payload, sizeof(discovery_payload), "{\"name\":\"AZ3166 Temperature\",\"stat_t\":\"homeassistant/sensor/az3166/state\",\"unit_of_meas\":\"°C\",\"val_tpl\":\"{{ value_json.temperature }}\",\"uniq_id\":\"az3166_temp\",%s}", device_json);
    message.payload = (void*)discovery_payload;
    message.payloadlen = strlen(discovery_payload);
    mqttClient.publish(discovery_topic, message);
    
    // Humidity
    snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/sensor/az3166/humidity/config");
    snprintf(discovery_payload, sizeof(discovery_payload), "{\"name\":\"AZ3166 Humidity\",\"stat_t\":\"homeassistant/sensor/az3166/state\",\"unit_of_meas\":\"%%\",\"val_tpl\":\"{{ value_json.humidity }}\",\"uniq_id\":\"az3166_humidity\",%s}", device_json);
    message.payload = (void*)discovery_payload;
    message.payloadlen = strlen(discovery_payload);
    mqttClient.publish(discovery_topic, message);

    // Pressure
    snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/sensor/az3166/pressure/config");
    snprintf(discovery_payload, sizeof(discovery_payload), "{\"name\":\"AZ3166 Pressure\",\"stat_t\":\"homeassistant/sensor/az3166/state\",\"unit_of_meas\":\"hPa\",\"val_tpl\":\"{{ value_json.pressure }}\",\"uniq_id\":\"az3166_pressure\",%s}", device_json);
    message.payload = (void*)discovery_payload;
    message.payloadlen = strlen(discovery_payload);
    mqttClient.publish(discovery_topic, message);

    // Accelerometer (X, Y, Z)
    const char* accel_axes[] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) {
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/sensor/az3166/accelerometer_%s/config", accel_axes[i]);
        snprintf(discovery_payload, sizeof(discovery_payload), "{\"name\":\"AZ3166 Accelerometer %s\",\"stat_t\":\"homeassistant/sensor/az3166/state\",\"unit_of_meas\":\"mg\",\"val_tpl\":\"{{ value_json.accelerometer_%s }}\",\"uniq_id\":\"az3166_accel_%s\",%s}", accel_axes[i], accel_axes[i], accel_axes[i], device_json);
        message.payload = (void*)discovery_payload;
        message.payloadlen = strlen(discovery_payload);
        mqttClient.publish(discovery_topic, message);
    }
  
    // Gyroscope (X, Y, Z)
    const char* gyro_axes[] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) {
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/sensor/az3166/gyroscope_%s/config", gyro_axes[i]);
        snprintf(discovery_payload, sizeof(discovery_payload), "{\"name\":\"AZ3166 Gyroscope %s\",\"stat_t\":\"homeassistant/sensor/az3166/state\",\"unit_of_meas\":\"dps\",\"val_tpl\":\"{{ value_json.gyroscope_%s }}\",\"uniq_id\":\"az3166_gyro_%s\",%s}", gyro_axes[i], gyro_axes[i], gyro_axes[i], device_json);
        message.payload = (void*)discovery_payload;
        message.payloadlen = strlen(discovery_payload);
        mqttClient.publish(discovery_topic, message);
    }

    // Magnetometer (X, Y, Z)
    const char* mag_axes[] = {"x", "y", "z"};
    for (int i = 0; i < 3; i++) {
        snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/sensor/az3166/magnetometer_%s/config", mag_axes[i]);
        snprintf(discovery_payload, sizeof(discovery_payload), "{\"name\":\"AZ3166 Magnetometer %s\",\"stat_t\":\"homeassistant/sensor/az3166/state\",\"unit_of_meas\":\"mgauss\",\"val_tpl\":\"{{ value_json.magnetometer_%s }}\",\"uniq_id\":\"az3166_mag_%s\",%s}", mag_axes[i], mag_axes[i], mag_axes[i], device_json);
        message.payload = (void*)discovery_payload;
        message.payloadlen = strlen(discovery_payload);
        mqttClient.publish(discovery_topic, message);
    }
}
