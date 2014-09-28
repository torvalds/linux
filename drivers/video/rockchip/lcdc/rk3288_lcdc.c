/*
 * drivers/video/rockchip/lcdc/rk3288_lcdc.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 *Author:hjc<hjc@rock-chips.com>
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
#include <linux/rockchip-iovmm.h>
#include <asm/div64.h>
#include <asm/uaccess.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/common.h>
#include <dt-bindings/clock/rk_system_status.h>

#include "rk3288_lcdc.h"

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

static int dbg_thresd;
module_param(dbg_thresd, int, S_IRUGO | S_IWUSR);

#define DBG(level, x...) do {			\
	if (unlikely(dbg_thresd >= level))	\
		printk(KERN_INFO x); } while (0)

static int rk3288_lcdc_set_bcsh(struct rk_lcdc_driver *dev_drv,
				     bool enable);

/*#define WAIT_FOR_SYNC 1*/

static int rk3288_lcdc_get_id(u32 phy_base)
{
	if (cpu_is_rk3288()) {
		if (phy_base == 0xff930000)/*vop big*/
			return 0;
		else if (phy_base == 0xff940000)/*vop lit*/	
			return 1;
		else
			return -EINVAL;
	} else {
		pr_err("un supported platform \n");
		return -EINVAL;
	}
}

static int rk3288_lcdc_set_lut(struct rk_lcdc_driver *dev_drv)
{
	int i,j;
	int __iomem *c;
	u32 v,r,g,b;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device,driver);
	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN, v_DSP_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	mdelay(25);
	for (i = 0; i < 256; i++) {
		v = dev_drv->cur_screen->dsp_lut[i];
		c = lcdc_dev->dsp_lut_addr_base + (i << 2);
		b = (v & 0xff) << 2;
		g = (v & 0xff00) << 4;
		r = (v & 0xff0000) << 6;
		v = r + g + b;
		for (j = 0; j < 4; j++) {
			writel_relaxed(v, c);
			v += (1 + (1 << 10) + (1 << 20)) ;
			c++;
		}
	}
	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN, v_DSP_LUT_EN(1));

	return 0;

}

static int rk3288_lcdc_clk_enable(struct lcdc_device *lcdc_dev)
{
#ifdef CONFIG_RK_FPGA
	lcdc_dev->clk_on = 1;
	return 0;
#endif	
	if (!lcdc_dev->clk_on) {
		clk_prepare_enable(lcdc_dev->hclk);
		clk_prepare_enable(lcdc_dev->dclk);
		clk_prepare_enable(lcdc_dev->aclk);
		clk_prepare_enable(lcdc_dev->pd);
		spin_lock(&lcdc_dev->reg_lock);
		lcdc_dev->clk_on = 1;
		spin_unlock(&lcdc_dev->reg_lock);
	}

	return 0;
}

static int rk3288_lcdc_clk_disable(struct lcdc_device *lcdc_dev)
{
#ifdef CONFIG_RK_FPGA
	lcdc_dev->clk_on = 0;
	return 0;
#endif	
	if (lcdc_dev->clk_on) {
		spin_lock(&lcdc_dev->reg_lock);
		lcdc_dev->clk_on = 0;
		spin_unlock(&lcdc_dev->reg_lock);
		mdelay(25);
		clk_disable_unprepare(lcdc_dev->dclk);
		clk_disable_unprepare(lcdc_dev->hclk);
		clk_disable_unprepare(lcdc_dev->aclk);
		clk_disable_unprepare(lcdc_dev->pd);
	}

	return 0;
}

static int rk3288_lcdc_disable_irq(struct lcdc_device *lcdc_dev)
{	
	u32 mask, val;
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		mask = m_DSP_HOLD_VALID_INTR_EN | m_FS_INTR_EN |
			m_LINE_FLAG_INTR_EN | m_BUS_ERROR_INTR_EN;
		val = v_DSP_HOLD_VALID_INTR_EN(0) | v_FS_INTR_EN(0) |
			v_LINE_FLAG_INTR_EN(0) | v_BUS_ERROR_INTR_EN(0);
		lcdc_msk_reg(lcdc_dev, INTR_CTRL0, mask, val);

		mask = m_DSP_HOLD_VALID_INTR_CLR | m_FS_INTR_CLR |
			m_LINE_FLAG_INTR_CLR | m_LINE_FLAG_INTR_CLR;
		val = v_DSP_HOLD_VALID_INTR_CLR(0) | v_FS_INTR_CLR(0) |
			v_LINE_FLAG_INTR_CLR(0) | v_BUS_ERROR_INTR_CLR(0);
		lcdc_msk_reg(lcdc_dev, INTR_CTRL0, mask, val);

		mask = m_WIN0_EMPTY_INTR_EN | m_WIN1_EMPTY_INTR_EN |
			m_WIN2_EMPTY_INTR_EN | m_WIN3_EMPTY_INTR_EN |
			m_HWC_EMPTY_INTR_EN | m_POST_BUF_EMPTY_INTR_EN |
			m_POST_BUF_EMPTY_INTR_EN;
		val = v_WIN0_EMPTY_INTR_EN(0) | v_WIN1_EMPTY_INTR_EN(0) |
			v_WIN2_EMPTY_INTR_EN(0) | v_WIN3_EMPTY_INTR_EN(0) |
			v_HWC_EMPTY_INTR_EN(0) | v_POST_BUF_EMPTY_INTR_EN(0) |
			v_PWM_GEN_INTR_EN(0);
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, mask, val);

		mask = m_WIN0_EMPTY_INTR_CLR | m_WIN1_EMPTY_INTR_CLR |
			m_WIN2_EMPTY_INTR_CLR | m_WIN3_EMPTY_INTR_CLR |
			m_HWC_EMPTY_INTR_CLR | m_POST_BUF_EMPTY_INTR_CLR |
			m_POST_BUF_EMPTY_INTR_CLR;
		val = v_WIN0_EMPTY_INTR_CLR(0) | v_WIN1_EMPTY_INTR_CLR(0) |
			v_WIN2_EMPTY_INTR_CLR(0) | v_WIN3_EMPTY_INTR_CLR(0) |
			v_HWC_EMPTY_INTR_CLR(0) | v_POST_BUF_EMPTY_INTR_CLR(0) |
			v_PWM_GEN_INTR_CLR(0);
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, mask, val);		
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
	}
	mdelay(1);
	return 0;
}
static int rk3288_lcdc_reg_dump(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						struct lcdc_device,
						driver);
	int *cbase = (int *)lcdc_dev->regs;
	int *regsbak = (int *)lcdc_dev->regsbak;
	int i, j;

	printk("back up reg:\n");
	for (i = 0; i <= (0x200 >> 4); i++) {
		printk("0x%04x: ",i*16);
		for (j = 0; j < 4; j++)
			printk("%08x  ", *(regsbak + i * 4 + j));
		printk("\n");
	}

	printk("lcdc reg:\n");
	for (i = 0; i <= (0x200 >> 4); i++) {
		printk("0x%04x: ",i*16);
		for (j = 0; j < 4; j++)
			printk("%08x  ", readl_relaxed(cbase + i * 4 + j));
		printk("\n");
	}
	return 0;

}

#define WIN_EN(id)		\
static int win##id##_enable(struct lcdc_device *lcdc_dev, int en)	\
{ \
	u32 msk, val;							\
	spin_lock(&lcdc_dev->reg_lock);					\
	msk =  m_WIN##id##_EN;						\
	val  =  v_WIN##id##_EN(en);					\
	lcdc_msk_reg(lcdc_dev, WIN##id##_CTRL0, msk, val);		\
	lcdc_cfg_done(lcdc_dev);					\
	val = lcdc_read_bit(lcdc_dev, WIN##id##_CTRL0, msk);		\
	while (val !=  (!!en))	{					\
		val = lcdc_read_bit(lcdc_dev, WIN##id##_CTRL0, msk);	\
	}								\
	spin_unlock(&lcdc_dev->reg_lock);				\
	return 0;							\
}

WIN_EN(0);
WIN_EN(1);
WIN_EN(2);
WIN_EN(3);
/*enable/disable win directly*/
static int rk3288_lcdc_win_direct_en
		(struct rk_lcdc_driver *drv, int win_id , int en)
{
	struct lcdc_device *lcdc_dev = container_of(drv,
					struct lcdc_device, driver);
	if (win_id == 0)
		win0_enable(lcdc_dev, en);
	else if (win_id == 1)
		win1_enable(lcdc_dev, en);
	else if (win_id == 2)
		win2_enable(lcdc_dev, en);
	else if (win_id == 3)
		win3_enable(lcdc_dev, en);
	else
		dev_err(lcdc_dev->dev, "invalid win number:%d\n", win_id);
	return 0;
		
}

#define SET_WIN_ADDR(id) \
static int set_win##id##_addr(struct lcdc_device *lcdc_dev, u32 addr) \
{							\
	u32 msk, val;					\
	spin_lock(&lcdc_dev->reg_lock);			\
	lcdc_writel(lcdc_dev,WIN##id##_YRGB_MST,addr);	\
	msk =  m_WIN##id##_EN;				\
	val  =  v_WIN0_EN(1);				\
	lcdc_msk_reg(lcdc_dev, WIN##id##_CTRL0, msk,val);	\
	lcdc_cfg_done(lcdc_dev);			\
	spin_unlock(&lcdc_dev->reg_lock);		\
	return 0;					\
}

SET_WIN_ADDR(0);
SET_WIN_ADDR(1);
int rk3288_lcdc_direct_set_win_addr
		(struct rk_lcdc_driver *dev_drv, int win_id, u32 addr)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
				struct lcdc_device, driver);
	if (win_id == 0)
		set_win0_addr(lcdc_dev, addr);
	else
		set_win1_addr(lcdc_dev, addr);
	
	return 0;
}

static void lcdc_read_reg_defalut_cfg(struct lcdc_device *lcdc_dev)
{
	int reg = 0;
	u32 val = 0;
	struct rk_screen *screen = lcdc_dev->driver.cur_screen;
	u32 h_pw_bp = screen->mode.hsync_len + screen->mode.left_margin;
	u32 v_pw_bp = screen->mode.vsync_len + screen->mode.upper_margin;
	u32 st_x, st_y;
	struct rk_lcdc_win *win0 = lcdc_dev->driver.win[0];

	spin_lock(&lcdc_dev->reg_lock);
	for (reg = 0; reg < 0x1a0; reg+= 4) {
		val = lcdc_readl(lcdc_dev, reg);
		switch (reg) {
			case WIN0_ACT_INFO:
				win0->area[0].xact = (val & m_WIN0_ACT_WIDTH)+1;
				win0->area[0].yact = ((val & m_WIN0_ACT_HEIGHT)>>16)+1;
				break;
			case WIN0_DSP_INFO:
				win0->area[0].xsize = (val & m_WIN0_DSP_WIDTH) + 1;
				win0->area[0].ysize = ((val & m_WIN0_DSP_HEIGHT) >> 16) + 1;
				break;
			case WIN0_DSP_ST:
				st_x = val & m_WIN0_DSP_XST;
				st_y = (val & m_WIN0_DSP_YST) >> 16;
				win0->area[0].xpos = st_x - h_pw_bp;
				win0->area[0].ypos = st_y - v_pw_bp;
				break;
			case WIN0_CTRL0:
				win0->state = val & m_WIN0_EN;
				win0->fmt_cfg = (val & m_WIN0_DATA_FMT) >> 1;
				win0->fmt_10 = (val & m_WIN0_FMT_10) >> 4;
				win0->format = win0->fmt_cfg;
				break;
			case WIN0_VIR:
				win0->area[0].y_vir_stride =
					val & m_WIN0_VIR_STRIDE;
				win0->area[0].uv_vir_stride =
					(val & m_WIN0_VIR_STRIDE_UV) >> 16;
				if (win0->format == ARGB888)
					win0->area[0].xvir =
						win0->area[0].y_vir_stride;
				else if (win0->format == RGB888)
					win0->area[0].xvir =
						win0->area[0].y_vir_stride * 4 / 3;
				else if (win0->format == RGB565)
					win0->area[0].xvir =
						2 * win0->area[0].y_vir_stride;
				else /* YUV */
					win0->area[0].xvir =
						4 * win0->area[0].y_vir_stride;
				break;
			case WIN0_YRGB_MST:
				win0->area[0].smem_start = val;
				break;
			case WIN0_CBR_MST:
				win0->area[0].cbr_start = val;
				break;
			default:
				break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
}

/********do basic init*********/
static int rk3288_lcdc_pre_init(struct rk_lcdc_driver *dev_drv)
{
	int v;
	u32 mask,val;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
							   struct
							   lcdc_device,
						   driver);
	if (lcdc_dev->pre_init)
		return 0;

	lcdc_dev->hclk = devm_clk_get(lcdc_dev->dev, "hclk_lcdc");
	lcdc_dev->aclk = devm_clk_get(lcdc_dev->dev, "aclk_lcdc");
	lcdc_dev->dclk = devm_clk_get(lcdc_dev->dev, "dclk_lcdc");
	lcdc_dev->pd   = devm_clk_get(lcdc_dev->dev, "pd_lcdc");
	
	if (IS_ERR(lcdc_dev->pd) || (IS_ERR(lcdc_dev->aclk)) ||
	    (IS_ERR(lcdc_dev->dclk)) || (IS_ERR(lcdc_dev->hclk))) {
		dev_err(lcdc_dev->dev, "failed to get lcdc%d clk source\n",
			lcdc_dev->id);
	}

	rk_disp_pwr_enable(dev_drv);
	rk3288_lcdc_clk_enable(lcdc_dev);

	/*backup reg config at uboot*/
	lcdc_read_reg_defalut_cfg(lcdc_dev);
#ifndef CONFIG_RK_FPGA
	if (lcdc_dev->pwr18 == true) {
		v = 0x00010001;	/*bit14: 1,1.8v;0,3.3v*/
		writel_relaxed(v, RK_GRF_VIRT + RK3288_GRF_IO_VSEL);
	} else {
		v = 0x00010000;
		writel_relaxed(v, RK_GRF_VIRT + RK3288_GRF_IO_VSEL);
	}
#endif	
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE0_0,0x15110903);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE0_1,0x00030911);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE1_0,0x1a150b04);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE1_1,0x00040b15);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE2_0,0x15110903);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE2_1,0x00030911);

	lcdc_writel(lcdc_dev,FRC_LOWER01_0,0x12844821);
	lcdc_writel(lcdc_dev,FRC_LOWER01_1,0x21488412);
	lcdc_writel(lcdc_dev,FRC_LOWER10_0,0xa55a9696);
	lcdc_writel(lcdc_dev,FRC_LOWER10_1,0x5aa56969);
	lcdc_writel(lcdc_dev,FRC_LOWER11_0,0xdeb77deb);
	lcdc_writel(lcdc_dev,FRC_LOWER11_1,0xed7bb7de);

	mask =  m_AUTO_GATING_EN;
	val  =  v_AUTO_GATING_EN(0);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask,val);
	lcdc_cfg_done(lcdc_dev);
	if (dev_drv->iommu_enabled) /*disable win0 to workaround iommu pagefault*/
		win0_enable(lcdc_dev, 0);
	lcdc_dev->pre_init = true;


	return 0;
}

static void rk3288_lcdc_deint(struct lcdc_device *lcdc_dev)
{

	
	rk3288_lcdc_disable_irq(lcdc_dev);
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_dev->clk_on = 0;
		lcdc_set_bit(lcdc_dev, SYS_CTRL, m_STANDBY_EN);
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
	}
	mdelay(1);
}
static int rk3288_lcdc_post_cfg(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u32 mask, val;
	u16 h_total,v_total;
	u16 post_hsd_en,post_vsd_en;
	u16 post_dsp_hact_st,post_dsp_hact_end;	
	u16 post_dsp_vact_st,post_dsp_vact_end;
	u16 post_dsp_vact_st_f1,post_dsp_vact_end_f1;
	u16 post_h_fac,post_v_fac;

	h_total = screen->mode.hsync_len+screen->mode.left_margin +
		  x_res + screen->mode.right_margin;
	v_total = screen->mode.vsync_len+screen->mode.upper_margin +
		  y_res + screen->mode.lower_margin;

	if(screen->post_dsp_stx + screen->post_xsize > x_res){		
		dev_warn(lcdc_dev->dev, "post:stx[%d] + xsize[%d] > x_res[%d]\n",
			screen->post_dsp_stx,screen->post_xsize,x_res);
		screen->post_dsp_stx = x_res - screen->post_xsize;
	}
	if(screen->x_mirror == 0){
		post_dsp_hact_st=screen->post_dsp_stx + 
			screen->mode.hsync_len+screen->mode.left_margin;
		post_dsp_hact_end = post_dsp_hact_st + screen->post_xsize;
	}else{
		post_dsp_hact_end = h_total - screen->mode.right_margin -
					screen->post_dsp_stx;
		post_dsp_hact_st = post_dsp_hact_end - screen->post_xsize;
	}	
	if((screen->post_xsize < x_res)&&(screen->post_xsize != 0)){
		post_hsd_en = 1;
		post_h_fac = 
			GET_SCALE_FACTOR_BILI_DN(x_res , screen->post_xsize); 
	}else{
		post_hsd_en = 0;
		post_h_fac = 0x1000;
	}


	if(screen->post_dsp_sty + screen->post_ysize > y_res){
		dev_warn(lcdc_dev->dev, "post:sty[%d] + ysize[%d] > y_res[%d]\n",
			screen->post_dsp_sty,screen->post_ysize,y_res);
		screen->post_dsp_sty = y_res - screen->post_ysize;	
	}
	
	if(screen->y_mirror == 0){
		post_dsp_vact_st = screen->post_dsp_sty + 
			screen->mode.vsync_len+screen->mode.upper_margin;
		post_dsp_vact_end = post_dsp_vact_st + screen->post_ysize;
	}else{
		post_dsp_vact_end = v_total - screen->mode.lower_margin -
					- screen->post_dsp_sty;
		post_dsp_vact_st = post_dsp_vact_end - screen->post_ysize;
	}
	if((screen->post_ysize < y_res)&&(screen->post_ysize != 0)){
		post_vsd_en = 1;
		post_v_fac = GET_SCALE_FACTOR_BILI_DN(y_res, screen->post_ysize);		
	}else{
		post_vsd_en = 0;
		post_v_fac = 0x1000;
	}

	if(screen->interlace == 1){
		post_dsp_vact_st_f1  = v_total + post_dsp_vact_st;
		post_dsp_vact_end_f1 = post_dsp_vact_st_f1 + screen->post_ysize;
	}else{
		post_dsp_vact_st_f1  = 0;
		post_dsp_vact_end_f1 = 0;
	}
	DBG(1,"post:xsize=%d,ysize=%d,xpos=%d,ypos=%d,"
	      "hsd_en=%d,h_fac=%d,vsd_en=%d,v_fac=%d\n",
		screen->post_xsize,screen->post_ysize,screen->xpos,screen->ypos,
		post_hsd_en,post_h_fac,post_vsd_en,post_v_fac);
	mask = m_DSP_HACT_END_POST | m_DSP_HACT_ST_POST;
	val = v_DSP_HACT_END_POST(post_dsp_hact_end) | 
	      v_DSP_HACT_ST_POST(post_dsp_hact_st);
	lcdc_msk_reg(lcdc_dev, POST_DSP_HACT_INFO, mask, val);

	mask = m_DSP_VACT_END_POST | m_DSP_VACT_ST_POST;
	val = v_DSP_VACT_END_POST(post_dsp_vact_end) | 
	      v_DSP_VACT_ST_POST(post_dsp_vact_st);
	lcdc_msk_reg(lcdc_dev, POST_DSP_VACT_INFO, mask, val);

	mask = m_POST_HS_FACTOR_YRGB | m_POST_VS_FACTOR_YRGB;
	val = v_POST_HS_FACTOR_YRGB(post_h_fac) |
		v_POST_VS_FACTOR_YRGB(post_v_fac);
	lcdc_msk_reg(lcdc_dev, POST_SCL_FACTOR_YRGB, mask, val);

	mask = m_DSP_VACT_END_POST_F1 | m_DSP_VACT_ST_POST_F1;
	val = v_DSP_VACT_END_POST_F1(post_dsp_vact_end_f1) |
		v_DSP_VACT_ST_POST_F1(post_dsp_vact_st_f1);
	lcdc_msk_reg(lcdc_dev, POST_DSP_VACT_INFO_F1, mask, val);

	mask = m_POST_HOR_SD_EN | m_POST_VER_SD_EN;
	val = v_POST_HOR_SD_EN(post_hsd_en) | v_POST_VER_SD_EN(post_vsd_en);
	lcdc_msk_reg(lcdc_dev, POST_SCL_CTRL, mask, val);
	return 0;
}

static int rk3288_lcdc_clr_key_cfg(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
							   struct
							   lcdc_device,
							   driver);
	struct rk_lcdc_win *win;
	u32  colorkey_r,colorkey_g,colorkey_b;
	int i,key_val;
	for(i=0;i<4;i++){
		win = dev_drv->win[i];
		key_val = win->color_key_val;
		colorkey_r = (key_val & 0xff)<<2;
		colorkey_g = ((key_val>>8)&0xff)<<12;
		colorkey_b = ((key_val>>16)&0xff)<<22;
		/*color key dither 565/888->aaa*/
		key_val = colorkey_r | colorkey_g | colorkey_b;
		switch(i){
		case 0:
			lcdc_writel(lcdc_dev, WIN0_COLOR_KEY, key_val);
			break;
		case 1:
			lcdc_writel(lcdc_dev, WIN1_COLOR_KEY, key_val);
			break;
		case 2:
			lcdc_writel(lcdc_dev, WIN2_COLOR_KEY, key_val);
			break;
		case 3:
			lcdc_writel(lcdc_dev, WIN3_COLOR_KEY, key_val);
			break;
		default:
			printk(KERN_WARNING "%s:un support win num:%d\n",
				__func__,i);		
			break;
		}
	}
	return 0;
}

static int rk3288_lcdc_alpha_cfg(struct rk_lcdc_driver *dev_drv,int win_id)
{
	struct lcdc_device *lcdc_dev =
		container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	struct alpha_config alpha_config;

	u32 mask, val;
	int ppixel_alpha,global_alpha;
	u32 src_alpha_ctl,dst_alpha_ctl;
	ppixel_alpha = ((win->format == ARGB888)||(win->format == ABGR888)) ? 1 : 0;
	global_alpha = (win->g_alpha_val == 0) ? 0 : 1; 
	alpha_config.src_global_alpha_val = win->g_alpha_val;
	win->alpha_mode = AB_SRC_OVER;
	/*printk("%s,alpha_mode=%d,alpha_en=%d,ppixel_a=%d,gla_a=%d\n",
		__func__,win->alpha_mode,win->alpha_en,ppixel_alpha,global_alpha);*/
	switch(win->alpha_mode){
	case AB_USER_DEFINE:
		break;
 	case AB_CLEAR:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_ZERO;		
		break;
 	case AB_SRC:
		alpha_config.src_factor_mode=AA_ONE;
		alpha_config.dst_factor_mode=AA_ZERO;
		break;
 	case AB_DST:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_ONE;
		break;
 	case AB_SRC_OVER:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		if(global_alpha)
			alpha_config.src_factor_mode=AA_SRC_GLOBAL;
		else
			alpha_config.src_factor_mode=AA_ONE;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;		
		break;
 	case AB_DST_OVER:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_ONE;
		break;
 	case AB_SRC_IN:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC;
		alpha_config.dst_factor_mode=AA_ZERO;
		break;
 	case AB_DST_IN:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_SRC;
		break;
 	case AB_SRC_OUT:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_ZERO;		
		break;
 	case AB_DST_OUT:
		alpha_config.src_factor_mode=AA_ZERO;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;	
		break;
 	case AB_SRC_ATOP:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;		
		break;
 	case AB_DST_ATOP:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_SRC;		
		break;
 	case XOR:
		alpha_config.src_color_mode=AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_INVERSE;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;			
		break;	
 	case AB_SRC_OVER_GLOBAL:	
		alpha_config.src_global_alpha_mode=AA_PER_PIX_GLOBAL;
		alpha_config.src_color_mode=AA_SRC_NO_PRE_MUL;
		alpha_config.src_factor_mode=AA_SRC_GLOBAL;
		alpha_config.dst_factor_mode=AA_SRC_INVERSE;
		break;
	default:
	    	pr_err("alpha mode error\n");
      		break;		
	}
	if((ppixel_alpha == 1)&&(global_alpha == 1)){
		alpha_config.src_global_alpha_mode = AA_PER_PIX_GLOBAL;
	}else if(ppixel_alpha == 1){
		alpha_config.src_global_alpha_mode = AA_PER_PIX;
	}else if(global_alpha == 1){
		alpha_config.src_global_alpha_mode = AA_GLOBAL;
	}else{
		dev_warn(lcdc_dev->dev,"alpha_en should be 0\n");
	}
	alpha_config.src_alpha_mode = AA_STRAIGHT;
	alpha_config.src_alpha_cal_m0 = AA_NO_SAT;

	switch(win_id){
	case 0:
		src_alpha_ctl = 0x60;
		dst_alpha_ctl = 0x64;
		break;
	case 1:
		src_alpha_ctl = 0xa0;
		dst_alpha_ctl = 0xa4;
		break;
	case 2:
		src_alpha_ctl = 0xdc;
		dst_alpha_ctl = 0xec;
		break;
	case 3:
		src_alpha_ctl = 0x12c;
		dst_alpha_ctl = 0x13c;
		break;
	}
	mask = m_WIN0_DST_FACTOR_M0;
	val  = v_WIN0_DST_FACTOR_M0(alpha_config.dst_factor_mode);
	lcdc_msk_reg(lcdc_dev, dst_alpha_ctl, mask, val);
	mask = m_WIN0_SRC_ALPHA_EN | m_WIN0_SRC_COLOR_M0 |
		m_WIN0_SRC_ALPHA_M0 | m_WIN0_SRC_BLEND_M0 |
		m_WIN0_SRC_ALPHA_CAL_M0 | m_WIN0_SRC_FACTOR_M0|
		m_WIN0_SRC_GLOBAL_ALPHA;
	val = v_WIN0_SRC_ALPHA_EN(1) | 
		v_WIN0_SRC_COLOR_M0(alpha_config.src_color_mode) |
		v_WIN0_SRC_ALPHA_M0(alpha_config.src_alpha_mode) |
		v_WIN0_SRC_BLEND_M0(alpha_config.src_global_alpha_mode) |
		v_WIN0_SRC_ALPHA_CAL_M0(alpha_config.src_alpha_cal_m0) |
		v_WIN0_SRC_FACTOR_M0(alpha_config.src_factor_mode) |
		v_WIN0_SRC_GLOBAL_ALPHA(alpha_config.src_global_alpha_val);
	lcdc_msk_reg(lcdc_dev, src_alpha_ctl, mask, val);

	return 0;
}
static int rk3288_lcdc_area_swap(struct rk_lcdc_win *win,int area_num)
{
	struct rk_lcdc_win_area area_temp;
	switch(area_num){
	case 2:
		area_temp = win->area[0];
		win->area[0] = win->area[1];
		win->area[1] = area_temp;
		break;
	case 3:
		area_temp = win->area[0];
		win->area[0] = win->area[2];
		win->area[2] = area_temp;
		break;
	case 4:
		area_temp = win->area[0];
		win->area[0] = win->area[3];
		win->area[3] = area_temp;
		
		area_temp = win->area[1];
		win->area[1] = win->area[2];
		win->area[2] = area_temp;	
		break;
	default:
		printk(KERN_WARNING "un supported area num!\n");
		break;
	}
	return 0;
}

static int rk3288_win_area_check_var(int win_id,int area_num,struct rk_lcdc_win_area *area_pre,
			struct rk_lcdc_win_area *area_now)
{
	if((area_pre->ypos >= area_now->ypos) ||
		(area_pre->ypos+area_pre->ysize > area_now->ypos)){
		area_now->state = 0;
		pr_err("win[%d]:\n"
			"area_pre[%d]:ypos[%d],ysize[%d]\n"
			"area_now[%d]:ypos[%d],ysize[%d]\n",
			win_id,
			area_num-1,area_pre->ypos,area_pre->ysize,
			area_num,  area_now->ypos,area_now->ysize);
		return -EINVAL;
	}
	return 0;
}

static int rk3288_win_0_1_reg_update(struct rk_lcdc_driver *dev_drv,int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	unsigned int mask, val, off;
	off = win_id * 0x40;
	if(win->win_lb_mode == 5)
		win->win_lb_mode = 4;

	if(win->state == 1){
		mask =  m_WIN0_EN | m_WIN0_DATA_FMT | m_WIN0_FMT_10 |
			m_WIN0_LB_MODE | m_WIN0_RB_SWAP;
		val  =  v_WIN0_EN(win->state) | v_WIN0_DATA_FMT(win->fmt_cfg) |
			v_WIN0_FMT_10(win->fmt_10) | 
			v_WIN0_LB_MODE(win->win_lb_mode) | 
			v_WIN0_RB_SWAP(win->swap_rb);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0+off, mask,val);	
	
		mask =	m_WIN0_BIC_COE_SEL |
			m_WIN0_VSD_YRGB_GT4 | m_WIN0_VSD_YRGB_GT2 |
			m_WIN0_VSD_CBR_GT4 | m_WIN0_VSD_CBR_GT2 |
			m_WIN0_YRGB_HOR_SCL_MODE | m_WIN0_YRGB_VER_SCL_MODE |
			m_WIN0_YRGB_HSD_MODE | m_WIN0_YRGB_VSU_MODE |
			m_WIN0_YRGB_VSD_MODE | m_WIN0_CBR_HOR_SCL_MODE |
			m_WIN0_CBR_VER_SCL_MODE | m_WIN0_CBR_HSD_MODE |
			m_WIN0_CBR_VSU_MODE | m_WIN0_CBR_VSD_MODE;
		val =	v_WIN0_BIC_COE_SEL(win->bic_coe_el) |
			v_WIN0_VSD_YRGB_GT4(win->vsd_yrgb_gt4) |
			v_WIN0_VSD_YRGB_GT2(win->vsd_yrgb_gt2) |
			v_WIN0_VSD_CBR_GT4(win->vsd_cbr_gt4) |
			v_WIN0_VSD_CBR_GT2(win->vsd_cbr_gt2) |
			v_WIN0_YRGB_HOR_SCL_MODE(win->yrgb_hor_scl_mode) |
			v_WIN0_YRGB_VER_SCL_MODE(win->yrgb_ver_scl_mode) |
			v_WIN0_YRGB_HSD_MODE(win->yrgb_hsd_mode) |
			v_WIN0_YRGB_VSU_MODE(win->yrgb_vsu_mode) |
			v_WIN0_YRGB_VSD_MODE(win->yrgb_vsd_mode) |
			v_WIN0_CBR_HOR_SCL_MODE(win->cbr_hor_scl_mode) |
			v_WIN0_CBR_VER_SCL_MODE(win->cbr_ver_scl_mode) |
			v_WIN0_CBR_HSD_MODE(win->cbr_hsd_mode) |
			v_WIN0_CBR_VSU_MODE(win->cbr_vsu_mode) |
			v_WIN0_CBR_VSD_MODE(win->cbr_vsd_mode);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL1+off, mask,val);
	
		val =	v_WIN0_VIR_STRIDE(win->area[0].y_vir_stride) |
			v_WIN0_VIR_STRIDE_UV(win->area[0].uv_vir_stride);	
		lcdc_writel(lcdc_dev, WIN0_VIR+off, val);	
		/*lcdc_writel(lcdc_dev, WIN0_YRGB_MST+off, win->area[0].y_addr); 
		lcdc_writel(lcdc_dev, WIN0_CBR_MST+off, win->area[0].uv_addr);*/
		val =	v_WIN0_ACT_WIDTH(win->area[0].xact) |
			v_WIN0_ACT_HEIGHT(win->area[0].yact);
		lcdc_writel(lcdc_dev, WIN0_ACT_INFO+off, val); 
	
		val =	v_WIN0_DSP_WIDTH(win->area[0].xsize) |
			v_WIN0_DSP_HEIGHT(win->area[0].ysize);
		lcdc_writel(lcdc_dev, WIN0_DSP_INFO+off, val); 
	
		val =	v_WIN0_DSP_XST(win->area[0].dsp_stx) |
			v_WIN0_DSP_YST(win->area[0].dsp_sty);
		lcdc_writel(lcdc_dev, WIN0_DSP_ST+off, val); 
	
		val =	v_WIN0_HS_FACTOR_YRGB(win->scale_yrgb_x) |
			v_WIN0_VS_FACTOR_YRGB(win->scale_yrgb_y);
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_YRGB+off, val); 
	
		val =	v_WIN0_HS_FACTOR_CBR(win->scale_cbcr_x) |
			v_WIN0_VS_FACTOR_CBR(win->scale_cbcr_y);
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_CBR+off, val); 
		if(win->alpha_en == 1)
			rk3288_lcdc_alpha_cfg(dev_drv,win_id);
		else{
			mask = m_WIN0_SRC_ALPHA_EN;
			val = v_WIN0_SRC_ALPHA_EN(0);
			lcdc_msk_reg(lcdc_dev,WIN0_SRC_ALPHA_CTRL+off,mask,val);				
		}
		/*offset*/	
	}else{
		mask = m_WIN0_EN;
		val = v_WIN0_EN(win->state);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0+off, mask,val); 
	}
	return 0;
}

static int rk3288_win_2_3_reg_update(struct rk_lcdc_driver *dev_drv,int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	struct rk_screen *screen = dev_drv->cur_screen;
	unsigned int mask, val, off;
	off = (win_id-2) * 0x50;
	if((screen->y_mirror == 1)&&(win->area_num > 1)){
		rk3288_lcdc_area_swap(win,win->area_num);
	}
	
	if(win->state == 1){
		mask =  m_WIN2_EN | m_WIN2_DATA_FMT | m_WIN2_RB_SWAP;
		val  =  v_WIN2_EN(1) | v_WIN2_DATA_FMT(win->fmt_cfg) |
			v_WIN2_RB_SWAP(win->swap_rb);	
		lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);
		/*area 0*/
		if(win->area[0].state == 1){
			mask = m_WIN2_MST0_EN;
			val  = v_WIN2_MST0_EN(win->area[0].state);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);

			mask = m_WIN2_VIR_STRIDE0;
			val  = v_WIN2_VIR_STRIDE0(win->area[0].y_vir_stride);
			lcdc_msk_reg(lcdc_dev,WIN2_VIR0_1+off,mask,val);

			/*lcdc_writel(lcdc_dev,WIN2_MST0+off,win->area[0].y_addr);*/
			val  = 	v_WIN2_DSP_WIDTH0(win->area[0].xsize) | 
				v_WIN2_DSP_HEIGHT0(win->area[0].ysize);
			lcdc_writel(lcdc_dev,WIN2_DSP_INFO0+off,val);
			val  =	v_WIN2_DSP_XST0(win->area[0].dsp_stx) |
				v_WIN2_DSP_YST0(win->area[0].dsp_sty);
			lcdc_writel(lcdc_dev,WIN2_DSP_ST0+off,val);	
		}else{
			mask = m_WIN2_MST0_EN;
			val  = v_WIN2_MST0_EN(0);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);
		}
		/*area 1*/
		if(win->area[1].state == 1){
			rk3288_win_area_check_var(win_id,1,&win->area[0],&win->area[1]);
			
			mask = m_WIN2_MST1_EN;
			val  = v_WIN2_MST1_EN(win->area[1].state);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);

			mask = m_WIN2_VIR_STRIDE1;
			val  = v_WIN2_VIR_STRIDE1(win->area[1].y_vir_stride);
			lcdc_msk_reg(lcdc_dev,WIN2_VIR0_1+off,mask,val);

			/*lcdc_writel(lcdc_dev,WIN2_MST1+off,win->area[1].y_addr);*/
			val  = 	v_WIN2_DSP_WIDTH1(win->area[1].xsize) | 
				v_WIN2_DSP_HEIGHT1(win->area[1].ysize);
			lcdc_writel(lcdc_dev,WIN2_DSP_INFO1+off,val);
			val  =	v_WIN2_DSP_XST1(win->area[1].dsp_stx) |
				v_WIN2_DSP_YST1(win->area[1].dsp_sty);
			lcdc_writel(lcdc_dev,WIN2_DSP_ST1+off,val);	
		}else{
			mask = m_WIN2_MST1_EN;
			val  = v_WIN2_MST1_EN(0);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);
		}
		/*area 2*/
		if(win->area[2].state == 1){
			rk3288_win_area_check_var(win_id,2,&win->area[1],&win->area[2]);
			
			mask = m_WIN2_MST2_EN;
			val  = v_WIN2_MST2_EN(win->area[2].state);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);

			mask = m_WIN2_VIR_STRIDE2;
			val  = v_WIN2_VIR_STRIDE2(win->area[2].y_vir_stride);
			lcdc_msk_reg(lcdc_dev,WIN2_VIR2_3+off,mask,val);

			/*lcdc_writel(lcdc_dev,WIN2_MST2+off,win->area[2].y_addr);*/
			val  = 	v_WIN2_DSP_WIDTH2(win->area[2].xsize) | 
				v_WIN2_DSP_HEIGHT2(win->area[2].ysize);
			lcdc_writel(lcdc_dev,WIN2_DSP_INFO2+off,val);
			val  =	v_WIN2_DSP_XST2(win->area[2].dsp_stx) |
				v_WIN2_DSP_YST2(win->area[2].dsp_sty);
			lcdc_writel(lcdc_dev,WIN2_DSP_ST2+off,val);	
		}else{
			mask = m_WIN2_MST2_EN;
			val  = v_WIN2_MST2_EN(0);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);
		}
		/*area 3*/
		if(win->area[3].state == 1){
			rk3288_win_area_check_var(win_id,3,&win->area[2],&win->area[3]);
			
			mask = m_WIN2_MST3_EN;
			val  = v_WIN2_MST3_EN(win->area[3].state);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);

			mask = m_WIN2_VIR_STRIDE3;
			val  = v_WIN2_VIR_STRIDE3(win->area[3].y_vir_stride);
			lcdc_msk_reg(lcdc_dev,WIN2_VIR2_3+off,mask,val);

			/*lcdc_writel(lcdc_dev,WIN2_MST3+off,win->area[3].y_addr);*/
			val  = 	v_WIN2_DSP_WIDTH3(win->area[3].xsize) | 
				v_WIN2_DSP_HEIGHT3(win->area[3].ysize);
			lcdc_writel(lcdc_dev,WIN2_DSP_INFO3+off,val);
			val  =	v_WIN2_DSP_XST3(win->area[3].dsp_stx) |
				v_WIN2_DSP_YST3(win->area[3].dsp_sty);
			lcdc_writel(lcdc_dev,WIN2_DSP_ST3+off,val);	
		}else{
			mask = m_WIN2_MST3_EN;
			val  = v_WIN2_MST3_EN(0);
			lcdc_msk_reg(lcdc_dev,WIN2_CTRL0+off,mask,val);
		}	

		if(win->alpha_en == 1)
			rk3288_lcdc_alpha_cfg(dev_drv,win_id);
		else{
			mask = m_WIN2_SRC_ALPHA_EN;
			val = v_WIN2_SRC_ALPHA_EN(0);
			lcdc_msk_reg(lcdc_dev,WIN2_SRC_ALPHA_CTRL+off,mask,val);				
		}
	}else{
		mask =  m_WIN2_EN | m_WIN2_MST0_EN |
			m_WIN2_MST0_EN | m_WIN2_MST2_EN |
			m_WIN2_MST3_EN;
		val  =  v_WIN2_EN(win->state) | v_WIN2_MST0_EN(0) |
			v_WIN2_MST1_EN(0) | v_WIN2_MST2_EN(0) |
			v_WIN2_MST3_EN(0);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL0+off, mask,val); 
	}
	return 0;
}

static int rk3288_lcdc_reg_update(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int timeout;
	unsigned long flags;

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on))
	{
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN,
			     v_STANDBY_EN(lcdc_dev->standby));
		rk3288_win_0_1_reg_update(dev_drv,0);
		rk3288_win_0_1_reg_update(dev_drv,1);
		rk3288_win_2_3_reg_update(dev_drv,2);
		rk3288_win_2_3_reg_update(dev_drv,3);
		/*rk3288_lcdc_post_cfg(dev_drv);*/
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
	/*if (dev_drv->wait_fs) {*/
	if (0){
		spin_lock_irqsave(&dev_drv->cpl_lock, flags);
		init_completion(&dev_drv->frame_done);
		spin_unlock_irqrestore(&dev_drv->cpl_lock, flags);
		timeout = wait_for_completion_timeout(&dev_drv->frame_done,
						      msecs_to_jiffies
						      (dev_drv->cur_screen->ft +
						       5));
		if (!timeout && (!dev_drv->frame_done.done)) {
			dev_warn(lcdc_dev->dev, "wait for new frame start time out!\n");
			return -ETIMEDOUT;
		}
	}
	DBG(2, "%s for lcdc%d\n", __func__, lcdc_dev->id);
	return 0;

}

static int rk3288_lcdc_reg_restore(struct lcdc_device *lcdc_dev)
{
	if (lcdc_dev->driver.iommu_enabled)
		memcpy((u8 *) lcdc_dev->regs, (u8 *) lcdc_dev->regsbak, 0x330);
	else
		memcpy((u8 *) lcdc_dev->regs, (u8 *) lcdc_dev->regsbak, 0x1fc);
	return 0;
}
static int rk3288_lcdc_mmu_en(struct rk_lcdc_driver *dev_drv)
{
	u32 mask,val;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		mask = m_MMU_EN;
		val = v_MMU_EN(1);
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
		mask = m_AXI_MAX_OUTSTANDING_EN | m_AXI_OUTSTANDING_MAX_NUM;
		val = v_AXI_OUTSTANDING_MAX_NUM(31) | v_AXI_MAX_OUTSTANDING_EN(1);
		lcdc_msk_reg(lcdc_dev, SYS_CTRL1, mask, val);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3288_lcdc_set_dclk(struct rk_lcdc_driver *dev_drv)
{
#ifdef CONFIG_RK_FPGA
	return 0;
#endif
	int ret,fps;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;

	ret = clk_set_rate(lcdc_dev->dclk, screen->mode.pixclock);
	if (ret)
		dev_err(dev_drv->dev, "set lcdc%d dclk failed\n", lcdc_dev->id);
	lcdc_dev->pixclock =
		 div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	lcdc_dev->driver.pixclock = lcdc_dev->pixclock;
	
	fps = rk_fb_calc_fps(screen, lcdc_dev->pixclock);
	screen->ft = 1000 / fps;
	dev_info(lcdc_dev->dev, "%s: dclk:%lu>>fps:%d ",
		 lcdc_dev->driver.name, clk_get_rate(lcdc_dev->dclk), fps);
	return 0;

}

static int rk3288_load_screen(struct rk_lcdc_driver *dev_drv, bool initscreen)
{
	u16 face = 0;
	u32 v=0;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u16 hsync_len = screen->mode.hsync_len;
	u16 left_margin = screen->mode.left_margin;
	u16 right_margin = screen->mode.right_margin;
	u16 vsync_len = screen->mode.vsync_len;
	u16 upper_margin = screen->mode.upper_margin;
	u16 lower_margin = screen->mode.lower_margin;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u32 mask, val;
	u16 h_total,v_total;
	
	h_total = hsync_len + left_margin  + x_res + right_margin;
	v_total = vsync_len + upper_margin + y_res + lower_margin;

	screen->post_dsp_stx = x_res * (100 - screen->overscan.left) / 200;
	screen->post_dsp_sty = y_res * (100 - screen->overscan.top) / 200;
	screen->post_xsize = x_res * (screen->overscan.left + screen->overscan.right) / 200;
	screen->post_ysize = y_res * (screen->overscan.top + screen->overscan.bottom) / 200;
	
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		switch (screen->face) {
		case OUT_P565:
			face = OUT_P565;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
			    v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_P666:
			face = OUT_P666;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
			    v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_D888_P565:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
			    v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_D888_P666:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
			    v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_P888:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_UP_EN;
			val = v_DITHER_DOWN_EN(0) | v_DITHER_UP_EN(0);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		default:
			dev_err(lcdc_dev->dev,"un supported interface!\n");
			break;
		}
		switch(screen->type){
		case SCREEN_RGB:
		case SCREEN_LVDS:
		case SCREEN_DUAL_LVDS:			
			mask = m_RGB_OUT_EN;
			val = v_RGB_OUT_EN(1);
			v = 1 << (3+16);
			v |= (lcdc_dev->id << 3);
			break;
		case SCREEN_HDMI:
			face = OUT_RGB_AAA;
			mask = m_HDMI_OUT_EN;
			val = v_HDMI_OUT_EN(1); 	
			break;
		case SCREEN_MIPI:
			mask = m_MIPI_OUT_EN;
			val = v_MIPI_OUT_EN(1); 		
			break;
		case SCREEN_DUAL_MIPI:
			mask = m_MIPI_OUT_EN | m_DOUB_CHANNEL_EN;
			val = v_MIPI_OUT_EN(1) | v_DOUB_CHANNEL_EN(1); 	
			break;
		case SCREEN_EDP:
			face = OUT_RGB_AAA;  /*RGB AAA output*/
			mask = m_DITHER_DOWN_EN | m_DITHER_UP_EN;
			val = v_DITHER_DOWN_EN(0) | v_DITHER_UP_EN(0);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			mask = m_EDP_OUT_EN;
			val = v_EDP_OUT_EN(1); 		
			break;
		}
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
#ifndef CONFIG_RK_FPGA
		writel_relaxed(v, RK_GRF_VIRT + RK3288_GRF_SOC_CON6);
#endif		
		mask = m_DSP_OUT_MODE | m_DSP_HSYNC_POL | m_DSP_VSYNC_POL |
		       m_DSP_DEN_POL | m_DSP_DCLK_POL | m_DSP_BG_SWAP | 
		       m_DSP_RB_SWAP | m_DSP_RG_SWAP | m_DSP_DELTA_SWAP |
		       m_DSP_DUMMY_SWAP | m_DSP_OUT_ZERO | m_DSP_BLANK_EN | 
		       m_DSP_BLACK_EN | m_DSP_X_MIR_EN | m_DSP_Y_MIR_EN;
		val = v_DSP_OUT_MODE(face) | v_DSP_HSYNC_POL(screen->pin_hsync) |
		      v_DSP_VSYNC_POL(screen->pin_vsync) | 
		      v_DSP_DEN_POL(screen->pin_den) | v_DSP_DCLK_POL(screen->pin_dclk) |
		      v_DSP_BG_SWAP(screen->swap_gb) | v_DSP_RB_SWAP(screen->swap_rb) | 
		      v_DSP_RG_SWAP(screen->swap_rg) | 
		      v_DSP_DELTA_SWAP(screen->swap_delta) |
		      v_DSP_DUMMY_SWAP(screen->swap_dumy) | v_DSP_OUT_ZERO(0) | 
		      v_DSP_BLANK_EN(0) | v_DSP_BLACK_EN(0) |
		      v_DSP_X_MIR_EN(screen->x_mirror) | v_DSP_Y_MIR_EN(screen->y_mirror);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);

		mask = m_DSP_BG_BLUE | m_DSP_BG_GREEN | m_DSP_BG_RED;
		val  = v_DSP_BG_BLUE(0) | v_DSP_BG_GREEN(0) | v_DSP_BG_RED(0);
		lcdc_msk_reg(lcdc_dev, DSP_BG, mask, val);

		mask = m_DSP_HS_PW | m_DSP_HTOTAL;
		val = v_DSP_HS_PW(hsync_len) | v_DSP_HTOTAL(h_total);
		lcdc_msk_reg(lcdc_dev, DSP_HTOTAL_HS_END, mask, val);

		mask = m_DSP_HACT_END | m_DSP_HACT_ST;
		val = v_DSP_HACT_END(hsync_len + left_margin + x_res) |
		    v_DSP_HACT_ST(hsync_len + left_margin);
		lcdc_msk_reg(lcdc_dev, DSP_HACT_ST_END, mask, val);

		mask = m_DSP_VS_PW | m_DSP_VTOTAL;
		val = v_DSP_VS_PW(vsync_len) | v_DSP_VTOTAL(v_total);
		lcdc_msk_reg(lcdc_dev, DSP_VTOTAL_VS_END, mask, val);

		mask = m_DSP_VACT_END | m_DSP_VACT_ST;
		val = v_DSP_VACT_END(vsync_len + upper_margin + y_res) |
		    v_DSP_VACT_ST(vsync_len + upper_margin);
		lcdc_msk_reg(lcdc_dev, DSP_VACT_ST_END, mask, val);

		rk3288_lcdc_post_cfg(dev_drv);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	rk3288_lcdc_set_dclk(dev_drv);
	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();
	if (screen->init)
		screen->init();
	
	return 0;
}

/*enable layer,open:1,enable;0 disable*/
static int win0_open(struct lcdc_device *lcdc_dev, bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		if (open) {
			if (!lcdc_dev->atv_layer_cnt) {
				dev_info(lcdc_dev->dev, "wakeup from standby!\n");
				lcdc_dev->standby = 0;
			}
			lcdc_dev->atv_layer_cnt++;
		} else if ((lcdc_dev->atv_layer_cnt > 0) && (!open)) {
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.win[0]->state = open;
		if (!lcdc_dev->atv_layer_cnt) {
			dev_info(lcdc_dev->dev, "no layer is used,go to standby!\n");
			lcdc_dev->standby = 1;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int win1_open(struct lcdc_device *lcdc_dev, bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		if (open) {
			if (!lcdc_dev->atv_layer_cnt) {
				dev_info(lcdc_dev->dev, "wakeup from standby!\n");
				lcdc_dev->standby = 0;
			}
			lcdc_dev->atv_layer_cnt++;
		} else if ((lcdc_dev->atv_layer_cnt > 0) && (!open)) {
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.win[1]->state = open;

		/*if no layer used,disable lcdc*/
		if (!lcdc_dev->atv_layer_cnt) {
			dev_info(lcdc_dev->dev, "no layer is used,go to standby!\n");
			lcdc_dev->standby = 1;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int win2_open(struct lcdc_device *lcdc_dev, bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		if (open) {
			if (!lcdc_dev->atv_layer_cnt) {
				dev_info(lcdc_dev->dev, "wakeup from standby!\n");
				lcdc_dev->standby = 0;
			}
			lcdc_dev->atv_layer_cnt++;
		} else if ((lcdc_dev->atv_layer_cnt > 0) && (!open)) {
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.win[2]->state = open;

		/*if no layer used,disable lcdc*/
		if (!lcdc_dev->atv_layer_cnt) {
			dev_info(lcdc_dev->dev, "no layer is used,go to standby!\n");
			lcdc_dev->standby = 1;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int win3_open(struct lcdc_device *lcdc_dev, bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		if (open) {
			if (!lcdc_dev->atv_layer_cnt) {
				dev_info(lcdc_dev->dev, "wakeup from standby!\n");
				lcdc_dev->standby = 0;
			}
			lcdc_dev->atv_layer_cnt++;
		} else if ((lcdc_dev->atv_layer_cnt > 0) && (!open)) {
			lcdc_dev->atv_layer_cnt--;
		}
		lcdc_dev->driver.win[3]->state = open;

		/*if no layer used,disable lcdc*/
		if (!lcdc_dev->atv_layer_cnt) {
			dev_info(lcdc_dev->dev, "no layer is used,go to standby!\n");
			lcdc_dev->standby = 1;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}
static int rk3288_lcdc_enable_irq(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);
	u32 mask,val;
	struct rk_screen *screen = dev_drv->cur_screen;
	
	mask = m_FS_INTR_CLR | m_FS_INTR_EN | m_LINE_FLAG_INTR_CLR |
			    m_LINE_FLAG_INTR_EN | m_BUS_ERROR_INTR_CLR | 
			    m_BUS_ERROR_INTR_EN | m_DSP_LINE_FLAG_NUM;
	val = v_FS_INTR_CLR(1) | v_FS_INTR_EN(1) | v_LINE_FLAG_INTR_CLR(1) |
	    v_LINE_FLAG_INTR_EN(1) | v_BUS_ERROR_INTR_CLR(1) | v_BUS_ERROR_INTR_EN(0) |
	    v_DSP_LINE_FLAG_NUM(screen->mode.vsync_len + screen->mode.upper_margin +
	    screen->mode.yres);
	lcdc_msk_reg(lcdc_dev, INTR_CTRL0, mask, val);	
#ifdef LCDC_IRQ_EMPTY_DEBUG
		 mask = m_WIN0_EMPTY_INTR_EN | m_WIN1_EMPTY_INTR_EN | m_WIN2_EMPTY_INTR_EN |
			 m_WIN3_EMPTY_INTR_EN |m_HWC_EMPTY_INTR_EN | m_POST_BUF_EMPTY_INTR_EN |
			 m_PWM_GEN_INTR_EN;
		 val = v_WIN0_EMPTY_INTR_EN(1) | v_WIN1_EMPTY_INTR_EN(1) | v_WIN2_EMPTY_INTR_EN(1) |
			 v_WIN3_EMPTY_INTR_EN(1)| v_HWC_EMPTY_INTR_EN(1) | v_POST_BUF_EMPTY_INTR_EN(1) |
			 v_PWM_GEN_INTR_EN(1);
		 lcdc_msk_reg(lcdc_dev, INTR_CTRL1, mask, val);
#endif 	
	return 0;
}

static int rk3288_lcdc_open(struct rk_lcdc_driver *dev_drv, int win_id,
			    bool open)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);
	int sys_status = (dev_drv->id == 0) ?
			SYS_STATUS_LCDC0 : SYS_STATUS_LCDC1;

	/*enable clk,when first layer open */
	if ((open) && (!lcdc_dev->atv_layer_cnt)) {
		rockchip_set_system_status(sys_status);
		rk3288_lcdc_pre_init(dev_drv);
		rk3288_lcdc_clk_enable(lcdc_dev);
#if defined(CONFIG_ROCKCHIP_IOMMU)
		if (dev_drv->iommu_enabled) {
			if (!dev_drv->mmu_dev) {
				dev_drv->mmu_dev =
                                        rk_fb_get_sysmmu_device_by_compatible(dev_drv->mmu_dts_name);
				if (dev_drv->mmu_dev) {
					rk_fb_platform_set_sysmmu(dev_drv->mmu_dev,
					                          dev_drv->dev);
                                } else {
					dev_err(dev_drv->dev,
						"failed to get rockchip iommu device\n");
					return -1;
				}
			}
			if (dev_drv->mmu_dev)
				rockchip_iovmm_activate(dev_drv->dev);
		}
#endif
		rk3288_lcdc_reg_restore(lcdc_dev);
		if (dev_drv->iommu_enabled)
			rk3288_lcdc_mmu_en(dev_drv);
		if ((support_uboot_display()&&(lcdc_dev->prop == PRMRY))) {
			rk3288_lcdc_set_dclk(dev_drv);
			rk3288_lcdc_enable_irq(dev_drv);
		} else {
			rk3288_load_screen(dev_drv, 1);
		}
		if (dev_drv->bcsh.enable)
			rk3288_lcdc_set_bcsh(dev_drv, 1);
		spin_lock(&lcdc_dev->reg_lock);
		if (dev_drv->cur_screen->dsp_lut)
			rk3288_lcdc_set_lut(dev_drv);
		spin_unlock(&lcdc_dev->reg_lock);
	}

	if (win_id == 0)
		win0_open(lcdc_dev, open);
	else if (win_id == 1)
		win1_open(lcdc_dev, open);
	else if (win_id == 2)
		win2_open(lcdc_dev, open);
	else if (win_id == 3)
		win3_open(lcdc_dev, open);
	else
		dev_err(lcdc_dev->dev, "invalid win id:%d\n", win_id);

	/* when all layer closed,disable clk */
	if ((!open) && (!lcdc_dev->atv_layer_cnt)) {
		rk3288_lcdc_disable_irq(lcdc_dev);
		rk3288_lcdc_reg_update(dev_drv);
#if defined(CONFIG_ROCKCHIP_IOMMU)
		if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_deactivate(dev_drv->dev);
		}
#endif
		rk3288_lcdc_clk_disable(lcdc_dev);
		rockchip_clear_system_status(sys_status);
	}

	return 0;
}

static int win0_display(struct lcdc_device *lcdc_dev,
			struct rk_lcdc_win *win)
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = win->area[0].smem_start+win->area[0].y_offset;/*win->smem_start + win->y_offset;*/
	uv_addr = win->area[0].cbr_start + win->area[0].c_offset;
	DBG(2, "lcdc%d>>%s:y_addr:0x%x>>uv_addr:0x%x>>offset:%d\n",
	    lcdc_dev->id, __func__, y_addr, uv_addr,win->area[0].y_offset);
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		win->area[0].y_addr = y_addr;
		win->area[0].uv_addr = uv_addr;	
		lcdc_writel(lcdc_dev, WIN0_YRGB_MST, win->area[0].y_addr); 
		lcdc_writel(lcdc_dev, WIN0_CBR_MST, win->area[0].uv_addr);
		/*lcdc_cfg_done(lcdc_dev);*/
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;

}

static int win1_display(struct lcdc_device *lcdc_dev,
			struct rk_lcdc_win *win)
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = win->area[0].smem_start + win->area[0].y_offset;
	uv_addr = win->area[0].cbr_start + win->area[0].c_offset;
	DBG(2, "lcdc%d>>%s>>y_addr:0x%x>>uv_addr:0x%x\n",
	    lcdc_dev->id, __func__, y_addr, uv_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		win->area[0].y_addr = y_addr;
		win->area[0].uv_addr = uv_addr;	
		lcdc_writel(lcdc_dev, WIN1_YRGB_MST, win->area[0].y_addr); 
		lcdc_writel(lcdc_dev, WIN1_CBR_MST, win->area[0].uv_addr);
	}
	spin_unlock(&lcdc_dev->reg_lock);


	return 0;
}

static int win2_display(struct lcdc_device *lcdc_dev,
			struct rk_lcdc_win *win)
{
	u32 i,y_addr;
	y_addr = win->area[0].smem_start + win->area[0].y_offset;
	DBG(2, "lcdc%d>>%s>>y_addr:0x%x>>\n",
	    lcdc_dev->id, __func__, y_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)){
		for(i=0;i<win->area_num;i++)
			win->area[i].y_addr = 
				win->area[i].smem_start + win->area[i].y_offset;
			lcdc_writel(lcdc_dev,WIN2_MST0,win->area[0].y_addr);
			lcdc_writel(lcdc_dev,WIN2_MST1,win->area[1].y_addr);
			lcdc_writel(lcdc_dev,WIN2_MST2,win->area[2].y_addr);
			lcdc_writel(lcdc_dev,WIN2_MST3,win->area[3].y_addr);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int win3_display(struct lcdc_device *lcdc_dev,
			struct rk_lcdc_win *win)
{
	u32 i,y_addr;
	y_addr = win->area[0].smem_start + win->area[0].y_offset;
	DBG(2, "lcdc%d>>%s>>y_addr:0x%x>>\n",
	    lcdc_dev->id, __func__, y_addr);

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)){
		for(i=0;i<win->area_num;i++)
			win->area[i].y_addr = 
				win->area[i].smem_start + win->area[i].y_offset;
			lcdc_writel(lcdc_dev,WIN3_MST0,win->area[0].y_addr);
			lcdc_writel(lcdc_dev,WIN3_MST1,win->area[1].y_addr);
			lcdc_writel(lcdc_dev,WIN3_MST2,win->area[2].y_addr);
			lcdc_writel(lcdc_dev,WIN3_MST3,win->area[3].y_addr);		
		}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3288_lcdc_pan_display(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
				struct lcdc_device, driver);
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;
	
#if defined(WAIT_FOR_SYNC)
	int timeout;
	unsigned long flags;
#endif
	win = dev_drv->win[win_id];
 	if (!screen) {
		dev_err(dev_drv->dev, "screen is null!\n");
		return -ENOENT;
	}
 	if(win_id == 0){
		win0_display(lcdc_dev, win);
	}else if(win_id == 1){
		win1_display(lcdc_dev, win);
	}else if(win_id == 2){
		win2_display(lcdc_dev, win);
	}else if(win_id == 3){
		win3_display(lcdc_dev, win);
	}else{
		dev_err(dev_drv->dev, "invalid win number:%d!\n", win_id);
		return -EINVAL;
	}
 
	/*this is the first frame of the system ,enable frame start interrupt */
	if ((dev_drv->first_frame)) {
		dev_drv->first_frame = 0;
		rk3288_lcdc_enable_irq(dev_drv);
	}
#if defined(WAIT_FOR_SYNC)
	spin_lock_irqsave(&dev_drv->cpl_lock, flags);
	init_completion(&dev_drv->frame_done);
	spin_unlock_irqrestore(&dev_drv->cpl_lock, flags);
	timeout = wait_for_completion_timeout(&dev_drv->frame_done,
					      msecs_to_jiffies(dev_drv->
							       cur_screen->ft +
							       5));
	if (!timeout && (!dev_drv->frame_done.done)) {
		dev_info(dev_drv->dev, "wait for new frame start time out!\n");
		return -ETIMEDOUT;
	}
#endif 
	return 0;
}

static int rk3288_lcdc_cal_scl_fac(struct rk_lcdc_win *win)
{
	u16 srcW;
	u16 srcH;
	u16 dstW;
	u16 dstH;
	u16 yrgb_srcW;
	u16 yrgb_srcH;
	u16 yrgb_dstW;
	u16 yrgb_dstH;
	u32 yrgb_vScaleDnMult;
	u32 yrgb_xscl_factor;
	u32 yrgb_yscl_factor;
	u8  yrgb_vsd_bil_gt2=0;
	u8  yrgb_vsd_bil_gt4=0;
	
	u16 cbcr_srcW;
	u16 cbcr_srcH;
	u16 cbcr_dstW;
	u16 cbcr_dstH;	  
	u32 cbcr_vScaleDnMult;
	u32 cbcr_xscl_factor;
	u32 cbcr_yscl_factor;
	u8  cbcr_vsd_bil_gt2=0;
	u8  cbcr_vsd_bil_gt4=0;
	u8  yuv_fmt=0;


	srcW = win->area[0].xact;
	srcH = win->area[0].yact;
	dstW = win->area[0].xsize;
	dstH = win->area[0].ysize;

	/*yrgb scl mode*/
	yrgb_srcW = srcW;
	yrgb_srcH = srcH;
	yrgb_dstW = dstW;
	yrgb_dstH = dstH;
	if ((yrgb_dstW >= yrgb_srcW*8) || (yrgb_dstH >= yrgb_srcH*8) ||
		(yrgb_dstW*8 <= yrgb_srcW) || (yrgb_dstH*8 <= yrgb_srcH)) {
		pr_err("ERROR: yrgb scale exceed 8,"
		       "srcW=%d,srcH=%d,dstW=%d,dstH=%d\n",
		       yrgb_srcW,yrgb_srcH,yrgb_dstW,yrgb_dstH);
	}
	if(yrgb_srcW < yrgb_dstW){
		win->yrgb_hor_scl_mode = SCALE_UP;
    	}else if(yrgb_srcW > yrgb_dstW){
        	win->yrgb_hor_scl_mode = SCALE_DOWN;
    	}else{
        	win->yrgb_hor_scl_mode = SCALE_NONE;
    	}

    	if(yrgb_srcH < yrgb_dstH){
        	win->yrgb_ver_scl_mode = SCALE_UP;
    	}else if (yrgb_srcH  > yrgb_dstH){
        	win->yrgb_ver_scl_mode = SCALE_DOWN;
    	}else{
        	win->yrgb_ver_scl_mode = SCALE_NONE;
    	}

	/*cbcr scl mode*/
	switch (win->format) {
	case YUV422:
	case YUV422_A:	
		cbcr_srcW = srcW/2;
		cbcr_dstW = dstW;
		cbcr_srcH = srcH;
		cbcr_dstH = dstH;
		yuv_fmt = 1;
		break;
	case YUV420:
	case YUV420_A:	
		cbcr_srcW = srcW/2;
		cbcr_dstW = dstW;
		cbcr_srcH = srcH/2;
		cbcr_dstH = dstH;
		yuv_fmt = 1;
		break;
	case YUV444:
	case YUV444_A:	
		cbcr_srcW = srcW;
		cbcr_dstW = dstW;
		cbcr_srcH = srcH;
		cbcr_dstH = dstH;
		yuv_fmt = 1;
		break;
	default:
		cbcr_srcW = 0;
	        cbcr_dstW = 0;
	        cbcr_srcH = 0;
	        cbcr_dstH = 0;
		yuv_fmt = 0;
		break;
	}		
	if (yuv_fmt) {
		if ((cbcr_dstW >= cbcr_srcW*8) || (cbcr_dstH >= cbcr_srcH*8) ||
			(cbcr_dstW*8 <= cbcr_srcW)||(cbcr_dstH*8 <= cbcr_srcH)) {
			pr_err("ERROR: cbcr scale exceed 8,"
		       "srcW=%d,srcH=%d,dstW=%d,dstH=%d\n",
		       cbcr_srcW,cbcr_srcH,cbcr_dstW,cbcr_dstH);
		}
	}
	
	if(cbcr_srcW < cbcr_dstW){
		win->cbr_hor_scl_mode = SCALE_UP;
	}else if(cbcr_srcW > cbcr_dstW){
		win->cbr_hor_scl_mode = SCALE_DOWN;
	}else{
		win->cbr_hor_scl_mode = SCALE_NONE;
	}
	
	if(cbcr_srcH < cbcr_dstH){
		win->cbr_ver_scl_mode = SCALE_UP;
	}else if(cbcr_srcH > cbcr_dstH){
		win->cbr_ver_scl_mode = SCALE_DOWN;
	}else{
		win->cbr_ver_scl_mode = SCALE_NONE;
	}
	DBG(1, "srcW:%d>>srcH:%d>>dstW:%d>>dstH:%d>>\n"
	       "yrgb:src:W=%d>>H=%d,dst:W=%d>>H=%d,H_mode=%d,V_mode=%d\n"
	       "cbcr:src:W=%d>>H=%d,dst:W=%d>>H=%d,H_mode=%d,V_mode=%d\n"
		,srcW,srcH,dstW,dstH,yrgb_srcW,yrgb_srcH,yrgb_dstW,
		yrgb_dstH,win->yrgb_hor_scl_mode,win->yrgb_ver_scl_mode,
		cbcr_srcW,cbcr_srcH,cbcr_dstW,cbcr_dstH,
		win->cbr_hor_scl_mode,win->cbr_ver_scl_mode);

    /*line buffer mode*/
    	if((win->format == YUV422) || (win->format == YUV420) || (win->format == YUV422_A) || (win->format == YUV420_A)){
        	if(win->cbr_hor_scl_mode == SCALE_DOWN){
            		if ((cbcr_dstW > 3840) || (cbcr_dstW == 0)) {
                		pr_err("ERROR cbcr_dstW = %d\n",cbcr_dstW);                
            		}else if(cbcr_dstW > 2560){
                		win->win_lb_mode = LB_RGB_3840X2;
            		}else if(cbcr_dstW > 1920){
                		if(win->yrgb_hor_scl_mode == SCALE_DOWN){
                    			if(yrgb_dstW > 3840){
                        			pr_err("ERROR yrgb_dst_width exceeds 3840\n");
		                    	}else if(yrgb_dstW > 2560){
		                        	win->win_lb_mode = LB_RGB_3840X2;
		                    	}else if(yrgb_dstW > 1920){
		                        	win->win_lb_mode = LB_RGB_2560X4;
		                    	}else{
		                        	pr_err("ERROR never run here!yrgb_dstW<1920 ==> cbcr_dstW>1920\n");
		                    	}
		        	}
            		}else if(cbcr_dstW > 1280){
                		win->win_lb_mode = LB_YUV_3840X5;
            		}else{
                		win->win_lb_mode = LB_YUV_2560X8;
            		}            
        	} else { /*SCALE_UP or SCALE_NONE*/
            		if ((cbcr_srcW > 3840) || (cbcr_srcW == 0)) {
                		pr_err("ERROR cbcr_srcW = %d\n",cbcr_srcW);
            		}else if(cbcr_srcW > 2560){                
                		win->win_lb_mode = LB_RGB_3840X2;
            		}else if(cbcr_srcW > 1920){
                		if(win->yrgb_hor_scl_mode == SCALE_DOWN){
                    			if(yrgb_dstW > 3840){
                        			pr_err("ERROR yrgb_dst_width exceeds 3840\n");
                    			}else if(yrgb_dstW > 2560){
                        			win->win_lb_mode = LB_RGB_3840X2;
                    			}else if(yrgb_dstW > 1920){
                        			win->win_lb_mode = LB_RGB_2560X4;
                    			}else{
                        			pr_err("ERROR never run here!yrgb_dstW<1920 ==> cbcr_dstW>1920\n");
                    			}
                		}  
            		}else if(cbcr_srcW > 1280){
               			 win->win_lb_mode = LB_YUV_3840X5;
            		}else{
                		win->win_lb_mode = LB_YUV_2560X8;
            		}            
        	}
    	}else {
        	if(win->yrgb_hor_scl_mode == SCALE_DOWN){
            		if ((yrgb_dstW > 3840) || (yrgb_dstW == 0)) {
                		pr_err("ERROR yrgb_dstW = %d\n",yrgb_dstW);
            		}else if(yrgb_dstW > 2560){
                		win->win_lb_mode = LB_RGB_3840X2;
            		}else if(yrgb_dstW > 1920){
                		win->win_lb_mode = LB_RGB_2560X4;
            		}else if(yrgb_dstW > 1280){
                		win->win_lb_mode = LB_RGB_1920X5;
            		}else{
                		win->win_lb_mode = LB_RGB_1280X8;
            		}            
        	}else{ /*SCALE_UP or SCALE_NONE*/
            		if ((yrgb_srcW > 3840) || (yrgb_srcW == 0)) {
                		pr_err("ERROR yrgb_srcW = %d\n",yrgb_srcW);
            		}else if(yrgb_srcW > 2560){
                		win->win_lb_mode = LB_RGB_3840X2;
            		}else if(yrgb_srcW > 1920){
                		win->win_lb_mode = LB_RGB_2560X4;
            		}else if(yrgb_srcW > 1280){
                		win->win_lb_mode = LB_RGB_1920X5;
            		}else{
                		win->win_lb_mode = LB_RGB_1280X8;
            		}            
        	}
    	}
    	DBG(1,"win->win_lb_mode = %d;\n",win->win_lb_mode);

	/*vsd/vsu scale ALGORITHM*/
	win->yrgb_hsd_mode = SCALE_DOWN_BIL;/*not to specify*/
	win->cbr_hsd_mode  = SCALE_DOWN_BIL;/*not to specify*/
	win->yrgb_vsd_mode = SCALE_DOWN_BIL;/*not to specify*/
	win->cbr_vsd_mode  = SCALE_DOWN_BIL;/*not to specify*/
	switch(win->win_lb_mode){
	    case LB_YUV_3840X5:
	    case LB_YUV_2560X8:
	    case LB_RGB_1920X5:
	    case LB_RGB_1280X8: 	
		win->yrgb_vsu_mode = SCALE_UP_BIC; 
		win->cbr_vsu_mode  = SCALE_UP_BIC; 
		break;
	    case LB_RGB_3840X2:
		if(win->yrgb_ver_scl_mode != SCALE_NONE) {
		    pr_err("ERROR : not allow yrgb ver scale\n");
		}
		if(win->cbr_ver_scl_mode != SCALE_NONE) {
		    pr_err("ERROR : not allow cbcr ver scale\n");
		}	     	  
		break;
	    case LB_RGB_2560X4:
		win->yrgb_vsu_mode = SCALE_UP_BIL; 
		win->cbr_vsu_mode  = SCALE_UP_BIL; 	    
		break;
	    default:
	    	printk(KERN_WARNING "%s:un supported win_lb_mode:%d\n",
			__func__,win->win_lb_mode);	
		break;
	}
	DBG(1,"yrgb:hsd=%d,vsd=%d,vsu=%d;cbcr:hsd=%d,vsd=%d,vsu=%d\n",
	       win->yrgb_hsd_mode,win->yrgb_vsd_mode,win->yrgb_vsu_mode,
	       win->cbr_hsd_mode,win->cbr_vsd_mode,win->cbr_vsu_mode);

    	/*SCALE FACTOR*/
    
    	/*(1.1)YRGB HOR SCALE FACTOR*/
    	switch(win->yrgb_hor_scl_mode){
        case SCALE_NONE:
        	yrgb_xscl_factor = (1<<SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
            	break;
        case SCALE_UP  :
            	yrgb_xscl_factor = GET_SCALE_FACTOR_BIC(yrgb_srcW, yrgb_dstW);
            	break;
        case SCALE_DOWN:
            	switch(win->yrgb_hsd_mode)
            	{
                case SCALE_DOWN_BIL:
                	yrgb_xscl_factor = GET_SCALE_FACTOR_BILI_DN(yrgb_srcW, yrgb_dstW);
                    	break;
                case SCALE_DOWN_AVG:
                    	yrgb_xscl_factor = GET_SCALE_FACTOR_AVRG(yrgb_srcW, yrgb_dstW);
                    	break;
                default :
			printk(KERN_WARNING "%s:un supported yrgb_hsd_mode:%d\n",
				__func__,win->yrgb_hsd_mode);		
                    	break;
            	} 
            	break;
        default :
		printk(KERN_WARNING "%s:un supported yrgb_hor_scl_mode:%d\n",
				__func__,win->yrgb_hor_scl_mode);	
            break;
    	} /*win->yrgb_hor_scl_mode*/

    	/*(1.2)YRGB VER SCALE FACTOR*/
    	switch(win->yrgb_ver_scl_mode)
    	{
        case SCALE_NONE:
            	yrgb_yscl_factor = (1<<SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
           	 break;
        case SCALE_UP  :
            	switch(win->yrgb_vsu_mode)
            	{
                case SCALE_UP_BIL:
                    	yrgb_yscl_factor = GET_SCALE_FACTOR_BILI_UP(yrgb_srcH, yrgb_dstH);
                    	break;
                case SCALE_UP_BIC:
                    	if(yrgb_srcH < 3){
                        	pr_err("yrgb_srcH should be greater than 3 !!!\n");
                    	}                    
                    	yrgb_yscl_factor = GET_SCALE_FACTOR_BIC(yrgb_srcH, yrgb_dstH);
                   	break;
                default :
			printk(KERN_WARNING "%s:un supported yrgb_vsu_mode:%d\n",
				__func__,win->yrgb_vsu_mode);			
                    	break;
            }
            break;
        case SCALE_DOWN:
            	switch(win->yrgb_vsd_mode)
            	{
                case SCALE_DOWN_BIL:
                    	yrgb_vScaleDnMult = getHardWareVSkipLines(yrgb_srcH, yrgb_dstH);
                    	yrgb_yscl_factor  = GET_SCALE_FACTOR_BILI_DN_VSKIP(yrgb_srcH, yrgb_dstH, yrgb_vScaleDnMult);                                 
                    	if(yrgb_vScaleDnMult == 4){
                        	yrgb_vsd_bil_gt4 = 1;
                        	yrgb_vsd_bil_gt2 = 0;
                    	}else if(yrgb_vScaleDnMult == 2){
                        	yrgb_vsd_bil_gt4 = 0;
                        	yrgb_vsd_bil_gt2 = 1;
                    	}else{
                        	yrgb_vsd_bil_gt4 = 0;
                        	yrgb_vsd_bil_gt2 = 0;
                    	}
                    	break;
                case SCALE_DOWN_AVG:
                    	yrgb_yscl_factor = GET_SCALE_FACTOR_AVRG(yrgb_srcH, yrgb_dstH);
                    	break;
                default:
			printk(KERN_WARNING "%s:un supported yrgb_vsd_mode:%d\n",
				__func__,win->yrgb_vsd_mode);		
                    	break;
            	} /*win->yrgb_vsd_mode*/
            	break;
	default :
		printk(KERN_WARNING "%s:un supported yrgb_ver_scl_mode:%d\n",
			__func__,win->yrgb_ver_scl_mode);		
            	break;
    	}
    	win->scale_yrgb_x = yrgb_xscl_factor;
    	win->scale_yrgb_y = yrgb_yscl_factor;
    	win->vsd_yrgb_gt4 = yrgb_vsd_bil_gt4;
    	win->vsd_yrgb_gt2 = yrgb_vsd_bil_gt2;
	DBG(1,"yrgb:h_fac=%d,v_fac=%d,gt4=%d,gt2=%d\n",yrgb_xscl_factor,
		yrgb_yscl_factor,yrgb_vsd_bil_gt4,yrgb_vsd_bil_gt2);

    	/*(2.1)CBCR HOR SCALE FACTOR*/
    	switch(win->cbr_hor_scl_mode)
    	{
        case SCALE_NONE:
            	cbcr_xscl_factor = (1<<SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
            	break;
        case SCALE_UP  :
            	cbcr_xscl_factor = GET_SCALE_FACTOR_BIC(cbcr_srcW, cbcr_dstW);
            	break;
        case SCALE_DOWN:
            	switch(win->cbr_hsd_mode)
            	{
                case SCALE_DOWN_BIL:
                    	cbcr_xscl_factor = GET_SCALE_FACTOR_BILI_DN(cbcr_srcW, cbcr_dstW);
                    	break;
                case SCALE_DOWN_AVG:
                    	cbcr_xscl_factor = GET_SCALE_FACTOR_AVRG(cbcr_srcW, cbcr_dstW);
                    	break;
                default :
			printk(KERN_WARNING "%s:un supported cbr_hsd_mode:%d\n",
				__func__,win->cbr_hsd_mode);	
                    	break;
            	}
            	break;
        default :
		printk(KERN_WARNING "%s:un supported cbr_hor_scl_mode:%d\n",
			__func__,win->cbr_hor_scl_mode);	
            	break;
    	} /*win->cbr_hor_scl_mode*/

    	/*(2.2)CBCR VER SCALE FACTOR*/
    	switch(win->cbr_ver_scl_mode)
    	{
        case SCALE_NONE:
            	cbcr_yscl_factor = (1<<SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
            	break;
        case SCALE_UP  :
            	switch(win->cbr_vsu_mode)
            	{
                case SCALE_UP_BIL:
                    	cbcr_yscl_factor = GET_SCALE_FACTOR_BILI_UP(cbcr_srcH, cbcr_dstH);
                    	break;
                case SCALE_UP_BIC:
                    	if(cbcr_srcH < 3) {
                        	pr_err("cbcr_srcH should be greater than 3 !!!\n");
                    	}                    
                    	cbcr_yscl_factor = GET_SCALE_FACTOR_BIC(cbcr_srcH, cbcr_dstH);
                    	break;
                default :
			printk(KERN_WARNING "%s:un supported cbr_vsu_mode:%d\n",
				__func__,win->cbr_vsu_mode);		
                    	break;
            	}
            	break;
        case SCALE_DOWN:
            	switch(win->cbr_vsd_mode)
            	{
                case SCALE_DOWN_BIL:
                    	cbcr_vScaleDnMult = getHardWareVSkipLines(cbcr_srcH, cbcr_dstH);
                    	cbcr_yscl_factor  = GET_SCALE_FACTOR_BILI_DN_VSKIP(cbcr_srcH, cbcr_dstH, cbcr_vScaleDnMult);                    
                    	if(cbcr_vScaleDnMult == 4){
                        	cbcr_vsd_bil_gt4 = 1;
                        	cbcr_vsd_bil_gt2 = 0;
                    	}else if(cbcr_vScaleDnMult == 2){
                        	cbcr_vsd_bil_gt4 = 0;
                        	cbcr_vsd_bil_gt2 = 1;
                    	}else{
                        	cbcr_vsd_bil_gt4 = 0;
                        	cbcr_vsd_bil_gt2 = 0;
                    	}
                    	break;
                case SCALE_DOWN_AVG:
                    	cbcr_yscl_factor = GET_SCALE_FACTOR_AVRG(cbcr_srcH, cbcr_dstH);
                    	break;
                default :
			printk(KERN_WARNING "%s:un supported cbr_vsd_mode:%d\n",
				__func__,win->cbr_vsd_mode);		
                    break;
            	}
            	break;
        default :
		printk(KERN_WARNING "%s:un supported cbr_ver_scl_mode:%d\n",
			__func__,win->cbr_ver_scl_mode);			
            	break;
    	}
    	win->scale_cbcr_x = cbcr_xscl_factor;
    	win->scale_cbcr_y = cbcr_yscl_factor;
   	win->vsd_cbr_gt4  = cbcr_vsd_bil_gt4;
    	win->vsd_cbr_gt2  = cbcr_vsd_bil_gt2;	

	DBG(1,"cbcr:h_fac=%d,v_fac=%d,gt4=%d,gt2=%d\n",cbcr_xscl_factor,
		cbcr_yscl_factor,cbcr_vsd_bil_gt4,cbcr_vsd_bil_gt2);
	return 0;
}



static int win0_set_par(struct lcdc_device *lcdc_dev,
			struct rk_screen *screen, struct rk_lcdc_win *win)
{
	u32 xact,yact,xvir, yvir,xpos, ypos;
	u8 fmt_cfg = 0;
	char fmt[9] = "NULL";

	xpos = win->area[0].xpos + screen->mode.left_margin + screen->mode.hsync_len;
	ypos = win->area[0].ypos + screen->mode.upper_margin + screen->mode.vsync_len;

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on)){
		rk3288_lcdc_cal_scl_fac(win);/*fac,lb,gt2,gt4*/
		switch (win->format){
		case ARGB888:
			fmt_cfg = 0;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case XBGR888:
		case ABGR888:
			fmt_cfg = 0;
			win->swap_rb = 1;
			win->fmt_10 = 0;
			break;
		case RGB888:
			fmt_cfg = 1;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case RGB565:
			fmt_cfg = 2;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV422:
			fmt_cfg = 5;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV420:	
			fmt_cfg = 4;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV444:	
			fmt_cfg = 6;
			win->swap_rb = 0;
			win->fmt_10 = 0;
		case YUV422_A:
			fmt_cfg = 5;
			win->swap_rb = 0;
			win->fmt_10 = 1;
			break;
		case YUV420_A:	
			fmt_cfg = 4;
			win->swap_rb = 0;
			win->fmt_10 = 1;
			break;
		case YUV444_A:	
			fmt_cfg = 6;
			win->swap_rb = 0;
			win->fmt_10 = 1;
			break;
		default:
			dev_err(lcdc_dev->driver.dev, "%s:un supported format!\n",
				__func__);
			break;
		}
		win->fmt_cfg = fmt_cfg;
		win->area[0].dsp_stx = xpos;
		win->area[0].dsp_sty = ypos;
		xact = win->area[0].xact;
		yact = win->area[0].yact;
		xvir = win->area[0].xvir;
		yvir = win->area[0].yvir;
	}
	rk3288_win_0_1_reg_update(&lcdc_dev->driver,0);
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(1, "lcdc%d>>%s\n>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d\n"
		">>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n", lcdc_dev->id,
		__func__, get_format_string(win->format, fmt), xact,
		yact, win->area[0].xsize, win->area[0].ysize, xvir, yvir, xpos, ypos);
	return 0;

}

static int win1_set_par(struct lcdc_device *lcdc_dev,
			struct rk_screen *screen, struct rk_lcdc_win *win)
{
	u32 xact,yact,xvir, yvir,xpos, ypos;
	u8 fmt_cfg = 0;
	char fmt[9] = "NULL";

	xpos = win->area[0].xpos + screen->mode.left_margin + screen->mode.hsync_len;
	ypos = win->area[0].ypos + screen->mode.upper_margin + screen->mode.vsync_len;

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on)){
		rk3288_lcdc_cal_scl_fac(win);/*fac,lb,gt2,gt4*/
		switch (win->format){
		case ARGB888:
			fmt_cfg = 0;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case XBGR888:
		case ABGR888:
			fmt_cfg = 0;
			win->swap_rb = 1;
			win->fmt_10 = 0;
			break;
		case RGB888:
			fmt_cfg = 1;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case RGB565:
			fmt_cfg = 2;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV422:
			fmt_cfg = 5;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV420:
			fmt_cfg = 4;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV444:
			fmt_cfg = 6;
			win->swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV422_A:
			fmt_cfg = 5;
			win->swap_rb = 0;
			win->fmt_10 = 1;
			break;
		case YUV420_A:	
			fmt_cfg = 4;
			win->swap_rb = 0;
			win->fmt_10 = 1;
			break;
		case YUV444_A:	
			fmt_cfg = 6;
			win->swap_rb = 0;
			win->fmt_10 = 1;
			break;			
		default:
			dev_err(lcdc_dev->driver.dev, "%s:un supported format!\n",
				__func__);
			break;
		}
		win->fmt_cfg = fmt_cfg;
		win->area[0].dsp_stx = xpos;
		win->area[0].dsp_sty = ypos;
		xact = win->area[0].xact;
		yact = win->area[0].yact;
		xvir = win->area[0].xvir;
		yvir = win->area[0].yvir;
	}
	rk3288_win_0_1_reg_update(&lcdc_dev->driver,1);
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(1, "lcdc%d>>%s\n>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d\n"
		">>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n", lcdc_dev->id,
		__func__, get_format_string(win->format, fmt), xact,
		yact, win->area[0].xsize, win->area[0].ysize, xvir, yvir, xpos, ypos);
	return 0;

}

static int win2_set_par(struct lcdc_device *lcdc_dev,
			struct rk_screen *screen, struct rk_lcdc_win *win)
{
	int i;
	u8 fmt_cfg;

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on)){
		for(i=0;i<win->area_num;i++){
			switch (win->format){
			case ARGB888:
				fmt_cfg = 0;
				win->swap_rb = 0;
				break;
			case XBGR888:
			case ABGR888:
				fmt_cfg = 0;
				win->swap_rb = 1;
				break;
			case RGB888:
				fmt_cfg = 1;
				win->swap_rb = 0;
				break;
			case RGB565:
				fmt_cfg = 2;
				win->swap_rb = 0;		
				break;
			default:
				dev_err(lcdc_dev->driver.dev, 
					"%s:un supported format!\n",
					__func__);
				break;
			}			
			win->fmt_cfg = fmt_cfg;
			win->area[i].dsp_stx = win->area[i].xpos + 
				screen->mode.left_margin +
				screen->mode.hsync_len;
			if (screen->y_mirror == 1) {
				win->area[i].dsp_sty = screen->mode.yres -
					win->area[i].ypos -
					win->area[i].ysize + 
					screen->mode.upper_margin +
					screen->mode.vsync_len;
			} else {
				win->area[i].dsp_sty = win->area[i].ypos + 
					screen->mode.upper_margin +
					screen->mode.vsync_len;
			}
		}
	}
	rk3288_win_2_3_reg_update(&lcdc_dev->driver,2);
	spin_unlock(&lcdc_dev->reg_lock);	
	return 0;
}

static int win3_set_par(struct lcdc_device *lcdc_dev,
			struct rk_screen *screen, struct rk_lcdc_win *win)

{
	int i;
	u8 fmt_cfg;

	spin_lock(&lcdc_dev->reg_lock);
	if(likely(lcdc_dev->clk_on)){
		for(i=0;i<win->area_num;i++){
			switch (win->format){
			case ARGB888:
				fmt_cfg = 0;
				win->swap_rb = 0;
				break;
			case XBGR888:
			case ABGR888:
				fmt_cfg = 0;
				win->swap_rb = 1;
				break;
			case RGB888:
				fmt_cfg = 1;
				win->swap_rb = 0;
				break;
			case RGB565:
				fmt_cfg = 2;
				win->swap_rb = 0;		
				break;
			default:
				dev_err(lcdc_dev->driver.dev, 
					"%s:un supported format!\n",
					__func__);
				break;
			}			
			win->fmt_cfg = fmt_cfg;
			win->area[i].dsp_stx = win->area[i].xpos + 
				screen->mode.left_margin +
				screen->mode.hsync_len;
			if (screen->y_mirror == 1) {
				win->area[i].dsp_sty = screen->mode.yres -
					win->area[i].ypos -
					win->area[i].ysize + 
					screen->mode.upper_margin +
					screen->mode.vsync_len;
			} else {
				win->area[i].dsp_sty = win->area[i].ypos + 
					screen->mode.upper_margin +
					screen->mode.vsync_len;
			}
		}
	}
	rk3288_win_2_3_reg_update(&lcdc_dev->driver,3);
	spin_unlock(&lcdc_dev->reg_lock);	
	return 0;
}

static int rk3288_lcdc_set_par(struct rk_lcdc_driver *dev_drv,int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;
	win = dev_drv->win[win_id];

	switch(win_id)
	{
	case 0:
		win0_set_par(lcdc_dev, screen, win);
		break;
	case 1:
		win1_set_par(lcdc_dev, screen, win);
		break;	
	case 2:
		win2_set_par(lcdc_dev, screen, win);
		break;
	case 3:
		win3_set_par(lcdc_dev, screen, win);
		break;		
	default:
		dev_err(dev_drv->dev, "unsupported win number:%d\n", win_id);
		break;	
	}
	return 0;
}

static int rk3288_lcdc_ioctl(struct rk_lcdc_driver *dev_drv, unsigned int cmd,
			     unsigned long arg, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
							   struct
							   lcdc_device,
							   driver);
	u32 panel_size[2];
	void __user *argp = (void __user *)arg;
	struct color_key_cfg clr_key_cfg;

	switch (cmd) {
	case RK_FBIOGET_PANEL_SIZE:
		panel_size[0] = lcdc_dev->screen->mode.xres;
		panel_size[1] = lcdc_dev->screen->mode.yres;
		if (copy_to_user(argp, panel_size, 8))
			return -EFAULT;
		break;
	case RK_FBIOPUT_COLOR_KEY_CFG:
		if (copy_from_user(&clr_key_cfg, argp,
				   sizeof(struct color_key_cfg)))
			return -EFAULT;
		rk3288_lcdc_clr_key_cfg(dev_drv);
		lcdc_writel(lcdc_dev, WIN0_COLOR_KEY,
			    clr_key_cfg.win0_color_key_cfg);
		lcdc_writel(lcdc_dev, WIN1_COLOR_KEY,
			    clr_key_cfg.win1_color_key_cfg);
		break;

	default:
		break;
	}
	return 0;
}

static int rk3288_lcdc_early_suspend(struct rk_lcdc_driver *dev_drv)
{
	u32 reg;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	if (dev_drv->suspend_flag)
		return 0;
	
	dev_drv->suspend_flag = 1;
	flush_kthread_worker(&dev_drv->update_regs_worker);
	
	for (reg = MMU_DTE_ADDR; reg <= MMU_AUTO_GATING; reg +=4)
			lcdc_readl(lcdc_dev, reg);
	if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
		dev_drv->trsm_ops->disable();
	
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_BLANK_EN,
					v_DSP_BLANK_EN(1));
		lcdc_msk_reg(lcdc_dev, INTR_CTRL0, m_FS_INTR_CLR | m_LINE_FLAG_INTR_CLR,
					v_FS_INTR_CLR(1) | v_LINE_FLAG_INTR_CLR(1));	
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_OUT_ZERO,
			     		v_DSP_OUT_ZERO(1));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN,
			    		v_STANDBY_EN(1));
		lcdc_cfg_done(lcdc_dev);

                if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_deactivate(dev_drv->dev);
		}

		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}
	rk3288_lcdc_clk_disable(lcdc_dev);
	rk_disp_pwr_disable(dev_drv);
	return 0;
}

static int rk3288_lcdc_early_resume(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int i, j;
	int __iomem *c;
	int v, r, g, b;

	if (!dev_drv->suspend_flag)
		return 0;
	rk_disp_pwr_enable(dev_drv);
	dev_drv->suspend_flag = 0;

	if (lcdc_dev->atv_layer_cnt) {
		rk3288_lcdc_clk_enable(lcdc_dev);
		rk3288_lcdc_reg_restore(lcdc_dev);

		spin_lock(&lcdc_dev->reg_lock);
		if (dev_drv->cur_screen->dsp_lut) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN,
				     v_DSP_LUT_EN(0));
			lcdc_cfg_done(lcdc_dev);
			mdelay(25);
			for (i = 0; i < 256; i++) {
				v = dev_drv->cur_screen->dsp_lut[i];
				c = lcdc_dev->dsp_lut_addr_base + (i << 2);
				b = (v & 0xff) << 2;
				g = (v & 0xff00) << 4;
				r = (v & 0xff0000) << 6;
				v = r + g + b;
				for (j = 0; j < 4; j++) {
					writel_relaxed(v, c);
					v += (1 + (1 << 10) + (1 << 20)) ;
					c++;
				}
			}
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN,
				     v_DSP_LUT_EN(1));
		}

		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_OUT_ZERO,
			     v_DSP_OUT_ZERO(0));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN,
			     v_STANDBY_EN(0));
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_BLANK_EN,
					v_DSP_BLANK_EN(0));	
		lcdc_cfg_done(lcdc_dev);

                if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_activate(dev_drv->dev);
		}

		spin_unlock(&lcdc_dev->reg_lock);
	}

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();

	return 0;
}

static int rk3288_lcdc_blank(struct rk_lcdc_driver *dev_drv,
			     int win_id, int blank_mode)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		rk3288_lcdc_early_resume(dev_drv);
		break;
	case FB_BLANK_NORMAL:	
		rk3288_lcdc_early_suspend(dev_drv);
		break;
	default:
		rk3288_lcdc_early_suspend(dev_drv);
		break;
	}

	dev_info(dev_drv->dev, "blank mode:%d\n", blank_mode);

	return 0;
}

static int rk3288_lcdc_get_win_state(struct rk_lcdc_driver *dev_drv, int win_id)
{
	return 0;
}

/*overlay will be do at regupdate*/
static int rk3288_lcdc_ovl_mgr(struct rk_lcdc_driver *dev_drv, int swap,
			       bool set)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = NULL;
	int i,ovl;
	unsigned int mask, val;
	int z_order_num=0;
	int layer0_sel,layer1_sel,layer2_sel,layer3_sel;
	if(swap == 0){
		for(i=0;i<4;i++){
			win = dev_drv->win[i];
			if(win->state == 1){
				z_order_num++;
			}	
		}
		for(i=0;i<4;i++){
			win = dev_drv->win[i];
			if(win->state == 0)
				win->z_order = z_order_num++;
			switch(win->z_order){
			case 0:
				layer0_sel = win->id;
				break;
			case 1:
				layer1_sel = win->id;
				break;
			case 2:
				layer2_sel = win->id;
				break;
			case 3:
				layer3_sel = win->id;
				break;
			default:
				break;
			}
		}
	}else{
		layer0_sel = swap %10;;
		layer1_sel = swap /10 % 10;
		layer2_sel = swap / 100 %10;
		layer3_sel = swap / 1000;
	}

	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on){
		if(set){
			mask = m_DSP_LAYER0_SEL | m_DSP_LAYER1_SEL |
				m_DSP_LAYER2_SEL | m_DSP_LAYER3_SEL;
			val  = v_DSP_LAYER0_SEL(layer0_sel) |
				v_DSP_LAYER1_SEL(layer1_sel) |
				v_DSP_LAYER2_SEL(layer2_sel) |
				v_DSP_LAYER3_SEL(layer3_sel);
			lcdc_msk_reg(lcdc_dev,DSP_CTRL1,mask,val);
		}else{
			layer0_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1, m_DSP_LAYER0_SEL);
			layer1_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1, m_DSP_LAYER1_SEL);
			layer2_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1, m_DSP_LAYER2_SEL);
			layer3_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1, m_DSP_LAYER3_SEL);
			ovl = layer3_sel*1000 + layer2_sel*100 + layer1_sel *10 + layer0_sel;
		}
	}else{
		ovl = -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return ovl;
}

static ssize_t rk3288_lcdc_get_disp_info(struct rk_lcdc_driver *dev_drv,
					 char *buf, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
							   struct
							   lcdc_device,
							   driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u16 hsync_len = screen->mode.hsync_len;
	u16 left_margin = screen->mode.left_margin;
	u16 vsync_len = screen->mode.vsync_len;
	u16 upper_margin = screen->mode.upper_margin;
	u32 h_pw_bp = hsync_len + left_margin;
	u32 v_pw_bp = vsync_len + upper_margin;
	u32 fmt_id;
	char format_w0[9] = "NULL";
	char format_w1[9] = "NULL";
	char format_w2[9] = "NULL";
	char format_w3[9] = "NULL";	
	u32 win_ctrl,zorder,vir_info,act_info,dsp_info,dsp_st,y_factor,uv_factor;
	u8 layer0_sel,layer1_sel,layer2_sel,layer3_sel;
	u8 w0_state,w1_state,w2_state,w3_state;
	u8 w2_0_state,w2_1_state,w2_2_state,w2_3_state;
	u8 w3_0_state,w3_1_state,w3_2_state,w3_3_state;

	u32 w0_vir_y,w0_vir_uv,w0_act_x,w0_act_y,w0_dsp_x,w0_dsp_y,w0_st_x=h_pw_bp,w0_st_y=v_pw_bp;
	u32 w1_vir_y,w1_vir_uv,w1_act_x,w1_act_y,w1_dsp_x,w1_dsp_y,w1_st_x=h_pw_bp,w1_st_y=v_pw_bp;
	u32 w0_y_h_fac,w0_y_v_fac,w0_uv_h_fac,w0_uv_v_fac;
	u32 w1_y_h_fac,w1_y_v_fac,w1_uv_h_fac,w1_uv_v_fac;

	u32 w2_0_vir_y,w2_1_vir_y,w2_2_vir_y,w2_3_vir_y;
	u32 w2_0_dsp_x,w2_1_dsp_x,w2_2_dsp_x,w2_3_dsp_x;
	u32 w2_0_dsp_y,w2_1_dsp_y,w2_2_dsp_y,w2_3_dsp_y;
	u32 w2_0_st_x=h_pw_bp,w2_1_st_x=h_pw_bp,w2_2_st_x=h_pw_bp,w2_3_st_x=h_pw_bp;
	u32 w2_0_st_y=v_pw_bp,w2_1_st_y=v_pw_bp,w2_2_st_y=v_pw_bp,w2_3_st_y=v_pw_bp;

	u32 w3_0_vir_y,w3_1_vir_y,w3_2_vir_y,w3_3_vir_y;
	u32 w3_0_dsp_x,w3_1_dsp_x,w3_2_dsp_x,w3_3_dsp_x;
	u32 w3_0_dsp_y,w3_1_dsp_y,w3_2_dsp_y,w3_3_dsp_y;
	u32 w3_0_st_x=h_pw_bp,w3_1_st_x=h_pw_bp,w3_2_st_x=h_pw_bp,w3_3_st_x=h_pw_bp;
	u32 w3_0_st_y=v_pw_bp,w3_1_st_y=v_pw_bp,w3_2_st_y=v_pw_bp,w3_3_st_y=v_pw_bp;
	u32 dclk_freq;

	dclk_freq = screen->mode.pixclock;
	/*rk3288_lcdc_reg_dump(dev_drv);*/

	spin_lock(&lcdc_dev->reg_lock);		
	if (lcdc_dev->clk_on) {
		zorder = lcdc_readl(lcdc_dev, DSP_CTRL1);
		layer0_sel = (zorder & m_DSP_LAYER0_SEL)>>8;
		layer1_sel = (zorder & m_DSP_LAYER1_SEL)>>10;
		layer2_sel = (zorder & m_DSP_LAYER2_SEL)>>12;
		layer3_sel = (zorder & m_DSP_LAYER3_SEL)>>14;
		/*WIN0*/
		win_ctrl = lcdc_readl(lcdc_dev, WIN0_CTRL0);
		w0_state = win_ctrl & m_WIN0_EN;
		fmt_id = (win_ctrl & m_WIN0_DATA_FMT)>>1;
		switch (fmt_id) {
		case 0:
			strcpy(format_w0, "ARGB888");
			break;
		case 1:
			strcpy(format_w0, "RGB888");
			break;
		case 2:
			strcpy(format_w0, "RGB565");
			break;
		case 4:
			strcpy(format_w0, "YCbCr420");
			break;
		case 5:
			strcpy(format_w0, "YCbCr422");
			break;
		case 6:
			strcpy(format_w0, "YCbCr444");
			break;
		default:
			strcpy(format_w0, "invalid\n");
			break;
		}
		vir_info = lcdc_readl(lcdc_dev,WIN0_VIR);
		act_info = lcdc_readl(lcdc_dev,WIN0_ACT_INFO);
		dsp_info = lcdc_readl(lcdc_dev,WIN0_DSP_INFO);
		dsp_st = lcdc_readl(lcdc_dev,WIN0_DSP_ST);
		y_factor = lcdc_readl(lcdc_dev,WIN0_SCL_FACTOR_YRGB);
		uv_factor = lcdc_readl(lcdc_dev,WIN0_SCL_FACTOR_CBR);
		w0_vir_y = vir_info & m_WIN0_VIR_STRIDE;
		w0_vir_uv = (vir_info & m_WIN0_VIR_STRIDE_UV)>>16;
		w0_act_x = (act_info & m_WIN0_ACT_WIDTH)+1;
		w0_act_y = ((act_info & m_WIN0_ACT_HEIGHT)>>16)+1;
		w0_dsp_x = (dsp_info & m_WIN0_DSP_WIDTH)+1;
		w0_dsp_y = ((dsp_info & m_WIN0_DSP_HEIGHT)>>16)+1;
		if (w0_state) {
			w0_st_x = dsp_st & m_WIN0_DSP_XST;
			w0_st_y = (dsp_st & m_WIN0_DSP_YST)>>16;
		}
		w0_y_h_fac = y_factor & m_WIN0_HS_FACTOR_YRGB;
		w0_y_v_fac = (y_factor & m_WIN0_VS_FACTOR_YRGB)>>16;
		w0_uv_h_fac = uv_factor & m_WIN0_HS_FACTOR_CBR;
		w0_uv_v_fac = (uv_factor & m_WIN0_VS_FACTOR_CBR)>>16;

		/*WIN1*/
		win_ctrl = lcdc_readl(lcdc_dev, WIN1_CTRL0);
		w1_state = win_ctrl & m_WIN1_EN;
		fmt_id = (win_ctrl & m_WIN1_DATA_FMT)>>1;
		switch (fmt_id) {
		case 0:
			strcpy(format_w1, "ARGB888");
			break;
		case 1:
			strcpy(format_w1, "RGB888");
			break;
		case 2:
			strcpy(format_w1, "RGB565");
			break;
		case 4:
			strcpy(format_w1, "YCbCr420");
			break;
		case 5:
			strcpy(format_w1, "YCbCr422");
			break;
		case 6:
			strcpy(format_w1, "YCbCr444");
			break;
		default:
			strcpy(format_w1, "invalid\n");
			break;
		}
		vir_info = lcdc_readl(lcdc_dev,WIN1_VIR);
		act_info = lcdc_readl(lcdc_dev,WIN1_ACT_INFO);
		dsp_info = lcdc_readl(lcdc_dev,WIN1_DSP_INFO);
		dsp_st = lcdc_readl(lcdc_dev,WIN1_DSP_ST);
		y_factor = lcdc_readl(lcdc_dev,WIN1_SCL_FACTOR_YRGB);
		uv_factor = lcdc_readl(lcdc_dev,WIN1_SCL_FACTOR_CBR);
		w1_vir_y = vir_info & m_WIN1_VIR_STRIDE;
		w1_vir_uv = (vir_info & m_WIN1_VIR_STRIDE_UV)>>16;
		w1_act_x = (act_info & m_WIN1_ACT_WIDTH)+1;
		w1_act_y = ((act_info & m_WIN1_ACT_HEIGHT)>>16)+1;
		w1_dsp_x = (dsp_info & m_WIN1_DSP_WIDTH)+1;
		w1_dsp_y =((dsp_info & m_WIN1_DSP_HEIGHT)>>16)+1;
		if (w1_state) {
			w1_st_x = dsp_st & m_WIN1_DSP_XST;
			w1_st_y = (dsp_st & m_WIN1_DSP_YST)>>16;
		}
		w1_y_h_fac = y_factor & m_WIN1_HS_FACTOR_YRGB;
		w1_y_v_fac = (y_factor & m_WIN1_VS_FACTOR_YRGB)>>16;
		w1_uv_h_fac = uv_factor & m_WIN1_HS_FACTOR_CBR;
		w1_uv_v_fac = (uv_factor & m_WIN1_VS_FACTOR_CBR)>>16;
		/*WIN2*/
		win_ctrl = lcdc_readl(lcdc_dev, WIN2_CTRL0);
		w2_state = win_ctrl & m_WIN2_EN;
		w2_0_state = (win_ctrl & m_WIN2_MST0_EN)>>4;
		w2_1_state = (win_ctrl & m_WIN2_MST1_EN)>>5;
		w2_2_state = (win_ctrl & m_WIN2_MST2_EN)>>6;
		w2_3_state = (win_ctrl & m_WIN2_MST3_EN)>>7;	
		vir_info = lcdc_readl(lcdc_dev,WIN2_VIR0_1);
		w2_0_vir_y = vir_info & m_WIN2_VIR_STRIDE0;
		w2_1_vir_y = (vir_info & m_WIN2_VIR_STRIDE1)>>16;
		vir_info = lcdc_readl(lcdc_dev,WIN2_VIR2_3);
		w2_2_vir_y = vir_info & m_WIN2_VIR_STRIDE2;
		w2_3_vir_y = (vir_info & m_WIN2_VIR_STRIDE3)>>16;			
		fmt_id = (win_ctrl & m_WIN2_DATA_FMT)>>1;
		switch (fmt_id) {
		case 0:
			strcpy(format_w2, "ARGB888");
			break;
		case 1:
			strcpy(format_w2, "RGB888");
			break;
		case 2:
			strcpy(format_w2, "RGB565");
			break;
                case 4:
                        strcpy(format_w2,"8bpp");
                        break;
                case 5:
                        strcpy(format_w2,"4bpp");
                        break;
                case 6:
                        strcpy(format_w2,"2bpp");
                        break;
                case 7:
                        strcpy(format_w2,"1bpp");
                        break;
		default:
			strcpy(format_w2, "invalid\n");
			break;
		} 
		dsp_info = lcdc_readl(lcdc_dev,WIN2_DSP_INFO0);
		dsp_st = lcdc_readl(lcdc_dev,WIN2_DSP_ST0);
		w2_0_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH0)+1;
		w2_0_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT0)>>16)+1;
		if (w2_0_state) {
			w2_0_st_x = dsp_st & m_WIN2_DSP_XST0;
			w2_0_st_y = (dsp_st & m_WIN2_DSP_YST0)>>16;
		}
		dsp_info = lcdc_readl(lcdc_dev,WIN2_DSP_INFO1);
		dsp_st = lcdc_readl(lcdc_dev,WIN2_DSP_ST1);
		w2_1_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH1)+1;
		w2_1_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT1)>>16)+1;
		if (w2_1_state) {
			w2_1_st_x = dsp_st & m_WIN2_DSP_XST1;
			w2_1_st_y = (dsp_st & m_WIN2_DSP_YST1)>>16;
		}
		dsp_info = lcdc_readl(lcdc_dev,WIN2_DSP_INFO2);
		dsp_st = lcdc_readl(lcdc_dev,WIN2_DSP_ST2);
		w2_2_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH2)+1;
		w2_2_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT2)>>16)+1;
		if (w2_2_state) {
			w2_2_st_x = dsp_st & m_WIN2_DSP_XST2;
			w2_2_st_y = (dsp_st & m_WIN2_DSP_YST2)>>16;
		}
		dsp_info = lcdc_readl(lcdc_dev,WIN2_DSP_INFO3);
		dsp_st = lcdc_readl(lcdc_dev,WIN2_DSP_ST3);
		w2_3_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH3)+1;
		w2_3_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT3)>>16)+1;
		if (w2_3_state) {
			w2_3_st_x = dsp_st & m_WIN2_DSP_XST3;
			w2_3_st_y = (dsp_st & m_WIN2_DSP_YST3)>>16;
		}

		/*WIN3*/
		win_ctrl = lcdc_readl(lcdc_dev, WIN3_CTRL0);
		w3_state = win_ctrl & m_WIN3_EN;
		w3_0_state = (win_ctrl & m_WIN3_MST0_EN)>>4;
		w3_1_state = (win_ctrl & m_WIN3_MST1_EN)>>5;
		w3_2_state = (win_ctrl & m_WIN3_MST2_EN)>>6;
		w3_3_state = (win_ctrl & m_WIN3_MST3_EN)>>7; 
		vir_info = lcdc_readl(lcdc_dev,WIN3_VIR0_1);
		w3_0_vir_y = vir_info & m_WIN3_VIR_STRIDE0;
		w3_1_vir_y = (vir_info & m_WIN3_VIR_STRIDE1)>>16;
		vir_info = lcdc_readl(lcdc_dev,WIN3_VIR2_3);
		w3_2_vir_y = vir_info & m_WIN3_VIR_STRIDE2;
		w3_3_vir_y = (vir_info & m_WIN3_VIR_STRIDE3)>>16;			
		fmt_id = (win_ctrl & m_WIN3_DATA_FMT)>>1;
		switch (fmt_id) {
		case 0:
			strcpy(format_w3, "ARGB888");
			break;
		case 1:
			strcpy(format_w3, "RGB888");
			break;
		case 2:
			strcpy(format_w3, "RGB565");
			break;
		case 4:
			strcpy(format_w3,"8bpp");
			break;
		case 5:
			strcpy(format_w3,"4bpp");
			break;
		case 6:
			strcpy(format_w3,"2bpp");
			break;
		case 7:
			strcpy(format_w3,"1bpp");
			break;
		default:
			strcpy(format_w3, "invalid");
			break;
		} 
		dsp_info = lcdc_readl(lcdc_dev,WIN3_DSP_INFO0);
		dsp_st = lcdc_readl(lcdc_dev,WIN3_DSP_ST0);
		w3_0_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH0)+1;
		w3_0_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT0)>>16)+1;
		if (w3_0_state) {
			w3_0_st_x = dsp_st & m_WIN3_DSP_XST0;
			w3_0_st_y = (dsp_st & m_WIN3_DSP_YST0)>>16;
		}
		
		dsp_info = lcdc_readl(lcdc_dev,WIN3_DSP_INFO1);
		dsp_st = lcdc_readl(lcdc_dev,WIN3_DSP_ST1);
		w3_1_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH1)+1;
		w3_1_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT1)>>16)+1;
		if (w3_1_state) {
			w3_1_st_x = dsp_st & m_WIN3_DSP_XST1;
			w3_1_st_y = (dsp_st & m_WIN3_DSP_YST1)>>16;
		}
		
		dsp_info = lcdc_readl(lcdc_dev,WIN3_DSP_INFO2);
		dsp_st = lcdc_readl(lcdc_dev,WIN3_DSP_ST2);
		w3_2_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH2)+1;
		w3_2_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT2)>>16)+1;
		if (w3_2_state) {
			w3_2_st_x = dsp_st & m_WIN3_DSP_XST2;
			w3_2_st_y = (dsp_st & m_WIN3_DSP_YST2)>>16;
		}
		
		dsp_info = lcdc_readl(lcdc_dev,WIN3_DSP_INFO3);
		dsp_st = lcdc_readl(lcdc_dev,WIN3_DSP_ST3);
		w3_3_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH3)+1;
		w3_3_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT3)>>16)+1;
		if (w3_3_state) {
			w3_3_st_x = dsp_st & m_WIN3_DSP_XST3;
			w3_3_st_y = (dsp_st & m_WIN3_DSP_YST3)>>16;
		}

	} else {
		spin_unlock(&lcdc_dev->reg_lock);
		return -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return snprintf(buf, PAGE_SIZE,
			"z-order:\n"
			"  layer3_sel_win[%d]\n"
			"  layer2_sel_win[%d]\n"
			"  layer1_sel_win[%d]\n"
			"  layer0_sel_win[%d]\n"
			"win0:\n"
			"  state:%d, "
			"  fmt:%s, "
			"  y_vir:%d, "
			"  uv_vir:%d\n"
			"  xact:%4d, "
			"  yact:%4d, "
			"  dsp_x:%4d, "
			"  dsp_y:%4d, "
			"  x_st:%4d, "
			"  y_st:%4d\n"
			"  y_h_fac:%8d, "
			"  y_v_fac:%8d, "
			"  uv_h_fac:%8d, "
			"  uv_v_fac:%8d\n"
			"  y_addr: 0x%08x, "
			"  uv_addr:0x%08x\n"
			"win1:\n"
			"  state:%d, "
			"  fmt:%s, "
			"  y_vir:%d, "
			"  uv_vir:%d\n"
			"  xact:%4d, "
			"  yact:%4d, "
			"  dsp_x:%4d, "
			"  dsp_y:%4d, "
			"  x_st:%4d, "
			"  y_st:%4d\n"
			"  y_h_fac:%8d, "
			"  y_v_fac:%8d, "
			"  uv_h_fac:%8d, "
			"  uv_v_fac:%8d\n"
			"  y_addr: 0x%08x, "
			"  uv_addr:0x%08x\n"	
			"win2:\n"
			"  state:%d\n"
			"  fmt:%s\n"
			"  area0:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d,"
			"  addr:0x%08x\n"
			"  area1:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d,"
			"  addr:0x%08x\n"
			"  area2:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d,"
			"  addr:0x%08x\n"
			"  area3:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d,"
			"  addr:0x%08x\n"
			"win3:\n"
			"  state:%d\n"
			"  fmt:%s\n"
			"  area0:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d,"
			"  addr:0x%08x\n"
			"  area1:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d "
			"  addr:0x%08x\n"
			"  area2:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d,"
			"  addr:0x%08x\n"
			"  area3:"
			"  state:%d,"
			"  y_vir:%4d,"
			"  dsp_x:%4d,"
			"  dsp_y:%4d,"
			"  x_st:%4d,"
			"  y_st:%4d,"
			"  addr:0x%08x\n",
			layer3_sel,layer2_sel,layer1_sel,layer0_sel,
			w0_state,format_w0,w0_vir_y,w0_vir_uv,w0_act_x,w0_act_y,
			w0_dsp_x,w0_dsp_y,w0_st_x-h_pw_bp,w0_st_y-v_pw_bp,w0_y_h_fac,w0_y_v_fac,w0_uv_h_fac,
			w0_uv_v_fac,lcdc_readl(lcdc_dev, WIN0_YRGB_MST),
			lcdc_readl(lcdc_dev, WIN0_CBR_MST),

			w1_state,format_w1,w1_vir_y,w1_vir_uv,w1_act_x,w1_act_y,
			w1_dsp_x,w1_dsp_y,w1_st_x-h_pw_bp,w1_st_y-v_pw_bp,w1_y_h_fac,w1_y_v_fac,w1_uv_h_fac,
			w1_uv_v_fac,lcdc_readl(lcdc_dev, WIN1_YRGB_MST),
			lcdc_readl(lcdc_dev, WIN1_CBR_MST),			

			w2_state,format_w2,
			w2_0_state,w2_0_vir_y,w2_0_dsp_x,w2_0_dsp_y,
			w2_0_st_x-h_pw_bp,w2_0_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN2_MST0),

			w2_1_state,w2_1_vir_y,w2_1_dsp_x,w2_1_dsp_y,
			w2_1_st_x-h_pw_bp,w2_1_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN2_MST1),

			w2_2_state,w2_2_vir_y,w2_2_dsp_x,w2_2_dsp_y,
			w2_2_st_x-h_pw_bp,w2_2_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN2_MST2),

			w2_3_state,w2_3_vir_y,w2_3_dsp_x,w2_3_dsp_y,
			w2_3_st_x-h_pw_bp,w2_3_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN2_MST3),
			
			w3_state,format_w3,
			w3_0_state,w3_0_vir_y,w3_0_dsp_x,w3_0_dsp_y,
			w3_0_st_x-h_pw_bp,w3_0_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN3_MST0),

			w3_1_state,w3_1_vir_y,w3_1_dsp_x,w3_1_dsp_y,
			w3_1_st_x-h_pw_bp,w3_1_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN3_MST1),

			w3_2_state,w3_2_vir_y,w3_2_dsp_x,w3_2_dsp_y,
			w3_2_st_x-h_pw_bp,w3_2_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN3_MST2),

			w3_3_state,w3_3_vir_y,w3_3_dsp_x,w3_3_dsp_y,
			w3_3_st_x-h_pw_bp,w3_3_st_y-v_pw_bp,lcdc_readl(lcdc_dev, WIN3_MST3)
	);
			
}

static int rk3288_lcdc_fps_mgr(struct rk_lcdc_driver *dev_drv, int fps,
			       bool set)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u64 ft = 0;
	u32 dotclk;
	int ret;
	u32 pixclock;
	u32 x_total, y_total;
	if (set) {
		if (fps == 0) {
			dev_info(dev_drv->dev, "unsupport set fps=0\n");
			return 0;
		}
		ft = div_u64(1000000000000llu, fps);
		x_total =
		    screen->mode.upper_margin + screen->mode.lower_margin +
		    screen->mode.yres + screen->mode.vsync_len;
		y_total =
		    screen->mode.left_margin + screen->mode.right_margin +
		    screen->mode.xres + screen->mode.hsync_len;
		dev_drv->pixclock = div_u64(ft, x_total * y_total);
		dotclk = div_u64(1000000000000llu, dev_drv->pixclock);
		ret = clk_set_rate(lcdc_dev->dclk, dotclk);
	}

	pixclock = div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	dev_drv->pixclock = lcdc_dev->pixclock = pixclock;
	fps = rk_fb_calc_fps(lcdc_dev->screen, pixclock);
	screen->ft = 1000 / fps;	/*one frame time in ms */

	if (set)
		dev_info(dev_drv->dev, "%s:dclk:%lu,fps:%d\n", __func__,
			 clk_get_rate(lcdc_dev->dclk), fps);

	return fps;
}

static int rk3288_fb_win_remap(struct rk_lcdc_driver *dev_drv, u16 order)
{
	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (order == FB_DEFAULT_ORDER)
		order = FB0_WIN0_FB1_WIN1_FB2_WIN2_FB3_WIN3;
	dev_drv->fb3_win_id = order / 1000;
	dev_drv->fb2_win_id = (order / 100) % 10;
	dev_drv->fb1_win_id = (order / 10) % 10;
	dev_drv->fb0_win_id = order % 10;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return 0;
}

static int rk3288_lcdc_get_win_id(struct rk_lcdc_driver *dev_drv,
				  const char *id)
{
	int win_id = 0;
	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (!strcmp(id, "fb0") || !strcmp(id, "fb4"))
		win_id = dev_drv->fb0_win_id;
	else if (!strcmp(id, "fb1") || !strcmp(id, "fb5"))
		win_id = dev_drv->fb1_win_id;
	else if (!strcmp(id, "fb2") || !strcmp(id, "fb6"))
		win_id = dev_drv->fb2_win_id;
	else if (!strcmp(id, "fb3") || !strcmp(id, "fb7"))
		win_id = dev_drv->fb3_win_id;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return win_id;
}

static int rk3288_set_dsp_lut(struct rk_lcdc_driver *dev_drv, int *lut)
{
	int i,j;
	int __iomem *c;
	int v, r, g, b;
	int ret = 0;

	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN, v_DSP_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	mdelay(25);
	if (dev_drv->cur_screen->dsp_lut) {
		for (i = 0; i < 256; i++) {
			v = dev_drv->cur_screen->dsp_lut[i] = lut[i];
			c = lcdc_dev->dsp_lut_addr_base + (i << 2);
			b = (v & 0xff) << 2;
			g = (v & 0xff00) << 4;
			r = (v & 0xff0000) << 6;
			v = r + g + b;
			for (j = 0; j < 4; j++) {
				writel_relaxed(v, c);
				v += (1 + (1 << 10) + (1 << 20)) ;
				c++;
			}
		}
	} else {
		dev_err(dev_drv->dev, "no buffer to backup lut data!\n");
		ret = -1;
	}
	
	do{
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN, v_DSP_LUT_EN(1));
		lcdc_cfg_done(lcdc_dev);
	}while(!lcdc_read_bit(lcdc_dev,DSP_CTRL1,m_DSP_LUT_EN));
	return ret;
}

static int rk3288_lcdc_config_done(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int i;
	unsigned int mask, val;
	struct rk_lcdc_win *win = NULL;
	spin_lock(&lcdc_dev->reg_lock);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN,
			     v_STANDBY_EN(lcdc_dev->standby));
	for (i=0;i<4;i++) {
		win = dev_drv->win[i];
		if ((win->state == 0)&&(win->last_state == 1)) {
			switch (win->id) {
			case 0:
				lcdc_writel(lcdc_dev,WIN0_CTRL1,0x0);
				mask =  m_WIN0_EN;
				val  =  v_WIN0_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask,val);	
				break;
			case 1:
				lcdc_writel(lcdc_dev,WIN1_CTRL1,0x0);
				mask =  m_WIN1_EN;
				val  =  v_WIN1_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN1_CTRL0, mask,val);		
				break;
			case 2:
				mask =  m_WIN2_EN | m_WIN2_MST0_EN | m_WIN2_MST1_EN |
					m_WIN2_MST2_EN | m_WIN2_MST3_EN;
				val  =  v_WIN2_EN(0) | v_WIN2_MST0_EN(0) | v_WIN2_MST1_EN(0) |
					v_WIN2_MST2_EN(0) | v_WIN2_MST3_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN2_CTRL0, mask,val);			
				break;
			case 3:
				mask =  m_WIN3_EN | m_WIN3_MST0_EN | m_WIN3_MST1_EN |
					m_WIN3_MST2_EN | m_WIN3_MST3_EN;
				val  =  v_WIN3_EN(0) | v_WIN3_MST0_EN(0) |  v_WIN3_MST1_EN(0) |
					v_WIN3_MST2_EN(0) | v_WIN3_MST3_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN3_CTRL0, mask,val);
				break;
			default:
				break;
			}
		}	
		win->last_state = win->state;
	}
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}


static int rk3288_lcdc_dpi_open(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	spin_lock(&lcdc_dev->reg_lock);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DIRECT_PATH_EN,
		     v_DIRECT_PATH_EN(open));
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3288_lcdc_dpi_win_sel(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);
	spin_lock(&lcdc_dev->reg_lock);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DIRECT_PATCH_SEL,
		     v_DIRECT_PATCH_SEL(win_id));
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;

}

static int rk3288_lcdc_dpi_status(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int ovl;
	spin_lock(&lcdc_dev->reg_lock);
	ovl = lcdc_read_bit(lcdc_dev, SYS_CTRL, m_DIRECT_PATH_EN);
	spin_unlock(&lcdc_dev->reg_lock);
	return ovl;
}
static int rk3288_lcdc_set_irq_to_cpu(struct rk_lcdc_driver * dev_drv,int enable)
{
       struct lcdc_device *lcdc_dev =
                                container_of(dev_drv,struct lcdc_device,driver);
       if (enable)
               enable_irq(lcdc_dev->irq);
       else
               disable_irq(lcdc_dev->irq);
       return 0;
}

int rk3288_lcdc_poll_vblank(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 int_reg;
	int ret;

	if (lcdc_dev->clk_on &&(!dev_drv->suspend_flag)){
		int_reg = lcdc_readl(lcdc_dev, INTR_CTRL0);
		if (int_reg & m_LINE_FLAG_INTR_STS) {
			lcdc_dev->driver.frame_time.last_framedone_t =
					lcdc_dev->driver.frame_time.framedone_t;
			lcdc_dev->driver.frame_time.framedone_t = cpu_clock(0);
			lcdc_msk_reg(lcdc_dev, INTR_CTRL0, m_LINE_FLAG_INTR_CLR,
				     v_LINE_FLAG_INTR_CLR(1));
			ret = RK_LF_STATUS_FC;
		} else
			ret = RK_LF_STATUS_FR;
	} else {
		ret = RK_LF_STATUS_NC;
	}

	return ret;
}
static int rk3288_lcdc_get_dsp_addr(struct rk_lcdc_driver *dev_drv,unsigned int *dsp_addr)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on){
		dsp_addr[0] = lcdc_readl(lcdc_dev, WIN0_YRGB_MST);
		dsp_addr[1] = lcdc_readl(lcdc_dev, WIN1_YRGB_MST);
		dsp_addr[2] = lcdc_readl(lcdc_dev, WIN2_MST0);
		dsp_addr[3] = lcdc_readl(lcdc_dev, WIN3_MST0);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static struct lcdc_cabc_mode cabc_mode[4] = {
/* pixel_num, stage_up, stage_down */
	{5,	128,	0},	/*mode 1*/
	{10,	128,	0},	/*mode 2*/
	{15,	128,	0},	/*mode 3*/
	{20,	128,	0},	/*mode 4*/
};

static int rk3288_lcdc_set_dsp_cabc(struct rk_lcdc_driver *dev_drv, int mode)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u32 total_pixel, calc_pixel, stage_up, stage_down, pixel_num;
	u32 mask = 0, val = 0, cabc_en = 0;
	u32 max_mode_num = sizeof(cabc_mode) / sizeof(struct lcdc_cabc_mode);

	dev_drv->cabc_mode = mode;

	/* iomux connect to vop or pwm */
	if (mode == 0) {
		DBG(3, "close cabc and select rk pwm\n");
		val = 0x30001;
		writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_GPIO7A_IOMUX);
		cabc_en = 0;
	} else if (mode > 0 && mode <= max_mode_num) {
		DBG(3, "open cabc and select vop pwm\n");
		val = (dev_drv->id == 0) ? 0x30002 : 0x30003;
		writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_GPIO7A_IOMUX);
		cabc_en = 1;
	} else if (mode > 0x10 && mode <= (max_mode_num + 0x10)) {
		DBG(3, "open cabc and select rk pwm\n");
		val = 0x30001;
		writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_GPIO7A_IOMUX);
		cabc_en = 1;
		mode -= 0x10;
	} else if (mode == 0xff) {
		DBG(3, "close cabc and select vop pwm\n");
		val = (dev_drv->id == 0) ? 0x30002 : 0x30003;
		writel_relaxed(val, RK_GRF_VIRT + RK3288_GRF_GPIO7A_IOMUX);
		cabc_en = 0;
	} else {
		dev_err(lcdc_dev->dev, "invalid cabc mode value:%d", mode);
		return 0;
	}

	if (cabc_en == 0) {
		spin_lock(&lcdc_dev->reg_lock);
		if(lcdc_dev->clk_on) {
			lcdc_msk_reg(lcdc_dev, CABC_CTRL0, m_CABC_EN, v_CABC_EN(0));
			lcdc_cfg_done(lcdc_dev);
		}
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}

	total_pixel = screen->mode.xres * screen->mode.yres;
        pixel_num = 1000 - (cabc_mode[mode - 1].pixel_num);
	calc_pixel = (total_pixel * pixel_num) / 1000;
	stage_up = cabc_mode[mode - 1].stage_up;
	stage_down = cabc_mode[mode - 1].stage_down;
	
	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on) {
		mask = m_CABC_TOTAL_NUM | m_CABC_STAGE_DOWN;
		val = v_CABC_TOTAL_NUM(total_pixel) | v_CABC_STAGE_DOWN(stage_down);
		lcdc_msk_reg(lcdc_dev, CABC_CTRL1, mask, val);

		mask = m_CABC_EN | m_CABC_CALC_PIXEL_NUM |
			m_CABC_STAGE_UP;
		val = v_CABC_EN(1) | v_CABC_CALC_PIXEL_NUM(calc_pixel) |
			v_CABC_STAGE_UP(stage_up);
		lcdc_msk_reg(lcdc_dev, CABC_CTRL0, mask, val);
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}
/*
	a:[-30~0]:
	    sin_hue = sin(a)*256 +0x100;
	    cos_hue = cos(a)*256;
	a:[0~30]
	    sin_hue = sin(a)*256;
	    cos_hue = cos(a)*256;
*/
static int rk3288_lcdc_get_bcsh_hue(struct rk_lcdc_driver *dev_drv,bcsh_hue_mode mode)
{

	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val;
			
	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		val = lcdc_readl(lcdc_dev, BCSH_H);
		switch(mode){
		case H_SIN:
			val &= m_BCSH_SIN_HUE;
			break;
		case H_COS:
			val &= m_BCSH_COS_HUE;
			val >>= 16;
			break;
		default:
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return val;
}


static int rk3288_lcdc_set_bcsh_hue(struct rk_lcdc_driver *dev_drv,int sin_hue, int cos_hue)
{

	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		mask = m_BCSH_SIN_HUE | m_BCSH_COS_HUE;
		val = v_BCSH_SIN_HUE(sin_hue) | v_BCSH_COS_HUE(cos_hue);
		lcdc_msk_reg(lcdc_dev, BCSH_H, mask, val);
		lcdc_cfg_done(lcdc_dev);
	}	
	spin_unlock(&lcdc_dev->reg_lock);
	
	return 0;
}

static int rk3288_lcdc_set_bcsh_bcs(struct rk_lcdc_driver *dev_drv,bcsh_bcs_mode mode,int value)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;
	
	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on) {
		switch (mode) {
		case BRIGHTNESS:
		/*from 0 to 255,typical is 128*/
			if (value < 0x80)
				value += 0x80;
			else if (value >= 0x80)
				value = value - 0x80;
			mask =  m_BCSH_BRIGHTNESS;
			val = v_BCSH_BRIGHTNESS(value);
			break;
		case CONTRAST:
		/*from 0 to 510,typical is 256*/
			mask =  m_BCSH_CONTRAST;
			val =  v_BCSH_CONTRAST(value);
			break;
		case SAT_CON:
		/*from 0 to 1015,typical is 256*/
			mask = m_BCSH_SAT_CON;
			val = v_BCSH_SAT_CON(value);
			break;
		default:
			break;
		}
		lcdc_msk_reg(lcdc_dev, BCSH_BCS, mask, val);
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return val;
}

static int rk3288_lcdc_get_bcsh_bcs(struct rk_lcdc_driver *dev_drv,bcsh_bcs_mode mode)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val;

	spin_lock(&lcdc_dev->reg_lock);
	if(lcdc_dev->clk_on) {
		val = lcdc_readl(lcdc_dev, BCSH_BCS);
		switch (mode) {
		case BRIGHTNESS:
			val &= m_BCSH_BRIGHTNESS;
			if(val > 0x80)
				val -= 0x80;
			else
				val += 0x80;
			break;
		case CONTRAST:
			val &= m_BCSH_CONTRAST;
			val >>= 8;
			break;
		case SAT_CON:
			val &= m_BCSH_SAT_CON;
			val >>= 20;
			break;
		default:
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return val;
}


static int rk3288_lcdc_open_bcsh(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (open) {
			lcdc_writel(lcdc_dev,BCSH_COLOR_BAR,0x1);
			lcdc_writel(lcdc_dev,BCSH_BCS,0xd0010000);
			lcdc_writel(lcdc_dev,BCSH_H,0x01000000);
		} else {
			mask = m_BCSH_EN;
			val = v_BCSH_EN(0);
			lcdc_msk_reg(lcdc_dev, BCSH_COLOR_BAR, mask, val);
		}
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3288_lcdc_set_bcsh(struct rk_lcdc_driver *dev_drv,
				     bool enable)
{
	if (!enable || !dev_drv->bcsh.enable) {
		rk3288_lcdc_open_bcsh(dev_drv, false);
		return 0;
	}

	if (dev_drv->bcsh.brightness <= 255 ||
	    dev_drv->bcsh.contrast <= 510 ||
	    dev_drv->bcsh.sat_con <= 1015 ||
	    (dev_drv->bcsh.sin_hue <= 511 && dev_drv->bcsh.cos_hue <= 511)) {
		rk3288_lcdc_open_bcsh(dev_drv, true);
		if (dev_drv->bcsh.brightness <= 255)
			rk3288_lcdc_set_bcsh_bcs(dev_drv, BRIGHTNESS,
						 dev_drv->bcsh.brightness);
		if (dev_drv->bcsh.contrast <= 510)
			rk3288_lcdc_set_bcsh_bcs(dev_drv, CONTRAST,
						 dev_drv->bcsh.contrast);
		if (dev_drv->bcsh.sat_con <= 1015)
			rk3288_lcdc_set_bcsh_bcs(dev_drv, SAT_CON,
						 dev_drv->bcsh.sat_con);
		if (dev_drv->bcsh.sin_hue <= 511 &&
		    dev_drv->bcsh.cos_hue <= 511)
			rk3288_lcdc_set_bcsh_hue(dev_drv,
						 dev_drv->bcsh.sin_hue,
						 dev_drv->bcsh.cos_hue);
	}
	return 0;
}

static struct rk_lcdc_win lcdc_win[] = {
	[0] = {
	       .name = "win0",
	       .id = 0,
	       .support_3d = false,
	       },
	[1] = {
	       .name = "win1",
	       .id = 1,
	       .support_3d = false,
	       },
	[2] = {
	       .name = "win2",
	       .id = 2,
	       .support_3d = false,
	       },
	[3] = {
	       .name = "win3",
	       .id = 3,
	       .support_3d = false,
	       },	       
};

static struct rk_lcdc_drv_ops lcdc_drv_ops = {
	.open 			= rk3288_lcdc_open,
	.win_direct_en		= rk3288_lcdc_win_direct_en,
	.load_screen 		= rk3288_load_screen,
	.set_par 		= rk3288_lcdc_set_par,
	.pan_display 		= rk3288_lcdc_pan_display,
	.direct_set_addr 	= rk3288_lcdc_direct_set_win_addr,
	.lcdc_reg_update	= rk3288_lcdc_reg_update,
	.blank 			= rk3288_lcdc_blank,
	.ioctl 			= rk3288_lcdc_ioctl,
	.suspend 		= rk3288_lcdc_early_suspend,
	.resume 		= rk3288_lcdc_early_resume,
	.get_win_state 		= rk3288_lcdc_get_win_state,
	.ovl_mgr 		= rk3288_lcdc_ovl_mgr,
	.get_disp_info 		= rk3288_lcdc_get_disp_info,
	.fps_mgr 		= rk3288_lcdc_fps_mgr,
	.fb_get_win_id 		= rk3288_lcdc_get_win_id,
	.fb_win_remap 		= rk3288_fb_win_remap,
	.set_dsp_lut 		= rk3288_set_dsp_lut,
	.poll_vblank 		= rk3288_lcdc_poll_vblank,
	.dpi_open 		= rk3288_lcdc_dpi_open,
	.dpi_win_sel 		= rk3288_lcdc_dpi_win_sel,
	.dpi_status 		= rk3288_lcdc_dpi_status,
	.get_dsp_addr 		= rk3288_lcdc_get_dsp_addr,
	.set_dsp_cabc 		= rk3288_lcdc_set_dsp_cabc,
	.set_dsp_bcsh_hue 	= rk3288_lcdc_set_bcsh_hue,
	.set_dsp_bcsh_bcs 	= rk3288_lcdc_set_bcsh_bcs,
	.get_dsp_bcsh_hue 	= rk3288_lcdc_get_bcsh_hue,
	.get_dsp_bcsh_bcs 	= rk3288_lcdc_get_bcsh_bcs,
	.open_bcsh		= rk3288_lcdc_open_bcsh,
	.dump_reg 		= rk3288_lcdc_reg_dump,
	.cfg_done		= rk3288_lcdc_config_done,
	.set_irq_to_cpu  	= rk3288_lcdc_set_irq_to_cpu,
};

#ifdef LCDC_IRQ_DEBUG
static int rk3288_lcdc_parse_irq(struct lcdc_device *lcdc_dev,unsigned int reg_val)
{
	if (reg_val & m_WIN0_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, m_WIN0_EMPTY_INTR_CLR,
			     v_WIN0_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"win0 empty irq!");
	}else if (reg_val & m_WIN1_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, m_WIN1_EMPTY_INTR_CLR,
			     v_WIN1_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"win1 empty irq!");
	}else if (reg_val & m_WIN2_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, m_WIN2_EMPTY_INTR_CLR,
			     v_WIN2_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"win2 empty irq!");
	}else if (reg_val & m_WIN3_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, m_WIN3_EMPTY_INTR_CLR,
			     v_WIN3_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"win3 empty irq!");
	}else if (reg_val & m_HWC_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, m_HWC_EMPTY_INTR_CLR,
			     v_HWC_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"HWC empty irq!");
	}else if (reg_val & m_POST_BUF_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, m_POST_BUF_EMPTY_INTR_CLR,
			     v_POST_BUF_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"post buf empty irq!");
	}else if (reg_val & m_PWM_GEN_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, INTR_CTRL1, m_PWM_GEN_INTR_CLR,
			     v_PWM_GEN_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"PWM gen irq!");
	}

	return 0;
}
#endif

static irqreturn_t rk3288_lcdc_isr(int irq, void *dev_id)
{
	struct lcdc_device *lcdc_dev =
	    (struct lcdc_device *)dev_id;
	ktime_t timestamp = ktime_get();
	u32 intr0_reg;

	intr0_reg = lcdc_readl(lcdc_dev, INTR_CTRL0);

	if(intr0_reg & m_FS_INTR_STS){
		timestamp = ktime_get();
		lcdc_msk_reg(lcdc_dev, INTR_CTRL0, m_FS_INTR_CLR,
			     v_FS_INTR_CLR(1));
		/*if(lcdc_dev->driver.wait_fs){	*/
		if (0) {
			spin_lock(&(lcdc_dev->driver.cpl_lock));
			complete(&(lcdc_dev->driver.frame_done));
			spin_unlock(&(lcdc_dev->driver.cpl_lock));
		}
#ifdef CONFIG_DRM_ROCKCHIP
		lcdc_dev->driver.irq_call_back(&lcdc_dev->driver);
#endif 
		lcdc_dev->driver.vsync_info.timestamp = timestamp;
		wake_up_interruptible_all(&lcdc_dev->driver.vsync_info.wait);

	}else if(intr0_reg & m_LINE_FLAG_INTR_STS){
		lcdc_dev->driver.frame_time.last_framedone_t =
				lcdc_dev->driver.frame_time.framedone_t;
		lcdc_dev->driver.frame_time.framedone_t = cpu_clock(0);
		lcdc_msk_reg(lcdc_dev, INTR_CTRL0, m_LINE_FLAG_INTR_CLR,
			     v_LINE_FLAG_INTR_CLR(1));
	}else if(intr0_reg & m_BUS_ERROR_INTR_STS){
		lcdc_msk_reg(lcdc_dev, INTR_CTRL0, m_BUS_ERROR_INTR_CLR,
			     v_BUS_ERROR_INTR_CLR(1));
		dev_warn(lcdc_dev->dev,"buf_error_int!");
	}

	/* for win empty debug */
#ifdef LCDC_IRQ_EMPTY_DEBUG
	intr1_reg = lcdc_readl(lcdc_dev, INTR_CTRL1);
	if (intr1_reg != 0) {
		rk3288_lcdc_parse_irq(lcdc_dev,intr1_reg);
	}
#endif
	return IRQ_HANDLED;
}

#if defined(CONFIG_PM)
static int rk3288_lcdc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int rk3288_lcdc_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define rk3288_lcdc_suspend NULL
#define rk3288_lcdc_resume  NULL
#endif

static int rk3288_lcdc_parse_dt(struct lcdc_device *lcdc_dev)
{
	struct device_node *np = lcdc_dev->dev->of_node;
	struct rk_lcdc_driver *dev_drv = &lcdc_dev->driver;
	int val;

	if (of_property_read_u32(np, "rockchip,prop", &val))
		lcdc_dev->prop = PRMRY;	/*default set it as primary */
	else
		lcdc_dev->prop = val;

	if (of_property_read_u32(np, "rockchip,mirror", &val))
		dev_drv->rotate_mode = NO_MIRROR;
	else
		dev_drv->rotate_mode = val;

	if (of_property_read_u32(np, "rockchip,cabc_mode", &val))
		dev_drv->cabc_mode = 0;	/* default set close cabc */
	else
		dev_drv->cabc_mode = val;

	if (of_property_read_u32(np, "rockchip,pwr18", &val))
		lcdc_dev->pwr18 = false;	/*default set it as 3.xv power supply */
	else
		lcdc_dev->pwr18 = (val ? true : false);

	if (of_property_read_u32(np, "rockchip,fb-win-map", &val))
		dev_drv->fb_win_map = FB_DEFAULT_ORDER;
	else
		dev_drv->fb_win_map = val;

	if (of_property_read_u32(np, "rockchip,bcsh-en", &val))
		dev_drv->bcsh.enable = false;
	else
		dev_drv->bcsh.enable = (val ? true : false);

	if (of_property_read_u32(np, "rockchip,brightness", &val))
		dev_drv->bcsh.brightness = 0xffff;
	else
		dev_drv->bcsh.brightness = val;

	if (of_property_read_u32(np, "rockchip,contrast", &val))
		dev_drv->bcsh.contrast = 0xffff;
	else
		dev_drv->bcsh.contrast = val;

	if (of_property_read_u32(np, "rockchip,sat-con", &val))
		dev_drv->bcsh.sat_con = 0xffff;
	else
		dev_drv->bcsh.sat_con = val;

	if (of_property_read_u32(np, "rockchip,hue", &val)) {
		dev_drv->bcsh.sin_hue = 0xffff;
		dev_drv->bcsh.cos_hue = 0xffff;
	} else {
		dev_drv->bcsh.sin_hue = val & 0xff;
		dev_drv->bcsh.cos_hue = (val >> 8) & 0xff;
	}

#if defined(CONFIG_ROCKCHIP_IOMMU)
	if (of_property_read_u32(np, "rockchip,iommu-enabled", &val))
		dev_drv->iommu_enabled = 0;
	else
		dev_drv->iommu_enabled = val;
#else
	dev_drv->iommu_enabled = 0;
#endif
	return 0;
}

static int rk3288_lcdc_probe(struct platform_device *pdev)
{
	struct lcdc_device *lcdc_dev = NULL;
	struct rk_lcdc_driver *dev_drv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	int prop;
	int ret = 0;

	/*if the primary lcdc has not registered ,the extend
	   lcdc register later */
	of_property_read_u32(np, "rockchip,prop", &prop);
	if (prop == EXTEND) {
		if (!is_prmry_rk_lcdc_registered())
			return -EPROBE_DEFER;
	}
	lcdc_dev = devm_kzalloc(dev,
				sizeof(struct lcdc_device), GFP_KERNEL);
	if (!lcdc_dev) {
		dev_err(&pdev->dev, "rk3288 lcdc device kmalloc fail!");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->dev = dev;
	rk3288_lcdc_parse_dt(lcdc_dev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lcdc_dev->reg_phy_base = res->start;
	lcdc_dev->len = resource_size(res);
	lcdc_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(lcdc_dev->regs))
		return PTR_ERR(lcdc_dev->regs);

	lcdc_dev->regsbak = devm_kzalloc(dev, lcdc_dev->len, GFP_KERNEL);
	if (IS_ERR(lcdc_dev->regsbak))
		return PTR_ERR(lcdc_dev->regsbak);
	lcdc_dev->dsp_lut_addr_base = (lcdc_dev->regs + GAMMA_LUT_ADDR);
	lcdc_dev->id = rk3288_lcdc_get_id(lcdc_dev->reg_phy_base);
	if (lcdc_dev->id < 0) {
		dev_err(&pdev->dev, "no such lcdc device!\n");
		return -ENXIO;
	}
	dev_set_name(lcdc_dev->dev, "lcdc%d", lcdc_dev->id);
	dev_drv = &lcdc_dev->driver;
	dev_drv->dev = dev;
	dev_drv->prop = prop;
	dev_drv->id = lcdc_dev->id;
	dev_drv->ops = &lcdc_drv_ops;
	dev_drv->lcdc_win_num = ARRAY_SIZE(lcdc_win);
	spin_lock_init(&lcdc_dev->reg_lock);

	lcdc_dev->irq = platform_get_irq(pdev, 0);
	if (lcdc_dev->irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ for lcdc%d\n",
			lcdc_dev->id);
		return -ENXIO;
	}

	ret = devm_request_irq(dev, lcdc_dev->irq, rk3288_lcdc_isr,
			       IRQF_DISABLED | IRQF_SHARED, dev_name(dev), lcdc_dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot requeset irq %d - err %d\n",
			lcdc_dev->irq, ret);
		return ret;
	}

	if (dev_drv->iommu_enabled) {
		if(lcdc_dev->id == 0){
			strcpy(dev_drv->mmu_dts_name, VOPB_IOMMU_COMPATIBLE_NAME);
		}else{
			strcpy(dev_drv->mmu_dts_name, VOPL_IOMMU_COMPATIBLE_NAME);
		}
	}

	ret = rk_fb_register(dev_drv, lcdc_win, lcdc_dev->id);
	if (ret < 0) {
		dev_err(dev, "register fb for lcdc%d failed!\n", lcdc_dev->id);
		return ret;
	}
	lcdc_dev->screen = dev_drv->screen0;
	dev_info(dev, "lcdc%d probe ok, iommu %s\n",
		lcdc_dev->id, dev_drv->iommu_enabled ? "enabled" : "disabled");

	return 0;
}

static int rk3288_lcdc_remove(struct platform_device *pdev)
{

	return 0;
}

static void rk3288_lcdc_shutdown(struct platform_device *pdev)
{
	struct lcdc_device *lcdc_dev = platform_get_drvdata(pdev);

	rk3288_lcdc_deint(lcdc_dev);
	rk_disp_pwr_disable(&lcdc_dev->driver);
}

#if defined(CONFIG_OF)
static const struct of_device_id rk3288_lcdc_dt_ids[] = {
	{.compatible = "rockchip,rk3288-lcdc",},
	{}
};
#endif

static struct platform_driver rk3288_lcdc_driver = {
	.probe = rk3288_lcdc_probe,
	.remove = rk3288_lcdc_remove,
	.driver = {
		   .name = "rk3288-lcdc",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk3288_lcdc_dt_ids),
		   },
	.suspend = rk3288_lcdc_suspend,
	.resume = rk3288_lcdc_resume,
	.shutdown = rk3288_lcdc_shutdown,
};

static int __init rk3288_lcdc_module_init(void)
{
	return platform_driver_register(&rk3288_lcdc_driver);
}

static void __exit rk3288_lcdc_module_exit(void)
{
	platform_driver_unregister(&rk3288_lcdc_driver);
}

fs_initcall(rk3288_lcdc_module_init);
module_exit(rk3288_lcdc_module_exit);


