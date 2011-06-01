#ifndef __GPIO_PWM_H__
#define __GPIO_PWM_H__

struct gpio_wave_platform_data {
	unsigned int gpio;	//the pin use to exert spuare wave
	int Htime;	//spuare wave Hight width
	int Ltime;	//spuare wave Low width
	int Dvalue;	//gpio default value 
};

#endif
