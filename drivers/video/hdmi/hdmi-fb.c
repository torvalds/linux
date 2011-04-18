#include <linux/console.h>
#include <linux/fb.h>

#include <linux/completion.h>
#include "../display/screen/screen.h"
#include <linux/hdmi.h>
#include "../rk29_fb.h"


/* Base */
#define LCD_ACLK		500000000// 312000000

#define OUT_TYPE		SCREEN_HDMI
#define OUT_FACE		OUT_P888
#define DCLK_POL		1
#define SWAP_RB			0

/* 720p@60Hz Timing */
#define OUT_CLK			74250000
#define H_PW			40
#define H_BP			220
#define H_VD			1280
#define H_FP			110
#define V_PW			5
#define V_BP			20
#define V_VD			720
#define V_FP			5

/* 720p@50Hz Timing */
#define OUT_CLK2	    74250000
#define H_PW2			40
#define H_BP2			220
#define H_VD2			1280
#define H_FP2			440
#define V_PW2			5
#define V_BP2			20
#define V_VD2			720
#define V_FP2			5

/* 576p@50Hz Timing */
#define OUT_CLK3		27000000
#define H_PW3			64
#define H_BP3			68
#define H_VD3			720
#define H_FP3			12
#define V_PW3			5
#define V_BP3			39
#define V_VD3			576
#define V_FP3			5

/* 1080p@50Hz Timing */
#define OUT_CLK4		148500000
#define H_PW4			44
#define H_BP4			148
#define H_VD4			1920
#define H_FP4			528
#define V_PW4			5
#define V_BP4			35
#define V_VD4			1080
#define V_FP4			5


extern int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable );

static int anx7150_init(void)
{
    return 0;
}

static int anx7150_standby(u8 enable)
{
    return 0;
}

static void hdmi_set_info(struct rk29fb_screen *screen)
{
    struct rk29fb_screen *screen2 = screen + 1;
	struct rk29fb_screen *screen3 = screen + 2;
	struct rk29fb_screen *screen4 = screen + 3;

    /* ****************** 720p@60Hz ******************* */
    /* screen type & face */
    screen->type = OUT_TYPE;
    screen->face = OUT_FACE;

    /* Screen size */
    screen->x_res = H_VD;
    screen->y_res = V_VD;

    /* Timing */
    screen->pixclock = OUT_CLK;
	screen4->lcdc_aclk = LCD_ACLK;
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
    screen->init = anx7150_init;
    screen->standby = anx7150_standby;


    /* ****************** 720p@50Hz ******************* */
    /* screen type & face */
    screen2->type = OUT_TYPE;
    screen2->face = OUT_FACE;

    /* Screen size */
    screen2->x_res = H_VD2;
    screen2->y_res = V_VD2;

    /* Timing */
    screen2->pixclock = OUT_CLK2;
	screen2->lcdc_aclk = LCD_ACLK;
	screen2->left_margin = H_BP2;
	screen2->right_margin = H_FP2;
	screen2->hsync_len = H_PW2;
	screen2->upper_margin = V_BP2;
	screen2->lower_margin = V_FP2;
	screen2->vsync_len = V_PW2;

	/* Pin polarity */
	screen2->pin_hsync = 0;
	screen2->pin_vsync = 0;
	screen2->pin_den = 0;
	screen2->pin_dclk = DCLK_POL;

	/* Swap rule */
    screen2->swap_rb = SWAP_RB;
    screen2->swap_rg = 0;
    screen2->swap_gb = 0;
    screen2->swap_delta = 0;
    screen2->swap_dumy = 0;

    /* Operation function*/
    screen2->init = anx7150_init;
    screen2->standby = anx7150_standby;

	/* ****************** 576p@50Hz ******************* */
	/* screen type & face */
	screen3->type = OUT_TYPE;
	screen3->face = OUT_FACE;

	/* Screen size */
	screen3->x_res = H_VD3;
	screen3->y_res = V_VD3;

	/* Timing */
	screen3->pixclock = OUT_CLK3;
	screen3->lcdc_aclk = LCD_ACLK;
	screen3->left_margin = H_BP3;
	screen3->right_margin = H_FP3;
	screen3->hsync_len = H_PW3;
	screen3->upper_margin = V_BP3;
	screen3->lower_margin = V_FP3;
	screen3->vsync_len = V_PW3;

	/* Pin polarity */
	screen3->pin_hsync = 0;
	screen3->pin_vsync = 0;
	screen3->pin_den = 0;
	screen3->pin_dclk = DCLK_POL;

	/* Swap rule */
	screen3->swap_rb = SWAP_RB;
	screen3->swap_rg = 0;
	screen3->swap_gb = 0;
	screen3->swap_delta = 0;
	screen3->swap_dumy = 0;

	/* Operation function*/
	screen3->init = anx7150_init;
	screen3->standby = anx7150_standby;
	/* ****************** 1080p@50Hz ******************* */
	/* screen type & face */
	screen4->type = OUT_TYPE;
	screen4->face = OUT_FACE;

	/* Screen size */
	screen4->x_res = H_VD4;
	screen4->y_res = V_VD4;

	/* Timing */
	screen4->pixclock = OUT_CLK4;
	screen4->lcdc_aclk = LCD_ACLK;
	screen4->left_margin = H_BP4;
	screen4->right_margin = H_FP4;
	screen4->hsync_len = H_PW4;
	screen4->upper_margin = V_BP4;
	screen4->lower_margin = V_FP4;
	screen4->vsync_len = V_PW4;

	/* Pin polarity */
	screen4->pin_hsync = 0;
	screen4->pin_vsync = 0;
	screen4->pin_den = 0;
	screen4->pin_dclk = DCLK_POL;

	/* Swap rule */
	screen4->swap_rb = SWAP_RB;
	screen4->swap_rg = 0;
	screen4->swap_gb = 0;
	screen4->swap_delta = 0;
	screen4->swap_dumy = 0;

	/* Operation function*/
	screen4->init = anx7150_init;
	screen4->standby = anx7150_standby;
}

int hdmi_switch_fb(struct hdmi *hdmi, int type)
{
	int rc = 0;
	struct rk29fb_screen hdmi_info[4];

	hdmi_set_info(&hdmi_info[0]);

	switch(hdmi->resolution)
	{
		case HDMI_1280x720p_50Hz:
			rc = FB_Switch_Screen(&hdmi_info[1], type);
			break;
		case HDMI_1280x720p_60Hz:
			rc = FB_Switch_Screen(&hdmi_info[0], type);
			break;
		case HDMI_720x576p_50Hz:
			rc = FB_Switch_Screen(&hdmi_info[2], type);
			break;
		case HDMI_1920x1080p_50Hz:
			rc = FB_Switch_Screen(&hdmi_info[3], type);
			break;
		default:
			rc = FB_Switch_Screen(&hdmi_info[1], type);
			break;		
	}
	if(hdmi->wait == 1) {
		complete(&hdmi->complete);
		hdmi->wait = 0;
	}
	return rc;
}
int hdmi_resolution_changed(struct hdmi *hdmi, int xres, int yres, int video_on)
{
	int ret = 0;
	if(hdmi->display_on == 0|| hdmi->plug == 0)
		return ret;
	if(xres > 1280 && hdmi->resolution != HDMI_1920x1080p_50Hz) 
	{
		hdmi->resolution = HDMI_1920x1080p_50Hz;
		hdmi->display_on = 1;
		hdmi->hdmi_set_param(hdmi);
		ret = 1;
	}
	

	else if(xres >1024 && xres <= 1280 && hdmi->resolution != HDMI_1280x720p_50Hz){
		hdmi->resolution = HDMI_1280x720p_50Hz;
		hdmi->display_on = 1;
		hdmi->hdmi_set_param(hdmi);
		ret = 1;
	}
	/*
	else {
		if(hdmi->display_on == 1)
			hdmi->hdmi_display_off(hdmi);
	}*/
	return ret;
}

int hdmi_get_default_resolution(void *screen)
{
    struct rk29fb_screen hdmi_info[4];

	hdmi_set_info(&hdmi_info[0]);
    memcpy((struct rk29fb_screen*)screen, &hdmi_info[HDMI_DEFAULT_RESOLUTION], sizeof(struct rk29fb_screen));
    return 0;  
}


EXPORT_SYMBOL(hdmi_resolution_changed);