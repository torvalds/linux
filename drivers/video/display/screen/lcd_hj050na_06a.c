/*
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * author: hhb@rock-chips.com
 * creat date: 2012-04-19
 * route:drivers/video/display/screen/lcd_hj050na_06a.c
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
#include <linux/rk_fb.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include<linux/rk_screen.h>


/* Base */
#define OUT_TYPE	    SCREEN_RGB

#define OUT_FACE	   OUT_D888_P666// OUT_D888_P666 //OUT_P888


#define OUT_CLK	         50000000    //50MHz
#define LCDC_ACLK        300000000   //29 lcdc axi DMA

/* Timing */
#define H_PW			5
#define H_BP			50
#define H_VD			640
#define H_FP			130

#define V_PW			3
#define V_BP			20//23
#define V_VD			960
#define V_FP			12

#define LCD_WIDTH       71    //uint mm the lenth of lcd active area
#define LCD_HEIGHT      106
/* Other */
#define DCLK_POL		0
#define SWAP_RB			0


#define CONFIG_DEEP_STANDBY_MODE 0


/* define spi write command and data interface function */

#define SIMULATION_SPI 1
#ifdef SIMULATION_SPI

    #define TXD_PORT        gLcd_info->txd_pin
	#define CLK_PORT        gLcd_info->clk_pin
	#define CS_PORT         gLcd_info->cs_pin
	#define LCD_RST_PORT    gLcd_info->reset_pin

	#define CS_OUT()        gpio_direction_output(CS_PORT, 0)
	#define CS_SET()        gpio_set_value(CS_PORT, GPIO_HIGH)
	#define CS_CLR()        gpio_set_value(CS_PORT, GPIO_LOW)
	#define CLK_OUT()       gpio_direction_output(CLK_PORT, 0)
	#define CLK_SET()       gpio_set_value(CLK_PORT, GPIO_HIGH)
	#define CLK_CLR()       gpio_set_value(CLK_PORT, GPIO_LOW)
	#define TXD_OUT()       gpio_direction_output(TXD_PORT, 0)
	#define TXD_SET()       gpio_set_value(TXD_PORT, GPIO_HIGH)
	#define TXD_CLR()       gpio_set_value(TXD_PORT, GPIO_LOW)
    #define LCD_RST_OUT()   gpio_direction_output(LCD_RST_PORT, 0)
    #define LCD_RST(i)      gpio_set_value(LCD_RST_PORT, i)

	#define bits_9
	#ifdef bits_9  //9bits
	#define Write_ADDR(cmd)    spi_write_9bit(0, cmd)
	#define Write_DATA(dat)    spi_write_9bit(0x100, dat)
	#else  //16bits
	#define Write_ADDR(cmd)    spi_write_16bit(0, cmd)
	#define Write_DATA(dat)    spi_write_16bit(1, dat)
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


static struct rk29lcd_info *gLcd_info = NULL;
int lcd_init(void);
int lcd_standby(u8 enable);


/* spi write a data frame,type mean command or data */
int spi_write_9bit(u32 type, u32 value)
{
//    if(type != 0 && type != 1)
//    	return -1;
    /*make a data frame of 9 bits,the 8th bit  0:mean command,1:mean data*/
    value &= 0xff;
    value |= type;
    type = 9;
	CS_CLR();
	//udelay(2);
	while(type--) {
		CLK_CLR();
		if(value & 0x100)
			TXD_SET();
        else
			TXD_CLR();
		value <<= 1;
		//udelay(2);
		CLK_SET();
		//udelay(2);
	}
    CS_SET();
    TXD_SET();

    return 0;
}


int lcd_init(void)
{
    if(gLcd_info)
        gLcd_info->io_init();

    printk("lcd hj050a_06a...\n");
#if 1
	gpio_direction_output(LCD_RST_PORT, 0);
	usleep_range(2*1000, 3*1000);
	gpio_set_value(LCD_RST_PORT, 1);
	usleep_range(7*1000, 7*1000);
#endif

//    Write_ADDR(0x0001);     // Software Reset
//    msleep(100);

    Write_ADDR(0x0011);     // Sleep Out
    msleep(60);

//<<<<<<<<<<<<<<<MANUFACTURE COMMAND ACCESS PROTECT>>>>>>>>>>>>>>>
    Write_ADDR(0x00B0);     //Manufacture Command Access Protect
    Write_DATA(0x0004);

//<<<<<<<<<<<<Source Output Number>>>>>>>>>>>>>
    Write_ADDR(0x00B3);     //Number of Source outputs & Pixel Format setting
    Write_DATA(0x0000);     //PSEL[2:0] = 640 RGB

//<<<<<<<<<<<<DSI Control>>>>>>>>>>>>>>>>>
    Write_ADDR(0x00B6);
    Write_DATA(0x0052);
    Write_DATA(0x0083);
    Write_DATA(0x0045);
    Write_DATA(0x0000);

//<<<<<<<<<<<<BACK LIGHT CONTROL SET>>>>>>>>>>>>
    Write_ADDR(0x00B8);     //Back Light Control(1)
    Write_DATA(0x0000);     //P1: CABCON = 0;
    Write_DATA(0x001A);     //P2: SSD_THRE = 1A;
    Write_DATA(0x0018);     //P3: SD_THRE = 18;
    Write_DATA(0x0002);     //P4: IPK_INTPO = 02;
    Write_DATA(0x0040);     //P5: IPK_TRANS = 40;

    Write_ADDR(0x00BB);     //Back Light Control(1)
    Write_DATA(0x0000);     //LEDPWME[3] = 1,PWMWM[1] = 0,PWMON[0] = 0;
    Write_DATA(0x00FF);     //BDCV = FF;
    Write_DATA(0x0001);     //PWMDIC=1

//<<<<<<<<<<<<PANEL DRIVING SETTING>>>>>>>>>>>>
    Write_ADDR(0x00C0);     //PANEL DRIVING SETTING 1   (36h=00)
    Write_DATA(0x000B);     //BLREV[5:4];REV[3];UD[2]=0:forward;BGR[1]=1:RGB->BGR;SS=1:S1920->S1
    Write_DATA(0x00BF);     //NL[7:0]      NL = 3BF : 960 Line
    Write_DATA(0x0003);     //NL[10:8]
    Write_DATA(0x0011);     //VBP[5:0]  Vertical back porch
    Write_DATA(0x0002);     //DIV[3:0]
    Write_DATA(0x0009);     //PCDIVL[4:0]   PCLKD Low Period
    Write_DATA(0x0009);     //PCDIVH[4:0]   PCLKD High Period

    Write_ADDR(0x00C1);     //PANEL DRIVING SETTING 2
    Write_DATA(0x0000);     //GDS_MODE = 0 : GIP Ctrl(single scan)
    Write_DATA(0x0010);     //LINEINV[6:4]:2 Line inversion; MFPOL[1]:No Phase inversion; PNSER[0]:Spatial mode1
    Write_DATA(0x0004);     //SEQMODE[7]:Source Pre-charge Mode;    SEQGND[3:0]: GND Pre-charge 3clk
    Write_DATA(0x0088);     //SEQVN[7:4]:VCL pre-charge 2clk   ;SEQVP[3:0]:VCL pre-charge 2clk
    Write_DATA(0x001B);     //DPM[7:6]: ;GEQ2W[5:3]/GEQ1W[2:0]:Gate pre-charge
    Write_DATA(0x0001);     //SDT[5:0] = 8 : Source output delay
    Write_DATA(0x0060);     //PSEUDO_EN = 0;
    Write_DATA(0x0001);     //GEM

    Write_ADDR(0x00C3);     //PANEL DRIVING SETTING 4
    Write_DATA(0x0000);     //GIPPAT[6:4]:Pattern-1 ; GIPMOD[2:0]: GIP mode 1
    Write_DATA(0x0000);     //STPEOFF:normal  ;   FWBWOFF:normal    ;   T_GALH:normal
    Write_DATA(0x0021);     //GSPF[5:0]:    33clk
    Write_DATA(0x0021);     //GSPS[5:0]:    33clk
    Write_DATA(0x0000);     //VFSTEN[7]: NO END Pulse ; VFST[4:0]: 0 line
    Write_DATA(0x0060);     //FL1[6]:   ; GLOL[5:4]:    ; VGSET[3]:  ; GIPSIDE=0:Single drive mode ; GOVERSEL=0:Overlap ; GIPSEL=0:8-phase clk
    Write_DATA(0x0003);     //VBPEX[6]: ; STVG[5:3]:    ; STVGA[2:0]:
    Write_DATA(0x0000);     //ACBF[7:6]:    ; ACF[5:4]: ; ACBR[3:2]:    ; ACR[1:0]:
    Write_DATA(0x0000);     //ACBF2[7:6]:   ; ACF2[5:4]:    ; ACBR2[3:2]:    ; ACR2[1:0]:
    Write_DATA(0x0090);     //9xH  ACCYC[3:2]:  ; ACFIX[1;0]:
    Write_DATA(0x001D);     //GOFF_L[7:0]
    Write_DATA(0x00FE);     //GOFF_L[15:8]
    Write_DATA(0x0003);     //GOFF_L[17:16]
    Write_DATA(0x001D);     //GOFF_R[7:0]
    Write_DATA(0x00FE);     //GOFF_R[15:8]
    Write_DATA(0x0003);     //GOFF_R[17:16]

//<<<<<<<<<<TCON Unusual Operation Setting>>>>>>>>>>
    Write_ADDR(0x00C7);     //TCON Unusual Operation Setting
    Write_DATA(0x0000);     //P1:
    Write_DATA(0x0000);     //P2:
    Write_DATA(0x0000);     //P3:
    Write_DATA(0x0000);     //P4:
    Write_DATA(0x0000);     //P5:
    Write_DATA(0x0000);     //P6:
    Write_DATA(0x0000);     //P7:
    Write_DATA(0x0000);     //P8:
    Write_DATA(0x0000);     //P9:
    Write_DATA(0x0000);     //P10:
    Write_DATA(0x0000);     //P11:
    Write_DATA(0x0000);     //P12:
    Write_DATA(0x0000);     //P13:
    Write_DATA(0x0000);     //P14:

//<<<<<<<<<<Gamma Setting>>>>>>>>>>
    Write_ADDR(0x00C8);     //Gamma Setting
    Write_DATA(0x0003);
    Write_DATA(0x000F);
    Write_DATA(0x0015);
    Write_DATA(0x0018);
    Write_DATA(0x001A);
    Write_DATA(0x0023);
    Write_DATA(0x0025);
    Write_DATA(0x0024);
    Write_DATA(0x0021);
    Write_DATA(0x001E);
    Write_DATA(0x0015);
    Write_DATA(0x000A);

    Write_DATA(0x0003);
    Write_DATA(0x000F);
    Write_DATA(0x0015);
    Write_DATA(0x0018);
    Write_DATA(0x001A);
    Write_DATA(0x0023);
    Write_DATA(0x0025);
    Write_DATA(0x0024);
    Write_DATA(0x0021);
    Write_DATA(0x001E);
    Write_DATA(0x0015);
    Write_DATA(0x000A);

//<<<<<<<<<<COLOR ENHANCEMENT SETTING>>>>>>>>>>
    Write_ADDR(0x00C9);     //COLOR ENHANCEMENT SETTING
    Write_DATA(0x0000);     //CE_ON = 0;
    Write_DATA(0x0080);
    Write_DATA(0x0080);
    Write_DATA(0x0080);
    Write_DATA(0x0080);
    Write_DATA(0x0080);
    Write_DATA(0x0080);
    Write_DATA(0x0080);
    Write_DATA(0x0080);
    Write_DATA(0x0000);
    Write_DATA(0x0000);
    Write_DATA(0x0002);
    Write_DATA(0x0080);

//<<<<<<<<<<<<<<<<<<<<POWER SETTING>>>>>>>>>>>>>>>>>>>
    Write_ADDR(0x00D0);     //POWER SETTING(CHARGE PUMP)
    Write_DATA(0x0054);     //P1:VC1 = 7; DC23 = 4
    Write_DATA(0x0019);     //P2:BT3 = 2; BT2 = 1           09
    Write_DATA(0x00DD);     //P3:VLMT1M = D; VLMT1 = D
    Write_DATA(0x0016);     //P4:VC3 = B; VC2 =B            3B
    Write_DATA(0x0092);     //P5:VLMT2B = 0; VLMT2 = 0A
    Write_DATA(0x00A1);     //P6:VLMT3B = 0; VLMT3 = 0F   A1
    Write_DATA(0x0000);     //P7:VBSON = 0; VBS = 00
    Write_DATA(0x00C0);     //P8:VGGON = 0; LVGLON = 0; VC6 = 0
    Write_DATA(0x00CC);     //P9:DC56 = ?

    Write_ADDR(0x00D1);     //POWER SETTING(SWITCHING REGULATOR)
    Write_DATA(0x004D);     //P1:VDF1 = 4; VDF0 = D
    Write_DATA(0x0024);     //P2:DC1CLKEN = 0; DC1MCLKEN = 0; VDF2 =4
    Write_DATA(0x0034);     //P3:VDWS2 = 3; VDWS1 = 4
    Write_DATA(0x0055);     //P4:VDW12 = 5; VDW11 = 5
    Write_DATA(0x0055);     //P5:VDW14 = 5; VDW13 = 5
    Write_DATA(0x0077);     //P6:VDW22 = 7; VDW21 = 7
    Write_DATA(0x0077);     //P7:VDW24 = 7; VDW23 = 7
    Write_DATA(0x0006);     //P8:LSWPH = 6

//<<<<<<<<<<<<<<<VPLVL/VNLVL SETTING>>>>>>>>>>>>>>>
    Write_ADDR(0x00D5);     //VPLVL/VNLVL SETTING
    Write_DATA(0x0020);     //P1:PVH = 24
    Write_DATA(0x0020);     //P2:NVH = 24

//<<<<<<<<<<<<<<<DSI Setting>>>>>>>>>>>>>>>>
    Write_ADDR(0x00D6);
    Write_DATA(0x00A8);

//<<<<<<<<<<<<<<<VCOMDC SETTING>>>>>>>>>>>>>>>
    Write_ADDR(0x00DE);     //VCOMDC SETTING
    Write_DATA(0x0003);     //P1:WCVDCB.[1] = 1; WCVDCF.[0] = 1
    Write_DATA(0x005A);     //P2:VDCF.[7:0] = ?     //57
    Write_DATA(0x005A);     //P3:VDCB.[7:0] = ?     //57


//<<<<<<<<<<<<<<<MANUFACTURE COMMAND ACCESS PROTECT>>>>>>>>>>>>>>>
    Write_ADDR(0x00B0);     //MANUFACTURE COMMAND ACCESS PROTECT
    Write_DATA(0x0003);     //
    msleep(17);

    Write_ADDR(0x0036);     //
    Write_DATA(0x0000);     //
    msleep(17);
    Write_ADDR(0x003A);     //
    Write_DATA(0x0060);     //
    msleep(17);
    Write_ADDR(0x0029);     //

    if(gLcd_info)
        gLcd_info->io_deinit();

    return 0;

}



int lcd_standby(u8 enable)
{
	if(enable) {
	    if(gLcd_info)
	        gLcd_info->io_init();
		printk("lcd_standby...\n");
		Write_ADDR(0x0028);     //set Display Off
		Write_ADDR(0x0010);		//enter sleep mode
		msleep(50);			//wait at least 3 frames time
#if 1
		Write_ADDR(0x00b0);
		Write_DATA(0x0004);
		Write_ADDR(0x00b1);
		Write_DATA(0x0001);
		msleep(1);				//wait at least 1ms
		gpio_direction_output(LCD_RST_PORT, 0);
#endif

	    if(gLcd_info)
	        gLcd_info->io_deinit();

	} else {
		lcd_init();
	}

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

    if(LCD_RST_PORT){
    	if (gpio_request(LCD_RST_PORT, NULL) != 0) {
    		gpio_free(LCD_RST_PORT);
    		printk("%s: request LCD_RST_PORT error\n", __func__);
    	}
    }
}
