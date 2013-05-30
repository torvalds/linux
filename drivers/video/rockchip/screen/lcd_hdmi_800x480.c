
#ifndef __LCD_HDMI_800x480__
#define __LCD_HDMI_800x480__

#ifdef CONFIG_RK610_LVDS
#include "../transmitter/rk610_lcd.h"
#endif


/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT      	LVDS_8BIT_1
#define OUT_FACE		OUT_P888  
#define DCLK			33000000
#define LCDC_ACLK        	150000000//312000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			1
#define H_BP			88
#define H_VD			800
#define H_FP			40

#define V_PW			3
#define V_BP			29
#define V_VD			480
#define V_FP			13

#define LCD_WIDTH       	154
#define LCD_HEIGHT      	85

/* Other */
#define DCLK_POL		0
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


#if  defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)&& defined(CONFIG_RK610_LVDS) 

/* scaler Timing    */
//1920*1080*60

#define S_OUT_CLK		SCALE_RATE(148500000,33000000)
#define S_H_PW			1
#define S_H_BP			88
#define S_H_VD			800
#define S_H_FP			211

#define S_V_PW			3
#define S_V_BP			10
#define S_V_VD			480
#define S_V_FP			7

#define S_H_ST			244
#define S_V_ST			11

//1920*1080*50
#define S1_OUT_CLK		SCALE_RATE(148500000,30375000)
#define S1_H_PW			1
#define S1_H_BP			88
#define S1_H_VD			800
#define S1_H_FP			326

#define S1_V_PW			3
#define S1_V_BP			9
#define S1_V_VD			480
#define S1_V_FP			8

#define S1_H_ST			270
#define S1_V_ST			13
//1280*720*60
#define S2_OUT_CLK		SCALE_RATE(74250000,33000000)
#define S2_H_PW			1
#define S2_H_BP			88
#define S2_H_VD			800
#define S2_H_FP			211

#define S2_V_PW			3
#define S2_V_BP			9
#define S2_V_VD			480
#define S2_V_FP			8

#define S2_H_ST			0
#define S2_V_ST			8
//1280*720*50

#define S3_OUT_CLK		SCALE_RATE(74250000,30375000)
#define S3_H_PW			1
#define S3_H_BP			88
#define S3_H_VD			800
#define S3_H_FP			326

#define S3_V_PW			3
#define S3_V_BP			9
#define S3_V_VD			480
#define S3_V_FP			8

#define S3_H_ST			0
#define S3_V_ST			8

//720*576*50
#define S4_OUT_CLK		SCALE_RATE(27000000,30000000)
#define S4_H_PW			1
#define S4_H_BP			88
#define S4_H_VD			800
#define S4_H_FP			263

#define S4_V_PW			3
#define S4_V_BP			9
#define S4_V_VD			480
#define S4_V_FP			28

#define S4_H_ST			0
#define S4_V_ST			33
//720*480*60
#define S5_OUT_CLK		SCALE_RATE(27000000,31500000)
#define S5_H_PW			1
#define S5_H_BP			88
#define S5_H_VD			800
#define S5_H_FP			112

#define S5_V_PW			3
#define S5_V_BP			9
#define S5_V_VD			480
#define S5_V_FP			28

#define S5_H_ST			0
#define S5_V_ST			29

#define S_DCLK_POL       0

#endif

#endif
