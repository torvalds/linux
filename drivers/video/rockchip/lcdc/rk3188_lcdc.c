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
#include <asm/div64.h>
#include <asm/uaccess.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>

#include "rk3188_lcdc.h"


static int dbg_thresd;
module_param(dbg_thresd, int, S_IRUGO | S_IWUSR);

#define DBG(level, x...) do {			\
	if (unlikely(dbg_thresd >= level))	\
		printk(KERN_INFO x); } while (0)

//#define WAIT_FOR_SYNC 1

static int rk3188_lcdc_get_id(u32 phy_base)
{
	if (cpu_is_rk319x()) {
		if (phy_base == 0xffc40000)
			return 0;
		else if (phy_base == 0xffc50000)
			return 1;
		else
			return -EINVAL;
	} else if (cpu_is_rk3188()) {
		if (phy_base == 0x1010c000)
			return 0;
		else if (phy_base == 0x1010e000)
			return 1;
		else
			return -EINVAL;
	} else if (cpu_is_rk3026()) {
		if (phy_base == 0x1010e000)
			return 0;
		else if (phy_base == 0x01110000)
			return 1;
		else
			return -EINVAL;
	} else {
		pr_err("un supported platform \n");
		return -EINVAL;
	}

}

static int rk3188_lcdc_set_lut(struct rk_lcdc_driver *dev_drv)
{
	int i = 0;
	int __iomem *c;
	int v;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
							   struct
							   lcdc_device,
							   driver);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN, v_DSP_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	mdelay(25);
	for (i = 0; i < 256; i++) {
		v = dev_drv->cur_screen->dsp_lut[i];
		c = lcdc_dev->dsp_lut_addr_base + i;
		writel_relaxed(v, c);

	}
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN, v_DSP_LUT_EN(1));

	return 0;

}

static int rk3188_lcdc_clk_enable(struct lcdc_device *lcdc_dev)
{

	if (!lcdc_dev->clk_on) {
		clk_prepare_enable(lcdc_dev->hclk);
		clk_prepare_enable(lcdc_dev->dclk);
		clk_prepare_enable(lcdc_dev->aclk);
		//clk_enable(lcdc_dev->pd);
		spin_lock(&lcdc_dev->reg_lock);
		lcdc_dev->clk_on = 1;
		spin_unlock(&lcdc_dev->reg_lock);
	}
	return 0;
}

static int rk3188_lcdc_clk_disable(struct lcdc_device *lcdc_dev)
{
	if (lcdc_dev->clk_on) {
		spin_lock(&lcdc_dev->reg_lock);
		lcdc_dev->clk_on = 0;
		spin_unlock(&lcdc_dev->reg_lock);
		mdelay(25);
		clk_disable_unprepare(lcdc_dev->dclk);
		clk_disable_unprepare(lcdc_dev->hclk);
		clk_disable_unprepare(lcdc_dev->aclk);
		//clk_disable(lcdc_dev->pd);
	}
	return 0;
}

static int rk3188_lcdc_reg_dump(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						struct lcdc_device,
						driver);
	int *cbase = (int *)lcdc_dev->regs;
	int *regsbak = (int *)lcdc_dev->regsbak;
	int i, j;

	printk("back up reg:\n");
	for (i = 0; i <= (0x90 >> 4); i++) {
		for (j = 0; j < 4; j++)
			printk("%08x  ", *(regsbak + i * 4 + j));
		printk("\n");
	}

	printk("lcdc reg:\n");
	for (i = 0; i <= (0x90 >> 4); i++) {
		for (j = 0; j < 4; j++)
			printk("%08x  ", readl_relaxed(cbase + i * 4 + j));
		printk("\n");
	}
	return 0;
}

static void rk3188_lcdc_read_reg_defalut_cfg(struct lcdc_device
					     *lcdc_dev)
{
	int reg = 0;
	u32 value = 0;
	struct rk_lcdc_win *win0 = lcdc_dev->driver.win[0];
	struct rk_lcdc_win *win1 = lcdc_dev->driver.win[1];

	spin_lock(&lcdc_dev->reg_lock);
	for (reg = 0; reg < REG_CFG_DONE; reg += 4) {
		value = lcdc_readl(lcdc_dev, reg);
		switch (reg) {
		case SYS_CTRL:
			lcdc_dev->standby = (value & m_LCDC_STANDBY) >> 17;
			win0->state = (value & m_WIN0_EN) >> 0;
			win1->state = (value & m_WIN1_EN) >> 1;
			if (lcdc_dev->id == 0)
				lcdc_dev->atv_layer_cnt = win0->state;
			else
				lcdc_dev->atv_layer_cnt = win1->state;
			win0->area[0].swap_rb = (value & m_WIN0_RB_SWAP) >> 15;
			win1->area[0].swap_rb = (value & m_WIN1_RB_SWAP) >> 19;
			win0->area[0].fmt_cfg = (value & m_WIN0_FORMAT) >> 3;
			win1->area[0].fmt_cfg = (value & m_WIN1_FORMAT) >> 6;
			break;
		case WIN0_SCL_FACTOR_YRGB:
			win0->scale_yrgb_x = (value >> 0) & 0xffff;
			win0->scale_yrgb_y = (value >> 16) & 0xffff;
			break;
		case WIN0_SCL_FACTOR_CBR:
			win0->scale_cbcr_x = (value >> 0) & 0xffff;
			win0->scale_cbcr_y = (value >> 16) & 0xffff;
			break;
		case WIN0_ACT_INFO:
			win0->area[0].xact = (((value >> 0) & 0x1fff) + 1);
			win0->area[0].yact = (((value >> 16) & 0x1fff) + 1);
			break;
		case WIN0_DSP_ST:
			win0->area[0].dsp_stx = (value >> 0) & 0xfff;
			win0->area[0].dsp_sty = (value >> 16) & 0xfff;
			break;
		case WIN0_DSP_INFO:
			win0->area[0].xsize = (((value >> 0) & 0x7ff) + 1);
			win0->area[0].ysize = (((value >> 16) & 0x7ff) + 1);
			break;
		case WIN_VIR:
			win0->area[0].y_vir_stride = (value >> 0) & 0x1fff;
			win1->area[0].y_vir_stride = (value) & 0x1fff0000;
			break;
		case WIN0_YRGB_MST0:
			win0->area[0].y_addr = value >> 0;
			break;
		case WIN0_CBR_MST0:
			win0->area[0].uv_addr = value >> 0;
			break;
		case WIN1_DSP_INFO:
			win1->area[0].xsize = (((value >> 0) & 0x7ff) + 1);
			win1->area[0].ysize = (((value >> 16) & 0x7ff) + 1);
			break;
		case WIN1_DSP_ST:
			win1->area[0].dsp_stx = (value >> 0) & 0xfff;
			win1->area[0].dsp_sty = (value >> 16) & 0xfff;
			break;
		case WIN1_MST:
			win1->area[0].y_addr = value >> 0;
			break;
		default:
			DBG(2, "%s:uncare reg\n", __func__);
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
}

/********do basic init*********/
static int rk3188_lcdc_pre_init(struct rk_lcdc_driver *dev_drv)
{
	int v;
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
							   struct
							   lcdc_device,
							   driver);

	if (lcdc_dev->pre_init)
		return 0;

	if (lcdc_dev->id == 0) {
		//lcdc_dev->pd  = clk_get(NULL,"pd_lcdc0");
		lcdc_dev->hclk = clk_get(NULL, "g_h_lcdc0");
		lcdc_dev->aclk = clk_get(NULL, "aclk_lcdc0");
		lcdc_dev->dclk = clk_get(NULL, "dclk_lcdc0");
	} else if (lcdc_dev->id == 1) {
		//lcdc_dev->pd  = clk_get(NULL,"pd_lcdc1");
		lcdc_dev->hclk = clk_get(NULL, "g_h_lcdc1");
		lcdc_dev->aclk = clk_get(NULL, "aclk_lcdc1");
		lcdc_dev->dclk = clk_get(NULL, "dclk_lcdc1");
	} else {
		dev_err(lcdc_dev->dev, "invalid lcdc device!\n");
		return -EINVAL;
	}
	if (IS_ERR(lcdc_dev->pd) || (IS_ERR(lcdc_dev->aclk)) ||
	    (IS_ERR(lcdc_dev->dclk)) || (IS_ERR(lcdc_dev->hclk))) {
		dev_err(lcdc_dev->dev, "failed to get lcdc%d clk source\n",
			lcdc_dev->id);
	}

	/*uboot display has enabled lcdc in boot */
	if (!support_uboot_display()) {
		rk_disp_pwr_enable(dev_drv);
		rk3188_lcdc_clk_enable(lcdc_dev);
	} else {
		lcdc_dev->clk_on = 1;
	}

	rk3188_lcdc_read_reg_defalut_cfg(lcdc_dev);

	if (lcdc_dev->id == 0) {
		if (lcdc_dev->pwr18 == true) {
			v = 0x40004000;	/*bit14: 1,1.8v;0,3.3v*/
			writel_relaxed(v, RK_GRF_VIRT + RK3188_GRF_IO_CON4);
		} else {
			v = 0x40000000;
			writel_relaxed(v, RK_GRF_VIRT + RK3188_GRF_IO_CON4);
		}
	}

	if (lcdc_dev->id == 1) {
		if (lcdc_dev->pwr18 == true) {
			v = 0x80008000;	/*bit14: 1,1.8v;0,3.3v*/
			writel_relaxed(v, RK_GRF_VIRT + RK3188_GRF_IO_CON4);
		} else {
			v = 0x80000000;
			writel_relaxed(v, RK_GRF_VIRT + RK3188_GRF_IO_CON4);
		}
		pinctrl_select_state(lcdc_dev->dev->pins->p,
				     lcdc_dev->dev->pins->default_state);
	}

	lcdc_set_bit(lcdc_dev, SYS_CTRL, m_AUTO_GATING_EN);
	lcdc_cfg_done(lcdc_dev);
	lcdc_dev->pre_init = true;

	return 0;
}

static void rk3188_lcdc_deint(struct lcdc_device *lcdc_dev)
{
	u32 mask, val;
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_dev->clk_on = 0;
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_FS_INT_CLEAR,
			     v_FS_INT_CLEAR(1));
		mask = m_HS_INT_EN | m_FS_INT_EN | m_LF_INT_EN |
			m_BUS_ERR_INT_EN;
		val = v_HS_INT_EN(0) | v_FS_INT_EN(0) |
			v_LF_INT_EN(0) | v_BUS_ERR_INT_EN(0);
		lcdc_msk_reg(lcdc_dev, INT_STATUS, mask, val);
		lcdc_set_bit(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY);
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
	}
	mdelay(1);

}

static int rk3188_lcdc_alpha_cfg(struct lcdc_device *lcdc_dev)
{
	int win0_top = 0;
	u32 mask, val;
	enum data_format win0_format = lcdc_dev->driver.win[0]->area[0].format;
	enum data_format win1_format = lcdc_dev->driver.win[1]->area[0].format;

	int win0_alpha_en = ((win0_format == ARGB888)
			     || (win0_format == ABGR888)) ? 1 : 0;
	int win1_alpha_en = ((win1_format == ARGB888)
			     || (win1_format == ABGR888)) ? 1 : 0;
	u32 *_pv = (u32 *) lcdc_dev->regsbak;
	_pv += (DSP_CTRL0 >> 2);
	win0_top = ((*_pv) & (m_WIN0_TOP)) >> 8;
	if (win0_top && (lcdc_dev->atv_layer_cnt >= 2) && (win0_alpha_en)) {
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, m_WIN0_ALPHA_EN |
			     m_WIN1_ALPHA_EN, v_WIN0_ALPHA_EN(1) |
			     v_WIN1_ALPHA_EN(0));
		mask = m_WIN0_ALPHA_MODE | m_ALPHA_MODE_SEL0 | m_ALPHA_MODE_SEL1;
		val = v_WIN0_ALPHA_MODE(1) | v_ALPHA_MODE_SEL0(1) | v_ALPHA_MODE_SEL1(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
	} else if ((!win0_top) && (lcdc_dev->atv_layer_cnt >= 2)
		   && (win1_alpha_en)) {
		mask =  m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN;
		val = v_WIN0_ALPHA_EN(0) | v_WIN1_ALPHA_EN(1);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);

		mask = m_WIN1_ALPHA_MODE | m_ALPHA_MODE_SEL0 | m_ALPHA_MODE_SEL1;
		val = v_WIN1_ALPHA_MODE(1) | v_ALPHA_MODE_SEL0(1) | v_ALPHA_MODE_SEL1(0);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
	} else {
		mask = m_WIN0_ALPHA_EN | m_WIN1_ALPHA_EN;
		val = v_WIN0_ALPHA_EN(0) | v_WIN1_ALPHA_EN(0);
		lcdc_msk_reg(lcdc_dev, ALPHA_CTRL, mask, val);
	}

	return 0;
}

static int rk3188_lcdc_reg_update(struct rk_lcdc_driver *dev_drv)
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
		lcdc_msk_reg(lcdc_dev, SYS_CTRL,
			     m_WIN0_EN | m_WIN1_EN | m_WIN0_RB_SWAP |
			     m_WIN1_RB_SWAP,
			     v_WIN0_EN(win0->state) | v_WIN1_EN(win1->state) |
			     v_WIN0_RB_SWAP(win0->area[0].swap_rb) |
			     v_WIN1_RB_SWAP(win1->area[0].swap_rb));
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_YRGB,
			    v_X_SCL_FACTOR(win0->scale_yrgb_x) |
			    v_Y_SCL_FACTOR(win0->scale_yrgb_y));
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_CBR,
			    v_X_SCL_FACTOR(win0->scale_cbcr_x) |
			    v_Y_SCL_FACTOR(win0->scale_cbcr_y));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_FORMAT,
			     v_WIN0_FORMAT(win0->area[0].fmt_cfg));
		lcdc_writel(lcdc_dev, WIN0_ACT_INFO, v_ACT_WIDTH(win0->area[0].xact) |
			    v_ACT_HEIGHT(win0->area[0].yact));
		lcdc_writel(lcdc_dev, WIN0_DSP_ST, v_DSP_STX(win0->area[0].dsp_stx) |
			    v_DSP_STY(win0->area[0].dsp_sty));
		lcdc_writel(lcdc_dev, WIN0_DSP_INFO, v_DSP_WIDTH(win0->area[0].xsize) |
			    v_DSP_HEIGHT(win0->area[0].ysize));
		lcdc_msk_reg(lcdc_dev, WIN_VIR, m_WIN0_VIR,
			     v_WIN0_VIR_VAL(win0->area[0].y_vir_stride));
		lcdc_writel(lcdc_dev, WIN0_YRGB_MST0, win0->area[0].y_addr);
		lcdc_writel(lcdc_dev, WIN0_CBR_MST0, win0->area[0].uv_addr);
		lcdc_writel(lcdc_dev, WIN1_DSP_INFO, v_DSP_WIDTH(win1->area[0].xsize) |
			    v_DSP_HEIGHT(win1->area[0].ysize));
		lcdc_writel(lcdc_dev, WIN1_DSP_ST, v_DSP_STX(win1->area[0].dsp_stx) |
			    v_DSP_STY(win1->area[0].dsp_sty));
		lcdc_msk_reg(lcdc_dev, WIN_VIR, m_WIN1_VIR,
			     ((win1->area[0].y_vir_stride)&0x1fff)<<16);
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN1_FORMAT,
			     v_WIN1_FORMAT(win1->area[0].fmt_cfg));
		lcdc_writel(lcdc_dev, WIN1_MST, win1->area[0].y_addr);
		rk3188_lcdc_alpha_cfg(lcdc_dev);
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
			dev_warn(lcdc_dev->dev, "wait for new frame start time out!\n");
			return -ETIMEDOUT;
		}
	}
	DBG(2, "%s for lcdc%d\n", __func__, lcdc_dev->id);
	return 0;

}

static int rk3188_lcdc_reg_restore(struct lcdc_device *lcdc_dev)
{
	memcpy((u8 *) lcdc_dev->regs, (u8 *) lcdc_dev->regsbak, 0x84);
	return 0;
}


static int rk3188_load_screen(struct rk_lcdc_driver *dev_drv, bool initscreen)
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
		switch (screen->face) {
		case OUT_P565:
			face = OUT_P565;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
			    v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
			break;
		case OUT_P666:
			face = OUT_P666;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
			    v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
			break;
		case OUT_D888_P565:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(0) |
			    v_DITHER_DOWN_SEL(1);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
			break;
		case OUT_D888_P666:
			face = OUT_P888;
			mask = m_DITHER_DOWN_EN | m_DITHER_DOWN_MODE |
			    m_DITHER_DOWN_SEL;
			val = v_DITHER_DOWN_EN(1) | v_DITHER_DOWN_MODE(1) |
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

		mask = m_DSP_OUT_FORMAT | m_HSYNC_POL | m_VSYNC_POL |
		    m_DEN_POL | m_DCLK_POL;
		val = v_DSP_OUT_FORMAT(face) | v_HSYNC_POL(screen->pin_hsync) |
		    v_VSYNC_POL(screen->pin_vsync) | v_DEN_POL(screen->pin_den) |
		    v_DCLK_POL(screen->pin_dclk);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);

		mask = m_BG_COLOR | m_DSP_BG_SWAP | m_DSP_RB_SWAP |
		    m_DSP_RG_SWAP | m_DSP_DELTA_SWAP |
		    m_DSP_DUMMY_SWAP | m_BLANK_EN;
		val = v_BG_COLOR(0x000000) | v_DSP_BG_SWAP(screen->swap_gb) |
		    v_DSP_RB_SWAP(screen->swap_rb) | v_DSP_RG_SWAP(screen->
								   swap_rg) |
		    v_DSP_DELTA_SWAP(screen->
				     swap_delta) | v_DSP_DUMMY_SWAP(screen->
								    swap_dumy) |
		    v_BLANK_EN(0) | v_BLACK_EN(0);
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

		val =
		    v_VSYNC(screen->mode.vsync_len) | v_VERPRD(screen->mode.
							       vsync_len +
							       upper_margin +
							       y_res +
							       lower_margin);
		lcdc_writel(lcdc_dev, DSP_VTOTAL_VS_END, val);

		val = v_VAEP(screen->mode.vsync_len + upper_margin + y_res) |
		    v_VASP(screen->mode.vsync_len + screen->mode.upper_margin);
		lcdc_writel(lcdc_dev, DSP_VACT_ST_END, val);
	}
	spin_unlock(&lcdc_dev->reg_lock);

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
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_EN, v_WIN0_EN(open));
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
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN1_EN, v_WIN1_EN(open));
		/*if no layer used,disable lcdc*/
		if (!lcdc_dev->atv_layer_cnt) {
			dev_info(lcdc_dev->dev, "no layer is used,go to standby!\n");
			lcdc_dev->standby = 1;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int rk3188_lcdc_open(struct rk_lcdc_driver *dev_drv, int win_id,
			    bool open)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);

	/*enable clk,when first layer open */
	if ((open) && (!lcdc_dev->atv_layer_cnt)) {
		rk3188_lcdc_pre_init(dev_drv);
		rk3188_lcdc_clk_enable(lcdc_dev);
		rk3188_lcdc_reg_restore(lcdc_dev);
		rk3188_load_screen(dev_drv, 1);
		spin_lock(&lcdc_dev->reg_lock);
		if (dev_drv->cur_screen->dsp_lut)
			rk3188_lcdc_set_lut(dev_drv);
		spin_unlock(&lcdc_dev->reg_lock);
	}

	if (win_id == 0)
		win0_open(lcdc_dev, open);
	else if (win_id == 1)
		win1_open(lcdc_dev, open);
	else
		dev_err(lcdc_dev->dev, "invalid win id:%d\n", win_id);

	/*when all layer closed,disable clk */
	if ((!open) && (!lcdc_dev->atv_layer_cnt)) {
		lcdc_msk_reg(lcdc_dev, INT_STATUS,
			     m_FS_INT_CLEAR, v_FS_INT_CLEAR(1));
		rk3188_lcdc_reg_update(dev_drv);
		rk3188_lcdc_clk_disable(lcdc_dev);
	}

	return 0;
}

static int win0_display(struct lcdc_device *lcdc_dev,
			struct rk_lcdc_win *win)
{
	u32 y_addr;
	u32 uv_addr;
	y_addr = win->area[0].smem_start+win->area[0].y_offset;
	uv_addr = win->area[0].cbr_start + win->area[0].c_offset;
	DBG(2, "lcdc%d>>%s:y_addr:0x%x>>uv_addr:0x%x\n",
	    	lcdc_dev->id, __func__, y_addr, uv_addr);
	
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_writel(lcdc_dev, WIN0_YRGB_MST0, y_addr);
		lcdc_writel(lcdc_dev, WIN0_CBR_MST0, uv_addr);
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
	if (likely(lcdc_dev->clk_on))
		lcdc_writel(lcdc_dev,WIN1_MST,y_addr);
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}


static int rk3188_lcdc_pan_display(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv, 
						struct lcdc_device, driver);
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;
	u32 msk, val;
#if defined(WAIT_FOR_SYNC)
	int timeout;
	unsigned long flags;
#endif

	if (!screen) {
		dev_err(dev_drv->dev,"screen is null!\n");
		return -ENOENT;
	}
	
	if (win_id == 0) {
		win = dev_drv->win[0];
		win0_display(lcdc_dev, win);
	} else if(win_id==1) {
		win = dev_drv->win[1];
		win1_display(lcdc_dev, win);
	} else {
		dev_err(dev_drv->dev,"invalid win number:%d!\n", win_id);
		return -EINVAL;
	}

	 /*this is the first frame of the system ,enable frame start interrupt*/
	if ((dev_drv->first_frame))  {
		dev_drv->first_frame = 0;
		msk = m_FS_INT_CLEAR |m_FS_INT_EN;
		val = v_FS_INT_CLEAR(1) | v_FS_INT_EN(1);
		lcdc_msk_reg(lcdc_dev, INT_STATUS, msk, val);

	}

#if defined(WAIT_FOR_SYNC)
	spin_lock_irqsave(&dev_drv->cpl_lock, flags);
	init_completion(&dev_drv->frame_done);
	spin_unlock_irqrestore(&dev_drv->cpl_lock, flags);
	timeout = wait_for_completion_timeout(&dev_drv->frame_done,
				msecs_to_jiffies(dev_drv->cur_screen->ft +5));
	if (!timeout && (!dev_drv->frame_done.done)) {
		dev_info(dev_drv->dev, "wait for new frame start time out!\n");
		return -ETIMEDOUT;
	}
#endif

	return 0;
}


static int win0_set_par(struct lcdc_device *lcdc_dev,
			struct rk_screen *screen, struct rk_lcdc_win *win)
{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u32 ScaleYrgbX = 0x1000;
	u32 ScaleYrgbY = 0x1000;
	u32 ScaleCbrX = 0x1000;
	u32 ScaleCbrY = 0x1000;
	u8 fmt_cfg = 0;
	char fmt[9] = "NULL";
	xact = win->area[0].xact;
	yact = win->area[0].yact;
	xvir = win->area[0].xvir;
	yvir = win->area[0].yvir;
	xpos = win->area[0].xpos + screen->mode.left_margin + screen->mode.hsync_len;
	ypos = win->area[0].ypos + screen->mode.upper_margin + screen->mode.vsync_len;

	ScaleYrgbX = CalScale(xact, win->area[0].xsize);
	ScaleYrgbY = CalScale(yact, win->area[0].ysize);
	switch (win->area[0].format) {
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
	case YUV422:
		fmt_cfg = 5;
		ScaleCbrX = CalScale((xact / 2), win->area[0].xsize);
		ScaleCbrY = CalScale(yact, win->area[0].ysize);
		break;
	case YUV420:
		fmt_cfg = 4;
		ScaleCbrX = CalScale(xact / 2, win->area[0].xsize);
		ScaleCbrY = CalScale(yact / 2, win->area[0].ysize);
		break;
	case YUV444:
		fmt_cfg = 6;
		ScaleCbrX = CalScale(xact, win->area[0].xsize);
		ScaleCbrY = CalScale(yact, win->area[0].ysize);
		break;
	default:
		dev_err(lcdc_dev->driver.dev, "%s:un supported format!\n",
			__func__);
		break;
	}

	DBG(1, "lcdc%d>>%s\n>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d\n"
		">>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n", lcdc_dev->id,
		__func__, get_format_string(win->area[0].format, fmt), xact,
		yact, win->area[0].xsize, win->area[0].ysize, xvir, yvir, xpos, ypos);

	spin_lock(&lcdc_dev->reg_lock);
	
	win->scale_yrgb_x = ScaleYrgbX;
	win->scale_yrgb_y = ScaleYrgbY;
	win->scale_cbcr_x = ScaleCbrX;
	win->scale_cbcr_y = ScaleCbrY;
	win->area[0].fmt_cfg = fmt_cfg;
	win->area[0].dsp_stx = xpos;
	win->area[0].dsp_sty = ypos;
	
	switch (win->area[0].format) {
	case XBGR888:
	case ABGR888:
		win->area[0].swap_rb = 1;
		break;
	case ARGB888:
		win->area[0].swap_rb = 0;
		break;
	case RGB888:
		win->area[0].swap_rb = 0;
		break;
	case RGB565:
		win->area[0].swap_rb = 0;
		break;
	case YUV422:
	case YUV420:
	case YUV444:
		win->area[0].swap_rb = 0;
		break;
	default:
		dev_err(lcdc_dev->driver.dev,
			"%s:un supported format!\n", __func__);
		break;
	}

	
	if (likely(lcdc_dev->clk_on)) {
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_YRGB,v_X_SCL_FACTOR(ScaleYrgbX) | v_Y_SCL_FACTOR(ScaleYrgbY));
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_CBR,v_X_SCL_FACTOR(ScaleCbrX) | v_Y_SCL_FACTOR(ScaleCbrY));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_WIN0_FORMAT,v_WIN0_FORMAT(fmt_cfg));         
		lcdc_writel(lcdc_dev, WIN0_ACT_INFO,v_ACT_WIDTH(xact) | v_ACT_HEIGHT(yact));
		lcdc_writel(lcdc_dev, WIN0_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
		lcdc_writel(lcdc_dev, WIN0_DSP_INFO,v_DSP_WIDTH(win->area[0].xsize) |
						v_DSP_HEIGHT(win->area[0].ysize));
		lcdc_msk_reg(lcdc_dev, WIN_VIR, m_WIN0_VIR, v_WIN0_VIR_VAL(win->area[0].y_vir_stride));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN0_EN | m_WIN0_RB_SWAP,
			     v_WIN0_EN(win->state) |
			     v_WIN0_RB_SWAP(win->area[0].swap_rb));
		lcdc_msk_reg(lcdc_dev, WIN0_COLOR_KEY, m_COLOR_KEY_EN, v_COLOR_KEY_EN(0));
	}
	spin_unlock(&lcdc_dev->reg_lock);
	
	return 0;

}

static int win1_set_par(struct lcdc_device *lcdc_dev,
			struct rk_screen *screen, struct rk_lcdc_win *win)
{
	u32 xact, yact, xvir, yvir, xpos, ypos;
	u8 fmt_cfg;
	char fmt[9] = "NULL";
	xact = win->area[0].xact;
	yact = win->area[0].yact;
	xvir = win->area[0].xvir;
	yvir = win->area[0].yvir;
	xpos = win->area[0].xpos + screen->mode.left_margin + screen->mode.hsync_len;
	ypos = win->area[0].ypos + screen->mode.upper_margin + screen->mode.vsync_len;

	DBG(1, "lcdc%d>>%s>>format:%s>>>xact:%d>>yact:%d>>xsize:%d>>ysize:%d\n"
		">>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n", lcdc_dev->id,
		__func__, get_format_string(win->area[0].format, fmt),
		xact, yact, win->area[0].xsize, win->area[0].ysize,
		xvir, yvir, xpos, ypos);

	spin_lock(&lcdc_dev->reg_lock);
	win->area[0].dsp_stx = xpos;
	win->area[0].dsp_sty = ypos;
	switch (win->area[0].format) {
	case XBGR888:
	case ABGR888:
		fmt_cfg = 0;
		win->area[0].swap_rb = 1;
		break;
	case ARGB888:
		fmt_cfg = 0;
		win->area[0].swap_rb = 0;

		break;
	case RGB888:
		fmt_cfg = 1;
		win->area[0].swap_rb = 0;
		break;
	case RGB565:
		fmt_cfg = 2;
		win->area[0].swap_rb = 0;
		break;
	default:
		dev_err(lcdc_dev->driver.dev,
			"%s:un supported format!\n", __func__);
		break;
	}
	win->area[0].fmt_cfg = fmt_cfg;
	if (likely(lcdc_dev->clk_on)) {
		lcdc_writel(lcdc_dev, WIN1_DSP_INFO,v_DSP_WIDTH(win->area[0].xsize) |
							v_DSP_HEIGHT(win->area[0].ysize));
		lcdc_writel(lcdc_dev, WIN1_DSP_ST,v_DSP_STX(xpos) | v_DSP_STY(ypos));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_WIN1_EN | m_WIN1_RB_SWAP,
			     v_WIN1_EN(win->state) |
			     v_WIN1_RB_SWAP(win->area[0].swap_rb));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL,m_WIN1_FORMAT, v_WIN1_FORMAT(fmt_cfg));
		lcdc_msk_reg(lcdc_dev, WIN_VIR, m_WIN1_VIR,
			     ((win->area[0].y_vir_stride)&0x1fff)<<16);
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int rk3188_lcdc_set_par(struct rk_lcdc_driver *dev_drv,int win_id)
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
		win0_set_par(lcdc_dev, screen, win);
	} else if (win_id == 1) {
		win = dev_drv->win[1];
		win1_set_par(lcdc_dev, screen, win);
	} else {
		dev_err(dev_drv->dev, "un supported win number:%d\n", win_id);
		return -EINVAL;
	}
	
	if (lcdc_dev->clk_on) {
		rk3188_lcdc_alpha_cfg(lcdc_dev);
	}

	return 0;
}



static int rk3188_lcdc_ioctl(struct rk_lcdc_driver *dev_drv, unsigned int cmd,
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

static int rk3188_lcdc_early_suspend(struct rk_lcdc_driver *dev_drv)
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
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}
	rk3188_lcdc_clk_disable(lcdc_dev);
	rk_disp_pwr_disable(dev_drv);
	return 0;
}

static int rk3188_lcdc_early_resume(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int i = 0;
	int __iomem *c;
	int v;

	if (!dev_drv->suspend_flag)
		return 0;
	rk_disp_pwr_enable(dev_drv);
	dev_drv->suspend_flag = 0;

	if (lcdc_dev->atv_layer_cnt) {
		rk3188_lcdc_clk_enable(lcdc_dev);
		rk3188_lcdc_reg_restore(lcdc_dev);

		spin_lock(&lcdc_dev->reg_lock);
		if (dev_drv->cur_screen->dsp_lut) {
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN,
				     v_DSP_LUT_EN(0));
			lcdc_cfg_done(lcdc_dev);
			mdelay(25);
			for (i = 0; i < 256; i++) {
				v = dev_drv->cur_screen->dsp_lut[i];
				c = lcdc_dev->dsp_lut_addr_base + i;
				writel_relaxed(v, c);
			}
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN,
				     v_DSP_LUT_EN(1));
		}

		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_OUT_ZERO,
			     v_DSP_OUT_ZERO(0));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_LCDC_STANDBY,
			     v_LCDC_STANDBY(0));
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_BLANK_EN,
			     v_BLANK_EN(0));
		lcdc_cfg_done(lcdc_dev);

		spin_unlock(&lcdc_dev->reg_lock);
	}
	
	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();
	return 0;
}


static int rk3188_lcdc_blank(struct rk_lcdc_driver *dev_drv,
			     int win_id, int blank_mode)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		rk3188_lcdc_early_resume(dev_drv);
		break;
	case FB_BLANK_NORMAL:
		rk3188_lcdc_early_suspend(dev_drv);
		break;
	default:
		rk3188_lcdc_early_suspend(dev_drv);
		break;
	}
	
	dev_info(dev_drv->dev, "blank mode:%d\n", blank_mode);

	return 0;
}

static int rk3188_lcdc_get_win_state(struct rk_lcdc_driver *dev_drv, int win_id)
{
	return 0;
}

static int rk3188_lcdc_ovl_mgr(struct rk_lcdc_driver *dev_drv, int swap,
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

static ssize_t rk3188_lcdc_get_disp_info(struct rk_lcdc_driver *dev_drv,
					 char *buf, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
							   struct
							   lcdc_device,
							   driver);

	char format_w0[9] = "NULL";
	char format_w1[9] = "NULL";
	char status_w0[9] = "NULL";
	char status_w1[9] = "NULL";
	u32 fmt_id, act_info, dsp_info, dsp_st, factor;
	u16 xvir_w0, x_act_w0, y_act_w0, x_dsp_w0, y_dsp_w0;
	u16 x_st_w0, y_st_w0, x_factor, y_factor;
	u16 xvir_w1, x_dsp_w1, y_dsp_w1, x_st_w1, y_st_w1;
	u16 x_scale, y_scale, ovl;
	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		fmt_id = lcdc_readl(lcdc_dev, SYS_CTRL);
		ovl = lcdc_read_bit(lcdc_dev, DSP_CTRL0, m_WIN0_TOP);
		switch ((fmt_id & m_WIN0_FORMAT) >> 3) {
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

		switch ((fmt_id & m_WIN1_FORMAT) >> 6) {
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
			strcpy(format_w1, "8bpp");
			break;
		case 5:
			strcpy(format_w1, "4bpp");
			break;
		case 6:
			strcpy(format_w1, "2bpp");
			break;
		case 7:
			strcpy(format_w1, "1bpp");
			break;
		default:
			strcpy(format_w1, "invalid\n");
			break;
		}

		if (fmt_id & m_WIN0_EN)
			strcpy(status_w0, "enabled");
		else
			strcpy(status_w0, "disabled");

		if ((fmt_id & m_WIN1_EN) >> 1)
			strcpy(status_w1, "enabled");
		else
			strcpy(status_w1, "disabled");

		xvir_w0 = lcdc_readl(lcdc_dev, WIN_VIR) & 0x1fff;
		act_info = lcdc_readl(lcdc_dev, WIN0_ACT_INFO);
		dsp_info = lcdc_readl(lcdc_dev, WIN0_DSP_INFO);
		dsp_st = lcdc_readl(lcdc_dev, WIN0_DSP_ST);
		factor = lcdc_readl(lcdc_dev, WIN0_SCL_FACTOR_YRGB);
		x_act_w0 = (act_info & 0x1fff) + 1;
		y_act_w0 = ((act_info >> 16) & 0x1fff) + 1;
		x_dsp_w0 = (dsp_info & 0x7ff) + 1;
		y_dsp_w0 = ((dsp_info >> 16) & 0x7ff) + 1;
		x_st_w0 = dsp_st & 0xffff;
		y_st_w0 = dsp_st >> 16;
		x_factor = factor & 0xffff;
		y_factor = factor >> 16;
		x_scale = 4096 * 100 / x_factor;
		y_scale = 4096 * 100 / y_factor;
		xvir_w1 = (lcdc_readl(lcdc_dev, WIN_VIR) >> 16) & 0x1fff;
		dsp_info = lcdc_readl(lcdc_dev, WIN1_DSP_INFO);
		dsp_st = lcdc_readl(lcdc_dev, WIN1_DSP_ST);
		x_dsp_w1 = (dsp_info & 0x7ff) + 1;
		y_dsp_w1 = ((dsp_info >> 16) & 0x7ff) + 1;
		x_st_w1 = dsp_st & 0xffff;
		y_st_w1 = dsp_st >> 16;
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
			lcdc_readl(lcdc_dev, WIN0_YRGB_MST0),
			lcdc_readl(lcdc_dev, WIN0_CBR_MST0),
			status_w1,
			xvir_w1,
			x_dsp_w1,
			y_dsp_w1,
			x_st_w1,
			y_st_w1,
			format_w1,
			lcdc_readl(lcdc_dev, WIN1_MST),
			ovl ? "win0 on the top of win1\n" :
			"win1 on the top of win0\n");
}

static int rk3188_lcdc_fps_mgr(struct rk_lcdc_driver *dev_drv, int fps,
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

static int rk3188_fb_win_remap(struct rk_lcdc_driver *dev_drv, u16 order)
{
	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (order == FB_DEFAULT_ORDER)
		order = FB0_WIN0_FB1_WIN1_FB2_WIN2;
	dev_drv->fb2_win_id = order / 100;
	dev_drv->fb1_win_id = (order / 10) % 10;
	dev_drv->fb0_win_id = order % 10;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return 0;
}

static int rk3188_lcdc_get_win_id(struct rk_lcdc_driver *dev_drv,
				  const char *id)
{
	int win_id = 0;
	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (!strcmp(id, "fb0") || !strcmp(id, "fb2"))
		win_id = dev_drv->fb0_win_id;
	else if (!strcmp(id, "fb1") || !strcmp(id, "fb3"))
		win_id = dev_drv->fb1_win_id;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return win_id;
}

static int rk3188_set_dsp_lut(struct rk_lcdc_driver *dev_drv, int *lut)
{
	int i = 0;
	int __iomem *c;
	int v;
	int ret = 0;

	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN, v_DSP_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	msleep(25);
	if (dev_drv->cur_screen->dsp_lut) {
		for (i = 0; i < 256; i++) {
			v = dev_drv->cur_screen->dsp_lut[i] = lut[i];
			c = lcdc_dev->dsp_lut_addr_base + i;
			writel_relaxed(v, c);

		}
	} else {
		dev_err(dev_drv->dev, "no buffer to backup lut data!\n");
		ret = -1;
	}
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DSP_LUT_EN, v_DSP_LUT_EN(1));
	lcdc_cfg_done(lcdc_dev);

	return ret;
}

static int rk3188_lcdc_dpi_open(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DIRECT_PATCH_EN,
		     v_DIRECT_PATCH_EN(open));
	lcdc_cfg_done(lcdc_dev);
	return 0;
}

static int rk3188_lcdc_dpi_win_sel(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
					struct lcdc_device, driver);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_DIRECT_PATH_LAY_SEL,
		     v_DIRECT_PATH_LAY_SEL(win_id));
	lcdc_cfg_done(lcdc_dev);
	return 0;

}

static int rk3188_lcdc_dpi_status(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int ovl = lcdc_read_bit(lcdc_dev, SYS_CTRL, m_DIRECT_PATCH_EN);
	return ovl;
}

int rk3188_lcdc_poll_vblank(struct rk_lcdc_driver *dev_drv)
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
		} else
			ret = RK_LF_STATUS_FR;
	} else {
		ret = RK_LF_STATUS_NC;
	}

	return ret;
}


static int rk3188_lcdc_get_dsp_addr(struct rk_lcdc_driver *dev_drv,unsigned int *dsp_addr)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if(lcdc_dev->clk_on){
		dsp_addr[0] = lcdc_readl(lcdc_dev, WIN0_YRGB_MST0);
		dsp_addr[1] = lcdc_readl(lcdc_dev, WIN1_MST);
	}
	return 0;
}

static int rk3188_lcdc_cfg_done(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv, 
					struct lcdc_device, driver);
	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on)
		lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}


static struct rk_lcdc_win lcdc_win[] = {
	[0] = {
	       .name = "win0",
	       .id = 0,
	       .support_3d = true,
	       },
	[1] = {
	       .name = "win1",
	       .id = 1,
	       .support_3d = false,
	       },
};

static struct rk_lcdc_drv_ops lcdc_drv_ops = {
	.open			= rk3188_lcdc_open,
	.load_screen 		= rk3188_load_screen,
	.set_par 		= rk3188_lcdc_set_par,
	.pan_display 		= rk3188_lcdc_pan_display,
	.blank 			= rk3188_lcdc_blank,
	.ioctl 			= rk3188_lcdc_ioctl,
	.get_win_state 		= rk3188_lcdc_get_win_state,
	.ovl_mgr 		= rk3188_lcdc_ovl_mgr,
	.get_disp_info 		= rk3188_lcdc_get_disp_info,
	.fps_mgr 		= rk3188_lcdc_fps_mgr,
	.fb_get_win_id 		= rk3188_lcdc_get_win_id,
	.fb_win_remap 		= rk3188_fb_win_remap,
	.set_dsp_lut 		= rk3188_set_dsp_lut,
	.poll_vblank 		= rk3188_lcdc_poll_vblank,
	.dpi_open 		= rk3188_lcdc_dpi_open,
	.dpi_win_sel 		= rk3188_lcdc_dpi_win_sel,
	.dpi_status 		= rk3188_lcdc_dpi_status,
	.get_dsp_addr 		= rk3188_lcdc_get_dsp_addr,
	.cfg_done		= rk3188_lcdc_cfg_done,
	.dump_reg 		= rk3188_lcdc_reg_dump,
};

static irqreturn_t rk3188_lcdc_isr(int irq, void *dev_id)
{
	struct lcdc_device *lcdc_dev =
	    (struct lcdc_device *)dev_id;
	ktime_t timestamp = ktime_get();
	u32 int_reg = lcdc_readl(lcdc_dev, INT_STATUS);

	if (int_reg & m_FS_INT_STA) {
		timestamp = ktime_get();
		lcdc_msk_reg(lcdc_dev, INT_STATUS, m_FS_INT_CLEAR,
			     v_FS_INT_CLEAR(1));
		//if (lcdc_dev->driver.wait_fs) {
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

#if defined(CONFIG_PM)
static int rk3188_lcdc_suspend(struct platform_device *pdev, pm_message_t state)
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

static int rk3188_lcdc_parse_dt(struct lcdc_device *lcdc_dev)
{
	struct device_node *np = lcdc_dev->dev->of_node;
	int val;
	if (of_property_read_u32(np, "rockchip,prop", &val))
		lcdc_dev->prop = PRMRY;	/*default set it as primary */
	else
		lcdc_dev->prop = val;

	if (of_property_read_u32(np, "rockchip,pwr18", &val))
		lcdc_dev->pwr18 = false;	/*default set it as 3.xv power supply */
	else
		lcdc_dev->pwr18 = (val ? true : false);

	if (of_property_read_u32(np, "rockchip,fb-win-map", &val))
		lcdc_dev->driver.fb_win_map = FB_DEFAULT_ORDER;
	else
		lcdc_dev->driver.fb_win_map = val;

	return 0;
}

static int rk3188_lcdc_probe(struct platform_device *pdev)
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
		dev_err(&pdev->dev, "rk3188 lcdc device kmalloc fail!");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->dev = dev;
	rk3188_lcdc_parse_dt(lcdc_dev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lcdc_dev->reg_phy_base = res->start;
	lcdc_dev->len = resource_size(res);
	lcdc_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(lcdc_dev->regs))
		return PTR_ERR(lcdc_dev->regs);

	lcdc_dev->regsbak = devm_kzalloc(dev, lcdc_dev->len, GFP_KERNEL);
	if (IS_ERR(lcdc_dev->regsbak))
		return PTR_ERR(lcdc_dev->regsbak);
	lcdc_dev->dsp_lut_addr_base = (lcdc_dev->regs + DSP_LUT_ADDR);
	lcdc_dev->id = rk3188_lcdc_get_id(lcdc_dev->reg_phy_base);
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

	ret = devm_request_irq(dev, lcdc_dev->irq, rk3188_lcdc_isr,
			       IRQF_DISABLED, dev_name(dev), lcdc_dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot requeset irq %d - err %d\n",
			lcdc_dev->irq, ret);
		return ret;
	}

	ret = rk_fb_register(dev_drv, lcdc_win, lcdc_dev->id);
	if (ret < 0) {
		dev_err(dev, "register fb for lcdc%d failed!\n", lcdc_dev->id);
		return ret;
	}
	lcdc_dev->screen = dev_drv->screen0;
	
	dev_info(dev, "lcdc%d probe ok\n", lcdc_dev->id);

	return 0;
}

static int rk3188_lcdc_remove(struct platform_device *pdev)
{

	return 0;
}

static void rk3188_lcdc_shutdown(struct platform_device *pdev)
{
	struct lcdc_device *lcdc_dev = platform_get_drvdata(pdev);

	rk3188_lcdc_deint(lcdc_dev);
	rk_disp_pwr_disable(&lcdc_dev->driver);
}

#if defined(CONFIG_OF)
static const struct of_device_id rk3188_lcdc_dt_ids[] = {
	{.compatible = "rockchip,rk3188-lcdc",},
	{}
};
#endif

static struct platform_driver rk3188_lcdc_driver = {
	.probe = rk3188_lcdc_probe,
	.remove = rk3188_lcdc_remove,
	.driver = {
		   .name = "rk3188-lcdc",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk3188_lcdc_dt_ids),
		   },
	.suspend = rk3188_lcdc_suspend,
	.resume = rk3188_lcdc_resume,
	.shutdown = rk3188_lcdc_shutdown,
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
