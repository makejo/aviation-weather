/* Open-source software distributed under the MIT license.
 * See: https://opensource.org/licenses/MIT 
 */ 

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <Wire.h>
#include "OLED.h" // Must install https://github.com/makejo/esp8266-OLED

// WiFi
const char* WIFI_SSID = "yourwifiname";
const char* WIFI_PWD = "yourwifipassword";

// Type of weather data to retrive. Choose either "METAR" or "TAF".
const String WEATHER_TYPE = "METAR";

// Airport for which weather data will be retrieved. Value must be valid airport ICAO code (example: "KSFO" or "KLAX").
const String ICAO_CODE = "KPAO";

// Web query used to retrieve weather data for specific airport
const String WEB_URL = "https://www.aviationweather.gov/adds/dataserver_current/httpparam?dataSource=" + WEATHER_TYPE + "s&requestType=retrieve&format=xml&stationString=" + ICAO_CODE + "&hoursBeforeNow=6";

// SHA1 fingerprint for aviationweather.gov, expires in October 2021, needs to be updated before this date. Invalid or expired fingerprint will cause HTTPS error.
const uint8_t fingerprint[20] = {0x40, 0x34, 0x2C, 0x79, 0x4B, 0x19, 0x15, 0x22, 0x60, 0x67, 0x4D, 0x4F, 0x5B, 0xDE, 0x60, 0x5E, 0xB5, 0x6D, 0x09, 0x55};

// How often weather data should be refreshed (seconds). Use at least 60 seconds to avoid sending too many queries. METARs are issued every hour, TAF every 6 hours.
const int REFRESH_WEATHER_DELAY = 300;

// When there's a WiFi connection issue, how long do we wait before we try to reconnect (seconds).
const int RECONNECT_WIFI_DELAY = 10;

// OLED display max number of characters per line.
const int DISPLAY_WIDTH = 16;

// OLED display max number of lines used to show the weather.
const int DISPLAY_HEIGHT = 6;

// SDA and SCL are the GPIO pins of ESP8266 that are connected to respective pins of display.
const int SDA_PIN = 0; // See ESP8266 GPIO for matching pinout
const int SCL_PIN = 2; // See ESP8266 GPIO for matching pinout

// Declare OLED display
OLED display(SDA_PIN, SCL_PIN);

// ----------- INITIALIZE ----------- //
void setup() {
  // Initialize display
  display.begin();
  display.print("[ Initializing ]");

  // Initialize serial
  Serial.begin(115200);
  Serial.println();
  Serial.println("[ Initializing ]");
  Serial.flush();

  // Give the board some time to start
  delay(2 * 1000);

  // Make sure board is in station mode and connect WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PWD);
}

// ----------- START ----------- //
void loop() {
  // Make sure WiFi is connected
  while (WiFi.status() == WL_CONNECTED) {

    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setFingerprint(fingerprint);
    HTTPClient https;
    Serial.println("[HTTPS] begin...");

    if (https.begin(*client, WEB_URL)) {
      Serial.println("[HTTPS] GET...");
      // Start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

        // File found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();

          // Trim the XML payload to only keep the raw weather data
          int weatherDataStart = payload.indexOf("raw_text");
          int weatherDataEnd = payload.indexOf("/raw_text");

          // Make sure the payload contains weather data and trim unnecessary characters
          if (weatherDataStart > 0 && weatherDataEnd > 0) {
            payload = payload.substring(weatherDataStart + 9, weatherDataEnd - 1);

            // Wrap text instead of truncating words of the next display lines
            payload = wrapText(payload, DISPLAY_WIDTH);

            // Shorten the payload if it's longer than what the display can show (6 lines with 16 characters per line)
            if (payload.length() > DISPLAY_HEIGHT * DISPLAY_WIDTH) {
              payload = payload.substring(0, DISPLAY_HEIGHT * DISPLAY_WIDTH);
            }

            // Manipulation to convert String to char[] before it can be shown on display
            char weatherType[WEATHER_TYPE.length() + 1];
            strcpy(weatherType, WEATHER_TYPE.c_str());

            char weatherData[payload.length() + 1];
            strcpy(weatherData, payload.c_str());

            // Show weather data on display
            display.clear();
            display.print(weatherType, 0);
            display.print(weatherData, 2);

          } else {
            display.clear();
            display.print("[ No data ]");
            Serial.println("[ No data ]");
          }

        }
      } else {
        Serial.printf("[HTTPS] GET... failed, error: % s\n", https.errorToString(httpCode).c_str());
        display.clear();
        display.print("[ HTTPS Fail ]");
      }

      https.end();
    } else {
      Serial.println("[ HTTPS Error ]");
      display.clear();
      display.print("[ HTTPS Error ]");
    }

    // Wait before we retrieve the next weather data
    delay(REFRESH_WEATHER_DELAY * 1000);
    display.clear();
    display.print("[ REFRESHING ]");
    Serial.println("[ REFRESHING ]");
  }

  display.clear();
  display.print("[ WiFi Error ]");
  Serial.println("[ WiFi Error ]");
  Serial.print("Connection status: ");
  Serial.println(wifiErrorToString(WiFi.status()));
  delay(RECONNECT_WIFI_DELAY * 1000);
}

// ----------- FUNCTIONS ----------- //
/* 
 *  This function wraps words over the next line instead of truncating them.   
 *  It does this by inserting white spaces until the truncated word is pushed to the next line with the rest of the text.   
 *  With this particular display and font, lines start at character 0x16=0, 1x16=16, 2x16=32, 3x16=48, 4x16=64, 5x16=80, 6x16=96, 7x16=112.
 */
String wrapText(String originalText, int lineWidth) {
  String wrappedText = originalText;

  // Wrap each line
  for (int c = 1; c <= (wrappedText.length() / lineWidth); c++) {

    // Find position of the white space character preceding the trucated word as it marks the begining of the word
    int spacePosition = lineWidth * c;
    while (wrappedText[spacePosition] != ' ') {
      spacePosition --;
    }

    // Prepare the right number of white spaces to be inserted to the left of the word
    String whiteSpaces;
    for (int i = 0; i < lineWidth * c - spacePosition; i++) {
      whiteSpaces += " ";
    }

    // Insert white spaces to push the word and the rest of the text to the next line
    wrappedText = wrappedText.substring(0, spacePosition) + whiteSpaces + wrappedText.substring(spacePosition + 1, wrappedText.length());
  }

  return wrappedText;
}

/* 
 *  Decode WiFi error code into a readable error message.
 *  See: https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html#check-return-codes
 */
String wifiErrorToString(int errorCode) {
  String errorMessage;
  
  switch (errorCode) {
    case 0:
      errorMessage = "WL_IDLE_STATUS: WiFi is in process of changing between statuses.";
      break;
    case 1:
      errorMessage = "WL_NO_SSID_AVAIL: Configured SSID cannot be reached.";
      break;
    case 3:
      errorMessage = "WL_CONNECTED: Connection is established.";
      break;
    case 4:
      errorMessage = "WL_CONNECT_FAILED: Connection failed.";
      break;
    case 6:
      errorMessage = "WL_CONNECT_WRONG_PASSWORD: Password is incorrect.";
      break;
    case 7:
      errorMessage = "WL_DISCONNECTED: WiFi module not configured in station mode.";
      break;
    default:
      errorMessage = "Unknown error code.";
      break;
  }

  return errorMessage;
}
