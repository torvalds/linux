/*
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * author: hhb@rock-chips.com
 * creat date: 2011-03-11
 * route:drivers/video/display/screen/lcd_rgb_tft480800_25_e.c - driver for rk29 phone sdk
 * station:haven't been tested in any hardware platform
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
#define OUT_CLK			26000000
#define LCDC_ACLK       150000000     //29 lcdc axi DMA Ƶ��

/* Timing */
#define H_PW		  8
#define H_BP			6
#define H_VD			480
#define H_FP			60

#define V_PW		  2
#define V_BP			12
#define V_VD			800
#define V_FP			4


#define LCD_WIDTH       800    //need modify
#define LCD_HEIGHT      480

/* Other */
#define DCLK_POL		1
#define SWAP_RB			0


/* define spi write command and data interface function */

#define SIMULATION_SPI 1
#ifdef SIMULATION_SPI

    #define TXD_PORT        gLcd_info->txd_pin
	#define CLK_PORT        gLcd_info->clk_pin
	#define CS_PORT         gLcd_info->cs_pin
	#define LCD_RST_PORT    RK29_PIN6_PC6     

	#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
	#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
	#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
	#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
	#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
	#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
	#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
	#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
	#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)
    #define LCD_RST_OUT()  gpio_direction_output(LCD_RST_PORT, 0)
    #define LCD_RST(i)      gpio_set_value(LCD_RST_PORT, i)

//	#define bits_9
	#ifdef bits_9  //9bits
	#define LCDSPI_InitCMD(cmd)    spi_write_9bit(0, cmd)
	#define LCDSPI_InitDAT(dat)    spi_write_9bit(1, dat)
	#else  //16bits
	#define LCDSPI_InitCMD(cmd)    spi_write_16bit(0, cmd)
	#define LCDSPI_InitDAT(dat)    spi_write_16bit(1, dat)
	#endif
	#define Lcd_EnvidOnOff(i)

#else

	#define bits_9 1
	#ifdef bits_9  //9bits
	#define LCDSPI_InitCMD(cmd)
	#define LCDSPI_InitDAT(dat)
	#else  //16bits
	#define LCDSPI_InitCMD(cmd)
	#define LCDSPI_InitDAT(dat)
	#endif

#endif


/* define lcd command */
#define ENTER_SLEEP_MODE        0x10
#define EXIT_SLEEP_MODE         0x11
#define SET_COLUMN_ADDRESS      0x2a
#define SET_PAGE_ADDRESS        0x2b
#define WRITE_MEMORY_START      0x2c
#define SET_DISPLAY_ON          0x29
#define SET_DISPLAY_OFF         0x28
#define SET_ADDRESS_MODE        0x36
#define SET_PIXEL_FORMAT        0x3a


#define DRVDelayUs(i)   udelay(i*2)

static struct rk29lcd_info *gLcd_info = NULL;
int lcd_init(void);
int lcd_standby(u8 enable);


/* spi write a data frame,type mean command or data */
int spi_write_9bit(u32 type, u32 value)
{
    u32 i = 0;

    if(type != 0 && type != 1)
    {
    	return -1;
    }
    /*make a data frame of 9 bits,the 8th bit  0:mean command,1:mean data*/
    value &= 0xff;
    value &= (type << 8);

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
	for(i = 0; i < 9; i++)  //reg
	{
		if(value & (1 << (8-i)))
        {
			TXD_SET();
		}
        else
        {
			TXD_CLR();
        }

		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}

	CS_SET();
	CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2);
    return 0;
}


/* spi write a data frame,type mean command or data */
int spi_write_16bit(u32 type, u32 value)
{
    u32 i = 0;
    u32 data = 0;
	
    if(type != 0 && type != 1)
    {
    	return -1;
    }
    /*make a data frame of 16 bits,the 8th bit  0:mean command,1:mean data*/
    data = (type << 8)|value; 

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
		if(data & (1 << (15-i)))
        {
			TXD_SET();
		}
        else
        {
			TXD_CLR();
        }

		CLK_CLR();
		DRVDelayUs(2);
		CLK_SET();
		DRVDelayUs(2);
	}

	CS_SET();
	CLK_CLR();
	TXD_CLR();
	DRVDelayUs(2);
    return 0;
}
int lcd_init(void)
{
    if(gLcd_info)
        gLcd_info->io_init();
    printk("lcd_init...\n");
/* reset lcd to start init lcd by software if there is no hardware reset circuit for the lcd */
#ifdef LCD_RST_PORT
	gpio_request(LCD_RST_PORT, NULL);
#endif

#if 1
    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    CS_SET();
    TXD_SET();
    CLK_SET();
    LCD_RST_OUT();
	LCD_RST(1);
	msleep(10);
	LCD_RST(0);
	msleep(100);
	LCD_RST(1);
	msleep(100);
#endif

#if 1

	LCDSPI_InitCMD(0xB9);  // SET password
	LCDSPI_InitDAT(0xFF);
	LCDSPI_InitDAT(0x83);
	LCDSPI_InitDAT(0x69);

    LCDSPI_InitCMD(0xB1);  //Set Power
    LCDSPI_InitDAT(0x85);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x34);
	LCDSPI_InitDAT(0x07);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x0F);
	LCDSPI_InitDAT(0x0F);
	LCDSPI_InitDAT(0x2A);
	LCDSPI_InitDAT(0x32);
	LCDSPI_InitDAT(0x3F);
	LCDSPI_InitDAT(0x3F);
	LCDSPI_InitDAT(0x01); //update VBIAS
	LCDSPI_InitDAT(0x3A);
	LCDSPI_InitDAT(0x01);
	LCDSPI_InitDAT(0xE6);
	LCDSPI_InitDAT(0xE6);
	LCDSPI_InitDAT(0xE6);
	LCDSPI_InitDAT(0xE6);
	LCDSPI_InitDAT(0xE6);



	LCDSPI_InitCMD(0xB2);  // SET Display  480x800
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x28);  //23
	LCDSPI_InitDAT(0x05); //03
	LCDSPI_InitDAT(0x05);  //03
	LCDSPI_InitDAT(0x70);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0xFF);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x03);
	LCDSPI_InitDAT(0x03);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x01);


	LCDSPI_InitCMD(0xB4);  // SET Display  480x800
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x18);
	LCDSPI_InitDAT(0x80);
	LCDSPI_InitDAT(0x06);
	LCDSPI_InitDAT(0x02);



	LCDSPI_InitCMD(0xB6);  // SET VCOM
	LCDSPI_InitDAT(0x42);  // Update VCOM
	LCDSPI_InitDAT(0x42);



	LCDSPI_InitCMD(0xD5);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x04);
	LCDSPI_InitDAT(0x03);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x01);
	LCDSPI_InitDAT(0x05);
	LCDSPI_InitDAT(0x28);
    LCDSPI_InitDAT(0x70);
	LCDSPI_InitDAT(0x01);
	LCDSPI_InitDAT(0x03);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x40);
	LCDSPI_InitDAT(0x06);
	LCDSPI_InitDAT(0x51);
	LCDSPI_InitDAT(0x07);
	LCDSPI_InitDAT(0x00);
    LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x41);
	LCDSPI_InitDAT(0x06);
	LCDSPI_InitDAT(0x50);
	LCDSPI_InitDAT(0x07);
	LCDSPI_InitDAT(0x07);
	LCDSPI_InitDAT(0x0F);
	LCDSPI_InitDAT(0x04);
	LCDSPI_InitDAT(0x00);


 ///Gamma2.2
	LCDSPI_InitCMD(0xE0);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x13);
	LCDSPI_InitDAT(0x19);
	LCDSPI_InitDAT(0x38);
	LCDSPI_InitDAT(0x3D);
	LCDSPI_InitDAT(0x3F);
	LCDSPI_InitDAT(0x28);
	LCDSPI_InitDAT(0x46);
	LCDSPI_InitDAT(0x07);
	LCDSPI_InitDAT(0x0D);
	LCDSPI_InitDAT(0x0E);
	LCDSPI_InitDAT(0x12);
	LCDSPI_InitDAT(0x15);
	LCDSPI_InitDAT(0x12);
	LCDSPI_InitDAT(0x14);
	LCDSPI_InitDAT(0x0F);
	LCDSPI_InitDAT(0x17);
	LCDSPI_InitDAT(0x00);
	LCDSPI_InitDAT(0x13);
	LCDSPI_InitDAT(0x19);
	LCDSPI_InitDAT(0x38);
	LCDSPI_InitDAT(0x3D);
	LCDSPI_InitDAT(0x3F);
	LCDSPI_InitDAT(0x28);
	LCDSPI_InitDAT(0x46);
	LCDSPI_InitDAT(0x07);
	LCDSPI_InitDAT(0x0D);
	LCDSPI_InitDAT(0x0E);
	LCDSPI_InitDAT(0x12);
	LCDSPI_InitDAT(0x15);
	LCDSPI_InitDAT(0x12);
	LCDSPI_InitDAT(0x14);
	LCDSPI_InitDAT(0x0F);
	LCDSPI_InitDAT(0x17);


	msleep(10);

///DGC Setting
	LCDSPI_InitCMD(0xC1);
	LCDSPI_InitDAT(0x01);
//R
	LCDSPI_InitDAT(0x04);
	LCDSPI_InitDAT(0x13);
	LCDSPI_InitDAT(0x1a);
	LCDSPI_InitDAT(0x20);
	LCDSPI_InitDAT(0x27);
	LCDSPI_InitDAT(0x2c);
	LCDSPI_InitDAT(0x32);
	LCDSPI_InitDAT(0x36);
	LCDSPI_InitDAT(0x3f);
	LCDSPI_InitDAT(0x47);
	LCDSPI_InitDAT(0x50);
	LCDSPI_InitDAT(0x59);
	LCDSPI_InitDAT(0x60);
	LCDSPI_InitDAT(0x68);
	LCDSPI_InitDAT(0x71);
	LCDSPI_InitDAT(0x7B);
	LCDSPI_InitDAT(0x82);
	LCDSPI_InitDAT(0x89);
	LCDSPI_InitDAT(0x91);
	LCDSPI_InitDAT(0x98);
	LCDSPI_InitDAT(0xA0);
	LCDSPI_InitDAT(0xA8);
	LCDSPI_InitDAT(0xB0);
	LCDSPI_InitDAT(0xB8);
	LCDSPI_InitDAT(0xC1);
	LCDSPI_InitDAT(0xC9);
	LCDSPI_InitDAT(0xD0);
	LCDSPI_InitDAT(0xD7);
	LCDSPI_InitDAT(0xE0);
	LCDSPI_InitDAT(0xE7);
	LCDSPI_InitDAT(0xEF);
	LCDSPI_InitDAT(0xF7);
	LCDSPI_InitDAT(0xFE);
	LCDSPI_InitDAT(0xCF);
	LCDSPI_InitDAT(0x52);
	LCDSPI_InitDAT(0x34);
	LCDSPI_InitDAT(0xF8);
	LCDSPI_InitDAT(0x51);
	LCDSPI_InitDAT(0xF5);
	LCDSPI_InitDAT(0x9D);
	LCDSPI_InitDAT(0x75);
	LCDSPI_InitDAT(0x00);
//G
	LCDSPI_InitDAT(0x04);
	LCDSPI_InitDAT(0x13);
	LCDSPI_InitDAT(0x1a);
	LCDSPI_InitDAT(0x20);
	LCDSPI_InitDAT(0x27);
	LCDSPI_InitDAT(0x2c);
	LCDSPI_InitDAT(0x32);
	LCDSPI_InitDAT(0x36);
	LCDSPI_InitDAT(0x3f);
	LCDSPI_InitDAT(0x47);
	LCDSPI_InitDAT(0x50);
	LCDSPI_InitDAT(0x59);
	LCDSPI_InitDAT(0x60);
	LCDSPI_InitDAT(0x68);
	LCDSPI_InitDAT(0x71);
	LCDSPI_InitDAT(0x7B);
	LCDSPI_InitDAT(0x82);
	LCDSPI_InitDAT(0x89);
	LCDSPI_InitDAT(0x91);
	LCDSPI_InitDAT(0x98);
	LCDSPI_InitDAT(0xA0);
	LCDSPI_InitDAT(0xA8);
	LCDSPI_InitDAT(0xB0);
	LCDSPI_InitDAT(0xB8);
	LCDSPI_InitDAT(0xC1);
	LCDSPI_InitDAT(0xC9);
	LCDSPI_InitDAT(0xD0);
	LCDSPI_InitDAT(0xD7);
	LCDSPI_InitDAT(0xE0);
	LCDSPI_InitDAT(0xE7);
	LCDSPI_InitDAT(0xEF);
	LCDSPI_InitDAT(0xF7);
	LCDSPI_InitDAT(0xFE);
	LCDSPI_InitDAT(0xCF);
	LCDSPI_InitDAT(0x52);
	LCDSPI_InitDAT(0x34);
	LCDSPI_InitDAT(0xF8);
	LCDSPI_InitDAT(0x51);
	LCDSPI_InitDAT(0xF5);
	LCDSPI_InitDAT(0x9D);
	LCDSPI_InitDAT(0x75);
	LCDSPI_InitDAT(0x00);
//B
	LCDSPI_InitDAT(0x04);
	LCDSPI_InitDAT(0x13);
	LCDSPI_InitDAT(0x1a);
	LCDSPI_InitDAT(0x20);
	LCDSPI_InitDAT(0x27);
	LCDSPI_InitDAT(0x2c);
	LCDSPI_InitDAT(0x32);
	LCDSPI_InitDAT(0x36);
	LCDSPI_InitDAT(0x3f);
	LCDSPI_InitDAT(0x47);
	LCDSPI_InitDAT(0x50);
	LCDSPI_InitDAT(0x59);
	LCDSPI_InitDAT(0x60);
	LCDSPI_InitDAT(0x68);
	LCDSPI_InitDAT(0x71);
	LCDSPI_InitDAT(0x7B);
	LCDSPI_InitDAT(0x82);
	LCDSPI_InitDAT(0x89);
	LCDSPI_InitDAT(0x91);
	LCDSPI_InitDAT(0x98);
	LCDSPI_InitDAT(0xA0);
	LCDSPI_InitDAT(0xA8);
	LCDSPI_InitDAT(0xB0);
	LCDSPI_InitDAT(0xB8);
	LCDSPI_InitDAT(0xC1);
	LCDSPI_InitDAT(0xC9);
	LCDSPI_InitDAT(0xD0);
	LCDSPI_InitDAT(0xD7);
	LCDSPI_InitDAT(0xE0);
	LCDSPI_InitDAT(0xE7);
	LCDSPI_InitDAT(0xEF);
	LCDSPI_InitDAT(0xF7);
	LCDSPI_InitDAT(0xFE);
	LCDSPI_InitDAT(0xCF);
	LCDSPI_InitDAT(0x52);
	LCDSPI_InitDAT(0x34);
	LCDSPI_InitDAT(0xF8);
	LCDSPI_InitDAT(0x51);
	LCDSPI_InitDAT(0xF5);
	LCDSPI_InitDAT(0x9D);
	LCDSPI_InitDAT(0x75);
	LCDSPI_InitDAT(0x00);

	msleep(10);


    //LCDSPI_InitCMD(0x36);
    //LCDSPI_InitDAT(0x80);   //µ÷Õû36HÖÐµÄ²ÎÊý¿ÉÒÔÊµÏÖGATEºÍSOURCEµÄ·­×ª

    LCDSPI_InitCMD(SET_PIXEL_FORMAT);
    LCDSPI_InitDAT(0x77);

    LCDSPI_InitCMD(EXIT_SLEEP_MODE);
    msleep(120);

	LCDSPI_InitCMD(SET_DISPLAY_ON);

	LCDSPI_InitCMD(WRITE_MEMORY_START);
#endif

    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;
}

int lcd_standby(u8 enable)
{
    if(gLcd_info)
        gLcd_info->io_init();

	if(enable) {
		Lcd_EnvidOnOff(0);  //RGB TIMENG OFF
		LCDSPI_InitCMD(ENTER_SLEEP_MODE);
		Lcd_EnvidOnOff(1);  //RGB TIMENG ON
		msleep(200);
		Lcd_EnvidOnOff(0);  //RGB TIMENG OFF
		msleep(100);
	} else {
		//LCD_RESET();
		LCDSPI_InitCMD(EXIT_SLEEP_MODE);
		msleep(200);
		Lcd_EnvidOnOff(1);  //RGB TIMENG ON
		msleep(200);
	}

    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;
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
    screen->init = lcd_init;
    screen->standby = lcd_standby;
    if(lcd_info)
        gLcd_info = lcd_info;
}



