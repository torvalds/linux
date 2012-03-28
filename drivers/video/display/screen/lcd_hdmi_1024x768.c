#include <linux/fb.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/board.h>
#include "screen.h"
#include <linux/hdmi.h>
#include "../../rk29_fb.h"
#include "../lcd/rk610_lcd.h"

/* Base */
#define OUT_TYPE		SCREEN_LVDS

#define OUT_FORMAT      LVDS_8BIT_3
#define OUT_FACE		OUT_D888_P666  
#define OUT_CLK			65000000
#define LCDC_ACLK        500000000//312000000           //29 lcdc axi DMA ÆµÂÊ

/* Timing */
#define H_PW			48 //10
#define H_BP			88 //100
#define H_VD			800 //1024
#define H_FP			40 //210

#define V_PW			3 //10
#define V_BP			32 //10
#define V_VD			480 //768
#define V_FP			13 //18

#define LCD_WIDTH       202
#define LCD_HEIGHT      152

/* scaler Timing    */
//1920*1080*60
#define S_OUT_CLK		SCALE_RATE(148500000,66000000)
#define S_H_PW			100
#define S_H_BP			100
#define S_H_VD			1024
#define S_H_FP			151

#define S_V_PW			5
#define S_V_BP			15
#define S_V_VD			768
#define S_V_FP			12

#define S_H_ST			1757
#define S_V_ST			14

//1920*1080*50
#define S1_OUT_CLK		SCALE_RATE(148500000,54000000)
#define S1_H_PW			100
#define S1_H_BP			100
#define S1_H_VD			1024
#define S1_H_FP			126

#define S1_V_PW			5
#define S1_V_BP			15
#define S1_V_VD			768
#define S1_V_FP			12

#define S1_H_ST			1757
#define S1_V_ST			14
/* Other */
#define DCLK_POL		0
#define SWAP_RB			0 
#ifdef  CONFIG_HDMI_DUAL_DISP
static int set_scaler_info(struct rk29fb_screen *screen, u8 hdmi_resolution)
{
    switch(hdmi_resolution){
    case HDMI_1920x1080p_60Hz:
                /* Scaler Timing    */
            screen->hdmi_resolution = hdmi_resolution;
	        screen->s_pixclock = S_OUT_CLK;
	        screen->s_hsync_len = S_H_PW;
	        screen->s_left_margin = S_H_BP;
	        screen->s_right_margin = S_H_FP;
	        screen->s_hsync_len = S_H_PW;
	        screen->s_upper_margin = S_V_BP;
	        screen->s_lower_margin = S_V_FP;
	        screen->s_vsync_len = S_V_PW;
	        screen->s_hsync_st = S_H_ST;
	        screen->s_vsync_st = S_V_ST;
	        break;
	case HDMI_1920x1080p_50Hz:
                /* Scaler Timing    */
            screen->hdmi_resolution = hdmi_resolution;
	        screen->s_pixclock = S1_OUT_CLK;
	        screen->s_hsync_len = S1_H_PW;
	        screen->s_left_margin = S1_H_BP;
	        screen->s_right_margin = S1_H_FP;
	        screen->s_hsync_len = S1_H_PW;
	        screen->s_upper_margin = S1_V_BP;
	        screen->s_lower_margin = S1_V_FP;
	        screen->s_vsync_len = S1_V_PW;
	        screen->s_hsync_st = S1_H_ST;
	        screen->s_vsync_st = S1_V_ST;
	        break;
    default :
            printk("%s lcd not support dual display at this hdmi resolution %d \n",__func__,hdmi_resolution);
            return -1;
	        break;
	}
	
	return 0;
}
#else
static int set_scaler_info(struct rk29fb_screen *screen, u8 hdmi_resolution){}
#endif

void set_lcd_info(struct rk29fb_screen *screen,  struct rk29lcd_info *lcd_info )
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
    screen->init = NULL;
    screen->standby = NULL;
    screen->sscreen_get = set_scaler_info;
#ifdef CONFIG_RK610_LCD
    screen->sscreen_set = rk610_lcd_scaler_set_param;
#endif
}