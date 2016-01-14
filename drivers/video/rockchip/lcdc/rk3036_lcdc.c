/*
 * drivers/video/rockchip/lcdc/rk3036_lcdc.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 * Author:zhengyang<zhengyang@rock-chips.com>
 * This software is licensed under the terms of the GNU General Public
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
#include <asm/div64.h>
#include <linux/uaccess.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/common.h>
#include <dt-bindings/clock/rk_system_status.h>
#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip-iovmm.h>
#endif
#include "rk3036_lcdc.h"

static int dbg_thresd;
module_param(dbg_thresd, int, S_IRUGO | S_IWUSR);

#define DBG(level, x...) do {			\
	if (unlikely(dbg_thresd >= level))	\
		dev_info(dev_drv->dev, x);		\
	} while (0)

#define grf_writel(offset, v)	do { \
	writel_relaxed(v, RK_GRF_VIRT + offset); \
	dsb(); \
	} while (0)

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
	       .name = "hwc",
	       .id = 2,
	       .support_3d = false,
	       },
};

static irqreturn_t rk3036_lcdc_isr(int irq, void *dev_id)
{
	struct lcdc_device *lcdc_dev =
	    (struct lcdc_device *)dev_id;
	ktime_t timestamp = ktime_get();
	u32 int_reg = lcdc_readl(lcdc_dev, INT_STATUS);

	if (int_reg & m_FS_INT_STA) {
		timestamp = ktime_get();
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_FS_INT_CLEAR,
			     v_FS_INT_CLEAR(1));
		/*if (lcdc_dev->driver.wait_fs) {*/
		if (0) {
			spin_lock(&(lcdc_dev->driver.cpl_lock));
			complete(&(lcdc_dev->driver.frame_done));
			spin_unlock(&(lcdc_dev->driver.cpl_lock));
		}
		lcdc_dev->driver.vsync_info.timestamp = timestamp;
		wake_up_interruptible_all(&lcdc_dev->driver.vsync_info.wait);

	} else if (int_reg & m_LF_INT_STA) {
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_LF_INT_CLEAR,
			     v_LF_INT_CLEAR(1));
	}
	return IRQ_HANDLED;
}

static int rk3036_lcdc_clk_enable(struct lcdc_device *lcdc_dev)
{
#ifdef CONFIG_RK_FPGA
	lcdc_dev->clk_on = 1;
	return 0;
#endif
	if (!lcdc_dev->clk_on) {
		clk_prepare_enable(lcdc_dev->hclk);
		clk_prepare_enable(lcdc_dev->dclk);
		clk_prepare_enable(lcdc_dev->aclk);
/*		clk_prepare_enable(lcdc_dev->pd);*/
		spin_lock(&lcdc_dev->reg_lock);
		lcdc_dev->clk_on = 1;
		spin_unlock(&lcdc_dev->reg_lock);
	}

	return 0;
}

static int rk3036_lcdc_clk_disable(struct lcdc_device *lcdc_dev)
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
/*		clk_disable_unprepare(lcdc_dev->pd);*/
	}

	return 0;
}

static int rk3036_lcdc_enable_irq(struct rk_lcdc_driver *dev_drv)
{
	u32 mask, val;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);
	mask = m_FS_INT_CLEAR | m_FS_INT_EN;
	val = v_FS_INT_CLEAR(1) | v_FS_INT_EN(1);
	lcdc_msk_reg(lcdc_dev, INT_STATUS, mask, val);
	return 0;
}
/*
static int rk3036_lcdc_disable_irq(struct lcdc_device *lcdc_dev)
{
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		mask = m_FS_INT_CLEAR | m_FS_INT_EN;
		val = v_FS_INT_CLEAR(0) | v_FS_INT_EN(0);
		lcdc_msk_reg(lcdc_dev, INT_STATUS, mask, val);
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
	}
	mdelay(1);
	return 0;
}*/

static void rk_lcdc_read_reg_defalut_cfg(struct lcdc_device
					     *lcdc_dev)
{
	int reg = 0;
	u32 value = 0;

	spin_lock(&lcdc_dev->reg_lock);
	for (reg = 0; reg < 0xe0; reg += 4)
		value = lcdc_readl(lcdc_dev, reg);

	spin_unlock(&lcdc_dev->reg_lock);
}

static int rk3036_lcdc_alpha_cfg(struct lcdc_device *lcdc_dev)
{
	int win0_top = 0;
	u32 mask, val;
	enum data_format win0_format = lcdc_dev->driver.win[0]->area[0].format;
	enum data_format win1_format = lcdc_dev->driver.win[1]->area[0].format;

	int win0_alpha_en = ((win0_format == ARGB888) ||
				(win0_format == ABGR888)) ? 1 : 0;
	int win1_alpha_en = ((win1_format == ARGB888) ||
				(win1_format == ABGR888)) ? 1 : 0;
	int atv_layer_cnt = lcdc_dev->driver.win[0]->state +
			lcdc_dev->driver.win[1]->state;
	u32 *_pv = (u32 *)lcdc_dev->regsbak;

	_pv += (DSP_CTRL0 >> 2);
	win0_top = ((*_pv) & (m_WIN0_TOP)) >> 8;

	if (win0_top && (atv_layer_cnt >= 2) && (win0_alpha_en)) {
		mask =  m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN |
			m_WIN1_PREMUL_SCALE;
		val = v_WIN0_ALPHA_EN(1) | v_WIN1_ALPHA_EN(0) |
			v_WIN1_PREMUL_SCALE(0);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);

		mask = m_WIN0_ALPHA_MODE | m_PREMUL_ALPHA_ENABLE |
			m_ALPHA_MODE_SEL1;
		val = v_WIN0_ALPHA_MODE(1) | v_PREMUL_ALPHA_ENABLE(1) |
			v_ALPHA_MODE_SEL1(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
	} else if ((!win0_top) && (atv_layer_cnt >= 2) &&
		   (win1_alpha_en)) {
		mask =  m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN |
			m_WIN1_PREMUL_SCALE;
		val = v_WIN0_ALPHA_EN(0) | v_WIN1_ALPHA_EN(1) |
			v_WIN1_PREMUL_SCALE(0);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);

		mask = m_WIN1_ALPHA_MODE | m_PREMUL_ALPHA_ENABLE |
			m_ALPHA_MODE_SEL1;
		val = v_WIN1_ALPHA_MODE(1) | v_PREMUL_ALPHA_ENABLE(1) |
			v_ALPHA_MODE_SEL1(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
	} else {
		mask = m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN;
		val = v_WIN0_ALPHA_EN(0) | v_WIN1_ALPHA_EN(0);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);
	}

	if (lcdc_dev->driver.win[2]->state == 1) {
		mask =  m_HWC_ALPAH_EN;
		val = v_HWC_ALPAH_EN(1);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);

		mask =  m_HWC_ALPHA_MODE;
		val = v_HWC_ALPHA_MODE(1);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
	} else {
		mask =  m_HWC_ALPAH_EN;
		val = v_HWC_ALPAH_EN(0);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);
	}

	return 0;
}

static void lcdc_layer_update_regs(struct lcdc_device *lcdc_dev,
				   struct rk_lcdc_win *win)
{
	u32 mask, val;
	int hwc_size;

	if (win->state == 1) {
		if (win->id == 0) {
			mask = m_WIN0_EN | m_WIN0_FORMAT | m_WIN0_RB_SWAP;
			val = v_WIN0_EN(win->state) |
			      v_WIN0_FORMAT(win->area[0].fmt_cfg) |
			      v_WIN0_RB_SWAP(win->area[0].swap_rb);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_YRGB,
				    v_X_SCL_FACTOR(win->scale_yrgb_x) |
				    v_Y_SCL_FACTOR(win->scale_yrgb_y));
			lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_CBR,
				    v_X_SCL_FACTOR(win->scale_cbcr_x) |
				    v_Y_SCL_FACTOR(win->scale_cbcr_y));
			lcdc_msk_reg(lcdc_dev, WIN0_VIR,
				     m_YRGB_VIR | m_CBBR_VIR,
				     v_YRGB_VIR(win->area[0].y_vir_stride) |
				     v_CBBR_VIR(win->area[0].uv_vir_stride));
			lcdc_writel(lcdc_dev, WIN0_ACT_INFO,
				    v_ACT_WIDTH(win->area[0].xact) |
				    v_ACT_HEIGHT(win->area[0].yact));
			lcdc_writel(lcdc_dev, WIN0_DSP_ST,
				    v_DSP_STX(win->area[0].dsp_stx) |
				    v_DSP_STY(win->area[0].dsp_sty));
			lcdc_writel(lcdc_dev, WIN0_DSP_INFO,
				    v_DSP_WIDTH(win->post_cfg.xsize) |
				    v_DSP_HEIGHT(win->post_cfg.ysize));

			lcdc_writel(lcdc_dev, WIN0_YRGB_MST,
				    win->area[0].y_addr);
			lcdc_writel(lcdc_dev, WIN0_CBR_MST,
				    win->area[0].uv_addr);
		} else if (win->id == 1) {
			mask = m_WIN1_EN | m_WIN1_FORMAT | m_WIN1_RB_SWAP;
			val = v_WIN1_EN(win->state) |
			      v_WIN1_FORMAT(win->area[0].fmt_cfg) |
			      v_WIN1_RB_SWAP(win->area[0].swap_rb);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			lcdc_writel(lcdc_dev, WIN1_SCL_FACTOR_YRGB,
				    v_X_SCL_FACTOR(win->scale_yrgb_x) |
				    v_Y_SCL_FACTOR(win->scale_yrgb_y));

			lcdc_msk_reg(lcdc_dev, WIN1_VIR, m_YRGB_VIR,
				     v_YRGB_VIR(win->area[0].y_vir_stride));
			lcdc_writel(lcdc_dev, WIN1_ACT_INFO,
				    v_ACT_WIDTH(win->area[0].xact) |
				    v_ACT_HEIGHT(win->area[0].yact));
			lcdc_writel(lcdc_dev, WIN1_DSP_INFO,
				    v_DSP_WIDTH(win->post_cfg.xsize) |
				    v_DSP_HEIGHT(win->post_cfg.ysize));
			lcdc_writel(lcdc_dev, WIN1_DSP_ST,
				    v_DSP_STX(win->area[0].dsp_stx) |
				    v_DSP_STY(win->area[0].dsp_sty));
			lcdc_writel(lcdc_dev, WIN1_MST, win->area[0].y_addr);
		} else if (win->id == 2) {
			mask = m_HWC_EN | m_HWC_LODAD_EN;
			val = v_HWC_EN(win->state) | v_HWC_LODAD_EN(1);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			if ((win->area[0].xsize == 32) &&
			    (win->area[0].ysize == 32))
				hwc_size = 0;
			else if ((win->area[0].xsize == 64) &&
				 (win->area[0].ysize == 64))
				hwc_size = 1;
			else
				dev_err(lcdc_dev->dev,
					"unsupport hwc size:x=%d,y=%d\n",
					win->area[0].xsize,
					win->area[0].ysize);
			lcdc_writel(lcdc_dev, HWC_DSP_ST,
				    v_DSP_STX(win->area[0].dsp_stx) |
				    v_DSP_STY(win->area[0].dsp_sty));
			lcdc_writel(lcdc_dev, HWC_MST, win->area[0].y_addr);
		}
	} else {
		win->area[0].y_addr = 0;
		win->area[0].uv_addr = 0;
		if (win->id == 0) {
			lcdc_msk_reg(lcdc_dev,
				     SYS_CTRL, m_WIN0_EN, v_WIN0_EN(0));
			lcdc_writel(lcdc_dev, WIN0_YRGB_MST,
				    win->area[0].y_addr);
			lcdc_writel(lcdc_dev, WIN0_CBR_MST,
				    win->area[0].uv_addr);
		} else if (win->id == 1) {
			lcdc_msk_reg(lcdc_dev,
				     SYS_CTRL, m_WIN1_EN, v_WIN1_EN(0));
			lcdc_writel(lcdc_dev, WIN1_MST, win->area[0].y_addr);
		} else if (win->id == 2) {
			lcdc_msk_reg(lcdc_dev,
				     SYS_CTRL, m_HWC_EN | m_HWC_LODAD_EN,
				     v_HWC_EN(0) | v_HWC_LODAD_EN(0));
			lcdc_writel(lcdc_dev, HWC_MST, win->area[0].y_addr);
		}
	}
	rk3036_lcdc_alpha_cfg(lcdc_dev);
}

static void lcdc_layer_enable(struct lcdc_device *lcdc_dev,
			      unsigned int win_id, bool open)
{
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on) &&
	    lcdc_dev->driver.win[win_id]->state != open) {
		if (open) {
			if (!lcdc_dev->atv_layer_cnt) {
				dev_info(lcdc_dev->dev,
					 "wakeup from standby!\n");
				lcdc_dev->standby = 0;
			}
			lcdc_dev->atv_layer_cnt |= (1 << win_id);
		} else if ((lcdc_dev->atv_layer_cnt & (1 << win_id)) && (!open)) {
			lcdc_dev->atv_layer_cnt &= ~(1 << win_id);
		}
		lcdc_dev->driver.win[win_id]->state = open;
		if (!open) {
			lcdc_layer_update_regs(lcdc_dev,
					       lcdc_dev->driver.win[win_id]);
			lcdc_cfg_done(lcdc_dev);
		}
		/*if no layer used,disable lcdc*/
		if (!lcdc_dev->atv_layer_cnt) {
			dev_info(lcdc_dev->dev,
				 "no layer is used, go to standby!\n");
			lcdc_dev->standby = 1;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
}
/*
static int rk3036_lcdc_reg_update(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win0 = lcdc_dev->driver.win[0];
	struct rk_lcdc_win *win1 = lcdc_dev->driver.win[1];
	int timeout;
	unsigned long flags;

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY,
			     v_LCDC_STANDBY(lcdc_dev->standby));
		lcdc_layer_update_regs(lcdc_dev, win0);
		lcdc_layer_update_regs(lcdc_dev, win1);
		rk3036_lcdc_alpha_cfg(lcdc_dev);
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	if (0) {
		spin_lock_irqsave(&dev_drv->cpl_lock, flags);
		init_completion(&dev_drv->frame_done);
		spin_unlock_irqrestore(&dev_drv->cpl_lock, flags);
		timeout = wait_for_completion_timeout(&dev_drv->frame_done,
						      msecs_to_jiffies
						      (dev_drv->cur_screen->ft
						       + 5));
		if (!timeout && (!dev_drv->frame_done.done)) {
			dev_warn(lcdc_dev->dev,
				 "wait for new frame start time out!\n");
			return -ETIMEDOUT;
		}
	}
	DBG(2, "%s for lcdc%d\n", __func__, lcdc_dev->id);
	return 0;
}
*/
static void rk3036_lcdc_reg_restore(struct lcdc_device *lcdc_dev)
{
	memcpy((u8 *)lcdc_dev->regs, (u8 *)lcdc_dev->regsbak, 0xe0);
}

static void rk3036_lcdc_mmu_en(struct rk_lcdc_driver *dev_drv)
{
	u32 mask, val;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	/*spin_lock(&lcdc_dev->reg_lock);*/
	if (likely(lcdc_dev->clk_on)) {
		mask = m_MMU_EN | m_AXI_MAX_OUTSTANDING_EN |
			m_AXI_OUTSTANDING_MAX_NUM;
		val = v_MMU_EN(1) | v_AXI_OUTSTANDING_MAX_NUM(31) |
			v_AXI_MAX_OUTSTANDING_EN(1);
		lcdc_msk_reg(lcdc_dev, AXI_BUS_CTRL, mask, val);
	}
	/*spin_unlock(&lcdc_dev->reg_lock);*/
}

static int rk3036_lcdc_set_hwc_lut(struct rk_lcdc_driver *dev_drv,
				   int *hwc_lut, int mode)
{
	int i = 0;
	int __iomem *c;
	int v;
	int len = 256*4;

	struct lcdc_device *lcdc_dev =
			container_of(dev_drv, struct lcdc_device, driver);
	if (dev_drv->hwc_lut == NULL)
		dev_drv->hwc_lut = devm_kzalloc(lcdc_dev->dev, len, GFP_KERNEL);

	spin_lock(&lcdc_dev->reg_lock);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_HWC_LUT_EN, v_HWC_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	mdelay(25);
	for (i = 0; i < 256; i++) {
		if (mode == 1)
			dev_drv->hwc_lut[i] = hwc_lut[i];
		v = dev_drv->hwc_lut[i];
		c = lcdc_dev->hwc_lut_addr_base + i;
		writel_relaxed(v, c);
	}
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_HWC_LUT_EN, v_HWC_LUT_EN(1));
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

#if 0
static int rk3036_lcdc_set_dclk(struct rk_lcdc_driver *dev_drv)
{
#ifdef CONFIG_RK_FPGA
	return 0;
#endif
	int ret, fps;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;

	ret = clk_set_rate(lcdc_dev->dclk, screen->mode.pixclock);
	if (ret)
		dev_err(dev_drv->dev,
			"set lcdc%d dclk failed\n", lcdc_dev->id);
	lcdc_dev->pixclock =
		 div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	lcdc_dev->driver.pixclock = lcdc_dev->pixclock;

	fps = rk_fb_calc_fps(screen, lcdc_dev->pixclock);
	screen->ft = 1000 / fps;
	dev_info(lcdc_dev->dev, "%s: dclk:%lu>>fps:%d ",
		 lcdc_dev->driver.name, clk_get_rate(lcdc_dev->dclk), fps);
	return 0;
}
#endif
/********do basic init*********/
static int rk3036_lcdc_pre_init(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
				struct lcdc_device, driver);

	if (lcdc_dev->pre_init)
		return 0;
	lcdc_dev->hclk = devm_clk_get(lcdc_dev->dev, "hclk_lcdc");
	lcdc_dev->aclk = devm_clk_get(lcdc_dev->dev, "aclk_lcdc");
	lcdc_dev->dclk = devm_clk_get(lcdc_dev->dev, "dclk_lcdc");
/*	lcdc_dev->pd   = devm_clk_get(lcdc_dev->dev, "pd_lcdc"); */

	if (/*IS_ERR(lcdc_dev->pd) ||*/ (IS_ERR(lcdc_dev->aclk)) ||
	    (IS_ERR(lcdc_dev->dclk)) || (IS_ERR(lcdc_dev->hclk))) {
		dev_err(lcdc_dev->dev, "failed to get lcdc%d clk source\n",
			lcdc_dev->id);
	}

	rk_disp_pwr_enable(dev_drv);
	rk3036_lcdc_clk_enable(lcdc_dev);

	/*backup reg config at uboot*/
	rk_lcdc_read_reg_defalut_cfg(lcdc_dev);
	if (lcdc_readl(lcdc_dev, AXI_BUS_CTRL) & m_TVE_DAC_DCLK_EN)
		dev_drv->cur_screen->type = SCREEN_TVOUT;

	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_AUTO_GATING_EN,
		     v_AUTO_GATING_EN(0));
	lcdc_cfg_done(lcdc_dev);
	if (dev_drv->iommu_enabled)
		/*disable win0 to workaround iommu pagefault*/
		lcdc_layer_enable(lcdc_dev, 0, 0);
	lcdc_dev->pre_init = true;

	return 0;
}

static int rk3036_load_screen(struct rk_lcdc_driver *dev_drv, bool initscreen)
{
	int ret = -EINVAL;
	int fps;
	u16 face = 0;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u16 right_margin = screen->mode.right_margin;
	u16 left_margin = screen->mode.left_margin;
	u16 lower_margin = screen->mode.lower_margin;
	u16 upper_margin = screen->mode.upper_margin;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		switch (screen->type) {
		case SCREEN_HDMI:
			mask = m_HDMI_DCLK_EN;
			val = v_HDMI_DCLK_EN(1);
			if (screen->pixelrepeat) {
				mask |= m_CORE_CLK_DIV_EN;
				val |= v_CORE_CLK_DIV_EN(1);
			} else {
				mask |= m_CORE_CLK_DIV_EN;
				val |= v_CORE_CLK_DIV_EN(0);
			}
			lcdc_msk_reg(lcdc_dev, AXI_BUS_CTRL, mask, val);
			mask = (1 << 4) | (1 << 5) | (1 << 6);
			val = (screen->pin_hsync << 4) |
				(screen->pin_vsync << 5) |
				(screen->pin_den << 6);
			grf_writel(RK3036_GRF_SOC_CON2, (mask << 16) | val);
			break;
		case SCREEN_TVOUT:
			mask = m_TVE_DAC_DCLK_EN;
			val = v_TVE_DAC_DCLK_EN(1);
			if (screen->pixelrepeat) {
				mask |= m_CORE_CLK_DIV_EN;
				val |= v_CORE_CLK_DIV_EN(1);
			} else {
				mask |= m_CORE_CLK_DIV_EN;
				val |= v_CORE_CLK_DIV_EN(0);
			}
			lcdc_msk_reg(lcdc_dev, AXI_BUS_CTRL, mask, val);
			if ((x_res == 720) && (y_res == 576)) {
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
					     m_TVE_MODE, v_TVE_MODE(TV_PAL));
			} else if ((x_res == 720) && (y_res == 480)) {
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
					     m_TVE_MODE, v_TVE_MODE(TV_NTSC));
			} else {
				dev_err(lcdc_dev->dev,
					"unsupported video timing!\n");
				return -1;
			}
			break;
		default:
			dev_err(lcdc_dev->dev, "un supported interface!\n");
			break;
		}

		mask = m_DSP_OUT_FORMAT | m_HSYNC_POL | m_VSYNC_POL |
		    m_DEN_POL | m_DCLK_POL;
		val = v_DSP_OUT_FORMAT(face) |
			v_HSYNC_POL(screen->pin_hsync) |
			v_VSYNC_POL(screen->pin_vsync) |
			v_DEN_POL(screen->pin_den) |
			v_DCLK_POL(screen->pin_dclk);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);

		mask = m_BG_COLOR | m_DSP_BG_SWAP | m_DSP_RB_SWAP |
		    m_DSP_RG_SWAP | m_DSP_DELTA_SWAP |
		    m_DSP_DUMMY_SWAP | m_BLANK_EN;

		val = v_BG_COLOR(0x000000) | v_DSP_BG_SWAP(screen->swap_gb) |
		    v_DSP_RB_SWAP(screen->swap_rb) |
		    v_DSP_RG_SWAP(screen->swap_rg) |
		    v_DSP_DELTA_SWAP(screen->swap_delta) |
				     v_DSP_DUMMY_SWAP(screen->swap_dumy) |
						      v_BLANK_EN(0) |
				     v_BLACK_EN(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
		val =
		    v_HSYNC(screen->mode.hsync_len) | v_HORPRD(screen->mode.
							       hsync_len +
							       left_margin +
							       x_res +
							       right_margin);
		lcdc_writel(lcdc_dev, DSP_HTOTAL_HS_END, val);
		val = v_HAEP(screen->mode.hsync_len + left_margin + x_res) |
		    v_HASP(screen->mode.hsync_len + left_margin);
		lcdc_writel(lcdc_dev, DSP_HACT_ST_END, val);

		if (screen->mode.vmode & FB_VMODE_INTERLACED) {
			/*First Field Timing*/
			lcdc_writel(lcdc_dev, DSP_VTOTAL_VS_END,
				    v_VSYNC(screen->mode.vsync_len) |
				    v_VERPRD(2 * (screen->mode.vsync_len +
						  upper_margin + lower_margin)
					     + y_res + 1));
			lcdc_writel(lcdc_dev, DSP_VACT_ST_END,
				    v_VAEP(screen->mode.vsync_len +
					upper_margin + y_res/2) |
				    v_VASP(screen->mode.vsync_len +
					upper_margin));
			/*Second Field Timing*/
			lcdc_writel(lcdc_dev, DSP_VS_ST_END_F1,
				    v_VSYNC_ST_F1(screen->mode.vsync_len +
						  upper_margin + y_res/2 +
						  lower_margin) |
				    v_VSYNC_END_F1(2 * screen->mode.vsync_len
						   + upper_margin + y_res/2 +
						   lower_margin));
			lcdc_writel(lcdc_dev, DSP_VACT_ST_END_F1,
				    v_VAEP(2 * (screen->mode.vsync_len +
						upper_margin) + y_res +
						lower_margin + 1) |
				    v_VASP(2 * (screen->mode.vsync_len +
						upper_margin) + y_res/2 +
						lower_margin + 1));

			lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
				     m_INTERLACE_DSP_EN |
				     m_INTERLACE_DSP_POL |
				     m_WIN1_DIFF_DCLK_EN |
				     m_WIN0_YRGB_DEFLICK_EN |
				     m_WIN0_CBR_DEFLICK_EN |
				     m_WIN0_INTERLACE_EN |
				     m_WIN1_INTERLACE_EN,
				     v_INTERLACE_DSP_EN(1) |
				     v_INTERLACE_DSP_POL(0) |
				     v_WIN1_DIFF_DCLK_EN(1) |
				     v_WIN0_YRGB_DEFLICK_EN(1) |
				     v_WIN0_CBR_DEFLICK_EN(1) |
				     v_WIN0_INTERLACE_EN(1) |
				     v_WIN1_INTERLACE_EN(1));
		} else {
			val = v_VSYNC(screen->mode.vsync_len) |
			      v_VERPRD(screen->mode.vsync_len + upper_margin +
					y_res + lower_margin);
			lcdc_writel(lcdc_dev, DSP_VTOTAL_VS_END, val);

			val = v_VAEP(screen->mode.vsync_len +
				     upper_margin + y_res) |
			    v_VASP(screen->mode.vsync_len +
				   screen->mode.upper_margin);
			lcdc_writel(lcdc_dev, DSP_VACT_ST_END, val);

			lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
				     m_INTERLACE_DSP_EN |
				     m_WIN1_DIFF_DCLK_EN |
				     m_WIN0_YRGB_DEFLICK_EN |
				     m_WIN0_CBR_DEFLICK_EN |
				     m_WIN0_INTERLACE_EN |
				     m_WIN1_INTERLACE_EN,
				     v_INTERLACE_DSP_EN(0) |
				     v_WIN1_DIFF_DCLK_EN(0) |
				     v_WIN0_YRGB_DEFLICK_EN(0) |
				     v_WIN0_CBR_DEFLICK_EN(0) |
				     v_WIN0_INTERLACE_EN(1) |
				     v_WIN1_INTERLACE_EN(1));
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	ret = clk_set_rate(lcdc_dev->dclk, screen->mode.pixclock);
	if (ret)
		dev_err(dev_drv->dev,
			"set lcdc%d dclk failed\n", lcdc_dev->id);
	lcdc_dev->pixclock =
	    div_u64(1000000000000llu, clk_get_rate(lcdc_dev->dclk));
	lcdc_dev->driver.pixclock = lcdc_dev->pixclock;

	fps = rk_fb_calc_fps(screen, lcdc_dev->pixclock);
	screen->ft = 1000 / fps;
	dev_info(lcdc_dev->dev, "%s: dclk:%lu>>fps:%d ",
		 lcdc_dev->driver.name, clk_get_rate(lcdc_dev->dclk), fps);
	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();
	if (screen->init)
		screen->init();

	return 0;
}

static int rk3036_lcdc_open(struct rk_lcdc_driver *dev_drv, int win_id,
			    bool open)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);

	/*enable clk,when first layer open */
	if ((open) && (!lcdc_dev->atv_layer_cnt)) {
		rk3036_lcdc_pre_init(dev_drv);
		rk3036_lcdc_clk_enable(lcdc_dev);
		if (dev_drv->iommu_enabled) {
			if (!dev_drv->mmu_dev) {
				dev_drv->mmu_dev =
				rk_fb_get_sysmmu_device_by_compatible(
					dev_drv->mmu_dts_name);
				if (dev_drv->mmu_dev) {
					rk_fb_platform_set_sysmmu(dev_drv->mmu_dev,
								  dev_drv->dev);
				} else {
					dev_err(dev_drv->dev,
						"failed to get iommu device\n"
						);
					return -1;
				}
			}
		}
		rk3036_lcdc_reg_restore(lcdc_dev);
		/*if (dev_drv->iommu_enabled)
			rk3036_lcdc_mmu_en(dev_drv);*/
		if ((support_uboot_display() && (lcdc_dev->prop == PRMRY))) {
			/*rk3036_lcdc_set_dclk(dev_drv);*/
			rk3036_lcdc_enable_irq(dev_drv);
		} else {
			rk3036_load_screen(dev_drv, 1);
		}
	}

	if (win_id < ARRAY_SIZE(lcdc_win))
		lcdc_layer_enable(lcdc_dev, win_id, open);
	else
		dev_err(lcdc_dev->dev, "invalid win id:%d\n", win_id);

	/*when all layer closed,disable clk */
/*
	if ((!open) && (!lcdc_dev->atv_layer_cnt)) {
		rk3036_lcdc_disable_irq(lcdc_dev);
		rk3036_lcdc_reg_update(dev_drv);
		if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_deactivate(dev_drv->dev);
		}
		rk3036_lcdc_clk_disable(lcdc_dev);
	}
*/
	return 0;
}

static int rk3036_lcdc_set_par(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
			container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	struct rk_lcdc_win *win = NULL;
	char fmt[9] = "NULL";

	if (!screen) {
		dev_err(dev_drv->dev, "screen is null!\n");
		return -ENOENT;
	}

	if (win_id == 0) {
		win = dev_drv->win[0];
	} else if (win_id == 1) {
		win = dev_drv->win[1];
	} else if (win_id == 2) {
		win = dev_drv->win[2];
	} else {
		dev_err(dev_drv->dev, "un supported win number:%d\n", win_id);
		return -EINVAL;
	}

	spin_lock(&lcdc_dev->reg_lock);
	win->post_cfg.xpos = win->area[0].xpos * (dev_drv->overscan.left +
		dev_drv->overscan.right)/200 + screen->mode.xres *
			(100 - dev_drv->overscan.left) / 200;

	win->post_cfg.ypos = win->area[0].ypos * (dev_drv->overscan.top +
		dev_drv->overscan.bottom)/200 +
		screen->mode.yres *
			(100 - dev_drv->overscan.top) / 200;
	win->post_cfg.xsize = win->area[0].xsize *
				(dev_drv->overscan.left +
				dev_drv->overscan.right)/200;
	win->post_cfg.ysize = win->area[0].ysize *
				(dev_drv->overscan.top +
				dev_drv->overscan.bottom)/200;

	win->area[0].dsp_stx = win->post_cfg.xpos + screen->mode.left_margin +
				screen->mode.hsync_len;
	if (screen->mode.vmode & FB_VMODE_INTERLACED) {
		win->post_cfg.ysize /= 2;
		win->area[0].dsp_sty = win->post_cfg.ypos/2 +
					screen->mode.upper_margin +
					screen->mode.vsync_len;
	} else {
		win->area[0].dsp_sty = win->post_cfg.ypos +
					screen->mode.upper_margin +
					screen->mode.vsync_len;
	}
	win->scale_yrgb_x = calscale(win->area[0].xact, win->post_cfg.xsize);
	win->scale_yrgb_y = calscale(win->area[0].yact, win->post_cfg.ysize);

	switch (win->area[0].format) {
	case ARGB888:
		win->area[0].fmt_cfg = VOP_FORMAT_ARGB888;
		win->area[0].swap_rb = 0;
		break;
	case XBGR888:
		win->area[0].fmt_cfg = VOP_FORMAT_ARGB888;
		win->area[0].swap_rb = 1;
		break;
	case ABGR888:
		win->area[0].fmt_cfg = VOP_FORMAT_ARGB888;
		win->area[0].swap_rb = 1;
		break;
	case RGB888:
		win->area[0].fmt_cfg = VOP_FORMAT_RGB888;
		win->area[0].swap_rb = 0;
		break;
	case RGB565:
		win->area[0].fmt_cfg = VOP_FORMAT_RGB565;
		win->area[0].swap_rb = 0;
		break;
	case YUV444:
		if (win_id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR444;
			win->scale_cbcr_x = calscale(win->area[0].xact,
						     win->post_cfg.xsize);
			win->scale_cbcr_y = calscale(win->area[0].yact,
						     win->post_cfg.ysize);
			win->area[0].swap_rb = 0;
		} else {
			dev_err(lcdc_dev->driver.dev,
				"%s:un supported format!\n",
				__func__);
		}
		break;
	case YUV422:
		if (win_id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR422;
			win->scale_cbcr_x = calscale((win->area[0].xact / 2),
						     win->post_cfg.xsize);
			win->scale_cbcr_y = calscale(win->area[0].yact,
						     win->post_cfg.ysize);
			win->area[0].swap_rb = 0;
		} else {
			dev_err(lcdc_dev->driver.dev,
				"%s:un supported format!\n",
				__func__);
		}
		break;
	case YUV420:
		if (win_id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR420;
			win->scale_cbcr_x = calscale(win->area[0].xact / 2,
						     win->post_cfg.xsize);
			win->scale_cbcr_y = calscale(win->area[0].yact / 2,
						     win->post_cfg.ysize);
			win->area[0].swap_rb = 0;
		} else {
			dev_err(lcdc_dev->driver.dev,
				"%s:un supported format!\n",
				__func__);
		}
		break;
	default:
		dev_err(lcdc_dev->driver.dev, "%s:un supported format!\n",
			__func__);
		break;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(2, "lcdc%d>>%s\n"
		">>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d\n"
		">>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n", lcdc_dev->id,
		__func__, get_format_string(win->area[0].format, fmt),
		win->area[0].xact, win->area[0].yact, win->post_cfg.xsize,
		win->post_cfg.ysize, win->area[0].xvir, win->area[0].yvir,
		win->post_cfg.xpos, win->post_cfg.ypos);
	return 0;
}

static int rk3036_lcdc_pan_display(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						struct lcdc_device, driver);
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;

	if (!screen) {
		dev_err(dev_drv->dev, "screen is null!\n");
		return -ENOENT;
	}

	if (win_id == 0) {
		win = dev_drv->win[0];
	} else if (win_id == 1) {
		win = dev_drv->win[1];
	} else if (win_id == 2) {
		win = dev_drv->win[2];
	} else {
		dev_err(dev_drv->dev, "invalid win number:%d!\n", win_id);
		return -EINVAL;
	}

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		win->area[0].y_addr = win->area[0].smem_start +
					win->area[0].y_offset;
		win->area[0].uv_addr = win->area[0].cbr_start +
					win->area[0].c_offset;
		if (win->area[0].y_addr)
			lcdc_layer_update_regs(lcdc_dev, win);
		/*lcdc_cfg_done(lcdc_dev);*/
	}
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(2, "lcdc%d>>%s:y_addr:0x%x>>uv_addr:0x%x>>offset:%d\n",
	    lcdc_dev->id, __func__, win->area[0].y_addr,
	    win->area[0].uv_addr, win->area[0].y_offset);
	 /* this is the first frame of the system,
		enable frame start interrupt*/
	if ((dev_drv->first_frame))  {
		dev_drv->first_frame = 0;
		rk3036_lcdc_enable_irq(dev_drv);
	}
	return 0;
}

static int rk3036_lcdc_ioctl(struct rk_lcdc_driver *dev_drv, unsigned int cmd,
			     unsigned long arg, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);
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

static int rk3036_lcdc_get_win_id(struct rk_lcdc_driver *dev_drv,
				  const char *id)
{
	int win_id = 0;

	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (!strcmp(id, "fb0"))
		win_id = dev_drv->fb0_win_id;
	else if (!strcmp(id, "fb1"))
		win_id = dev_drv->fb1_win_id;
	else if (!strcmp(id, "fb2"))
		win_id = dev_drv->fb2_win_id;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return win_id;
}

static int rk3036_lcdc_get_win_state(struct rk_lcdc_driver *dev_drv,
				     int win_id,
				     int area_id)
{
	return dev_drv->win[win_id]->state;
}

static int rk3036_lcdc_ovl_mgr(struct rk_lcdc_driver *dev_drv, int swap,
			       bool set)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win0 = lcdc_dev->driver.win[0];
	struct rk_lcdc_win *win1 = lcdc_dev->driver.win[1];
	int ovl, needswap = 0;

	if (!swap) {
		if (win0->z_order >= 0 &&
		    win1->z_order >= 0 &&
		    win0->z_order > win1->z_order)
			needswap = 1;
		else
			needswap = 0;
	} else {
		needswap = swap;
	}
	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (set) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_WIN0_TOP,
				     v_WIN0_TOP(needswap));
			ovl = swap;
		} else {
			ovl = lcdc_read_bit(lcdc_dev, DSP_CTRL0, m_WIN0_TOP);
		}
	} else {
		ovl = -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return ovl;
}

static int rk3036_lcdc_early_suspend(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);
	if (dev_drv->suspend_flag)
		return 0;
	dev_drv->suspend_flag = 1;
	flush_kthread_worker(&dev_drv->update_regs_worker);

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
		dev_drv->trsm_ops->disable();
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BLANK_EN,
			     v_BLANK_EN(1));
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_FS_INT_CLEAR,
			     v_FS_INT_CLEAR(1));
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_OUT_ZERO,
			     v_DSP_OUT_ZERO(1));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY,
			     v_LCDC_STANDBY(1));
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
	rk3036_lcdc_clk_disable(lcdc_dev);
	rk_disp_pwr_disable(dev_drv);
	return 0;
}

static int rk3036_lcdc_early_resume(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (!dev_drv->suspend_flag)
		return 0;
	rk_disp_pwr_enable(dev_drv);
	dev_drv->suspend_flag = 0;

	if (lcdc_dev->atv_layer_cnt) {
		rk3036_lcdc_clk_enable(lcdc_dev);
		rk3036_lcdc_reg_restore(lcdc_dev);
		/*set hwc lut*/
		rk3036_lcdc_set_hwc_lut(dev_drv, dev_drv->hwc_lut, 0);

		spin_lock(&lcdc_dev->reg_lock);

		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_OUT_ZERO,
			     v_DSP_OUT_ZERO(0));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY,
			     v_LCDC_STANDBY(0));
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BLANK_EN,
			     v_BLANK_EN(0));
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


static int rk3036_lcdc_blank(struct rk_lcdc_driver *dev_drv,
			     int win_id, int blank_mode)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		rk3036_lcdc_early_resume(dev_drv);
		break;
	case FB_BLANK_NORMAL:
		rk3036_lcdc_early_suspend(dev_drv);
		break;
	default:
		rk3036_lcdc_early_suspend(dev_drv);
		break;
	}

	dev_info(dev_drv->dev, "blank mode:%d\n", blank_mode);

	return 0;
}

static int rk3036_lcdc_cfg_done(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int i;
	struct rk_lcdc_win *win = NULL;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (dev_drv->iommu_enabled) {
			if (!lcdc_dev->iommu_status && dev_drv->mmu_dev) {
				lcdc_dev->iommu_status = 1;
				if (support_uboot_display() &&
				    lcdc_dev->prop == PRMRY) {
					lcdc_msk_reg(lcdc_dev, SYS_CTRL,
						     m_WIN0_EN,
						     v_WIN0_EN(0));
				}
				lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY,
					     v_LCDC_STANDBY(1));
				lcdc_cfg_done(lcdc_dev);
				mdelay(50);
				rockchip_iovmm_activate(dev_drv->dev);
				rk3036_lcdc_mmu_en(dev_drv);
			}
		}
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY,
			     v_LCDC_STANDBY(lcdc_dev->standby));
		for (i = 0; i < ARRAY_SIZE(lcdc_win); i++) {
			win = dev_drv->win[i];
			if ((win->state == 0) && (win->last_state == 1))
				lcdc_layer_update_regs(lcdc_dev, win);
			win->last_state = win->state;
		}
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
static int rk3036_lcdc_get_bcsh_hue(struct rk_lcdc_driver *dev_drv,
				    bcsh_hue_mode mode)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		val = lcdc_readl(lcdc_dev, BCSH_H);
		switch (mode) {
		case H_SIN:
			val &= m_BCSH_SIN_HUE;
			break;
		case H_COS:
			val &= m_BCSH_COS_HUE;
			val >>= 8;
			break;
		default:
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return val;
}


static int rk3036_lcdc_set_bcsh_hue(struct rk_lcdc_driver *dev_drv,
				    int sin_hue, int cos_hue)
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

static int rk3036_lcdc_set_bcsh_bcs(struct rk_lcdc_driver *dev_drv,
				    bcsh_bcs_mode mode, int value)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		switch (mode) {
		case BRIGHTNESS:
		/*from 0 to 255,typical is 128*/
			if (value < 0x20)
				value += 0x20;
			else if (value >= 0x20)
				value = value - 0x20;
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

static int rk3036_lcdc_get_bcsh_bcs(struct rk_lcdc_driver *dev_drv,
				    bcsh_bcs_mode mode)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		val = lcdc_readl(lcdc_dev, BCSH_BCS);
		switch (mode) {
		case BRIGHTNESS:
			val &= m_BCSH_BRIGHTNESS;
			if (val > 0x20)
				val -= 0x20;
			else if (val == 0x20)
				val = -32;
			break;
		case CONTRAST:
			val &= m_BCSH_CONTRAST;
			val >>= 8;
			break;
		case SAT_CON:
			val &= m_BCSH_SAT_CON;
			val >>= 16;
			break;
		default:
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return val;
}


static int rk3036_lcdc_open_bcsh(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (open) {
			lcdc_writel(lcdc_dev, BCSH_CTRL,
				    v_BCSH_EN(1) | v_BCSH_OUT_MODE(3));
			lcdc_writel(lcdc_dev, BCSH_BCS,
				    v_BCSH_BRIGHTNESS(0x00) |
				    v_BCSH_CONTRAST(0x80) |
				    v_BCSH_SAT_CON(0x80));
			lcdc_writel(lcdc_dev, BCSH_H, v_BCSH_COS_HUE(0x80));
		} else {
			mask = m_BCSH_EN;
			val = v_BCSH_EN(0);
			lcdc_msk_reg(lcdc_dev, BCSH_CTRL, mask, val);
		}
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3036_lcdc_set_overscan(struct rk_lcdc_driver *dev_drv,
				    struct overscan *overscan)
{
	int i;

	dev_drv->overscan = *overscan;
	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		if (dev_drv->win[i] && dev_drv->win[i]->state) {
			rk3036_lcdc_set_par(dev_drv, i);
			rk3036_lcdc_pan_display(dev_drv, i);
		}
	}
	rk3036_lcdc_cfg_done(dev_drv);
	return 0;
}

static int rk3036_fb_win_remap(struct rk_lcdc_driver *dev_drv, u16 order)
{
	struct rk_lcdc_win_area area;
	int fb2_win_id, fb1_win_id, fb0_win_id;

	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (order == FB_DEFAULT_ORDER)
		order = FB0_WIN0_FB1_WIN1_FB2_WIN2;

	fb2_win_id = order / 100;
	fb1_win_id = (order / 10) % 10;
	fb0_win_id = order % 10;

	if (fb0_win_id != dev_drv->fb0_win_id) {
		area = dev_drv->win[(int)dev_drv->fb0_win_id]->area[0];
		dev_drv->win[(int)dev_drv->fb0_win_id]->area[0] =
			dev_drv->win[fb0_win_id]->area[0];
		dev_drv->win[fb0_win_id]->area[0] = area;
		dev_drv->fb0_win_id = fb0_win_id;
	}
	dev_drv->fb1_win_id = fb1_win_id;
	dev_drv->fb2_win_id = fb2_win_id;

	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return 0;
}

static int rk3036_lcdc_fps_mgr(struct rk_lcdc_driver *dev_drv, int fps,
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
	lcdc_dev->pixclock = pixclock;
	dev_drv->pixclock = pixclock;
	fps = rk_fb_calc_fps(lcdc_dev->screen, pixclock);
	screen->ft = 1000 / fps;	/*one frame time in ms */

	if (set)
		dev_info(dev_drv->dev, "%s:dclk:%lu,fps:%d\n", __func__,
			 clk_get_rate(lcdc_dev->dclk), fps);

	return fps;
}

static int rk3036_lcdc_poll_vblank(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 int_reg;
	int ret;

	if (lcdc_dev->clk_on) {
		int_reg = lcdc_readl(lcdc_dev, INT_STATUS);
		if (int_reg & m_LF_INT_STA) {
			lcdc_msk_reg(lcdc_dev, INT_STATUS, m_LF_INT_CLEAR,
				     v_LF_INT_CLEAR(1));
			ret = RK_LF_STATUS_FC;
		} else {
			ret = RK_LF_STATUS_FR;
		}
	} else {
		ret = RK_LF_STATUS_NC;
	}

	return ret;
}

static int rk3036_lcdc_get_dsp_addr(struct rk_lcdc_driver *dev_drv,
				    unsigned int dsp_addr[][4])
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (lcdc_dev->clk_on) {
		dsp_addr[0][0] = lcdc_readl(lcdc_dev, WIN0_YRGB_MST);
		dsp_addr[1][0] = lcdc_readl(lcdc_dev, WIN1_MST);
	}
	return 0;
}

static ssize_t rk3036_lcdc_get_disp_info(struct rk_lcdc_driver *dev_drv,
					 char *buf, int win_id)
{
	struct rk_lcdc_win *win = NULL;
	char fmt[9] = "NULL";
	u32	size;

	if (win_id < ARRAY_SIZE(lcdc_win)) {
		win = dev_drv->win[win_id];
	} else {
		dev_err(dev_drv->dev, "invalid win number:%d!\n", win_id);
		return 0;
	}

	size = snprintf(buf, PAGE_SIZE, "win%d: %s\n", win_id,
			get_format_string(win->area[0].format, fmt));
	size += snprintf(buf + size, PAGE_SIZE - size,
			 "	xact %d yact %d xvir %d yvir %d\n",
		win->area[0].xact, win->area[0].yact,
		win->area[0].xvir, win->area[0].yvir);
	size += snprintf(buf + size, PAGE_SIZE - size,
			 "	xpos %d ypos %d xsize %d ysize %d\n",
		win->area[0].xpos, win->area[0].ypos,
		win->area[0].xsize, win->area[0].ysize);
	size += snprintf(buf + size, PAGE_SIZE - size,
			 "	yaddr 0x%x uvaddr 0x%x\n",
		win->area[0].y_addr, win->area[0].uv_addr);
	return size;
}

static int rk3036_lcdc_reg_dump(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						struct lcdc_device,
						driver);
	int *cbase = (int *)lcdc_dev->regs;
	int *regsbak = (int *)lcdc_dev->regsbak;
	int i, j;

	dev_info(dev_drv->dev, "back up reg:\n");
	for (i = 0; i <= (0xDC >> 4); i++) {
		for (j = 0; j < 4; j++)
			dev_info(dev_drv->dev, "%08x  ",
				 *(regsbak + i * 4 + j));
		dev_info(dev_drv->dev, "\n");
	}

	dev_info(dev_drv->dev, "lcdc reg:\n");
	for (i = 0; i <= (0xDC >> 4); i++) {
		for (j = 0; j < 4; j++)
			dev_info(dev_drv->dev, "%08x  ",
				 readl_relaxed(cbase + i * 4 + j));
		dev_info(dev_drv->dev, "\n");
	}
	return 0;
}

static struct rk_lcdc_drv_ops lcdc_drv_ops = {
	.open			= rk3036_lcdc_open,
	.load_screen		= rk3036_load_screen,
	.set_par		= rk3036_lcdc_set_par,
	.pan_display		= rk3036_lcdc_pan_display,
	.blank			= rk3036_lcdc_blank,
	.ioctl			= rk3036_lcdc_ioctl,
	.get_win_state		= rk3036_lcdc_get_win_state,
	.ovl_mgr		= rk3036_lcdc_ovl_mgr,
	.get_disp_info		= rk3036_lcdc_get_disp_info,
	.fps_mgr		= rk3036_lcdc_fps_mgr,
	.fb_get_win_id		= rk3036_lcdc_get_win_id,
	.fb_win_remap		= rk3036_fb_win_remap,
	.poll_vblank		= rk3036_lcdc_poll_vblank,
	.get_dsp_addr		= rk3036_lcdc_get_dsp_addr,
	.cfg_done		= rk3036_lcdc_cfg_done,
	.dump_reg		= rk3036_lcdc_reg_dump,
	.set_dsp_bcsh_hue	= rk3036_lcdc_set_bcsh_hue,
	.set_dsp_bcsh_bcs	= rk3036_lcdc_set_bcsh_bcs,
	.get_dsp_bcsh_hue	= rk3036_lcdc_get_bcsh_hue,
	.get_dsp_bcsh_bcs	= rk3036_lcdc_get_bcsh_bcs,
	.open_bcsh		= rk3036_lcdc_open_bcsh,
	.set_overscan		= rk3036_lcdc_set_overscan,
	.set_hwc_lut		= rk3036_lcdc_set_hwc_lut,
};

static int rk3036_lcdc_parse_dt(struct lcdc_device *lcdc_dev)
{
	struct device_node *np = lcdc_dev->dev->of_node;
	int val;

	if (of_property_read_u32(np, "rockchip,iommu-enabled", &val))
		lcdc_dev->driver.iommu_enabled = 0;
	else
		lcdc_dev->driver.iommu_enabled = val;
	if (of_property_read_u32(np, "rockchip,fb-win-map", &val))
		lcdc_dev->driver.fb_win_map = FB_DEFAULT_ORDER;
	else
		lcdc_dev->driver.fb_win_map = val;

	return 0;
}

static int rk3036_lcdc_probe(struct platform_device *pdev)
{
	struct lcdc_device *lcdc_dev = NULL;
	struct rk_lcdc_driver *dev_drv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	lcdc_dev = devm_kzalloc(dev,
				sizeof(struct lcdc_device), GFP_KERNEL);
	if (!lcdc_dev) {
		dev_err(&pdev->dev, "rk3036 lcdc device kmalloc fail!");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->dev = dev;
	rk3036_lcdc_parse_dt(lcdc_dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lcdc_dev->reg_phy_base = res->start;
	lcdc_dev->len = resource_size(res);
	lcdc_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(lcdc_dev->regs))
		return PTR_ERR(lcdc_dev->regs);

	lcdc_dev->regsbak = devm_kzalloc(dev, lcdc_dev->len, GFP_KERNEL);
	if (IS_ERR(lcdc_dev->regsbak))
		return PTR_ERR(lcdc_dev->regsbak);

	lcdc_dev->hwc_lut_addr_base = (lcdc_dev->regs + HWC_LUT_ADDR);
	lcdc_dev->prop = PRMRY;
	dev_set_name(lcdc_dev->dev, "lcdc%d", lcdc_dev->id);
	dev_drv = &lcdc_dev->driver;
	dev_drv->dev = dev;
	dev_drv->prop = PRMRY;
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

	ret = devm_request_irq(dev, lcdc_dev->irq, rk3036_lcdc_isr,
			       IRQF_DISABLED | IRQF_SHARED,
			       dev_name(dev), lcdc_dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot requeset irq %d - err %d\n",
			lcdc_dev->irq, ret);
		return ret;
	}

	if (dev_drv->iommu_enabled)
		strcpy(dev_drv->mmu_dts_name, VOP_IOMMU_COMPATIBLE_NAME);

	ret = rk_fb_register(dev_drv, lcdc_win, lcdc_dev->id);
	if (ret < 0) {
		dev_err(dev, "register fb for lcdc%d failed!\n", lcdc_dev->id);
		return ret;
	}
	lcdc_dev->screen = dev_drv->screen0;

	dev_info(dev, "lcdc probe ok, iommu %s\n",
		 dev_drv->iommu_enabled ? "enabled" : "disabled");

	return 0;
}

#if defined(CONFIG_PM)
static int rk3036_lcdc_suspend(struct platform_device *pdev,
			       pm_message_t state)
{
	return 0;
}

static int rk3036_lcdc_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define rk3036_lcdc_suspend NULL
#define rk3036_lcdc_resume  NULL
#endif

static int rk3036_lcdc_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk3036_lcdc_shutdown(struct platform_device *pdev)
{
}

#if defined(CONFIG_OF)
static const struct of_device_id rk3036_lcdc_dt_ids[] = {
	{.compatible = "rockchip,rk3036-lcdc",},
	{}
};
#endif

static struct platform_driver rk3036_lcdc_driver = {
	.probe = rk3036_lcdc_probe,
	.remove = rk3036_lcdc_remove,
	.driver = {
		.name = "rk3036-lcdc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rk3036_lcdc_dt_ids),
	},
	.suspend = rk3036_lcdc_suspend,
	.resume = rk3036_lcdc_resume,
	.shutdown = rk3036_lcdc_shutdown,
};

static int __init rk3036_lcdc_module_init(void)
{
	return platform_driver_register(&rk3036_lcdc_driver);
}

static void __exit rk3036_lcdc_module_exit(void)
{
	platform_driver_unregister(&rk3036_lcdc_driver);
}

fs_initcall(rk3036_lcdc_module_init);
module_exit(rk3036_lcdc_module_exit);
