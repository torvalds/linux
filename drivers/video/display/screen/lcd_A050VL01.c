/* This Lcd Driver is HSD070IDW1 write by cst 2009.10.27 */
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
#define OUT_CLK			23500000
#define LCDC_ACLK       150000000     //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			2
#define H_BP			2
#define H_VD			480
#define H_FP			2

#define V_PW			2
#define V_BP			2
#define V_VD			800 
#define V_FP			2

/* Other */
#define DCLK_POL		1
#define SWAP_RB			0

#define LCD_WIDTH       480    //need modify
#define LCD_HEIGHT      800

#if 1
#define RXD_PORT        RK29_PIN2_PC7
#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin
#define RESET_PORT      RK29_PIN6_PC6

#define CS_OUT()        gpio_direction_output(CS_PORT, 1)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0) 
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 1)   
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)
#define RXD_IN()        gpio_direction_input(RXD_PORT)
#define RXD_GET()	    gpio_get_value(RXD_PORT)
#endif

static struct rk29lcd_info *gLcd_info = NULL;

#define DRVDelayUs(i)   udelay(i*4)

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
    /*screen->init = NULL;*/
    screen->standby = standby;
    if(lcd_info)
        gLcd_info = lcd_info;
}
//cannot need init,so set screen->init = null at rk29_fb.c file

#if 1
void spi_screenreg_command(u16 command)
{
    u8 i,buff;

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    DRVDelayUs(2);
    DRVDelayUs(2);

    CS_SET();
    TXD_SET();
    CLK_SET();
    DRVDelayUs(2);

    buff = 0x20;
    CS_CLR();
    for(i = 0; i < 8; i++)
    {
        if(buff & (1<<(7-i)))
            TXD_SET();
        else
            TXD_CLR();
        CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
    }
    
	for(i = 0; i < 8; i++)  //reg hight
	{
		if(command &(1<<(15-i)))
			TXD_SET();
		else
			TXD_CLR();
		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}
    CS_SET();
    CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2);

	buff = 0x00;
    CS_CLR();
    for(i = 0; i < 8; i++)
    {
        if(buff & (1<<(7-i)))
            TXD_SET();
        else
            TXD_CLR();
        CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
    }
    
	for(i = 8; i < 16; i++)  //reg low
	{
		if(command &(1<<(15-i)))
			TXD_SET();
		else
			TXD_CLR();
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
void spi_screenreg_command_data(u16 Addr, u16 Data)
{
    u8 i,buff;

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    DRVDelayUs(2);
    DRVDelayUs(2);

    CS_SET();
    TXD_SET();
    CLK_SET();
    DRVDelayUs(2);

    buff = 0x20;
    CS_CLR();
    for(i = 0; i < 8; i++)
    {
        if(buff & (1<<(7-i)))
            TXD_SET();
        else
            TXD_CLR();
        CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
    }
    
	for(i = 0; i < 8; i++)  //reg hight
	{
		if(Addr &(1<<(15-i)))
			TXD_SET();
		else
			TXD_CLR();
		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}
    CS_SET();
    CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2);

	buff = 0x00;
    CS_CLR();
    for(i = 0; i < 8; i++)
    {
        if(buff & (1<<(7-i)))
            TXD_SET();
        else
            TXD_CLR();
        CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
    }
    
	for(i = 8; i < 16; i++)  //reg low
	{
		if(Addr &(1<<(15-i)))
			TXD_SET();
		else
			TXD_CLR();
		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}
    CS_SET();
    CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2);

	buff = 0x40;
    CS_CLR();
    for(i = 0; i < 8; i++)
    {
        if(buff & (1<<(7-i)))
            TXD_SET();
        else
            TXD_CLR();
        CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
    }

	for(i = 0; i < 8; i++)  //data
	{
		if(Data &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();
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

int init(void)
{
    if(gLcd_info)
        gLcd_info->io_init();
        
#ifdef RESET_PORT
    gpio_request(RESET_PORT, NULL);
    gpio_direction_output(RESET_PORT, 0);
    mdelay(2);
    gpio_set_value(RESET_PORT, 1);
    mdelay(10);
    gpio_free(RESET_PORT);
#endif

    spi_screenreg_command(0x1100);
    spi_screenreg_command_data(0xC000,0x8a);
    spi_screenreg_command_data(0xC002,0x8a);
    spi_screenreg_command_data(0xC200,0x02);
    spi_screenreg_command_data(0xC202,0x32);
    spi_screenreg_command_data(0xC100,0x40);
    spi_screenreg_command_data(0xC700,0x8b);
    mdelay(200);
    spi_screenreg_command(0x2900);
    spi_screenreg_command(0x2C00);
    
    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}
#endif

extern void rk29_lcd_spim_spin_lock(void);
extern void rk29_lcd_spim_spin_unlock(void);
int standby(u8 enable)
{
	rk29_lcd_spim_spin_lock();
	if(gLcd_info)
	        gLcd_info->io_init();
	if(enable) {
#ifdef RESET_PORT
    gpio_request(RESET_PORT, NULL);
    gpio_direction_output(RESET_PORT, 0);
    mdelay(2);
    gpio_set_value(RESET_PORT, 1);
    mdelay(10);
    gpio_free(RESET_PORT);
#endif
		spi_screenreg_command(0x2800);
		spi_screenreg_command(0x1000);
	} else {
		init();
	}
	if(gLcd_info)
	        gLcd_info->io_deinit();
	rk29_lcd_spim_spin_unlock();        
    return 0;
}

