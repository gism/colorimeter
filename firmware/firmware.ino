#include <math.h>
#include <EEPROM.h>
#include "Streaming.h"
#include "io_pins.h"
#include "RGBLed.h"
#include "ColorSensor.h"
#include "Colorimeter.h"
#include "EEPROMAnything.h"
#include "SerialReceiver.h"
#include "SerialHandler.h"
#include "tests.h"
#include <U8glib.h>

#define encoder0PinA  2
#define encoder0PinB  A0
#define encoder0SW    3

#define AMMONIA 1
#define NITRATE 2
#define NITRITE 3

const uint8_t EEPROM_NUM_RECORDS = 16;
const uint8_t EEPROM_RECORDS = 18;

#define DISPLAY_BUFFER_SIZE 20
char displayBuf[DISPLAY_BUFFER_SIZE];

struct record_t
{
  unsigned char substance;
  unsigned char height;
};

Colorimeter colorimeter;
SerialHandler comm;

volatile int encoder0Pos = 0;
volatile int menu0SW = 0;
int menuPos = 1;


// FONTS: https://github.com/olikraus/u8glib/wiki/fontsize
//U8GLIB_SSD1306_128X64 u8g(13, 12, 8, 2, 3);
//U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NONE|U8G_I2C_OPT_DEV_0); // I2C / TWI 
//U8GLIB_SSD1306_128X64 u8g(10, 9);    // HW SPI Com: CS = 10, A0 = 9 (Hardware Pins are  SCK = 13 and MOSI = 11)

U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE|U8G_I2C_OPT_DEV_0); // I2C / TWI 



int freeRam () 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

void drawRecordsMenu()
{  
  unsigned char substance = drawSubstanceSelector();

  int numRecords =0;
  EEPROM_readAnything(EEPROM_NUM_RECORDS, numRecords);
  
//  Serial.print("NUM RECORDS: ");
//  Serial.println(numRecords);

  record_t record;

  unsigned char displayArray[125];      //Allocate max size of points for the chart
  int numSelected = 0;
  int i = 0;
  while (i < numRecords){
    EEPROM_readAnything(EEPROM_RECORDS + sizeof(record) * i, record);
    
//    Serial.print("SUBSTANCE: ");
//    Serial.println(record.substance);
//    Serial.print("HEIGHT: ");
//    Serial.println(record.height);
    
    if(record.substance == substance){
      displayArray[numSelected] = (unsigned char) record.height;
      numSelected ++;
    }

    // To do: Fix this case:
    if(numSelected>124){
      break;
    }
    
    i++;
  }

  // Calculate distance between points
  unsigned char pointPadding = 0;
  if (numSelected != 0){
    pointPadding = (128 - 3) / (numSelected + 1);
  }

  // Print Chart
  u8g.firstPage(); 
  do 
  {
    // Print chart substance name:
    u8g.setFont(u8g_font_helvR08);
    if (substance == AMMONIA){
      u8g.drawStr(15, 8, "Amoniaco (NH4+)"); 
    }else if(substance == NITRATE){
      u8g.drawStr(15, 8, "Nitrato (NO3-)"); 
    }else if(substance == NITRITE){
      u8g.drawStr(15, 8, "Nitrito (NO2-)"); 
    }else{
      u8g.drawStr(15, 8, "Desconocido"); 
    }

    // Print axis:
    u8g.drawHLine(0, 61, 128);
    u8g.drawVLine(0, 3, 64);

    // Print chart points:
    i = 0;
    while (i < numSelected){
      
//      Serial.print("X: ");
//      Serial.print(3 + pointPadding * (i + 1));
//      Serial.print(" Y: ");
//      Serial.println(61 - displayArray[i]);
       
      u8g.drawCircle(3 + pointPadding * (i + 1), 61 - displayArray[i], 1);
      i++;
    }
    
  } while(u8g.nextPage());  

  waitHmiButton();
  delay(500);
  menu0SW  = 0;
}

void drawMainMenu(){
  u8g.firstPage();  
  do 
  {
   u8g.drawStr(15, 8, "MENU:"); 
   u8g.drawHLine(0, 9, 128);
   
   u8g.drawStr(15, 25, "CALIBRAR");
   u8g.drawStr(15, 35, "ANÁLISIS");
   u8g.drawStr(15, 45, "DATOS MEMORIA");
   // u8g.drawStr(15, 55, "GET_NUM_SAMPLES");
   
   u8g.drawStr(1, 10 * menuPos + 15, "+");
  } while(u8g.nextPage());  

  if (encoder0Pos > 1){
    menuPos = menuPos + 1;
    encoder0Pos = 0;
  }else if (encoder0Pos < -1){
    menuPos = menuPos - 1;
    encoder0Pos = 0;
  }

  if (menuPos > 3){
    menuPos = 3;
  }else if (menuPos < 1){
    menuPos = 1;
  }
  
  if (menu0SW){
    menu0SW = 0;
    
    if (menuPos == 1)
    {
      calibrateByHMI();  
    }
    else if(menuPos == 2)
    {
      unsigned char substance = drawSubstanceSelector();
      colorimeter.getMeasurement();
      //drawMesurement();
      drawSubstanceInfo(substance);
      menuPos = 2;
    }
    else if(menuPos == 3)
    {
      drawRecordsMenu();
    }
  }
}

void drawSubstanceInfo(unsigned char substance)
{
  colorimeter.getMeasurement();

  float absorbance = 0;
  if (substance == AMMONIA){
    absorbance = colorimeter.absorbance.red;
  }else if(substance == NITRATE){
    absorbance = colorimeter.absorbance.green;
  }else if(substance == NITRITE){
    absorbance = colorimeter.absorbance.green; 
  }
  
  float concentration = getConcentration(absorbance, substance);
  unsigned char safetyLevel = getSafetyLevel(concentration, substance);

  char str_temp[10];
  dtostrf(concentration, 4, 2, str_temp);
  snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "%s mg/ml", str_temp);
 
  u8g.firstPage();  
  do 
  { 
    u8g.setFont(u8g_font_helvR08);
    if (substance == AMMONIA){
      u8g.drawStr(15, 8, "Amoniaco (NH4+)"); 
    }else if(substance == NITRATE){
      u8g.drawStr(15, 8, "Nitrato (NO3-)"); 
    }else if(substance == NITRITE){
      u8g.drawStr(15, 8, "Nitrito (NO2-)"); 
    }else{
      u8g.drawStr(15, 8, "Desconocido"); 
    }
    u8g.drawHLine(0, 9, 128);
    
    u8g.setFont(u8g_font_helvR18);
    u8g.drawStr(5, 36,  displayBuf);

    u8g.drawHLine(0, 46, 128);
    u8g.setFont(u8g_font_helvR08);

    if(safetyLevel == 0){
      u8g.drawStr(15, 56, "Seguro");
    }else if(safetyLevel == 1){
      u8g.drawStr(15, 56, "Seguro");
    }else if(safetyLevel == 2){
      u8g.drawStr(15, 56, "Alerta");
    }else if(safetyLevel == 3){
      u8g.drawStr(15, 56, "Peligro");
    }else if (safetyLevel == 4){
      u8g.drawStr(15, 56, "Toxico");
    }else{
      u8g.drawStr(15, 56, "ERROR");
    }    
  } while(u8g.nextPage());  
    
  waitHmiButton();
  if(drawSaveMenu()){
    // Read number of records saved
    int numRecords = 0;
    EEPROM_readAnything(EEPROM_NUM_RECORDS, numRecords);
    numRecords++;
    
//    Serial.print("Increment NUM RECORDS: ");
//    Serial.println(numRecords);

    record_t record;
    record.substance = substance;
    record.height = (unsigned char) mapfloat(concentration, 0, 8, 0, 64);

//    Serial.print("RECORDING SUBSTANCE: ");
//    Serial.println(record.substance);
//    Serial.print("RECORDING HEIGHT: ");
//    Serial.println(record.height);

    if(EEPROM_RECORDS + sizeof(record) * (numRecords) <= EEPROM.length()){
      EEPROM_writeAnything(EEPROM_RECORDS + sizeof(record) * (numRecords - 1), record);
      EEPROM_writeAnything(EEPROM_NUM_RECORDS, numRecords);
    }
  }
}

void resetRecords(){
  EEPROM_writeAnything(EEPROM_NUM_RECORDS, 0);
}

unsigned char getSafetyLevel(float c, unsigned char substance)
{
  float SafetyLevels [3] [4] = {
    {0, 0.02, 0.05, 0.2},         // Ammonia
    {50, 60, 70, 85},             // Nitrate
    {0.2, 0.45, 0.75, 1}          // Nitrite
  };
   
  unsigned char i = 0;
  while (i<4 && SafetyLevels[substance][i] < c){
    i++;
  }
  return i;
}

float getConcentration(float x, unsigned char substance)
{  
  //                              0  1     2     3      4     5     6     7     8     9     10    11    12    13    14    15
  float AmmoniaAbsorbance[] =    {0, 0.1,  0.09, 0.18,  0.17, 0.28, 0.26, 0.35, 0.36, 0.74, 0.75, 1.46, 1.49, 2.27, 2.29, 2.94};
  float AmmoniaConcentration[] = {0, 0.25, 0.25, 0.5,   0.5,  0.75, 0.75, 1,    1,    2,    2,    4,    4,    6,    6,    8};

  float NitrateAbsorbance[] =    {0, 0.2, 0.21, 0.39, 0.37, 0.54, 0.53, 0.72, 0.73, 0.86, 0.85, 0.99, 0.97, 1.06, 1.09, 1.19, 1.18, 1.25, 1.27, 1.88, 1.88, 2.2, 2.17, 2.34, 2.34, 2.45};
  float NitrateConcentration[] = {0, 5, 5, 10, 10, 15, 15, 20, 20, 25, 25, 30, 30, 35, 35, 40, 40, 40, 40, 80, 80, 120, 120, 160, 160, 200};

  float NitriteAbsorbance[] = {0, 0.13, 0.12, 0.28, 0.31, 0.46, 0.67, 0.66, 0.84, 0.85, 1.04, 1.05, 1.4, 1.42, 0.5, 1.19, 1.21};
  float NitriteConcentration[] = {0, 0.25, 0.25, 0.5, 0.5, 0.75, 1, 1, 1.25, 1.25, 1.5, 1.5, 2, 2, 0.75, 1.75, 1.75};

  int arraySize;
  float *absorbance;
  float *concentration;
  
  if (substance == AMMONIA)
  {
    arraySize = sizeof(AmmoniaAbsorbance)/sizeof(AmmoniaAbsorbance[0]);
    absorbance = (float*)malloc(arraySize);
    concentration = (float*)malloc(arraySize);

    absorbance = AmmoniaAbsorbance;
    concentration = AmmoniaConcentration;  
  }
  else if (substance == NITRATE)
  {
    arraySize = sizeof(NitrateAbsorbance)/sizeof(NitrateAbsorbance[0]);
    absorbance = (float*)malloc(arraySize);
    concentration = (float*)malloc(arraySize);

    absorbance = NitrateAbsorbance;
    concentration = NitrateConcentration;  
  }
  else if (substance == NITRITE)
  {
    arraySize = sizeof(NitriteAbsorbance)/sizeof(NitriteAbsorbance[0]);
    absorbance = (float*)malloc(arraySize);
    concentration = (float*)malloc(arraySize);

    absorbance = NitriteAbsorbance;
    concentration = NitriteConcentration;
  }
  else
  {
    return 0;
  }
  
  int i = 0;  
  while (i<arraySize){
    if (absorbance[i] > x){
      break;
    }
    i = i + 1;
  }

  float r = concentration[0];
  if (i > 0){
    r = mapfloat(x, absorbance[i-1], absorbance[i], concentration[i-1], concentration[i]);
  }

  free(absorbance);
  free(concentration);
  
  return r;  
}

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max)
{
 return (float)(x - in_min) * (out_max - out_min) / (float)(in_max - in_min) + out_min;
}

void setup() {
    // ENCODER STUFF
    pinMode(encoder0PinA, INPUT);
    digitalWrite(encoder0PinA, HIGH);       // turn on pull-up resistor
    pinMode(encoder0PinB, INPUT);
    digitalWrite(encoder0PinB, HIGH);       // turn on pull-up resistor
    pinMode(encoder0SW, INPUT);
    digitalWrite(encoder0SW, HIGH);       // turn on pull-up resistor 
    
    attachInterrupt(0, doEncoder, CHANGE);  // encoder pin on interrupt 0 - pin 2
    attachInterrupt(1, doSW, RISING);  // encoder pin on interrupt 1 - pin 3
    
    Serial.begin(9600);
    printDebugMenu();
    
    drawLogo();
    delay(4000);
    colorimeter.initialize();
    
    // resetRecords();
}

void loop() {
    comm.processInput();
    drawMainMenu();
}

void waitHmiButton(){
  menu0SW = 0;
  delay(1000);
  while(!menu0SW){
    delay(500);
  }
  delay(1000);
  menu0SW = 0;
}

void doSW() {
  menu0SW  = 1;
  //Serial.println ("OK");
}

void doEncoder() {
  /* If pinA and pinB are both high or both low, it is spinning
     forward. If they're different, it's going backward.

     For more information on speeding up this process, see
     [Reference/PortManipulation], specifically the PIND register.
  */
  if (digitalRead(encoder0PinA) == digitalRead(encoder0PinB)) {
    encoder0Pos--;
  } else {
    encoder0Pos++;
  }
  //Serial.println (encoder0Pos, DEC);
}

void drawLogo(){
  u8g.firstPage();  
  do 
  {
   u8g.setFont(u8g_font_helvR18);
   u8g.drawStr(22, 30, "LOGO");
   u8g.setFont(u8g_font_helvR08);
   u8g.drawStr(20, 55, "AquaColor v1.0");
  } while(u8g.nextPage());  
}

void calibrateByHMI(){
  
  u8g.firstPage(); 
  do
  {
   u8g.setFont(u8g_font_helvR08);
   u8g.drawStr(0, 35, "CALIBRANDO....");
  } while(u8g.nextPage());

  colorimeter.calibrate();
  if (colorimeter.checkCalibration() ) {
    colorimeter.EEPROM_saveCalibration();
    
    u8g.firstPage();  
    do 
    {
     u8g.setFont(u8g_font_helvR08);
     u8g.drawStr(0, 35, "CALIBRADO CORRECTO");
     u8g.drawStr(55, 55, "[OK]");
    } while(u8g.nextPage());

    
  }else {
    // CALIBRATION FAIL
    u8g.firstPage(); 
    do
    {
     u8g.setFont(u8g_font_helvR08);
     u8g.drawStr(0, 35, "ERROR EN CALIBRADO");
     u8g.drawStr(55, 55, "[OK]");
    } while(u8g.nextPage());
    
  }
  waitHmiButton();
}

unsigned char drawSubstanceSelector(){
  delay(1000);
  menu0SW = 0;
  menuPos = 1;
  
  while(!menu0SW){
    u8g.firstPage();  
    do 
    {
     u8g.drawStr(15, 25, "Amoniaco (NH4+)");
     u8g.drawStr(15, 35, "Nitrato (NO3-)");
     u8g.drawStr(15, 45, "Nitrito (NO2-)");
     
     u8g.drawStr(1, 10 * menuPos + 15, "+");
    } while(u8g.nextPage());  
  
    if (encoder0Pos > 1){
      menuPos = menuPos + 1;
      encoder0Pos = 0;
    }else if (encoder0Pos < -1){
      menuPos = menuPos - 1;
      encoder0Pos = 0;
    }
  
    if (menuPos > 3){
      menuPos = 3;
    }else if (menuPos < 1){
      menuPos = 1;
    }
  }
  delay(1000);
  menu0SW = 0;
  
  return menuPos;
}

int drawSaveMenu(){
  menuPos = 1;
  while(!menu0SW){
    u8g.firstPage();  
    do 
    {
      u8g.drawStr(15, 25, "Salvar datos?");
      if (menuPos == 1){
        u8g.drawStr(15, 35, "[SI]      No");
      }else if (menuPos == 2){
        u8g.drawStr(15, 35, " SI      [No]");
      }
    } while(u8g.nextPage());  
    
    if (encoder0Pos > 1){
      menuPos = menuPos + 1;
      encoder0Pos = 0;
    }else if (encoder0Pos < -1){
      menuPos = menuPos - 1;
      encoder0Pos = 0;
    }
  
    if (menuPos > 2){
      menuPos = 2;
    }else if (menuPos < 1){
      menuPos = 1;
    }
  }
  delay(500);
  menu0SW = 0;

  if (menuPos == 1){
    return 1;
  }

  return 0;
}

void printDebugMenu(){
//  Serial << "Hola soy un colorimetro 2\n";
//  Serial << "MENU:\n";
//  Serial << "=========================\n";
//  Serial << "[0] - CMD_CALIBRATE\n";
//  Serial << "[1] - CMD_GET_MEASUREMENT\n";
//  Serial << "[2] - CMD_SET_NUM_SAMPLES\n";
//  Serial << "[3] - CMD_GET_NUM_SAMPLES\n";
//  Serial << "[4] - CMD_GET_CALIBRATION\n\n";
//  Serial << "[5] - CMD_CALIBRATE_RED\n";
//  Serial << "[6] - CMD_CALIBRATE_GREEN\n";
//  Serial << "[7] - CMD_CALIBRATE_BLUE\n";
//  Serial << "[8] - CMD_CALIBRATE_WHITE\n\n";
//  Serial << "[9] - CMD_GET_MEASUREMENT_RED\n";
//  Serial << "[10] - CMD_GET_MEASUREMENT_GREEN\n";
//  Serial << "[11] - CMD_GET_MEASUREMENT_BLUE\n";
//  Serial << "[12] - CMD_GET_MEASUREMENT_WHITE\n\n";
//  Serial << "[13] - CMD_SET_MODE_COLOR_SPECIFIC\n";
//  Serial << "[14] - CMD_SET_MODE_COLOR_INDEPENDENT\n";
//  Serial << "[15] - CMD_GET_SENSOR_MODE\n";
}

//void drawMesurement(){
//  
//  char str_temp[20];
//  
//  Serial << '[' << "OK";
//  Serial << ',' << _DEC(colorimeter.frequency.red);
//  Serial << ',' << _DEC(colorimeter.frequency.green);
//  Serial << ',' << _DEC(colorimeter.frequency.blue);
//  Serial << ',' << _DEC(colorimeter.frequency.white);
//
//  dtostre(colorimeter.transmission.red, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//  dtostre(colorimeter.transmission.green, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//  dtostre(colorimeter.transmission.blue, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//  dtostre(colorimeter.transmission.white, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//
//  dtostre(colorimeter.absorbance.red, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//  dtostre(colorimeter.absorbance.green, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//  dtostre(colorimeter.absorbance.blue, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//  dtostre(colorimeter.absorbance.white, str_temp, 12, 0);
//  Serial << ',' << str_temp;
//  Serial << ']' << endl;
//
//  u8g.firstPage();  
//  do 
//  {
//   u8g.setFont(u8g_font_helvR08);
//   u8g.drawStr(0, 10, "MEAS. FREQUENCY:");
//   
//   dtostrf(colorimeter.frequency.red, 4, 2, str_temp);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[R: %s]", str_temp);
//   u8g.drawStr(0, 25,  displayBuf);
//
//   dtostrf(colorimeter.frequency.green, 4, 2, str_temp);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[G: %s]", str_temp);
//   u8g.drawStr(0, 35, displayBuf);
//
//   dtostrf(colorimeter.frequency.blue, 4, 2, str_temp);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[B: %s]", str_temp);
//   u8g.drawStr(0, 45, displayBuf);
//
//   dtostrf(colorimeter.frequency.white, 4, 2, str_temp);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[W: %s]", str_temp);
//   u8g.drawStr(0, 55, displayBuf);
//   
//  } while(u8g.nextPage());
//  
//  delay(1000);
//  menu0SW = 0;
//  while(!menu0SW){
//    delay(500);
//  }
//  delay(1000);
//  menu0SW = 0;
//  
//  u8g.firstPage();  
//  do 
//  {
//   u8g.setFont(u8g_font_helvR08);
//   u8g.drawStr(0, 10, "MEAS. TRANSMISSION:");
//   
//   dtostre(colorimeter.transmission.red, str_temp, 12, 0);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[R: %s]", str_temp);
//   u8g.drawStr(0, 25,  displayBuf);
//
//   dtostre(colorimeter.transmission.green, str_temp, 12, 0);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[G: %s]", str_temp);
//   u8g.drawStr(0, 35, displayBuf);
//
//   dtostre(colorimeter.transmission.blue, str_temp, 12, 0);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[B: %s]", str_temp);
//   u8g.drawStr(0, 45, displayBuf);
//
//   dtostre(colorimeter.transmission.white, str_temp, 12, 0);
//   snprintf (displayBuf, DISPLAY_BUFFER_SIZE, "[W: %s]", str_temp);
//   u8g.drawStr(0, 55, displayBuf);
//   
//  } while(u8g.nextPage());
//
//  while(!menu0SW){
//    delay(500);
//  }
//  delay(1000);
//  menu0SW = 0;
//}
