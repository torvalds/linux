
#ifndef _LCD_WY__
#define _LCD_WY__

#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>

/* Base */
#define LCD_WIDTH       154    //need modify
#define LCD_HEIGHT      85

#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT		LVDS_8BIT_1
#define OUT_FACE		OUT_P666
#define DCLK			 33000000
#define LCDC_ACLK       150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			30
#define H_BP			16
#define H_VD			800
#define H_FP			210

#define V_PW			13
#define V_BP			10
#define V_VD			480
#define V_FP			22

/* Other */
#define DCLK_POL                0
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0 

static struct rk29lcd_info *gLcd_info = NULL;
#define RK_SCREEN_INIT
static int rk_lcd_init(void)
{
	int ret = 0;
	
	if(gLcd_info && gLcd_info->io_init)
		gLcd_info->io_init();

	return 0;
}

static int rk_lcd_standby(u8 enable)
{
	if(!enable)
	{
		if(gLcd_info && gLcd_info->io_enable)
			gLcd_info->io_enable();
	}
	else 
	{
		if(gLcd_info && gLcd_info->io_disable)
			gLcd_info->io_disable();
	}
	return 0;
}

#endif
