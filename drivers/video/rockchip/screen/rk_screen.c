
#include <linux/rk_fb.h>
#include "lcd.h"
#if defined(CONFIG_RK_HDMI)
#include "../hdmi/rk_hdmi.h"
#endif
#if defined(CONFIG_MACH_RK_FAC)
#include <plat/config.h>
extern uint lcd_param[LCD_PARAM_MAX];
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
	#if defined(CONFIG_MACH_RK_FAC)
	screen->type = lcd_param[OUT_TYPE_INDEX];
	screen->face = lcd_param[OUT_FACE_INDEX];
	screen->lvds_format = lcd_param[LVDS_FORMAT_INDEX];  //lvds data format
	
		
	screen->x_res = lcd_param[H_VD_INDEX];
	screen->y_res = lcd_param[V_VD_INDEX];
	
	screen->width = lcd_param[LCD_WIDTH_INDEX];
	screen->height = lcd_param[LCD_HEIGHT_INDEX];
	
	    
	screen->lcdc_aclk = lcd_param[LCDC_ACLK_INDEX];
	screen->pixclock = lcd_param[OUT_CLK_INDEX];
	screen->left_margin = lcd_param[H_BP_INDEX];
	screen->right_margin = lcd_param[H_FP_INDEX];
	screen->hsync_len = lcd_param[H_PW_INDEX];
	screen->upper_margin = lcd_param[V_BP_INDEX];
	screen->lower_margin = lcd_param[V_FP_INDEX];
	screen->vsync_len = lcd_param[V_PW_INDEX];
	
		
	screen->pin_hsync = HSYNC_POL; //Pin polarity 
	screen->pin_vsync = VSYNC_POL;
	screen->pin_den = DEN_POL;
	screen->pin_dclk = lcd_param[DCLK_POL_INDEX];
	
		
	screen->swap_rb = lcd_param[SWAP_RB_INDEX];
	screen->swap_rg = SWAP_RG;
	screen->swap_gb = SWAP_GB;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;
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
	#endif
	
#if defined(CONFIG_MIPI_DSI)
       /* MIPI DSI */
#if defined(MIPI_DSI_LANE)       
    screen->dsi_lane = MIPI_DSI_LANE;
#else
	screen->dsi_lane = 4;
#endif
    //screen->dsi_video_mode = MIPI_DSI_VIDEO_MODE;
#if defined(MIPI_DSI_HS_CLK)    
    screen->hs_tx_clk = MIPI_DSI_HS_CLK;
#else    
    screen->hs_tx_clk = 1000000000;        //1GHz
#endif

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

#if defined(CONFIG_MACH_RK_FAC)
size_t get_fb_size(void)
{
	size_t size = 0;
	char *pchar=NULL;
	char lcdParam[]="lcd_param";
	char LcdWith[10];
	char LcdHigh[10];
	int mLcdWith,mLcdHigh;
 	int num=0,i;
  	int count=20;
	pchar=strstr(boot_command_line,lcdParam);
	memset(LcdWith,0,sizeof(char)*10);
	memset(LcdHigh,0,sizeof(char)*10);

	if(pchar!=NULL)
	{
		do{
			if(count==13)
			{
				num=strcspn(pchar,",");
				for(i=0;i<num;i++)
					LcdWith[i]=pchar[i];

				mLcdWith=simple_strtol(LcdWith,NULL,10);		
			}
			
		  	if(count==9){		
				num=strcspn(pchar,",");
				for(i=0;i<num;i++)
					LcdHigh[i]=pchar[i];
				
				mLcdHigh=simple_strtol(LcdHigh,NULL,10);		
				break;
			}

			num=strcspn(pchar,",");
			pchar=pchar+num+1;
			
		}while(count--);
			
	}
	
  	if((mLcdWith>0)&&(mLcdHigh>0))
  	{
		lcd_param[H_VD_INDEX]=mLcdWith;
		lcd_param[V_VD_INDEX]=mLcdHigh;
	}
	#if defined(CONFIG_THREE_FB_BUFFER)
		size = ((lcd_param[H_VD_INDEX])*(lcd_param[V_VD_INDEX])<<2)* 3; //three buffer
	#else
		size = ((lcd_param[H_VD_INDEX])*(lcd_param[V_VD_INDEX])<<2)<<1; //two buffer
	#endif
	return ALIGN(size,SZ_1M);
}
#else
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
#endif
