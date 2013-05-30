
#ifndef __LCD_HSD100PXN__
#define __LCD_HSD100PXN__
/* Base */
#define SCREEN_TYPE		SCREEN_LVDS
#define LVDS_FORMAT      	LVDS_8BIT_2
#define OUT_FACE		OUT_D888_P666  
#define DCLK			65000000
#define LCDC_ACLK        	300000000//312000000           //29 lcdc axi DMA ÆµÂÊ

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
/* Other */
#define DCLK_POL		1
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


#ifdef  CONFIG_ONE_LCDC_DUAL_OUTPUT_INF
/* scaler Timing    */
//1920*1080*60

#define S_OUT_CLK		64512000
#define S_H_PW			114
#define S_H_BP			210
#define S_H_VD			1024
#define S_H_FP			0

#define S_V_PW			4
#define S_V_BP			10
#define S_V_VD			768
#define S_V_FP			0

#define S_H_ST			0
#define S_V_ST			23

//1920*1080*50
#define S1_OUT_CLK		53760000
#define S1_H_PW			114
#define S1_H_BP			210
#define S1_H_VD			1024
#define S1_H_FP			0

#define S1_V_PW			4
#define S1_V_BP			10
#define S1_V_VD			768
#define S1_V_FP			0

#define S1_H_ST			0
#define S1_V_ST			23
//1280*720*60
#define S2_OUT_CLK		64512000
#define S2_H_PW			114
#define S2_H_BP			210
#define S2_H_VD			1024
#define S2_H_FP			0

#define S2_V_PW			4
#define S2_V_BP			10
#define S2_V_VD			768
#define S2_V_FP			0

#define S2_H_ST			0
#define S2_V_ST			23
//1280*720*50

#define S3_OUT_CLK		53760000
#define S3_H_PW			114
#define S3_H_BP			210
#define S3_H_VD			1024
#define S3_H_FP			0

#define S3_V_PW			4
#define S3_V_BP			10
#define S3_V_VD			768
#define S3_V_FP			0

#define S3_H_ST			0
#define S3_V_ST			23

//720*576*50
#define S4_OUT_CLK		 30000000
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
#define S5_OUT_CLK		 30000000
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

#define S_DCLK_POL       	1

#endif

#endif
