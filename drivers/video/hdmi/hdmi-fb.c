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


/* 720p@50Hz Timing */
#define OUT_CLK0	    74250000
#define H_PW0			40
#define H_BP0			220
#define H_VD0			1280
#define H_FP0			440
#define V_PW0			5
#define V_BP0			20
#define V_VD0			720
#define V_FP0			5

/* 720p@60Hz Timing */
#define OUT_CLK1		74250000
#define H_PW1			40
#define H_BP1			220
#define H_VD1			1280
#define H_FP1			110
#define V_PW1			5
#define V_BP1			20
#define V_VD1			720
#define V_FP1			5

/* 576p@50Hz Timing */
#define OUT_CLK2		27000000
#define H_PW2			64
#define H_BP2			68
#define H_VD2			720
#define H_FP2			12
#define V_PW2			5
#define V_BP2			39
#define V_VD2			576
#define V_FP2			5

/* 720x480p@60Hz Timing */
#define OUT_CLK3		27000000
#define H_PW3			62
#define H_BP3			60
#define H_VD3			720
#define H_FP3			16
#define V_PW3			6
#define V_BP3			30
#define V_VD3			480
#define V_FP3			9

/* 1080p@50Hz Timing */
#define OUT_CLK5		148500000
#define H_PW4			44
#define H_BP4			148
#define H_VD4			1920
#define H_FP4			528
#define V_PW4			5
#define V_BP4			36
#define V_VD4			1080
#define V_FP4			4

/* 1080p@60Hz Timing */
#define OUT_CLK4		148500000
#define H_PW5			44
#define H_BP5			148
#define H_VD5			1920
#define H_FP5			88
#define V_PW5			5
#define V_BP5			36
#define V_VD5			1080
#define V_FP5			4


extern int FB_Switch_Screen( struct rk29fb_screen *screen, u32 enable );

static int anx7150_init(void)
{
    return 0;
}

static int anx7150_standby(u8 enable)
{
    return 0;
}


struct rk29fb_screen hdmi_info[] = {
	{
	    .hdmi_resolution = HDMI_1280x720p_50Hz,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD0,
		.y_res = V_VD0,
		.pixclock = OUT_CLK0,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP0,
		.right_margin = H_FP0,
		.hsync_len = H_PW0,
		.upper_margin = V_BP0,
		.lower_margin = V_FP0,
		.vsync_len = V_PW0,
		.pin_hsync = 1,
		.pin_vsync = 1,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_1280x720p_50Hz
	{
	    .hdmi_resolution = HDMI_1280x720p_60Hz,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD1,
		.y_res = V_VD1,
		.pixclock = OUT_CLK1,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP1,
		.right_margin = H_FP1,
		.hsync_len = H_PW1,
		.upper_margin = V_BP1,
		.lower_margin = V_FP1,
		.vsync_len = V_PW1,
		.pin_hsync = 1,
		.pin_vsync = 1,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_1280x720p_60Hz	
	{
	    .hdmi_resolution = HDMI_720x576p_50Hz_4x3,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD2,
		.y_res = V_VD2,
		.pixclock = OUT_CLK2,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP2,
		.right_margin = H_FP2,
		.hsync_len = H_PW2,
		.upper_margin = V_BP2,
		.lower_margin = V_FP2,
		.vsync_len = V_PW2,
		.pin_hsync = 0,
		.pin_vsync = 0,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_720x576p_50Hz_4x3
	{
	    .hdmi_resolution = HDMI_720x576p_50Hz_16x9,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD2,
		.y_res = V_VD2,
		.pixclock = OUT_CLK2,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP2,
		.right_margin = H_FP2,
		.hsync_len = H_PW2,
		.upper_margin = V_BP2,
		.lower_margin = V_FP2,
		.vsync_len = V_PW2,
		.pin_hsync = 0,
		.pin_vsync = 0,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_720x576p_50Hz_16x9
	{
	    .hdmi_resolution = HDMI_720x480p_60Hz_4x3,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD3,
		.y_res = V_VD3,
		.pixclock = OUT_CLK3,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP3,
		.right_margin = H_FP3,
		.hsync_len = H_PW3,
		.upper_margin = V_BP3,
		.lower_margin = V_FP3,
		.vsync_len = V_PW3,
		.pin_hsync = 0,
		.pin_vsync = 0,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_720x480p_60Hz_4x3
	{
	    .hdmi_resolution = HDMI_720x480p_60Hz_16x9,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD3,
		.y_res = V_VD3,
		.pixclock = OUT_CLK3,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP3,
		.right_margin = H_FP3,
		.hsync_len = H_PW3,
		.upper_margin = V_BP3,
		.lower_margin = V_FP3,
		.vsync_len = V_PW3,
		.pin_hsync = 0,
		.pin_vsync = 0,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_720x480p_60Hz_16x9
	{
	    .hdmi_resolution = HDMI_1920x1080p_50Hz,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD4,
		.y_res = V_VD4,
		.pixclock = OUT_CLK4,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP4,
		.right_margin = H_FP4,
		.hsync_len = H_PW4,
		.upper_margin = V_BP4,
		.lower_margin = V_FP4,
		.vsync_len = V_PW4,
		.pin_hsync = 1,
		.pin_vsync = 1,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_1920x1080p_50Hz
	{
	    .hdmi_resolution = HDMI_1920x1080p_60Hz,
		.type = OUT_TYPE,
		.face = OUT_FACE,
		.x_res = H_VD5,
		.y_res = V_VD5,
		.pixclock = OUT_CLK5,
		.lcdc_aclk = LCD_ACLK,
		.left_margin = H_BP5,
		.right_margin = H_FP5,
		.hsync_len = H_PW5,
		.upper_margin = V_BP5,
		.lower_margin = V_FP5,
		.vsync_len = V_PW5,
		.pin_hsync = 1,
		.pin_vsync = 1,
		.pin_den = 0,
		.pin_dclk = DCLK_POL,
		.swap_rb = SWAP_RB,
		.swap_rg = 0,
		.swap_gb = 0,
		.swap_delta = 0,
		.swap_dumy = 0,
		.init = anx7150_init,
		.standby = anx7150_standby,	
	},		//HDMI_1920x1080p_60Hz
};

int hdmi_switch_fb(struct hdmi *hdmi, int type)
{
	int rc = 0;
	
	switch(hdmi->resolution)
	{
		case HDMI_1280x720p_50Hz:
			rc = FB_Switch_Screen(&hdmi_info[0], type);
			break;
		case HDMI_1280x720p_60Hz:
			rc = FB_Switch_Screen(&hdmi_info[1], type);
			break;
		case HDMI_720x576p_50Hz_4x3:
			rc = FB_Switch_Screen(&hdmi_info[2], type);
			break;
		case HDMI_720x576p_50Hz_16x9:
			rc = FB_Switch_Screen(&hdmi_info[3], type);
			break;
		case HDMI_720x480p_60Hz_4x3:
			rc = FB_Switch_Screen(&hdmi_info[4], type);
			break;
		case HDMI_720x480p_60Hz_16x9:
			rc = FB_Switch_Screen(&hdmi_info[5], type);
			break;
		case HDMI_1920x1080p_50Hz:
			rc = FB_Switch_Screen(&hdmi_info[6], type);
			break;
		case HDMI_1920x1080p_60Hz:
			rc = FB_Switch_Screen(&hdmi_info[7], type);
			break;
		default:
			rc = FB_Switch_Screen(&hdmi_info[0], type);
			break;		
	}
	if(hdmi->wait == 1) {
		complete(&hdmi->complete);
		hdmi->wait = 0;
	}
	return rc;
}
