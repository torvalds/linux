/*
 * drivers/video/rockchip/lcdc/rk312x_lcdc.c
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
 * Author:      zhuangwenlong<zwl@rock-chips.com>
 *              zhengyang<zhengyang@rock-chips.com>
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
#include <linux/rockchip-iovmm.h>
#include "rk312x_lcdc.h"
#include <linux/rockchip/dvfs.h>

static int dbg_thresd;
module_param(dbg_thresd, int, S_IRUGO | S_IWUSR);

#define DBG(level, x...) do {			\
	if (unlikely(dbg_thresd >= level))	\
		pr_info(KERN_INFO x); \
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

static irqreturn_t rk312x_lcdc_isr(int irq, void *dev_id)
{
	struct lcdc_device *lcdc_dev = (struct lcdc_device *)dev_id;
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
		lcdc_dev->driver.frame_time.last_framedone_t =
				lcdc_dev->driver.frame_time.framedone_t;
		lcdc_dev->driver.frame_time.framedone_t = cpu_clock(0);
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_LF_INT_CLEAR,
			     v_LF_INT_CLEAR(1));
	}

#ifdef LCDC_IRQ_EMPTY_DEBUG
	if (int_reg & m_WIN0_EMPTY_INT_STA) {
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_WIN0_EMPTY_INT_CLEAR,
			     v_WIN0_EMPTY_INT_CLEAR(1));
		dev_info(lcdc_dev->dev, "win0 empty irq\n");
	} else if (int_reg & m_WIN1_EMPTY_INT_STA) {
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_WIN1_EMPTY_INT_CLEAR,
			     v_WIN1_EMPTY_INT_CLEAR(1));
		dev_info(lcdc_dev->dev, "win1 empty irq\n");
	}
#endif

	return IRQ_HANDLED;
}

static int rk312x_lcdc_clk_enable(struct lcdc_device *lcdc_dev)
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

static int rk312x_lcdc_clk_disable(struct lcdc_device *lcdc_dev)
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

static int rk312x_lcdc_enable_irq(struct rk_lcdc_driver *dev_drv)
{
	u32 mask, val;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	/*struct rk_screen *screen = dev_drv->cur_screen;*/

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
			mask = m_FS_INT_CLEAR | m_FS_INT_EN |
			m_LF_INT_CLEAR | m_LF_INT_EN |
			m_BUS_ERR_INT_CLEAR | m_BUS_ERR_INT_EN;
		val = v_FS_INT_CLEAR(1) | v_FS_INT_EN(1) |
			v_LF_INT_CLEAR(1) | v_LF_INT_EN(1) |
			v_BUS_ERR_INT_CLEAR(1) | v_BUS_ERR_INT_EN(0);
		#if 0
			mask |= m_LF_INT_NUM;
			val  |= v_LF_INT_NUM(screen->mode.vsync_len +
						screen->mode.upper_margin +
						screen->mode.yres)
		#endif
#ifdef LCDC_IRQ_EMPTY_DEBUG
		mask |= m_WIN0_EMPTY_INT_EN | m_WIN1_EMPTY_INT_EN;
		val |= v_WIN0_EMPTY_INT_EN(1) | v_WIN1_EMPTY_INT_EN(1);
#endif

		lcdc_msk_reg(lcdc_dev, INT_STATUS, mask, val);
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
	}

	return 0;
}

static int rk312x_lcdc_disable_irq(struct lcdc_device *lcdc_dev)
{
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		mask = m_FS_INT_CLEAR | m_FS_INT_EN |
			m_LF_INT_CLEAR | m_LF_INT_EN |
			m_BUS_ERR_INT_CLEAR | m_BUS_ERR_INT_EN;
		val = v_FS_INT_CLEAR(0) | v_FS_INT_EN(0) |
			v_LF_INT_CLEAR(0) | v_LF_INT_EN(0) |
			v_BUS_ERR_INT_CLEAR(0) | v_BUS_ERR_INT_EN(0);
#ifdef LCDC_IRQ_EMPTY_DEBUG
		mask |= m_WIN0_EMPTY_INT_EN | m_WIN1_EMPTY_INT_EN;
		val |= v_WIN0_EMPTY_INT_EN(0) | v_WIN1_EMPTY_INT_EN(0);
#endif

		lcdc_msk_reg(lcdc_dev, INT_STATUS, mask, val);
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
	}
	mdelay(1);
	return 0;
}


static int win0_set_addr(struct lcdc_device *lcdc_dev, u32 addr)
{
	spin_lock(&lcdc_dev->reg_lock);
	lcdc_writel(lcdc_dev, WIN0_YRGB_MST, addr);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_EN, v_WIN0_EN(1));
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int win1_set_addr(struct lcdc_device *lcdc_dev, u32 addr)
{
	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->soc_type == VOP_RK3036)
		lcdc_writel(lcdc_dev, WIN1_MST, addr);
	else
		lcdc_writel(lcdc_dev, WIN1_MST_RK312X, addr);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN1_EN, v_WIN1_EN(1));
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

int rk312x_lcdc_direct_set_win_addr(struct rk_lcdc_driver *dev_drv,
				    int win_id, u32 addr)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
				struct lcdc_device, driver);
	if (win_id == 0)
		win0_set_addr(lcdc_dev, addr);
	else
		win1_set_addr(lcdc_dev, addr);

	return 0;
}

static void rk_lcdc_read_reg_defalut_cfg(struct lcdc_device *lcdc_dev)
{
	int reg = 0;
	u32 val = 0;
	struct rk_lcdc_win *win0 = lcdc_dev->driver.win[0];
	struct rk_lcdc_win *win1 = lcdc_dev->driver.win[1];

	spin_lock(&lcdc_dev->reg_lock);
	for (reg = 0; reg < 0xe0; reg += 4) {
		val = lcdc_readl_backup(lcdc_dev, reg);
		if (reg == WIN0_ACT_INFO) {
			win0->area[0].xact = (val & m_ACT_WIDTH)+1;
			win0->area[0].yact = ((val & m_ACT_HEIGHT)>>16)+1;
		}

		if (lcdc_dev->soc_type == VOP_RK312X) {
			if (reg == WIN1_DSP_INFO_RK312X) {
				win1->area[0].xact = (val & m_DSP_WIDTH) + 1;
				win1->area[0].yact =
					((val & m_DSP_HEIGHT) >> 16) + 1;
			}
		} else {
			if (reg == WIN1_ACT_INFO) {
				win1->area[0].xact = (val & m_ACT_WIDTH) + 1;
				win1->area[0].yact =
					((val & m_ACT_HEIGHT) >> 16) + 1;
			}
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
}

static int rk312x_lcdc_alpha_cfg(struct lcdc_device *lcdc_dev)
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
		mask =  m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN;
		val = v_WIN0_ALPHA_EN(1) | v_WIN1_ALPHA_EN(0);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);

		mask = m_WIN0_ALPHA_MODE |
				m_ALPHA_MODE_SEL0 | m_ALPHA_MODE_SEL1;
		val = v_WIN0_ALPHA_MODE(1) |
				v_ALPHA_MODE_SEL0(1) | v_ALPHA_MODE_SEL1(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
		/*this vop bg layer not support yuv domain overlay,so bg val
		have to set 0x800a80 equeal to 0x000000 at rgb domian,after
		android start we recover to 0x00000*/
		mask = m_BG_COLOR;
		val = v_BG_COLOR(0x000000);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
	} else if ((!win0_top) && (atv_layer_cnt >= 2) &&
				(win1_alpha_en)) {
		mask =  m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN;
		val = v_WIN0_ALPHA_EN(0) | v_WIN1_ALPHA_EN(1);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);

		mask = m_WIN1_ALPHA_MODE |
				m_ALPHA_MODE_SEL0 | m_ALPHA_MODE_SEL1;
		if (lcdc_dev->driver.overlay_mode == VOP_YUV_DOMAIN)
			val = v_WIN0_ALPHA_MODE(1) |
			      v_ALPHA_MODE_SEL0(0) |
			      v_ALPHA_MODE_SEL1(0);
		else
			val = v_WIN1_ALPHA_MODE(1) |
			      v_ALPHA_MODE_SEL0(1) |
			      v_ALPHA_MODE_SEL1(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
		/*this vop bg layer not support yuv domain overlay,so bg val
		have to set 0x800a80 equeal to 0x000000 at rgb domian,after
		android start we recover to 0x00000*/
		mask = m_BG_COLOR;
		val = v_BG_COLOR(0x000000);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
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

static void lcdc_layer_csc_mode(struct lcdc_device *lcdc_dev,
				struct rk_lcdc_win *win)
{
	struct rk_lcdc_driver *dev_drv = &lcdc_dev->driver;
	struct rk_screen *screen = dev_drv->cur_screen;

	if (dev_drv->overlay_mode == VOP_YUV_DOMAIN) {
		switch (win->area[0].fmt_cfg) {
		case VOP_FORMAT_ARGB888:
		case VOP_FORMAT_RGB888:
		case VOP_FORMAT_RGB565:
			if ((screen->mode.xres < 1280) &&
			    (screen->mode.yres < 720)) {
				win->csc_mode = VOP_R2Y_CSC_BT601;
			} else {
				win->csc_mode = VOP_R2Y_CSC_BT709;
			}
			break;
		default:
			break;
		}
		if (win->id  == 0) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_WIN0_CSC_MODE,
				     v_WIN0_CSC_MODE(win->csc_mode));
		} else if (win->id  == 1) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_WIN1_CSC_MODE,
				     v_WIN1_CSC_MODE(win->csc_mode));
		}
	} else if (dev_drv->overlay_mode == VOP_RGB_DOMAIN) {
		switch (win->area[0].fmt_cfg) {
		case VOP_FORMAT_YCBCR420:
			if (win->id  == 0) {
				win->csc_mode = VOP_Y2R_CSC_MPEG;
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
					     m_WIN0_CSC_MODE,
					v_WIN0_CSC_MODE(win->csc_mode));
			}
			break;
		default:
			break;
		}
	}
}


static void lcdc_layer_update_regs(struct lcdc_device *lcdc_dev,
				   struct rk_lcdc_win *win)
{
	u32 mask, val;
	int hwc_size;

	if (win->state == 1) {
		if (lcdc_dev->soc_type == VOP_RK312X)
			lcdc_layer_csc_mode(lcdc_dev, win);

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
				     v_CBCR_VIR(win->area[0].uv_vir_stride));
			lcdc_writel(lcdc_dev, WIN0_ACT_INFO,
				    v_ACT_WIDTH(win->area[0].xact) |
				    v_ACT_HEIGHT(win->area[0].yact));
			lcdc_writel(lcdc_dev, WIN0_DSP_ST,
				    v_DSP_STX(win->area[0].dsp_stx) |
				    v_DSP_STY(win->area[0].dsp_sty));
			lcdc_writel(lcdc_dev, WIN0_DSP_INFO,
				    v_DSP_WIDTH(win->area[0].xsize) |
				    v_DSP_HEIGHT(win->area[0].ysize));

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
			/* rk312x unsupport win1 scale */
			if (lcdc_dev->soc_type == VOP_RK3036) {
				lcdc_writel(lcdc_dev, WIN1_SCL_FACTOR_YRGB,
					    v_X_SCL_FACTOR(win->scale_yrgb_x) |
					    v_Y_SCL_FACTOR(win->scale_yrgb_y));
				lcdc_writel(lcdc_dev, WIN1_ACT_INFO,
					    v_ACT_WIDTH(win->area[0].xact) |
					    v_ACT_HEIGHT(win->area[0].yact));
				lcdc_writel(lcdc_dev, WIN1_DSP_INFO,
					    v_DSP_WIDTH(win->area[0].xsize) |
					    v_DSP_HEIGHT(win->area[0].ysize));
				lcdc_writel(lcdc_dev, WIN1_DSP_ST,
					    v_DSP_STX(win->area[0].dsp_stx) |
					    v_DSP_STY(win->area[0].dsp_sty));
				lcdc_writel(lcdc_dev,
					    WIN1_MST, win->area[0].y_addr);
			} else {
				lcdc_writel(lcdc_dev, WIN1_DSP_INFO_RK312X,
					    v_DSP_WIDTH(win->area[0].xact) |
					    v_DSP_HEIGHT(win->area[0].yact));
				lcdc_writel(lcdc_dev, WIN1_DSP_ST_RK312X,
					    v_DSP_STX(win->area[0].dsp_stx) |
					    v_DSP_STY(win->area[0].dsp_sty));

				lcdc_writel(lcdc_dev,
					    WIN1_MST_RK312X,
					    win->area[0].y_addr);
			}

			lcdc_msk_reg(lcdc_dev, WIN1_VIR, m_YRGB_VIR,
				     v_YRGB_VIR(win->area[0].y_vir_stride));


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
				dev_err(lcdc_dev->dev, "unsupport hwc size:x=%d,y=%d\n",
					win->area[0].xsize, win->area[0].ysize);
			lcdc_writel(lcdc_dev, HWC_DSP_ST,
				    v_DSP_STX(win->area[0].dsp_stx) |
				    v_DSP_STY(win->area[0].dsp_sty));

			lcdc_writel(lcdc_dev, HWC_MST, win->area[0].y_addr);
		}
	} else {
		win->area[0].y_addr = 0;
		win->area[0].uv_addr = 0;
		if (win->id == 0) {
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_EN,
				     v_WIN0_EN(0));
			lcdc_writel(lcdc_dev, WIN0_YRGB_MST,
				    win->area[0].y_addr);
			lcdc_writel(lcdc_dev, WIN0_CBR_MST,
				    win->area[0].uv_addr);
		} else if (win->id == 1) {
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN1_EN,
				     v_WIN1_EN(0));
			 lcdc_writel(lcdc_dev, WIN1_MST, win->area[0].y_addr);
		} else if (win->id == 2) {
			lcdc_msk_reg(lcdc_dev,
			             SYS_CTRL, m_HWC_EN | m_HWC_LODAD_EN,
			             v_HWC_EN(0) | v_HWC_LODAD_EN(0));
			lcdc_writel(lcdc_dev, HWC_MST, win->area[0].y_addr);
		}
	}
	rk312x_lcdc_alpha_cfg(lcdc_dev);
}

static void lcdc_layer_enable(struct lcdc_device *lcdc_dev, unsigned int win_id,
			      bool open)
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
			lcdc_dev->atv_layer_cnt  |= (1 << win_id);
		} else if ((lcdc_dev->atv_layer_cnt & (1 << win_id)) && (!open)) {
			 lcdc_dev->atv_layer_cnt &= ~(1 << win_id);
		}
		lcdc_dev->driver.win[win_id]->state = open;
		if (!open) {
			lcdc_layer_update_regs(lcdc_dev,
					       lcdc_dev->driver.win[win_id]);
			lcdc_cfg_done(lcdc_dev);
		}
		/*if no layer used,disable lcdc */
		if (!lcdc_dev->atv_layer_cnt) {
			dev_info(lcdc_dev->dev,
				 "no layer is used,go to standby!\n");
			lcdc_dev->standby = 1;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
}
/*
static int rk312x_lcdc_reg_update(struct rk_lcdc_driver *dev_drv)
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
		rk312x_lcdc_alpha_cfg(lcdc_dev);
		lcdc_cfg_done(lcdc_dev);

	}
	spin_unlock(&lcdc_dev->reg_lock);
	//if (dev_drv->wait_fs) {
	if (0) {
		spin_lock_irqsave(&dev_drv->cpl_lock, flags);
		init_completion(&dev_drv->frame_done);
		spin_unlock_irqrestore(&dev_drv->cpl_lock, flags);
		timeout = wait_for_completion_timeout(&dev_drv->frame_done,
						      msecs_to_jiffies
						      (dev_drv->cur_screen->ft +
						       5));
		if (!timeout && (!dev_drv->frame_done.done)) {
			dev_warn(lcdc_dev->dev,
				 "wait for new frame start time out!\n");
			return -ETIMEDOUT;
		}
	}
	DBG(2, "%s for lcdc%d\n", __func__, lcdc_dev->id);
	return 0;

}*/

static void rk312x_lcdc_reg_restore(struct lcdc_device *lcdc_dev)
{
	memcpy((u8 *)lcdc_dev->regs, (u8 *)lcdc_dev->regsbak, 0xe0);
}

static int rk312x_lcdc_mmu_en(struct rk_lcdc_driver *dev_drv)
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
	#if defined(CONFIG_ROCKCHIP_IOMMU)
	if (dev_drv->iommu_enabled) {
		if (!lcdc_dev->iommu_status && dev_drv->mmu_dev) {
			lcdc_dev->iommu_status = 1;
			rockchip_iovmm_activate(dev_drv->dev);
		}
	}
	#endif

	return 0;
}

static int rk312x_lcdc_set_hwc_lut(struct rk_lcdc_driver *dev_drv,
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

static int rk312x_lcdc_set_lut(struct rk_lcdc_driver *dev_drv,
			       int *dsp_lut)
{
	int i = 0;
	int __iomem *c;
	int v;
	struct lcdc_device *lcdc_dev =
		container_of(dev_drv, struct lcdc_device, driver);

	if (!dsp_lut)
		return 0;

	spin_lock(&lcdc_dev->reg_lock);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN, v_DSP_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	mdelay(25);
	for (i = 0; i < 256; i++) {
		v = dsp_lut[i];
		c = lcdc_dev->dsp_lut_addr_base + i;
		writel_relaxed(v, c);
	}
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN, v_DSP_LUT_EN(1));
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk312x_lcdc_set_dclk(struct rk_lcdc_driver *dev_drv,
				    int reset_rate)
{
#ifdef CONFIG_RK_FPGA
	return 0;
#endif
	int ret, fps;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;

	if (reset_rate)
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

/********do basic init*********/
static int rk312x_lcdc_pre_init(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	if (lcdc_dev->pre_init)
		return 0;

	lcdc_dev->hclk = devm_clk_get(lcdc_dev->dev, "hclk_lcdc");
	lcdc_dev->aclk = devm_clk_get(lcdc_dev->dev, "aclk_lcdc");
	lcdc_dev->dclk = devm_clk_get(lcdc_dev->dev, "dclk_lcdc");
	lcdc_dev->sclk = devm_clk_get(lcdc_dev->dev, "sclk_lcdc");
	lcdc_dev->pd   = devm_clk_get(lcdc_dev->dev, "pd_lcdc");
	lcdc_dev->pll_sclk = devm_clk_get(lcdc_dev->dev, "sclk_pll");

	if (/*IS_ERR(lcdc_dev->pd) || */ (IS_ERR(lcdc_dev->aclk)) ||
	    (IS_ERR(lcdc_dev->dclk)) || (IS_ERR(lcdc_dev->hclk))) {
		dev_err(lcdc_dev->dev, "failed to get lcdc%d clk source\n",
			lcdc_dev->id);
	}

	rk_disp_pwr_enable(dev_drv);
	rk312x_lcdc_clk_enable(lcdc_dev);

	/* backup reg config at uboot */
	rk_lcdc_read_reg_defalut_cfg(lcdc_dev);

	/* config for the FRC mode of dither down */
	lcdc_writel(lcdc_dev, FRC_LOWER01_0, 0x12844821);
	lcdc_writel(lcdc_dev, FRC_LOWER01_1, 0x21488412);
	lcdc_writel(lcdc_dev, FRC_LOWER10_0, 0x55aaaa55);
	lcdc_writel(lcdc_dev, FRC_LOWER10_1, 0x55aaaa55);
	lcdc_writel(lcdc_dev, FRC_LOWER11_0, 0xdeb77deb);
	lcdc_writel(lcdc_dev, FRC_LOWER11_1, 0xed7bb7de);

	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_AUTO_GATING_EN, v_AUTO_GATING_EN(0));
	lcdc_cfg_done(lcdc_dev);
	/*if (dev_drv->iommu_enabled)
		{// disable all wins to workaround iommu pagefault
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_EN | m_WIN1_EN,
			     v_WIN0_EN(0) | v_WIN1_EN(0));
		lcdc_cfg_done(lcdc_dev);
		while(lcdc_readl(lcdc_dev, SYS_CTRL) & (m_WIN0_EN | m_WIN1_EN));
	}*/
	if ((dev_drv->ops->open_bcsh) && (dev_drv->output_color == COLOR_YCBCR)) {
		if (support_uboot_display())
			dev_drv->bcsh_init_status = 1;
		else
			dev_drv->ops->open_bcsh(dev_drv, 1);
	}
	lcdc_dev->pre_init = true;

	return 0;
}

static void rk312x_lcdc_deinit(struct lcdc_device *lcdc_dev)
{
	rk312x_lcdc_disable_irq(lcdc_dev);
}

static u32 calc_sclk_freq(struct rk_screen *src_screen,
			  struct rk_screen *dst_screen)
{
	u32 dsp_vtotal;
	u64 dsp_htotal;
	u32 dsp_in_vtotal;
	u64 dsp_in_htotal;
	u64 sclk_freq;

	if (!src_screen || !dst_screen)
		return 0;

	dsp_vtotal = dst_screen->mode.yres;
	dsp_htotal = dst_screen->mode.left_margin + dst_screen->mode.hsync_len +
			dst_screen->mode.xres + dst_screen->mode.right_margin;
	dsp_in_vtotal = src_screen->mode.yres;
	dsp_in_htotal = src_screen->mode.left_margin +
			src_screen->mode.hsync_len +
			src_screen->mode.xres + src_screen->mode.right_margin;
	sclk_freq = dsp_vtotal * dsp_htotal * src_screen->mode.pixclock;
	do_div(sclk_freq, dsp_in_vtotal * dsp_in_htotal);

	return (u32)sclk_freq;
}

#define SCLK_PLL_LIMIT		594000000
#define GPU_FREQ_MAX_LIMIT	297000000
#define GPU_FREQ_NEED		400000000

static u32 calc_sclk_pll_freq(u32 sclk_freq)
{
	u32 multi_num;

	if (sclk_freq < (SCLK_PLL_LIMIT / 10)) {
		return (sclk_freq * 10);
	} else {
		multi_num = GPU_FREQ_NEED / sclk_freq;
		return (sclk_freq * multi_num);
	}
}

static int calc_dsp_frm_vst_hst(struct rk_screen *src,
				struct rk_screen *dst, u32 sclk_freq)
{
	u32 BP_in, BP_out;
	u32 v_scale_ratio;
	long long T_frm_st;
	u64 T_BP_in, T_BP_out, T_Delta, Tin;
	u32 src_pixclock, dst_pixclock;
	u64 temp;
	u32 dsp_htotal, dsp_vtotal, src_htotal, src_vtotal;

	if (unlikely(!src) || unlikely(!dst))
		return -1;

	src_pixclock = div_u64(1000000000000llu, src->mode.pixclock);
	dst_pixclock = div_u64(1000000000000llu, sclk_freq);
	dsp_htotal = dst->mode.left_margin + dst->mode.hsync_len +
		     dst->mode.xres + dst->mode.right_margin;
	dsp_vtotal = dst->mode.upper_margin + dst->mode.vsync_len +
		     dst->mode.yres + dst->mode.lower_margin;
	src_htotal = src->mode.left_margin + src->mode.hsync_len +
		     src->mode.xres + src->mode.right_margin;
	src_vtotal = src->mode.upper_margin + src->mode.vsync_len +
		     src->mode.yres + src->mode.lower_margin;
	BP_in  = (src->mode.upper_margin + src->mode.vsync_len) * src_htotal +
		 src->mode.hsync_len + src->mode.left_margin;
	BP_out = (dst->mode.upper_margin + dst->mode.vsync_len) * dsp_htotal +
		 dst->mode.hsync_len + dst->mode.left_margin;

	T_BP_in = BP_in * src_pixclock;
	T_BP_out = BP_out * dst_pixclock;
	Tin = src_vtotal * src_htotal * src_pixclock;

	v_scale_ratio = src->mode.yres / dst->mode.yres;
	if (v_scale_ratio <= 2)
		T_Delta = 5 * src_htotal * src_pixclock;
	else
		T_Delta = 12 * src_htotal * src_pixclock;

	if (T_BP_in + T_Delta > T_BP_out)
		T_frm_st = (T_BP_in + T_Delta - T_BP_out);
	else
		T_frm_st = Tin - (T_BP_out - (T_BP_in + T_Delta));

	/* (T_frm_st = scl_vst * src_htotal * src_pixclock +
						scl_hst * src_pixclock) */
	temp = do_div(T_frm_st, src_pixclock);
	temp = do_div(T_frm_st, src_htotal);
	dst->scl_hst = temp - 1;
	dst->scl_vst = T_frm_st;

	return 0;
}

static int rk312x_lcdc_set_scaler(struct rk_lcdc_driver *dev_drv,
				  struct rk_screen *dst_screen, bool enable)
{
	u32 dsp_htotal, dsp_hs_end, dsp_hact_st, dsp_hact_end;
	u32 dsp_vtotal, dsp_vs_end, dsp_vact_st, dsp_vact_end;
	u32 dsp_hbor_end, dsp_hbor_st, dsp_vbor_end, dsp_vbor_st;
	u32 scl_v_factor, scl_h_factor;
	u32 dst_frame_hst, dst_frame_vst;
	u32 src_w, src_h, dst_w, dst_h;
	u16 bor_right = 0;
	u16 bor_left = 0;
	u16 bor_up = 0;
	u16 bor_down = 0;
	u32 pll_freq = 0;
	struct rk_screen *src;
	struct rk_screen *dst;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	struct dvfs_node *gpu_clk = clk_get_dvfs_node("clk_gpu");

	if (unlikely(!lcdc_dev->clk_on))
		return 0;

	if (!enable) {
		spin_lock(&lcdc_dev->reg_lock);
		lcdc_msk_reg(lcdc_dev, SCALER_CTRL,
			     m_SCALER_EN | m_SCALER_OUT_ZERO |
					m_SCALER_OUT_EN,
					v_SCALER_EN(0) | v_SCALER_OUT_ZERO(1) |
					v_SCALER_OUT_EN(0));
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
		if (lcdc_dev->sclk_on) {
			clk_disable_unprepare(lcdc_dev->sclk);
			lcdc_dev->sclk_on = false;
		}

		/* switch pll freq as default when sclk is no used */
		if (clk_get_rate(lcdc_dev->pll_sclk) != GPU_FREQ_NEED) {
			dvfs_clk_enable_limit(gpu_clk, GPU_FREQ_MAX_LIMIT,
					      GPU_FREQ_MAX_LIMIT);
			clk_set_rate(lcdc_dev->pll_sclk, GPU_FREQ_NEED);
			dvfs_clk_enable_limit(gpu_clk, 0, -1);
		}
		dev_dbg(lcdc_dev->dev, "%s: disable\n", __func__);
		return 0;
	}

	/*
	 * rk312x used one lcdc to apply dual disp
	 * hdmi screen is used for scaler src
	 * prmry screen is used for scaler dst
	 */
	dst = dst_screen;
	src = dev_drv->cur_screen;
	if (!dst || !src) {
		dev_err(lcdc_dev->dev, "%s: dst screen is null!\n", __func__);
		return -EINVAL;
	}

	if (!lcdc_dev->sclk_on) {
		clk_prepare_enable(lcdc_dev->sclk);
		lcdc_dev->s_pixclock = calc_sclk_freq(src, dst);
		pll_freq = calc_sclk_pll_freq(lcdc_dev->s_pixclock);

		/* limit gpu freq */
		dvfs_clk_enable_limit(gpu_clk,
				      GPU_FREQ_MAX_LIMIT,
				      GPU_FREQ_MAX_LIMIT);
		/* set pll freq */
		clk_set_rate(lcdc_dev->pll_sclk, pll_freq);
		/* cancel limit gpu freq */
		dvfs_clk_enable_limit(gpu_clk, 0, -1);

		clk_set_rate(lcdc_dev->sclk, lcdc_dev->s_pixclock);
		lcdc_dev->sclk_on = true;
		dev_info(lcdc_dev->dev, "%s:sclk=%d\n", __func__,
			 lcdc_dev->s_pixclock);
	}

	/* config scale timing */
	calc_dsp_frm_vst_hst(src, dst, lcdc_dev->s_pixclock);
	dst_frame_vst = dst->scl_vst;
	dst_frame_hst = dst->scl_hst;

	dsp_htotal    = dst->mode.hsync_len + dst->mode.left_margin +
			dst->mode.xres + dst->mode.right_margin;
	dsp_hs_end    = dst->mode.hsync_len;

	dsp_vtotal    = dst->mode.vsync_len + dst->mode.upper_margin +
			dst->mode.yres + dst->mode.lower_margin;
	dsp_vs_end    = dst->mode.vsync_len;

	dsp_hbor_end  = dst->mode.hsync_len + dst->mode.left_margin +
			dst->mode.xres;
	dsp_hbor_st   = dst->mode.hsync_len + dst->mode.left_margin;
	dsp_vbor_end  = dst->mode.vsync_len + dst->mode.upper_margin +
			dst->mode.yres;
	dsp_vbor_st   = dst->mode.vsync_len + dst->mode.upper_margin;

	dsp_hact_st   = dsp_hbor_st  + bor_left;
	dsp_hact_end  = dsp_hbor_end - bor_right;
	dsp_vact_st   = dsp_vbor_st  + bor_up;
	dsp_vact_end  = dsp_vbor_end - bor_down;

	src_w = src->mode.xres;
	src_h = src->mode.yres;
	dst_w = dsp_hact_end - dsp_hact_st;
	dst_h = dsp_vact_end - dsp_vact_st;

	/* calc scale factor */
	scl_h_factor = ((src_w - 1) << 12) / (dst_w - 1);
	scl_v_factor = ((src_h - 1) << 12) / (dst_h - 1);

	spin_lock(&lcdc_dev->reg_lock);
	if (dst->color_mode != src->color_mode) {
		/*dev_drv->output_color = dst->color_mode;
		if (dev_drv->output_color == COLOR_YCBCR)
			dev_drv->overlay_mode = VOP_YUV_DOMAIN;
		else
			dev_drv->overlay_mode = VOP_RGB_DOMAIN;
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_SW_OVERLAY_MODE,
			     v_SW_OVERLAY_MODE(dev_drv->overlay_mode));*/
	}

	lcdc_writel(lcdc_dev, SCALER_FACTOR,
		    v_SCALER_H_FACTOR(scl_h_factor) |
		    v_SCALER_V_FACTOR(scl_v_factor));

	lcdc_writel(lcdc_dev, SCALER_FRAME_ST,
		    v_SCALER_FRAME_HST(dst_frame_hst) |
		    v_SCALER_FRAME_VST(dst_frame_vst));
	lcdc_writel(lcdc_dev, SCALER_DSP_HOR_TIMING,
		    v_SCALER_HS_END(dsp_hs_end) |
		    v_SCALER_HTOTAL(dsp_htotal));
	lcdc_writel(lcdc_dev, SCALER_DSP_HACT_ST_END,
		    v_SCALER_HAEP(dsp_hact_end) |
		    v_SCALER_HASP(dsp_hact_st));
	lcdc_writel(lcdc_dev, SCALER_DSP_VER_TIMING,
		    v_SCALER_VS_END(dsp_vs_end) |
		    v_SCALER_VTOTAL(dsp_vtotal));
	lcdc_writel(lcdc_dev, SCALER_DSP_VACT_ST_END,
		    v_SCALER_VAEP(dsp_vact_end) |
		    v_SCALER_VASP(dsp_vact_st));
	lcdc_writel(lcdc_dev, SCALER_DSP_HBOR_TIMING,
		    v_SCALER_HBOR_END(dsp_hbor_end) |
		    v_SCALER_HBOR_ST(dsp_hbor_st));
	lcdc_writel(lcdc_dev, SCALER_DSP_VBOR_TIMING,
		    v_SCALER_VBOR_END(dsp_vbor_end) |
		    v_SCALER_VBOR_ST(dsp_vbor_st));
	lcdc_msk_reg(lcdc_dev, SCALER_CTRL,
		     m_SCALER_VSYNC_VST | m_SCALER_VSYNC_MODE,
		     v_SCALER_VSYNC_VST(4) | v_SCALER_VSYNC_MODE(2));
	lcdc_msk_reg(lcdc_dev, SCALER_CTRL,
		     m_SCALER_EN | m_SCALER_OUT_ZERO |
		     m_SCALER_OUT_EN,
		     v_SCALER_EN(1) | v_SCALER_OUT_ZERO(0) |
		     v_SCALER_OUT_EN(1));

	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static void rk312x_lcdc_select_bcsh(struct rk_lcdc_driver *dev_drv,
				    struct lcdc_device *lcdc_dev)
{
	u32 bcsh_ctrl;
	if (dev_drv->overlay_mode == VOP_YUV_DOMAIN) {
		if (dev_drv->output_color == COLOR_YCBCR)/* bypass */
			lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
				     m_BCSH_Y2R_EN | m_BCSH_R2Y_EN,
				     v_BCSH_Y2R_EN(0) | v_BCSH_R2Y_EN(0));
	else	/* YUV2RGB */
		lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
			     m_BCSH_Y2R_EN | m_BCSH_Y2R_CSC_MODE |
			     m_BCSH_R2Y_EN,
			     v_BCSH_Y2R_EN(1) |
			     v_BCSH_Y2R_CSC_MODE(VOP_Y2R_CSC_MPEG) |
			     v_BCSH_R2Y_EN(0));
	} else {	/* overlay_mode=VOP_RGB_DOMAIN */
		if (dev_drv->output_color == COLOR_RGB) {
			/* bypass */
			bcsh_ctrl = lcdc_readl(lcdc_dev, BCSH_CTRL);
			if (((bcsh_ctrl&m_BCSH_EN) == 1) ||
				(dev_drv->bcsh.enable == 1))/*bcsh enabled*/
				lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
				     	m_BCSH_R2Y_EN | m_BCSH_Y2R_EN,
				     	v_BCSH_R2Y_EN(1) | v_BCSH_Y2R_EN(1));
			else/*bcsh disabled*/
				lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
				     	m_BCSH_R2Y_EN | m_BCSH_Y2R_EN,
				     	v_BCSH_R2Y_EN(0) | v_BCSH_Y2R_EN(0));
		} else	/* RGB2YUV */
			lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
				     m_BCSH_R2Y_EN |
					m_BCSH_R2Y_CSC_MODE | m_BCSH_Y2R_EN,
					v_BCSH_R2Y_EN(1) |
					v_BCSH_R2Y_CSC_MODE(VOP_Y2R_CSC_MPEG) |
					v_BCSH_Y2R_EN(0));
		}
}

static int rk312x_get_dspbuf_info(struct rk_lcdc_driver *dev_drv, u16 *xact,
				  u16 *yact, int *format, u32 *dsp_addr)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	u32 val;

	spin_lock(&lcdc_dev->reg_lock);

	val = lcdc_readl(lcdc_dev, WIN0_ACT_INFO);
	*xact = (val & m_ACT_WIDTH)+1;
	*yact = ((val & m_ACT_HEIGHT)>>16)+1;

	val = lcdc_readl(lcdc_dev, SYS_CTRL);

	*format = (val & m_WIN0_FORMAT) >> 3;
	*dsp_addr = lcdc_readl(lcdc_dev, WIN0_YRGB_MST);

	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int rk312x_post_dspbuf(struct rk_lcdc_driver *dev_drv, u32 rgb_mst,
			      int format, u16 xact, u16 yact, u16 xvir)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	u32 val, mask;

	mask = m_WIN0_FORMAT;
	val = v_WIN0_FORMAT(format);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);

	lcdc_msk_reg(lcdc_dev, WIN0_VIR, m_YRGB_VIR,
			v_YRGB_VIR(xvir));
	lcdc_writel(lcdc_dev, WIN0_ACT_INFO, v_ACT_WIDTH(xact) |
		    v_ACT_HEIGHT(yact));

	lcdc_writel(lcdc_dev, WIN0_YRGB_MST, rgb_mst);

	lcdc_cfg_done(lcdc_dev);

	return 0;
}

static int rk312x_load_screen(struct rk_lcdc_driver *dev_drv, bool initscreen)
{
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
		lcdc_msk_reg(lcdc_dev, SYS_CTRL,
			     m_LCDC_STANDBY, v_LCDC_STANDBY(1));
		lcdc_cfg_done(lcdc_dev);
		mdelay(50);
		/* Select output color domain */
		/*dev_drv->output_color = screen->color_mode;
		if (lcdc_dev->soc_type == VOP_RK312X) {
			if (dev_drv->output_color == COLOR_YCBCR)
				dev_drv->overlay_mode = VOP_YUV_DOMAIN;
			else
				dev_drv->overlay_mode = VOP_RGB_DOMAIN;
		} else {
			dev_drv->output_color = COLOR_RGB;
			dev_drv->overlay_mode = VOP_RGB_DOMAIN;
		}*/
		dev_drv->overlay_mode = VOP_RGB_DOMAIN;
		/*something wrong at yuv domain*/

		switch (screen->type) {
		case SCREEN_RGB:
			if (lcdc_dev->soc_type == VOP_RK312X) {
				mask = m_RGB_DCLK_EN | m_RGB_DCLK_INVERT;
				val = v_RGB_DCLK_EN(1) | v_RGB_DCLK_INVERT(0);
				lcdc_msk_reg(lcdc_dev, AXI_BUS_CTRL, mask, val);
			}
			break;
		case SCREEN_LVDS:
			if (lcdc_dev->soc_type == VOP_RK312X) {
				mask = m_LVDS_DCLK_EN | m_LVDS_DCLK_INVERT;
				val = v_LVDS_DCLK_EN(1) | v_LVDS_DCLK_INVERT(1);
				lcdc_msk_reg(lcdc_dev, AXI_BUS_CTRL, mask, val);
			}
			break;
		case SCREEN_MIPI:
			if (lcdc_dev->soc_type == VOP_RK312X) {
				mask = m_MIPI_DCLK_EN | m_MIPI_DCLK_INVERT;
				val = v_MIPI_DCLK_EN(1) | v_MIPI_DCLK_INVERT(0);
				lcdc_msk_reg(lcdc_dev, AXI_BUS_CTRL, mask, val);
			}
			break;
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
			if (lcdc_dev->soc_type == VOP_RK312X) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
				     m_SW_UV_OFFSET_EN,
				     v_SW_UV_OFFSET_EN(0));
				mask = m_HDMI_HSYNC_POL | m_HDMI_VSYNC_POL |
				       m_HDMI_DEN_POL;
				val = v_HDMI_HSYNC_POL(screen->pin_hsync) |
				      v_HDMI_VSYNC_POL(screen->pin_vsync) |
				      v_HDMI_DEN_POL(screen->pin_den);
				lcdc_msk_reg(lcdc_dev, INT_SCALER, mask, val);
			} else {
				mask = (1 << 4) | (1 << 5) | (1 << 6);
				val = (screen->pin_hsync << 4) |
					(screen->pin_vsync << 5) |
					(screen->pin_den << 6);
				grf_writel(RK3036_GRF_SOC_CON2,
					   (mask << 16) | val);
			}
			rk312x_lcdc_select_bcsh(dev_drv,  lcdc_dev);
			break;
		case SCREEN_TVOUT:
		case SCREEN_TVOUT_TEST:
			mask = m_TVE_DAC_DCLK_EN;
			val = v_TVE_DAC_DCLK_EN(1);
			if (screen->pixelrepeat) {
				mask |= m_CORE_CLK_DIV_EN;
				val |= v_CORE_CLK_DIV_EN(1);
			}
			lcdc_msk_reg(lcdc_dev, AXI_BUS_CTRL, mask, val);
			if (x_res == 720 && y_res == 576)
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_TVE_MODE,
					     v_TVE_MODE(TV_PAL));
			else if (x_res == 720 && y_res == 480)
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_TVE_MODE,
					     v_TVE_MODE(TV_NTSC));
			else {
				dev_err(lcdc_dev->dev,
					"unsupported video timing!\n");
				return -1;
			}
			if (lcdc_dev->soc_type == VOP_RK312X) {
				if (screen->type == SCREEN_TVOUT_TEST)
			/*for TVE index test,vop must ovarlay at yuv domain*/
					dev_drv->overlay_mode = VOP_YUV_DOMAIN;
					lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
						     m_SW_UV_OFFSET_EN,
						     v_SW_UV_OFFSET_EN(1));

			rk312x_lcdc_select_bcsh(dev_drv, lcdc_dev);
			}
			break;
		default:
			dev_err(lcdc_dev->dev, "un supported interface!\n");
			break;
		}
		if (lcdc_dev->soc_type == VOP_RK312X) {
			switch (dev_drv->screen0->face) {
			case OUT_P565:
				face = OUT_P565;
				mask = m_DITHER_DOWN_EN |
				       m_DITHER_DOWN_MODE |
				       m_DITHER_DOWN_SEL;
				val = v_DITHER_DOWN_EN(1) |
				      v_DITHER_DOWN_MODE(0) |
				      v_DITHER_DOWN_SEL(1);
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
				break;
			case OUT_P666:
				face = OUT_P666;
				mask = m_DITHER_DOWN_EN |
				       m_DITHER_DOWN_MODE |
				       m_DITHER_DOWN_SEL;
				val = v_DITHER_DOWN_EN(1) |
				      v_DITHER_DOWN_MODE(1) |
				      v_DITHER_DOWN_SEL(1);
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
				break;
			case OUT_D888_P565:
				face = OUT_P888;
				mask = m_DITHER_DOWN_EN |
				       m_DITHER_DOWN_MODE |
				       m_DITHER_DOWN_SEL;
				val = v_DITHER_DOWN_EN(1) |
				      v_DITHER_DOWN_MODE(0) |
				      v_DITHER_DOWN_SEL(1);
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
				break;
			case OUT_D888_P666:
				face = OUT_P888;
				mask = m_DITHER_DOWN_EN |
				       m_DITHER_DOWN_MODE |
				       m_DITHER_DOWN_SEL;
				val = v_DITHER_DOWN_EN(1) |
				      v_DITHER_DOWN_MODE(1) |
				      v_DITHER_DOWN_SEL(1);
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
				break;
			case OUT_P888:
				face = OUT_P888;
				mask = m_DITHER_DOWN_EN | m_DITHER_UP_EN;
				val = v_DITHER_DOWN_EN(0) | v_DITHER_UP_EN(0);
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
				break;
			default:
				dev_err(lcdc_dev->dev, "un supported interface!\n");
				break;
			}
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_SW_OVERLAY_MODE,
				     v_SW_OVERLAY_MODE(dev_drv->overlay_mode));
		}

		mask = m_HSYNC_POL | m_VSYNC_POL |
		       m_DEN_POL | m_DCLK_POL;
		val = v_HSYNC_POL(screen->pin_hsync) |
		      v_VSYNC_POL(screen->pin_vsync) |
		      v_DEN_POL(screen->pin_den) |
		      v_DCLK_POL(screen->pin_dclk);

		if (screen->type != SCREEN_HDMI) {
			mask |= m_DSP_OUT_FORMAT;
			val |= v_DSP_OUT_FORMAT(face);
		}

		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);

		mask = m_BG_COLOR | m_DSP_BG_SWAP | m_DSP_RB_SWAP |
		       m_DSP_RG_SWAP | m_DSP_DELTA_SWAP |
		       m_DSP_DUMMY_SWAP | m_BLANK_EN | m_BLACK_EN;

		val = v_BG_COLOR(0x000000) | v_DSP_BG_SWAP(screen->swap_gb) |
		      v_DSP_RB_SWAP(screen->swap_rb) |
		      v_DSP_RG_SWAP(screen->swap_rg) |
		      v_DSP_DELTA_SWAP(screen->swap_delta) |
		      v_DSP_DUMMY_SWAP(screen->swap_dumy) |
		      v_BLANK_EN(0) | v_BLACK_EN(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);

		/* config timing */
		val = v_HSYNC(screen->mode.hsync_len) |
		      v_HORPRD(screen->mode.hsync_len + left_margin + x_res +
				right_margin);
		lcdc_writel(lcdc_dev, DSP_HTOTAL_HS_END, val);
		val = v_HAEP(screen->mode.hsync_len + left_margin + x_res) |
		      v_HASP(screen->mode.hsync_len + left_margin);
		lcdc_writel(lcdc_dev, DSP_HACT_ST_END, val);

		if (screen->mode.vmode == FB_VMODE_INTERLACED) {
			/* First Field Timing */
			lcdc_writel(lcdc_dev, DSP_VTOTAL_VS_END,
				    v_VSYNC(screen->mode.vsync_len) |
				    v_VERPRD(2 * (screen->mode.vsync_len +
						  upper_margin +
						  lower_margin) + y_res + 1));
			lcdc_writel(lcdc_dev, DSP_VACT_ST_END,
				    v_VAEP(screen->mode.vsync_len +
					   upper_margin + y_res / 2) |
				    v_VASP(screen->mode.vsync_len +
					   upper_margin));
			/* Second Field Timing */
			lcdc_writel(lcdc_dev, DSP_VS_ST_END_F1,
				    v_VSYNC_ST_F1(screen->mode.vsync_len +
						  upper_margin + y_res / 2 +
						lower_margin) |
				    v_VSYNC_END_F1(2 * screen->mode.vsync_len +
						   upper_margin + y_res / 2 +
						   lower_margin));
			lcdc_writel(lcdc_dev, DSP_VACT_ST_END_F1,
				    v_VAEP(2 * (screen->mode.vsync_len +
						upper_margin) +
						y_res + lower_margin + 1) |
				    v_VASP(2 * (screen->mode.vsync_len +
						upper_margin) +
						y_res / 2 + lower_margin + 1));

			lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
				     m_INTERLACE_DSP_EN |
				     m_WIN0_YRGB_DEFLICK_EN |
				     m_WIN0_CBR_DEFLICK_EN |
				     m_INTERLACE_FIELD_POL |
				     m_WIN0_INTERLACE_EN |
				     m_WIN1_INTERLACE_EN,
				     v_INTERLACE_DSP_EN(1) |
				     v_WIN0_YRGB_DEFLICK_EN(1) |
				     v_WIN0_CBR_DEFLICK_EN(1) |
				     v_INTERLACE_FIELD_POL(0) |
				     v_WIN0_INTERLACE_EN(1) |
				     v_WIN1_INTERLACE_EN(1));
			mask = m_LF_INT_NUM;
			val = v_LF_INT_NUM(screen->mode.vsync_len +
					   screen->mode.upper_margin +
					   screen->mode.yres/2);
			lcdc_msk_reg(lcdc_dev, INT_STATUS, mask, val);
		} else {
			val = v_VSYNC(screen->mode.vsync_len) |
			      v_VERPRD(screen->mode.vsync_len + upper_margin +
				     y_res + lower_margin);
			lcdc_writel(lcdc_dev, DSP_VTOTAL_VS_END, val);

			val = v_VAEP(screen->mode.vsync_len +
				     upper_margin + y_res) |
			      v_VASP(screen->mode.vsync_len + upper_margin);
			lcdc_writel(lcdc_dev, DSP_VACT_ST_END, val);

			lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
				     m_INTERLACE_DSP_EN |
				     m_WIN0_YRGB_DEFLICK_EN |
				     m_WIN0_CBR_DEFLICK_EN |
				     m_INTERLACE_FIELD_POL |
				     m_WIN0_INTERLACE_EN |
				     m_WIN1_INTERLACE_EN,
				     v_INTERLACE_DSP_EN(0) |
				     v_WIN0_YRGB_DEFLICK_EN(0) |
				     v_WIN0_CBR_DEFLICK_EN(0) |
				     v_INTERLACE_FIELD_POL(0) |
				     v_WIN0_INTERLACE_EN(0) |
				     v_WIN1_INTERLACE_EN(0));
			mask = m_LF_INT_NUM;
			val = v_LF_INT_NUM(screen->mode.vsync_len +
					   screen->mode.upper_margin +
					   screen->mode.yres);
			lcdc_msk_reg(lcdc_dev, INT_STATUS, mask, val);
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
	rk312x_lcdc_set_dclk(dev_drv, 1);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY, v_LCDC_STANDBY(0));
	lcdc_cfg_done(lcdc_dev);
	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();
	if (screen->init)
		screen->init();

	return 0;
}

static int rk312x_lcdc_open(struct rk_lcdc_driver *dev_drv, int win_id,
			    bool open)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);

	/* enable clk,when first layer open */
	if ((open) && (!lcdc_dev->atv_layer_cnt)) {
		rockchip_set_system_status(SYS_STATUS_LCDC0);
		rk312x_lcdc_pre_init(dev_drv);
		rk312x_lcdc_clk_enable(lcdc_dev);
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
			/*if (dev_drv->mmu_dev)
				rockchip_iovmm_activate(dev_drv->dev);*/
		}
#endif
		rk312x_lcdc_reg_restore(lcdc_dev);
		/*if (dev_drv->iommu_enabled)
			rk312x_lcdc_mmu_en(dev_drv);*/
		if ((support_uboot_display() && (lcdc_dev->prop == PRMRY))) {
			rk312x_lcdc_set_dclk(dev_drv, 0);
			rk312x_lcdc_enable_irq(dev_drv);
		} else {
			rk312x_load_screen(dev_drv, 1);
		}

		/* set screen lut */
		if (dev_drv->cur_screen->dsp_lut)
			rk312x_lcdc_set_lut(dev_drv,
					    dev_drv->cur_screen->dsp_lut);
	}

	if (win_id < ARRAY_SIZE(lcdc_win))
		lcdc_layer_enable(lcdc_dev, win_id, open);
	else
		dev_err(lcdc_dev->dev, "invalid win id:%d\n", win_id);

	/* when all layer closed,disable clk */
/*	if ((!open) && (!lcdc_dev->atv_layer_cnt)) {
		rk312x_lcdc_disable_irq(lcdc_dev);
		rk312x_lcdc_reg_update(dev_drv);
#if defined(CONFIG_ROCKCHIP_IOMMU)
		if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_deactivate(dev_drv->dev);
		}
#endif
		rk312x_lcdc_clk_disable(lcdc_dev);
		rockchip_clear_system_status(SYS_STATUS_LCDC0);
	}*/
	return 0;
}

static int rk312x_lcdc_set_par(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
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
	win->area[0].dsp_stx = win->area[0].xpos + screen->mode.left_margin +
			       screen->mode.hsync_len;
	if (screen->mode.vmode == FB_VMODE_INTERLACED) {
		win->area[0].ysize /= 2;
		win->area[0].dsp_sty = win->area[0].ypos / 2 +
				       screen->mode.upper_margin +
				       screen->mode.vsync_len;
	} else {
		win->area[0].dsp_sty = win->area[0].ypos +
				       screen->mode.upper_margin +
				       screen->mode.vsync_len;
	}
	win->scale_yrgb_x = CalScale(win->area[0].xact, win->area[0].xsize);
	win->scale_yrgb_y = CalScale(win->area[0].yact, win->area[0].ysize);

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
			win->scale_cbcr_x =
			    CalScale(win->area[0].xact, win->area[0].xsize);
			win->scale_cbcr_y =
			    CalScale(win->area[0].yact, win->area[0].ysize);
			win->area[0].swap_rb = 0;
		} else {
			dev_err(lcdc_dev->driver.dev,
				"%s:un supported format!\n", __func__);
		}
		break;
	case YUV422:
		if (win_id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR422;
			win->scale_cbcr_x = CalScale((win->area[0].xact / 2),
					    win->area[0].xsize);
			win->scale_cbcr_y =
			    CalScale(win->area[0].yact, win->area[0].ysize);
			win->area[0].swap_rb = 0;
		} else {
			dev_err(lcdc_dev->driver.dev,
				"%s:un supported format!\n", __func__);
		}
		break;
	case YUV420:
		if (win_id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR420;
			win->scale_cbcr_x =
			    CalScale(win->area[0].xact / 2, win->area[0].xsize);
			win->scale_cbcr_y =
			    CalScale(win->area[0].yact / 2, win->area[0].ysize);
			win->area[0].swap_rb = 0;
		} else {
			dev_err(lcdc_dev->driver.dev,
				"%s:un supported format!\n", __func__);
		}
		break;
	default:
		dev_err(lcdc_dev->driver.dev, "%s:un supported format!\n",
			__func__);
		break;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(1,
	    "lcdc%d>>%s\n>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d\n"
	    ">>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n", lcdc_dev->id, __func__,
	    get_format_string(win->area[0].format, fmt), win->area[0].xact,
	    win->area[0].yact, win->area[0].xsize, win->area[0].ysize,
	    win->area[0].xvir, win->area[0].yvir, win->area[0].xpos,
	    win->area[0].ypos);
	return 0;
}

static int rk312x_lcdc_pan_display(struct rk_lcdc_driver *dev_drv, int win_id)
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
		win->area[0].y_addr =
		    win->area[0].smem_start + win->area[0].y_offset;
		win->area[0].uv_addr =
		    win->area[0].cbr_start + win->area[0].c_offset;
		if (win->area[0].y_addr)
			lcdc_layer_update_regs(lcdc_dev, win);
		/* lcdc_cfg_done(lcdc_dev); */
	}
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(2, "lcdc%d>>%s:y_addr:0x%x>>uv_addr:0x%x>>offset:%d\n",
	    lcdc_dev->id, __func__, win->area[0].y_addr, win->area[0].uv_addr,
	    win->area[0].y_offset);
	/* this is the first frame of the system,enable frame start interrupt */
	if ((dev_drv->first_frame)) {
		dev_drv->first_frame = 0;
		rk312x_lcdc_enable_irq(dev_drv);
	}

	return 0;
}

static int rk312x_lcdc_ioctl(struct rk_lcdc_driver *dev_drv, unsigned int cmd,
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

static int rk312x_lcdc_get_win_id(struct rk_lcdc_driver *dev_drv,
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

static int rk312x_lcdc_get_win_state(struct rk_lcdc_driver *dev_drv, int win_id)
{
	return 0;
}

static int rk312x_lcdc_ovl_mgr(struct rk_lcdc_driver *dev_drv, int swap,
			       bool set)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int ovl;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (set) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_WIN0_TOP,
				     v_WIN0_TOP(swap));
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

static int rk312x_lcdc_get_backlight_device(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	struct device_node *backlight;

	if (lcdc_dev->backlight)
		return 0;

	backlight = of_parse_phandle(lcdc_dev->dev->of_node,
				     "backlight", 0);
	if (backlight) {
		lcdc_dev->backlight = of_find_backlight_by_node(backlight);
		if (!lcdc_dev->backlight)
			dev_info(lcdc_dev->dev, "No find backlight device\n");
	} else {
		dev_info(lcdc_dev->dev, "No find backlight device node\n");
	}

	return 0;
}

static int rk312x_lcdc_early_suspend(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	if (dev_drv->suspend_flag)
		return 0;

	/* close the backlight */
	rk312x_lcdc_get_backlight_device(dev_drv);
	if (lcdc_dev->backlight) {
		lcdc_dev->backlight->props.fb_blank = FB_BLANK_POWERDOWN;
		backlight_update_status(lcdc_dev->backlight);
	}

	dev_drv->suspend_flag = 1;
	flush_kthread_worker(&dev_drv->update_regs_worker);

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
		dev_drv->trsm_ops->disable();
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BLANK_EN, v_BLANK_EN(1));
		lcdc_msk_reg(lcdc_dev, INT_STATUS,
			     m_FS_INT_CLEAR | m_LF_INT_CLEAR,
			     v_FS_INT_CLEAR(1) | v_LF_INT_CLEAR(1));
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
	rk312x_lcdc_clk_disable(lcdc_dev);
	rk_disp_pwr_disable(dev_drv);
	return 0;
}

static int rk312x_lcdc_early_resume(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (!dev_drv->suspend_flag)
		return 0;
	rk_disp_pwr_enable(dev_drv);

	rk312x_lcdc_clk_enable(lcdc_dev);
	rk312x_lcdc_reg_restore(lcdc_dev);

	/* config for the FRC mode of dither down */
	if (dev_drv->cur_screen &&
			dev_drv->cur_screen->face != OUT_P888) {
		lcdc_writel(lcdc_dev, FRC_LOWER01_0, 0x12844821);
		lcdc_writel(lcdc_dev, FRC_LOWER01_1, 0x21488412);
		lcdc_writel(lcdc_dev, FRC_LOWER10_0, 0x55aaaa55);
		lcdc_writel(lcdc_dev, FRC_LOWER10_1, 0x55aaaa55);
		lcdc_writel(lcdc_dev, FRC_LOWER11_0, 0xdeb77deb);
		lcdc_writel(lcdc_dev, FRC_LOWER11_1, 0xed7bb7de);
	}

	/* set screen lut */
	if (dev_drv->cur_screen && dev_drv->cur_screen->dsp_lut)
		rk312x_lcdc_set_lut(dev_drv,
				    dev_drv->cur_screen->dsp_lut);
	/*set hwc lut*/
	rk312x_lcdc_set_hwc_lut(dev_drv, dev_drv->hwc_lut, 0);

	spin_lock(&lcdc_dev->reg_lock);

	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_OUT_ZERO,
			v_DSP_OUT_ZERO(0));
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY,
			v_LCDC_STANDBY(0));
	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BLANK_EN, v_BLANK_EN(0));
	lcdc_cfg_done(lcdc_dev);

	if (dev_drv->iommu_enabled) {
		if (dev_drv->mmu_dev)
			rockchip_iovmm_activate(dev_drv->dev);
	}

	spin_unlock(&lcdc_dev->reg_lock);
	dev_drv->suspend_flag = 0;

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();
	mdelay(100);

	return 0;
}

static int rk312x_lcdc_blank(struct rk_lcdc_driver *dev_drv,
			     int win_id, int blank_mode)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		rk312x_lcdc_early_resume(dev_drv);
		break;
	case FB_BLANK_NORMAL:
		rk312x_lcdc_early_suspend(dev_drv);
		break;
	default:
		rk312x_lcdc_early_suspend(dev_drv);
		break;
	}

	dev_info(dev_drv->dev, "blank mode:%d\n", blank_mode);

	return 0;
}

static int rk312x_lcdc_cfg_done(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	int i;
	struct rk_lcdc_win *win = NULL;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
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
static int rk312x_lcdc_get_bcsh_hue(struct rk_lcdc_driver *dev_drv,
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
			val >>= 16;
			break;
		default:
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return val;
}

static int rk312x_lcdc_set_bcsh_hue(struct rk_lcdc_driver *dev_drv, int sin_hue,
				    int cos_hue)
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

static int rk312x_lcdc_set_bcsh_bcs(struct rk_lcdc_driver *dev_drv,
				    bcsh_bcs_mode mode, int value)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		switch (mode) {
		case BRIGHTNESS:
			/* from 0 to 255,typical is 128 */
			if (value < 0x80)
				value += 0x80;
			else if (value >= 0x80)
				value = value - 0x80;
			mask = m_BCSH_BRIGHTNESS;
			val = v_BCSH_BRIGHTNESS(value);
			break;
		case CONTRAST:
			/* from 0 to 510,typical is 256 */
			mask = m_BCSH_CONTRAST;
			val = v_BCSH_CONTRAST(value);
			break;
		case SAT_CON:
			/* from 0 to 1015,typical is 256 */
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

static int rk312x_lcdc_get_bcsh_bcs(struct rk_lcdc_driver *dev_drv,
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
			if (val > 0x80)
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

static int rk312x_lcdc_open_bcsh(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;
	if (dev_drv->bcsh_init_status && open) {
		dev_drv->bcsh_init_status = 0;
		return 0;
	}
	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (open) {
			lcdc_msk_reg(lcdc_dev,
				     BCSH_CTRL, m_BCSH_EN | m_BCSH_OUT_MODE,
				     v_BCSH_EN(1) | v_BCSH_OUT_MODE(3));
			lcdc_writel(lcdc_dev, BCSH_BCS,
				    v_BCSH_BRIGHTNESS(0x00) |
				    v_BCSH_CONTRAST(0x80) |
				    v_BCSH_SAT_CON(0x80));
			lcdc_writel(lcdc_dev, BCSH_H, v_BCSH_COS_HUE(0x80));
			dev_drv->bcsh.enable = 1;
		} else {
			mask = m_BCSH_EN;
			val = v_BCSH_EN(0);
			lcdc_msk_reg(lcdc_dev, BCSH_CTRL, mask, val);
			dev_drv->bcsh.enable = 0;
		}
		rk312x_lcdc_select_bcsh(dev_drv,  lcdc_dev);
		lcdc_cfg_done(lcdc_dev);
	}

	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk312x_fb_win_remap(struct rk_lcdc_driver *dev_drv, u16 order)
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

static int rk312x_lcdc_fps_mgr(struct rk_lcdc_driver *dev_drv, int fps,
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
	dev_drv->pixclock = lcdc_dev->pixclock = pixclock;
	fps = rk_fb_calc_fps(lcdc_dev->screen, pixclock);
	screen->ft = 1000 / fps;	/*one frame time in ms */

	if (set)
		dev_info(dev_drv->dev, "%s:dclk:%lu,fps:%d\n", __func__,
			 clk_get_rate(lcdc_dev->dclk), fps);

	return fps;
}

static int rk312x_lcdc_set_irq_to_cpu(struct rk_lcdc_driver *dev_drv,
				      int enable)
{
	struct lcdc_device *lcdc_dev =
				container_of(dev_drv,
					     struct lcdc_device, driver);
	if (enable)
		enable_irq(lcdc_dev->irq);
	else
		disable_irq(lcdc_dev->irq);
	return 0;
}

static int rk312x_lcdc_poll_vblank(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 int_reg;
	int ret;

	if (lcdc_dev->clk_on && (!dev_drv->suspend_flag)) {
		int_reg = lcdc_readl(lcdc_dev, INT_STATUS);
		if (int_reg & m_LF_INT_STA) {
			dev_drv->frame_time.last_framedone_t =
					dev_drv->frame_time.framedone_t;
			dev_drv->frame_time.framedone_t = cpu_clock(0);
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

static int rk312x_lcdc_get_dsp_addr(struct rk_lcdc_driver *dev_drv,
				    unsigned int *dsp_addr)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (lcdc_dev->clk_on) {
		dsp_addr[0] = lcdc_readl(lcdc_dev, WIN0_YRGB_MST);
		if (lcdc_dev->soc_type == VOP_RK3036)
			dsp_addr[1] = lcdc_readl(lcdc_dev, WIN1_MST);
		else if (lcdc_dev->soc_type == VOP_RK312X)
			dsp_addr[1] = lcdc_readl(lcdc_dev, WIN1_MST_RK312X);
	}
	return 0;
}

static ssize_t rk312x_lcdc_get_disp_info(struct rk_lcdc_driver *dev_drv,
					 char *buf, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv, struct lcdc_device,
						    driver);
	char format_w0[9] = "NULL";
	char format_w1[9] = "NULL";
	char status_w0[9] = "NULL";
	char status_w1[9] = "NULL";
	u32 fmt_id, act_info, dsp_info, dsp_st, factor;
	u16 xvir_w0, x_act_w0, y_act_w0, x_dsp_w0, y_dsp_w0, x_st_w0, y_st_w0;
	u16 xvir_w1, x_act_w1, y_act_w1, x_dsp_w1, y_dsp_w1, x_st_w1, y_st_w1;
	u16 x_factor, y_factor, x_scale, y_scale;
	u16 ovl;
	u32 win1_dsp_yaddr = 0;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		/* data format */
		fmt_id = lcdc_readl(lcdc_dev, SYS_CTRL);
		get_format_string((fmt_id & m_WIN0_FORMAT) >> 3, format_w0);
		get_format_string((fmt_id & m_WIN1_FORMAT) >> 6, format_w1);

		/* win status */
		if (fmt_id & m_WIN0_EN)
			strcpy(status_w0, "enabled");
		else
			strcpy(status_w0, "disabled");

		if ((fmt_id & m_WIN1_EN) >> 1)
			strcpy(status_w1, "enabled");
		else
			strcpy(status_w1, "disabled");

		/* ovl */
		ovl = lcdc_read_bit(lcdc_dev, DSP_CTRL0, m_WIN0_TOP);

		/* xvir */
		xvir_w0 = lcdc_readl(lcdc_dev, WIN0_VIR) & m_YRGB_VIR;
		xvir_w1 = lcdc_readl(lcdc_dev, WIN1_VIR) & m_YRGB_VIR;

		/* xact/yact */
		act_info = lcdc_readl(lcdc_dev, WIN0_ACT_INFO);
		x_act_w0 = (act_info & m_ACT_WIDTH) + 1;
		y_act_w0 = ((act_info & m_ACT_HEIGHT) >> 16) + 1;

		if (lcdc_dev->soc_type == VOP_RK3036) {
			act_info = lcdc_readl(lcdc_dev, WIN1_ACT_INFO);
			x_act_w1 = (act_info & m_ACT_WIDTH) + 1;
			y_act_w1 = ((act_info & m_ACT_HEIGHT) >> 16) + 1;
		} else if (lcdc_dev->soc_type == VOP_RK312X) {
			/* rk312x unsupport win1 scaler,so have no act info */
			x_act_w1 = 0;
			y_act_w1 = 0;
		}

		/* xsize/ysize */
		dsp_info = lcdc_readl(lcdc_dev, WIN0_DSP_INFO);
		x_dsp_w0 = (dsp_info & m_DSP_WIDTH) + 1;
		y_dsp_w0 = ((dsp_info & m_DSP_HEIGHT) >> 16) + 1;

		if (lcdc_dev->soc_type == VOP_RK3036)
			dsp_info = lcdc_readl(lcdc_dev, WIN1_DSP_INFO);
		else if (lcdc_dev->soc_type == VOP_RK312X)
			dsp_info = lcdc_readl(lcdc_dev, WIN1_DSP_INFO_RK312X);
		x_dsp_w1 = (dsp_info & m_DSP_WIDTH) + 1;
		y_dsp_w1 = ((dsp_info & m_DSP_HEIGHT) >> 16) + 1;

		/* xpos/ypos */
		dsp_st = lcdc_readl(lcdc_dev, WIN0_DSP_ST);
		x_st_w0 = dsp_st & m_DSP_STX;
		y_st_w0 = (dsp_st & m_DSP_STY) >> 16;

		if (lcdc_dev->soc_type == VOP_RK3036)
			dsp_st = lcdc_readl(lcdc_dev, WIN1_DSP_ST);
		else if (lcdc_dev->soc_type == VOP_RK312X)
			dsp_st = lcdc_readl(lcdc_dev, WIN1_DSP_ST_RK312X);

		x_st_w1 = dsp_st & m_DSP_STX;
		y_st_w1 = (dsp_st & m_DSP_STY) >> 16;

		/* scale factor */
		factor = lcdc_readl(lcdc_dev, WIN0_SCL_FACTOR_YRGB);
		x_factor = factor & m_X_SCL_FACTOR;
		y_factor = (factor & m_Y_SCL_FACTOR) >> 16;
		x_scale = 4096 * 100 / x_factor;
		y_scale = 4096 * 100 / y_factor;

		/* dsp addr */
		if (lcdc_dev->soc_type == VOP_RK3036)
			win1_dsp_yaddr = lcdc_readl(lcdc_dev, WIN1_MST);
		else if (lcdc_dev->soc_type == VOP_RK312X)
			win1_dsp_yaddr = lcdc_readl(lcdc_dev, WIN1_MST_RK312X);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
		return -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return snprintf(buf, PAGE_SIZE,
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
			"xact:%d\n"
			"yact:%d\n"
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
			x_scale / 100,
			x_scale % 100,
			y_scale / 100,
			y_scale % 100,
			format_w0,
			lcdc_readl(lcdc_dev, WIN0_YRGB_MST),
			lcdc_readl(lcdc_dev, WIN0_CBR_MST),
			status_w1,
			xvir_w1,
			x_act_w1,
			y_act_w1,
			x_dsp_w1,
			y_dsp_w1,
			x_st_w1,
			y_st_w1,
			format_w1,
			win1_dsp_yaddr,
			ovl ? "win0 on the top of win1\n" :
			"win1 on the top of win0\n");
}

static int rk312x_lcdc_reg_dump(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device,
						    driver);
	int *cbase = (int *)lcdc_dev->regs;
	int *regsbak = (int *)lcdc_dev->regsbak;
	int i, j;

	pr_info("back up reg:\n");
	for (i = 0; i <= (0xDC >> 4); i++) {
		for (j = 0; j < 4; j++)
			pr_info("%08x  ", *(regsbak + i * 4 + j));
		pr_info("\n");
	}

	pr_info("lcdc reg:\n");
	for (i = 0; i <= (0xDC >> 4); i++) {
		for (j = 0; j < 4; j++)
			pr_info("%08x  ", readl_relaxed(cbase + i * 4 + j));
		pr_info("\n");
	}
	return 0;
}

static int rk312x_lcdc_dpi_open(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	if (lcdc_dev->soc_type == VOP_RK312X) {
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DIRECT_PATH_EN,
			     v_DIRECT_PATH_EN(open));
		lcdc_cfg_done(lcdc_dev);
	}
	return 0;
}

static int rk312x_lcdc_dpi_win_sel(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);

	if (lcdc_dev->soc_type == VOP_RK312X) {
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DIRECT_PATH_LAYER,
			     v_DIRECT_PATH_LAYER(win_id));
		lcdc_cfg_done(lcdc_dev);
	}
	return 0;
}

static int rk312x_lcdc_dpi_status(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	int ovl = 0;

	if (lcdc_dev->soc_type == VOP_RK312X)
		ovl = lcdc_read_bit(lcdc_dev, SYS_CTRL, m_DIRECT_PATH_EN);

	return ovl;
}

static int rk312x_lcdc_dsp_black(struct rk_lcdc_driver *dev_drv, int enable)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);

	rk312x_lcdc_get_backlight_device(dev_drv);

	if (enable) {
		/* close the backlight */
		if (lcdc_dev->backlight) {
			lcdc_dev->backlight->props.power = FB_BLANK_POWERDOWN;
			backlight_update_status(lcdc_dev->backlight);
		}

		spin_lock(&lcdc_dev->reg_lock);
		if (likely(lcdc_dev->clk_on)) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BLACK_EN,
				     v_BLACK_EN(1));
			lcdc_cfg_done(lcdc_dev);
		}
		spin_unlock(&lcdc_dev->reg_lock);

		if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
			dev_drv->trsm_ops->disable();
	} else {
		spin_lock(&lcdc_dev->reg_lock);
		if (likely(lcdc_dev->clk_on)) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BLACK_EN,
				     v_BLACK_EN(0));
			lcdc_cfg_done(lcdc_dev);
		}
		spin_unlock(&lcdc_dev->reg_lock);
		if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
			dev_drv->trsm_ops->enable();
		msleep(100);
		/* open the backlight */
		if (lcdc_dev->backlight) {
			lcdc_dev->backlight->props.power = FB_BLANK_UNBLANK;
			backlight_update_status(lcdc_dev->backlight);
		}
	}

	return 0;
}


static struct rk_lcdc_drv_ops lcdc_drv_ops = {
	.open = rk312x_lcdc_open,
	.load_screen = rk312x_load_screen,
	.get_dspbuf_info = rk312x_get_dspbuf_info,
	.post_dspbuf = rk312x_post_dspbuf,
	.set_par = rk312x_lcdc_set_par,
	.pan_display = rk312x_lcdc_pan_display,
	.direct_set_addr = rk312x_lcdc_direct_set_win_addr,
	.blank = rk312x_lcdc_blank,
	.ioctl = rk312x_lcdc_ioctl,
	.get_win_state = rk312x_lcdc_get_win_state,
	.ovl_mgr = rk312x_lcdc_ovl_mgr,
	.get_disp_info = rk312x_lcdc_get_disp_info,
	.fps_mgr = rk312x_lcdc_fps_mgr,
	.fb_get_win_id = rk312x_lcdc_get_win_id,
	.fb_win_remap = rk312x_fb_win_remap,
	.poll_vblank = rk312x_lcdc_poll_vblank,
	.get_dsp_addr = rk312x_lcdc_get_dsp_addr,
	.cfg_done = rk312x_lcdc_cfg_done,
	.dump_reg = rk312x_lcdc_reg_dump,
	.dpi_open = rk312x_lcdc_dpi_open,
	.dpi_win_sel = rk312x_lcdc_dpi_win_sel,
	.dpi_status = rk312x_lcdc_dpi_status,
	.set_dsp_bcsh_hue = rk312x_lcdc_set_bcsh_hue,
	.set_dsp_bcsh_bcs = rk312x_lcdc_set_bcsh_bcs,
	.get_dsp_bcsh_hue = rk312x_lcdc_get_bcsh_hue,
	.get_dsp_bcsh_bcs = rk312x_lcdc_get_bcsh_bcs,
	.open_bcsh = rk312x_lcdc_open_bcsh,
	.set_screen_scaler = rk312x_lcdc_set_scaler,
	.set_dsp_lut = rk312x_lcdc_set_lut,
	.set_hwc_lut = rk312x_lcdc_set_hwc_lut,
	.set_irq_to_cpu = rk312x_lcdc_set_irq_to_cpu,
	.dsp_black = rk312x_lcdc_dsp_black,
	.mmu_en = rk312x_lcdc_mmu_en,
};
#if 0
static const struct rk_lcdc_drvdata rk3036_lcdc_drvdata = {
	.soc_type = VOP_RK3036,
};
#endif
static const struct rk_lcdc_drvdata rk312x_lcdc_drvdata = {
	.soc_type = VOP_RK312X,
};

#if defined(CONFIG_OF)
static const struct of_device_id rk312x_lcdc_dt_ids[] = {
#if 0
	{
		.compatible = "rockchip,rk3036-lcdc",
		.data = (void *)&rk3036_lcdc_drvdata,
	},
#endif
	{
		.compatible = "rockchip,rk312x-lcdc",
		.data = (void *)&rk312x_lcdc_drvdata,
	},
};
#endif

static int rk312x_lcdc_parse_dt(struct lcdc_device *lcdc_dev)
{
	struct device_node *np = lcdc_dev->dev->of_node;
	const struct of_device_id *match;
	const struct rk_lcdc_drvdata *lcdc_drvdata;
	int val;

#if defined(CONFIG_ROCKCHIP_IOMMU)
	if (of_property_read_u32(np, "rockchip,iommu-enabled", &val))
		lcdc_dev->driver.iommu_enabled = 0;
	else
		lcdc_dev->driver.iommu_enabled = val;
#else
	lcdc_dev->driver.iommu_enabled = 0;
#endif

	if (of_property_read_u32(np, "rockchip,fb-win-map", &val))
		lcdc_dev->driver.fb_win_map = FB_DEFAULT_ORDER;
	else
		lcdc_dev->driver.fb_win_map = val;

	match = of_match_node(rk312x_lcdc_dt_ids, np);
	if (match) {
		lcdc_drvdata = (const struct rk_lcdc_drvdata *)match->data;
		lcdc_dev->soc_type = lcdc_drvdata->soc_type;
	} else {
		return PTR_ERR(match);
	}

	return 0;
}

static int rk312x_lcdc_probe(struct platform_device *pdev)
{
	struct lcdc_device *lcdc_dev = NULL;
	struct rk_lcdc_driver *dev_drv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	lcdc_dev = devm_kzalloc(dev, sizeof(struct lcdc_device), GFP_KERNEL);
	if (!lcdc_dev) {
		dev_err(&pdev->dev, "rk312x lcdc device kzalloc fail!\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->dev = dev;
	if (rk312x_lcdc_parse_dt(lcdc_dev)) {
		dev_err(lcdc_dev->dev, "rk312x lcdc parse dt failed!\n");
		goto err_parse_dt;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lcdc_dev->reg_phy_base = res->start;
	lcdc_dev->len = resource_size(res);
	lcdc_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(lcdc_dev->regs)) {
		ret = PTR_ERR(lcdc_dev->regs);
		goto err_remap_reg;
	}

	lcdc_dev->regsbak = devm_kzalloc(dev, lcdc_dev->len, GFP_KERNEL);
	if (IS_ERR(lcdc_dev->regsbak)) {
		dev_err(&pdev->dev, "rk312x lcdc device kmalloc fail!\n");
		ret = PTR_ERR(lcdc_dev->regsbak);
		goto err_remap_reg;
	}
	lcdc_dev->hwc_lut_addr_base = (lcdc_dev->regs + HWC_LUT_ADDR);
	lcdc_dev->dsp_lut_addr_base = (lcdc_dev->regs + DSP_LUT_ADDR);
	lcdc_dev->prop = PRMRY;
	dev_set_name(lcdc_dev->dev, "lcdc%d", lcdc_dev->id);
	dev_drv = &lcdc_dev->driver;
	dev_drv->dev = dev;
	dev_drv->prop = lcdc_dev->prop;
	dev_drv->id = lcdc_dev->id;
	dev_drv->ops = &lcdc_drv_ops;
	dev_drv->lcdc_win_num = ARRAY_SIZE(lcdc_win);
	spin_lock_init(&lcdc_dev->reg_lock);

	lcdc_dev->irq = platform_get_irq(pdev, 0);
	if (lcdc_dev->irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ for lcdc%d\n",
			lcdc_dev->id);
		ret = -ENXIO;
		goto err_request_irq;
	}

	ret = devm_request_irq(dev, lcdc_dev->irq, rk312x_lcdc_isr,
			       IRQF_DISABLED | IRQF_SHARED,
			       dev_name(dev), lcdc_dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot requeset irq %d - err %d\n",
			lcdc_dev->irq, ret);
		goto err_request_irq;
	}

	if (dev_drv->iommu_enabled)
		strcpy(dev_drv->mmu_dts_name, VOP_IOMMU_COMPATIBLE_NAME);

	ret = rk_fb_register(dev_drv, lcdc_win, lcdc_dev->id);
	if (ret < 0) {
		dev_err(dev, "register fb for lcdc%d failed!\n", lcdc_dev->id);
		goto err_register_fb;
	}
	lcdc_dev->screen = dev_drv->screen0;

	dev_info(dev, "lcdc%d probe ok, iommu %s\n",
		 lcdc_dev->id, dev_drv->iommu_enabled ? "enabled" : "disabled");

	return 0;
err_register_fb:
err_request_irq:
	devm_kfree(lcdc_dev->dev, lcdc_dev->regsbak);
err_remap_reg:
err_parse_dt:
	devm_kfree(&pdev->dev, lcdc_dev);
	return ret;
}

#if defined(CONFIG_PM)
static int rk312x_lcdc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int rk312x_lcdc_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define rk312x_lcdc_suspend NULL
#define rk312x_lcdc_resume  NULL
#endif

static int rk312x_lcdc_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk312x_lcdc_shutdown(struct platform_device *pdev)
{
	struct lcdc_device *lcdc_dev = platform_get_drvdata(pdev);
	struct rk_lcdc_driver *dev_drv=&lcdc_dev->driver;

	flush_kthread_worker(&dev_drv->update_regs_worker);
	kthread_stop(dev_drv->update_regs_thread);

	rk312x_lcdc_deinit(lcdc_dev);
	rk312x_lcdc_clk_disable(lcdc_dev);
	rk_disp_pwr_disable(&lcdc_dev->driver);
}

static struct platform_driver rk312x_lcdc_driver = {
	.probe = rk312x_lcdc_probe,
	.remove = rk312x_lcdc_remove,
	.driver = {
		   .name = "rk312x-lcdc",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk312x_lcdc_dt_ids),
		   },
	.suspend = rk312x_lcdc_suspend,
	.resume = rk312x_lcdc_resume,
	.shutdown = rk312x_lcdc_shutdown,
};

static int __init rk312x_lcdc_module_init(void)
{
	return platform_driver_register(&rk312x_lcdc_driver);
}

static void __exit rk312x_lcdc_module_exit(void)
{
	platform_driver_unregister(&rk312x_lcdc_driver);
}

fs_initcall(rk312x_lcdc_module_init);
module_exit(rk312x_lcdc_module_exit);
