/*
o* Driver for MT9M001 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/videodev2.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/log2.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/circ_buf.h>
#include <linux/miscdevice.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>
#include <media/soc_camera.h>
#include <plat/rk_camera.h>

static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

#define SENSOR_TR(format, ...) printk(KERN_ERR format, ## __VA_ARGS__)
#define SENSOR_DG(format, ...) dprintk(1, format, ## __VA_ARGS__)


#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#define MIN(x,y)   ((x<y) ? x: y)
#define MAX(x,y)    ((x>y) ? x: y)

/* Sensor Driver Configuration */
#define SENSOR_NAME sid130B
#define SENSOR_V4L2_IDENT V4L2_IDENT_SID130B
#define SENSOR_ID 0x1B
#define SENSOR_MIN_WIDTH    176
#define SENSOR_MIN_HEIGHT   144
#define SENSOR_MAX_WIDTH    1600
#define SENSOR_MAX_HEIGHT   1200
#define SENSOR_INIT_WIDTH	800			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  600
#define SENSOR_INIT_WINSEQADR sensor_svga
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_UYVY8_2X8

#define CONFIG_SENSOR_WhiteBalance 1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         0
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Focus         0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         1
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0

#define CONFIG_SENSOR_I2C_SPEED     80000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0

#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |\
                          SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_LOW|\
                          SOCAM_DATA_ACTIVE_HIGH | SOCAM_DATAWIDTH_8  |SOCAM_MCLK_24MHZ)

#define COLOR_TEMPERATURE_CLOUDY_DN  6500
#define COLOR_TEMPERATURE_CLOUDY_UP    8000
#define COLOR_TEMPERATURE_CLEARDAY_DN  5000
#define COLOR_TEMPERATURE_CLEARDAY_UP    6500
#define COLOR_TEMPERATURE_OFFICE_DN     3500
#define COLOR_TEMPERATURE_OFFICE_UP     5000
#define COLOR_TEMPERATURE_HOME_DN       2500
#define COLOR_TEMPERATURE_HOME_UP       3500

#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SENSOR_AF_IS_ERR    (0x00<<0)
#define SENSOR_AF_IS_OK		(0x01<<0)
#define SENSOR_INIT_IS_ERR   (0x00<<28)
#define SENSOR_INIT_IS_OK    (0x01<<28)

struct reginfo
{
    u8 reg;
    u8 val;
};

//flash off in fixed time to prevent from too hot , zyc
struct  flash_timer{
    struct soc_camera_device *icd;
	struct hrtimer timer;
};
static enum hrtimer_restart flash_off_func(struct hrtimer *timer);

static struct  flash_timer flash_off_timer;
//for user defined if user want to customize the series , zyc
#ifdef CONFIG_SID130B_USER_DEFINED_SERIES
#include "sid130B_user_series.c"
#else
/* init 352X288 SVGA */
static struct reginfo sensor_init_data[] =
{
#if 0
	   {0x00,  0x00},  
	{0x04,  0x00},  //Group A 0x10
	{0x05,  0x0F},  //UXGA Output
	{0x06,  0x86}, 
	{0x07,  0x08}, 


	{0x08,  0xa2},  //PLL off
	{0x09,  0x12},  
	{0x0A,  0x12},  
	{0x10,  0x17},  
	{0x11,  0x01}, 
	{0x12,  0x8A}, 
	{0x13,  0x1a}, 
	{0x14,  0x27}, 
	{0x15,  0x22}, 

	{0x17,  0xCb},  
	{0x18,  0x38},  
	
	{0x40,  0x0f}, 
	{0x41,  0x17}, 
	{0x42,  0x52}, 
	{0x43,  0x80}, 
	{0x44,  0x00}, 
	{0x45,  0x00}, 

//Flicker - 50Hz  - Still mode ,     
	{0x00,  0x00}, 
	{0x20,  0x00}, 
	{0x21,  0x02}, 
	{0x23,  0x29},//15}, 
	{0x00,  0x01}, 
	{0x35,  0x50},//3c}, 

//Flicker - 50Hz - Preview mode 24MHz MCLK
	{0x00,  0x00}, 
	{0x24,  0x00}, 
	{0x25,  0xe6},//10}, 
	{0x27,  0x0d},//4b}, 
	{0x00,  0x01}, 
	{0x34,  0x80},//64}, 


//AE Block 
	{0x00,  0x01}, 
	{0x10,  0x00}, 
	{0x11,  0x0a}, //.0c
	{0x12,  0x78}, 
	{0x13,  0x78}, 
	{0x14,  0x78}, 
	{0x17,  0xC4}, 
	{0x1c,  0x05},    

	{0x36,  0x28},  //26

	{0x40,  0x40},  //Max Again  //anyuan
	//{0x41,  0x20}, 
	//{0x42,  0x20}, 
	//{0x43,  0x00}, 
	//{0x44,  0x00}, 
	//{0x45,  0x01}, 
	//{0x46,  0x1c}, 
	//{0x47,  0x11}, 
	//{0x48,  0x15}, 
	//{0x49,  0x17}, 
	//{0x4A,  0x1a}, 
	//{0x4B,  0x1c}, 
	//{0x4C,  0x1e}, 
	//{0x4D,  0x1e}, 
	//{0x4E,  0x0f}, 
	//{0x4F,  0x09}, 
	//{0x50,  0x07}, 
	//{0x51,  0x05}, 
	//{0x52,  0x04}, 
	//{0x53,  0x03}, 
	//{0x54,  0x02}, 
	//{0x55,  0x01}, 
	//{0x66,  0x00}, 
	//{0x67,  0x00}, 
	//{0x68,  0x00}, 
	//{0x69,  0x00}, 
	//{0x6a,  0x00}, 
	//{0x6b,  0x00},

	{0x70,  0xc4}, 
	{0x73,  0x22}, 
	{0x74,  0x07}, 
	{0x77,  0xd0}, 
	{0x78,  0xd8}, 
	{0x79,  0x70}, 
  //
	{0x90,  0x00}, 
	{0x92,  0x0c}, //0bank 0x11 과 동일 
	{0x95,  0x40}, 	//0 bank 0x40 과 동일  
	//AWB Block

	{0x00,  0x02}, 
	{0x10,  0xD3}, 
	{0x11,  0x11}, 
	{0x13,  0x7e}, 
	{0x14,  0x7c}, 
	{0x15,  0xee}, 
	{0x16,  0x80}, 
	{0x17,  0xd0}, 
	{0x18,  0x80}, 
	{0x19,  0x98}, 
	{0x1A,  0x68}, 
	{0x1B,  0x98}, 
	{0x1C,  0x68}, 
	{0x1D,  0x90}, 
	{0x1E,  0x74}, 
	{0x20,  0xF0}, 
	{0x21,  0x85}, 
	{0x22,  0xB4}, 
	{0x23,  0x20}, 
	{0x25,  0x20}, 
	{0x26,  0x05}, 
	{0x27,  0x78}, 
	{0x28,  0xd8}, 
	{0x29,  0xb8}, 
	{0x2A,  0x88}, 
	{0x30,  0x00}, 
	{0x31,  0x10}, 
	{0x32,  0x00}, 
	{0x33,  0x10}, 
	{0x34,  0x06}, 
	{0x35,  0x30}, 
	{0x36,  0x04}, 
	{0x37,  0xA0}, 
	{0x40,  0x01}, 
	{0x41,  0x04}, 
	{0x42,  0x08}, 
	{0x43,  0x10}, 
	{0x44,  0x13}, 
	{0x45,  0x6B}, 
	{0x46,  0x82},	



//CMA change  -D65~A
	{0x53,  0xa1},  //AWB R Gain for D30 to D20
	{0x54,  0xc0},  //AWB B Gain for D30 to D20
	{0x55,  0xa1},  //AWB R Gain for D20 to D30
	{0x56,  0xc0},  //AWB B Gain for D20 to D30
	{0x57,  0xc8},  //AWB R Gain for D65 to D30
	{0x58,  0xa0},  //AWB B Gain for D65 to D30
	{0x59,  0xc8},  //AWB R Gain for D30 to D65
	{0x5A,  0xa0},  //AWB B Gain for D30 to D65

  {0x64,  0x00},
  {0x65,  0x00},
  {0x66,  0x00},
  {0x67,  0x00},
  {0x68,  0xa5},
  {0x69,  0xb4},
  {0x6a,  0xb3},
  {0x6b,  0xac},
  {0x6c,  0xb7},
  {0x6d,  0x98},
  {0x6e,  0xba},
  {0x6f,  0x90},
  
  {0x70,  0xbf},
  {0x71,  0x9b},
  {0x72,  0xce},
  {0x73,  0x8c},
  {0x74,  0x7f},
  {0x75,  0x8c},
  {0x76,  0xad},
  {0x77,  0xba},
  {0x78,  0x8f},
  {0x79,  0x9a},
  {0x7A,  0xa3},
  {0x7B,  0xac},
  {0x7C,  0xa0},
  {0x7D,  0xa9},
  {0x7E,  0x95},
  {0x7F,  0xac},
  {0x80,  0xad},
  {0x81,  0xbc},
  {0x82,  0x98},
  {0x83,  0xa4},
  {0x84,  0x00},
  {0x85,  0x00},
  {0x86,  0x00},
  {0x87,  0x00},
  {0x88,  0xc9},
  {0x89,  0xd5},
  {0x8a,  0x70},
  {0x8b,  0x7b},
  {0x8c,  0xd0},
  {0x8d,  0xe5},
  {0x8e,  0x58},
  {0x8f,  0x70},
    
	{0xB4,  0x05},  
	{0xB5,  0x0F},  
	{0xB6,  0x06},  
	{0xB7,  0x06},  
	{0xB8,  0x40},  
	{0xB9,  0x10},  
	{0xBA,  0x06},  

//IDP
	{0x00,  0x03}, 
	{0x10,  0xFF}, 
	{0x11,  0x1D}, 
	{0x12,  0x1D}, 
	{0x13,  0xFF}, 
	{0x14,  0x00}, 
	{0x15,  0xc0}, 

//DPC
	{0x30,  0x88},  //DPCNRCTRL
	{0x31,  0x14},  //DPTHR @ AGAIN = 00
	{0x32,  0x10},  //DPTHR @ AGAIN = 20
	{0x33,  0x0c},  //DPTHR @ AGAIN = 40
	{0x34,  0x08},  //DPTHR @ AGAIN = 60
	{0x35,  0x04},  //DPTHR @ AGAIN = 80
	{0x36,  0x44},  //DPTHVRNG
	{0x37,  0x66},  //DPNUMBER
	{0x38,  0x00},  //0x00	// NRTHR0 @ AGAIN = 00
	{0x39,  0x04},  //0x0C	// NRTHR1 @ AGAIN = 20
	{0x3A,  0x04},  //0x18	// NRTHR2 @ AGAIN = 40
	{0x3B,  0x2c},  //0x30	// NRTHR3 @ AGAIN = 60
	{0x3C,  0x3c},  //		   NRTHR4 @ AGAIN = 80
	{0x3D,  0x04},  //NRTHVRNG0 @ AGAIN = 00
	{0x3E,  0x04},  //NRTHVRNG1 @ AGAIN = 20
	{0x3F,  0x04},  //NRTHVRNG2 @ AGAIN = 40
	{0x40,  0x2c},  //NRTHVRNG3 @ AGAIN = 60
	{0x41,  0x3c},  //NRTHVRNG4 @ AGAIN = 80
	{0x42,  0xff},  //NRTHVRNGMAX
	{0x43,  0x40},  //NRTHRWGT
	{0x44,  0x40},  //BASELVL
	{0x45,  0x06},  //SHUMAXH
	{0x46,  0x40},  //SHUMAXL
	{0x47,  0x30},  //ILLUMITHDRK

// shading
	{0x50,  0x0a},	//0x00      
	{0x51,  0x20},	//0x45  
	{0x52,  0x12},	//0x24      
	{0x53,  0x12},	//0x24 
                                      
	{0x54,  0x22},	//0x00      
	{0x55,  0x18},	//0x40  
	{0x56,  0x08},	//0x10      
	{0x57,  0x10},	//0x20 
                                        
	{0x58,  0x22},	//0x00      
	{0x59,  0x18},	//0x40  
	{0x5A,  0x08},	//0x10      
	{0x5B,  0x10},	//0x20 
                                       
	{0x5C,  0x25},	//0x04      
	{0x5D,  0x1a},	//0x55  
	{0x5E,  0x12},	//0x25    
	{0x5F,  0x10},	//0x20 
             
	{0x60,  0x32},	//0x32                   
	{0x61,  0x20},  //20//
	{0x62,  0x58},	//67
//	{0x63,  0x89 //8c R Center start gain
//	{0x66,  0x79 //B Center start gain   73               
	{0x6B,  0x00}, //01 //01                 
	{0x6C,  0x01}, //22                   
	{0x6D,  0x23}, //22                   
	{0x6E,  0x55}, //55                   
	{0x6F,  0x55}, //77                   
	{0x70,  0x55}, //65                   
	{0x71,  0x00}, //01 //AB (6)               
	{0x72,  0x01}, //23     (5)             
	{0x73,  0x23}, //33     (4)            
	{0x74,  0x44}, //45     (3)             
	{0x75,  0x45}, //55     (2)               
	{0x76,  0x66}, //55     (1)             
	{0x77,  0x00}, //01 //AB (6)                 
	{0x78,  0x01}, //23     (5)              
	{0x79,  0x23}, //33     (4)              
	{0x7A,  0x44}, //45     (3)              
	{0x7B,  0x45}, //55     (2)                
	{0x7C,  0x66}, //55     (1)              
	{0x7D,  0x00}, //00    //00                   
	{0x7E,  0x00}, //00    //00                   
	{0x7F,  0x12}, //11    //12                   
	{0x80,  0x33}, //33    //33 //44               
	{0x81,  0x44}, //33    //33 //44                  
	{0x82,  0x55}, //22    //44 //45 
	//{0x83,  0x14}, 
	//{0x84,  0x0f}, 

	{0x94,0x02},
	{0x95,0x80},
	{0x96,0x01},
	{0x97,0xe0}, 

//Interpolation
	{0xA0,  0x3F},
	{0xA1,  0x05},
	{0xA2,  0xB7},
	{0xA3,  0xB7},
	{0xA4,  0x04},
	{0xA5,  0xFF},
	{0xA6,  0x04},
	{0xA7,  0xFF},
	{0xA8,  0x00},
	{0xA9,  0x00},
	{0xAA,  0x00},
	{0xAB,  0x00},
	{0xAC,  0x60},
	{0xAD,  0x18},
	{0xAE,  0x10},
	{0xAF,  0x20},
	{0xB0,  0x08},
	{0xB1,  0x00},

//Color Matrix for D65
#if 0
	{0xC0,  0x2F}, // CMASB D20 or D30 or Dark Condition Color Matrix Selection
	{0xC1,  0x66}, 
	{0xC2,  0xd4}, 
	{0xC3,  0x05}, 
	{0xC4,  0xf0}, 
	{0xC5,  0x5a}, 
	{0xC6,  0xf5}, 
	{0xC7,  0xf9}, 
	{0xC8,  0xbf}, 
	{0xC9,  0x88}, 
	{0xCA,  0xa0}, 
	{0xCB,  0x50}, 
	{0xCC,  0xe2}, 
	{0xCD,  0x00}, 
	{0xCE,  0x00}, 
#else
{0xC0,  0x2F}, // CMASB D20 or D30 or Dark Condition Color Matrix Selection
{0xC1,  0x71}, 
{0xC2,  0xcc}, 
{0xC3,  0x01}, 
{0xC4,  0xe8}, 
{0xC5,  0x68}, 
{0xC6,  0xef}, 
{0xC7,  0xfa}, 
{0xC8,  0xc6}, 
{0xC9,  0x7e}, 
{0xCA,  0xec}, 
{0xCB,  0x67}, 
{0xCC,  0x34}, 
{0xCD,  0x3a}, 
{0xCE,  0x08},

#endif
//Color Matrix for CWF
	{0xD0,  0x2F}, 
	{0xD1,  0x66}, 
	{0xD2,  0xd4}, 
	{0xD3,  0x05}, 
	{0xD4,  0xf0}, 
	{0xD5,  0x5a}, 
	{0xD6,  0xf5}, 
	{0xD7,  0xe9}, 
	{0xD8,  0xbf}, 
	{0xD9,  0x88},
	{0xDA,  0xa0}, 
	{0xDB,  0x50}, 
	{0xDC,  0xe2}, 
	{0xDD,  0x00}, 
	{0xDE,  0x00}, 

//Color Matrix for A
	{0xE0,  0x2F}, 
	{0xE1,  0x6e}, 
	{0xE2,  0xc7}, 
	{0xE3,  0x0d}, 
	{0xE4,  0xe3}, 
	{0xE5,  0x61}, 
	{0xE6,  0xfa}, 
	{0xE7,  0xe5}, 
	{0xE8,  0xcb}, 
	{0xE9,  0x8e}, 
	{0xEA,  0xc4}, 
	{0xEB,  0x04}, 
	{0xEC,  0xf3}, 
	{0xED,  0xdd}, 
	{0xEE,  0x06}, 

//IDP 4
	{0x00,  0x04}, 

//Gamma - r   
	{0x10,  0x00}, 
	{0x11,  0x04}, 
	{0x12,  0x10},
	{0x13,  0x20},
	{0x14,  0x40},
	{0x15,  0x5c},
	{0x16,  0x74},
	{0x17,  0x84},
	{0x18,  0x98},
	{0x19,  0xa4},
	{0x1A,  0xb0},
	{0x1B,  0xc8},
	{0x1C,  0xdc},
	{0x1D,  0xf0},
	{0x1E,  0xf8},
	{0x1F,  0xFF},
                        
//Gamma - G             
	{0x20,  0x00}, 
	{0x21,  0x04}, 
	{0x22,  0x10},
	{0x23,  0x20},
	{0x24,  0x40},
	{0x25,  0x5c},
	{0x26,  0x74},
	{0x27,  0x84},
	{0x28,  0x98},
	{0x29,  0xa4},
	{0x2A,  0xb0},
	{0x2B,  0xc8},
	{0x2C,  0xdc},
	{0x2D,  0xf0},
	{0x2E,  0xf8},
	{0x2F,  0xFF},
                        
//Gamma - B             
	{0x30,  0x00}, 
	{0x31,  0x04}, 
	{0x32,  0x10},
	{0x33,  0x20},
	{0x34,  0x40},
	{0x35,  0x5c},
	{0x36,  0x74},
	{0x37,  0x84},
	{0x38,  0x98},
	{0x39,  0xa4},
	{0x3A,  0xb0},
	{0x3B,  0xc8},
	{0x3C,  0xdc},
	{0x3D,  0xf0},
	{0x3E,  0xf8},
	{0x3F,  0xFF},

//DARK GAMMA
	{0x40,  0x00}, 
	{0x41,  0x0b}, 
	{0x42,  0x15},
	{0x43,  0x29},
	{0x44,  0x47},
	{0x45,  0x5D},
	{0x46,  0x72},
	{0x47,  0x83},
	{0x48,  0x92},
	{0x49,  0xA0},
	{0x4A,  0xac},
	{0x4B,  0xc6},
	{0x4C,  0xdA},
	{0x4D,  0xeC},
	{0x4E,  0xf6},
	{0x4F,  0xFF},
  
	{0x50,  0x00}, //DARK GAMMA on/off
	
//CSC                   
	{0x60,  0x33}, 
	{0x61,  0x20}, 
	{0x62,  0xE4}, 
	{0x63,  0xFA}, 
	{0x64,  0x13}, 
	{0x65,  0x25}, 
	{0x66,  0x07}, 
	{0x67,  0xF5}, 
	{0x68,  0xEA}, 
	{0x69,  0x20}, 
	{0x6A,  0xC8}, 
	{0x6B,  0xC4}, 
	{0x6C,  0x84}, 
	{0x6D,  0x04}, 
	{0x6E,  0x0C}, 
	{0x6F,  0x00}, 
                        
//Edge                  
// {0x70,  0x00},
// {0x71,  0x18},
// {0x72,  0x18},
// {0x73,  0x04},
// {0x74,  0x08},
// {0x76,  0x20},
// {0x77,  0x04},
// {0x78,  0x08},
// {0x7a,  0x20},
// {0x7d,  0x08},
// {0X7e,  0x30},

 	{0x80,  0x22}, 
	{0x81,  0x14}, 
	{0x82,  0x14}, 
	{0x83,  0x04}, 
	{0x84,  0x06}, 
	{0x85,  0x06}, 
	{0x87,  0x04},
	{0x88,  0x10},
	{0x89,  0x06},
	{0X90,  0x06},
	{0x91,  0x03},
	{0x93,  0xe0},

//Cr/Cb Coring
	{0x94,  0x00}, 
	{0x95,  0x00}, 
	{0x96,  0x4C}, 
	{0x97,  0x76}, 
	{0x9A,  0xf5}, 

	{0xA1,  0x08},   //@ 0
	{0xA2,  0x10},   //@ 20
	{0xA3,  0x16},   //@ 40
	{0xA4,  0x20},   //@ 60
	{0xA5,  0x30},   //@ 80
	{0xA6,  0xa0}, 
	{0xA7,  0x06}, 
	{0xA8,  0x40}, 

	{0xA9,  0x38}, 
	{0xAa,  0x35}, 

	{0xAc,  0xff}, 
	{0xAd,  0x09}, 
	{0xAe,  0x96}, 
	{0xAf,  0x18}, 

	{0xB2,  0x38},   //color suppression start
	{0xB3,  0x53}, 
	{0xB6,  0x00}, 
	
//Color Saturation
	{0xBC, 0x10}, 
	{0xBD, 0x10}, 
	{0xBE, 0x10}, 
	{0xBF, 0x10}, 
	{0xc0, 0x10}, 
	{0xc1, 0x10}, 	
	{0xc2, 0x10}, 
	{0xc3, 0x10}, 
	{0xc4, 0x10}, 
	{0xc5, 0x10}, 

	{0xcc, 0x04}, 
	{0xcd, 0x40}, 
	{0xce, 0x00}, 
                      
//IDP 3                 
	{0x00,  0x05}, 
                        
//Memory                
	{0x40,  0x15}, 
	{0x41,  0x28}, 
	{0x42,  0x04}, 
	{0x43,  0x15}, 
	{0x44,  0x28}, 
	{0x45,  0x04}, 
	{0x46,  0x15}, 
	{0x47,  0x28}, 
	{0x48,  0x04}, 
                        
//Knee                  
	{0x90,  0xca},  //
	{0x91,  0x81},  //knee function selection/knee point H
	{0x92,  0x00},  //knee point L
	{0x93,  0x50},  //Knee gain
	{0x94,  0x41},  //[6:4]knee start H/[2:0]Knee END H
	{0x95,  0x7e},  //knee start L
	{0x96,  0x48},  //knee END L
                        
//ADG                   
	{0x99,  0xC0}, 
	{0xA0,  0x10}, 
	{0xA1,  0x22}, 
	{0xA2,  0x36}, 
	{0xA3,  0x49}, 
	{0xA4,  0x5D}, 
	{0xA5,  0x70}, 
	{0xA6,  0x82}, 
	{0xA7,  0x94}, 
	{0xA8,  0xA5}, 
	{0xA9,  0xB5}, 
	{0xAA,  0xC3}, 
	{0xAB,  0xD1}, 
	{0xAC,  0xDE}, 
	{0xAD,  0xEA}, 
	{0xAE,  0xF5}, 
	{0xAF,  0xFF}, 
                        
//YXGMA                 
	{0xB0,  0xc0},  //YGMACTRL
	{0xB1,  0x04},  //YGMASLOP
	{0xB8,  0x0f},  //DRKTHR1
	{0xB9,  0x10},  //DRKTHR2
	//{0xBA,  0x38},  //DRKTHR3
	//{0xBB,  0x39},  //DRKTHR4
	{0xC0,  0x03}, 
	{0xC1,  0x0E}, 
	{0xC2,  0x16}, 
	{0xC3,  0x24}, 
	{0xC4,  0x3F}, 
	{0xC5,  0x56}, 
	{0xC6,  0x6A}, 
	{0xC7,  0x7C}, 
	{0xC8,  0x8C}, 
	{0xC9,  0x98}, 
	{0xCA,  0xA2}, 
	{0xCB,  0xB8}, 
	{0xCC,  0xCD}, 
	{0xCD,  0xE2}, 
	{0xCE,  0xF0}, 
	{0xCF,  0xFF}, 


// edge value adjustment
	{0xe0,  0x81}, 
	{0xe1,  0x83}, 
	{0xe2,  0x07}, 
	{0xe3,  0x0c}, 
	{0xe4,  0x14}, 
	{0xe5,  0x1c}, 


//Sensor on
	{0x00,  0x00}, 
	{0x03,  0xc5}, 
	{0x00,  0x01}, 
	{0x10,  0x84}, 
	{0x00,  0x02}, 
	{0x10,  0xd3}, 
	{0xff,  0xff}, 
#else

     {0x00,  0x00},  
	{0x04,  0x00},  //Group A 0x10
	{0x05,  0x0F},  //UXGA Output
	{0x06,  0x86}, 
	{0x07,  0x08}, 


	{0x08,  0xa2},  //PLL on
	{0x09,  0x12},  
	{0x0A,  0x12},  
	{0x10,  0x13},//17},  
	{0x11,  0x01}, 
	{0x12,  0x8A}, 

    {0x13, 0x1a},//16},    
    {0x15, 0x22},//16},    
    {0x14, 0x27},//16},    
    {0x17, 0xcb},//CF},    
    {0x18, 0x38},//34},    
    {0x41, 0x17},    
    {0x42, 0x52},    
                                              
    //Flicker - 50Hz  - Still mode            
    {0x00, 0x00},    
	{0x20,  0x00}, 
	{0x21,  0x02}, 
	{0x23,  0x29},//15}, 
	{0x00,  0x01}, 
	{0x35,  0x50},//3c}, 

    //Flicker - 50Hz - Preview mode 24MHz MCLK
    {0x00, 0x00},    
    {0x24, 0x00},    
	{0x25,  0xe6},//10}, 
	{0x27,  0x0d},//4b}, 
	{0x00,  0x01}, 
	{0x34,  0x80},//64}, 
	
    //AE Block                                
    {0x00, 0x01},    
    {0x10, 0x80},    
    {0x11, 0x0A},    
    {0x12, 0x74},    
    {0x13, 0x74},    
    {0x14, 0x70},    
    {0x17, 0xC4},    
    {0x1c, 0x02},    
                                              
    {0x19, 0x00},//0x
    {0x1A, 0x00},//0x
    {0x1c, 0x02},    
                                              
    {0x36, 0x26},    
                                              
    {0x40, 0x40}, //M
#if 0                                         
    {0x41, 0x28},    
    {0x42, 0x28},    
    {0x43, 0x08},    
    {0x44, 0x08},    
    {0x45, 0x09},    
    {0x46, 0x17},    
    {0x47, 0x1D},    
    {0x48, 0x21},    
    {0x49, 0x23},    
    {0x4A, 0x24},    
    {0x4B, 0x26},    
    {0x4C, 0x27},    
    {0x4D, 0x27},    
    {0x4E, 0x1A},    
    {0x4F, 0x14},    
    {0x50, 0x11},    
    {0x51, 0x0F},    
    {0x52, 0x0D},    
    {0x53, 0x0C},    
    {0x54, 0x0A},    
    {0x55, 0x09},    
#else                                         
    {0x41, 0x20},    
    {0x42, 0x20},    
    {0x43, 0x00},    
    {0x44, 0x00},    
    {0x45, 0x01},    
    {0x46, 0x0c},    
    {0x47, 0x11},    
    {0x48, 0x15},    
    {0x49, 0x17},    
    {0x4A, 0x1a},    
    {0x4B, 0x1c},    
    {0x4C, 0x1e},    
    {0x4D, 0x1e},    
    {0x4E, 0x0f},    
    {0x4F, 0x09},    
    {0x50, 0x07},    
    {0x51, 0x05},    
    {0x52, 0x04},    
    {0x53, 0x03},    
    {0x54, 0x02},    
    {0x55, 0x01},    
    {0x60, 0x7e},    
    {0x61, 0xff},    
    {0x62, 0xff},    
    {0x63, 0xff},    
    {0x64, 0xff},    
    {0x65, 0x7e},    
    {0x66, 0x50},    
    {0x67, 0x50},    
    {0x68, 0x50},    
    {0x69, 0x50},    
    {0x6A, 0x50},    
    {0x6B, 0x00},    
    {0x6C, 0x06},  
	{0x9a, 0x80},
	{0x6b, 0x80},


#endif                                        
                                              
    //AWB Block                               
    {0x00, 0x02},    
    {0x10, 0xD3},    
    {0x11, 0x11},    
    {0x13, 0x80},    
    {0x14, 0x80},    
    {0x15, 0xE0},//FE},    
    {0x16, 0x88},    
    {0x17, 0xD0},    
    {0x18, 0x78},    
    {0x19, 0x98},    
    {0x1A, 0x68},    
    {0x1B, 0x8d},    
    {0x1C, 0x68},    
    {0x1D, 0x8C},    
    {0x1E, 0x74},    
    {0x20, 0xF0},    
    {0x21, 0x70},    
    {0x22, 0xa8},    
    {0x23, 0x10},    
    {0x25, 0x20},    
    {0x26, 0x05},    
    {0x27, 0x63},    
    {0x28, 0x63},    
    {0x29, 0xA0},    
    {0x2A, 0x89},    
    {0x30, 0x00},    
    {0x31, 0x10},    
    {0x32, 0x00},    
    {0x33, 0x10},    
    {0x34, 0x06},    
    {0x35, 0x30},    
    {0x36, 0x04},    
    {0x37, 0xA0},    
    {0x40, 0x01},    
    {0x41, 0x04},    
    {0x42, 0x08},    
    {0x43, 0x10},    
    {0x44, 0x13},    
    {0x45, 0x6B},    
    {0x46, 0x82},    
    {0x52, 0x82},    
	 


    //CMA change  -D65~A                      
    {0x53, 0x86}, //A
    {0x54, 0xA0}, //A
    {0x55, 0x88}, //A
    {0x56, 0x9F}, //A
    {0x57, 0xA5}, //A
    {0x58, 0x89}, //A
    {0x59, 0xA6}, //A
    {0x5A, 0x87}, //A
                                              
    {0x64, 0x00}, //T
    {0x65, 0x00},    
    {0x66, 0x00}, //T
    {0x67, 0x00},    
    {0x68, 0xA5}, //T
    {0x69, 0xB4},    
    {0x6A, 0xB3}, //T
    {0x6B, 0xAC},    
    {0x6C, 0xB7}, //T
    {0x6D, 0x98},    
    {0x6E, 0xBA}, //T
    {0x6F, 0x90},    
    {0x70, 0xBF}, //T
    {0x71, 0x9B},    
    {0x72, 0xCE}, //T
    {0x73, 0x8C},    

	 
		 //IDP
	 

    {0x74, 0x7F}, //T
    {0x75, 0x8C}, //T
    {0x76, 0xAD}, //T
    {0x77, 0xBA}, //T
    {0x78, 0x8F}, //T
    {0x79, 0x9A}, //T
    {0x7A, 0xA3}, //T
    {0x7B, 0xAC}, //T
    {0x7C, 0xA0}, //T
    {0x7D, 0xA9}, //T
    {0x7E, 0x95}, //T
    {0x7F, 0xAC}, //T
    {0x80, 0xAD}, //T
    {0x81, 0xBC}, //T
    {0x82, 0x98}, //T
    {0x83, 0xA4}, //T
    {0x84, 0x00}, //T
    {0x85, 0x00}, //T
    {0x86, 0x00}, //T
    {0x87, 0x00}, //T
    {0x88, 0xC9}, //T
    {0x89, 0xD5}, //T
    {0x8A, 0x70}, //T
    {0x8B, 0x7B}, //T
    {0x8C, 0xD0}, //T
    {0x8D, 0xE5}, //T
    {0x8E, 0x58}, //T
    {0x8F, 0x70}, //T
                                              
    {0xB4, 0x05},    
    {0xB5, 0x0F},    
    {0xB6, 0x06},    
    {0xB7, 0x06},    
    {0xB8, 0x40},    
    {0xB9, 0x10},    
    {0xBA, 0x06},    
                                              
    //IDP                                     
    {0x00, 0x03},    
    {0x10, 0xFF},    
    {0x11, 0x1D},    
    {0x12, 0x1D},    
    {0x13, 0xFB},    
    {0x14, 0x00}, 
    {0x15, 0xc0},    
                                              
    //DPC                                     
    {0x30, 0x88}, //D
    {0x31, 0x14},//04}, //D
    {0x32, 0x10}, //D
    {0x33, 0x0C}, //D
    {0x34, 0x08}, //D
    {0x35, 0x04}, //D
    {0x36, 0x44}, //D
    {0x37, 0x66}, //D
    {0x38, 0x00}, //0
    {0x39, 0x08}, //0
    {0x3A, 0x08}, //0
    {0x3B, 0x20}, //0
    {0x3C, 0x20}, // 
    {0x3D, 0x04}, //N
    {0x3E, 0x08}, //N
    {0x3F, 0x10}, //N
    {0x40, 0x10}, //N
    {0x41, 0x20}, //N
    {0x42, 0xFF}, //N
    {0x43, 0x40},//F0}, //N
    {0x44, 0x40}, //B
    {0x45, 0x06},//09}, //S
    {0x46, 0x40}, //S
    {0x47, 0x30}, //I

		//DPC
 #if 0                                             
    //Shading                                 
    {0x50, 0x24}, //R
    {0x51, 0x44}, //R
    {0x52, 0x20}, //R
    {0x53, 0x30}, //R
                                              
    {0x54, 0x20}, //G
    {0x55, 0x20}, //G
    {0x56, 0x00}, //G
    {0x57, 0x10}, //G
                                              
    {0x58, 0x20}, //G
    {0x59, 0x20}, //G
    {0x5A, 0x00}, //G
    {0x5B, 0x10}, //G
                                              
    {0x5C, 0x34}, //B
    {0x5D, 0x48}, //B
    {0x5E, 0x18}, //B
    {0x5F, 0x2a}, //B
                                              
    {0x60, 0x32}, //#
    {0x61, 0x00}, //#
    {0x62, 0x67}, //#
                                              
    {0x6B, 0x23},    
    {0x6C, 0x44},    
    {0x6D, 0x45},    
    {0x6E, 0x67},    
    {0x6F, 0x8a},    
    {0x70, 0x89}, 
    
    {0x71, 0x34},    
    {0x72, 0x45},    
    {0x73, 0x56},    
    {0x74, 0x77},    
    {0x75, 0x78},    
    {0x76, 0x89},    

	{0x77, 0x34},    
    {0x78, 0x45},    
    {0x79, 0x56},    
    {0x7A, 0x77},    
    {0x7B, 0x78},    
    {0x7C, 0x89},    

	{0x7D, 0x22},    
    {0x7E, 0x32},    
    {0x7F, 0x34},    
    {0x80, 0x45},    
    {0x81, 0x56},    
    {0x82, 0x67},    
    {0x83, 0x1f},    
    {0x84, 0x50},    
#endif

    //Shading                                 
    {0x50, 0x0b}, //R
    {0x51, 0x0b}, //R
    {0x52, 0x19}, //R
    {0x53, 0x18}, //R
                                              
    {0x54, 0x10}, //G
    {0x55, 0x00}, //G
    {0x56, 0x08}, //G
    {0x57, 0x00}, //G
                                              
    {0x58, 0x10}, //G
    {0x59, 0x00}, //G
    {0x5A, 0x08}, //G
    {0x5B, 0x00}, //G
                                              
    {0x5C, 0x14}, //B
    {0x5D, 0x0f}, //B
    {0x5E, 0x24}, //B
    {0x5F, 0x12}, //B
                                              
    {0x60, 0x32}, //#
    {0x61, 0x00}, //#
    {0x62, 0x67}, //#
                                              
    {0x6B, 0x23},    
    {0x6C, 0x44},    
    {0x6D, 0x44},    
    {0x6E, 0x45},    
    {0x6F, 0x44},    
    {0x70, 0x00}, 
    
    {0x71, 0x23},    
    {0x72, 0x44},    
    {0x73, 0x55},    
    {0x74, 0x55},    
    {0x75, 0x55},    
    {0x76, 0x00},    

	{0x77, 0x23},    
    {0x78, 0x44},    
    {0x79, 0x55},    
    {0x7A, 0x55},    
    {0x7B, 0x55},    
    {0x7C, 0x00},    

	{0x7D, 0x23},    
    {0x7E, 0x44},    
    {0x7F, 0x34},    
    {0x80, 0x43},    
    {0x81, 0x44},    
    {0x82, 0x00},    
    {0x83, 0x12},    
    {0x84, 0x14},

                                              
    //Interpolation                           
    {0xA0, 0x2F},    
    {0xA1, 0xB7},    
    {0xA2, 0xB7},    
    {0xA3, 0xB7},    
    {0xA4, 0x04},    
    {0xA5, 0xFF},    
    {0xA6, 0x06},    
    {0xA7, 0xFF},    
    {0xA8, 0x00},    
    {0xA9, 0x00},    
    {0xAA, 0x00},    
    {0xAB, 0x00},    
    {0xAC, 0x60},    
    {0xAD, 0x18},    
    {0xAE, 0x0c},    
    {0xAF, 0x14},    
    {0xB0, 0x08},    
    {0xB1, 0x00},    


    //Color Matrix for D65                    
    {0xC0, 0xAF},// C
    {0xC1, 0x66},    
    {0xC2, 0xd4},    
    {0xC3, 0x06},    
    {0xC4, 0xf0},    
    {0xC5, 0x5a},    
    {0xC6, 0xF6},    
    {0xC7, 0xF9},    
    {0xC8, 0xbf},    
    {0xC9, 0x88},    
    {0xCA, 0x00},    
    {0xCB, 0x00},    
    {0xCC, 0x00},    
    {0xCD, 0x00},    
    {0xCE, 0x00},    
    //Color Matrix for CWF                    
    {0xD0, 0x2F},    
    {0xD1, 0x72},    
    {0xD2, 0xC9},    
    {0xD3, 0x04},    
    {0xD4, 0xE3},    
    {0xD5, 0x60},    
    {0xD6, 0xFC},    
    {0xD7, 0xF5},    
    {0xD8, 0xb0},    
    {0xD9, 0x99},    
    {0xDA, 0xC3},    
    {0xDB, 0x14},    
    {0xDC, 0x3A},    
    {0xDD, 0xAD},    
    {0xDE, 0x09},    
                                              
    //Color Matrix for A                      
    {0xE0, 0x2F},    
    {0xE1, 0x5C},    
    {0xE2, 0xDD},    
    {0xE3, 0x06},    
    {0xE4, 0xE0},    
    {0xE5, 0x69},    
    {0xE6, 0xF6},    
    {0xE7, 0xE5},    
    {0xE8, 0xAB},    
    {0xE9, 0xAE},    
    {0xEA, 0x6A},    
    {0xEB, 0x01},    
    {0xEC, 0x2D},    
    {0xED, 0xEE},    
    {0xEE, 0x04},    
                                              
    //IDP 2                                   
    {0x00, 0x04},    
                                              
    //Gamma - R                               
    {0x10, 0x00},    
    {0x11, 0x06},    
    {0x12, 0x0e},    
    {0x13, 0x20},    
    {0x14, 0x40},    
    {0x15, 0x5e},    
    {0x16, 0x74},    
    {0x17, 0x87},    
    {0x18, 0x98},    
    {0x19, 0xa6},    
    {0x1A, 0xb3},    
    {0x1B, 0xca},    
    {0x1C, 0xde},    
    {0x1D, 0xf0},    
    {0x1E, 0xf8},    
    {0x1F, 0xff},    


    //Gamma - G                               
    {0x20, 0x00},    
    {0x21, 0x06},    
    {0x22, 0x0e},    
    {0x23, 0x20},    
    {0x24, 0x40},    
    {0x25, 0x5e},    
    {0x26, 0x74},    
    {0x27, 0x87},    
    {0x28, 0x98},    
    {0x29, 0xa6},    
    {0x2A, 0xb3},    
    {0x2B, 0xca},    
    {0x2C, 0xde},    
    {0x2D, 0xf0},    
    {0x2E, 0xf8},    
    {0x2F, 0xff},    
                                              
    //Gamma - B                               
    {0x30, 0x00},    
    {0x31, 0x06},    
    {0x32, 0x0e},    
    {0x33, 0x20},    
    {0x34, 0x40},    
    {0x35, 0x5e},    
    {0x36, 0x74},    
    {0x37, 0x87},    
    {0x38, 0x98},    
    {0x39, 0xa6},    
    {0x3A, 0xb3},    
    {0x3B, 0xca},    
    {0x3C, 0xde},    
    {0x3D, 0xf0},    
    {0x3E, 0xf8},    
    {0x3F, 0xff},    
                                              
    //CSC                                     
    {0x60, 0x33},    
    {0x61, 0x20},    
    {0x62, 0xE4},    
    {0x63, 0xFA},    
    {0x64, 0x13},    
    {0x65, 0x25},    
    {0x66, 0x07},    
    {0x67, 0xF5},    
    {0x68, 0xEA},    
    {0x69, 0x20},    
    {0x6A, 0xC8},    
    {0x6B, 0xC4},    
    {0x6C, 0x84},    
    {0x6D, 0x04},    
    {0x6E, 0x0C},    
    {0x6F, 0x00},    


    //Edge                                    
    {0x80, 0xa2},    
    {0x81, 0x10},    
    {0x82, 0x10},    
    {0x83, 0x04},    
    {0x84, 0x0a},    
    {0x85, 0x06},    
    {0x86, 0x00},    
    {0x87, 0x04},    
    {0x88, 0x0a},    
    {0x89, 0x06},    
    {0x8a, 0x10},    
    {0x8b, 0x30},    
    {0x8c, 0x30},    
    {0X90, 0x16},    
    {0x91, 0x03},    
    {0x93, 0xE0},    

    //Cr/Cb Coring                            
    {0x94, 0x00},    
    {0x95, 0x00},    
    {0x96, 0x4c},    
    {0x97, 0x66},    
    {0x9A, 0xf5},    
    {0xA1, 0x08},    
    {0xA2, 0x10},    
    {0xA3, 0x16},    
    {0xA4, 0x14},    
    {0xA5, 0x18},    
    {0xA6, 0xa0},    
    {0xA7, 0x06},    
    {0xA8, 0x40},    
                                              
    {0xA9, 0x20},    
    {0xAA, 0x28},    
    {0xAc, 0xff},    
    {0xAd, 0x09},    
    {0xAe, 0x18},    
    {0xAf, 0x18},    
                                              
    {0xB2, 0x24},    
    {0xB3, 0x54},    
    {0xB6, 0x08},    
                                            
    //Color Saturation                        
    {0xBC, 0x10},    
    {0xBD, 0x10},    
    {0xBE, 0x10},    
    {0xBF, 0x10},    
    {0xc2, 0x12},    
    {0xc3, 0x12},    
                                              
    //IDP 3                                   
    {0x00, 0x05},    
                                              
    //Memory                                  
    {0x40, 0x15},    
    {0x41, 0x28},    
    {0x42, 0x04},    
    {0x43, 0x15},    
    {0x44, 0x28},    
    {0x45, 0x04},    
    {0x46, 0x15},    
    {0x47, 0x28},    
    {0x48, 0x04},    
                                              
    //Knee                                    
    {0x90, 0x00}, // 
    {0x91, 0x00}, //k
    {0x92, 0x00}, //k
    {0x93, 0x00}, //K
    {0x94, 0x00}, //[
    {0x95, 0x00}, //k
    {0x96, 0x00}, //k
                                              
    //ADG                                     
    #if 0                                     
    {0x99, 0xC0},    
    {0xA0, 0x10},    
    {0xA1, 0x22},    
    {0xA2, 0x36},    
    {0xA3, 0x49},    
    {0xA4, 0x5D},    
    {0xA5, 0x70},    
    {0xA6, 0x82},    
    {0xA7, 0x94},    
    {0xA8, 0xA5},    
    {0xA9, 0xB5},    
    {0xAA, 0xC3},    
    {0xAB, 0xD1},    
    {0xAC, 0xDE},    
    {0xAD, 0xEA},    
    {0xAE, 0xF5},    
    {0xAF, 0xFF},    
    //YXGMA                                   
    {0xB0, 0xc0}, //Y
    {0xB1, 0x04}, //Y
    {0xB8, 0x0f}, //D
    {0xB9, 0x10}, //D
    //{0xBA, 0x38}, /
    //{0xBB, 0x39}, /
    {0xC0, 0x03},    
    {0xC1, 0x0E},    
    {0xC2, 0x16},    
    {0xC3, 0x24},    
    {0xC4, 0x3F},    
    {0xC5, 0x56},    
    {0xC6, 0x6A},    
    {0xC7, 0x7C},    
    {0xC8, 0x8C},    
    {0xC9, 0x98},    
    {0xCA, 0xA2},    
    {0xCB, 0xB8},    
    {0xCC, 0xCD},    
    {0xCD, 0xE2},    
    {0xCE, 0xF0},    
    {0xCF, 0xFF},    
   #else                                      
    {0x99, 0x00},    
    {0xA0, 0x00},    
    {0xA1, 0x00},    
    {0xA2, 0x00},    
    {0xA3, 0x00},    
    {0xA4, 0x00},    
    {0xA5, 0x00},    
    {0xA6, 0x00},    
    {0xA7, 0x00},    
    {0xA8, 0x00},    
    {0xA9, 0x00},    
    {0xAA, 0x00},    
    {0xAB, 0x00},    
    {0xAC, 0x00},    
    {0xAD, 0x00},    
    {0xAE, 0x00},    
    {0xAF, 0x00},    
                                              
    //YXGMA                                   
    {0xB0, 0x00}, //Y
    {0xB1, 0x00}, //Y
    {0xB8, 0x00}, //D
    {0xB9, 0x00}, //D
    //{0xBA, 0x38}, /
    //{0xBB, 0x39}, /
    {0xC0, 0x00},    
    {0xC1, 0x00},    
    {0xC2, 0x00},    
    {0xC3, 0x00},    
    {0xC4, 0x00},    
    {0xC5, 0x00},    
    {0xC6, 0x00},    
    {0xC7, 0x00},    
    {0xC8, 0x00},    
    {0xC9, 0x00},    
    {0xCA, 0x00},    
    {0xCB, 0x00},    
    {0xCC, 0x00},    
    {0xCD, 0x00},    
    {0xCE, 0x00},    
    {0xCF, 0x00},    
    {0xe0, 0x01},    
    {0xe1, 0x03},    
    {0xe2, 0x04},    
    {0xe3, 0x0c},    
    {0xe4, 0x11},    
    {0xe5, 0x16},    
    {0xe6, 0x1b},    
    {0xe7, 0x24},    
    {0xe8, 0x30},    
  #endif                                      
    //Sensor on                               
	{0x00,  0x00}, 
	{0x03,  0xc5}, 
	{0x00,  0x01}, 
	{0x10,  0x84}, 
	{0x00,  0x02}, 
	{0x10,  0xd3}, 
	{0xff,  0xff},                            
  

#endif
};

/* 1600X1200 UXGA */
static struct reginfo sensor_uxga[] =
{
   {0x00,0x00},
   {0x04,0x10},
   {0x05,0x8f},
   {0x00,0x03},
   {0x94,0x06},
   {0x95,0x40},
   {0x96,0x04},
   {0x97,0xb0}, 
   {0xff, 0xff},
};

/* 1280X1024 SXGA */
static struct reginfo sensor_sxga[] =
{
       {0x00,0x00},
       {0x04,0x10},
       {0x05,0x8b},
	{0x00,0x03},
	{0x94,0x05},
	{0x95,0x00},
	{0x96,0x04},
	{0x97,0x00}, 
	{0xff, 0xff}, 
};
static struct reginfo sensor_xga[] =
{
	{0xff, 0xff}
};
/* 800X600 SVGA*/
static struct reginfo sensor_svga[] =
{
    {0x00,0x03},
    {0x94,0x03},
    {0x95,0x20},
    {0x96,0x02},
    {0x97,0x58}, 
    {0xff, 0xff}
};

/* 640X480 VGA */
static struct reginfo sensor_vga[] =
{
      {0x00,0x00},
      {0x04,0x00},
      {0x05,0x0f},
      {0x00,0x03},
      {0x94,0x02}, //0x02
      {0x95,0x80}, //0x80
      {0x96,0x01},//0x01
      {0x97,0xe0}, //0xe0
      {0xff, 0xff}, 
};

/* 352X288 CIF */
static struct reginfo sensor_cif[] =
{
    {0x00,0x03},
    {0x94,0x01},
    {0x95,0x60},
    {0x96,0x01},
    {0x97,0x20}, 
    {0xff, 0xff}, 
};

/* 320*240 QVGA */
static  struct reginfo sensor_qvga[] =
{
       {0x00,0x03},
       {0x94,0x01},
       {0x95,0x40},
       {0x96,0x00},
       {0x97,0xf0}, 
       {0xff, 0xff}, 
};

/* 176X144 QCIF*/
static struct reginfo sensor_qcif[] =
{
       {0x00,0x03},
       {0x94,0x00},
       {0x95,0xB0},
       {0x96,0x00},
       {0x97,0x90}, 
       {0xff, 0xff},
};
#endif
static  struct reginfo sensor_ClrFmt_YUYV[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_ClrFmt_UYVY[]=
{
    {0xff, 0xff}
};

#if CONFIG_SENSOR_WhiteBalance
static  struct reginfo sensor_WhiteB_Auto[]={
    {0x00, 0x02},
    {0x10, 0xd3},
    {0xff, 0xff}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
    {0x00, 0x02},
    {0x10, 0x00},
    {0x50, 0xd0},
    {0x51, 0x88},
    {0xff, 0xff}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
    {0x00, 0x02},
    {0x10, 0x00},
    {0x50, 0xaa},
    {0x51, 0x90},
    {0xff, 0xff}

};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
    {0x00, 0x02},
    {0x10, 0x00},
    {0x50, 0xc2},
    {0x51, 0x9e},
    {0xff, 0xff}
};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
    {0x00, 0x02},
    {0x10, 0x00},
    {0x50, 0xaa},
    {0x51, 0xbe},
    {0xff, 0xff}
};
static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};
#endif

#if CONFIG_SENSOR_Brightness
static  struct reginfo sensor_Brightness0[]=
{
    // Brightness -2
    {0x00, 0x04},
    {0xb6, 0xa0},
    {0xff, 0xff}

};

static  struct reginfo sensor_Brightness1[]=
{
    // Brightness -1
    {0x00, 0x04},
    {0xb6, 0x90},
    {0xff, 0xff}

};

static  struct reginfo sensor_Brightness2[]=
{
    //  Brightness 0
    {0x00, 0x04},
    {0xb6, 0x00},
    {0xff, 0xff}

};

static  struct reginfo sensor_Brightness3[]=
{
    // Brightness +1
   {0x00, 0x04},
   {0xb6, 0x10},
   {0xff, 0xff}

};

static  struct reginfo sensor_Brightness4[]=
{
    //  Brightness +2
   {0x00, 0x04},
   {0xb6, 0x20},
   {0xff, 0xff}

};

static  struct reginfo sensor_Brightness5[]=
{
   {0x00, 0x04},
   {0xb6, 0x30},
   {0xff, 0xff}
};
static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

#endif

#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
    {0x00, 0x04},
    {0xd9, 0x00},
    {0xff, 0xff}
};

static  struct reginfo sensor_Effect_WandB[] =
{
    {0x00, 0x04},
    {0xd9, 0x40},
    {0x00, 0x00},
    {0xff, 0xff}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
    {0x00, 0x04},
    {0xd9, 0x80},
    {0xda, 0x60},
    {0xdb, 0xa0},
    {0xff, 0xff}
};

static  struct reginfo sensor_Effect_Negative[] =
{
   {0x00, 0x04},
   {0xd9, 0x20},
   {0xff, 0xff}
};
static  struct reginfo sensor_Effect_Bluish[] =
{
    // Bluish
    {0x00, 0x04},
    {0xd9, 0x80},
    {0xda, 0xc0},
    {0xdb, 0x60},
    {0xff, 0xff}
};

static  struct reginfo sensor_Effect_Green[] =
{
    //  Greenish
    {0x00, 0x04},
    {0xd9, 0x80},
    {0xda, 0x50},
    {0xdb, 0x50},
    {0xff, 0xff}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};
#endif
#if CONFIG_SENSOR_Exposure
static  struct reginfo sensor_Exposure0[]=
{
    //-3
   {0xff, 0xff}
};

static  struct reginfo sensor_Exposure1[]=
{
    //-2
    {0xff, 0xff}
};

static  struct reginfo sensor_Exposure2[]=
{
    //-0.3EV
   {0xff, 0xff}
};

static  struct reginfo sensor_Exposure3[]=
{
    //default
  {0xff, 0xff}
};

static  struct reginfo sensor_Exposure4[]=
{
    // 1
  {0xff, 0xff}
};

static  struct reginfo sensor_Exposure5[]=
{
    // 2
  {0xff, 0xff}
};

static  struct reginfo sensor_Exposure6[]=
{
    // 3
   {0xff, 0xff}
};

static struct reginfo *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};
#endif
#if CONFIG_SENSOR_Saturation
static  struct reginfo sensor_Saturation0[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_Saturation1[]=
{
   {0xff, 0xff}
};

static  struct reginfo sensor_Saturation2[]=
{
  {0xff, 0xff}
};
static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

#endif
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_Contrast1[]=
{
   {0xff, 0xff}
};

static  struct reginfo sensor_Contrast2[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_Contrast3[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_Contrast4[]=
{
   {0xff, 0xff}
};


static  struct reginfo sensor_Contrast5[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_Contrast6[]=
{
    {0xff, 0xff}
};
static struct reginfo *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};

#endif
#if CONFIG_SENSOR_Mirror
static  struct reginfo sensor_MirrorOn[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_MirrorOff[]=
{
     {0xff, 0xff}
};
static struct reginfo *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};
#endif
#if CONFIG_SENSOR_Flip
static  struct reginfo sensor_FlipOn[]=
{
    {0xff, 0xff}
};

static  struct reginfo sensor_FlipOff[]=
{
     {0xff, 0xff}
};
static struct reginfo *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

#endif
#if CONFIG_SENSOR_Scene
static  struct reginfo sensor_SceneAuto[] =
{
   {0x00, 0x01},
   {0x11, 0x0a},
   {0x00, 0x04},
   {0xb6, 0x00},
   {0xff, 0xff}
};

static  struct reginfo sensor_SceneNight[] =
{
   {0x00, 0x01},
   {0x11, 0x14},
   {0x00, 0x04},
   {0xb6, 0x10},
   {0xff, 0xff}
};
static struct reginfo *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

#endif
#if CONFIG_SENSOR_DigitalZoom
static struct reginfo sensor_Zoom0[] =
{
    {0xff, 0xff}
};

static struct reginfo sensor_Zoom1[] =
{
     {0xff, 0xff}
};

static struct reginfo sensor_Zoom2[] =
{
    {0xff, 0xff}
};


static struct reginfo sensor_Zoom3[] =
{
    {0xff, 0xff}
};
static struct reginfo *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL,};
#endif
static  struct v4l2_querymenu sensor_menus[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 0,  .name = "auto",  .reserved = 0, }, {  .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 1, .name = "incandescent",  .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 2,  .name = "fluorescent", .reserved = 0,}, {  .id = V4L2_CID_DO_WHITE_BALANCE, .index = 3,  .name = "daylight", .reserved = 0,},
    { .id = V4L2_CID_DO_WHITE_BALANCE,  .index = 4,  .name = "cloudy-daylight", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Effect
    { .id = V4L2_CID_EFFECT,  .index = 0,  .name = "none",  .reserved = 0, }, {  .id = V4L2_CID_EFFECT,  .index = 1, .name = "mono",  .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 2,  .name = "negative", .reserved = 0,}, {  .id = V4L2_CID_EFFECT, .index = 3,  .name = "sepia", .reserved = 0,},
    { .id = V4L2_CID_EFFECT,  .index = 4, .name = "posterize", .reserved = 0,} ,{ .id = V4L2_CID_EFFECT,  .index = 5,  .name = "aqua", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Scene
    { .id = V4L2_CID_SCENE,  .index = 0, .name = "auto", .reserved = 0,} ,{ .id = V4L2_CID_SCENE,  .index = 1,  .name = "night", .reserved = 0,},
    #endif

	#if CONFIG_SENSOR_Flash
    { .id = V4L2_CID_FLASH,  .index = 0,  .name = "off",  .reserved = 0, }, {  .id = V4L2_CID_FLASH,  .index = 1, .name = "auto",  .reserved = 0,},
    { .id = V4L2_CID_FLASH,  .index = 2,  .name = "on", .reserved = 0,}, {  .id = V4L2_CID_FLASH, .index = 3,  .name = "torch", .reserved = 0,},
    #endif
};

static  struct v4l2_queryctrl sensor_controls[] =
{
	#if CONFIG_SENSOR_WhiteBalance
    {
        .id		= V4L2_CID_DO_WHITE_BALANCE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "White Balance Control",
        .minimum	= 0,
        .maximum	= 4,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Brightness
	{
        .id		= V4L2_CID_BRIGHTNESS,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Brightness Control",
        .minimum	= -3,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Effect
	{
        .id		= V4L2_CID_EFFECT,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Effect Control",
        .minimum	= 0,
        .maximum	= 5,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Exposure
	{
        .id		= V4L2_CID_EXPOSURE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Exposure Control",
        .minimum	= 0,
        .maximum	= 6,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Saturation
	{
        .id		= V4L2_CID_SATURATION,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Saturation Control",
        .minimum	= 0,
        .maximum	= 2,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Contrast
	{
        .id		= V4L2_CID_CONTRAST,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Contrast Control",
        .minimum	= -3,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif

	#if CONFIG_SENSOR_Mirror
	{
        .id		= V4L2_CID_HFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Mirror Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Flip
	{
        .id		= V4L2_CID_VFLIP,
        .type		= V4L2_CTRL_TYPE_BOOLEAN,
        .name		= "Flip Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 1,
    },
    #endif

	#if CONFIG_SENSOR_Scene
    {
        .id		= V4L2_CID_SCENE,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Scene Control",
        .minimum	= 0,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_DigitalZoom
    {
        .id		= V4L2_CID_ZOOM_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_ZOOM_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "DigitalZoom Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
    #endif

	#if CONFIG_SENSOR_Focus
	{
        .id		= V4L2_CID_FOCUS_RELATIVE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= -1,
        .maximum	= 1,
        .step		= 1,
        .default_value = 0,
    }, {
        .id		= V4L2_CID_FOCUS_ABSOLUTE,
        .type		= V4L2_CTRL_TYPE_INTEGER,
        .name		= "Focus Control",
        .minimum	= 0,
        .maximum	= 255,
        .step		= 1,
        .default_value = 125,
    },
    #endif

	#if CONFIG_SENSOR_Flash
	{
        .id		= V4L2_CID_FLASH,
        .type		= V4L2_CTRL_TYPE_MENU,
        .name		= "Flash Control",
        .minimum	= 0,
        .maximum	= 3,
        .step		= 1,
        .default_value = 0,
    },
	#endif
};

static int sensor_probe(struct i2c_client *client, const struct i2c_device_id *did);
static int sensor_video_probe(struct soc_camera_device *icd, struct i2c_client *client);
static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int sensor_g_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);
static int sensor_s_ext_controls(struct v4l2_subdev *sd,  struct v4l2_ext_controls *ext_ctrl);
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg);
static int sensor_resume(struct soc_camera_device *icd);
static int sensor_set_bus_param(struct soc_camera_device *icd,unsigned long flags);
static unsigned long sensor_query_bus_param(struct soc_camera_device *icd);
#if CONFIG_SENSOR_Effect
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#endif
#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
#endif
static int sensor_deactivate(struct i2c_client *client);

static struct soc_camera_ops sensor_ops =
{
    .suspend                     = sensor_suspend,
    .resume                       = sensor_resume,
    .set_bus_param		= sensor_set_bus_param,
    .query_bus_param	= sensor_query_bus_param,
    .controls		= sensor_controls,
    .menus                         = sensor_menus,
    .num_controls		= ARRAY_SIZE(sensor_controls),
    .num_menus		= ARRAY_SIZE(sensor_menus),
};

/* only one fixed colorspace per pixelcode */
struct sensor_datafmt {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
};

/* Find a data format by a pixel code in an array */
static const struct sensor_datafmt *sensor_find_datafmt(
	enum v4l2_mbus_pixelcode code, const struct sensor_datafmt *fmt,
	int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (fmt[i].code == code)
			return fmt + i;

	return NULL;
}

static const struct sensor_datafmt sensor_colour_fmts[] = {
    {V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
    {V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG}	
};

typedef struct sensor_info_priv_s
{
    int whiteBalance;
    int brightness;
    int contrast;
    int saturation;
    int effect;
    int scene;
    int digitalzoom;
    int focus;
    int flash;
    int exposure;
	bool snap2preview;
	bool video2preview;
    unsigned char mirror;                                        /* HFLIP */
    unsigned char flip;                                          /* VFLIP */
    unsigned int winseqe_cur_addr;
	struct sensor_datafmt fmt;
    unsigned int funmodule_state;
} sensor_info_priv_t;

struct sensor
{
    struct v4l2_subdev subdev;
    struct i2c_client *client;
    sensor_info_priv_t info_priv;
    int model;	/* V4L2_IDENT_OV* codes from v4l2-chip-ident.h */
#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_t tasklock_cnt;
#endif
	struct rk29camera_platform_data *sensor_io_request;
    struct rk29camera_gpio_res *sensor_gpio_res;
};

static struct sensor* to_sensor(const struct i2c_client *client)
{
    return container_of(i2c_get_clientdata(client), struct sensor, subdev);
}

static int sensor_task_lock(struct i2c_client *client, int lock)
{
#if CONFIG_SENSOR_I2C_NOSCHED
	int cnt = 3;
    struct sensor *sensor = to_sensor(client);

	if (lock) {
		if (atomic_read(&sensor->tasklock_cnt) == 0) {
			while ((atomic_read(&client->adapter->bus_lock.count) < 1) && (cnt>0)) {
				SENSOR_TR("\n %s will obtain i2c in atomic, but i2c bus is locked! Wait...\n",SENSOR_NAME_STRING());
				msleep(35);
				cnt--;
			}
			if ((atomic_read(&client->adapter->bus_lock.count) < 1) && (cnt<=0)) {
				SENSOR_TR("\n %s obtain i2c fail in atomic!!\n",SENSOR_NAME_STRING());
				goto sensor_task_lock_err;
			}
			preempt_disable();
		}

		atomic_add(1, &sensor->tasklock_cnt);
	} else {
		if (atomic_read(&sensor->tasklock_cnt) > 0) {
			atomic_sub(1, &sensor->tasklock_cnt);

			if (atomic_read(&sensor->tasklock_cnt) == 0)
				preempt_enable();
		}
	}
	return 0;
sensor_task_lock_err:
	return -1;  
#else
    return 0;
#endif

}

/* sensor register write */
static int sensor_write(struct i2c_client *client, u8 reg, u8 val)
{
    int err,cnt;
    u8 buf[2];
    struct i2c_msg msg[1];

    //buf[0] = reg >> 8;
    buf[0] = reg & 0xFF;
    buf[1] = val;
	//printk("%s----%d-----\n",__func__,__LINE__);

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = sizeof(buf);
    msg->scl_rate = CONFIG_SENSOR_I2C_SPEED;         /* ddl@rock-chips.com : 100kHz */
    msg->read_type = 0;               /* fpga i2c:0==I2C_NORMAL : direct use number not enum for don't want include spi_fpga.h */

    cnt = 3;
    err = -EAGAIN;

    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 1);

        if (err >= 0) {
            return 0;
        } else {
            SENSOR_TR("\n %s write reg(0x%x, val:0x%x) failed, try to write again!\n",SENSOR_NAME_STRING(),reg, val);
            udelay(10);
        }
    }

    return err;
}
#if 0
/* sensor register read */
static int sensor_read(struct i2c_client *client, u8 reg, u8 *val)
{
    int err,cnt;
    u8 buf[1];
    struct i2c_msg msg[2];

   // buf[0] = reg >> 8;
    buf[0] = reg & 0xFF;

    msg[0].addr = client->addr;
    msg[0].flags = client->flags;
    msg[0].buf = buf;
    msg[0].len = sizeof(buf);
    msg[0].scl_rate = CONFIG_SENSOR_I2C_SPEED;       /* ddl@rock-chips.com : 100kHz */
    msg[0].read_type = 2;   /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    msg[1].addr = client->addr;
    msg[1].flags = client->flags|I2C_M_RD;
    msg[1].buf = buf;
    msg[1].len = 1;
    msg[1].scl_rate = CONFIG_SENSOR_I2C_SPEED;                       /* ddl@rock-chips.com : 100kHz */
    msg[1].read_type = 2;                             /* fpga i2c:0==I2C_NO_STOP : direct use number not enum for don't want include spi_fpga.h */

    cnt = 3;
    err = -EAGAIN;
    while ((cnt-- > 0) && (err < 0)) {                       /* ddl@rock-chips.com :  Transfer again if transent is failed   */
        err = i2c_transfer(client->adapter, msg, 2);

        if (err >= 0) {
            *val = buf[0];
            return 0;
        } else {
        	SENSOR_TR("\n %s read reg(0x%x val:0x%x) failed, try to read again! \n",SENSOR_NAME_STRING(),reg, *val);
            udelay(10);
        }
    }

    return err;
}
#endif
/* write a array of registers  */
static int sensor_write_array(struct i2c_client *client, struct reginfo *regarray)
{
    int err = 0, cnt;
    int i = 0;
#if CONFIG_SENSOR_I2C_RDWRCHK    
	char valchk;
#endif

	cnt = 0;
	if (sensor_task_lock(client, 1) < 0)
		goto sensor_write_array_end;
    while (regarray[i].reg != 0xff)
    {
        err = sensor_write(client, regarray[i].reg, regarray[i].val);
        if (err < 0)
        {
            if (cnt-- > 0) {
			    SENSOR_TR("%s..write failed current reg:0x%x, Write array again !\n", SENSOR_NAME_STRING(),regarray[i].reg);
				i = 0;
				continue;
            } else {
                SENSOR_TR("%s..write array failed!!!\n", SENSOR_NAME_STRING());
                err = -EPERM;
				goto sensor_write_array_end;
            }
        } else {
        #if CONFIG_SENSOR_I2C_RDWRCHK
			sensor_read(client, regarray[i].reg, &valchk);
			if (valchk != regarray[i].val)
				SENSOR_TR("%s Reg:0x%x write(0x%x, 0x%x) fail\n",SENSOR_NAME_STRING(), regarray[i].reg, regarray[i].val, valchk);
		#endif
        }
        i++;
    }

sensor_write_array_end:
	sensor_task_lock(client,0);
	return err;
}
#if CONFIG_SENSOR_I2C_RDWRCHK
static int sensor_readchk_array(struct i2c_client *client, struct reginfo *regarray)
{
    int cnt;
    int i = 0;
	char valchk;

	cnt = 0;
	valchk = 0;
    while (regarray[i].reg != 0xff)
    {
		sensor_read(client, regarray[i].reg, &valchk);
		if (valchk != regarray[i].val)
			SENSOR_TR("%s Reg:0x%x read(0x%x, 0x%x) error\n",SENSOR_NAME_STRING(), regarray[i].reg, regarray[i].val, valchk);

        i++;
    }
    return 0;
}
#endif
static int sensor_ioctrl(struct soc_camera_device *icd,enum rk29sensor_power_cmd cmd, int on)
{
	struct soc_camera_link *icl = to_soc_camera_link(icd);
	int ret = 0;

    SENSOR_DG("%s %s  cmd(%d) on(%d)\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd,on);
	switch (cmd)
	{
		case Sensor_PowerDown:
		{
			if (icl->powerdown) {
				ret = icl->powerdown(icd->pdev, on);
				if (ret == RK29_CAM_IO_SUCCESS) {
					if (on == 0) {
						mdelay(2);
						if (icl->reset)
							icl->reset(icd->pdev);
					}
				} else if (ret == RK29_CAM_EIO_REQUESTFAIL) {
					ret = -ENODEV;
					goto sensor_power_end;
				}
			}
			break;
		}
		case Sensor_Flash:
		{
			struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    		struct sensor *sensor = to_sensor(client);

			if (sensor->sensor_io_request && sensor->sensor_io_request->sensor_ioctrl) {
				sensor->sensor_io_request->sensor_ioctrl(icd->pdev,Cam_Flash, on);
                if(on){
                    //flash off after 2 secs
            		hrtimer_cancel(&(flash_off_timer.timer));
            		hrtimer_start(&(flash_off_timer.timer),ktime_set(0, 800*1000*1000),HRTIMER_MODE_REL);
                    }
			}
            break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown!",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}
sensor_power_end:
	return ret;
}

static enum hrtimer_restart flash_off_func(struct hrtimer *timer){
	struct flash_timer *fps_timer = container_of(timer, struct flash_timer, timer);
    sensor_ioctrl(fps_timer->icd,Sensor_Flash,0);
	SENSOR_DG("%s %s !!!!!!",SENSOR_NAME_STRING(),__FUNCTION__);
    return 0;
    
}
static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl;
    const struct sensor_datafmt *fmt;
    int ret;

    SENSOR_DG("\n%s..%s.. \n",SENSOR_NAME_STRING(),__FUNCTION__);

     if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
          ret = -ENODEV;
          goto sensor_INIT_ERR;
     }
     msleep(100);
#if 0
    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;
    ret = sensor_write(client, 0x3012, 0x80);
    if (ret != 0)
    {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }

    mdelay(5);  //delay 5 microseconds
	/* check if it is an sensor sensor */
    ret = sensor_read(client, 0x300a, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    pid |= (value << 8);

    ret = sensor_read(client, 0x300b, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id low byte failed\n");
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }

    pid |= (value & 0xff);
    SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }
#endif
#if 0
   sensor_read(client,0x01,&value);
    pid = (value & 0xff);
    SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_INIT_ERR;
    }
#endif

    ret = sensor_write_array(client, sensor_init_data);
    if (ret != 0)
    {
        SENSOR_TR("error: %s initial failed\n",SENSOR_NAME_STRING());
        goto sensor_INIT_ERR;
    }
    sensor_task_lock(client,0);
    sensor->info_priv.winseqe_cur_addr  = (int)SENSOR_INIT_WINSEQADR;
    fmt = sensor_find_datafmt(SENSOR_INIT_PIXFMT,sensor_colour_fmts, ARRAY_SIZE(sensor_colour_fmts));
    if (!fmt) {
        SENSOR_TR("error: %s initial array colour fmts is not support!!",SENSOR_NAME_STRING());
        ret = -EINVAL;
        goto sensor_INIT_ERR;
    }
	sensor->info_priv.fmt = *fmt;

    /* sensor sensor information for initialization  */
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
	if (qctrl)
    	sensor->info_priv.whiteBalance = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_BRIGHTNESS);
	if (qctrl)
    	sensor->info_priv.brightness = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
	if (qctrl)
    	sensor->info_priv.effect = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EXPOSURE);
	if (qctrl)
        sensor->info_priv.exposure = qctrl->default_value;

	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SATURATION);
	if (qctrl)
        sensor->info_priv.saturation = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_CONTRAST);
	if (qctrl)
        sensor->info_priv.contrast = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_HFLIP);
	if (qctrl)
        sensor->info_priv.mirror = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_VFLIP);
	if (qctrl)
        sensor->info_priv.flip = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_SCENE);
	if (qctrl)
        sensor->info_priv.scene = qctrl->default_value;
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.digitalzoom = qctrl->default_value;

    /* ddl@rock-chips.com : if sensor support auto focus and flash, programer must run focus and flash code  */
	#if CONFIG_SENSOR_Focus
    sensor_set_focus();
    qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FOCUS_ABSOLUTE);
	if (qctrl)
        sensor->info_priv.focus = qctrl->default_value;
	#endif

	#if CONFIG_SENSOR_Flash
	qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_FLASH);
	if (qctrl)
        sensor->info_priv.flash = qctrl->default_value;
    flash_off_timer.icd = icd;
	flash_off_timer.timer.function = flash_off_func;
    #endif

    SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),((val == 0)?__FUNCTION__:"sensor_reinit"),icd->user_width,icd->user_height);
    sensor->info_priv.funmodule_state |= SENSOR_INIT_IS_OK;
    return 0;
sensor_INIT_ERR:
    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	sensor_task_lock(client,0);
	sensor_deactivate(client);
    return ret;
}

static int sensor_deactivate(struct i2c_client *client)
{
	struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    
	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);

	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
	
	sensor_ioctrl(icd, Sensor_PowerDown, 1);

	/* ddl@rock-chips.com : sensor config init width , because next open sensor quickly(soc_camera_open -> Try to configure with default parameters) */
	icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
	msleep(100);

    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	return 0;
}

static  struct reginfo sensor_power_down_sequence[]=
{
    {0xff,0xff}
};
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
    int ret;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if (pm_msg.event == PM_EVENT_SUSPEND) {
        SENSOR_DG("\n %s Enter Suspend.. \n", SENSOR_NAME_STRING());
        ret = sensor_write_array(client, sensor_power_down_sequence) ;
        if (ret != 0) {
            SENSOR_TR("\n %s..%s WriteReg Fail.. \n", SENSOR_NAME_STRING(),__FUNCTION__);
            return ret;
        } else {
            ret = sensor_ioctrl(icd, Sensor_PowerDown, 1);
            if (ret < 0) {
			    SENSOR_TR("\n %s suspend fail for turn on power!\n", SENSOR_NAME_STRING());
                return -EINVAL;
            }
        }
    } else {
        SENSOR_TR("\n %s cann't suppout Suspend..\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }
    return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{
	int ret;

    ret = sensor_ioctrl(icd, Sensor_PowerDown, 0);
    if (ret < 0) {
		SENSOR_TR("\n %s resume fail for turn on power!\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }

	SENSOR_DG("\n %s Enter Resume.. \n", SENSOR_NAME_STRING());

    return 0;

}

static int sensor_set_bus_param(struct soc_camera_device *icd,
                                unsigned long flags)
{

    return 0;
}

static unsigned long sensor_query_bus_param(struct soc_camera_device *icd)
{
    struct soc_camera_link *icl = to_soc_camera_link(icd);
    unsigned long flags = SENSOR_BUS_PARAM;

    return soc_camera_apply_sensor_flags(icl, flags);
}

static int sensor_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);

    mf->width	= icd->user_width;
	mf->height	= icd->user_height;
	mf->code	= sensor->info_priv.fmt.code;
	mf->colorspace	= sensor->info_priv.fmt.colorspace;
	mf->field	= V4L2_FIELD_NONE;

    return 0;
}
static bool sensor_fmt_capturechk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    bool ret = false;

	if ((mf->width == 1024) && (mf->height == 768)) {
		ret = true;
	} else if ((mf->width == 1280) && (mf->height == 1024)) {
		ret = true;
	} else if ((mf->width == 1600) && (mf->height == 1200)) {
		ret = true;
	} else if ((mf->width == 2048) && (mf->height == 1536)) {
		ret = true;
	} else if ((mf->width == 2592) && (mf->height == 1944)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is capture format\n", __FUNCTION__, mf->width, mf->height);
	return ret;
}

static bool sensor_fmt_videochk(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    bool ret = false;

	if ((mf->width == 1280) && (mf->height == 720)) {
		ret = true;
	} else if ((mf->width == 1920) && (mf->height == 1080)) {
		ret = true;
	}

	if (ret == true)
		SENSOR_DG("%s %dx%d is video format\n", __FUNCTION__, mf->width, mf->height);
	return ret;
}
static int sensor_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct sensor_datafmt *fmt;
	const struct v4l2_queryctrl *qctrl;
	struct soc_camera_device *icd = client->dev.platform_data;
    struct reginfo *winseqe_set_addr=NULL;
    int ret=0, set_w,set_h;

	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (!fmt) {
        ret = -EINVAL;
        goto sensor_s_fmt_end;
    }

	if (sensor->info_priv.fmt.code != mf->code) {
		switch (mf->code)
		{
			case V4L2_MBUS_FMT_YUYV8_2X8:
			{
				winseqe_set_addr = sensor_ClrFmt_YUYV;
				break;
			}
			case V4L2_MBUS_FMT_UYVY8_2X8:
			{
				winseqe_set_addr = sensor_ClrFmt_UYVY;
				break;
			}
			default:
				break;
		}
		if (winseqe_set_addr != NULL) {
            sensor_write_array(client, winseqe_set_addr);
			sensor->info_priv.fmt.code = mf->code;
            sensor->info_priv.fmt.colorspace= mf->colorspace;            
			SENSOR_DG("%s v4l2_mbus_code:%d set success!\n", SENSOR_NAME_STRING(),mf->code);
		} else {
			SENSOR_TR("%s v4l2_mbus_code:%d is invalidate!\n", SENSOR_NAME_STRING(),mf->code);
		}
	}

    set_w = mf->width;
    set_h = mf->height;

	if (((set_w <= 176) && (set_h <= 144)) &&( sensor_qcif[0].reg!=0xff))
	{
		winseqe_set_addr = sensor_qcif;
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && (sensor_qvga[0].reg!=0xff))
    {
        winseqe_set_addr = sensor_qvga;
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && (sensor_cif[0].reg!=0xff))
    {
        winseqe_set_addr = sensor_cif;
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) &&( sensor_vga[0].reg!=0xff))
    {
        winseqe_set_addr = sensor_vga;
        set_w =640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && (sensor_svga[0].reg!=0xff))
    {
        winseqe_set_addr = sensor_svga;
        set_w = 800;
        set_h = 600;
    }
	else if (((set_w <= 1024) && (set_h <= 768)) &&( sensor_xga[0].reg!=0xff))
    {
        winseqe_set_addr = sensor_xga;
        set_w = 1024;
        set_h = 768;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && (sensor_sxga[0].reg!=0xff))
    {
        winseqe_set_addr = sensor_sxga;
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && (sensor_uxga[0].reg!=0xff))
    {
        winseqe_set_addr = sensor_uxga;
        set_w = 1600;
        set_h = 1200;
    }
    else
    {
        winseqe_set_addr = SENSOR_INIT_WINSEQADR;               /* ddl@rock-chips.com : Sensor output smallest size if  isn't support app  */
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;		
		SENSOR_TR("\n %s..%s Format is Invalidate. pix->width = %d.. pix->height = %d\n",SENSOR_NAME_STRING(),__FUNCTION__,mf->width,mf->height);
    }

    if ((int)winseqe_set_addr  != sensor->info_priv.winseqe_cur_addr) {
        #if CONFIG_SENSOR_Flash
        if (sensor_fmt_capturechk(sd,mf) == true) {      /* ddl@rock-chips.com : Capture */
            if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                sensor_ioctrl(icd, Sensor_Flash, Flash_On);
                SENSOR_DG("%s flash on in capture!\n", SENSOR_NAME_STRING());
            }           
        } else {                                        /* ddl@rock-chips.com : Video */
            if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
                SENSOR_DG("%s flash off in preivew!\n", SENSOR_NAME_STRING());
            }
        }
        #endif
        ret |= sensor_write_array(client, winseqe_set_addr);
        if (ret != 0) {
            SENSOR_TR("%s set format capability failed\n", SENSOR_NAME_STRING());
            #if CONFIG_SENSOR_Flash
            if (sensor_fmt_capturechk(sd,mf) == true) {
                if ((sensor->info_priv.flash == 1) || (sensor->info_priv.flash == 2)) {
                    sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
                    SENSOR_TR("%s Capture format set fail, flash off !\n", SENSOR_NAME_STRING());
                }
            }
            #endif
            goto sensor_s_fmt_end;
        }

        sensor->info_priv.winseqe_cur_addr  = (int)winseqe_set_addr;

		if (sensor_fmt_capturechk(sd,mf) == true) {				    /* ddl@rock-chips.com : Capture */
        #if CONFIG_SENSOR_Effect
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
        #endif
        #if CONFIG_SENSOR_WhiteBalance
			if (sensor->info_priv.whiteBalance != 0) {
				qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
				sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
			}
        #endif
			sensor->info_priv.snap2preview = true;
		} else if (sensor_fmt_videochk(sd,mf) == true) {			/* ddl@rock-chips.com : Video */
		#if CONFIG_SENSOR_Effect
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
        #endif
        #if CONFIG_SENSOR_WhiteBalance
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
			sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
        #endif
			sensor->info_priv.video2preview = true;
		} else if ((sensor->info_priv.snap2preview == true) || (sensor->info_priv.video2preview == true)) {
		#if CONFIG_SENSOR_Effect
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
        #endif
        #if CONFIG_SENSOR_WhiteBalance
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
			sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
        #endif
			sensor->info_priv.video2preview = false;
			sensor->info_priv.snap2preview = false;
		}
        SENSOR_DG("\n%s..%s.. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),__FUNCTION__,set_w,set_h);
    }
    else
    {
        SENSOR_DG("\n %s .. Current Format is validate. icd->width = %d.. icd->height %d\n",SENSOR_NAME_STRING(),set_w,set_h);
    }

	mf->width = set_w;
    mf->height = set_h;

sensor_s_fmt_end:
    return ret;
}

static int sensor_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct sensor_datafmt *fmt;
    int ret = 0,set_w,set_h;
   
	fmt = sensor_find_datafmt(mf->code, sensor_colour_fmts,
				   ARRAY_SIZE(sensor_colour_fmts));
	if (fmt == NULL) {
		fmt = &sensor->info_priv.fmt;
        mf->code = fmt->code;
	} 

    if (mf->height > SENSOR_MAX_HEIGHT)
        mf->height = SENSOR_MAX_HEIGHT;
    else if (mf->height < SENSOR_MIN_HEIGHT)
        mf->height = SENSOR_MIN_HEIGHT;

    if (mf->width > SENSOR_MAX_WIDTH)
        mf->width = SENSOR_MAX_WIDTH;
    else if (mf->width < SENSOR_MIN_WIDTH)
        mf->width = SENSOR_MIN_WIDTH;

    set_w = mf->width;
    set_h = mf->height;

	if (((set_w <= 176) && (set_h <= 144)) &&( sensor_qcif[0].reg!=0xff))
	{
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && (sensor_qvga[0].reg!=0xff))
    {
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && (sensor_cif[0].reg!=0xff))
    {
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) &&( sensor_vga[0].reg!=0xff))
    {
        set_w =640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && (sensor_svga[0].reg!=0xff))
    {
        set_w = 800;
        set_h = 600;
    }
	else if (((set_w <= 1024) && (set_h <= 768)) &&( sensor_xga[0].reg!=0xff))
    {
        set_w = 1024;
        set_h = 768;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && (sensor_sxga[0].reg!=0xff))
    {
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && (sensor_uxga[0].reg!=0xff))
    {
        set_w = 1600;
        set_h = 1200;
    }
    else
    {
        set_w = SENSOR_INIT_WIDTH;
        set_h = SENSOR_INIT_HEIGHT;		
    }
    mf->width = set_w;
    mf->height = set_h;  
    mf->colorspace = fmt->colorspace;
    
    return ret;
}

 static int sensor_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *id)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);

    if (id->match.type != V4L2_CHIP_MATCH_I2C_ADDR)
        return -EINVAL;

    if (id->match.addr != client->addr)
        return -ENODEV;

    id->ident = SENSOR_V4L2_IDENT;      /* ddl@rock-chips.com :  Return OV2655  identifier */
    id->revision = 0;

    return 0;
}
#if CONFIG_SENSOR_Brightness
static int sensor_set_brightness(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_BrightnessSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_BrightnessSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Effect
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_EffectSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_EffectSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Exposure
static int sensor_set_exposure(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_ExposureSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_ExposureSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Saturation
static int sensor_set_saturation(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_SaturationSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_SaturationSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Contrast
static int sensor_set_contrast(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_ContrastSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_ContrastSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Mirror
static int sensor_set_mirror(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_MirrorSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_MirrorSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Flip
static int sensor_set_flip(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_FlipSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_FlipSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Scene
static int sensor_set_scene(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_SceneSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_SceneSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
    SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_WhiteBalance
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

    if ((value >= qctrl->minimum) && (value <= qctrl->maximum))
    {
        if (sensor_WhiteBalanceSeqe[value - qctrl->minimum] != NULL)
        {
            if (sensor_write_array(client, sensor_WhiteBalanceSeqe[value - qctrl->minimum]) != 0)
            {
                SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
                return -EINVAL;
            }
            SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
            return 0;
        }
    }
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_DigitalZoom
static int sensor_set_digitalzoom(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
	const struct v4l2_queryctrl *qctrl_info;
    int digitalzoom_cur, digitalzoom_total;

	qctrl_info = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_ZOOM_ABSOLUTE);
	if (qctrl_info)
		return -EINVAL;

    digitalzoom_cur = sensor->info_priv.digitalzoom;
    digitalzoom_total = qctrl_info->maximum;

    if ((value > 0) && (digitalzoom_cur >= digitalzoom_total))
    {
        SENSOR_TR("%s digitalzoom is maximum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if  ((value < 0) && (digitalzoom_cur <= qctrl_info->minimum))
    {
        SENSOR_TR("%s digitalzoom is minimum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if ((value > 0) && ((digitalzoom_cur + value) > digitalzoom_total))
    {
        value = digitalzoom_total - digitalzoom_cur;
    }

    if ((value < 0) && ((digitalzoom_cur + value) < 0))
    {
        value = 0 - digitalzoom_cur;
    }

    digitalzoom_cur += value;

    if (sensor_ZoomSeqe[digitalzoom_cur] != NULL)
    {
        if (sensor_write_array(client, sensor_ZoomSeqe[digitalzoom_cur]) != 0)
        {
            SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
            return -EINVAL;
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
        return 0;
    }

    return -EINVAL;
}
#endif
#if CONFIG_SENSOR_Flash
static int sensor_set_flash(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value)
{    
    if ((value >= qctrl->minimum) && (value <= qctrl->maximum)) {
        if (value == 3) {       /* ddl@rock-chips.com: torch */
            sensor_ioctrl(icd, Sensor_Flash, Flash_Torch);   /* Flash On */
        } else {
            sensor_ioctrl(icd, Sensor_Flash, Flash_Off);
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, value);
        return 0;
    }
    
	SENSOR_TR("\n %s..%s valure = %d is invalidate..    \n",SENSOR_NAME_STRING(),__FUNCTION__,value);
    return -EINVAL;
}
#endif

static int sensor_g_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    const struct v4l2_queryctrl *qctrl;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
        case V4L2_CID_BRIGHTNESS:
            {
                ctrl->value = sensor->info_priv.brightness;
                break;
            }
        case V4L2_CID_SATURATION:
            {
                ctrl->value = sensor->info_priv.saturation;
                break;
            }
        case V4L2_CID_CONTRAST:
            {
                ctrl->value = sensor->info_priv.contrast;
                break;
            }
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                ctrl->value = sensor->info_priv.whiteBalance;
                break;
            }
        case V4L2_CID_EXPOSURE:
            {
                ctrl->value = sensor->info_priv.exposure;
                break;
            }
        case V4L2_CID_HFLIP:
            {
                ctrl->value = sensor->info_priv.mirror;
                break;
            }
        case V4L2_CID_VFLIP:
            {
                ctrl->value = sensor->info_priv.flip;
                break;
            }
        default :
                break;
    }
    return 0;
}



static int sensor_s_control(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;
    const struct v4l2_queryctrl *qctrl;


    qctrl = soc_camera_find_qctrl(&sensor_ops, ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ctrl->id);
        return -EINVAL;
    }

    switch (ctrl->id)
    {
#if CONFIG_SENSOR_Brightness
        case V4L2_CID_BRIGHTNESS:
            {
                if (ctrl->value != sensor->info_priv.brightness)
                {
                    if (sensor_set_brightness(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.brightness = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Exposure
        case V4L2_CID_EXPOSURE:
            {
                if (ctrl->value != sensor->info_priv.exposure)
                {
                    if (sensor_set_exposure(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.exposure = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Saturation
        case V4L2_CID_SATURATION:
            {
                if (ctrl->value != sensor->info_priv.saturation)
                {
                    if (sensor_set_saturation(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.saturation = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Contrast
        case V4L2_CID_CONTRAST:
            {
                if (ctrl->value != sensor->info_priv.contrast)
                {
                    if (sensor_set_contrast(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.contrast = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_WhiteBalance
        case V4L2_CID_DO_WHITE_BALANCE:
            {
                if (ctrl->value != sensor->info_priv.whiteBalance)
                {
                    if (sensor_set_whiteBalance(icd, qctrl,ctrl->value) != 0)
                    {
                        return -EINVAL;
                    }
                    sensor->info_priv.whiteBalance = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Mirror
        case V4L2_CID_HFLIP:
            {
                if (ctrl->value != sensor->info_priv.mirror)
                {
                    if (sensor_set_mirror(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.mirror = ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Flip
        case V4L2_CID_VFLIP:
            {
                if (ctrl->value != sensor->info_priv.flip)
                {
                    if (sensor_set_flip(icd, qctrl,ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.flip = ctrl->value;
                }
                break;
            }
#endif
        default:
            break;
    }

    return 0;
}
static int sensor_g_ext_control(struct soc_camera_device *icd , struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

    switch (ext_ctrl->id)
    {
        case V4L2_CID_SCENE:
            {
                ext_ctrl->value = sensor->info_priv.scene;
                break;
            }
        case V4L2_CID_EFFECT:
            {
                ext_ctrl->value = sensor->info_priv.effect;
                break;
            }
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                ext_ctrl->value = sensor->info_priv.digitalzoom;
                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                ext_ctrl->value = sensor->info_priv.focus;
                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                return -EINVAL;
            }
        case V4L2_CID_FLASH:
            {
                ext_ctrl->value = sensor->info_priv.flash;
                break;
            }
        default :
            break;
    }
    return 0;
}
static int sensor_s_ext_control(struct soc_camera_device *icd, struct v4l2_ext_control *ext_ctrl)
{
    const struct v4l2_queryctrl *qctrl;
    struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
    struct sensor *sensor = to_sensor(client);
    int val_offset;

    qctrl = soc_camera_find_qctrl(&sensor_ops, ext_ctrl->id);

    if (!qctrl)
    {
        SENSOR_TR("\n %s ioctrl id = %d  is invalidate \n", SENSOR_NAME_STRING(), ext_ctrl->id);
        return -EINVAL;
    }

	val_offset = 0;
    switch (ext_ctrl->id)
    {
#if CONFIG_SENSOR_Scene
        case V4L2_CID_SCENE:
            {
                if (ext_ctrl->value != sensor->info_priv.scene)
                {
                    if (sensor_set_scene(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.scene = ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Effect
        case V4L2_CID_EFFECT:
            {
                if (ext_ctrl->value != sensor->info_priv.effect)
                {
                    if (sensor_set_effect(icd, qctrl,ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.effect= ext_ctrl->value;
                }
                break;
            }
#endif
#if CONFIG_SENSOR_DigitalZoom
        case V4L2_CID_ZOOM_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                if (ext_ctrl->value != sensor->info_priv.digitalzoom)
                {
                    val_offset = ext_ctrl->value -sensor->info_priv.digitalzoom;

                    if (sensor_set_digitalzoom(icd, qctrl,&val_offset) != 0)
                        return -EINVAL;
                    sensor->info_priv.digitalzoom += val_offset;

                    SENSOR_DG("%s digitalzoom is %x\n",SENSOR_NAME_STRING(),  sensor->info_priv.digitalzoom);
                }

                break;
            }
        case V4L2_CID_ZOOM_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    if (sensor_set_digitalzoom(icd, qctrl,&ext_ctrl->value) != 0)
                        return -EINVAL;
                    sensor->info_priv.digitalzoom += ext_ctrl->value;

                    SENSOR_DG("%s digitalzoom is %x\n", SENSOR_NAME_STRING(), sensor->info_priv.digitalzoom);
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Focus
        case V4L2_CID_FOCUS_ABSOLUTE:
            {
                if ((ext_ctrl->value < qctrl->minimum) || (ext_ctrl->value > qctrl->maximum))
                    return -EINVAL;

                if (ext_ctrl->value != sensor->info_priv.focus)
                {
                    val_offset = ext_ctrl->value -sensor->info_priv.focus;

                    sensor->info_priv.focus += val_offset;
                }

                break;
            }
        case V4L2_CID_FOCUS_RELATIVE:
            {
                if (ext_ctrl->value)
                {
                    sensor->info_priv.focus += ext_ctrl->value;

                    SENSOR_DG("%s focus is %x\n", SENSOR_NAME_STRING(), sensor->info_priv.focus);
                }
                break;
            }
#endif
#if CONFIG_SENSOR_Flash
        case V4L2_CID_FLASH:
            {
                if (sensor_set_flash(icd, qctrl,ext_ctrl->value) != 0)
                    return -EINVAL;
                sensor->info_priv.flash = ext_ctrl->value;

                SENSOR_DG("%s flash is %x\n",SENSOR_NAME_STRING(), sensor->info_priv.flash);
                break;
            }
#endif
        default:
            break;
    }

    return 0;
}

static int sensor_g_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;


    for (i=0; i<ext_ctrl->count; i++) {
        if (sensor_g_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

static int sensor_s_ext_controls(struct v4l2_subdev *sd, struct v4l2_ext_controls *ext_ctrl)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    int i, error_cnt=0, error_idx=-1;


    for (i=0; i<ext_ctrl->count; i++) {
        if (sensor_s_ext_control(icd, &ext_ctrl->controls[i]) != 0) {
            error_cnt++;
            error_idx = i;
        }
    }

    if (error_cnt > 1)
        error_idx = ext_ctrl->count;

    if (error_idx != -1) {
        ext_ctrl->error_idx = error_idx;
        return -EINVAL;
    } else {
        return 0;
    }
}

/* Interface active, can use i2c. If it fails, it can indeed mean, that
 * this wasn't our capture interface, so, we wait for the right one */
static int sensor_video_probe(struct soc_camera_device *icd,
			       struct i2c_client *client)
{
    int ret;
    

    /* We must have a parent by now. And it cannot be a wrong one.
     * So this entire test is completely redundant. */
    if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_video_probe_err;
	}
#if 0
    /* soft reset */
    ret = sensor_write(client, 0x3012, 0x80);
    if (ret != 0)
    {
        SENSOR_TR("soft reset %s failed\n",SENSOR_NAME_STRING());
        return -ENODEV;
    }
    mdelay(5);          //delay 5 microseconds

    /* check if it is an sensor sensor */
    ret = sensor_read(client, 0x300a, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id high byte failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    
    ret = sensor_read(client, 0x01, &value);
    if (ret != 0) {
        SENSOR_TR("read chip id low byte failed\n");
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    pid = (value & 0xff);
    SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }
#endif

    return 0;

sensor_video_probe_err:

    return ret;
}
static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct soc_camera_device *icd = client->dev.platform_data;
    struct sensor *sensor = to_sensor(client);
    int ret = 0;
#if CONFIG_SENSOR_Flash	
    int i;
#endif
    
	SENSOR_DG("\n%s..%s..cmd:%x \n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
	switch (cmd)
	{
		case RK29_CAM_SUBDEV_DEACTIVATE:
		{
			sensor_deactivate(client);
			break;
		}

		case RK29_CAM_SUBDEV_IOREQUEST:
		{
			sensor->sensor_io_request = (struct rk29camera_platform_data*)arg;           
            if (sensor->sensor_io_request != NULL) { 
                sensor->sensor_gpio_res = NULL;
                for (i=0; i<RK29_CAM_SUPPORT_NUMS;i++) {
                    if (sensor->sensor_io_request->gpio_res[i].dev_name && 
                        (strcmp(sensor->sensor_io_request->gpio_res[i].dev_name, dev_name(icd->pdev)) == 0)) {
                        sensor->sensor_gpio_res = (struct rk29camera_gpio_res*)&sensor->sensor_io_request->gpio_res[i];
                    }
                }
                if (sensor->sensor_gpio_res == NULL) {
                    SENSOR_TR("%s %s obtain gpio resource failed when RK29_CAM_SUBDEV_IOREQUEST \n",SENSOR_NAME_STRING(),__FUNCTION__);
                    ret = -EINVAL;
                    goto sensor_ioctl_end;
                }
            } else {
                SENSOR_TR("%s %s RK29_CAM_SUBDEV_IOREQUEST fail\n",SENSOR_NAME_STRING(),__FUNCTION__);
                ret = -EINVAL;
                goto sensor_ioctl_end;
            }
            /* ddl@rock-chips.com : if gpio_flash havn't been set in board-xxx.c, sensor driver must notify is not support flash control 
               for this project */
            #if CONFIG_SENSOR_Flash	
        	if (sensor->sensor_gpio_res) { 
                if (sensor->sensor_gpio_res->gpio_flash == INVALID_GPIO) {
                    for (i = 0; i < icd->ops->num_controls; i++) {
                		if (V4L2_CID_FLASH == icd->ops->controls[i].id) {
                			//memset((char*)&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));  
                              sensor_controls[i].id=0xffff;         			
                		}
                    }
                    sensor->info_priv.flash = 0xff;
                    SENSOR_DG("%s flash gpio is invalidate!\n",SENSOR_NAME_STRING());
                }else{ //two cameras are the same,need to deal diffrently ,zyc
                    for (i = 0; i < icd->ops->num_controls; i++) {
                           if(0xffff == icd->ops->controls[i].id){
                              sensor_controls[i].id=V4L2_CID_FLASH;
                           }               
                    }
                }
        	}
            #endif
			break;
		}
		default:
		{
			SENSOR_TR("%s %s cmd(0x%x) is unknown !\n",SENSOR_NAME_STRING(),__FUNCTION__,cmd);
			break;
		}
	}
sensor_ioctl_end:
	return ret;

}
static int sensor_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			    enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(sensor_colour_fmts))
		return -EINVAL;

	*code = sensor_colour_fmts[index].code;
	return 0;
}
static struct v4l2_subdev_core_ops sensor_subdev_core_ops = {
	.init		= sensor_init,
	.g_ctrl		= sensor_g_control,
	.s_ctrl		= sensor_s_control,
	.g_ext_ctrls          = sensor_g_ext_controls,
	.s_ext_ctrls          = sensor_s_ext_controls,
	.g_chip_ident	= sensor_g_chip_ident,
	.ioctl = sensor_ioctl,
};

static struct v4l2_subdev_video_ops sensor_subdev_video_ops = {
	.s_mbus_fmt	= sensor_s_fmt,
	.g_mbus_fmt	= sensor_g_fmt,
	.try_mbus_fmt	= sensor_try_fmt,
	.enum_mbus_fmt	= sensor_enum_fmt,
};

static struct v4l2_subdev_ops sensor_subdev_ops = {
	.core	= &sensor_subdev_core_ops,
	.video = &sensor_subdev_video_ops,
};

static int sensor_probe(struct i2c_client *client,
			 const struct i2c_device_id *did)
{
    struct sensor *sensor;
    struct soc_camera_device *icd = client->dev.platform_data;
    struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
    struct soc_camera_link *icl;
    int ret;

    SENSOR_DG("\n%s..%s..%d..\n",__FUNCTION__,__FILE__,__LINE__);
    if (!icd) {
        dev_err(&client->dev, "%s: missing soc-camera data!\n",SENSOR_NAME_STRING());
        return -EINVAL;
    }

    icl = to_soc_camera_link(icd);
    if (!icl) {
        dev_err(&client->dev, "%s driver needs platform data\n", SENSOR_NAME_STRING());
        return -EINVAL;
    }

    if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
        dev_warn(&adapter->dev,
        	 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
        return -EIO;
    }

    sensor = kzalloc(sizeof(struct sensor), GFP_KERNEL);
    if (!sensor)
        return -ENOMEM;

    v4l2_i2c_subdev_init(&sensor->subdev, client, &sensor_subdev_ops);

    /* Second stage probe - when a capture adapter is there */
    icd->ops		= &sensor_ops;
    sensor->info_priv.fmt = sensor_colour_fmts[0];
	#if CONFIG_SENSOR_I2C_NOSCHED
	atomic_set(&sensor->tasklock_cnt,0);
	#endif

    ret = sensor_video_probe(icd, client);
    if (ret < 0) {
        icd->ops = NULL;
        i2c_set_clientdata(client, NULL);
        kfree(sensor);
		sensor = NULL;
    }
	hrtimer_init(&(flash_off_timer.timer), CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    SENSOR_DG("\n%s..%s..%d  ret = %x \n",__FUNCTION__,__FILE__,__LINE__,ret);
    return ret;
}

static int sensor_remove(struct i2c_client *client)
{
    struct sensor *sensor = to_sensor(client);
    struct soc_camera_device *icd = client->dev.platform_data;

    icd->ops = NULL;
    i2c_set_clientdata(client, NULL);
    client->driver = NULL;
    kfree(sensor);
	sensor = NULL;
    return 0;
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME_STRING(), 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_i2c_driver = {
	.driver = {
		.name = SENSOR_NAME_STRING(),
	},
	.probe		= sensor_probe,
	.remove		= sensor_remove,
	.id_table	= sensor_id,
};

static int __init sensor_mod_init(void)
{
    SENSOR_DG("\n%s..%s.. \n",__FUNCTION__,SENSOR_NAME_STRING());
    return i2c_add_driver(&sensor_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
    i2c_del_driver(&sensor_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION(SENSOR_NAME_STRING(Camera sensor driver));
MODULE_AUTHOR("ddl <kernel@rock-chips>");
MODULE_LICENSE("GPL");
