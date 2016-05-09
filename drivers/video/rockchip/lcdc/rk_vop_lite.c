/*
 * rockchip VOP(Video Output Processer) hardware driver.
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd.
 * Author: WenLong Zhuang <daisen.zhuang@rock-chips.com>
 *
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
#include <linux/pm_runtime.h>
#include <linux/rockchip-iovmm.h>
#include <asm/div64.h>
#include <linux/uaccess.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/common.h>
#include <dt-bindings/clock/rk_system_status.h>

#include "rk_vop_lite.h"

static int dbg_thresd;
module_param(dbg_thresd, int, S_IRUGO | S_IWUSR);

#define DBG(level, x...) do {			\
	if (unlikely(dbg_thresd >= level))	\
		pr_info(x);\
	} while (0)

#define to_vop_dev(drv) container_of(drv, struct vop_device, driver)

static struct rk_lcdc_win vop_win[] = {
	{ .name = "win0", .id = 0},
	{ .name = "win1", .id = 1},
	{ .name = "hwc",  .id = 2}
};

static int vop_set_bcsh(struct rk_lcdc_driver *dev_drv, bool enable);

static int vop_clk_enable(struct vop_device *vop_dev)
{
	if (!vop_dev->clk_on) {
		pm_runtime_get_sync(vop_dev->dev);

		clk_enable(vop_dev->hclk);
		clk_enable(vop_dev->aclk);
		clk_enable(vop_dev->dclk);
		spin_lock(&vop_dev->reg_lock);
		vop_dev->clk_on = 1;
		spin_unlock(&vop_dev->reg_lock);
	}

	return 0;
}

static int vop_clk_disable(struct vop_device *vop_dev)
{
	if (vop_dev->clk_on) {
		spin_lock(&vop_dev->reg_lock);
		vop_dev->clk_on = 0;
		spin_unlock(&vop_dev->reg_lock);
		clk_disable(vop_dev->dclk);
		clk_disable(vop_dev->aclk);
		clk_disable(vop_dev->hclk);

		pm_runtime_put(vop_dev->dev);
	}

	return 0;
}

static int vop_irq_enable(struct vop_device *vop_dev)
{
	u64 val;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_mask_writel(vop_dev, INTR_CLEAR, INTR_MASK, INTR_MASK);

		val = INTR_FS0 | INTR_FS1 | INTR_LINE_FLAG0 | INTR_LINE_FLAG1 |
			INTR_BUS_ERROR | INTR_WIN0_EMPTY | INTR_WIN1_EMPTY |
			INTR_DSP_HOLD_VALID;
		vop_mask_writel(vop_dev, INTR_EN, INTR_MASK, val);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_irq_disable(struct vop_device *vop_dev)
{
	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_writel(vop_dev, INTR_EN, 0xffff0000);
		vop_writel(vop_dev, INTR_CLEAR, 0xffffffff);
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_standby_enable(struct vop_device *vop_dev)
{
	u64 val;
	int ret;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_dev->sync.stdbyfin.done = 0;

		vop_msk_reg(vop_dev, DSP_CTRL2, V_DSP_BLANK_EN(1));
		/*vop_mask_writel(vop_dev, INTR_CLEAR0, INTR_MASK, INTR_MASK);*/
		val = V_IMD_VOP_STANDBY_EN(1) | V_IMD_VOP_DMA_STOP(1) |
			V_IMD_DSP_OUT_ZERO(1);
		vop_msk_reg(vop_dev, SYS_CTRL2, val);
		vop_cfg_done(vop_dev);
		spin_unlock(&vop_dev->reg_lock);

		/* wait for standby hold valid */
		ret = vop_completion_timeout_ms(&vop_dev->sync.stdbyfin,
						vop_dev->sync.stdbyfin_to);
		if (!ret) {
			dev_err(vop_dev->dev,
				"wait standby hold valid timeout %dms\n",
				vop_dev->sync.stdbyfin_to);
			return -ETIMEDOUT;
		}
	} else {
		spin_unlock(&vop_dev->reg_lock);
	}

	return 0;
}

static int vop_standby_disable(struct vop_device *vop_dev)
{
	u64 val;
	int ret;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_dev->sync.frmst.done = 0;
		val = V_IMD_VOP_STANDBY_EN(0) | V_IMD_VOP_DMA_STOP(0) |
			V_IMD_DSP_OUT_ZERO(0);
		vop_msk_reg(vop_dev, SYS_CTRL2, val);
		vop_msk_reg(vop_dev, DSP_CTRL2, V_DSP_BLANK_EN(0));
		vop_cfg_done(vop_dev);
		spin_unlock(&vop_dev->reg_lock);

		/* win address maybe effect after next frame start,
		 * but mmu maybe effect right now, so need wait frame start
		 */
		ret = vop_completion_timeout_ms(&vop_dev->sync.frmst,
						vop_dev->sync.frmst_to);
		if (!ret) {
			dev_err(vop_dev->dev, "wait frame start timeout %dms\n",
				vop_dev->sync.frmst_to);
			return -ETIMEDOUT;
		}
	} else {
		spin_unlock(&vop_dev->reg_lock);
	}

	return 0;
}

static int vop_mmu_enable(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (!dev_drv->iommu_enabled || !dev_drv->mmu_dev) {
		pr_debug("%s: VOP iommu is disabled or not find mmu dev\n",
			 __func__);
		return -ENODEV;
	}

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		if (!vop_dev->iommu_status) {
			vop_dev->iommu_status = 1;
			rockchip_iovmm_activate(dev_drv->dev);
		}
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_mmu_disable(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (!dev_drv->iommu_enabled || !dev_drv->mmu_dev) {
		pr_debug("%s: VOP iommu is disabled or not find mmu dev\n",
			 __func__);
		return -ENODEV;
	}

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		if (vop_dev->iommu_status) {
			vop_dev->iommu_status = 0;
			rockchip_iovmm_deactivate(dev_drv->dev);
		}
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_reg_dump(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	int *cbase = (int *)vop_dev->regs;
	int *regsbak = (int *)vop_dev->regsbak;
	int i, j, val;
	char dbg_message[30];
	char buf[10];

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		pr_info("vop back up reg:\n");
		memset(dbg_message, 0, sizeof(dbg_message));
		memset(buf, 0, sizeof(buf));
		for (i = 0; i <= (0x200 >> 4); i++) {
			val = sprintf(dbg_message, "0x%04x: ", i * 16);
			for (j = 0; j < 4; j++) {
				val = sprintf(buf, "%08x  ",
					      *(regsbak + i * 4 + j));
				strcat(dbg_message, buf);
			}
			pr_info("%s\n", dbg_message);
			memset(dbg_message, 0, sizeof(dbg_message));
			memset(buf, 0, sizeof(buf));
		}

		pr_info("vop reg:\n");
		for (i = 0; i <= (0x200 >> 4); i++) {
			val = sprintf(dbg_message, "0x%04x: ", i * 16);
			for (j = 0; j < 4; j++) {
				sprintf(buf, "%08x  ",
					readl_relaxed(cbase + i * 4 + j));
				strcat(dbg_message, buf);
			}
			pr_info("%s\n", dbg_message);
			memset(dbg_message, 0, sizeof(dbg_message));
			memset(buf, 0, sizeof(buf));
		}
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

#define WIN_EN(id)		\
static int win##id##_enable(struct vop_device *vop_dev, int en)	\
{ \
	spin_lock(&vop_dev->reg_lock);					\
	vop_msk_reg(vop_dev, WIN##id##_CTRL0, V_WIN##id##_EN((u64)en));	\
	vop_cfg_done(vop_dev);						\
	spin_unlock(&vop_dev->reg_lock);				\
	return 0;							\
}

WIN_EN(0);
WIN_EN(1);

/*
 * enable/disable win directly
 */
static int vop_win_direct_en(struct rk_lcdc_driver *drv,
			     int win_id, int en)
{
	struct vop_device *vop_dev = to_vop_dev(drv);

	if (win_id == 0)
		win0_enable(vop_dev, en);
	else if (win_id == 1)
		win1_enable(vop_dev, en);
	else
		dev_err(vop_dev->dev, "invalid win number:%d\n", win_id);
	return 0;
}

#define SET_WIN_ADDR(id) \
static int set_win##id##_addr(struct vop_device *vop_dev, u32 addr) \
{							\
	spin_lock(&vop_dev->reg_lock);			\
	vop_writel(vop_dev, WIN##id##_YRGB_MST, addr);	\
	vop_msk_reg(vop_dev, WIN##id##_CTRL0, V_WIN##id##_EN(1));	\
	vop_cfg_done(vop_dev);			\
	spin_unlock(&vop_dev->reg_lock);		\
	return 0;					\
}

SET_WIN_ADDR(0);
SET_WIN_ADDR(1);

static int vop_direct_set_win_addr(struct rk_lcdc_driver *dev_drv,
				   int win_id, u32 addr)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (win_id == 0)
		set_win0_addr(vop_dev, addr);
	else
		set_win1_addr(vop_dev, addr);

	return 0;
}

static void vop_read_reg_default_cfg(struct vop_device *vop_dev)
{
	int reg = 0;
	u32 val = 0;
	struct rk_screen *screen = vop_dev->driver.cur_screen;
	u32 h_pw_bp = screen->mode.hsync_len + screen->mode.left_margin;
	u32 V_pw_bp = screen->mode.vsync_len + screen->mode.upper_margin;
	u32 st_x, st_y;
	struct rk_lcdc_win *win0 = vop_dev->driver.win[0];

	spin_lock(&vop_dev->reg_lock);
	for (reg = 0; reg < vop_dev->len; reg += 4) {
		val = vop_readl_backup(vop_dev, reg);
		switch (reg) {
		case WIN0_ACT_INFO:
			win0->area[0].xact = (val & MASK(WIN0_ACT_WIDTH)) + 1;
			win0->area[0].yact =
				((val & MASK(WIN0_ACT_HEIGHT)) >> 16) + 1;
			break;
		case WIN0_DSP_INFO:
			win0->area[0].xsize = (val & MASK(WIN0_DSP_WIDTH)) + 1;
			win0->area[0].ysize =
			    ((val & MASK(WIN0_DSP_HEIGHT)) >> 16) + 1;
			break;
		case WIN0_DSP_ST:
			st_x = val & MASK(WIN0_DSP_XST);
			st_y = (val & MASK(WIN0_DSP_YST)) >> 16;
			win0->area[0].xpos = st_x - h_pw_bp;
			win0->area[0].ypos = st_y - V_pw_bp;
			break;
		case WIN0_CTRL0:
			win0->state = val & MASK(WIN0_EN);
			win0->area[0].fmt_cfg =
					(val & MASK(WIN0_DATA_FMT)) >> 1;
			win0->area[0].format = win0->area[0].fmt_cfg;
			break;
		case WIN0_VIR:
			win0->area[0].y_vir_stride =
					val & MASK(WIN0_YRGB_VIR_STRIDE);
			win0->area[0].uv_vir_stride =
			    (val & MASK(WIN0_CBR_VIR_STRIDE)) >> 16;
			if (win0->area[0].format == ARGB888)
				win0->area[0].xvir = win0->area[0].y_vir_stride;
			else if (win0->area[0].format == RGB888)
				win0->area[0].xvir =
				    win0->area[0].y_vir_stride * 4 / 3;
			else if ((win0->area[0].format == RGB565) ||
				 (win0->area[0].format == BGR565))
				win0->area[0].xvir =
				    2 * win0->area[0].y_vir_stride;
			else
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
	spin_unlock(&vop_dev->reg_lock);
}

static int vop_pre_init(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (vop_dev->pre_init)
		return 0;

	if (dev_drv->iommu_enabled) {
		dev_drv->mmu_dev = rk_fb_get_sysmmu_device_by_compatible(
							dev_drv->mmu_dts_name);
		if (dev_drv->mmu_dev)
			rk_fb_platform_set_sysmmu(
				dev_drv->mmu_dev, dev_drv->dev);
		else
			dev_err(dev_drv->dev, "fail get rk iommu device\n");
	}

	if (!support_uboot_display())
		rk_disp_pwr_enable(dev_drv);

	vop_clk_enable(vop_dev);

	/* backup reg config at uboot */
	vop_read_reg_default_cfg(vop_dev);

	/* vop io voltage select-->0: 3.3v; 1: 1.8v */
	if (vop_dev->pwr18 == 1)
		vop_grf_writel(vop_dev->grf_base, GRF_IO_VSEL,
			       V_VOP_IOVOL_SEL(1));
	else
		vop_grf_writel(vop_dev->grf_base, GRF_IO_VSEL,
			       V_VOP_IOVOL_SEL(0));

	vop_msk_reg(vop_dev, SYS_CTRL1, V_SW_AXI_MAX_OUTSTAND_EN(1) |
		    V_SW_AXI_MAX_OUTSTAND_NUM(31));
	vop_msk_reg(vop_dev, SYS_CTRL2, V_IMD_AUTO_GATING_EN(0));
	vop_cfg_done(vop_dev);
	vop_dev->pre_init = true;

	return 0;
}

static void vop_deinit(struct vop_device *vop_dev)
{
	struct rk_lcdc_driver *dev_drv = &vop_dev->driver;

	vop_standby_enable(vop_dev);
	vop_irq_disable(vop_dev);
	vop_mmu_disable(dev_drv);
	vop_clk_disable(vop_dev);
	clk_unprepare(vop_dev->dclk);
	clk_unprepare(vop_dev->aclk);
	clk_unprepare(vop_dev->hclk);
	pm_runtime_disable(vop_dev->dev);
}

static void __maybe_unused
vop_win_csc_mode(struct vop_device *vop_dev, struct rk_lcdc_win *win,
		 int csc_mode)
{
	u64 val;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		if (win->id == 0) {
			val = V_WIN0_CSC_MODE(csc_mode);
			vop_msk_reg(vop_dev, WIN0_CTRL0, val);
		} else if (win->id == 1) {
			val = V_WIN1_CSC_MODE(csc_mode);
			vop_msk_reg(vop_dev, WIN1_CTRL0, val);
		} else {
			dev_err(vop_dev->dev, "%s win%d unsupport csc mode",
				__func__, win->id);
		}
	}
	spin_unlock(&vop_dev->reg_lock);
}

static int vop_clr_key_cfg(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_lcdc_win *win;
	int i;

	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		win = dev_drv->win[i];
		switch (i) {
		case 0:
			vop_writel(vop_dev, WIN0_COLOR_KEY, win->color_key_val);
			break;
		case 1:
			vop_writel(vop_dev, WIN1_COLOR_KEY, win->color_key_val);
			break;
		default:
			pr_info("%s:un support win num:%d\n",
				__func__, i);
			break;
		}
	}
	return 0;
}

static int vop_alpha_cfg(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	u64 val;
	int ppixel_alpha = 0;
	int alpha_en = win->alpha_en;
	int i;

	if (!alpha_en) {
		if (win_id == 0) {
			val = V_WIN0_ALPHA_EN(0);
			vop_msk_reg(vop_dev, WIN0_ALPHA_CTRL, val);
		} else {
			val = V_WIN1_ALPHA_EN(0);
			vop_msk_reg(vop_dev, WIN1_ALPHA_CTRL, val);
		}
		return 0;
	}

	ppixel_alpha = ((win->area[0].format == ARGB888) ||
			(win->area[0].format == ABGR888)) ? 1 : 0;

	for (i = 0; i < dev_drv->lcdc_win_num; i++) {
		if (!dev_drv->win[i]->state)
			continue;
		if (win->z_order > dev_drv->win[i]->z_order)
			break;
	}

	/*
	 * The bottom layer not support ppixel_alpha mode.
	 */
	if (i == dev_drv->lcdc_win_num) {
		ppixel_alpha = 0;
		alpha_en = 0;
	}

	if (win_id == 0) {
		val = V_WIN0_ALPHA_EN(alpha_en) |
			V_WIN0_ALPHA_MODE(ppixel_alpha) |
			V_WIN0_ALPHA_PRE_MUL(ppixel_alpha) |
			V_WIN0_ALPHA_SAT_MODE(0);
		vop_msk_reg(vop_dev, WIN0_ALPHA_CTRL, val);
	} else if (win_id == 1) {
		val = V_WIN1_ALPHA_EN(alpha_en) |
			V_WIN1_ALPHA_MODE(ppixel_alpha) |
			V_WIN1_ALPHA_PRE_MUL(ppixel_alpha) |
			V_WIN1_ALPHA_SAT_MODE(0);
		vop_msk_reg(vop_dev, WIN1_ALPHA_CTRL, val);
	} else {
		dev_err(vop_dev->dev, "%s: invalid win id=%d or unsupport\n",
			__func__, win_id);
	}

	return 0;
}

static int vop_axi_gather_cfg(struct vop_device *vop_dev,
			      struct rk_lcdc_win *win)
{
	u64 val;
	u16 yrgb_gather_num = 3;
	u16 cbcr_gather_num = 1;

	switch (win->area[0].format) {
	case ARGB888:
	case XBGR888:
	case ABGR888:
	case XRGB888:
		yrgb_gather_num = 3;
		break;
	case RGB888:
	case RGB565:
	case BGR888:
	case BGR565:
		yrgb_gather_num = 2;
		break;
	case YUV444:
	case YUV422:
	case YUV420:
	case YUV420_A:
	case YUV422_A:
	case YUV444_A:
	case YUV420_NV21:
		yrgb_gather_num = 1;
		cbcr_gather_num = 2;
		break;
	default:
		dev_err(vop_dev->driver.dev, "%s:un supported format[%d]\n",
			__func__, win->area[0].format);
		return -EINVAL;
	}

	if (win->id == 0) {
		val = V_WIN0_YRGB_AXI_GATHER_EN(1) |
			V_WIN0_CBR_AXI_GATHER_EN(1) |
			V_WIN0_YRGB_AXI_GATHER_NUM(yrgb_gather_num) |
			V_WIN0_CBR_AXI_GATHER_NUM(cbcr_gather_num);
		vop_msk_reg(vop_dev, WIN0_CTRL1, val);
	} else if (win->id == 1) {
		val = V_WIN1_AXI_GATHER_EN(1) |
			V_WIN1_AXI_GATHER_NUM(yrgb_gather_num);
		vop_msk_reg(vop_dev, WIN1_CTRL1, val);
	}
	return 0;
}

static int vop_win0_reg_update(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	u64 val;

	if (win->state == 1) {
		vop_axi_gather_cfg(vop_dev, win);
		val = V_WIN0_EN(win->state) |
			V_WIN0_DATA_FMT(win->area[0].fmt_cfg) |
			V_WIN0_RB_SWAP(win->area[0].swap_rb) |
			V_WIN0_UV_SWAP(win->area[0].swap_uv);
		if (dev_drv->cur_screen->mode.vmode & FB_VMODE_INTERLACED)
			val |= V_WIN0_INTERLACE_READ(1);
		else
			val |= V_WIN0_INTERLACE_READ(0);
		vop_msk_reg(vop_dev, WIN0_CTRL0, val);

		val = V_WIN0_YRGB_VIR_STRIDE(win->area[0].y_vir_stride) |
		    V_WIN0_CBR_VIR_STRIDE(win->area[0].uv_vir_stride);
		vop_writel(vop_dev, WIN0_VIR, val);

		val = V_WIN0_DSP_WIDTH(win->area[0].xsize - 1) |
		    V_WIN0_DSP_HEIGHT(win->area[0].ysize - 1);
		vop_writel(vop_dev, WIN0_DSP_INFO, val);

		val = V_WIN0_DSP_XST(win->area[0].dsp_stx) |
		    V_WIN0_DSP_YST(win->area[0].dsp_sty);
		vop_writel(vop_dev, WIN0_DSP_ST, val);

		/* only win0 support scale and yuv */
		val = V_WIN0_ACT_WIDTH(win->area[0].xact - 1) |
			V_WIN0_ACT_HEIGHT(win->area[0].yact - 1);
		vop_writel(vop_dev, WIN0_ACT_INFO, val);

		val = V_WIN0_HS_FACTOR_YRGB(win->scale_yrgb_x) |
			V_WIN0_VS_FACTOR_YRGB(win->scale_yrgb_y);
		vop_writel(vop_dev, WIN0_SCL_FACTOR_YRGB, val);

		val = V_WIN0_HS_FACTOR_CBR(win->scale_cbcr_x) |
			V_WIN0_VS_FACTOR_CBR(win->scale_cbcr_y);
		vop_writel(vop_dev, WIN0_SCL_FACTOR_CBR, val);

		if (win->area[0].y_addr > 0)
			vop_writel(vop_dev, WIN0_YRGB_MST, win->area[0].y_addr);
		if (win->area[0].uv_addr > 0)
			vop_writel(vop_dev, WIN0_CBR_MST, win->area[0].uv_addr);

		vop_alpha_cfg(dev_drv, win_id);
	} else {
		val = V_WIN0_EN(win->state);
		vop_msk_reg(vop_dev, WIN0_CTRL0, val);
	}

	return 0;
}

static int vop_win1_reg_update(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	u64 val;

	if (win->state == 1) {
		vop_axi_gather_cfg(vop_dev, win);
		val = V_WIN1_EN(win->state) |
			V_WIN1_DATA_FMT(win->area[0].fmt_cfg) |
			V_WIN1_RB_SWAP(win->area[0].swap_rb);
		if (dev_drv->cur_screen->mode.vmode & FB_VMODE_INTERLACED)
			val |= V_WIN1_INTERLACE_READ(1);
		vop_msk_reg(vop_dev, WIN1_CTRL0, val);

		val = V_WIN1_VIR_STRIDE(win->area[0].y_vir_stride);
		vop_writel(vop_dev, WIN1_VIR, val);

		val = V_WIN1_DSP_WIDTH(win->area[0].xsize - 1) |
		    V_WIN1_DSP_HEIGHT(win->area[0].ysize - 1);
		vop_writel(vop_dev, WIN1_DSP_INFO, val);

		val = V_WIN1_DSP_XST(win->area[0].dsp_stx) |
		    V_WIN1_DSP_YST(win->area[0].dsp_sty);
		vop_writel(vop_dev, WIN1_DSP_ST, val);

		if (win->area[0].y_addr > 0)
			vop_writel(vop_dev, WIN1_YRGB_MST, win->area[0].y_addr);

		vop_alpha_cfg(dev_drv, win_id);
	} else {
		val = V_WIN1_EN(win->state);
		vop_msk_reg(vop_dev, WIN1_CTRL0, val);
	}

	return 0;
}

static int vop_hwc_reg_update(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	unsigned int hwc_size = 0;
	u64 val;

	if (win->state == 1) {
		vop_axi_gather_cfg(vop_dev, win);

		if ((win->area[0].xsize == 32) && (win->area[0].ysize == 32))
			hwc_size = 0;
		else if ((win->area[0].xsize == 64) &&
			 (win->area[0].ysize == 64))
			hwc_size = 1;
		else
			dev_err(vop_dev->dev, "unsupported hwc size[%dx%d]!\n",
				win->area[0].xsize, win->area[0].ysize);

		val = V_HWC_EN(1) | V_HWC_SIZE(hwc_size);
		vop_msk_reg(vop_dev, HWC_CTRL0, val);

		val = V_HWC_DSP_XST(win->area[0].dsp_stx) |
		    V_HWC_DSP_YST(win->area[0].dsp_sty);
		vop_msk_reg(vop_dev, HWC_DSP_ST, val);

		if (win->area[0].y_addr > 0)
			vop_writel(vop_dev, HWC_MST, win->area[0].y_addr);
	} else {
		val = V_HWC_EN(win->state);
		vop_msk_reg(vop_dev, HWC_CTRL0, val);
	}

	return 0;
}

static int vop_layer_update_regs(struct vop_device *vop_dev,
				 struct rk_lcdc_win *win)
{
	struct rk_lcdc_driver *dev_drv = &vop_dev->driver;

	vop_msk_reg(vop_dev, SYS_CTRL2,
		    V_IMD_VOP_STANDBY_EN(vop_dev->standby));
	if (win->id == 0)
		vop_win0_reg_update(dev_drv, win->id);
	else if (win->id == 1)
		vop_win1_reg_update(dev_drv, win->id);
	else if (win->id == 2)
		vop_hwc_reg_update(dev_drv, win->id);
	vop_cfg_done(vop_dev);

	DBG(2, "%s for vop%d\n", __func__, vop_dev->id);
	return 0;
}

static int vop_set_hwc_lut(struct rk_lcdc_driver *dev_drv,
			   int *hwc_lut, int mode)
{
	int i = 0;
	int __iomem *c;
	int v;
	int len = 256 * sizeof(u32);
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (!dev_drv->hwc_lut)
		dev_drv->hwc_lut = devm_kzalloc(vop_dev->dev, len, GFP_KERNEL);

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_msk_reg(vop_dev, HWC_CTRL0, V_HWC_LUT_EN(0));
		vop_cfg_done(vop_dev);
		mdelay(25);
		for (i = 0; i < 256; i++) {
			if (mode == 1)
				dev_drv->hwc_lut[i] = hwc_lut[i];

			v = dev_drv->hwc_lut[i];
			c = vop_dev->hwc_lut_addr_base + (i << 2);
			writel_relaxed(v, c);
		}
		vop_msk_reg(vop_dev, HWC_CTRL0, V_HWC_LUT_EN(1));
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_set_lut(struct rk_lcdc_driver *dev_drv, int *dsp_lut)
{
	int i = 0;
	int __iomem *c;
	int v;
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (!dsp_lut)
		return 0;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_msk_reg(vop_dev, DSP_CTRL2, V_DSP_LUT_EN(0));
		vop_cfg_done(vop_dev);
		mdelay(25);
		for (i = 0; i < 256; i++) {
			v = dsp_lut[i];
			c = vop_dev->dsp_lut_addr_base + (i << 2);
			writel_relaxed(v, c);
		}
		vop_msk_reg(vop_dev, DSP_CTRL2, V_DSP_LUT_EN(1));
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_set_dclk(struct rk_lcdc_driver *dev_drv, int reset_rate)
{
	int ret = 0, fps = 0;
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_screen *screen = dev_drv->cur_screen;

	if (reset_rate)
		ret = clk_set_rate(vop_dev->dclk, screen->mode.pixclock);
	if (ret)
		dev_err(dev_drv->dev, "set lcdc%d dclk[%d] failed\n",
			vop_dev->id, screen->mode.pixclock);
	vop_dev->pixclock =
	    div_u64(1000000000000llu, clk_get_rate(vop_dev->dclk));
	vop_dev->driver.pixclock = vop_dev->pixclock;

	fps = rk_fb_calc_fps(screen, vop_dev->pixclock);
	screen->ft = 1000 / fps;
	dev_info(vop_dev->dev, "%s: dclk:%lu>>fps:%d ",
		 vop_dev->driver.name, clk_get_rate(vop_dev->dclk), fps);
	return 0;
}

static int vop_config_timing(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_screen *screen = dev_drv->cur_screen;
	u16 hsync_len = screen->mode.hsync_len;
	u16 left_margin = screen->mode.left_margin;
	u16 right_margin = screen->mode.right_margin;
	u16 vsync_len = screen->mode.vsync_len;
	u16 upper_margin = screen->mode.upper_margin;
	u16 lower_margin = screen->mode.lower_margin;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u64 val;
	u16 h_total, v_total;
	u16 vact_end_f1, vact_st_f1, vs_end_f1, vs_st_f1;

	/* config timing reg valid immediately or after frame start */
	if (screen->mode.vmode & FB_VMODE_INTERLACED) /* after frame start */
		vop_msk_reg(vop_dev, SYS_CTRL2, V_IMD_DSP_TIMING_IMD(1));
	else /* timing reg valid immediately */
		vop_msk_reg(vop_dev, SYS_CTRL2, V_IMD_DSP_TIMING_IMD(0));

	h_total = hsync_len + left_margin + x_res + right_margin;
	v_total = vsync_len + upper_margin + y_res + lower_margin;

	val = V_DSP_HS_END(hsync_len) | V_DSP_HTOTAL(h_total);
	vop_msk_reg(vop_dev, DSP_HTOTAL_HS_END, val);

	val = V_DSP_HACT_END(hsync_len + left_margin + x_res) |
	    V_DSP_HACT_ST(hsync_len + left_margin);
	vop_msk_reg(vop_dev, DSP_HACT_ST_END, val);

	if (screen->mode.vmode & FB_VMODE_INTERLACED) {
		/* First Field Timing */
		val = V_DSP_VS_END(vsync_len) |
		    V_DSP_VTOTAL(2 * (vsync_len + upper_margin +
				      lower_margin) + y_res + 1);
		vop_msk_reg(vop_dev, DSP_VTOTAL_VS_END, val);

		val = V_DSP_VACT_END(vsync_len + upper_margin + y_res / 2) |
		    V_DSP_VACT_ST(vsync_len + upper_margin);
		vop_msk_reg(vop_dev, DSP_VACT_ST_END, val);

		/* Second Field Timing */
		vs_st_f1 = vsync_len + upper_margin + y_res / 2 + lower_margin;
		vs_end_f1 = 2 * vsync_len + upper_margin + y_res / 2 +
		    lower_margin;
		val = V_DSP_VS_ST_F1(vs_st_f1) | V_DSP_VS_END_F1(vs_end_f1);
		vop_msk_reg(vop_dev, DSP_VS_ST_END_F1, val);

		vact_end_f1 = 2 * (vsync_len + upper_margin) + y_res +
		    lower_margin + 1;
		vact_st_f1 = 2 * (vsync_len + upper_margin) + y_res / 2 +
		    lower_margin + 1;
		val = V_DSP_VACT_END_F1(vact_end_f1) |
			V_DSP_VACT_ST_F1(vact_st_f1);
		vop_msk_reg(vop_dev, DSP_VACT_ST_END_F1, val);

		val = V_DSP_LINE_FLAG0_NUM(lower_margin ?
					   vact_end_f1 : vact_end_f1 - 1);

		val |= V_DSP_LINE_FLAG1_NUM(lower_margin ?
					    vact_end_f1 : vact_end_f1 - 1);
		vop_msk_reg(vop_dev, LINE_FLAG, val);
	} else {
		val = V_DSP_VS_END(vsync_len) | V_DSP_VTOTAL(v_total);
		vop_msk_reg(vop_dev, DSP_VTOTAL_VS_END, val);

		val = V_DSP_VACT_END(vsync_len + upper_margin + y_res) |
		    V_DSP_VACT_ST(vsync_len + upper_margin);
		vop_msk_reg(vop_dev, DSP_VACT_ST_END, val);

		val = V_DSP_LINE_FLAG0_NUM(vsync_len + upper_margin + y_res) |
			V_DSP_LINE_FLAG1_NUM(vsync_len + upper_margin + y_res);
		vop_msk_reg(vop_dev, LINE_FLAG, val);
	}

	return 0;
}

static int vop_config_source(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_screen *screen = dev_drv->cur_screen;
	u64 val = 0;

	switch (screen->type) {
	case SCREEN_RGB:
		vop_grf_writel(vop_dev->grf_base, GRF_SOC_CON5,
			       V_RGB_VOP_SEL(dev_drv->id));
		val = V_RGB_DCLK_EN(1) | V_RGB_DCLK_POL(screen->pin_dclk) |
			V_RGB_HSYNC_POL(screen->pin_hsync) |
			V_RGB_VSYNC_POL(screen->pin_vsync) |
			V_RGB_DEN_POL(screen->pin_den);
		break;
	case SCREEN_HDMI:
		vop_grf_writel(vop_dev->grf_base, GRF_SOC_CON0,
			       V_HDMI_VOP_SEL(dev_drv->id));
		val = V_HDMI_DCLK_EN(1) | V_HDMI_DCLK_POL(screen->pin_dclk) |
			V_HDMI_HSYNC_POL(screen->pin_hsync) |
			V_HDMI_VSYNC_POL(screen->pin_vsync) |
			V_HDMI_DEN_POL(screen->pin_den);
		break;
	case SCREEN_LVDS:
		vop_grf_writel(vop_dev->grf_base, GRF_SOC_CON0,
			       V_LVDS_VOP_SEL(dev_drv->id));
		val = V_LVDS_DCLK_EN(1) | V_LVDS_DCLK_POL(screen->pin_dclk) |
			V_LVDS_HSYNC_POL(screen->pin_hsync) |
			V_LVDS_VSYNC_POL(screen->pin_vsync) |
			V_LVDS_DEN_POL(screen->pin_den);
		break;
	case SCREEN_MIPI:
		vop_grf_writel(vop_dev->grf_base, GRF_SOC_CON0,
			       V_DSI0_VOP_SEL(dev_drv->id));
		val = V_MIPI_DCLK_EN(1) | V_MIPI_DCLK_POL(screen->pin_dclk) |
			V_MIPI_HSYNC_POL(screen->pin_hsync) |
			V_MIPI_VSYNC_POL(screen->pin_vsync) |
			V_MIPI_DEN_POL(screen->pin_den);
		break;
	default:
		dev_err(vop_dev->dev, "un supported interface[%d]!\n",
			screen->type);
		break;
	}

	val |= V_SW_CORE_CLK_SEL(!!screen->pixelrepeat);
	if (screen->mode.vmode & FB_VMODE_INTERLACED)
		val |= V_SW_HDMI_CLK_I_SEL(1);
	else
		val |= V_SW_HDMI_CLK_I_SEL(0);
	vop_msk_reg(vop_dev, DSP_CTRL0, val);

	return 0;
}

static int vop_config_interface(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_screen *screen = dev_drv->cur_screen;
	u64 val = 0;

	/* FRC dither down init */
	if (screen->face != OUT_P888) {
		vop_writel(vop_dev, FRC_LOWER01_0, 0x12844821);
		vop_writel(vop_dev, FRC_LOWER01_1, 0x21488412);
		vop_writel(vop_dev, FRC_LOWER10_0, 0xa55a9696);
		vop_writel(vop_dev, FRC_LOWER10_1, 0x5aa56969);
		vop_writel(vop_dev, FRC_LOWER11_0, 0xdeb77deb);
		vop_writel(vop_dev, FRC_LOWER11_1, 0xed7bb7de);
	}

	switch (screen->face) {
	case OUT_P888:
		val = V_DSP_OUT_MODE(OUT_P888) | V_DITHER_DOWN(0);
		break;
	case OUT_P565:
		val = V_DSP_OUT_MODE(OUT_P565) | V_DITHER_DOWN(1) |
			V_DITHER_DOWN_MODE(DITHER_888_565) |
			V_DITHER_DOWN_SEL(DITHER_SEL_FRC);
		break;
	case OUT_P666:
		val = V_DSP_OUT_MODE(OUT_P666) | V_DITHER_DOWN(1) |
			V_DITHER_DOWN_MODE(DITHER_888_666) |
			V_DITHER_DOWN_SEL(DITHER_SEL_FRC);
		break;
	case OUT_D888_P565:
		val = V_DSP_OUT_MODE(OUT_P888) | V_DITHER_DOWN(1) |
			V_DITHER_DOWN_MODE(DITHER_888_565) |
			V_DITHER_DOWN_SEL(DITHER_SEL_FRC);
		break;
	case OUT_D888_P666:
		val = V_DSP_OUT_MODE(OUT_P888) | V_DITHER_DOWN(1) |
			V_DITHER_DOWN_MODE(DITHER_888_666) |
			V_DITHER_DOWN_SEL(DITHER_SEL_FRC);
		break;
	default:
		dev_err(vop_dev->dev, "un supported screen face[%d]!\n",
			screen->face);
		break;
	}

	if (screen->mode.vmode & FB_VMODE_INTERLACED)
		val |= V_DSP_INTERLACE(1) | V_INTERLACE_FIELD_POL(0);
	else
		val |= V_DSP_INTERLACE(0) | V_INTERLACE_FIELD_POL(0);

	dev_drv->output_color = screen->color_mode;
	if (screen->color_mode == COLOR_RGB)
		dev_drv->overlay_mode = VOP_RGB_DOMAIN;
	else
		dev_drv->overlay_mode = VOP_YUV_DOMAIN;

	val |= V_SW_OVERLAY_MODE(dev_drv->overlay_mode) |
		V_DSP_BG_SWAP(screen->swap_gb) |
		V_DSP_RB_SWAP(screen->swap_rb) |
		V_DSP_RG_SWAP(screen->swap_rg) |
		V_DSP_DELTA_SWAP(screen->swap_delta) |
		V_DSP_DUMMY_SWAP(screen->swap_dumy) |
		V_DSP_BLANK_EN(0) | V_DSP_BLACK_EN(0);
	vop_msk_reg(vop_dev, DSP_CTRL2, val);

	return 0;
}

static void vop_config_background(struct rk_lcdc_driver *dev_drv, int rgb)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u64 val;
	int b = rgb & 0xff;
	int g = (rgb >> 8) & 0xff;
	int r = (rgb >> 16) & 0xff;

	val = V_DSP_BG_BLUE(b) | V_DSP_BG_GREEN(g) | V_DSP_BG_RED(r);
	vop_msk_reg(vop_dev, DSP_BG, val);
}

static void vop_bcsh_path_sel(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (dev_drv->overlay_mode == VOP_YUV_DOMAIN) {
		if (dev_drv->output_color == COLOR_YCBCR)	/* bypass */
			vop_msk_reg(vop_dev, BCSH_CTRL,
				    V_SW_BCSH_Y2R_EN(0) | V_SW_BCSH_R2Y_EN(0));
		else		/* YUV2RGB */
			vop_msk_reg(vop_dev, BCSH_CTRL, V_SW_BCSH_Y2R_EN(1) |
				    V_SW_BCSH_Y2R_CSC_MODE(VOP_Y2R_CSC_MPEG) |
				    V_SW_BCSH_R2Y_EN(0));
	} else {
		/* overlay_mode=VOP_RGB_DOMAIN */
		/* bypass  --need check,if bcsh close? */
		if (dev_drv->output_color == COLOR_RGB) {
			if (dev_drv->bcsh.enable == 1)
				vop_msk_reg(vop_dev, BCSH_CTRL,
					    V_SW_BCSH_R2Y_EN(1) |
					    V_SW_BCSH_Y2R_EN(1));
			else
				vop_msk_reg(vop_dev, BCSH_CTRL,
					    V_SW_BCSH_R2Y_EN(0) |
					    V_SW_BCSH_Y2R_EN(0));
		} else {
			/* RGB2YUV */
			vop_msk_reg(vop_dev, BCSH_CTRL,
				    V_SW_BCSH_R2Y_EN(1) |
				    V_SW_BCSH_R2Y_CSC_MODE(VOP_Y2R_CSC_MPEG) |
				    V_SW_BCSH_Y2R_EN(0));
		}
	}
}

static int vop_get_dspbuf_info(struct rk_lcdc_driver *dev_drv, u16 *xact,
			       u16 *yact, int *format, u32 *dsp_addr,
			       int *ymirror)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u32 val;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		val = vop_readl(vop_dev, WIN0_ACT_INFO);
		*xact = (val & MASK(WIN0_ACT_WIDTH)) + 1;
		*yact = ((val & MASK(WIN0_ACT_HEIGHT)) >> 16) + 1;

		val = vop_readl(vop_dev, WIN0_CTRL0);
		*format = (val & MASK(WIN0_DATA_FMT)) >> 1;
		*dsp_addr = vop_readl(vop_dev, WIN0_YRGB_MST);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_post_dspbuf(struct rk_lcdc_driver *dev_drv, u32 rgb_mst,
			   int format, u16 xact, u16 yact, u16 xvir,
			   int ymirror)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	int swap = (format == RGB888) ? 1 : 0;
	u64 val;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		val = V_WIN0_DATA_FMT(format) | V_WIN0_RB_SWAP(swap);
		vop_msk_reg(vop_dev, WIN0_CTRL0, val);

		vop_msk_reg(vop_dev, WIN0_VIR, V_WIN0_YRGB_VIR_STRIDE(xvir));
		vop_writel(vop_dev, WIN0_ACT_INFO, V_WIN0_ACT_WIDTH(xact - 1) |
			   V_WIN0_ACT_HEIGHT(yact - 1));

		vop_writel(vop_dev, WIN0_YRGB_MST, rgb_mst);

		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static void vop_reg_restore(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	int len = FLAG_REG;

	spin_lock(&vop_dev->reg_lock);

	if (likely(vop_dev->clk_on))
		memcpy(vop_dev->regs, vop_dev->regsbak, len);

	spin_unlock(&vop_dev->reg_lock);

	/* set screen GAMMA lut */
	if (dev_drv->cur_screen && dev_drv->cur_screen->dsp_lut)
		vop_set_lut(dev_drv, dev_drv->cur_screen->dsp_lut);

	/* set hwc lut */
	vop_set_hwc_lut(dev_drv, dev_drv->hwc_lut, 0);
}

static int vop_load_screen(struct rk_lcdc_driver *dev_drv, bool initscreen)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_screen *screen = dev_drv->cur_screen;

	/*if (!vop_dev->standby && initscreen && (dev_drv->first_frame != 1))*/
	/*	flush_kthread_worker(&dev_drv->update_regs_worker);*/

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_config_interface(dev_drv);
		vop_config_source(dev_drv);
		vop_config_timing(dev_drv);
		if (dev_drv->overlay_mode == VOP_YUV_DOMAIN)
			vop_config_background(dev_drv, 0x801080);
		else
			vop_config_background(dev_drv, 0x000000);

		vop_bcsh_path_sel(dev_drv);
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	vop_set_dclk(dev_drv, 1);
	if (screen->init)
		screen->init();

	return 0;
}

/*
 * enable or disable layer according to win id
 * @open: 1 enable; 0 disable
 */
static void vop_layer_enable(struct vop_device *vop_dev,
			     unsigned int win_id, bool open)
{
	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on) &&
	    vop_dev->driver.win[win_id]->state != open) {
		if (open) {
			if (!vop_dev->atv_layer_cnt) {
				dev_info(vop_dev->dev,
					 "wakeup from standby!\n");
				vop_dev->standby = 0;
			}
			vop_dev->atv_layer_cnt |= (1 << win_id);
		} else {
			if (vop_dev->atv_layer_cnt & (1 << win_id))
				vop_dev->atv_layer_cnt &= ~(1 << win_id);
		}
		vop_dev->driver.win[win_id]->state = open;
		if (!open) {
			vop_layer_update_regs(vop_dev,
					      vop_dev->driver.win[win_id]);
			vop_cfg_done(vop_dev);
		}
		/* if no layer used,disable lcdc */
		if (!vop_dev->atv_layer_cnt) {
			dev_info(vop_dev->dev,
				 "no layer is used,go to standby!\n");
			vop_dev->standby = 1;
		}
	}
	spin_unlock(&vop_dev->reg_lock);
}

static int vop_open(struct rk_lcdc_driver *dev_drv, int win_id,
		    bool open)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	/* enable clk,when first layer open */
	if ((open) && (!vop_dev->atv_layer_cnt)) {
		/* rockchip_set_system_status(sys_status); */
		vop_pre_init(dev_drv);
		vop_clk_enable(vop_dev);
		vop_irq_enable(vop_dev);

		if (support_uboot_display() && (vop_dev->prop == PRMRY)) {
			vop_set_dclk(dev_drv, 0);
		} else {
			vop_load_screen(dev_drv, 1);
			if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
				dev_drv->trsm_ops->enable();
		}
		if (dev_drv->bcsh.enable)
			vop_set_bcsh(dev_drv, 1);

		/* set screen GAMMA lut */
		if (dev_drv->cur_screen && dev_drv->cur_screen->dsp_lut)
			vop_set_lut(dev_drv, dev_drv->cur_screen->dsp_lut);
	}

	if (win_id < ARRAY_SIZE(vop_win))
		vop_layer_enable(vop_dev, win_id, open);
	else
		dev_err(vop_dev->dev, "invalid win id:%d\n", win_id);

	dev_drv->first_frame = 0;
	return 0;
}

static int vop_pan_display(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;

	if (!screen) {
		dev_err(dev_drv->dev, "screen is null!\n");
		return -ENOENT;
	}

	if (win_id >= dev_drv->lcdc_win_num) {
		dev_err(dev_drv->dev, "invalid win id:%d!\n", win_id);
		return -EINVAL;
	}

	win = dev_drv->win[win_id];
	win->area[0].y_addr = win->area[0].smem_start + win->area[0].y_offset;
	/* only win0 support yuv format */
	if (win_id == 0)
		win->area[0].uv_addr =
			win->area[0].cbr_start + win->area[0].c_offset;
	else
		win->area[0].uv_addr = 0;

	DBG(2, "lcdc[%d]:win[%d]>>:y_addr:0x%x>>uv_addr:0x%x",
	    vop_dev->id, win->id, win->area[0].y_addr, win->area[0].uv_addr);
	DBG(2, ">>y_offset:0x%x>>c_offset=0x%x\n",
	    win->area[0].y_offset, win->area[0].c_offset);
	return 0;
}

static int win_0_1_set_par(struct vop_device *vop_dev,
			   struct rk_screen *screen, struct rk_lcdc_win *win)
{
	char fmt[9] = "NULL";

	win->area[0].dsp_stx = win->area[0].xpos + screen->mode.left_margin +
				screen->mode.hsync_len;
	if (screen->mode.vmode & FB_VMODE_INTERLACED) {
		win->area[0].ysize /= 2;
		win->area[0].dsp_sty = win->area[0].ypos / 2 +
			screen->mode.upper_margin + screen->mode.vsync_len;
	} else {
		win->area[0].dsp_sty = win->area[0].ypos +
			screen->mode.upper_margin + screen->mode.vsync_len;
	}

	win->scale_yrgb_x = CALSCALE(win->area[0].xact, win->area[0].xsize);
	win->scale_yrgb_y = CALSCALE(win->area[0].yact, win->area[0].ysize);

	switch (win->area[0].format) {
	case ARGB888:
		win->area[0].fmt_cfg = VOP_FORMAT_ARGB888;
		win->area[0].swap_rb = 0;
		break;
	case XBGR888:
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
	case XRGB888:
		win->area[0].fmt_cfg = VOP_FORMAT_ARGB888;
		win->area[0].swap_rb = 0;
		break;
	case BGR888:
		win->area[0].fmt_cfg = VOP_FORMAT_RGB888;
		win->area[0].swap_rb = 1;
		break;
	case BGR565:
		win->area[0].fmt_cfg = VOP_FORMAT_RGB565;
		win->area[0].swap_rb = 1;
		break;
	case YUV422:
		if (win->id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR422;
			win->area[0].swap_rb = 0;
			win->area[0].swap_uv = 0;
			win->scale_cbcr_x = CALSCALE(win->area[0].xact / 2,
						     win->area[0].xsize);
			win->scale_cbcr_y = CALSCALE(win->area[0].yact,
						     win->area[0].ysize);
		} else {
			dev_err(vop_dev->dev, "%s:win%d unsupport YUV format\n",
				__func__, win->id);
		}
		break;
	case YUV420:
		if (win->id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR420;
			win->area[0].swap_rb = 0;
			win->area[0].swap_uv = 0;
			win->scale_cbcr_x = CALSCALE(win->area[0].xact / 2,
						     win->area[0].xsize);
			win->scale_cbcr_y = CALSCALE(win->area[0].yact / 2,
						     win->area[0].ysize);
		} else {
			dev_err(vop_dev->dev, "%s:win%d unsupport YUV format\n",
				__func__, win->id);
		}

		break;
	case YUV420_NV21:
		if (win->id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR420;
			win->area[0].swap_rb = 0;
			win->area[0].swap_uv = 1;
			win->scale_cbcr_x = CALSCALE(win->area[0].xact / 2,
						     win->area[0].xsize);
			win->scale_cbcr_y = CALSCALE(win->area[0].yact / 2,
						     win->area[0].ysize);
		} else {
			dev_err(vop_dev->dev, "%s:win%d unsupport YUV format\n",
				__func__, win->id);
		}
		break;
	case YUV444:
		if (win->id == 0) {
			win->area[0].fmt_cfg = VOP_FORMAT_YCBCR444;
			win->area[0].swap_rb = 0;
			win->area[0].swap_uv = 0;
			win->scale_cbcr_x =
				CALSCALE(win->area[0].xact, win->area[0].xsize);
			win->scale_cbcr_y =
				CALSCALE(win->area[0].yact, win->area[0].ysize);
		} else {
			dev_err(vop_dev->dev, "%s:win%d unsupport YUV format\n",
				__func__, win->id);
		}
		break;
	default:
		dev_err(vop_dev->dev, "%s:unsupport format[%d]!\n",
			__func__, win->area[0].format);
		break;
	}

	DBG(1, "lcdc[%d]:win[%d]\n>>format:%s>>>xact:%d>>yact:%d>>xsize:%d",
	    vop_dev->id, win->id, get_format_string(win->area[0].format, fmt),
	    win->area[0].xact, win->area[0].yact, win->area[0].xsize);
	DBG(1, ">>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
	    win->area[0].ysize, win->area[0].xvir, win->area[0].yvir,
	    win->area[0].xpos, win->area[0].ypos);

	return 0;
}

static int hwc_set_par(struct vop_device *vop_dev,
		       struct rk_screen *screen, struct rk_lcdc_win *win)
{
	win->area[0].dsp_stx = win->area[0].xpos + screen->mode.left_margin +
				screen->mode.hsync_len;
	win->area[0].dsp_sty = win->area[0].ypos + screen->mode.upper_margin +
				screen->mode.vsync_len;

	DBG(1, "lcdc[%d]:hwc>>%s\n>>xsize:%d>>ysize:%d>>xpos:%d>>ypos:%d",
	    vop_dev->id, __func__, win->area[0].xsize, win->area[0].ysize,
	    win->area[0].xpos, win->area[0].ypos);
	return 0;
}

static int vop_set_par(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct rk_screen *screen = dev_drv->cur_screen;

	if (!screen) {
		dev_err(dev_drv->dev, "screen is null!\n");
		return -ENOENT;
	}

	switch (win_id) {
	case 0:
		win_0_1_set_par(vop_dev, screen, dev_drv->win[0]);
		break;
	case 1:
		win_0_1_set_par(vop_dev, screen, dev_drv->win[1]);
		break;
	case 2:
		hwc_set_par(vop_dev, screen, dev_drv->win[2]);
		break;
	default:
		dev_err(dev_drv->dev, "%s: unsupported win id:%d\n",
			__func__, win_id);
		break;
	}
	return 0;
}

static int vop_ioctl(struct rk_lcdc_driver *dev_drv, unsigned int cmd,
		     unsigned long arg, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u32 panel_size[2];
	void __user *argp = (void __user *)arg;
	struct color_key_cfg clr_key_cfg;

	switch (cmd) {
	case RK_FBIOGET_PANEL_SIZE:
		panel_size[0] = vop_dev->screen->mode.xres;
		panel_size[1] = vop_dev->screen->mode.yres;
		if (copy_to_user(argp, panel_size, 8))
			return -EFAULT;
		break;
	case RK_FBIOPUT_COLOR_KEY_CFG:
		if (copy_from_user(&clr_key_cfg, argp,
				   sizeof(struct color_key_cfg)))
			return -EFAULT;
		vop_clr_key_cfg(dev_drv);
		vop_writel(vop_dev, WIN0_COLOR_KEY,
			   clr_key_cfg.win0_color_key_cfg);
		vop_writel(vop_dev, WIN1_COLOR_KEY,
			   clr_key_cfg.win1_color_key_cfg);
		break;

	default:
		break;
	}
	return 0;
}

static int vop_get_backlight_device(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	struct device_node *backlight;
	struct property *prop;
	u32 *brightness_levels;
	u32 length, max, last;

	if (vop_dev->backlight)
		return 0;
	backlight = of_parse_phandle(vop_dev->dev->of_node, "backlight", 0);
	if (backlight) {
		vop_dev->backlight = of_find_backlight_by_node(backlight);
		if (!vop_dev->backlight)
			dev_info(vop_dev->dev, "No find backlight device\n");
	} else {
		dev_info(vop_dev->dev, "No find backlight device node\n");
	}
	prop = of_find_property(backlight, "brightness-levels", &length);
	if (!prop)
		return -EINVAL;
	max = length / sizeof(u32);
	last = max - 1;
	brightness_levels = kmalloc(256, GFP_KERNEL);
	if (brightness_levels)
		return -ENOMEM;

	if (!of_property_read_u32_array(backlight, "brightness-levels",
					brightness_levels, max)) {
		if (brightness_levels[0] > brightness_levels[last])
			dev_drv->cabc_pwm_pol = 1;/*negative*/
		else
			dev_drv->cabc_pwm_pol = 0;/*positive*/
	} else {
		dev_info(vop_dev->dev,
			 "Can not read brightness-levels value\n");
	}

	kfree(brightness_levels);

	return 0;
}

static int vop_backlight_close(struct rk_lcdc_driver *dev_drv, int enable)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (dev_drv->suspend_flag)
		return 0;

	vop_get_backlight_device(dev_drv);

	if (enable) {
		/* close the backlight */
		if (vop_dev->backlight) {
			vop_dev->backlight->props.power = FB_BLANK_POWERDOWN;
			backlight_update_status(vop_dev->backlight);
		}
		if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
			dev_drv->trsm_ops->disable();
	} else {
		if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
			dev_drv->trsm_ops->enable();
		msleep(100);
		/* open the backlight */
		if (vop_dev->backlight) {
			vop_dev->backlight->props.power = FB_BLANK_UNBLANK;
			backlight_update_status(vop_dev->backlight);
		}
	}

	return 0;
}

static int vop_early_suspend(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (dev_drv->suspend_flag)
		return 0;

	dev_drv->suspend_flag = 1;
	smp_wmb();
	flush_kthread_worker(&dev_drv->update_regs_worker);

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
		dev_drv->trsm_ops->disable();

	vop_standby_enable(vop_dev);
	vop_mmu_disable(dev_drv);
	vop_clk_disable(vop_dev);
	rk_disp_pwr_disable(dev_drv);

	return 0;
}

static int vop_early_resume(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (!dev_drv->suspend_flag)
		return 0;

	rk_disp_pwr_enable(dev_drv);
	vop_clk_enable(vop_dev);
	vop_reg_restore(dev_drv);
	vop_standby_disable(vop_dev);
	vop_mmu_enable(dev_drv);
	dev_drv->suspend_flag = 0;

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();

	return 0;
}

static int vop_blank(struct rk_lcdc_driver *dev_drv, int win_id, int blank_mode)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		vop_early_resume(dev_drv);
		break;
	case FB_BLANK_NORMAL:
		vop_early_suspend(dev_drv);
		break;
	default:
		vop_early_suspend(dev_drv);
		break;
	}

	dev_info(dev_drv->dev, "blank mode:%d\n", blank_mode);

	return 0;
}

static int vop_get_win_state(struct rk_lcdc_driver *dev_drv,
			     int win_id, int area_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u32 area_status = 0, state = 0;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		switch (win_id) {
		case 0:
			area_status =
				vop_read_bit(vop_dev, WIN0_CTRL0, V_WIN0_EN(0));
			break;
		case 1:
			area_status =
				vop_read_bit(vop_dev, WIN1_CTRL0, V_WIN1_EN(0));
			break;
		case 2:
			area_status =
				vop_read_bit(vop_dev, HWC_CTRL0, V_HWC_EN(0));
			break;
		default:
			pr_err("%s: win[%d]area[%d],unsupport!!!\n",
			       __func__, win_id, area_id);
			break;
		}
	}
	spin_unlock(&vop_dev->reg_lock);

	state = (area_status > 0) ? 1 : 0;
	return state;
}

static int vop_get_area_num(struct rk_lcdc_driver *dev_drv,
			    unsigned int *area_support)
{
	area_support[0] = 1;
	area_support[1] = 1;

	return 0;
}

static int vop_ovl_mgr(struct rk_lcdc_driver *dev_drv, int swap, bool set)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	int ovl;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		if (set) {
			vop_msk_reg(vop_dev, DSP_CTRL2, V_DSP_WIN0_TOP(swap));
			ovl = swap;
		} else {
			ovl =
			    vop_read_bit(vop_dev, DSP_CTRL2, V_DSP_WIN0_TOP(0));
		}
	} else {
		ovl = -EPERM;
	}
	spin_unlock(&vop_dev->reg_lock);

	return ovl;
}

static char *vop_format_to_string(int format, char *fmt)
{
	if (!fmt)
		return NULL;

	switch (format) {
	case 0:
		strcpy(fmt, "ARGB888");
		break;
	case 1:
		strcpy(fmt, "RGB888");
		break;
	case 2:
		strcpy(fmt, "RGB565");
		break;
	case 4:
		strcpy(fmt, "YCbCr420");
		break;
	case 5:
		strcpy(fmt, "YCbCr422");
		break;
	case 6:
		strcpy(fmt, "YCbCr444");
		break;
	default:
		strcpy(fmt, "invalid\n");
		break;
	}
	return fmt;
}

static ssize_t vop_get_disp_info(struct rk_lcdc_driver *dev_drv,
				 char *buf, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
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
	char dsp_buf[100];
	u32 win_ctrl, ovl, vir_info, act_info, dsp_info, dsp_st;
	u32 y_factor, uv_factor;
	u8 w0_state, w1_state;

	u32 w0_vir_y, w0_vir_uv, w0_act_x, w0_act_y, w0_dsp_x, w0_dsp_y;
	u32 w0_st_x = h_pw_bp, w0_st_y = v_pw_bp;
	u32 w1_vir_y, w1_dsp_x, w1_dsp_y;
	u32 w1_st_x = h_pw_bp, w1_st_y = v_pw_bp;
	u32 w0_y_h_fac, w0_y_v_fac, w0_uv_h_fac, w0_uv_v_fac;

	u32 dclk_freq;
	int size = 0;

	dclk_freq = screen->mode.pixclock;
	/*vop_reg_dump(dev_drv); */

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		ovl = vop_read_bit(vop_dev, DSP_CTRL2, V_DSP_WIN0_TOP(0));
		/* WIN0 */
		win_ctrl = vop_readl(vop_dev, WIN0_CTRL0);
		w0_state = win_ctrl & MASK(WIN0_EN);
		fmt_id = (win_ctrl & MASK(WIN0_DATA_FMT)) >> 1;
		vop_format_to_string(fmt_id, format_w0);
		vir_info = vop_readl(vop_dev, WIN0_VIR);
		act_info = vop_readl(vop_dev, WIN0_ACT_INFO);
		dsp_info = vop_readl(vop_dev, WIN0_DSP_INFO);
		dsp_st = vop_readl(vop_dev, WIN0_DSP_ST);
		y_factor = vop_readl(vop_dev, WIN0_SCL_FACTOR_YRGB);
		uv_factor = vop_readl(vop_dev, WIN0_SCL_FACTOR_CBR);
		w0_vir_y = vir_info & MASK(WIN0_YRGB_VIR_STRIDE);
		w0_vir_uv = (vir_info & MASK(WIN0_CBR_VIR_STRIDE)) >> 16;
		w0_act_x = (act_info & MASK(WIN0_ACT_WIDTH)) + 1;
		w0_act_y = ((act_info & MASK(WIN0_ACT_HEIGHT)) >> 16) + 1;
		w0_dsp_x = (dsp_info & MASK(WIN0_DSP_WIDTH)) + 1;
		w0_dsp_y = ((dsp_info & MASK(WIN0_DSP_HEIGHT)) >> 16) + 1;
		if (w0_state) {
			w0_st_x = dsp_st & MASK(WIN0_DSP_XST);
			w0_st_y = (dsp_st & MASK(WIN0_DSP_YST)) >> 16;
		}
		w0_y_h_fac = y_factor & MASK(WIN0_HS_FACTOR_YRGB);
		w0_y_v_fac = (y_factor & MASK(WIN0_VS_FACTOR_YRGB)) >> 16;
		w0_uv_h_fac = uv_factor & MASK(WIN0_HS_FACTOR_CBR);
		w0_uv_v_fac = (uv_factor & MASK(WIN0_VS_FACTOR_CBR)) >> 16;

		/* WIN1 */
		win_ctrl = vop_readl(vop_dev, WIN1_CTRL0);
		w1_state = win_ctrl & MASK(WIN1_EN);
		fmt_id = (win_ctrl & MASK(WIN1_DATA_FMT)) >> 1;
		vop_format_to_string(fmt_id, format_w1);
		vir_info = vop_readl(vop_dev, WIN1_VIR);
		dsp_info = vop_readl(vop_dev, WIN1_DSP_INFO);
		dsp_st = vop_readl(vop_dev, WIN1_DSP_ST);
		w1_vir_y = vir_info & MASK(WIN1_VIR_STRIDE);
		w1_dsp_x = (dsp_info & MASK(WIN1_DSP_WIDTH)) + 1;
		w1_dsp_y = ((dsp_info & MASK(WIN1_DSP_HEIGHT)) >> 16) + 1;
		if (w1_state) {
			w1_st_x = dsp_st & MASK(WIN1_DSP_XST);
			w1_st_y = (dsp_st & MASK(WIN1_DSP_YST)) >> 16;
		}
	} else {
		spin_unlock(&vop_dev->reg_lock);
		return -EPERM;
	}
	spin_unlock(&vop_dev->reg_lock);
	/* win0 */
	size += snprintf(dsp_buf, 80,
		 "win0:\n  state:%d, fmt:%7s\n  y_vir:%4d, uv_vir:%4d,",
		 w0_state, format_w0, w0_vir_y, w0_vir_uv);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 " x_act  :%5d, y_act  :%5d, dsp_x   :%5d, dsp_y   :%5d\n",
		 w0_act_x, w0_act_y, w0_dsp_x, w0_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 "  x_st :%4d, y_st  :%4d, y_h_fac:%5d, y_v_fac:%5d, ",
		 w0_st_x - h_pw_bp, w0_st_y - v_pw_bp, w0_y_h_fac, w0_y_v_fac);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 "uv_h_fac:%5d, uv_v_fac:%5d\n  y_addr:0x%08x,    uv_addr:0x%08x\n",
		 w0_uv_h_fac, w0_uv_v_fac, vop_readl(vop_dev, WIN0_YRGB_MST),
		 vop_readl(vop_dev, WIN0_CBR_MST));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/* win1 */
	size += snprintf(dsp_buf, 80,
		 "win1:\n  state:%d, fmt:%7s\n  y_vir:%4d,",
		 w1_state, format_w1, w1_vir_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 " dsp_x   :%5d, dsp_y   :%5d\n",
		 w1_dsp_x, w1_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 "  x_st :%4d, y_st  :%4d, ",
		 w1_st_x - h_pw_bp, w1_st_y - v_pw_bp);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 "y_addr:0x%08x\n",
		 vop_readl(vop_dev, WIN1_YRGB_MST));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/* zorder */
	size += snprintf(dsp_buf, 80,
			 ovl ? "win0 on the top of win1\n" :
				"win1 on the top of win0\n");
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	return size;
}

static int vop_fps_mgr(struct rk_lcdc_driver *dev_drv, int fps, bool set)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
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
		ret = clk_set_rate(vop_dev->dclk, dotclk);
	}

	pixclock = div_u64(1000000000000llu, clk_get_rate(vop_dev->dclk));
	vop_dev->pixclock = pixclock;
	dev_drv->pixclock = vop_dev->pixclock;
	fps = rk_fb_calc_fps(screen, pixclock);
	screen->ft = 1000 / fps;	/*one frame time in ms */

	if (set)
		dev_info(dev_drv->dev, "%s:dclk:%lu,fps:%d\n", __func__,
			 clk_get_rate(vop_dev->dclk), fps);

	return fps;
}

static int vop_fb_win_remap(struct rk_lcdc_driver *dev_drv, u16 order)
{
	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (order == FB_DEFAULT_ORDER)
		order = FB0_WIN0_FB1_WIN1_FB2_WIN2_FB3_WIN3_FB4_HWC;
	dev_drv->fb4_win_id = order / 10000;
	dev_drv->fb3_win_id = (order / 1000) % 10;
	dev_drv->fb2_win_id = (order / 100) % 10;
	dev_drv->fb1_win_id = (order / 10) % 10;
	dev_drv->fb0_win_id = order % 10;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return 0;
}

static int vop_get_win_id(struct rk_lcdc_driver *dev_drv, const char *id)
{
	int win_id = 0;

	mutex_lock(&dev_drv->fb_win_id_mutex);
	if (!strcmp(id, "fb0") || !strcmp(id, "fb5"))
		win_id = dev_drv->fb0_win_id;
	else if (!strcmp(id, "fb1") || !strcmp(id, "fb6"))
		win_id = dev_drv->fb1_win_id;
	else if (!strcmp(id, "fb2") || !strcmp(id, "fb7"))
		win_id = dev_drv->fb2_win_id;
	else if (!strcmp(id, "fb3") || !strcmp(id, "fb8"))
		win_id = dev_drv->fb3_win_id;
	else if (!strcmp(id, "fb4") || !strcmp(id, "fb9"))
		win_id = dev_drv->fb4_win_id;
	mutex_unlock(&dev_drv->fb_win_id_mutex);

	return win_id;
}

static int vop_config_done(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	int i;
	struct rk_lcdc_win *win = NULL;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		for (i = 0; i < dev_drv->lcdc_win_num; i++) {
			win = dev_drv->win[i];
			vop_layer_update_regs(vop_dev, win);
		}
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_dpi_open(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_msk_reg(vop_dev, SYS_CTRL0, V_DIRECT_PATH_EN(open));
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_dpi_win_sel(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		vop_msk_reg(vop_dev, SYS_CTRL0,
			    V_DIRECT_PATH_LAYER_SEL(win_id));
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);
	return 0;
}

static int vop_dpi_status(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	int status = 0;

	spin_lock(&vop_dev->reg_lock);

	if (likely(vop_dev->clk_on))
		status = vop_read_bit(vop_dev, SYS_CTRL0, V_DIRECT_PATH_EN(0));

	spin_unlock(&vop_dev->reg_lock);

	return status;
}

static int vop_set_irq_to_cpu(struct rk_lcdc_driver *dev_drv, int enable)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (enable)
		enable_irq(vop_dev->irq);
	else
		disable_irq(vop_dev->irq);
	return 0;
}

static int vop_poll_vblank(struct rk_lcdc_driver *dev_drv)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u32 int_reg;
	int ret;

	if (vop_dev->clk_on && (!dev_drv->suspend_flag)) {
		int_reg = vop_readl(vop_dev, INTR_STATUS);
		if (int_reg & INTR_LINE_FLAG0) {
			vop_dev->driver.frame_time.last_framedone_t =
			    vop_dev->driver.frame_time.framedone_t;
			vop_dev->driver.frame_time.framedone_t = cpu_clock(0);
			vop_mask_writel(vop_dev, INTR_CLEAR, INTR_LINE_FLAG0,
					INTR_LINE_FLAG0);
			ret = RK_LF_STATUS_FC;
		} else {
			ret = RK_LF_STATUS_FR;
		}
	} else {
		ret = RK_LF_STATUS_NC;
	}

	return ret;
}

static int vop_get_dsp_addr(struct rk_lcdc_driver *dev_drv,
			    unsigned int dsp_addr[][4])
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		dsp_addr[0][0] = vop_readl(vop_dev, WIN0_YRGB_MST);
		dsp_addr[1][0] = vop_readl(vop_dev, WIN1_YRGB_MST);
		dsp_addr[2][0] = vop_readl(vop_dev, HWC_MST);
	}
	spin_unlock(&vop_dev->reg_lock);
	return 0;
}

/*
 * a:[-30~0]:
 *	sin_hue = sin(a)*256 +0x100;
 *	cos_hue = cos(a)*256;
 * a:[0~30]
 *	sin_hue = sin(a)*256;
 *	cos_hue = cos(a)*256;
 */
static int vop_get_bcsh_hue(struct rk_lcdc_driver *dev_drv, bcsh_hue_mode mode)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u32 val = 0;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		val = vop_readl(vop_dev, BCSH_H);
		switch (mode) {
		case H_SIN:
			val &= MASK(SIN_HUE);
			val <<= 1;
			break;
		case H_COS:
			val &= MASK(COS_HUE);
			val >>= 8;
			val <<= 1;
			break;
		default:
			break;
		}
	}
	spin_unlock(&vop_dev->reg_lock);

	return val;
}

static int vop_set_bcsh_hue(struct rk_lcdc_driver *dev_drv,
			    int sin_hue, int cos_hue)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u64 val;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		/*
		 * config range is [0, 510), typical value is 256
		 * register range is [0, 255], cos_hue typical value is 128
		 * sin_hue typical value is 0
		 */
		val = V_SIN_HUE(sin_hue >> 1) | V_COS_HUE(cos_hue >> 1);
		vop_msk_reg(vop_dev, BCSH_H, val);
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_set_bcsh_bcs(struct rk_lcdc_driver *dev_drv,
			    bcsh_bcs_mode mode, int value)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u64 val = 0;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		switch (mode) {
		case BRIGHTNESS:
			/*
			 * user range is [0, 255], typical value is 128
			 * register range is [-32, 31], typical value is 0
			 */
			value >>= 2; /* 0-->32-->63 for user, typical is 32 */
			if (value < 0x20)
				value += 0x20;
			else if (value >= 0x20)
				value = value - 0x20;
			val = V_BRIGHTNESS(value);
			break;
		case CONTRAST:
			/*
			 * config range is [0, 510), typical value is 256
			 * register range is [0, 255], typical value is 128
			 */
			value >>= 1;
			val = V_CONTRAST(value);
			break;
		case SAT_CON:
			/*
			 * config range is [0, 1015], typical value is 512
			 * register range is [0, 255], typical value is 128
			 */
			value >>= 2;
			val = V_SAT_CON(value);
			break;
		default:
			break;
		}
		vop_msk_reg(vop_dev, BCSH_BCS, val);
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return val;
}

static int vop_get_bcsh_bcs(struct rk_lcdc_driver *dev_drv, bcsh_bcs_mode mode)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);
	u64 val = 0;

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		val = vop_readl(vop_dev, BCSH_BCS);
		switch (mode) {
		case BRIGHTNESS:
			val &= MASK(BRIGHTNESS);
			if (val >= 0x20)
				val -= 0x20;
			else
				val += 0x20;
			val <<= 2;
			break;
		case CONTRAST:
			val &= MASK(CONTRAST);
			val >>= 8;
			val <<= 1;
			break;
		case SAT_CON:
			val &= MASK(SAT_CON);
			val >>= 16;
			val <<= 2;
			break;
		default:
			break;
		}
	}
	spin_unlock(&vop_dev->reg_lock);
	return val;
}

static int vop_open_bcsh(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	spin_lock(&vop_dev->reg_lock);
	if (likely(vop_dev->clk_on)) {
		if (open) {
			vop_writel(vop_dev, BCSH_BCS,
				   V_BRIGHTNESS(0x00) | V_CONTRAST(0x80) |
				   V_SAT_CON(0x80));
			vop_writel(vop_dev, BCSH_H,
				   V_SIN_HUE(0x00) | V_COS_HUE(0x80));
			vop_msk_reg(vop_dev, BCSH_CTRL, V_BCSH_EN(1) |
				    V_VIDEO_MODE(BCSH_MODE_VIDEO));
			dev_drv->bcsh.enable = 1;
		} else {
			vop_msk_reg(vop_dev, BCSH_CTRL, V_BCSH_EN(0));
			dev_drv->bcsh.enable = 0;
		}
		vop_bcsh_path_sel(dev_drv);
		vop_cfg_done(vop_dev);
	}
	spin_unlock(&vop_dev->reg_lock);

	return 0;
}

static int vop_set_bcsh(struct rk_lcdc_driver *dev_drv, bool enable)
{
	if (!enable || !dev_drv->bcsh.enable) {
		vop_open_bcsh(dev_drv, false);
		return 0;
	}

	if (dev_drv->bcsh.brightness <= 255 ||
	    dev_drv->bcsh.contrast < 510 ||
	    dev_drv->bcsh.sat_con <= 1015 ||
	    (dev_drv->bcsh.sin_hue < 510 && dev_drv->bcsh.cos_hue < 510)) {
		vop_open_bcsh(dev_drv, true);
		if (dev_drv->bcsh.brightness <= 255)
			vop_set_bcsh_bcs(dev_drv, BRIGHTNESS,
					 dev_drv->bcsh.brightness);
		if (dev_drv->bcsh.contrast < 510)
			vop_set_bcsh_bcs(dev_drv, CONTRAST,
					 dev_drv->bcsh.contrast);
		if (dev_drv->bcsh.sat_con <= 1015)
			vop_set_bcsh_bcs(dev_drv, SAT_CON,
					 dev_drv->bcsh.sat_con);
		if (dev_drv->bcsh.sin_hue < 510 &&
		    dev_drv->bcsh.cos_hue < 510)
			vop_set_bcsh_hue(dev_drv, dev_drv->bcsh.sin_hue,
					 dev_drv->bcsh.cos_hue);
	}

	return 0;
}

static int __maybe_unused
vop_dsp_black(struct rk_lcdc_driver *dev_drv, int enable)
{
	struct vop_device *vop_dev = to_vop_dev(dev_drv);

	if (enable) {
		spin_lock(&vop_dev->reg_lock);
		if (likely(vop_dev->clk_on)) {
			vop_msk_reg(vop_dev, DSP_CTRL0, V_DSP_BLACK_EN(1));
			vop_cfg_done(vop_dev);
		}
		spin_unlock(&vop_dev->reg_lock);
	} else {
		spin_lock(&vop_dev->reg_lock);
		if (likely(vop_dev->clk_on)) {
			vop_msk_reg(vop_dev, DSP_CTRL0, V_DSP_BLACK_EN(0));
			vop_cfg_done(vop_dev);
		}
		spin_unlock(&vop_dev->reg_lock);
	}

	return 0;
}

static struct rk_lcdc_drv_ops lcdc_drv_ops = {
	.open = vop_open,
	.win_direct_en = vop_win_direct_en,
	.load_screen = vop_load_screen,
	.get_dspbuf_info = vop_get_dspbuf_info,
	.post_dspbuf = vop_post_dspbuf,
	.set_par = vop_set_par,
	.pan_display = vop_pan_display,
	.direct_set_addr = vop_direct_set_win_addr,
	.blank = vop_blank,
	.ioctl = vop_ioctl,
	.suspend = vop_early_suspend,
	.resume = vop_early_resume,
	.get_win_state = vop_get_win_state,
	.area_support_num = vop_get_area_num,
	.ovl_mgr = vop_ovl_mgr,
	.get_disp_info = vop_get_disp_info,
	.fps_mgr = vop_fps_mgr,
	.fb_get_win_id = vop_get_win_id,
	.fb_win_remap = vop_fb_win_remap,
	.poll_vblank = vop_poll_vblank,
	.dpi_open = vop_dpi_open,
	.dpi_win_sel = vop_dpi_win_sel,
	.dpi_status = vop_dpi_status,
	.get_dsp_addr = vop_get_dsp_addr,
	.set_dsp_bcsh_hue = vop_set_bcsh_hue,
	.set_dsp_bcsh_bcs = vop_set_bcsh_bcs,
	.get_dsp_bcsh_hue = vop_get_bcsh_hue,
	.get_dsp_bcsh_bcs = vop_get_bcsh_bcs,
	.open_bcsh = vop_open_bcsh,
	.set_dsp_lut = vop_set_lut,
	.set_hwc_lut = vop_set_hwc_lut,
	.dump_reg = vop_reg_dump,
	.cfg_done = vop_config_done,
	.set_irq_to_cpu = vop_set_irq_to_cpu,
	/*.dsp_black = vop_dsp_black,*/
	.backlight_close = vop_backlight_close,
	.mmu_en = vop_mmu_enable,
};

static irqreturn_t vop_isr(int irq, void *dev_id)
{
	struct vop_device *vop_dev = (struct vop_device *)dev_id;
	ktime_t timestamp = ktime_get();
	u32 intr_status;
	unsigned long flags;

	spin_lock_irqsave(&vop_dev->irq_lock, flags);

	intr_status = vop_readl(vop_dev, INTR_STATUS);
	vop_mask_writel(vop_dev, INTR_CLEAR, INTR_MASK, intr_status);

	spin_unlock_irqrestore(&vop_dev->irq_lock, flags);

	intr_status &= 0xffff;	/* ignore raw status at 16~32bit */
	/* This is expected for vop iommu irqs, since the irq is shared */
	if (!intr_status)
		return IRQ_NONE;

	if (intr_status & INTR_FS0) {
		timestamp = ktime_get();
		vop_dev->driver.vsync_info.timestamp = timestamp;
		wake_up_interruptible_all(&vop_dev->driver.vsync_info.wait);
		complete(&vop_dev->sync.frmst);
		intr_status &= ~INTR_FS0;
	}

	/* fs1 interrupt occur only when the address is different */
	if (intr_status & INTR_FS1)
		intr_status &= ~INTR_FS1;

	if (intr_status & INTR_ADDR_SAME)
		intr_status &= ~INTR_ADDR_SAME;

	if (intr_status & INTR_DSP_HOLD_VALID) {
		complete(&vop_dev->sync.stdbyfin);
		intr_status &= ~INTR_DSP_HOLD_VALID;
	}

	if (intr_status & INTR_LINE_FLAG0)
		intr_status &= ~INTR_LINE_FLAG0;

	if (intr_status & INTR_LINE_FLAG1)
		intr_status &= ~INTR_LINE_FLAG1;

	if (intr_status & INTR_BUS_ERROR) {
		intr_status &= ~INTR_BUS_ERROR;
		dev_warn_ratelimited(vop_dev->dev, "bus error!");
	}

	if (intr_status & INTR_WIN0_EMPTY) {
		intr_status &= ~INTR_WIN0_EMPTY;
		dev_warn_ratelimited(vop_dev->dev, "intr win0 empty!");
	}

	if (intr_status & INTR_WIN1_EMPTY) {
		intr_status &= ~INTR_WIN1_EMPTY;
		dev_warn_ratelimited(vop_dev->dev, "intr win1 empty!");
	}

	if (intr_status & INTR_DMA_FINISH)
		intr_status &= ~INTR_DMA_FINISH;

	if (intr_status & INTR_MMU_STATUS)
		intr_status &= ~INTR_MMU_STATUS;

	if (intr_status)
		dev_err(vop_dev->dev, "Unknown VOP IRQs: %#02x\n", intr_status);

	return IRQ_HANDLED;
}

#if defined(CONFIG_PM)
static int vop_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int vop_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define vop_suspend NULL
#define vop_resume  NULL
#endif

static int vop_parse_dt(struct vop_device *vop_dev)
{
	struct device_node *np = vop_dev->dev->of_node;
	struct rk_lcdc_driver *dev_drv = &vop_dev->driver;
	int val;

	if (of_property_read_u32(np, "rockchip,prop", &val))
		vop_dev->prop = PRMRY;	/*default set it as primary */
	else
		vop_dev->prop = val;

	if (of_property_read_u32(np, "rockchip,mirror", &val))
		dev_drv->rotate_mode = NO_MIRROR;
	else
		dev_drv->rotate_mode = val;

	if (of_property_read_u32(np, "rockchip,pwr18", &val))
		/*default set it as 3.xv power supply */
		vop_dev->pwr18 = false;
	else
		vop_dev->pwr18 = (val ? true : false);

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

	if (of_property_read_u32(np, "rockchip,iommu-enabled", &val))
		dev_drv->iommu_enabled = 0;
	else
		dev_drv->iommu_enabled = val;

	return 0;
}

static int vop_probe(struct platform_device *pdev)
{
	struct vop_device *vop_dev = NULL;
	struct rk_lcdc_driver *dev_drv;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	int prop;
	int ret = 0;

	/*
	 * if the primary lcdc has not registered ,the extend
	 * lcdc register later
	 */
	of_property_read_u32(np, "rockchip,prop", &prop);
	if (prop == EXTEND) {
		if (!is_prmry_rk_lcdc_registered())
			return -EPROBE_DEFER;
	}

	vop_dev = devm_kzalloc(dev, sizeof(struct vop_device), GFP_KERNEL);
	if (!vop_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, vop_dev);
	vop_dev->dev = dev;
	vop_parse_dt(vop_dev);

	/* enable power domain */
	pm_runtime_enable(dev);

	vop_dev->hclk = devm_clk_get(vop_dev->dev, "hclk_lcdc");
	if (IS_ERR(vop_dev->hclk)) {
		dev_err(vop_dev->dev, "failed to get hclk source\n");
		return PTR_ERR(vop_dev->hclk);
	}

	vop_dev->aclk = devm_clk_get(vop_dev->dev, "aclk_lcdc");
	if (IS_ERR(vop_dev->aclk)) {
		dev_err(vop_dev->dev, "failed to get aclk source\n");
		return PTR_ERR(vop_dev->aclk);
	}
	vop_dev->dclk = devm_clk_get(vop_dev->dev, "dclk_lcdc");
	if (IS_ERR(vop_dev->dclk)) {
		dev_err(vop_dev->dev, "failed to get dclk source\n");
		return PTR_ERR(vop_dev->dclk);
	}

	clk_prepare(vop_dev->hclk);
	clk_prepare(vop_dev->aclk);
	clk_prepare(vop_dev->dclk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vop_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(vop_dev->regs)) {
		ret = PTR_ERR(vop_dev->regs);
		goto err_exit;
	}

	vop_dev->reg_phy_base = res->start;
	vop_dev->len = resource_size(res);
	vop_dev->regsbak = devm_kzalloc(dev, vop_dev->len, GFP_KERNEL);
	if (!vop_dev->regsbak) {
		ret = -ENOMEM;
		goto err_exit;
	}

	vop_dev->hwc_lut_addr_base = (vop_dev->regs + HWC_LUT_ADDR);
	vop_dev->dsp_lut_addr_base = (vop_dev->regs + GAMMA_LUT_ADDR);
	vop_dev->grf_base = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(vop_dev->grf_base)) {
		dev_err(vop_dev->dev, "ERROR!! can't find grf reg property\n");
		vop_dev->grf_base = NULL;
	}

	vop_dev->id = 1;
	dev_set_name(vop_dev->dev, "vop%d", vop_dev->id);
	dev_drv = &vop_dev->driver;
	dev_drv->dev = dev;
	dev_drv->prop = prop;
	dev_drv->id = vop_dev->id;
	dev_drv->ops = &lcdc_drv_ops;
	dev_drv->lcdc_win_num = ARRAY_SIZE(vop_win);
	dev_drv->reserved_fb = 0;
	spin_lock_init(&vop_dev->reg_lock);
	spin_lock_init(&vop_dev->irq_lock);
	init_completion(&vop_dev->sync.stdbyfin);
	init_completion(&vop_dev->sync.frmst);
	vop_dev->sync.stdbyfin_to = 50;	/* timeout 50 ms */
	vop_dev->sync.frmst_to = 50;

	vop_dev->irq = platform_get_irq(pdev, 0);
	if (vop_dev->irq < 0) {
		dev_err(dev, "cannot find IRQ for lcdc%d\n", vop_dev->id);
		ret = vop_dev->irq;
		goto err_exit;
	}

	ret = devm_request_irq(dev, vop_dev->irq, vop_isr,
			       IRQF_SHARED,
			       dev_name(dev), vop_dev);
	if (ret) {
		dev_err(dev, "cannot requeset irq %d - err %d\n",
			vop_dev->irq, ret);
		goto err_exit;
	}

	if (dev_drv->iommu_enabled)
		strcpy(dev_drv->mmu_dts_name, VOPL_IOMMU_COMPATIBLE_NAME);

	ret = rk_fb_register(dev_drv, vop_win, vop_dev->id);
	if (ret < 0) {
		dev_err(dev, "register fb for failed!\n");
		goto err_exit;
	}
	vop_dev->screen = dev_drv->screen0;
	dev_info(dev, "lcdc%d probe ok, iommu %s\n",
		 vop_dev->id, dev_drv->iommu_enabled ? "enabled" : "disabled");

	return 0;

err_exit:
	clk_unprepare(vop_dev->dclk);
	clk_unprepare(vop_dev->aclk);
	clk_unprepare(vop_dev->hclk);
	pm_runtime_disable(dev);

	return ret;
}

static int vop_remove(struct platform_device *pdev)
{
	return 0;
}

static void vop_shutdown(struct platform_device *pdev)
{
	struct vop_device *vop_dev = platform_get_drvdata(pdev);
	struct rk_lcdc_driver *dev_drv = &vop_dev->driver;

	dev_drv->suspend_flag = 1;
	smp_wmb();
	flush_kthread_worker(&dev_drv->update_regs_worker);
	kthread_stop(dev_drv->update_regs_thread);

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
		dev_drv->trsm_ops->disable();

	vop_deinit(vop_dev);
	rk_disp_pwr_disable(dev_drv);
}

#if defined(CONFIG_OF)
static const struct of_device_id vop_dt_ids[] = {
	{.compatible = "rockchip,rk3366-lcdc-lite",},
	{}
};
#endif

static struct platform_driver vop_driver = {
	.probe = vop_probe,
	.remove = vop_remove,
	.driver = {
		   .name = "rk-vop-lite",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(vop_dt_ids),
		   },
	.suspend = vop_suspend,
	.resume = vop_resume,
	.shutdown = vop_shutdown,
};

static int __init vop_module_init(void)
{
	return platform_driver_register(&vop_driver);
}

static void __exit vop_module_exit(void)
{
	platform_driver_unregister(&vop_driver);
}

fs_initcall(vop_module_init);
module_exit(vop_module_exit);
