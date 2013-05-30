#ifndef __LCD_TD043MGEA__
#define __LCD_TD043MGEA__


/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT      	LVDS_8BIT_2
#define OUT_FACE		OUT_P888
#define DCLK			27000000
#define LCDC_ACLK       	150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			206
#define H_VD			800
#define H_FP			40

#define V_PW			10
#define V_BP			25
#define V_VD			480
#define V_FP			10

#define LCD_WIDTH       	800    //need modify
#define LCD_HEIGHT      	480

/* Other */
#define DCLK_POL		0
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_DUMMY		0
#define SWAP_GB			0
#define SWAP_RG			0



#endif

