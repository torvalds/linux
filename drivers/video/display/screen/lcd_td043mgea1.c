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
#define OUT_CLK			 27000000
#define LCDC_ACLK       150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			10
#define H_BP			206
#define H_VD			800
#define H_FP			40

#define V_PW			10
#define V_BP			25
#define V_VD			480
#define V_FP			10

#define LCD_WIDTH       800    //need modify
#define LCD_HEIGHT      480

/* Other */
#define DCLK_POL		0
#define SWAP_RB			0

#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin

#if 0
#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)
#endif


static struct rk29lcd_info *gLcd_info = NULL;
int init(void);
int standby(u8 enable);

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
    screen->init = init;
    screen->standby = standby;
    if(lcd_info)
        gLcd_info = lcd_info;
}

#if 0
void spi_screenreg_set(u32 Addr, u32 Data)
{

#define DRVDelayUs(i)   udelay(i*2)

    u32 i;

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    DRVDelayUs(2);
    DRVDelayUs(2);

    CS_SET();
    TXD_SET();
    CLK_SET();
    DRVDelayUs(2);

	CS_CLR();
	for(i = 0; i < 6; i++)  //reg
	{
		if(Addr &(1<<(5-i)))
			TXD_SET();
		else
			TXD_CLR();

		// \u6a21\u62dfCLK
		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}

	TXD_CLR();  //write

	// \u6a21\u62dfCLK
    CLK_CLR();
    DRVDelayUs(2);
    CLK_SET();
    DRVDelayUs(2);

	TXD_SET();  //highz

	// \u6a21\u62dfCLK
    CLK_CLR();
    DRVDelayUs(2);
    CLK_SET();
    DRVDelayUs(2);


	for(i = 0; i < 8; i++)  //data
	{
		if(Data &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		// \u6a21\u62dfCLK
        CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}

	CS_SET();
	CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2);

}
#endif


int init(void)
{
    if(gLcd_info)
        gLcd_info->io_init();

#if 0
    spi_screenreg_set(0x02, 0x07);
    spi_screenreg_set(0x03, 0x5f);
    spi_screenreg_set(0x04, 0x17);
    spi_screenreg_set(0x05, 0x20);
    spi_screenreg_set(0x06, 0x08);
    spi_screenreg_set(0x07, 0x20);
    spi_screenreg_set(0x08, 0x20);
    spi_screenreg_set(0x09, 0x20);
    spi_screenreg_set(0x0a, 0x20);
    spi_screenreg_set(0x0b, 0x22);
    spi_screenreg_set(0x0c, 0x22);
    spi_screenreg_set(0x0d, 0x22);
    spi_screenreg_set(0x0e, 0x10);
    spi_screenreg_set(0x0f, 0x10);
    spi_screenreg_set(0x10, 0x10);

    spi_screenreg_set(0x11, 0x15);
    spi_screenreg_set(0x12, 0xAA);
    spi_screenreg_set(0x13, 0xFF);
    spi_screenreg_set(0x14, 0xb0);
    spi_screenreg_set(0x15, 0x8e);
    spi_screenreg_set(0x16, 0xd6);
    spi_screenreg_set(0x17, 0xfe);
    spi_screenreg_set(0x18, 0x28);
    spi_screenreg_set(0x19, 0x52);
    spi_screenreg_set(0x1A, 0x7c);

    spi_screenreg_set(0x1B, 0xe9);
    spi_screenreg_set(0x1C, 0x42);
    spi_screenreg_set(0x1D, 0x88);
    spi_screenreg_set(0x1E, 0xb8);
    spi_screenreg_set(0x1F, 0xFF);
    spi_screenreg_set(0x20, 0xF0);
    spi_screenreg_set(0x21, 0xF0);
    spi_screenreg_set(0x22, 0x09);
#endif

    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}

int standby(u8 enable)
{
    if(gLcd_info)
        gLcd_info->io_init();
#if 0
	if(enable) {
		spi_screenreg_set(0x03, 0xde);
	} else {
		spi_screenreg_set(0x03, 0x5f);
	}
#endif
    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}

