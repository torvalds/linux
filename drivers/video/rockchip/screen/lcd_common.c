#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include "screen.h"

/* Base */
#define OUT_TYPE	    SCREEN_RGB

#define OUT_FACE	    OUT_D888_P666


#define OUT_CLK	          71000000
#define LCDC_ACLK        300000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			100
#define H_VD			1280
#define H_FP			18

#define V_PW			2
#define V_BP			8
#define V_VD			800
#define V_FP			6

#define LCD_WIDTH          216
#define LCD_HEIGHT         135
/* Other */
#define DCLK_POL		0
#define SWAP_RB		0


u32 lcdpamara[]={0x4B434F52,0x64636C5F,0x61746164,SCREEN_RGB,OUT_D888_P666,71000000,300000000,10,100,1280,18,2,8,800,6,216,135,0,0};

void set_lcd_info(struct rk29fb_screen *screen,  struct rk29lcd_info *lcd_info )
{
   /* screen type & face */
    screen->type = lcdpamara[3];
    screen->face = lcdpamara[4];

    /* Screen size */
    screen->x_res =  lcdpamara[9];
    screen->y_res =  lcdpamara[13];

    screen->width =  lcdpamara[15];
    screen->height = lcdpamara[16];

    /* Timing */
    screen->lcdc_aclk =  lcdpamara[6];
    screen->pixclock =  lcdpamara[5];
	screen->left_margin = lcdpamara[8];
	screen->right_margin =  lcdpamara[10];
	screen->hsync_len =  lcdpamara[7];
	screen->upper_margin =  lcdpamara[12];
	screen->lower_margin = lcdpamara[14];
	screen->vsync_len =  lcdpamara[11];

	/* Pin polarity */
	screen->pin_hsync = 0;
	screen->pin_vsync = 0;
	screen->pin_den = 0;
	screen->pin_dclk =  lcdpamara[17];

	/* Swap rule */
    screen->swap_rb =  lcdpamara[18];
    screen->swap_rg = 0;
    screen->swap_gb = 0;
    screen->swap_delta = 0;
    screen->swap_dumy = 0;

    /* Operation function*/
    screen->init = NULL;
    screen->standby = NULL;
 
}

