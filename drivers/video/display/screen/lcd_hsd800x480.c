/* This Lcd Driver is HSD070IDW1 write by cst 2009.10.27 */
#include <linux/fb.h>
#include <linux/delay.h>
#include <asm/arch/lcdcon.h>
#include <asm/arch/rk28_i2c.h>
#include <asm/arch/rk28_fb.h>
#include <asm/arch/gpio.h>
#include <asm/arch/iomux.h>
#include "screen.h"

/* Base */
#define OUT_TYPE		SCREEN_RGB
#define OUT_FACE		OUT_P888
#define OUT_CLK			28

/* Timing */
#define H_PW			1
#define H_BP			88
#define H_VD			800
#define H_FP			40

#define V_PW			3
#define V_BP			29
#define V_VD			480
#define V_FP			13

/* Other */
#define DCLK_POL		1
#define SWAP_RB			1

int init(void);
int standby(u8 enable);

void set_lcd_info(struct rk28fb_screen *screen)
{
    /* screen type & face */
    screen->type = OUT_TYPE;
    screen->face = OUT_FACE;

    /* Screen size */
    screen->x_res = H_VD;
    screen->y_res = V_VD;

    /* Timing */
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
//cannot need init,so set screen->init = null at rk28_fb.c file 

void spi_screenreg_set(uint32 Addr, uint32 Data)
{
#define CS_OUT()        GPIOSetPinDirection(GPIOPortB_Pin3, GPIO_OUT)
#define CS_SET()        GPIOSetPinLevel(GPIOPortB_Pin3, GPIO_HIGH)
#define CS_CLR()        GPIOSetPinLevel(GPIOPortB_Pin3, GPIO_LOW)
#define CLK_OUT()       GPIOSetPinDirection(GPIOPortE_Pin7, GPIO_OUT)  //I2C0_SCL
#define CLK_SET()       GPIOSetPinLevel(GPIOPortE_Pin7, GPIO_HIGH)
#define CLK_CLR()       GPIOSetPinLevel(GPIOPortE_Pin7, GPIO_LOW)
#define TXD_OUT()       GPIOSetPinDirection(GPIOPortE_Pin6, GPIO_OUT)  //I2C0_SDA
#define TXD_SET()       GPIOSetPinLevel(GPIOPortE_Pin6, GPIO_HIGH)
#define TXD_CLR()       GPIOSetPinLevel(GPIOPortE_Pin6, GPIO_LOW)

#define DRVDelayUs(i)   udelay(i*2)

    uint32 i;

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


int init(void)
{
    rockchip_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_GPIO1_A45);

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

    rockchip_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_I2C0);
    return 0;
}

int standby(u8 enable)
{
#if 0
    rockchip_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_GPIO1_A45);
	if(enable) {
		spi_screenreg_set(0x03, 0xde);
	} else {
		spi_screenreg_set(0x03, 0x5f);
	}
    rockchip_mux_api_set(GPIOE_I2C0_SEL_NAME, IOMUXA_I2C0);
#else
    
    GPIOSetPinDirection(GPIOPortB_Pin3, GPIO_OUT);
    GPIOSetPinDirection(GPIOPortB_Pin2, GPIO_OUT);

    if(enable)
       {
        GPIOSetPinLevel(GPIOPortB_Pin3, GPIO_LOW);          
        GPIOSetPinLevel(GPIOPortB_Pin2, GPIO_HIGH);          
       }
    else
       {
        GPIOSetPinLevel(GPIOPortB_Pin3, GPIO_HIGH);          
        GPIOSetPinLevel(GPIOPortB_Pin2, GPIO_LOW);          
        }
#endif
    return 0;
}

