#ifndef __LCD_E242868__
#define __LCD_E242868__
/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT       	LVDS_8BIT_1
#define OUT_FACE		OUT_P888
#define DCLK			50000000
#define LCDC_ACLK       	500000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			30
#define H_BP			10
#define H_VD			1024 
#define H_FP			210

#define V_PW			13
#define V_BP			10 
#define V_VD			600
#define V_FP			22 

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

#endif
