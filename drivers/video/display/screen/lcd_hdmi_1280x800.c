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

#define OUT_FORMAT      LVDS_8BIT_2

#define OUT_FACE		OUT_D888_P666  
#define OUT_CLK			65000000
#define LCDC_ACLK        500000000//312000000           //29 lcdc axi DMA ÆµÂÊ


/* Timing */
#define H_PW			10
#define H_BP			10
#define H_VD			1280
#define H_FP			20

#define V_PW			10
#define V_BP			10
#define V_VD			800
#define V_FP			13

#define LCD_WIDTH       202
#define LCD_HEIGHT      152

/* scaler Timing    */
//1920*1080*60

#define S_OUT_CLK		SCALE_RATE(148500000,66000000) //m=16 n=9 no=4
#define S_H_PW			10
#define S_H_BP			10
#define S_H_VD			1280
#define S_H_FP			20

#define S_V_PW			10
#define S_V_BP			10
#define S_V_VD			800
#define S_V_FP			13

#define S_H_ST			440
#define S_V_ST			13

//1920*1080*50
#define S1_OUT_CLK		SCALE_RATE(148500000,57375000)  //m=17 n=11 no=4 
#define S1_H_PW			10
#define S1_H_BP			10
#define S1_H_VD			1280
#define S1_H_FP			77

#define S1_V_PW			10
#define S1_V_BP			10
#define S1_V_VD			800
#define S1_V_FP			13

#define S1_H_ST			459
#define S1_V_ST			13

//1280*720*60
#define S2_OUT_CLK		SCALE_RATE(74250000,66000000)  //m=32 n=9 no=4
#define S2_H_PW			10
#define S2_H_BP			10
#define S2_H_VD			1280
#define S2_H_FP			20

#define S2_V_PW			10
#define S2_V_BP			10
#define S2_V_VD			800
#define S2_V_FP			13

#define S2_H_ST			440
#define S2_V_ST			13

//1280*720*50

#define S3_OUT_CLK		SCALE_RATE(74250000,57375000)   // m=34 n=11 no=4
#define S3_H_PW			10
#define S3_H_BP			10
#define S3_H_VD			1280
#define S3_H_FP			77

#define S3_V_PW			10
#define S3_V_BP			10
#define S3_V_VD			800
#define S3_V_FP			13

#define S3_H_ST			459
#define S3_V_ST			13

//720*576*50
#define S4_OUT_CLK		SCALE_RATE(27000000,63281250)  //m=75 n=4 no=8
#define S4_H_PW			10
#define S4_H_BP			10
#define S4_H_VD			1280
#define S4_H_FP			185

#define S4_V_PW			10
#define S4_V_BP			10
#define S4_V_VD			800
#define S4_V_FP			48

#define S4_H_ST			81
#define S4_V_ST			48

//720*480*60
#define S5_OUT_CLK		SCALE_RATE(27000000,75000000)  //m=100 n=9 no=4
#define S5_H_PW			10
#define S5_H_BP			10
#define S5_H_VD			1280
#define S5_H_FP			130

#define S5_V_PW			10
#define S5_V_BP			10
#define S5_V_VD			800
#define S5_V_FP			54

#define S5_H_ST			476
#define S5_V_ST			48

#define S_DCLK_POL       0

/* Other */
#define DCLK_POL		0
#define SWAP_RB			0 
#ifdef  CONFIG_HDMI_DUAL_DISP
static int set_scaler_info(struct rk29fb_screen *screen, u8 hdmi_resolution)
{
    screen->s_clk_inv = S_DCLK_POL;
    screen->s_den_inv = 0;
    screen->s_hv_sync_inv = 0;
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
	case HDMI_1280x720p_60Hz:
                /* Scaler Timing    */
            screen->hdmi_resolution = hdmi_resolution;
	        screen->s_pixclock = S2_OUT_CLK;
	        screen->s_hsync_len = S2_H_PW;
	        screen->s_left_margin = S2_H_BP;
	        screen->s_right_margin = S2_H_FP;
	        screen->s_hsync_len = S2_H_PW;
	        screen->s_upper_margin = S2_V_BP;
	        screen->s_lower_margin = S2_V_FP;
	        screen->s_vsync_len = S2_V_PW;
	        screen->s_hsync_st = S2_H_ST;
	        screen->s_vsync_st = S2_V_ST;
	        break;
    case HDMI_1280x720p_50Hz:
                /* Scaler Timing    */
            screen->hdmi_resolution = hdmi_resolution;
	        screen->s_pixclock = S3_OUT_CLK;
	        screen->s_hsync_len = S3_H_PW;
	        screen->s_left_margin = S3_H_BP;
	        screen->s_right_margin = S3_H_FP;
	        screen->s_hsync_len = S3_H_PW;
	        screen->s_upper_margin = S3_V_BP;
	        screen->s_lower_margin = S3_V_FP;
	        screen->s_vsync_len = S3_V_PW;
	        screen->s_hsync_st = S3_H_ST;
	        screen->s_vsync_st = S3_V_ST;
	        break;
    case HDMI_720x576p_50Hz_4x3:
    case HDMI_720x576p_50Hz_16x9:
                /* Scaler Timing    */
            screen->hdmi_resolution = hdmi_resolution;
	        screen->s_pixclock = S4_OUT_CLK;
	        screen->s_hsync_len = S4_H_PW;
	        screen->s_left_margin = S4_H_BP;
	        screen->s_right_margin = S4_H_FP;
	        screen->s_hsync_len = S4_H_PW;
	        screen->s_upper_margin = S4_V_BP;
	        screen->s_lower_margin = S4_V_FP;
	        screen->s_vsync_len = S4_V_PW;
	        screen->s_hsync_st = S4_H_ST;
	        screen->s_vsync_st = S4_V_ST;
	        break;
    case HDMI_720x480p_60Hz_16x9:
    case HDMI_720x480p_60Hz_4x3:
                /* Scaler Timing    */
            screen->hdmi_resolution = hdmi_resolution;
	        screen->s_pixclock = S5_OUT_CLK;
	        screen->s_hsync_len = S5_H_PW;
	        screen->s_left_margin = S5_H_BP;
	        screen->s_right_margin = S5_H_FP;
	        screen->s_hsync_len = S5_H_PW;
	        screen->s_upper_margin = S5_V_BP;
	        screen->s_lower_margin = S5_V_FP;
	        screen->s_vsync_len = S5_V_PW;
	        screen->s_hsync_st = S5_H_ST;
	        screen->s_vsync_st = S5_V_ST;
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
    screen->hw_format = OUT_FORMAT;
    
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
    screen->sscreen_set = rk610_lcd_scaler_set_param;
}



