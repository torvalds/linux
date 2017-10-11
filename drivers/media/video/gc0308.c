/*
 * drivers/media/video/gc0308.c
 *
 * Copyright (C) ROCKCHIP, Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "generic_sensor.h"
/*
*      Driver Version Note
*v0.0.1: this driver is compatible with generic_sensor
*v0.0.3:
*        add sensor_focus_af_const_pause_usr_cb;
*/
static int version = KERNEL_VERSION(0,0,3);
module_param(version, int, S_IRUGO);



static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug >= level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_GC0308
#define SENSOR_V4L2_IDENT V4L2_IDENT_GC0308
#define SENSOR_ID 0x9b
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
													 V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_LOW|\
													 V4L2_MBUS_DATA_ACTIVE_HIGH  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W					 640
#define SENSOR_PREVIEW_H					 480
#define SENSOR_PREVIEW_FPS					 15000	   // 15fps 
#define SENSOR_FULLRES_L_FPS				 7500	   // 7.5fps
#define SENSOR_FULLRES_H_FPS				 7500	   // 7.5fps
#define SENSOR_720P_FPS 					 0
#define SENSOR_1080P_FPS					 0

#define SENSOR_REGISTER_LEN 				 1		   // sensor register address bytes
#define SENSOR_VALUE_LEN					 1		   // sensor register value bytes
									
static unsigned int SensorConfiguration = (CFG_WhiteBalance|CFG_Effect|CFG_Scene);
static unsigned int SensorChipID[] = {SENSOR_ID};
/* Sensor Driver Configuration End */


#define SENSOR_NAME_STRING(a) STR(CONS(SENSOR_NAME, a))
#define SENSOR_NAME_VARFUN(a) CONS(SENSOR_NAME, a)

#define SensorRegVal(a,b) CONS4(SensorReg,SENSOR_REGISTER_LEN,Val,SENSOR_VALUE_LEN)(a,b)
#define sensor_write(client,reg,v) CONS4(sensor_write_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
#define sensor_read(client,reg,v) CONS4(sensor_read_reg,SENSOR_REGISTER_LEN,val,SENSOR_VALUE_LEN)(client,(reg),(v))
#define sensor_write_array generic_sensor_write_array

struct sensor_parameter
{
	unsigned int PreviewDummyPixels;
	unsigned int CaptureDummyPixels;
	unsigned int preview_exposure;
	unsigned short int preview_line_width;
	unsigned short int preview_gain;

	unsigned short int PreviewPclk;
	unsigned short int CapturePclk;
	char awb[6];
};

struct specific_sensor{
	struct generic_sensor common_sensor;
	//define user data below
	struct sensor_parameter parameter;

};

/*
*  The follow setting need been filled.
*  
*  Must Filled:
*  sensor_init_data :				Sensor initial setting;
*  sensor_fullres_lowfps_data : 	Sensor full resolution setting with best auality, recommand for video;
*  sensor_preview_data :			Sensor preview resolution setting, recommand it is vga or svga;
*  sensor_softreset_data :			Sensor software reset register;
*  sensor_check_id_data :			Sensir chip id register;
*
*  Optional filled:
*  sensor_fullres_highfps_data: 	Sensor full resolution setting with high framerate, recommand for video;
*  sensor_720p: 					Sensor 720p setting, it is for video;
*  sensor_1080p:					Sensor 1080p setting, it is for video;
*
*  :::::WARNING:::::
*  The SensorEnd which is the setting end flag must be filled int the last of each setting;
*/

/* Sensor initial setting */
static struct rk_sensor_reg sensor_init_data[] ={
		{0xfe , 0x80},		
			
		{0xfe , 0x00},	 // set page0
		
		{0xd2 , 0x10},	 // close AEC
		{0x22 , 0x55},	 // close AWB
	
		{0x03 , 0x01},									
		{0x04 , 0x2c},									
		{0x5a , 0x56},
		{0x5b , 0x40},
		{0x5c , 0x4a},			
	
		{0x22 , 0x57},	 // Open AWB
	
		{0x01 , 0xfa},									  
		{0x02 , 0x70},									
		{0x0f , 0x01},									
																	   
																	   
		{0xe2 , 0x00},	 //anti-flicker step [11:8] 	
		{0xe3 , 0x64},	 //anti-flicker step [7:0]		
																		   
		{0xe4 , 0x02},	 //exp level 1	16.67fps		
		{0xe5 , 0x58},									
		{0xe6 , 0x03},	 //exp level 2	12.5fps 		
		{0xe7 , 0x20},									
		{0xe8 , 0x04},	 //exp level 3	8.33fps 		
		{0xe9 , 0xb0},									
		{0xea , 0x09},	 //exp level 4	4.00fps 		
		{0xeb , 0xc4},									
	
		//{0xec , 0x20},
		   
		{0x05 , 0x00},									
		{0x06 , 0x00},									
		{0x07 , 0x00},									
		{0x08 , 0x00},									
		{0x09 , 0x01},									
		{0x0a , 0xe8},									
		{0x0b , 0x02},									
		{0x0c , 0x88},									
		{0x0d , 0x02},									
		{0x0e , 0x02},									
		{0x10 , 0x22},									
		{0x11 , 0xfd},									
		{0x12 , 0x2a},									
		{0x13 , 0x00},									
		{0x14 , 0x10},	//0x10
		//-------------H_V_Switch(4)---------------//
		/*	1:	// normal
					{0x14 , 0x10},			
			2:	// IMAGE_H_MIRROR
					{0x14 , 0x11},
					
			3:	// IMAGE_V_MIRROR
					{0x14 , 0x12},
					
			4:	// IMAGE_HV_MIRROR
					{0x14 , 0x13},*/										
		{0x15 , 0x0a},									
		{0x16 , 0x05},									
		{0x17 , 0x01},									
		{0x18 , 0x44},									
		{0x19 , 0x44},									
		{0x1a , 0x1e},									
		{0x1b , 0x00},									
		{0x1c , 0xc1},									
		{0x1d , 0x08},									
		{0x1e , 0x60},									
		{0x1f , 0x17},									
																	   
																		 
		{0x20 , 0xff},									
		{0x21 , 0xf8},									
		{0x22 , 0x57},									
		{0x24 , 0xa2},									
		{0x25 , 0x0f},									
																		 
		//output sync_mode												 
		{0x26 , 0x02},	 //0x03  20101016 zhj								  
		{0x2f , 0x01},									
		{0x30 , 0xf7},									
		{0x31 , 0x50},
		{0x32 , 0x00},
		{0x39 , 0x04},
		{0x3a , 0x18},
		{0x3b , 0x20},									
		{0x3c , 0x00},									
		{0x3d , 0x00},									
		{0x3e , 0x00},									
		{0x3f , 0x00},									
		{0x50 , 0x10},									
		{0x53 , 0x82},									
		{0x54 , 0x80},									
		{0x55 , 0x80},									
		{0x56 , 0x82},									
		{0x8b , 0x40},									
		{0x8c , 0x40},									
		{0x8d , 0x40},									
		{0x8e , 0x2e},									
		{0x8f , 0x2e},									
		{0x90 , 0x2e},									
		{0x91 , 0x3c},									
		{0x92 , 0x50},									
		{0x5d , 0x12},									
		{0x5e , 0x1a},									
		{0x5f , 0x24},									
		{0x60 , 0x07},									
		{0x61 , 0x15},									
		{0x62 , 0x08},									
		{0x64 , 0x03},									
		{0x66 , 0xe8},									
		{0x67 , 0x86},									
		{0x68 , 0xa2},									
		{0x69 , 0x18},									
		{0x6a , 0x0f},									
		{0x6b , 0x00},									
		{0x6c , 0x5f},									
		{0x6d , 0x8f},									
		{0x6e , 0x55},									
		{0x6f , 0x38},									
		{0x70 , 0x15},									
		{0x71 , 0x33},									
		{0x72 , 0xdc},									
		{0x73 , 0x80},									
		{0x74 , 0x02},									
		{0x75 , 0x3f},									
		{0x76 , 0x02},									
		{0x77 , 0x36},									
		{0x78 , 0x88},									
		{0x79 , 0x81},									
		{0x7a , 0x81},									
		{0x7b , 0x22},									
		{0x7c , 0xff},									
		{0x93 , 0x48},									
		{0x94 , 0x00},									
		{0x95 , 0x05},									
		{0x96 , 0xe8},									
		{0x97 , 0x40},									
		{0x98 , 0xf0},									
		{0xb1 , 0x38},									
		{0xb2 , 0x38},									
		{0xbd , 0x38},									
		{0xbe , 0x36},									
		{0xd0 , 0xc9},									
		{0xd1 , 0x10},									
		//{0xd2 , 0x90},								
		{0xd3 , 0x80},									
		{0xd5 , 0xf2},									
		{0xd6 , 0x16},									
		{0xdb , 0x92},									
		{0xdc , 0xa5},									
		{0xdf , 0x23},									
		{0xd9 , 0x00},									
		{0xda , 0x00},									
		{0xe0 , 0x09},									
								   
		{0xed , 0x04},									
		{0xee , 0xa0},									
		{0xef , 0x40},									
		{0x80 , 0x03},									
		{0x80 , 0x03},									
		{0x9F , 0x10},									
		{0xA0 , 0x20},									
		{0xA1 , 0x38},									
		{0xA2 , 0x4E},									
		{0xA3 , 0x63},									
		{0xA4 , 0x76},									
		{0xA5 , 0x87},									
		{0xA6 , 0xA2},									
		{0xA7 , 0xB8},									
		{0xA8 , 0xCA},									
		{0xA9 , 0xD8},									
		{0xAA , 0xE3},									
		{0xAB , 0xEB},									
		{0xAC , 0xF0},									
		{0xAD , 0xF8},									
		{0xAE , 0xFD},									
		{0xAF , 0xFF},									
		 /*GC0308_GAMMA_Select,
			1:											   //smallest gamma curve
				{0x9F , 0x0B},
				{0xA0 , 0x16},
				{0xA1 , 0x29},
				{0xA2 , 0x3C},
				{0xA3 , 0x4F},
				{0xA4 , 0x5F},
				{0xA5 , 0x6F},
				{0xA6 , 0x8A},
				{0xA7 , 0x9F},
				{0xA8 , 0xB4}, 
				{0xA9 , 0xC6},
				{0xAA , 0xD3},
				{0xAB , 0xDD},
				{0xAC , 0xE5},
				{0xAD , 0xF1},
				{0xAE , 0xFA},
				{0xAF , 0xFF},	
				
			2:			
				{0x9F , 0x0E},
				{0xA0 , 0x1C},
				{0xA1 , 0x34},
				{0xA2 , 0x48},
				{0xA3 , 0x5A},
				{0xA4 , 0x6B},
				{0xA5 , 0x7B},
				{0xA6 , 0x95},
				{0xA7 , 0xAB},
				{0xA8 , 0xBF},
				{0xA9 , 0xCE},
				{0xAA , 0xD9},
				{0xAB , 0xE4},
				{0xAC , 0xEC},
				{0xAD , 0xF7},
				{0xAE , 0xFD},
				{0xAF , 0xFF},
				
			3:
				{0x9F , 0x10},
				{0xA0 , 0x20},
				{0xA1 , 0x38},
				{0xA2 , 0x4E},
				{0xA3 , 0x63},
				{0xA4 , 0x76},
				{0xA5 , 0x87},
				{0xA6 , 0xA2},
				{0xA7 , 0xB8},
				{0xA8 , 0xCA},
				{0xA9 , 0xD8},
				{0xAA , 0xE3},
				{0xAB , 0xEB},
				{0xAC , 0xF0},
				{0xAD , 0xF8},
				{0xAE , 0xFD},
				{0xAF , 0xFF},
	
			4:
				{0x9F , 0x14},
				{0xA0 , 0x28},
				{0xA1 , 0x44},
				{0xA2 , 0x5D},
				{0xA3 , 0x72},
				{0xA4 , 0x86},
				{0xA5 , 0x95},
				{0xA6 , 0xB1},
				{0xA7 , 0xC6},
				{0xA8 , 0xD5},
				{0xA9 , 0xE1},
				{0xAA , 0xEA},
				{0xAB , 0xF1},
				{0xAC , 0xF5},
				{0xAD , 0xFB},
				{0xAE , 0xFE},
				{0xAF , 0xFF},
				
			5:								//largest gamma curve
				{0x9F , 0x15},
				{0xA0 , 0x2A},
				{0xA1 , 0x4A},
				{0xA2 , 0x67},
				{0xA3 , 0x79},
				{0xA4 , 0x8C},
				{0xA5 , 0x9A},
				{0xA6 , 0xB3},
				{0xA7 , 0xC5},
				{0xA8 , 0xD5},
				{0xA9 , 0xDF},
				{0xAA , 0xE8},
				{0xAB , 0xEE},
				{0xAC , 0xF3},
				{0xAD , 0xFA},
				{0xAE , 0xFD},
				{0xAF , 0xFF}, */
		//-----------GAMMA Select End--------------//
									
		{0xc0 , 0x00},									
		{0xc1 , 0x10},									
		{0xc2 , 0x1C},									
		{0xc3 , 0x30},									
		{0xc4 , 0x43},									
		{0xc5 , 0x54},									
		{0xc6 , 0x65},									
		{0xc7 , 0x75},									
		{0xc8 , 0x93},									
		{0xc9 , 0xB0},									
		{0xca , 0xCB},									
		{0xcb , 0xE6},									
		{0xcc , 0xFF},									
		{0xf0 , 0x02},									
		{0xf1 , 0x01},									
		{0xf2 , 0x01},									
		{0xf3 , 0x30},									
		{0xf9 , 0x9f},									
		{0xfa , 0x78},									
																	   
		//---------------------------------------------------------------
		{0xfe , 0x01},// set page1											  
																	   
		{0x00 , 0xf5},									
		{0x02 , 0x1a},									
		{0x0a , 0xa0},									
		{0x0b , 0x60},									
		{0x0c , 0x08},									
		{0x0e , 0x4c},									
		{0x0f , 0x39},									
		{0x11 , 0x3f},									
		{0x12 , 0x72},									
		{0x13 , 0x13},									
		{0x14 , 0x42},									
		{0x15 , 0x43},									
		{0x16 , 0xc2},									
		{0x17 , 0xa8},									
		{0x18 , 0x18},									
		{0x19 , 0x40},									
		{0x1a , 0xd0},									
		{0x1b , 0xf5},									
		{0x70 , 0x40},									
		{0x71 , 0x58},									
		{0x72 , 0x30},									
		{0x73 , 0x48},									
		{0x74 , 0x20},									
		{0x75 , 0x60},									
		{0x77 , 0x20},									
		{0x78 , 0x32},									
		{0x30 , 0x03},									
		{0x31 , 0x40},									
		{0x32 , 0xe0},									
		{0x33 , 0xe0},									
		{0x34 , 0xe0},									
		{0x35 , 0xb0},									
		{0x36 , 0xc0},									
		{0x37 , 0xc0},									
		{0x38 , 0x04},									
		{0x39 , 0x09},									
		{0x3a , 0x12},									
		{0x3b , 0x1C},									
		{0x3c , 0x28},									
		{0x3d , 0x31},									
		{0x3e , 0x44},									
		{0x3f , 0x57},									
		{0x40 , 0x6C},									
		{0x41 , 0x81},									
		{0x42 , 0x94},									
		{0x43 , 0xA7},									
		{0x44 , 0xB8},									
		{0x45 , 0xD6},									
		{0x46 , 0xEE},									
		{0x47 , 0x0d},									
		{0xfe , 0x00}, // set page0
		 
		//-----------Update the registers 2010/07/06-------------//
		//Registers of Page0
		{0xfe , 0x00}, // set page0
		{0x10 , 0x26},								   
		{0x11 , 0x0d},	// fd,modified by mormo 2010/07/06								 
		{0x1a , 0x2a},	// 1e,modified by mormo 2010/07/06									
	
		{0x1c , 0x49}, // c1,modified by mormo 2010/07/06								  
		{0x1d , 0x9a}, // 08,modified by mormo 2010/07/06								  
		{0x1e , 0x61}, // 60,modified by mormo 2010/07/06								  
	
		{0x3a , 0x20},
	
		{0x50 , 0x14},	// 10,modified by mormo 2010/07/06								 
		{0x53 , 0x80},									
		{0x56 , 0x80},
		
		{0x8b , 0x20}, //LSC								 
		{0x8c , 0x20},									
		{0x8d , 0x20},									
		{0x8e , 0x14},									
		{0x8f , 0x10},									
		{0x90 , 0x14},									
	
		{0x94 , 0x02},									
		{0x95 , 0x07},									
		{0x96 , 0xe0},									
	
		{0xb1 , 0x40}, // YCPT								   
		{0xb2 , 0x40},									
		{0xb3 , 0x40},
		{0xb6 , 0xe0},
	
		{0xd0 , 0xcb}, // AECT	c9,modifed by mormo 2010/07/06								  
		{0xd3 , 0x48}, // 80,modified by mormor 2010/07/06							 
	
		{0xf2 , 0x02},									
		{0xf7 , 0x12},
		{0xf8 , 0x0a},
	
		//Registers of Page1
		{0xfe , 0x01},// set page1	  
		{0x02 , 0x20},
		{0x04 , 0x10},
		{0x05 , 0x08},
		{0x06 , 0x20},
		{0x08 , 0x0a},
	
		{0x0e , 0x44},									
		{0x0f , 0x32},
		{0x10 , 0x41},									
		{0x11 , 0x37},									
		{0x12 , 0x22},									
		{0x13 , 0x19},									
		{0x14 , 0x44},									
		{0x15 , 0x44},	
		
		{0x19 , 0x50},									
		{0x1a , 0xd8}, 
		
		{0x32 , 0x10}, 
		
		{0x35 , 0x00},									
		{0x36 , 0x80},									
		{0x37 , 0x00}, 
		//-----------Update the registers end---------//
	
	
		{0xfe , 0x00}, // set page0
		{0xd2 , 0x90},	
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={
	SensorEnd

};

/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{

	SensorEnd
};
/* 1280x720 */
static struct rk_sensor_reg sensor_720p[]={
	SensorEnd
};

/* 1920x1080 */
static struct rk_sensor_reg sensor_1080p[]={
	SensorEnd
};


static struct rk_sensor_reg sensor_softreset_data[]={
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[]={
	SensorRegVal(0x00,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0x5a, 0x4c}, 
	{0x5b, 0x40},
	{0x5c, 0x4a},
	{0x22, 0x57},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0x22, 0x55},	// Disable AWB 
	{0x5a, 0x5a},
	{0x5b, 0x42},
	{0x5c, 0x40},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
	{0x22, 0x55},	// Disable AWB 
	{0x5a, 0x50},
	{0x5b, 0x45},
	{0x5c, 0x40},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
	{0x22, 0x55},	 // Disable AWB 
	{0x5a, 0x48},
	{0x5b, 0x40},
	{0x5c, 0x5c},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	{0x22, 0x55},	// Disable AWB 
	{0x5a, 0x40},
	{0x5b, 0x42}, 
	{0x5c, 0x50},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	{0xb5, 0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1
	{0xb5, 0xf0},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0
	{0xb5, 0x00},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1
	{0xb5, 0x20},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2
	{0xb5, 0x30},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3
	{0xb5, 0x40},

	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0x23,0x00}, 
	{0x2d,0x0a}, 
	{0x20,0x7f}, 
	{0xd2,0x90}, 
	{0x73,0x00}, 
	{0x77,0x38},
	{0xb3,0x40}, 
	{0xb4,0x80}, 
	{0xba,0x00}, 
	{0xbb,0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0x23,0x02}, 
	{0x2d,0x0a}, 
	{0x20,0x7f}, 
	{0xd2,0x90}, 
	{0x73,0x00}, 
	{0xb3,0x40},
	{0xb4,0x80}, 
	{0xba,0x00}, 
	{0xbb,0x00}, 
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0x23,0x02},
	{0x2d,0x0a},
	{0x20,0x7f},
	{0xd2,0x90},
	{0x73,0x00},
	{0xb3,0x40},
	{0xb4,0x80},
	{0xba,0xd2},
	{0xbb,0x28},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	{0x23,0x01},
	{0x2d,0x0a},
	{0x20,0x7f},
	{0xd2,0x90},
	{0x73,0x00},
	{0xb3,0x40},
	{0xb4,0x80},
	{0xba,0x00},
	{0xbb,0x00},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
	{0x23,0x02},
	{0x2d,0x0a},
	{0x20,0x7f},
	{0xd2,0x90},
	{0x73,0x00},
	{0xb3,0x40},
	{0xb4,0x80},
	{0xba,0x50},
	{0xbb,0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	{0x23,0x02},
	{0x2d,0x0a},
	{0x20,0x7f},
	{0xd2,0x90},
	{0x77,0x88},
	{0xb3,0x40},
	{0xb4,0x80},
	{0xba,0xc0},
	{0xbb,0xc0},
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{
	{0xd3, 0x30},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	{0xd3, 0x38},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	{0xd3, 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	{0xd3, 0x48},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	{0xd3, 0x50},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{
	{0xd3, 0x58},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
	{0xd3, 0x60},
	SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
	sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};

static	struct rk_sensor_reg sensor_Saturation0[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation1[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation2[]=
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

static	struct rk_sensor_reg sensor_Contrast0[]=
{
	{0xb3,0x34},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{
	{0xb3,0x38},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{
	{0xb3,0x3d},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{
	{0xb3,0x40},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{
	{0xb3,0x44},

	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{
	{0xb3,0x48},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{
	{0xb3,0x50},

	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
	sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};
static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	{0xec, 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	{0xec, 0x30},
	SensorEnd
};
static struct rk_sensor_reg *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

static struct rk_sensor_reg sensor_Zoom0[] =
{
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom1[] =
{
	SensorEnd
};

static struct rk_sensor_reg sensor_Zoom2[] =
{
	SensorEnd
};


static struct rk_sensor_reg sensor_Zoom3[] =
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL,};

/*
* User could be add v4l2_querymenu in sensor_controls by new_usr_v4l2menu
*/
static struct v4l2_querymenu sensor_menus[] =
{
};
/*
* User could be add v4l2_queryctrl in sensor_controls by new_user_v4l2ctrl
*/
static struct sensor_v4l2ctrl_usr_s sensor_controls[] =
{
};

//MUST define the current used format as the first item   
static struct rk_sensor_datafmt sensor_colour_fmts[] = {
	{MEDIA_BUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG}
};
/*static struct soc_camera_ops sensor_ops;*/


/*
**********************************************************
* Following is local code:
* 
* Please codeing your program here 
**********************************************************
*/
/*
**********************************************************
* Following is callback
* If necessary, you could coding these callback
**********************************************************
*/
/*
* the function is called in open sensor  
*/
static int sensor_activate_cb(struct i2c_client *client)
{
	
	return 0;
}
/*
* the function is called in close sensor
*/
static int sensor_deactivate_cb(struct i2c_client *client)
{
	
	return 0;
}
/*
* the function is called before sensor register setting in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{

	return 0;
}
/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_bh (struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
	return 0;
}
static int sensor_try_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf)
{
	return 0;
}

static int sensor_softrest_usr_cb(struct i2c_client *client,struct rk_sensor_reg *series)
{
	
	return 0;
}
static int sensor_check_id_usr_cb(struct i2c_client *client,struct rk_sensor_reg *series)
{
	return 0;
}
static int sensor_suspend(struct soc_camera_device *icd, pm_message_t pm_msg)
{
	//struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));
		
	if (pm_msg.event == PM_EVENT_SUSPEND) {
		SENSOR_DG("Suspend");
		
	} else {
		SENSOR_TR("pm_msg.event(0x%x) != PM_EVENT_SUSPEND\n",pm_msg.event);
		return -EINVAL;
	}
	return 0;
}

static int sensor_resume(struct soc_camera_device *icd)
{

	SENSOR_DG("Resume");

	return 0;

}
static int sensor_mirror_cb (struct i2c_client *client, int mirror)
{
	char val;
	int err = 0;
	
	SENSOR_DG("mirror: %d",mirror);
	if (mirror) {
		
		//{0xfe , 0x00}, // set page0
		
		//{0x14 , 0x13},	//0x10
		//-------------H_V_Switch(4)---------------//
		/*	1:	// normal
					{0x14 , 0x10},			
			2:	// IMAGE_H_MIRROR
					{0x14 , 0x11},
					
			3:	// IMAGE_V_MIRROR
					{0x14 , 0x12},
					
			4:	// IMAGE_HV_MIRROR
					{0x14 , 0x13},*/										
		sensor_write(client, 0xfe, 0);
		err = sensor_read(client, 0x14, &val);
		if (err == 0) {
			if((val & 0x1) == 0)
				err = sensor_write(client, 0x14, (val |0x1));
			else 
				err = sensor_write(client, 0x14, (val & 0xfe));
		}
	} else {
		//do nothing
	}

	return err;    
}
/*
* the function is v4l2 control V4L2_CID_HFLIP callback	
*/
static int sensor_v4l2ctrl_mirror_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
													 struct v4l2_ext_control *ext_ctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

	if (sensor_mirror_cb(client,ext_ctrl->value) != 0)
		SENSOR_TR("sensor_mirror failed, value:0x%x",ext_ctrl->value);
	
	SENSOR_DG("sensor_mirror success, value:0x%x",ext_ctrl->value);
	return 0;
}

static int sensor_flip_cb(struct i2c_client *client, int flip)
{
	char val;
	int err = 0;	

	SENSOR_DG("flip: %d",flip);
	if (flip) {
		
		//{0xfe , 0x00}, // set page0
		
		//{0x14 , 0x13},	//0x10
		//-------------H_V_Switch(4)---------------//
		/*	1:	// normal
					{0x14 , 0x10},			
			2:	// IMAGE_H_MIRROR
					{0x14 , 0x11},
					
			3:	// IMAGE_V_MIRROR
					{0x14 , 0x12},
					
			4:	// IMAGE_HV_MIRROR
					{0x14 , 0x13},*/	
		sensor_write(client, 0xfe, 0);
		err = sensor_read(client, 0x14, &val);
		if (err == 0) {
			if((val & 0x2) == 0)
				err = sensor_write(client, 0x14, (val |0x2));
			else 
				err = sensor_write(client, 0x14, (val & 0xfc));
		}
	} else {
		//do nothing
	}

	return err;    
}
/*
* the function is v4l2 control V4L2_CID_VFLIP callback	
*/
static int sensor_v4l2ctrl_flip_cb(struct soc_camera_device *icd, struct sensor_v4l2ctrl_info_s *ctrl_info, 
													 struct v4l2_ext_control *ext_ctrl)
{
	struct i2c_client *client = to_i2c_client(to_soc_camera_control(icd));

	if (sensor_flip_cb(client,ext_ctrl->value) != 0)
		SENSOR_TR("sensor_flip failed, value:0x%x",ext_ctrl->value);
	
	SENSOR_DG("sensor_flip success, value:0x%x",ext_ctrl->value);
	return 0;
}
/*
* the functions are focus callbacks
*/
static int sensor_focus_init_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_single_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_near_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_far_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_specialpos_usr_cb(struct i2c_client *client,int pos){
	return 0;
}

static int sensor_focus_af_const_usr_cb(struct i2c_client *client){
	return 0;
}
static int sensor_focus_af_const_pause_usr_cb(struct i2c_client *client)
{
    return 0;
}
static int sensor_focus_af_close_usr_cb(struct i2c_client *client){
	return 0;
}

static int sensor_focus_af_zoneupdate_usr_cb(struct i2c_client *client, int *zone_tm_pos)
{
    return 0;
}

/*
face defect call back
*/
static int	sensor_face_detect_usr_cb(struct i2c_client *client,int on){
	return 0;
}

/*
*	The function can been run in sensor_init_parametres which run in sensor_probe, so user can do some
* initialization in the function. 
*/
static void sensor_init_parameters_user(struct specific_sensor* spsensor,struct soc_camera_device *icd)
{
	return;
}

/*
* :::::WARNING:::::
* It is not allowed to modify the following code
*/

sensor_init_parameters_default_code();

sensor_v4l2_struct_initialization();

sensor_probe_default_code();

sensor_remove_default_code();

sensor_driver_default_module_code();



