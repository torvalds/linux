#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "rk616_vif.h"

struct rk616_vif *g_vif;
extern int rk616_pll_set_rate(struct mfd_rk616 *rk616,int id,u32 cfg_val,u32 frac);

/*rk616 video interface config*/
static int rk616_vif_cfg(struct mfd_rk616 *rk616,rk_screen *fscreen,int id)
{
	int ret = 0;
	u32 val = 0;
	int offset = 0;
	int pll_id;
	rk_screen *screen  = NULL;
	bool pll_use_mclk12m = false;
	
	if(id == 0) //video interface 0
	{
		if(!rk616->route.vif0_en)
		{
			val = (VIF0_EN << 16); //disable vif0
			ret = rk616->write_dev(rk616,VIF0_REG0,&val);
			return 0;
		}
		offset = 0;
		pll_id = rk616->route.vif0_clk_sel;
		if(rk616->route.pll0_clk_sel == PLL0_CLK_SEL(MCLK_12M))
			pll_use_mclk12m = true;
		else
			pll_use_mclk12m = false;
	}
	else       //vide0 interface 1
	{
		if(!rk616->route.vif1_en)
		{
			val = (VIF0_EN << 16); //disabl VIF1
			ret = rk616->write_dev(rk616,VIF1_REG0,&val);
			return 0;
		}
		offset = 0x18;
		pll_id = (rk616->route.vif1_clk_sel >> 6);
		if(rk616->route.pll1_clk_sel == PLL1_CLK_SEL(MCLK_12M))
			pll_use_mclk12m = true;
		else
			pll_use_mclk12m = false;
	}

	screen = fscreen->ext_screen;
	if(!screen)
	{
		dev_err(rk616->dev,"%s:screen is null.........\n",__func__);
		return -EINVAL;
	}

	
	
	val |= (VIF0_DDR_CLK_EN <<16) | (VIF0_DDR_PHASEN_EN << 16) | (VIF0_DDR_MODE_EN << 16)|
		(VIF0_EN <<16) | VIF0_EN; //disable ddr mode,enable VIF
	
	ret = rk616->write_dev(rk616,VIF0_REG0 + offset,&val);	

	if( (screen->x_res == 1920) && (screen->y_res == 1080))
	{
		if(pll_use_mclk12m)
			rk616_pll_set_rate(rk616,pll_id,0xc11025,0x200000);
		else
			rk616_pll_set_rate(rk616,pll_id,0x02bf5276,0);
	}
	else if((screen->x_res == 1280) && (screen->y_res == 720))
	{
		if(pll_use_mclk12m)
			rk616_pll_set_rate(rk616,pll_id,0x01811025,0x200000);
		else
			rk616_pll_set_rate(rk616,pll_id,0x1422014,0);
	}
	else if((screen->x_res == 720))
	{
		if(pll_use_mclk12m)
			rk616_pll_set_rate(rk616,pll_id,0x01413021,0xc00000);
		else
			rk616_pll_set_rate(rk616,pll_id,0x1c13015,0);
	}

	//val = fscreen->vif_hst | (fscreen->vif_vst<<16);
	val = (0xc1) | (0x01 <<16);
	ret = rk616->write_dev(rk616,VIF0_REG1 + offset,&val);

	val = (screen->hsync_len << 16) | (screen->hsync_len + screen->left_margin + 
		screen->right_margin + screen->x_res);
	ret = rk616->write_dev(rk616,VIF0_REG2 + offset,&val);

	
	val = ((screen->hsync_len + screen->left_margin + screen->x_res)<<16) |
		(screen->hsync_len + screen->left_margin);
	ret = rk616->write_dev(rk616,VIF0_REG3 + offset,&val);

	val = (screen->vsync_len << 16) | (screen->vsync_len + screen->upper_margin + 
		screen->lower_margin + screen->y_res);
	ret = rk616->write_dev(rk616,VIF0_REG4 + offset,&val);


	val = ((screen->vsync_len + screen->upper_margin + screen->y_res)<<16) |
		(screen->vsync_len + screen->upper_margin);
	ret = rk616->write_dev(rk616,VIF0_REG5 + offset,&val);
	
	return ret;
	
}



static int rk616_scaler_cfg(struct mfd_rk616 *rk616,rk_screen *screen)
{
	u32 val = 0;
	int ret = 0;
	u32 scl_hor_mode,scl_ver_mode;
	u32 scl_v_factor,scl_h_factor;
	u32 scl_reg0_value,scl_reg1_value,scl_reg2_value;                //scl_con,scl_h_factor,scl_v_factor,
	u32 scl_reg3_value,scl_reg4_value,scl_reg5_value,scl_reg6_value; //dsp_frame_hst,dsp_frame_vst,dsp_timing,dsp_act_timing
	u32 scl_reg7_value,scl_reg8_value;                               //dsp_hbor ,dsp_vbor
	u32 dst_frame_hst,dst_frame_vst;                    //时序缓存
	u32 dst_htotal,dst_hs_end,dst_hact_st,dst_hact_end; //屏幕typical h参数
	u32 dst_vtotal,dst_vs_end,dst_vact_st,dst_vact_end; //屏幕typical v参数

	u32 dsp_htotal,dsp_hs_end,dsp_hact_st,dsp_hact_end; //scaler输出的timing参数
	u32 dsp_vtotal,dsp_vs_end,dsp_vact_st,dsp_vact_end; 
	u32 dsp_hbor_end,dsp_hbor_st,dsp_vbor_end,dsp_vbor_st;
	u32 src_w,src_h,src_htotal,src_vtotal,dst_w,dst_h,src_hact_st,src_vact_st;
	u16 bor_right = 0;
	u16 bor_left = 0;
	u16 bor_up = 0;
	u16 bor_down = 0;
	u8 hor_down_mode = 0;  //1:average,0:bilinear
	u8 ver_down_mode = 0;
	u8 bic_coe_sel = 2;
	rk_screen *src;
	rk_screen *dst;
	int pll_id;

	struct rk616_route *route = &rk616->route;


	if(!route->scl_en)
	{
		val &= (~SCL_EN);	//disable scaler
		val |= (SCL_EN<<16);
		ret = rk616->write_dev(rk616,SCL_REG0,&val);
		return 0;
	}
	
	
	dst = screen;
	if(!dst)
	{
		dev_err(rk616->dev,"%s:screen is null!\n",__func__);
		return -EINVAL;
	}

	if(route->scl_bypass)
	{
		src = dst;
		dst->pll_cfg_val = 0x01422014;
		dst->frac = 0;
	}
	else
		src = screen->ext_screen;
	
	if(route->sclk_sel == SCLK_SEL(SCLK_SEL_PLL0))
		pll_id = 0;
	else
		pll_id = 1;
	rk616_pll_set_rate(rk616,pll_id,dst->pll_cfg_val,dst->frac);
	dst_frame_vst = dst->scl_vst;
	dst_frame_hst = dst->scl_hst;


#if 1

	src_htotal = src->hsync_len + src->left_margin + src->x_res + src->right_margin;
	src_vact_st = src->vsync_len + src->upper_margin  ;
	dst_vact_st = dst->vsync_len + dst->upper_margin;

	dsp_htotal    = dst->hsync_len + dst->left_margin + dst->x_res + dst->right_margin; //dst_htotal ;
	dsp_hs_end    = dst->hsync_len;

	dsp_vtotal    = dst->vsync_len + dst->upper_margin + dst->y_res + dst->lower_margin;
	dsp_vs_end    = dst->vsync_len;

	dsp_hbor_end  = dst->hsync_len + dst->left_margin + dst->x_res;
	dsp_hbor_st   = dst->hsync_len + dst->left_margin  ;
	dsp_vbor_end  = dst->vsync_len + dst->upper_margin + dst->y_res; //dst_vact_end ;
	dsp_vbor_st   = dst_vact_st  ;

	dsp_hact_st   = dsp_hbor_st  + bor_left;
	dsp_hact_end  = dsp_hbor_end - bor_right; 
	dsp_vact_st   = dsp_vbor_st  + bor_up;
	dsp_vact_end  = dsp_vbor_end - bor_down; 

	src_w = src->x_res;
	src_h = src->y_res;
	dst_w = dsp_hact_end - dsp_hact_st ;
	dst_h = dsp_vact_end - dsp_vact_st ;

	if(src_w > dst_w)         //判断hor的缩放模式 0：no_scl 1：scl_up 2：scl_down
	{
		scl_hor_mode = 0x2;   //scl_down
		if(hor_down_mode == 0)//bilinear
	    	{
			if((src_w-1)/(dst_w-1) > 2)
		    	{
				scl_h_factor = ((src_w-1)<<14)/(dst_w-1);
		    	}
			else
				scl_h_factor = ((src_w-2)<<14)/(dst_w-1);
		}
		else  //average
		{
			scl_h_factor = ((dst_w)<<16)/(src_w-1);
		}
	}
	else if(src_w == dst_w)
	{
		scl_hor_mode = 0x0;   //no_Scl
		scl_h_factor = 0x0;
	} 
	else 
	{
		scl_hor_mode = 0x1;   //scl_up
		scl_h_factor = ((src_w-1)<<16)/(dst_w-1);
	} 
    
	if(src_h > dst_h)         //判断ver的缩放模式 0：no_scl 1：scl_up 2：scl_down
	{
		scl_ver_mode = 0x2;   //scl_down
		if(ver_down_mode == 0)//bilinearhor_down_mode,u8 ver_down_mode
		{
			if((src_h-1)/(dst_h-1) > 2)
			{
				scl_v_factor = ((src_h-1)<<14)/(dst_h-1);
			}
			else
				scl_v_factor = ((src_h-2)<<14)/(dst_h-1);
		}
		else
		{
			scl_v_factor = ((dst_h)<<16)/(src_h-1);
		}
	}
	else if(src_h == dst_h)
	{
		scl_ver_mode = 0x0;   //no_Scl
		scl_v_factor = 0x0;
	}
	else 
	{
		scl_ver_mode = 0x1;   //scl_up
		scl_v_factor = ((src_h-1)<<16)/(dst_h-1);
	}

	//control   register0 
	scl_reg0_value = (0x1ff<<16) | SCL_EN | (scl_hor_mode<<1) |
			(scl_ver_mode<<3) | (bic_coe_sel<<5) | 
			(hor_down_mode<<7) | (ver_down_mode<<8) ;
	//factor    register1 
	scl_reg1_value = (scl_v_factor << 16) | scl_h_factor ;
	//dsp_frame register2 
	scl_reg2_value = dst_frame_vst<<16 | dst_frame_hst ;
	//dsp_h     register3
	scl_reg3_value = dsp_hs_end<<16 | dsp_htotal ;
	//dsp_hact  register4
	scl_reg4_value = dsp_hact_end <<16 | dsp_hact_st ;
	//dsp_v     register5
	scl_reg5_value = dsp_vs_end<<16 | dsp_vtotal ;
	//dsp_vact  register6
	scl_reg6_value = dsp_vact_end<<16 | dsp_vact_st ;
	//hbor      register7
	scl_reg7_value = dsp_hbor_end<<16 | dsp_hbor_st ;
	//vbor      register8
	scl_reg8_value = dsp_vbor_end<<16 | dsp_vbor_st ;

    
	rk616->write_dev(rk616,SCL_REG0,&scl_reg0_value);  
	rk616->write_dev(rk616,SCL_REG1,&scl_reg1_value);  
	rk616->write_dev(rk616,SCL_REG2,&scl_reg2_value);  
	rk616->write_dev(rk616,SCL_REG3,&scl_reg3_value);  
	rk616->write_dev(rk616,SCL_REG4,&scl_reg4_value);  
	rk616->write_dev(rk616,SCL_REG5,&scl_reg5_value);  
	rk616->write_dev(rk616,SCL_REG6,&scl_reg6_value);  
	rk616->write_dev(rk616,SCL_REG7,&scl_reg7_value);  
	rk616->write_dev(rk616,SCL_REG8,&scl_reg8_value);  
#endif
	return 0;
	
}


static int rk616_dual_input_cfg(struct mfd_rk616 *rk616,rk_screen *screen,
					bool enable)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	struct rk616_route *route = &rk616->route;
	
	route->vif0_bypass = VIF0_CLK_BYPASS;
	route->vif0_en     = 0;
 	route->vif0_clk_sel = VIF0_CLKIN_SEL(VIF_CLKIN_SEL_PLL0);
	route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);
	route->pll1_clk_sel = PLL1_CLK_SEL(MCLK_12M);
	route->vif1_clk_sel = VIF1_CLKIN_SEL(VIF_CLKIN_SEL_PLL1);
	route->hdmi_sel     = HDMI_IN_SEL(HDMI_CLK_SEL_VIF1);
	if(enable)  //hdmi plug in
	{
		route->vif1_bypass  = 0;
		route->vif1_en      = 1;
		
	}
	else  //hdmi plug out
	{
		route->vif1_bypass = VIF1_CLK_BYPASS;
		route->vif1_en     = 0;
	}

	route->sclin_sel   = SCL_IN_SEL(SCL_SEL_VIF0); //from vif0
	route->scl_en      = 0;            //dual lcdc, scaler not needed
	route->dither_sel  = DITHER_IN_SEL(DITHER_SEL_VIF0); //dither from vif0
	route->lcd1_input  = 1; 
	

	if(screen->type == SCREEN_RGB)
	{
		route->lvds_en	   = 1;
		route->lvds_mode   = RGB; //rgb output 
	}
	else if(screen->type == SCREEN_LVDS)
	{
		route->lvds_en	   = 1;
		route->lvds_mode = LVDS;
		route->lvds_ch_nr = pdata->lvds_ch_nr;
	}
	else if(screen->type == SCREEN_MIPI)
	{
		route->lvds_en = 0;
	}
	else
	{
		dev_err(rk616->dev,"un supported interface:%d\n",screen->type);
		return -EINVAL;
	}

	return 0;
	
}

static int rk616_lcd0_input_lcd1_unused_cfg(struct mfd_rk616 *rk616,rk_screen *screen,
							bool enable)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	struct rk616_route *route = &rk616->route;
	
	if(enable)  //hdmi plug in
	{
		route->vif0_bypass  = 0;
		route->vif0_en      = 1;
		route->vif0_clk_sel = VIF0_CLKIN_SEL(VIF_CLKIN_SEL_PLL0);
		route->sclin_sel    = SCL_IN_SEL(SCL_SEL_VIF0); //from vif0
		route->scl_en       = 1;
		route->sclk_sel     = SCLK_SEL(SCLK_SEL_PLL1);
		route->dither_sel   = DITHER_IN_SEL(DITHER_SEL_SCL); //dither from sclaer
		route->hdmi_sel     = HDMI_IN_SEL(HDMI_CLK_SEL_VIF0);//from vif0
		
	}
	else
	{
		route->vif0_bypass = VIF0_CLK_BYPASS;
		route->vif0_en     = 0;
		route->sclin_sel   = SCL_IN_SEL(SCL_SEL_VIF0); //from vif0
		route->scl_en      = 0;
		route->dither_sel  = DITHER_IN_SEL(DITHER_SEL_VIF0); //dither from sclaer
		route->hdmi_sel    = HDMI_IN_SEL(HDMI_CLK_SEL_VIF0);//from vif0
	}
	route->pll1_clk_sel = PLL1_CLK_SEL(LCD0_DCLK);
	route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);
	route->vif1_bypass = VIF1_CLK_BYPASS;
	route->vif1_en     = 0;
	route->lcd1_input  = 0;  
	
	if(screen->type == SCREEN_RGB)
	{
		route->lvds_en	   = 1;
		route->lvds_mode   = RGB; //rgb output 
	}
	else if(screen->type == SCREEN_LVDS)
	{
		route->lvds_en	   = 1;
		route->lvds_mode = LVDS;
		route->lvds_ch_nr = pdata->lvds_ch_nr;
	}
	else if(screen->type == SCREEN_MIPI)
	{
		route->lvds_en = 0;
	}
	else
	{
		dev_err(rk616->dev,"un supported interface:%d\n",screen->type);
		return -EINVAL;
	}

	return 0;
}


static int rk616_lcd0_input_lcd1_output_cfg(struct mfd_rk616 *rk616,rk_screen *screen,
							bool enable)
{
	struct rk616_route *route = &rk616->route;

	if(enable)
	{
		route->vif0_bypass  = 0;
		route->vif0_en      = 1;
		route->vif0_clk_sel = VIF0_CLKIN_SEL(VIF_CLKIN_SEL_PLL0);
		route->sclin_sel    = SCL_IN_SEL(SCL_SEL_VIF0); //from vif0
		route->scl_en       = 1;
		route->sclk_sel     = SCLK_SEL(SCLK_SEL_PLL1);
		route->dither_sel   = DITHER_IN_SEL(DITHER_SEL_SCL); //dither from sclaer
		route->hdmi_sel     = HDMI_IN_SEL(HDMI_CLK_SEL_VIF0);//from vif0
	}
	else
	{
		route->vif0_bypass = VIF0_CLK_BYPASS;
		route->vif0_en     = 0;
		route->sclin_sel   = SCL_IN_SEL(SCL_SEL_VIF0); //from vif0
		route->scl_en      = 0;
		route->dither_sel  = DITHER_IN_SEL(DITHER_SEL_VIF0); //dither from sclaer
		route->hdmi_sel    = HDMI_IN_SEL(HDMI_CLK_SEL_VIF0);//from vif0	
	}
	route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);
	route->pll1_clk_sel = PLL1_CLK_SEL(LCD0_DCLK);
	route->vif1_bypass = VIF1_CLK_BYPASS;
	route->vif1_en = 0;
	route->lcd1_input = 0; //lcd1 as out put
	route->lvds_en	= 0;

	return 0;
	
}


static int rk616_lcd0_unused_lcd1_input_cfg(struct mfd_rk616 *rk616,rk_screen *screen,
							bool enable)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	struct rk616_route *route = &rk616->route;

	route->pll0_clk_sel = PLL0_CLK_SEL(LCD1_DCLK);
	route->pll1_clk_sel = PLL1_CLK_SEL(LCD1_DCLK);
	route->vif0_bypass = VIF0_CLK_BYPASS;
	route->vif0_en     = 0;
	if(enable)
	{
		route->vif1_bypass = 0;
		route->vif1_en     = 1;
		route->scl_bypass  = 0;
	}
	else
	{
		route->vif1_bypass = VIF1_CLK_BYPASS;
		route->vif1_en     = 0;
		route->scl_bypass = 1; //1:1 scaler
	}
	route->vif1_clk_sel = VIF1_CLKIN_SEL(VIF_CLKIN_SEL_PLL1);
	route->sclin_sel   = SCL_IN_SEL(SCL_SEL_VIF1); //from vif1
	route->scl_en      = 1;
	route->sclk_sel    = SCLK_SEL(SCLK_SEL_PLL0);
	
	route->dither_sel  = DITHER_IN_SEL(DITHER_SEL_SCL); //dither from sclaer
	route->hdmi_sel    = HDMI_IN_SEL(HDMI_CLK_SEL_VIF1); //from vif1
	route->lcd1_input  = 1;  
	if(screen->type == SCREEN_RGB)
	{
		route->lvds_en	   = 1;
		route->lvds_mode   = RGB; //rgb output 
	}
	else if(screen->type == SCREEN_LVDS)
	{
		route->lvds_en = 1;
		route->lvds_mode = LVDS;
		route->lvds_ch_nr = pdata->lvds_ch_nr;
	}
	else if(screen->type == SCREEN_MIPI)
	{
		route->lvds_en = 0;
	}
	else
	{
		dev_err(rk616->dev,"un supported interface:%d\n",screen->type);
		return -EINVAL;
	}

	return 0;
}

static int  rk616_set_router(struct mfd_rk616 *rk616,rk_screen *screen,bool enable)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	int ret;

	if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == INPUT))
	{
		
		ret = rk616_dual_input_cfg(rk616,screen,enable);
		dev_info(rk616->dev,"rk616 use dual input for dual display!\n");
	}
	else if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == UNUSED))
	{
		ret = rk616_lcd0_input_lcd1_unused_cfg(rk616,screen,enable);

		dev_info(rk616->dev,
			"rk616 use lcd0 as input and lvds/rgb "
			"port as output for dual display\n");
	}
	else if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == OUTPUT))
	{
		ret = rk616_lcd0_input_lcd1_output_cfg(rk616,screen,enable);
		
		dev_info(rk616->dev,
			"rk616 use lcd0 as input and lcd1 as "
			"output for dual display\n");
	}
	else if((pdata->lcd0_func == UNUSED) && (pdata->lcd1_func == INPUT))
	{
		ret = rk616_lcd0_unused_lcd1_input_cfg(rk616,screen,enable);
		dev_info(rk616->dev,
			"rk616 use lcd1 as input and lvds/rgb as "
			"output for dual display\n");
	}
	else
	{
		dev_err(rk616->dev,
			"invalid configration,please check your"
			"rk616_platform_data setting in your board file!\n");
		return -EINVAL;
	}

	return ret ;
	
}


static int rk616_lvds_cfg(struct mfd_rk616 *rk616,rk_screen *screen)
{
	struct rk616_route *route = &rk616->route;
	u32 val = 0;
	int ret;
	int odd = (screen->left_margin&0x01)?0:1;
	
	if(!route->lvds_en)  //lvds port is not used ,power down lvds
	{
		val &= ~(LVDS_CH1TTL_EN | LVDS_CH0TTL_EN | LVDS_CH1_PWR_EN |
			LVDS_CH0_PWR_EN | LVDS_CBG_PWR_EN);
		val |= LVDS_PLL_PWR_DN | (LVDS_CH1TTL_EN << 16) | (LVDS_CH0TTL_EN << 16) |
			(LVDS_CH1_PWR_EN << 16) | (LVDS_CH0_PWR_EN << 16) |
			(LVDS_CBG_PWR_EN << 16) | (LVDS_PLL_PWR_DN << 16);
		ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);

		if(!route->lcd1_input)  //set lcd1 port for output as RGB interface
		{
			val = (LCD1_INPUT_EN << 16);
			ret = rk616->write_dev(rk616,CRU_IO_CON0,&val);
		}
	}
	else
	{
		if(route->lvds_mode)  //lvds mode
		{

			if(route->lvds_ch_nr == 2) //dual lvds channel
			{
				val = 0;
				val &= ~(LVDS_CH0TTL_EN | LVDS_CH1TTL_EN | LVDS_PLL_PWR_DN);
				val = (LVDS_DCLK_INV)|(LVDS_CH1_PWR_EN) |(LVDS_CH0_PWR_EN) | LVDS_HBP_ODD(odd) |
					(LVDS_CBG_PWR_EN) | (LVDS_CH_SEL) | (LVDS_OUT_FORMAT(screen->hw_format)) | 
					(LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH1_PWR_EN << 16) | 
					(LVDS_CH0_PWR_EN << 16) | (LVDS_CBG_PWR_EN << 16) | (LVDS_CH_SEL << 16) | 
					(LVDS_OUT_FORMAT_MASK) | (LVDS_DCLK_INV << 16) | (LVDS_PLL_PWR_DN << 16) |
					(LVDS_HBP_ODD_MASK);
				ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);
				
				dev_info(rk616->dev,"rk616 use dual lvds channel.......\n");
			}
			else //single lvds channel
			{
				val = 0;
				val &= ~(LVDS_CH0TTL_EN | LVDS_CH1TTL_EN | LVDS_CH1_PWR_EN | LVDS_PLL_PWR_DN | LVDS_CH_SEL); //use channel 0
				val |= (LVDS_CH0_PWR_EN) |(LVDS_CBG_PWR_EN) | (LVDS_OUT_FORMAT(screen->hw_format)) | 
				      (LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH0_PWR_EN << 16) | 
				       (LVDS_DCLK_INV ) | (LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH0_PWR_EN << 16) | 
				        (LVDS_CBG_PWR_EN << 16)|(LVDS_CH_SEL << 16) | (LVDS_PLL_PWR_DN << 16)| 
				       (LVDS_OUT_FORMAT_MASK) | (LVDS_DCLK_INV << 16);
				ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);

				dev_info(rk616->dev,"rk616 use single lvds channel.......\n");
				
			}

		}
		else //mux lvds port to RGB mode
		{
			val &= ~(LVDS_CBG_PWR_EN| LVDS_CH1_PWR_EN | LVDS_CH0_PWR_EN);
			val |= (LVDS_CH0TTL_EN)|(LVDS_CH1TTL_EN )|(LVDS_PLL_PWR_DN)|
				(LVDS_CH0TTL_EN<< 16)|(LVDS_CH1TTL_EN<< 16)|(LVDS_CH1_PWR_EN << 16) | 
				(LVDS_CH0_PWR_EN << 16)|(LVDS_CBG_PWR_EN << 16)|(LVDS_PLL_PWR_DN << 16);
			ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);

			val &= ~(LVDS_OUT_EN);
			val |= (LVDS_OUT_EN << 16);
			ret = rk616->write_dev(rk616,CRU_IO_CON0,&val);
			dev_info(rk616->dev,"rk616 use RGB output.....\n");
			
		}
	}

	return 0;
	
}


static int rk616_dither_cfg(struct mfd_rk616 *rk616,rk_screen *screen,bool enable)
{
	u32 val = 0;
	int ret = 0;
	val = FRC_DCLK_INV | (FRC_DCLK_INV << 16);
	if((screen->face != OUT_P888) && enable)  //enable frc dither if the screen is not 24bit
		val |= FRC_DITHER_EN | (FRC_DITHER_EN << 16);
	else
		val |= (FRC_DITHER_EN << 16);
	ret = rk616->write_dev(rk616,FRC_REG,&val);

	return 0;
	
}

static int rk616_router_cfg(struct mfd_rk616 *rk616)
{
	u32 val;
	int ret;
	struct rk616_route *route = &rk616->route;
	val = (route->pll0_clk_sel) | (route->pll1_clk_sel) |
		PLL1_CLK_SEL_MASK | PLL0_CLK_SEL_MASK; //pll1 clk from lcdc1_dclk,pll0 clk from lcdc0_dclk,mux_lcdx = lcdx_clk
	ret = rk616->write_dev(rk616,CRU_CLKSEL0_CON,&val);
	
	val = (route->sclk_sel) | SCLK_SEL_MASK;
	ret = rk616->write_dev(rk616,CRU_CLKSEL1_CON,&val);
	
	val = (SCL_IN_SEL_MASK) | (DITHER_IN_SEL_MASK) | (HDMI_IN_SEL_MASK) | 
		(VIF1_CLKIN_SEL_MASK) | (VIF0_CLKIN_SEL_MASK) | (VIF1_CLK_BYPASS << 16) | 
		(VIF0_CLK_BYPASS << 16) |(route->sclin_sel) | (route->dither_sel) | 
		(route->hdmi_sel) | (route->vif1_bypass) | (route->vif0_bypass) |
		(route->vif1_clk_sel)| (route->vif0_clk_sel); 
	ret = rk616->write_dev(rk616,CRU_CLKSE2_CON,&val);

	return ret;
}
static int rk616_display_router_cfg(struct mfd_rk616 *rk616,rk_screen *screen,bool enable)
{
	int ret;
	
	ret = rk616_set_router(rk616,screen,enable);
	if(ret < 0)
		return ret;
	ret = rk616_router_cfg(rk616);
	ret = rk616_dither_cfg(rk616,screen,enable);
	ret = rk616_lvds_cfg(rk616,screen);
	ret = rk616_vif_cfg(rk616,screen,0);
	ret = rk616_vif_cfg(rk616,screen,1);
	ret = rk616_scaler_cfg(rk616,screen);

	return 0;
	
}


int rk610_lcd_scaler_set_param(rk_screen *screen,bool enable )//enable:0 bypass 1: scale
{
	int ret;
	struct mfd_rk616 *rk616 = g_vif->rk616;
	if(!rk616)
	{
		printk(KERN_ERR "%s:mfd rk616 is null!\n",__func__);
		return -1;
	}
	g_vif->screen = screen;
	ret = rk616_display_router_cfg(rk616,screen,enable);
	return ret;
}

int rk616_set_vif(rk_screen *screen,bool connect)
{
	
	struct rk616_platform_data *pdata;
	rk_screen *lcd_screen = g_vif->screen;
	struct mfd_rk616 *rk616 = g_vif->rk616;
	if(!rk616 || !lcd_screen)
	{
		printk(KERN_ERR "%s:mfd rk616 is null!\n",__func__);
		return -1;
	}
	else
	{
		pdata = rk616->pdata;
		lcd_screen->ext_screen = screen;
	}
	
	if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == INPUT))
	{
		
		rk610_lcd_scaler_set_param(lcd_screen,connect);
	}

	return 0;
	
	
}


#if	defined(CONFIG_HAS_EARLYSUSPEND)
static void rk616_vif_early_suspend(struct early_suspend *h)
{
	struct rk616_vif *vif = container_of(h, struct rk616_vif,early_suspend);
	struct mfd_rk616 *rk616 = vif->rk616;
	u32 val = 0;
	int ret = 0;

	val &= ~(LVDS_CH1_PWR_EN | LVDS_CH0_PWR_EN | LVDS_CBG_PWR_EN);
	val |= LVDS_PLL_PWR_DN |(LVDS_CH1_PWR_EN << 16) | (LVDS_CH0_PWR_EN << 16) |
		(LVDS_CBG_PWR_EN << 16) | (LVDS_PLL_PWR_DN << 16);
	ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);
	
}

static void rk616_vif_late_resume(struct early_suspend *h)
{
	struct rk616_vif *vif = container_of(h, struct rk616_vif,early_suspend);
	struct mfd_rk616 *rk616 = vif->rk616;
	rk616_lvds_cfg(rk616,vif->screen);
}

#endif

static int rk616_vif_probe(struct platform_device *pdev)
{
	struct rk616_vif *vif = NULL; 
	struct mfd_rk616 *rk616 = NULL;

	vif = kzalloc(sizeof(struct rk616_vif),GFP_KERNEL);
	if(!vif)
	{
		printk(KERN_ALERT "alloc for struct rk616_vif fail\n");
		return  -ENOMEM;
	}

	rk616 = dev_get_drvdata(pdev->dev.parent);
	if(!rk616)
	{
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		return -ENODEV;
	}
	else
		g_vif = vif;
		vif->rk616 = rk616;

#ifdef CONFIG_HAS_EARLYSUSPEND
	vif->early_suspend.suspend = rk616_vif_early_suspend;
	vif->early_suspend.resume = rk616_vif_late_resume;
    	vif->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	register_early_suspend(&vif->early_suspend);
#endif
	

	dev_info(&pdev->dev,"rk616 vif probe success!\n");

	return 0;
	
}

static int rk616_vif_remove(struct platform_device *pdev)
{
	
	return 0;
}

static void rk616_vif_shutdown(struct platform_device *pdev)
{
	
	return;
}

static struct platform_driver rk616_lvds_driver = {
	.driver		= {
		.name	= "rk616-vif",
		.owner	= THIS_MODULE,
	},
	.probe		= rk616_vif_probe,
	.remove		= rk616_vif_remove,
	.shutdown	= rk616_vif_shutdown,
};

static int __init rk616_lvds_init(void)
{
	return platform_driver_register(&rk616_lvds_driver);
}
subsys_initcall_sync(rk616_lvds_init);

static void __exit rk616_lvds_exit(void)
{
	platform_driver_unregister(&rk616_lvds_driver);
}
module_exit(rk616_lvds_exit);

