#ifndef _LCD_TX23D88VM_H_
#define _LCD_TX23D88VM_H_


/* Base */
#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT		LVDS_8BIT_1
#define OUT_FACE		OUT_D888_P666
#define DCLK			 66000000//64000000
#define LCDC_ACLK        500000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			20
#define H_VD			1200
#define H_FP			70

#define V_PW			2
#define V_BP			4
#define V_VD			800
#define V_FP			14

#define LCD_WIDTH       188
#define LCD_HEIGHT      125
/* Other */
#define DCLK_POL		0
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0


#endif
