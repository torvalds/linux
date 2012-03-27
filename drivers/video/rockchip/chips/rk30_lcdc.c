/*
 * drivers/video/rockchip/chips/rk30_lcdc.c
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

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/earlysuspend.h>
#include <linux/fb.h>
#include <asm/div64.h>
#include <asm/uaccess.h>

#include <mach/board.h>
#include "../../display/screen/screen.h"
#include "../rk_fb.h"
#include "rk30_lcdc.h"




static int dbg_thresd = 0;
module_param(dbg_thresd, int, S_IRUGO|S_IWUSR);
#define DBG(x...) do { if(unlikely(dbg_thresd)) printk(KERN_INFO x); } while (0)


static int init_rk30_lcdc(struct rk30_lcdc_device *lcdc_dev)
{

	if(lcdc_dev->id == 0) //lcdc0
	{
		lcdc_dev->hclk = clk_get(NULL,"hclk_lcdc0"); 
		lcdc_dev->aclk = clk_get(NULL,"aclk_lcdc0");
		lcdc_dev->dclk = clk_get(NULL,"dclk_lcdc0");
	}
	else if(lcdc_dev->id == 1)
	{
		lcdc_dev->hclk = clk_get(NULL,"hclk_lcdc1"); 
		lcdc_dev->aclk = clk_get(NULL,"aclk_lcdc1");
		lcdc_dev->dclk = clk_get(NULL,"dclk_lcdc1");
	}
	else
	{
		printk(KERN_ERR "invalid lcdc device!\n");
		return -EINVAL;
	}
	if ((IS_ERR(lcdc_dev->aclk)) ||(IS_ERR(lcdc_dev->dclk)) || (IS_ERR(lcdc_dev->hclk)))
    	{
       		printk(KERN_ERR "failed to get lcdc clk source\n");
   	 }
	clk_enable(lcdc_dev->hclk);  //enable aclk for register config
	LcdSetBit(lcdc_dev,DSP_CTRL0, m_LCDC_AXICLK_AUTO_ENABLE);//eanble axi-clk auto gating for low power
	LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);  // write any value to  REG_CFG_DONE let config become effective
	return 0;
}

int rk30_lcdc_deinit(void)
{
    return 0;
}

int rk30_load_screen(struct rk30_lcdc_device*lcdc_dev, bool initscreen)
{
	int ret = -EINVAL;
	rk_screen *screen = lcdc_dev->screen;
	u16 face;
	u16 mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend;
	u16 right_margin = screen->right_margin;
	u16 lower_margin = screen->lower_margin;
	u16 x_res = screen->x_res, y_res = screen->y_res;

	// set the rgb or mcu

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

    		DBG(">> mcu_total=%d, mcu_rwstart=%d, mcu_csstart=%d, mcu_rwend=%d, mcu_csend=%d \n",
        		mcu_total, mcu_rwstart, mcu_csstart, mcu_rwend, mcu_csend);

		// set horizontal & vertical out timing
	
	    	right_margin = x_res/6; 
		screen->pixclock = 150000000; //mcu fix to 150 MHz
		LcdMskReg(lcdc_dev, MCU_CTRL,m_MCU_CS_ST | m_MCU_CS_END| m_MCU_RW_ST | m_MCU_RW_END |
             		m_MCU_WRITE_PERIOD | m_MCU_HOLDMODE_SELECT | m_MCU_HOLDMODE_FRAME_ST,
            		v_MCU_CS_ST(mcu_csstart) | v_MCU_CS_END(mcu_csend) | v_MCU_RW_ST(mcu_rwstart) |
            		v_MCU_RW_END(mcu_rwend) |  v_MCU_WRITE_PERIOD(mcu_total) |
            		v_MCU_HOLDMODE_SELECT((SCREEN_MCU==screen->type)?(1):(0)) | v_MCU_HOLDMODE_FRAME_ST(0));
	
	}

	

	// set synchronous pin polarity and data pin swap rule
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

	//set background color to black,set swap according to the screen panel
	LcdMskReg(lcdc_dev, DSP_CTRL1, m_BG_COLOR | m_OUTPUT_RB_SWAP | m_OUTPUT_RG_SWAP | m_DELTA_SWAP | 
	 	m_DUMMY_SWAP,  v_BG_COLOR(0x000000) | v_OUTPUT_RB_SWAP(screen->swap_rb) | 
	 	v_OUTPUT_RG_SWAP(screen->swap_rg) | v_DELTA_SWAP(screen->swap_delta) | v_DUMMY_SWAP(screen->swap_dumy) );

	
	LcdWrReg(lcdc_dev, DSP_HTOTAL_HS_END,v_HSYNC(screen->hsync_len) |
             v_HORPRD(screen->hsync_len + screen->left_margin + x_res + right_margin));
	LcdWrReg(lcdc_dev, DSP_HACT_ST_END, v_HAEP(screen->hsync_len + screen->left_margin + x_res) |
             v_HASP(screen->hsync_len + screen->left_margin));

	LcdWrReg(lcdc_dev, DSP_VTOTAL_VS_END, v_VSYNC(screen->vsync_len) |
              v_VERPRD(screen->vsync_len + screen->upper_margin + y_res + lower_margin));
	LcdWrReg(lcdc_dev, DSP_VACT_ST_END,  v_VAEP(screen->vsync_len + screen->upper_margin+y_res)|
              v_VASP(screen->vsync_len + screen->upper_margin));
	// let above to take effect
	LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);
                                                        
 

	ret = clk_set_rate(lcdc_dev->dclk, screen->pixclock);
	if(ret)
	{
        	printk(KERN_ERR ">>>>>> set lcdc dclk failed\n");
	}
    	else
    	{
    		lcdc_dev->driver.pixclock = lcdc_dev->pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	 	clk_enable(lcdc_dev->dclk);
    		printk("%s: dclk:%lu ",lcdc_dev->driver.name,clk_get_rate(lcdc_dev->dclk));
    	}
    	if(initscreen)
    	{
        	if(screen->lcdc_aclk)
		{
			ret = clk_set_rate(lcdc_dev->aclk, screen->lcdc_aclk);
			if(ret)
			{
           	 		printk(KERN_ERR ">>>>>> set lcdc aclk  rate failed\n");
        		}
			else
			{
        			clk_enable(lcdc_dev->aclk);
				printk("aclk:%lu\n",clk_get_rate(lcdc_dev->aclk));
			}
        	}
        	
	}
  
   
    	if(screen->init)
    	{
    		screen->init();
    	}
	printk("%s for lcdc%d ok!\n",__func__,lcdc_dev->id);
	return 0;
}

static int mcu_refresh(struct rk30_lcdc_device *lcdc_dev)
{
   
    return 0;
}

static int win0_blank(int blank_mode,struct rk30_lcdc_device *lcdc_dev)
{
	switch(blank_mode)
	{
		case FB_BLANK_UNBLANK:
        	LcdMskReg(lcdc_dev, SYS_CTRL1, m_W0_EN, v_W0_EN(1));
        	break;
    	case FB_BLANK_NORMAL:
         	LcdMskReg(lcdc_dev, SYS_CTRL1, m_W0_EN, v_W0_EN(0));
	     	break;
    	default:
        	LcdMskReg(lcdc_dev, SYS_CTRL1, m_W0_EN, v_W1_EN(1));
        	break;
	}
  	mcu_refresh(lcdc_dev);
    return 0;
}

static int win1_blank(int blank_mode, struct rk30_lcdc_device *lcdc_dev)
{
	printk(KERN_INFO "%s>>>>>>>>>>mode:%d\n",__func__,blank_mode);
	switch(blank_mode)
    {
    case FB_BLANK_UNBLANK:
        LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN, v_W1_EN(1));
        break;
    case FB_BLANK_NORMAL:
         LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN, v_W1_EN(0));
	     break;
    default:
        LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN, v_W1_EN(0));
        break;
    }
    
	//mcu_refresh(inf);
    return 0;
}
static int rk30_lcdc_blank(struct rk_lcdc_device_driver*lcdc_drv,int layer_id,int blank_mode)
{
	struct rk30_lcdc_device * lcdc_dev = container_of(lcdc_drv,struct rk30_lcdc_device ,driver);
	if(layer_id==0)
	{
	 	win0_blank(blank_mode,lcdc_dev);
	}
	else if(layer_id==1)
	{
		win1_blank(blank_mode,lcdc_dev);
	}

	LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);
    return 0;
}

static  int win0_display(struct rk30_lcdc_device *lcdc_dev,struct layer_par *par )
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = par->smem_start + par->y_offset;
    	uv_addr = par->cbr_start + par->c_offset;
	DBG(KERN_INFO "%s:y_addr:0x%x>>uv_addr:0x%x\n",__func__,y_addr,uv_addr);
	LcdWrReg(lcdc_dev, WIN0_YRGB_MST0, y_addr);
    	LcdWrReg(lcdc_dev,WIN0_CBR_MST0,uv_addr);
	LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);  // write any value to  REG_CFG_DONE let config become effective
	return 0;
	
}

static  int win1_display(struct rk30_lcdc_device *lcdc_dev,struct layer_par *par )
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = par->smem_start + par->y_offset;
    	uv_addr = par->cbr_start + par->c_offset;
	DBG(KERN_INFO "%s>>y_addr:0x%x>>uv_addr:0x%x\n",__func__,y_addr,uv_addr);
	LcdWrReg(lcdc_dev, WIN1_YRGB_MST, y_addr);
    	LcdWrReg(lcdc_dev,WIN1_CBR_MST,uv_addr);
	LcdWrReg(lcdc_dev, REG_CFG_DONE, 0x01);

	return 0;
}

static  int win0_set_par(struct rk30_lcdc_device *lcdc_dev,rk_screen *screen,
	struct layer_par *par )
{
    u32 xact, yact, xvir, yvir, xpos, ypos;
    u32 ScaleYrgbX,ScaleYrgbY, ScaleCbrX, ScaleCbrY;
    
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
            ScaleCbrX=  CalScale((xact/2), par->xsize);
            ScaleCbrY = CalScale(yact, par->ysize);
       	    break;
       case YUV420: // yuv420
           ScaleCbrX= CalScale(xact/2, par->xsize);
           ScaleCbrY =  CalScale(yact/2, par->ysize);
           break;
       case YUV444:// yuv444
           ScaleCbrX= CalScale(xact, par->xsize);
           ScaleCbrY = CalScale(yact, par->ysize);
           break;
       default:
           break;
    }

	DBG("%s>>format:%d>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>ypos:%d>>\n",
		__func__,par->format,xact,yact,par->xsize,par->ysize,xvir,yvir,ypos);
    LcdMskReg(lcdc_dev,SYS_CTRL1,  m_W0_FORMAT , v_W0_FORMAT(par->format));		//(inf->video_mode==0)
    LcdWrReg(lcdc_dev, WIN0_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
    LcdWrReg(lcdc_dev, WIN0_DSP_ST, v_DSP_STX(xpos) | v_DSP_STY(ypos));
    LcdWrReg(lcdc_dev, WIN0_DSP_INFO, v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
    LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_YRGB, v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
    LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_CBR,  v_X_SCL_FACTOR(ScaleCbrX) | v_Y_SCL_FACTOR(ScaleCbrY));
    switch(par->format)
    {
        case ARGB888:
            LcdWrReg(lcdc_dev, WIN0_VIR,v_ARGB888_VIRWIDTH(xvir));
            LcdMskReg(lcdc_dev,SYS_CTRL1,m_W0_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
            break;
        case RGB888:  //rgb888
            LcdWrReg(lcdc_dev, WIN0_VIR,v_RGB888_VIRWIDTH(xvir));
            LcdMskReg(lcdc_dev,SYS_CTRL1,m_W0_RGB_RB_SWAP,v_W0_RGB_RB_SWAP(1));
            break;
        case RGB565:  //rgb565
            LcdWrReg(lcdc_dev, WIN0_VIR,v_RGB565_VIRWIDTH(xvir));
            break;
        case YUV422:
        case YUV420:   
            LcdWrReg(lcdc_dev, WIN0_VIR,v_YUV_VIRWIDTH(xvir));
            break;
        default:
            LcdWrReg(lcdc_dev, WIN0_VIR,v_RGB888_VIRWIDTH(xvir));
            break;
    }
	
    return 0;

}

static int win1_set_par(struct rk30_lcdc_device *lcdc_dev,rk_screen *screen,
	struct layer_par *par )

{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u32 ScaleYrgbX,ScaleYrgbY, ScaleCbrX, ScaleCbrY;
	
	xact = par->xact;			
	yact = par->yact;
	xvir = par->xvir;		
	yvir = par->yvir;
	xpos = par->xpos+screen->left_margin + screen->hsync_len;
	ypos = par->ypos+screen->upper_margin + screen->vsync_len;
	
	ScaleYrgbX = CalScale(xact, par->xsize);
	ScaleYrgbY = CalScale(yact, par->ysize);
	DBG("%s>>format:%d>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>ypos:%d>>\n",
		__func__,par->format,xact,yact,par->xsize,par->ysize,xvir,yvir,ypos);
	switch (par->format)
 	{
		case YUV422:// yuv422
			ScaleCbrX =	CalScale((xact/2), par->xsize);
			ScaleCbrY = CalScale(yact, par->ysize);
			break;
		case YUV420: // yuv420
			ScaleCbrX = CalScale(xact/2, par->xsize);
			ScaleCbrY =	CalScale(yact/2, par->ysize);
			break;
		case YUV444:// yuv444
			ScaleCbrX = CalScale(xact, par->xsize);
			ScaleCbrY = CalScale(yact, par->ysize);
			break;
		default:
			break;
	}

    LcdWrReg(lcdc_dev, WIN1_SCL_FACTOR_YRGB, v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
    LcdWrReg(lcdc_dev, WIN1_SCL_FACTOR_CBR,  v_X_SCL_FACTOR(ScaleCbrX) | v_Y_SCL_FACTOR(ScaleCbrY));
    LcdMskReg(lcdc_dev, SYS_CTRL1, m_W1_EN|m_W1_FORMAT, v_W1_EN(1)|v_W1_FORMAT(par->format));
    LcdWrReg(lcdc_dev, WIN1_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
    LcdWrReg(lcdc_dev, WIN1_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
    LcdWrReg(lcdc_dev, WIN1_DSP_INFO,v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
     // enable win1 color key and set the color to black(rgb=0)
    LcdMskReg(lcdc_dev, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR, v_COLORKEY_EN(1) | v_KEYCOLOR(0));
	switch(par->format)
    {
        case ARGB888:
            LcdWrReg(lcdc_dev, WIN1_VIR,v_ARGB888_VIRWIDTH(xvir));
            //LcdMskReg(lcdc_dev,SYS_CTRL1,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
            break;
        case RGB888:  //rgb888
            LcdWrReg(lcdc_dev, WIN1_VIR,v_RGB888_VIRWIDTH(xvir));
           // LcdMskReg(lcdc_dev,SYS_CTRL1,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
            break;
        case RGB565:  //rgb565
            LcdWrReg(lcdc_dev, WIN1_VIR,v_RGB565_VIRWIDTH(xvir));
            break;
        case YUV422:
        case YUV420:   
            LcdWrReg(lcdc_dev, WIN1_VIR,v_YUV_VIRWIDTH(xvir));
            break;
        default:
            LcdWrReg(lcdc_dev, WIN1_VIR,v_RGB888_VIRWIDTH(xvir));
            break;
    }
	
    return 0;
}

static int rk30_lcdc_set_par(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	struct rk30_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk30_lcdc_device,driver);
	struct layer_par *par = NULL;
	rk_screen *screen = lcdc_dev->screen;
	if(!screen)
	{
		printk(KERN_ERR "screen is null!\n");
		return -ENOENT;
	}
	if(layer_id==0)
	{
		par = &(dev_drv->layer_par[0]);
        	win0_set_par(lcdc_dev,screen,par);
	}
	else if(layer_id==1)
	{
		par = &(dev_drv->layer_par[1]);
        	win1_set_par(lcdc_dev,screen,par);
	}
	
	return 0;
}

int rk30_lcdc_pan_display(struct rk_lcdc_device_driver * dev_drv,int layer_id)
{
	struct rk30_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk30_lcdc_device,driver);
	struct layer_par *par = NULL;
	rk_screen *screen = lcdc_dev->screen;
	if(!screen)
	{
		printk(KERN_ERR "screen is null!\n");
		return -ENOENT;	
	}
	
	if(layer_id==0)
	{
		par = &(dev_drv->layer_par[0]);
        	win0_display(lcdc_dev,par);
	}
	else if(layer_id==1)
	{
		par = &(dev_drv->layer_par[1]);
        	win1_display(lcdc_dev,par);
	}
	
    return 0;
}

int rk30_lcdc_ioctl(struct rk_lcdc_device_driver * dev_drv,unsigned int cmd, unsigned long arg,int layer_id)
{
	struct rk30_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk30_lcdc_device,driver);
	u32 panel_size[2];
	void __user *argp = (void __user *)arg;
	switch(cmd)
	{
		case FB1_IOCTL_GET_PANEL_SIZE:    //get panel size
                	panel_size[0] = lcdc_dev->screen->x_res;
                	panel_size[1] = lcdc_dev->screen->y_res;
            		if(copy_to_user(argp, panel_size, 8)) 
				return -EFAULT;
			break;
		default:
			break;
	}
		
   return 0;
}

int rk30_lcdc_suspend(struct layer_par *layer_par)
{
    return 0;
}


int rk30_lcdc_resume(struct layer_par *layer_par)
{  
    
    return 0;
}

static struct layer_par lcdc_layer[] = {
	[0] = {
       	.name  		= "win0",
		.id			= 0,
		.support_3d	= true,
	},
	[1] = {
        .name  		= "win1",
		.id			= 1,
		.support_3d	= false,
	},
};

static struct rk_lcdc_device_driver lcdc_driver = {
	.name			= "lcdc",
	.layer_par		= lcdc_layer,
	.num_layer		= ARRAY_SIZE(lcdc_layer),
	.ioctl			= rk30_lcdc_ioctl,
	.suspend		= rk30_lcdc_suspend,
	.resume			= rk30_lcdc_resume,
	.set_par       		= rk30_lcdc_set_par,
	.blank         		= rk30_lcdc_blank,
	.pan_display            = rk30_lcdc_pan_display,
};

static int __devinit rk30_lcdc_probe (struct platform_device *pdev)
{
	struct rk30_lcdc_device *lcdc_dev=NULL;
	rk_screen *screen;
	struct resource *res = NULL;
	struct resource *mem;
	
	int ret = 0;
	
	/*************Malloc rk30lcdc_inf and set it to pdev for drvdata**********/
	lcdc_dev = kzalloc(sizeof(struct rk30_lcdc_device), GFP_KERNEL);
    	if(!lcdc_dev)
    	{
        	dev_err(&pdev->dev, ">>rk30 lcdc device kmalloc fail!");
        	return -ENOMEM;
    	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->id = pdev->id;
	screen =  kzalloc(sizeof(rk_screen), GFP_KERNEL);
	if(!screen)
	{
		dev_err(&pdev->dev, ">>rk30 lcdc screen kmalloc fail!");
        	ret =  -ENOMEM;
		goto err0;
	}
	else
	{
		lcdc_dev->screen = screen;
	}
	/****************get lcdc0 reg  *************************/
	res = platform_get_resource(pdev, IORESOURCE_MEM,0);
	if (res == NULL)
    	{
        	dev_err(&pdev->dev, "failed to get io resource for lcdc%d \n",lcdc_dev->id);
        	ret = -ENOENT;
		goto err1;
    	}
    	lcdc_dev->reg_phy_base = res->start;
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
	init_lcdc_device_driver(&lcdc_driver,&(lcdc_dev->driver),lcdc_dev->id);
	lcdc_dev->driver.dev=&pdev->dev;
	
	/*****************	set lcdc screen	********/
	set_lcd_info(screen, NULL);
	lcdc_dev->driver.screen = screen;
	/*****************	INIT LCDC		********/
	ret = init_rk30_lcdc(lcdc_dev);
	if(ret < 0)
	{
		printk(KERN_ERR "init rk30 lcdc failed!\n");
		goto err3;
	}
	ret = rk30_load_screen(lcdc_dev,1);
	if(ret < 0)
	{
		printk(KERN_ERR "rk30 load screen for lcdc0 failed!\n");
		goto err3;
	}
	/*****************	lcdc register		********/
	ret = rk_fb_register(&(lcdc_dev->driver));
	if(ret < 0)
	{
		printk(KERN_ERR "registe fb for lcdc0 failed!\n");
		goto err3;
	}
	printk("rk30 lcdc%d probe ok!\n",lcdc_dev->id);

	return 0;

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
static int __devexit rk30_lcdc_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk30_lcdc_shutdown(struct platform_device *pdev)
{

}


static struct platform_driver rk30lcdc_driver = {
	.probe		= rk30_lcdc_probe,
	.remove		= __devexit_p(rk30_lcdc_remove),
	.driver		= {
		.name	= "rk30-lcdc",
		.owner	= THIS_MODULE,
	},
	.shutdown   = rk30_lcdc_shutdown,
};

static int __init rk30_lcdc_init(void)
{
    return platform_driver_register(&rk30lcdc_driver);
}

static void __exit rk30_lcdc_exit(void)
{
    platform_driver_unregister(&rk30lcdc_driver);
}



fs_initcall(rk30_lcdc_init);
module_exit(rk30_lcdc_exit);



