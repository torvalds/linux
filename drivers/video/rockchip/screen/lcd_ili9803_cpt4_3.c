/*
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * author: hhb@rock-chips.com
 * creat date: 2011-05-14
 * route:drivers/video/display/screen/lcd_ili9803_cpt4_3.c - driver for rk29 phone sdk or rk29 a22
 * station:haven been tested in a22 hardware platform
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
#define OUT_CLK			26000000
#define LCDC_ACLK       150000000     //29 lcdc axi DMA

/* Timing */
#define H_PW		    8
#define H_BP			6
#define H_VD			480
#define H_FP			60

#define V_PW		    2
#define V_BP			12
#define V_VD			800
#define V_FP			4


#define LCD_WIDTH       480    //need modify
#define LCD_HEIGHT      800

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

	#define bits_9
	#ifdef bits_9  //9bits

	#define LCD_ILI9803_CMD(cmd)    		spi_write_9bit(0, cmd)
	#define LCD_ILI9803_Parameter(dat)    	spi_write_9bit(1, dat)
	#else  //16bits
	#define LCD_ILI9803_CMD(cmd)    		spi_write_16bit(0, cmd)
	#define LCD_ILI9803_Parameter(dat)    	spi_write_16bit(1, dat)
	#endif
	#define Lcd_EnvidOnOff(i)

#else

	#define bits_9 1
	#ifdef bits_9  //9bits
	#define LCD_ILI9803_CMD(cmd)
	#define LCD_ILI9803_Parameter(dat)
	#else  //16bits
	#define LCD_ILI9803_CMD(cmd)
	#define LCD_ILI9803_Parameter(dat)
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
    value |= (type << 8);
//    if(0 == type){
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
//    }

	for(i = 0; i < 9; i++)  //reg
	{

		CLK_CLR();
		DRVDelayUs(2);
		if(value & (1 << (8-i)))
        {
			TXD_SET();
		}
        else
        {
			TXD_CLR();
        }
		CLK_SET();
		DRVDelayUs(2);
	}

//	if(0 == type){
		CS_SET();
		CLK_CLR();
		TXD_CLR();
//	}

	DRVDelayUs(2);
    return 0;
}


int lcd_init(void)
{
    if(gLcd_info)
        gLcd_info->io_init();
    printk("*****lcd_init...*****\n");
/* reset lcd to start init lcd by software if there is no hardware reset circuit for the lcd */
#ifdef LCD_RST_PORT
	gpio_request(LCD_RST_PORT, NULL);
	LCD_RST_OUT();
	LCD_RST(1);
	msleep(1);
	LCD_RST(0);
	msleep(10);
	LCD_RST(1);
	msleep(120);

#endif

    TXD_OUT();
    CLK_OUT();
    CS_OUT();
    CS_SET();
    TXD_SET();
    CLK_SET();

	LCD_ILI9803_CMD(0xB1);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_CMD(0xB2);
	LCD_ILI9803_Parameter(0x10);
	LCD_ILI9803_Parameter(0xC7);
	LCD_ILI9803_CMD(0xB3);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_CMD(0xB4);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_CMD(0xB9);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_CMD(0xC3);
	LCD_ILI9803_Parameter(0x07);
	LCD_ILI9803_CMD(0xB2);
	LCD_ILI9803_Parameter(0x04);
	LCD_ILI9803_Parameter(0x0B);
	LCD_ILI9803_Parameter(0x0B);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x07);
	LCD_ILI9803_Parameter(0x04);
	LCD_ILI9803_CMD(0xC5);
	LCD_ILI9803_Parameter(0x6E);
	LCD_ILI9803_CMD(0xC2);
	LCD_ILI9803_Parameter(0x20);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x10);
	msleep(20);
	LCD_ILI9803_CMD(0xC8);
	LCD_ILI9803_Parameter(0xA3);
	LCD_ILI9803_CMD(0xC9);
	LCD_ILI9803_Parameter(0x32);
	LCD_ILI9803_Parameter(0x06);
	LCD_ILI9803_CMD(0xD7);
	LCD_ILI9803_Parameter(0x03);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x0F);
	LCD_ILI9803_Parameter(0x0F);
	LCD_ILI9803_CMD(0xCF);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x08);
	LCD_ILI9803_CMD(0xB6);
	LCD_ILI9803_Parameter(0x20);
	LCD_ILI9803_Parameter(0xC2);
	LCD_ILI9803_Parameter(0xFF);
	LCD_ILI9803_Parameter(0x04);
	LCD_ILI9803_CMD(0xEA);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_CMD(0x2A);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x01);
	LCD_ILI9803_Parameter(0xDF);
	LCD_ILI9803_CMD(0x2B);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x03);
	LCD_ILI9803_Parameter(0xEF);
	LCD_ILI9803_CMD(0xB0);
	LCD_ILI9803_Parameter(0x01);
	LCD_ILI9803_CMD(0x0C);
	LCD_ILI9803_Parameter(0x50);
	LCD_ILI9803_CMD(0x36);
	LCD_ILI9803_Parameter(0x48);
	LCD_ILI9803_CMD(0x3A);
	LCD_ILI9803_Parameter(0x66);
	LCD_ILI9803_CMD(0xE0);
	LCD_ILI9803_Parameter(0x05);
	LCD_ILI9803_Parameter(0x07);
	LCD_ILI9803_Parameter(0x0B);
	LCD_ILI9803_Parameter(0x14);
	LCD_ILI9803_Parameter(0x11);
	LCD_ILI9803_Parameter(0x14);
	LCD_ILI9803_Parameter(0x0A);
	LCD_ILI9803_Parameter(0x07);
	LCD_ILI9803_Parameter(0x04);
	LCD_ILI9803_Parameter(0x0B);
	LCD_ILI9803_Parameter(0x02);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x04);
	LCD_ILI9803_Parameter(0x33);
	LCD_ILI9803_Parameter(0x36);
	LCD_ILI9803_Parameter(0x1F);
	LCD_ILI9803_CMD(0xE1);
	LCD_ILI9803_Parameter(0x1F);
	LCD_ILI9803_Parameter(0x36);
	LCD_ILI9803_Parameter(0x33);
	LCD_ILI9803_Parameter(0x04);
	LCD_ILI9803_Parameter(0x00);
	LCD_ILI9803_Parameter(0x02);
	LCD_ILI9803_Parameter(0x0B);
	LCD_ILI9803_Parameter(0x04);
	LCD_ILI9803_Parameter(0x07);
	LCD_ILI9803_Parameter(0x0A);
	LCD_ILI9803_Parameter(0x14);
	LCD_ILI9803_Parameter(0x11);
	LCD_ILI9803_Parameter(0x14);
	LCD_ILI9803_Parameter(0x0B);
	LCD_ILI9803_Parameter(0x07);
	LCD_ILI9803_Parameter(0x05);
	LCD_ILI9803_CMD(EXIT_SLEEP_MODE);
	msleep(70);
	LCD_ILI9803_CMD(SET_DISPLAY_ON);
	msleep(10);
	LCD_ILI9803_CMD(WRITE_MEMORY_START);

    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;
}

extern void rk29_lcd_spim_spin_lock(void);
extern void rk29_lcd_spim_spin_unlock(void);
int lcd_standby(u8 enable)
{
	rk29_lcd_spim_spin_lock();
	if(gLcd_info)
        gLcd_info->io_init();

	if(enable) {
		LCD_ILI9803_CMD(ENTER_SLEEP_MODE);
		msleep(150);
		printk("lcd enter sleep mode\n");
	} else {
		LCD_ILI9803_CMD(EXIT_SLEEP_MODE);
		msleep(150);
		printk("lcd exit sleep mode\n");
	}

    if(gLcd_info)
        gLcd_info->io_deinit();
	rk29_lcd_spim_spin_unlock();

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



