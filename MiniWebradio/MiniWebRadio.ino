//******************************************************************************************
//*  Esp_radio -- Webradio receiver for ESP32, 2.8 color display and VS1053 MP3 module.    *
//******************************************************************************************
//
//  if not enough space in nvs or flash: change defaut.cvs
//
//  Name,     Type,   SubType,   Offset,   Size,     Flags
//  phy_init, data,   phy,       0x9000,   0x7000
//  factory,  app,    factory,   0x10000,  3M
//  nvs,      data,   nvs,       ,         0x32000
//
//-----------------------------------------------------------------------------------------
//  for german umlaut change Properties in Eclipse in UTF-8
//  File/Properties/Resource:  Text file encoding (other) UTF-8
//-----------------------------------------------------------------------------------------
//
//  if You see a compiler message in Eclipse: "Invalid arguments ' Candidates are: String SSID()"
//  comment lines 45...56 in WiFi.h and insert his:
//
//  String  SSID(uint8_t networkItem)    {return WiFiScanClass::SSID(networkItem);}
//  int32_t RSSI(uint8_t networkItem)    {return WiFiScanClass::RSSI(networkItem);}
//  uint8_t *BSSID(uint8_t networkItem)  {return WiFiScanClass::BSSID(networkItem);}
//  String  BSSIDstr(uint8_t networkItem){return WiFiScanClass::BSSIDstr(networkItem);}
//  int32_t channel(uint8_t networkItem) {return WiFiScanClass::channel(networkItem);}
//
//  String  SSID(void)                   {return WiFiSTAClass::SSID();}
//  int32_t RSSI(void)                   {return WiFiSTAClass::RSSI();}
//  uint8_t *BSSID(void)                 {return WiFiSTAClass::BSSID();}
//  String  BSSIDstr(void)               {return WiFiSTAClass::BSSIDstr();}
//  int32_t channel(void)                {return WiFiGenericClass::channel();}
//
//  Display 320x240
//
//
//  +-------------------------------------------+ _yHeader=0
//  | Header                                    |       _hHeader=20px
//  +-------------------------------------------+ _yName=20
//  |                                           |
//  | Logo                   StationName        |       _hName=100px
//  |                                           |
//  +-------------------------------------------+ _yTitle=120
//  |                                           |
//  |              StreamTitle                  |       _hTitle=100px
//  |                                           |
//  +-------------------------------------------+ _yFooter=220
//  | Footer                                    |       _hFooter=18px
//  +-------------------------------------------+ 240
//                                             320
// system libraries
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <SD.h>
#include <FS.h>

// own libraries
#include "IR.h"             // see my repository at github "ESP32-IR-Remote-Control"
#include "vs1053_ext.h"     // see my repository at github "ESP32-vs1053_ext"
#include "tft.h"            // see my repository at github "ESP32-TFT-Library-ILI9431-HX8347D"

#include "html.h"
#include "rtime.h"
#include "web.h"

// Digital I/O used
#define VS1053_CS      2
#define VS1053_DCS     4
#define VS1053_DREQ   36
#define TFT_CS        22
#define TFT_DC        21
#define TFT_BL        17
#define TP_IRQ        39
#define TP_CS         16
#define SD_CS          5
#define IR_PIN        34

//global variables
char     sbuf[256], myIP[100];
String   _station="",  _stationname="", _stationURL="", _homepage="";
String   _title="", _info="", _myIP="",_alarmtime="", _time_s="", _hour="", _bitrate="";
String   _SSID="";
String   _mp3Name[10], _pressBtn[5], _releaseBtn[5];
int8_t   _mp3Index=0;           // pointer _mp3Name[]
uint8_t  _releaseNr=0;
uint8_t  _timefile=0;           // speak the time
uint8_t  _commercial_dur=0;     // duration of advertising
uint32_t _millis=0;
uint32_t _alarmdays=0;
boolean  f_1sec=false;          // flag set every one second
boolean  f_SD_okay=false;       // true if SD card in place and readable
boolean  f_mute=false;
boolean  f_rtc=false;           // true if time from ntp is received
boolean  f_mp3eof=false;        // set at the end of mp3 file
boolean  f_alarm=false;         // set if alarmday and alarmtime is equal localtime
boolean  f_timespeech=false;    // if true activate timespeech
boolean  f_has_ST=false;        // has StreamTitle?
boolean  semaphore=false;

// display layout
const uint16_t _yHeader =0;                     // yPos Header
const uint16_t _hHeader =20;                    // height Header
const uint16_t _yName  =_yHeader+_hHeader;      // yPos StationName
const uint16_t _hName  =100;                    // height Stationname
const uint16_t _yTitle =_yName+_hName;          // yPos StreamTitle
const uint16_t _hTitle =100;                    // height StreamTitle
const uint16_t _yFooter =_yTitle+_hTitle;       // yPos Footer
const uint16_t _hFooter=20;                     // height Footer
const uint16_t _yVolBar=_yTitle+30;             // yPos VolumeBar
const uint16_t _hVolBar=5;                      // height VolumeBar
const uint16_t _wLogo=96;                       // width Logo
const uint16_t _hLogo=96;                       // height Logo
const uint16_t _yLogo=_yName+(_hName-_hLogo)/2; // yPos Logos
const uint16_t _wBtn=64;                        // width Button
const uint16_t _hBtn=64;                        // height Button
const uint16_t _yBtn=_yVolBar+_hVolBar+10;      // yPos Buttons

enum status{RADIO=0, RADIOico=1, RADIOmenue=2, CLOCK=3, CLOCKico=4, BRIGHTNESS=5, MP3PLAYER=6, MP3PLAYERico=7, ALARM=8};
status _state=RADIO;            //statemaschine

//objects
TFT tft(1);                     // parameter:  (0) ILI9341,  (1)HX8347D
VS1053 mp3(VS1053_CS, VS1053_DCS, VS1053_DREQ);
hw_timer_t* timer=NULL;         // instance of the timer
HTML web;
Preferences pref;
RTIME rtc;
IR ir(IR_PIN);                  // do not change the objectname, it must be "ir"
TP tp(TP_CS, TP_IRQ);

//**************************************************************************************************
//                                D E F A U L T S E T T I N G S                                    *
//**************************************************************************************************

void clearallpresets(){
    uint16_t i=0;
    char tkey[12];
    for(i=0; i<256; i++){
        sprintf (tkey, "preset_%03d",i);
        pref.remove(tkey);
    }
}

void defaultsettings(){
    String str="", info="";
    char tkey[12];
    uint16_t i=0;
    log_i("set default");
    //
    pref.clear();
    //
    pref.putString("wifi_00","Wolles-FRITZBOX|xxxxx"); // here Your default login credentials
    //
    pref.putString("ESP32_Radio","default");
    pref.putUInt("brightness",100); // 100% display backlight
    pref.putUInt("alarm_weekday",0); // for alarmclock
    pref.putString("alarm_time","00:00");
    pref.putUInt("ringvolume",21);
    //
    pref.putUInt("volume",12); // 0...21
    pref.putUInt("mute",   0); // no mute
    pref.putUInt("toneha", 0); // BassFreq 0...15
    pref.putUInt("tonehf", 0); // TrebleGain 0...14
    pref.putUInt("tonela", 0); // BassGain 0...15
    pref.putUInt("tonelf", 0); // BassFreq 0...13
    //
    pref.putUInt("preset", 1);
    //
    // StationList
    File file = SD.open("/presets.csv");
    if(file){                                   // try to read from SD
        str=file.readStringUntil('\n');         // headerline
        while(file.available()){
            str=file.readStringUntil(';');      // favorit
            if(str!="*"){str=file.readStringUntil('\n'); continue;}// ignore this entry
            i++;
            str=file.readStringUntil(';');      // country
            str=file.readStringUntil(';');      // station
            str+="#";
            str+=file.readStringUntil(';');     // url
            info=file.readStringUntil('\n');    // info
            sprintf(tkey, "preset_%03d", i);
            //str=ASCIItoUTF8(str.c_str());
            pref.putString(tkey, str);
        }
        file.close();
        pref.putUInt("maxstations", i);
    }
    else{                                       // file not available
        log_i("SD/presets.csv not found, use default stream URls");
        String s[12], u[12];
        s[  1]="030-berlinfm";          u[  1]="vtuner.stream.laut.fm/030-berlinfm"; //D
        s[  2]="104.6 RTL";             u[  2]="rtlberlin.hoerradar.de/rtlberlin-live-mp3-128"; //D
        s[  3]="105.5 Spreeradio";      u[  3]="stream.spreeradio.de/spree-live/mp3-128/vtuner/"; //D
        s[  4]="106.4 TOP FM";          u[  4]="mp3.topfm.c.nmdn.net/ps-topfm/livestream.mp3"; //D
        s[  5]="1Live, WDR Koeln";      u[  5]="www.wdr.de/wdrlive/media/einslive.m3u"; //D
        s[  6]="88vier";                u[  6]="ice.rosebud-media.de:8000/88vier"; //D
        s[  7]="93,6 JAM FM";           u[  7]="stream.jam.fm/jamfm-live/mp3-128/vtuner/"; //D
        s[  8]="94.3 RS2";              u[  8]="stream.rs2.de/rs2/mp3-128/internetradio"; //D
        s[  9]="94.3 RS2 Partymix";     u[  9]="stream.rs2.de/rs2-relax/mp3-128/internetradio"; //D
        s[ 10]="95.5 Charivari";        u[ 10]="rs5.stream24.net:80/stream"; //D
        s[ 11]="98.8 KISS FM";          u[ 11]="stream.kissfm.de/kissfm/mp3-128/internetradio"; //D
        for(i=0; i<12; i++){
            sprintf(tkey, "preset_%03d", i);
            str=s[i]+String("#")+u[i];
            pref.putString(tkey, str);
        }
        pref.putUInt("maxstations", 11);
    }
    // establish a WiFi Networklist from "SD/networks.csv" if present
    for(i=1; i<10; i++){                        // clear all WiFi items except item wifi_00
        sprintf(tkey, "wifi_%02d", i);
        pref.putString(tkey, "|");
    }

    file = SD.open("/networks.csv");
    if(file){                                   // try to read from SD
    str=file.readStringUntil('\n');             // headerline
        i=0;
        while(file.available()){
            str=file.readStringUntil(';');      // SSID/PW
            info=file.readStringUntil('\n');    // info
            sprintf(tkey, "wifi_%02d", i);
            pref.putString(tkey, str);
            i++;
            if(i==10) break;
        }
        file.close();
    }
    else{
        log_i("SD/network.csv not found, use default WiFi credentials");
    }
}
boolean ST_rep(){  // replace Streamtitle, seek in presets.csv
    if(!f_SD_okay)return false;
    File file = SD.open("/presets.csv");
    if(!file)return false;
    uint16_t i=0;
    uint16_t preset=pref.getUInt("preset");
    String str="";
    while(file.available()){ // try to read from SD
        file.readStringUntil('\n');
        if(file.readStringUntil(';')=="*") i++; // is favorit?
        if(i==100) mp3.loop();
        if(i==200) mp3.loop();
        if(i==300) mp3.loop();
        if(i==400) mp3.loop();
        if(i==preset)break;
    }
    file.readStringUntil(';'); //Country
    file.readStringUntil(';'); //Stationname
    file.readStringUntil(';'); //URL
    str=file.readStringUntil('\n');//probably replacement information
    file.close();
    if(str.length()>5){showTitle(str); return true;}
    return false;
}

//**************************************************************************************************
//                                T F T   B R I G H T N E S S                                      *
//**************************************************************************************************

void setTFTbrightness(uint8_t duty){ //duty 0...100 (min...max)
    ledcAttachPin(TFT_BL, 1);        //Configure variable led, TFT_BL pin to channel 1
    ledcSetup(1, 12000, 8);          // 12 kHz PWM and 8 bit resolution
    ledcWrite(1, duty);
}
inline uint32_t getTFTbrightness(){return ledcRead(1);}

inline uint8_t downBrightness(){
    uint8_t br; br=pref.getUInt("brightness");
    if(br>5) {br-=5; pref.putUInt("brightness", br); setTFTbrightness(br);} return br;}
inline uint8_t upBrightness(){
    uint8_t br; br=pref.getUInt("brightness");
    if(br<100){br+=5; pref.putUInt("brightness", br); setTFTbrightness(br);} return br;}
inline uint8_t getBrightness(){return pref.getUInt("brightness");}

//**************************************************************************************************
//                                       A S C I I                                                 *
//**************************************************************************************************

char ASCIIfromUNI(char ch){  // if no ascci char available return blank
    uint8_t ascii;
    char tab[96]={
     96,173,155,156, 32,157, 32, 32, 32, 32,166,174,170, 32, 32, 32,248,241,253, 32,
     32,230, 32,250, 32, 32,167,175,172,171, 32,168, 32, 32, 32, 32,142,143,146,128,
     32,144, 32, 32, 32, 32, 32, 32, 32,165, 32, 32, 32, 32,153, 32, 32, 32, 32, 32,
    154, 32, 32,225,133,160,131, 32,132,134,145,135,138,130,136,137,141,161,140,139,
     32,164,149,162,147, 32,148,246, 32,151,163,150,129, 32, 32,152};
    ascii=ch;
    if(ch<128) return ascii;
    if(ch<160) return 32;
    ascii-=160;
    ascii=tab[ascii];
    return ascii;
}

uint16_t UNIfromASCII(char ch){
    uint16_t uni;
    uint16_t tab[128]={
     199, 252, 233, 226, 228, 224, 229, 231, 234, 235, 232, 239, 238, 236, 196, 197,
     201, 230, 198, 244, 246, 242, 251, 249, 255, 214, 220, 162, 163, 165,8359, 402,
     225, 237, 243, 250, 241, 209, 170, 186, 191,8976, 172, 189, 188, 161, 171, 187,
    9617,9618,9619,9474,9508,9569,9570,9558,9557,9571,9553,9559,9565,9564,9563,9488,
    9492,9524,9516,9500,9472,9532,9566,9567,9562,9556,9577,9574,9568,9552,9580,9575,
    9576,9572,9573,9561,9560,9554,9555,9579,9578,9496,9484,9608,9604,9612,9616,9600,
     945, 223, 915, 960, 931, 963, 181, 964, 934, 920, 937, 948,8734, 966, 949,8745,
    8801, 177,8805,8804,8992,8993, 247,8776, 176,8729, 183,8730,8319, 178,9632, 160};
    uni=ch;
    if(ch<128)return uni;
    uni-=128;
    uni=tab[uni];
    return uni;
}

const char* ASCIItoUTF8(const char* str){
    uint16_t i=0, j=0, uni=0;
    static char chbuf[256];
    while(str[i]!=0){
            uni=UNIfromASCII(str[i]);
            switch(uni){
            case   0 ... 127:{chbuf[j]=str[i]; i++; j++; break;}
            case 160 ... 191:{chbuf[j]=0xC2; chbuf[j+1]=uni; j+=2; i++; break;}
            case 192 ... 255:{chbuf[j]=0xC3; chbuf[j+1]=uni-64; j+=2; i++; break;}
            default:{chbuf[j]=' '; i++; j++;}}
    }
    chbuf[j]=0;
    return chbuf;
}

const char* UTF8toASCII(const char* str){
    uint16_t i=0, j=0;
    static char chbuf[255];
    while(str[i] != 0){
        chbuf[j]=str[i];
        if(str[i]==0xC2){ // compute unicode from utf-8
            i++;
            if((str[i]>159)&&(str[i]<192)) chbuf[j]=ASCIIfromUNI(str[i]);
            else chbuf[j]=32;
        }
        else if(str[i]==0xC3){
            i++;
            if((str[i]>127)&&(str[i]<192)) chbuf[j]=ASCIIfromUNI(str[i]+64);
            else chbuf[j]=32;
        }
        i++; j++;
    }
    chbuf[j]=0;
    return (chbuf);
}

const char* decodeURL(const char* str){ // decode url in ascii
    static char chbuf[255];
    uint16_t i=0, j=0;
    char a, b, a1, b1, ch=0;
    while(str[i] != 0){
        if(str[i] == '%'){
            a1=0, b1=0;
            if((str[i+1]=='C')&&(str[i+2]=='3')){
                if(str[i+3]=='%'){
                    a = str[i+4];
                    b = str[i+5];
                    if((a>='a' && a<='f')) a1=a-87;
                    if((a>='A' && a<='F')) a1=a-55;
                    if((a>='0' && a<='9')) a1=a-48;
                    if((b>='a' && b<='f')) b1=b-87;
                    if((b>='A' && b<='F')) b1=b-55;
                    if((b>='0' && b<='9')) b1=b-48;
                    ch=16*a1+b1;
                    ch=ch+0x40;
                } else ch=' ';
                i+=6;
            ch=ASCIIfromUNI(ch);
            }
            else if((str[i+1]>'1')&&(str[i+1]<'8')){  // '2'...'7'
                a = str[i+2];
                if((a>='a' && a<='f')) a1=a-87;
                if((a>='A' && a<='F')) a1=a-55;
                if((a>='0' && a<='9')) a1=a-48;
                b1=str[i+1]-48; ch=a1+b1*16;
                i+=3;
            }
            chbuf[j] = ch;
            j++;
        }
        else {
             chbuf[j]=str[i]; i++; j++;
        }
    }
    chbuf[j] = '\0';
    log_i("%s",chbuf);
    return (chbuf);
}

//**************************************************************************************************
//                                        T I M E R                                                *
//**************************************************************************************************
void IRAM_ATTR timer1sec() {
    f_1sec=true;
    //log_i("Wday %i",rtc.getweekday());
    //log_i("aday %i",_alarmdays);
}
void IRAM_ATTR timer5(){                               // called every 5ms
    static uint8_t  count1sec=0;

    count1sec++;
    if(count1sec == 200){
        count1sec=0; timer1sec();                      // 1 second passed?
    }
}
void startTimer() {
    timer = timerBegin(0, 80, true); // timer_id = 0; divider=80; countUp = true;
    timerAttachInterrupt(timer, &timer5, true); // edge = true
    timerAlarmWrite(timer, 5000, true); //5 ms
    timerAlarmEnable(timer);
    delay(1000);
}
//**************************************************************************************************
//                                       D I S P L A Y                                             *
//**************************************************************************************************
inline void clearHeader() {tft.fillRect(0, _yHeader, tft.width(), _hHeader-1, TFT_BLACK);}  // y   0...19
inline void clearStation(){tft.fillRect(0, _yName,   tft.width(), _hName-1,   TFT_BLACK);}  // y  20...119
inline void clearTitle()  {tft.fillRect(0, _yTitle,  tft.width(), _hTitle-1,  TFT_BLACK);}  // y 120...219
inline void clearFooter() {tft.fillRect(0, _yFooter, tft.width(), _hFooter-1, TFT_BLACK);}  // y 220...239
inline void clearDisplay(){tft.fillScreen(TFT_BLACK);}                       // y   0...239

void displayinfo(const char *str, int ypos, int height, uint16_t color, uint16_t indent){
    tft.fillRect(0, ypos, tft.width(), height, TFT_BLACK);  // Clear the space for new info
    tft.setTextColor(color);                                // Set the requested color
    tft.setCursor(indent, ypos);                            // Prepare to show the info
    tft.print(str);                                         // Show the string
}

void showTitle(String str){
    static String title;
    str.trim();  // remove all leading or trailing whitespaces
    str.replace(" | ","|"); //replace whitespace before and next a pipe ( | means \n )
    if(str.length()>4) f_has_ST=true; else {if(str.length()==0) f_has_ST=false;}
        tft.setTextSize(4);
        if(str.length()>45) tft.setTextSize(3);
        if(str.length()>120) tft.setTextSize(1);
        if(_state==0){displayinfo(str.c_str(), _yTitle, _hTitle, TFT_CYAN, 0);}  //state RADIO
        title=str;
}
void showStation(){
    String str="";
    tft.setTextSize(3);
    if(_stationname==""){
        if(_station.length()>75) tft.setTextSize(1);
        displayinfo(_station.c_str(), _yName, _hName, TFT_YELLOW, _wLogo+14);// Show station name
        showTitle("");   // and delete showstreamtitle
        str=_station;
        str.toLowerCase();
        str.replace(",",".");
        sprintf(sbuf,"/logo/%s.bmp",UTF8toASCII(str.c_str()));
        if(f_SD_okay) if(tft.drawBmpFile(SD, sbuf, 0, _yLogo)==false) tft.drawBmpFile(SD, "/logo/unknown.bmp", 1,22);
    }else{
        tft.setTextSize(4);
        if(_stationname.length()>30) tft.setTextSize(3);
        displayinfo(_stationname.c_str(), _yName, _hName, TFT_YELLOW, _wLogo+14);
        showTitle("");
        str=_stationname;
        //log_i("%s", _stationname.c_str());
        str.toLowerCase();
        str.replace(",",".");
        sprintf(sbuf,"/logo/%s.bmp",UTF8toASCII(str.c_str()));
        //log_i("%s", sbuf);
        if(f_SD_okay) if(tft.drawBmpFile(SD, sbuf, 0, _yLogo)==false) tft.drawBmpFile(SD, "/logo/unknown.bmp", 1,22);}
}
void showHeadlineVolume(uint8_t vol){
    if(_state == ALARM || _state== BRIGHTNESS) return;
    sprintf(sbuf,"Vol %02d",vol);
    tft.fillRect(175, _yHeader, 69, _hHeader, TFT_BLACK);
    tft.setCursor(175, _yHeader);
    tft.setTextSize(2);
    tft.setTextColor(TFT_DEEPSKYBLUE);
    tft.print(sbuf);
}
void showHeadlineItem(const char* hl){
    tft.setTextSize(2);
    displayinfo(hl, _yHeader, _hHeader, TFT_WHITE, 0);
    showHeadlineVolume(getvolume());
}
void showHeadlineTime(){
    if(_state==CLOCK || _state==CLOCKico || _state==BRIGHTNESS || _state==ALARM) return;
    tft.setTextSize(2);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.fillRect(250, _yHeader, 89, _hHeader, TFT_BLACK);
    if(!f_rtc) return; // has rtc the correct time? no -> return
    tft.setCursor(250, 0);
    tft.print(rtc.gettime_s());
}
void showFooter(){  // bitrate stationnumber, IPaddress
    clearFooter();
    if(_bitrate.length()==0) _bitrate="   ";  // if bitrate is unknown
    tft.setTextSize(1);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.setCursor(5, _yFooter);
    tft.print("BR:");
    tft.setTextColor(TFT_LAVENDER);
    tft.print(_bitrate.c_str());

    char tkey[10];   // Key as an array of chars
    uint16_t preset=pref.getUInt("preset");
    tft.setCursor(80, _yFooter);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.print("STA:");
    sprintf(tkey, "%03d", preset);
    tft.setTextColor(TFT_LAVENDER);
    tft.print(tkey);

    tft.setCursor(170, _yFooter);
    tft.setTextColor(TFT_GREENYELLOW);
    tft.print("myIP:");
    tft.setTextColor(TFT_LAVENDER);
    tft.print(_myIP);
}

//**************************************************************************************************
//                                  R E A D H O S T F R O M P R E F                                *
//**************************************************************************************************
String tone(){
    String str_tone="";
    uint8_t u8_tone[4];
    u8_tone[0]=pref.getUInt("toneha"); u8_tone[1]=pref.getUInt("tonehf");
    u8_tone[2]=pref.getUInt("tonela"); u8_tone[3]=pref.getUInt("tonelf");
    sprintf(sbuf, "toneha=%i\ntonehf=%i\ntonela=%i\ntonelf=%i\n",u8_tone[0],u8_tone[1],u8_tone[2],u8_tone[3]);
    str_tone=String(sbuf);
    f_mute=pref.getUInt("mute");
    if(f_mute==false) mp3.setVolume(pref.getUInt("volume"));
    else {mp3.setVolume(0);showHeadlineVolume(0);}
    mp3.setTone(u8_tone);
    return str_tone;
}
String readhostfrompref(uint16_t preset) // 0 read from current preset, 1....max read nr station
{
      String content = "" ;    // Result of search
      char   tkey[12] ;        // Key as an array of chars
      _homepage="";
      //log_i("MaxS %i",pref.getUInt("maxstations"));
      if(preset>pref.getUInt("maxstations")) return "";
      if(preset==0) preset=pref.getUInt("preset");
      sprintf ( tkey, "preset_%03d", preset);
      content=pref.getString(tkey);
      _stationname=content.substring(0, content.indexOf("#")); //get stationname from preset
      content=content.substring(content.indexOf("#")+1, content.length()); //get URL from preset
      content.trim();
      _stationURL=content;
      if(preset>0) pref.putUInt("preset", preset);
      f_has_ST=false; // will probably be set in ShowStreamtitle
      return content;
}
String readnexthostfrompref(boolean updown){
      String content = "" ;    // Result of search
      int16_t preset;
      char    tkey[12] ;        // Key as an array of chars
      int16_t maxtry=0 ;        // Limit number of tries
      int16_t pos=0;
      _homepage="";
      preset=pref.getUInt("preset");
      do{
          if(updown==true){preset++; if(preset>pref.getUInt("maxstations")) preset=0;}
          else{preset--; if(preset<0)preset=pref.getUInt("maxstations");}
          sprintf(tkey, "preset_%03d", preset);
          content=pref.getString(tkey);
          pos=content.indexOf("#");
          if(pos>0){ //entry is not empty
              _stationname=content.substring(0, content.indexOf("#")); //get stationname from preset
              content=content.substring(content.indexOf("#")+1, content.length()); //get URL from preset
              content.trim();
              _stationURL=content;
          }
          else content="";
          maxtry++;
          if(maxtry==255) return"";
      }while(content == "" );
      pref.putUInt("preset", preset);
      f_has_ST=false; // will probably be set in ShowStreamtitle
      return content;
}
//**************************************************************************************************
//                                       L I S T M P 3 F I L E                                     *
//**************************************************************************************************
String listmp3file(const char * dirname="/mp3files", uint8_t levels=2, fs::FS &fs=SD){
    static String SD_outbuf="";            // Output buffer for cmdclient
    String filename;                       // Copy of filename for lowercase test
    uint8_t index=0;
    if(!f_SD_okay) return "";              // See if known card
    File root = fs.open(dirname);
    if(!root){log_e("Failed to open directory"); return ""; }
    if(!root.isDirectory()){log_e("Not a directory"); return "";}
    SD_outbuf="";
    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            if(levels){
                listmp3file(file.name(), levels -1, fs);
            }
        } else {
            //log_i("FILE: %s, SIZE: %i",file.name(), file.size());
            filename=file.name();
            filename.substring(filename.length()-4).toLowerCase();
            filename=filename.substring(1,filename.length()); // remove first '/'
            if(filename.endsWith(".mp3")){
                sprintf(sbuf,"%s\n",filename.c_str());
                if(index<10){_mp3Name[index]=sbuf; index++;}  //store the first 10 Names
                SD_outbuf+=ASCIItoUTF8(sbuf);}
        }
        file = root.openNextFile();
    }
    if(SD_outbuf=="") SD_outbuf+"\n"; //nothing found
    return SD_outbuf;
}
//**************************************************************************************************
//                               C O N N E C T   TO   W I F I                                      *
//**************************************************************************************************
bool connectToWiFi(){
    String ssid="", password="", WiFiName="";
    char tkey[12];
    int WiFiNr = WiFi.scanNetworks();   // WiFiNr: nr of scanned Networks
    if(WiFiNr==0) {                     // no WiFi networks can be found
        tft.fillScreen(TFT_BLACK);      // Clear screen
        tft.setTextSize(6);
        displayinfo("no WiFi networks found", 20, 220, TFT_YELLOW, 5);
        while(1){};                     // endless loop until reset
    }
    else {
        int k=0;
        for (int i=0; i<WiFiNr; ++i) {
            WiFiName=WiFi.SSID(i);
            for(int j=0; j<10; j++){
                sprintf(tkey, "wifi_%02d", j);
                ssid=pref.getString(tkey);
                if(ssid=="|") break;                                // no predefined SSID in list?
                password=ssid.substring(ssid.indexOf("|")+1);
                ssid=ssid.substring(0,ssid.indexOf("|"));
                if(ssid==WiFiName){                                 // available network found, try to connect
                    WiFi.begin(ssid.c_str(), password.c_str());     // Connect to selected SSID
                    k=0;
                    while((WiFi.status() != WL_CONNECTED)&&(k<10)){ // wait max 10 sec
                        log_i("Try WiFi %s", ssid.c_str());
                        delay(1000); k++;
                    }
                    if(WiFi.status()==WL_CONNECTED){ _SSID=ssid; return true;} // connection successful
                }
            }
        }
    }
    return false;  // can't connect to any network
}

//**************************************************************************************************
//                                           S E T U P                                             *
//**************************************************************************************************
void setup(){
    Serial.begin(115200); // For debug
    SPI.begin();    // Init SPI bus
    tft.begin();    // Init TFT interface
    SD.end();       // to recognise sd after reset correctly
    SD.begin(SD_CS, SPI, 16000000); // faster speed, after tft.begin() because GPIO_TP in TFT must be set first
    delay(100); // wait while SD is ready
    ir.begin();  // Init InfraredDecoder
    tft.setRotation(3); // Use landscape format
    tp.setRotation(3);
    tft.setUTF8encoder(true);
    pref.begin("ESP32_RADIO", false);
    setTFTbrightness(pref.getUInt("brightness"));
    f_SD_okay=(SD.cardType() != CARD_NONE); // See if known card
    if(pref.getString("ESP32_Radio") != "default") defaultsettings();  // first init
    if(f_SD_okay) tft.drawBmpFile(SD, "/ESP32_Radio.bmp", 0, 0); //Welcomescreen
    mp3.begin(); // Initialize VS1053 player
    _alarmdays=pref.getUInt("alarm_weekday");
    _alarmtime=pref.getString("alarm_time");
    setTFTbrightness(pref.getUInt("brightness"));  // 50% of maxbrigthness
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("ESP32Radio");
    delay(100);
    if(!connectToWiFi()){
        tft.fillScreen(TFT_BLACK);      // Clear screen
        tft.setTextSize(6);
        displayinfo("can't connect to WiFi, check Your credentials", 20, 220, TFT_YELLOW, 5);
        while(1){};                     // endless loop until reset
    }
    web.begin();
    f_rtc= rtc.begin();
    tft.fillScreen(TFT_BLACK); // Clear screen
    showHeadlineItem("** Internet radio **");
    sprintf(myIP, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
    _myIP=String(myIP);
    tft.setTextSize(1);
    displayinfo(myIP, _yFooter, _hFooter, TFT_MAGENTA, 165);
    tone();
    mp3.connecttohost(readhostfrompref(0)); //last used station
    startTimer();
}

//**************************************************************************************************
//                                      S E T T I N G S                                            *
//**************************************************************************************************
void getsettings(int8_t config=0){ //config=0 update index.html, config=1 update config.html
    String val="", content="", s="", u="";
    int i, j, ix, s_len=0;
    char tkey[12];
    char nr[8];
    for ( i = 1 ; i < (pref.getUInt("maxstations")+1) ; i++ ){ // max presets
        sprintf(tkey, "preset_%03d", i);
        sprintf(nr, "%03d - ", i);
        content=pref.getString(tkey);
        ix=content.indexOf("#");
        if(ix>0){
          u=content.substring(ix, content.length());
          s=content.substring(0,ix);
          s_len=s.length();
          for(j=0;j<s_len;j++){if(s[j]==0xC3) s_len--;} //count umlaut, reduce len
        }
        else {s=" "; u=" ";}

        if(config==0){
            content=String(nr) + s;
            val=String(tkey) + String("=") + String(content) + String("\n");
            web.reply(val, false);
        }
        if(config==1){
            for(j=s_len;j<39;j++) s+=String(" ");
            content=s+u;
            val=String(nr) +(String(content) + String("\n"));
            web.reply(val, false);
        }
    }
}
//**************************************************************************************************
inline uint8_t downvolume(){
    uint8_t vol; vol=pref.getUInt("volume");
    if(vol>0) {vol--; pref.putUInt("volume", vol); if(f_mute==false) mp3.setVolume(vol);}
    showHeadlineVolume(vol); return vol;}

inline uint8_t upvolume(){
    uint8_t vol; vol=pref.getUInt("volume");
    if(vol<21){vol++; pref.putUInt("volume", vol) ;if(f_mute==false) mp3.setVolume(vol);}
    showHeadlineVolume(vol); return vol;}

inline uint8_t getvolume(){return pref.getUInt("volume");}

inline void mute() {
    if(f_mute==false){f_mute=true; mp3.setVolume(0); showHeadlineVolume(0);}
    else {f_mute=false; mp3.setVolume(getvolume()); showHeadlineVolume(getvolume());}
    pref.putUInt("mute", f_mute);
}

inline void showVolumeBar(){uint16_t vol=tft.width()* pref.getUInt("volume")/21;
    tft.fillRect(0, _yVolBar, vol, _hVolBar, TFT_RED);
    tft.fillRect(vol+1, _yVolBar, tft.width()-vol+1, _hVolBar, TFT_GREEN);}

inline void showBrightness(){uint16_t br=tft.width()* pref.getUInt("brightness")/100;
    tft.fillRect(0, 140, br, 5, TFT_RED); tft.fillRect(br+1, 140, tft.width()-br+1, 5, TFT_GREEN);}

inline String StationsItems(){return (String(pref.getUInt("preset")) + " " + _stationURL + " " + _stationname);}

//**************************************************************************************************
//                                M E N U E / B U T T O N S                                        *
//**************************************************************************************************
void changeState(int state){
    if(!f_SD_okay) return;
    _state=static_cast<status>(state);
    switch(_state) {
    case RADIO:{
        showFooter(); showHeadlineItem("** Internet radio **");
        showStation(); showTitle(_title); break;
    }
    case RADIOico:{
        _pressBtn[0]="/btn/Button_Mute_Red.bmp";           _releaseBtn[0]="/btn/Button_Mute_Off_Green.bmp";
        _pressBtn[1]="/btn/Button_Volume_Down_Yellow.bmp"; _releaseBtn[1]="/btn/Button_Volume_Down_Blue.bmp";
        _pressBtn[2]="/btn/Button_Volume_Up_Yellow.bmp";   _releaseBtn[2]="/btn/Button_Volume_Up_Blue.bmp";
        _pressBtn[3]="/btn/Button_Previous_Yellow.bmp";    _releaseBtn[3]="/btn/Button_Previous_Green.bmp";
        _pressBtn[4]="/btn/Button_Next_Yellow.bmp";        _releaseBtn[4]="/btn/Button_Next_Green.bmp";
        clearTitle(); clearFooter(); showVolumeBar();
        break;}
    case RADIOmenue:{
        _pressBtn[0]="/btn/MP3_Yellow.bmp";                _releaseBtn[0]="/btn/MP3_Green.bmp";
        _pressBtn[1]="/btn/Clock_Yellow.bmp";              _releaseBtn[1]="/btn/Clock_Green.bmp";
        _pressBtn[2]="/btn/Radio_Yellow.bmp";              _releaseBtn[2]="/btn/Radio_Green.bmp";
        _pressBtn[3]="/btn/Bulb_Yellow.bmp";               _releaseBtn[3]="/btn/Bulb_Green.bmp";
        _pressBtn[4]="/btn/Black.bmp";                     _releaseBtn[4]="/btn/Black.bmp";
        clearTitle(); clearFooter();
        break;}
    case CLOCKico:{
        _pressBtn[0]="/btn/MP3_Yellow.bmp";                _releaseBtn[0]="/btn/MP3_Green.bmp";
        _pressBtn[1]="/btn/Bell_Yellow.bmp";               _releaseBtn[1]="/btn/Bell_Green.bmp";
        _pressBtn[2]="/btn/Radio_Yellow.bmp";              _releaseBtn[2]="/btn/Radio_Green.bmp";
        _pressBtn[3]="/btn/Black.bmp";                     _releaseBtn[3]="/btn/Black.bmp";
        _pressBtn[4]="/btn/Black.bmp";                     _releaseBtn[4]="/btn/Black.bmp";
        break;}
    case BRIGHTNESS:{
        _pressBtn[0]="/btn/Button_Left_Yellow.bmp";        _releaseBtn[0]="/btn/Button_Left_Blue.bmp";
        _pressBtn[1]="/btn/Button_Right_Yellow.bmp";       _releaseBtn[1]="/btn/Button_Right_Blue.bmp";
        _pressBtn[2]="/btn/Button_Ready_Yellow.bmp";       _releaseBtn[2]="/btn/Button_Ready_Blue.bmp";
        _pressBtn[3]="/btn/Black.bmp";                     _releaseBtn[3]="/btn/Black.bmp";
        _pressBtn[4]="/btn/Black.bmp";                     _releaseBtn[4]="/btn/Black.bmp";
        break;}
    case MP3PLAYER:{
        _pressBtn[0]="/btn/Radio_Yellow.bmp";              _releaseBtn[0]="/btn/Radio_Green.bmp";
        _pressBtn[1]="/btn/Button_Left_Yellow.bmp";        _releaseBtn[1]="/btn/Button_Left_Blue.bmp";
        _pressBtn[2]="/btn/Button_Right_Yellow.bmp";       _releaseBtn[2]="/btn/Button_Right_Blue.bmp";
        _pressBtn[3]="/btn/Button_Ready_Yellow.bmp";       _releaseBtn[3]="/btn/Button_Ready_Blue.bmp";
        _pressBtn[4]="/btn/Black.bmp";                     _releaseBtn[4]="/btn/Black.bmp";
        break;}
    case MP3PLAYERico:{
        _pressBtn[0]="/btn/Button_Mute_Red.bmp";           _releaseBtn[0]="/btn/Button_Mute_Off_Green.bmp";
        _pressBtn[1]="/btn/Button_Volume_Down_Yellow.bmp"; _releaseBtn[1]="/btn/Button_Volume_Down_Blue.bmp";
        _pressBtn[2]="/btn/Button_Volume_Up_Yellow.bmp";   _releaseBtn[2]="/btn/Button_Volume_Up_Blue.bmp";
        _pressBtn[3]="/btn/MP3_Yellow.bmp";                _releaseBtn[3]="/btn/MP3_Green.bmp";
        _pressBtn[4]="/btn/Radio_Yellow.bmp";              _releaseBtn[4]="/btn/Radio_Green.bmp";
        break;}
    case ALARM:{
        _pressBtn[0]="/btn/Button_Left_Yellow.bmp";        _releaseBtn[0]="/btn/Button_Left_Blue.bmp";
        _pressBtn[1]="/btn/Button_Right_Yellow.bmp";       _releaseBtn[1]="/btn/Button_Right_Blue.bmp";
        _pressBtn[2]="/btn/Button_Up_Yellow.bmp";          _releaseBtn[2]="/btn/Button_Up_Blue.bmp";
        _pressBtn[3]="/btn/Button_Down_Yellow.bmp";        _releaseBtn[3]="/btn/Button_Down_Blue.bmp";
        _pressBtn[4]="/btn/Button_Ready_Yellow.bmp";       _releaseBtn[4]="/btn/Button_Ready_Blue.bmp";
        break;}
    case CLOCK:{ break;}
    }
    if(_state!=RADIO && _state!=CLOCK){ // RADIO and CLOCK have no Buttons
        int j=0;
        if(_state==RADIOico || _state==MP3PLAYERico){  // show correct mute button
            if(f_mute==false) {tft.drawBmpFile(SD, _releaseBtn[0].c_str(), 0, _yBtn); mp3.loop();}
            else {tft.drawBmpFile(SD, _pressBtn[0].c_str(), 0, _yBtn); mp3.loop();}
            j=1;}
        for(int i=j; i<5; i++){tft.drawBmpFile(SD, _releaseBtn[i].c_str(), i*_wBtn, _yBtn); mp3.loop();}
    }
}
void changeBtn_pressed(uint8_t btnNr){
    if(_state!=RADIO && _state!=CLOCK) tft.drawBmpFile(SD, _pressBtn[btnNr].c_str(), btnNr*_wBtn , _yBtn);
}
void changeBtn_released(uint8_t btnNr){
    if(_state!=RADIO && _state!=CLOCK) tft.drawBmpFile(SD, _releaseBtn[btnNr].c_str(), btnNr*_wBtn , _yBtn);
}

void displayWeekdays(uint8_t ad, boolean showall=false){
    uint8_t i=0;
    static uint8_t d, old_d;
    d=ad; //alarmday
    for(i=0;i<7;i++){
        if((d & (1<<i))==(old_d & (1<<i))&&!showall) continue; //icon is alread displayed
        if(d & (1<<i)) sprintf(sbuf,"/day/%i_rt.bmp",i); // k<<i instead pow(2,i)
        else sprintf(sbuf,"/day/%i_gn.bmp",i);
        if(f_SD_okay) tft.drawBmpFile(SD, sbuf, 5+i*44, 0);
        mp3.loop();
    }
    old_d=ad;
}

void displayAlarmtime(int8_t xy=0, int8_t ud=0, boolean showall=false){
    uint8_t i=0, j[4]={5,77,173,245}, k[4]={0,1,3,4};
    uint8_t ch=0;
    static int8_t pos=0, oldpos=0;;
    static String oldt="";
    if(ud==1){
        ch=_alarmtime[k[pos]]; ch++;
        if(pos==0){if(_alarmtime[1]>51){if(ch==50) ch=48; _alarmtime[k[pos]]=ch;} //hour 0...1
                   else{if(ch==51) ch=48; _alarmtime[k[pos]]=ch;} //hour 0...2
        }
        if(pos==1){if(_alarmtime[0]=='2'){if(ch==52) ch=48; _alarmtime[k[pos]]=ch;} //hour*10 0...3
                   else{if(ch==58) ch=48; _alarmtime[k[pos]]=ch;} //hour*10 0...9
        }
        if(pos==2){if(ch==54) ch=48; _alarmtime[k[pos]]=ch;} //min 0...5
        if(pos==3){if(ch==58) ch=48; _alarmtime[k[pos]]=ch;} //min*10 0...9
    }
    if(ud==-1){
        ch=_alarmtime[k[pos]]; ch--;
        if(pos==0){if(_alarmtime[1]>51){if(ch==47) ch=49; _alarmtime[k[pos]]=ch;}//hour 1...0
                   else{if(ch==47) ch=50; _alarmtime[k[pos]]=ch;} //hour 2...0
        }
        if(pos==1){if(_alarmtime[0]=='2'){if(ch==47) ch=51; _alarmtime[k[pos]]=ch;} //hour*10 0...3
                   else{if(ch==47) ch=57; _alarmtime[k[pos]]=ch;} //hour*10 9...0
        }
        if(pos==2){if(ch==47) ch=53; _alarmtime[k[pos]]=ch;} //min 5...0
        if(pos==3){if(ch==47) ch=57; _alarmtime[k[pos]]=ch;} //min*10 9...0
        }

    if(xy==1) pos++; if(pos==4) pos=0; //pos only 0...3
    if(xy==-1)pos--; if(pos==-1)pos=3;

    if(showall==true){oldt="";
        if(f_SD_okay){tft.drawBmpFile(SD,"/digits/ert.bmp", 149, 45);mp3.loop();}}
    String at=_alarmtime;
    //log_i("at=%s",_alarmtime.c_str());
    if(pos!=oldpos){
        sprintf(sbuf,"/digits/%cor.bmp",at.charAt(k[pos]));
        if(f_SD_okay){tft.drawBmpFile(SD, sbuf, j[pos], 45);mp3.loop();}
        sprintf(sbuf,"/digits/%crt.bmp",at.charAt(k[oldpos]));
        if(f_SD_okay){tft.drawBmpFile(SD, sbuf, j[oldpos], 45);mp3.loop();}
    }
    for(i=0;i<4;i++){
        if(at[k[i]]!=oldt[k[i]]){
            if(i==pos){
                sprintf(sbuf,"/digits/%cor.bmp",at.charAt(k[i])); //show orange number
                if(f_SD_okay){tft.drawBmpFile(SD, sbuf, j[i], 45);mp3.loop();}
            }
            else{
                sprintf(sbuf,"/digits/%crt.bmp",at.charAt(k[i])); //show red numbers
                if(f_SD_okay){tft.drawBmpFile(SD, sbuf, j[i], 45);mp3.loop();}
            }
        }
    }
    oldt=at; oldpos=pos;
}

void display_time(boolean showall=false){ //show current time on the TFT Display
    static String t, oldt="";
    static boolean k=false;
    uint8_t i=0;
    uint16_t j=0;
    if(showall==true) oldt="";
    if((_state==CLOCK)||(_state==CLOCKico)){
        t=rtc.gettime_s();
        for(i=0;i<5;i++){
            if(t[i]==':'){if(k==false){k=true; t[i]='d';}else{t[i]='e'; k=false;}}
            if(t[i]!=oldt[i]){
                sprintf(sbuf,"/digits/%cgn.bmp",t[i]);
                if(f_SD_okay) tft.drawBmpFile(SD, sbuf, 5+j, 45);
                mp3.loop();
            }
            if((t[i]=='d')||(t[i]=='e'))j+=24; else j+=72;
        }
        oldt=t;}
}

//**************************************************************************************************
//                                           L O O P                                               *
//**************************************************************************************************
void loop() {
    static uint8_t sec=0;
    mp3.loop();
    web.loop();
    ir.loop();
    tp.loop();
    if(f_1sec==true){
        if(f_rtc==true){ // true -> rtc has the current time
            int8_t h=0;
            char tkey[20];
            _time_s=rtc.gettime_s();
            if(f_mute==false){
                if(_time_s.endsWith("59:51")) { // speech the time 9 sec before a new hour is arrived
                    _hour=_time_s.substring(0,2); // extract the hour
                    h=_hour.toInt();
                    h++;
                    if(h==24) h=0;
                    sprintf ( tkey, "/voice_time/%03d.mp3", h);
                    //Serial.println(tkey);
                    _timefile=3;
                    mp3.connecttoSD(tkey);
                }
            }
            showHeadlineTime();
        }
        display_time();
        if(f_has_ST==false) sec++; else sec=0; // Streamtitle==""?
        if(sec>4){
            sec=0;
            if(!ST_rep())showTitle("Station provides no Streamtitle");
        }
        if(_commercial_dur>0){
            _commercial_dur--;
            if(_commercial_dur==2)showTitle("");// end of commercial? clear streamtitle
        }
        f_1sec=false;
    }
    if(_alarmtime==rtc.gettime_xs()){ //is alarmtime
        if((_alarmdays>>rtc.getweekday())&1){ //is alarmday
            if(!semaphore) {f_alarm=true; f_mute=false; semaphore=true;} //set alarmflag
        }
    }
    else semaphore=false;

    if(_millis+5000<millis()){  //5sec no touch?
        if(_state==RADIOico)  {_state=RADIO; showTitle(_title); showFooter();}
        if(_state==RADIOmenue){_state=RADIO; showTitle(_title); showFooter();}
        if(_state==CLOCKico)  {_state=CLOCK; displayinfo("",160,79, TFT_BLACK, 0);}
    }

    if(f_alarm){
        f_alarm=false;
        mp3.connecttoSD("/ring/alarm_clock.mp3");
        mp3.setVolume(21);
    }

    if(f_mp3eof){
        if(_timefile>0){
            if(_timefile==1){mp3.connecttoSD("/voice_time/080.mp3"); _timefile--;}  // stroke
            if(_timefile==2){mp3.connecttoSD("/voice_time/200.mp3"); _timefile--;} // precisely
            if(_timefile==3){mp3.connecttoSD("/voice_time/O'clock.mp3"); _timefile--;}
        }
        else {
            _state=RADIO;
            showHeadlineItem("** Internet radio **");
            mp3.setVolume(pref.getUInt("volume"));
            mp3.connecttohost(readhostfrompref(0));
        }
        f_mp3eof=false;
    }
}

//**************************************************************************************************
//                                            E V E N T S                                          *
//**************************************************************************************************
//Events from vs1053_ext library
void vs1053_showstation(const char *info){              // called from vs1053
    //log_i("_station %s",info);
    _station=info;
    _title="";
    showStation();
}
void vs1053_showstreamtitle(const char *info){          // called from vs1053
    //log_i("showTitle %s", info);
    _title=info;
    if(_state==RADIO)showTitle(_title);                 //state RADIO
}
void vs1053_showstreaminfo(const char *info){           // called from vs1053
//    s_info=info;
//    tft.setTextSize(1);                               // host and port
//    displayinfo(s_info.c_str(), 167, 55, TFT_YELLOW); // show info at position 167
}
void vs1053_eof_mp3(const char *info){                  // end of mp3 file (filename)
    f_mp3eof=true;
//    Serial.print("vs1053_eof: ");
//    Serial.print(info);
}
void vs1053_bitrate(const char *br){
    _bitrate=br;
    if(_state==RADIO)showFooter();
}
void vs1053_info(const char *info) {                    // called from vs1053
    String str=info;
    Serial.print("vs1053_info: ");
    if((str.startsWith("Stream lost"))&&(f_rtc)) Serial.print(String(rtc.gettime())+" ");
    Serial.print(info);                                 // debug infos
}
void vs1053_commercial(const char *info){               // called from vs1053
    String str=info;                                    // info is the duration of advertising
    _commercial_dur=str.toInt();
    showTitle("Advertising "+str+"s");
}
void vs1053_icyurl(const char *info){                   // if the Radio has a homepage, this event is calling
    String str=info;
    if(str.length()>5){
        _homepage=String(info);
        if(!_homepage.startsWith("http")) _homepage="http://"+_homepage;
        Serial.print("Homepage:    ");
        Serial.println(info);
    }
}

//Events from tft library
void tft_info(const char *info){
    Serial.print("tft_info: ");
    Serial.print(info);
}
//Events from html library
void HTML_command(const String cmd){                    // called from html
    uint8_t vol;
    String  str;
    boolean f_tone=false;
    if(cmd=="homepage"){(web.reply(_homepage)); return;}
    if(cmd=="to_listen"){web.reply(StationsItems());} // return the name and number of the current station
    if(cmd=="settings"){getsettings(0); return;}
    if(cmd=="getprefs") {getsettings(1); return;}
    if(cmd=="getdefs"){defaultsettings(); getsettings(1); return;}
    if(cmd=="gettone"){ web.reply(tone()); return;}
    if(cmd=="test") {sprintf(sbuf, "free memory: %u, buffer filled: %d, available stream: %d\n", ESP.getFreeHeap(),mp3.ringused(), mp3.streamavail()); web.reply(sbuf); return;}
    if(cmd=="reset") {ESP.restart(); return;}
    if(cmd.startsWith("toneha=")){pref.putUInt("toneha",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Treble Gain set");f_tone=true;}
    if(cmd.startsWith("tonehf=")){pref.putUInt("tonehf",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Treble Freq set");f_tone=true;}
    if(cmd.startsWith("tonela=")){pref.putUInt("tonela",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Bass Gain set");f_tone=true;}
    if(cmd.startsWith("tonelf=")){pref.putUInt("tonelf",(cmd.substring(cmd.indexOf("=")+1).toInt()));web.reply("Bass Freq set");f_tone=true;}
    if(f_tone==true){f_tone=false; tone(); return;}
    if(cmd.startsWith("downvolume")){ str="Volume is now "; str.concat(downvolume()); web.reply(str); return;}
    if(cmd.startsWith("upvolume")){ str="Volume is now "; str.concat(vol=upvolume()); web.reply(str); return;}
    if(cmd.startsWith("mute")) {mute();web.reply("OK\n"); return;}
    if(cmd.startsWith("downpreset")){str=readnexthostfrompref(false); mp3.connecttohost(str); web.reply(StationsItems()); return;}
    if(cmd.startsWith("uppreset")){str=readnexthostfrompref(true); mp3.connecttohost(str); web.reply(StationsItems()); return;}
    if(cmd.startsWith("preset=")){ mp3.connecttohost(str=readhostfrompref(cmd.substring(cmd.indexOf("=")+1).toInt())); web.reply(StationsItems()); return;}
    if(cmd.startsWith("station=")){_stationname=""; mp3.connecttohost(cmd.substring(cmd.indexOf("=")+1));web.reply("OK\n"); return;}
    if(cmd.startsWith("getnetworks")){web.reply(_SSID+"\n"); return;}
    if(cmd.startsWith("saveprefs")){clearallpresets(); web.reply(""); return;} // after that starts POST Event "HTML_request"
    if(cmd.startsWith("mp3list")){web.reply(listmp3file()); return;}
    if(cmd.startsWith("mp3track=")){str=cmd; str.replace("mp3track=", "/"); mp3.connecttoSD(str); web.reply("OK\n"); return;}
    log_e("unknown HTMLcommand %s", cmd.c_str());
}
void HTML_file(const String file){                  // called from html
    //log_i("HTML_file %s", file.c_str());
    web.printhttpheader(file).c_str();
    if(file=="index.html") {web.show(web_html); return;}
    if(file=="favicon.ico"){web.streamfile(SD, "/favicon.ico"); return;}
    if(file.startsWith("SD")){web.streamfile(SD, (file.substring(2).c_str())); return;}
    if(file.startsWith("url=")){web.streamfile(SD,(decodeURL(file.substring(6).c_str()))); return;}
    log_e("unknown HTMLfile %s", file.c_str());
}
void HTML_request(const String request){
    String str1, str2, s, u ;
    int ix;
    //log_i("%s",request.c_str());
    if(request.indexOf(" -")==3){
        ix=request.indexOf("#");
        if(ix>6){
            s=request.substring(6,ix); s.trim();
            u=request.substring(ix+1, request.length()); u.trim();
        }
        else{s=" "; u=" ";}
        str1="preset_"+ request.substring(0,3);
        str2=s+String("#")+u;
        pref.putString(str1.c_str(), str2); return;
        return;
    }
    else {
        //log_e("unknown request: %s",request.c_str());
    }
}
void HTML_info(const char *info){                   // called from html
    Serial.print("HTML_info:   ");
    Serial.print(info);                             // for debug infos
}
// Events from IR Library
void ir_res(uint32_t res){
    while(res>pref.getUInt("maxstations")) res=res%100;
    if(_state==0)mp3.connecttohost(readhostfrompref(res));//state RADIO
}
void ir_number(const char* num){
    tft.setTextSize(7);
    if(_state==0) displayinfo(num, _yName, _hName +_hTitle, TFT_YELLOW, 100); //state RADIO
}
void ir_key(const char* key){
    switch(key[0]){
        case 'o': break; //ok
        case 'r': upvolume(); break; // right
        case 'l': downvolume(); break; // left
        case 'u': mp3.connecttohost(readnexthostfrompref(true)); break; // up
        case 'd': mp3.connecttohost(readnexthostfrompref(false)); break; // down
        case '#': mute();break; // #
        case '*': break; // *
        default: break;
        }
}

// Event from TouchPad
void tp_pressed(uint16_t x, uint16_t y){
    uint8_t yPos=255, y1Pos=255, d=0;
    _millis=millis();
    if(y<167){
        if(_state==RADIOico) changeState(RADIOmenue);
        if(_state==RADIO) changeState(RADIOico);
        if(_state==CLOCK) changeState(CLOCKico);
        if(_state==BRIGHTNESS){}
        if(y<40){
            switch(x){  //weekdays
                case   0 ...  48: y1Pos=0; break; //So
                case  49 ...  92: y1Pos=1; break; //Mo
                case  93 ... 136: y1Pos=2; break; //Tu
                case 137 ... 180: y1Pos=3; break; //We
                case 181 ... 224: y1Pos=4; break; //Th
                case 225 ... 268: y1Pos=5; break; //Fr
                case 269 ... 319: y1Pos=6; break;}//Sa
            }
    }
    else{
        switch(x){  // icons
            case   0 ...  63: yPos=0; break;
            case  64 ... 127: yPos=1; break;
            case 128 ... 191: yPos=2; break;
            case 192 ... 255: yPos=3; break;
            case 256 ... 319: yPos=4; break;
        }
        changeBtn_pressed(yPos);
    }
    if(_state==RADIOico){
        if(yPos==0){mute(); if(f_mute==false) changeBtn_released(yPos);}
        if(yPos==1){_releaseNr= 1; downvolume(); showVolumeBar();} // Vol-
        if(yPos==2){_releaseNr= 2; upvolume(); showVolumeBar();}   // Vol+
        if(yPos==3){_releaseNr= 3; mp3.connecttohost(readnexthostfrompref(false));}
        if(yPos==4){_releaseNr= 4; mp3.connecttohost(readnexthostfrompref(true));}
    }
    if(_state==RADIOmenue){
        if(yPos==0){_releaseNr= 5; mp3.stop_mp3client(); listmp3file();} // MP3
        if(yPos==1){_releaseNr= 6;} // Clock
        if(yPos==2){_releaseNr= 7;} // Radio
        if(yPos==3){_releaseNr=16;} // Brightness
    }
    if(_state==CLOCKico){
        if(yPos==0){_releaseNr= 5; listmp3file();} // MP3
        if(yPos==1){_releaseNr= 9;} // Bell
        if(yPos==2){_releaseNr= 7;} // Radio
    }
    if(_state==ALARM){
        if(yPos==0){_releaseNr=11;} // left
        if(yPos==1){_releaseNr=12;} // right
        if(yPos==2){_releaseNr=13;} // up
        if(yPos==3){_releaseNr=14;} // down
        if(yPos==4){_releaseNr=15;} // ready (return to CLOCK)

        if(y1Pos<7){d=(1<<y1Pos);
        if((_alarmdays & d))_alarmdays-=d; else _alarmdays+=d; displayWeekdays(_alarmdays);}
    }
    if(_state==BRIGHTNESS){
        if(yPos==0){_releaseNr=17;} // left
        if(yPos==1){_releaseNr=18;} // right
        if(yPos==2){_releaseNr= 7;} // ready (return to RADIO)
    }
    if(_state==MP3PLAYER){
        if(yPos==0){_releaseNr=7; mp3.connecttohost(readhostfrompref(-1));} // Radio
        if(yPos==1){_releaseNr=21;} // left
        if(yPos==2){_releaseNr=22;} // right
        if(yPos==3){_releaseNr=23;} // ready
    }
    if(_state==MP3PLAYERico){
        if(yPos==0){mute(); if(f_mute==false) changeBtn_released(yPos);}
        if(yPos==1){_releaseNr=1; downvolume(); showVolumeBar();} // Vol-
        if(yPos==2){_releaseNr=2; upvolume();   showVolumeBar();} // Vol+
        if(yPos==3){_releaseNr=26;} // MP3
        if(yPos==4){_releaseNr=7; mp3.connecttohost(readhostfrompref(-1));} // Radio
    }
}

void tp_released(){
    static String str="";
    switch(_releaseNr){
    case  1: changeBtn_released(1); break; // Vol-
    case  2: changeBtn_released(2); break; // Vol+
    case  3: changeBtn_released(3); break; // RADIO nextstation
    case  4: changeBtn_released(4); break; // RADIO previousstation
    case  5: tft.fillScreen(TFT_BLACK);
             showHeadlineItem("* MP3Player *");changeState(MP3PLAYER);
             tft.setTextSize(4); str=_mp3Name[_mp3Index];
             str=str.substring(str.lastIndexOf("/")+1, str.length()-5); //only filename, get rid of foldername(s) and suffix
             displayinfo(ASCIItoUTF8(str.c_str()), _yName, _hName, TFT_CYAN, 5); break; //MP3
    case  6: tft.fillScreen(TFT_BLACK); changeState(CLOCK);
             showHeadlineItem("** Wecker **"); display_time(true); break;//Clock
    case  7: changeState(RADIO); break;
    case  9: changeState(ALARM); showHeadlineItem("");
             displayWeekdays(_alarmdays, true);
             displayAlarmtime(0, 0, true); break;
    case 11: displayAlarmtime(-1);    changeBtn_released(0);  break;
    case 12: displayAlarmtime(+1);    changeBtn_released(1);  break;
    case 13: displayAlarmtime(0, +1); changeBtn_released(2);  break; // alarmtime up
    case 14: displayAlarmtime(0, -1); changeBtn_released(3);  break; // alarmtime down
    case 15: pref.putUInt("alarm_weekday", _alarmdays); // ready
             pref.putString("alarm_time", _alarmtime);
             tft.fillScreen(TFT_BLACK); changeState(CLOCK);
             showHeadlineItem("** Wecker **");
             display_time(true); break;//Clock
    case 16: tft.fillScreen(TFT_BLACK); changeState(BRIGHTNESS); showHeadlineItem("** Helligkeit **");
             showBrightness(); mp3.loop();
             tft.drawBmpFile(SD, "/Brightness.bmp",0, 21); break;
    case 17: changeBtn_released(0); downBrightness(); showBrightness(); break;
    case 18: changeBtn_released(1); upBrightness(); showBrightness(); break;
    case 21: changeBtn_released(1); _mp3Index--; if(_mp3Index==-1) _mp3Index=9;
             str=_mp3Name[_mp3Index];
             while(str.length()==0){_mp3Index--; str=_mp3Name[_mp3Index]; if(_mp3Index==0) break;}
             str=str.substring(str.lastIndexOf("/")+1, str.length()-5); //only filename, get rid of foldername(s) and suffix
             tft.setTextSize(4);
             displayinfo(ASCIItoUTF8(str.c_str()), _yName, _hName, TFT_CYAN, 5);
             break; // left file--
    case 22: changeBtn_released(2); _mp3Index++; if(_mp3Index>9) _mp3Index=0;
             str=_mp3Name[_mp3Index];
             if(str.length()==0){_mp3Index=0; str=_mp3Name[_mp3Index];}
             str=str.substring(str.lastIndexOf("/")+1, str.length()-5); //only filename, get rid of foldername(s) and suffix
             tft.setTextSize(4);
             displayinfo(ASCIItoUTF8(str.c_str()), _yName, _hName, TFT_CYAN, 5);
             break; // right file++
    case 23: changeState(MP3PLAYERico); showVolumeBar();
             mp3.connecttoSD("/"+_mp3Name[_mp3Index]); break; // play mp3file
    case 26: clearTitle(); changeState(MP3PLAYER); break;
    }
    _releaseNr=0;
}
