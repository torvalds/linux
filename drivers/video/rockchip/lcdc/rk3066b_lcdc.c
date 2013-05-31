/*
 * drivers/video/rockchip/chips/rk3066b_lcdc.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *Author:yzq<yzq@rock-chips.com>
 *	yxj<yxj@rock-chips.com>
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

#include "rk3066b_lcdc.h"






static int dbg_thresd = 0;
module_param(dbg_thresd, int, S_IRUGO|S_IWUSR);
#define DBG(level,x...) do { if(unlikely(dbg_thresd >= level)) printk(KERN_INFO x); } while (0)


static int  rk3066b_lcdc_clk_enable(struct rk3066b_lcdc_device *lcdc_dev)
{

	clk_enable(lcdc_dev->hclk);
	clk_enable(lcdc_dev->dclk);
	clk_enable(lcdc_dev->aclk);
	clk_enable(lcdc_dev->pd);

	spin_lock(&lcdc_dev->reg_lock);
	lcdc_dev->clk_on = 1;
	spin_unlock(&lcdc_dev->reg_lock);
	printk("rk3066b lcdc%d clk enable...\n",lcdc_dev->id);
	
	return 0;
}

static int rk3066b_lcdc_clk_disable(struct rk3066b_lcdc_device *lcdc_dev)
{
	spin_lock(&lcdc_dev->reg_lock);
	lcdc_dev->clk_on = 0;
	spin_unlock(&lcdc_dev->reg_lock);
	mdelay(25);
	clk_disable(lcdc_dev->dclk);
	clk_disable(lcdc_dev->hclk);
	clk_disable(lcdc_dev->aclk);
	clk_disable(lcdc_dev->pd);
	printk("rk3066b lcdc%d clk disable...\n",lcdc_dev->id);
	
	return 0;
}

static int rk3066b_lcdc_reg_resume(struct rk3066b_lcdc_device *lcdc_dev)
{
	memcpy((u8*)lcdc_dev->preg, (u8*)&lcdc_dev->regbak, 0x9C);
	return 0;	
}

static int init_rk3066b_lcdc(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	int v;
	
	if(lcdc_dev->id == 0) //lcdc0
	{
		lcdc_dev->pd = clk_get(NULL,"pd_lcdc0");
		lcdc_dev->hclk = clk_get(NULL,"hclk_lcdc0"); 
		lcdc_dev->aclk = clk_get(NULL,"aclk_lcdc0");
		lcdc_dev->dclk = clk_get(NULL,"dclk_lcdc0");
	}
	else if(lcdc_dev->id == 1)
	{
		lcdc_dev->pd = clk_get(NULL,"pd_lcdc1");
		lcdc_dev->hclk = clk_get(NULL,"hclk_lcdc1");  
		lcdc_dev->aclk = clk_get(NULL,"aclk_lcdc1");
		lcdc_dev->dclk = clk_get(NULL,"dclk_lcdc1");
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

	rk3066b_lcdc_clk_enable(lcdc_dev);
	
	if(lcdc_dev->id == 0)
	{
		#if defined(CONFIG_RK3066B_LCDC0_IO_18V)
		v = 0x40004000;               //bit14: 1,1.8v;0,3.3v
		writel_relaxed(v,RK30_GRF_BASE + GRF_IO_CON4);
		#else
		v = 0x40000000;              
		writel_relaxed(v,RK30_GRF_BASE + GRF_IO_CON4);
		#endif
	}

	if(lcdc_dev->id == 1) //iomux for lcdc1
	{
		#if defined(CONFIG_RK3066B_LCDC1_IO_18V)
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
	LcdMskReg(lcdc_dev,SYS_CFG, m_LCDC_AXICLK_AUTO_ENABLE | m_W0_AXI_OUTSTANDING2 |
		m_W1_AXI_OUTSTANDING2,v_LCDC_AXICLK_AUTO_ENABLE(1) | v_W0_AXI_OUTSTANDING2(1) |
		v_W1_AXI_OUTSTANDING2(1));//eanble axi-clk auto gating for low power
	 LcdWrReg(lcdc_dev,AXI_MS_ID,v_HWC_CHANNEL_ID(5) | v_WIN2_CHANNEL_ID(4) |
	 	v_WIN1_YRGB_CHANNEL_ID(3) | v_WIN0_CBR_CHANNEL_ID(2) | 
	 	v_WIN0_YRGB_CHANNEL_ID(1));
	LcdMskReg(lcdc_dev, INT_STATUS,m_HOR_STARTMASK| m_FRM_STARTMASK | 
    	      m_SCANNING_MASK, v_HOR_STARTMASK(1) | v_FRM_STARTMASK(1) | 
    	      v_SCANNING_MASK(1));  //mask all interrupt in init
	LcdMskReg(lcdc_dev,FIFO_WATER_MARK,m_WIN1_FIFO_FULL_LEVEL,v_WIN1_FIFO_FULL_LEVEL(0x1e0));
	//LCDC_REG_CFG_DONE();  // write any value to  REG_CFG_DONE let config become effective

	rk3066b_lcdc_clk_disable(lcdc_dev);
	return 0;
}

static int rk3066b_lcdc_deinit(struct rk3066b_lcdc_device *lcdc_dev)
{
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_dev->clk_on = 0;
		LcdMskReg(lcdc_dev, INT_STATUS, m_FRM_STARTCLEAR, v_FRM_STARTCLEAR(1));
		LcdMskReg(lcdc_dev, INT_STATUS,m_HOR_STARTMASK| m_FRM_STARTMASK | 
    	      		  m_SCANNING_MASK, v_HOR_STARTMASK(1) | v_FRM_STARTMASK(1) | 
    	                  v_SCANNING_MASK(1));  //mask all interrupt in init
		LcdSetBit(lcdc_dev,SYS_CFG,m_LCDC_STANDBY);
		LCDC_REG_CFG_DONE();
		spin_unlock(&lcdc_dev->reg_lock);
	}
	else   //clk already disabled 
	{
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}
	mdelay(1);
	
	return 0;
}

static int rk3066b_load_screen(struct rk_lcdc_device_driver *dev_drv, bool initscreen)
{
	int ret = -EINVAL;
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	rk_screen *screen = dev_drv->cur_screen;
	u64 ft;
	int fps;
	u16 face;
	u16 mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend;
	u16 right_margin = screen->right_margin;
	u16 lower_margin = screen->lower_margin;
	u16 x_res = screen->x_res, y_res = screen->y_res;

	// set the rgb or mcu
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(screen->type==SCREEN_MCU)
		{
	    		LcdMskReg(lcdc_dev, MCU_CTRL, m_MCU_OUTPUT_SELECT,v_MCU_OUTPUT_SELECT(1));
			// set out format and mcu timing
	   		mcu_total  = (screen->mcu_wrperiod*150*1000)/1000000;
	    		if(mcu_total>31)    
				mcu_total = 31;
	   		if(mcu_total<3)    
				mcu_total = 3;
	    		mcu_rwstart = (mcu_total+1)/4 - 1;
	    		mcu_rwend = ((mcu_total+1)*3)/4 - 1;
	    		mcu_csstart = (mcu_rwstart>2) ? (mcu_rwstart-3) : (0);
	    		mcu_csend = (mcu_rwend>15) ? (mcu_rwend-1) : (mcu_rwend);

	    		//DBG(1,">> mcu_total=%d, mcu_rwstart=%d, mcu_csstart=%d, mcu_rwend=%d, mcu_csend=%d \n",
	        	//	mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend);

			// set horizontal & vertical out timing
		
		    	right_margin = x_res/6; 
			screen->pixclock = 150000000; //mcu fix to 150 MHz
			LcdMskReg(lcdc_dev, MCU_CTRL,m_MCU_CS_ST | m_MCU_CS_END| m_MCU_RW_ST | m_MCU_RW_END |
	             		m_MCU_WRITE_PERIOD | m_MCU_HOLDMODE_SELECT | m_MCU_HOLDMODE_FRAME_ST,
	            		v_MCU_CS_ST(mcu_csstart) | v_MCU_CS_END(mcu_csend) | v_MCU_RW_ST(mcu_rwstart) |
	            		v_MCU_RW_END(mcu_rwend) |  v_MCU_WRITE_PERIOD(mcu_total) |
	            		v_MCU_HOLDMODE_SELECT((SCREEN_MCU==screen->type)?(1):(0)) | v_MCU_HOLDMODE_FRAME_ST(0));
	
		}

		switch (screen->face)
		{
	        	case OUT_P565:
	            		face = OUT_P565;
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
	            		break;
	        	case OUT_P666:
	            		face = OUT_P666;
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
	            		break;
	        	case OUT_D888_P565:
	            		face = OUT_P888;
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
	            		break;
	        	case OUT_D888_P666:
	            		face = OUT_P888;
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
	            		break;
	        	case OUT_P888:
	            		face = OUT_P888;
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(1));
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
	            		break;
	        	default:
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_UP_EN, v_DITHER_UP_EN(0));
	            		LcdMskReg(lcdc_dev, DSP_CTRL0, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
	            		face = screen->face;
	            		break;
		}

		//use default overlay,set vsyn hsync den dclk polarity
		LcdMskReg(lcdc_dev, DSP_CTRL0,m_DISPLAY_FORMAT | m_HSYNC_POLARITY | m_VSYNC_POLARITY |
	     		m_DEN_POLARITY |m_DCLK_POLARITY,v_DISPLAY_FORMAT(face) | 
	     		v_HSYNC_POLARITY(screen->pin_hsync) | v_VSYNC_POLARITY(screen->pin_vsync) |
	        	v_DEN_POLARITY(screen->pin_den) | v_DCLK_POLARITY(screen->pin_dclk));

		//set background color to black,set swap according to the screen panel,disable blank mode
		LcdMskReg(lcdc_dev,DSP_CTRL1,m_BLANK_MODE | m_BLACK_MODE | m_BG_COLOR, v_BLANK_MODE(0) | 
					v_BLACK_MODE(0) | v_BG_COLOR(0x000000));
		LcdMskReg(lcdc_dev,SWAP_CTRL,m_OUTPUT_RB_SWAP | m_OUTPUT_RG_SWAP | m_DELTA_SWAP | m_DUMMY_SWAP, 
					v_OUTPUT_RB_SWAP(screen->swap_rb) | v_OUTPUT_RG_SWAP(screen->swap_rg) | 
		 			v_DELTA_SWAP(screen->swap_delta) | v_DUMMY_SWAP(screen->swap_dumy));
		LcdWrReg(lcdc_dev, DSP_HTOTAL_HS_END,v_HSYNC(screen->hsync_len) |
	             v_HORPRD(screen->hsync_len + screen->left_margin + x_res + right_margin));
		LcdWrReg(lcdc_dev, DSP_HACT_ST_END, v_HAEP(screen->hsync_len + screen->left_margin + x_res) |
	             v_HASP(screen->hsync_len + screen->left_margin));

		LcdWrReg(lcdc_dev, DSP_VTOTAL_VS_END, v_VSYNC(screen->vsync_len) |
	              v_VERPRD(screen->vsync_len + screen->upper_margin + y_res + lower_margin));
		LcdWrReg(lcdc_dev, DSP_VACT_ST_END,  v_VAEP(screen->vsync_len + screen->upper_margin+y_res)|
	              v_VASP(screen->vsync_len + screen->upper_margin));
		// let above to take effect
		//LCDC_REG_CFG_DONE();
	}
 	spin_unlock(&lcdc_dev->reg_lock);

	ret = clk_set_rate(lcdc_dev->dclk, screen->pixclock);
	if(ret)
	{
        	printk(KERN_ERR ">>>>>> set lcdc%d dclk failed\n",lcdc_dev->id);
	}
    	lcdc_dev->driver.pixclock = lcdc_dev->pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	
	
	ft = (u64)(screen->upper_margin + screen->lower_margin + screen->y_res +screen->vsync_len)*
		(screen->left_margin + screen->right_margin + screen->x_res + screen->hsync_len)*
		(dev_drv->pixclock);       // one frame time ,(pico seconds)
	fps = div64_u64(1000000000000llu,ft);
	screen->ft = 1000/fps;
    	printk("%s: dclk:%lu>>fps:%d ",lcdc_dev->driver.name,clk_get_rate(lcdc_dev->dclk),fps);

    	if(screen->init)
    	{
    		screen->init();
    	}
	printk("%s for lcdc%d ok!\n",__func__,lcdc_dev->id);
	return 0;
}

static int mcu_refresh(struct rk3066b_lcdc_device *lcdc_dev)
{
   
    return 0;
}



//enable layer,open:1,enable;0 disable
static int win0_open(struct rk3066b_lcdc_device *lcdc_dev,bool open)
{
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(open)
		{
			if(!lcdc_dev->atv_layer_cnt)
			{
				LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(0));
			}
			lcdc_dev->atv_layer_cnt++;
		}
		else
		{
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.layer_par[0]->state = open;
		
		LcdMskReg(lcdc_dev, SYS_CFG, m_W0_EN, v_W0_EN(open));
		if(!lcdc_dev->atv_layer_cnt)  //if no layer used,disable lcdc
		{
			LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(1));
		}
		//LCDC_REG_CFG_DONE();	
	}
	spin_unlock(&lcdc_dev->reg_lock);
	printk(KERN_INFO "lcdc%d win0 %s\n",lcdc_dev->id,open?"open":"closed");
	return 0;
}
static int win1_open(struct rk3066b_lcdc_device *lcdc_dev,bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(open)
		{
			if(!lcdc_dev->atv_layer_cnt)
			{
				printk("lcdc%d wakeup from stanby\n",lcdc_dev->id);
				LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(0));
			}
			lcdc_dev->atv_layer_cnt++;
		}
		else
		{
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.layer_par[1]->state = open;
		
		LcdMskReg(lcdc_dev, SYS_CFG, m_W1_EN, v_W1_EN(open));
		if(!lcdc_dev->atv_layer_cnt)  //if no layer used,disable lcdc
		{
			printk(KERN_INFO "no layer of lcdc%d is used,go to standby!",lcdc_dev->id);
			LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(1));
		}
		LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);
	printk(KERN_INFO "lcdc%d win1 %s\n",lcdc_dev->id,open?"open":"closed");
	return 0;
}


static int rk3066b_lcdc_blank(struct rk_lcdc_device_driver*lcdc_drv,int layer_id,int blank_mode)
{
	struct rk3066b_lcdc_device * lcdc_dev = container_of(lcdc_drv,struct rk3066b_lcdc_device ,driver);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		switch(blank_mode)
	    	{
	    		case FB_BLANK_UNBLANK:
	      			LcdMskReg(lcdc_dev,DSP_CTRL1,m_BLANK_MODE ,v_BLANK_MODE(0));
				break;
	    		case FB_BLANK_NORMAL:
	         		LcdMskReg(lcdc_dev,DSP_CTRL1,m_BLANK_MODE ,v_BLANK_MODE(1));
				break;
	    		default:
				LcdMskReg(lcdc_dev,DSP_CTRL1,m_BLANK_MODE ,v_BLANK_MODE(1));
				break;
		}
		LCDC_REG_CFG_DONE();
		printk(KERN_INFO "%s>>>>>%d\n",__func__, blank_mode);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
    	return 0;
}

static  int win0_display(struct rk3066b_lcdc_device *lcdc_dev,struct layer_par *par )
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = par->smem_start + par->y_offset;
    	uv_addr = par->cbr_start + par->c_offset;
	DBG(2,KERN_INFO "lcdc%d>>%s:y_addr:0x%x>>uv_addr:0x%x\n",lcdc_dev->id,__func__,y_addr,uv_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		LcdWrReg(lcdc_dev, WIN0_YRGB_MST,y_addr);
	    	LcdWrReg(lcdc_dev, WIN0_CBR_MST,uv_addr);
		LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
	
}

static  int win1_display(struct rk3066b_lcdc_device *lcdc_dev,struct layer_par *par )
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = par->smem_start + par->y_offset;
    	uv_addr = par->cbr_start + par->c_offset;
	DBG(2,KERN_INFO "lcdc%d>>%s>>y_addr:0x%x>>uv_addr:0x%x\n",lcdc_dev->id,__func__,y_addr,uv_addr);
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		LcdWrReg(lcdc_dev, WIN1_YRGB_MST, y_addr);
		LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
	return 0;
}

static  int win0_set_par(struct rk3066b_lcdc_device *lcdc_dev,rk_screen *screen,
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
   
	DBG(1,"%s for lcdc%d>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
		__func__,lcdc_dev->id,get_format_string(par->format,fmt),xact,yact,par->xsize,par->ysize,xvir,yvir,xpos,ypos);
	
	ScaleYrgbX = CalScale(xact, par->xsize); //both RGB and yuv need this two factor
	ScaleYrgbY = CalScale(yact, par->ysize);
	switch (par->format)
	{
		case XBGR888:
		case ABGR888:
		case ARGB888:
			fmt_cfg = 0;
			break;
		case RGB565:
			fmt_cfg = 1;
			break;
		case YUV422:// yuv422
			fmt_cfg = 2;
			ScaleCbrX = CalScale((xact/2), par->xsize);
			ScaleCbrY = CalScale(yact, par->ysize);
			break;
		case YUV420: // yuv420
			fmt_cfg = 3;
			ScaleCbrX = CalScale(xact/2, par->xsize);
		   	ScaleCbrY = CalScale(yact/2, par->ysize);
		   	break;
		case YUV444:// yuv444
			fmt_cfg = 4;
			ScaleCbrX = CalScale(xact, par->xsize);
			ScaleCbrY = CalScale(yact, par->ysize);
			break;
		default:
			printk("%s:un supported format\n",__func__);
		   break;
	}

	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_YRGB, v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
		LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_CBR,v_X_SCL_FACTOR(ScaleCbrX)| v_Y_SCL_FACTOR(ScaleCbrY));
		LcdMskReg(lcdc_dev,SYS_CFG, m_W0_FORMAT, v_W0_FORMAT(fmt_cfg));		//(inf->video_mode==0)
		LcdWrReg(lcdc_dev, WIN0_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
		LcdWrReg(lcdc_dev, WIN0_DSP_ST, v_DSP_STX(xpos) | v_DSP_STY(ypos));
		LcdWrReg(lcdc_dev, WIN0_DSP_INFO, v_DSP_WIDTH(par->xsize)| v_DSP_HEIGHT(par->ysize));
		LcdMskReg(lcdc_dev, WIN0_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR,
			v_COLORKEY_EN(0) | v_KEYCOLOR(0));
		LcdWrReg(lcdc_dev,WIN0_VIR,v_VIRWIDTH(xvir));
		//LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);

    return 0;

}

static int win1_set_par(struct rk3066b_lcdc_device *lcdc_dev,rk_screen *screen,
	struct layer_par *par )
{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u8 fmt_cfg;
	char fmt[9]="NULL";
	xact = par->xact;			
	yact = par->yact;
	xvir = par->xvir;		
	yvir = par->yvir;
	xpos = par->xpos+screen->left_margin + screen->hsync_len;
	ypos = par->ypos+screen->upper_margin + screen->vsync_len;
	
	
	DBG(1,"%s for lcdc%d>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
		__func__,lcdc_dev->id,get_format_string(par->format,fmt),xact,yact,par->xsize,par->ysize,xvir,yvir,xpos,ypos);

	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		switch (par->format)
	 	{
	 		case XBGR888:
			case ABGR888:
			case ARGB888:
				fmt_cfg = 0;
				break;
			case RGB565:
				fmt_cfg = 1;
				break;
			default:
				printk("%s:un supported format\n",__func__);
				break;
		}

		LcdMskReg(lcdc_dev,SYS_CFG, m_W1_FORMAT, v_W1_FORMAT(fmt_cfg));
		LcdWrReg(lcdc_dev, WIN1_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
		LcdWrReg(lcdc_dev, WIN1_DSP_INFO,v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
		// enable win1 color key and set the color to black(rgb=0)
		LcdMskReg(lcdc_dev,WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR,v_COLORKEY_EN(1) | v_KEYCOLOR(0));
		LcdWrReg(lcdc_dev,WIN1_VIR,v_VIRWIDTH(xvir));
		
		//LCDC_REG_CFG_DONE(); 
	}
	spin_unlock(&lcdc_dev->reg_lock);
    return 0;
}

static int rk3066b_lcdc_open(struct rk_lcdc_device_driver *dev_drv,int layer_id,bool open)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);

	if((open) && (!lcdc_dev->atv_layer_cnt)) //enable clk,when first layer open
	{
		rk3066b_lcdc_clk_enable(lcdc_dev);
		rk3066b_lcdc_reg_resume(lcdc_dev); //resume reg
		LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(0));
		rk3066b_load_screen(dev_drv,1);
	}
	if(layer_id == 0)
	{
		win0_open(lcdc_dev,open);	
	}
	else if(layer_id == 1)
	{
		win1_open(lcdc_dev,open);
	}
	
	if((!open) && (!lcdc_dev->atv_layer_cnt))  //when all layer closed,disable clk
	{
		LcdMskReg(lcdc_dev, INT_STATUS, m_FRM_STARTCLEAR, v_FRM_STARTCLEAR(1));
		LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(1));
		LCDC_REG_CFG_DONE();
		rk3066b_lcdc_clk_disable(lcdc_dev);
	}
	return 0;
}

static int rk3066b_lcdc_set_par(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	struct layer_par *par = NULL;
	rk_screen *screen = dev_drv->cur_screen;
	if(!screen)
	{
		printk(KERN_ERR "screen is null!\n");
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
	
	return 0;
}

int rk3066b_lcdc_pan_display(struct rk_lcdc_device_driver * dev_drv,int layer_id)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	struct layer_par *par = NULL;
	rk_screen *screen = dev_drv->cur_screen;
	unsigned long flags;
	int timeout;
	if(!screen)
	{
		printk(KERN_ERR "screen is null!\n");
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
	if((dev_drv->first_frame))  //this is the first frame of the system ,enable frame start interrupt
	{
		dev_drv->first_frame = 0;
		LcdMskReg(lcdc_dev,INT_STATUS,m_FRM_STARTCLEAR | m_FRM_STARTMASK ,
			  v_FRM_STARTCLEAR(1) | v_FRM_STARTMASK(0));
		LCDC_REG_CFG_DONE();  // write any value to  REG_CFG_DONE let config become effective
		 
	}

#if 0
	if(dev_drv->num_buf < 3) //3buffer ,no need to  wait for sysn
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
#endif
	
	return 0;
}

int rk3066b_lcdc_ioctl(struct rk_lcdc_device_driver * dev_drv,unsigned int cmd, unsigned long arg,int layer_id)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	u32 panel_size[2];
	void __user *argp = (void __user *)arg;
	int ret = 0;
	switch(cmd)
	{
		case RK_FBIOGET_PANEL_SIZE:    //get panel size
                	panel_size[0] = lcdc_dev->screen->x_res;
                	panel_size[1] = lcdc_dev->screen->y_res;
            		if(copy_to_user(argp, panel_size, 8)) 
				return -EFAULT;
			break;
		default:
			break;
	}

	return ret;
}
static int rk3066b_lcdc_get_layer_state(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	struct layer_par *par = dev_drv->layer_par[layer_id];

	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on)
	{
		if(layer_id == 0)
		{
			par->state = LcdReadBit(lcdc_dev,SYS_CFG,m_W0_EN);
		}
		else if( layer_id == 1)
		{
			par->state = LcdReadBit(lcdc_dev,SYS_CFG,m_W1_EN);
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
	return par->state;
	
}

/***********************************
overlay manager
swap:1 win0 on the top of win1
        0 win1 on the top of win0
set  : 1 set overlay 
        0 get overlay state
************************************/
static int rk3066b_lcdc_ovl_mgr(struct rk_lcdc_device_driver *dev_drv,int swap,bool set)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	int ovl;
	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on)
	{
		if(set)  //set overlay
		{
			LcdMskReg(lcdc_dev,DSP_CTRL0,m_W0W1_POSITION_SWAP,v_W0W1_POSITION_SWAP(swap));
			LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);
			LCDC_REG_CFG_DONE();
			ovl = swap;
		}
		else  //get overlay
		{
			ovl = LcdReadBit(lcdc_dev,DSP_CTRL0,m_W0W1_POSITION_SWAP);
		}
	}
	else
	{
		ovl = -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return ovl;
}

static ssize_t rk3066b_lcdc_get_disp_info(struct rk_lcdc_device_driver *dev_drv,char *buf,int layer_id)

{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	char format_w0[9]= "NULL";
	char format_w1[9]= "NULL";
	char status_w0[9]= "NULL";
	char status_w1[9]= "NULL";
	u32 fmt_id = LcdRdReg(lcdc_dev,SYS_CFG);
	u32 act_info,dsp_info,dsp_st,factor;
	u16 xvir_w0,x_act_w0,y_act_w0,x_dsp_w0,y_dsp_w0,x_st_w0,y_st_w0;
	u16 xvir_w1,x_dsp_w1,y_dsp_w1,x_st_w1,y_st_w1;
	u16 x_scale,y_scale;
	int ovl = LcdReadBit(lcdc_dev,DSP_CTRL0,m_W0W1_POSITION_SWAP);

        switch((fmt_id&m_W0_FORMAT)>>3)
        {
                case 0:
                        strcpy(format_w0,"ARGB888");
                        break;
                case 1:
                        strcpy(format_w0,"RGB565");
                        break;
                case 2:
                        strcpy(format_w0,"YCbCr422");
                        break;
                case 3:
                        strcpy(format_w0,"YCbCr420");
                        break;
                case 5:
                        strcpy(format_w0,"YCbCr444");
                        break;
                default:
                        strcpy(format_w0,"invalid\n");
                        break;
        }

	 switch((fmt_id&m_W1_FORMAT)>>2)
        {
                case 0:
                        strcpy(format_w1,"ARGB888");
                        break;
                case 1:
                        strcpy(format_w1,"RGB565");
                        break;
                default:
                        strcpy(format_w1,"invalid\n");
                        break;
        }

	if((fmt_id&m_W0_EN)>>11)
	{
		strcpy(status_w0,"enabled");
	}
	else
	{
		strcpy(status_w0,"disabled");
	}

	if((fmt_id&m_W1_EN)>>10)
	{
		strcpy(status_w1,"enabled");
	}
	else
	{
		strcpy(status_w1,"disabled");
	}

	xvir_w0 = LcdRdReg(lcdc_dev,WIN0_VIR)&0xffff;
	act_info = LcdRdReg(lcdc_dev,WIN0_ACT_INFO);
	dsp_info = LcdRdReg(lcdc_dev,WIN0_DSP_INFO);
	dsp_st = LcdRdReg(lcdc_dev,WIN0_DSP_ST);
	factor = LcdRdReg(lcdc_dev,WIN0_SCL_FACTOR_YRGB);
	x_act_w0 =  (act_info&0xffff);
	y_act_w0 = (act_info>>16);
	x_dsp_w0 = (dsp_info&0x7ff);
	y_dsp_w0 = (dsp_info>>16);
	x_scale = 4096*100/(factor&0xffff);
	y_scale = 4096*100/(factor>>16);
	x_st_w0 = dsp_st&0xffff;
	y_st_w0 = dsp_st>>16;

	
	xvir_w1 = LcdRdReg(lcdc_dev,WIN1_VIR)&0xffff;
	dsp_info = LcdRdReg(lcdc_dev,WIN1_DSP_INFO);
	dsp_st = LcdRdReg(lcdc_dev,WIN1_DSP_ST);
	x_dsp_w1 = dsp_info&0x7ff;
	y_dsp_w1 = dsp_info>>16;
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
                LcdRdReg(lcdc_dev,WIN0_YRGB_MST),
                LcdRdReg(lcdc_dev,WIN0_CBR_MST),
                status_w1,
                xvir_w1,
                x_dsp_w1,
                y_dsp_w1,
                x_st_w1,
                y_st_w1,
                format_w1,
                LcdRdReg(lcdc_dev,WIN1_YRGB_MST),
                ovl ? "win0 on the top of win1\n":"win1 on the top of win0\n");
        return 0;
}


/*******************************************
lcdc fps manager,set or get lcdc fps
set:0 get
     1 set
********************************************/
static int rk3066b_lcdc_fps_mgr(struct rk_lcdc_device_driver *dev_drv,int fps,bool set)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);
	rk_screen * screen = dev_drv->cur_screen;
	u64 ft = 0;
	u32 dotclk;
	int ret;

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
		}
	    	dev_drv->pixclock = lcdc_dev->pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
			
	}
	
	ft = (u64)(screen->upper_margin + screen->lower_margin + screen->y_res +screen->vsync_len)*
	(screen->left_margin + screen->right_margin + screen->x_res + screen->hsync_len)*
	(dev_drv->pixclock);       // one frame time ,(pico seconds)
	fps = div64_u64(1000000000000llu,ft);
	screen->ft = 1000/fps ;  //one frame time in ms
	return fps;
}

static int rk3066b_fb_layer_remap(struct rk_lcdc_device_driver *dev_drv,
        enum fb_win_map_order order)
{
        mutex_lock(&dev_drv->fb_win_id_mutex);
	if(order == FB_DEFAULT_ORDER)
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

static int rk3066b_fb_get_layer(struct rk_lcdc_device_driver *dev_drv,const char *id)
{
        int layer_id = 0;
        mutex_lock(&dev_drv->fb_win_id_mutex);
        if(!strcmp(id,"fb0") || !strcmp(id,"fb2"))
        {
                layer_id = dev_drv->fb0_win_id;
        }
        else if(!strcmp(id,"fb1") || !strcmp(id,"fb3"))
        {
                layer_id = dev_drv->fb1_win_id;
        }
        else
        {
                printk(KERN_ERR "%s>>un supported %s\n",__func__,id);
                layer_id = -1;
        }
        mutex_unlock(&dev_drv->fb_win_id_mutex);
	
        return  layer_id;
}


static void rk3066b_lcdc_reg_dump(struct rk3066b_lcdc_device *lcdc_dev)
{
	int *cbase =  (int *)lcdc_dev->reg_vir_base;
	int i,j;
	
	for(i=0; i<=(0xa0>>4);i++)
	{
		for(j=0;j<4;j++)
			printk("%08x  ",readl(cbase+i*4 +j));
		printk("\n");
	}
	
}

int rk3066b_lcdc_early_suspend(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);

	if(dev_drv->screen0->standby)
		dev_drv->screen0->standby(1);
	if(dev_drv->screen_ctr_info->io_disable)
		dev_drv->screen_ctr_info->io_disable();
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_dev->clk_on = 0;
		LcdMskReg(lcdc_dev, INT_STATUS, m_FRM_STARTCLEAR, v_FRM_STARTCLEAR(1));
		LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(1));
		LCDC_REG_CFG_DONE();
		spin_unlock(&lcdc_dev->reg_lock);
	}
	else  //clk already disabled
	{
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}
	
		
	mdelay(30);
	clk_disable(lcdc_dev->dclk);
	clk_disable(lcdc_dev->hclk);
	clk_disable(lcdc_dev->aclk);
	clk_disable(lcdc_dev->pd);

	return 0;
}


int rk3066b_lcdc_early_resume(struct rk_lcdc_device_driver *dev_drv)
{  
	struct rk3066b_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk3066b_lcdc_device,driver);

	if(dev_drv->screen_ctr_info->io_enable) 		//power on
		dev_drv->screen_ctr_info->io_enable();
	
	if(!lcdc_dev->clk_on)
	{
		clk_enable(lcdc_dev->pd);
		clk_enable(lcdc_dev->hclk);
		clk_enable(lcdc_dev->dclk);
		clk_enable(lcdc_dev->aclk);
	}
	mdelay(5);
	memcpy((u8*)lcdc_dev->preg, (u8*)&lcdc_dev->regbak, 0x24);  //resume reg ,skip INT_STATUS reg
	memcpy(((u8*)lcdc_dev->preg) + 0x28,((u8*)&lcdc_dev->regbak) + 0x28, 0x74);

	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->atv_layer_cnt)
	{
		LcdMskReg(lcdc_dev, SYS_CFG,m_LCDC_STANDBY,v_LCDC_STANDBY(0));
		LcdMskReg(lcdc_dev, INT_STATUS, m_SCANNING_CLEAR | m_FRM_STARTCLEAR | m_HOR_STARTCLEAR |
					m_SCANNING_MASK | m_HOR_STARTMASK | m_FRM_STARTMASK , 
					v_SCANNING_CLEAR(1) | v_FRM_STARTCLEAR(1) | v_HOR_STARTCLEAR(1) | 
					v_SCANNING_MASK(1) | v_FRM_STARTMASK(0) | v_HOR_STARTMASK(1));
		LCDC_REG_CFG_DONE();
	}
	lcdc_dev->clk_on = 1;
	spin_unlock(&lcdc_dev->reg_lock);

	if(!lcdc_dev->atv_layer_cnt)
		rk3066b_lcdc_clk_disable(lcdc_dev);
	
	if(dev_drv->screen0->standby)
		dev_drv->screen0->standby(0);	      //screen wake up
	
    	return 0;
}
static irqreturn_t rk3066b_lcdc_isr(int irq, void *dev_id)
{
	struct rk3066b_lcdc_device *lcdc_dev = (struct rk3066b_lcdc_device *)dev_id;
	ktime_t timestamp = ktime_get();
	
	LcdMskReg(lcdc_dev, INT_STATUS, m_FRM_STARTCLEAR, v_FRM_STARTCLEAR(1));
	
	//LcdMskReg(lcdc_dev, INT_STATUS, m_LINE_FLAG_INT_CLEAR, v_LINE_FLAG_INT_CLEAR(1));

#if 0
	if(lcdc_dev->driver.num_buf < 3)  //three buffer ,no need to wait for sync
	{
		spin_lock(&(lcdc_dev->driver.cpl_lock));
		complete(&(lcdc_dev->driver.frame_done));
		spin_unlock(&(lcdc_dev->driver.cpl_lock));
	}
#endif

	lcdc_dev->driver.vsync_info.timestamp = timestamp;
	wake_up_interruptible_all(&lcdc_dev->driver.vsync_info.wait);
	
	return IRQ_HANDLED;
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
	.open			= rk3066b_lcdc_open,
	.init_lcdc		= init_rk3066b_lcdc,
	.ioctl			= rk3066b_lcdc_ioctl,
	.suspend		= rk3066b_lcdc_early_suspend,
	.resume			= rk3066b_lcdc_early_resume,
	.set_par       		= rk3066b_lcdc_set_par,
	.blank         		= rk3066b_lcdc_blank,
	.pan_display            = rk3066b_lcdc_pan_display,
	.load_screen		= rk3066b_load_screen,
	.get_layer_state	= rk3066b_lcdc_get_layer_state,
	.ovl_mgr		= rk3066b_lcdc_ovl_mgr,
	.get_disp_info		= rk3066b_lcdc_get_disp_info,
	.fps_mgr		= rk3066b_lcdc_fps_mgr,
	.fb_get_layer           = rk3066b_fb_get_layer,
	.fb_layer_remap         = rk3066b_fb_layer_remap,
};
#ifdef CONFIG_PM
static int rk3066b_lcdc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int rk3066b_lcdc_resume(struct platform_device *pdev)
{
	return 0;
}

#else
#define rk3066b_lcdc_suspend NULL
#define rk3066b_lcdc_resume NULL
#endif

static int __devinit rk3066b_lcdc_probe (struct platform_device *pdev)
{
	struct rk3066b_lcdc_device *lcdc_dev=NULL;
	rk_screen *screen;
	rk_screen *screen1;
	struct rk29fb_info *screen_ctr_info;
	struct resource *res = NULL;
	struct resource *mem;
	int ret = 0;
	
	/*************Malloc rk3066blcdc_inf and set it to pdev for drvdata**********/
	lcdc_dev = kzalloc(sizeof(struct rk3066b_lcdc_device), GFP_KERNEL);
    	if(!lcdc_dev)
    	{
        	dev_err(&pdev->dev, ">>rk3066b lcdc device kmalloc fail!");
        	return -ENOMEM;
    	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->id = pdev->id;
	screen_ctr_info = (struct rk29fb_info * )pdev->dev.platform_data;
	screen =  kzalloc(sizeof(rk_screen), GFP_KERNEL);
	if(!screen)
	{
		dev_err(&pdev->dev, ">>rk3066b lcdc screen kmalloc fail!");
        	ret =  -ENOMEM;
		goto err0;
	}
	else
	{
		lcdc_dev->screen = screen;
	}
	screen->lcdc_id = lcdc_dev->id;
	screen->screen_id = 0;

#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)&& (defined(CONFIG_RK610_LVDS) || defined(CONFIG_RK616_LVDS))
	screen1 =  kzalloc(sizeof(rk_screen), GFP_KERNEL);
	if(!screen1)
	{
		dev_err(&pdev->dev, ">>rk3066b lcdc screen1 kmalloc fail!");
        	ret =  -ENOMEM;
		goto err0;
	}
	screen1->lcdc_id = 1;
	screen1->screen_id = 1;
	printk("use lcdc%d and rk610 implemention dual display!\n",lcdc_dev->id);
	
#endif
	/****************get lcdc0 reg  *************************/
	res = platform_get_resource(pdev, IORESOURCE_MEM,0);
	if (res == NULL)
    	{
        	dev_err(&pdev->dev, "failed to get io resource for lcdc%d \n",lcdc_dev->id);
        	ret = -ENOENT;
		goto err1;
    	}
    	lcdc_dev->reg_phy_base = res->start;
	lcdc_dev->len = resource_size(res);
    	mem = request_mem_region(lcdc_dev->reg_phy_base, resource_size(res), pdev->name);
    	if (mem == NULL)
    	{
        	dev_err(&pdev->dev, "failed to request mem region for lcdc%d\n",lcdc_dev->id);
        	ret = -ENOENT;
		goto err1;
    	}
	lcdc_dev->reg_vir_base = ioremap(lcdc_dev->reg_phy_base,  resource_size(res));
	if (lcdc_dev->reg_vir_base == NULL)
	{
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err2;
	}
	
    	lcdc_dev->preg = (LCDC_REG*)lcdc_dev->reg_vir_base;
	printk("lcdc%d:reg_phy_base = 0x%08x,reg_vir_base:0x%p\n",pdev->id,lcdc_dev->reg_phy_base, lcdc_dev->preg);
	lcdc_dev->driver.dev=&pdev->dev;
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
		dev_err(&pdev->dev, "cannot find IRQ\n");
		goto err3;
	}
	ret = request_irq(lcdc_dev->irq, rk3066b_lcdc_isr, IRQF_DISABLED,dev_name(&pdev->dev),lcdc_dev);
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
		ret =  -ENODEV;
		goto err4;
	}
		
	ret = rk_fb_register(&(lcdc_dev->driver),&lcdc_driver,lcdc_dev->id);
	if(ret < 0)
	{
		printk(KERN_ERR "register fb for lcdc%d failed!\n",lcdc_dev->id);
		goto err4;
	}
	printk("rk3066b lcdc%d probe ok!\n",lcdc_dev->id);

	return 0;

err4:
	free_irq(lcdc_dev->irq,lcdc_dev);
err3:	
	iounmap(lcdc_dev->reg_vir_base);
err2:
	release_mem_region(lcdc_dev->reg_phy_base,resource_size(res));
err1:
	kfree(screen);
err0:
	platform_set_drvdata(pdev, NULL);
	kfree(lcdc_dev);
	return ret;
    
}
static int __devexit rk3066b_lcdc_remove(struct platform_device *pdev)
{
	struct rk3066b_lcdc_device *lcdc_dev = platform_get_drvdata(pdev);
	rk_fb_unregister(&(lcdc_dev->driver));
	rk3066b_lcdc_deinit(lcdc_dev);
	iounmap(lcdc_dev->reg_vir_base);
	release_mem_region(lcdc_dev->reg_phy_base,lcdc_dev->len);
	kfree(lcdc_dev->screen);
	kfree(lcdc_dev);
	return 0;
}

static void rk3066b_lcdc_shutdown(struct platform_device *pdev)
{
	struct rk3066b_lcdc_device *lcdc_dev = platform_get_drvdata(pdev);
	if(lcdc_dev->driver.cur_screen->standby) //standby the screen if necessary
		lcdc_dev->driver.cur_screen->standby(1);
	if(lcdc_dev->driver.screen_ctr_info->io_disable) //power off the screen if necessary
		lcdc_dev->driver.screen_ctr_info->io_disable();
	if(lcdc_dev->driver.cur_screen->sscreen_set) //turn off  lvds if necessary
		lcdc_dev->driver.cur_screen->sscreen_set(lcdc_dev->driver.cur_screen , 0);

	rk3066b_lcdc_deinit(lcdc_dev);
	rk_fb_unregister(&(lcdc_dev->driver));
	
	/*iounmap(lcdc_dev->reg_vir_base);
	release_mem_region(lcdc_dev->reg_phy_base,lcdc_dev->len);
	kfree(lcdc_dev->screen);
	kfree(lcdc_dev);*/
}


static struct platform_driver rk3066b_lcdc_driver = {
	.probe		= rk3066b_lcdc_probe,
	.remove		= __devexit_p(rk3066b_lcdc_remove),
	.driver		= {
		.name	= "rk30-lcdc",
		.owner	= THIS_MODULE,
	},
	.suspend	= rk3066b_lcdc_suspend,
	.resume		= rk3066b_lcdc_resume,
	.shutdown   	= rk3066b_lcdc_shutdown,
};

static int __init rk3066b_lcdc_init(void)
{
	return platform_driver_register(&rk3066b_lcdc_driver);
}

static void __exit rk3066b_lcdc_exit(void)
{
	platform_driver_unregister(&rk3066b_lcdc_driver);
}



fs_initcall(rk3066b_lcdc_init);
module_exit(rk3066b_lcdc_exit);



