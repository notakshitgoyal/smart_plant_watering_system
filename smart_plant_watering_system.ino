// Include necessary libraries
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP085.h>
#include <UARTClass.h>

// Initialize Wire and SPI for Aries board
TwoWire Wire(1);  // Use I2C-1
SPIClass SPI(0);  // Use SPI-0

// Debug flag
#define DEBUG 1

// Pin Definitions for Aries Board
#define TOUCH_PIN       0    // Touch sensor pin
#define RELAY_PIN       1    // Relay control pin
#define BUZZER_PIN      2    // Buzzer pin
#define LED_MATRIX_DIN  11   // LED Matrix Data In (MOSI)
#define LED_MATRIX_CLK  13   // LED Matrix Clock (SCK)
#define LED_MATRIX_CS   10   // LED Matrix Chip Select
#define LDR_PIN        A0    // Light sensor pin

// Buzzer patterns
#define BUZZ_SHORT      100   // 100ms
#define BUZZ_MEDIUM     500   // 500ms
#define BUZZ_LONG      1000   // 1 second

// Safety timeout for water
#define MAX_WATER_TIME 300000  // 5 minutes in milliseconds

// I2C Addresses and Hardware Configuration
#define OLED_ADDRESS    0x3C  // OLED display I2C address
#define SCREEN_WIDTH    128   // OLED display width
#define SCREEN_HEIGHT   64    // OLED display height
#define OLED_RESET     -1    // Reset pin (or -1 if sharing Arduino reset pin)

// LED Matrix Configuration
#define HARDWARE_TYPE   MD_MAX72XX::FC16_HW
#define MAX_DEVICES     1     // Number of LED matrix modules
#define MAX_MOISTURE_LEVEL 8  // Maximum moisture level
#define MATRIX_INTENSITY 8    // LED Matrix brightness (0-15)

// Initialize objects
MD_MAX72XX matrix(HARDWARE_TYPE, LED_MATRIX_DIN, LED_MATRIX_CLK, LED_MATRIX_CS, MAX_DEVICES);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BMP085 tempSensor;
UARTClass bluetooth(1);

// Global variables
int currentMoistureLevel = 0;
bool isWaterOn = false;
unsigned long lastTouchCheck = 0;
unsigned long lastSensorUpdate = 0;
unsigned long lastMatrixUpdate = 0;
unsigned long lastDebugOutput = 0;
unsigned long waterStartTime = 0;
unsigned long buzzerStartTime = 0;
bool buzzerActive = false;


// Helper function to convert float to string
String floatToString(float value, int precision) {
    char buffer[10];
    int intPart = (int)value;
    int fracPart = abs((int)((value - intPart) * pow(10, precision)));
    sprintf(buffer, "%d.%d", intPart, fracPart);
    return String(buffer);
}

// Buzzer pattern function
void buzzerPattern(uint8_t pattern) {
    switch(pattern) {
        case 1: // Touch detected
            digitalWrite(BUZZER_PIN, HIGH);
            delay(BUZZ_SHORT);
            digitalWrite(BUZZER_PIN, LOW);
            break;
            
        case 2: // Water ON
            for(int i = 0; i < 2; i++) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(BUZZ_SHORT);
                digitalWrite(BUZZER_PIN, LOW);
                delay(100);
            }
            break;
            
        case 3: // Water OFF
            digitalWrite(BUZZER_PIN, HIGH);
            delay(BUZZ_MEDIUM);
            digitalWrite(BUZZER_PIN, LOW);
            break;
            
        case 4: // Maximum moisture reached
            for(int i = 0; i < 3; i++) {
                digitalWrite(BUZZER_PIN, HIGH);
                delay(BUZZ_SHORT);
                digitalWrite(BUZZER_PIN, LOW);
                delay(100);
            }
            break;
    }
}

// Moisture control based on touch sensor
void updateMoistureLevel() {
    static bool lastTouchState = LOW;
    bool currentTouchState = digitalRead(TOUCH_PIN);
    
    if (currentTouchState == HIGH && lastTouchState == LOW && 
        millis() - lastTouchCheck >= 1000) {
        
        lastTouchCheck = millis();
        currentMoistureLevel = min(currentMoistureLevel + 1, MAX_MOISTURE_LEVEL);
        
        #if DEBUG
        Serial.print("Touch detected! Moisture Level: ");
        Serial.println(currentMoistureLevel);
        #endif
        
        updateLEDMatrix();
        buzzerPattern(1);  // Touch detected pattern
        
        if (currentMoistureLevel >= MAX_MOISTURE_LEVEL) {
            #if DEBUG
            Serial.println("Maximum moisture level reached!");
            #endif
            buzzerPattern(4);  // Maximum moisture pattern
            controlWaterTap(false);
        }
    }
    
    lastTouchState = currentTouchState;
}

// LED Matrix update function
void updateLEDMatrix() {
    #if DEBUG
    Serial.println("Updating LED Matrix...");
    Serial.print("Current Moisture Level: ");
    Serial.println(currentMoistureLevel);
    #endif
    
    matrix.clear();
    matrix.control(MD_MAX72XX::INTENSITY, MATRIX_INTENSITY);
    
    for (int i = 0; i < currentMoistureLevel && i < 8; i++) {
        matrix.setColumn(i, 0xFF);
        delay(10);
    }
    
    matrix.update();
}

// Water control based on conditions
void checkWateringConditions() {
    float temperature = tempSensor.readTemperature();
    int lightLevel = analogRead(LDR_PIN);
    bool shouldWater = false;
    
    #if DEBUG
    if(millis() - lastDebugOutput > 5000) {
        lastDebugOutput = millis();
        Serial.println("\n--- Sensor Readings ---");
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" C");
        Serial.print("Light Level: ");
        Serial.println(lightLevel);
        Serial.print("Current Moisture: ");
        Serial.println(currentMoistureLevel);
        Serial.println("--------------------");
    }
    #endif
    
    if (temperature > 24 && lightLevel < 80 && currentMoistureLevel < MAX_MOISTURE_LEVEL) {
        shouldWater = true;
    }
    
    // Check safety timeout
    if (isWaterOn && (millis() - waterStartTime >= MAX_WATER_TIME)) {
        #if DEBUG
        Serial.println("Safety timeout - turning water off");
        #endif
        shouldWater = false;
    }
    
    if (shouldWater != isWaterOn) {
        if (shouldWater) {
            waterStartTime = millis();
        }
        controlWaterTap(shouldWater);
    }
}

// Control water relay
void controlWaterTap(bool turnOn) {
    if (turnOn != isWaterOn) {
        isWaterOn = turnOn;
        
        #if DEBUG
        Serial.print("\nWater tap turning ");
        Serial.println(turnOn ? "ON" : "OFF");
        #endif
        
        digitalWrite(RELAY_PIN, turnOn ? HIGH : LOW);
        buzzerPattern(turnOn ? 2 : 3);  // Pattern 2 for ON, 3 for OFF
    }
}




// Update OLED display
void updateDisplay() {
    if (millis() - lastSensorUpdate >= 2000) {
        lastSensorUpdate = millis();
        
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        
        // Title
        display.setCursor(0,0);
        display.println("Plant Care System");
        
        // Sensor readings
        float temp = tempSensor.readTemperature();
        int light = analogRead(LDR_PIN);
        light = -light + 1697;
        
        display.setCursor(0,16);
        display.print("Temp: ");
        display.print(temp, 1);
        display.println(" C");
        
        display.print("Light: ");
        display.println(light);
        
        display.print("Moisture: ");
        display.println(currentMoistureLevel);
        
        display.print("Water: ");
        display.println(isWaterOn ? "ON" : "OFF");
        
        if (isWaterOn) {
            unsigned long waterTime = (millis() - waterStartTime) / 1000; // Convert to seconds
            display.print("Water Time: ");
            display.print(waterTime);
            display.println("s");
        }
        
        display.display();
        sendBluetoothData();
    }
}

// Send data via Bluetooth
void sendBluetoothData() {
    String data = floatToString(tempSensor.readTemperature(), 2) + "," +
                 String(analogRead(LDR_PIN)) + "," +
                 String(currentMoistureLevel) + "," +
                 String(isWaterOn);
    bluetooth.println(data);
}

void setup() {
    // Initialize Serial
    Serial.begin(115200);
    while(!Serial) delay(10);
    Serial.println("\n=== Plant Care System Starting ===");
    
    // Initialize pins
    pinMode(TOUCH_PIN, INPUT);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_MATRIX_CS, OUTPUT);
    
    // Set initial states
    digitalWrite(RELAY_PIN, LOW);  // Start with water off
    digitalWrite(LED_MATRIX_CS, HIGH);
    
    // Initialize SPI for LED Matrix
    SPI.begin();
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    
    // Initialize I2C
    Wire.begin();
    Wire.setClock(100000);
    
    // Initialize LED Matrix
    Serial.println("Initializing LED Matrix...");
    matrix.begin();
    matrix.clear();
    matrix.control(MD_MAX72XX::INTENSITY, MATRIX_INTENSITY);
    
    // Test LED Matrix
    for (int i = 0; i < 8; i++) {
        matrix.setColumn(i, 0xFF);
        matrix.update();
        delay(100);
    }
    delay(1000);
    matrix.clear();
    matrix.update();
    Serial.println("LED Matrix initialized");
    
    // Initialize OLED
    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("OLED initialization failed");
        while(1) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(100);
            digitalWrite(BUZZER_PIN, LOW);
            delay(100);
        }
    }
    display.clearDisplay();
    display.display();
    Serial.println("OLED initialized");
    
    // Initialize temperature sensor
    if(!tempSensor.begin()) {
        Serial.println("BMP180 initialization failed");
        while(1) {
            digitalWrite(BUZZER_PIN, HIGH);
            delay(200);
            digitalWrite(BUZZER_PIN, LOW);
            delay(200);
        }
    }
    Serial.println("Temperature sensor initialized");
    
    // Initialize Bluetooth
    bluetooth.begin(9600);
    Serial.println("Bluetooth initialized");
    
    // System ready indication
    buzzerPattern(1);
    Serial.println("\n=== System Ready! ===\n");
}

void loop() {
    static unsigned long lastLoopTime = 0;
    
    // Update moisture and LED Matrix
    updateMoistureLevel();
    
    // Force matrix update periodically
    if (millis() - lastMatrixUpdate >= 2000) {
        lastMatrixUpdate = millis();
        updateLEDMatrix();
    }
    
    // Update other components
    checkWateringConditions();
    updateDisplay();
    
    // Debug output
    if(millis() - lastLoopTime >= 5000) {
        lastLoopTime = millis();
        Serial.println("\n--- System Status ---");
        Serial.print("Uptime: ");
        Serial.print(millis() / 1000);
        Serial.println(" seconds");
        if(isWaterOn) {
            Serial.print("Water running for: ");
            Serial.print((millis() - waterStartTime) / 1000);
            Serial.println(" seconds");
        }
    }
    
    delay(50);  // Small delay for stability
}
