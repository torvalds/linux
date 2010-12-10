#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include "screen.h"


/* Base */
#define OUT_TYPE		SCREEN_RGB
#define OUT_FACE		OUT_P666	/*OUT_P888*/
#define OUT_CLK			 10000000	//***27
#define LCDC_ACLK       150000000     //29 lcdc axi DMA 频率

/* Timing */
#define H_PW			8
#define H_BP			6
#define H_VD			320	//***800
#define H_FP			60

#define V_PW			12
#define V_BP			4
#define V_VD			480	//***480
#define V_FP			40

#define LCD_WIDTH       320    //need modify
#define LCD_HEIGHT      480

/* Other */
#define DCLK_POL		0
#define SWAP_RB			0

static struct rk29lcd_info *gLcd_info = NULL;
int init(void);
int standby(u8 enable);


#define TXD_PORT        gLcd_info->txd_pin
#define CLK_PORT        gLcd_info->clk_pin
#define CS_PORT         gLcd_info->cs_pin

#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)

#if 0
static void screen_set_iomux(u8 enable)
{
    int ret=-1;
    if(enable)
    {
        rk29_mux_api_set(GPIOH6_IQ_SEL_NAME, 0);
        ret = gpio_request(RK29_PIN_PH6, NULL);
        if(0)//(ret != 0)
        {
            gpio_free(RK29_PIN_PH6);
            printk(">>>>>> lcd cs gpio_request err \n ");
            goto pin_err;
        }

        rk29_mux_api_set(GPIOE_I2C0_SEL_NAME, 1);

        ret = gpio_request(RK29_PIN_PE5, NULL);
        if(0)//(ret != 0)
        {
            gpio_free(RK29_PIN_PE5);
            printk(">>>>>> lcd clk gpio_request err \n ");
            goto pin_err;
        }

        ret = gpio_request(RK29_PIN_PE4, NULL);
        if(0)//(ret != 0)
        {
            gpio_free(RK29_PIN_PE4);
            printk(">>>>>> lcd txd gpio_request err \n ");
            goto pin_err;
        }
    }
    else
    {
         gpio_free(RK29_PIN_PH6);
       //  rk29_mux_api_set(CXGPIO_HSADC_SEL_NAME, 1);

         gpio_free(RK29_PIN_PE5);
         gpio_free(RK29_PIN_PE4);
         rk29_mux_api_set(GPIOE_I2C0_SEL_NAME, 0);
    }
    return ;
pin_err:
    return ;

}
#endif

void spi_screenreg_set(u32 Addr, u32 Data)
{
#define DRVDelayUs(i)   udelay(i*2)

    u32 i;
    u32 control_bit;


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
	control_bit = 0x70<<8;
	Addr = (control_bit | Addr);
	//printk("addr is 0x%x \n", Addr);
	for(i = 0; i < 16; i++)  //reg
	{
		if(Addr &(1<<(15-i)))
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
	TXD_SET();
	CLK_SET();
	DRVDelayUs(2);
	CS_CLR();

	control_bit = 0x72<<8;
	Data = (control_bit | Data);
	//printk("data is 0x%x \n", Data);
	for(i = 0; i < 16; i++)  //data
	{
		if(Data &(1<<(15-i)))
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

void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
{
	//printk("lcd_hx8357 set_lcd_info \n");
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

    if(gLcd_info)
        gLcd_info->io_init();

#if 0 											//***这句代码是不是写错了
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
#else
	spi_screenreg_set(0xff, 0x00);
	spi_screenreg_set(0x16, 0x08);
	spi_screenreg_set(0x01, 0x02);
	spi_screenreg_set(0xe2, 0x00);
	spi_screenreg_set(0xe3, 0x00);
	spi_screenreg_set(0xf2, 0x00);
	spi_screenreg_set(0xe4, 0x1c);
	spi_screenreg_set(0xe5, 0x1c);
	spi_screenreg_set(0xe6, 0x00);
	spi_screenreg_set(0xe7, 0x1c);

	spi_screenreg_set(0x19, 0x01);
	mdelay(10);
	spi_screenreg_set(0x2a, 0x00);
	spi_screenreg_set(0x2b, 0x13);
	spi_screenreg_set(0x2f, 0x01);
	spi_screenreg_set(0x02, 0x00);
	spi_screenreg_set(0x03, 0x00);
	spi_screenreg_set(0x04, 0x01);
	spi_screenreg_set(0x05, 0x3f);
	spi_screenreg_set(0x06, 0x00);
	spi_screenreg_set(0x07, 0x00);

	spi_screenreg_set(0x08, 0x01);
	spi_screenreg_set(0x09, 0xdf);
	spi_screenreg_set(0x24, 0x91);
	spi_screenreg_set(0x25, 0x8a);
	spi_screenreg_set(0x29, 0x01);
	spi_screenreg_set(0x18, 0x22);
	spi_screenreg_set(0x1b, 0x30);
	mdelay(10);
	spi_screenreg_set(0x1d, 0x22);
	mdelay(10);
	spi_screenreg_set(0x40, 0x00);
	spi_screenreg_set(0x41, 0x3c);
	spi_screenreg_set(0x42, 0x38);
	spi_screenreg_set(0x43, 0x34);
	spi_screenreg_set(0x44, 0x2e);
	spi_screenreg_set(0x45, 0x2f);
	spi_screenreg_set(0x46, 0x41);
	spi_screenreg_set(0x47, 0x7d);
	spi_screenreg_set(0x48, 0x0b);
	spi_screenreg_set(0x49, 0x05);
	spi_screenreg_set(0x4a, 0x06);
	spi_screenreg_set(0x4b, 0x12);
	spi_screenreg_set(0x4c, 0x16);
	spi_screenreg_set(0x50, 0x10);
	spi_screenreg_set(0x51, 0x11);
	spi_screenreg_set(0x52, 0x0b);
	spi_screenreg_set(0x53, 0x07);
	spi_screenreg_set(0x54, 0x03);
	spi_screenreg_set(0x55, 0x3f);
	spi_screenreg_set(0x56, 0x02);
	spi_screenreg_set(0x57, 0x3e);
	spi_screenreg_set(0x58, 0x09);
	spi_screenreg_set(0x59, 0x0d);
	spi_screenreg_set(0x5a, 0x19);
	spi_screenreg_set(0x5b, 0x1a);
	spi_screenreg_set(0x5c, 0x14);
	spi_screenreg_set(0x5d, 0xc0);
	spi_screenreg_set(0x1a, 0x05);
	mdelay(10);

	spi_screenreg_set(0x1c, 0x03);
	mdelay(10);
	spi_screenreg_set(0x1f, 0x90);
	mdelay(10);
	spi_screenreg_set(0x1f, 0xd2);
	mdelay(10);
	spi_screenreg_set(0x28, 0x04);
	mdelay(40);
	spi_screenreg_set(0x28, 0x38);
	mdelay(40);
	spi_screenreg_set(0x28, 0x3c);
	mdelay(40);
	spi_screenreg_set(0x80, 0x00);
	spi_screenreg_set(0x81, 0x00);
	spi_screenreg_set(0x82, 0x00);
	spi_screenreg_set(0x83, 0x00);

	spi_screenreg_set(0x60, 0x08);
	spi_screenreg_set(0x31, 0x02);
	spi_screenreg_set(0x32, 0x08 /*0x00*/);
	spi_screenreg_set(0x17, 0x60);	//***RGB666
	spi_screenreg_set(0x2d, 0x1f);
	spi_screenreg_set(0xe8, 0x90);
#endif
    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;
}

int standby(u8 enable)	//***enable =1 means suspend, 0 means resume
{

    if(gLcd_info)
        gLcd_info->io_init();
	if(enable) {
		//printk("---------hx8357   screen suspend--------------\n");
		#if 0
		spi_screenreg_set(0x03, 0xde);
		#else
		//modify by robert
		#if 0
		spi_screenreg_set(0x1f, 0x91);
		spi_screenreg_set(0x19, 0x00);
		#else
		spi_screenreg_set(0x28, 0x38);
		msleep(10);
		spi_screenreg_set(0x28, 0x24);
		msleep(10);
		spi_screenreg_set(0x28, 0x04);
		#endif
		//modify end
		#endif
	} else {
		//printk("---------  hx8357 screen resume--------------\n ");
		#if 0
		spi_screenreg_set(0x03, 0x5f);
		#else
		//modify by robert
		#if 0
		spi_screenreg_set(0x19, 0x01);
		spi_screenreg_set(0x1f, 0x90);
		mdelay(10);
		spi_screenreg_set(0x1f, 0xd2);
		#else
		spi_screenreg_set(0x28, 0x38);
		msleep(10);
		spi_screenreg_set(0x28, 0x3c);
		msleep(10);
		spi_screenreg_set(0x80, 0x00);
		spi_screenreg_set(0x81, 0x00);
		spi_screenreg_set(0x82, 0x00);
		spi_screenreg_set(0x83, 0x00);

		#endif
		//modify end
		#endif
	}

    if(gLcd_info)
        gLcd_info->io_deinit();
    return 0;
}

