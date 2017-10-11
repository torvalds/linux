/*
 * drivers/media/video/gt2005.c
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
#define SENSOR_NAME RK29_CAM_SENSOR_GT2005
#define SENSOR_V4L2_IDENT V4L2_IDENT_GT2005
#define SENSOR_ID 0x5138
#define SENSOR_BUS_PARAM                     (V4L2_MBUS_MASTER |\
														 V4L2_MBUS_PCLK_SAMPLE_RISING|V4L2_MBUS_HSYNC_ACTIVE_HIGH| V4L2_MBUS_VSYNC_ACTIVE_HIGH|\
														 V4L2_MBUS_DATA_ACTIVE_HIGH  |SOCAM_MCLK_24MHZ)
#define SENSOR_PREVIEW_W					 640
#define SENSOR_PREVIEW_H					 480
#define SENSOR_PREVIEW_FPS					 15000	   // 15fps 
#define SENSOR_FULLRES_L_FPS				 7500	   // 7.5fps
#define SENSOR_FULLRES_H_FPS				 7500	   // 7.5fps
#define SENSOR_720P_FPS 					 0
#define SENSOR_1080P_FPS					 0

#define SENSOR_REGISTER_LEN 				 2		   // sensor register address bytes
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
static struct rk_sensor_reg sensor_init_data[] ={
		{0x0101 , 0x00},
		{0x0103 , 0x00}, 
		{0x0105 , 0x00},
		{0x0106 , 0xF0},
		{0x0107 , 0x00},
		{0x0108 , 0x1C},
		{0x0109 , 0x01},
		{0x010A , 0x00},
		{0x010B , 0x00},
		{0x010C , 0x00},
		{0x010D , 0x08},
		{0x010E , 0x00},
		{0x010F , 0x08},
		{0x0110 , 0x06},
		{0x0111 , 0x40},
		{0x0112 , 0x04},
		{0x0113 , 0xB0},
	{0x0114 , 0x00},
		{0x0115 , 0x00},
		//{0x0116 , 0x02},
		//{0x0117 , 0x00},
		  // {0x0118 , 0x67},
		//{0x0119 , 0x02},
		//{0x011A , 0x04},
		//{0x011B , 0x01},
	{0x011C , 0x00},//0x01 2011 11 04
	{0x011D , 0x02},
	{0x011E , 0x00},
	{0x011F , 0x00},
	{0x0120 , 0x1C},
	{0x0121 , 0x00},
	{0x0122 , 0x04},
	{0x0123 , 0x00},
	{0x0124 , 0x00},
	{0x0125 , 0x00},
	{0x0126 , 0x00},
	{0x0127 , 0x00},
	{0x0128 , 0x00},
	{0x0200 , 0x00},
		{0x0201 , 0x08},  //0x00
	{0x0202 , 0x40},
	{0x0203 , 0x00},
		{0x0204 , 0x78},
	{0x0205 , 0x1F},
	{0x0206 , 0x0B},
	{0x0207 , 0x20},
	{0x0208 , 0x00},
	{0x0209 , 0x2A},
	{0x020A , 0x01},
	{0x020B , 0x48},
	{0x020C , 0x64},
	{0x020D , 0xC8},
	{0x020E , 0xBC},
	{0x020F , 0x08},
	{0x0210 , 0xD6},
	{0x0211 , 0x00},
	{0x0212 , 0x20},
	{0x0213 , 0x81},
	{0x0214 , 0x15},
	{0x0215 , 0x00},
	{0x0216 , 0x00},
	{0x0217 , 0x00},
	{0x0218 , 0x46},
	{0x0219 , 0x30},
	{0x021A , 0x03},
	{0x021B , 0x28},
	{0x021C , 0x02},
	{0x021D , 0x60},
	{0x021E , 0x00},
		{0x021F , 0x00},
		{0x0220 , 0x10},
		{0x0221 , 0x10},
		{0x0222 , 0x10},
		{0x0223 , 0x10},
		{0x0224 , 0x1F},
	{0x0225 , 0x1E},
	{0x0226 , 0x18},
	{0x0227 , 0x1D},
	{0x0228 , 0x1F},
	{0x0229 , 0x1F},
	{0x022A , 0x01},
	{0x022B , 0x04},
	{0x022C , 0x05},
	{0x022D , 0x05},
	{0x022E , 0x04},
	{0x022F , 0x03},
	{0x0230 , 0x02},
	{0x0231 , 0x1F},
	{0x0232 , 0x1A},
	{0x0233 , 0x19},
	{0x0234 , 0x19},
	{0x0235 , 0x1B},
	{0x0236 , 0x1F},
	{0x0237 , 0x04},
	{0x0238 , 0xEE},
	{0x0239 , 0xFF},
	{0x023A , 0x00},
	{0x023B , 0x00},
	{0x023C , 0x00},
	{0x023D , 0x00},
	{0x023E , 0x00},
	{0x023F , 0x00},
	{0x0240 , 0x00},
	{0x0241 , 0x00},
	{0x0242 , 0x00},
	{0x0243 , 0x21},
	{0x0244 , 0x42},
	{0x0245 , 0x53},
	{0x0246 , 0x54},
	{0x0247 , 0x54},
	{0x0248 , 0x54},
	{0x0249 , 0x33},
	{0x024A , 0x11},
	{0x024B , 0x00},
	{0x024C , 0x00},
	{0x024D , 0xFF},
	{0x024E , 0xEE},
	{0x024F , 0xDD},
	{0x0250 , 0x00},
	{0x0251 , 0x00},
	{0x0252 , 0x00},
	{0x0253 , 0x00},
	{0x0254 , 0x00},
	{0x0255 , 0x00},
	{0x0256 , 0x00},
	{0x0257 , 0x00},
	{0x0258 , 0x00},
	{0x0259 , 0x00},
	{0x025A , 0x00},
	{0x025B , 0x00},
	{0x025C , 0x00},
	{0x025D , 0x00},
	{0x025E , 0x00},
	{0x025F , 0x00},
	{0x0260 , 0x00},
	{0x0261 , 0x00},
	{0x0262 , 0x00},
	{0x0263 , 0x00},
	{0x0264 , 0x00},
	{0x0265 , 0x00},
	{0x0266 , 0x00},
	{0x0267 , 0x00},
	{0x0268 , 0x8F},
	{0x0269 , 0xA3},
	{0x026A , 0xB4},
	{0x026B , 0x90},
	{0x026C , 0x00},
	{0x026D , 0xD0},
	{0x026E , 0x60},
	{0x026F , 0xA0},
	{0x0270 , 0x40},
	{0x0300 , 0x81},
	{0x0301 , 0x80},
	{0x0302 , 0x22},
	{0x0303 , 0x06},
	{0x0304 , 0x03},
	{0x0305 , 0x83},
	{0x0306 , 0x00},
	{0x0307 , 0x22},
	{0x0308 , 0x00},
	{0x0309 , 0x55},
	{0x030A , 0x55},
	{0x030B , 0x55},
	{0x030C , 0x54},
	{0x030D , 0x1F},
	{0x030E , 0x13},
	{0x030F , 0x10},
	{0x0310 , 0x04},
	{0x0311 , 0xFF},
		{0x0312 , 0x98},
	{0x0313 , 0x28},
	{0x0314 , 0x66},
	{0x0315 , 0x16},
	{0x0316 , 0x26},
	{0x0317 , 0x02},
	{0x0318 , 0x08},
	{0x0319 , 0x0C},
	{0x031A , 0x81},
	{0x031B , 0x00},
	{0x031C , 0x3D},
	{0x031D , 0x00},
	{0x031E , 0xF9},
	{0x031F , 0x00},
	{0x0320 , 0x24},
	{0x0321 , 0x14},
	{0x0322 , 0x1A},
	{0x0323 , 0x24},
	{0x0324 , 0x08},
	{0x0325 , 0xF0},
	{0x0326 , 0x30}, 
	{0x0327 , 0x17},
	{0x0328 , 0x11},
	{0x0329 , 0x22},
	{0x032A , 0x2F},
	{0x032B , 0x21},
	{0x032C , 0xDA},
	{0x032D , 0x10},
	{0x032E , 0xEA},
	{0x032F , 0x18},
	{0x0330 , 0x29},
	{0x0331 , 0x25},
	{0x0332 , 0x12},
	{0x0333 , 0x0F},
	{0x0334 , 0xE0},
	{0x0335 , 0x13},
	{0x0336 , 0xFF},
	{0x0337 , 0x20},
	{0x0338 , 0x46},
	{0x0339 , 0x04},
	{0x033A , 0x04},
	{0x033B , 0xFF},
	{0x033C , 0x01},
	{0x033D , 0x00},
	{0x033E , 0x03},
	{0x033F , 0x28},
	{0x0340 , 0x02},
	{0x0341 , 0x60},
	{0x0342 , 0xAC},
	{0x0343 , 0x97},
	{0x0344 , 0x7F},
	{0x0400 , 0xE8},
	{0x0401 , 0x40},
	{0x0402 , 0x00},
	{0x0403 , 0x00},
	{0x0404 , 0xF8},
	{0x0405 , 0x03},
	{0x0406 , 0x03},
	{0x0407 , 0x85},
	{0x0408 , 0x44},
	{0x0409 , 0x1F},
	{0x040A , 0x40},
	{0x040B , 0x33},
	{0x040C , 0xA0},
	{0x040D , 0x00},
	{0x040E , 0x00},
	{0x040F , 0x00},
	{0x0410 , 0x0D},
	{0x0411 , 0x0D},
	{0x0412 , 0x0C},
	{0x0413 , 0x04},
	{0x0414 , 0x00},
	{0x0415 , 0x00},
	{0x0416 , 0x07},
	{0x0417 , 0x09},
	{0x0418 , 0x16},
	{0x0419 , 0x14},
	{0x041A , 0x11},
	{0x041B , 0x14},
	{0x041C , 0x07},
	{0x041D , 0x07},
	{0x041E , 0x06},
	{0x041F , 0x02},
	{0x0420 , 0x42},
	{0x0421 , 0x42},
	{0x0422 , 0x47},
	{0x0423 , 0x39},
	{0x0424 , 0x3E},
	{0x0425 , 0x4D},
	{0x0426 , 0x46},
	{0x0427 , 0x3A},
	{0x0428 , 0x21},
	{0x0429 , 0x21},
	{0x042A , 0x26},
	{0x042B , 0x1C},
	{0x042C , 0x25},
	{0x042D , 0x25},
	{0x042E , 0x28},
	{0x042F , 0x20},
	{0x0430 , 0x3E},
	{0x0431 , 0x3E},
	{0x0432 , 0x33},
	{0x0433 , 0x2E},
	{0x0434 , 0x54},
	{0x0435 , 0x53},
	{0x0436 , 0x3C},
	{0x0437 , 0x51},
	{0x0438 , 0x2B},
	{0x0439 , 0x2B},
	{0x043A , 0x38},
	{0x043B , 0x22},
	{0x043C , 0x3B},
	{0x043D , 0x3B},
	{0x043E , 0x31},
	{0x043F , 0x37},
	{0x0440 , 0x00},
	{0x0441 , 0x4B},
	{0x0442 , 0x00},
	{0x0443 , 0x00},
	{0x0444 , 0x31},
	{0x0445 , 0x00},
	{0x0446 , 0x00},
	{0x0447 , 0x00},
	{0x0448 , 0x00},
	{0x0449 , 0x00},
	{0x044A , 0x00},
	{0x044D , 0xE0},
	{0x044E , 0x05},
	{0x044F , 0x07},
	{0x0450 , 0x00},
	{0x0451 , 0x00},
	{0x0452 , 0x00},
	{0x0453 , 0x00},
	{0x0454 , 0x00},
	{0x0455 , 0x00},
	{0x0456 , 0x00},
	{0x0457 , 0x00},
	{0x0458 , 0x00},
	{0x0459 , 0x00},
	{0x045A , 0x00},
	{0x045B , 0x00},
	{0x045C , 0x00},
	{0x045D , 0x00},
	{0x045E , 0x00},
	{0x045F , 0x00},
	{0x0460 , 0x80},
	{0x0461 , 0x10},
	{0x0462 , 0x10},
	{0x0463 , 0x10},
	{0x0464 , 0x08},
	{0x0465 , 0x08},
	{0x0466 , 0x11},
	{0x0467 , 0x09},
	{0x0468 , 0x23},
	{0x0469 , 0x2A},
	{0x046A , 0x2A},
	{0x046B , 0x47},
	{0x046C , 0x52},
	{0x046D , 0x42},
	{0x046E , 0x36},
	{0x046F , 0x46},
	{0x0470 , 0x3A},
	{0x0471 , 0x32},
	{0x0472 , 0x32},
	{0x0473 , 0x38},
	{0x0474 , 0x3D},
	{0x0475 , 0x2F},
	{0x0476 , 0x29},
	{0x0477 , 0x48},
	{0x0600 , 0x00},
	{0x0601 , 0x24},
	{0x0602 , 0x45},
	{0x0603 , 0x0E},
	{0x0604 , 0x14},
	{0x0605 , 0x2F},
	{0x0606 , 0x01},
	{0x0607 , 0x0E},
	{0x0608 , 0x0E},
	{0x0609 , 0x37},
	{0x060A , 0x18},
	{0x060B , 0xA0},
	{0x060C , 0x20},
	{0x060D , 0x07},
	{0x060E , 0x47},
	{0x060F , 0x90},
	{0x0610 , 0x06},
	{0x0611 , 0x0C},
	{0x0612 , 0x28},
	{0x0613 , 0x13},
	{0x0614 , 0x0B},
	{0x0615 , 0x10},
	{0x0616 , 0x14},
	{0x0617 , 0x19},
	{0x0618 , 0x52},
	{0x0619 , 0xA0},
	{0x061A , 0x11},
	{0x061B , 0x33},
	{0x061C , 0x56},
	{0x061D , 0x20},
	{0x061E , 0x28},
	{0x061F , 0x2B},
	{0x0620 , 0x22},
	{0x0621 , 0x11},
	{0x0622 , 0x75},
	{0x0623 , 0x49},
	{0x0624 , 0x6E},
	{0x0625 , 0x80},
	{0x0626 , 0x02},
	{0x0627 , 0x0C},
	{0x0628 , 0x51},
	{0x0629 , 0x25},
	{0x062A , 0x01},
	{0x062B , 0x3D},
	{0x062C , 0x04},
	{0x062D , 0x01},
	{0x062E , 0x0C},
	{0x062F , 0x2C},
	{0x0630 , 0x0D},
	{0x0631 , 0x14},
	{0x0632 , 0x12},
	{0x0633 , 0x34},
	{0x0634 , 0x00},
	{0x0635 , 0x00},
	{0x0636 , 0x00},
	{0x0637 , 0xB1},
	{0x0638 , 0x22},
	{0x0639 , 0x32},
	{0x063A , 0x0E},
	{0x063B , 0x18},
	{0x063C , 0x88},
	{0x0640 , 0xB2},
	{0x0641 , 0xC0},
	{0x0642 , 0x01},
	{0x0643 , 0x26},
	{0x0644 , 0x13},
	{0x0645 , 0x88},
	{0x0646 , 0x64},
	{0x0647 , 0x00},
	{0x0681 , 0x1B},
	{0x0682 , 0xA0},
	{0x0683 , 0x28},
	{0x0684 , 0x00},
	{0x0685 , 0xB0},
	{0x0686 , 0x6F},
	{0x0687 , 0x33},
	{0x0688 , 0x1F},
	{0x0689 , 0x44},
	{0x068A , 0xA8},
	{0x068B , 0x44},
	{0x068C , 0x08},
	{0x068D , 0x08},
	{0x068E , 0x00},
	{0x068F , 0x00},
	{0x0690 , 0x01},
	{0x0691 , 0x00},
	{0x0692 , 0x01},
	{0x0693 , 0x00},
	{0x0694 , 0x00},
	{0x0695 , 0x00},
	{0x0696 , 0x00},
	{0x0697 , 0x00},
	{0x0698 , 0x2A},
	{0x0699 , 0x80},
	{0x069A , 0x1F},
	{0x069B , 0x00},
	{0x069C , 0x02},
	{0x069D , 0xF5},
	{0x069E , 0x03},
	{0x069F , 0x6D},
	{0x06A0 , 0x0C},
	{0x06A1 , 0xB8},
	{0x06A2 , 0x0D},
	{0x06A3 , 0x74},
	{0x06A4 , 0x00},
	{0x06A5 , 0x2F},
	{0x06A6 , 0x00},
	{0x06A7 , 0x2F},
	{0x0F00 , 0x00},
	{0x0F01 , 0x00},
	{0x0100 , 0x01},
	{0x0102 , 0x02},
	{0x0104 , 0x03},
	
	
	///////////////////////////
	{0x020B , 0x48},
	{0x020C , 0x64},
	{0x040A , 0x40},
	{0x040B , 0x33},
	{0x0109 , 0x00},
	{0x010A , 0x04},
	{0x010B , 0x03},
	
	{0x0110, 0x03},
	{0x0111, 0x20},
	{0x0112, 0x02},
	{0x0113, 0x58},
	
	{0x0116 , 0x02},
	{0x0118 , 0x56},//56  0x40
	{0x0119 , 0x02},
	{0x011a , 0x04},
	{0x011B , 0x01},
	{0x0313 , 0x36},//36
	{0x0314 , 0xff},//ff
	{0x0315 , 0x16},
	/*
	   {0x020B , 0x48},
	   {0x020C , 0x64},
	   {0x040A , 0x40},
	   {0x040B , 0x33},
	   {0x0109 , 0x00},
	   {0x010A , 0x04},
	   {0x010B , 0x03},
	   {0x010c , 0x00},
	   {0x010d , 0xa8},
	   {0x010e , 0x00},
	   {0x010f , 0x60},
	   {0x010a , 0x04},
	
	   {0x0110 , 0x02},
	   {0x0111 , 0x80},
	   {0x0112 , 0x01},
	   {0x0113 , 0xe0},
	
	   {0x0116 , 0x02},
	   {0x0118 , 0x40},
	   {0x0119 , 0x01},
	   {0x011a , 0x04},
	   {0x011B , 0x00},
	   {0x0313 , 0x35},
	   {0x0314 , 0x36},
	   {0x0315 , 0x16}, */
	SensorEnd
};
/* Senor full resolution setting: recommand for capture */
static struct rk_sensor_reg sensor_fullres_lowfps_data[] ={
	//Binning&Resoultion
	{0x0109 , 0x01},//Fixed the number of lines by VCOUNT setting
	{0x010A , 0x00},
	{0x010B , 0x00},
	
	{0x0110 , 0x06},
	{0x0111 , 0x40},
	{0x0112 , 0x04},
	{0x0113 , 0xB0}, // 1600*1200 SETTING
	SensorEnd

};

/* Senor full resolution setting: recommand for video */
static struct rk_sensor_reg sensor_fullres_highfps_data[] ={
	SensorEnd
};
/* Preview resolution setting*/
static struct rk_sensor_reg sensor_preview_data[] =
{
	{0x0109 , 0x00},
	{0x010A , 0x04},
	{0x010B , 0x03},
	
	{0x0110 , 0x02},
	{0x0111 , 0x80},
	{0x0112 , 0x01},
	{0x0113 , 0xe0},
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
	SensorRegVal(0x0000,0),
	SensorRegVal(0x0001,0),
	SensorEnd
};
/*
*  The following setting must been filled, if the function is turn on by CONFIG_SENSOR_xxxx
*/
static struct rk_sensor_reg sensor_WhiteB_Auto[]=
{
	{0x031a , 0x81},
	{0x0320 , 0x24},
	{0x0321 , 0x14},
	{0x0322 , 0x1a},
	{0x0323 , 0x24},
	{0x0441 , 0x4B},
	{0x0442 , 0x00},
	{0x0443 , 0x00},
	{0x0444 , 0x31},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	   {0x0320 , 0x02},
	{0x0321 , 0x02},
	{0x0322 , 0x02},
	{0x0323 , 0x02},
	{0x0441 , 0x80},
	{0x0442 , 0x00},
	{0x0443 , 0x00},
	{0x0444 , 0x0D},		
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
			{0x0320 , 0x02},
			{0x0321 , 0x02},
			{0x0322 , 0x02},
			{0x0323 , 0x02},
			{0x0441 , 0x60},
			{0x0442 , 0x00},
			{0x0443 , 0x00},
			{0x0444 , 0x14},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
	{0x0320 , 0x02},
	{0x0321 , 0x02},
	{0x0322 , 0x02},
	{0x0323 , 0x02},
	{0x0441 , 0x50},
	{0x0442 , 0x00},
	{0x0443 , 0x00},
	{0x0444 , 0x30},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
	{0x0320 , 0x02},
	{0x0321 , 0x02},
	{0x0322 , 0x02},
	{0x0323 , 0x02},
	{0x0441 , 0x0B},
	{0x0442 , 0x00},
	{0x0443 , 0x00},
	{0x0444 , 0x5E},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	{0x0300 , 0x81},
	{0x0301 , 0x60},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1
	 {0x0300 , 0x81},
	{0x0301 , 0x70},
	
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0
	{0x0300 , 0x81},
	{0x0301 , 0x80},
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1
	{0x0300 , 0x81},
	{0x0301 , 0x90},
	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2
	{0x0300 , 0x81},
	{0x0301 , 0xa0},
	

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3
	{0x0300 , 0x81},
	{0x0301 , 0xb0},

	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
	{0x0115,0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	{0x0115,0x06},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0x0115,0x0a},
	{0x026e,0x60},
	{0x026f,0xa0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	{0x0115,0x09}, //bit[6] negative
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
	{0x0115,0x0a},
	{0x026e,0xfb},
	{0x026f,0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish
	{0x0115,0x0a},
	{0x026e,0x20},
	{0x026f,0x00},
	SensorEnd
};
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{
	//-3
	{0x0300 , 0x81},
	{0x0301 , 0x50},
	{0x0201 , 0xa0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	//-2
	{0x0300 , 0x81},
	{0x0301 , 0x60},
	{0x0201 , 0xb0},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	//-0.3EV
	{0x0300 , 0x81},
	{0x0301 , 0x70},
	{0x0201 , 0xd0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	//default
	{0x0300 , 0x81},
	{0x0301 , 0x80},
	{0x0201 , 0x10},//0c
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	// 1
	{0x0300 , 0x81},
	{0x0301 , 0x90},
	{0x0201 , 0x30},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{
	// 2
	{0x0300 , 0x81},
	{0x0301 , 0xa0},
	{0x0201 , 0x50},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
	// 3
	{0x0300 , 0x81},
	{0x0301 , 0xb0},
	{0x0201 , 0x60},
	SensorEnd
};

static struct rk_sensor_reg *sensor_ExposureSeqe[] = {sensor_Exposure0, sensor_Exposure1, sensor_Exposure2, sensor_Exposure3,
	sensor_Exposure4, sensor_Exposure5,sensor_Exposure6,NULL,
};

static	struct rk_sensor_reg sensor_Saturation0[]=
{
	{0x0202 , 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation1[]=
{
	{0x0202 , 0x50},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Saturation2[]=
{
	{0x0202 , 0x60},
	SensorEnd
};
static struct rk_sensor_reg *sensor_SaturationSeqe[] = {sensor_Saturation0, sensor_Saturation1, sensor_Saturation2, NULL,};

static	struct rk_sensor_reg sensor_Contrast0[]=
{
	//Contrast -3
	{0x0200 , 0xe8},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast1[]=
{
	 //Contrast -2
	{0x0200 , 0xf0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast2[]=
{
	 // Contrast -1
	{0x0200 , 0xf8},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast3[]=
{
	//Contrast 0
	{0x0200 , 0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast4[]=
{
	//Contrast +1
	{0x0200 , 0x10},
	SensorEnd
};


static	struct rk_sensor_reg sensor_Contrast5[]=
{
	//Contrast +2
	{0x0200 , 0x20},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Contrast6[]=
{
	//Contrast +3
	{0x0200 , 0x30},
	SensorEnd
};
static struct rk_sensor_reg *sensor_ContrastSeqe[] = {sensor_Contrast0, sensor_Contrast1, sensor_Contrast2, sensor_Contrast3,
	sensor_Contrast4, sensor_Contrast5, sensor_Contrast6, NULL,
};
static	struct rk_sensor_reg sensor_SceneAuto[] =
{
	{0x0312, 0x08},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	//30fps ~ 5fps night mode for 60/50Hz light environment, 24Mhz clock input,36Mzh pclk
	
	
	 {0x0312, 0x98},
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
	char val;
	int err = 0;
	
	SENSOR_DG("mirror: %d",mirror);
	if (mirror) {
		err = sensor_read(client, 0x0101, &val);
		if (err == 0) {
			if((val & 0x1) == 0){
				err = sensor_write(client, 0x0101, (val |0x1));
				}
			else 
				err = sensor_write(client, 0x0101, (val & 0xfe));
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
		err = sensor_read(client, 0x0101, &val);
		if (err == 0) {
			if((val & 0x2) == 0){
				err = sensor_write(client, 0x0101, (val |0x2));
				}
			else {
				err = sensor_write(client, 0x0101, (val & 0xfc));
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






