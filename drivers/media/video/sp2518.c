/*
 * drivers/media/video/sp2518.c
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
*v0.1.1:
*        add sensor_focus_af_const_pause_usr_cb;
*/
static int version = KERNEL_VERSION(0,1,1);
module_param(version, int, S_IRUGO);



static int debug;
module_param(debug, int, S_IRUGO|S_IWUSR);

#define dprintk(level, fmt, arg...) do {			\
	if (debug > level) 					\
	printk(KERN_WARNING fmt , ## arg); } while (0)

/* Sensor Driver Configuration Begin */
#define SENSOR_NAME RK29_CAM_SENSOR_SP2518
#define SENSOR_V4L2_IDENT V4L2_IDENT_SP2518
#define SENSOR_ID 0x53
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
															 V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_HIGH|\
															 V4L2_MBUS_DATA_ACTIVE_HIGH  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W                     800
#define SENSOR_PREVIEW_H                     600
#define SENSOR_PREVIEW_FPS                   15000     // 15fps 
#define SENSOR_FULLRES_L_FPS                 7500      // 7.5fps
#define SENSOR_FULLRES_H_FPS                 7500      // 7.5fps
#define SENSOR_720P_FPS                      0
#define SENSOR_1080P_FPS                     0

#define SENSOR_REGISTER_LEN                  1         // sensor register address bytes
#define SENSOR_VALUE_LEN                     1         // sensor register value bytes

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
* Local define
*/
//AE
#define  SP2518_P0_0xf7  0x80//78
#define  SP2518_P0_0xf8  0x74//6e
#define  SP2518_P0_0xf9  0x80//74
#define  SP2518_P0_0xfa  0x74//6a
//HEQ
#define  SP2518_P0_0xdd  0x80
#define  SP2518_P0_0xde  0x95
//auto lum
#define SP2518_NORMAL_Y0ffset  	  0x10	//0x0f	 modify by sp_yjp,20120813
#define SP2518_LOWLIGHT_Y0ffset  0x20

/*
*  The follow setting need been filled.
*  
*  Must Filled:
*  sensor_init_data :               Sensor initial setting;
*  sensor_fullres_lowfps_data :     Sensor full resolution setting with best auality, recommand for video;
*  sensor_preview_data :            Sensor preview resolution setting, recommand it is vga or svga;
*  sensor_softreset_data :          Sensor software reset register;
*  sensor_check_id_data :           Sensir chip id register;
*
*  Optional filled:
*  sensor_fullres_highfps_data:     Sensor full resolution setting with high framerate, recommand for video;
*  sensor_720p:                     Sensor 720p setting, it is for video;
*  sensor_1080p:                    Sensor 1080p setting, it is for video;
*
*  :::::WARNING:::::
*  The SensorEnd which is the setting end flag must be filled int the last of each setting;
*/

/* Sensor initial setting */
static struct rk_sensor_reg sensor_init_data[] = {
	{0xfd,0x00},
	{0x1b,0x1a},//maximum drv ability //0x02 modify by sp_yjp,20120809
	{0x0e,0x01},

	{0x0f,0x2f},
	{0x10,0x2e},
	{0x11,0x00},
	{0x12,0x4f},
	{0x14,0x40},// 0x20 zch 20120920
	{0x16,0x02},
	{0x17,0x10},
	{0x1a,0x1f},
	{0x1e,0x81},
	{0x21,0x00},
	{0x22,0x1b},
	{0x25,0x10},
	{0x26,0x25},
	{0x27,0x6d},
	{0x2c,0x23},//31 Ronlus remove balck dot0x45},
	{0x2d,0x75},
	{0x2e,0x38},//sxga 0x18

	{0x31,0x10},//mirror upside down
	{0x44,0x03},
	{0x6f,0x00},
	{0xa0,0x04},
	{0x5f,0x01},
	{0x32,0x00},
	{0xfd,0x01},
	{0x2c,0x00},
	{0x2d,0x00},
	{0xfd,0x00},
	{0xfb,0x83},
	{0xf4,0x09},
	//Pregain
	{0xfd,0x01},
	{0xc6,0x90},
	{0xc7,0x90},
	{0xc8,0x90},
	{0xc9,0x90},
	//blacklevel
	{0xfd,0x00},
	{0x65,0x08},
	{0x66,0x08},
	{0x67,0x08},
	{0x68,0x08},

	//bpc
	{0x46,0xff},
	//rpc
	{0xfd,0x00},
	{0xe0,0x6c},
	{0xe1,0x54},
	{0xe2,0x48},
	{0xe3,0x40},
	{0xe4,0x40},
	{0xe5,0x3e},
	{0xe6,0x3e},
	{0xe8,0x3a},
	{0xe9,0x3a},
	{0xea,0x3a},
	{0xeb,0x38},
	{0xf5,0x38},
	{0xf6,0x38},
	{0xfd,0x01},
	{0x94,0xc0},//f8
	{0x95,0x38},
	{0x9c,0x6c},
	{0x9d,0x38},	
	#if 0
	///SP2518 UXGA 24MEclk 3倍频 1分频 50Hz fix 10fps
	{0xfd , 0x00},
	{0x03 , 0x03},
	{0x04 , 0x66},
	{0x05 , 0x00},
	{0x06 , 0x8b},
	{0x07 , 0x00},
	{0x08 , 0x8b},
	{0x09 , 0x01},
	{0x0a , 0x3b},
	{0x2f , 0x00},
	{0x30 , 0x08},
	{0xf0 , 0x91},
	{0xf1 , 0x00},
	{0xfd , 0x01},
	{0x90 , 0x0a},
	{0x92 , 0x01},
	{0x98 , 0x91},
	{0x99 , 0x00},
	{0x9a , 0x01},
	{0x9b , 0x00},
	///Status 
	{0xfd , 0x01},
	{0xce , 0xaa},
	{0xcf , 0x05},
	{0xd0 , 0xaa},
	{0xd1 , 0x05},
	{0xd7 , 0x93},//8d
	{0xd8 , 0x00},
	{0xd9 , 0x97},//91
	{0xda , 0x00},
	{0xfd , 0x00},
	#endif
	
	#if 1
	/*24*3pll 13-13fps 50hz*/
		
	{0xfd,0x00},
	{0x03,0x03},
	{0x04,0xf6},
	{0x05,0x00},
	{0x06,0x00},
	{0x07,0x00},
	{0x08,0x00},
	{0x09,0x00},
	{0x0a,0x8b},
	{0x2f,0x00},
	{0x30,0x08},
	{0xf0,0xa9},
	{0xf1,0x00},
	{0xfd,0x01},
	{0x90,0x07},
	{0x92,0x01},
	{0x98,0xa9},
	{0x99,0x00},
	{0x9a,0x01},
	{0x9b,0x00},
	{0xfd,0x01},
	{0xce,0x9f},
	{0xcf,0x04},
	{0xd0,0x9f},
	{0xd1,0x04},
	{0xd7,0xa5},
	{0xd8,0x00},
	{0xd9,0xa9},
	{0xda,0x00},
	{0xfd,0x00},

	#endif

	{0xfd,0x01},
	{0xca,0x30},//mean dummy2low
	{0xcb,0x50},//mean low2dummy
	{0xcc,0xc0},//f8;rpc low
	{0xcd,0xc0},//rpc dummy
	{0xd5,0x80},//mean normal2dummy
	{0xd6,0x90},//mean dummy2normal
	{0xfd,0x00},  
	
	//lens shading 
	{0xfd,0x00},
	{0xa1,0x20},
	{0xa2,0x20},
	{0xa3,0x20},
	{0xa4,0xff},
	{0xa5,0x80},
	{0xa6,0x80},
	{0xfd,0x01},
	{0x64,0x1e},//28
	{0x65,0x1c},//25
	{0x66,0x1c},//2a
	{0x67,0x16},//25
	{0x68,0x1c},//25
	{0x69,0x1c},//29
	{0x6a,0x1a},//28
	{0x6b,0x16},//20
	{0x6c,0x1a},//22
	{0x6d,0x1a},//22
	{0x6e,0x1a},//22
	{0x6f,0x16},//1c
	{0xb8,0x04},//0a
	{0xb9,0x13},//0a
	{0xba,0x00},//23
	{0xbb,0x03},//14
	{0xbc,0x03},//08
	{0xbd,0x11},//08
	{0xbe,0x00},//12
	{0xbf,0x02},//00
	{0xc0,0x04},//05
	{0xc1,0x0e},//05
	{0xc2,0x00},//18
	{0xc3,0x05},//08   
	//raw filter
	{0xfd,0x01},
	{0xde,0x0f},
	{0xfd,0x00},
	{0x57,0x08},//raw_dif_thr
	{0x58,0x08},//a
	{0x56,0x08},//a
	{0x59,0x10},

	{0x5a,0xa0},//raw_rb_fac_outdoor
	{0xc4,0xa0},//60raw_rb_fac_indoor
	{0x43,0xa0},//40raw_rb_fac_dummy  
	{0xad,0x40},//raw_rb_fac_low  

	{0x4f,0xa0},//raw_gf_fac_outdoor
	{0xc3,0xa0},//60raw_gf_fac_indoor
	{0x3f,0xa0},//40raw_gf_fac_dummy
	{0x42,0x40},//raw_gf_fac_low
	{0xc2,0x15},

	{0xb6,0x80},//raw_gflt_fac_outdoor
	{0xb7,0x80},//60raw_gflt_fac_normal
	{0xb8,0x40},//40raw_gflt_fac_dummy
	{0xb9,0x20},//raw_gflt_fac_low

	{0xfd,0x01},
	{0x50,0x0c},//raw_grgb_thr
	{0x51,0x0c},
	{0x52,0x10},
	{0x53,0x10},
	{0xfd,0x00},	
	// awb1
	{0xfd,0x01},
	{0x11,0x10},
	{0x12,0x1f},
	{0x16,0x1c},
	{0x18,0x00},
	{0x19,0x00},
	{0x1b,0x96},
	{0x1a,0x9a},//95
	{0x1e,0x2f},
	{0x1f,0x29},
	{0x20,0xff},
	{0x22,0xff},  
	{0x28,0xce},
	{0x29,0x8a},
	{0xfd,0x00},
	{0xe7,0x03},
	{0xe7,0x00},
	{0xfd,0x01},
	{0x2a,0xf0},
	{0x2b,0x10},
	{0x2e,0x04},
	{0x2f,0x18},
	{0x21,0x60},
	{0x23,0x60},
	{0x8b,0xab},
	{0x8f,0x12},
	//awb2
	{0xfd,0x01},
	{0x1a,0x80},
	{0x1b,0x80},
	{0x43,0x80},
    //outdoor
    {0x00,0xd4},
    {0x01,0xb0},
    {0x02,0x90},
    {0x03,0x78},
	//d65
	{0x35,0xd6},//d6;b0
	{0x36,0xf0},//f0;d1;e9
	{0x37,0x7a},//8a;70
	{0x38,0x9a},//dc;9a;af
	//indoor
	{0x39,0xab},
	{0x3a,0xca},
	{0x3b,0xa3},
	{0x3c,0xc1},
	//f
	{0x31,0x82},//7d
	{0x32,0xa5},//a0;74
	{0x33,0xd6},//d2
	{0x34,0xec},//e8
	{0x3d,0xa5},//a7;88
	{0x3e,0xc2},//be;bb
	{0x3f,0xa7},//b3;ad
	{0x40,0xc5},//c5;d0
	//Color Correction				  
	{0xfd,0x01},
	{0x1c,0xc0},
	{0x1d,0x95},
	{0xa0,0xa6},//b8 
	{0xa1,0xda},//;d5
	{0xa2,0x00},//;f2
	{0xa3,0x06},//;e8
	{0xa4,0xb2},//;95
	{0xa5,0xc7},//;03
	{0xa6,0x00},//;f2
	{0xa7,0xce},//;c4
	{0xa8,0xb2},//;ca
	{0xa9,0x0c},//;3c
	{0xaa,0x30},//;03
	{0xab,0x0c},//;0f
	{0xac,0xc0},//b8 
	{0xad,0xc0},//d5
	{0xae,0x00},//f2
	{0xaf,0xf2},//e8
	{0xb0,0xa6},//95
	{0xb1,0xe8},//03
	{0xb2,0x00},//f2
	{0xb3,0xe7},//c4
	{0xb4,0x99},//ca
	{0xb5,0x0c},//3c
	{0xb6,0x33},//03
	{0xb7,0x0c},//0f
	//Saturation
	{0xfd,0x00},
	{0xbf,0x01},
	{0xbe,0xbb},
	{0xc0,0xb0},
	{0xc1,0xf0},
	
    {0xd3,0x77},
    {0xd4,0x77},
    {0xd6,0x77},
    {0xd7,0x77},
    {0xd8,0x77},
    {0xd9,0x77},
    {0xda,0x77},
    {0xdb,0x77},
	//uv_dif
	{0xfd,0x00},
	{0xf3,0x03},
	{0xb0,0x00},
	{0xb1,0x23},
	//gamma1
	{0xfd,0x00},//
	{0x8b,0x0 },//0 ;0	
	{0x8c,0xA },//14;A 
	{0x8d,0x13},//24;13
	{0x8e,0x25},//3a;25
	{0x8f,0x43},//59;43
	{0x90,0x5D},//6f;5D
	{0x91,0x74},//84;74
	{0x92,0x88},//95;88
	{0x93,0x9A},//a3;9A
	{0x94,0xA9},//b1;A9
	{0x95,0xB5},//be;B5
	{0x96,0xC0},//c7;C0
	{0x97,0xCA},//d1;CA
	{0x98,0xD4},//d9;D4
	{0x99,0xDD},//e1;DD
	{0x9a,0xE6},//e9;E6
	{0x9b,0xEF},//f1;EF
	{0xfd,0x01},//01;01
	{0x8d,0xF7},//f9;F7
	{0x8e,0xFF},//ff;FF
	//gamma2   
	{0xfd,0x00},//
	{0x78,0x0 },//0   
	{0x79,0xA },//14
	{0x7a,0x13},//24
	{0x7b,0x25},//3a
	{0x7c,0x43},//59
	{0x7d,0x5D},//6f
	{0x7e,0x74},//84
	{0x7f,0x88},//95
	{0x80,0x9A},//a3
	{0x81,0xA9},//b1
	{0x82,0xB5},//be
	{0x83,0xC0},//c7
	{0x84,0xCA},//d1
	{0x85,0xD4},//d9
	{0x86,0xDD},//e1
	{0x87,0xE6},//e9
	{0x88,0xEF},//f1
	{0x89,0xF7},//f9
	{0x8a,0xFF},//ff
 /*//光斑过度好
 //gamma1
	{0xfd,0x00},
	{0x8b,0x00},
	{0x8c,0x14},
	{0x8d,0x24},
	{0x8e,0x3A},
	{0x8f,0x59},
	{0x90,0x70},
	{0x91,0x85},
	{0x92,0x96},
	{0x93,0xA6},
	{0x94,0xB3},
	{0x95,0xBE},
	{0x96,0xC9},
	{0x97,0xD2},
	{0x98,0xDB},
	{0x99,0xE3},
	{0x9a,0xEB},
	{0x9b,0xF2},
	{0xfd,0x01},
	{0x8d,0xF9},
	{0x8e,0xFF},
	//gamma2   
	{0xfd,0x00},
	{0x78,0x00},
	{0x79,0x14},
	{0x7a,0x24},
	{0x7b,0x3A},
	{0x7c,0x59},
	{0x7d,0x70},
	{0x7e,0x85},
	{0x7f,0x96},
	{0x80,0xA6},
	{0x81,0xB3},
	{0x82,0xBE},
	{0x83,0xC9},
	{0x84,0xD2},
	{0x85,0xDB},
	{0x86,0xE3},
	{0x87,0xEB},
	{0x88,0xF2},
	{0x89,0xF9},
	{0x8a,0xFF},    
*/
	//gamma_ae  
	{0xfd,0x01},
	{0x96,0x46},
	{0x97,0x14},
	{0x9f,0x06},
	//HEQ
	{0xfd,0x00},//
	{0xdd,SP2518_P0_0xdd},//0x80	   
	{0xde,SP2518_P0_0xde},//a0	
	{0xdf,0x80},//
	//Ytarget 
	{0xfd,0x00},// 
	{0xec,0x70},//6a
	{0xed,0x86},//7c
	{0xee,0x70},//65
	{0xef,0x86},//78
	{0xf7,0x80},//78
	{0xf8,0x74},//6e
	{0xf9,0x80},//74
	{0xfa,0x74},//6a 
	//sharpen
	{0xfd,0x01},
	{0xdf,0x0f},
	{0xe5,0x10},
	{0xe7,0x10},
	{0xe8,0x20},
	{0xec,0x20},
	{0xe9,0x20},
	{0xed,0x20},
	{0xea,0x10},
	{0xef,0x10},
	{0xeb,0x10},
	{0xf0,0x10},
	//;gw
	{0xfd,0x01},//
	{0x70,0x76},//
	{0x7b,0x40},//
	{0x81,0x30},//
	//;Y_offset
	{0xfd,0x00},
	{0xb2,0X10},
	{0xb3,0x1f},
	{0xb4,0x30},
	{0xb5,0x50},
	//;CNR
	{0xfd,0x00},
	{0x5b,0x20},
	{0x61,0x80},
	{0x77,0x80},
	{0xca,0x80},
	//;YNR  
	{0xab,0x00},
	{0xac,0x02},
	{0xae,0x08},
	{0xaf,0x20},
	{0xfd,0x00},
	{0x31,0x10},
	{0x32,0x0d},
	{0x33,0xcf},//ef
	{0x34,0x7f},//3f
	
	{0x35,0x40},//3        
	
	{0x1b,0x1a},
	{0xe7,0x03},
	{0xe7,0x00},
	
	//SVGA
	{0xfd,0x00},	

	{0x47,0x00},
	{0x48,0x00},
	{0x49,0x04},
	{0x4a,0xb0},
	
	{0x4b,0x00},
	{0x4c,0x00},	
	{0x4d,0x06},
	{0x4e,0x40},	
	{0xfd,0x01},
	{0x06,0x00},
	{0x07,0x40},
	{0x08,0x00},
	{0x09,0x40},
	{0x0a,0x02},	//600
	{0x0b,0x58},
	{0x0c,0x03},	//800
	{0x0d,0x20},
	{0x0e,0x01},
	{0xfd,0x00},	
	{0x5d,0x0e},	//vsync delay
	
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={

	{0xfd,0x00},	
	{0x47,0x00},
	{0x48,0x00},
	{0x49,0x04},
	{0x4a,0xb0},
	
	{0x4b,0x00},
	{0x4c,0x00},	
	{0x4d,0x06},
	{0x4e,0x40},	

	{0xfd,0x01},
	{0x0e,0x00},
	{0xfd,0x00},
	SensorEnd
};
/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
	{0xfd,0x00},
	{0x47,0x00},
	{0x48,0x00},
	{0x49,0x04},
	{0x4a,0xb0},	
	{0x4b,0x00},
	{0x4c,0x00},	
	{0x4d,0x06},
	{0x4e,0x40},	
	{0xfd,0x01},
	{0x06,0x00},
	{0x07,0x40},
	{0x08,0x00},
	{0x09,0x40},
	{0x0a,0x02},	//600
	{0x0b,0x58},
	{0x0c,0x03},	//800
	{0x0d,0x20},
	{0x0e,0x01},
	{0xfd,0x00},

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
    SensorRegVal(0x02,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
    {0xfd, 0x01}, 
	{0x28, 0xce},
	{0x29, 0x8a},
	{0xfd, 0x00},
	{0x32, 0x0d},
	{0xfd, 0x00},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0xfd,0x00},
	{0x32,0x05},
	{0xfd,0x01},
	{0x28,0xe2},
	{0x29,0x82},
	{0xfd,0x00},	
	{0xfd,0x00},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	{0xfd,0x00},
	{0x32,0x05},
	{0xfd,0x01},
	{0x28,0xc1},
	{0x29,0x88},
	{0xfd,0x00},	
	{0xfd,0x00},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	{0xfd,0x00},
	{0x32,0x05},
	{0xfd,0x01},
	{0x28,0x7b},
	{0x29,0xd3},
	{0xfd,0x00},
	{0xfd,0x00},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	{0x3406, 0x01},
	{0x3400, 0x04},
	{0x3401, 0x58},
	{0x3402, 0x04},
	{0x3403, 0x00},
	{0x3404, 0x07},
	{0x3405, 0x24},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3

	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0xfd, 0x00},
	{0x62, 0x00},
	{0x63, 0x80},
	{0x64, 0x80},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0xfd, 0x00},
	{0x62, 0x20},
	{0x63, 0x80},
	{0x64, 0x80},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0xfd, 0x00},
	{0x62, 0x10},
	{0x63, 0xb0},
	{0x64, 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	{0xfd, 0x00},
	{0x62, 0x04},
	{0x63, 0x80},
	{0x64, 0x80},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	{0xfd, 0x00},
	{0x62, 0x10},
	{0x63, 0x80},
	{0x64, 0xb0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	{0xfd, 0x00},
	{0x62, 0x10},
	{0x63, 0x50},
	{0x64, 0x50},
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
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{
	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{
	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
	sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};
static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	{0xfd, 0x00},
	{0xb2,SP2518_NORMAL_Y0ffset},
	{0xb3,0x1f},

	//SP2518 UXGA 24MEclk 3PLL 50Hz fix13fps
		
	{0xfd,0x00},
	{0x03,0x03},
	{0x04,0xf6},
	{0x05,0x00},
	{0x06,0x00},
	{0x07,0x00},
	{0x08,0x00},
	{0x09,0x00},
	{0x0a,0x8b},
	{0x2f,0x00},
	{0x30,0x08},
	{0xf0,0xa9},
	{0xf1,0x00},
	{0xfd,0x01},
	{0x90,0x07},
	{0x92,0x01},
	{0x98,0xa9},
	{0x99,0x00},
	{0x9a,0x01},
	{0x9b,0x00},
	{0xfd,0x01},
	{0xce,0x9f},
	{0xcf,0x04},
	{0xd0,0x9f},
	{0xd1,0x04},
	{0xd7,0xa5},
	{0xd8,0x00},
	{0xd9,0xa9},
	{0xda,0x00},
	{0xfd,0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	{0xfd,0x00},
	{0xb2,SP2518_LOWLIGHT_Y0ffset},
	{0xb3,0x1f},

	//SP2518 UXGA 24MEclk 3PLL 50Hz fix 6fps
	{0xfd , 0x00},
	{0x03 , 0x01},
	{0x04 , 0xfe},
	{0x05 , 0x00},
	{0x06 , 0x6d},
	{0x07 , 0x00},
	{0x08 , 0x6d},
	{0x09 , 0x04},
	{0x0a , 0xa8},
	{0xf0 , 0x55},
	{0xf1 , 0x00},
	{0xfd , 0x01},
	{0x90 , 0x10},
	{0x92 , 0x01},
	{0x98 , 0x55},
	{0x99 , 0x00},
	{0x9a , 0x01},
	{0x9b , 0x00},
	///Status   
	{0xfd , 0x01},
	{0xce , 0x50},
	{0xcf , 0x05},
	{0xd0 , 0x50},
	{0xd1 , 0x05},
	{0xd7 , 0x57},//51
	{0xd8 , 0x00},
	{0xd9 , 0x5b},//55
	{0xda , 0x00},
	 
	{0xfd,0x00},
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

    SENSOR_DG("%s",__FUNCTION__);	

	
	return 0;
}
/*
* the function is called in close sensor
*/
static int sensor_deactivate_cb(struct i2c_client *client)
{
	//struct generic_sensor *sensor = to_generic_sensor(client);

    SENSOR_DG("%s",__FUNCTION__);
    
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
	int err = 0;
    
    SENSOR_DG("mirror: %d",mirror);
	

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
	int err = 0;	

    SENSOR_DG("flip: %d",flip);
	
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
static int 	sensor_face_detect_usr_cb(struct i2c_client *client,int on){
	return 0;
}

/*
*   The function can been run in sensor_init_parametres which run in sensor_probe, so user can do some
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


