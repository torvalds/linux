#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include "screen.h"

/* Base */
#define OUT_TYPE		SCREEN_LVDS
#define OUT_FORMAT      1//LVDS_8BIT_2

#define OUT_FACE		OUT_D888_P666
#define OUT_CLK			60000000
#define LCDC_ACLK       300000000//500000000//312000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			100
#define H_BP			100
#define H_VD			1024
#define H_FP			120

#define V_PW			10
#define V_BP			10
#define V_VD			600
#define V_FP			150

#define LCD_WIDTH       202
#define LCD_HEIGHT      152
/* Other */
#define DCLK_POL		1
#define SWAP_RB			0   

void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
{
    /* screen type & face */
    screen->type = OUT_TYPE;
    screen->face = OUT_FACE;

    /* Screen size */
    screen->x_res = H_VD;
    screen->y_res = V_VD;

    screen->width = LCD_WIDTH;
    screen->height = LCD_HEIGHT;
    screen->hw_format = OUT_FORMAT;

    /* Timing */
    screen->lcdc_aclk = LCDC_ACLK;
    screen->pixclock = OUT_CLK;
	screen->left_margin = H_BP;
	screen->right_margin = H_FP;
	screen->hsync_len = H_PW;
	screen->upper_margin = V_BP;
	screen->lower_margin = V_FP;
	screen->vsync_len = V_PW;

	/* Pin polarity */
	screen->pin_hsync = 0;
	screen->pin_vsync = 0;
	screen->pin_den = 0;
	screen->pin_dclk = DCLK_POL;

	/* Swap rule */
    screen->swap_rb = SWAP_RB;
    screen->swap_rg = 0;
    screen->swap_gb = 0;
    screen->swap_delta = 0;
    screen->swap_dumy = 0;

    /* Operation function*/
    screen->init = NULL;
    screen->standby = NULL;
}



