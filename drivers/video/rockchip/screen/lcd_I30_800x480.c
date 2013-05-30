#ifndef __LCD_I30__
#define __LCD_I30__
/* Base */
#define LCD_WIDTH       154    //need modify
#define LCD_HEIGHT      85

#define SCREEN_TYPE		SCREEN_RGB
#define LVDS_FORMAT       	LVDS_8BIT_1
#define OUT_FACE		OUT_P666
#define DCLK			30000000
#define LCDC_ACLK       	150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			48 //10
#define H_BP			88 //100
#define H_VD			800
#define H_FP			40 //210

#define V_PW			3 //10
#define V_BP			32 //10
#define V_VD			480
#define V_FP			13 //18

/* Other */
#define DCLK_POL                1
#define DEN_POL			0
#define VSYNC_POL		0
#define HSYNC_POL		0

#define SWAP_RB			0
#define SWAP_RG			0
#define SWAP_GB			0

#define RK_SCREEN_INIT
static struct rk29lcd_info *gLcd_info = NULL;

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
