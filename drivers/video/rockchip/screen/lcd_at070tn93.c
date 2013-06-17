#ifndef _LCD_AT070NT_H_
#define _LCD_AT070NT_H_

/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT		LVDS_8BIT_1
#define OUT_FACE		OUT_P888  
#define DCLK			46800000//33300000//50000000
#define LCDC_ACLK       	500000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			36
#define H_VD			800
#define H_FP			210

#define V_PW			10
#define V_BP			13
#define V_VD			480
#define V_FP			22

#define LCD_WIDTH       	154
#define LCD_HEIGHT      	86
/* Other */
#define DCLK_POL               0
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0

#endif
