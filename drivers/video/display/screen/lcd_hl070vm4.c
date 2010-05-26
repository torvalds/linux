#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk2818_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "screen.h"

/* Base */
#define OUT_TYPE		SCREEN_RGB
#define OUT_FACE		OUT_P888
#define OUT_CLK			27

/* Timing */
#define H_PW			10
#define H_BP			206  
#define H_VD			800
#define H_FP			40

#define V_PW			10
#define V_BP			25
#define V_VD			480
#define V_FP			10

/* Other */
#define DCLK_POL		1 ///0
#define SWAP_RB			0

#define CS_OUT()        gpio_direction_output(RK2818_PIN_PA4, 0)
#define CS_SET()        gpio_set_value(RK2818_PIN_PA4, GPIO_HIGH)
#define CS_CLR()        gpio_set_value(RK2818_PIN_PA4, GPIO_LOW)
#define CLK_OUT()       gpio_direction_output(RK2818_PIN_PE7, 0)  //I2C0_SCL
#define CLK_SET()       gpio_set_value(RK2818_PIN_PE7, GPIO_HIGH)
#define CLK_CLR()       gpio_set_value(RK2818_PIN_PE7, GPIO_LOW)
#define TXD_OUT()       gpio_direction_output(RK2818_PIN_PE6, 0)  //I2C0_SDA
#define TXD_SET()       gpio_set_value(RK2818_PIN_PE6, GPIO_HIGH)
#define TXD_CLR()       gpio_set_value(RK2818_PIN_PE6, GPIO_LOW)

#define DRVDelayUs(i)   udelay(i*2)

int init(void);
int standby(u8 enable);

void screen_set_iomux(u8 enable)
{
    int ret=-1;
    if(enable)
    {
        rk2818_mux_api_set(CXGPIO_HSADC_SEL_NAME, 0);
        ret = gpio_request(RK2818_PIN_PA4, NULL); 
        if(0)//(ret != 0)
        {
            gpio_free(RK2818_PIN_PA4);
            printk(">>>>>> lcd cs gpio_request err \n ");           
            goto pin_err;
        }  
        
        rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, 0);                   

        ret = gpio_request(RK2818_PIN_PE7, NULL); 
        if(0)//(ret != 0)
        {
            gpio_free(RK2818_PIN_PE7);
            printk(">>>>>> lcd clk gpio_request err \n "); 
            goto pin_err;
        }  
        
        ret = gpio_request(RK2818_PIN_PE6, NULL); 
        if(0)//(ret != 0)
        {
            gpio_free(RK2818_PIN_PE6);
            printk(">>>>>> lcd txd gpio_request err \n "); 
            goto pin_err;
        }        
    }
    else
    {
         gpio_free(RK2818_PIN_PA4); 
         rk2818_mux_api_set(CXGPIO_HSADC_SEL_NAME, 1);

         gpio_free(RK2818_PIN_PE7);   
         gpio_free(RK2818_PIN_PE6); 
         rk2818_mux_api_set(GPIOE_U1IR_I2C1_NAME, 2);
   }
    return ;
pin_err:
    return ;

}

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
    screen->init = init;
    screen->standby = standby;
}


//void spi_screenreg_set(uint32 Addr, uint32 Data)
void spi_screenreg_set(u32 Data)
{
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
	for(i = 0; i < 16; i++)  //reg
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

/*
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


	//for(i = 0; i < 8; i++)  //data
	for(i = 0; i < 16; i++)
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
*/
	CS_SET();
	CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2); 

}


int init(void)
{
    screen_set_iomux(1);   
/*
r0 00000010 11011011
r1 00010001 01101111
r2 00100000 10000000
r3 00110000 00001000
r4 01000001 10010000-->>01000001 10011111
r5 01100001 11001110
*/
	spi_screenreg_set(0x02db);
	spi_screenreg_set(0x116f);
	spi_screenreg_set(0x2080);
	spi_screenreg_set(0x3008);
	spi_screenreg_set(0x419f);
	spi_screenreg_set(0x61ce);
    screen_set_iomux(0);   
    return 0;
}

int standby(u8 enable)
{
    screen_set_iomux(1);   
	if(!enable) {
		init();
	} //else {
//		spi_screenreg_set(0x03, 0x5f);
//	}
    screen_set_iomux(0);   
    return 0;
}

