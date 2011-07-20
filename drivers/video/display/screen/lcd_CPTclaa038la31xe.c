/*
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * author: hhb@rock-chips.com
 * creat date: 2011-03-22
 * route:drivers/video/display/screen/lcd_ls035y8dx02a.c - driver for rk29 phone sdk
 * declaration: This program driver have been tested in rk29_phonesdk hardware platform at 2011.03.31.
 * about migration: you need just 3 interface functions,such as lcd_init(void),lcd_standby(u8 enable),
 * set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
#define OUT_FACE		OUT_P666
#define OUT_CLK			(26*1000000)	//***27  uint Hz
#define LCDC_ACLK       150000000     //29 lcdc axi DMA Ƶ��

/* Timing */
#define H_PW			10//8 //16
#define H_BP			10//24
#define H_VD			480//320
#define H_FP			10//60//16

#define V_PW			3
#define V_BP			3
#define V_VD			800//480
#define V_FP			3

#define LCD_WIDTH       800    //need modify
#define LCD_HEIGHT      480

/* Other */
#define DCLK_POL		0           //0 
#define SWAP_RB			0

static struct rk29lcd_info *gLcd_info = NULL;
int lcd_init(void);
int lcd_standby(u8 enable);

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

#define DRVDelayUs(i)   udelay(i*4)
#define DRVDelayMs(i)   mdelay(i*4)

/*----------------------------------------------------------------------
Name	:   Claa0381a31RegSet
Desc	:   IO模拟SPI对屏寄存器进行设置
Params  :   Reg         寄存器地址
            Data        数据
Return  :
Notes   :   设置前需要调用SetIOSpiMode(1)进入IO模式
            设置后需要调用SetIOSpiMode(0)退出IO模式
----------------------------------------------------------------------*/
void Claa0381a31Cmd(u32 data)
{
    u32 i;
    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    DRVDelayUs(2);

    CS_SET();
    TXD_SET();
    CLK_SET();
    DRVDelayUs(2);

    if(data)
    {
    	CS_CLR();
        DRVDelayUs(2);

        TXD_CLR();   //wr 0
        CLK_CLR();
        DRVDelayUs(2);
        CLK_SET();
        DRVDelayUs(2);
        
    	for(i = 0; i < 8; i++)  //reg
    	{
    		if(data &(1<<(7-i)))
    			TXD_SET();
    		else
    			TXD_CLR();
    
    		// 模拟CLK
    		CLK_CLR();
    		DRVDelayUs(2);
    		CLK_SET();
    		DRVDelayUs(2);
    	}
    }    
}

void Claa0381a31Data(u32 data)
{
    u32 i;
    
    TXD_SET();
    CLK_CLR();
    DRVDelayUs(2);
    CLK_SET();
    DRVDelayUs(2);

	for(i = 0; i < 8; i++)  //reg
	{
		if(data &(1<<(7-i)))
			TXD_SET();
		else
			TXD_CLR();

		// 模拟CLK
		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}
      
} 


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
    screen->init = lcd_init;
    screen->standby = lcd_standby;
    if(lcd_info)
        gLcd_info = lcd_info;
}

int lcd_init(void)
{ 
	volatile u32 data;
    if(gLcd_info){
        gLcd_info->io_init();
	}

    /* reset lcd to start init lcd by software if there is no hardware reset circuit for the lcd */
#ifdef RESET_PORT
    gpio_request(RESET_PORT, NULL);
    gpio_direction_output(RESET_PORT, 0);
    mdelay(2);
    gpio_set_value(RESET_PORT, 1);
    mdelay(10);
    gpio_free(RESET_PORT);
#endif

	printk("lcd init...\n");

    Claa0381a31Cmd(0xb9);
    Claa0381a31Data(0xff);
    Claa0381a31Data(0x83);
    Claa0381a31Data(0x63);
    Claa0381a31Cmd(0);

    Claa0381a31Cmd(0xb1);
    Claa0381a31Data(0x81);
    Claa0381a31Data(0x30);
    Claa0381a31Data(0x03);
    Claa0381a31Data(0x34);
    Claa0381a31Data(0x02);
    Claa0381a31Data(0x13);  
    Claa0381a31Data(0x11);
    Claa0381a31Data(0x00);
    Claa0381a31Data(0x35); 
    Claa0381a31Data(0x3e);
    Claa0381a31Data(0x16);
    Claa0381a31Data(0x16);  
    Claa0381a31Cmd(0);

    Claa0381a31Cmd(0x11);
    Claa0381a31Cmd(0);

    DRVDelayMs(150);

    Claa0381a31Cmd(0xb6);
    Claa0381a31Data(0x42);
    Claa0381a31Cmd(0);

    Claa0381a31Cmd(0xb3);
    Claa0381a31Data(0x01);
    Claa0381a31Cmd(0);

    Claa0381a31Cmd(0xb4);
    Claa0381a31Data(0x04);
    Claa0381a31Cmd(0);

    Claa0381a31Cmd(0xe0);
    Claa0381a31Data(0x00);
    Claa0381a31Data(0x1e);
    Claa0381a31Data(0x23);
    Claa0381a31Data(0x2d);
    Claa0381a31Data(0x2d);
    Claa0381a31Data(0x3f);  
    Claa0381a31Data(0x08);
    Claa0381a31Data(0xcc);
    Claa0381a31Data(0x8c); 
    Claa0381a31Data(0xcf);
    Claa0381a31Data(0x51);
    Claa0381a31Data(0x12); 
    Claa0381a31Data(0x52);
    Claa0381a31Data(0x92);
    Claa0381a31Data(0x1E);
    Claa0381a31Data(0x00);
    Claa0381a31Data(0x1e);
    Claa0381a31Data(0x23);  
    Claa0381a31Data(0x2d);
    Claa0381a31Data(0x2d);
    Claa0381a31Data(0x3f); 
    Claa0381a31Data(0x08);
    Claa0381a31Data(0xcc);
    Claa0381a31Data(0x8c); 
    Claa0381a31Data(0xcf);  
    Claa0381a31Data(0x51);
    Claa0381a31Data(0x12);
    Claa0381a31Data(0x52); 
    Claa0381a31Data(0x92);
    Claa0381a31Data(0x1E);    
    Claa0381a31Cmd(0);

    Claa0381a31Cmd(0xcc);
    Claa0381a31Data(0x0b);
    Claa0381a31Cmd(0);

    Claa0381a31Cmd(0x3a);
    Claa0381a31Data(0x60);
    Claa0381a31Cmd(0);

    DRVDelayMs(20);

    Claa0381a31Cmd(0x29);
    Claa0381a31Cmd(0);
	
    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;
}

int lcd_standby(u8 enable)	//***enable =1 means suspend, 0 means resume 
{

    if(gLcd_info)
       gLcd_info->io_init();
	printk("lcd standby\n");
	if(enable) {
		printk("lcd standby...enable =1 means suspend\n");
		//spi_screenreg_set(0x10, 0xffff, 0xffff);
		//mdelay(120);
		//spi_screenreg_set(0x28, 0xffff, 0xffff);
	} else { 
		printk("lcd standby...0 means resume\n");
		//spi_screenreg_set(0x29, 0xffff, 0xffff);
		//spi_screenreg_set(0x11, 0xffff, 0xffff);
		//mdelay(150);
	}

    if(gLcd_info)
       gLcd_info->io_deinit();
    return 0;
}

