#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

typedef struct {
  const char* ORIGIN;
  const int CHANNEL;
  const char* RELAY;
  const char* STATE;
} DEVICE;

#include "config.h"

const char STATE_ON[] = "on";
const char STATE_OFF[] = "off";

const char TPL_TOPIC_GLOBAL[] = "%s/#";
const char TPL_TOPIC_GLOBAL_COMMAND[] = "%s/command";
const char TPL_TOPIC_DEVICE_COMMAND[] = "%s/%s/command";
const char TPL_TOPIC_DEVICE_STATE[] = "%s/%s";

char TOPIC_GLOBAL[100];
char TOPIC_GLOBAL_COMMAND[100];
char TOPIC_DEVICE_COMMANDS[NUM_DEVICES][100];
char TOPIC_DEVICE_STATES[NUM_DEVICES][100];

Ticker deviceTimer[NUM_DEVICES];

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

void setup() {
  Serial.begin(SERIAL_SPEED);
  delay(1000);
  Serial.println("MADqtt started...");

  generateTopics();
  initChannels();
  
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  if (MQTT_BROKER_USER != "" or MQTT_BROKER_PASS != "") {
    mqttClient.setCredentials(MQTT_BROKER_USER, MQTT_BROKER_PASS);
  }

  connectToWifi();
}

void loop() {}

void generateTopics() {
  sprintf(TOPIC_GLOBAL, TPL_TOPIC_GLOBAL, MQTT_TOPIC);
  sprintf(TOPIC_GLOBAL_COMMAND, TPL_TOPIC_GLOBAL_COMMAND, MQTT_TOPIC);
  for (int i=0; i<NUM_DEVICES; i++) {
    sprintf(TOPIC_DEVICE_COMMANDS[i], TPL_TOPIC_DEVICE_COMMAND, MQTT_TOPIC, DEVICES[i].ORIGIN);
    sprintf(TOPIC_DEVICE_STATES[i], TPL_TOPIC_DEVICE_STATE, MQTT_TOPIC, DEVICES[i].ORIGIN);
  }
}

void initChannels() {
  for (int i=0; i<NUM_DEVICES; i++) {
    pinMode(DEVICES[i].CHANNEL, OUTPUT);
  }
}

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(WIFI_HOST);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi");
  connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  Serial.println("Disconnected from Wi-Fi");
  mqttReconnectTimer.detach();
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT");
  mqttClient.subscribe(TOPIC_GLOBAL, MQTT_QOS);
  commandOn(-1);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.print("Disconnected from MQTT, reason: ");
  if (reason == AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT) {
    Serial.println("Bad server fingerprint.");
  } else if (reason == AsyncMqttClientDisconnectReason::TCP_DISCONNECTED) {
    Serial.println("TCP Disconnected.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION) {
    Serial.println("Bad server fingerprint.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED) {
    Serial.println("MQTT Identifier rejected.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE) {
    Serial.println("MQTT server unavailable.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS) {
    Serial.println("MQTT malformed credentials.");
  } else if (reason == AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED) {
    Serial.println("MQTT not authorized.");
  } else if (reason == AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE) {
    Serial.println("Not enough space on esp8266.");
  }

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (strcmp(topic, TOPIC_GLOBAL_COMMAND) == 0) {
    if (strncmp(payload, "update", len) == 0) {
      commandUpdate(-1);
    }
    else if (strncmp(payload, "on", len) == 0) {
      commandOn(-1);
    }
    else if (strncmp(payload, "off", len) == 0) {
      commandOff(-1);
    }
    else if (strncmp(payload, "toggle", len) == 0) {
      commandToggle(-1);
    }
    else if (strncmp(payload, "restart", len) == 0) {
      commandRestart(-1);
    }
  }
  else {
    for (int i=0; i<NUM_DEVICES; i++) {
      if (strcmp(topic, TOPIC_DEVICE_COMMANDS[i]) == 0) {
        if (strncmp(payload, "on", len) == 0) {
          commandOn(i);
        }
        else if (strncmp(payload, "off", len) == 0) {
          commandOff(i);
        }
        else if (strncmp(payload, "toggle", len) == 0) {
          commandToggle(i);
        }
        else if (strncmp(payload, "restart", len) == 0) {
          commandRestart(i);
        }
        break;
      }
    }
  }
}

void commandUpdate(int id) {
  for (int i=0; i<NUM_DEVICES; i++) {
    if (id == -1 || i == id) {
      Serial.print("Update Device ");
      Serial.println(DEVICES[i].ORIGIN);

      mqttClient.publish(TOPIC_DEVICE_STATES[i], MQTT_QOS, true, DEVICES[i].STATE);
    }
  }
}

void commandOn(int id) {
  for (int i=0; i<NUM_DEVICES; i++) {
    if (id == -1 || i == id) {
      Serial.print("Turn on Device ");
      Serial.println(DEVICES[i].ORIGIN);
      
      if (DEVICES[i].RELAY == "NO") {
        digitalWrite(DEVICES[i].CHANNEL, HIGH);
      }
      else {
        digitalWrite(DEVICES[i].CHANNEL, LOW);
      }

      DEVICES[i].STATE = STATE_ON;
    }
  }
  commandUpdate(id);
}

void commandOff(int id) {
  for (int i=0; i<NUM_DEVICES; i++) {
    if (id == -1 || i == id) {
      Serial.print("Turn off Device ");
      Serial.println(DEVICES[i].ORIGIN);
      
      if (DEVICES[i].RELAY == "NO") {
        digitalWrite(DEVICES[i].CHANNEL, LOW);
      }
      else {
        digitalWrite(DEVICES[i].CHANNEL, HIGH);
      }

      DEVICES[i].STATE = STATE_OFF;
    }
  }
  commandUpdate(id);
}

void commandToggle(int id) {
  for (int i=0; i<NUM_DEVICES; i++) {
    if (id == -1 || i == id) {
      Serial.print("Toggle Device ");
      Serial.println(DEVICES[i].ORIGIN);

      if (DEVICES[i].STATE == STATE_ON) {
        commandOff(i);
      } else {
        commandOn(i);
      }
    }
  }
}

void commandRestart(int id) {
  for (int i=0; i<NUM_DEVICES; i++) {
    if (id == -1 || i == id) {
      Serial.print("Restart Device ");
      Serial.println(DEVICES[i].ORIGIN);

      commandOff(i);
      deviceTimer[i].once(5, commandOn, i);
    }
  }
}
