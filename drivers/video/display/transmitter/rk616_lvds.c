#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/rk616.h>
#include <linux/rk_fb.h>

struct mfd_rk616 *g_rk616;

/*rk616 video interface config*/
static int rk616_vif_cfg(struct mfd_rk616 *rk616,rk_screen *screen,int id)
{
	int ret = 0;
	u32 val = 0;
	int offset = 0;
	if(id == 0) //video interface 0
	{
		if(!rk616->route.vif0_en)
		{
			val = (VIF0_EN << 16); //disable vif0
			ret = rk616->write_dev(rk616,VIF0_REG0,&val);
			return 0;
		}
		offset = 0;
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
	}
	
	val |= (VIF0_DDR_CLK_EN <<16) | (VIF0_DDR_PHASEN_EN << 16) | (VIF0_DDR_MODE_EN << 16)|
		(VIF0_EN <<16) | VIF0_EN; //disable ddr mode,enable VIF
	
	ret = rk616->write_dev(rk616,VIF0_REG0 + offset,&val);	

	val = (1) | (1<<16);
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
	u32 pll_value,T_frm_st;
	u16 bor_right = 0;
	u16 bor_left = 0;
	u16 bor_up = 0;
	u16 bor_down = 0;
	u8 hor_down_mode = 1;  //default use Proprietary averaging filter
	u8 ver_down_mode = 1;
	u8 bic_coe_sel = 2;

	double fk = 1.0;
	struct rk616_route *route = &rk616->route;
#if 0
	rk_screen *src = screen->ext_screen;
	rk_screen *dst = screen;
	u32 DCLK_IN = src->pixclock;
#endif

	if(!route->scl_en)
	{
		val &= (~SCL_EN);	//disable scaler
		val |= (SCL_EN<<16);
		ret = rk616->write_dev(rk616,SCL_REG0,&val);
		return 0;
	}
	

#if 0

	src_htotal = src->hsync_len + src->left_margin + src->x_res + src->right_margin;
	src_vact_st = src->hsync_len + src->left_margin  ;

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


	pll_value = (DCLK_IN*dsp_htotal*(dsp_vact_end-dsp_vact_st))/(src_htotal*src_h);
	dev_info(rk616->dev,"SCALER CLK Frq  =%u\n",pll_value);

	T_frm_st = ((src_vact_st + 7)*src_htotal/DCLK_IN) -(dst_vact_st*dst_htotal)/pll_value;
	if(T_frm_st <0)
	{
		T_frm_st = (src_htotal * src_vtotal)/DCLK_IN + T_frm_st;
	}
	 
	dst_frame_vst = (T_frm_st * DCLK_IN) / src_htotal ;
	dst_frame_hst = (u32)(T_frm_st * DCLK_IN) % src_htotal ;    //dst

	dev_info(rk616->dev,"scaler:dst_frame_h_st =%d\n"
		"dst_frame_v_st =%d\n",
		dst_frame_hst,dst_frame_vst);


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
static int  rk616_set_router(struct mfd_rk616 *rk616,rk_screen *screen)
{
	struct rk616_platform_data *pdata = rk616->pdata;
	struct rk616_route *route = &rk616->route;
	if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == INPUT))
	{
		route->vif0_bypass = 1;
		route->vif0_en     = 0;
		route->vif1_bypass = 1;
		route->vif1_en     = 0;
		route->sclin_sel   = 0;
		route->scl_en      = 0;
		route->dither_sel  = 0; //dither from vif0
		route->hdmi_sel    = 0;
		route->lcd1_input  = 1; 
		route->lvds_en	   = 1;
		if(screen->type == SCREEN_RGB)
		{
			route->lvds_mode   = 0; //rgb output 
		}
		else if(screen->type == SCREEN_LVDS)
		{
			route->lvds_mode = 1;
			route->lvds_ch_nr = pdata->lvds_ch_nr;
		}
		else
		{
			dev_err(rk616->dev,"un supported interface:%d\n",screen->type);
			return -EINVAL;
		}
			
		dev_info(rk616->dev,"rk616 use dual input for dual display!\n");
	}
	else if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == UNUSED))
	{
		route->vif0_bypass = 1;
		route->vif0_en     = 0;
		route->vif1_bypass = 1;
		route->vif1_en     = 0;
		route->sclin_sel   = 0; //from vif0
		route->scl_en      = 1;
		route->dither_sel  = 1; //dither from sclaer
		route->hdmi_sel    = 2;//from vif0
		route->lcd1_input  = 0;  
		route->lvds_en	   = 1;
		if(screen->type == SCREEN_RGB)
		{
			route->lvds_mode   = 0; //rgb output 
		}
		else if(screen->type == SCREEN_LVDS)
		{
			route->lvds_mode = 1;
			route->lvds_ch_nr = pdata->lvds_ch_nr;
		}
		else
		{
			dev_err(rk616->dev,"un supported interface:%d\n",screen->type);
			return -EINVAL;
		}
			
		dev_info(rk616->dev,
			"rk616 use lcd0 as input and lvds/rgb "
			"port as output for dual display\n");
	}
	else if((pdata->lcd0_func == INPUT) && (pdata->lcd1_func == OUTPUT))
	{
		route->vif0_bypass = 1;
		route->vif0_en = 0;
		route->vif1_bypass = 1;
		route->vif1_en = 0;
		route->sclin_sel = 0;  // scl from vif0
		route->scl_en	= 1;
		route->dither_sel = 0;
		route->hdmi_sel = 2;  //hdmi from lcd0
		route->lcd1_input = 0; //lcd1 as out put
		route->lvds_en	= 0;
		dev_info(rk616->dev,
			"rk616 use lcd0 as input and lcd1 as"
			"output for dual display\n");

	}
	else if((pdata->lcd0_func == UNUSED) && (pdata->lcd1_func == INPUT))
	{
		route->vif0_bypass = 1;
		route->vif0_en     = 0;
		route->vif1_bypass = 1;
		route->vif1_en     = 0;
		route->sclin_sel   = 1; //from vif1
		route->scl_en      = 1;
		route->dither_sel  = 1; //dither from sclaer
		route->hdmi_sel    = 0; //from vif1
		route->lcd1_input  = 1;  
		route->lvds_en	   = 1;
		if(screen->type == SCREEN_RGB)
		{
			route->lvds_mode   = 0; //rgb output 
		}
		else if(screen->type == SCREEN_LVDS)
		{
			route->lvds_mode = 1;
			route->lvds_ch_nr = pdata->lvds_ch_nr;
		}
		else
		{
			dev_err(rk616->dev,"un supported interface:%d\n",screen->type);
			return -EINVAL;
		}
			
		
	}
	else
	{
		dev_err(rk616->dev,
			"invalid configration,please check your"
			"rk616_platform_data setting in your board file!\n");	
	}

	return 0;
	
}
static int rk616_display_router_cfg(struct mfd_rk616 *rk616,rk_screen *screen)
{
	u32 val = 0;
	int ret;
	struct rk616_route *route = &rk616->route;

	

	ret = rk616_set_router(rk616,screen);
	if(ret < 0)
		return ret;
	
	val = (SCLIN_CLK_SEL << 16) | (DITHER_CLK_SEL << 16) | (HDMI_CLK_SEL_MASK) | 
		(VIF1_CLK_BYPASS << 16) | (VIF0_CLK_BYPASS << 16) |(route->sclin_sel<<15) | 
		(route->dither_sel<<14) | (route->hdmi_sel<<12) | (route->vif1_bypass<<7) | 
		(route->vif0_bypass<<1); 
	ret = rk616->write_dev(rk616,CRU_CLKSE2_CON,&val);

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
				val &= ~(LVDS_CH0TTL_EN | LVDS_CH1TTL_EN);
				val = (LVDS_DCLK_INV)  | (LVDS_CH1_PWR_EN) |(LVDS_CH0_PWR_EN) | 
					(LVDS_CBG_PWR_EN) | (LVDS_CH_SEL) | (LVDS_OUT_FORMAT(screen->hw_format)) | 
					(LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH1_PWR_EN << 16) | 
					(LVDS_CH0_PWR_EN << 16) | (LVDS_CBG_PWR_EN << 16) | (LVDS_CH_SEL << 16) | 
					(LVDS_OUT_FORMAT_MASK);
				ret = rk616->write_dev(rk616,CRU_LVDS_CON0,&val);
				
				dev_info(rk616->dev,"rk616 use dual lvds channel.......\n");
			}
			else //single lvds channel
			{
				val &= ~(LVDS_CH0TTL_EN | LVDS_CH1TTL_EN | LVDS_CH1_PWR_EN | LVDS_PLL_PWR_DN | LVDS_CH_SEL); //use channel 0
				val |= (LVDS_CH0_PWR_EN) |(LVDS_CBG_PWR_EN) | (LVDS_OUT_FORMAT(screen->hw_format)) | 
					(LVDS_CH0TTL_EN << 16) | (LVDS_CH1TTL_EN << 16) |(LVDS_CH0_PWR_EN << 16) | 
					(LVDS_CBG_PWR_EN << 16)|(LVDS_CH_SEL << 16) | (LVDS_PLL_PWR_DN << 16)| 
					(LVDS_OUT_FORMAT_MASK);
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
	
	
	val = FRC_DCLK_INV | (FRC_DCLK_INV << 16);
	ret = rk616->write_dev(rk616,FRC_REG,&val);
	
	ret = rk616_vif_cfg(rk616,screen,0);
	ret = rk616_vif_cfg(rk616,screen,1);
	ret = rk616_scaler_cfg(rk616,screen);

	return 0;
	
}
int rk610_lcd_scaler_set_param(rk_screen *screen,bool enable )//enable:0 bypass 1: scale
{
	int ret;
	struct mfd_rk616 *rk616 = g_rk616;
	if(!rk616)
	{
		printk(KERN_ERR "%s:mfd rk616 is null!\n",__func__);
		return -1;
	}
	rk616_display_router_cfg(rk616,screen);
	return ret;
}

static int rk616_lvds_probe(struct platform_device *pdev)
{

	struct mfd_rk616 *rk616 = dev_get_drvdata(pdev->dev.parent);
	if(!rk616)
	{
		dev_err(&pdev->dev,"null mfd device rk616!\n");
		return -ENODEV;
	}
	else
		g_rk616 = rk616;
	

	dev_info(&pdev->dev,"rk616 lvds probe success!\n");

	return 0;
	
}

static int rk616_lvds_remove(struct platform_device *pdev)
{
	
	return 0;
}

static void rk616_lvds_shutdown(struct platform_device *pdev)
{
	
	return;
}

static struct platform_driver rk616_lvds_driver = {
	.driver		= {
		.name	= "rk616-lvds",
		.owner	= THIS_MODULE,
	},
	.probe		= rk616_lvds_probe,
	.remove		= rk616_lvds_remove,
	.shutdown	= rk616_lvds_shutdown,
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

