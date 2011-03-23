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
#define OUT_CLK			26	//***27

/* Timing */
#define H_PW			16 //8Ç°ÏûÓ°
#define H_BP			24//6
#define H_VD			480//320	//***800 
#define H_FP			16//60

#define V_PW			2//12
#define V_BP			2// 4
#define V_VD			800//480	//***480
#define V_FP			4//40

/* Other */
#define DCLK_POL		1//0 
#define SWAP_RB			0

static struct rk29lcd_info *gLcd_info = NULL;
int init(void);
int standby(u8 enable);

#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin

#define CS_OUT()        gpio_direction_output(CS_PORT, 1)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0) 
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 1) 
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)


#define DRVDelayUs(i)   udelay(i*4)

void spi_screenreg_cmd(u8 Addr)
{
 u32 i;
    u32 control_bit;

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    DRVDelayUs(2);
    DRVDelayUs(2);

    CS_SET();
    TXD_SET();
    CLK_CLR();
    DRVDelayUs(30);

        CS_CLR();
        control_bit = 0x0000;
        Addr = (control_bit | Addr);//spi_screenreg_set(0x36, 0x0000, 0xffff);	
        //printk("addr is 0x%x \n", Addr); 
        for(i = 0; i < 9; i++)  //reg
        {
                if(Addr &(1<<(8-i)))
                        TXD_SET();
                else
                        TXD_CLR();

                // \u6a21\u62dfCLK
                CLK_SET();
                DRVDelayUs(2);
                CLK_CLR();
                DRVDelayUs(2);
        }

        CS_SET();
        TXD_SET();
        CLK_CLR();		
        DRVDelayUs(10);
}


void spi_screenreg_param(u8 Param)
{

	u32 i;
    u32 control_bit;

   CS_CLR();
 
        control_bit = 0x0100;
        Param = (control_bit | Param);
        //printk("data0 is 0x%x \n", Data); 
        for(i = 0; i < 9; i++)  //data
        {
                if(Param &(1<<(8-i)))
                        TXD_SET();
                else
                        TXD_CLR();

                // \u6a21\u62dfCLK
                CLK_SET();
                DRVDelayUs(2);
                CLK_CLR();
                DRVDelayUs(2);
        }

        CS_SET();
        CLK_CLR();
        TXD_CLR();
        DRVDelayUs(10);
}

void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
{
	//printk("lcd_hx8357 set_lcd_info \n"); 
    /* screen type & face */
    screen->type = OUT_TYPE;
    screen->face = OUT_FACE;
 
    /* Screen size */
    screen->x_res = H_VD;
    screen->y_res = V_VD;

    /* Timing */
    screen->pixclock = OUT_CLK;
	screen->left_margin = H_BP;		/*>2*/ 
	screen->right_margin = H_FP;	/*>2*/ 
	screen->hsync_len = H_PW;		/*>2*/ //***all > 326, 4<PW+BP<15, 
	screen->upper_margin = V_BP;	/*>2*/ 
	screen->lower_margin = V_FP;	/*>2*/ 
	screen->vsync_len = V_PW;		/*>6*/ 

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

int init(void)
{ 
	volatile u32 data;

    if(gLcd_info)
        gLcd_info->io_init();

	printk("lcd init...\n");
	spi_screenreg_cmd(0xB1);
	spi_screenreg_param(0x00);
	spi_screenreg_cmd(0xB2);
	spi_screenreg_param(0x10);
	spi_screenreg_param(0xC7);
	spi_screenreg_cmd(0xB3);
	spi_screenreg_param(0x00);
	spi_screenreg_cmd(0xB4);
	spi_screenreg_param(0x00);
	spi_screenreg_cmd(0xB9);
	spi_screenreg_param(0x00);
	spi_screenreg_cmd(0xC3);
	spi_screenreg_param(0x07);
	spi_screenreg_cmd(0xB2);
	spi_screenreg_param(0x04);
	spi_screenreg_param(0x0B);
	spi_screenreg_param(0x0B);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x07);
	spi_screenreg_param(0x04);
	spi_screenreg_cmd(0xC5);
	spi_screenreg_param(0x6E);
	spi_screenreg_cmd(0xC2);
	spi_screenreg_param(0x20);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x10);
	mdelay(20);//20ms
    
	spi_screenreg_cmd(0xC8);
	spi_screenreg_param(0xA3);
	spi_screenreg_cmd(0xC9);
	spi_screenreg_param(0x32);
	spi_screenreg_param(0x06);
	spi_screenreg_cmd(0xD7);
	spi_screenreg_param(0x03);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x0F);
	spi_screenreg_param(0x0F);
	spi_screenreg_cmd(0xCF);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x08);
	spi_screenreg_cmd(0xB6);
	spi_screenreg_param(0x20);
	spi_screenreg_param(0xC2);
	spi_screenreg_param(0xFF);
	spi_screenreg_param(0x04);
	spi_screenreg_cmd(0xEA);
	spi_screenreg_param(0x00);
	spi_screenreg_cmd(0x2A);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x01);
	spi_screenreg_param(0xDF);
	spi_screenreg_cmd(0x2B);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x03);
	spi_screenreg_param(0xEF);
	spi_screenreg_cmd(0xB0);
	spi_screenreg_param(0x01);
	spi_screenreg_cmd(0x0C);
	spi_screenreg_param(0x50);
	spi_screenreg_cmd(0x36);
	spi_screenreg_param(0x48);
	spi_screenreg_cmd(0x3A);
	spi_screenreg_param(0x66);
	spi_screenreg_cmd(0xE0);
	spi_screenreg_param(0x05);
	spi_screenreg_param(0x07);
	spi_screenreg_param(0x0B);
	spi_screenreg_param(0x14);
	spi_screenreg_param(0x11);
	spi_screenreg_param(0x14);
	spi_screenreg_param(0x0A);
	spi_screenreg_param(0x07);
	spi_screenreg_param(0x04);
	spi_screenreg_param(0x0B);
	spi_screenreg_param(0x02);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x04);
	spi_screenreg_param(0x33);
	spi_screenreg_param(0x36);
	spi_screenreg_param(0x1F);
	spi_screenreg_cmd(0xE1);
	spi_screenreg_param(0x1F);
	spi_screenreg_param(0x36);
	spi_screenreg_param(0x33);
	spi_screenreg_param(0x04);
	spi_screenreg_param(0x00);
	spi_screenreg_param(0x02);
	spi_screenreg_param(0x0B);
	spi_screenreg_param(0x04);
	spi_screenreg_param(0x07);
	spi_screenreg_param(0x0A);
	spi_screenreg_param(0x14);
	spi_screenreg_param(0x11);
	spi_screenreg_param(0x14);
	spi_screenreg_param(0x0B);
	spi_screenreg_param(0x07);
	spi_screenreg_param(0x05);
	spi_screenreg_cmd(0x11);
	mdelay(70);
	spi_screenreg_cmd(0x29);
	mdelay(10);
	spi_screenreg_cmd(0x2C);
    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;
}

int standby(u8 enable)	//***enable =1 means suspend, 0 means resume 
{
	
    if(gLcd_info)
        gLcd_info->io_init();
	#if 0
	if(enable) {
		spi_screenreg_set(0x10, 0xffff, 0xffff);
		spi_screenreg_set(0x28, 0xffff, 0xffff);
	} else { 
		spi_screenreg_set(0x29, 0xffff, 0xffff);
		spi_screenreg_set(0x11, 0xffff, 0xffff);
	}
  #endif
    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}
void set_backlight(int brightness)
{
#if 0
	if (g_spi != NULL)
	{
		fbprintk("AMS369FG06:set_backlight = %d\r\n", brightness);
		if (brightness < 0)
		{
			brightness = 0;
		}
		if (brightness > 4)
		{
			brightness = 4;
		}

		g_backlight_level = brightness;
		write_data(pBrighenessLevel[brightness], ARRAY_SIZE(pBrighenessLevel[brightness]));
	}
#endif
}
void rk2818_backlight_ctl(int suspend)
{
	standby(suspend);
}