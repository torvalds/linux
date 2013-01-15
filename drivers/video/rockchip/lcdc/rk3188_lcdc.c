/*
 * drivers/video/rockchip/lcdc/rk3188_lcdc.c
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *Author:yxj<yxj@rock-chips.com>
 *This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/earlysuspend.h>
#include <asm/div64.h>
#include <asm/uaccess.h>
#include "rk3188_lcdc.h"



static int dbg_thresd = 0;
module_param(dbg_thresd, int, S_IRUGO|S_IWUSR);
#define DBG(level,x...) do { 			\
	if(unlikely(dbg_thresd >= level)) 	\
		printk(KERN_INFO x);} while (0)

//#define WAIT_FOR_SYNC 1
static int  rk3188_lcdc_clk_enable(struct rk3188_lcdc_device *lcdc_dev)
{

	clk_enable(lcdc_dev->pd);
	clk_enable(lcdc_dev->hclk);
	clk_enable(lcdc_dev->dclk);
	clk_enable(lcdc_dev->aclk);

	spin_lock(&lcdc_dev->reg_lock);
	lcdc_dev->clk_on = 1;
	spin_unlock(&lcdc_dev->reg_lock);
	printk("rk3188 lcdc%d clk enable...\n",lcdc_dev->id);
	
	return 0;
}

static int rk3188_lcdc_clk_disable(struct rk3188_lcdc_device *lcdc_dev)
{
	spin_lock(&lcdc_dev->reg_lock);
	lcdc_dev->clk_on = 0;
	spin_unlock(&lcdc_dev->reg_lock);

	clk_disable(lcdc_dev->dclk);
	clk_disable(lcdc_dev->hclk);
	clk_disable(lcdc_dev->aclk);
	clk_disable(lcdc_dev->pd);
	printk("rk3188 lcdc%d clk disable...\n",lcdc_dev->id);
	
	return 0;
}

static int rk3188_lcdc_resume_reg(struct rk3188_lcdc_device *lcdc_dev)
{
	memcpy((u8*)lcdc_dev->regs, (u8*)lcdc_dev->regsbak, 0x84);
	return 0;	
}


//enable layer,open:1,enable;0 disable
static int win0_open(struct rk3188_lcdc_device *lcdc_dev,bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(open)
		{
			if(!lcdc_dev->atv_layer_cnt)
			{
				printk(KERN_INFO "lcdc%d wakeup from standby!\n",lcdc_dev->id);
				lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_LCDC_STANDBY,v_LCDC_STANDBY(0));
			}
			lcdc_dev->atv_layer_cnt++;
		}
		else if((lcdc_dev->atv_layer_cnt > 0) && (!open))
		{
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.layer_par[0]->state = open;

		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_EN, v_WIN0_EN(open));
		if(!lcdc_dev->atv_layer_cnt)  //if no layer used,disable lcdc
		{
			printk(KERN_INFO "no layer of lcdc%d is used,go to standby!\n",lcdc_dev->id);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_LCDC_STANDBY,v_LCDC_STANDBY(1));
		}
		lcdc_cfg_done(lcdc_dev);	
	}
	spin_unlock(&lcdc_dev->reg_lock);


	return 0;
}

static int win1_open(struct rk3188_lcdc_device *lcdc_dev,bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(open)
		{
			if(!lcdc_dev->atv_layer_cnt)
			{
				printk(KERN_INFO "lcdc%d wakeup from standby!\n",lcdc_dev->id);
				lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_LCDC_STANDBY,v_LCDC_STANDBY(0));
			}
			lcdc_dev->atv_layer_cnt++;
		}
		else if((lcdc_dev->atv_layer_cnt > 0) && (!open))
		{
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.layer_par[1]->state = open;
		
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN1_EN, v_WIN1_EN(open));
		if(!lcdc_dev->atv_layer_cnt)  //if no layer used,disable lcdc
		{
			printk(KERN_INFO "no layer of lcdc%d is used,go to standby!\n",lcdc_dev->id);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_LCDC_STANDBY,v_LCDC_STANDBY(1));
		}
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
	return 0;
}

static int rk3188_lcdc_open(struct rk_lcdc_device_driver *dev_drv,int layer_id,bool open)
{
	int i=0;
	int __iomem *c;
	int v;
	struct rk3188_lcdc_device *lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);

	if((open) && (!lcdc_dev->atv_layer_cnt)) //enable clk,when first layer open
	{
		rk3188_lcdc_clk_enable(lcdc_dev);
		rk3188_lcdc_resume_reg(lcdc_dev); //resume reg
		spin_lock(&lcdc_dev->reg_lock);
		if(dev_drv->cur_screen->dsp_lut)			//resume dsp lut
		{
			lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(0));
			lcdc_cfg_done(lcdc_dev);
			mdelay(25); //wait for dsp lut disabled
			for(i=0;i<256;i++)
			{
				v = dev_drv->cur_screen->dsp_lut[i];
				c = lcdc_dev->dsp_lut_addr_base+i;
				writel_relaxed(v,c);

			}
			lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(1)); //enable dsp lut
		}
		spin_unlock(&lcdc_dev->reg_lock);
	}

	if(layer_id == 0)
	{
		win0_open(lcdc_dev,open);	
	}
	else if(layer_id == 1)
	{
		win1_open(lcdc_dev,open);
	}
	else 
	{
		printk("invalid win number:%d\n",layer_id);
	}

	if((!open) && (!lcdc_dev->atv_layer_cnt))  //when all layer closed,disable clk
	{
		rk3188_lcdc_clk_disable(lcdc_dev);
	}

	printk(KERN_INFO "lcdc%d win%d %s,atv layer:%d\n",
		lcdc_dev->id,layer_id,open?"open":"closed",
		lcdc_dev->atv_layer_cnt);
	return 0;
}

static int rk3188_lcdc_init(struct rk_lcdc_device_driver *dev_drv)
{
	int i = 0;
	int __iomem *c;
	int v;
	struct rk3188_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3188_lcdc_device,driver);
	if(lcdc_dev->id == 0) //lcdc0
	{
		lcdc_dev->pd 	= clk_get(NULL,"pd_lcdc0");
		lcdc_dev->hclk 	= clk_get(NULL,"hclk_lcdc0"); 
		lcdc_dev->aclk 	= clk_get(NULL,"aclk_lcdc0");
		lcdc_dev->dclk 	= clk_get(NULL,"dclk_lcdc0");
	}
	else if(lcdc_dev->id == 1)
	{
		lcdc_dev->pd 	= clk_get(NULL,"pd_lcdc1");
		lcdc_dev->hclk 	= clk_get(NULL,"hclk_lcdc1");  
		lcdc_dev->aclk 	= clk_get(NULL,"aclk_lcdc1");
		lcdc_dev->dclk 	= clk_get(NULL,"dclk_lcdc1");
	}
	else
	{
		printk(KERN_ERR "invalid lcdc device!\n");
		return -EINVAL;
	}
	if (IS_ERR(lcdc_dev->pd) || (IS_ERR(lcdc_dev->aclk)) ||(IS_ERR(lcdc_dev->dclk)) || (IS_ERR(lcdc_dev->hclk)))
    	{
       		printk(KERN_ERR "failed to get lcdc%d clk source\n",lcdc_dev->id);
   	}
	
	rk3188_lcdc_clk_enable(lcdc_dev);
	
	lcdc_set_bit(lcdc_dev,SYS_CTRL,m_AUTO_GATING_EN);//eanble axi-clk auto gating for low power
        if(dev_drv->cur_screen->dsp_lut)
        {
        	lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(0));
		lcdc_cfg_done(lcdc_dev);
		msleep(25);
		for(i=0;i<256;i++)
		{
			v = dev_drv->cur_screen->dsp_lut[i];
			c = lcdc_dev->dsp_lut_addr_base+i;
			writel_relaxed(v,c);
			
		}
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(1));
        }
	
	lcdc_cfg_done(lcdc_dev);  // write any value to  REG_CFG_DONE let config become effective

	rk3188_lcdc_clk_disable(lcdc_dev);
	
	return 0;
}


//set lcdc according the screen info
static int rk3188_load_screen(struct rk_lcdc_device_driver *dev_drv, bool initscreen)
{
	int ret = -EINVAL;
	int fps;
	u16 face = 0;
	struct rk3188_lcdc_device *lcdc_dev = 
				container_of(dev_drv,struct rk3188_lcdc_device,driver);
	rk_screen *screen = dev_drv->cur_screen;
	u16 right_margin = screen->right_margin;
	u16 left_margin = screen->left_margin;
	u16 lower_margin = screen->lower_margin;
	u16 upper_margin = screen->upper_margin;
	u16 x_res = screen->x_res;
	u16 y_res = screen->y_res;

	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(screen->type==SCREEN_MCU)
		{
	    		printk("MUC¡¡screen not supported now!\n");
			return -EINVAL;
		}

		switch (screen->face)
		{
		case OUT_P565:
			face = OUT_P565;  //dither down to rgb565
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
			break;
		case OUT_P666:
			face = OUT_P666; //dither down to rgb666
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
			break;
		case OUT_D888_P565:
			face = OUT_P888;
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
			break;
		case OUT_D888_P666:
			face = OUT_P888;
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
			break;
		case OUT_P888:
			face = OUT_P888;
			break;
		default:
			printk("unsupported display output interface!\n");
			break;
		}

		//use default overlay,set vsyn hsync den dclk polarity
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0,m_DSP_OUT_FORMAT | m_HSYNC_POL | m_VSYNC_POL |
	     		     m_DEN_POL |m_DCLK_POL,v_DSP_OUT_FORMAT(face) | v_HSYNC_POL(screen->pin_hsync) | 
	     		     v_VSYNC_POL(screen->pin_vsync) | v_DEN_POL(screen->pin_den) | v_DCLK_POL(screen->pin_dclk));

		
		//set background color to black,set swap according to the screen panel,disable blank mode
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BG_COLOR| m_DSP_BG_SWAP | m_DSP_RB_SWAP | 
			     m_DSP_RG_SWAP | m_DSP_DELTA_SWAP | m_DSP_DUMMY_SWAP | m_BLANK_EN,
			     v_BG_COLOR(0x000000) | v_DSP_BG_SWAP(screen->swap_bg) | 
			     v_DSP_RB_SWAP(screen->swap_rb) | v_DSP_RG_SWAP(screen->swap_rg) | 
			     v_DSP_DELTA_SWAP(screen->swap_delta) | v_DSP_DUMMY_SWAP(screen->swap_dumy) |
			     v_BLANK_EN(0) | v_BLACK_EN(0));
		lcdc_writel(lcdc_dev,DSP_HTOTAL_HS_END,v_HSYNC(screen->hsync_len) |
			    v_HORPRD(screen->hsync_len + left_margin + x_res + right_margin));
		lcdc_writel(lcdc_dev,DSP_HACT_ST_END,v_HAEP(screen->hsync_len + left_margin + x_res) |
			    v_HASP(screen->hsync_len + left_margin));

		lcdc_writel(lcdc_dev,DSP_VTOTAL_VS_END, v_VSYNC(screen->vsync_len) |
			    v_VERPRD(screen->vsync_len + upper_margin + y_res + lower_margin));
		lcdc_writel(lcdc_dev,DSP_VACT_ST_END,v_VAEP(screen->vsync_len + upper_margin+y_res)|
			    v_VASP(screen->vsync_len + screen->upper_margin));
	}
 	spin_unlock(&lcdc_dev->reg_lock);

	ret = clk_set_rate(lcdc_dev->dclk, screen->pixclock);
	if(ret)
	{
        	dev_err(dev_drv->dev,"set lcdc%d dclk failed\n",lcdc_dev->id);
	}
    	lcdc_dev->driver.pixclock = lcdc_dev->pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	
	fps = rk_fb_calc_fps(screen,lcdc_dev->pixclock);
	screen->ft = 1000/fps;
    	printk("%s: dclk:%lu>>fps:%d ",lcdc_dev->driver.name,clk_get_rate(lcdc_dev->dclk),fps);

    	if(screen->init)
    	{
    		screen->init();
    	}
	
	dev_info(dev_drv->dev,"%s for lcdc%d ok!\n",__func__,lcdc_dev->id);
	return 0;
}


static  int win0_set_par(struct rk3188_lcdc_device *lcdc_dev,rk_screen *screen,
			    struct layer_par *par )
{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u32 ScaleYrgbX = 0x1000;
	u32 ScaleYrgbY = 0x1000;
	u32 ScaleCbrX = 0x1000;
	u32 ScaleCbrY = 0x1000;

	xact = par->xact;			    //active (origin) picture window width/height		
	yact = par->yact;
	xvir = par->xvir;			   // virtual resolution		
	yvir = par->yvir;
	xpos = par->xpos+screen->left_margin + screen->hsync_len;
	ypos = par->ypos+screen->upper_margin + screen->vsync_len;

	
	ScaleYrgbX = CalScale(xact, par->xsize); //both RGB and yuv need this two factor
	ScaleYrgbY = CalScale(yact, par->ysize);
	switch (par->format)
	{
	case YUV422:// yuv422
		ScaleCbrX = CalScale((xact/2), par->xsize);
		ScaleCbrY = CalScale(yact, par->ysize);
		break;
	case YUV420: // yuv420
		ScaleCbrX = CalScale(xact/2, par->xsize);
		ScaleCbrY = CalScale(yact/2, par->ysize);
		break;
	case YUV444:// yuv444
		ScaleCbrX = CalScale(xact, par->xsize);
		ScaleCbrY = CalScale(yact, par->ysize);
		break;
	default:
		break;
	}

	DBG(1,"%s for lcdc%d>>format:%d>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
		__func__,lcdc_dev->id,par->format,xact,yact,par->xsize,par->ysize,xvir,yvir,xpos,ypos);
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_writel(lcdc_dev,WIN0_SCL_FACTOR_YRGB,v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
		lcdc_writel(lcdc_dev,WIN0_SCL_FACTOR_CBR,v_X_SCL_FACTOR(ScaleCbrX) | v_Y_SCL_FACTOR(ScaleCbrY));
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_WIN0_FORMAT,v_WIN0_FORMAT(par->format));		//(inf->video_mode==0)
		lcdc_writel(lcdc_dev,WIN0_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
		lcdc_writel(lcdc_dev,WIN0_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
		lcdc_writel(lcdc_dev,WIN0_DSP_INFO,v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
		lcdc_msk_reg(lcdc_dev,WIN0_COLOR_KEY,m_COLOR_KEY_EN,v_COLOR_KEY_EN(0));
		
		switch(par->format) 
		{
		case ARGB888:
			lcdc_msk_reg(lcdc_dev, WIN_VIR,m_WIN0_VIR,v_ARGB888_VIRWIDTH(xvir));
			//lcdc_msk_reg(lcdc_dev,SYS_CTRL1,m_W0_RGB_RB_SWAP,v_W0_RGB_RB_SWAP(1));
			break;
		case RGB888:  //rgb888
			lcdc_msk_reg(lcdc_dev, WIN_VIR,m_WIN0_VIR,v_RGB888_VIRWIDTH(xvir));
			//lcdc_msk_reg(lcdc_dev,SYS_CTRL1,m_W0_RGB_RB_SWAP,v_W0_RGB_RB_SWAP(1));
			break;
		case RGB565:  //rgb565
			lcdc_msk_reg(lcdc_dev, WIN_VIR,m_WIN0_VIR,v_RGB565_VIRWIDTH(xvir));
			break;
		case YUV422:
		case YUV420:
		case YUV444:
			lcdc_msk_reg(lcdc_dev, WIN_VIR,m_WIN0_VIR,v_YUV_VIRWIDTH(xvir));
			break;
		default:
			dev_err(lcdc_dev->driver.dev,"un supported format!\n");
			break;
		}

	}
	spin_unlock(&lcdc_dev->reg_lock);

    return 0;

}

static int win1_set_par(struct rk3188_lcdc_device *lcdc_dev,rk_screen *screen,
			   struct layer_par *par )
{
	u32 xact, yact, xvir, yvir, xpos, ypos;

	xact = par->xact;			
	yact = par->yact;
	xvir = par->xvir;		
	yvir = par->yvir;
	xpos = par->xpos+screen->left_margin + screen->hsync_len;
	ypos = par->ypos+screen->upper_margin + screen->vsync_len;

	
	DBG(1,"%s for lcdc%d>>format:%d>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
		__func__,lcdc_dev->id,par->format,xact,yact,par->xsize,par->ysize,xvir,yvir,xpos,ypos);

	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_WIN1_FORMAT, v_WIN1_FORMAT(par->format));
		lcdc_writel(lcdc_dev, WIN1_DSP_INFO,v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
		lcdc_writel(lcdc_dev, WIN1_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
		// disable win1 color key and set the color to black(rgb=0)
		lcdc_msk_reg(lcdc_dev, WIN1_COLOR_KEY,m_COLOR_KEY_EN,v_COLOR_KEY_EN(0));
		switch(par->format)
		{
		case ARGB888:
			lcdc_msk_reg(lcdc_dev, WIN_VIR,m_WIN1_VIR,v_WIN1_ARGB888_VIRWIDTH(xvir));
			//lcdc_msk_reg(lcdc_dev,SYS_CTRL1,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
			break;
		case RGB888:  //rgb888
			lcdc_msk_reg(lcdc_dev, WIN_VIR,m_WIN1_VIR,v_WIN1_RGB888_VIRWIDTH(xvir));
			// lcdc_msk_reg(lcdc_dev,SYS_CTRL1,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
			break;
		case RGB565:  //rgb565
			lcdc_msk_reg(lcdc_dev, WIN_VIR,m_WIN1_VIR,v_WIN1_RGB565_VIRWIDTH(xvir));
			break;
		default:
			dev_err(lcdc_dev->driver.dev,"un supported format!\n");
			break;
		}

	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}


static int rk3188_lcdc_set_par(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	struct rk3188_lcdc_device *lcdc_dev = 
			container_of(dev_drv,struct rk3188_lcdc_device,driver);
	struct layer_par *par = NULL;
	rk_screen *screen = dev_drv->cur_screen;

	if(!screen)
	{
		dev_err(dev_drv->dev,"screen is null!\n");
		return -ENOENT;
	}
	if(layer_id==0)
	{
		par = dev_drv->layer_par[0];
		win0_set_par(lcdc_dev,screen,par);
	}
	else if(layer_id==1)
	{
		par = dev_drv->layer_par[1];
		win1_set_par(lcdc_dev,screen,par);
	}
	else
	{
		dev_err(dev_drv->dev,"unsupported win number:%d\n",layer_id);
		return -EINVAL;
	}
	
	return 0;
}

static  int win0_display(struct rk3188_lcdc_device *lcdc_dev,struct layer_par *par )
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = par->smem_start + par->y_offset;
	uv_addr = par->cbr_start + par->c_offset;
	DBG(2,KERN_INFO "lcdc%d>>%s:y_addr:0x%x>>uv_addr:0x%x\n",lcdc_dev->id,__func__,y_addr,uv_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_writel(lcdc_dev, WIN0_YRGB_MST0, y_addr);
		lcdc_writel(lcdc_dev, WIN0_CBR_MST0, uv_addr);
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
	
}

static  int win1_display(struct rk3188_lcdc_device *lcdc_dev,struct layer_par *par )
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = par->smem_start + par->y_offset;
	uv_addr = par->cbr_start + par->c_offset;
	DBG(2,KERN_INFO "lcdc%d>>%s>>y_addr:0x%x>>uv_addr:0x%x\n",lcdc_dev->id,__func__,y_addr,uv_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_writel(lcdc_dev,WIN1_MST,y_addr);
		lcdc_cfg_done(lcdc_dev); 
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int rk3188_lcdc_pan_display(struct rk_lcdc_device_driver * dev_drv,int layer_id)
{
	struct rk3188_lcdc_device *lcdc_dev = 
				container_of(dev_drv,struct rk3188_lcdc_device,driver);
	struct layer_par *par = NULL;
	rk_screen *screen = dev_drv->cur_screen;
	unsigned long flags;
	int timeout;
	
	if(!screen)
	{
		dev_err(dev_drv->dev,"screen is null!\n");
		return -ENOENT;	
	}
	if(layer_id==0)
	{
		par = dev_drv->layer_par[0];
		win0_display(lcdc_dev,par);
	}
	else if(layer_id==1)
	{
		par = dev_drv->layer_par[1];
		win1_display(lcdc_dev,par);
	}
	else 
	{
		dev_err(dev_drv->dev,"invalid win number:%d!\n",layer_id);
		return -EINVAL;
	}
	if((dev_drv->first_frame))  //this is the first frame of the system ,enable frame start interrupt
	{
		dev_drv->first_frame = 0;
		lcdc_msk_reg(lcdc_dev,INT_STATUS,m_FS_INT_CLEAR |m_FS_INT_EN ,
			  v_FS_INT_CLEAR(1) | v_FS_INT_EN(1));
		lcdc_cfg_done(lcdc_dev);  // write any value to  REG_CFG_DONE let config become effective
		 
	}

#if defined(WAIT_FOR_SYNC)
	spin_lock_irqsave(&dev_drv->cpl_lock,flags);
	init_completion(&dev_drv->frame_done);
	spin_unlock_irqrestore(&dev_drv->cpl_lock,flags);
	timeout = wait_for_completion_timeout(&dev_drv->frame_done,msecs_to_jiffies(dev_drv->cur_screen->ft+5));
	if(!timeout&&(!dev_drv->frame_done.done))
	{
		printk(KERN_ERR "wait for new frame start time out!\n");
		return -ETIMEDOUT;
	}
#endif
	return 0;
}

static int rk3188_lcdc_blank(struct rk_lcdc_device_driver *dev_drv,
				int layer_id,int blank_mode)
{
	return 0;
}


static int rk3188_lcdc_ioctl(struct rk_lcdc_device_driver *dev_drv, unsigned int cmd,unsigned long arg,int layer_id)
{
	return 0;
}

static int rk3188_lcdc_early_suspend(struct rk_lcdc_device_driver *dev_drv)
{
	return 0;
}

static int rk3188_lcdc_early_resume(struct rk_lcdc_device_driver *dev_drv)
{
	return 0;
}

static int rk3188_lcdc_get_layer_state(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	return 0;
}

static int rk3188_lcdc_ovl_mgr(struct rk_lcdc_device_driver *dev_drv,int swap,bool set)
{
	return 0;
}

static ssize_t rk3188_lcdc_get_disp_info(struct rk_lcdc_device_driver *dev_drv,char *buf,int layer_id)
{
	return 0;
}

static int rk3188_lcdc_fps_mgr(struct rk_lcdc_device_driver *dev_drv,int fps,bool set)
{
	return 0;
}


static int rk3188_fb_layer_remap(struct rk_lcdc_device_driver *dev_drv,
	enum fb_win_map_order order)
{
	mutex_lock(&dev_drv->fb_win_id_mutex);
	if(order == FB_DEFAULT_ORDER )
	{
		order = FB0_WIN0_FB1_WIN1_FB2_WIN2;
	}
	dev_drv->fb2_win_id  = order/100;
	dev_drv->fb1_win_id = (order/10)%10;
	dev_drv->fb0_win_id = order%10;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	printk("fb0:win%d\nfb1:win%d\nfb2:win%d\n",dev_drv->fb0_win_id,dev_drv->fb1_win_id,
	       dev_drv->fb2_win_id);

       return 0;
}

static int rk3188_fb_get_layer(struct rk_lcdc_device_driver *dev_drv,const char *id)
{
       int layer_id = 0;
       mutex_lock(&dev_drv->fb_win_id_mutex);
       if(!strcmp(id,"fb0")||!strcmp(id,"fb2"))
       {
               layer_id = dev_drv->fb0_win_id;
       }
       else if(!strcmp(id,"fb1")||!strcmp(id,"fb3"))
       {
               layer_id = dev_drv->fb1_win_id;
       }
       mutex_unlock(&dev_drv->fb_win_id_mutex);

       return  layer_id;
}

static int rk3188_set_dsp_lut(struct rk_lcdc_device_driver *dev_drv,int *lut)
{
	int i=0;
	int __iomem *c;
	int v;
	int ret = 0;

	struct rk3188_lcdc_device *lcdc_dev = 
				container_of(dev_drv,struct rk3188_lcdc_device,driver);
	lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	msleep(25);
	if(dev_drv->cur_screen->dsp_lut)
	{
		for(i=0;i<256;i++)
		{
			v = dev_drv->cur_screen->dsp_lut[i] = lut[i];
			c = lcdc_dev->dsp_lut_addr_base+i;
			writel_relaxed(v,c);
			
		}
	}
	else
	{
		dev_err(dev_drv->dev,"no buffer to backup lut data!\n");
		ret =  -1;
	}
	lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(1));
	lcdc_cfg_done(lcdc_dev);

	return ret;
}

static struct layer_par lcdc_layer[] = {
	[0] = {
		.name  		= "win0",
		.id		= 0,
		.support_3d	= true,
	},
	[1] = {
		.name  		= "win1",
		.id		= 1,
		.support_3d	= false,
	},
};

static struct rk_lcdc_device_driver lcdc_driver = {
	.name			= "lcdc",
	.def_layer_par		= lcdc_layer,
	.num_layer		= ARRAY_SIZE(lcdc_layer),
	.open			= rk3188_lcdc_open,
	.init_lcdc		= rk3188_lcdc_init,
	.load_screen		= rk3188_load_screen,
	.set_par       		= rk3188_lcdc_set_par,
	.pan_display            = rk3188_lcdc_pan_display,
	.blank         		= rk3188_lcdc_blank,
	.ioctl			= rk3188_lcdc_ioctl,
	.suspend		= rk3188_lcdc_early_suspend,
	.resume			= rk3188_lcdc_early_resume,
	.get_layer_state	= rk3188_lcdc_get_layer_state,
	.ovl_mgr		= rk3188_lcdc_ovl_mgr,
	.get_disp_info		= rk3188_lcdc_get_disp_info,
	.fps_mgr		= rk3188_lcdc_fps_mgr,
	.fb_get_layer           = rk3188_fb_get_layer,
	.fb_layer_remap         = rk3188_fb_layer_remap,
	.set_dsp_lut            = rk3188_set_dsp_lut,
};

static irqreturn_t rk3188_lcdc_isr(int irq, void *dev_id)
{
	struct rk3188_lcdc_device *lcdc_dev = 
				(struct rk3188_lcdc_device *)dev_id;
	
	lcdc_msk_reg(lcdc_dev, INT_STATUS, m_FS_INT_CLEAR, v_FS_INT_CLEAR(1));

#if defined(WAIT_FOR_SYNC)
	if(lcdc_dev->driver.num_buf < 3)  //three buffer ,no need to wait for sync
	{
		spin_lock(&(lcdc_dev->driver.cpl_lock));
		complete(&(lcdc_dev->driver.frame_done));
		spin_unlock(&(lcdc_dev->driver.cpl_lock));
	}
#endif
	return IRQ_HANDLED;
}


#if defined(CONFIG_PM)
static int rk3188_lcdc_suspend(struct platform_device *pdev,
					pm_message_t state)
{
	return 0;
}

static int rk3188_lcdc_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define rk3188_lcdc_suspend NULL
#define rk3188_lcdc_resume  NULL
#endif
static int __devinit rk3188_lcdc_probe(struct platform_device *pdev)
{
	struct rk3188_lcdc_device *lcdc_dev = NULL;
	struct device *dev = &pdev->dev;
	rk_screen *screen;
	struct rk29fb_info *screen_ctr_info;
	struct resource *res = NULL;
	struct resource *mem = NULL;
	int ret = 0;
	
	lcdc_dev = devm_kzalloc(dev,sizeof(struct rk3188_lcdc_device), GFP_KERNEL);
	if(!lcdc_dev)
	{
		dev_err(&pdev->dev, ">>rk3188 lcdc device kmalloc fail!");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->id = pdev->id;
	screen_ctr_info = (struct rk29fb_info * )pdev->dev.platform_data;
	if(!screen_ctr_info)
	{
		dev_err(dev, "no platform data specified for screen control info!\n");
		ret = -EINVAL;
		goto err0;
	}
	screen =  kzalloc(sizeof(rk_screen), GFP_KERNEL);
	if(!screen)
	{
		dev_err(&pdev->dev, "rk screen kmalloc fail!");
		ret = -ENOMEM;
		goto err0;
	}
	
	res = platform_get_resource(pdev, IORESOURCE_MEM,0);
	if (res == NULL)
    	{
        	dev_err(&pdev->dev, "failed to get register resource for lcdc%d \n",lcdc_dev->id);
        	ret = -ENOENT;
		goto err1;
    	}
	
    	lcdc_dev->reg_phy_base = res->start;
	lcdc_dev->len = resource_size(res);
    	mem = request_mem_region(lcdc_dev->reg_phy_base,lcdc_dev->len, pdev->name);
    	if (!mem)
    	{
        	dev_err(&pdev->dev, "failed to request mem region for lcdc%d\n",lcdc_dev->id);
        	ret = -ENOENT;
		goto err1;
    	}
	lcdc_dev->regs = ioremap(lcdc_dev->reg_phy_base,lcdc_dev->len);
	if (!lcdc_dev->regs)
	{
		dev_err(&pdev->dev, "cannot map register for lcdc%d\n",lcdc_dev->id);
		ret = -ENXIO;
		goto err2;
	}
	
	lcdc_dev->regsbak = kzalloc(lcdc_dev->len,GFP_KERNEL);
	if(!lcdc_dev->regsbak)
	{
		dev_err(&pdev->dev, "failed to map memory for reg backup!\n");
		ret = -ENOMEM;
		goto err3;
	}
	lcdc_dev->dsp_lut_addr_base = (lcdc_dev->regs + DSP_LUT_ADDR);
	printk("lcdc%d:reg_phy_base = 0x%08x,reg_vir_base:0x%p\n",pdev->id,lcdc_dev->reg_phy_base, lcdc_dev->regs);
	lcdc_dev->driver.dev = dev;
	lcdc_dev->driver.screen0 = screen;
	lcdc_dev->driver.cur_screen = screen;
	lcdc_dev->driver.screen_ctr_info = screen_ctr_info;
	
	spin_lock_init(&lcdc_dev->reg_lock);
	
	lcdc_dev->irq = platform_get_irq(pdev, 0);
	if(lcdc_dev->irq < 0)
	{
		dev_err(&pdev->dev, "cannot find IRQ for lcdc%d\n",lcdc_dev->id);
		goto err3;
	}
	ret = devm_request_irq(dev,lcdc_dev->irq, rk3188_lcdc_isr, IRQF_DISABLED,dev_name(dev),lcdc_dev);
	if (ret)
	{
	       dev_err(&pdev->dev, "cannot requeset irq %d - err %d\n", lcdc_dev->irq, ret);
	       ret = -EBUSY;
	       goto err3;
	}
	ret = rk_fb_register(&(lcdc_dev->driver),&lcdc_driver,lcdc_dev->id);
	if(ret < 0)
	{
		dev_err(dev,"register fb for lcdc%d failed!\n",lcdc_dev->id);
		goto err4;
	}
	printk("rk3188 lcdc%d probe ok!\n",lcdc_dev->id);

err4:
	free_irq(lcdc_dev->irq,lcdc_dev);
err3:
	iounmap(lcdc_dev->regs);
err2:
	release_mem_region(lcdc_dev->reg_phy_base,lcdc_dev->len);
err1:
	kfree(screen);
err0:
	platform_set_drvdata(pdev, NULL);
	kfree(lcdc_dev);
	
	return ret;
}

static int __devexit rk3188_lcdc_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk3188_lcdc_shutdown(struct platform_device *pdev)
{
	
}
static struct platform_driver rk3188_lcdc_driver = {
	.probe		= rk3188_lcdc_probe,
	.remove		= __devexit_p(rk3188_lcdc_remove),
	.driver		= {
		.name 	= "rk3188-lcdc",
		.owner  = THIS_MODULE,
	},
	.suspend 	= rk3188_lcdc_suspend,
	.resume  	= rk3188_lcdc_resume,
	.shutdown 	= rk3188_lcdc_shutdown,
};
static int __init rk3188_lcdc_module_init(void)
{
	return platform_driver_register(&rk3188_lcdc_driver);
}

static void __exit rk3188_lcdc_module_exit(void)
{
	platform_driver_unregister(&rk3188_lcdc_driver);
}
fs_initcall(rk3188_lcdc_module_init);
module_exit(rk3188_lcdc_module_exit);
