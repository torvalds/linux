/*
 * drivers/media/video/sp0838.c
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
#define SENSOR_NAME RK29_CAM_SENSOR_SP0838
#define SENSOR_V4L2_IDENT V4L2_IDENT_SP0838
#define SENSOR_ID 0x27
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
                                             V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_HIGH|\
                                             V4L2_MBUS_DATA_ACTIVE_LOW   | SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W                     640
#define SENSOR_PREVIEW_H                     480
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

#ifndef SP0838_MIRROR
#define SP0838_MIRROR 0
#endif
#ifndef SP0838_FLIP
#define SP0838_FLIP 0
#endif
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

/* Sensor initial setting *//* init 640X480 VGA */
static struct rk_sensor_reg sensor_init_data[] =
{
	{0xfd , 0x00}, //P0
	{0x1B , 0x02},
#ifdef SP0838_FRONT_DRVCAP 
	{0x1C , 0x03},	//add by sp_yjp,bit2: driver ability set(8mA)
#else
	{0x1C , 0x07},
#endif
	{0x30 , 0x02},//   1/2ispclk
	{0x27 , 0xe8},
	{0x28 , 0x0b}, // 0x0B pzt 2012-7-26
	{0x32 , 0x00},
	{0x22 , 0xc0},
	{0x26 , 0x10}, 
	{0x31 , 0x10|(SP0838_FLIP<<6)|(SP0838_MIRROR<<5)},//0x10},   //Upside/mirr/Pclk inv/sub

	{0x5f , 0x11},   //Bayer order
	{0xfd , 0x01},   //P1
//{0x20 , 0x68}, 
//{0x21 , 0x68}, 
//{0x22 , 0x7f}, 
//{0x23 , 0x30}, 

	
	{0x25 , 0x1a},   //Awb start
	{0x26 , 0xfb}, 
	{0x28 , 0x75}, 
	{0x29 , 0x4e},
	{0xfd , 0x00},   
	{0xe7 , 0x03}, 
	{0xe7 , 0x00}, 
	{0xfd , 0x01},

	{0x31 , 0x60},//64
	{0x32 , 0x18},
	{0x4d , 0xdc},
	{0x4e , 0x53},
	{0x41 , 0x8c},
	{0x42 , 0x57},
	{0x55 , 0xff},
	{0x56 , 0x00},
	{0x59 , 0x82},
	{0x5a , 0x00},
	{0x5d , 0xff},
	{0x5e , 0x6f},
	{0x57 , 0xff},
	{0x58 , 0x00},
	{0x5b , 0xff},
	{0x5c , 0xa8},
	{0x5f , 0x75},
	{0x60 , 0x00},
	{0x2d , 0x00},
	{0x2e , 0x00},
	{0x2f , 0x00},
	{0x30 , 0x00},
	{0x33 , 0x00},
	{0x34 , 0x00},
	{0x37 , 0x00},
	{0x38 , 0x00},  //awb end
	{0xfd , 0x00},  //P0
	{0x33 , 0x6f},  //LSC BPC EN  // 0x6f pzt 2012-7-26
	{0x51 , 0x3f},  //BPC debug start
	{0x52 , 0x09},  
	{0x53 , 0x00},  
	{0x54 , 0x00},
	{0x55 , 0x10},  //BPC debug end
	{0x4f , 0xff},  //blueedge
	{0x50 , 0xff},
	{0x57 , 0x10},  //Raw filter debut start
	{0x58 , 0x10},
	{0x59 , 0x10},
	{0x56 , 0x71},  //0x70  modify by sp_yjp,20120613
	
	{0x5a , 0x06},	//{0x5a , 0x02},    //0x05 modify by sp_yjp,20130709
	{0x5b , 0x08},//	{0x5b , 0x02},  //0x06 modify by sp_yjp,20130709
	{0x5c , 0x30},  //Raw filter debut end  //0x20 modify by sp_yjp,20120613

	{0x65 , 0x04},  //Sharpness debug start	//0x03 modify by sp_yjp,20130709
	{0x66 , 0x01}, //	{0x66 , 0x01},sp_wyq
	{0x67 , 0x08},//	{0x67 , 0x03},  //0x05  //modify by sp_yjp,20120613
	{0x68 , 0x46},//	{0x68 , 0x46},//0x46 zch 20120725
	
	{0x69 , 0x7f},
	{0x6a , 0x01},
	
	{0x6b , 0x04},//{0x6b , 0x04},	//0x06 modify by sp_yjp,20130709
	{0x6c , 0x01},
	{0x6d , 0x06},  //Edge gain normal {0x6d , 0x03},  //0x05  //modify by sp_yjp,20120613
	{0x6e , 0x46},  //Edge gain normal 	{0x6e , 0x46}, //0x46 zch 20120725
	
	{0x6f , 0x7f},
	{0x70 , 0x01},

	{0x71 , 0x0a}, //Ã¯Ã¯Â¿Â½Ã¯Â¿Â½Ã¯Â¿Â½Ã¯Â¿Â½Ã¯Â¿Â½ÃÂµ//0x09    modify by sp_yjp,20130709       
	{0x72 , 0x01}, //Ã¯Â¿Â½Ã¯Â½Ã¯Â¿Â½Ã¯Â¿Â½Ã¯Â¿Â½Ã¯Â¿Â½Ã?      
	{0x73 , 0x02}, //? //0x05  //modify by sp_yjp,20120613
	{0x74 , 0x43}, //Ã¯Â¿//0x47? //modify by sp_yjp,20120613


	{0x75 , 0x7f}, //ÃÂ¹Ã¿Â¿Â½Ã¿Â¿Â½ÃÂ»           
	{0x76 , 0x01},  //Sharpness debug end
	{0xcb , 0x07},  //HEQ&Saturation debug start 
	{0xcc , 0x04},
	{0xce , 0xff},
	{0xcf , 0x10},
	{0xd0 , 0x20},
	{0xd1 , 0x00},
	{0xd2 , 0x1c},
	{0xd3 , 0x16},
	{0xd4 , 0x00},
	{0xd6 , 0x1c},
	{0xd7 , 0x16},
	{0xdc , 0x08},
	{0xdd , 0x70},  //Contrast  //0x70  //modify by sp_yjp,20130709
	{0xde , 0xa0},  //HEQ&Saturation debug end  //0x90 //modify by sp_yjp,20130709

	#if 0
	{0x7f , 0xe1},  //Color Correction start
	{0x80 , 0xbc},                          
	{0x81 , 0xe3},                          
	{0x82 , 0xd7},                          
	{0x83 , 0xd5},                          
	{0x84 , 0xd4},                          
	{0x85 , 0xff},                          
	{0x86 , 0x89},                          
	{0x87 , 0xf8},                          
	{0x88 , 0x3c},                          
	{0x89 , 0x33},                          
	{0x8a , 0x0f},  //Color Correction end  
	#endif


#if 0//ÉÔºì                                                         
		{0x7f , 0xee},	//Color Correction start					
		{0x80 , 0xc9},												
		{0x81 , 0xc9},												
		{0x82 , 0xd7},												
		{0x83 , 0xe5},												
		{0x84 , 0xc4},												
		{0x85 , 0x07},												
		{0x86 , 0x9a},												
		{0x87 , 0xde},												
		{0x88 , 0x3c},												
		{0x89 , 0x33},												
		{0x8a , 0x0c},	//Color Correction end						
#endif                                                      


#if 1
{0x7f , 0xf4},//0xed//0xe1	//Color Correction start
{0x80 , 0xbc},//0xbc//0xbc							
{0x81 , 0xd0},//0xd7//0xe3							
{0x82 , 0xbe},//0xbe//0xd7							
{0x83 , 0xd5},//0xd5//0xd5							
{0x84 , 0xee},//0xee//0xd4							
{0x85 , 0xec},//0xf3//0xff							
{0x86 , 0xb6},//0xaf//0x89							
{0x87 , 0xde},//0xdf//0xf8							
{0x88 , 0x3c},//0x3c//0x3c							
{0x89 , 0x33},//0x33//0x33							
{0x8a , 0x0f},//0x0f//0x0f	//Color Correction end	
#endif

	
	{0x8b , 0x00},  //gamma start
	{0x8c , 0x1a},               
	{0x8d , 0x29},               
	{0x8e , 0x41},               
	{0x8f , 0x62},               
	{0x90 , 0x7c},               
	{0x91 , 0x90},               
	{0x92 , 0xa2},               
	{0x93 , 0xaf},               
	{0x94 , 0xbc},               
	{0x95 , 0xc5},               
	{0x96 , 0xcd},               
	{0x97 , 0xd5},               
	{0x98 , 0xda},               
	{0x99 , 0xe3},               
	{0x9a , 0xe8},               
	{0x9b , 0xf0},               
	{0xfd , 0x01},  //P1         
	{0x8d , 0xf6},               
	{0x8e , 0xfa},  //gamma end  
	{0xfd , 0x00},  //P0
	{0xca , 0xcf},

	#if 0
	{0xd8 , 0x58},  //UV outdoor
	{0xd9 , 0x58},  //UV indoor 
	{0xda , 0x50},  //UV dummy  //0x48  //modify by sp_yjp,20120613
	{0xdb , 0x40},  //UV lowlight   //0x48  //modify by sp_yjp,20120613
	#else
	{0xd8 , 0x58},  //UV outdoor
	{0xd9 , 0x58},  //UV indoor 
	{0xda , 0x50},  //UV dummy  //0x48  //modify by sp_yjp,20120613
	{0xdb , 0x50},  //UV lowlight   //0x48  //modify by sp_yjp,20120613
	#endif
	
	{0xb9 , 0x00},  //Ygamma start
	{0xba , 0x04},
	{0xbb , 0x08},
	{0xbc , 0x10},
	{0xbd , 0x20},
	{0xbe , 0x30},
	{0xbf , 0x40},
	{0xc0 , 0x50},
	{0xc1 , 0x60},
	{0xc2 , 0x70},
	{0xc3 , 0x80},
	{0xc4 , 0x90},
	{0xc5 , 0xA0},
	{0xc6 , 0xB0},
	{0xc7 , 0xC0},
	{0xc8 , 0xD0},
	{0xc9 , 0xE0},
	{0xfd , 0x01},  //P1
	{0x89 , 0xf0},
	{0x8a , 0xff},  //Ygamma end
	{0xfd , 0x00},  //P0
	{0xe8 , 0x30},  //AEdebug start
	{0xe9 , 0x30},
	{0xea , 0x40},  //Alc Window sel
	{0xf4 , 0x1b},  //outdoor mode sel
	{0xf5 , 0x80},

	///{0xf7 , 0x78},  //AE target
	///{0xf8 , 0x63},  
	///{0xf9 , 0x68},  //AE target
	///{0xfa , 0x53},

	#if 1
	{0xf7 , 0x88},  //AE target  //modify by sp_yjp,20130709
	{0xf8 , 0x73},  
	{0xf9 , 0x78},  //AE target
	{0xfa , 0x63},
	#else
	{0xf7 , 0x80},  //AE target  //modify by sp_yjp,20120613
	{0xf8 , 0x6b},  
	{0xf9 , 0x70},  //AE target
	{0xfa , 0x5b},
	#endif

	{0xfd , 0x01},  //P1
	{0x09 , 0x31},  //AE Step 3.0
	{0x0a , 0x85},
	{0x0b , 0x0b},  //AE Step 3.0
	{0x14 , 0x20},
	{0x15 , 0x0f},

	/*
#if 0//24M 1div 50HZ 16-8fps    //modify by sp_yjp,20120613
	{0xfd , 0x00},
	{0x05 , 0x0 },
	{0x06 , 0x0 },
	{0x09 , 0x2 },
	{0x0a , 0x9d},
	{0xf0 , 0x4f},
	{0xf1 , 0x0 },
	{0xf2 , 0x5b},
	{0xf5 , 0x74},
	{0xfd , 0x01},
	{0x00 , 0xae},
	{0x0f , 0x5c},
	{0x16 , 0x5c},
	{0x17 , 0x9e},
	{0x18 , 0xa6},
	{0x1b , 0x5c},
	{0x1c , 0xa6},
	{0xb4 , 0x21},
	{0xb5 , 0x3b},
	{0xb6 , 0x4b},
	{0xb9 , 0x40},
	{0xba , 0x4f},
	{0xbb , 0x47},
	{0xbc , 0x45},
	{0xbd , 0x43},
	{0xbe , 0x42},
	{0xbf , 0x42},
	{0xc0 , 0x42},
	{0xc1 , 0x41},
	{0xc2 , 0x41},
	{0xc3 , 0x41},
	{0xc4 , 0x41},
	{0xc5 , 0x70},  //0x70
	{0xc6 , 0x41},
	{0xca , 0x70},  //0x70
	{0xcb , 0xc },
	{0xfd , 0x00},
#else//caprure preview daylight 24M 50hz 20-8FPS maxgain:0x70	
	{0xfd , 0x00},
	{0x05 , 0x0 },
	{0x06 , 0x0 },
	{0x09 , 0x1 },
	{0x0a , 0x76},
	{0xf0 , 0x62},
	{0xf1 , 0x0 },
	{0xf2 , 0x5f},
	{0xf5 , 0x78},
	{0xfd , 0x01},
	{0x00 , 0xb2},
	{0x0f , 0x60},
	{0x16 , 0x60},
	{0x17 , 0xa2},
	{0x18 , 0xaa},
	{0x1b , 0x60},
	{0x1c , 0xaa},
	{0xb4 , 0x20},
	{0xb5 , 0x3a},
	{0xb6 , 0x5e},
	{0xb9 , 0x40},
	{0xba , 0x4f},
	{0xbb , 0x47},
	{0xbc , 0x45},
	{0xbd , 0x43},
	{0xbe , 0x42},
	{0xbf , 0x42},
	{0xc0 , 0x42},
	{0xc1 , 0x41},
	{0xc2 , 0x41},
	{0xc3 , 0x41},
	{0xc4 , 0x41},
	{0xc5 , 0x70},
	{0xc6 , 0x41},
	{0xca , 0x70},
	{0xcb , 0xc },
	{0xfd , 0x00},
#endif	
	*/
	#if 0//zch 20130709
	//caprure preview daylight 24M 50hz 15-10FPS maxgain:0x68				 
	{0xfd,0x00},
	{0x05,0x00},
	{0x06,0x00},
	{0x09,0x03},
	{0x0a,0x04},
	{0xf0,0x4a},
	{0xf1,0x00},
	{0xf2,0x59},
	{0xf5,0x72},
	{0xfd,0x01},
	{0x00,0xa0},
	{0x0f,0x5a},
	{0x16,0x5a},
	{0x17,0x98},
	{0x18,0xa0},
	{0x1b,0x5a},
	{0x1c,0xa0},
	{0xb4,0x20},
	{0xb5,0x3a},
	{0xb6,0x46},
	{0xb9,0x40},
	{0xba,0x4f},
	{0xbb,0x47},
	{0xbc,0x45},
	{0xbd,0x43},
	{0xbe,0x42},
	{0xbf,0x42},
	{0xc0,0x42},
	{0xc1,0x41},
	{0xc2,0x41},
	{0xc3,0x68},
	{0xc4,0x41},
	{0xc5,0x41},
	{0xc6,0x41},
	{0xca,0x68},
	{0xcb,0x0a},
	{0xfd,0x00},
	#endif

	#if 0//2/1/2 ISP9-12fps
	{0xfd,0x00},						
	{0x05,0x00},						
	{0x06,0x00},						
	{0x09,0x00},						
	{0x0a,0xa7},						
	{0xf0,0x3b},						
	{0xf1,0x00},						
	{0xf2,0x56},						
	{0xf5,0x6f},						
	{0xfd,0x01},						
	{0x00,0xa9},						
	{0x0f,0x57},						
	{0x16,0x57},						
	{0x17,0x99},						
	{0x18,0xa1},						
	{0x1b,0x57},						
	{0x1c,0xa1},						
	{0xb4,0x21},						
	{0xb5,0x39},						
	{0xb6,0x39},						
	{0xb9,0x40},						
	{0xba,0x4f},						
	{0xbb,0x47},						
	{0xbc,0x45},						
	{0xbd,0x43},						
	{0xbe,0x42},						
	{0xbf,0x42},						
	{0xc0,0x42},						
	{0xc1,0x41},						
	{0xc2,0x41},						
	{0xc3,0x41},						
	{0xc4,0x41},						
	{0xc5,0x70},						
	{0xc6,0x41},						
	{0xca,0x70},						
	{0xcb,0x0c},						
	{0xfd,0x00},						
					 
	#endif

#if 0//10-12fps
{0xfd,0x00},      
{0x05,0x00},      
{0x06,0x00},      
{0x09,0x00},      
{0x0a,0xa7},      
{0xf0,0x3b},      
{0xf1,0x00},      
{0xf2,0x56},      
{0xf5,0x6f},      
{0xfd,0x01},      
{0x00,0xa7},      
{0x0f,0x57},      
{0x16,0x57},      
{0x17,0x97},      
{0x18,0x9f},      
{0x1b,0x57},      
{0x1c,0x9f},      
{0xb4,0x21},      
{0xb5,0x39},      
{0xb6,0x39},      
{0xb9,0x40},      
{0xba,0x4f},      
{0xbb,0x47},      
{0xbc,0x45},      
{0xbd,0x43},      
{0xbe,0x42},      
{0xbf,0x42},      
{0xc0,0x42},      
{0xc1,0x41},      
{0xc2,0x41},      
{0xc3,0x41},      
{0xc4,0x70},      
{0xc5,0x41},      
{0xc6,0x41},      
{0xca,0x70},      
{0xcb,0x0b},      
{0xfd,0x00},      

#endif


#if 1
//sensor AE settings:6-11fps                                      
{0xfd,0x00},                                                      
{0x05,0x00},                                                      
{0x06,0x00},                                                      
{0x09,0x01},                                                      
{0x0a,0x05},                                                      
{0xf0,0x36},                                                      
{0xf1,0x00},                                                      
{0xf2,0x53},                                                      
{0xf5,0x6c},                                                      
{0xfd,0x01},                                                      
{0x00,0x9c},                                                      
{0x0f,0x54},                                                      
{0x16,0x54},                                                      
{0x17,0x9c},                                                      
{0x18,0x00},                                                      
{0x1b,0x54},                                                      
{0x1c,0x43},                                                      
{0xb4,0x20},                                                      
{0xb5,0x34},                                                      
{0xb6,0x34},                                                      
{0xb9,0x40},                                                      
{0xba,0x4f},                                                      
{0xbb,0x47},                                                      
{0xbc,0x45},                                                      
{0xbd,0x43},                                                      
{0xbe,0x42},                                                      
{0xbf,0x42},                                                      
{0xc0,0x42},                                                      
{0xc1,0x41},                                                      
{0xc2,0x41},                                                      
{0xc3,0x41},                                                      
{0xc4,0x41},                                                      
{0xc5,0x41},                                                      
{0xc6,0x41},                                                      
{0xca,0x60},                                                      
{0xcb,0x10},                                                      
{0xfd,0x00},                                                                                                       
#endif
	//added by yaowei@2013-06-05
	//{0xfd,0x00},  //P0
	//{0xd9,0x55},
	//{0xda,0x55},
	//{0xdb,0x55},
	//{0xdc,0xe0},
	

	{0xfd , 0x00},  //P0
	{0x32 , 0x15},  //Auto_mode set
	{0x34 , 0x66},  //Isp_mode set
	{0x35 , 0x40},   //out format
	{0xfd , 0x00},  //P0
	SensorEnd
};


/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={

    {0x47, 0x00},
    {0x48, 0x00},
    {0x49, 0x01},
    {0x4a, 0xe0},
    {0x4b, 0x00},
    {0x4c, 0x00},
    {0x4d, 0x02},
    {0x4e, 0x80},
	SensorEnd
};
/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
    {0x47, 0x00},
    {0x48, 0x00},
    {0x49, 0x01},
    {0x4a, 0xe0},
    {0x4b, 0x00},
    {0x4c, 0x00},
    {0x4d, 0x02},
    {0x4e, 0x80},

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
    SensorRegVal(0x02,0x27),
	SensorEnd
};

///=========sp0838-modify by sp_yjp,20120529=================

static  struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	//sp0838_reg_WB_auto         ï¿½Ô¶ï¿½     
	{0xfd, 0x01},                                                          
	{0x28, 0x75},		                                                       
	{0x29, 0x4e},
	{0xfd, 0x00},  // AUTO 3000K~7000K                                     
	{0x32, 0x15}, 
	{0xfd, 0x00},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static  struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	// sp0838_reg_WB_auto   ï¿½ï¿½ï¿½ï¿½
	{0xfd, 0x00},                                   
	{0x32, 0x05},                                                          
	{0xfd, 0x01},                                                          
	{0x28, 0x71},		                                                       
	{0x29, 0x41},		                                                       
    {0xfd, 0x00},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K  */
static  struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
    //Sunny
	// sp0838_reg_WB_auto  ï¿½ï¿½ï¿½ï¿½ 
	{0xfd, 0x00},                                   
	{0x32, 0x05},                                                          
	{0xfd, 0x01},                                                          
	{0x28, 0x6b},		                                                       
	{0x29, 0x48},		                                                       
    {0xfd, 0x00},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static  struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
    //Office
	//sp0838_reg_WB_auto  Ó«ï¿½ï¿½ï¿½ï¿½ 
    {0xfd, 0x00},
    {0x32, 0x05},                                                          
    {0xfd, 0x01},                                                          
    {0x28, 0x41},		                                                       
    {0x29, 0x71},		                                                       
    {0xfd, 0x00},
	SensorEnd
};
/* Home Colour Temperature : 2500K - 3500K  */
static  struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
    //Home
	//sp0838_reg_WB_auto ï¿½×³ï¿½ï¿½ï¿½ 
    {0xfd, 0x00},                                  
    {0x32, 0x05},                                                          
    {0xfd, 0x01},                                                          
    {0x28, 0x5a},		                                                       
    {0x29, 0x62},		                                                       
    {0xfd, 0x00},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
    sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

///=========sp0838-modify by sp_yjp,20120529=================
static  struct rk_sensor_reg sensor_Brightness0[]=
{
    // Brightness -2
    {0xfd, 0x00},
	{0xdc, 0xe0},
	{0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Brightness1[]=
{
    // Brightness -1
    {0xfd, 0x00},
	{0xdc, 0xf0},
	{0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Brightness2[]=
{
    //  Brightness 0
    {0xfd, 0x00},
	{0xdc, 0x10},
	{0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Brightness3[]=
{
    // Brightness +1
    {0xfd, 0x00},
	{0xdc, 0x10},
	{0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Brightness4[]=
{
    //  Brightness +2
    {0xfd, 0x00},
	{0xdc, 0x20},
	{0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Brightness5[]=
{
    //  Brightness +3
    {0xfd, 0x00},
	{0xdc, 0x30},
	{0xfd, 0x00},
	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
    sensor_Brightness4, sensor_Brightness5,NULL,
};

///=========sp0838-modify by sp_yjp,20120529=================
static  struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0xfd, 0x00},
	{0x62, 0x00},
	{0x63, 0x80},
	{0x64, 0x80},
    {0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0xfd, 0x00},
	{0x62, 0x40},
	{0x63, 0x80},
	{0x64, 0x80},
    {0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0xfd, 0x00},
	{0x62, 0x20},
	{0x63, 0xc0},
	{0x64, 0x20},
    {0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Effect_Negative[] =
{
    //Negative
	{0xfd, 0x00},
	{0x62, 0x10},
	{0x63, 0x80},
	{0x64, 0x80},
    {0xfd, 0x00},
	SensorEnd
};
static  struct rk_sensor_reg sensor_Effect_Bluish[] =
{
    // Bluish
    {0xfd, 0x00},
	{0x62, 0x20},
	{0x63, 0x20},
	{0x64, 0xf0},
    {0xfd, 0x00},
	SensorEnd
};

static  struct rk_sensor_reg sensor_Effect_Green[] =
{
    //  Greenish
    {0xfd, 0x00},
	{0x62, 0x20},
	{0x63, 0x20},
	{0x64, 0x20},
    {0xfd, 0x00},
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
    sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

///=========sp0838-modify by sp_yjp,20120529=================
static  struct rk_sensor_reg sensor_Exposure0[]=
{
	{0xfd, 0x00},
	{0xdc, 0xd0},
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_Exposure1[]=
{
	{0xfd, 0x00},
	{0xdc, 0xe0},
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_Exposure2[]=
{
	{0xfd, 0x00},
	{0xdc, 0xf0},
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_Exposure3[]=
{
	{0xfd, 0x00},
	{0xdc, 0x10},
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_Exposure4[]=
{
	{0xfd, 0x00},
	{0xdc, 0x10},
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_Exposure5[]=
{
	{0xfd, 0x00},
	{0xdc, 0x20},
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_Exposure6[]=
{
	{0xfd, 0x00},
	{0xdc, 0x30},
    {0xfd, 0x00},
    SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
    sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};

///=========sp0838-modify by sp_yjp,20120529=================
static  struct rk_sensor_reg sensor_Saturation0[]=
{
    SensorEnd
};

static  struct rk_sensor_reg sensor_Saturation1[]=
{
    SensorEnd
};

static  struct rk_sensor_reg sensor_Saturation2[]=
{
    SensorEnd
};
static struct rk_sensor_reg *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

///=========sp0838-modify by sp_yjp,20120529=================
static  struct rk_sensor_reg sensor_Contrast0[]=
{
    SensorEnd
};

static  struct rk_sensor_reg sensor_Contrast1[]=
{
    SensorEnd
};

static  struct rk_sensor_reg sensor_Contrast2[]=
{
SensorEnd
};

static  struct rk_sensor_reg sensor_Contrast3[]=
{
SensorEnd
};

static  struct rk_sensor_reg sensor_Contrast4[]=
{
SensorEnd
};


static  struct rk_sensor_reg sensor_Contrast5[]=
{
SensorEnd
};

static  struct rk_sensor_reg sensor_Contrast6[]=
{
SensorEnd
};




static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
    sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};


/*
///=========sp0838-modify by sp_yjp,20120529=================
static  struct rk_sensor_reg sensor_MirrorOn[]=
{
    {0xfd, 0x00},   //page 0
    {0x31, 0x30},   //bit6:flip  bit5:mirror  bit4:pclk
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_MirrorOff[]=
{
    {0xfd, 0x00},   //page 0
    {0x31, 0x10},
    {0xfd, 0x00},
    SensorEnd
};
//static struct rk_sensor_reg *sensor_MirrorSeqe[] = {sensor_MirrorOff, sensor_MirrorOn,NULL,};

static  struct rk_sensor_reg sensor_FlipOn[]=
{
    {0xfd, 0x00},   //page 0
    {0x31, 0x50},
    {0xfd, 0x00},
    SensorEnd
};

static  struct rk_sensor_reg sensor_FlipOff[]=
{
    {0xfd, 0x00},   //page 0
    {0x31, 0x10},
    {0xfd, 0x00},
    SensorEnd
};*/
//static struct rk_sensor_reg *sensor_FlipSeqe[] = {sensor_FlipOff, sensor_FlipOn,NULL,};

///=========sp0838-modify by sp_yjp,20120529=================

static  struct rk_sensor_reg sensor_SceneAuto[] =
{
#if 0
//caprure preview daylight 24M 50hz 20-10FPS maxgain:0x70				 
{0xfd,0x00},
{0x05,0x00},
{0x06,0x00},
{0x07,0x00},
{0x08,0x00},
{0x09,0x01},
{0x0a,0x76},
{0xf0,0x62},
{0xf1,0x00},
{0xf2,0x5f},
{0xf5,0x78},
{0xfd,0x01},
{0x00,0xae},
{0x0f,0x60},
{0x16,0x60},
{0x17,0x9e},
{0x18,0xa6},
{0x1b,0x60},
{0x1c,0xa6},
{0xb4,0x20},
{0xb5,0x3a},
{0xb6,0x5e},
{0xb9,0x40},
{0xba,0x4f},
{0xbb,0x47},
{0xbc,0x45},
{0xbd,0x43},
{0xbe,0x42},
{0xbf,0x42},
{0xc0,0x42},
{0xc1,0x41},
{0xc2,0x41},
{0xc3,0x70},
{0xc4,0x41},
{0xc5,0x41},
{0xc6,0x41},
{0xca,0x70},
{0xcb,0x0a},
{0x14,0x20},
{0x15,0x0f},
{0xfd,0x00},
#endif
#if 0//9-12

{0xfd,0x00},                        
{0x05,0x00},                        
{0x06,0x00},                        
{0x09,0x00},                        
{0x0a,0xa7},                        
{0xf0,0x3b},                        
{0xf1,0x00},                        
{0xf2,0x56},                        
{0xf5,0x6f},                        
{0xfd,0x01},                        
{0x00,0xa9},                        
{0x0f,0x57},                        
{0x16,0x57},                        
{0x17,0x99},                        
{0x18,0xa1},                        
{0x1b,0x57},                        
{0x1c,0xa1},                        
{0xb4,0x21},                        
{0xb5,0x39},                        
{0xb6,0x39},                        
{0xb9,0x40},                        
{0xba,0x4f},                        
{0xbb,0x47},                        
{0xbc,0x45},                        
{0xbd,0x43},                        
{0xbe,0x42},                        
{0xbf,0x42},                        
{0xc0,0x42},                        
{0xc1,0x41},                        
{0xc2,0x41},                        
{0xc3,0x41},                        
{0xc4,0x41},                        
{0xc5,0x70},                        
{0xc6,0x41},                        
{0xca,0x70},                        
{0xcb,0x0c},                        
{0xfd,0x00},                                    
#endif

#if 0//10-12
{0xfd,0x00},      
{0x05,0x00},      
{0x06,0x00},      
{0x09,0x00},      
{0x0a,0xa7},      
{0xf0,0x3b},      
{0xf1,0x00},      
{0xf2,0x56},      
{0xf5,0x6f},      
{0xfd,0x01},      
{0x00,0xa7},      
{0x0f,0x57},      
{0x16,0x57},      
{0x17,0x97},      
{0x18,0x9f},      
{0x1b,0x57},      
{0x1c,0x9f},      
{0xb4,0x21},      
{0xb5,0x39},      
{0xb6,0x39},      
{0xb9,0x40},      
{0xba,0x4f},      
{0xbb,0x47},      
{0xbc,0x45},      
{0xbd,0x43},      
{0xbe,0x42},      
{0xbf,0x42},      
{0xc0,0x42},      
{0xc1,0x41},      
{0xc2,0x41},      
{0xc3,0x41},      
{0xc4,0x70},      
{0xc5,0x41},      
{0xc6,0x41},      
{0xca,0x70},      
{0xcb,0x0b},      
{0xfd,0x00},      

#endif

#if 1

//sensor AE settings:6-11fps                                      
{0xfd,0x00},                                                      
{0x05,0x00},                                                      
{0x06,0x00},                                                      
{0x09,0x01},                                                      
{0x0a,0x05},                                                      
{0xf0,0x36},                                                      
{0xf1,0x00},                                                      
{0xf2,0x53},                                                      
{0xf5,0x6c},                                                      
{0xfd,0x01},                                                      
{0x00,0x9c},                                                      
{0x0f,0x54},                                                      
{0x16,0x54},                                                      
{0x17,0x9c},                                                      
{0x18,0x00},                                                      
{0x1b,0x54},                                                      
{0x1c,0x43},                                                      
{0xb4,0x20},                                                      
{0xb5,0x34},                                                      
{0xb6,0x34},                                                      
{0xb9,0x40},                                                      
{0xba,0x4f},                                                      
{0xbb,0x47},                                                      
{0xbc,0x45},                                                      
{0xbd,0x43},                                                      
{0xbe,0x42},                                                      
{0xbf,0x42},                                                      
{0xc0,0x42},                                                      
{0xc1,0x41},                                                      
{0xc2,0x41},                                                      
{0xc3,0x41},                                                      
{0xc4,0x41},                                                      
{0xc5,0x41},                                                      
{0xc6,0x41},                                                      
{0xca,0x60},                                                      
{0xcb,0x10},                                                      
{0xfd,0x00},                                                      
                                                 
#endif

SensorEnd
};

static  struct rk_sensor_reg sensor_SceneNight[] =
{
#if 0
	//caprure preview night 24M 50hz 20-6FPS maxgain:0x78                  
	{0xfd,0x00},
	{0x05,0x0 },
	{0x06,0x0 },
	{0x09,0x1 },
	{0x0a,0x76},
	{0xf0,0x62},
	{0xf1,0x0 },
	{0xf2,0x5f},
	{0xf5,0x78},
	{0xfd,0x01},
	{0x00,0xc0},
	{0x0f,0x60},
	{0x16,0x60},
	{0x17,0xa8},
	{0x18,0xb0},
	{0x1b,0x60},
	{0x1c,0xb0},
	{0xb4,0x20},
	{0xb5,0x3a},
	{0xb6,0x5e},
	{0xb9,0x40},
	{0xba,0x4f},
	{0xbb,0x47},
	{0xbc,0x45},
	{0xbd,0x43},
	{0xbe,0x42},
	{0xbf,0x42},
	{0xc0,0x42},
	{0xc1,0x41},
	{0xc2,0x41},
	{0xc3,0x41},
	{0xc4,0x41},
	{0xc5,0x41},
	{0xc6,0x41},
	{0xca,0x78},
	{0xcb,0x10},
	{0x14,0x20},
	{0x15,0x1f},
	{0xfd,0x00},
	#endif
	#if 0
	{0xfd,0x00},								   
	{0x05,0x00},								   
	{0x06,0x00},								   
	{0x09,0x00},								   
	{0x0a,0xa7},								   
	{0xf0,0x3b},								   
	{0xf1,0x00},								   
	{0xf2,0x56},								   
	{0xf5,0x6f},								   
	{0xfd,0x01},								   
	{0x00,0xaf},								   
	{0x0f,0x57},								   
	{0x16,0x57},								   
	{0x17,0x9f},								   
	{0x18,0xa7},								   
	{0x1b,0x57},								   
	{0x1c,0xa7},								   
	{0xb4,0x21},								   
	{0xb5,0x39},								   
	{0xb6,0x39},								   
	{0xb9,0x40},								   
	{0xba,0x4f},								   
	{0xbb,0x47},								   
	{0xbc,0x45},								   
	{0xbd,0x43},								   
	{0xbe,0x42},								   
	{0xbf,0x42},								   
	{0xc0,0x42},								   
	{0xc1,0x41},								   
	{0xc2,0x41},								   
	{0xc3,0x41},								   
	{0xc4,0x41},								   
	{0xc5,0x41},								   

	#endif
	
#if 1
	
//sensor AE settings:6-11fps                                      
{0xfd,0x00},                                                      
{0x05,0x00},                                                      
{0x06,0x00},                                                      
{0x09,0x01},                                                      
{0x0a,0x05},                                                      
{0xf0,0x36},                                                      
{0xf1,0x00},                                                      
{0xf2,0x53},                                                      
{0xf5,0x6c},                                                      
{0xfd,0x01},                                                      
{0x00,0x9c},                                                      
{0x0f,0x54},                                                      
{0x16,0x54},                                                      
{0x17,0x9c},                                                      
{0x18,0x00},                                                      
{0x1b,0x54},                                                      
{0x1c,0x43},                                                      
{0xb4,0x20},                                                      
{0xb5,0x34},                                                      
{0xb6,0x34},                                                      
{0xb9,0x40},                                                      
{0xba,0x4f},                                                      
{0xbb,0x47},                                                      
{0xbc,0x45},                                                      
{0xbd,0x43},                                                      
{0xbe,0x42},                                                      
{0xbf,0x42},                                                      
{0xc0,0x42},                                                      
{0xc1,0x41},                                                      
{0xc2,0x41},                                                      
{0xc3,0x41},                                                      
{0xc4,0x41},                                                      
{0xc5,0x41},                                                      
{0xc6,0x41},                                                      
{0xca,0x60},                                                      
{0xcb,0x10},                                                      
{0xfd,0x00},                                                      
								 
	
#endif
	
SensorEnd
};

static struct rk_sensor_reg *sensor_SceneSeqe[] = {sensor_SceneAuto, sensor_SceneNight,NULL,};

///=========sp0838-modify by sp_yjp,20120529=================
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


///=========sp0838-modify by sp_yjp,20120529=================
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
//static struct soc_camera_ops sensor_ops;

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

static int sensor_focus_af_zoneupdate_usr_cb(struct i2c_client *client, int *zone_tm_pos){
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

#define CHIP_NAME   RK29_CAM_SENSOR_NAME_SP0838
#define DBG_DIR "fcam"
#include "./add-cam-inf.c"

* :::::WARNING:::::
* It is not allowed to modify the following code
*/

sensor_init_parameters_default_code();

sensor_v4l2_struct_initialization();

sensor_probe_default_code();

sensor_remove_default_code();

sensor_driver_default_module_code();

