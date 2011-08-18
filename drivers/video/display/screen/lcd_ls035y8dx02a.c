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
#define OUT_FACE		OUT_P888
#define OUT_CLK			(26*1000000)	//***27  uint Hz
#define LCDC_ACLK       300000000     //29 lcdc axi DMA Ƶ��

/* Timing */
#define H_PW			8 //16
#define H_BP			6//24
#define H_VD			480//320
#define H_FP			60//16

#define V_PW			2
#define V_BP			2
#define V_VD			800//480
#define V_FP			4

#define LCD_WIDTH       480    //need modify
#define LCD_HEIGHT      800

/* Other */
#define DCLK_POL		1             //0 
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

u32 spi_screenreg_get(u32 Addr)
{
    u32 i, data = 0;
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
        Addr = (control_bit | Addr);
        printk("addr is 0x%x \n", Addr); 
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
		
        CS_CLR();	
        for(i = 0; i < 9; i++)
        {
			CLK_SET();
			DRVDelayUs(2);
			CLK_CLR();
			if(RXD_GET())
			{
				data |= 1<<(8-i);
			}
			else
			{
				data &= ~(1<<(8-i));
			}
			DRVDelayUs(2);
        }
        CS_SET();
        CLK_CLR();
        TXD_CLR();
        DRVDelayUs(30);
        
	return data;
}

void spi_screenreg_set(u32 Addr, u32 Data0, u32 Data1)
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
        Addr = (control_bit | Addr);
        //printk("addr is 0x%x \n", Addr); 
        for(i = 0; i < 9; i++)  //reg
        {
			if(Addr &(1<<(8-i)))
					TXD_SET();
			else
					TXD_CLR();
			CLK_SET();
			DRVDelayUs(2);
			CLK_CLR();
			DRVDelayUs(2);
        }

        CS_SET();
        TXD_SET();
        CLK_CLR();		
        DRVDelayUs(10);

	 if(0xffff == Data0){
		 return;
	 }
		
        CS_CLR();
 
        control_bit = 0x0100;
        Data0 = (control_bit | Data0);
        //printk("data0 is 0x%x \n", Data); 
        for(i = 0; i < 9; i++)  //data
        {
			if(Data0 &(1<<(8-i)))
					TXD_SET();
			else
					TXD_CLR();
			CLK_SET();
			DRVDelayUs(2);
			CLK_CLR();
			DRVDelayUs(2);
        }

        CS_SET();
        CLK_CLR();
        TXD_CLR();
        DRVDelayUs(10);

	 if(0xffff == Data1)
		return;
		
        CS_CLR();
 
        control_bit = 0x0100;
        Data1 = (control_bit | Data1);
        //printk("data1 is 0x%x \n", Data); 
        for(i = 0; i < 9; i++)  //data
        {
			if(Data1 &(1<<(8-i)))
					TXD_SET();
			else
					TXD_CLR();
			CLK_SET();
			DRVDelayUs(2);
			CLK_CLR();
			DRVDelayUs(2);
        }

        CS_SET();
        CLK_CLR();
        TXD_CLR();
        DRVDelayUs(30);
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
	spi_screenreg_set(0x29, 0xffff, 0xffff);      //display on
	spi_screenreg_set(0x11, 0xffff, 0xffff);      //sleep out
	mdelay(150);
	spi_screenreg_set(0x36, 0x0000, 0xffff);      //set address mode
	
	while(0)  //this code is not used here
	{	
		data = spi_screenreg_get(0x0a);	
		printk("------------liuylcd init reg 0x0a=0x%x \n", spi_screenreg_get(0x0a));
		data = spi_screenreg_get(0x0b);
		printk("------------liuylcd init reg 0x0b=0x%x \n", spi_screenreg_get(0x0b));
		data = spi_screenreg_get(0x0c);
		printk("------------liuylcd init reg 0x0c=0x%x \n", spi_screenreg_get(0x0c));
		data = spi_screenreg_get(0x0d);
		printk("------------liuylcd init reg 0x0d=0x%x \n", spi_screenreg_get(0x0d));
		data = spi_screenreg_get(0x0f);
		printk("------------liuylcd init reg 0x0f=0x%x \n", spi_screenreg_get(0x0f));
	}	

	spi_screenreg_set(0x3a, 0x0070, 0xffff);      //set pixel format
	spi_screenreg_set(0xb0, 0x0000, 0xffff);      //enable command acess
	spi_screenreg_set(0xb8, 0x0001, 0xffff);      //BLC setting
	spi_screenreg_set(0xb9, 0x0001, 0x00ff);      //LED PWM
	spi_screenreg_set(0xb0, 0x0003, 0xffff);      //disable command acess
	
    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;
}

extern void rk29_lcd_spim_spin_lock(void);
extern void rk29_lcd_spim_spin_unlock(void);
int lcd_standby(u8 enable)	//***enable =1 means suspend, 0 means resume 
{
	rk29_lcd_spim_spin_lock();
	if(gLcd_info)
       gLcd_info->io_init();

	if(enable) {
		//printk("lcd suspend\n");
		spi_screenreg_set(0x10, 0xffff, 0xffff);
		mdelay(120);
		spi_screenreg_set(0x28, 0xffff, 0xffff);
	} else { 
		//printk("lcd resume\n");
		spi_screenreg_set(0x29, 0xffff, 0xffff);
		spi_screenreg_set(0x11, 0xffff, 0xffff);
		mdelay(130);
		spi_screenreg_set(0x36, 0x0000, 0xffff);      //set address mode
		spi_screenreg_set(0x3a, 0x0070, 0xffff);      //set pixel format
		spi_screenreg_set(0xb0, 0x0000, 0xffff);      //enable command acess
		spi_screenreg_set(0xb8, 0x0001, 0xffff);      //BLC setting
		spi_screenreg_set(0xb9, 0x0001, 0x00ff);      //LED PWM
		spi_screenreg_set(0xb0, 0x0003, 0xffff);      //disable command acess
	}

    if(gLcd_info)
       gLcd_info->io_deinit();
	rk29_lcd_spim_spin_unlock();
    return 0;
}

