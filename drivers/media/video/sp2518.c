
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
static int debug=0;
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
#define SENSOR_NAME RK29_CAM_SENSOR_SP2518
#define SENSOR_V4L2_IDENT V4L2_IDENT_SP2518
#define SENSOR_ID 0x53
#define SENSOR_ID_REG	0x02
#define SENSOR_MIN_WIDTH    640
#define SENSOR_MIN_HEIGHT   480
#define SENSOR_MAX_WIDTH    1600
#define SENSOR_MAX_HEIGHT   1200
#define SENSOR_INIT_WIDTH	  800			/* Sensor pixel size for sensor_init_data array */
#define SENSOR_INIT_HEIGHT  600
#define SENSOR_INIT_WINSEQADR sensor_vga
#define SENSOR_INIT_PIXFMT V4L2_MBUS_FMT_YUYV8_2X8

#define CONFIG_SENSOR_WhiteBalance	1
#define CONFIG_SENSOR_Brightness	0
#define CONFIG_SENSOR_Contrast      0
#define CONFIG_SENSOR_Saturation    0
#define CONFIG_SENSOR_Effect        1
#define CONFIG_SENSOR_Scene         1
#define CONFIG_SENSOR_DigitalZoom   0
#define CONFIG_SENSOR_Focus         0
#define CONFIG_SENSOR_Exposure      0
#define CONFIG_SENSOR_Flash         0
#define CONFIG_SENSOR_Mirror        0
#define CONFIG_SENSOR_Flip          0

#define CONFIG_SENSOR_I2C_SPEED     100000	///250000       /* Hz */
/* Sensor write register continues by preempt_disable/preempt_enable for current process not be scheduled */
#define CONFIG_SENSOR_I2C_NOSCHED   0
#define CONFIG_SENSOR_I2C_RDWRCHK   0

#define SENSOR_BUS_PARAM  (SOCAM_MASTER | SOCAM_PCLK_SAMPLE_RISING |\
                          SOCAM_HSYNC_ACTIVE_HIGH | SOCAM_VSYNC_ACTIVE_HIGH |\	
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

//AE
#define  SP2518_P0_0xf7  0x78///0x80//78
#define  SP2518_P0_0xf8  0x6e///0x74//6e
#define  SP2518_P0_0xf9  0x74///0x80//74
#define  SP2518_P0_0xfa  0x6a///0x74//6a

//HEQ
#define  SP2518_P0_0xdd  0x7c	//0x80 modify by sp_yjp,20120814
#define  SP2518_P0_0xde  0x90	//0x95 modify by sp_yjp,20120814

//auto lum
#define SP2518_NORMAL_Y0ffset  	  0x10	//0x0f	 modify by sp_yjp,20120813
#define SP2518_LOWLIGHT_Y0ffset  0x20





struct reginfo
{
    u8 reg;
    u8 val;
};
///=========SP2518-modify by sp_yjp,20120529=================
/* init 640X480 VGA */
static struct reginfo sensor_init_data[] =
{
	#if 1
	{0xfd,0x00},
	{0x1b,0x1a},//maximum drv ability //0x02 modify by sp_yjp,20120809
	{0x0e,0x01},

	{0x0f,0x2f},
	{0x10,0x2e},
	{0x11,0x00},
	{0x12,0x4f},
	{0x14,0x20},
	{0x16,0x02},
	{0x17,0x10},
	{0x1a,0x1f},
	{0x1e,0x81},
	{0x21,0x00},
	{0x22,0x1b},
	{0x25,0x10},
	{0x26,0x25},
	{0x27,0x6d},
	{0x2c,0x31},//Ronlus remove balck dot0x45},
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
	///SP2518 UXGA 24MEclk 3±¶Æµ 1·ÖÆµ 50Hz fix 11fps
	{0xfd , 0x00},
	{0x03 , 0x03},
	{0x04 , 0xa8},
	{0x05 , 0x00},
	{0x06 , 0x6d},
	{0x07 , 0x00},
	{0x08 , 0x6d},
	{0x09 , 0x00},
	{0x0a , 0xe4},
	{0x2f , 0x00},
	{0x30 , 0x08},
	{0xf0 , 0x9c},
	{0xf1 , 0x00},
	{0xfd , 0x01},
	{0x90 , 0x09},
	{0x92 , 0x01},
	{0x98 , 0x9c},
	{0x99 , 0x00},
	{0x9a , 0x01},
	{0x9b , 0x00},
	///Status
	{0xfd , 0x01},
	{0xce , 0x7c},
	{0xcf , 0x05},
	{0xd0 , 0x7c},
	{0xd1 , 0x05},
	{0xd7 , 0x98},
	{0xd8 , 0x00},
	{0xd9 , 0x9c},
	{0xda , 0x00},
	{0xfd , 0x00},
	#endif

	#if 1
	///SP2518 UXGA 24MEclk 3±¶Æµ 1·ÖÆµ 50Hz fix 10fps
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
	{0xd7 , 0x8d},
	{0xd8 , 0x00},
	{0xd9 , 0x91},
	{0xda , 0x00},
	{0xfd , 0x00},
	#endif
	
	
	#if 0
	/// UXGA 24MEclk 3±¶Æµ 1·ÖÆµ 50Hz fix 9fps
	{0xfd , 0x00},
	{0x03 , 0x03},
	{0x04 , 0x42},
	{0x05 , 0x00},
	{0x06 , 0xf0},
	{0x07 , 0x00},
	{0x08 , 0xf0},
	{0x09 , 0x01},
	{0x0a , 0x71},
	{0x2f , 0x00},
	{0x30 , 0x08},
	{0xf0 , 0x8b},
	{0xf1 , 0x00},
	{0xfd , 0x01},
	{0x90 , 0x0b},
	{0x92 , 0x01},
	{0x98 , 0x8b},
	{0x99 , 0x00},
	{0x9a , 0x01},
	{0x9b , 0x00},
	///Status  
	{0xfd , 0x01},
	{0xce , 0xf9},
	{0xcf , 0x05},
	{0xd0 , 0xf9},
	{0xd1 , 0x05},
	{0xd7 , 0x87},
	{0xd8 , 0x00},
	{0xd9 , 0x8b},
	{0xda , 0x00},
	{0xfd , 0x00},
	#endif
	
	#if 0
	/*24*3pll 8~13fps 50hz*/
	{0xfd , 0x00},
	{0x03 , 0x03},
	{0x04 , 0xf6},
	{0x05 , 0x00},
	{0x06 , 0x00},
	{0x07 , 0x00},
	{0x08 , 0x00},
	{0x09 , 0x00},
	{0x0a , 0x8b},
	///////////////////SP2518_write_cmos_sensor(0x2f , 0x00},	modify by sp_yjp,20120809
	{0x30 , 0x08},	//0x08 modify by sp_yjp,20120809
	{0xf0 , 0xa9},
	{0xf1 , 0x00},
	{0xfd , 0x01},
	{0x90 , 0x0c},
	{0x92 , 0x01},
	{0x98 , 0xa9},
	{0x99 , 0x00},
	{0x9a , 0x01},
	{0x9b , 0x00},
	
	//Status
	{0xfd , 0x01},
	{0xce , 0xec},
	{0xcf , 0x07},
	{0xd0 , 0xec},
	{0xd1 , 0x07},
	{0xd7 , 0xa5},
	{0xd8 , 0x00},
	{0xd9 , 0xa9},
	{0xda , 0x00},
	{0xfd , 0x00},
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
	
	{0xd3,0x68},	//0x77 modify by sp_yjp,20120814
	{0xd4,0x68},	//0x77 modify by sp_yjp,20120814
	{0xd6,0x68},	//0x77 modify by sp_yjp,20120814
	{0xd7,0x68},	//0x77 modify by sp_yjp,20120814
	{0xd8,0x68},	//0x77 modify by sp_yjp,20120814
	{0xd9,0x68},	//0x77 modify by sp_yjp,20120814
	{0xda,0x68},	//0x77 modify by sp_yjp,20120814
	{0xdb,0x68},	//0x77 modify by sp_yjp,20120814
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
	
	{0x1b,0x02},
	{0xe7,0x03},
	{0xe7,0x00},

	//SP2518_config_window(WINDOW_SIZE_VGA}
	#if 1
	{0xfd,0x00},
	{0x4b,0x00},
	{0x4c,0x00},
	{0x47,0x00},
	{0x48,0x00},
	{0x4d,0x06},
	{0x4e,0x40},
	{0x49,0x04},
	{0x4a,0xb0},

	{0xfd,0x01},
	{0x06,0x00},
	{0x07,0x50},
	{0x08,0x00},
	{0x09,0x50},
	{0x0a,0x01},	//480
	{0x0b,0xe0},	
	{0x0c,0x02},	//640
	{0x0d,0x80},
	{0x0e,0x01},
	{0xfd,0x00},	
	#else	
	{0xfd,0x00},
	{0x4b,0x00},
	{0x4c,0x00},
	{0x47,0x00},
	{0x48,0x00},
	{0x4d,0x06},
	{0x4e,0x40},
	{0x49,0x04},
	{0x4a,0xb0},
	
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
	#endif
	#endif

	{0x5d,0x0e},	
	{0xff,0xff}//The end flag
};


/* 1600X1200 UXGA */
static struct reginfo sensor_uxga[] = 
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
	{0x0e,0x00},
	{0xfd,0x00},
	{0xfd,0x00},
	
	{0xff,0xff}//The end flag
};


/* 1280X1024 SXGA */
static struct reginfo sensor_sxga[] =
{
	{0xfd,0x00},	
	{0x47,0x00},
	{0x48,0x50},
	{0x49,0x04},
	{0x4a,0x00},
	
	{0x4b,0x00},
	{0x4c,0x90},	
	{0x4d,0x05},
	{0x4e,0x00},	

	{0xfd,0x01},
	{0x0e,0x00},
	{0xfd,0x00},
	{0xff,0xff}
};

/* 800X600 SVGA*/
static struct reginfo sensor_svga[] =
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
	
	{0xff,0xff}//The end flag
};

/* 640X480 VGA */
static struct reginfo sensor_vga[] =
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
	{0x07,0x50},
	{0x08,0x00},
	{0x09,0x50},
	{0x0a,0x01},	//480
	{0x0b,0xe0},	
	{0x0c,0x02},	//640
	{0x0d,0x80},
	{0x0e,0x01},
	{0xfd,0x00},	
	
	{0xff,0xff}//The end flag


};
///=========SP2518-modify by sp_yjp,20120529=================

/* 352X288 CIF */
static struct reginfo sensor_cif[] =
{
    {0xfd, 0x00},
    {0xff,0xff}
};

/* 320*240 QVGA */
static  struct reginfo sensor_qvga[] =
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
	{0x07,0xa0},	
	{0x08,0x00},
	{0x09,0xa0},
	
	{0x0a,0x00},	//240
	{0x0b,0xf0},		
	{0x0c,0x01},	//320
	{0x0d,0x40},
	
	{0x0e,0x01},
	{0xfd,0x00},	

	{0xff,0xff}//The end flag
};

/* 176X144 QCIF*/
static struct reginfo sensor_qcif[] =
{
    {0xfd, 0x00},{0xff,0xff}
};

static  struct reginfo sensor_ClrFmt_YUYV[]=
{
    {0xfd, 0x00},{0xff,0xff}
};

static  struct reginfo sensor_ClrFmt_UYVY[]=
{
    {0xfd, 0x00},{0xff,0xff}
};

///=========SP2518-modify by sp_yjp,20120529=================

#if CONFIG_SENSOR_WhiteBalance
static  struct reginfo sensor_WhiteB_Auto[]=
{	
	{0xfd, 0x01}, 
	{0x28, 0xce},
	{0x29, 0x8a},
	{0xfd, 0x00},
	{0x32, 0x0d},
	{0xfd, 0x00},
	{0xff, 0xff}
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct reginfo sensor_WhiteB_Cloudy[]=
{
	{0xfd,0x00},
	{0x32,0x05},
	{0xfd,0x01},
	{0x28,0xe2},
	{0x29,0x82},
	{0xfd,0x00},	
	{0xfd,0x00},
	{0xff,0xff}
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct reginfo sensor_WhiteB_ClearDay[]=
{
	{0xfd,0x00},
	{0x32,0x05},
	{0xfd,0x01},
	{0x28,0xc1},
	{0x29,0x88},
	{0xfd,0x00},	
	{0xfd,0x00},
	{0xff,0xff}
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct reginfo sensor_WhiteB_TungstenLamp1[]=
{
	{0xfd,0x00},
	{0x32,0x05},
	{0xfd,0x01},
	{0x28,0x7b},
	{0x29,0xd3},
	{0xfd,0x00},
	{0xfd,0x00},
	{0xff,0xff}

};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct reginfo sensor_WhiteB_TungstenLamp2[]=
{
	{0xfd,0x00},
	{0x32,0x05},
	{0xfd,0x01},
	{0x28,0xae},
	{0x29,0xcc},
	{0xfd,0x00},	 
	{0xfd,0x00},
	{0xff,0xff}
};
static struct reginfo *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};
#endif


///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_Brightness
static  struct reginfo sensor_Brightness0[]=
{
    	{0xfd, 0x00},
	{0xdc, 0xe0},
	{0xff, 0xff}
};

static  struct reginfo sensor_Brightness1[]=
{
    	{0xfd, 0x00},
	{0xdc, 0xf0},
	{0xff, 0xff}
};

static  struct reginfo sensor_Brightness2[]=
{
    	{0xfd, 0x00},
	{0xdc, 0x00},
	{0xfd, 0x00},
	{0xff, 0xff}
};

static  struct reginfo sensor_Brightness3[]=
{
    	{0xfd, 0x00},
	{0xdc, 0x10},
	{0xff, 0xff}
};

static  struct reginfo sensor_Brightness4[]=
{
	{0xfd, 0x00},
	{0xdc, 0x20},
	{0xff, 0xff}
};

static  struct reginfo sensor_Brightness5[]=
{
    	{0xfd, 0x00},
	{0xdc, 0x30},
	{0xff, 0xff}
};
static struct reginfo *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

#endif

///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_Effect
static  struct reginfo sensor_Effect_Normal[] =
{
	{0xfd, 0x00},
	{0x62, 0x00},
	{0x63, 0x80},
	{0x64, 0x80},
	{0xff, 0xff}
};

static  struct reginfo sensor_Effect_WandB[] =
{
	{0xfd, 0x00},
	{0x62, 0x20},
	{0x63, 0x80},
	{0x64, 0x80},
	{0xff, 0xff}
};

static  struct reginfo sensor_Effect_Sepia[] =
{
	{0xfd, 0x00},
	{0x62, 0x10},
	{0x63, 0xb0},
	{0x64, 0x40},
	{0xff, 0xff}
};

static  struct reginfo sensor_Effect_Negative[] =
{
	{0xfd, 0x00},
	{0x62, 0x04},
	{0x63, 0x80},
	{0x64, 0x80},
	{0xff, 0xff}
};
static  struct reginfo sensor_Effect_Bluish[] =
{
	{0xfd, 0x00},
	{0x62, 0x10},
	{0x63, 0x80},
	{0x64, 0xb0},
	{0xff, 0xff}
};

static  struct reginfo sensor_Effect_Green[] =
{
	{0xfd, 0x00},
	{0x62, 0x10},
	{0x63, 0x50},
	{0x64, 0x50},
	{0xff, 0xff}
};
static struct reginfo *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};
#endif

///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_Exposure
static  struct reginfo sensor_Exposure0[]=
{
	//level -3   
	{0xfd,0x00},   
	{0xed,SP2518_P0_0xf7-0x18+0x04},	
	{0xf7,SP2518_P0_0xf7-0x18},
	{0xf8,SP2518_P0_0xf8-0x18},	
	{0xec,SP2518_P0_0xf8-0x18-0x04},
	{0xef,SP2518_P0_0xf9-0x18+0x04},
	{0xf9,SP2518_P0_0xf9-0x18},
	{0xfa,SP2518_P0_0xfa-0x18},	
	{0xee,SP2518_P0_0xfa-0x18-0x04},		
	
	{0xfd, 0x00},
	{0xff,0xff}
};

static  struct reginfo sensor_Exposure1[]=
{
	//level -2   
	{0xfd,0x00},   
	{0xed,SP2518_P0_0xf7-0x10+0x04},	
	{0xf7,SP2518_P0_0xf7-0x10},
	{0xf8,SP2518_P0_0xf8-0x10},	
	{0xec,SP2518_P0_0xf8-0x10-0x04},
	{0xef,SP2518_P0_0xf9-0x10+0x04},
	{0xf9,SP2518_P0_0xf9-0x10},
	{0xfa,SP2518_P0_0xfa-0x10},	
	{0xee,SP2518_P0_0xfa-0x10-0x04},
	
	{0xfd, 0x00},
	{0xff,0xff}
};

static  struct reginfo sensor_Exposure2[]=
{
	//level -1   
	{0xfd,0x00},   
	{0xed,SP2518_P0_0xf7-0x08+0x04},	
	{0xf7,SP2518_P0_0xf7-0x08},
	{0xf8,SP2518_P0_0xf8-0x08},	
	{0xec,SP2518_P0_0xf8-0x08-0x04},
	{0xef,SP2518_P0_0xf9-0x08+0x04},
	{0xf9,SP2518_P0_0xf9-0x08},
	{0xfa,SP2518_P0_0xfa-0x08},	
	{0xee,SP2518_P0_0xfa-0x08-0x04},	
	
	{0xfd, 0x00},
	{0xff,0xff}
};

static  struct reginfo sensor_Exposure3[]=
{
	//level 0   
	{0xfd,0x00},   
	{0xed,SP2518_P0_0xf7+0x04},	
	{0xf7,SP2518_P0_0xf7},
	{0xf8,SP2518_P0_0xf8},	
	{0xec,SP2518_P0_0xf8-0x04},
	{0xef,SP2518_P0_0xf9+0x04},
	{0xf9,SP2518_P0_0xf9},
	{0xfa,SP2518_P0_0xfa},	
	{0xee,SP2518_P0_0xfa-0x04},		
	
	{0xfd, 0x00},
	{0xff,0xff}
};

static  struct reginfo sensor_Exposure4[]=
{
	//level +1   
	{0xfd,0x00},   
	{0xed,SP2518_P0_0xf7+0x08+0x04},	
	{0xf7,SP2518_P0_0xf7+0x08},
	{0xf8,SP2518_P0_0xf8+0x08},	
	{0xec,SP2518_P0_0xf8+0x08-0x04},
	{0xef,SP2518_P0_0xf9+0x08+0x04},
	{0xf9,SP2518_P0_0xf9+0x08},
	{0xfa,SP2518_P0_0xfa+0x08},	
	{0xee,SP2518_P0_0xfa+0x08-0x04},
	
	{0xfd, 0x00},
	{0xff,0xff}
};

static  struct reginfo sensor_Exposure5[]=
{
	//level +2   
	{0xfd,0x00},   
	{0xed,SP2518_P0_0xf7+0x10+0x04},	
	{0xf7,SP2518_P0_0xf7+0x10},
	{0xf8,SP2518_P0_0xf8+0x10},	
	{0xec,SP2518_P0_0xf8+0x10-0x04},
	{0xef,SP2518_P0_0xf9+0x10+0x04},
	{0xf9,SP2518_P0_0xf9+0x10},
	{0xfa,SP2518_P0_0xfa+0x10},	
	{0xee,SP2518_P0_0xfa+0x10-0x04},
	
	{0xfd, 0x00},
	{0xff,0xff}
};

static  struct reginfo sensor_Exposure6[]=
{
	//level +3   
	{0xfd,0x00},   
	{0xed,SP2518_P0_0xf7+0x18+0x04},	
	{0xf7,SP2518_P0_0xf7+0x18},
	{0xf8,SP2518_P0_0xf8+0x18},	
	{0xec,SP2518_P0_0xf8+0x18-0x04},
	{0xef,SP2518_P0_0xf9+0x18+0x04},
	{0xf9,SP2518_P0_0xf9+0x18},
	{0xfa,SP2518_P0_0xfa+0x18},	
	{0xee,SP2518_P0_0xfa+0x18-0x04},	
	
	{0xfd, 0x00},
	{0xff,0xff}
};

static struct reginfo *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};
#endif

///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_Saturation
static  struct reginfo sensor_Saturation0[]=
{
	{0xfd, 0x00},
	{0xff, 0xff}
};

static  struct reginfo sensor_Saturation1[]=
{
	{0xfd, 0x00},
	{0xff, 0xff}
};

static  struct reginfo sensor_Saturation2[]=
{
	{0xfd, 0x00},
	{0xff, 0xff}
};
static struct reginfo *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

#endif

///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_Contrast
static  struct reginfo sensor_Contrast0[]=
{
	//level -3
	{0xfd , 0x00},
	{0xdd , SP2518_P0_0xdd-0x30},	
	{0xde , SP2518_P0_0xde-0x30},	
	{0xff , 0xff}
};

static  struct reginfo sensor_Contrast1[]=
{
	//level -2
	{0xfd , 0x00},
	{0xdd , SP2518_P0_0xdd-0x20},	
	{0xde , SP2518_P0_0xde-0x20},	
	{0xff , 0xff}
};

static  struct reginfo sensor_Contrast2[]=
{
    //level -1
	{0xfd , 0x00},
	{0xdd , SP2518_P0_0xdd-0x10},	
	{0xde , SP2518_P0_0xde-0x10},	
	{0xff , 0xff}
};

static  struct reginfo sensor_Contrast3[]=
{
    //level 0
	{0xfd , 0x00},
	{0xdd , SP2518_P0_0xdd},	
	{0xde , SP2518_P0_0xde},	
	{0xff , 0xff}
};

static  struct reginfo sensor_Contrast4[]=
{
    //level +1
	{0xfd , 0x00},
	{0xdd , SP2518_P0_0xdd+0x10},	
	{0xde , SP2518_P0_0xde+0x10},	
	{0xff , 0xff}
};


static  struct reginfo sensor_Contrast5[]=
{
    //level +2
	{0xfd , 0x00},
	{0xdd , SP2518_P0_0xdd+0x20},	
	{0xde , SP2518_P0_0xde+0x20},	
	{0xff , 0xff}
};

static  struct reginfo sensor_Contrast6[]=
{
    //level +3
	{0xfd , 0x00},
	{0xdd , SP2518_P0_0xdd+0x30},	
	{0xde , SP2518_P0_0xde+0x30},	
	{0xff , 0xff}
};




static struct reginfo *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};

#endif


///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_Mirror
static  struct reginfo sensor_MirrorOn[]=
{
	{0xfd, 0x00},
	{0x31, 0x30},
	{0xff, 0xff}
};

static  struct reginfo sensor_MirrorOff[]=
{
	{0xfd, 0x00},
	{0x31, 0x10},
	{0xff, 0xff}
};
static struct reginfo *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};
#endif
#if CONFIG_SENSOR_Flip
static  struct reginfo sensor_FlipOn[]=
{
	{0xfd, 0x00},
	{0x31, 0x50},
	{0xff, 0xff}
};

static  struct reginfo sensor_FlipOff[]=
{
	{0xfd, 0x00},
	{0x31, 0x10},
	{0xff, 0xff}
};
static struct reginfo *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

#endif

///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_Scene//? zch
static  struct reginfo sensor_SceneAuto[] =
{
	#if 1
	{0xfd, 0x00},
	{0xb2,SP2518_NORMAL_Y0ffset},
	{0xb3,0x1f},

	///SP2518 UXGA 24MEclk 3PLL 50Hz fix 10fps
	{0xfd , 0x00},
	{0x03 , 0x03},
	{0x04 , 0x66},
	{0x05 , 0x00},
	{0x06 , 0x8b},
	{0x07 , 0x00},
	{0x08 , 0x8b},
	{0x09 , 0x01},
	{0x0a , 0x3b},
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
	{0xd7 , 0x8d},
	{0xd8 , 0x00},
	{0xd9 , 0x91},
	{0xda , 0x00},
	{0xfd , 0x00},
	#endif
	
	{0xfd, 0x00},
	{0xff, 0xff}
};

static  struct reginfo sensor_SceneNight[] =
{
	#if 1
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
	{0xd7 , 0x51},
	{0xd8 , 0x00},
	{0xd9 , 0x55},
	{0xda , 0x00},
	#endif
	 
	{0xfd,0x00},
	{0xff,0xff}
};



static struct reginfo *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

#endif

///=========SP2518-modify by sp_yjp,20120529=================
#if CONFIG_SENSOR_DigitalZoom
static struct reginfo sensor_Zoom0[] =
{
	{0xfd,0x00},
	{0xff,0xff}
};

static struct reginfo sensor_Zoom1[] =
{
  	{0xfd,0x00},
	{0xff,0xff}
};

static struct reginfo sensor_Zoom2[] =
{
   	{0xfd,0x00},
	{0xff,0xff}
};


static struct reginfo sensor_Zoom3[] =
{
   	{0xfd,0x00},
	{0xff,0xff}
};
static struct reginfo *sensor_ZoomSeqe[] = {sensor_Zoom0, sensor_Zoom1, sensor_Zoom2, sensor_Zoom3, NULL,};
#endif


///=========SP2518-modify by sp_yjp,20120529=================
static const struct v4l2_querymenu sensor_menus[] =
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

static const struct v4l2_queryctrl sensor_controls[] =
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
static int sensor_set_effect(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
static int sensor_set_whiteBalance(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int value);
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

    buf[0] = reg & 0xFF;
    buf[1] = val;

    msg->addr = client->addr;
    msg->flags = client->flags;
    msg->buf = buf;
    msg->len = sizeof(buf);
    msg->scl_rate = CONFIG_SENSOR_I2C_SPEED;                                        /* ddl@rock-chips.com : 100kHz */
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

/* sensor register read */
static int sensor_read(struct i2c_client *client, u8 reg, u8 *val)
{
    int err,cnt;
    //u8 buf[2];
    u8 buf[1];
    struct i2c_msg msg[2];

    //buf[0] = reg >> 8;
    buf[0] = reg;
    buf[1] = reg & 0xFF;

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

    cnt = 1;
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

/* write a array of registers  */
static int sensor_write_array(struct i2c_client *client, struct reginfo *regarray)
{
    int err;
    int i = 0;

	while((regarray[i].reg != 0xff) || (regarray[i].val != 0xff))
    {
        err = sensor_write(client, regarray[i].reg, regarray[i].val);
        if (err != 0)
        {
            SENSOR_TR("%s..write failed current i = %d\n", SENSOR_NAME_STRING(),i);
            return err;
        }
		i++;
    }
    
    return 0;
}
#if CONFIG_SENSOR_I2C_RDWRCHK
static int sensor_check_array(struct i2c_client *client, struct reginfo *regarray)
{
    int ret;
    int i = 0;

    u8 value;

    while((regarray[i].reg != 0xff) || (regarray[i].val != 0xff))
    {
        ret = sensor_read(client,regarray[i].reg,&value);
        if(ret !=0)
        {
            SENSOR_TR("read value failed\n");

        }
        if(regarray[i].val != value)
        {
            SENSOR_DG("%s reg[0x%x] check err,writte :0x%x  read:0x%x\n",__FUNCTION__,regarray[i].reg,regarray[i].val,value);
        }

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
	msleep(10);
	if (sensor_ioctrl(icd, Sensor_PowerDown, 1) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}
	msleep(10);
	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_INIT_ERR;
	}
	msleep(10);

    /* soft reset */
	if (sensor_task_lock(client,1)<0)
		goto sensor_INIT_ERR;
   /* ret = sensor_write(client, 0x12, 0x80);
    if (ret != 0)
    {
        SENSOR_TR("%s soft reset sensor failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
		goto sensor_INIT_ERR;
    }

    mdelay(5); */ //delay 5 microseconds

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
	//u8 reg_val;
    struct sensor *sensor = to_sensor(client);
	SENSOR_DG("\n%s..%s.. Enter\n",SENSOR_NAME_STRING(),__FUNCTION__);

	/* ddl@rock-chips.com : all sensor output pin must change to input for other sensor */
	sensor_ioctrl(icd, Sensor_PowerDown, 1);
    msleep(100); 

	/* ddl@rock-chips.com : sensor config init width , because next open sensor quickly(soc_camera_open -> Try to configure with default parameters) */
	icd->user_width = SENSOR_INIT_WIDTH;
    icd->user_height = SENSOR_INIT_HEIGHT;
    sensor->info_priv.funmodule_state &= ~SENSOR_INIT_IS_OK;
	
	return 0;
}
static  struct reginfo sensor_power_down_sequence[]=
{
    {0xfd,0x00},
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
    const struct sensor_datafmt *fmt;
    struct sensor *sensor = to_sensor(client);
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

	if (((set_w <= 176) && (set_h <= 144)) && sensor_qcif[0].reg)
	{
		winseqe_set_addr = sensor_qcif;
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && sensor_qvga[0].reg)
    {
        winseqe_set_addr = sensor_qvga;
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && sensor_cif[0].reg)
    {
        winseqe_set_addr = sensor_cif;
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && sensor_vga[0].reg)
    {
        winseqe_set_addr = sensor_vga;
        set_w = 640-16;
        set_h = 480-16;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && sensor_svga[0].reg)
    {
        winseqe_set_addr = sensor_svga;
        set_w = 800;
        set_h = 600;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && sensor_sxga[0].reg)
    {
        winseqe_set_addr = sensor_sxga;
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && sensor_uxga[0].reg)
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
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
			if (sensor->info_priv.whiteBalance != 0) {
				qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
				sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
			}
			sensor->info_priv.snap2preview = true;
		} else if (sensor_fmt_videochk(sd,mf) == true) {			/* ddl@rock-chips.com : Video */
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
			sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
			sensor->info_priv.video2preview = true;
		} else if ((sensor->info_priv.snap2preview == true) || (sensor->info_priv.video2preview == true)) {
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_EFFECT);
			sensor_set_effect(icd, qctrl,sensor->info_priv.effect);
			qctrl = soc_camera_find_qctrl(&sensor_ops, V4L2_CID_DO_WHITE_BALANCE);
			sensor_set_whiteBalance(icd, qctrl,sensor->info_priv.whiteBalance);
            msleep(600);
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

	if (((set_w <= 176) && (set_h <= 144)) && sensor_qcif[0].reg)
	{
        set_w = 176;
        set_h = 144;
	}
	else if (((set_w <= 320) && (set_h <= 240)) && sensor_qvga[0].reg)
    {
        set_w = 320;
        set_h = 240;
    }
    else if (((set_w <= 352) && (set_h<= 288)) && sensor_cif[0].reg)
    {
        set_w = 352;
        set_h = 288;
    }
    else if (((set_w <= 640) && (set_h <= 480)) && sensor_vga[0].reg)
    {
        set_w = 640;
        set_h = 480;
    }
    else if (((set_w <= 800) && (set_h <= 600)) && sensor_svga[0].reg)
    {
        set_w = 800;
        set_h = 600;
    }
    else if (((set_w <= 1280) && (set_h <= 1024)) && sensor_sxga[0].reg)
    {
        set_w = 1280;
        set_h = 1024;
    }
    else if (((set_w <= 1600) && (set_h <= 1200)) && sensor_uxga[0].reg)
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

    id->ident = SENSOR_V4L2_IDENT;      /* ddl@rock-chips.com :  Return OV9650  identifier */
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
static int sensor_set_digitalzoom(struct soc_camera_device *icd, const struct v4l2_queryctrl *qctrl, int *value)
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

    if ((*value > 0) && (digitalzoom_cur >= digitalzoom_total))
    {
        SENSOR_TR("%s digitalzoom is maximum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if  ((*value < 0) && (digitalzoom_cur <= qctrl_info->minimum))
    {
        SENSOR_TR("%s digitalzoom is minimum - %x\n", SENSOR_NAME_STRING(), digitalzoom_cur);
        return -EINVAL;
    }

    if ((*value > 0) && ((digitalzoom_cur + *value) > digitalzoom_total))
    {
        *value = digitalzoom_total - digitalzoom_cur;
    }

    if ((*value < 0) && ((digitalzoom_cur + *value) < 0))
    {
        *value = 0 - digitalzoom_cur;
    }

    digitalzoom_cur += *value;

    if (sensor_ZoomSeqe[digitalzoom_cur] != NULL)
    {
        if (sensor_write_array(client, sensor_ZoomSeqe[digitalzoom_cur]) != 0)
        {
            SENSOR_TR("%s..%s WriteReg Fail.. \n",SENSOR_NAME_STRING(), __FUNCTION__);
            return -EINVAL;
        }
        SENSOR_DG("%s..%s : %x\n",SENSOR_NAME_STRING(),__FUNCTION__, *value);
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
    char pid = 0;
    int ret;
    struct sensor *sensor = to_sensor(client);

    /* We must have a parent by now. And it cannot be a wrong one.
     * So this entire test is completely redundant. */
    if (!icd->dev.parent ||
	    to_soc_camera_host(icd->dev.parent)->nr != icd->iface)
		return -ENODEV;

	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_video_probe_err;
	}
	msleep(10);
	if (sensor_ioctrl(icd, Sensor_PowerDown, 1) < 0) {
		ret = -ENODEV;
		goto sensor_video_probe_err;
	}
	msleep(10);
	if (sensor_ioctrl(icd, Sensor_PowerDown, 0) < 0) {
		ret = -ENODEV;
		goto sensor_video_probe_err;
	}
	msleep(10);

    /* check if it is an sensor sensor */
	ret = sensor_read(client, SENSOR_ID_REG, &pid);
    if (ret != 0) {
        SENSOR_TR("%s read chip id high byte failed\n",SENSOR_NAME_STRING());
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }

    SENSOR_DG("\n %s  pid = 0x%x\n", SENSOR_NAME_STRING(), pid);

    if (pid == SENSOR_ID) {
        sensor->model = SENSOR_V4L2_IDENT;
    } else {
        SENSOR_TR("error: %s mismatched   pid = 0x%x\n", SENSOR_NAME_STRING(), pid);
        ret = -ENODEV;
        goto sensor_video_probe_err;
    }
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

    int i;

    
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
                			memset((char*)&icd->ops->controls[i],0x00,sizeof(struct v4l2_queryctrl));                			
                		}
                    }
                    sensor->info_priv.flash = 0xff;
                    SENSOR_DG("%s flash gpio is invalidate!\n",SENSOR_NAME_STRING());
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


