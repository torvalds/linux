/*
 * drivers/media/video/gc0307.c
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
#define SENSOR_NAME RK29_CAM_SENSOR_GC0307
#define SENSOR_V4L2_IDENT V4L2_IDENT_GC0307
#define SENSOR_ID 0x99
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
	//========= close output
		{0x43  ,0x00}, 
		{0x44  ,0xa2}, 
		
		//========= close some functions
		// open them after configure their parmameters
		{0x40  ,0x10}, 
		{0x41  ,0x00},			
		{0x42  ,0x10},						
		{0x47  ,0x00}, //mode1, 				
		{0x48  ,0xc3}, //mode2, 	
		{0x49  ,0x00}, //dither_mode		
		{0x4a  ,0x00}, //clock_gating_en
		{0x4b  ,0x00}, //mode_reg3
		{0x4E  ,0x22},//0x23}, //sync mode yaowei
		{0x4F  ,0x01}, //AWB, AEC, every N frame	
		
		//========= frame timing
		{0x01  ,0x6a}, //HB
		{0x02  ,0x70}, //VB
		{0x1C  ,0x00}, //Vs_st
		{0x1D  ,0x00}, //Vs_et
		{0x10  ,0x00}, //high 4 bits of VB, HB
		{0x11  ,0x05}, //row_tail,	AD_pipe_number
	
	
					
		
		
		//========= windowing
		{0x05  ,0x00}, //row_start
		{0x06  ,0x00},
		{0x07  ,0x00}, //col start
		{0x08  ,0x00}, 
		{0x09  ,0x01}, //win height
		{0x0A  ,0xE8},
		{0x0B  ,0x02}, //win width, pixel array only 640
		{0x0C  ,0x80},
		
		//========= analog
		{0x0D  ,0x22}, //rsh_width
						  
		{0x0E  ,0x02}, //CISCTL mode2,	
	
				  
		{0x12  ,0x70}, //7 hrst, 6_4 darsg,
		{0x13  ,0x00}, //7 CISCTL_restart, 0 apwd
		{0x14  ,0x00}, //NA
		{0x15  ,0xba}, //7_4 vref
		{0x16  ,0x13}, //5to4 _coln_r,	__1to0__da18
		{0x17  ,0x52}, //opa_r, ref_r, sRef_r
		//{0x18  ,0xc0}, //analog_mode, best case for left band.
		
		{0x1E  ,0x0d}, //tsp_width		   
		{0x1F  ,0x32}, //sh_delay
		
		//========= offset
		{0x47  ,0x00},	//7__test_image, __6__fixed_pga, __5__auto_DN, __4__CbCr_fix, 
					//__3to2__dark_sequence, __1__allow_pclk_vcync, __0__LSC_test_image
		{0x19  ,0x06},	//pga_o 		 
		{0x1a  ,0x06},	//pga_e 		 
		
		{0x31  ,0x00},	//4 //pga_oFFset ,	 high 8bits of 11bits
		{0x3B  ,0x00},	//global_oFFset, low 8bits of 11bits
		
		{0x59  ,0x0f},	//offset_mode	
		{0x58  ,0x88},	//DARK_VALUE_RATIO_G,  DARK_VALUE_RATIO_RB
		{0x57  ,0x08},	//DARK_CURRENT_RATE
		{0x56  ,0x77},	//PGA_OFFSET_EVEN_RATIO, PGA_OFFSET_ODD_RATIO
		
		//========= blk
		{0x35  ,0xd8},	//blk_mode
	
		{0x36  ,0x40},	
		
		{0x3C  ,0x00}, 
		{0x3D  ,0x00}, 
		{0x3E  ,0x00}, 
		{0x3F  ,0x00}, 
		
		{0xb5  ,0x70}, 
		{0xb6  ,0x40}, 
		{0xb7  ,0x00}, 
		{0xb8  ,0x38}, 
		{0xb9  ,0xc3},		  
		{0xba  ,0x0f}, 
		
		{0x7e  ,0x50},//0x45 ylz++ 
		{0x7f  ,0x76},	 //0x66
		
		{0x5c  ,0x48}, //78
		{0x5d  ,0x58}, //88
		
		
		//========= manual_gain 
		{0x61  ,0x80}, //manual_gain_g1 
		{0x63  ,0x80}, //manual_gain_r
		{0x65  ,0x98}, //manual_gai_b, 0xa0=1.25, 0x98=1.1875
		{0x67  ,0x80}, //manual_gain_g2
		{0x68  ,0x18}, //global_manual_gain  2.4bits
		
		//=========CC _R
		{0x69  ,0x58},	//54
		{0x6A  ,0xf6},	//ff
		{0x6B  ,0xfb},	//fe
		{0x6C  ,0xf4},	//ff
		{0x6D  ,0x5a},	//5f
		{0x6E  ,0xe6},	//e1
	
		{0x6f  ,0x00},	
		
		//=========lsc							  
		{0x70  ,0x14}, 
		{0x71  ,0x1c}, 
		{0x72  ,0x20}, 
		
		{0x73  ,0x10},	
		{0x74  ,0x3c}, 
		{0x75  ,0x52}, 
		
		//=========dn																			 
		{0x7d  ,0x2f},	//dn_mode		
		{0x80  ,0x0c}, //when auto_dn, check 7e,7f
		{0x81  ,0x0c},
		{0x82  ,0x44},
																							
		//dd																		   
		{0x83  ,0x18},	//DD_TH1					  
		{0x84  ,0x18},	//DD_TH2					  
		{0x85  ,0x04},	//DD_TH3																							  
		{0x87  ,0x34},	//32 b DNDD_low_range X16,	DNDD_low_range_C_weight_center					
		
		   
		//=========intp-ee																		   
		{0x88  ,0x04},														   
		{0x89  ,0x01},											  
		{0x8a  ,0x50},//60											   
		{0x8b  ,0x50},//60											   
		{0x8c  ,0x07},																	  
																						  
		{0x50  ,0x0c},									
		{0x5f  ,0x3c},																					 
																						 
		{0x8e  ,0x02},																  
		{0x86  ,0x02},																	  
																						
		{0x51  ,0x20},																	
		{0x52  ,0x08},	
		{0x53  ,0x00}, 
		
		
		//========= YCP 
		//contrast_center																			  
		{0x77  ,0x80}, //contrast_center																  
		{0x78  ,0x00}, //fixed_Cb																		  
		{0x79  ,0x00}, //fixed_Cr																		  
		{0x7a  ,0x00}, //luma_offset																																							
		{0x7b  ,0x40}, //hue_cos																		  
		{0x7c  ,0x00}, //hue_sin																		  
																								 
		//saturation																				  
		{0xa0  ,0x40}, //global_saturation
		{0xa1  ,0x42}, //luma_contrast																	  
		{0xa2  ,0x40}, //saturation_Cb		//ylz  34															  
		{0xa3  ,0x34}, //saturation_Cr
																					
		{0xa4  ,0xc8},																  
		{0xa5  ,0x02}, 
		{0xa6  ,0x28},																			  
		{0xa7  ,0x02}, 
		
		//skin																								  
		{0xa8  ,0xee},															  
		{0xa9  ,0x12},															  
		{0xaa  ,0x01},														  
		{0xab  ,0x20},													  
		{0xac  ,0xf0},														  
		{0xad  ,0x10},															  
			
		//========= ABS
		{0xae  ,0x18}, 
		{0xaf  ,0x74}, 
		{0xb0  ,0xe0},	  
		{0xb1  ,0x20}, 
		{0xb2  ,0x6c}, 
		{0xb3  ,0x40}, 
		{0xb4  ,0x04}, 
			
		//========= AWB 
		{0xbb  ,0x42}, 
		{0xbc  ,0x60},
		{0xbd  ,0x50},
		{0xbe  ,0x50},
		
		{0xbf  ,0x0c}, 
		{0xc0  ,0x06}, 
		{0xc1  ,0x60}, 
		{0xc2  ,0xf1},	//f1
		{0xc3  ,0x40},
		{0xc4  ,0x1c}, //18//20
		{0xc5  ,0x56},	//33
		{0xc6  ,0x1d}, 
	
		{0xca  ,0x70}, 
		{0xcb  ,0x70}, 
		{0xcc  ,0x78},
		
		{0xcd  ,0x80}, //R_ratio									 
		{0xce  ,0x80}, //G_ratio  , cold_white white								   
		{0xcf  ,0x80}, //B_ratio	
		
		//=========  aecT  
		{0x20  ,0x06},//0x02 
		{0x21  ,0xc0}, 
		{0x22  ,0x40},	  
		{0x23  ,0x88}, 
		{0x24  ,0x96}, 
		{0x25  ,0x30}, 
		{0x26  ,0xd0}, 
		{0x27  ,0x00}, 
		
		{0x28  ,0x02}, //AEC_exp_level_1bit11to8   
		{0x29  ,0x58}, //AEC_exp_level_1bit7to0   
		{0x2a  ,0x03}, //AEC_exp_level_2bit11to8   
		{0x2b  ,0x84}, //AEC_exp_level_2bit7to0 		 
		{0x2c  ,0x09}, //AEC_exp_level_3bit11to8   659 - 8FPS,	8ca - 6FPS	//	 
		{0x2d  ,0x60}, //AEC_exp_level_3bit7to0 		 
		{0x2e  ,0x0a}, //AEC_exp_level_4bit11to8   4FPS 
		{0x2f  ,0x8c}, //AEC_exp_level_4bit7to0  
		
		{0x30  ,0x20},						  
		{0x31  ,0x00},					   
		{0x32  ,0x1c}, 
		{0x33  ,0x90},			  
		{0x34  ,0x10},	
		
		{0xd0  ,0x34}, 
				   
		{0xd1  ,0x40}, //AEC_target_Y						   
		{0xd2  ,0x61},//0xf2	  
		{0xd4  ,0x96}, 
		{0xd5  ,0x01}, // william 0318
		{0xd6  ,0x96}, //antiflicker_step					   
		{0xd7  ,0x03}, //AEC_exp_time_min ,william 20090312 		   
		{0xd8  ,0x02}, 
				   
		{0xdd  ,0x12},//0x12 
																	
		//========= measure window										
		{0xe0  ,0x03},						 
		{0xe1  ,0x02},							 
		{0xe2  ,0x27},								 
		{0xe3  ,0x1e},				 
		{0xe8  ,0x3b},					 
		{0xe9  ,0x6e},						 
		{0xea  ,0x2c},					 
		{0xeb  ,0x50},					 
		{0xec  ,0x73},		 
		
		//========= close_frame 												
		{0xed  ,0x00}, //close_frame_num1 ,can be use to reduce FPS 			 
		{0xee  ,0x00}, //close_frame_num2  
		{0xef  ,0x00}, //close_frame_num
		
		// page1
		{0xf0  ,0x01}, //select page1 
		
		{0x00  ,0x20},							  
		{0x01  ,0x20},							  
		{0x02  ,0x20},									
		{0x03  ,0x20},							
		{0x04  ,0x78}, 
		{0x05  ,0x78},					 
		{0x06  ,0x78},								  
		{0x07  ,0x78},									 
		
		
		
		{0x10  ,0x04},						  
		{0x11  ,0x04},							  
		{0x12  ,0x04},						  
		{0x13  ,0x04},							  
		{0x14  ,0x01},							  
		{0x15  ,0x01},							  
		{0x16  ,0x01},						 
		{0x17  ,0x01},						 
			  
														 
		{0x20  ,0x00},					  
		{0x21  ,0x00},					  
		{0x22  ,0x00},						  
		{0x23  ,0x00},						  
		{0x24  ,0x00},					  
		{0x25  ,0x00},						  
		{0x26  ,0x00},					  
		{0x27  ,0x00},							  
		
		{0x40  ,0x11}, 
		
		//=============================lscP 
		{0x45  ,0x06},	 
		{0x46  ,0x06},			 
		{0x47  ,0x05}, 
		
		{0x48  ,0x04},	
		{0x49  ,0x03},		 
		{0x4a  ,0x03}, 
		
	
		{0x62  ,0xd8}, 
		{0x63  ,0x24}, 
		{0x64  ,0x24},
		{0x65  ,0x24}, 
		{0x66  ,0xd8}, 
		{0x67  ,0x24},
		
		{0x5a  ,0x00}, 
		{0x5b  ,0x00}, 
		{0x5c  ,0x00}, 
		{0x5d  ,0x00}, 
		{0x5e  ,0x00}, 
		{0x5f  ,0x00}, 
		
		
		//============================= ccP 
		
		{0x69  ,0x03}, //cc_mode
			  
		//CC_G
		{0x70  ,0x5d}, 
		{0x71  ,0xed}, 
		{0x72  ,0xff}, 
		{0x73  ,0xe5}, 
		{0x74  ,0x5f}, 
		{0x75  ,0xe6}, 
		
		  //CC_B
		{0x76  ,0x41}, 
		{0x77  ,0xef}, 
		{0x78  ,0xff}, 
		{0x79  ,0xff}, 
		{0x7a  ,0x5f}, 
		{0x7b  ,0xfa},	 
		
		
		//============================= AGP
		
		{0x7e  ,0x00},	
		{0x7f  ,0x20},	//x040
		{0x80  ,0x48},	
		{0x81  ,0x06},	
		{0x82  ,0x08},	
		
		{0x83  ,0x23},	
		{0x84  ,0x38},	
		{0x85  ,0x4F},	
		{0x86  ,0x61},	
		{0x87  ,0x72},	
		{0x88  ,0x80},	
		{0x89  ,0x8D},	
		{0x8a  ,0xA2},	
		{0x8b  ,0xB2},	
		{0x8c  ,0xC0},	
		{0x8d  ,0xCA},	
		{0x8e  ,0xD3},	
		{0x8f  ,0xDB},	
		{0x90  ,0xE2},	
		{0x91  ,0xED},	
		{0x92  ,0xF6},	
		{0x93  ,0xFD},	
		
		//about gamma1 is hex r oct
		{0x94  ,0x04},	
		{0x95  ,0x0E},	
		{0x96  ,0x1B},	
		{0x97  ,0x28},	
		{0x98  ,0x35},	
		{0x99  ,0x41},	
		{0x9a  ,0x4E},	
		{0x9b  ,0x67},	
		{0x9c  ,0x7E},	
		{0x9d  ,0x94},	
		{0x9e  ,0xA7},	
		{0x9f  ,0xBA},	
		{0xa0  ,0xC8},	
		{0xa1  ,0xD4},	
		{0xa2  ,0xE7},	
		{0xa3  ,0xF4},	
		{0xa4  ,0xFA}, 
		
		//========= open functions	
		{0xf0  ,0x00}, //set back to page0	
		{0x40  ,0x7e}, 
		{0x41  ,0x2F},
	
	/////  请注意，调整GC0307的镜像和翻转，需要同时修改三个寄存器，如下:
	
		{0x0f, 0xb2},
		{0x45, 0x27},
		{0x47, 0x2c},	
	///banding setting	 
			{  0x01  ,0xfa}, // 24M  
			{  0x02  ,0x70}, 
			{  0x10  ,0x01},   
			{  0xd6  ,0x64}, 
			{  0x28  ,0x02}, 
			{  0x29  ,0x58}, 
			{  0x2a  ,0x02}, 
			{  0x2b  ,0x58}, 
			{  0x2c  ,0x02}, 
			{  0x2d  ,0x58}, 
			{  0x2e  ,0x06}, 
			{  0x2f  ,0x40}, 
		
		/************
		   {0x0f, 0x02},//82
		{0x45, 0x24},
		{0x47, 0x20},	
		**************/
	/////  四种不同的翻转和镜像设定，客户可直接复制!!!!!!
	
	
#if 0
	//	IMAGE_NORMAL:
		{0x0f, 0xb2},
		{0x45, 0x27},
		{0x47, 0x2c},			
	
	// IMAGE_H_MIRROR:
		{0x0f, 0xa2},
		{0x45, 0x26},
		{0x47, 0x28},	
		
	// IMAGE_V_MIRROR:			
		{0x0f, 0x92},
		{0x45, 0x25},
		{0x47, 0x24},			
	
	// IMAGE_HV_MIRROR:    // 180
		{0x0f, 0x82},
		{0x45, 0x24},
		{0x47, 0x20},		
#endif
	{0x43, 0x40},
		{0x44, 0xe2},	

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
#if 1
	{	0x05 ,	0x00},
	{	0x06 ,	0x00},
	{	0x07 ,	0x00},
	{	0x08 ,	0x00},//0x10  james 20100715
	{	0x09 ,	0x01},
	{	0x0a ,	0xe8},
	{	0x0b ,	0x02},
	{	0x0c ,	0x88},//0x80   james 20100715
	{	0x45 ,	0x24},		// bit[7:2]=001001
	{	0x48 ,	0x84},		// bit[7]=1
	{	0xe0 ,	0x03},
	{	0xe1 ,	0x02},
	{	0xe2 ,	0x27},
	{	0xe3 ,	0x1e},
	{	0xe8 ,	0x3b},
	{	0xe9 ,	0x6e},
	{	0xea ,	0x2c},
	{	0xeb ,	0x50},
	{	0xec ,	0x73},
#else
	{0x17, 0x13},
	{0x18, 0x01},
	{0x32, 0xbf},
	{0x19, 0x03},
	{0x1a, 0x7b},
	{0x03, 0x0a},
	
#endif
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
	{0xc7,0x4c}, //for AWB can adjust back
	{0xc8,0x40},
	{0xc9,0x4a},			
	{0x41,0x2f},
	SensorEnd
};
/* Cloudy Colour Temperature : 6500K - 8000K  */
static	struct rk_sensor_reg sensor_WhiteB_Cloudy[]=
{
	{0x41,0x2b},   // Enable AWB 
	{0xc7,0x5a}, //WB_manual_gain
	{0xc8,0x42},
	{0xc9,0x40},
	SensorEnd
};
/* ClearDay Colour Temperature : 5000K - 6500K	*/
static	struct rk_sensor_reg sensor_WhiteB_ClearDay[]=
{
	//Sunny
		{0x41,0x2b},   // Enable AWB 
		{0xc7,0x50},
		{0xc8,0x45},
		{0xc9,0x40},
	SensorEnd
};
/* Office Colour Temperature : 3500K - 5000K  */
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp1[]=
{
	//Office
		{0x41,0x2b},   // Enable AWB 
		{0xc7,0x48},
		{0xc8,0x40},
		{0xc9,0x5c},
	SensorEnd

};
/* Home Colour Temperature : 2500K - 3500K	*/
static	struct rk_sensor_reg sensor_WhiteB_TungstenLamp2[]=
{
	//Home
		{0x41,0x2b},   // Enable AWB 
		{0xc7,0x40},
		{0xc8,0x42},
		{0xc9,0x50},
	SensorEnd
};
static struct rk_sensor_reg *sensor_WhiteBalanceSeqe[] = {sensor_WhiteB_Auto, sensor_WhiteB_TungstenLamp1,sensor_WhiteB_TungstenLamp2,
	sensor_WhiteB_ClearDay, sensor_WhiteB_Cloudy,NULL,
};

static	struct rk_sensor_reg sensor_Brightness0[]=
{
	// Brightness -2
	{0x7a, 0xe0},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness1[]=
{
	// Brightness -1
	{0x7a, 0xf0},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness2[]=
{
	//	Brightness 0
	{0x7a, 0x00},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness3[]=
{
	// Brightness +1
	{0x7a, 0x10},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness4[]=
{
	//	Brightness +2
	{0x7a, 0x20},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Brightness5[]=
{
	//	Brightness +3
	{0x7a, 0x30},
	SensorEnd
};
static struct rk_sensor_reg *sensor_BrightnessSeqe[] = {sensor_Brightness0, sensor_Brightness1, sensor_Brightness2, sensor_Brightness3,
	sensor_Brightness4, sensor_Brightness5,NULL,
};

static	struct rk_sensor_reg sensor_Effect_Normal[] =
{
		  {0x41,0x2f},			//	1
	{0x40,0x7e},
	{0x42,0x10},
	{0x47,0x24},//20
	{0x48,0xc3},
	{0x8a,0x50},//60
	{0x8b,0x50},
	{0x8c,0x07},
	{0x50,0x0c},
	{0x77,0x80},
	{0xa1,0x40},
	{0x7a,0x00},
	{0x78,0x00},
	{0x79,0x00},
	{0x7b,0x40},
	{0x7c,0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_WandB[] =
{
	   {0x41,0x2f}, 	// danse	
	{0x40,0x7e},
	{0x42,0x10},
	{0x47,0x3c},
	{0x48,0xc3},
	{0x8a,0x60},
	{0x8b,0x60},
	{0x8c,0x07},
	{0x50,0x0c},
	{0x77,0x80},
	{0xa1,0x40},
	{0x7a,0x00},
	{0x78,0x00},
	{0x79,0x00},
	{0x7b,0x40},
	{0x7c,0x00},	
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Sepia[] =
{
	{0x41,0x2f},			
		{0x40,0x7e},
		{0x42,0x10},
		{0x47,0x3c},
		{0x48,0xc3},
		{0x8a,0x60},
		{0x8b,0x60},
		{0x8c,0x07},
		{0x50,0x0c},
		{0x77,0x80},
		{0xa1,0x40},
		{0x7a,0x00},
		{0x78,0xc0},
		{0x79,0x20},
		{0x7b,0x40},
		{0x7c,0x00},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Negative[] =
{
	//Negative
	  {0x41,0x6f},			// 4
			{0x40,0x7e},
			{0x42,0x10},
			{0x47,0x20},
			{0x48,0xc3},
			{0x8a,0x60},
			{0x8b,0x60},
			{0x8c,0x07},
			{0x50,0x0c},
			{0x77,0x80},
			{0xa1,0x40},
			{0x7a,0x00},
			{0x78,0x00},
			{0x79,0x00},
			{0x7b,0x40},
			{0x7c,0x00},
	SensorEnd
};
static	struct rk_sensor_reg sensor_Effect_Bluish[] =
{
	// Bluish
					 {0x41,0x2f},		// 5
			{0x40,0x7e},
			{0x42,0x10},
			{0x47,0x2c},
			{0x48,0xc3},
			{0x8a,0x60},
			{0x8b,0x60},
			{0x8c,0x07},
			{0x50,0x0c},
			{0x77,0x80},
			{0xa1,0x40},
			{0x7a,0x00},
			{0x78,0x70},
			{0x79,0x00},
			{0x7b,0x3f},
			{0x7c,0xf5},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Effect_Green[] =
{
	//	Greenish   6
		{0x41,0x2f},			
			{0x40,0x7e},
			{0x42,0x10},
			{0x47,0x3c},
			{0x48,0xc3},
			{0x8a,0x60},
			{0x8b,0x60},
			{0x8c,0x07},
			{0x50,0x0c},
			{0x77,0x80},
			{0xa1,0x40},
			{0x7a,0x00},
			{0x78,0xc0},
			{0x79,0xc0},
			{0x7b,0x40},
			{0x7c,0x00},
	SensorEnd
};
#if 0
static struct rk_sensor_reg sensor_Effect_Grayscale[]=
{
	{0x23,0x02},	
	{0x2d,0x0a},
	{0x20,0xff},
	{0xd2,0x90},
	{0x73,0x00},

	{0xb3,0x40},
	{0xb4,0x80},
	{0xba,0x00},
	{0xbb,0x00},
	{0x00,0x00},
	SensorEnd
};
#endif
static struct rk_sensor_reg *sensor_EffectSeqe[] = {sensor_Effect_Normal, sensor_Effect_WandB, sensor_Effect_Negative,sensor_Effect_Sepia,
	sensor_Effect_Bluish, sensor_Effect_Green,NULL,
};

static	struct rk_sensor_reg sensor_Exposure0[]=
{
	//-3
	{0xd1, 0x38},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure1[]=
{
	//-2
	{0xd1, 0x40},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure2[]=
{
	//-1
	{0xd1, 0x48},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure3[]=
{
	//default
	{0xd1, 0x50},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure4[]=
{
	// 1
	{0xd1, 0x58},
	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure5[]=
{
	// 2
	{0xd1, 0x60},

	SensorEnd
};

static	struct rk_sensor_reg sensor_Exposure6[]=
{
	// 3
	{0xd1, 0x68},
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
	{	0xdd  ,0x22},  //0x12
		{	0x41  ,0x2f}, 
		{	0x21  ,0xc0},
		{	0xd2  ,0x02},
	SensorEnd
};

static	struct rk_sensor_reg sensor_SceneNight[] =
{
	{	0xdd  ,0x32},
		{	0x41  ,0x0f},
		{	0xb0  ,0x10},
		{	0x21  ,0xf0},
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
	char val1,val2,val3;
	int err = 0;
	
	SENSOR_DG("mirror: %d",mirror);
	if (mirror) {
			sensor_write(client, 0xf0, 0);
			err = sensor_read(client,0x0f,&val1);
			err = sensor_read(client,0x45,&val2);
			err = sensor_read(client,0x47,&val3);
			if(err ==0){
				if((val1 == 0xb2) && (val2 == 0x27) && (val3 == 0x2c)){//normal
					err = sensor_write(client, 0x0f, 0xa2);
					err = sensor_write(client, 0x45, 0x26);
					err = sensor_write(client, 0x47, 0x28);
				}else if((val1 == 0xa2) && (val2 == 0x26) && (val3 == 0x28)){//h_mir
					err = sensor_write(client, 0x0f, 0xb2);
					err = sensor_write(client, 0x45, 0x27);
					err = sensor_write(client, 0x47, 0x2c);
				}else if((val1 == 0x92) && (val2 == 0x25) && (val3 == 0x24)){//v_flip
					err = sensor_write(client, 0x0f, 0x82);
					err = sensor_write(client, 0x45, 0x24);
					err = sensor_write(client, 0x47, 0x20);
				}else if((val1 == 0x82) && (val2 == 0x24) && (val3 == 0x20)){//h_v_mir
					err = sensor_write(client, 0x0f, 0x92);
					err = sensor_write(client, 0x45, 0x25);
					err = sensor_write(client, 0x47, 0x24);
				}

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
	char val1,val2,val3;
	int err = 0;	

	SENSOR_DG("flip: %d",flip);
	if (flip) {
			sensor_write(client, 0xf0, 0);
			err = sensor_read(client,0x0f,&val1);
			err = sensor_read(client,0x45,&val2);
			err = sensor_read(client,0x47,&val3);
			if(err ==0){
				if((val1 == 0xb2) && (val2 == 0x27) && (val3 == 0x2c)){//normal
					err = sensor_write(client, 0x0f, 0x92);
					err = sensor_write(client, 0x45, 0x25);
					err = sensor_write(client, 0x47, 0x24);
				}else if((val1 == 0xa2) && (val2 == 0x26) && (val3 == 0x28)){//h_mir
					err = sensor_write(client, 0x0f, 0x82);
					err = sensor_write(client, 0x45, 0x24);
					err = sensor_write(client, 0x47, 0x20);
				}else if((val1 == 0x92) && (val2 == 0x25) && (val3 == 0x24)){//v_flip
					err = sensor_write(client, 0x0f, 0xb2);
					err = sensor_write(client, 0x45, 0x27);
					err = sensor_write(client, 0x47, 0x2c);
				}else if((val1 == 0x82) && (val2 == 0x24) && (val3 == 0x20)){//h_v_mir
					err = sensor_write(client, 0x0f, 0xa2);
					err = sensor_write(client, 0x45, 0x26);
					err = sensor_write(client, 0x47, 0x28);
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





