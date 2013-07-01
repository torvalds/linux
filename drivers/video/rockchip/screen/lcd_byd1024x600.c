/*
 * This Lcd Driver is for BYD 5' LCD BM800480-8545FTGE.
 * written by Michael Lin, 2010-06-18
 */

#ifndef _LCD_BYD1024X600__
#define _LCD_BYD1024X600__

/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT		LVDS_8BIT_1
#define OUT_FACE		OUT_P888
#define DCLK			47000000
#define LCDC_ACLK       	150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			160
#define H_VD			1024
#define H_FP			119

#define V_PW			3
#define V_BP			23
#define V_VD			600
#define V_FP			9

/* Other */
#define DCLK_POL                0
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0 



#define LCD_WIDTH       	153    //need modify
#define LCD_HEIGHT      	90

#endif
