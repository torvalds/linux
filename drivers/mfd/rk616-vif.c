#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mfd/rk616.h>



extern int rk616_pll_set_rate(struct mfd_rk616 *rk616,int id,u32 cfg_val,u32 frac);
extern int rk616_pll_pwr_down(struct mfd_rk616 *rk616,int id);


/*rk616 video interface config*/

 int rk616_vif_disable(struct mfd_rk616 *rk616,int id)
{
	u32 val = 0;
	int ret = 0;
	
	if(id == 0) //video interface 0
	{
			val = (VIF0_EN << 16); //disable vif0
			ret = rk616->write_dev(rk616,VIF0_REG0,&val);
		
	}
	else       //vide0 interface 1
	{
			val = (VIF0_EN << 16); //disabl VIF1
			ret = rk616->write_dev(rk616,VIF1_REG0,&val);
			
	}
	
	msleep(21);
	
	if(id == 0) //video interface 0
	{
			val = VIF0_CLK_GATE | (VIF0_CLK_GATE << 16); //gating vif0
			ret = rk616->write_dev(rk616,CRU_CLKSEL2_CON,&val);
		
	}
	else       //vide0 interface 1
	{
			val = VIF1_CLK_GATE | (VIF1_CLK_GATE << 16); //gating vif1
			ret = rk616->write_dev(rk616,CRU_CLKSEL2_CON,&val);
			
	}

	rk616_dbg(rk616->dev,"rk616 vif%d disable\n",id);
	
	return 0;
}


int rk616_vif_enable(struct mfd_rk616 *rk616,int id)
{
	u32 val = 0;
	u32 offset = 0;
	int ret;

	
	if(id == 0)
	{
		val = (VIF0_CLK_BYPASS << 16) | (VIF0_CLK_GATE << 16);
		offset = 0;
	}
	else
	{
		val = (VIF1_CLK_BYPASS << 16) |(VIF1_CLK_GATE << 16);
		offset = 0x18;
	}

	ret = rk616->write_dev(rk616,CRU_CLKSEL2_CON,&val);
	
	val = 0;
	val |= (VIF0_DDR_CLK_EN <<16) | (VIF0_DDR_PHASEN_EN << 16) | (VIF0_DDR_MODE_EN << 16)|
		(VIF0_EN <<16) | VIF0_EN; //disable ddr mode,enable VIF
	ret = rk616->write_dev(rk616,VIF0_REG0 + offset,&val);

	
	rk616_dbg(rk616->dev,"rk616 vif%d enable\n",id);

	return 0;
	
}
static int  rk616_vif_bypass(struct mfd_rk616 *rk616,int id)
{
	u32 val = 0;
	int ret;

	if(id == 0)
	{
		val = (VIF0_CLK_BYPASS | VIF0_CLK_BYPASS << 16);
	}
	else
	{
		val = (VIF1_CLK_BYPASS | VIF1_CLK_BYPASS << 16);
	}

	ret = rk616->write_dev(rk616,CRU_CLKSEL2_CON,&val);

	rk616_dbg(rk616->dev,"rk616 vif%d bypass\n",id);
	return 0;
}

static bool pll_sel_mclk12m(struct mfd_rk616 *rk616,int pll_id)
{
	if(pll_id == 0) //pll0
	{
		if(rk616->route.pll0_clk_sel == PLL0_CLK_SEL(MCLK_12M))
			return true;
		else
			return false;
	}
	else
	{
		if(rk616->route.pll1_clk_sel == PLL1_CLK_SEL(MCLK_12M))
			return  true;
		else
			return false;	
	}

	return false;
}



int rk616_vif_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen,int id)
{
	int ret = 0;
	u32 val = 0;
	int offset = 0;
	int pll_id;
	bool pll_use_mclk12m = false;
	
	if(id == 0) //video interface 0
	{
		if(!rk616->route.vif0_en)
		{
			rk616_vif_disable(rk616,id);
			return 0;
		}
		offset = 0;
		pll_id = rk616->route.vif0_clk_sel;
	}
	else       //vide0 interface 1
	{
		if(!rk616->route.vif1_en)
		{
			rk616_vif_disable(rk616,id);
			return 0;
		}
		offset = 0x18;
		pll_id = (rk616->route.vif1_clk_sel >> 6);
		
	}

	pll_use_mclk12m = pll_sel_mclk12m(rk616,pll_id);
	
	if(pll_use_mclk12m)
	{
		//clk_set_rate(rk616->mclk, 12000000);
		rk616_mclk_set_rate(rk616->mclk,12000000);
	}

	
	if(!screen)
	{
		dev_err(rk616->dev,"%s:screen is null.........\n",__func__);
		return -EINVAL;
	}


	rk616_vif_disable(rk616,id);
	if( (screen->mode.xres == 1920) && (screen->mode.yres == 1080))
	{
		if(pll_use_mclk12m)
			//rk616_pll_set_rate(rk616,pll_id,0xc11025,0x200000);
			rk616_pll_set_rate(rk616,pll_id,0x028853de,0);
		else
			rk616_pll_set_rate(rk616,pll_id,0x02bf5276,0);
		
		val = (0xc1) | (0x01 <<16);
	}
	else if((screen->mode.xres == 1280) && (screen->mode.yres == 720))
	{
		if(pll_use_mclk12m)
			//rk616_pll_set_rate(rk616,pll_id,0x01811025,0x200000);
			rk616_pll_set_rate(rk616,pll_id,0x0288418c,0);
		else
			rk616_pll_set_rate(rk616,pll_id,0x1422014,0);
		
		val = (0xc1) | (0x01 <<16);
	
	}
	else if((screen->mode.xres == 720))
	{
		if(pll_use_mclk12m )
		{
			rk616_pll_set_rate(rk616,pll_id,0x0306510e,0);
		}
		else
			rk616_pll_set_rate(rk616,pll_id,0x1c13015,0);
		
		val = (0x1) | (0x01 <<16);
	}

	
	
	ret = rk616->write_dev(rk616,VIF0_REG1 + offset,&val);

	val = (screen->mode.hsync_len << 16) | (screen->mode.hsync_len + screen->mode.left_margin + 
		screen->mode.right_margin + screen->mode.xres);
	ret = rk616->write_dev(rk616,VIF0_REG2 + offset,&val);

	
	val = ((screen->mode.hsync_len + screen->mode.left_margin + screen->mode.xres)<<16) |
		(screen->mode.hsync_len + screen->mode.left_margin);
	ret = rk616->write_dev(rk616,VIF0_REG3 + offset,&val);

	val = (screen->mode.vsync_len << 16) | (screen->mode.vsync_len + screen->mode.upper_margin + 
		screen->mode.lower_margin + screen->mode.yres);
	ret = rk616->write_dev(rk616,VIF0_REG4 + offset,&val);


	val = ((screen->mode.vsync_len + screen->mode.upper_margin + screen->mode.yres)<<16) |
		(screen->mode.vsync_len + screen->mode.upper_margin);
	ret = rk616->write_dev(rk616,VIF0_REG5 + offset,&val);

	if(id == 0)
	{
		val = VIF0_SYNC_EN | (VIF0_SYNC_EN << 16);
		rk616->write_dev(rk616,CRU_IO_CON0,&val);
	}
	else
	{
		val = VIF1_SYNC_EN | (VIF1_SYNC_EN << 16);
		rk616->write_dev(rk616,CRU_IO_CON0,&val);
	}
	rk616_vif_enable(rk616,id);
	
	return ret;
	
}


static int rk616_scaler_disable(struct mfd_rk616 *rk616)
{
	u32 val = 0;
	int ret;
	val &= (~SCL_EN);	//disable scaler
	val |= (SCL_EN<<16);
	ret = rk616->write_dev(rk616,SCL_REG0,&val);
	rk616_dbg(rk616->dev,"rk616 scaler disable\n");
	return 0;
}

int rk616_scaler_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen)
{
	u32 scl_hor_mode,scl_ver_mode;
	u32 scl_v_factor,scl_h_factor;
	u32 scl_reg0_value,scl_reg1_value,scl_reg2_value;                //scl_con,scl_h_factor,scl_v_factor,
	u32 scl_reg3_value,scl_reg4_value,scl_reg5_value,scl_reg6_value; //dsp_frame_hst,dsp_frame_vst,dsp_timing,dsp_act_timing
	u32 scl_reg7_value,scl_reg8_value;                               //dsp_hbor ,dsp_vbor
	u32 dst_frame_hst,dst_frame_vst;                    //时序缓存
	u32 dst_vact_st;

	u32 dsp_htotal,dsp_hs_end,dsp_hact_st,dsp_hact_end; //scaler输出的timing参数
	u32 dsp_vtotal,dsp_vs_end,dsp_vact_st,dsp_vact_end; 
	u32 dsp_hbor_end,dsp_hbor_st,dsp_vbor_end,dsp_vbor_st;
	u32 src_w,src_h,src_htotal,dst_w,dst_h,src_vact_st;
	u16 bor_right = 0;
	u16 bor_left = 0;
	u16 bor_up = 0;
	u16 bor_down = 0;
	u8 hor_down_mode = 0;  //1:average,0:bilinear
	u8 ver_down_mode = 0;
	u8 bic_coe_sel = 2;
	struct rk_screen *src;
	struct rk_screen *dst;
	int pll_id;

	struct rk616_route *route = &rk616->route;


	if(!route->scl_en)
	{
		rk616_scaler_disable(rk616);
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

	rk616_scaler_disable(rk616);
	rk616_pll_set_rate(rk616,pll_id,dst->pll_cfg_val,dst->frac);
	dst_frame_vst = dst->scl_vst;
	dst_frame_hst = dst->scl_hst;


#if 1

	src_htotal = src->mode.hsync_len + src->mode.left_margin + src->mode.xres + src->mode.right_margin;
	src_vact_st = src->mode.vsync_len + src->mode.upper_margin  ;
	dst_vact_st = dst->mode.vsync_len + dst->mode.upper_margin;

	dsp_htotal    = dst->mode.hsync_len + dst->mode.left_margin + dst->mode.xres + dst->mode.right_margin; //dst_htotal ;
	dsp_hs_end    = dst->mode.hsync_len;

	dsp_vtotal    = dst->mode.vsync_len + dst->mode.upper_margin + dst->mode.yres + dst->mode.lower_margin;
	dsp_vs_end    = dst->mode.vsync_len;

	dsp_hbor_end  = dst->mode.hsync_len + dst->mode.left_margin + dst->mode.xres;
	dsp_hbor_st   = dst->mode.hsync_len + dst->mode.left_margin  ;
	dsp_vbor_end  = dst->mode.vsync_len + dst->mode.upper_margin + dst->mode.yres; //dst_vact_end ;
	dsp_vbor_st   = dst_vact_st  ;

	dsp_hact_st   = dsp_hbor_st  + bor_left;
	dsp_hact_end  = dsp_hbor_end - bor_right; 
	dsp_vact_st   = dsp_vbor_st  + bor_up;
	dsp_vact_end  = dsp_vbor_end - bor_down; 

	src_w = src->mode.xres;
	src_h = src->mode.yres;
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
 
	rk616->write_dev(rk616,SCL_REG1,&scl_reg1_value);  
	rk616->write_dev(rk616,SCL_REG2,&scl_reg2_value);  
	rk616->write_dev(rk616,SCL_REG3,&scl_reg3_value);  
	rk616->write_dev(rk616,SCL_REG4,&scl_reg4_value);  
	rk616->write_dev(rk616,SCL_REG5,&scl_reg5_value);  
	rk616->write_dev(rk616,SCL_REG6,&scl_reg6_value);  
	rk616->write_dev(rk616,SCL_REG7,&scl_reg7_value);  
	rk616->write_dev(rk616,SCL_REG8,&scl_reg8_value);
	rk616->write_dev(rk616,SCL_REG0,&scl_reg0_value); 

	rk616_dbg(rk616->dev,"rk616 scaler enable\n");
#endif
	return 0;
	
}


static int rk616_dual_input_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen,
					bool enable)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	struct rk616_route *route = &rk616->route;
	
	route->vif0_bypass = VIF0_CLK_BYPASS;
	route->vif0_en     = 0;
 	route->vif0_clk_sel = VIF0_CLKIN_SEL(VIF_CLKIN_SEL_PLL0);
	route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);

#if defined(CONFIG_RK616_USE_MCLK_12M)
	route->pll1_clk_sel = PLL1_CLK_SEL(MCLK_12M);
#else
	route->pll1_clk_sel = PLL1_CLK_SEL(LCD1_DCLK);
#endif

	route->vif1_clk_sel = VIF1_CLKIN_SEL(VIF_CLKIN_SEL_PLL1);
	route->hdmi_sel     = HDMI_IN_SEL(HDMI_IN_SEL_VIF1);
	route->hdmi_clk_sel = HDMI_CLK_SEL(HDMI_CLK_SEL_VIF1);
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
	

	return 0;
	
}

static int rk616_lcd0_input_lcd1_unused_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen,
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
		route->hdmi_sel     = HDMI_IN_SEL(HDMI_IN_SEL_VIF0);//from vif0
		route->hdmi_clk_sel = HDMI_CLK_SEL(HDMI_CLK_SEL_VIF0);	
	}
	else
	{
		route->vif0_bypass = VIF0_CLK_BYPASS;
		route->vif0_en     = 0;
		route->sclin_sel   = SCL_IN_SEL(SCL_SEL_VIF0); //from vif0
		route->scl_en      = 0;
		route->dither_sel  = DITHER_IN_SEL(DITHER_SEL_VIF0); //dither from sclaer
		route->hdmi_sel    = HDMI_IN_SEL(HDMI_IN_SEL_VIF0);//from vif0
	}
	route->pll1_clk_sel = PLL1_CLK_SEL(LCD0_DCLK);
	//route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);

#if defined(CONFIG_RK616_USE_MCLK_12M)
	route->pll0_clk_sel = PLL0_CLK_SEL(MCLK_12M);
#else
	route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);
#endif
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
	

	return 0;
}


static int rk616_lcd0_input_lcd1_output_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen,
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
		route->hdmi_sel     = HDMI_IN_SEL(HDMI_IN_SEL_VIF0);//from vif0
		route->hdmi_clk_sel = HDMI_CLK_SEL(HDMI_CLK_SEL_VIF0);
	}
	else
	{
		route->vif0_bypass = VIF0_CLK_BYPASS;
		route->vif0_en     = 0;
		route->sclin_sel   = SCL_IN_SEL(SCL_SEL_VIF0); //from vif0
		route->scl_en      = 0;
		route->dither_sel  = DITHER_IN_SEL(DITHER_SEL_VIF0); //dither from sclaer
		route->hdmi_sel    = HDMI_IN_SEL(HDMI_IN_SEL_VIF0);//from vif0
		route->hdmi_clk_sel = HDMI_CLK_SEL(HDMI_CLK_SEL_VIF1);
	}
	//route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);
	route->pll1_clk_sel = PLL1_CLK_SEL(LCD0_DCLK);

#if defined(CONFIG_RK616_USE_MCLK_12M)
	route->pll0_clk_sel = PLL0_CLK_SEL(MCLK_12M);
#else
	route->pll0_clk_sel = PLL0_CLK_SEL(LCD0_DCLK);
#endif
	route->vif1_bypass = VIF1_CLK_BYPASS;
	route->vif1_en = 0;
	route->lcd1_input = 0; //lcd1 as out put
	route->lvds_en	= 0;

	//route->scl_en      = 0;
	//route->dither_sel  = DITHER_IN_SEL(DITHER_SEL_VIF0);

	return 0;
	
}


static int rk616_lcd0_unused_lcd1_input_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen,
							bool enable)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	struct rk616_route *route = &rk616->route;

	route->pll0_clk_sel = PLL0_CLK_SEL(LCD1_DCLK);
//	route->pll1_clk_sel = PLL1_CLK_SEL(LCD1_DCLK);
#if defined(CONFIG_RK616_USE_MCLK_12M)
	route->pll1_clk_sel = PLL1_CLK_SEL(MCLK_12M);
#else
	route->pll1_clk_sel = PLL1_CLK_SEL(LCD1_DCLK);
#endif
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
	route->hdmi_sel    = HDMI_IN_SEL(HDMI_IN_SEL_VIF1); //from vif1
	route->hdmi_clk_sel = HDMI_CLK_SEL(HDMI_CLK_SEL_VIF1);
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
	

	return 0;
}

int  rk616_set_router(struct mfd_rk616 *rk616,struct rk_screen *screen,bool enable)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	int ret;

	if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == INPUT))
	{
		
		ret = rk616_dual_input_cfg(rk616,screen,enable);
		rk616_dbg(rk616->dev,"rk616 use dual input for dual display!\n");
	}
	else if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == UNUSED))
	{
		ret = rk616_lcd0_input_lcd1_unused_cfg(rk616,screen,enable);

		rk616_dbg(rk616->dev,
			"rk616 use lcd0 as input and lvds/rgb "
			"port as output for dual display\n");
	}
	else if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == OUTPUT))
	{
		ret = rk616_lcd0_input_lcd1_output_cfg(rk616,screen,enable);
		
		rk616_dbg(rk616->dev,
			"rk616 use lcd0 as input and lcd1 as "
			"output for dual display\n");
	}
	else if((pdata->lcd0_func == UNUSED) && (pdata->lcd1_func == INPUT))
	{
		ret = rk616_lcd0_unused_lcd1_input_cfg(rk616,screen,enable);
		rk616_dbg(rk616->dev,
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
	ret = rk616->write_dev(rk616,CRU_CLKSEL2_CON,&val);
	val = route->hdmi_clk_sel;
	ret = rk616->write_dev_bits(rk616,CRU_CFGMISC_CON,HDMI_CLK_SEL_MASK,&val);

	return ret;
}


static int rk616_dither_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen,bool enable)
{
	u32 val = 0;
	int ret = 0;

	if(screen->type != SCREEN_RGB) //if RGB screen , not invert D_CLK
		val = FRC_DCLK_INV | (FRC_DCLK_INV << 16);
	
	if((screen->face != OUT_P888) && enable)  //enable frc dither if the screen is not 24bit
		val |= FRC_DITHER_EN | (FRC_DITHER_EN << 16);
		//val |= (FRC_DITHER_EN << 16);
	else
		val |= (FRC_DITHER_EN << 16);
	ret = rk616->write_dev(rk616,FRC_REG,&val);

	return 0;
	
}

int rk616_display_router_cfg(struct mfd_rk616 *rk616,struct rk_screen *screen,bool enable)
{
	int ret;
	struct rk_screen *hdmi_screen = screen->ext_screen;
	ret = rk616_set_router(rk616,screen,enable);
	if(ret < 0)
		return ret;
	ret = rk616_router_cfg(rk616);
	
	/*
		If wake up, does not execute the rk616_vif_cfg can save 50ms time
	*/
	if(rk616->resume != 1){
		ret = rk616_vif_cfg(rk616,hdmi_screen,0);
		ret = rk616_vif_cfg(rk616,hdmi_screen,1);
	}

	ret = rk616_scaler_cfg(rk616,screen);			
	ret = rk616_dither_cfg(rk616,screen,enable);
	return 0;
	
}

int rk616_set_vif(struct mfd_rk616 *rk616,struct rk_screen *screen,bool connect)
{
	struct rk616_platform_data *pdata;
	if(!rk616)
	{
		printk(KERN_ERR "%s:mfd rk616 is null!\n",__func__);
		return -1;
	}
	else
	{
		pdata = rk616->pdata;
	}

	if(!connect)
	{
		rk616_vif_disable(rk616,0);
		rk616_vif_disable(rk616,1);
                rk616_mclk_set_rate(rk616->mclk, 11289600);
		return 0;
	}
#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)
	return 0;
#else
	if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == INPUT))
	{
		
		rk616_dual_input_cfg(rk616,screen,connect);
		rk616_dbg(rk616->dev,"rk616 use dual input for dual display!\n");
	}
	else if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == UNUSED))
	{
		rk616_lcd0_input_lcd1_unused_cfg(rk616,screen,connect);
		rk616_dbg(rk616->dev,"rk616 use lcd0 input for hdmi display!\n");
	}
	rk616_router_cfg(rk616);
	rk616_vif_cfg(rk616,screen,0);
	rk616_vif_cfg(rk616,screen,1);
	rk616_scaler_disable(rk616);
#endif
	
	return 0;
	
	
}




