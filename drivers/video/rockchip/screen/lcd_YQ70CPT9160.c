/* This Lcd Driver is HSD070IDW1 write by cst 2009.10.27 */


#ifndef __LCD_YQ70CPT__
#define __LCD_YQ70CPT__
/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT       	LVDS_8BIT_1
#define OUT_FACE		OUT_P666
#define DCLK			33000000
#define LCDC_ACLK       	150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			30//48 //10
#define H_BP			10//40 //100
#define H_VD			800 //1024
#define H_FP			210// //210

#define V_PW			13//10
#define V_BP			10// //10
#define V_VD			480 //768
#define V_FP			22 //18

/* Other */
#define DCLK_POL                1
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


#define LCD_WIDTH       154    //need modify
#define LCD_HEIGHT      85

#endif
