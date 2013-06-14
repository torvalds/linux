
#include <linux/rk_fb.h>
#include "lcd.h"
#if defined(CONFIG_RK_HDMI)
#include "../hdmi/rk_hdmi.h"
#endif






// if we use one lcdc with jetta for dual display,we need these configration
#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF) && defined(CONFIG_RK_HDMI)
static int set_scaler_info(struct rk29fb_screen *screen, u8 hdmi_resolution)
{
	#if defined(CONFIG_RK610_LVDS)
	screen->s_clk_inv = S_DCLK_POL;
	screen->s_den_inv = 0;
	screen->s_hv_sync_inv = 0;
	#endif
	
	switch(hdmi_resolution)
	{
	case HDMI_1920x1080p_60Hz:
                /* Scaler Timing    */
	#if defined(CONFIG_RK610_LVDS)
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
	#endif

		//bellow are for JettaB
	#if defined(CONFIG_RK616_LVDS)
		screen->pll_cfg_val = S_PLL_CFG_VAL;
		screen->frac	    = S_FRAC;
		screen->scl_vst	    = S_SCL_VST;
		screen->scl_hst     = S_SCL_HST;
		screen->vif_vst     = S_VIF_VST;
		screen->vif_hst     = S_VIF_HST;
	#endif
		break;
	case HDMI_1920x1080p_50Hz:
                /* Scaler Timing    */
	#if defined(CONFIG_RK610_LVDS)
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
	#endif

	#if defined(CONFIG_RK616_LVDS)
		screen->pll_cfg_val = S1_PLL_CFG_VAL;
		screen->frac	    = S1_FRAC;
		screen->scl_vst	    = S1_SCL_VST;
		screen->scl_hst     = S1_SCL_HST;
		screen->vif_vst     = S1_VIF_VST;
		screen->vif_hst     = S1_VIF_HST;
	#endif
		break;
	case HDMI_1280x720p_60Hz:
                /* Scaler Timing    */
	#if defined(CONFIG_RK610_LVDS)
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
	#endif
	
	#if defined(CONFIG_RK616_LVDS)
		screen->pll_cfg_val = S2_PLL_CFG_VAL;
		screen->frac	    = S2_FRAC;
		screen->scl_vst	    = S2_SCL_VST;
		screen->scl_hst     = S2_SCL_HST;
		screen->vif_vst     = S2_VIF_VST;
		screen->vif_hst     = S2_VIF_HST;
	#endif
		break;
	case HDMI_1280x720p_50Hz:
                /* Scaler Timing    */
	#if defined(CONFIG_RK610_LVDS)
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
	#endif
	
	#if defined(CONFIG_RK616_LVDS)
		screen->pll_cfg_val = S3_PLL_CFG_VAL;
		screen->frac	    = S3_FRAC;
		screen->scl_vst	    = S3_SCL_VST;
		screen->scl_hst     = S3_SCL_HST;
		screen->vif_vst     = S3_VIF_VST;
		screen->vif_hst     = S3_VIF_HST;
	#endif
		break;
	case HDMI_720x576p_50Hz_4_3:
	case HDMI_720x576p_50Hz_16_9:
                /* Scaler Timing    */
	#if defined(CONFIG_RK610_LVDS)
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
	#endif
	
	#if defined(CONFIG_RK616_LVDS)
		screen->pll_cfg_val = S4_PLL_CFG_VAL;
		screen->frac	    = S4_FRAC;
		screen->scl_vst	    = S4_SCL_VST;
		screen->scl_hst     = S4_SCL_HST;
		screen->vif_vst     = S4_VIF_VST;
		screen->vif_hst     = S4_VIF_HST;
	#endif
		break;
		
	case HDMI_720x480p_60Hz_16_9:
	case HDMI_720x480p_60Hz_4_3:
                /* Scaler Timing    */
	#if defined(CONFIG_RK610_LVDS)
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
	#endif
	
	#if defined(CONFIG_RK616_LVDS)
		screen->pll_cfg_val = S5_PLL_CFG_VAL;
		screen->frac	    = S5_FRAC;
		screen->scl_vst	    = S5_SCL_VST;
		screen->scl_hst     = S5_SCL_HST;
		screen->vif_vst     = S5_VIF_VST;
		screen->vif_hst     = S5_VIF_HST;
	#endif
		break;
	default :
            	printk("%s lcd not support dual display at this hdmi resolution %d \n",__func__,hdmi_resolution);
            	return -1;
	        break;
	}
	
	return 0;
}
#else
#define set_scaler_info  NULL
#endif

void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info )
{

#if defined(RK_USE_SCREEN_ID)
	set_lcd_info_by_id(screen,lcd_info);
#else
	screen->type = SCREEN_TYPE;
	screen->face = OUT_FACE;
	screen->lvds_format = LVDS_FORMAT;  //lvds data format

	
	screen->x_res = H_VD;		//screen resolution
	screen->y_res = V_VD;

	screen->width = LCD_WIDTH;
	screen->height = LCD_HEIGHT;

    
	screen->lcdc_aclk = LCDC_ACLK; // Timing 
	screen->pixclock = DCLK;
	screen->left_margin = H_BP;
	screen->right_margin = H_FP;
	screen->hsync_len = H_PW;
	screen->upper_margin = V_BP;
	screen->lower_margin = V_FP;
	screen->vsync_len = V_PW;

	
	screen->pin_hsync = HSYNC_POL; //Pin polarity 
	screen->pin_vsync = VSYNC_POL;
	screen->pin_den = DEN_POL;
	screen->pin_dclk = DCLK_POL;

	
	screen->swap_rb = SWAP_RB; // Swap rule 
	screen->swap_rg = SWAP_RG;
	screen->swap_gb = SWAP_GB;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;
	
#if defined(CONFIG_MIPI_DSI)
       /* MIPI DSI */
    screen->dsi_lane = MIPI_DSI_LANE;
    //screen->dsi_video_mode = MIPI_DSI_VIDEO_MODE;
    screen->hs_tx_clk = MIPI_DSI_HS_CLK;
#endif
	/* Operation function*/
#if defined(RK_SCREEN_INIT)  //some screen need to init by spi or i2c
	screen->init = rk_lcd_init;
   	screen->standby = rk_lcd_standby;
	if(lcd_info)
       		gLcd_info = lcd_info;	
#endif

#if defined(USE_RK_DSP_LUT)
	screen->dsp_lut = dsp_lut;
#endif
	
#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF) 
    	screen->sscreen_get = set_scaler_info;
#endif

#endif

}

size_t get_fb_size(void)
{
	size_t size = 0;
	#if defined(CONFIG_THREE_FB_BUFFER)
		size = ((H_VD)*(V_VD)<<2)* 3; //three buffer
	#else
		size = ((H_VD)*(V_VD)<<2)<<1; //two buffer
	#endif
	return ALIGN(size,SZ_1M);
}
