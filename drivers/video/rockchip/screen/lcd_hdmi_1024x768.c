
#ifndef __LCD_HDMI_1024x768__
#define __LCD_HDMI_1024x768__

#ifdef CONFIG_RK610_LVDS
#include "../transmitter/rk610_lcd.h"
#endif


/* Base */
#define SCREEN_TYPE		SCREEN_LVDS
#define LVDS_FORMAT      	LVDS_8BIT_2
#define OUT_FACE		OUT_D888_P666  
#define DCLK			65000000
#define LCDC_ACLK        	500000000//312000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			100
#define H_VD			1024
#define H_FP			210

#define V_PW			10
#define V_BP			10
#define V_VD			768
#define V_FP			18

#define LCD_WIDTH       	202
#define LCD_HEIGHT      	152
#define DCLK_POL		1

#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


#if  defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)&& defined(CONFIG_RK610_LVDS)

/* scaler Timing    */
//1920*1080*60
#define S_OUT_CLK		SCALE_RATE(148500000,66000000) //m=16 n=9 no=4
#define S_H_PW			100
#define S_H_BP			100
#define S_H_VD			1024
#define S_H_FP			151

#define S_V_PW			5
#define S_V_BP			15
#define S_V_VD			768
#define S_V_FP			12

#define S_H_ST			1757
#define S_V_ST			14

//1920*1080*50
#define S1_OUT_CLK		SCALE_RATE(148500000,54000000)  //m=16 n=11 no=4 
#define S1_H_PW			100
#define S1_H_BP			100
#define S1_H_VD			1024
#define S1_H_FP			126

#define S1_V_PW			5
#define S1_V_BP			15
#define S1_V_VD			768
#define S1_V_FP			12

#define S1_H_ST			1757
#define S1_V_ST			14

//1280*720*60
#define S2_OUT_CLK		SCALE_RATE(74250000,66000000)  //m=32 n=9 no=4
#define S2_H_PW			100
#define S2_H_BP			100
#define S2_H_VD			1024
#define S2_H_FP			151

#define S2_V_PW			5
#define S2_V_BP			15
#define S2_V_VD			768
#define S2_V_FP			12

#define S2_H_ST			0
#define S2_V_ST			12
//1280*720*50

#define S3_OUT_CLK		SCALE_RATE(74250000,54000000)   // m=32 n=11 no=4 
#define S3_H_PW			100
#define S3_H_BP			100
#define S3_H_VD			1024
#define S3_H_FP			151

#define S3_V_PW			5
#define S3_V_BP			15
#define S3_V_VD			768
#define S3_V_FP			12

#define S3_H_ST			0
#define S3_V_ST			12

//720*576*50
#define S4_OUT_CLK		SCALE_RATE(27000000,54375000)  //m=145 n=9 no=8 
#define S4_H_PW			100
#define S4_H_BP			100
#define S4_H_VD			1024
#define S4_H_FP			81

#define S4_V_PW			5
#define S4_V_BP			15
#define S4_V_VD			768
#define S4_V_FP			45


#define S4_H_ST			435
#define S4_V_ST			45
//720*480*60
#define S5_OUT_CLK		SCALE_RATE(27000000,72000000)  //m=32 n=3 no=4 
#define S5_H_PW			100
#define S5_H_BP			100
#define S5_H_VD			1024
#define S5_H_FP			81

#define S5_V_PW			5
#define S5_V_BP			15
#define S5_V_VD			768
#define S5_V_FP			51

#define S5_H_ST			858
#define S5_V_ST			45

#define S_DCLK_POL       	0

#endif

#endif
