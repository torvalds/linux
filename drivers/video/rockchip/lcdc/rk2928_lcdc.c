/*
 * drivers/video/rockchip/chips/rk2928_lcdc.c
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
#include <mach/gpio.h>
#include "rk2928_lcdc.h"
#include "../lvds/rk_lvds.h"





static int dbg_thresd = 0;
module_param(dbg_thresd, int, S_IRUGO|S_IWUSR);
#define DBG(level,x...) do { if(unlikely(dbg_thresd >= level)) printk(KERN_INFO x); } while (0)



//rk2928 lcdc iomux,mode:0,gpio,1 lcdc io
static int rk2928_lcdc_iomux(rk_screen *screen,int mode)
{
	int ret = 0;
	int i=0;
	if((screen->type == SCREEN_RGB)&&(mode)) //iomux for RGB screen
	{
		for(i=0;i<8;i++)
		{
			gpio_free(RK2928_PIN2_PB0 + i); 
		}
		for(i=0;i<4;i++)
		{
			gpio_free(RK2928_PIN2_PC0 + i);
		}

		if(screen->lcdc_id == 0)
		{
			rk30_mux_api_set(GPIO2B0_LCDC0_DCLK_LCDC1_DCLK_NAME, GPIO2B_LCDC0_DCLK);
			rk30_mux_api_set(GPIO2B1_LCDC0_HSYNC_LCDC1_HSYNC_NAME, GPIO2B_LCDC0_HSYNC);
			rk30_mux_api_set(GPIO2B2_LCDC0_VSYNC_LCDC1_VSYNC_NAME, GPIO2B_LCDC0_VSYNC);
			rk30_mux_api_set(GPIO2B3_LCDC0_DEN_LCDC1_DEN_NAME, GPIO2B_LCDC0_DEN);
			rk30_mux_api_set(GPIO2B4_LCDC0_D10_LCDC1_D10_NAME, GPIO2B_LCDC0_D10);
			rk30_mux_api_set(GPIO2B5_LCDC0_D11_LCDC1_D11_NAME, GPIO2B_LCDC0_D11);
			rk30_mux_api_set(GPIO2B6_LCDC0_D12_LCDC1_D12_NAME, GPIO2B_LCDC0_D12);
			rk30_mux_api_set(GPIO2B7_LCDC0_D13_LCDC1_D13_NAME, GPIO2B_LCDC0_D13);
			rk30_mux_api_set(GPIO2C0_LCDC0_D14_LCDC1_D14_NAME, GPIO2C_LCDC0_D14);
			rk30_mux_api_set(GPIO2C1_LCDC0_D15_LCDC1_D15_NAME, GPIO2C_LCDC0_D15);
			rk30_mux_api_set(GPIO2C2_LCDC0_D16_LCDC1_D16_NAME, GPIO2C_LCDC0_D16);
			rk30_mux_api_set(GPIO2C3_LCDC0_D17_LCDC1_D17_NAME, GPIO2C_LCDC0_D17);
		}
		else if(screen->lcdc_id == 1)
		{
			rk30_mux_api_set(GPIO2B0_LCDC0_DCLK_LCDC1_DCLK_NAME, GPIO2B_LCDC1_DCLK);
			rk30_mux_api_set(GPIO2B1_LCDC0_HSYNC_LCDC1_HSYNC_NAME, GPIO2B_LCDC1_HSYNC);
			rk30_mux_api_set(GPIO2B2_LCDC0_VSYNC_LCDC1_VSYNC_NAME, GPIO2B_LCDC1_VSYNC);
			rk30_mux_api_set(GPIO2B3_LCDC0_DEN_LCDC1_DEN_NAME, GPIO2B_LCDC1_DEN);
			rk30_mux_api_set(GPIO2B4_LCDC0_D10_LCDC1_D10_NAME, GPIO2B_LCDC1_D10);
			rk30_mux_api_set(GPIO2B5_LCDC0_D11_LCDC1_D11_NAME, GPIO2B_LCDC1_D11);
			rk30_mux_api_set(GPIO2B6_LCDC0_D12_LCDC1_D12_NAME, GPIO2B_LCDC1_D12);
			rk30_mux_api_set(GPIO2B7_LCDC0_D13_LCDC1_D13_NAME, GPIO2B_LCDC1_D13);
			rk30_mux_api_set(GPIO2C0_LCDC0_D14_LCDC1_D14_NAME, GPIO2C_LCDC1_D14);
			rk30_mux_api_set(GPIO2C1_LCDC0_D15_LCDC1_D15_NAME, GPIO2C_LCDC1_D15);
			rk30_mux_api_set(GPIO2C2_LCDC0_D16_LCDC1_D16_NAME, GPIO2C_LCDC1_D16);
			rk30_mux_api_set(GPIO2C3_LCDC0_D17_LCDC1_D17_NAME, GPIO2C_LCDC1_D17);
		}
		else
		{
			printk(KERN_WARNING "%s>>>no such interface:%d\n",__func__,screen->lcdc_id);
			return -1;
		}	
	}
	else if((screen->type == SCREEN_RGB)&&(!mode))
	{
		rk30_mux_api_set(GPIO2B0_LCDC0_DCLK_LCDC1_DCLK_NAME, GPIO2B_GPIO2B0 );
		rk30_mux_api_set(GPIO2B1_LCDC0_HSYNC_LCDC1_HSYNC_NAME,GPIO2B_GPIO2B1);
		rk30_mux_api_set(GPIO2B2_LCDC0_VSYNC_LCDC1_VSYNC_NAME, 	GPIO2B_GPIO2B2);
		rk30_mux_api_set(GPIO2B3_LCDC0_DEN_LCDC1_DEN_NAME, GPIO2B_GPIO2B3);
		rk30_mux_api_set(GPIO2B4_LCDC0_D10_LCDC1_D10_NAME, GPIO2B_GPIO2B4 );
		rk30_mux_api_set(GPIO2B5_LCDC0_D11_LCDC1_D11_NAME, GPIO2B_GPIO2B5 );
		rk30_mux_api_set(GPIO2B6_LCDC0_D12_LCDC1_D12_NAME, GPIO2B_GPIO2B6 );
		rk30_mux_api_set(GPIO2B7_LCDC0_D13_LCDC1_D13_NAME, GPIO2B_GPIO2B7 );
		rk30_mux_api_set(GPIO2C0_LCDC0_D14_LCDC1_D14_NAME, GPIO2C_GPIO2C0);
		rk30_mux_api_set(GPIO2C1_LCDC0_D15_LCDC1_D15_NAME, GPIO2C_GPIO2C1);
		rk30_mux_api_set(GPIO2C2_LCDC0_D16_LCDC1_D16_NAME, GPIO2C_GPIO2C2);
		rk30_mux_api_set(GPIO2C3_LCDC0_D17_LCDC1_D17_NAME, GPIO2C_GPIO2C3);
		for(i=0;i<8;i++)
		{
			ret += gpio_request(RK2928_PIN2_PB0 + i, NULL);
			gpio_direction_output(RK2928_PIN2_PB0 + i, 0); 
		}
		for(i=0;i<4;i++)
		{
			ret += gpio_request(RK2928_PIN2_PC0 + i, NULL);
			gpio_direction_output(RK2928_PIN2_PC0 + i, 0); 
		}
		if(ret < 0)
		{
			printk("%s:some gpio request fail\n",__func__);
		}
	}
	return 0;
}
static int init_rk2928_lcdc(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
	if(lcdc_dev->id == 0) //lcdc0
	{
		lcdc_dev->hclk = clk_get(NULL,"hclk_lcdc0"); 
		lcdc_dev->aclk = clk_get(NULL,"aclk_lcdc0");
		lcdc_dev->dclk = clk_get(NULL,"dclk_lcdc0");
		lcdc_dev->sclk = clk_get(NULL,"sclk_lcdc0");
	}
	else
	{
		printk(KERN_ERR "invalid lcdc device!\n");
		return -EINVAL;
	}
	if (IS_ERR(lcdc_dev->sclk) || (IS_ERR(lcdc_dev->aclk)) ||(IS_ERR(lcdc_dev->dclk)) || (IS_ERR(lcdc_dev->hclk)))
    	{
       		printk(KERN_ERR "failed to get lcdc%d clk source\n",lcdc_dev->id);
   	}

	clk_enable(lcdc_dev->pd);
	clk_enable(lcdc_dev->hclk);  //enable aclk and hclk for register config
	clk_enable(lcdc_dev->aclk);  
	lcdc_dev->clk_on = 1;
	LcdSetBit(lcdc_dev,SYS_CFG, m_LCDC_AXICLK_AUTO_ENABLE);//eanble axi-clk auto gating for low power
	LcdMskReg(lcdc_dev,INT_STATUS,m_FRM_START_INT_CLEAR | m_BUS_ERR_INT_CLEAR | m_LINE_FLAG_INT_EN |
              m_FRM_START_INT_EN | m_HOR_START_INT_EN,v_FRM_START_INT_CLEAR(1) | v_BUS_ERR_INT_CLEAR(0) |
              v_LINE_FLAG_INT_EN(0) | v_FRM_START_INT_EN(0) | v_HOR_START_INT_EN(0));  //enable frame start interrupt for sync
	//LCDC_REG_CFG_DONE();  // write any value to  REG_CFG_DONE let config become effective
	return 0;
}

static int rk2928_lcdc_deinit(struct rk2928_lcdc_device *lcdc_dev)
{
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_dev->clk_on = 0;
		LcdMskReg(lcdc_dev, INT_STATUS, m_FRM_START_INT_CLEAR, v_FRM_START_INT_CLEAR(1));
		LcdMskReg(lcdc_dev, INT_STATUS, m_HOR_START_INT_EN | m_FRM_START_INT_EN | 
			m_LINE_FLAG_INT_EN | m_BUS_ERR_INT_EN,v_HOR_START_INT_EN(0) | v_FRM_START_INT_EN(0) | 
			v_LINE_FLAG_INT_EN(0) | v_BUS_ERR_INT_EN(0));  //disable all lcdc interrupt
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

static int rk2928_load_screen(struct rk_lcdc_device_driver *dev_drv, bool initscreen)
{
	int ret = -EINVAL;
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
	rk_screen *screen = dev_drv->cur_screen;
	rk_screen *screen0 = dev_drv->screen0;
	u64 ft;
	int fps;
	u16 face;
	u16 right_margin = screen->right_margin;
	u16 lower_margin = screen->lower_margin;
	u16 x_res = screen->x_res, y_res = screen->y_res;
	DBG(1,"left_margin:%d>>hsync_len:%d>>xres:%d>>right_margin:%d>>upper_margin:%d>>vsync_len:%d>>yres:%d>>lower_margin:%d\n",
		screen->left_margin,screen->hsync_len,screen->x_res,screen->right_margin,screen->upper_margin,screen->vsync_len,screen->y_res,
		screen->lower_margin);
	// set the rgb or mcu
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(screen->type==SCREEN_MCU)
		{
	    		printk(KERN_ERR "MCU Screen is not supported by RK2928\n");
	
		}

		switch (screen->face)
		{
	        	case OUT_P565:
	            		face = OUT_P565;
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
	            		break;
	        	case OUT_P666:
	            		face = OUT_P666;
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
	            		break;
	        	case OUT_D888_P565:
	            		face = OUT_P888;
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0));
	            		break;
	        	case OUT_D888_P666:
	            		face = OUT_P888;
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1));
	            		break;
	        	case OUT_P888:
	            		face = OUT_P888;
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_UP_EN, v_DITHER_UP_EN(0));
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
	            		break;
	        	default:
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_UP_EN, v_DITHER_UP_EN(0));
	            		LcdMskReg(lcdc_dev, DSP_CTRL, m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE, v_DITHER_DOWN_EN(0) | v_DITHER_DOWN_MODE(0));
	            		face = screen->face;
	            		break;
		}

		//use default overlay,set vsyn hsync den dclk polarity
		LcdMskReg(lcdc_dev, DSP_CTRL,m_DISPLAY_FORMAT | m_HSYNC_POLARITY | m_VSYNC_POLARITY |
	     		m_DEN_POLARITY |m_DCLK_POLARITY | m_OUTPUT_RB_SWAP | m_OUTPUT_RG_SWAP | m_BLACK_MODE,
	     		v_DISPLAY_FORMAT(face) | v_HSYNC_POLARITY(screen->pin_hsync) | 
	     		v_VSYNC_POLARITY(screen->pin_vsync) | v_DEN_POLARITY(screen->pin_den) |
	     		v_DCLK_POLARITY(screen->pin_dclk) | v_OUTPUT_RB_SWAP(screen->swap_rb) | 
		 	v_OUTPUT_RG_SWAP(screen->swap_rg) |v_BLACK_MODE(0));

		//set background color to black,set swap according to the screen panel,disable blank mode
		LcdMskReg(lcdc_dev, BG_COLOR, m_BG_COLOR ,v_BG_COLOR(0x000000));

		
		LcdWrReg(lcdc_dev, DSP_HTOTAL_HS_END,v_HSYNC(screen->hsync_len) |
	             v_HORPRD(screen->hsync_len + screen->left_margin + x_res + right_margin));
		LcdWrReg(lcdc_dev, DSP_HACT_ST_END, v_HAEP(screen->hsync_len + screen->left_margin + x_res) |
	             v_HASP(screen->hsync_len + screen->left_margin));

		LcdWrReg(lcdc_dev, DSP_VTOTAL_VS_END, v_VSYNC(screen->vsync_len) |
	              v_VERPRD(screen->vsync_len + screen->upper_margin + y_res + lower_margin));
		LcdWrReg(lcdc_dev, DSP_VACT_ST_END,  v_VAEP(screen->vsync_len + screen->upper_margin+y_res)|
	              v_VASP(screen->vsync_len + screen->upper_margin));

#if defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)
	 if(dev_drv->screen0->lcdc_id == 1)
	 {
		//set register for scaller
		LcdMskReg(lcdc_dev,SCL_REG0,m_SCL_DSP_ZERO | m_SCL_DEN_INVERT |
			m_SCL_SYNC_INVERT | m_SCL_DCLK_INVERT | m_SCL_EN,v_SCL_DSP_ZERO(0) |
			v_SCL_DEN_INVERT(screen0->s_den_inv) | v_SCL_SYNC_INVERT(screen0->s_hv_sync_inv) |
			v_SCL_DCLK_INVERT(screen0->s_clk_inv) | v_SCL_EN(1));
		LcdWrReg(lcdc_dev,SCL_REG2,v_HASP(screen0->s_vsync_st) | v_HAEP(screen0->s_hsync_st));
		LcdWrReg(lcdc_dev,SCL_REG3,v_HASP(screen0->s_hsync_len) |
			 v_HAEP(screen0->s_hsync_len + screen0->s_left_margin + 
			 screen0->x_res + screen0->s_right_margin));
		LcdWrReg(lcdc_dev,SCL_REG4,v_HASP(screen0->s_hsync_len + screen0->s_left_margin) |
			 v_HAEP(screen0->s_hsync_len + screen0->s_left_margin + screen0->x_res));
		LcdWrReg(lcdc_dev,SCL_REG5,v_VASP(screen0->s_vsync_len) |
			 v_VAEP(screen0->s_vsync_len + screen0->s_upper_margin +
			 screen0->y_res + screen0->s_lower_margin));
		LcdWrReg(lcdc_dev,SCL_REG6,v_VASP(screen0->s_vsync_len + 
			 screen0->s_upper_margin) | v_VAEP(screen0->s_vsync_len +
			 screen0->s_upper_margin + screen0->y_res ));
		LcdWrReg(lcdc_dev,SCL_REG8,v_VASP(screen0->s_vsync_len + screen0->s_upper_margin) |
			 v_VAEP(screen0->s_vsync_len + screen0->s_upper_margin + screen0->y_res));
		LcdWrReg(lcdc_dev,SCL_REG7,v_HASP(screen0->s_hsync_len + screen0->s_left_margin) |
			 v_HAEP(screen0->s_hsync_len + screen0->s_left_margin + screen0->x_res ));
		LcdWrReg(lcdc_dev,SCL_REG1,v_SCL_V_FACTOR(0x1000)|v_SCL_H_FACTOR(0x1000));
	 }
#endif
		// let above to take effect
		//LCDC_REG_CFG_DONE();
	}
 	spin_unlock(&lcdc_dev->reg_lock);

#ifdef CONFIG_RK_LVDS
	rk_lvds_register(dev_drv->screen0);
#endif
	if(dev_drv->screen0->type == SCREEN_RGB) //iomux for RGB screen
	{

		if(dev_drv->screen0->lcdc_id == 0)
		{
			rk30_mux_api_set(GPIO2B0_LCDC0_DCLK_LCDC1_DCLK_NAME, GPIO2B_LCDC0_DCLK);
			rk30_mux_api_set(GPIO2B1_LCDC0_HSYNC_LCDC1_HSYNC_NAME, GPIO2B_LCDC0_HSYNC);
			rk30_mux_api_set(GPIO2B2_LCDC0_VSYNC_LCDC1_VSYNC_NAME, GPIO2B_LCDC0_VSYNC);
			rk30_mux_api_set(GPIO2B3_LCDC0_DEN_LCDC1_DEN_NAME, GPIO2B_LCDC0_DEN);
			rk30_mux_api_set(GPIO2B4_LCDC0_D10_LCDC1_D10_NAME, GPIO2B_LCDC0_D10);
			rk30_mux_api_set(GPIO2B5_LCDC0_D11_LCDC1_D11_NAME, GPIO2B_LCDC0_D11);
			rk30_mux_api_set(GPIO2B6_LCDC0_D12_LCDC1_D12_NAME, GPIO2B_LCDC0_D12);
			rk30_mux_api_set(GPIO2B7_LCDC0_D13_LCDC1_D13_NAME, GPIO2B_LCDC0_D13);
			rk30_mux_api_set(GPIO2C0_LCDC0_D14_LCDC1_D14_NAME, GPIO2C_LCDC0_D14);
			rk30_mux_api_set(GPIO2C1_LCDC0_D15_LCDC1_D15_NAME, GPIO2C_LCDC0_D15);
			rk30_mux_api_set(GPIO2C2_LCDC0_D16_LCDC1_D16_NAME, GPIO2C_LCDC0_D16);
			rk30_mux_api_set(GPIO2C3_LCDC0_D17_LCDC1_D17_NAME, GPIO2C_LCDC0_D17);
		}
		else if(dev_drv->screen0->lcdc_id == 1)
		{
			rk30_mux_api_set(GPIO2B0_LCDC0_DCLK_LCDC1_DCLK_NAME, GPIO2B_LCDC1_DCLK);
			rk30_mux_api_set(GPIO2B1_LCDC0_HSYNC_LCDC1_HSYNC_NAME, GPIO2B_LCDC1_HSYNC);
			rk30_mux_api_set(GPIO2B2_LCDC0_VSYNC_LCDC1_VSYNC_NAME, GPIO2B_LCDC1_VSYNC);
			rk30_mux_api_set(GPIO2B3_LCDC0_DEN_LCDC1_DEN_NAME, GPIO2B_LCDC1_DEN);
			rk30_mux_api_set(GPIO2B4_LCDC0_D10_LCDC1_D10_NAME, GPIO2B_LCDC1_D10);
			rk30_mux_api_set(GPIO2B5_LCDC0_D11_LCDC1_D11_NAME, GPIO2B_LCDC1_D11);
			rk30_mux_api_set(GPIO2B6_LCDC0_D12_LCDC1_D12_NAME, GPIO2B_LCDC1_D12);
			rk30_mux_api_set(GPIO2B7_LCDC0_D13_LCDC1_D13_NAME, GPIO2B_LCDC1_D13);
			rk30_mux_api_set(GPIO2C0_LCDC0_D14_LCDC1_D14_NAME, GPIO2C_LCDC1_D14);
			rk30_mux_api_set(GPIO2C1_LCDC0_D15_LCDC1_D15_NAME, GPIO2C_LCDC1_D15);
			rk30_mux_api_set(GPIO2C2_LCDC0_D16_LCDC1_D16_NAME, GPIO2C_LCDC1_D16);
			rk30_mux_api_set(GPIO2C3_LCDC0_D17_LCDC1_D17_NAME, GPIO2C_LCDC1_D17);
		}
		else
		{
			printk(KERN_WARNING "%s>>>no such interface:%d\n",__func__,dev_drv->cur_screen->lcdc_id);
			return -1;
		}
		
		//rk30_mux_api_set(GPIO2C4_LCDC0_D18_LCDC1_D18_I2C2_SDA_NAME, GPIO2C_LCDC1_D18);
		//rk30_mux_api_set(GPIO2C5_LCDC0_D19_LCDC1_D19_I2C2_SCL_NAME, GPIO2C_LCDC1_D19);
		//rk30_mux_api_set(GPIO2C6_LCDC0_D20_LCDC1_D20_UART2_SIN_NAME, GPIO2C_LCDC1_D20);
		//rk30_mux_api_set(GPIO2C7_LCDC0_D21_LCDC1_D21_UART2_SOUT_NAME, GPIO2C_LCDC1_D21);
		//rk30_mux_api_set(GPIO2D0_LCDC0_D22_LCDC1_D22_NAME, GPIO2D_LCDC1_D22);
		//rk30_mux_api_set(GPIO2D1_LCDC0_D23_LCDC1_D23_NAME, GPIO2D_LCDC1_D23);
		printk("RGB screen connect to rk2928 lcdc interface%d\n",dev_drv->screen0->lcdc_id);

	}

	ret = clk_set_rate(lcdc_dev->dclk, screen->pixclock);
	if(ret)
	{
        	printk(KERN_ERR ">>>>>> set lcdc%d dclk failed\n",lcdc_dev->id);
	}
	lcdc_dev->driver.pixclock = lcdc_dev->pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	clk_enable(lcdc_dev->dclk);
#if  defined(CONFIG_ONE_LCDC_DUAL_OUTPUT_INF)

	if(dev_drv->screen0->lcdc_id == 1)  //if connect to output interface 1,need scale
	{
		ret = clk_set_rate(lcdc_dev->sclk, screen0->s_pixclock);
		if(ret)
		{
	        	printk(KERN_ERR ">>>>>> set lcdc%d sclk failed\n",lcdc_dev->id);
		}
		lcdc_dev->driver.pixclock = lcdc_dev->pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->sclk));
		//printk("%s: sclk:%lu>>need:%d",lcdc_dev->driver.name,,screen0->s_pixclock);
		clk_enable(lcdc_dev->sclk);
	}
#endif
	ft = (u64)(screen->upper_margin + screen->lower_margin + screen->y_res +screen->vsync_len)*
		(screen->left_margin + screen->right_margin + screen->x_res + screen->hsync_len)*
		(dev_drv->pixclock);       // one frame time ,(pico seconds)
	fps = div64_u64(1000000000000llu,ft);
	screen->ft = 1000/fps;
    	printk("%s: dclk:%lu>>fps:%d ",lcdc_dev->driver.name,clk_get_rate(lcdc_dev->dclk), 
		fps);

    	if(screen->init)
    	{
    		screen->init();
    	}
	
	printk("%s for lcdc%d ok!\n",__func__,lcdc_dev->id);
	return 0;
}


//enable layer,open:1,enable;0 disable
static int win0_open(struct rk2928_lcdc_device *lcdc_dev,bool open)
{
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(open)
		{
			if(!lcdc_dev->atv_layer_cnt)
			{
				LcdClrBit(lcdc_dev, SYS_CFG,m_LCDC_STANDBY);
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
			LcdSetBit(lcdc_dev, SYS_CFG,m_LCDC_STANDBY);
		}
		//LCDC_REG_CFG_DONE();	
	}
	spin_unlock(&lcdc_dev->reg_lock);
	printk(KERN_INFO "lcdc%d win0 %s\n",lcdc_dev->id,open?"open":"closed");
	return 0;
}
static int win1_open(struct rk2928_lcdc_device *lcdc_dev,bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		if(open)
		{
			if(!lcdc_dev->atv_layer_cnt)
			{
				printk("lcdc%d wakeup from stanby\n",lcdc_dev->id);
				LcdClrBit(lcdc_dev, SYS_CFG,m_LCDC_STANDBY);
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
			LcdSetBit(lcdc_dev, SYS_CFG,m_LCDC_STANDBY);
		}
		//LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);
	printk(KERN_INFO "lcdc%d win1 %s\n",lcdc_dev->id,open?"open":"closed");
	return 0;
}


static int rk2928_lcdc_blank(struct rk_lcdc_device_driver*lcdc_drv,int layer_id,int blank_mode)
{
	struct rk2928_lcdc_device * lcdc_dev = container_of(lcdc_drv,struct rk2928_lcdc_device ,driver);

	printk(KERN_INFO "%s>>>>>%d\n",__func__, blank_mode);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		switch(blank_mode)
	    	{
	    		case FB_BLANK_UNBLANK:
	      			LcdMskReg(lcdc_dev,DSP_CTRL,m_BLANK_MODE ,v_BLANK_MODE(0));
				break;
	    		case FB_BLANK_NORMAL:
	         		LcdMskReg(lcdc_dev,DSP_CTRL,m_BLANK_MODE ,v_BLANK_MODE(1));
				break;
	    		default:
				LcdMskReg(lcdc_dev,DSP_CTRL,m_BLANK_MODE ,v_BLANK_MODE(1));
				break;
		}
		LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
    	return 0;
}

static  int win0_display(struct rk2928_lcdc_device *lcdc_dev,struct layer_par *par )
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

static  int win1_display(struct rk2928_lcdc_device *lcdc_dev,struct layer_par *par )
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = par->smem_start + par->y_offset;
    	uv_addr = par->cbr_start + par->c_offset;
	DBG(2,KERN_INFO "lcdc%d>>%s>>y_addr:0x%x>>uv_addr:0x%x\n",lcdc_dev->id,__func__,y_addr,uv_addr);
	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		LcdWrReg(lcdc_dev, WIN1_RGB_MST, y_addr);
		LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
	return 0;
}

static  int win0_set_par(struct rk2928_lcdc_device *lcdc_dev,rk_screen *screen,
	struct layer_par *par )
{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u32 ScaleYrgbX = 0x1000;
	u32 ScaleYrgbY = 0x1000;
	u32 ScaleCbrX = 0x1000;
	u32 ScaleCbrY = 0x1000;
	u8 fmt_cfg = 0;
	
	xact = par->xact;			    //active (origin) picture window width/height		
	yact = par->yact;
	xvir = par->xvir;			   // virtual resolution		
	yvir = par->yvir;
	xpos = par->xpos+screen->left_margin + screen->hsync_len;
	ypos = par->ypos+screen->upper_margin + screen->vsync_len;

	ScaleYrgbX = CalScale(xact, par->xsize); //both RGB and yuv need this two factor
	ScaleYrgbY = CalScale(yact, par->ysize);

	DBG(1,"%s for lcdc%d>>format:%d>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
		__func__,lcdc_dev->id,par->format,xact,yact,par->xsize,par->ysize,xvir,yvir,xpos,ypos);
	switch (par->format)
	{
		case XBGR888:
		case ABGR888:
		case ARGB888:
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
			printk("%s:un supported format\n",__func__);
		   	break;
	}

	
	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_YRGB, v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
		LcdWrReg(lcdc_dev, WIN0_SCL_FACTOR_CBR,v_X_SCL_FACTOR(ScaleCbrX)| v_Y_SCL_FACTOR(ScaleCbrY));
		LcdMskReg(lcdc_dev, SYS_CFG, m_W0_FORMAT, v_W0_FORMAT(fmt_cfg));		//(inf->video_mode==0)
		LcdWrReg(lcdc_dev, WIN0_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
		LcdWrReg(lcdc_dev, WIN0_DSP_ST, v_DSP_STX(xpos) | v_DSP_STY(ypos));
		LcdWrReg(lcdc_dev, WIN0_DSP_INFO, v_DSP_WIDTH(par->xsize)| v_DSP_HEIGHT(par->ysize));
		LcdMskReg(lcdc_dev,WIN0_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR,
			v_COLORKEY_EN(1) | v_KEYCOLOR(0));
		switch(par->format) 
		{
			case XBGR888:
				LcdMskReg(lcdc_dev, WIN_VIR,m_WIN0_VIR,v_WIN0_ARGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W0_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
				break;
			case ARGB888:
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN0_VIR,v_WIN0_ARGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W0_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(0));
				break;
			case ABGR888:
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN0_VIR,v_WIN0_ARGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W0_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
				break;
			case RGB888:  //rgb888
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN0_VIR,v_WIN0_RGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W0_RGB_RB_SWAP,v_W0_RGB_RB_SWAP(0));
				break;
			case RGB565:  //rgb565
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN0_VIR,v_WIN0_RGB565_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,DSP_CTRL,m_W0_RGB_RB_SWAP,v_W0_RGB_RB_SWAP(0));
				break;
			case YUV422:
			case YUV420:   
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN0_VIR,v_WIN0_YUV_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W0_RGB_RB_SWAP,v_W0_RGB_RB_SWAP(0));
				break;
			default:
				printk("%s:un supported format\n",__func__);
				break;
		}
		
		LCDC_REG_CFG_DONE();
	}
	spin_unlock(&lcdc_dev->reg_lock);

    return 0;

}

static int win1_set_par(struct rk2928_lcdc_device *lcdc_dev,rk_screen *screen,
	struct layer_par *par )
{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u32 ScaleYrgbX = 0x1000;
	u32 ScaleYrgbY = 0x1000;
	u32 ScaleCbrX = 0x1000;
	u32 ScaleCbrY = 0x1000;
	u8 fmt_cfg = 0;
	
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
		LcdWrReg(lcdc_dev, WIN1_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
		LcdWrReg(lcdc_dev, WIN1_DSP_INFO,v_DSP_WIDTH(par->xsize) | v_DSP_HEIGHT(par->ysize));
		// enable win1 color key and set the color to black(rgb=0)
		LcdMskReg(lcdc_dev, WIN1_COLOR_KEY_CTRL, m_COLORKEY_EN | m_KEYCOLOR,v_COLORKEY_EN(1) | v_KEYCOLOR(0));

		
		switch(par->format)
	       {	
			case XBGR888:
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN1_VIR,v_WIN1_ARGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
				break;
			case ABGR888:
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN1_VIR,v_WIN1_ARGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(1));
				break;
			case ARGB888:
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN1_VIR,v_WIN1_ARGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(0));
				break;
			case RGB888:  //rgb888
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN1_VIR,v_WIN1_RGB888_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,DSP_CTRL,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(0));
				break;
			case RGB565:  //rgb565
				LcdMskReg(lcdc_dev,WIN_VIR,m_WIN1_VIR,v_WIN1_RGB565_VIRWIDTH(xvir));
				LcdMskReg(lcdc_dev,SYS_CFG,m_W1_RGB_RB_SWAP,v_W1_RGB_RB_SWAP(0));
				break;
			default:
				printk("%s:un supported format\n",__func__);
				break;
	       }
		
	    	LcdMskReg(lcdc_dev,SYS_CFG, m_W1_FORMAT, v_W1_FORMAT(fmt_cfg));
		//LCDC_REG_CFG_DONE(); 
	}
	spin_unlock(&lcdc_dev->reg_lock);
    return 0;
}

static int rk2928_lcdc_open(struct rk_lcdc_device_driver *dev_drv,int layer_id,bool open)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);

	if(open)
	{
		rk2928_load_screen(dev_drv,1);
	}
	if(layer_id == 0)
	{
		win0_open(lcdc_dev,open);	
	}
	else if(layer_id == 1)
	{
		win1_open(lcdc_dev,open);
	}

	return 0;
}

static int rk2928_lcdc_set_par(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
	struct layer_par *par = NULL;
	rk_screen *screen = dev_drv->cur_screen;
	rk_screen *screen0 = dev_drv->screen0;
	u32 Scl_X = 0x1000;
	u32 Scl_Y = 0x1000;
	
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
	Scl_X = CalScale(screen->x_res - 1,screen0->x_res - 1);
	Scl_Y = CalScale(screen->y_res - 1 ,screen0->y_res - 1);
	LcdWrReg(lcdc_dev,SCL_REG1,v_SCL_V_FACTOR(Scl_Y)|v_SCL_H_FACTOR(Scl_X));
	
	return 0;
}

int rk2928_lcdc_pan_display(struct rk_lcdc_device_driver * dev_drv,int layer_id)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
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
		LcdMskReg(lcdc_dev,INT_STATUS,m_FRM_START_INT_CLEAR |m_FRM_START_INT_EN ,
			  v_FRM_START_INT_CLEAR(1) | v_FRM_START_INT_EN(1));
		LCDC_REG_CFG_DONE();  // write any value to  REG_CFG_DONE let config become effective
		 
	}

	if(dev_drv->num_buf < 3) //3buffer ,no need to  wait for sysn
	{
		spin_lock_irqsave(&dev_drv->cpl_lock,flags);
		init_completion(&dev_drv->frame_done);
		spin_unlock_irqrestore(&dev_drv->cpl_lock,flags);
		timeout = wait_for_completion_timeout(&dev_drv->frame_done,msecs_to_jiffies(dev_drv->cur_screen->ft+5));
		if(!timeout&&(!dev_drv->frame_done.done))
		{
			//printk(KERN_ERR "wait for new frame start time out!\n");
			return -ETIMEDOUT;
		}
	}
	
	return 0;
}

int rk2928_lcdc_ioctl(struct rk_lcdc_device_driver * dev_drv,unsigned int cmd, unsigned long arg,int layer_id)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
	u32 panel_size[2];
	void __user *argp = (void __user *)arg;
	int ret = 0;
	switch(cmd)
	{
		case RK_FBIOGET_PANEL_SIZE:    //get panel size
                	panel_size[0] = dev_drv->screen0->x_res;
                	panel_size[1] = dev_drv->screen0->y_res;
            		if(copy_to_user(argp, panel_size, 8)) 
				return -EFAULT;
			break;
		default:
			break;
	}

	return ret;
}
static int rk2928_lcdc_get_layer_state(struct rk_lcdc_device_driver *dev_drv,int layer_id)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
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
static int rk2928_lcdc_ovl_mgr(struct rk_lcdc_device_driver *dev_drv,int swap,bool set)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
	int ovl;
	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on)
	{
		if(set)  //set overlay
		{
			LcdMskReg(lcdc_dev,DSP_CTRL,m_W0W1_POSITION_SWAP,v_W0W1_POSITION_SWAP(swap));
			LCDC_REG_CFG_DONE();
			ovl = swap;
		}
		else  //get overlay
		{
			ovl = LcdReadBit(lcdc_dev,DSP_CTRL,m_W0W1_POSITION_SWAP);
		}
	}
	else
	{
		ovl = -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return ovl;
}

static ssize_t dump_win0_disp_info(struct rk2928_lcdc_device *lcdc_dev,char *buf)
{
        char format[9] = "NULL";
        u32 fmt_id = LcdRdReg(lcdc_dev,SYS_CFG);
        u32 xvir,act_info,dsp_info,dsp_st,factor;
        u16 x_act,y_act,x_dsp,y_dsp,x_factor,y_factor;
        u16 x_scale,y_scale;
        switch((fmt_id&m_W0_FORMAT)>>3)
        {
                case 0:
                        strcpy(format,"ARGB888");
                        break;
                case 1:
                        strcpy(format,"RGB888");
                        break;
                case 2:
                        strcpy(format,"RGB565");
                        break;
                case 4:
                        strcpy(format,"YCbCr422");
                        break;
                case 5:
                        strcpy(format,"YCbCr420");
                        break;
                case 6:
                        strcpy(format,"YCbCr444");
                        break;
                default:
                        strcpy(format,"inval\n");
                        break;
        }

        xvir = LcdRdReg(lcdc_dev,WIN_VIR)&0xffff;
        act_info = LcdRdReg(lcdc_dev,WIN0_ACT_INFO);
        dsp_info = LcdRdReg(lcdc_dev,WIN0_DSP_INFO);
        dsp_st = LcdRdReg(lcdc_dev,WIN0_DSP_ST);
        factor = LcdRdReg(lcdc_dev,WIN0_SCL_FACTOR_YRGB);
        x_act =  (act_info&0xffff) + 1;
        y_act = (act_info>>16) + 1;
        x_dsp = (dsp_info&0xffff) + 1;
        y_dsp = (dsp_info>>16) + 1;
	x_factor = factor&0xffff;
        y_factor = factor>>16;
        x_scale = 4096*100/x_factor;
        y_scale = 4096*100/y_factor;
        return snprintf(buf,PAGE_SIZE,"xvir:%d\nxact:%d\nyact:%d\nxdsp:%d\nydsp:%d\nx_st:%d\ny_st:%d\nx_scale:%d.%d\ny_scale:%d.%d\nformat:%s\n",
                xvir,x_act,y_act,x_dsp,y_dsp,dsp_st&0xffff,dsp_st>>16,x_scale/100,x_scale%100,y_scale/100,y_scale%100,format);

}

static ssize_t dump_win1_disp_info(struct rk2928_lcdc_device *lcdc_dev,char *buf)
{
        char format[9] = "NULL";
        u32 fmt_id = LcdRdReg(lcdc_dev,SYS_CFG);
        u32 xvir,act_info,dsp_info,dsp_st,factor;
        u16 x_act,y_act,x_dsp,y_dsp,x_factor,y_factor;
        u16 x_scale,y_scale;
        switch((fmt_id&m_W1_FORMAT)>>6)
        {
                case 0:
                        strcpy(format,"ARGB888");
                        break;
                case 1:
                        strcpy(format,"RGB888");
                        break;
                case 2:
                        strcpy(format,"RGB565");
                        break;
                default:
                        strcpy(format,"inval\n");
                        break;
        }

        xvir = (LcdRdReg(lcdc_dev,WIN_VIR)>>16)&0xfff;
        dsp_info = LcdRdReg(lcdc_dev,WIN1_DSP_INFO);
        dsp_st = LcdRdReg(lcdc_dev,WIN1_DSP_ST);

        x_dsp = dsp_info&0xffff;
        y_dsp = dsp_info>>16;

        return snprintf(buf,PAGE_SIZE,"xvir:%d\nxdsp:%d\nydsp:%d\nx_st:%d\ny_st:%d\nformat:%s\n",
                xvir,x_dsp,y_dsp,dsp_st&0xffff,dsp_st>>16,format);
}

static ssize_t  rk2928_lcdc_get_disp_info(struct rk_lcdc_device_driver *dev_drv,char *buf,int layer_id)
{
        struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
        if(layer_id == 0)
        {
                return dump_win0_disp_info(lcdc_dev,buf);
        }
        else if(layer_id == 1)
        {
                return dump_win1_disp_info(lcdc_dev,buf);
        }
       
        return 0;
}



/*******************************************
lcdc fps manager,set or get lcdc fps
set:0 get
     1 set
********************************************/
static int rk2928_lcdc_fps_mgr(struct rk_lcdc_device_driver *dev_drv,int fps,bool set)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);
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


static int rk2928_fb_layer_remap(struct rk_lcdc_device_driver *dev_drv,
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

static int rk2928_fb_get_layer(struct rk_lcdc_device_driver *dev_drv,const char *id)
{
        int layer_id = 0;
        mutex_lock(&dev_drv->fb_win_id_mutex);
        if(!strcmp(id,"fb0"))
        {
                layer_id = dev_drv->fb0_win_id;
        }
        else if(!strcmp(id,"fb1"))
        {
                layer_id = dev_drv->fb1_win_id;
        }
        else if(!strcmp(id,"fb2"))
        {
                layer_id = dev_drv->fb2_win_id;
        }
        else
        {
                printk(KERN_ERR "%s>>un supported %s\n",__func__,id);
                layer_id = -1;
        }
        mutex_unlock(&dev_drv->fb_win_id_mutex);
	//printk("%s %s win%d\n",__func__,id,layer_id);
        return  layer_id;
}

static int rk2928_lcdc_hdmi_process(struct rk_lcdc_device_driver *dev_drv,int mode)
{
	printk("%s>>>>>>>>mode:%d\n",__func__,mode);
	if(mode)
	{
		rk2928_lcdc_iomux(dev_drv->screen0,0); //switch to gpio mode,to avoid  current leakage
		if(dev_drv->screen_ctr_info->io_disable)
			dev_drv->screen_ctr_info->io_disable();
	}
	else
	{
		rk2928_lcdc_iomux(dev_drv->screen0,1); //switch to gpio mode,to avoid  current leakage
		if(dev_drv->screen_ctr_info->io_enable)
			dev_drv->screen_ctr_info->io_enable();
	}
	
	return 0;
	
}
int rk2928_lcdc_early_suspend(struct rk_lcdc_device_driver *dev_drv)
{
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);

	if(dev_drv->screen0->standby)
		dev_drv->screen0->standby(1);
	if(dev_drv->screen_ctr_info->io_disable)
		dev_drv->screen_ctr_info->io_disable();
	
	if(dev_drv->cur_screen->sscreen_set)
		dev_drv->cur_screen->sscreen_set(dev_drv->cur_screen , 0);

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_dev->clk_on = 0;
		LcdMskReg(lcdc_dev, INT_STATUS, m_FRM_START_INT_CLEAR, v_FRM_START_INT_CLEAR(1));
		LcdSetBit(lcdc_dev,DSP_CTRL,m_BLACK_MODE);
		LcdMskReg(lcdc_dev, SYS_CFG,m_DSP_OUT_ZERO | m_LCDC_STANDBY,
			v_DSP_OUT_ZERO(1) | v_LCDC_STANDBY(1));
		LCDC_REG_CFG_DONE();
		spin_unlock(&lcdc_dev->reg_lock);
	}
	else  //clk already disabled
	{
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}
	
		
	mdelay(10);
 	rk2928_lcdc_iomux(dev_drv->screen0,0); //switch to gpio mode,to avoid  current leakage
	clk_disable(lcdc_dev->dclk);
	clk_disable(lcdc_dev->hclk);
	clk_disable(lcdc_dev->aclk);
	clk_disable(lcdc_dev->pd);

	return 0;
}



int rk2928_lcdc_early_resume(struct rk_lcdc_device_driver *dev_drv)
{  
	struct rk2928_lcdc_device *lcdc_dev = container_of(dev_drv,struct rk2928_lcdc_device,driver);

	if(dev_drv->screen_ctr_info->io_enable) 		//power on
		dev_drv->screen_ctr_info->io_enable();
	
	if(!lcdc_dev->clk_on)
	{
		clk_enable(lcdc_dev->pd);
		clk_enable(lcdc_dev->hclk);
		clk_enable(lcdc_dev->dclk);
		clk_enable(lcdc_dev->aclk);
	}
	rk2928_lcdc_iomux(dev_drv->screen0,1); //switch to lcdc io
	memcpy((u8*)lcdc_dev->preg, (u8*)&lcdc_dev->regbak, 0xc4);  //resume reg

	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->atv_layer_cnt)
	{
		LcdMskReg(lcdc_dev, SYS_CFG,m_DSP_OUT_ZERO | m_LCDC_STANDBY,
			v_DSP_OUT_ZERO(0) | v_LCDC_STANDBY(0));
		LcdClrBit(lcdc_dev,DSP_CTRL,m_BLACK_MODE);
		LCDC_REG_CFG_DONE();
	}
	lcdc_dev->clk_on = 1;
	spin_unlock(&lcdc_dev->reg_lock);
	
	
	if(dev_drv->cur_screen->sscreen_set)
		dev_drv->cur_screen->sscreen_set(dev_drv->cur_screen , 1);

	if(dev_drv->screen0->standby)
		dev_drv->screen0->standby(0);	      //screen wake up
	

    	return 0;
}
static irqreturn_t rk2928_lcdc_isr(int irq, void *dev_id)
{
	struct rk2928_lcdc_device *lcdc_dev = (struct rk2928_lcdc_device *)dev_id;

	ktime_t timestamp = ktime_get();
	
	LcdMskReg(lcdc_dev, INT_STATUS, m_FRM_START_INT_CLEAR, v_FRM_START_INT_CLEAR(1));
	//LCDC_REG_CFG_DONE();
	//LcdMskReg(lcdc_dev, INT_STATUS, m_LINE_FLAG_INT_CLEAR, v_LINE_FLAG_INT_CLEAR(1));
 
	if(lcdc_dev->driver.num_buf < 3)  //three buffer ,no need to wait for sync
	{
		spin_lock(&(lcdc_dev->driver.cpl_lock));
		complete(&(lcdc_dev->driver.frame_done));
		spin_unlock(&(lcdc_dev->driver.cpl_lock));
	}
	
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
	.open			= rk2928_lcdc_open,
	.init_lcdc		= init_rk2928_lcdc,
	.ioctl			= rk2928_lcdc_ioctl,
	.suspend		= rk2928_lcdc_early_suspend,
	.resume			= rk2928_lcdc_early_resume,
	.set_par       		= rk2928_lcdc_set_par,
	.blank         		= rk2928_lcdc_blank,
	.pan_display            = rk2928_lcdc_pan_display,
	.load_screen		= rk2928_load_screen,
	.get_layer_state	= rk2928_lcdc_get_layer_state,
	.ovl_mgr		= rk2928_lcdc_ovl_mgr,
	.get_disp_info		= rk2928_lcdc_get_disp_info,
	.fps_mgr		= rk2928_lcdc_fps_mgr,
	.fb_get_layer           = rk2928_fb_get_layer,
	.fb_layer_remap         = rk2928_fb_layer_remap,
	.lcdc_hdmi_process	= rk2928_lcdc_hdmi_process,
};
#ifdef CONFIG_PM
static int rk2928_lcdc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int rk2928_lcdc_resume(struct platform_device *pdev)
{
	return 0;
}

#else
#define rk2928_lcdc_suspend NULL
#define rk2928_lcdc_resume NULL
#endif

static int __devinit rk2928_lcdc_probe (struct platform_device *pdev)
{
	struct rk2928_lcdc_device *lcdc_dev=NULL;
	rk_screen *screen0;
	rk_screen *screen1;
	struct rk29fb_info *screen_ctr_info;
	struct resource *res = NULL;
	struct resource *mem;
	int ret = 0;
	
	/*************Malloc rk2928lcdc_inf and set it to pdev for drvdata**********/
	lcdc_dev = kzalloc(sizeof(struct rk2928_lcdc_device), GFP_KERNEL);
    	if(!lcdc_dev)
    	{
        	dev_err(&pdev->dev, ">>rk2928 lcdc device kmalloc fail!");
        	return -ENOMEM;
    	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->id = pdev->id;
	screen_ctr_info = (struct rk29fb_info * )pdev->dev.platform_data;
	screen0 =  kzalloc(sizeof(rk_screen), GFP_KERNEL); //rk2928 has one lcdc but two outputs
	if(!screen0)
	{
		dev_err(&pdev->dev, ">>rk2928 lcdc screen1 kmalloc fail!");
        	ret =  -ENOMEM;
		goto err0;
	}
	screen0->lcdc_id = 0;  //this id can be changed dynamic
	screen0->screen_id = 0; //this id is fixed
	screen1 =  kzalloc(sizeof(rk_screen), GFP_KERNEL);
	if(!screen1)
	{
		dev_err(&pdev->dev, ">>rk2928 lcdc screen1 kmalloc fail!");
        	ret =  -ENOMEM;
		goto err0;
	}
	screen1->lcdc_id = 1;
	screen1->screen_id = 1;
	
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
	lcdc_dev->driver.screen0 = screen0;  //direct out put
	lcdc_dev->driver.screen1 = screen1; //out put from scale
	lcdc_dev->driver.cur_screen = screen0;
	lcdc_dev->driver.screen_ctr_info = screen_ctr_info;
	spin_lock_init(&lcdc_dev->reg_lock);
	lcdc_dev->irq = platform_get_irq(pdev, 0);
	if(lcdc_dev->irq < 0)
	{
		dev_err(&pdev->dev, "cannot find IRQ\n");
		goto err3;
	}
	ret = request_irq(lcdc_dev->irq, rk2928_lcdc_isr, IRQF_DISABLED,dev_name(&pdev->dev),lcdc_dev);
	if (ret)
	{
	       dev_err(&pdev->dev, "cannot requeset irq %d - err %d\n", lcdc_dev->irq, ret);
	       ret = -EBUSY;
	       goto err3;
	}

	if(screen_ctr_info->set_screen_info)
	{
		screen_ctr_info->set_screen_info(screen0,screen_ctr_info->lcd_info);
		if(SCREEN_NULL==screen0->type)
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
	printk("rk2928 lcdc%d probe ok!\n",lcdc_dev->id);

	return 0;

err4:
	free_irq(lcdc_dev->irq,lcdc_dev);
err3:	
	iounmap(lcdc_dev->reg_vir_base);
err2:
	release_mem_region(lcdc_dev->reg_phy_base,resource_size(res));
err1:
	kfree(screen0);
err0:
	platform_set_drvdata(pdev, NULL);
	kfree(lcdc_dev);
	return ret;
    
}
static int __devexit rk2928_lcdc_remove(struct platform_device *pdev)
{
	struct rk2928_lcdc_device *lcdc_dev = platform_get_drvdata(pdev);
	rk_fb_unregister(&(lcdc_dev->driver));
	rk2928_lcdc_deinit(lcdc_dev);
	iounmap(lcdc_dev->reg_vir_base);
	release_mem_region(lcdc_dev->reg_phy_base,lcdc_dev->len);
	kfree(lcdc_dev->screen);
	kfree(lcdc_dev);
	return 0;
}

static void rk2928_lcdc_shutdown(struct platform_device *pdev)
{
	struct rk2928_lcdc_device *lcdc_dev = platform_get_drvdata(pdev);
	if(lcdc_dev->driver.cur_screen->standby) //standby the screen if necessary
		lcdc_dev->driver.cur_screen->standby(1);
	if(lcdc_dev->driver.screen_ctr_info->io_disable) //power off the screen if necessary
		lcdc_dev->driver.screen_ctr_info->io_disable();
	if(lcdc_dev->driver.cur_screen->sscreen_set) //turn off  lvds
		lcdc_dev->driver.cur_screen->sscreen_set(lcdc_dev->driver.cur_screen , 0);
	rk_fb_unregister(&(lcdc_dev->driver));
	rk2928_lcdc_deinit(lcdc_dev);
	/*iounmap(lcdc_dev->reg_vir_base);
	release_mem_region(lcdc_dev->reg_phy_base,lcdc_dev->len);
	kfree(lcdc_dev->screen);
	kfree(lcdc_dev);*/
}


static struct platform_driver rk2928lcdc_driver = {
	.probe		= rk2928_lcdc_probe,
	.remove		= __devexit_p(rk2928_lcdc_remove),
	.driver		= {
		.name	= "rk2928-lcdc",
		.owner	= THIS_MODULE,
	},
	.suspend	= rk2928_lcdc_suspend,
	.resume		= rk2928_lcdc_resume,
	.shutdown   = rk2928_lcdc_shutdown,
};

static int __init rk2928_lcdc_init(void)
{
    return platform_driver_register(&rk2928lcdc_driver);
}

static void __exit rk2928_lcdc_exit(void)
{
	platform_driver_unregister(&rk2928lcdc_driver);
}



fs_initcall(rk2928_lcdc_init);
module_exit(rk2928_lcdc_exit);



