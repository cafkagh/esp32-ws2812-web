#include "esp32_digital_led_lib.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include "Regexp.h"
#include <String.h>

WiFiMulti wifiMulti;

const char* ap_ssid     = "ESP32";
const char* ap_password = "147258369";

const char* sta_ssid     = "Parasite";
const char* sta_password = "147258369";

QueueHandle_t queue;

WiFiServer server(80);

String header;

MatchState ms;

// 0 关 1开
int state = 0;
// 1彩虹 2滚动彩虹
int type = 1;
int lednum = 60;
int delays = 20;
int brightness = 64;

int reset = 0;

unsigned long int color = 0;

void http_server(void *client);


#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

void espPinMode(int pinNum, int pinDir) {
	if (pinNum == 32 || pinNum == 33) {
		uint64_t gpioBitMask = (pinNum == 32) ? 1ULL<<GPIO_NUM_32 : 1ULL<<GPIO_NUM_33;
		gpio_mode_t gpioMode = (pinDir == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
		gpio_config_t io_conf;
		io_conf.intr_type = GPIO_INTR_DISABLE;
		io_conf.mode = gpioMode;
		io_conf.pin_bit_mask = gpioBitMask;
		io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
		gpio_config(&io_conf);
	} else pinMode(pinNum, pinDir);
}

void gpioSetup(int gpioNum, int gpioMode, int gpioVal) {
  #if defined(ARDUINO) && ARDUINO >= 100
    espPinMode(gpioNum, gpioMode);
    digitalWrite (gpioNum, gpioVal);
  #elif defined(ESP_PLATFORM)
    gpio_num_t gpioNumNative = static_cast<gpio_num_t>(gpioNum);
    gpio_mode_t gpioModeNative = static_cast<gpio_mode_t>(gpioMode);
    gpio_pad_select_gpio(gpioNumNative);
    gpio_set_direction(gpioNumNative, gpioModeNative);
    gpio_set_level(gpioNumNative, gpioVal);
  #endif
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"  // It's noisy here with `-Wall`
strand_t strand = {.rmtChannel = 0, .gpioNum = 27, .ledType = LED_WS2812B_V2, .brightLimit = 64, .numPixels = 240};
strand_t * STRANDS [] = { &strand };
int STRANDCNT = COUNT_OF(STRANDS); 
#pragma GCC diagnostic pop


void setup(){

    Serial.begin(115200);

    digitalLeds_initDriver();
    // Init unused outputs low to reduce noise
    gpioSetup(14, OUTPUT, LOW);
    gpioSetup(15, OUTPUT, LOW);
    gpioSetup(26, OUTPUT, LOW);
    gpioSetup(27, OUTPUT, LOW);

    gpioSetup(strand.gpioNum, OUTPUT, LOW);
    digitalLeds_addStrands(STRANDS, STRANDCNT);
    digitalLeds_initDriver();
    digitalLeds_resetPixels(STRANDS, STRANDCNT);
    
    Serial.print("Setting AP (Access Point)…");
    WiFi.softAP(ap_ssid, ap_password);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    wifiMulti.addAP(sta_ssid, sta_password);
    Serial.println("Connecting Wifi...");
    if(wifiMulti.run() == WL_CONNECTED) {
        Serial.println("");
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
    }
    server.begin();

	xTaskCreatePinnedToCore(http_server,  "http_server",  2048, NULL, 1 ,  NULL, 1);

	colorWipe(pixelFromRGB(255, 0, 0),lednum);
	colorWipe(pixelFromRGB(0, 255, 0),lednum);
	colorWipe(pixelFromRGB(0, 0, 255),lednum);
}


void loop(){
	if(reset==1){
		reset=0;
		digitalLeds_resetPixels(STRANDS, STRANDCNT);
	}
	rainbowCycle(lednum);
}

void colorWipe(pixelColor_t c,int lednums) {
	for(uint16_t i=0; i<lednums; i++) {
		digitalLeds_resetPixels(STRANDS, STRANDCNT);
		STRANDS[0]->pixels[i] = c;
    	digitalLeds_drawPixels(STRANDS, STRANDCNT);
		delay(20);
	}
}

void rainbowCycle(int lednums) {
    uint16_t i, j;
	for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
		if(state==1){
			if(color==0){
				for(i=0; i< lednums; i++) {
					if(type==1){
						STRANDS[0]->pixels[i] = Wheel((i+j) & 255);
					}else{
						STRANDS[0]->pixels[i] = Wheel(((i * 256 / lednums) + j) & 255);
					}
				}
			}else{
				int blue = (char)color;
				int green = (char)(color >> 8);
				int red = (char)(color >> 16);
				for(i=0; i< lednums; i++) {
					STRANDS[0]->pixels[i] =  pixelFromRGB(red, green, blue);
				}
			}
		}else{
			for(i=0; i< lednums; i++) {
				STRANDS[0]->pixels[i] =  pixelFromRGBL(0, 0, 0, 0);
			}
		}
		digitalLeds_drawPixels(STRANDS, STRANDCNT);
		delay(delays);
	}
}

pixelColor_t Wheel(byte WheelPos) {
    WheelPos = 255 - WheelPos;
    if(WheelPos < 85) {
		return pixelFromRGBL(255 - WheelPos * 3, 0, WheelPos * 3, brightness);
    }
    if(WheelPos < 170) {
        WheelPos -= 85;
		return pixelFromRGBL(0, WheelPos * 3, 255 - WheelPos * 3, brightness);

    }
    WheelPos -= 170;
	return pixelFromRGBL(WheelPos * 3, 255 - WheelPos * 3, 0, brightness);

}


void http_server(void *client)
{
	for (;;)
	{
		WiFiClient client = server.available();
  		if (client) {
			Serial.println("New Client");
			String currentLine = "";
			while (client.connected()) {
      			if (client.available()) {
					char c = client.read();
					// Serial.write(c);
					header += c;
        			if (c == '\n') {
						if (currentLine.length() == 0) {
							client.println("HTTP/1.1 200 OK");
							client.println("Content-type:text/html");
							client.println("Connection: close");
							client.println();


							char *buf = const_cast<char *>(header.c_str()) ;
							ms.Target(buf); 
							

							char result = ms.Match("GET /(%a+)/(%d+)", 0);

							if(result > 0){
								String key = ms.GetCapture(buf, 0);
								uint value = atol(ms.GetCapture(buf, 1));

								Serial.println("-----");
								Serial.println(key);
								Serial.println(value);
								Serial.println("-----");

								if(key == "color"){
									color = value;
								}else{
									color = 0;

									if(key == "state"){
										state = value;
									}else if(key == "type"){
										type = value;
									}else if(key == "lednum"){
										lednum = value;
										reset=1;
										if(lednum>240){
											lednum=240;
										}
									}else if(key == "delays"){
										delays = value;
									}else if(key == "brightness"){
										brightness = value;
										if(brightness>255){
											brightness=255;
										}
									}else{

									}
								}
							}
							
							client.println("<!DOCTYPE html>\
							<html>\
							<head>\
								<meta name='viewport' content='width=device-width, initial-scale=1'>\
								<meta charset='UTF-8'>\
								<link rel='icon' href='data:,'>\
								<script src='https://cdn.bootcss.com/jquery/3.4.1/jquery.min.js'></script>\
								<link rel='stylesheet' href='https://stackpath.bootstrapcdn.com/bootstrap/4.4.1/css/bootstrap.min.css'>\
							</head>\
							<body>\
								<div style='text-align:center' class='container col-lg-3'>\
									<h2>WS2812 彩虹流水灯</h2>\
									<p>&nbsp;</p>\
									<p>状态</p>\
							");

							if (state==1) {
								client.println("<p><a href='/state/0' class='btn btn-success state col-md-1 col-lg-10'>开</a></p>");
							} else {
								client.println("<p><a href='/state/1' class='btn btn-danger state col-md-1 col-lg-10'>关</a></p>");
							}

							client.println("<p>&nbsp;</p><p>彩虹</p>");

							if (type==2) {
								client.println("<p><a href='/type/1' class='btn btn-primary type col-md-1 col-lg-10'>滚动</a></p>");
							} else {
								client.println("<p><a href='/type/2' class='btn btn-info type col-md-1 col-lg-10'>渐变</a></p>");
							}

							client.printf("<p>&nbsp;</p>\
							<p style='display: block;height: 60px;'>\
								<span class='col-3 col-lg-3' style='display: block;float: left;line-height: 40px;text-align: right;'>延迟：</span>\
								<input class='form-control col-md-1 col-lg-5' type='number' id='delays' style='width:120px;float: left;margin: 0 20px' value='%d'>\
								<a href='javascript:;' class='btn btn-info delays-submit' style='float: left;'>确定</a>\
							</p>\
							",delays);

							client.printf("<p style='display: block;height: 60px;'>\
								<span class='col-3 col-lg-3' style='display: block;float: left;line-height: 40px;text-align: right;'>亮度：</span>\
								<input class='form-control col-md-1 col-lg-5' type='number' id='brightness' style='width:120px;float: left;margin: 0 20px' value='%d'>\
								<a href='javascript:;' class='btn btn-info brightness-submit' style='float: left;'>确定</a>\
							</p>\
							",brightness);

							client.printf("<p style='display: block;height: 60px;'>\
								<span class='col-3 col-lg-3' style='display: block;float: left;line-height: 40px;text-align: right;'>数量：</span>\
								<input class='form-control col-md-1 col-lg-5' type='number' id='lednum' style='width:120px;float: left;margin: 0 20px' value='%d'>\
								<a href='javascript:;' class='btn btn-info lednum-submit' style='float: left;'>确定</a>\
							</p>\
							",lednum);

							client.printf("<p style='display: block;height: 60px;'>\
								<span class='col-3 col-lg-3' style='display: block;float: left;line-height: 40px;text-align: right;'>颜色：</span>\
								<input class='form-control col-md-1 col-lg-5' type='color' id='color' style='width:120px;float: left;margin: 0 20px' value='#%06x'>\
								<a href='javascript:;' class='btn btn-info color-submit' style='float: left;'>确定</a>\
							</p>\
							",color);

							client.println("</div>\
								<script>\
									$('.delays-submit').click(function() {\
										window.location.href='/delays/'+$('#delays').val();\
									});\
									$('.brightness-submit').click(function() {\
										window.location.href='/brightness/'+$('#brightness').val();\
									});\
									$('.lednum-submit').click(function() {\
										window.location.href='/lednum/'+$('#lednum').val();\
									});\
									$('.color-submit').click(function() {\
										color = $('#color').val().replace('#','');\
										window.location.href='/color/'+parseInt(color,16);\
									});\
								</script>\
							</body>\
							</html>\
							");
							client.println();
							break;
						} else {
            				currentLine = "";
          				}
        			} else if (c != '\r') {  
          				currentLine += c;
        			}
      			}
			}
    		header = "";
			client.stop();
			Serial.println("Client disconnected.");
			Serial.println("");
  		}
	}
}