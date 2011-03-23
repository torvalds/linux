/*
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * author: hhb@rock-chips.com
 * creat date: 2011-03-11
 * route:drivers/video/display/screen/lcd_mcu_tft480800_25_e.c - driver for rk29 phone sdk
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
#define OUT_FACE		OUT_P888

/* Timing */
#define H_PW			1
#define H_BP		    1
#define H_VD			480
#define H_FP			5

#define V_PW			1
#define V_BP			1
#define V_VD			800
#define V_FP			1

#define LCD_WIDTH       480           //need modify
#define LCD_HEIGHT      800

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


#define WMLCDCOM(command)       mcu_ioctl(MCU_WRCMD,command)
#define WMLCDDATA(data)         mcu_ioctl(MCU_WRDATA,data)




/* initialize the lcd registers to make it function noamally*/

int lcd_init(void)
{
    int k = 0;
    mcu_ioctl(MCU_SETBYPASS, 1);
    
#if 1  //HX8369-A

    WMLCDCOM(0xB9);  // SET password
	WMLCDDATA(0xFF);
	WMLCDDATA(0x83);
	WMLCDDATA(0x69);

	WMLCDCOM(0xB0);  //Enable internal oscillator
	WMLCDDATA(0x01);
    WMLCDDATA(0x0B);


    WMLCDCOM(0xB1);  //Set Power
    WMLCDDATA(0x85);
	WMLCDDATA(0x00);
	WMLCDDATA(0x34);
	WMLCDDATA(0x0A);
	WMLCDDATA(0x00);
	WMLCDDATA(0x0F);
	WMLCDDATA(0x0F);
	WMLCDDATA(0x2A);
	WMLCDDATA(0x32);
	WMLCDDATA(0x3F);
	WMLCDDATA(0x3F);
	WMLCDDATA(0x01); //update VBIAS
	WMLCDDATA(0x23);
	WMLCDDATA(0x01);
	WMLCDDATA(0xE6);
	WMLCDDATA(0xE6);
	WMLCDDATA(0xE6);
	WMLCDDATA(0xE6);
	WMLCDDATA(0xE6);


	WMLCDCOM(0xB2);  // SET Display  480x800
	WMLCDDATA(0x00);
	WMLCDDATA(0x20);
	WMLCDDATA(0x05);
	WMLCDDATA(0x05);
	WMLCDDATA(0x70);  //70
	WMLCDDATA(0x00); //00
	WMLCDDATA(0xFF); //FF
	WMLCDDATA(0x00);
	WMLCDDATA(0x00);
	WMLCDDATA(0x00);
	WMLCDDATA(0x00); //1
	WMLCDDATA(0x03);
	WMLCDDATA(0x03);
	WMLCDDATA(0x00);
	WMLCDDATA(0x01);



	WMLCDCOM(0xB4);    // SET Display  480x800
	WMLCDDATA(0x00);   //00
	WMLCDDATA(0x18);   //18
	WMLCDDATA(0x80);   //80
	WMLCDDATA(0x06);
	WMLCDDATA(0x02);

	WMLCDCOM(0xB6);    // SET VCOM
	WMLCDDATA(0x3A);   // Update VCOM
	WMLCDDATA(0x3A);


	/************CABC test ***************/

	WMLCDCOM(0X51);//Write Display Brightness
	WMLCDDATA(0Xff);//DBV[7:0]=0XE4
	msleep(20);

	/*
	WMLCDCOM(0XC9);//SETCABC
	WMLCDDATA(0X5F);//PWM_DIV="110" PWM_CLK 64·ÖÆµ INVPULS="1"
	WMLCDDATA(0X7F);//WMLCDDATA(0X7F);
	WMLCDDATA(0X20);//PWM_EPERIOD
	WMLCDDATA(0X00);//SAVEPOWER[6:0]
	WMLCDDATA(0X20);//DIM_FRAM[6:0]
	WMLCDDATA(0X00);//
	WMLCDDATA(0X03);//CABC_FLM
	WMLCDDATA(0X20);//
	msleep(20);
	*/

	WMLCDCOM(0X53);//WRITE CTRL DISPLAY
	WMLCDDATA(0X24);//WMLCDDATA(0X26)  BCTRL="1" BL="1" DD="1"/"0"
	msleep(20);

	WMLCDCOM(0X55);
	WMLCDDATA(0X02);//STILL PICTURE
	msleep(20);

	//WMLCDCOM(0X5E);//Write CABC minimum brightness (5Eh)
	//WMLCDDATA(0X00);//CMB[7:0=0X00
	//msleep(20);


    /***************************************/

    WMLCDCOM(0x2A);  //set window
    WMLCDDATA(0x00);
	WMLCDDATA(0x00);
	WMLCDDATA(0x0);
	WMLCDDATA(0xF0);

	WMLCDCOM(0x2B);
    WMLCDDATA(0x00);
	WMLCDDATA(0x00);
	WMLCDDATA(0x01);
	WMLCDDATA(0x40);

	WMLCDCOM(0xD5);  //Set GIP
	WMLCDDATA(0x00);
	WMLCDDATA(0x04);
	WMLCDDATA(0x03);
	WMLCDDATA(0x00);
	WMLCDDATA(0x01);
	WMLCDDATA(0x05);
	WMLCDDATA(0x28);
    WMLCDDATA(0x70);
	WMLCDDATA(0x01);
	WMLCDDATA(0x03);
	WMLCDDATA(0x00);
	WMLCDDATA(0x00);
	WMLCDDATA(0x40);
	WMLCDDATA(0x06);
	WMLCDDATA(0x51);
	WMLCDDATA(0x07);
	WMLCDDATA(0x00);
    WMLCDDATA(0x00);
	WMLCDDATA(0x41);
	WMLCDDATA(0x06);
	WMLCDDATA(0x50);
	WMLCDDATA(0x07);
	WMLCDDATA(0x07);
	WMLCDDATA(0x0F);
	WMLCDDATA(0x04);
	WMLCDDATA(0x00);


    //Gamma2.2
	WMLCDCOM(0xE0);
	WMLCDDATA(0x00);
	WMLCDDATA(0x13);
	WMLCDDATA(0x19);
	WMLCDDATA(0x38);
	WMLCDDATA(0x3D);
	WMLCDDATA(0x3F);
	WMLCDDATA(0x28);
	WMLCDDATA(0x46);
	WMLCDDATA(0x07);
	WMLCDDATA(0x0D);
	WMLCDDATA(0x0E);
	WMLCDDATA(0x12);
	WMLCDDATA(0x15);
	WMLCDDATA(0x12);
	WMLCDDATA(0x14);
	WMLCDDATA(0x0F);
	WMLCDDATA(0x17);
	WMLCDDATA(0x00);
	WMLCDDATA(0x13);
	WMLCDDATA(0x19);
	WMLCDDATA(0x38);
	WMLCDDATA(0x3D);
	WMLCDDATA(0x3F);
	WMLCDDATA(0x28);
	WMLCDDATA(0x46);
	WMLCDDATA(0x07);
	WMLCDDATA(0x0D);
	WMLCDDATA(0x0E);
	WMLCDDATA(0x12);
	WMLCDDATA(0x15);
	WMLCDDATA(0x12);
	WMLCDDATA(0x14);
	WMLCDDATA(0x0F);
	WMLCDDATA(0x17);
	msleep(10);

	//DGC Setting
	WMLCDCOM(0xC1);
	WMLCDDATA(0x01);

	//R
	WMLCDDATA(0x00);
	WMLCDDATA(0x04);
	WMLCDDATA(0x11);
	WMLCDDATA(0x19);
	WMLCDDATA(0x20);
	WMLCDDATA(0x29);
	WMLCDDATA(0x30);
	WMLCDDATA(0x37);
	WMLCDDATA(0x40);
	WMLCDDATA(0x4A);
	WMLCDDATA(0x52);
	WMLCDDATA(0x59);
	WMLCDDATA(0x60);
	WMLCDDATA(0x68);
	WMLCDDATA(0x70);
	WMLCDDATA(0x79);
	WMLCDDATA(0x81);
	WMLCDDATA(0x89);
	WMLCDDATA(0x91);
	WMLCDDATA(0x99);
	WMLCDDATA(0xA1);
	WMLCDDATA(0xA8);
	WMLCDDATA(0xB0);
	WMLCDDATA(0xB8);
	WMLCDDATA(0xC1);
	WMLCDDATA(0xC9);
	WMLCDDATA(0xD0);
	WMLCDDATA(0xD8);
	WMLCDDATA(0xE1);
	WMLCDDATA(0xE8);
	WMLCDDATA(0xF1);
	WMLCDDATA(0xF8);
	WMLCDDATA(0xFF);
	WMLCDDATA(0x31);
	WMLCDDATA(0x9C);
	WMLCDDATA(0x57);
	WMLCDDATA(0xED);
	WMLCDDATA(0x57);
	WMLCDDATA(0x7F);
	WMLCDDATA(0x61);
	WMLCDDATA(0xAD);
	WMLCDDATA(0xC0);
//G
	WMLCDDATA(0x00);
	WMLCDDATA(0x04);
	WMLCDDATA(0x11);
	WMLCDDATA(0x19);
	WMLCDDATA(0x20);
	WMLCDDATA(0x29);
	WMLCDDATA(0x30);
	WMLCDDATA(0x37);
	WMLCDDATA(0x40);
	WMLCDDATA(0x4A);
	WMLCDDATA(0x52);
	WMLCDDATA(0x59);
	WMLCDDATA(0x60);
	WMLCDDATA(0x68);
	WMLCDDATA(0x70);
	WMLCDDATA(0x79);
	WMLCDDATA(0x81);
	WMLCDDATA(0x89);
	WMLCDDATA(0x91);
	WMLCDDATA(0x99);
	WMLCDDATA(0xA1);
	WMLCDDATA(0xA8);
	WMLCDDATA(0xB0);
	WMLCDDATA(0xB8);
	WMLCDDATA(0xC1);
	WMLCDDATA(0xC9);
	WMLCDDATA(0xD0);
	WMLCDDATA(0xD8);
	WMLCDDATA(0xE1);
	WMLCDDATA(0xE8);
	WMLCDDATA(0xF1);
	WMLCDDATA(0xF8);
	WMLCDDATA(0xFF);
	WMLCDDATA(0x31);
	WMLCDDATA(0x9C);
	WMLCDDATA(0x57);
	WMLCDDATA(0xED);
	WMLCDDATA(0x57);
	WMLCDDATA(0x7F);
	WMLCDDATA(0x61);
	WMLCDDATA(0xAD);
	WMLCDDATA(0xC0);
    //B
	WMLCDDATA(0x00);
	WMLCDDATA(0x04);
	WMLCDDATA(0x11);
	WMLCDDATA(0x19);
	WMLCDDATA(0x20);
	WMLCDDATA(0x29);
	WMLCDDATA(0x30);
	WMLCDDATA(0x37);
	WMLCDDATA(0x40);
	WMLCDDATA(0x4A);
	WMLCDDATA(0x52);
	WMLCDDATA(0x59);
	WMLCDDATA(0x60);
	WMLCDDATA(0x68);
	WMLCDDATA(0x70);
	WMLCDDATA(0x79);
	WMLCDDATA(0x81);
	WMLCDDATA(0x89);
	WMLCDDATA(0x91);
	WMLCDDATA(0x99);
	WMLCDDATA(0xA1);
	WMLCDDATA(0xA8);
	WMLCDDATA(0xB0);
	WMLCDDATA(0xB8);
	WMLCDDATA(0xC1);
	WMLCDDATA(0xC9);
	WMLCDDATA(0xD0);
	WMLCDDATA(0xD8);
	WMLCDDATA(0xE1);
	WMLCDDATA(0xE8);
	WMLCDDATA(0xF1);
	WMLCDDATA(0xF8);
	WMLCDDATA(0xFF);
	WMLCDDATA(0x31);
	WMLCDDATA(0x9C);
	WMLCDDATA(0x57);
	WMLCDDATA(0xED);
	WMLCDDATA(0x57);
	WMLCDDATA(0x7F);
	WMLCDDATA(0x61);
	WMLCDDATA(0xAD);
	WMLCDDATA(0xC0);
	WMLCDCOM(0x2D);//Look up table

	for(k = 0; k < 64; k++) //RED
	{
        WMLCDDATA(8*k);
	}
	for(k = 0; k < 64; k++) //GREEN
	{
	    WMLCDDATA(4*k);
	}
	for(k = 0; k < 64; k++) //BLUE
	{
        WMLCDDATA(8*k);
	}

	msleep(10);
	WMLCDCOM(SET_PIXEL_FORMAT);   //pixel format setting
	WMLCDDATA(0x77);

	WMLCDCOM(EXIT_SLEEP_MODE);
	msleep(120);

	WMLCDCOM(SET_DISPLAY_ON);     //Display on
	WMLCDCOM(WRITE_MEMORY_START);

#endif

    mcu_ioctl(MCU_SETBYPASS, 0);
    return 0;
}

/* set lcd to sleep mode or not */

int lcd_standby(u8 enable)
{
    mcu_ioctl(MCU_SETBYPASS, 1);

    if(enable) {
        mcu_ioctl(MCU_WRCMD, ENTER_SLEEP_MODE);
        msleep(10);
    } else {
        mcu_ioctl(MCU_WRCMD, EXIT_SLEEP_MODE);
        msleep(20);
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
			mcu_ioctl(MCU_WRDATA, (LCD_WIDTH >> 8) & 0x00ff);
			mcu_ioctl(MCU_WRDATA, LCD_WIDTH & 0x00ff);
			msleep(1);
			mcu_ioctl(MCU_WRCMD, SET_PAGE_ADDRESS);
			mcu_ioctl(MCU_WRDATA, 0);
			mcu_ioctl(MCU_WRDATA, 0);
			mcu_ioctl(MCU_WRDATA, (LCD_HEIGHT >> 8) & 0x00ff);
			mcu_ioctl(MCU_WRDATA, LCD_HEIGHT & 0x00ff);
			msleep(1);
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

//  mcu_ioctl(MCU_WRCMD, SET_DISPLAY_OFF);

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






