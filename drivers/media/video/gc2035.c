/*
 * drivers/media/video/gc2035.c
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
#define SENSOR_NAME RK29_CAM_SENSOR_GC2035
#define SENSOR_V4L2_IDENT V4L2_IDENT_GC2035
#define SENSOR_ID 0x2035
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
														 V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_HIGH|\
														 V4L2_MBUS_DATA_ACTIVE_HIGH  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W					 800
#define SENSOR_PREVIEW_H					 600
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
	u16 shutter;

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
static struct rk_sensor_reg sensor_init_data[] =
{
	{0xfe , 0x80},
	{0xfe , 0x80},
	{0xfe , 0x80},  
	{0xfc , 0x06},
	{0xf2 , 0x00},
	{0xf3 , 0x00},
	{0xf4 , 0x00},
	{0xf5 , 0x00},
	{0xf9 , 0xfe}, //[0] pll enable
	{0xfa , 0x00},
	{0xf6 , 0x00},
	{0xf7 , 0x15}, //pll enable

	{0xf8 , 0x85},
	{0xfe , 0x00},
	{0x82 , 0x00},
	{0xb3 , 0x60},
	{0xb4 , 0x40},
	{0xb5 , 0x60},

	{0x03 , 0x02},
	{0x04 , 0x80},

	//////////measure window  ///////////
	{0xfe , 0x00},
	{0xec , 0x06},//04 
	{0xed , 0x06},//04 
	{0xee , 0x62},//60 
	{0xef , 0x92},//90 

	///////////analog/////////////
	{0x0a , 0x00}, //row start
	{0x0c , 0x00}, //col start
	{0x0d , 0x04},
	{0x0e , 0xc0},
	{0x0f , 0x06}, //Window setting
	{0x10 , 0x58}, 
	{0x17 , 0x14}, //[0]mirror [1]flip


	{0x18 , 0x0e}, //sdark 4 row in even frame??
	{0x19 , 0x0c}, //AD pipe number

	/*
	///  Ã«´Ì ÏÖÏó
	{0x18 , 0x0a}, //sdark 4 row in even frame??
	{0x19 , 0x0a}, //AD pipe number
	*/
	
	{0x1a , 0x01}, //CISCTL mode4
	{0x1b , 0x8b},
	{0x1e , 0x88}, //analog mode1 [7] tx-high en [5:3]COL_bias
	{0x1f , 0x08}, //[3] tx-low en//
	{0x20 , 0x05}, //[0]adclk mode , 0x[1]rowclk_MODE [2]rsthigh_en
	{0x21 , 0x0f}, //[6:4]rsg
	{0x22 , 0xf0}, //[3:0]vref
	{0x23 , 0xc3}, //f3//ADC_r
	{0x24 , 0x17}, //pad drive  16

	//AEC
	{0xfe , 0x01},
	{0x11 , 0x20},//AEC_out_slope , 0x
	{0x1f , 0xc0},//max_post_gain
	{0x20 , 0x60},//max_pre_gain
	{0x47 , 0x30},//AEC_outdoor_th
	{0x0b , 0x10},//
	{0x13 , 0x75},//y_target
	{0xfe , 0x00},


	{0x05 , 0x01},//hb
	{0x06 , 0x11},
	{0x07 , 0x00},//vb
	{0x08 , 0x50},
	{0xfe , 0x01},
	{0x27 , 0x00},//step
	{0x28 , 0xa0},
	{0x29 , 0x05},//level1
	{0x2a , 0x00},
	{0x2b , 0x05},//level2
	{0x2c , 0x00},
	{0x2d , 0x06},//6e8//level3
	{0x2e , 0xe0},
	{0x2f , 0x0a},//level4
	{0x30 , 0x00},
	{0x3e , 0x40},
	{0xfe , 0x00},
	{0xfe , 0x00},  //0x , 0x , 0x , 0x , 0x 
	{0xb6 , 0x03}, //AEC enable
	{0xfe , 0x00},

	/////////BLK//////
	{0x3f , 0x00}, //prc close
	{0x40 , 0x77},//
	{0x42 , 0x7f},
	{0x43 , 0x30},
	{0x5c , 0x08},
	{0x5e , 0x20},
	{0x5f , 0x20},
	{0x60 , 0x20},
	{0x61 , 0x20},
	{0x62 , 0x20},
	{0x63 , 0x20},
	{0x64 , 0x20},
	{0x65 , 0x20},

	///block////////////
	{0x80 , 0xff},
	{0x81 , 0x26},//38 , 0x//skin_Y 8c_debug
	{0x87 , 0x90}, //[7]middle gamma 
	{0x84 , 0x00}, //output put foramat
	{0x86 , 0x07}, //02 //sync plority 
	{0x8b , 0xbc},
	{0xb0 , 0x80}, //globle gain
	{0xc0 , 0x40},//Yuv bypass

	//////lsc/////////////
	{0xfe , 0x01},
	{0xc2 , 0x38},
	{0xc3 , 0x25},
	{0xc4 , 0x21},
	{0xc8 , 0x19},
	{0xc9 , 0x12},
	{0xca , 0x0e},
	{0xbc , 0x43},
	{0xbd , 0x18},
	{0xbe , 0x1b},
	{0xb6 , 0x40},
	{0xb7 , 0x2e},
	{0xb8 , 0x26},
	{0xc5 , 0x05},
	{0xc6 , 0x03},
	{0xc7 , 0x04},
	{0xcb , 0x00},
	{0xcc , 0x00},
	{0xcd , 0x00},
	{0xbf , 0x14},
	{0xc0 , 0x22},
	{0xc1 , 0x1b},
	{0xb9 , 0x00},
	{0xba , 0x05},
	{0xbb , 0x05},
	{0xaa , 0x35},
	{0xab , 0x33},
	{0xac , 0x33},
	{0xad , 0x25},
	{0xae , 0x22},
	{0xaf , 0x27},
	{0xb0 , 0x1d},
	{0xb1 , 0x20},
	{0xb2 , 0x22},
	{0xb3 , 0x14},
	{0xb4 , 0x15},
	{0xb5 , 0x16},
	{0xd0 , 0x00},
	{0xd2 , 0x07},
	{0xd3 , 0x08},
	{0xd8 , 0x00},
	{0xda , 0x13},
	{0xdb , 0x17},
	{0xdc , 0x00},
	{0xde , 0x0a},
	{0xdf , 0x08},
	{0xd4 , 0x00},
	{0xd6 , 0x00},
	{0xd7 , 0x0c},
	{0xa4 , 0x00},
	{0xa5 , 0x00},
	{0xa6 , 0x00},
	{0xa7 , 0x00},
	{0xa8 , 0x00},
	{0xa9 , 0x00},
	{0xa1 , 0x80},
	{0xa2 , 0x80},

	//////////cc//////////////
	{0xfe , 0x02},
	{0xc0 , 0x01},
	{0xc1 , 0x40}, //Green_cc for d
	{0xc2 , 0xfc},
	{0xc3 , 0x05},
	{0xc4 , 0xec},
	{0xc5 , 0x42},
	{0xc6 , 0xf8},
	{0xc7 , 0x40},//for cwf 
	{0xc8 , 0xf8},
	{0xc9 , 0x06},
	{0xca , 0xfd},
	{0xcb , 0x3e},
	{0xcc , 0xf3},
	{0xcd , 0x36},//for A
	{0xce , 0xf6},
	{0xcf , 0x04},
	{0xe3 , 0x0c},
	{0xe4 , 0x44},
	{0xe5 , 0xe5},
	{0xfe , 0x00},

	///////awb start ////////////////
	//AWB clear
	{0xfe , 0x01},
	{0x4f , 0x00},
	{0x4d , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x10}, // 10
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x20}, // 20
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x30},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00}, // 30
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x40}, // 40
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x50}, // 50
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x60}, // 60
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x70}, // 70
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x80}, // 80
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0x90}, // 90
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0xa0}, // a0
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0xb0}, // b0
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0xc0}, // c0
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4d , 0xd0}, // d0
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4e , 0x00},
	{0x4f , 0x01},
	/////// awb value////////
	{0xfe , 0x01},
	{0x4f , 0x00},
	{0x4d , 0x30},
	{0x4e , 0x00},
	{0x4e , 0x80},
	{0x4e , 0x80},
	{0x4e , 0x02},
	{0x4e , 0x02},
	{0x4d , 0x40},
	{0x4e , 0x00},
	{0x4e , 0x80},
	{0x4e , 0x80},
	{0x4e , 0x02},
	{0x4e , 0x02},
	{0x4e , 0x02},
	{0x4d , 0x53},
	{0x4e , 0x08},
	{0x4e , 0x04},
	{0x4d , 0x62},
	{0x4e , 0x10},
	{0x4d , 0x72},
	{0x4e , 0x20},
	{0x4f , 0x01},

	/////awb////
	{0xfe , 0x01},
	{0x50 , 0x88},//c0//[6]green mode
	{0x52 , 0x40},
	{0x54 , 0x60},
	{0x56 , 0x06},
	{0x57 , 0x20}, //pre adjust
	{0x58 , 0x01}, 
	{0x5b , 0x02}, //AWB_gain_delta
	{0x61 , 0xaa},//R/G stand
	{0x62 , 0xaa},//R/G stand
	{0x71 , 0x00},
	{0x74 , 0x10},  //0x//AWB_C_max
	{0x77 , 0x08}, // 0x//AWB_p2_x
	{0x78 , 0xfd}, //AWB_p2_y
	{0x86 , 0x30},
	{0x87 , 0x00},
	{0x88 , 0x04},//06 , 0x//[1]dark mode
	{0x8a , 0xc0},//awb move mode
	{0x89 , 0x75},
	{0x84 , 0x08},  //0x//auto_window
	{0x8b , 0x00}, // 0x//awb compare luma
	{0x8d , 0x70}, //awb gain limit R 
	{0x8e , 0x70},//G
	{0x8f , 0xf4},//B
	{0xfe , 0x00},
	{0x82 , 0x02},//awb_en
       /////////awb end /////////////
       
	///==========asde
	{0xfe , 0x01},
	{0x21 , 0xbf},
	{0xfe , 0x02},
	{0xa4 , 0x00},//
	{0xa5 , 0x40}, //lsc_th
	{0xa2 , 0xa0}, //lsc_dec_slope
	{0xa6 , 0x80}, //dd_th
	{0xa7 , 0x80}, //ot_th
	{0xab , 0x31}, //
	{0xa9 , 0x6f}, //
	{0xb0 , 0x99}, //0x//edge effect slope low
	{0xb1 , 0x34},//edge effect slope low
	{0xb3 , 0x80}, //saturation dec slope
	{0xde , 0xb6},  //
	{0x38 , 0x0f}, // 
	{0x39 , 0x60}, //
	{0xfe , 0x00},
	{0x81 , 0x26},
	{0xfe , 0x02},
	{0x83 , 0x00},//
	{0x84 , 0x45},//
	////////////YCP//////////
	{0xd1 , 0x38},//saturation_cb
	{0xd2 , 0x38},//saturation_Cr
	{0xd3 , 0x40},//contrast ?
	{0xd4 , 0x80},//contrast center 
	{0xd5 , 0x00},//luma_offset 
	{0xdc , 0x30},
	{0xdd , 0xb8},//edge_sa_g,b
	{0xfe , 0x00},
	///////dndd///////////
	{0xfe , 0x02},
	{0x88 , 0x15},//dn_b_base
	{0x8c , 0xf6}, //[2]b_in_dark_inc
	{0x89 , 0x03}, //dn_c_weight
	////////EE ///////////
	{0xfe , 0x02},
	{0x90 , 0x6c},// EEINTP mode1
	{0x97 , 0x45},// edge effect
	////==============RGB Gamma 
	{0xfe , 0x02},
	{0x15 , 0x0a},
	{0x16 , 0x12},
	{0x17 , 0x19},
	{0x18 , 0x1f},
	{0x19 , 0x2c},
	{0x1a , 0x38},
	{0x1b , 0x42},
	{0x1c , 0x4e},
	{0x1d , 0x63},
	{0x1e , 0x76},
	{0x1f , 0x87},
	{0x20 , 0x96},
	{0x21 , 0xa2},
	{0x22 , 0xb8},
	{0x23 , 0xca},
	{0x24 , 0xd8},
	{0x25 , 0xe3},
	{0x26 , 0xf0},
	{0x27 , 0xf8},
	{0x28 , 0xfd},
	{0x29 , 0xff},

	///=================y gamma
	{0xfe , 0x02},
	{0x2b , 0x00},
	{0x2c , 0x04},
	{0x2d , 0x09},
	{0x2e , 0x18},
	{0x2f , 0x27},
	{0x30 , 0x37},
	{0x31 , 0x49},
	{0x32 , 0x5c},
	{0x33 , 0x7e},
	{0x34 , 0xa0},
	{0x35 , 0xc0},
	{0x36 , 0xe0},
	{0x37 , 0xff},
	/////1600x1200size// 
	{0xfe , 0x00},//
	{0x90 , 0x01}, //0x//crop enable
	{0x95 , 0x04},  //0x//1600x1200
	{0x96 , 0xb0},
	{0x97 , 0x06},
	{0x98 , 0x40},

	{0xfe , 0x03},
	{0x42 , 0x40}, 
	{0x43 , 0x06}, //output buf width
	{0x41 , 0x02}, // Pclk_polarity
	{0x40 , 0x40},  //00  
	{0x17 , 0x00}, //widv 
	{0xfe , 0x00},
	////output DVP/////
	{0xfe , 0x00},
	{0xb6 , 0x03},
	{0xf7 , 0x15},

	{0xc8 , 0x00},//close scaler
	{0x99 , 0x22},// 1/2 subsample
	{0x9a , 0x06},
	{0x9b , 0x00},
	{0x9c , 0x00},
	{0x9d , 0x00},
	{0x9e , 0x00},
	{0x9f , 0x00},
	{0xa0 , 0x00},  
	{0xa1 , 0x00},
	{0xa2  ,0x00},
	
	{0x90 , 0x01},  //crop enable
	{0x94 , 0x02},
	{0x95 , 0x02},
	{0x96 , 0x58},
	{0x97 , 0x03},
	{0x98 , 0x20},
	{0xfe , 0x00},
	{0x82 , 0xfe},  // fe
	{0xf2 , 0x70}, 
	{0xf3 , 0xff},
	{0xf4 , 0x00},
	{0xf5 , 0x30},
	
	  #if 0   
        /////////  re zao///
	{0xfe,0x00},
	{0x22,0xd0},
	{0xfe,0x01},
	{0x21,0xff},
	{0xfe,0x02},  
	{0x8a,0x33},
	{0x8c,0x76},
	{0x8d,0x85},
	{0xa6,0xf0},	
	{0xae,0x9f},
	{0xa2,0x90},
	{0xa5,0x40},  
	{0xa7,0x30},
	{0xb0,0x88},
	{0x38,0x0b},
	{0x39,0x30},
	{0xfe,0x00},  
	{0x87,0xb0},

       //// small  RGB gamma////
	{0xfe , 0x02},
	{0x15 , 0x0b},
	{0x16 , 0x0e},
	{0x17 , 0x10},
	{0x18 , 0x12},
	{0x19 , 0x19},
	{0x1a , 0x21},
	{0x1b , 0x29},
	{0x1c , 0x31},
	{0x1d , 0x41},
	{0x1e , 0x50},
	{0x1f , 0x5f},
	{0x20 , 0x6d},
	{0x21 , 0x79},
	{0x22 , 0x91},
	{0x23 , 0xa5},
	{0x24 , 0xb9},
	{0x25 , 0xc9},
	{0x26 , 0xe1},
	{0x27 , 0xee},
	{0x28 , 0xf7},
	{0x29 , 0xff},
	
 	////dark sun/////
	{0xfe , 0x02},
	{0x40 , 0x06},
	{0x41 , 0x23},
	{0x42 , 0x3f},
	{0x43 , 0x06},
	{0x44 , 0x00},
	{0x45 , 0x00},
	{0x46 , 0x14},
	{0x47 , 0x09},
 
  #endif
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={
 
 
	{0xfe , 0x00},
	{0xc8 , 0x00},
	{0xf7 , 0x17},

	{0x99 , 0x11}, // disable sambsample
	{0x9a , 0x06},
	{0x9b , 0x00},
	{0x9c , 0x00},
	{0x9d , 0x00},
	{0x9e , 0x00},
	{0x9f , 0x00},
	{0xa0 , 0x00},  
	{0xa1 , 0x00},
	{0xa2 , 0x00},
	        
	{0x90 , 0x01},
	{0x95 , 0x04},
	{0x96 , 0xb0},  
	{0x97 , 0x06},
	{0x98 , 0x40},
                
	SensorEnd 


};

/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
	{0xfe , 0x00},
	{0xb6 , 0x03},
	{0xf7 , 0x15},

	{0xc8 , 0x00},//close scaler
	{0x99 , 0x22},// 1/2 subsample
	{0x9a , 0x06},
	{0x9b , 0x00},
	{0x9c , 0x00},
	{0x9d , 0x00},
	{0x9e , 0x00},
	{0x9f , 0x00},
	{0xa0 , 0x00},  
	{0xa1 , 0x00},
	{0xa2 , 0x00},
	
	{0x90 , 0x01},  //crop enable
	{0x94 , 0x02},
	{0x95 , 0x02},
	{0x96 , 0x58},
	{0x97 , 0x03},
	{0x98 , 0x20},
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
    SensorRegVal(0xfe,80),
    SensorWaitMs(5),
	SensorEnd
};

static struct rk_sensor_reg sensor_check_id_data[]={
	SensorRegVal(0xf0,0),
	SensorRegVal(0xf1,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0xb3, 0x61},
	{0xb4, 0x40},
	{0xb5, 0x61},
	{0x82, 0xfe},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0x82, 0xfc},
	{0xb3, 0x58},
	{0xb4, 0x40},
	{0xb5, 0x50},	
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
			{0x82, 0xfc},
			{0xb3, 0x58},
			{0xb4, 0x40},
			{0xb5, 0x50},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
			{0x82, 0xfc},
			{0xb3, 0x50},
			{0xb4, 0x40},
			{0xb5, 0xa8},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
			{0x82, 0xfc},
			{0xb3, 0xa0},
			{0xb4, 0x45},
			{0xb5, 0x40},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	
	{0xfe, 0x01},
	{0x13, 0x40},
	{0xfe, 0x02},
	{0xd5, 0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1
	
	{0xfe, 0x01},
	{0x13, 0x48},
	{0xfe, 0x02},
	{0xd5, 0xf0},
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0
	
	{0xfe, 0x01},
	{0x13, 0x58},
	{0xfe, 0x02},
	{0xd5, 0x00},
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1
	{0xfe, 0x01},
	{0x13, 0x60},
	{0xfe, 0x02},
	{0xd5, 0x10},
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2
	{0xfe, 0x01},
	{0x13, 0x68},
	{0xfe, 0x02},
	{0xd5, 0x20},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3
	{0xfe, 0x01},
	{0x13, 0x78},
	{0xfe, 0x02},
	{0xd5, 0x30},

	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0xfe, 0x00},
	{0x83, 0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0xfe, 0x00},
	{0x83, 0x12},			
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0xfe, 0x00},
	{0x83, 0x82},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	{0xfe, 0x00},
	{0x83, 0x01},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
	{0xfe, 0x00},
	{0x83, 0x62},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	{0xfe, 0x00},
	{0x83, 0x52},
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
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
	//Contrast -3

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{
	//Contrast -2

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{
	// Contrast -1

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{
	//Contrast 0

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{
	//Contrast +1

	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{
	//Contrast +2

	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{
	//Contrast +3

	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
	sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};
static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	{0xfe,0x01},
	
	{0x3e,0x40},	
	{0xfe,0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	{0xfe,0x01},
	
	{0x3e,0x60},	
	{0xfe,0x00},
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
	{MEDIA_BUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG}
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
	char value;
	unsigned   int pid=0,shutter,temp_reg;
	if(mf->width == 1600 && mf->height == 1200){
		 sensor_write(client, 0xfe, 0x00);
		 sensor_write(client, 0xb6, 0x02);
		
		
		 sensor_read(client, 0x03, &value);
		 pid |= (value << 8);
		sensor_read(client, 0x04, &value);
		 pid |= (value & 0xff);
		 shutter=pid;
		
		
		 temp_reg= shutter /2;	 // 2
		
		 if(temp_reg < 1) temp_reg = 1;
		
		
		 sensor_write(client, 0x03, ((temp_reg>>8)&0xff));
		 sensor_write(client, 0x04, (temp_reg&0xff));
	}

	return 0;
}
/*
* the function is called after sensor register setting finished in VIDIOC_S_FMT  
*/
static int sensor_s_fmt_cb_bh (struct i2c_client *client,struct v4l2_mbus_framefmt *mf, bool capture)
{
	/* add delay for rk312x*/
	msleep(300); 
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
static int sensor_try_fmt_cb_th(struct i2c_client *client,struct v4l2_mbus_framefmt *mf)
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
		sensor_write(client, 0xfe, 0);
		err = sensor_read(client, 0x17, &val);
		val-=4;
		if (err == 0) {
			if((val & 0x1) == 0){
				err = sensor_write(client, 0x17, ((val |0x1)+4));
				}
			else 
				err = sensor_write(client, 0x17, ((val & 0xfe)+4));
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
		
		sensor_write(client, 0xfe, 0);
		err = sensor_read(client, 0x17, &val);
		val-=4;
		if (err == 0) {
			if((val & 0x2) == 0){
				err = sensor_write(client, 0x17, ((val |0x2)+4));
				}
			else {
				err = sensor_write(client, 0x17, ((val & 0xfc)+4));
				}
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





