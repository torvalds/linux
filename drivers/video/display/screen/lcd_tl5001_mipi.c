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
#include <linux/rk_screen.h>
#include <linux/ktime.h>
#include "../transmitter/tc358768.h"

/* Base */
#define OUT_TYPE	    SCREEN_RGB
#define OUT_FACE	    OUT_P888
#define BYTE_PP	    	3    //bytes per pixel


#define OUT_CLK	         65000000  //  in fact it is 61384615
#define LCDC_ACLK        300000000

/* Timing */
#define H_PW			10
#define H_BP			20
#define H_VD			720
#define H_FP			82

#define V_PW			8
#define V_BP			6
#define V_VD			1280
#define V_FP			4


#define LCD_WIDTH       62    //uint mm the lenth of lcd active area
#define LCD_HEIGHT      111
/* Other */
#define VSYNC_POL		0
#define HSYNC_POL		VSYNC_POL
#define DCLK_POL		1
#define DEN_POL         0 //positive
#define SWAP_RB			0


#define LCD_TEST 0
#define CONFIG_DEEP_STANDBY_MODE 0
#define CONFIG_TC358768_INIT_MODE     0   //1:ARRAY  0:FUNCTION

#define dsi_init(data) 				mipi_dsi.dsi_init(data, ARRAY_SIZE(data))
#define dsi_send_dcs_packet(data) 	mipi_dsi.dsi_send_dcs_packet(data, ARRAY_SIZE(data))
#define dsi_hs_start(data)			mipi_dsi.dsi_hs_start(data, ARRAY_SIZE(data))

#define lap_define ktime_t k0,k1;
#define lap_start  k0 = ktime_get();
#define lap_end  { k1 = ktime_get(); k1 = ktime_sub(k1, k0); }

static struct rk29lcd_info *gLcd_info = NULL;
struct mipi_dsi_t mipi_dsi;
struct tc358768_t *lcd_tc358768 = NULL;

int lcd_init(void);
int lcd_standby(u8 enable);


#if CONFIG_TC358768_INIT_MODE
struct spi_cmd_data32 {
     unsigned int delay;
     unsigned int value;
};

struct spi_cmd_data32 TC358768XBG_INIT[] = {

     {0xffffffff,    0xffffffff}
};

#else

//high speed mode
static unsigned int re_initialize[] = {


};

static unsigned int initialize[] = {
// **************************************************
// Initizlize  -> Display On after power-on
// **************************************************
// **************************************************
// Power on TC358768XBG according to recommended power-on sequence
// Relase reset (RESX="H")
// Start input REFCK and PCLK
// **************************************************
// **************************************************
// TC358768XBG Software Reset
// **************************************************
    0x00020001, //SYSctl, S/W Reset
    10, 
    0x00020000, //SYSctl, S/W Reset release

// **************************************************
// TC358768XBG PLL,Clock Setting
// **************************************************
    0x00161063, //PLL Control Register 0 (PLL_PRD,PLL_FBD)
    0x00180603, //PLL_FRS,PLL_LBWS, PLL oscillation enable
    1000, 
    0x00180613, //PLL_FRS,PLL_LBWS, PLL clock out enable

// **************************************************
// TC358768XBG DPI Input Control
// **************************************************
    0x00060032, //FIFO Control Register

// **************************************************
// TC358768XBG D-PHY Setting
// **************************************************
    0x01400000, //D-PHY Clock lane enable
    0x01420000, //
    0x01440000, //D-PHY Data lane0 enable
    0x01460000, //
    0x01480000, //D-PHY Data lane1 enable
    0x014A0000, //
    0x014C0000, //D-PHY Data lane2 enable
    0x014E0000, //
    0x01500000, //D-PHY Data lane3 enable
    0x01520000, //

// **************************************************
// TC358768XBG DSI-TX PPI Control
// **************************************************
    0x021009C4, //LINEINITCNT
    0x02120000, //
    0x02140002, //LPTXTIMECNT
    0x02160000, //
    0x02200002, //THS_HEADERCNT
    0x02220000, //
    0x02244268, //TWAKEUPCNT
    0x02260000, //
    0x022C0001, //THS_TRAILCNT
    0x022E0000, //
    0x02300005, //HSTXVREGCNT
    0x02320000, //
    0x0234001F, //HSTXVREGEN enable
    0x02360000, //
    0x02380001, //DSI clock Enable/Disable during LP
    0x023A0000, //
    0x023C0001, //BTACNTRL1
    0x023E0002, //
    0x02040001, //STARTCNTRL
    0x02060000, //

// **************************************************
// TC358768XBG DSI-TX Timing Control
// **************************************************
    0x06200001, //Sync Pulse/Sync Event mode setting
    0x0622000E, //V Control Register1
    0x06240006, //V Control Register2
    0x06260500, //V Control Register3
    0x0628005E, //H Control Register1
    0x062A003F, //H Control Register2
    0x062C0870, //H Control Register3

    0x05180001, //DSI Start
    0x051A0000, //

};



static unsigned int start_dsi_hs_mode[] = {

// **************************************************
// Set to HS mode
// **************************************************
    0x05000087, //DSI lane setting, DSI mode=HS
    0x0502A300, //bit set
    0x05008000, //Switch to DSI mode
    0x0502C300, //

// **************************************************
// Host: RGB(DPI) input start
// **************************************************

    0x00080037, //DSI-TX Format setting
    0x0050003E, //DSI-TX Pixel stream packet Data Type setting
    0x00040044 //Configuration Control Register


};

#endif

static unsigned char boe_set_extension_command[] = {0xB9, 0xFF, 0x83, 0x94};
static unsigned char boe_set_MIPI_ctrl[] = {0xBA, 0x13};
static unsigned char boe_set_power[] = {0xB1, 0x7C, 0x00, 0x34, 0x09, 0x01, 0x11, 0x11, 0x36, 0x3E, 0x26, 0x26, 0x57, 0x12, 0x01, 0xE6};
static unsigned char boe_setcyc[] = {0xB4, 0x00, 0x00, 0x00, 0x05, 0x06, 0x41, 0x42, 0x02, 0x41, 0x42, 0x43, 0x47, 0x19, 0x58, 
                                       0x60, 0x08, 0x85, 0x10};
static unsigned char boe_config05[] = {0xC7, 0x00,	0x20};
static unsigned char boe_set_gip[] = {0xD5,0x4C,0x01,0x00,0x01,0xCD,0x23,0xEF,0x45,0x67,0x89,0xAB,0x11,0x00,0xDC,0x10,0xFE,0x32,
                                       0xBA,0x98,0x76,0x54,0x00,0x11,0x40};   
                                       
//static unsigned char boe_set_panel[] = {0xCC, 0x01};
//static unsigned char boe_set_vcom[] = {0xB6, 0x2a};
static unsigned char boe_set_panel[] = {0xCC, 0x05};
static unsigned char boe_set_vcom[] = {0xB6, 0x31};

static unsigned char boe_set_gamma[] = {0xE0,0x24,0x33,0x36,0x3F,0x3f,0x3f,0x3c,0x56,0x05,0x0C,0x0e,0x11,0x13,0x12,0x14,0x12,0x1e,
                                       0x24,0x33,0x36,0x3F,0x3f,0x3F,0x3c,0x56,0x05,0x0c, 0x0e,0x11,0x13,0x12,0x14,0x12, 0x1e};	//
static unsigned char boe_set_addr_mode[] = {0x36, 0x00};
static unsigned char boe_set_pixel[] = {0x3a, 0x60};

static unsigned char boe_enter_sleep_mode[] = {0x10};
static unsigned char boe_exit_sleep_mode[] = {0x11};
static unsigned char boe_set_diaplay_on[] = {0x29};
static unsigned char boe_set_diaplay_off[] = {0x28};
static unsigned char boe_enter_invert_mode[] = {0x21};
static unsigned char boe_all_pixel_on[] = {0x23};
static unsigned char boe_set_id[] = {0xc3, 0xaa, 0x55, 0xee};


void lcd_power_on(void) {


}

void lcd_power_off(void) {


}
#if LCD_TEST
void lcd_test(void) {
	u8 buf[8];
	printk("**mipi lcd test\n");
    buf[0] = 0x0c;
	mipi_dsi.dsi_read_dcs_packet(buf, 1);
	printk("**Get_pixel_format 0x0c:%02x\n", buf[0]);
	buf[0] = 0x0a;
	mipi_dsi.dsi_read_dcs_packet(buf, 1);
	printk("**Get_power_mode 0x0a:%02x\n", buf[0]);
	buf[0] = 0x0f;
	mipi_dsi.dsi_read_dcs_packet(buf, 1);
	printk("**Get_diagnostic_result 0x0f:%02x\n", buf[0]);
    buf[0] = 0x09;
	mipi_dsi.dsi_read_dcs_packet(buf, 4);
	printk("**Read Display Status 0x09:%02x,%02x,%02x,%02x\n", buf[0],buf[1],buf[2],buf[3]);
}
#endif



static unsigned char boe_set_wrdisbv[] = {0x51, 0xff};
static unsigned char boe_set_wrctrld[] = {0x53, 0x24};
static unsigned char boe_set_wrcabc[] = {0x55, 0x02};
static unsigned char boe_set_wrcabcmb[] = {0x5e, 0x0};
static unsigned char boe_set_cabc[] = {0xc9, 0x0d, 0x01, 0x0, 0x0, 0x0, 0x22, 0x0, 0x0, 0x0};
static unsigned char boe_set_cabc_gain[] = {0xca, 0x32, 0x2e, 0x2c, 0x2a, 0x28, 0x26, 0x24, 0x22, 0x20};


void lcd_cabc(u8 brightness) {
	
}



int lcd_init(void)
{
   
    int i = 0;
    lap_define
    //power on
    lcd_tc358768->power_up(NULL);
    
	if(gLcd_info)
        gLcd_info->io_init();

    i = 0;
    lap_start
	//Re-Initialize
#if CONFIG_TC358768_INIT_MODE
    i = 0;
    while (1) {
    	if(TC358768XBG_INIT[i].delay == 0xffffffff)
    		break;
    	tc358768_wr_reg_32bits_delay(TC358768XBG_INIT[i].delay, TC358768XBG_INIT[i].value);
    	i++;
    }
#else
	dsi_init(initialize);
   
       
    //lcd init
    dsi_send_dcs_packet(boe_exit_sleep_mode);
    msleep(150);
    dsi_send_dcs_packet(boe_set_extension_command);
    msleep(1);
    dsi_send_dcs_packet(boe_set_MIPI_ctrl);
    msleep(1);
    dsi_send_dcs_packet(boe_set_power);
    msleep(1);
    dsi_send_dcs_packet(boe_setcyc);
    msleep(1);
    dsi_send_dcs_packet(boe_set_vcom);
    msleep(1);
    dsi_send_dcs_packet(boe_set_panel);
    msleep(1);
    dsi_send_dcs_packet(boe_set_gip);
    msleep(1);
    dsi_send_dcs_packet(boe_set_gamma);
    msleep(1);
    dsi_send_dcs_packet(boe_set_addr_mode);
    msleep(1);
    dsi_send_dcs_packet(boe_set_diaplay_on);
#if LCD_TEST    
    lcd_test();
#endif    
    dsi_hs_start(start_dsi_hs_mode);
    
	msleep(10);
#endif
    lap_end
    printk(">>time:%lld\n", k1.tv64);
    return 0;

}

int lcd_standby(u8 enable)
{
	//int ret = 0;
	if(enable) {

	    printk("suspend lcd\n");
		//power down
	    if(gLcd_info)
	        gLcd_info->io_deinit();
	        
	    lcd_tc358768->power_down(NULL);    
	        
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
	screen->pin_hsync = HSYNC_POL;
	screen->pin_vsync = VSYNC_POL;
	screen->pin_den = DEN_POL;
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
    
    if(tc358768_init(&mipi_dsi) == 0)
    	lcd_tc358768 = (struct tc358768_t *)mipi_dsi.chip;
    else
    	printk("%s: %s:%d",__FILE__, __func__, __LINE__);	

}
