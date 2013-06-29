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
#include <mach/iomux.h>
#include "rk3188_lcdc.h"



static int dbg_thresd = 0;
module_param(dbg_thresd, int, S_IRUGO|S_IWUSR);

#define DBG(level,x...) do { 			\
	if(unlikely(dbg_thresd >= level)) 	\
		printk(KERN_INFO x);} while (0)

//#define WAIT_FOR_SYNC 1

static int rk3188_load_screen(struct rk_lcdc_device_driver *dev_drv, bool initscreen);
static int  rk3188_lcdc_clk_enable(struct rk3188_lcdc_device *lcdc_dev)
{

	clk_enable(lcdc_dev->hclk);
	clk_enable(lcdc_dev->dclk);
	clk_enable(lcdc_dev->aclk);
	clk_enable(lcdc_dev->pd);

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
	mdelay(25);
	clk_disable(lcdc_dev->dclk);
	clk_disable(lcdc_dev->hclk);
	clk_disable(lcdc_dev->aclk);
	clk_disable(lcdc_dev->pd);
	printk("rk3188 lcdc%d clk disable...\n",lcdc_dev->id);
	
	return 0;
}



static void rk3188_lcdc_reg_dump(struct rk3188_lcdc_device *lcdc_dev)
{
       int *cbase =  (int *)lcdc_dev->regs;
       int *regsbak = (int*)lcdc_dev->regsbak;
       int i,j;

       printk("back up reg:\n");
       for(i=0; i<=(0x90>>4);i++)
       {
               for(j=0;j<4;j++)
                       printk("%08x  ",*(regsbak+i*4 +j));
               printk("\n");
       }

       printk("lcdc reg:\n");
       for(i=0; i<=(0x90>>4);i++)
       {
               for(j=0;j<4;j++)
                       printk("%08x  ",readl_relaxed(cbase+i*4 +j));
               printk("\n");
       }
       
}
static void rk3188_lcdc_read_reg_defalut_cfg(struct rk3188_lcdc_device *lcdc_dev)
{
	int reg=0;
	u32 value=0;
	struct layer_par *win0 =  lcdc_dev->driver.layer_par[0];
	struct layer_par *win1 =  lcdc_dev->driver.layer_par[1];

	spin_lock(&lcdc_dev->reg_lock);
	for(reg = 0; reg < REG_CFG_DONE;reg += 4)
	{
		value = lcdc_readl(lcdc_dev,reg);
		switch(reg)
		{
		case SYS_CTRL:
			lcdc_dev->standby = (value & m_LCDC_STANDBY)>>17;
			win0->state = (value & m_WIN0_EN)>>0;
			win1->state = (value & m_WIN1_EN)>>1;
			if(lcdc_dev->id == 0)
				lcdc_dev->atv_layer_cnt = win0->state;
			else
				lcdc_dev->atv_layer_cnt = win1->state;
			win0->swap_rb = (value & m_WIN0_RB_SWAP)>>15;
			win1->swap_rb = (value & m_WIN1_RB_SWAP)>>19;
			win0->fmt_cfg = (value & m_WIN0_FORMAT)>>3;
			win1->fmt_cfg = (value & m_WIN1_FORMAT)>>6;
			DBG(2,"standby=%d,win0->state=%d,win1->state=%d,win0->swap_rb=%d,win1->swap_rb=%d,win0->fmt_cfg=%d,win1->fmt_cfg=%d\n",
				   lcdc_dev->standby,win0->state,win1->state,win0->swap_rb,win1->swap_rb,win0->fmt_cfg,win1->fmt_cfg);
			break;
		case WIN0_SCL_FACTOR_YRGB:
			win0->scale_yrgb_x = (value>> 0)&0xffff;
			win0->scale_yrgb_y = (value>>16)&0xffff;
			DBG(2,"win0->scale_yrgb_x=%d,win0->scale_yrgb_y=%d\n",win0->scale_yrgb_x,win0->scale_yrgb_y);
			break;
		case WIN0_SCL_FACTOR_CBR:
			win0->scale_cbcr_x = (value>> 0)&0xffff;
			win0->scale_cbcr_y = (value>>16)&0xffff;
			DBG(2,"win0->scale_cbc_x=%d,win0->scale_cbcr_y=%d\n",win0->scale_cbcr_x,win0->scale_cbcr_y);
			break;
		case WIN0_ACT_INFO:
			win0->xact = (((value>> 0)&0x1fff)+1);
			win0->yact = (((value>>16)&0x1fff)+1);
			DBG(2,"win0->xact=%d,win0->yact=%d\n",win0->xact,win0->yact);
			break;
		case WIN0_DSP_ST:
			win0->dsp_stx = (value>> 0)&0xfff;
			win0->dsp_sty = (value>>16)&0xfff;
			DBG(2,"win0->dsp_stx=%d,win0->dsp_sty=%d\n",win0->dsp_stx,win0->dsp_sty);
			break;
		case WIN0_DSP_INFO:
			win0->xsize = (((value>> 0)&0x7ff)+1);
			win0->ysize = (((value>>16)&0x7ff)+1);
			DBG(2,"win0->xsize=%d,win0->ysize=%d\n",win0->xsize,win0->ysize);
			break;
		case WIN_VIR:
			win0->vir_stride = (value>> 0)&0x1fff;
			win1->vir_stride = (value>>16)&0x1fff;
			DBG(2,"win0->vir_stride=%d,win1->vir_stride=%d\n",win0->vir_stride,win1->vir_stride);
			break;
		case WIN0_YRGB_MST0:
			win0->y_addr = value>> 0;
			DBG(2,"win0->y_addr=%d\n",win0->y_addr);
			break;
		case WIN0_CBR_MST0:
			win0->uv_addr = value>> 0;
			DBG(2,"win0->uv_addr=%d\n",win0->uv_addr);
			break;
		case WIN1_DSP_INFO:
			win1->xsize = (((value>> 0)&0x7ff)+1);
			win1->ysize = (((value>>16)&0x7ff)+1);
			DBG(2,"win1->xsize=%d,win1->ysize=%d\n",win1->xsize,win1->ysize);
			break;
		case WIN1_DSP_ST:
			win1->dsp_stx = (value>> 0)&0xfff;
			win1->dsp_sty = (value>>16)&0xfff;
			DBG(2,"win1->dsp_stx=%d,win1->dsp_sty=%d\n",win1->dsp_stx,win1->dsp_sty);
			break;
		case WIN1_MST:
			win1->y_addr =value>> 0;
			DBG(2,"win1->y_addr=%d\n",win1->y_addr);
			break;
		default:
			DBG(2,"%s:uncare reg\n",__func__);
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
	DBG(2,"%s,done\n",__func__);
}

static int rk3188_lcdc_alpha_cfg(struct rk3188_lcdc_device *lcdc_dev)
{
	int win0_top = 0;
	enum data_format win0_format =  lcdc_dev->driver.layer_par[0]->format;
	enum data_format win1_format  =  lcdc_dev->driver.layer_par[1]->format;

	int win0_alpha_en = ((win0_format == ARGB888) || (win0_format == ABGR888))?1:0;
	int win1_alpha_en = ((win1_format == ARGB888) || (win1_format == ABGR888))?1:0;
	u32 *_pv = (u32*)lcdc_dev->regsbak;
	_pv += (DSP_CTRL0 >> 2);
	win0_top = ((*_pv)&(m_WIN0_TOP))>> 8;
	
	if(win0_top && (lcdc_dev->atv_layer_cnt >= 2) && (win0_alpha_en))
	{
		lcdc_msk_reg(lcdc_dev,ALPHA_CTRL,m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN,
				v_WIN0_ALPHA_EN(1) | v_WIN1_ALPHA_EN(0));
		lcdc_msk_reg(lcdc_dev,DSP_CTRL0,m_WIN0_ALPHA_MODE | m_ALPHA_MODE_SEL0 |
			m_ALPHA_MODE_SEL1,v_WIN0_ALPHA_MODE(1) | v_ALPHA_MODE_SEL0(1) |
			v_ALPHA_MODE_SEL1(0));//default set to per-pixel alpha	
	}
	else if((!win0_top) && (lcdc_dev->atv_layer_cnt >= 2) && (win1_alpha_en))
	{
		lcdc_msk_reg(lcdc_dev,ALPHA_CTRL,m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN,
				v_WIN0_ALPHA_EN(0) | v_WIN1_ALPHA_EN(1));
		lcdc_msk_reg(lcdc_dev,DSP_CTRL0,m_WIN1_ALPHA_MODE | m_ALPHA_MODE_SEL0 |
			m_ALPHA_MODE_SEL1,v_WIN1_ALPHA_MODE(1) | v_ALPHA_MODE_SEL0(1) |
			v_ALPHA_MODE_SEL1(0));//default set to per-pixel alpha	
	}
	else
	{
		lcdc_msk_reg(lcdc_dev,ALPHA_CTRL,m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN,
				v_WIN0_ALPHA_EN(0) | v_WIN1_ALPHA_EN(0));
	}
	

	return 0;
}

static int rk3188_lcdc_reg_update(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk3188_lcdc_device *lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);
	struct layer_par *win0 =  lcdc_dev->driver.layer_par[0];
	struct layer_par *win1 =  lcdc_dev->driver.layer_par[1];
	int timeout;
	unsigned long flags;

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_LCDC_STANDBY,v_LCDC_STANDBY(lcdc_dev->standby));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_EN | m_WIN1_EN | m_WIN0_RB_SWAP | m_WIN1_RB_SWAP, 
			v_WIN0_EN(win0->state) | v_WIN1_EN(win1->state) | v_WIN0_RB_SWAP(win0->swap_rb) |
			v_WIN1_RB_SWAP(win1->swap_rb));
		lcdc_writel(lcdc_dev,WIN0_SCL_FACTOR_YRGB,v_X_SCL_FACTOR(win0->scale_yrgb_x) | 
			v_Y_SCL_FACTOR(win0->scale_yrgb_y));
		lcdc_writel(lcdc_dev,WIN0_SCL_FACTOR_CBR,v_X_SCL_FACTOR(win0->scale_cbcr_x) | 
			v_Y_SCL_FACTOR(win0->scale_cbcr_y));
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_WIN0_FORMAT,v_WIN0_FORMAT(win0->fmt_cfg));		//(inf->video_mode==0)
		lcdc_writel(lcdc_dev,WIN0_ACT_INFO,v_ACT_WIDTH(win0->xact) | v_ACT_HEIGHT(win0->yact));
		lcdc_writel(lcdc_dev,WIN0_DSP_ST,v_DSP_STX(win0->dsp_stx) | v_DSP_STY(win0->dsp_sty));
		lcdc_writel(lcdc_dev,WIN0_DSP_INFO,v_DSP_WIDTH(win0->xsize) | v_DSP_HEIGHT(win0->ysize));
		lcdc_msk_reg(lcdc_dev,WIN_VIR,m_WIN0_VIR,v_WIN0_VIR_VAL(win0->vir_stride));
		lcdc_writel(lcdc_dev,WIN0_YRGB_MST0, win0->y_addr);
		lcdc_writel(lcdc_dev,WIN0_CBR_MST0, win0->uv_addr);
		lcdc_writel(lcdc_dev,WIN1_DSP_INFO,v_DSP_WIDTH(win1->xsize) | v_DSP_HEIGHT(win1->ysize));
		lcdc_writel(lcdc_dev,WIN1_DSP_ST,v_DSP_STX(win1->dsp_stx) | v_DSP_STY(win1->dsp_sty));
		lcdc_msk_reg(lcdc_dev,WIN_VIR,m_WIN1_VIR,v_WIN1_VIR_VAL(win1->vir_stride));
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_WIN1_FORMAT, v_WIN1_FORMAT(win1->fmt_cfg));
		lcdc_writel(lcdc_dev,WIN1_MST,win1->y_addr);
		rk3188_lcdc_alpha_cfg(lcdc_dev);
		lcdc_cfg_done(lcdc_dev);
		
	}
	spin_unlock(&lcdc_dev->reg_lock);
	if(dev_drv->wait_fs)
	{
		spin_lock_irqsave(&dev_drv->cpl_lock,flags);
		init_completion(&dev_drv->frame_done);
		spin_unlock_irqrestore(&dev_drv->cpl_lock,flags);
		timeout = wait_for_completion_timeout(&dev_drv->frame_done,msecs_to_jiffies(dev_drv->cur_screen->ft+5));
		if(!timeout&&(!dev_drv->frame_done.done))
		{
			printk(KERN_ERR "wait for new frame start time out!\n");
			return -ETIMEDOUT;
		}
	}
	DBG(2,"%s for lcdc%d\n",__func__,lcdc_dev->id);
	return 0;
	
}
static int rk3188_lcdc_reg_resume(struct rk3188_lcdc_device *lcdc_dev)
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
				lcdc_dev->standby = 0;
			}
			lcdc_dev->atv_layer_cnt++;
		}
		else if((lcdc_dev->atv_layer_cnt > 0) && (!open))
		{
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.layer_par[0]->state = open;
		if(!lcdc_dev->atv_layer_cnt)  //if no layer used,disable lcdc
		{
			printk(KERN_INFO "no layer of lcdc%d is used,go to standby!\n",lcdc_dev->id);
			lcdc_dev->standby = 1;
		}
		//lcdc_cfg_done(lcdc_dev);	
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
				lcdc_dev->standby = 0;
			}
			lcdc_dev->atv_layer_cnt++;
		}
		else if((lcdc_dev->atv_layer_cnt > 0) && (!open))
		{
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.layer_par[1]->state = open;
		
		if(!lcdc_dev->atv_layer_cnt)  //if no layer used,disable lcdc
		{
			printk(KERN_INFO "no layer of lcdc%d is used,go to standby!\n",lcdc_dev->id);
			lcdc_dev->standby = 1;
		}
		//lcdc_cfg_done(lcdc_dev);
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
		rk3188_lcdc_reg_resume(lcdc_dev); //resume reg
		rk3188_load_screen(dev_drv,1);
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
		lcdc_msk_reg(lcdc_dev,INT_STATUS,m_FS_INT_CLEAR,v_FS_INT_CLEAR(1));
		rk3188_lcdc_reg_update(dev_drv);
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
	if(!support_uboot_display())
	{
		rk3188_lcdc_clk_enable(lcdc_dev);
	}
	else
	{
		lcdc_dev->clk_on = 1;
	}

	rk3188_lcdc_read_reg_defalut_cfg(lcdc_dev);
	if(lcdc_dev->id == 0)
	{
		#if defined(CONFIG_LCDC0_IO_18V)
		v = 0x40004000;               //bit14: 1,1.8v;0,3.3v
		writel_relaxed(v,RK30_GRF_BASE + GRF_IO_CON4);
		#else
		v = 0x40000000;              
		writel_relaxed(v,RK30_GRF_BASE + GRF_IO_CON4);
		#endif
	}

	if(lcdc_dev->id == 1) //iomux for lcdc1
	{
		#if defined(CONFIG_LCDC1_IO_18V)
		v = 0x80008000;               //bit14: 1,1.8v;0,3.3v
		writel_relaxed(v,RK30_GRF_BASE + GRF_IO_CON4);
		#else
		v = 0x80000000;
		writel_relaxed(v,RK30_GRF_BASE + GRF_IO_CON4);
		#endif
		iomux_set(LCDC1_DCLK);
		iomux_set(LCDC1_DEN);
		iomux_set(LCDC1_HSYNC);
		iomux_set(LCDC1_VSYNC);
		iomux_set(LCDC1_D0);
		iomux_set(LCDC1_D1);
		iomux_set(LCDC1_D2);
		iomux_set(LCDC1_D3);
		iomux_set(LCDC1_D4);
		iomux_set(LCDC1_D5);
		iomux_set(LCDC1_D6);
		iomux_set(LCDC1_D7);
		iomux_set(LCDC1_D8);
		iomux_set(LCDC1_D9);
		iomux_set(LCDC1_D10);
		iomux_set(LCDC1_D11);
		iomux_set(LCDC1_D12);
		iomux_set(LCDC1_D13);
		iomux_set(LCDC1_D14);
		iomux_set(LCDC1_D15);
		iomux_set(LCDC1_D16);
		iomux_set(LCDC1_D17);
		iomux_set(LCDC1_D18);
		iomux_set(LCDC1_D19);
		iomux_set(LCDC1_D20);
		iomux_set(LCDC1_D21);
		iomux_set(LCDC1_D22);
		iomux_set(LCDC1_D23);
		
	}
	lcdc_set_bit(lcdc_dev,SYS_CTRL,m_AUTO_GATING_EN);//eanble axi-clk auto gating for low power
	//lcdc_set_bit(lcdc_dev,DSP_CTRL0,m_WIN0_TOP);
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
	if((!support_uboot_display())||(dev_drv->screen_ctr_info->prop == EXTEND))
	{
		rk3188_lcdc_clk_disable(lcdc_dev);
	}
	
	return 0;
}


static void rk3188_lcdc_deint(struct rk3188_lcdc_device * lcdc_dev)
{
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_dev->clk_on = 0;
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_FS_INT_CLEAR, v_FS_INT_CLEAR(1));
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_HS_INT_EN | m_FS_INT_EN | 
			m_LF_INT_EN | m_BUS_ERR_INT_EN,v_HS_INT_EN(0) | v_FS_INT_EN(0) | 
			v_LF_INT_EN(0) | v_BUS_ERR_INT_EN(0));  //disable all lcdc interrupt
		lcdc_set_bit(lcdc_dev,SYS_CTRL,m_LCDC_STANDBY);
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
	}
	else   //clk already disabled 
	{
		spin_unlock(&lcdc_dev->reg_lock);
	}
	mdelay(1);
	
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
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				 m_DITHER_DOWN_SEL,v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
				 v_DITHER_DOWN_SEL(1));
			break;
		case OUT_P666:
			face = OUT_P666; //dither down to rgb666
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
				v_DITHER_DOWN_SEL(1));
			break;
		case OUT_D888_P565:
			face = OUT_P888;
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
				v_DITHER_DOWN_SEL(1));
			break;
		case OUT_D888_P666:
			face = OUT_P888;
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
				m_DITHER_DOWN_SEL, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
				v_DITHER_DOWN_SEL(1));
			break;
		case OUT_P888:
			face = OUT_P888;
			lcdc_msk_reg(lcdc_dev,DSP_CTRL0,m_DITHER_DOWN_EN | m_DITHER_UP_EN,
					v_DITHER_DOWN_EN(0) | v_DITHER_UP_EN(0));
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
			     v_BG_COLOR(0x000000) | v_DSP_BG_SWAP(screen->swap_gb) | 
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
	u8 fmt_cfg =0 ; //data format register config value
	char fmt[9] = "NULL";
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
	case ARGB888:
	case XBGR888:
	case ABGR888:
	     	fmt_cfg = 0;
		break;
	case RGB888:
		fmt_cfg = 1;
		break;
	case RGB565:
		fmt_cfg = 2;
		break;
	case YUV422:// yuv422
		fmt_cfg = 5;
		ScaleCbrX = CalScale((xact/2), par->xsize);
		ScaleCbrY = CalScale(yact, par->ysize);
		break;
	case YUV420: // yuv420
		fmt_cfg = 4;
		ScaleCbrX = CalScale(xact/2, par->xsize);
		ScaleCbrY = CalScale(yact/2, par->ysize);
		break;
	case YUV444:// yuv444
		fmt_cfg = 6;
		ScaleCbrX = CalScale(xact, par->xsize);
		ScaleCbrY = CalScale(yact, par->ysize);
		break;
	default:
		dev_err(lcdc_dev->driver.dev,"%s:un supported format!\n",__func__);
		break;
	}

	DBG(1,"lcdc%d>>%s>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
		lcdc_dev->id,__func__,get_format_string(par->format,fmt),xact,yact,par->xsize,par->ysize,xvir,yvir,xpos,ypos);
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		par->scale_yrgb_x = ScaleYrgbX;
		par->scale_yrgb_y = ScaleYrgbY;
		par->scale_cbcr_x = ScaleCbrX;
		par->scale_cbcr_y = ScaleCbrY;
		par->fmt_cfg = fmt_cfg;
		par->dsp_stx = xpos;
		par->dsp_sty = ypos;
		switch(par->format) 
		{
		case XBGR888:
		case ABGR888:
			par->vir_stride = v_ARGB888_VIRWIDTH(xvir);
			par->swap_rb = 1;
			break;
		case ARGB888:
			par->vir_stride = v_ARGB888_VIRWIDTH(xvir);
			par->swap_rb = 0;
			break;
		case RGB888:  //rgb888
			par->vir_stride = v_RGB888_VIRWIDTH(xvir);
			par->swap_rb = 0;
			break;
		case RGB565:  //rgb565
			par->vir_stride = v_RGB565_VIRWIDTH(xvir);
			par->swap_rb = 0;
			break;
		case YUV422:
		case YUV420:
		case YUV444:
			par->vir_stride = v_YUV_VIRWIDTH(xvir);
			par->swap_rb = 0;
			break;
		default:
			dev_err(lcdc_dev->driver.dev,"%s:un supported format!\n",__func__);
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
	u8 fmt_cfg;
	char fmt[9] = "NULL";
	xact = par->xact;			
	yact = par->yact;
	xvir = par->xvir;		
	yvir = par->yvir;
	xpos = par->xpos+screen->left_margin + screen->hsync_len;
	ypos = par->ypos+screen->upper_margin + screen->vsync_len;

	
	DBG(1,"lcdc%d>>%s>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
		lcdc_dev->id,__func__,get_format_string(par->format,fmt),xact,yact,par->xsize,par->ysize,xvir,yvir,xpos,ypos);

	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		par->dsp_stx = xpos;
		par->dsp_sty = ypos;
		switch(par->format)
		{
		case XBGR888:
		case ABGR888:
			fmt_cfg = 0;
			par->vir_stride = v_WIN1_ARGB888_VIRWIDTH(xvir);
			par->swap_rb = 1;
			break;
		case ARGB888:
			fmt_cfg = 0;
			par->vir_stride = v_WIN1_ARGB888_VIRWIDTH(xvir);
			par->swap_rb = 0;
			
			break;
		case RGB888:  //rgb888
			fmt_cfg = 1;
			par->vir_stride = v_WIN1_RGB888_VIRWIDTH(xvir);
			par->swap_rb = 0;
			break;
		case RGB565:  //rgb565
			fmt_cfg = 2;
			par->vir_stride = v_WIN1_RGB565_VIRWIDTH(xvir);
			par->swap_rb = 0;
			break;
		default:
			dev_err(lcdc_dev->driver.dev,"%s:un supported format!\n",__func__);
			break;
		}
		par->fmt_cfg = fmt_cfg;

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
	DBG(2,"lcdc%d>>%s:y_addr:0x%x>>uv_addr:0x%x\n",lcdc_dev->id,__func__,y_addr,uv_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		par->y_addr = y_addr;
		par->uv_addr = uv_addr;
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
	DBG(2,"lcdc%d>>%s>>y_addr:0x%x>>uv_addr:0x%x\n",lcdc_dev->id,__func__,y_addr,uv_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		par->y_addr = y_addr;
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
#if defined(WAIT_FOR_SYNC)
	int timeout;
	unsigned long flags;
#endif
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
	struct rk3188_lcdc_device * lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		switch(blank_mode)
		{
		case FB_BLANK_UNBLANK:
			lcdc_msk_reg(lcdc_dev,DSP_CTRL1,m_BLANK_EN ,v_BLANK_EN(0));
			break;
		case FB_BLANK_NORMAL:
			lcdc_msk_reg(lcdc_dev,DSP_CTRL1,m_BLANK_EN ,v_BLANK_EN(1));
			break;
		default:
			lcdc_msk_reg(lcdc_dev,DSP_CTRL1,m_BLANK_EN ,v_BLANK_EN(1));
			break;
		}
		lcdc_cfg_done(lcdc_dev);
		dev_info(dev_drv->dev,"blank mode:%d\n",blank_mode);
	}
	spin_unlock(&lcdc_dev->reg_lock);

    	return 0;
}


static int rk3188_lcdc_ioctl(struct rk_lcdc_device_driver *dev_drv, unsigned int cmd,unsigned long arg,int layer_id)
{
	struct rk3188_lcdc_device *lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);
	u32 panel_size[2];
	void __user *argp = (void __user *)arg;
	struct color_key_cfg clr_key_cfg;
	
	switch(cmd)
	{
		case RK_FBIOGET_PANEL_SIZE:    //get panel size
                	panel_size[0] = lcdc_dev->screen->x_res;
                	panel_size[1] = lcdc_dev->screen->y_res;
            		if(copy_to_user(argp, panel_size, 8)) 
				return -EFAULT;
			break;
		case RK_FBIOPUT_COLOR_KEY_CFG:
			if(copy_from_user(&clr_key_cfg,argp,sizeof(struct color_key_cfg ))) 
				return -EFAULT;
			lcdc_writel(lcdc_dev,WIN0_COLOR_KEY,clr_key_cfg.win0_color_key_cfg);
			lcdc_writel(lcdc_dev,WIN1_COLOR_KEY,clr_key_cfg.win1_color_key_cfg);
			break;
			
		default:
			break;
	}
	return 0;
}

static int rk3188_lcdc_early_suspend(struct rk_lcdc_device_driver *dev_drv)
{
	
	struct rk3188_lcdc_device *lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);

	if(dev_drv->screen0->standby)
		dev_drv->screen0->standby(1);
	if(dev_drv->screen_ctr_info->io_disable)
		dev_drv->screen_ctr_info->io_disable();

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_msk_reg(lcdc_dev,INT_STATUS,m_FS_INT_CLEAR,v_FS_INT_CLEAR(1));
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_LCDC_STANDBY,v_LCDC_STANDBY(1));
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
	}
	else  //clk already disabled
	{
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}

	rk3188_lcdc_clk_disable(lcdc_dev);
	return 0;
}

static int rk3188_lcdc_early_resume(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk3188_lcdc_device *lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);
	int i=0;
	int __iomem *c;
	int v;

	if(dev_drv->screen_ctr_info->io_enable) 		//power on
		dev_drv->screen_ctr_info->io_enable();
	
	if(!lcdc_dev->clk_on)
	{
		rk3188_lcdc_clk_enable(lcdc_dev);
	}
	rk3188_lcdc_reg_resume(lcdc_dev);  //resume reg

	spin_lock(&lcdc_dev->reg_lock);
	if(dev_drv->cur_screen->dsp_lut)			//resume dsp lut
	{
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(0));
		lcdc_cfg_done(lcdc_dev);
		mdelay(25);
		for(i=0;i<256;i++)
		{
			v = dev_drv->cur_screen->dsp_lut[i];
			c = lcdc_dev->dsp_lut_addr_base+i;
			writel_relaxed(v,c);
		}
		lcdc_msk_reg(lcdc_dev,SYS_CTRL,m_DSP_LUT_EN,v_DSP_LUT_EN(1));
	}
	
	if(lcdc_dev->atv_layer_cnt)
	{
		lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_LCDC_STANDBY,v_LCDC_STANDBY(0));
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);

	if(!lcdc_dev->atv_layer_cnt)
		rk3188_lcdc_clk_disable(lcdc_dev);

	if(dev_drv->screen0->standby)
		dev_drv->screen0->standby(0);	      //screen wake up
	
	return 0;
}

static int rk3188_lcdc_get_layer_state(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	return 0;
}

static int rk3188_lcdc_ovl_mgr(struct rk_lcdc_device_driver *dev_drv,int swap,bool set)
{
	struct rk3188_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3188_lcdc_device,driver);
	int ovl;
	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on)
	{
		if(set)  //set overlay
		{
			lcdc_msk_reg(lcdc_dev,DSP_CTRL0,m_WIN0_TOP,v_WIN0_TOP(swap));
			ovl = swap;
		}
		else  //get overlay
		{
			ovl = lcdc_read_bit(lcdc_dev,DSP_CTRL0,m_WIN0_TOP);
		}
	}
	else
	{
		ovl = -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return ovl;
}


static ssize_t rk3188_lcdc_get_disp_info(struct rk_lcdc_device_driver *dev_drv,char *buf,int layer_id)
{
	
	struct rk3188_lcdc_device *lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);

        char format_w0[9]= "NULL";
	char format_w1[9]= "NULL";
	char status_w0[9]= "NULL";
	char status_w1[9]= "NULL";
        u32 fmt_id = lcdc_readl(lcdc_dev,SYS_CTRL);
        u32 act_info,dsp_info,dsp_st,factor;
        u16 xvir_w0,x_act_w0,y_act_w0,x_dsp_w0,y_dsp_w0,x_st_w0,y_st_w0,x_factor,y_factor;
	u16 xvir_w1,x_dsp_w1,y_dsp_w1,x_st_w1,y_st_w1;
        u16 x_scale,y_scale;
	int ovl = lcdc_read_bit(lcdc_dev,DSP_CTRL0,m_WIN0_TOP);
	
        switch((fmt_id&m_WIN0_FORMAT)>>3)
        {
                case 0:
                        strcpy(format_w0,"ARGB888");
                        break;
                case 1:
                        strcpy(format_w0,"RGB888");
                        break;
                case 2:
                        strcpy(format_w0,"RGB565");
                        break;
                case 4:
                        strcpy(format_w0,"YCbCr420");
                        break;
                case 5:
                        strcpy(format_w0,"YCbCr422");
                        break;
                case 6:
                        strcpy(format_w0,"YCbCr444");
                        break;
                default:
                        strcpy(format_w0,"invalid\n");
                        break;
        }

	 switch((fmt_id&m_WIN1_FORMAT)>>6)
        {
                case 0:
                        strcpy(format_w1,"ARGB888");
                        break;
                case 1:
                        strcpy(format_w1,"RGB888");
                        break;
                case 2:
                        strcpy(format_w1,"RGB565");
                        break;
                case 4:
                        strcpy(format_w1,"8bpp");
                        break;
                case 5:
	                strcpy(format_w1,"4bpp");
	                break;
                case 6:
                        strcpy(format_w1,"2bpp");
                        break;
                case 7:
                        strcpy(format_w1,"1bpp");
                        break;
                default:
                        strcpy(format_w1,"invalid\n");
                        break;
        }

	if(fmt_id&m_WIN0_EN)
	{
		strcpy(status_w0,"enabled");
	}
	else
	{
		strcpy(status_w0,"disabled");
	}

	if((fmt_id&m_WIN1_EN)>>1)
	{
		strcpy(status_w1,"enabled");
	}
	else
	{
		strcpy(status_w1,"disabled");
	}

        xvir_w0 = lcdc_readl(lcdc_dev,WIN_VIR)&0x1fff;
        act_info = lcdc_readl(lcdc_dev,WIN0_ACT_INFO);
        dsp_info = lcdc_readl(lcdc_dev,WIN0_DSP_INFO);
        dsp_st = lcdc_readl(lcdc_dev,WIN0_DSP_ST);
        factor = lcdc_readl(lcdc_dev,WIN0_SCL_FACTOR_YRGB);
        x_act_w0= (act_info&0x1fff) + 1;
        y_act_w0= ((act_info>>16)&0x1fff) + 1;
        x_dsp_w0= (dsp_info&0x7ff) + 1;
        y_dsp_w0= ((dsp_info>>16)&0x7ff) + 1;
	x_st_w0 = dsp_st&0xffff;
        y_st_w0 = dsp_st>>16;
	x_factor = factor&0xffff;
        y_factor = factor>>16;
        x_scale = 4096*100/x_factor;
        y_scale = 4096*100/y_factor;

	xvir_w1= (lcdc_readl(lcdc_dev,WIN_VIR)>> 16)&0x1fff;
        dsp_info = lcdc_readl(lcdc_dev,WIN1_DSP_INFO);
        dsp_st = lcdc_readl(lcdc_dev,WIN1_DSP_ST);
        x_dsp_w1= (dsp_info&0x7ff) + 1;
        y_dsp_w1= ((dsp_info>>16)&0x7ff) + 1;
	x_st_w1 = dsp_st&0xffff;
        y_st_w1 = dsp_st>>16;
        return snprintf(buf,PAGE_SIZE,
		"win0:%s\n"
		"xvir:%d\n"
		"xact:%d\n"
		"yact:%d\n"
		"xdsp:%d\n"
		"ydsp:%d\n"
		"x_st:%d\n"
		"y_st:%d\n"
		"x_scale:%d.%d\n"
		"y_scale:%d.%d\n"
		"format:%s\n"
		"YRGB buffer addr:0x%08x\n"
		"CBR buffer addr:0x%08x\n\n"
		"win1:%s\n"
		"xvir:%d\n"
		"xdsp:%d\n"
		"ydsp:%d\n"
		"x_st:%d\n"
		"y_st:%d\n"
		"format:%s\n"
		"YRGB buffer addr:0x%08x\n"
		"overlay:%s\n",
		status_w0,
                xvir_w0,
                x_act_w0,
                y_act_w0,
                x_dsp_w0,
                y_dsp_w0,
               	x_st_w0,
               	y_st_w0,
                x_scale/100,
                x_scale%100,
                y_scale/100,
                y_scale%100,
                format_w0,
                lcdc_readl(lcdc_dev,WIN0_YRGB_MST0),
		lcdc_readl(lcdc_dev,WIN0_CBR_MST0),
                status_w1,
                xvir_w1,
                x_dsp_w1,
                y_dsp_w1,
                x_st_w1,
                y_st_w1,
                format_w1,
                lcdc_readl(lcdc_dev,WIN1_MST),
                ovl ? "win0 on the top of win1\n":"win1 on the top of win0\n");

}


static int rk3188_lcdc_fps_mgr(struct rk_lcdc_device_driver *dev_drv,int fps,bool set)
{
	struct rk3188_lcdc_device *lcdc_dev = 
		container_of(dev_drv,struct rk3188_lcdc_device,driver);
	rk_screen * screen = dev_drv->cur_screen;
	u64 ft = 0;
	u32 dotclk;
	int ret;
	u32 pixclock;
	if(set)
	{
		ft = div_u64(1000000000000llu,fps);
		dev_drv->pixclock = div_u64(ft,(screen->upper_margin + screen->lower_margin + screen->y_res +screen->vsync_len)*
				(screen->left_margin + screen->right_margin + screen->x_res + screen->hsync_len));
		dotclk = div_u64(1000000000000llu,dev_drv->pixclock);
		ret = clk_set_rate(lcdc_dev->dclk, dotclk);
		if(ret)
		{
	        	printk(KERN_ERR ">>>>>> set lcdc%d dclk failed\n",lcdc_dev->id);
			return 0;
		}			
	}
	
	
	pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	dev_drv->pixclock = lcdc_dev->pixclock = pixclock;
	fps = rk_fb_calc_fps(lcdc_dev->screen,pixclock);
	screen->ft = 1000/fps ;  //one frame time in ms

	if(set)
		dev_info(dev_drv->dev,"%s:dclk:%lu,fps:%d\n",__func__,clk_get_rate(lcdc_dev->dclk),fps);
	
	return fps;
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
	.lcdc_reg_update	= rk3188_lcdc_reg_update,
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
	ktime_t timestamp = ktime_get();
	
	lcdc_msk_reg(lcdc_dev, INT_STATUS, m_FS_INT_CLEAR, v_FS_INT_CLEAR(1));

	if(lcdc_dev->driver.wait_fs)  //three buffer ,no need to wait for sync
	{
		spin_lock(&(lcdc_dev->driver.cpl_lock));
		complete(&(lcdc_dev->driver.frame_done));
		spin_unlock(&(lcdc_dev->driver.cpl_lock));
	}
	lcdc_dev->driver.vsync_info.timestamp = timestamp;
	wake_up_interruptible_all(&lcdc_dev->driver.vsync_info.wait);
	
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
#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)
	rk_screen *screen1;
#endif
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
	screen =  kzalloc(sizeof(rk_screen), GFP_KERNEL);
	if(!screen_ctr_info)
	{
		dev_err(dev, "no platform data specified for screen control info!\n");
		ret = -EINVAL;
		goto err0;
	}
	if(!screen)
	{
		dev_err(&pdev->dev, "rk screen kmalloc fail!");
		ret = -ENOMEM;
		goto err0;
	}
	else
	{
		lcdc_dev->screen = screen;
	}
	screen->lcdc_id = lcdc_dev->id;
	screen->screen_id = 0;
#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)&&  (defined(CONFIG_RK610_LVDS) || defined(CONFIG_RK616_LVDS))
	screen1 =  kzalloc(sizeof(rk_screen), GFP_KERNEL);
	if(!screen1)
	{
		dev_err(&pdev->dev, ">>rk3066b lcdc screen1 kmalloc fail!");
        	ret =  -ENOMEM;
		goto err0;
	}
	screen1->lcdc_id = 1;
	screen1->screen_id = 1;
	printk("use lcdc%d and jetta implemention dual display!\n",lcdc_dev->id);
	
#endif
	
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
#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)&& (defined(CONFIG_RK610_LVDS) || defined(CONFIG_RK616_LVDS))
	lcdc_dev->driver.screen1 = screen1;
#endif
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

	if(screen_ctr_info->set_screen_info)
	{
		screen_ctr_info->set_screen_info(screen,screen_ctr_info->lcd_info);
		if(SCREEN_NULL==screen->type)
		{
			printk(KERN_WARNING "no display device on lcdc%d!?\n",lcdc_dev->id);
			ret = -ENODEV;
		}
		if(screen_ctr_info->io_init)
			screen_ctr_info->io_init(NULL);
	}
	else
	{
		printk(KERN_WARNING "no display device on lcdc%d!?\n",lcdc_dev->id);
		ret = -ENODEV;
		goto err3;
	}
	
	ret = rk_fb_register(&(lcdc_dev->driver),&lcdc_driver,lcdc_dev->id);
	if(ret < 0)
	{
		dev_err(dev,"register fb for lcdc%d failed!\n",lcdc_dev->id);
		goto err4;
	}
	
	printk("rk3188 lcdc%d probe ok!\n",lcdc_dev->id);
	
	return 0;

err4:
err3:
	iounmap(lcdc_dev->regs);
err2:
	release_mem_region(lcdc_dev->reg_phy_base,lcdc_dev->len);
err1:
	kfree(screen);
err0:
	platform_set_drvdata(pdev, NULL);

	return ret;
}

static int __devexit rk3188_lcdc_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk3188_lcdc_shutdown(struct platform_device *pdev)
{
	struct rk3188_lcdc_device *lcdc_dev = platform_get_drvdata(pdev);
	if(lcdc_dev->driver.cur_screen->standby) //standby the screen if necessary
		lcdc_dev->driver.cur_screen->standby(1);
	if(lcdc_dev->driver.screen_ctr_info->io_disable) //power off the screen if necessary
		lcdc_dev->driver.screen_ctr_info->io_disable();
	rk3188_lcdc_deint(lcdc_dev);
	rk_fb_unregister(&(lcdc_dev->driver));	
}
static struct platform_driver rk3188_lcdc_driver = {
	.probe		= rk3188_lcdc_probe,
	.remove		= __devexit_p(rk3188_lcdc_remove),
	.driver		= {
		.name 	= "rk30-lcdc",
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
