//AT90USB1286 Fuses
//Extended: 0xF3
//HIGH: 0xDF
//LOW: 0x5E

//Use SERIAL_CONNECT.bat file found in the 'tools' folder of this repository.
//----OR----
//Access serial connection using Arduino IDE. Ensure that teensy software suite has been installed https://www.pjrc.com/teensy/td_download.html
//In Arduino, look for Tools->Port->(Emulated Serial)
//Open serial monitor
//Device should reset. You may need to try this a couple times.

//Programming:
/*
You can use the ArduinoISP sketch from the Arduino IDE to turn any standard Arduno Uno/Nano/Mega into a programmer.
It is reccomended that you change the ArduinoISP programmer speed to  #define BAUDRATE	1000000 (line 144), but the default
setting of 19200 works too, though it is very slow.

Pin connections:

The Mega MIDI 6-pin programming connector looks like this:

      MISO °. . 5V 
      SCK   . . MOSI
      RST   . . GND

Arduino | Mega MIDI programming header
GND     | GND
10      | RST
11      | MOSI
12      | MISO
13      | SCK

You do not need to connect 5V.

Using the FLASH.bat file found in the tools folder, or by using the terminal below, you can easily flash your Mega MIDI
Vscode terminal command:
////////////////////////
cd tools
./FLASH.bat
///////////////////////

The SD card will not read properly after being programmed with an ISP device. Remove the SD card, reinsert the card, then press the RESET button on the Mega MIDI board.

Default AVRDUDE command is:
avrdude -c arduino -p usb1286 -P COM16 -b 19200 -U flash:w:"LOCATION_OF_YOUR_PROJECT_FOLDER\.pioenvs\teensy20pp\firmware.hex":a -U lfuse:w:0x5E:m -U hfuse:w:0xDF:m -U efuse:w:0xF3:m 
*/

#define FW_VERSION "1.3"



#define F_CPU 16000000UL

#include "usb_midi_serial.h"
#include <Arduino.h>
#include "Voice.h"
#include "YM2612.h"
#include "SN76489.h"
#include "SdFat.h"
#include <MIDI.h>
#include <Encoder.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include "LCDChars.h"
#include "NPRM.h"

//Music
#include "Adjustments.h" //Look in this file for tuning & pitchbend settings

//MIDI
#define YM_CHANNEL 1
#define PSG_CHANNEL 2
#define YM_VELOCITY_CHANNEL 3
#define PSG_VELOCITY_CHANNEL 4
#define PSG_NOISE_CHANNEL 5
NPRM nprm;


MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

//DEBUG
#define DLED 8

//INPUT
#define PROG_UP 5
#define PROG_DOWN 6
#define LFO_TOG 7
#define ENC_BTN 0
Encoder encoder(18, 19);
long encoderPos = 0;

//LCD
#define LCD_ROWS 4
#define LCD_COLS 20
uint16_t fileNameScrollIndex = 0;
String fileScroll;
uint8_t lcdSelectionIndex = 0;
LiquidCrystal lcd(17, 26, 38, 39, 40, 41, 42, 43, 44, 45); //PC7 & PB6 + Same data bus as sound chips
bool redrawLCDOnNextLoop = false;
bool stopLCDFileUpdate = false;

//Clocks
uint32_t masterClockFrequency = 8000000;

//Sound Chips
SN76489 sn76489 = SN76489();
YM2612 ym2612 = YM2612();
#define PSG_READY 37

//SD Card
SdFat SD;
File file;
#define SD_CHIP_SELECT SS //PB0 
#define FIRST_FILE 0x00
#define NEXT_FILE 0x01
#define PREV_FILE 0x02
#define MAX_FILE_NAME_SIZE 128
char fileName[MAX_FILE_NAME_SIZE];
uint32_t numberOfFiles = 0;
uint32_t currentFileNumber = 0;
bool isFileValid = false;

//Favorites
uint8_t currentFavorite = 0xFF; //If favorite = 0xFF, go back to SD card voices

//Prototypes
void KeyOn(byte channel, byte key, byte velocity);
void KeyOff(byte channel, byte key, byte velocity);
void ProgramChange(byte channel, byte program);
void PitchChange(byte channel, int pitch);
void ControlChange(byte channel, byte control, byte value);
void SystemExclusive(byte *data, uint16_t length);
void HandleFavoriteButtons(byte portValue);
bool LoadFile(byte strategy);
void BlinkLED(byte led);
void ClearLCDLine(byte line);
bool LoadFile(String req);
void PutFavoriteIntoEEPROM(Voice v, uint16_t index);
void SetVoice(Voice v);
void removeMeta();
void ReadVoiceData();
void HandleSerialIn();
void DumpVoiceData(Voice v);
void ResetSoundChips();
void HandleRotaryButtonDown();
void HandleRotaryEncoder();
void LCDRedraw(uint8_t graphicCursorPos = 0);
void ScrollFileNameLCD();
void IntroLEDs();
void UpdateLEDs();
void ProgramNewFavorite();
void SDReadFailure();
void HandleNPRM(uint8_t channel);
Voice GetFavoriteFromEEPROM(uint16_t index);

void setup() 
{
  //YM2612 and PSG Clock Generation
  pinMode(25, OUTPUT);
  pinMode(16, OUTPUT);
  //8MHz on PB5 (YM2612)
  // set up Timer 1
  TCCR1A = bit (COM1A0);  // toggle OC1A on Compare Match
  TCCR1B = bit (WGM12) | bit (CS10);   // CTC, no prescaling
  OCR1A =  0; //Divide by 2

  //4MHz on PC6 (PSG)
  // set up Timer 3
  TCCR3A = bit (COM3A0);  // toggle OC3A on Compare Match
  TCCR3B = bit (WGM32) | bit (CS30);   // CTC, no prescaling
  OCR3A =  1; //Divide by 4

  Serial.begin(115200);
  lcd.createChar(0, arrowCharLeft);
  lcd.createChar(1, arrowCharRight);
  lcd.createChar(2, heartChar);
  lcd.begin(LCD_COLS, LCD_ROWS);

  lcd.print("     Welcome To");
  lcd.setCursor(0,1);
  lcd.print("      MEGA MIDI");
  lcd.setCursor(0,2);
  lcd.print("   Aidan Lawrence");
  lcd.setCursor(0,3);
  lcd.print("        2019  ");
  lcd.print(FW_VERSION);

  MIDI.begin(MIDI_CHANNEL_OMNI);

  delay(20); //Wait for clocks to start
  sn76489.Reset();
  ym2612.Reset();

  usbMIDI.setHandleNoteOn(KeyOn);
  usbMIDI.setHandleNoteOff(KeyOff);
  usbMIDI.setHandleProgramChange(ProgramChange);
  usbMIDI.setHandlePitchChange(PitchChange);
  usbMIDI.setHandleControlChange(ControlChange);
  usbMIDI.setHandleSystemExclusive(SystemExclusive);

  MIDI.setHandleNoteOn(KeyOn);
  MIDI.setHandleNoteOff(KeyOff);
  MIDI.setHandleProgramChange(ProgramChange);
  MIDI.setHandlePitchBend(PitchChange);
  MIDI.setHandleControlChange(ControlChange);

  pinMode(DLED, OUTPUT);
  pinMode(PROG_UP, INPUT_PULLUP);
  pinMode(PROG_DOWN, INPUT_PULLUP);
  pinMode(LFO_TOG, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(PSG_READY, INPUT);

  MIDI.turnThruOff();
  UCSR1B &= ~(1UL << 3); //Release the Hardware Serial1 TX pin from USART transmitter.

  DDRA = 0x00;
  PORTA = 0xFF;
  for(int i = 0; i<8; i++)
  {
    pinMode(leds[i], OUTPUT);
    digitalWrite(leds[i], LOW);
  }
  IntroLEDs();

  if(!SD.begin(SD_CHIP_SELECT, SPI_HALF_SPEED))
  {
    Serial.println("SD Mount failed!");
    SDReadFailure();
  }
  removeMeta();
  attachInterrupt(digitalPinToInterrupt(ENC_BTN), HandleRotaryButtonDown, FALLING);
  LoadFile(FIRST_FILE);
  ReadVoiceData();
  ym2612.SetVoice(voices[0]);
  DumpVoiceData(voices[0]);
  LCDRedraw();
}

void PutFavoriteIntoEEPROM(Voice v, uint16_t index)
{
  if(index > 7)
    return;
  FavoriteVoice fv;
  fv.v = v;
  fv.index = index;
  strncpy(fv.fileName, fileName, 20);
  fv.fileName[20] = '\0';
  fv.voiceNumber = currentProgram;
  fv.octaveShift = ym2612.GetOctaveShift();
  EEPROM.put(sizeof(FavoriteVoice)*index, fv);
}

Voice GetFavoriteFromEEPROM(uint16_t index)
{
  if(index >= 8)
    return voices[currentProgram];
  FavoriteVoice fv;
  EEPROM.get(sizeof(FavoriteVoice)*index, fv);
  if(fv.index != index)
  {
    Serial.println("ERROR, index mismatch!");
    Serial.print("Wanted: "); Serial.println(index, HEX);
    Serial.print("Got: "); Serial.println(fv.index, HEX);
    currentFavorite = 0xFF;
    LCDRedraw(lcdSelectionIndex);
    lcd.setCursor(0, 2);
    lcd.print("No favorite set");
    lcd.setCursor(0,3);
    lcd.print("Hold to set favorite");
    return voices[currentProgram];
  }
  ym2612.SetOctaveShift(fv.octaveShift);
  LCDRedraw(lcdSelectionIndex);
  return fv.v;
}

void IntroLEDs()
{
  int i = 0;
  for(i=0; i<8; i++)
  {
    digitalWrite(leds[i], HIGH);
    delay(100);
  }

  for(i=0; i<8; i++)
  {
    digitalWrite(leds[i], LOW);
    delay(100);
  }
}

bool LoadFile(byte strategy) //Request a file with NEXT, PREV, FIRST commands
{
  File nextFile;
  memset(fileName, 0x00, MAX_FILE_NAME_SIZE);
  switch(strategy)
  {
    case FIRST_FILE:
    {
      File countFile;
      while ( countFile.openNext( SD.vwd(), O_READ ))
      {
        countFile.close();
        numberOfFiles++;
      }
      countFile.close();
      SD.vwd()->rewind();

      if(file.isOpen())
        file.close();
      file.openNext(SD.vwd(), O_READ);
      if(!file)
      {
        Serial.println("File Read failed!");
        SDReadFailure();
      }
      file.getName(fileName, MAX_FILE_NAME_SIZE);
      Serial.println(fileName);
      currentFileNumber = 0;
      return true;
    break;
    }
    case NEXT_FILE:
    {
      if(currentFileNumber+1 >= numberOfFiles)
      {
          SD.vwd()->rewind();
          currentFileNumber = 0;
      }
      else
          currentFileNumber++;
      nextFile.openNext(SD.vwd(), O_READ);
      nextFile.getName(fileName, MAX_FILE_NAME_SIZE);
      nextFile.close();
      break;
    }
    case PREV_FILE:
    {
      if(currentFileNumber != 0)
      {
        currentFileNumber--;
        SD.vwd()->rewind();
        for(uint32_t i = 0; i<=currentFileNumber; i++)
        {
          nextFile.close();
          nextFile.openNext(SD.vwd(), O_READ);
        }
        nextFile.getName(fileName, MAX_FILE_NAME_SIZE);
        nextFile.close();
      }
      else
      {
        currentFileNumber = numberOfFiles-1;
        SD.vwd()->rewind();
        for(uint32_t i = 0; i<=currentFileNumber; i++)
        {
          nextFile.close();
          nextFile.openNext(SD.vwd(), O_READ);
        }
        nextFile.getName(fileName, MAX_FILE_NAME_SIZE);
        nextFile.close();
      }
      break;
    }
  }
  if(file.isOpen())
    file.close();
  file = SD.open(fileName, FILE_READ);
  if(!file)
  {
    Serial.println("Failed to read file");
    SDReadFailure();
  }
  ReadVoiceData();
  ym2612.SetVoice(voices[0]);
  currentProgram = 0;
  LCDRedraw();
  return true;
}

void SDReadFailure()
{
  lcd.clear();
  lcd.home();
  lcd.print("SD card");
  lcd.setCursor(0,1);
  lcd.print("read failure!");
  for(int i = 0; i<8; i++)
  {
    if(i%2==0)
      digitalWrite(leds[i], HIGH);
    else
      digitalWrite(leds[i], LOW);
  }
  while(true){}
}

bool LoadFile(String req) //Request a file (string) to load
{
  bool fileFound = false;
  char searchFn[MAX_FILE_NAME_SIZE];
  SD.vwd()->rewind();
  Serial.print("REQUEST: "); Serial.println(req);
  File nextFile;
  for(uint32_t i = 0; i<numberOfFiles; i++)
  {
    nextFile.close();
    nextFile.openNext(SD.vwd(), O_READ);
    nextFile.getName(searchFn, MAX_FILE_NAME_SIZE);
    String tmpFN = String(searchFn);
    tmpFN.trim();
    req.trim();
    if(tmpFN == req)
    {
      currentFileNumber = i;
      fileFound = true;
      break;
    }
  }
  nextFile.close();
  if(!fileFound)
  {
    Serial.println("Error: File not found!");
    return false;
  }
  memset(fileName, 0x00, MAX_FILE_NAME_SIZE);
  strncpy(fileName, searchFn, MAX_FILE_NAME_SIZE);
  if(file.isOpen())
    file.close();
  file = SD.open(fileName, FILE_READ);
  if(!file)
  {
    Serial.println("Failed to read file");
    digitalWrite(DLED, HIGH);
    while(true){}
  }
  ReadVoiceData();
  ym2612.SetVoice(voices[0]);
  currentProgram = 0;
  LCDRedraw();
  return true;
}

void removeMeta() //Remove useless meta files
{
  File tmpFile;
  while ( tmpFile.openNext( SD.vwd(), O_READ ))
  {
    memset(fileName, 0x00, MAX_FILE_NAME_SIZE);
    tmpFile.getName(fileName, MAX_FILE_NAME_SIZE);
    if(fileName[0]=='.')
    {
      if(!SD.remove(fileName))
      if(!tmpFile.rmRfStar())
      {
        Serial.print("FAILED TO DELETE META FILE"); Serial.println(fileName);
      }
    }
    if(String(fileName) == "System Volume Information")
    {
      if(!tmpFile.rmRfStar())
        Serial.println("FAILED TO REMOVE SVI");
    }
    tmpFile.close();
  }
  tmpFile.close();
  SD.vwd()->rewind();
}

void HandleRotaryButtonDown()
{
  lcdSelectionIndex++;
  lcdSelectionIndex %= 3;
  redrawLCDOnNextLoop = true;
}

void LCDRedraw(uint8_t graphicCursorPos)
{
  lcd.clear();
  lcd.home();
  lcdSelectionIndex = graphicCursorPos;
  lcd.setCursor(0, lcdSelectionIndex);
  lcd.print(" ");
  if(lcdSelectionIndex < 2)
  {
    lcd.setCursor(0, lcdSelectionIndex);
    lcd.print(" ");
    lcd.setCursor(0, lcdSelectionIndex);
    lcd.write((uint8_t)0); //Arrow Right
  }

  lcd.setCursor(1, 0);
  String fn = fileName;
  fn = fn.remove(LCD_COLS-1);
  fileScroll = fileName;
  lcd.print(fn);
  lcd.setCursor(1, 1);
  if(isFileValid)
  {
    lcd.print("Voice #");
    lcd.print(currentProgram);
    lcd.print("/");
    lcd.print(maxValidVoices-1);
    lcd.print("  ");
  }
  else
  {
    lcd.print("NO VOICES");
  }
  

  lcd.setCursor(15, 1);
  lcd.print("OCT");
  int8_t oct = ym2612.GetOctaveShift();
  if(oct >= 0)
    lcd.print("+");
  lcd.print(oct);
  if(lcdSelectionIndex == 2)
  {
    lcd.setCursor(14, 1);
    lcd.print(" ");
    lcd.setCursor(14, 1);
    lcd.write((uint8_t)0); //Arrow Right
  }

  if(currentFavorite != 0xFF && currentFavorite < 8)
  {
    FavoriteVoice fv;
    EEPROM.get(sizeof(FavoriteVoice)*currentFavorite, fv);
    lcd.setCursor(0, 2);
    lcd.print(fv.fileName);
    lcd.setCursor(0, 3);
    lcd.print("Voice #"); lcd.print(fv.voiceNumber); lcd.print("   "); lcd.write(2); lcd.print(currentFavorite);
  }
}

void ResetSoundChips()
{
  ym2612.Reset();
  sn76489.Reset();
  ym2612.SetVoice(voices[currentProgram]);
  Serial.println("Soundchips Reset");
}

void ReadVoiceData()
{
  size_t n;
  uint8_t voiceCount = 0;
  char * pEnd;
  uint8_t vDataRaw[6][11];
  const size_t LINE_DIM = 60;
  char line[LINE_DIM];
  bool foundNoName = false;
  while ((n = file.fgets(line, sizeof(line))) > 0) 
  {
      String l = line;
      //Ignore comments
      if(l.startsWith("//"))
        continue;
      if(l.startsWith("@:"+String(voiceCount)+" no Name"))
      {
        maxValidVoices = voiceCount;
        foundNoName = true;
        break;
      }
      else if(l.startsWith("@:"+String(voiceCount)))
      {
        for(int i=0; i<6; i++)
        {
          file.fgets(line, sizeof(line));
          l = line;
          l.replace("LFO: ", "");
          l.replace("CH: ", "");
          l.replace("M1: ", "");
          l.replace("C1: ", "");
          l.replace("M2: ", "");
          l.replace("C2: ", "");
          l.toCharArray(line, sizeof(line), 0);

          vDataRaw[i][0] = strtoul(line, &pEnd, 10); 
          for(int j = 1; j<11; j++)
          {
            vDataRaw[i][j] = strtoul(pEnd, &pEnd, 10);
          }
        }

        for(int i=0; i<5; i++) //LFO
          voices[voiceCount].LFO[i] = vDataRaw[0][i];
        for(int i=0; i<7; i++) //CH
          voices[voiceCount].CH[i] = vDataRaw[1][i];
        for(int i=0; i<11; i++) //M1
          voices[voiceCount].M1[i] = vDataRaw[2][i];
        for(int i=0; i<11; i++) //C1
          voices[voiceCount].C1[i] = vDataRaw[3][i];
        for(int i=0; i<11; i++) //M2
          voices[voiceCount].M2[i] = vDataRaw[4][i];
        for(int i=0; i<11; i++) //C2
          voices[voiceCount].C2[i] = vDataRaw[5][i];
        voiceCount++;
      }
      if(voiceCount == MAX_VOICES-1)
        break;
  }
  if(!foundNoName)
    maxValidVoices = voiceCount;
  if(voiceCount == 0)
  {
    isFileValid = false;
    Serial.println("No voices found");
  }
  else
  {
    isFileValid = true;
    Serial.println("Done Reading Voice Data");
  }
}

void DumpVoiceData(Voice v) //Used to check operator settings from loaded OPM file
{
  Serial.print("LFO: ");
  for(int i = 0; i<5; i++)
  {
    Serial.print(v.LFO[i]); Serial.print(" ");
  }
  Serial.println();
  Serial.print("CH: ");
  for(int i = 0; i<7; i++)
  {
    Serial.print(v.CH[i]); Serial.print(" ");
  }
  Serial.println();
  Serial.print("M1: ");
  for(int i = 0; i<11; i++)
  {
    Serial.print(v.M1[i]); Serial.print(" ");
  }
  Serial.println();
  Serial.print("C1: ");
  for(int i = 0; i<11; i++)
  {
    Serial.print(v.C1[i]); Serial.print(" ");
  }
  Serial.println();
  Serial.print("M2: ");
  for(int i = 0; i<11; i++)
  {
    Serial.print(v.M2[i]); Serial.print(" ");
  }
  Serial.println();
  Serial.print("C2: ");
  for(int i = 0; i<11; i++)
  {
    Serial.print(v.C2[i]); Serial.print(" ");
  }
  Serial.println();
}

void PitchChange(byte channel, int pitch)
{
  if(channel == YM_CHANNEL || channel == YM_VELOCITY_CHANNEL)
  {
    pitchBendYM = pitch;
    for(int i = 0; i<MAX_CHANNELS_YM; i++)
    {
      ym2612.AdjustPitch(i, pitch);
    }
  }
  else if(channel == PSG_CHANNEL || channel == PSG_VELOCITY_CHANNEL)
  {
    for(int i = 0; i<MAX_CHANNELS_PSG; i++)
    {
      sn76489.PitchChange(i, pitch);
    }
  }
}

void KeyOn(byte channel, byte key, byte velocity)
{
  stopLCDFileUpdate = true;
  if(channel == YM_CHANNEL || channel == YM_VELOCITY_CHANNEL)
  {
    if(isFileValid || currentFavorite != 0xFF)
      ym2612.SetChannelOn(key+SEMITONE_ADJ_YM, velocity, channel == YM_VELOCITY_CHANNEL);
  }
  else if(channel == PSG_CHANNEL || channel == PSG_VELOCITY_CHANNEL)
  {
    sn76489.SetChannelOn(key+SEMITONE_ADJ_PSG, velocity, channel == PSG_VELOCITY_CHANNEL);
  }
  else if(channel == PSG_NOISE_CHANNEL)
  {
    sn76489.SetNoiseOn(key, velocity, 1);
  }
}

void KeyOff(byte channel, byte key, byte velocity)
{
  if(channel == YM_CHANNEL || channel == YM_VELOCITY_CHANNEL)
  {
    ym2612.SetChannelOff(key+SEMITONE_ADJ_YM);
  }
  else if(channel == PSG_CHANNEL || channel == PSG_VELOCITY_CHANNEL)
  {
    sn76489.SetChannelOff(key+SEMITONE_ADJ_PSG);
  }
  else if(channel == PSG_NOISE_CHANNEL)
  {
    sn76489.SetNoiseOff(key);
  }
}

void ControlChange(byte channel, byte control, byte value)
{
  //Serial.print("CONTROL: "); Serial.print("CH:"); Serial.print(channel); Serial.print("CNT:"); Serial.print(control); Serial.print("VALUE:"); Serial.println(value);
  if(control == 0x01 && (channel == YM_CHANNEL || channel == YM_VELOCITY_CHANNEL))
  {
    ym2612.AdjustLFO(value);
  }
  else if(control == 0x01 && channel == PSG_NOISE_CHANNEL)
  {
    sn76489.MIDISetNoiseControl(0x01, value);
  }
  else if(control == 0x40) //Sustain
  {
    if(channel == YM_CHANNEL || channel == YM_VELOCITY_CHANNEL)
    {
      YMsustainEnabled = (value >= 64);
      YMsustainEnabled == true ? ym2612.ClampSustainedKeys() : ym2612.ReleaseSustainedKeys();
    }
    else if(channel == PSG_CHANNEL || channel == PSG_VELOCITY_CHANNEL)
    {
      PSGsustainEnabled = (value >= 64);
      PSGsustainEnabled == true ? sn76489.ClampSustainedKeys() : sn76489.ReleaseSustainedKeys();
    }
  }
  else
  {
    switch (control) //NRPN to control synth manually
    {
      case 99:
      nprm.parameter = value << 7;
      break;
      case 98:
      nprm.parameter += value;
      break;
      case 6:
      nprm.value = value << 7;
      break;
      case 38:
      nprm.value = nprm.value + value;

      //Handle NPRM
      Serial.print("NPRM --- "); Serial.print("PARAM: "); Serial.print(nprm.parameter); Serial.print("   "); Serial.print("VALUE: "); Serial.println(nprm.value);

      if(channel != 0)
        channel--;

      HandleNPRM(channel);
      break;
      default:
      return;
    }
  }
}

void SystemExclusive(byte *data, uint16_t length)
{
  Serial.print("SYSEX: "); Serial.print(" DATA: "); Serial.print(data[0]); Serial.print(" LENGTH: "); Serial.println(length);
}

uint8_t lastProgram = 0;
void ProgramChange(byte channel, byte program)
{
  if(channel == YM_CHANNEL || channel == YM_VELOCITY_CHANNEL)
  {
    if(program == 255)
      program = maxValidVoices-1;
    program %= maxValidVoices;
    currentProgram = program;
    LCDRedraw(lcdSelectionIndex);
    ym2612.SetVoice(voices[currentProgram]);
    Serial.print("Current Voice Number: "); Serial.print(currentProgram); Serial.print("/"); Serial.println(maxValidVoices-1);
    DumpVoiceData(voices[currentProgram]);
    lastProgram = program;
  }
}

void HandleSerialIn()
{
  while(Serial.available())
  {
    char serialCmd = Serial.read();
    switch(serialCmd)
    {
      case 't':
      {

        return;
      }
      case 'o': //Dump current voice operator info
      {
        Serial.print("Current Voice Number: "); Serial.print(currentProgram); Serial.print("/"); Serial.println(maxValidVoices-1);
        DumpVoiceData(voices[currentProgram]);
        return;
      }
      case 'l': //Toggle the Low Frequency Oscillator
      {
        ym2612.ToggleLFO();
        return;
      }
      case '+': //Move up one voice in current OPM file
      {
        ProgramChange(YM_CHANNEL, currentProgram+1);
        return;
      }
      case '-': //Move down one voice in current OPM file
      {
        ProgramChange(YM_CHANNEL, currentProgram-1);
        return;
      }
      case '>': //Move the entire keyboard up one octave for the YM2612
      {
        ym2612.ShiftOctaveUp();
        return;
      }
      case '<': //Move the entire keyboard down one octave for the YM2612
      {
        ym2612.ShiftOctaveDown();
        return;
      }
      case '?': //List the currently loaded OPM file
      {
        Serial.println(fileName);
        return;
      }
      case '!': //Reset the sound chips
      {
        ResetSoundChips();
        return;
      }
      case 'r': //Request a new opm file. format:    r:myOpmFile.opm
      {
        String req = Serial.readString();
        req.remove(0, 1); //Remove colon character
        LoadFile(req);
      }
      break;
      case 'd': //Dump YM2612 shadow registers
      {
        ym2612.DumpShadowRegisters();
        return;
      }
      break;
      default:
        continue;
    }
  }
}

void HandleRotaryEncoder()
{
  long enc = encoder.read();
  if(enc != encoderPos && enc % 4 == 0) //Rotary encoders often have multiple steps between each descrete 'click.' In this case, it is 4
  {
    bool isEncoderUp = enc > encoderPos;
    switch(lcdSelectionIndex)
    {
      case 0:
      {
        LoadFile(isEncoderUp ? NEXT_FILE : PREV_FILE);
        currentFavorite = 0xFF;
        stopLCDFileUpdate = false;
        LCDRedraw(lcdSelectionIndex);
        UpdateLEDs();
      break;
      }
      case 1:
      {
        ProgramChange(YM_CHANNEL, isEncoderUp ? currentProgram+1 : currentProgram-1);
        currentFavorite = 0xFF;
        LCDRedraw(lcdSelectionIndex);
        UpdateLEDs();
      break;
      }
      case 2:
      {
        isEncoderUp == true ? ym2612.ShiftOctaveUp() : ym2612.ShiftOctaveDown();
        LCDRedraw(lcdSelectionIndex); 
      }
      break;
    }
    encoderPos = enc;
  }
}

uint32_t prevMilli = 0;
uint16_t scrollDelay = 500;
void ScrollFileNameLCD()
{
  if(lcdSelectionIndex != 0 || stopLCDFileUpdate)
    return;
  if(strlen(fileName) <= LCD_COLS-1)
    return;
  uint32_t curMilli = millis();
  if(curMilli - prevMilli >= scrollDelay)
  {
    prevMilli = curMilli;
    //Clear top line
    lcd.setCursor(0, 0);
    lcd.println("");
    lcd.setCursor(0, 0);

    //Draw filename substring    
    lcd.setCursor(1, 0);
    String sbStr = fileScroll.substring(fileNameScrollIndex, fileNameScrollIndex+LCD_COLS-1);
    fileNameScrollIndex++;
    if(fileNameScrollIndex+(LCD_COLS-2) >= strlen(fileName))
    {
      fileNameScrollIndex = 0;
      scrollDelay *= 5;
    }
    else
      scrollDelay = 500;
    lcd.print(sbStr);

    //Redraw selection arrow
    if(lcdSelectionIndex == 0)
    {
      lcd.setCursor(0, lcdSelectionIndex);
      lcd.write(" ");
      lcd.setCursor(0, lcdSelectionIndex);
      lcd.write((uint8_t)0); //Arrow Right
    }
    else
    {
      lcd.setCursor(0,0);
      lcd.write(" ");
      lcd.setCursor(0,0);
    }    
  }
}

void UpdateLEDs()
{
  for(int i = 1; i<8; i++)
      digitalWriteFast(leds[i], LOW);
  if(currentFavorite >= 8)
    return;
  digitalWriteFast(leds[currentFavorite], HIGH);
}

void HandleFavoriteButtons(byte portValue)
{
  uint8_t prevFavorite = currentFavorite;
  switch(portValue)
  {
    case 1: //LFO
    ym2612.ToggleLFO();
    break;
    case 2: //Fav 1
    currentFavorite != 1 ? currentFavorite = 1 : currentFavorite = 0xFF;
    break;
    case 4: //Fav 2
    currentFavorite != 2 ? currentFavorite = 2 : currentFavorite = 0xFF;
    break;
    case 8: //Fav 3
    currentFavorite != 3 ? currentFavorite = 3 : currentFavorite = 0xFF;
    break;
    case 16: //Fav 4
    currentFavorite != 4 ? currentFavorite = 4 : currentFavorite = 0xFF;
    break;
    case 32: //Fav 5
    currentFavorite != 5 ? currentFavorite = 5 : currentFavorite = 0xFF;
    break;
    case 64: //Fav 6
    currentFavorite != 6 ? currentFavorite = 6 : currentFavorite = 0xFF;
    break;
    case 128: //Fav 7
    currentFavorite != 7 ? currentFavorite = 7 : currentFavorite = 0xFF;
    break;
    default:
    break;
  }

  uint32_t i = 0;
  bool favoriteProgrammed = false;
  while(PINA == portValue || PINA != 0xFF)
  {
    if(portValue != 1)
    {
      delay(1);
      if(i >= 2000 && !favoriteProgrammed)
      {
        if(!isFileValid)
          return;
        if(currentFavorite == 0xFF)
          currentFavorite = prevFavorite;
        ProgramNewFavorite();
        favoriteProgrammed = true;
      }
      i++;
    }
  } 
  if(!favoriteProgrammed)
  {
    if(currentFavorite != 0xFF)
      ym2612.SetVoice(GetFavoriteFromEEPROM(currentFavorite));
    else
    {
      ym2612.SetVoice(voices[currentProgram]);
      LCDRedraw(lcdSelectionIndex);
      currentFavorite = 0xFF;
    }
  }
  UpdateLEDs();
  delay(50); //Debounce
}

void BlinkLED(byte led)
{
  if(led >= 8)
    return;
  for(int i = 0; i < 4; i++)
  {
    digitalWriteFast(leds[led], HIGH);
    delay(100);
    digitalWriteFast(leds[led], LOW);
    delay(100);
  }
}

void ClearLCDLine(byte line)
{
  if(line >= LCD_ROWS-1)
    return;
  lcd.setCursor(0, line);
  for(int i=0; i<LCD_COLS; i++)
  {}
    lcd.write(' ');
  lcd.setCursor(0, line);
}

void ProgramNewFavorite()
{
  if(currentFavorite == 0xFF)
    return;
  Serial.print("NEW FAVORITE: "); Serial.println(currentFavorite);
  PutFavoriteIntoEEPROM(voices[currentProgram], currentFavorite);
  GetFavoriteFromEEPROM(currentFavorite);
  LCDRedraw(lcdSelectionIndex);
  BlinkLED(currentFavorite);
  digitalWriteFast(leds[currentFavorite], HIGH);
}

void HandleNPRM(uint8_t channel)
{
  uint8_t op = ((nprm.parameter/10)%10)-1;
  switch(nprm.parameter)
  {
    case 10:
    case 20:
    case 30:
    case 40:
      ym2612.SetDetune(channel, op, nprm.value);
      break;
    case 11:
    case 21:
    case 31:
    case 41:
      ym2612.SetMult(channel, op, nprm.value);
      break;
    case 12:
    case 22:
    case 32:
    case 42:
      ym2612.SetTL(channel, op, nprm.value);
      break;
    case 13:
    case 23:
    case 33:
    case 43:
      ym2612.SetAR(channel, op, nprm.value);
      break;
    case 14:
    case 24:
    case 34:
    case 44:
      ym2612.SetD1R(channel, op, nprm.value);
      break;
    case 15:
    case 25:
    case 35:
    case 45:
      ym2612.SetD1L(channel, op, nprm.value);
      break;
    case 16:
    case 26:
    case 36:
    case 46:
      ym2612.SetD2R(channel, op, nprm.value);
      break;
    case 17:
    case 27:
    case 37:
    case 47:
      ym2612.SetRR(channel, op, nprm.value);
      break;
    case 18:
    case 28:
    case 38:
    case 48:
      ym2612.SetRateScaling(channel, op, nprm.value);
      break;  
    case 19:
    case 29:
    case 39:
    case 49:
      ym2612.SetAmplitudeModulation(channel, op, nprm.value > 63);
      break;  
    

    // case 12:
    // ym2612.SetTL(channel, 0, nprm.value);
    // break;
    // case 22:
    // ym2612.SetTL(channel, 1, nprm.value);
    // break;
    // case 32:
    // ym2612.SetTL(channel, 2, nprm.value);
    // break;
    // case 42:
    // ym2612.SetTL(channel, 3, nprm.value);
    // break;

    // case 13:
    // ym2612.SetAR(channel, 0, nprm.value);
    // break;
    // case 23:
    // ym2612.SetAR(channel, 1, nprm.value);
    // break;
    // case 33:
    // ym2612.SetAR(channel, 2, nprm.value);
    // break;
    // case 43:
    // ym2612.SetAR(channel, 3, nprm.value);
    // break;

    // case 14:
    // ym2612.SetD1R(channel, 0, nprm.value);
    // break;
    // case 24:
    // ym2612.SetD1R(channel, 1, nprm.value);
    // break;
    // case 34:
    // ym2612.SetD1R(channel, 2, nprm.value);
    // break;
    // case 44:
    // ym2612.SetD1R(channel, 3, nprm.value);
    // break;

    // case 15:
    // ym2612.SetD1L(channel, 0, nprm.value);
    // break;
    // case 25:
    // ym2612.SetD1L(channel, 1, nprm.value);
    // break;
    // case 35:
    // ym2612.SetD1L(channel, 2, nprm.value);
    // break;
    // case 45:
    // ym2612.SetD1L(channel, 3, nprm.value);
    // break;

    // case 16:
    // ym2612.SetD2R(channel, 0, nprm.value);
    // break;
    // case 26:
    // ym2612.SetD2R(channel, 1, nprm.value);
    // break;
    // case 36:
    // ym2612.SetD2R(channel, 2, nprm.value);
    // break;
    // case 46:
    // ym2612.SetD2R(channel, 3, nprm.value);
    // break;

    // case 17:
    // ym2612.SetRR(channel, 0, nprm.value);
    // break;
    // case 27:
    // ym2612.SetRR(channel, 1, nprm.value);
    // break;
    // case 37:
    // ym2612.SetRR(channel, 2, nprm.value);
    // break;
    // case 47:
    // ym2612.SetRR(channel, 3, nprm.value);
    // break;

    // case 19:
    // ym2612.SetAmplitudeModulation(channel, 0, nprm.value > 63);
    // break;
    // case 29:
    // ym2612.SetAmplitudeModulation(channel, 1, nprm.value > 63);
    // break;
    // case 39:
    // ym2612.SetAmplitudeModulation(channel, 2, nprm.value > 63);
    // break;
    // case 49:
    // ym2612.SetAmplitudeModulation(channel, 3, nprm.value > 63);
    // break;   

    default:
    Serial.println("NPRM DEFAULT");
    break;

  }
}

void loop() 
{
  usbMIDI.read();
  MIDI.read();
  HandleRotaryEncoder();
  if(redrawLCDOnNextLoop)
  {
    redrawLCDOnNextLoop = false;
    LCDRedraw(lcdSelectionIndex);
  }
  ScrollFileNameLCD();
  
  if(Serial.available() > 0)
    HandleSerialIn();
  byte portARead = ~PINA;
  if(portARead)
    HandleFavoriteButtons(portARead);
}
 