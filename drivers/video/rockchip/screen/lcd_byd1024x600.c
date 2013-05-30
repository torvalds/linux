/*
 * This Lcd Driver is for BYD 5' LCD BM800480-8545FTGE.
 * written by Michael Lin, 2010-06-18
 */

#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include "screen.h"


/* Base */
#define OUT_TYPE		SCREEN_RGB
#define OUT_FACE		OUT_P888
//tcl miaozh modify
//#define OUT_CLK			50000000
#define OUT_CLK			47000000
//#define OUT_CLK			42000000
#define LCDC_ACLK       150000000     //29 lcdc axi DMA ÆµÂÊ

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
#define DCLK_POL		0
#define SWAP_RB			0

//tcl miaozh modify
//#define LCD_WIDTH       1024    //need modify
//#define LCD_HEIGHT      600
#define LCD_WIDTH       153    //need modify
#define LCD_HEIGHT      90

static struct rk29lcd_info *gLcd_info = NULL;

#define DRVDelayUs(i)   udelay(i*2)

static int init(void);
static int standby(u8 enable);

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
    /*screen->init = init;*/
    screen->init = NULL; 
    screen->standby = standby;
}


static int standby(u8 enable)
{
	printk(KERN_INFO "byd1024x600 lcd standby, enable=%d\n", enable);
	if (enable)
	{
		//rockchip_mux_api_set(LED_CON_IOMUX_PINNAME, LED_CON_IOMUX_PINDIR);
		//GPIOSetPinDirection(LED_CON_IOPIN,GPIO_OUT);
		//GPIOSetPinLevel(LED_CON_IOPIN,GPIO_HIGH);
//		gpio_set_value(LCD_DISP_ON_IOPIN, GPIO_LOW);
	}
	else
	{
		//rockchip_mux_api_set(LED_CON_IOMUX_PINNAME, 1);
//		gpio_set_value(LCD_DISP_ON_IOPIN, GPIO_HIGH);
	}
	return 0;
}

