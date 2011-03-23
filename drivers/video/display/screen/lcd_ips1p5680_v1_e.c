/*
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * author: hhb@rock-chips.com
 * creat date: 2011-03-07 
 * route:drivers/video/display/screen/lcd_ips1p5680_v1_e.c - driver for rk29 phone sdk
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
#include "screen.h"

/* Base */
#define OUT_TYPE		SCREEN_MCU
#define OUT_FACE		OUT_P565

/* Timing */
#define H_PW			1
#define H_BP		    1
#define H_VD			320
#define H_FP			5

#define V_PW			1
#define V_BP			1
#define V_VD			480
#define V_FP			1

#define LCD_WIDTH       320           //need modify
#define LCD_HEIGHT      480

#define LCDC_ACLK       150000000     //29 lcdc axi DMA ÆµÂÊ

#define P_WR            27
#define USE_FMARK       0             //2  ÊÇ·ñÊ¹ÓÃFMK (0:²»Ö§³Ö 1:ºáÆÁÖ§³Ö 2:ºáÊúÆÁ¶ŒÖ§³Ö)
#define FRMRATE         60            //MCUÆÁµÄË¢ÐÂÂÊ (FMKÓÐÐ§Ê±ÓÃ)


/* Other */
#define DCLK_POL		0
#define SWAP_RB			0


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


/* initialize the lcd registers to make it function noamally*/

int lcd_init(void)
{
    int i =0;
    mcu_ioctl(MCU_SETBYPASS, 1);
    msleep(5);
    mcu_ioctl(MCU_WRCMD, SET_ADDRESS_MODE);  //set address normal mode
    mcu_ioctl(MCU_WRDATA, 0);
    mcu_ioctl(MCU_WRCMD, SET_PIXEL_FORMAT);  //set 16 bits per pixel
    mcu_ioctl(MCU_WRDATA, 0x55);
    mcu_ioctl(MCU_WRCMD, EXIT_SLEEP_MODE);  //set lcd exit sleep mode,because the lcd is in sleep mode when power on
    msleep(1000*6 / FRMRATE + 10);            //wait for about 6 frames' time
    mcu_ioctl(MCU_WRCMD, SET_DISPLAY_ON);   //set display on
    msleep(1000/FRMRATE);
    
    /*init lcd internal ram,so lcd won't display randomly*/
    mcu_ioctl(MCU_WRCMD, SET_COLUMN_ADDRESS);
	mcu_ioctl(MCU_WRDATA, 0);
	mcu_ioctl(MCU_WRDATA, 0);
	mcu_ioctl(MCU_WRDATA, (LCD_WIDTH >> 8) & 0x0003);
	mcu_ioctl(MCU_WRDATA, LCD_WIDTH & 0x00ff);
	msleep(10);
	mcu_ioctl(MCU_WRCMD, SET_PAGE_ADDRESS);
	mcu_ioctl(MCU_WRDATA, 0);
	mcu_ioctl(MCU_WRDATA, 0);
	mcu_ioctl(MCU_WRDATA, (LCD_HEIGHT >> 8) & 0x0003);
	mcu_ioctl(MCU_WRDATA, LCD_HEIGHT & 0x00ff);
	msleep(10);
	mcu_ioctl(MCU_WRCMD, WRITE_MEMORY_START);

	for(i = 0; i < LCD_WIDTH*LCD_HEIGHT; i++)
	{
	        mcu_ioctl(MCU_WRDATA, 0x00000000);
	}

    mcu_ioctl(MCU_SETBYPASS, 0);
    return 0;
}

/* set lcd to sleep mode or not */

int lcd_standby(u8 enable)
{
    mcu_ioctl(MCU_SETBYPASS, 1);

    if(enable) {
        mcu_ioctl(MCU_WRCMD, ENTER_SLEEP_MODE);
    } else {
        mcu_ioctl(MCU_WRCMD, EXIT_SLEEP_MODE);
    }

    mcu_ioctl(MCU_SETBYPASS, 0);

    return 0;
}

/* set lcd to write memory mode, so the lcdc of RK29xx can send the fb content to the lcd internal ram in hold mode*/

int lcd_refresh(u8 arg)
{
    mcu_ioctl(MCU_SETBYPASS, 1);

    switch(arg)
    {
		case REFRESH_PRE:   //start to write the image data to lcd ram
			mcu_ioctl(MCU_WRCMD, SET_COLUMN_ADDRESS);  //set
			mcu_ioctl(MCU_WRDATA, 0);
			mcu_ioctl(MCU_WRDATA, 0);
			mcu_ioctl(MCU_WRDATA, (LCD_WIDTH >> 8) & 0x0003);
			mcu_ioctl(MCU_WRDATA, LCD_WIDTH & 0x00ff);
			msleep(10);
			mcu_ioctl(MCU_WRCMD, SET_PAGE_ADDRESS);
			mcu_ioctl(MCU_WRDATA, 0);
			mcu_ioctl(MCU_WRDATA, 0);
			mcu_ioctl(MCU_WRDATA, (LCD_HEIGHT >> 8) & 0x0003);
			mcu_ioctl(MCU_WRDATA, LCD_HEIGHT & 0x00ff);
			msleep(10);
			mcu_ioctl(MCU_WRCMD, WRITE_MEMORY_START);
			break;

		case REFRESH_END:   //set display on
			mcu_ioctl(MCU_WRCMD, SET_DISPLAY_ON);
			break;

		default:
			break;
    }

    mcu_ioctl(MCU_SETBYPASS, 0);

    return 0;
}


/* not used */

int lcd_scandir(u16 dir)
{
    mcu_ioctl(MCU_SETBYPASS, 1);
    
//   mcu_ioctl(MCU_WRCMD, SET_DISPLAY_OFF);
   
    mcu_ioctl(MCU_SETBYPASS, 0);
    return 0;
}


/* not used */

int lcd_disparea(u8 area)
{
    mcu_ioctl(MCU_SETBYPASS, 1);
    mcu_ioctl(MCU_SETBYPASS, 0);
    return (0);
}


/* set real information about lcd which we use in this harware platform */

void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info)
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
	screen->left_margin = H_BP;
	screen->right_margin = H_FP;
	screen->hsync_len = H_PW;
	screen->upper_margin = V_BP;
	screen->lower_margin = V_FP;
	screen->vsync_len = V_PW;

	screen->mcu_wrperiod = P_WR;
	screen->mcu_usefmk = USE_FMARK;
    screen->mcu_frmrate = FRMRATE;

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
    screen->scandir = lcd_scandir;
    screen->refresh = lcd_refresh;
    screen->disparea = lcd_disparea;
}






