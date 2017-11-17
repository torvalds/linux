/*
 * drivers/video/rockchip/lcdc/rk3368_lcdc.c
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
#include <linux/uaccess.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include <dt-bindings/clock/rk_system_status.h>

#include "rk3368_lcdc.h"

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif
/*#define CONFIG_RK_FPGA 1*/

static int dbg_thresd;
module_param(dbg_thresd, int, S_IRUGO | S_IWUSR);

#define DBG(level, x...) do {			\
	if (unlikely(dbg_thresd >= level))	\
		pr_info(x);\
	} while (0)

#define EARLY_TIME 500 /*us*/
static struct rk_lcdc_win lcdc_win[] = {
	[0] = {
		.name = "win0",
		.id = 0,
		.property.feature = SUPPORT_WIN_IDENTIFY | SUPPORT_SCALE |
					SUPPORT_YUV,
		.property.max_input_x = 4096,
		.property.max_input_y = 2304
		},
	[1] = {
		.name = "win1",
		.id = 1,
		.property.feature = SUPPORT_WIN_IDENTIFY | SUPPORT_SCALE |
					SUPPORT_YUV,
		.property.max_input_x = 4096,
		.property.max_input_y = 2304
		},
	[2] = {
		.name = "win2",
		.id = 2,
		.property.feature = SUPPORT_WIN_IDENTIFY | SUPPORT_MULTI_AREA,
		.property.max_input_x = 4096,
		.property.max_input_y = 2304
		},
	[3] = {
		.name = "win3",
		.id = 3,
		.property.feature = SUPPORT_WIN_IDENTIFY | SUPPORT_MULTI_AREA,
		.property.max_input_x = 4096,
		.property.max_input_y = 2304
		},
	[4] = {
		.name = "hwc",
		.id = 4,
		.property.feature = SUPPORT_WIN_IDENTIFY | SUPPORT_HWC_LAYER,
		.property.max_input_x = 128,
		.property.max_input_y = 128
		},
};

static int rk3368_lcdc_set_bcsh(struct rk_lcdc_driver *dev_drv, bool enable);

/*#define WAIT_FOR_SYNC 1*/
u32 rk3368_get_hard_ware_vskiplines(u32 srch, u32 dsth)
{
	u32 vscalednmult;

	if (srch >= (u32) (4 * dsth * MIN_SCALE_FACTOR_AFTER_VSKIP))
		vscalednmult = 4;
	else if (srch >= (u32) (2 * dsth * MIN_SCALE_FACTOR_AFTER_VSKIP))
		vscalednmult = 2;
	else
		vscalednmult = 1;

	return vscalednmult;
}


static int rk3368_set_cabc_lut(struct rk_lcdc_driver *dev_drv, int *cabc_lut)
{
	int i;
	int __iomem *c;
	u32 v;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	lcdc_msk_reg(lcdc_dev, CABC_CTRL1, m_CABC_LUT_EN,
		     v_CABC_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	mdelay(25);
	for (i = 0; i < 128; i++) {
		v = cabc_lut[i];
		c = lcdc_dev->cabc_lut_addr_base + i;
		writel_relaxed(v, c);
	}
	lcdc_msk_reg(lcdc_dev, CABC_CTRL1, m_CABC_LUT_EN,
		     v_CABC_LUT_EN(1));
	return 0;
}


static int rk3368_lcdc_set_lut(struct rk_lcdc_driver *dev_drv, int *dsp_lut)
{
	int i;
	int __iomem *c;
	u32 v;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN,
		     v_DSP_LUT_EN(0));
	lcdc_cfg_done(lcdc_dev);
	mdelay(25);
	for (i = 0; i < 256; i++) {
		v = dsp_lut[i];
		c = lcdc_dev->dsp_lut_addr_base + i;
		writel_relaxed(v, c);
	}
	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN,
		     v_DSP_LUT_EN(1));

	return 0;
}

static int rk3368_lcdc_clk_enable(struct lcdc_device *lcdc_dev)
{
#ifdef CONFIG_RK_FPGA
	lcdc_dev->clk_on = 1;
	return 0;
#endif
	if (!lcdc_dev->clk_on) {
		clk_prepare_enable(lcdc_dev->hclk);
		clk_prepare_enable(lcdc_dev->dclk);
		clk_prepare_enable(lcdc_dev->aclk);
		if (lcdc_dev->pd)
			clk_prepare_enable(lcdc_dev->pd);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		pm_runtime_get_sync(lcdc_dev->dev);
#endif
		spin_lock(&lcdc_dev->reg_lock);
		lcdc_dev->clk_on = 1;
		spin_unlock(&lcdc_dev->reg_lock);
	}

	return 0;
}

static int rk3368_lcdc_clk_disable(struct lcdc_device *lcdc_dev)
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
		if (lcdc_dev->pd)
			clk_disable_unprepare(lcdc_dev->pd);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		pm_runtime_put(lcdc_dev->dev);
#endif
		clk_disable_unprepare(lcdc_dev->dclk);
		clk_disable_unprepare(lcdc_dev->hclk);
		clk_disable_unprepare(lcdc_dev->aclk);
	}

	return 0;
}

static int __maybe_unused
	rk3368_lcdc_disable_irq(struct lcdc_device *lcdc_dev)
{
	u32 mask, val;
	u32 intr_en_reg, intr_clr_reg;

	if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
		intr_clr_reg = INTR_CLEAR_RK3366;
		intr_en_reg = INTR_EN_RK3366;
	} else {
		intr_clr_reg = INTR_CLEAR_RK3368;
		intr_en_reg = INTR_EN_RK3368;
	}

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		mask = m_FS_INTR_EN | m_FS_NEW_INTR_EN |
		    m_ADDR_SAME_INTR_EN | m_LINE_FLAG0_INTR_EN |
		    m_LINE_FLAG1_INTR_EN | m_BUS_ERROR_INTR_EN |
		    m_WIN0_EMPTY_INTR_EN | m_WIN1_EMPTY_INTR_EN |
		    m_WIN2_EMPTY_INTR_EN | m_WIN3_EMPTY_INTR_EN |
		    m_HWC_EMPTY_INTR_EN | m_POST_BUF_EMPTY_INTR_EN |
		    m_PWM_GEN_INTR_EN | m_DSP_HOLD_VALID_INTR_EN;
		val = v_FS_INTR_EN(0) | v_FS_NEW_INTR_EN(0) |
		    v_ADDR_SAME_INTR_EN(0) |
		    v_LINE_FLAG0_INTR_EN(0) | v_LINE_FLAG1_INTR_EN(0) |
		    v_BUS_ERROR_INTR_EN(0) | v_WIN0_EMPTY_INTR_EN(0) |
		    v_WIN1_EMPTY_INTR_EN(0) | v_WIN2_EMPTY_INTR_EN(0) |
		    v_WIN3_EMPTY_INTR_EN(0) | v_HWC_EMPTY_INTR_EN(0) |
		    v_POST_BUF_EMPTY_INTR_EN(0) |
		    v_PWM_GEN_INTR_EN(0) | v_DSP_HOLD_VALID_INTR_EN(0);
		lcdc_msk_reg(lcdc_dev, intr_en_reg, mask, val);

		mask = m_FS_INTR_CLR | m_FS_NEW_INTR_CLR |
		    m_ADDR_SAME_INTR_CLR | m_LINE_FLAG0_INTR_CLR |
		    m_LINE_FLAG1_INTR_CLR | m_BUS_ERROR_INTR_CLR |
		    m_WIN0_EMPTY_INTR_CLR | m_WIN1_EMPTY_INTR_CLR |
		    m_WIN2_EMPTY_INTR_CLR | m_WIN3_EMPTY_INTR_CLR |
		    m_HWC_EMPTY_INTR_CLR | m_POST_BUF_EMPTY_INTR_CLR |
		    m_PWM_GEN_INTR_CLR | m_DSP_HOLD_VALID_INTR_CLR;
		val = v_FS_INTR_CLR(1) | v_FS_NEW_INTR_CLR(1) |
		    v_ADDR_SAME_INTR_CLR(1) |
		    v_LINE_FLAG0_INTR_CLR(1) | v_LINE_FLAG1_INTR_CLR(1) |
		    v_BUS_ERROR_INTR_CLR(1) | v_WIN0_EMPTY_INTR_CLR(1) |
		    v_WIN1_EMPTY_INTR_CLR(1) | v_WIN2_EMPTY_INTR_CLR(1) |
		    v_WIN3_EMPTY_INTR_CLR(1) | v_HWC_EMPTY_INTR_CLR(1) |
		    v_POST_BUF_EMPTY_INTR_CLR(1) |
		    v_PWM_GEN_INTR_CLR(1) | v_DSP_HOLD_VALID_INTR_CLR(1);
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, mask, val);
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_unlock(&lcdc_dev->reg_lock);
	}
	mdelay(1);
	return 0;
}

static int rk3368_lcdc_reg_dump(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int *cbase = (int *)lcdc_dev->regs;
	int *regsbak = (int *)lcdc_dev->regsbak;
	int i, j, val;
	char dbg_message[30];
	char buf[10];

	pr_info("lcd back up reg:\n");
	memset(dbg_message, 0, sizeof(dbg_message));
	memset(buf, 0, sizeof(buf));
	for (i = 0; i <= (0x200 >> 4); i++) {
		val = sprintf(dbg_message, "0x%04x: ", i * 16);
		for (j = 0; j < 4; j++) {
			val = sprintf(buf, "%08x  ", *(regsbak + i * 4 + j));
			strcat(dbg_message, buf);
		}
		pr_info("%s\n", dbg_message);
		memset(dbg_message, 0, sizeof(dbg_message));
		memset(buf, 0, sizeof(buf));
	}

	pr_info("lcdc reg:\n");
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
	spin_unlock(&lcdc_dev->reg_lock);				\
	return 0;							\
}

WIN_EN(0);
WIN_EN(1);
WIN_EN(2);
WIN_EN(3);
/*enable/disable win directly*/
static int rk3368_lcdc_win_direct_en(struct rk_lcdc_driver *drv,
				     int win_id, int en)
{
	struct lcdc_device *lcdc_dev =
	    container_of(drv, struct lcdc_device, driver);
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
	lcdc_writel(lcdc_dev, WIN##id##_YRGB_MST, addr);	\
	msk =  m_WIN##id##_EN;				\
	val  =  v_WIN0_EN(1);				\
	lcdc_msk_reg(lcdc_dev, WIN##id##_CTRL0, msk, val);	\
	lcdc_cfg_done(lcdc_dev);			\
	spin_unlock(&lcdc_dev->reg_lock);		\
	return 0;					\
}

SET_WIN_ADDR(0);
SET_WIN_ADDR(1);
int rk3368_lcdc_direct_set_win_addr(struct rk_lcdc_driver *dev_drv,
				    int win_id, u32 addr)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
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
	u32 st_x = 0, st_y = 0;
	struct rk_lcdc_win *win0 = lcdc_dev->driver.win[0];

	spin_lock(&lcdc_dev->reg_lock);
	for (reg = 0; reg < SCAN_LINE_NUM; reg += 4) {
		val = lcdc_readl_backup(lcdc_dev, reg);
		switch (reg) {
		case VERSION_INFO:
			lcdc_dev->soc_type = val;
			break;
		case WIN0_ACT_INFO:
			win0->area[0].xact = (val & m_WIN0_ACT_WIDTH) + 1;
			win0->area[0].yact =
			    ((val & m_WIN0_ACT_HEIGHT) >> 16) + 1;
			break;
		case WIN0_DSP_INFO:
			win0->area[0].xsize = (val & m_WIN0_DSP_WIDTH) + 1;
			win0->area[0].ysize =
			    ((val & m_WIN0_DSP_HEIGHT) >> 16) + 1;
			break;
		case WIN0_DSP_ST:
			st_x = val & m_WIN0_DSP_XST;
			st_y = (val & m_WIN0_DSP_YST) >> 16;
			win0->area[0].xpos = st_x - h_pw_bp;
			win0->area[0].ypos = st_y - v_pw_bp;
			break;
		case WIN0_CTRL0:
			win0->state = val & m_WIN0_EN;
			win0->area[0].fmt_cfg = (val & m_WIN0_DATA_FMT) >> 1;
			win0->fmt_10 = (val & m_WIN0_FMT_10) >> 4;
			win0->area[0].format = win0->area[0].fmt_cfg;
			break;
		case WIN0_VIR:
			win0->area[0].y_vir_stride = val & m_WIN0_VIR_STRIDE;
			win0->area[0].uv_vir_stride =
			    (val & m_WIN0_VIR_STRIDE_UV) >> 16;
			if (win0->area[0].format == ARGB888)
				win0->area[0].xvir = win0->area[0].y_vir_stride;
			else if (win0->area[0].format == RGB888)
				win0->area[0].xvir =
				    win0->area[0].y_vir_stride * 4 / 3;
			else if (win0->area[0].format == RGB565)
				win0->area[0].xvir =
				    2 * win0->area[0].y_vir_stride;
			else	/* YUV */
				win0->area[0].xvir =
				    4 * win0->area[0].y_vir_stride;
			break;
		case WIN0_YRGB_MST:
			win0->area[0].smem_start = val;
			break;
		case WIN0_CBR_MST:
			win0->area[0].cbr_start = val;
			break;
		case DSP_VACT_ST_END:
			if (support_uboot_display()) {
				screen->mode.yres =
				(val & 0x1fff) - ((val >> 16) & 0x1fff);
				win0->area[0].ypos =
				st_y - ((val >> 16) & 0x1fff);
			}
			break;
		case DSP_HACT_ST_END:
			if (support_uboot_display()) {
				screen->mode.xres =
				(val & 0x1fff) - ((val >> 16) & 0x1fff);
				win0->area[0].xpos =
				st_x - ((val >> 16) & 0x1fff);
			}
			break;
		default:
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
}

/********do basic init*********/
static int rk3368_lcdc_pre_init(struct rk_lcdc_driver *dev_drv)
{
	u32 mask, val;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	if (lcdc_dev->pre_init)
		return 0;

	lcdc_dev->hclk = devm_clk_get(lcdc_dev->dev, "hclk_lcdc");
	lcdc_dev->aclk = devm_clk_get(lcdc_dev->dev, "aclk_lcdc");
	lcdc_dev->dclk = devm_clk_get(lcdc_dev->dev, "dclk_lcdc");
	if ((IS_ERR(lcdc_dev->aclk)) || (IS_ERR(lcdc_dev->dclk)) ||
	    (IS_ERR(lcdc_dev->hclk))) {
		dev_err(lcdc_dev->dev, "failed to get lcdc%d clk source\n",
			lcdc_dev->id);
	}

	lcdc_dev->pd = devm_clk_get(lcdc_dev->dev, "pd_lcdc");
	if (IS_ERR(lcdc_dev->pd)) {
		dev_err(lcdc_dev->dev, "failed to get lcdc%d pdclk source\n",
			lcdc_dev->id);
		lcdc_dev->pd = NULL;
	}

	if (!support_uboot_display())
		rk_disp_pwr_enable(dev_drv);
	rk3368_lcdc_clk_enable(lcdc_dev);

	/*backup reg config at uboot */
	lcdc_read_reg_defalut_cfg(lcdc_dev);
	if (lcdc_dev->soc_type == VOP_FULL_RK3366)
		lcdc_grf_writel(lcdc_dev->grf_base, RK3366_GRF_IO_VSEL,
				RK3366_GRF_VOP_IOVOL_SEL(lcdc_dev->pwr18));
	else
		lcdc_grf_writel(lcdc_dev->pmugrf_base,
				PMUGRF_SOC_CON0_VOP,
				RK3368_GRF_VOP_IOVOL_SEL(lcdc_dev->pwr18));

	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE0_0,0x15110903);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE0_1,0x00030911);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE1_0,0x1a150b04);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE1_1,0x00040b15);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE2_0,0x15110903);
	lcdc_writel(lcdc_dev,CABC_GAUSS_LINE2_1,0x00030911);

	lcdc_writel(lcdc_dev, FRC_LOWER01_0, 0x12844821);
	lcdc_writel(lcdc_dev, FRC_LOWER01_1, 0x21488412);
	lcdc_writel(lcdc_dev, FRC_LOWER10_0, 0xa55a9696);
	lcdc_writel(lcdc_dev, FRC_LOWER10_1, 0x5aa56969);
	lcdc_writel(lcdc_dev, FRC_LOWER11_0, 0xdeb77deb);
	lcdc_writel(lcdc_dev, FRC_LOWER11_1, 0xed7bb7de);

	mask = m_AUTO_GATING_EN;
	val = v_AUTO_GATING_EN(0);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
	mask = m_DITHER_UP_EN;
	val = v_DITHER_UP_EN(1);
	lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
	lcdc_cfg_done(lcdc_dev);
	/*disable win0 to workaround iommu pagefault */
	/*if (dev_drv->iommu_enabled) */
	/*      win0_enable(lcdc_dev, 0); */
	lcdc_dev->pre_init = true;

	return 0;
}

static void rk3368_lcdc_deint(struct lcdc_device *lcdc_dev)
{
	u32 mask, val;

	if (lcdc_dev->clk_on) {
		rk3368_lcdc_disable_irq(lcdc_dev);
		spin_lock(&lcdc_dev->reg_lock);
		mask = m_WIN0_EN;
		val = v_WIN0_EN(0);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask, val);
		lcdc_msk_reg(lcdc_dev, WIN1_CTRL0, mask, val);

		mask = m_WIN2_EN | m_WIN2_MST0_EN |
			m_WIN2_MST1_EN |
			m_WIN2_MST2_EN | m_WIN2_MST3_EN;
		val = v_WIN2_EN(0) | v_WIN2_MST0_EN(0) |
			v_WIN2_MST1_EN(0) |
			v_WIN2_MST2_EN(0) | v_WIN2_MST3_EN(0);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL0, mask, val);
		lcdc_msk_reg(lcdc_dev, WIN3_CTRL0, mask, val);
		lcdc_cfg_done(lcdc_dev);
		spin_unlock(&lcdc_dev->reg_lock);
		mdelay(50);
	}
}

static int rk3368_lcdc_post_cfg(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u16 x_res = screen->mode.xres;
	u16 y_res = screen->mode.yres;
	u32 mask, val;
	u16 h_total, v_total;
	u16 post_hsd_en, post_vsd_en;
	u16 post_dsp_hact_st, post_dsp_hact_end;
	u16 post_dsp_vact_st, post_dsp_vact_end;
	u16 post_dsp_vact_st_f1, post_dsp_vact_end_f1;
	u16 post_h_fac, post_v_fac;

	screen->post_dsp_stx = x_res * (100 - dev_drv->overscan.left) / 200;
	screen->post_dsp_sty = y_res * (100 - dev_drv->overscan.top) / 200;
	screen->post_xsize = x_res *
	    (dev_drv->overscan.left + dev_drv->overscan.right) / 200;
	screen->post_ysize = y_res *
	    (dev_drv->overscan.top + dev_drv->overscan.bottom) / 200;

	h_total = screen->mode.hsync_len + screen->mode.left_margin +
	    x_res + screen->mode.right_margin;
	v_total = screen->mode.vsync_len + screen->mode.upper_margin +
	    y_res + screen->mode.lower_margin;

	if (screen->post_dsp_stx + screen->post_xsize > x_res) {
		dev_warn(lcdc_dev->dev, "post:stx[%d]+xsize[%d]>x_res[%d]\n",
			 screen->post_dsp_stx, screen->post_xsize, x_res);
		screen->post_dsp_stx = x_res - screen->post_xsize;
	}
	if (screen->x_mirror == 0) {
		post_dsp_hact_st = screen->post_dsp_stx +
		    screen->mode.hsync_len + screen->mode.left_margin;
		post_dsp_hact_end = post_dsp_hact_st + screen->post_xsize;
	} else {
		post_dsp_hact_end = h_total - screen->mode.right_margin -
		    screen->post_dsp_stx;
		post_dsp_hact_st = post_dsp_hact_end - screen->post_xsize;
	}
	if ((screen->post_xsize < x_res) && (screen->post_xsize != 0)) {
		post_hsd_en = 1;
		post_h_fac =
		    GET_SCALE_FACTOR_BILI_DN(x_res, screen->post_xsize);
	} else {
		post_hsd_en = 0;
		post_h_fac = 0x1000;
	}

	if (screen->post_dsp_sty + screen->post_ysize > y_res) {
		dev_warn(lcdc_dev->dev, "post:sty[%d]+ysize[%d]> y_res[%d]\n",
			 screen->post_dsp_sty, screen->post_ysize, y_res);
		screen->post_dsp_sty = y_res - screen->post_ysize;
	}

	if ((screen->post_ysize < y_res) && (screen->post_ysize != 0)) {
		post_vsd_en = 1;
		post_v_fac = GET_SCALE_FACTOR_BILI_DN(y_res,
						      screen->post_ysize);
	} else {
		post_vsd_en = 0;
		post_v_fac = 0x1000;
	}

	if (screen->mode.vmode & FB_VMODE_INTERLACED) {
		post_dsp_vact_st = screen->post_dsp_sty / 2 +
					screen->mode.vsync_len +
					screen->mode.upper_margin;
		post_dsp_vact_end = post_dsp_vact_st +
					screen->post_ysize / 2;

		post_dsp_vact_st_f1 = screen->mode.vsync_len +
				      screen->mode.upper_margin +
				      y_res/2 +
				      screen->mode.lower_margin +
				      screen->mode.vsync_len +
				      screen->mode.upper_margin +
				      screen->post_dsp_sty / 2 +
				      1;
		post_dsp_vact_end_f1 = post_dsp_vact_st_f1 +
					screen->post_ysize/2;
	} else {
		if (screen->y_mirror == 0) {
			post_dsp_vact_st = screen->post_dsp_sty +
			    screen->mode.vsync_len +
			    screen->mode.upper_margin;
			post_dsp_vact_end = post_dsp_vact_st +
				screen->post_ysize;
		} else {
			post_dsp_vact_end = v_total -
				screen->mode.lower_margin -
			    screen->post_dsp_sty;
			post_dsp_vact_st = post_dsp_vact_end -
				screen->post_ysize;
		}
		post_dsp_vact_st_f1 = 0;
		post_dsp_vact_end_f1 = 0;
	}
	DBG(1, "post:xsize=%d,ysize=%d,xpos=%d",
	    screen->post_xsize, screen->post_ysize, screen->xpos);
	DBG(1, ",ypos=%d,hsd_en=%d,h_fac=%d,vsd_en=%d,v_fac=%d\n",
	    screen->ypos, post_hsd_en, post_h_fac, post_vsd_en, post_v_fac);
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

static int rk3368_lcdc_clr_key_cfg(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win;
	u32 colorkey_r, colorkey_g, colorkey_b;
	int i, key_val;

	for (i = 0; i < 4; i++) {
		win = dev_drv->win[i];
		key_val = win->color_key_val;
		colorkey_r = (key_val & 0xff) << 2;
		colorkey_g = ((key_val >> 8) & 0xff) << 12;
		colorkey_b = ((key_val >> 16) & 0xff) << 22;
		/*color key dither 565/888->aaa */
		key_val = colorkey_r | colorkey_g | colorkey_b;
		switch (i) {
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
			pr_info("%s:un support win num:%d\n",
				__func__, i);
			break;
		}
	}
	return 0;
}

static int rk3368_lcdc_alpha_cfg(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	struct alpha_config alpha_config;
	u32 mask, val;
	int ppixel_alpha = 0, global_alpha = 0, i;
	u32 src_alpha_ctl = 0, dst_alpha_ctl = 0;

	memset(&alpha_config, 0, sizeof(struct alpha_config));
	for (i = 0; i < win->area_num; i++) {
		ppixel_alpha |= ((win->area[i].format == ARGB888) ||
				 (win->area[i].format == FBDC_ARGB_888) ||
				 (win->area[i].format == FBDC_ABGR_888) ||
				 (win->area[i].format == ABGR888)) ? 1 : 0;
	}
	global_alpha = (win->g_alpha_val == 0) ? 0 : 1;
	alpha_config.src_global_alpha_val = win->g_alpha_val;
	win->alpha_mode = AB_SRC_OVER;
	switch (win->alpha_mode) {
	case AB_USER_DEFINE:
		break;
	case AB_CLEAR:
		alpha_config.src_factor_mode = AA_ZERO;
		alpha_config.dst_factor_mode = AA_ZERO;
		break;
	case AB_SRC:
		alpha_config.src_factor_mode = AA_ONE;
		alpha_config.dst_factor_mode = AA_ZERO;
		break;
	case AB_DST:
		alpha_config.src_factor_mode = AA_ZERO;
		alpha_config.dst_factor_mode = AA_ONE;
		break;
	case AB_SRC_OVER:
		alpha_config.src_color_mode = AA_SRC_PRE_MUL;
		if (global_alpha)
			alpha_config.src_factor_mode = AA_SRC_GLOBAL;
		else
			alpha_config.src_factor_mode = AA_ONE;
		alpha_config.dst_factor_mode = AA_SRC_INVERSE;
		break;
	case AB_DST_OVER:
		alpha_config.src_color_mode = AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode = AA_SRC_INVERSE;
		alpha_config.dst_factor_mode = AA_ONE;
		break;
	case AB_SRC_IN:
		alpha_config.src_color_mode = AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode = AA_SRC;
		alpha_config.dst_factor_mode = AA_ZERO;
		break;
	case AB_DST_IN:
		alpha_config.src_factor_mode = AA_ZERO;
		alpha_config.dst_factor_mode = AA_SRC;
		break;
	case AB_SRC_OUT:
		alpha_config.src_color_mode = AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode = AA_SRC_INVERSE;
		alpha_config.dst_factor_mode = AA_ZERO;
		break;
	case AB_DST_OUT:
		alpha_config.src_factor_mode = AA_ZERO;
		alpha_config.dst_factor_mode = AA_SRC_INVERSE;
		break;
	case AB_SRC_ATOP:
		alpha_config.src_color_mode = AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode = AA_SRC;
		alpha_config.dst_factor_mode = AA_SRC_INVERSE;
		break;
	case AB_DST_ATOP:
		alpha_config.src_color_mode = AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode = AA_SRC_INVERSE;
		alpha_config.dst_factor_mode = AA_SRC;
		break;
	case XOR:
		alpha_config.src_color_mode = AA_SRC_PRE_MUL;
		alpha_config.src_factor_mode = AA_SRC_INVERSE;
		alpha_config.dst_factor_mode = AA_SRC_INVERSE;
		break;
	case AB_SRC_OVER_GLOBAL:
		alpha_config.src_global_alpha_mode = AA_PER_PIX_GLOBAL;
		alpha_config.src_color_mode = AA_SRC_NO_PRE_MUL;
		alpha_config.src_factor_mode = AA_SRC_GLOBAL;
		alpha_config.dst_factor_mode = AA_SRC_INVERSE;
		break;
	default:
		pr_err("alpha mode error\n");
		break;
	}
	if ((ppixel_alpha == 1) && (global_alpha == 1))
		alpha_config.src_global_alpha_mode = AA_PER_PIX_GLOBAL;
	else if (ppixel_alpha == 1)
		alpha_config.src_global_alpha_mode = AA_PER_PIX;
	else if (global_alpha == 1)
		alpha_config.src_global_alpha_mode = AA_GLOBAL;
	else
		dev_warn(lcdc_dev->dev, "alpha_en should be 0\n");
	alpha_config.src_alpha_mode = AA_STRAIGHT;
	alpha_config.src_alpha_cal_m0 = AA_NO_SAT;

	switch (win_id) {
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
	case 4:
		src_alpha_ctl = 0x160;
		dst_alpha_ctl = 0x164;
		break;
	}
	mask = m_WIN0_DST_FACTOR_M0;
	val = v_WIN0_DST_FACTOR_M0(alpha_config.dst_factor_mode);
	lcdc_msk_reg(lcdc_dev, dst_alpha_ctl, mask, val);
	mask = m_WIN0_SRC_ALPHA_EN | m_WIN0_SRC_COLOR_M0 |
	    m_WIN0_SRC_ALPHA_M0 | m_WIN0_SRC_BLEND_M0 |
	    m_WIN0_SRC_ALPHA_CAL_M0 | m_WIN0_SRC_FACTOR_M0 |
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

static int rk3368_lcdc_area_xst(struct rk_lcdc_win *win, int area_num)
{
	struct rk_lcdc_win_area area_temp;
	int i, j;

	for (i = 0; i < area_num; i++) {
		for (j = i + 1; j < area_num; j++) {
			if (win->area[i].dsp_stx >  win->area[j].dsp_stx) {
				memcpy(&area_temp, &win->area[i],
				       sizeof(struct rk_lcdc_win_area));
				memcpy(&win->area[i], &win->area[j],
				       sizeof(struct rk_lcdc_win_area));
				memcpy(&win->area[j], &area_temp,
				       sizeof(struct rk_lcdc_win_area));
			}
		}
	}

	return 0;
}

static int __maybe_unused
	rk3368_lcdc_area_swap(struct rk_lcdc_win *win, int area_num)
{
	struct rk_lcdc_win_area area_temp;

	switch (area_num) {
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
		pr_info("un supported area num!\n");
		break;
	}
	return 0;
}

static int __maybe_unused
rk3368_win_area_check_var(int win_id, int area_num,
			  struct rk_lcdc_win_area *area_pre,
			  struct rk_lcdc_win_area *area_now)
{
	if ((area_pre->xpos > area_now->xpos) ||
	    ((area_pre->xpos + area_pre->xsize > area_now->xpos) &&
	     (area_pre->ypos + area_pre->ysize > area_now->ypos))) {
		area_now->state = 0;
		pr_err("win[%d]:\n"
		       "area_pre[%d]:xpos[%d],xsize[%d],ypos[%d],ysize[%d]\n"
		       "area_now[%d]:xpos[%d],xsize[%d],ypos[%d],ysize[%d]\n",
		       win_id,
		       area_num - 1, area_pre->xpos, area_pre->xsize,
		       area_pre->ypos, area_pre->ysize,
		       area_num, area_now->xpos, area_now->xsize,
		       area_now->ypos, area_now->ysize);
		return -EINVAL;
	}
	return 0;
}

static int __maybe_unused rk3368_get_fbdc_idle(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val, i;

	for (i = 0; i < 100; i++) {
		val = lcdc_readl(lcdc_dev, IFBDC_DEBUG0);
		val &= m_DBG_IFBDC_IDLE;
		if (val)
			continue;
		else
			mdelay(10);
	};
	return val;
}

static int rk3368_fbdc_reg_update(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	u32 mask, val;

	if (lcdc_dev->soc_type != VOP_FULL_RK3368) {
		pr_err("soc: 0x%08x not support FBDC\n", lcdc_dev->soc_type);
		return 0;
	}
	mask = m_IFBDC_CTRL_FBDC_COR_EN |
	    m_IFBDC_CTRL_FBDC_WIN_SEL | m_IFBDC_CTRL_FBDC_ROTATION_MODE |
	    m_IFBDC_CTRL_FBDC_FMT | m_IFBDC_CTRL_WIDTH_RATIO;
	val = v_IFBDC_CTRL_FBDC_COR_EN(win->area[0].fbdc_cor_en) |
	    v_IFBDC_CTRL_FBDC_WIN_SEL(win->id) |
	    v_IFBDC_CTRL_FBDC_ROTATION_MODE((win->xmirror &&
					     win->ymirror) << 1) |
	    v_IFBDC_CTRL_FBDC_FMT(win->area[0].fbdc_fmt_cfg) |
	    v_IFBDC_CTRL_WIDTH_RATIO(win->area[0].fbdc_dsp_width_ratio);
	lcdc_msk_reg(lcdc_dev, IFBDC_CTRL, mask, val);

	mask = m_IFBDC_TILES_NUM;
	val = v_IFBDC_TILES_NUM(win->area[0].fbdc_num_tiles);
	lcdc_msk_reg(lcdc_dev, IFBDC_TILES_NUM, mask, val);

	mask = m_IFBDC_MB_SIZE_WIDTH | m_IFBDC_MB_SIZE_HEIGHT;
	val = v_IFBDC_MB_SIZE_WIDTH(win->area[0].fbdc_mb_width) |
	    v_IFBDC_MB_SIZE_HEIGHT(win->area[0].fbdc_mb_height);
	lcdc_msk_reg(lcdc_dev, IFBDC_MB_SIZE, mask, val);

	mask = m_IFBDC_CMP_INDEX_INIT;
	val = v_IFBDC_CMP_INDEX_INIT(win->area[0].fbdc_cmp_index_init);
	lcdc_msk_reg(lcdc_dev, IFBDC_CMP_INDEX_INIT, mask, val);

	mask = m_IFBDC_MB_VIR_WIDTH;
	val = v_IFBDC_MB_VIR_WIDTH(win->area[0].fbdc_mb_vir_width);
	lcdc_msk_reg(lcdc_dev, IFBDC_MB_VIR_WIDTH, mask, val);

	return 0;
}

static int rk3368_init_fbdc_config(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	u8 fbdc_dsp_width_ratio = 0;
	u16 fbdc_mb_vir_width = 0, fbdc_mb_vir_height = 0;
	u16 fbdc_mb_width = 0, fbdc_mb_height = 0;
	u16 fbdc_mb_xst = 0, fbdc_mb_yst = 0, fbdc_num_tiles = 0;
	u16 fbdc_cmp_index_init = 0;
	u8 mb_w_size = 0, mb_h_size = 0;
	struct rk_screen *screen = dev_drv->cur_screen;

	if (screen->mode.flag & FB_VMODE_INTERLACED) {
		dev_err(lcdc_dev->dev, "unsupport fbdc+interlace!\n");
		return 0;
	}

	if (lcdc_dev->soc_type != VOP_FULL_RK3368) {
		pr_err("soc: 0x%08x not support FBDC\n", lcdc_dev->soc_type);
		return 0;
	}
	switch (win->area[0].fmt_cfg) {
	case VOP_FORMAT_ARGB888:
		fbdc_dsp_width_ratio = 0;
		mb_w_size = 16;
		break;
	case VOP_FORMAT_RGB888:
		fbdc_dsp_width_ratio = 0;
		mb_w_size = 16;
		break;
	case VOP_FORMAT_RGB565:
		fbdc_dsp_width_ratio = 1;
		mb_w_size = 32;
		break;
	default:
		dev_err(lcdc_dev->dev,
			"in fbdc mode,unsupport fmt:%d!\n",
			win->area[0].fmt_cfg);
		break;
	}
	mb_h_size = 4;

	/*macro block xvir and yvir */
	if ((win->area[0].xvir % mb_w_size == 0) &&
	    (win->area[0].yvir % mb_h_size == 0)) {
		fbdc_mb_vir_width = win->area[0].xvir / mb_w_size;
		fbdc_mb_vir_height = win->area[0].yvir / mb_h_size;
	} else {
		pr_err("fbdc fmt[%d]:", win->area[0].fmt_cfg);
		pr_err("xvir[%d]/yvir[%d] should %d/%d pix align!\n",
		       win->area[0].xvir, win->area[0].yvir,
		       mb_w_size, mb_h_size);
	}
	/*macro block xact and yact */
	if ((win->area[0].xact % mb_w_size == 0) &&
	    (win->area[0].yact % mb_h_size == 0)) {
		fbdc_mb_width = win->area[0].xact / mb_w_size;
		fbdc_mb_height = win->area[0].yact / mb_h_size;
	} else {
		pr_err("fbdc fmt[%d]:", win->area[0].fmt_cfg);
		pr_err("xact[%d]/yact[%d] should %d/%d pix align!\n",
		       win->area[0].xact, win->area[0].yact,
		       mb_w_size, mb_h_size);
	}
	/*macro block xoff and yoff */
	if ((win->area[0].xoff % mb_w_size == 0) &&
	    (win->area[0].yoff % mb_h_size == 0)) {
		fbdc_mb_xst = win->area[0].xoff / mb_w_size;
		fbdc_mb_yst = win->area[0].yoff / mb_h_size;
	} else {
		pr_err("fbdc fmt[%d]:", win->area[0].fmt_cfg);
		pr_err("xoff[%d]/yoff[%d] should %d/%d pix align!\n",
		       win->area[0].xoff, win->area[0].yoff,
		       mb_w_size, mb_h_size);
	}

	/*FBDC tiles */
	fbdc_num_tiles = fbdc_mb_vir_width * fbdc_mb_vir_height;

	/*
	   switch (fbdc_rotation_mode)  {
	   case FBDC_ROT_NONE:
	   fbdc_cmp_index_init =
	   (fbdc_mb_yst*fbdc_mb_vir_width) +  fbdc_mb_xst;
	   break;
	   case FBDC_X_MIRROR:
	   fbdc_cmp_index_init =
	   (fbdc_mb_yst*fbdc_mb_vir_width) + (fbdc_mb_xst+
	   (fbdc_mb_width-1));
	   break;
	   case FBDC_Y_MIRROR:
	   fbdc_cmp_index_init =
	   ((fbdc_mb_yst+(fbdc_mb_height-1))*fbdc_mb_vir_width)  +
	   fbdc_mb_xst;
	   break;
	   case FBDC_ROT_180:
	   fbdc_cmp_index_init =
	   ((fbdc_mb_yst+(fbdc_mb_height-1))*fbdc_mb_vir_width) +
	   (fbdc_mb_xst+(fbdc_mb_width-1));
	   break;
	   }
	 */
	if (win->xmirror && win->ymirror && ((win_id == 2) || (win_id == 3))) {
		fbdc_cmp_index_init =
		    ((fbdc_mb_yst + (fbdc_mb_height - 1)) * fbdc_mb_vir_width) +
		    (fbdc_mb_xst + (fbdc_mb_width - 1));
	} else {
		fbdc_cmp_index_init =
		    (fbdc_mb_yst * fbdc_mb_vir_width) + fbdc_mb_xst;
	}
	/*fbdc fmt maybe need to change*/
	win->area[0].fbdc_dsp_width_ratio = fbdc_dsp_width_ratio;
	win->area[0].fbdc_mb_vir_width = fbdc_mb_vir_width;
	win->area[0].fbdc_mb_vir_height = fbdc_mb_vir_height;
	win->area[0].fbdc_mb_width = fbdc_mb_width;
	win->area[0].fbdc_mb_height = fbdc_mb_height;
	win->area[0].fbdc_mb_xst = fbdc_mb_xst;
	win->area[0].fbdc_mb_yst = fbdc_mb_yst;
	win->area[0].fbdc_num_tiles = fbdc_num_tiles;
	win->area[0].fbdc_cmp_index_init = fbdc_cmp_index_init;

	return 0;
}

static int rk3368_lcdc_axi_gather_cfg(struct lcdc_device *lcdc_dev,
				      struct rk_lcdc_win *win)
{
	u32 mask, val;
	u16 yrgb_gather_num = 3;
	u16 cbcr_gather_num = 1;

	switch (win->area[0].format) {
	case ARGB888:
	case XBGR888:
	case XRGB888:
	case ABGR888:
	case FBDC_ARGB_888:
	case FBDC_RGBX_888:
	case FBDC_ABGR_888:
		yrgb_gather_num = 3;
		break;
	case RGB888:
	case RGB565:
	case BGR888:
	case BGR565:
	case FBDC_RGB_565:
		yrgb_gather_num = 2;
		break;
	case YUV444:
	case YUV422:
	case YUV420:
	case YUV420_NV21:
		yrgb_gather_num = 1;
		cbcr_gather_num = 2;
		break;
	default:
		dev_err(lcdc_dev->driver.dev, "%s:un supported format!\n",
			__func__);
		return -EINVAL;
	}

	if ((win->id == 0) || (win->id == 1)) {
		mask = m_WIN0_YRGB_AXI_GATHER_EN | m_WIN0_CBR_AXI_GATHER_EN |
			m_WIN0_YRGB_AXI_GATHER_NUM | m_WIN0_CBR_AXI_GATHER_NUM;
		val = v_WIN0_YRGB_AXI_GATHER_EN(1) |
			v_WIN0_CBR_AXI_GATHER_EN(1) |
			v_WIN0_YRGB_AXI_GATHER_NUM(yrgb_gather_num) |
			v_WIN0_CBR_AXI_GATHER_NUM(cbcr_gather_num);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL1 + (win->id * 0x40),
			     mask, val);
	} else if ((win->id == 2) || (win->id == 3)) {
		mask = m_WIN2_AXI_GATHER_EN | m_WIN2_AXI_GATHER_NUM;
		val = v_WIN2_AXI_GATHER_EN(1) |
			v_WIN2_AXI_GATHER_NUM(yrgb_gather_num);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL1 + ((win->id - 2) * 0x50),
			     mask, val);
	} else if (win->id == 4) {
		mask = m_HWC_AXI_GATHER_EN | m_HWC_AXI_GATHER_NUM;
		val = v_HWC_AXI_GATHER_EN(1) |
			v_HWC_AXI_GATHER_NUM(yrgb_gather_num);
		lcdc_msk_reg(lcdc_dev, HWC_CTRL1, mask, val);
	}
	return 0;
}

static void rk3368_lcdc_csc_mode(struct lcdc_device *lcdc_dev,
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
	} else if (dev_drv->overlay_mode == VOP_RGB_DOMAIN) {
		switch (win->area[0].fmt_cfg) {
		case VOP_FORMAT_YCBCR420:
			if ((win->id == 0) || (win->id == 1))
				win->csc_mode = VOP_Y2R_CSC_MPEG;
			break;
		default:
			break;
		}
	}
}

static int rk3368_win_0_1_reg_update(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	unsigned int mask, val, off;

	off = win_id * 0x40;
	/*if(win->win_lb_mode == 5)
	   win->win_lb_mode = 4;
	   for rk3288 to fix hw bug? */

	if (win->state == 1) {
		rk3368_lcdc_csc_mode(lcdc_dev, win);
		rk3368_lcdc_axi_gather_cfg(lcdc_dev, win);
		if (win->area[0].fbdc_en)
			rk3368_fbdc_reg_update(&lcdc_dev->driver, win_id);
		mask = m_WIN0_EN | m_WIN0_DATA_FMT | m_WIN0_FMT_10 |
			m_WIN0_LB_MODE | m_WIN0_RB_SWAP | m_WIN0_X_MIRROR |
			m_WIN0_Y_MIRROR | m_WIN0_CSC_MODE | m_WIN0_UV_SWAP;
		val = v_WIN0_EN(win->state) |
			v_WIN0_DATA_FMT(win->area[0].fmt_cfg) |
			v_WIN0_FMT_10(win->fmt_10) |
			v_WIN0_LB_MODE(win->win_lb_mode) |
			v_WIN0_RB_SWAP(win->area[0].swap_rb) |
			v_WIN0_X_MIRROR(win->xmirror) |
			v_WIN0_Y_MIRROR(win->ymirror) |
			v_WIN0_CSC_MODE(win->csc_mode) |
			v_WIN0_UV_SWAP(win->area[0].swap_uv);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0 + off, mask, val);

		mask = m_WIN0_BIC_COE_SEL |
		    m_WIN0_VSD_YRGB_GT4 | m_WIN0_VSD_YRGB_GT2 |
		    m_WIN0_VSD_CBR_GT4 | m_WIN0_VSD_CBR_GT2 |
		    m_WIN0_YRGB_HOR_SCL_MODE | m_WIN0_YRGB_VER_SCL_MODE |
		    m_WIN0_YRGB_HSD_MODE | m_WIN0_YRGB_VSU_MODE |
		    m_WIN0_YRGB_VSD_MODE | m_WIN0_CBR_HOR_SCL_MODE |
		    m_WIN0_CBR_VER_SCL_MODE | m_WIN0_CBR_HSD_MODE |
		    m_WIN0_CBR_VSU_MODE | m_WIN0_CBR_VSD_MODE;
		val = v_WIN0_BIC_COE_SEL(win->bic_coe_el) |
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
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL1 + off, mask, val);
		val = v_WIN0_VIR_STRIDE(win->area[0].y_vir_stride) |
		    v_WIN0_VIR_STRIDE_UV(win->area[0].uv_vir_stride);
		lcdc_writel(lcdc_dev, WIN0_VIR + off, val);
		/*lcdc_writel(lcdc_dev, WIN0_YRGB_MST+off,
				win->area[0].y_addr);
		   lcdc_writel(lcdc_dev, WIN0_CBR_MST+off,
				win->area[0].uv_addr); */
		val = v_WIN0_ACT_WIDTH(win->area[0].xact) |
		    v_WIN0_ACT_HEIGHT(win->area[0].yact);
		lcdc_writel(lcdc_dev, WIN0_ACT_INFO + off, val);

		val = v_WIN0_DSP_WIDTH(win->area[0].xsize) |
		    v_WIN0_DSP_HEIGHT(win->area[0].ysize);
		lcdc_writel(lcdc_dev, WIN0_DSP_INFO + off, val);

		val = v_WIN0_DSP_XST(win->area[0].dsp_stx) |
		    v_WIN0_DSP_YST(win->area[0].dsp_sty);
		lcdc_writel(lcdc_dev, WIN0_DSP_ST + off, val);

		val = v_WIN0_HS_FACTOR_YRGB(win->scale_yrgb_x) |
		    v_WIN0_VS_FACTOR_YRGB(win->scale_yrgb_y);
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_YRGB + off, val);

		val = v_WIN0_HS_FACTOR_CBR(win->scale_cbcr_x) |
		    v_WIN0_VS_FACTOR_CBR(win->scale_cbcr_y);
		lcdc_writel(lcdc_dev, WIN0_SCL_FACTOR_CBR + off, val);
		if (win->alpha_en == 1) {
			rk3368_lcdc_alpha_cfg(dev_drv, win_id);
		} else {
			mask = m_WIN0_SRC_ALPHA_EN;
			val = v_WIN0_SRC_ALPHA_EN(0);
			lcdc_msk_reg(lcdc_dev, WIN0_SRC_ALPHA_CTRL + off,
				     mask, val);
		}

		if (dev_drv->cur_screen->mode.vmode & FB_VMODE_INTERLACED) {
			mask = m_WIN0_YRGB_DEFLICK | m_WIN0_CBR_DEFLICK;
			if (win->area[0].yact == 2 * win->area[0].ysize)
				val = v_WIN0_YRGB_DEFLICK(0) |
					v_WIN0_CBR_DEFLICK(0);
			else
				val = v_WIN0_YRGB_DEFLICK(1) |
					v_WIN0_CBR_DEFLICK(1);
			lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask, val);
		}
	} else {
		mask = m_WIN0_EN;
		val = v_WIN0_EN(win->state);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0 + off, mask, val);
	}
	return 0;
}

static int rk3368_win_2_3_reg_update(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	unsigned int mask, val, off;

	off = (win_id - 2) * 0x50;
	rk3368_lcdc_area_xst(win, win->area_num);

	if (win->state == 1) {
		rk3368_lcdc_csc_mode(lcdc_dev, win);
		rk3368_lcdc_axi_gather_cfg(lcdc_dev, win);
		if (win->area[0].fbdc_en)
			rk3368_fbdc_reg_update(&lcdc_dev->driver, win_id);

		mask = m_WIN2_EN | m_WIN2_CSC_MODE;
		val = v_WIN2_EN(1) | v_WIN1_CSC_MODE(win->csc_mode);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);
		/*area 0 */
		if (win->area[0].state == 1) {
			mask = m_WIN2_MST0_EN | m_WIN2_DATA_FMT0 |
			    m_WIN2_RB_SWAP0;
			val = v_WIN2_MST0_EN(win->area[0].state) |
			    v_WIN2_DATA_FMT0(win->area[0].fmt_cfg) |
			    v_WIN2_RB_SWAP0(win->area[0].swap_rb);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);

			mask = m_WIN2_VIR_STRIDE0;
			val = v_WIN2_VIR_STRIDE0(win->area[0].y_vir_stride);
			lcdc_msk_reg(lcdc_dev, WIN2_VIR0_1 + off, mask, val);

			/*lcdc_writel(lcdc_dev,WIN2_MST0+off,
			   win->area[0].y_addr); */
			val = v_WIN2_DSP_WIDTH0(win->area[0].xsize) |
			    v_WIN2_DSP_HEIGHT0(win->area[0].ysize);
			lcdc_writel(lcdc_dev, WIN2_DSP_INFO0 + off, val);
			val = v_WIN2_DSP_XST0(win->area[0].dsp_stx) |
			    v_WIN2_DSP_YST0(win->area[0].dsp_sty);
			lcdc_writel(lcdc_dev, WIN2_DSP_ST0 + off, val);
		} else {
			mask = m_WIN2_MST0_EN;
			val = v_WIN2_MST0_EN(0);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);
		}
		/*area 1 */
		if (win->area[1].state == 1) {
			/*rk3368_win_area_check_var(win_id, 1,
						  &win->area[0], &win->area[1]);
			*/

			mask = m_WIN2_MST1_EN | m_WIN2_DATA_FMT1 |
			    m_WIN2_RB_SWAP1;
			val = v_WIN2_MST1_EN(win->area[1].state) |
			    v_WIN2_DATA_FMT1(win->area[1].fmt_cfg) |
			    v_WIN2_RB_SWAP1(win->area[1].swap_rb);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);

			mask = m_WIN2_VIR_STRIDE1;
			val = v_WIN2_VIR_STRIDE1(win->area[1].y_vir_stride);
			lcdc_msk_reg(lcdc_dev, WIN2_VIR0_1 + off, mask, val);

			/*lcdc_writel(lcdc_dev,WIN2_MST1+off,
			   win->area[1].y_addr); */
			val = v_WIN2_DSP_WIDTH1(win->area[1].xsize) |
			    v_WIN2_DSP_HEIGHT1(win->area[1].ysize);
			lcdc_writel(lcdc_dev, WIN2_DSP_INFO1 + off, val);
			val = v_WIN2_DSP_XST1(win->area[1].dsp_stx) |
			    v_WIN2_DSP_YST1(win->area[1].dsp_sty);
			lcdc_writel(lcdc_dev, WIN2_DSP_ST1 + off, val);
		} else {
			mask = m_WIN2_MST1_EN;
			val = v_WIN2_MST1_EN(0);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);
		}
		/*area 2 */
		if (win->area[2].state == 1) {
			/*rk3368_win_area_check_var(win_id, 2,
						  &win->area[1], &win->area[2]);
			*/

			mask = m_WIN2_MST2_EN | m_WIN2_DATA_FMT2 |
			    m_WIN2_RB_SWAP2;
			val = v_WIN2_MST2_EN(win->area[2].state) |
			    v_WIN2_DATA_FMT2(win->area[2].fmt_cfg) |
			    v_WIN2_RB_SWAP2(win->area[2].swap_rb);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);

			mask = m_WIN2_VIR_STRIDE2;
			val = v_WIN2_VIR_STRIDE2(win->area[2].y_vir_stride);
			lcdc_msk_reg(lcdc_dev, WIN2_VIR2_3 + off, mask, val);

			/*lcdc_writel(lcdc_dev,WIN2_MST2+off,
			   win->area[2].y_addr); */
			val = v_WIN2_DSP_WIDTH2(win->area[2].xsize) |
			    v_WIN2_DSP_HEIGHT2(win->area[2].ysize);
			lcdc_writel(lcdc_dev, WIN2_DSP_INFO2 + off, val);
			val = v_WIN2_DSP_XST2(win->area[2].dsp_stx) |
			    v_WIN2_DSP_YST2(win->area[2].dsp_sty);
			lcdc_writel(lcdc_dev, WIN2_DSP_ST2 + off, val);
		} else {
			mask = m_WIN2_MST2_EN;
			val = v_WIN2_MST2_EN(0);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);
		}
		/*area 3 */
		if (win->area[3].state == 1) {
			/*rk3368_win_area_check_var(win_id, 3,
						  &win->area[2], &win->area[3]);
			*/

			mask = m_WIN2_MST3_EN | m_WIN2_DATA_FMT3 |
			    m_WIN2_RB_SWAP3;
			val = v_WIN2_MST3_EN(win->area[3].state) |
			    v_WIN2_DATA_FMT3(win->area[3].fmt_cfg) |
			    v_WIN2_RB_SWAP3(win->area[3].swap_rb);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);

			mask = m_WIN2_VIR_STRIDE3;
			val = v_WIN2_VIR_STRIDE3(win->area[3].y_vir_stride);
			lcdc_msk_reg(lcdc_dev, WIN2_VIR2_3 + off, mask, val);

			/*lcdc_writel(lcdc_dev,WIN2_MST3+off,
			   win->area[3].y_addr); */
			val = v_WIN2_DSP_WIDTH3(win->area[3].xsize) |
			    v_WIN2_DSP_HEIGHT3(win->area[3].ysize);
			lcdc_writel(lcdc_dev, WIN2_DSP_INFO3 + off, val);
			val = v_WIN2_DSP_XST3(win->area[3].dsp_stx) |
			    v_WIN2_DSP_YST3(win->area[3].dsp_sty);
			lcdc_writel(lcdc_dev, WIN2_DSP_ST3 + off, val);
		} else {
			mask = m_WIN2_MST3_EN;
			val = v_WIN2_MST3_EN(0);
			lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);
		}

		if (win->alpha_en == 1) {
			rk3368_lcdc_alpha_cfg(dev_drv, win_id);
		} else {
			mask = m_WIN2_SRC_ALPHA_EN;
			val = v_WIN2_SRC_ALPHA_EN(0);
			lcdc_msk_reg(lcdc_dev, WIN2_SRC_ALPHA_CTRL + off,
				     mask, val);
		}
	} else {
		mask = m_WIN2_EN | m_WIN2_MST0_EN |
		    m_WIN2_MST0_EN | m_WIN2_MST2_EN | m_WIN2_MST3_EN;
		val = v_WIN2_EN(win->state) | v_WIN2_MST0_EN(0) |
		    v_WIN2_MST1_EN(0) | v_WIN2_MST2_EN(0) | v_WIN2_MST3_EN(0);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL0 + off, mask, val);
	}
	return 0;
}

static int rk3368_hwc_reg_update(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = dev_drv->win[win_id];
	unsigned int mask, val, hwc_size = 0;

	if (win->state == 1) {
		rk3368_lcdc_csc_mode(lcdc_dev, win);
		rk3368_lcdc_axi_gather_cfg(lcdc_dev, win);
		mask = m_HWC_EN | m_HWC_DATA_FMT |
		    m_HWC_RB_SWAP | m_WIN0_CSC_MODE;
		val = v_HWC_EN(1) | v_HWC_DATA_FMT(win->area[0].fmt_cfg) |
		    v_HWC_RB_SWAP(win->area[0].swap_rb) |
		    v_WIN0_CSC_MODE(win->csc_mode);
		lcdc_msk_reg(lcdc_dev, HWC_CTRL0, mask, val);

		if ((win->area[0].xsize == 32) && (win->area[0].ysize == 32))
			hwc_size = 0;
		else if ((win->area[0].xsize == 64) &&
			 (win->area[0].ysize == 64))
			hwc_size = 1;
		else if ((win->area[0].xsize == 96) &&
			 (win->area[0].ysize == 96))
			hwc_size = 2;
		else if ((win->area[0].xsize == 128) &&
			 (win->area[0].ysize == 128))
			hwc_size = 3;
		else
			dev_err(lcdc_dev->dev, "un supported hwc size!\n");

		mask = m_HWC_SIZE;
		val = v_HWC_SIZE(hwc_size);
		lcdc_msk_reg(lcdc_dev, HWC_CTRL0, mask, val);

		mask = m_HWC_DSP_XST | m_HWC_DSP_YST;
		val = v_HWC_DSP_XST(win->area[0].dsp_stx) |
		    v_HWC_DSP_YST(win->area[0].dsp_sty);
		lcdc_msk_reg(lcdc_dev, HWC_DSP_ST, mask, val);

		if (win->alpha_en == 1) {
			rk3368_lcdc_alpha_cfg(dev_drv, win_id);
		} else {
			mask = m_WIN2_SRC_ALPHA_EN;
			val = v_WIN2_SRC_ALPHA_EN(0);
			lcdc_msk_reg(lcdc_dev, WIN2_SRC_ALPHA_CTRL, mask, val);
		}
	} else {
		mask = m_HWC_EN;
		val = v_HWC_EN(win->state);
		lcdc_msk_reg(lcdc_dev, HWC_CTRL0, mask, val);
	}
	return 0;
}

static int rk3368_lcdc_layer_update_regs(struct lcdc_device *lcdc_dev,
					 struct rk_lcdc_win *win)
{
	struct rk_lcdc_driver *dev_drv = &lcdc_dev->driver;
	int timeout;
	unsigned long flags;

	if (likely(lcdc_dev->clk_on)) {
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN,
			     v_STANDBY_EN(lcdc_dev->standby));
		if ((win->id == 0) || (win->id == 1))
			rk3368_win_0_1_reg_update(dev_drv, win->id);
		else if ((win->id == 2) || (win->id == 3))
			rk3368_win_2_3_reg_update(dev_drv, win->id);
		else if (win->id == 4)
			rk3368_hwc_reg_update(dev_drv, win->id);
		/*rk3368_lcdc_post_cfg(dev_drv); */
		lcdc_cfg_done(lcdc_dev);
	}

	/*if (dev_drv->wait_fs) { */
	if (0) {
		spin_lock_irqsave(&dev_drv->cpl_lock, flags);
		init_completion(&dev_drv->frame_done);
		spin_unlock_irqrestore(&dev_drv->cpl_lock, flags);
		timeout =
		    wait_for_completion_timeout(&dev_drv->frame_done,
						msecs_to_jiffies
						(dev_drv->cur_screen->ft + 5));
		if (!timeout && (!dev_drv->frame_done.done)) {
			dev_warn(lcdc_dev->dev,
				 "wait for new frame start time out!\n");
			return -ETIMEDOUT;
		}
	}
	DBG(2, "%s for lcdc%d\n", __func__, lcdc_dev->id);
	return 0;
}

static int rk3368_lcdc_reg_restore(struct lcdc_device *lcdc_dev)
{
	if (lcdc_dev->soc_type == VOP_FULL_RK3366)
		memcpy((u8 *)lcdc_dev->regs, (u8 *)lcdc_dev->regsbak, 0x2a4);
	else
		memcpy((u8 *)lcdc_dev->regs, (u8 *)lcdc_dev->regsbak, 0x270);

	return 0;
}

static int __maybe_unused rk3368_lcdc_mmu_en(struct rk_lcdc_driver *dev_drv)
{
	u32 mask, val;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}
	if (dev_drv->iommu_enabled) {
		if (!lcdc_dev->iommu_status && dev_drv->mmu_dev) {
			if (likely(lcdc_dev->clk_on)) {
				mask = m_MMU_EN;
				val = v_MMU_EN(1);
				lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
				mask = m_AXI_MAX_OUTSTANDING_EN |
					m_AXI_OUTSTANDING_MAX_NUM;
				val = v_AXI_OUTSTANDING_MAX_NUM(31) |
					v_AXI_MAX_OUTSTANDING_EN(1);
				lcdc_msk_reg(lcdc_dev, SYS_CTRL1, mask, val);
			}
			lcdc_dev->iommu_status = 1;
			rockchip_iovmm_activate(dev_drv->dev);
		}
	}
	return 0;
}

static int rk3368_lcdc_set_dclk(struct rk_lcdc_driver *dev_drv, int reset_rate)
{
	int ret = 0, fps = 0;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
#ifdef CONFIG_RK_FPGA
	return 0;
#endif
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

static int rk3368_config_timing(struct rk_lcdc_driver *dev_drv)
{
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
	u16 h_total, v_total;
	u16 vact_end_f1, vact_st_f1, vs_end_f1, vs_st_f1;
	u32 frame_time;
	u32 line_flag_reg;

	if (lcdc_dev->soc_type == VOP_FULL_RK3366)
		line_flag_reg = LINE_FLAG_RK3366;
	else
		line_flag_reg = LINE_FLAG_RK3368;

	h_total = hsync_len + left_margin + x_res + right_margin;
	v_total = vsync_len + upper_margin + y_res + lower_margin;
	frame_time = 1000 * v_total * h_total / (screen->mode.pixclock / 1000);
	mask = m_DSP_HS_PW | m_DSP_HTOTAL;
	val = v_DSP_HS_PW(hsync_len) | v_DSP_HTOTAL(h_total);
	lcdc_msk_reg(lcdc_dev, DSP_HTOTAL_HS_END, mask, val);

	mask = m_DSP_HACT_END | m_DSP_HACT_ST;
	val = v_DSP_HACT_END(hsync_len + left_margin + x_res) |
	    v_DSP_HACT_ST(hsync_len + left_margin);
	lcdc_msk_reg(lcdc_dev, DSP_HACT_ST_END, mask, val);

	if (screen->mode.vmode & FB_VMODE_INTERLACED) {
		/* First Field Timing */
		mask = m_DSP_VS_PW | m_DSP_VTOTAL;
		val = v_DSP_VS_PW(vsync_len) |
		    v_DSP_VTOTAL(2 * (vsync_len + upper_margin +
				      lower_margin) + y_res + 1);
		lcdc_msk_reg(lcdc_dev, DSP_VTOTAL_VS_END, mask, val);

		mask = m_DSP_VACT_END | m_DSP_VACT_ST;
		val = v_DSP_VACT_END(vsync_len + upper_margin + y_res / 2) |
		    v_DSP_VACT_ST(vsync_len + upper_margin);
		lcdc_msk_reg(lcdc_dev, DSP_VACT_ST_END, mask, val);

		/* Second Field Timing */
		mask = m_DSP_VS_ST_F1 | m_DSP_VS_END_F1;
		vs_st_f1 = vsync_len + upper_margin + y_res / 2 + lower_margin;
		vs_end_f1 = 2 * vsync_len + upper_margin + y_res / 2 +
		    lower_margin;
		val = v_DSP_VS_ST_F1(vs_st_f1) | v_DSP_VS_END_F1(vs_end_f1);
		lcdc_msk_reg(lcdc_dev, DSP_VS_ST_END_F1, mask, val);

		mask = m_DSP_VACT_END_F1 | m_DSP_VAC_ST_F1;
		vact_end_f1 = 2 * (vsync_len + upper_margin) + y_res +
		    lower_margin + 1;
		vact_st_f1 = 2 * (vsync_len + upper_margin) + y_res / 2 +
		    lower_margin + 1;
		val =
		    v_DSP_VACT_END_F1(vact_end_f1) |
		    v_DSP_VAC_ST_F1(vact_st_f1);
		lcdc_msk_reg(lcdc_dev, DSP_VACT_ST_END_F1, mask, val);

		lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
			     m_DSP_INTERLACE | m_DSP_FIELD_POL,
			     v_DSP_INTERLACE(1) | v_DSP_FIELD_POL(0));
		if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
			if (y_res <= 576)
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
					     m_SW_CORE_DCLK_SEL,
					     v_SW_CORE_DCLK_SEL(1));
			else
				lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
					     m_SW_CORE_DCLK_SEL,
					     v_SW_CORE_DCLK_SEL(0));
		}
		mask =
		    m_WIN0_INTERLACE_READ | m_WIN0_YRGB_DEFLICK |
		    m_WIN0_CBR_DEFLICK;
		val =
		    v_WIN0_INTERLACE_READ(1) | v_WIN0_YRGB_DEFLICK(0) |
		    v_WIN0_CBR_DEFLICK(0);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask, val);

		mask =
		    m_WIN1_INTERLACE_READ | m_WIN1_YRGB_DEFLICK |
		    m_WIN1_CBR_DEFLICK;
		val =
		    v_WIN1_INTERLACE_READ(1) | v_WIN1_YRGB_DEFLICK(0) |
		    v_WIN1_CBR_DEFLICK(0);
		lcdc_msk_reg(lcdc_dev, WIN1_CTRL0, mask, val);

		mask = m_WIN2_INTERLACE_READ;
		val = v_WIN2_INTERLACE_READ(1);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL0, mask, val);

		mask = m_WIN3_INTERLACE_READ;
		val = v_WIN3_INTERLACE_READ(1);
		lcdc_msk_reg(lcdc_dev, WIN3_CTRL0, mask, val);

		mask = m_HWC_INTERLACE_READ;
		val = v_HWC_INTERLACE_READ(1);
		lcdc_msk_reg(lcdc_dev, HWC_CTRL0, mask, val);

		mask = m_DSP_LINE_FLAG0_NUM | m_DSP_LINE_FLAG1_NUM;
		val =
		    v_DSP_LINE_FLAG0_NUM(vact_end_f1) |
		    v_DSP_LINE_FLAG1_NUM(vact_end_f1 -
					 EARLY_TIME * v_total / frame_time);
		lcdc_msk_reg(lcdc_dev, line_flag_reg, mask, val);
	} else {
		mask = m_DSP_VS_PW | m_DSP_VTOTAL;
		val = v_DSP_VS_PW(vsync_len) | v_DSP_VTOTAL(v_total);
		lcdc_msk_reg(lcdc_dev, DSP_VTOTAL_VS_END, mask, val);

		mask = m_DSP_VACT_END | m_DSP_VACT_ST;
		val = v_DSP_VACT_END(vsync_len + upper_margin + y_res) |
		    v_DSP_VACT_ST(vsync_len + upper_margin);
		lcdc_msk_reg(lcdc_dev, DSP_VACT_ST_END, mask, val);

		lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
			     m_DSP_INTERLACE | m_DSP_FIELD_POL,
			     v_DSP_INTERLACE(0) | v_DSP_FIELD_POL(0));
		if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0,
				     m_SW_CORE_DCLK_SEL,
				     v_SW_CORE_DCLK_SEL(0));
		}
		mask =
		    m_WIN0_INTERLACE_READ | m_WIN0_YRGB_DEFLICK |
		    m_WIN0_CBR_DEFLICK;
		val =
		    v_WIN0_INTERLACE_READ(0) | v_WIN0_YRGB_DEFLICK(0) |
		    v_WIN0_CBR_DEFLICK(0);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask, val);

		mask =
		    m_WIN1_INTERLACE_READ | m_WIN1_YRGB_DEFLICK |
		    m_WIN1_CBR_DEFLICK;
		val =
		    v_WIN1_INTERLACE_READ(0) | v_WIN1_YRGB_DEFLICK(0) |
		    v_WIN1_CBR_DEFLICK(0);
		lcdc_msk_reg(lcdc_dev, WIN1_CTRL0, mask, val);

		mask = m_WIN2_INTERLACE_READ;
		val = v_WIN2_INTERLACE_READ(0);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL0, mask, val);

		mask = m_WIN3_INTERLACE_READ;
		val = v_WIN3_INTERLACE_READ(0);
		lcdc_msk_reg(lcdc_dev, WIN3_CTRL0, mask, val);

		mask = m_HWC_INTERLACE_READ;
		val = v_HWC_INTERLACE_READ(0);
		lcdc_msk_reg(lcdc_dev, HWC_CTRL0, mask, val);

		mask = m_DSP_LINE_FLAG0_NUM | m_DSP_LINE_FLAG1_NUM;
		val = v_DSP_LINE_FLAG0_NUM(vsync_len + upper_margin + y_res) |
			v_DSP_LINE_FLAG1_NUM(vsync_len + upper_margin + y_res -
					     EARLY_TIME * v_total / frame_time);
		lcdc_msk_reg(lcdc_dev, line_flag_reg, mask, val);
	}
	rk3368_lcdc_post_cfg(dev_drv);
	return 0;
}

static void rk3368_lcdc_bcsh_path_sel(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 bcsh_ctrl;

	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_OVERLAY_MODE,
		     v_OVERLAY_MODE(dev_drv->overlay_mode));
	if (dev_drv->overlay_mode == VOP_YUV_DOMAIN) {
		if (IS_YUV_COLOR(dev_drv->output_color))	/* bypass */
			lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
				     m_BCSH_Y2R_EN | m_BCSH_R2Y_EN,
				     v_BCSH_Y2R_EN(0) | v_BCSH_R2Y_EN(0));
		else		/* YUV2RGB */
			lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
				     m_BCSH_Y2R_EN | m_BCSH_Y2R_CSC_MODE |
				     m_BCSH_R2Y_EN,
				     v_BCSH_Y2R_EN(1) |
				     v_BCSH_Y2R_CSC_MODE(VOP_Y2R_CSC_MPEG) |
				     v_BCSH_R2Y_EN(0));
	} else {		/* overlay_mode=VOP_RGB_DOMAIN */
		/* bypass  --need check,if bcsh close? */
		if (dev_drv->output_color == COLOR_RGB) {
			bcsh_ctrl = lcdc_readl(lcdc_dev, BCSH_CTRL);
			if (((bcsh_ctrl & m_BCSH_EN) == 1) ||
			    (dev_drv->bcsh.enable == 1))/*bcsh enabled */
				lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
					     m_BCSH_R2Y_EN |
					     m_BCSH_Y2R_EN,
					     v_BCSH_R2Y_EN(1) |
					     v_BCSH_Y2R_EN(1));
			else
				lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
					     m_BCSH_R2Y_EN | m_BCSH_Y2R_EN,
					     v_BCSH_R2Y_EN(0) |
					     v_BCSH_Y2R_EN(0));
		} else		/* RGB2YUV */
			lcdc_msk_reg(lcdc_dev, BCSH_CTRL,
				     m_BCSH_R2Y_EN |
				     m_BCSH_R2Y_CSC_MODE | m_BCSH_Y2R_EN,
				     v_BCSH_R2Y_EN(1) |
				     v_BCSH_R2Y_CSC_MODE(VOP_Y2R_CSC_MPEG) |
				     v_BCSH_Y2R_EN(0));
	}
}

static int rk3368_get_dspbuf_info(struct rk_lcdc_driver *dev_drv, u16 *xact,
				  u16 *yact, int *format, u32 *dsp_addr,
				  int *ymirror)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	u32 val;

	spin_lock(&lcdc_dev->reg_lock);

	val = lcdc_readl(lcdc_dev, WIN0_ACT_INFO);
	*xact = (val & m_WIN0_ACT_WIDTH) + 1;
	*yact = ((val & m_WIN0_ACT_HEIGHT)>>16) + 1;

	val = lcdc_readl(lcdc_dev, WIN0_CTRL0);
	*format = (val & m_WIN0_DATA_FMT) >> 1;
	*ymirror = (val & m_WIN0_Y_MIRROR) >> 22;
	*dsp_addr = lcdc_readl(lcdc_dev, WIN0_YRGB_MST);

	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int rk3368_post_dspbuf(struct rk_lcdc_driver *dev_drv, u32 rgb_mst,
			      int format, u16 xact, u16 yact, u16 xvir,
			      int ymirror)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	u32 val, mask;
	struct rk_lcdc_win *win = dev_drv->win[0];
	int swap = (format == RGB888) ? 1 : 0;

	mask = m_WIN0_DATA_FMT | m_WIN0_RB_SWAP | m_WIN0_Y_MIRROR;
	val = v_WIN0_DATA_FMT(format) | v_WIN0_RB_SWAP(swap) |
		v_WIN0_Y_MIRROR(ymirror);
	lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask, val);

	lcdc_msk_reg(lcdc_dev, WIN0_VIR, m_WIN0_VIR_STRIDE,
		     v_WIN0_VIR_STRIDE(xvir));
	lcdc_writel(lcdc_dev, WIN0_ACT_INFO, v_WIN0_ACT_WIDTH(xact) |
		    v_WIN0_ACT_HEIGHT(yact));

	lcdc_writel(lcdc_dev, WIN0_YRGB_MST, rgb_mst);

	lcdc_cfg_done(lcdc_dev);
	if (format == RGB888)
		win->area[0].format = BGR888;
	else
		win->area[0].format = format;

	win->ymirror = ymirror;
	win->state = 1;
	win->last_state = 1;

	return 0;
}

static int lcdc_reset(struct rk_lcdc_driver *dev_drv, bool initscreen)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;
	u32 __maybe_unused v;
	if (!lcdc_dev->standby && initscreen && (dev_drv->first_frame != 1)) {
		mdelay(150);
		mask = m_WIN0_EN;
		val = v_WIN0_EN(0);
		lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask, val);
		lcdc_msk_reg(lcdc_dev, WIN1_CTRL0, mask, val);

		mask = m_WIN2_EN | m_WIN2_MST0_EN |
			m_WIN2_MST1_EN |
			m_WIN2_MST2_EN | m_WIN2_MST3_EN;
		val = v_WIN2_EN(0) | v_WIN2_MST0_EN(0) |
			v_WIN2_MST1_EN(0) |
			v_WIN2_MST2_EN(0) | v_WIN2_MST3_EN(0);
		lcdc_msk_reg(lcdc_dev, WIN2_CTRL0, mask, val);
		lcdc_msk_reg(lcdc_dev, WIN3_CTRL0, mask, val);
		mask = m_HDMI_OUT_EN;
		val = v_HDMI_OUT_EN(0);
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
		lcdc_cfg_done(lcdc_dev);
		mdelay(50);
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN, v_STANDBY_EN(1));
		writel_relaxed(0, lcdc_dev->regs + REG_CFG_DONE);
		mdelay(50);
#ifdef VOP_RESET
		if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_deactivate(dev_drv->dev);
		}
		lcdc_cru_writel(lcdc_dev->cru_base, 0x0318,
				(1 << 4)  | (1 << 5)  | (1 << 6) |
				(1 << 20) | (1 << 21) | (1 << 22));
		udelay(100);
		v = lcdc_cru_readl(lcdc_dev->cru_base, 0x0318);
		pr_info("cru read = 0x%x\n", v);
		lcdc_cru_writel(lcdc_dev->cru_base, 0x0318,
				(0 << 4)  | (0 << 5)  | (0 << 6) |
				(1 << 20) | (1 << 21) | (1 << 22));
		mdelay(100);
		if (dev_drv->iommu_enabled) {
			if (dev_drv->mmu_dev)
				rockchip_iovmm_activate(dev_drv->dev);
		}
		mdelay(50);
		rk3368_lcdc_reg_restore(lcdc_dev);
		mdelay(50);
#endif
	}
	return 0;
}

static int rk3368_load_screen(struct rk_lcdc_driver *dev_drv, bool initscreen)
{
	u16 face = 0;
	u16 dclk_ddr = 0;
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u32 mask = 0, val = 0;

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}

	if (!lcdc_dev->standby && initscreen && (dev_drv->first_frame != 1))
		flush_kthread_worker(&dev_drv->update_regs_worker);

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		dev_drv->overlay_mode = VOP_RGB_DOMAIN;
#if 0
		if (!lcdc_dev->standby && !initscreen) {
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN,
				     v_STANDBY_EN(1));
			lcdc_cfg_done(lcdc_dev);
			mdelay(50);
		}
#else
	lcdc_reset(dev_drv, initscreen);
#endif
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
			mask = m_DITHER_DOWN_EN;
			val = v_DITHER_DOWN_EN(0);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_YUV_420:
			/*yuv420 output prefer yuv domain overlay */
			face = OUT_YUV_420;
			dclk_ddr = 1;
			mask = m_DITHER_DOWN_EN;
			val = v_DITHER_DOWN_EN(0);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_S888:
			face = OUT_S888;
			mask = m_DITHER_DOWN_EN;
			val = v_DITHER_DOWN_EN(0);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_S888DUMY:
			face = OUT_S888DUMY;
			mask = m_DITHER_DOWN_EN;
			val = v_DITHER_DOWN_EN(0);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		case OUT_CCIR656:
			if (screen->color_mode == COLOR_RGB)
				dev_drv->overlay_mode = VOP_RGB_DOMAIN;
			else
				dev_drv->overlay_mode = VOP_YUV_DOMAIN;
			face = OUT_CCIR656_MODE_0;
			mask = m_DITHER_DOWN_EN;
			val = v_DITHER_DOWN_EN(0);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
			break;
		default:
			dev_err(lcdc_dev->dev, "un supported interface!\n");
			break;
		}
		switch (screen->type) {
		case SCREEN_RGB:
			mask = m_RGB_OUT_EN;
			val = v_RGB_OUT_EN(1);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			mask = m_RGB_LVDS_HSYNC_POL | m_RGB_LVDS_VSYNC_POL |
			    m_RGB_LVDS_DEN_POL | m_RGB_LVDS_DCLK_POL;
			val = v_RGB_LVDS_HSYNC_POL(screen->pin_hsync) |
			    v_RGB_LVDS_VSYNC_POL(screen->pin_vsync) |
			    v_RGB_LVDS_DEN_POL(screen->pin_den) |
			    v_RGB_LVDS_DCLK_POL(screen->pin_dclk);
			if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
				lcdc_grf_writel(lcdc_dev->grf_base,
						RK3366_GRF_SOC_CON5,
						RGB_SOURCE_SEL(dev_drv->id));
				lcdc_grf_writel(lcdc_dev->grf_base,
						RK3366_GRF_SOC_CON0,
						RGB_DATA_PLANA);
			}
			break;
		case SCREEN_LVDS:
			mask = m_RGB_OUT_EN;
			val = v_RGB_OUT_EN(1);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			mask = m_RGB_LVDS_HSYNC_POL | m_RGB_LVDS_VSYNC_POL |
			    m_RGB_LVDS_DEN_POL | m_RGB_LVDS_DCLK_POL;
			val = v_RGB_LVDS_HSYNC_POL(screen->pin_hsync) |
			    v_RGB_LVDS_VSYNC_POL(screen->pin_vsync) |
			    v_RGB_LVDS_DEN_POL(screen->pin_den) |
			    v_RGB_LVDS_DCLK_POL(screen->pin_dclk);
			if (lcdc_dev->soc_type == VOP_FULL_RK3366)
				lcdc_grf_writel(lcdc_dev->grf_base,
						RK3366_GRF_SOC_CON0,
						LVDS_SOURCE_SEL(dev_drv->id));
			break;
		case SCREEN_HDMI:
			if (screen->color_mode == COLOR_RGB)
				dev_drv->overlay_mode = VOP_RGB_DOMAIN;
			else
				dev_drv->overlay_mode = VOP_YUV_DOMAIN;
			mask = m_HDMI_OUT_EN  | m_RGB_OUT_EN;
			val = v_HDMI_OUT_EN(1) | v_RGB_OUT_EN(0);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			mask = m_HDMI_HSYNC_POL | m_HDMI_VSYNC_POL |
			    m_HDMI_DEN_POL | m_HDMI_DCLK_POL;
			val = v_HDMI_HSYNC_POL(screen->pin_hsync) |
			    v_HDMI_VSYNC_POL(screen->pin_vsync) |
			    v_HDMI_DEN_POL(screen->pin_den) |
			    v_HDMI_DCLK_POL(screen->pin_dclk);
			if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
				lcdc_grf_writel(lcdc_dev->grf_base,
						RK3366_GRF_SOC_CON0,
						HDMI_SOURCE_SEL(dev_drv->id));
			}
			break;
		case SCREEN_MIPI:
			mask = m_MIPI_OUT_EN  | m_RGB_OUT_EN;
			val = v_MIPI_OUT_EN(1) | v_RGB_OUT_EN(0);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			mask = m_MIPI_HSYNC_POL | m_MIPI_VSYNC_POL |
			    m_MIPI_DEN_POL | m_MIPI_DCLK_POL;
			val = v_MIPI_HSYNC_POL(screen->pin_hsync) |
			    v_MIPI_VSYNC_POL(screen->pin_vsync) |
			    v_MIPI_DEN_POL(screen->pin_den) |
			    v_MIPI_DCLK_POL(screen->pin_dclk);
			if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
				lcdc_grf_writel(lcdc_dev->grf_base,
						RK3366_GRF_SOC_CON0,
						MIPI_SOURCE_SEL(dev_drv->id));
			}
			break;
		case SCREEN_DUAL_MIPI:
			mask = m_MIPI_OUT_EN | m_DOUB_CHANNEL_EN  |
				m_RGB_OUT_EN;
			val = v_MIPI_OUT_EN(1) | v_DOUB_CHANNEL_EN(1) |
				v_RGB_OUT_EN(0);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);
			mask = m_MIPI_HSYNC_POL | m_MIPI_VSYNC_POL |
			    m_MIPI_DEN_POL | m_MIPI_DCLK_POL;
			val = v_MIPI_HSYNC_POL(screen->pin_hsync) |
			    v_MIPI_VSYNC_POL(screen->pin_vsync) |
			    v_MIPI_DEN_POL(screen->pin_den) |
			    v_MIPI_DCLK_POL(screen->pin_dclk);
			break;
		case SCREEN_EDP:
			face = OUT_P888;	/*RGB 888 output */

			mask = m_EDP_OUT_EN | m_RGB_OUT_EN;
			val = v_EDP_OUT_EN(1) | v_RGB_OUT_EN(0);
			lcdc_msk_reg(lcdc_dev, SYS_CTRL, mask, val);

			mask = m_EDP_HSYNC_POL | m_EDP_VSYNC_POL |
			    m_EDP_DEN_POL | m_EDP_DCLK_POL;
			val = v_EDP_HSYNC_POL(screen->pin_hsync) |
			    v_EDP_VSYNC_POL(screen->pin_vsync) |
			    v_EDP_DEN_POL(screen->pin_den) |
			    v_EDP_DCLK_POL(screen->pin_dclk);
			break;
		}
		/*hsync vsync den dclk polo,dither */
		lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
		mask = m_DSP_OUT_MODE | m_DSP_DCLK_DDR | m_DSP_BG_SWAP |
		    m_DSP_RB_SWAP | m_DSP_RG_SWAP | m_DSP_DELTA_SWAP |
		    m_DSP_DUMMY_SWAP | m_DSP_OUT_ZERO | m_DSP_BLANK_EN |
		    m_DSP_BLACK_EN | m_DSP_X_MIR_EN | m_DSP_Y_MIR_EN;
		val = v_DSP_OUT_MODE(face) | v_DSP_DCLK_DDR(dclk_ddr) |
		    v_DSP_BG_SWAP(screen->swap_gb) |
		    v_DSP_RB_SWAP(screen->swap_rb) |
		    v_DSP_RG_SWAP(screen->swap_rg) |
		    v_DSP_DELTA_SWAP(screen->swap_delta) |
		    v_DSP_DUMMY_SWAP(screen->swap_dumy) | v_DSP_OUT_ZERO(0) |
		    v_DSP_BLANK_EN(0) | v_DSP_BLACK_EN(0) |
		    v_DSP_X_MIR_EN(screen->x_mirror) |
		    v_DSP_Y_MIR_EN(screen->y_mirror);
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, mask, val);
		/*BG color */
		mask = m_DSP_BG_BLUE | m_DSP_BG_GREEN | m_DSP_BG_RED;
		if (dev_drv->overlay_mode == VOP_YUV_DOMAIN)
			val = v_DSP_BG_BLUE(0x80) | v_DSP_BG_GREEN(0x10) |
				v_DSP_BG_RED(0x80);
		else
			val = v_DSP_BG_BLUE(0) | v_DSP_BG_GREEN(0) |
				v_DSP_BG_RED(0);
		lcdc_msk_reg(lcdc_dev, DSP_BG, mask, val);
		dev_drv->output_color = screen->color_mode;
		if (screen->dsp_lut == NULL)
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN,
				     v_DSP_LUT_EN(0));
		else
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, m_DSP_LUT_EN,
				     v_DSP_LUT_EN(1));
		rk3368_lcdc_bcsh_path_sel(dev_drv);
		rk3368_config_timing(dev_drv);
		if (lcdc_dev->soc_type == VOP_FULL_RK3366)
			lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	rk3368_lcdc_set_dclk(dev_drv, 1);
	if (screen->type != SCREEN_HDMI &&
	    screen->type != SCREEN_TVOUT &&
	    dev_drv->trsm_ops &&
	    dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();
		if (screen->init)
			screen->init();
	/*if (!lcdc_dev->standby)
		lcdc_msk_reg(lcdc_dev, SYS_CTRL,
			m_STANDBY_EN, v_STANDBY_EN(0));*/
	return 0;
}


/*enable layer,open:1,enable;0 disable*/
static void rk3368_lcdc_layer_enable(struct lcdc_device *lcdc_dev,
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
		} else {
			if (lcdc_dev->atv_layer_cnt & (1 << win_id))
				lcdc_dev->atv_layer_cnt &= ~(1 << win_id);
		}
		lcdc_dev->driver.win[win_id]->state = open;
		if (!open) {
			/*rk3368_lcdc_reg_update(dev_drv);*/
			rk3368_lcdc_layer_update_regs
			(lcdc_dev, lcdc_dev->driver.win[win_id]);
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

static int rk3368_lcdc_enable_irq(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	u32 mask, val;
	/*struct rk_screen *screen = dev_drv->cur_screen; */
	u32 intr_en_reg, intr_clr_reg;

	if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
		intr_clr_reg = INTR_CLEAR_RK3366;
		intr_en_reg = INTR_EN_RK3366;
	} else {
		intr_clr_reg = INTR_CLEAR_RK3368;
		intr_en_reg = INTR_EN_RK3368;
	}

	mask = m_FS_INTR_CLR | m_FS_NEW_INTR_CLR | m_LINE_FLAG0_INTR_CLR |
	    m_LINE_FLAG1_INTR_CLR;
	val = v_FS_INTR_CLR(1) | v_FS_NEW_INTR_CLR(1) |
	    v_LINE_FLAG0_INTR_CLR(1) | v_LINE_FLAG1_INTR_CLR(1);
	lcdc_msk_reg(lcdc_dev, intr_clr_reg, mask, val);

	mask = m_FS_INTR_EN | m_LINE_FLAG0_INTR_EN |
		m_BUS_ERROR_INTR_EN | m_LINE_FLAG1_INTR_EN;
	val = v_FS_INTR_EN(1) | v_LINE_FLAG0_INTR_EN(1) |
	    v_BUS_ERROR_INTR_EN(1) | v_LINE_FLAG1_INTR_EN(0);
	lcdc_msk_reg(lcdc_dev, intr_en_reg, mask, val);
#ifdef LCDC_IRQ_EMPTY_DEBUG
	mask = m_WIN0_EMPTY_INTR_EN | m_WIN1_EMPTY_INTR_EN |
	    m_WIN2_EMPTY_INTR_EN |
	    m_WIN3_EMPTY_INTR_EN | m_HWC_EMPTY_INTR_EN |
	    m_POST_BUF_EMPTY_INTR_EN | m_PWM_GEN_INTR_EN;
	val = v_WIN0_EMPTY_INTR_EN(1) | v_WIN1_EMPTY_INTR_EN(1) |
	    v_WIN2_EMPTY_INTR_EN(1) |
	    v_WIN3_EMPTY_INTR_EN(1) | v_HWC_EMPTY_INTR_EN(1) |
	    v_POST_BUF_EMPTY_INTR_EN(1) | v_PWM_GEN_INTR_EN(1);
	lcdc_msk_reg(lcdc_dev, intr_en_reg, mask, val);
#endif
	return 0;
}

static int rk3368_lcdc_open(struct rk_lcdc_driver *dev_drv, int win_id,
			    bool open)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	/*enable clk,when first layer open */
	if ((open) && (!lcdc_dev->atv_layer_cnt)) {
		/*rockchip_set_system_status(sys_status);*/
		rk3368_lcdc_pre_init(dev_drv);
		rk3368_lcdc_clk_enable(lcdc_dev);
		rk3368_lcdc_enable_irq(dev_drv);
		if (dev_drv->iommu_enabled) {
			if (!dev_drv->mmu_dev) {
				dev_drv->mmu_dev =
				    rk_fb_get_sysmmu_device_by_compatible
				    (dev_drv->mmu_dts_name);
				if (dev_drv->mmu_dev) {
					rk_fb_platform_set_sysmmu
					    (dev_drv->mmu_dev, dev_drv->dev);
				} else {
					dev_err(dev_drv->dev,
						"fail get rk iommu device\n");
					return -1;
				}
			}
			/*if (dev_drv->mmu_dev)
			   rockchip_iovmm_activate(dev_drv->dev); */
		}
		rk3368_lcdc_reg_restore(lcdc_dev);
		/*if (dev_drv->iommu_enabled)
		   rk3368_lcdc_mmu_en(dev_drv); */
		if ((support_uboot_display() && (lcdc_dev->prop == PRMRY))) {
			rk3368_lcdc_set_dclk(dev_drv, 0);
			/*rk3368_lcdc_enable_irq(dev_drv);*/
		} else {
			rk3368_load_screen(dev_drv, 1);
		}
		if (dev_drv->bcsh.enable)
			rk3368_lcdc_set_bcsh(dev_drv, 1);
		spin_lock(&lcdc_dev->reg_lock);
		if (dev_drv->cur_screen->dsp_lut)
			rk3368_lcdc_set_lut(dev_drv,
					    dev_drv->cur_screen->dsp_lut);
		spin_unlock(&lcdc_dev->reg_lock);
	}

	if (win_id < ARRAY_SIZE(lcdc_win))
		rk3368_lcdc_layer_enable(lcdc_dev, win_id, open);
	else
		dev_err(lcdc_dev->dev, "invalid win id:%d\n", win_id);


	/* when all layer closed,disable clk */
	/*if ((!open) && (!lcdc_dev->atv_layer_cnt)) {
	   rk3368_lcdc_disable_irq(lcdc_dev);
	   rk3368_lcdc_reg_update(dev_drv);
	   if (dev_drv->iommu_enabled) {
	   if (dev_drv->mmu_dev)
	   rockchip_iovmm_deactivate(dev_drv->dev);
	   }
	   rk3368_lcdc_clk_disable(lcdc_dev);
	   #ifndef CONFIG_RK_FPGA
	   rockchip_clear_system_status(sys_status);
	   #endif
	   } */
	dev_drv->first_frame = 0;
	return 0;
}

static int win_0_1_display(struct lcdc_device *lcdc_dev,
			   struct rk_lcdc_win *win)
{
	u32 y_addr;
	u32 uv_addr;
	unsigned int off;

	off = win->id * 0x40;
	/*win->smem_start + win->y_offset; */
	y_addr = win->area[0].smem_start + win->area[0].y_offset;
	uv_addr = win->area[0].cbr_start + win->area[0].c_offset;
	DBG(2, "lcdc[%d]:win[%d]>>:y_addr:0x%x>>uv_addr:0x%x",
	    lcdc_dev->id, win->id, y_addr, uv_addr);
	DBG(2, ">>y_offset:0x%x>>c_offset=0x%x\n",
	    win->area[0].y_offset, win->area[0].c_offset);
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		win->area[0].y_addr = y_addr;
		win->area[0].uv_addr = uv_addr;
		lcdc_writel(lcdc_dev, WIN0_YRGB_MST + off, win->area[0].y_addr);
		lcdc_writel(lcdc_dev, WIN0_CBR_MST + off, win->area[0].uv_addr);
		if (win->area[0].fbdc_en == 1)
			lcdc_writel(lcdc_dev, IFBDC_BASE_ADDR,
				    win->area[0].y_addr);
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int win_2_3_display(struct lcdc_device *lcdc_dev,
			   struct rk_lcdc_win *win)
{
	u32 i, y_addr;
	unsigned int off;

	off = (win->id - 2) * 0x50;
	y_addr = win->area[0].smem_start + win->area[0].y_offset;
	DBG(2, "lcdc[%d]:win[%d]:", lcdc_dev->id, win->id);

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		for (i = 0; i < win->area_num; i++) {
			DBG(2, "area[%d]:yaddr:0x%x>>offset:0x%x>>\n",
			    i, win->area[i].y_addr, win->area[i].y_offset);
			win->area[i].y_addr =
			    win->area[i].smem_start + win->area[i].y_offset;
			}
		lcdc_writel(lcdc_dev, WIN2_MST0 + off, win->area[0].y_addr);
		lcdc_writel(lcdc_dev, WIN2_MST1 + off, win->area[1].y_addr);
		lcdc_writel(lcdc_dev, WIN2_MST2 + off, win->area[2].y_addr);
		lcdc_writel(lcdc_dev, WIN2_MST3 + off, win->area[3].y_addr);
		if (win->area[0].fbdc_en == 1)
			lcdc_writel(lcdc_dev, IFBDC_BASE_ADDR,
				    win->area[0].y_addr);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int hwc_display(struct lcdc_device *lcdc_dev, struct rk_lcdc_win *win)
{
	u32 y_addr;

	y_addr = win->area[0].smem_start + win->area[0].y_offset;
	DBG(2, "lcdc[%d]:hwc>>%s>>y_addr:0x%x>>\n",
	    lcdc_dev->id, __func__, y_addr);
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		win->area[0].y_addr = y_addr;
		lcdc_writel(lcdc_dev, HWC_MST, win->area[0].y_addr);
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static int rk3368_lcdc_pan_display(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
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
	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}
	if (win_id == 0) {
		win_0_1_display(lcdc_dev, win);
	} else if (win_id == 1) {
		win_0_1_display(lcdc_dev, win);
	} else if (win_id == 2) {
		win_2_3_display(lcdc_dev, win);
	} else if (win_id == 3) {
		win_2_3_display(lcdc_dev, win);
	} else if (win_id == 4) {
		hwc_display(lcdc_dev, win);
	} else {
		dev_err(dev_drv->dev, "invalid win number:%d!\n", win_id);
		return -EINVAL;
	}

#if defined(WAIT_FOR_SYNC)
	spin_lock_irqsave(&dev_drv->cpl_lock, flags);
	init_completion(&dev_drv->frame_done);
	spin_unlock_irqrestore(&dev_drv->cpl_lock, flags);
	timeout =
	    wait_for_completion_timeout(&dev_drv->frame_done,
					msecs_to_jiffies(dev_drv->
							 cur_screen->ft + 5));
	if (!timeout && (!dev_drv->frame_done.done)) {
		dev_info(dev_drv->dev, "wait for new frame start time out!\n");
		return -ETIMEDOUT;
	}
#endif
	return 0;
}

static int rk3368_lcdc_cal_scl_fac(struct rk_lcdc_win *win,
				   struct rk_screen *screen)
{
	u16 srcW = 0;
	u16 srcH = 0;
	u16 dstW = 0;
	u16 dstH = 0;
	u16 yrgb_srcW = 0;
	u16 yrgb_srcH = 0;
	u16 yrgb_dstW = 0;
	u16 yrgb_dstH = 0;
	u32 yrgb_vscalednmult = 0;
	u32 yrgb_xscl_factor = 0;
	u32 yrgb_yscl_factor = 0;
	u8 yrgb_vsd_bil_gt2 = 0;
	u8 yrgb_vsd_bil_gt4 = 0;

	u16 cbcr_srcW = 0;
	u16 cbcr_srcH = 0;
	u16 cbcr_dstW = 0;
	u16 cbcr_dstH = 0;
	u32 cbcr_vscalednmult = 0;
	u32 cbcr_xscl_factor = 0;
	u32 cbcr_yscl_factor = 0;
	u8 cbcr_vsd_bil_gt2 = 0;
	u8 cbcr_vsd_bil_gt4 = 0;
	u8 yuv_fmt = 0;

	srcW = win->area[0].xact;
	if ((screen->mode.vmode & FB_VMODE_INTERLACED) &&
	    (win->area[0].yact == 2 * win->area[0].ysize)) {
		srcH = win->area[0].yact / 2;
		yrgb_vsd_bil_gt2 = 1;
		cbcr_vsd_bil_gt2 = 1;
	} else {
		srcH = win->area[0].yact;
	}
	dstW = win->area[0].xsize;
	dstH = win->area[0].ysize;

	/*yrgb scl mode */
	yrgb_srcW = srcW;
	yrgb_srcH = srcH;
	yrgb_dstW = dstW;
	yrgb_dstH = dstH;
	if ((yrgb_dstW * 8 <= yrgb_srcW) || (yrgb_dstH * 8 <= yrgb_srcH)) {
		pr_err("ERROR: yrgb scale exceed 8,");
		pr_err("srcW=%d,srcH=%d,dstW=%d,dstH=%d\n",
		       yrgb_srcW, yrgb_srcH, yrgb_dstW, yrgb_dstH);
	}
	if (yrgb_srcW < yrgb_dstW)
		win->yrgb_hor_scl_mode = SCALE_UP;
	else if (yrgb_srcW > yrgb_dstW)
		win->yrgb_hor_scl_mode = SCALE_DOWN;
	else
		win->yrgb_hor_scl_mode = SCALE_NONE;

	if (yrgb_srcH < yrgb_dstH)
		win->yrgb_ver_scl_mode = SCALE_UP;
	else if (yrgb_srcH > yrgb_dstH)
		win->yrgb_ver_scl_mode = SCALE_DOWN;
	else
		win->yrgb_ver_scl_mode = SCALE_NONE;

	/*cbcr scl mode */
	switch (win->area[0].format) {
	case YUV422:
	case YUV422_A:
		cbcr_srcW = srcW / 2;
		cbcr_dstW = dstW;
		cbcr_srcH = srcH;
		cbcr_dstH = dstH;
		yuv_fmt = 1;
		break;
	case YUV420:
	case YUV420_A:
	case YUV420_NV21:
		cbcr_srcW = srcW / 2;
		cbcr_dstW = dstW;
		cbcr_srcH = srcH / 2;
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
		if ((cbcr_dstW * 8 <= cbcr_srcW) ||
		    (cbcr_dstH * 8 <= cbcr_srcH)) {
			pr_err("ERROR: cbcr scale exceed 8,");
			pr_err("srcW=%d,srcH=%d,dstW=%d,dstH=%d\n", cbcr_srcW,
			       cbcr_srcH, cbcr_dstW, cbcr_dstH);
		}
	}

	if (cbcr_srcW < cbcr_dstW)
		win->cbr_hor_scl_mode = SCALE_UP;
	else if (cbcr_srcW > cbcr_dstW)
		win->cbr_hor_scl_mode = SCALE_DOWN;
	else
		win->cbr_hor_scl_mode = SCALE_NONE;

	if (cbcr_srcH < cbcr_dstH)
		win->cbr_ver_scl_mode = SCALE_UP;
	else if (cbcr_srcH > cbcr_dstH)
		win->cbr_ver_scl_mode = SCALE_DOWN;
	else
		win->cbr_ver_scl_mode = SCALE_NONE;

	/*DBG(1, "srcW:%d>>srcH:%d>>dstW:%d>>dstH:%d>>\n"
	    "yrgb:src:W=%d>>H=%d,dst:W=%d>>H=%d,H_mode=%d,V_mode=%d\n"
	    "cbcr:src:W=%d>>H=%d,dst:W=%d>>H=%d,H_mode=%d,V_mode=%d\n", srcW,
	    srcH, dstW, dstH, yrgb_srcW, yrgb_srcH, yrgb_dstW, yrgb_dstH,
	    win->yrgb_hor_scl_mode, win->yrgb_ver_scl_mode, cbcr_srcW,
	    cbcr_srcH, cbcr_dstW, cbcr_dstH, win->cbr_hor_scl_mode,
	    win->cbr_ver_scl_mode);*/

	/*line buffer mode */
	if ((win->area[0].format == YUV422) ||
	    (win->area[0].format == YUV420) ||
	    (win->area[0].format == YUV420_NV21) ||
	    (win->area[0].format == YUV422_A) ||
	    (win->area[0].format == YUV420_A)) {
		if (win->cbr_hor_scl_mode == SCALE_DOWN) {
			if ((cbcr_dstW > VOP_INPUT_MAX_WIDTH / 2) ||
			    (cbcr_dstW == 0))
				pr_err("ERROR cbcr_dstW = %d,exceeds 2048\n",
				       cbcr_dstW);
			else if (cbcr_dstW > 1280)
				win->win_lb_mode = LB_YUV_3840X5;
			else
				win->win_lb_mode = LB_YUV_2560X8;
		} else {	/*SCALE_UP or SCALE_NONE */
			if ((cbcr_srcW > VOP_INPUT_MAX_WIDTH / 2) ||
			    (cbcr_srcW == 0))
				pr_err("ERROR cbcr_srcW = %d,exceeds 2048\n",
				       cbcr_srcW);
			else if (cbcr_srcW > 1280)
				win->win_lb_mode = LB_YUV_3840X5;
			else
				win->win_lb_mode = LB_YUV_2560X8;
		}
	} else {
		if (win->yrgb_hor_scl_mode == SCALE_DOWN) {
			if ((yrgb_dstW > VOP_INPUT_MAX_WIDTH) ||
			    (yrgb_dstW == 0))
				pr_err("ERROR yrgb_dstW = %d\n", yrgb_dstW);
			else if (yrgb_dstW > 2560)
				win->win_lb_mode = LB_RGB_3840X2;
			else if (yrgb_dstW > 1920)
				win->win_lb_mode = LB_RGB_2560X4;
			else if (yrgb_dstW > 1280)
				win->win_lb_mode = LB_RGB_1920X5;
			else
				win->win_lb_mode = LB_RGB_1280X8;
		} else {	/*SCALE_UP or SCALE_NONE */
			if ((yrgb_srcW > VOP_INPUT_MAX_WIDTH) ||
			    (yrgb_srcW == 0))
				pr_err("ERROR yrgb_srcW = %d\n", yrgb_srcW);
			else if (yrgb_srcW > 2560)
				win->win_lb_mode = LB_RGB_3840X2;
			else if (yrgb_srcW > 1920)
				win->win_lb_mode = LB_RGB_2560X4;
			else if (yrgb_srcW > 1280)
				win->win_lb_mode = LB_RGB_1920X5;
			else
				win->win_lb_mode = LB_RGB_1280X8;
		}
	}
	DBG(1, "win->win_lb_mode = %d;\n", win->win_lb_mode);

	/*vsd/vsu scale ALGORITHM */
	win->yrgb_hsd_mode = SCALE_DOWN_BIL;	/*not to specify */
	win->cbr_hsd_mode = SCALE_DOWN_BIL;	/*not to specify */
	win->yrgb_vsd_mode = SCALE_DOWN_BIL;	/*not to specify */
	win->cbr_vsd_mode = SCALE_DOWN_BIL;	/*not to specify */
	switch (win->win_lb_mode) {
	case LB_YUV_3840X5:
	case LB_YUV_2560X8:
	case LB_RGB_1920X5:
	case LB_RGB_1280X8:
		win->yrgb_vsu_mode = SCALE_UP_BIC;
		win->cbr_vsu_mode = SCALE_UP_BIC;
		break;
	case LB_RGB_3840X2:
		if (win->yrgb_ver_scl_mode != SCALE_NONE)
			pr_err("ERROR : not allow yrgb ver scale\n");
		if (win->cbr_ver_scl_mode != SCALE_NONE)
			pr_err("ERROR : not allow cbcr ver scale\n");
		break;
	case LB_RGB_2560X4:
		win->yrgb_vsu_mode = SCALE_UP_BIL;
		win->cbr_vsu_mode = SCALE_UP_BIL;
		break;
	default:
		pr_info("%s:un supported win_lb_mode:%d\n",
			__func__, win->win_lb_mode);
		break;
	}
	if (win->ymirror == 1)
		win->yrgb_vsd_mode = SCALE_DOWN_BIL;

	if (screen->mode.vmode & FB_VMODE_INTERLACED) {
		/*interlace mode must bill */
		win->yrgb_vsd_mode = SCALE_DOWN_BIL;
		win->cbr_vsd_mode = SCALE_DOWN_BIL;
	}
	if ((win->yrgb_ver_scl_mode == SCALE_DOWN) &&
	    (win->area[0].fbdc_en == 1)) {
		/*in this pattern,use bil mode,not support souble scd,
		use avg mode, support double scd, but aclk should be
		bigger than dclk,aclk>>dclk */
		if (yrgb_srcH >= 2 * yrgb_dstH) {
			pr_err("ERROR : fbdc mode,not support y scale down:");
			pr_err("srcH[%d] > 2 *dstH[%d]\n",
			       yrgb_srcH, yrgb_dstH);
		}
	}
	DBG(1, "yrgb:hsd=%d,vsd=%d,vsu=%d;cbcr:hsd=%d,vsd=%d,vsu=%d\n",
	    win->yrgb_hsd_mode, win->yrgb_vsd_mode, win->yrgb_vsu_mode,
	    win->cbr_hsd_mode, win->cbr_vsd_mode, win->cbr_vsu_mode);

	/*SCALE FACTOR */

	/*(1.1)YRGB HOR SCALE FACTOR */
	switch (win->yrgb_hor_scl_mode) {
	case SCALE_NONE:
		yrgb_xscl_factor = (1 << SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
		break;
	case SCALE_UP:
		yrgb_xscl_factor = GET_SCALE_FACTOR_BIC(yrgb_srcW, yrgb_dstW);
		break;
	case SCALE_DOWN:
		switch (win->yrgb_hsd_mode) {
		case SCALE_DOWN_BIL:
			yrgb_xscl_factor =
			    GET_SCALE_FACTOR_BILI_DN(yrgb_srcW, yrgb_dstW);
			break;
		case SCALE_DOWN_AVG:
			yrgb_xscl_factor =
			    GET_SCALE_FACTOR_AVRG(yrgb_srcW, yrgb_dstW);
			break;
		default:
			pr_info(
				"%s:un supported yrgb_hsd_mode:%d\n", __func__,
			       win->yrgb_hsd_mode);
			break;
		}
		break;
	default:
		pr_info("%s:un supported yrgb_hor_scl_mode:%d\n",
			__func__, win->yrgb_hor_scl_mode);
		break;
	}			/*win->yrgb_hor_scl_mode */

	/*(1.2)YRGB VER SCALE FACTOR */
	switch (win->yrgb_ver_scl_mode) {
	case SCALE_NONE:
		yrgb_yscl_factor = (1 << SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
		break;
	case SCALE_UP:
		switch (win->yrgb_vsu_mode) {
		case SCALE_UP_BIL:
			yrgb_yscl_factor =
			    GET_SCALE_FACTOR_BILI_UP(yrgb_srcH, yrgb_dstH);
			break;
		case SCALE_UP_BIC:
			if (yrgb_srcH < 3) {
				pr_err("yrgb_srcH should be");
				pr_err(" greater than 3 !!!\n");
			}
			yrgb_yscl_factor = GET_SCALE_FACTOR_BIC(yrgb_srcH,
								yrgb_dstH);
			break;
		default:
			pr_info("%s:un support yrgb_vsu_mode:%d\n",
				__func__, win->yrgb_vsu_mode);
			break;
		}
		break;
	case SCALE_DOWN:
		switch (win->yrgb_vsd_mode) {
		case SCALE_DOWN_BIL:
			yrgb_vscalednmult =
			    rk3368_get_hard_ware_vskiplines(yrgb_srcH,
							    yrgb_dstH);
			yrgb_yscl_factor =
			    GET_SCALE_FACTOR_BILI_DN_VSKIP(yrgb_srcH, yrgb_dstH,
							   yrgb_vscalednmult);
			if (yrgb_yscl_factor >= 0x2000) {
				pr_err("yrgb_yscl_factor should be ");
				pr_err("less than 0x2000,yrgb_yscl_factor=%4x;\n",
				       yrgb_yscl_factor);
			}
			if (yrgb_vscalednmult == 4) {
				yrgb_vsd_bil_gt4 = 1;
				yrgb_vsd_bil_gt2 = 0;
			} else if (yrgb_vscalednmult == 2) {
				yrgb_vsd_bil_gt4 = 0;
				yrgb_vsd_bil_gt2 = 1;
			} else {
				yrgb_vsd_bil_gt4 = 0;
				yrgb_vsd_bil_gt2 = 0;
			}
			break;
		case SCALE_DOWN_AVG:
			yrgb_yscl_factor = GET_SCALE_FACTOR_AVRG(yrgb_srcH,
								 yrgb_dstH);
			break;
		default:
			pr_info("%s:un support yrgb_vsd_mode:%d\n",
				__func__, win->yrgb_vsd_mode);
			break;
		}		/*win->yrgb_vsd_mode */
		break;
	default:
		pr_info("%s:un supported yrgb_ver_scl_mode:%d\n",
			__func__, win->yrgb_ver_scl_mode);
		break;
	}
	win->scale_yrgb_x = yrgb_xscl_factor;
	win->scale_yrgb_y = yrgb_yscl_factor;
	win->vsd_yrgb_gt4 = yrgb_vsd_bil_gt4;
	win->vsd_yrgb_gt2 = yrgb_vsd_bil_gt2;
	DBG(1, "yrgb:h_fac=%d, v_fac=%d,gt4=%d, gt2=%d\n", yrgb_xscl_factor,
	    yrgb_yscl_factor, yrgb_vsd_bil_gt4, yrgb_vsd_bil_gt2);

	/*(2.1)CBCR HOR SCALE FACTOR */
	switch (win->cbr_hor_scl_mode) {
	case SCALE_NONE:
		cbcr_xscl_factor = (1 << SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
		break;
	case SCALE_UP:
		cbcr_xscl_factor = GET_SCALE_FACTOR_BIC(cbcr_srcW, cbcr_dstW);
		break;
	case SCALE_DOWN:
		switch (win->cbr_hsd_mode) {
		case SCALE_DOWN_BIL:
			cbcr_xscl_factor =
			    GET_SCALE_FACTOR_BILI_DN(cbcr_srcW, cbcr_dstW);
			break;
		case SCALE_DOWN_AVG:
			cbcr_xscl_factor =
			    GET_SCALE_FACTOR_AVRG(cbcr_srcW, cbcr_dstW);
			break;
		default:
			pr_info("%s:un support cbr_hsd_mode:%d\n",
				__func__, win->cbr_hsd_mode);
			break;
		}
		break;
	default:
		pr_info("%s:un supported cbr_hor_scl_mode:%d\n",
			__func__, win->cbr_hor_scl_mode);
		break;
	}			/*win->cbr_hor_scl_mode */

	/*(2.2)CBCR VER SCALE FACTOR */
	switch (win->cbr_ver_scl_mode) {
	case SCALE_NONE:
		cbcr_yscl_factor = (1 << SCALE_FACTOR_DEFAULT_FIXPOINT_SHIFT);
		break;
	case SCALE_UP:
		switch (win->cbr_vsu_mode) {
		case SCALE_UP_BIL:
			cbcr_yscl_factor =
			    GET_SCALE_FACTOR_BILI_UP(cbcr_srcH, cbcr_dstH);
			break;
		case SCALE_UP_BIC:
			if (cbcr_srcH < 3) {
				pr_err("cbcr_srcH should be ");
				pr_err("greater than 3 !!!\n");
			}
			cbcr_yscl_factor = GET_SCALE_FACTOR_BIC(cbcr_srcH,
								cbcr_dstH);
			break;
		default:
			pr_info("%s:un support cbr_vsu_mode:%d\n",
				__func__, win->cbr_vsu_mode);
			break;
		}
		break;
	case SCALE_DOWN:
		switch (win->cbr_vsd_mode) {
		case SCALE_DOWN_BIL:
			cbcr_vscalednmult =
			    rk3368_get_hard_ware_vskiplines(cbcr_srcH,
							    cbcr_dstH);
			cbcr_yscl_factor =
			    GET_SCALE_FACTOR_BILI_DN_VSKIP(cbcr_srcH, cbcr_dstH,
							   cbcr_vscalednmult);
			if (cbcr_yscl_factor >= 0x2000) {
				pr_err("cbcr_yscl_factor should be less ");
				pr_err("than 0x2000,cbcr_yscl_factor=%4x;\n",
				       cbcr_yscl_factor);
			}

			if (cbcr_vscalednmult == 4) {
				cbcr_vsd_bil_gt4 = 1;
				cbcr_vsd_bil_gt2 = 0;
			} else if (cbcr_vscalednmult == 2) {
				cbcr_vsd_bil_gt4 = 0;
				cbcr_vsd_bil_gt2 = 1;
			} else {
				cbcr_vsd_bil_gt4 = 0;
				cbcr_vsd_bil_gt2 = 0;
			}
			break;
		case SCALE_DOWN_AVG:
			cbcr_yscl_factor = GET_SCALE_FACTOR_AVRG(cbcr_srcH,
								 cbcr_dstH);
			break;
		default:
			pr_info("%s:un support cbr_vsd_mode:%d\n",
				__func__, win->cbr_vsd_mode);
			break;
		}
		break;
	default:
		pr_info("%s:un supported cbr_ver_scl_mode:%d\n",
			__func__, win->cbr_ver_scl_mode);
		break;
	}
	win->scale_cbcr_x = cbcr_xscl_factor;
	win->scale_cbcr_y = cbcr_yscl_factor;
	win->vsd_cbr_gt4 = cbcr_vsd_bil_gt4;
	win->vsd_cbr_gt2 = cbcr_vsd_bil_gt2;

	DBG(1, "cbcr:h_fac=%d,v_fac=%d,gt4=%d,gt2=%d\n", cbcr_xscl_factor,
	    cbcr_yscl_factor, cbcr_vsd_bil_gt4, cbcr_vsd_bil_gt2);
	return 0;
}

static int dsp_x_pos(int mirror_en, struct rk_screen *screen,
		     struct rk_lcdc_win_area *area)
{
	int pos;

	if (screen->x_mirror && mirror_en)
		pr_err("not support both win and global mirror\n");

	if ((!mirror_en) && (!screen->x_mirror))
		pos = area->xpos + screen->mode.left_margin +
			screen->mode.hsync_len;
	else
		pos = screen->mode.xres - area->xpos -
			area->xsize + screen->mode.left_margin +
			screen->mode.hsync_len;

	return pos;
}

static int dsp_y_pos(int mirror_en, struct rk_screen *screen,
		     struct rk_lcdc_win_area *area)
{
	int pos;

	if (screen->y_mirror && mirror_en)
		pr_err("not support both win and global mirror\n");
	if (!(screen->mode.vmode & FB_VMODE_INTERLACED)) {
		if ((!mirror_en) && (!screen->y_mirror))
			pos = area->ypos + screen->mode.upper_margin +
				screen->mode.vsync_len;
		else
			pos = screen->mode.yres - area->ypos -
				area->ysize + screen->mode.upper_margin +
				screen->mode.vsync_len;
	} else {
		pos = area->ypos / 2 + screen->mode.upper_margin +
			screen->mode.vsync_len;
		area->ysize /= 2;
	}

	return pos;
}

static int win_0_1_set_par(struct lcdc_device *lcdc_dev,
			   struct rk_screen *screen, struct rk_lcdc_win *win)
{
	u32 xact = 0, yact = 0, xvir = 0, yvir = 0, xpos = 0, ypos = 0;
	u8 fmt_cfg = 0, swap_rb = 0, swap_uv = 0;
	char fmt[9] = "NULL";

	xpos = dsp_x_pos(win->xmirror, screen, &win->area[0]);
	ypos = dsp_y_pos(win->ymirror, screen, &win->area[0]);

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		rk3368_lcdc_cal_scl_fac(win, screen);	/*fac,lb,gt2,gt4 */
		switch (win->area[0].format) {
		case FBDC_RGB_565:
			fmt_cfg = 2;
			swap_rb = 0;
			win->fmt_10 = 0;
			win->area[0].fbdc_fmt_cfg = 0x05;
			break;
		case FBDC_ARGB_888:
			fmt_cfg = 0;
			swap_rb = 0;
			win->fmt_10 = 0;
			win->area[0].fbdc_fmt_cfg = 0x0c;
			break;
		case FBDC_ABGR_888:
			fmt_cfg = 0;
			swap_rb = 1;
			win->fmt_10 = 0;
			win->area[0].fbdc_fmt_cfg = 0x0c;
			break;
		case FBDC_RGBX_888:
			fmt_cfg = 0;
			swap_rb = 0;
			win->fmt_10 = 0;
			win->area[0].fbdc_fmt_cfg = 0x3a;
			break;
		case ARGB888:
			fmt_cfg = 0;
			swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case XBGR888:
		case ABGR888:
			fmt_cfg = 0;
			swap_rb = 1;
			win->fmt_10 = 0;
			break;
		case BGR888:
			fmt_cfg = 1;
			swap_rb = 1;
			win->fmt_10 = 0;
			break;
		case RGB888:
			fmt_cfg = 1;
			swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case RGB565:
			fmt_cfg = 2;
			swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV422:
			fmt_cfg = 5;
			swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV420:
			fmt_cfg = 4;
			swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV420_NV21:
			fmt_cfg = 4;
			swap_rb = 0;
			swap_uv = 1;
			win->fmt_10 = 0;
			break;
		case YUV444:
			fmt_cfg = 6;
			swap_rb = 0;
			win->fmt_10 = 0;
			break;
		case YUV422_A:
			fmt_cfg = 5;
			swap_rb = 0;
			win->fmt_10 = 1;
			break;
		case YUV420_A:
			fmt_cfg = 4;
			swap_rb = 0;
			win->fmt_10 = 1;
			break;
		case YUV444_A:
			fmt_cfg = 6;
			swap_rb = 0;
			win->fmt_10 = 1;
			break;
		default:
			dev_err(lcdc_dev->driver.dev, "%s:unsupport format!\n",
				__func__);
			break;
		}
		win->area[0].fmt_cfg = fmt_cfg;
		win->area[0].swap_rb = swap_rb;
		win->area[0].swap_uv = swap_uv;
		win->area[0].dsp_stx = xpos;
		win->area[0].dsp_sty = ypos;
		xact = win->area[0].xact;
		yact = win->area[0].yact;
		xvir = win->area[0].xvir;
		yvir = win->area[0].yvir;
	}
	if (win->area[0].fbdc_en)
		rk3368_init_fbdc_config(&lcdc_dev->driver, win->id);
	rk3368_win_0_1_reg_update(&lcdc_dev->driver, win->id);
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(1, "lcdc[%d]:win[%d]\n>>format:%s>>>xact:%d>>yact:%d>>xsize:%d",
	    lcdc_dev->id, win->id, get_format_string(win->area[0].format, fmt),
	    xact, yact, win->area[0].xsize);
	DBG(1, ">>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
	    win->area[0].ysize, xvir, yvir, xpos, ypos);

	return 0;
}


static int win_2_3_set_par(struct lcdc_device *lcdc_dev,
			   struct rk_screen *screen, struct rk_lcdc_win *win)
{
	int i;
	u8 fmt_cfg = 0, swap_rb = 0;
	char fmt[9] = "NULL";

	if (win->ymirror)
		pr_err("win[%d] not support y mirror\n", win->id);
	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		DBG(2, "lcdc[%d]:win[%d]>>\n>\n", lcdc_dev->id, win->id);
		for (i = 0; i < win->area_num; i++) {
			switch (win->area[i].format) {
			case FBDC_RGB_565:
				fmt_cfg = 2;
				swap_rb = 0;
				win->fmt_10 = 0;
				win->area[0].fbdc_fmt_cfg = 0x05;
				break;
			case FBDC_ARGB_888:
				fmt_cfg = 0;
				swap_rb = 0;
				win->fmt_10 = 0;
				win->area[0].fbdc_fmt_cfg = 0x0c;
				break;
			case FBDC_ABGR_888:
				fmt_cfg = 0;
				swap_rb = 1;
				win->fmt_10 = 0;
				win->area[0].fbdc_fmt_cfg = 0x0c;
				break;
			case FBDC_RGBX_888:
				fmt_cfg = 0;
				swap_rb = 0;
				win->fmt_10 = 0;
				win->area[0].fbdc_fmt_cfg = 0x3a;
				break;
			case ARGB888:
				fmt_cfg = 0;
				swap_rb = 0;
				break;
			case XBGR888:
			case ABGR888:
				fmt_cfg = 0;
				swap_rb = 1;
				break;
			case RGB888:
				fmt_cfg = 1;
				swap_rb = 0;
				break;
			case RGB565:
				fmt_cfg = 2;
				swap_rb = 0;
				break;
			default:
				dev_err(lcdc_dev->driver.dev,
					"%s:un supported format!\n", __func__);
				break;
			}
			win->area[i].fmt_cfg = fmt_cfg;
			win->area[i].swap_rb = swap_rb;
			win->area[i].dsp_stx =
					dsp_x_pos(win->xmirror, screen,
						  &win->area[i]);
			win->area[i].dsp_sty =
					dsp_y_pos(win->ymirror, screen,
						  &win->area[i]);
			if (((win->area[i].xact != win->area[i].xsize) ||
			     (win->area[i].yact != win->area[i].ysize)) &&
			     !(screen->mode.vmode & FB_VMODE_INTERLACED)) {
				pr_err("win[%d]->area[%d],not support scale\n",
				       win->id, i);
				pr_err("xact=%d,yact=%d,xsize=%d,ysize=%d\n",
				       win->area[i].xact, win->area[i].yact,
				       win->area[i].xsize, win->area[i].ysize);
				win->area[i].xsize = win->area[i].xact;
				win->area[i].ysize = win->area[i].yact;
			}
			DBG(2, "fmt:%s:xsize:%d>>ysize:%d>>xpos:%d>>ypos:%d\n",
			    get_format_string(win->area[i].format, fmt),
			    win->area[i].xsize, win->area[i].ysize,
			    win->area[i].xpos, win->area[i].ypos);
		}
	}
	if (win->area[0].fbdc_en)
		rk3368_init_fbdc_config(&lcdc_dev->driver, win->id);
	rk3368_win_2_3_reg_update(&lcdc_dev->driver, win->id);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int hwc_set_par(struct lcdc_device *lcdc_dev,
		       struct rk_screen *screen, struct rk_lcdc_win *win)
{
	u32 xact = 0, yact = 0, xvir = 0, yvir = 0, xpos = 0, ypos = 0;
	u8 fmt_cfg = 0, swap_rb = 0;
	char fmt[9] = "NULL";

	xpos = win->area[0].xpos + screen->mode.left_margin +
	    screen->mode.hsync_len;
	ypos = win->area[0].ypos + screen->mode.upper_margin +
	    screen->mode.vsync_len;

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		/*rk3368_lcdc_cal_scl_fac(win); *//*fac,lb,gt2,gt4 */
		switch (win->area[0].format) {
		case ARGB888:
			fmt_cfg = 0;
			swap_rb = 0;
			break;
		case XBGR888:
		case ABGR888:
			fmt_cfg = 0;
			swap_rb = 1;
			break;
		case RGB888:
			fmt_cfg = 1;
			swap_rb = 0;
			break;
		case RGB565:
			fmt_cfg = 2;
			swap_rb = 0;
			break;
		default:
			dev_err(lcdc_dev->driver.dev,
				"%s:un supported format!\n", __func__);
			break;
		}
		win->area[0].fmt_cfg = fmt_cfg;
		win->area[0].swap_rb = swap_rb;
		win->area[0].dsp_stx = xpos;
		win->area[0].dsp_sty = ypos;
		xact = win->area[0].xact;
		yact = win->area[0].yact;
		xvir = win->area[0].xvir;
		yvir = win->area[0].yvir;
	}
	rk3368_hwc_reg_update(&lcdc_dev->driver, 4);
	spin_unlock(&lcdc_dev->reg_lock);

	DBG(1, "lcdc[%d]:hwc>>%s\n>>format:%s>>>xact:%d>>yact:%d>>xsize:%d",
	    lcdc_dev->id, __func__, get_format_string(win->area[0].format, fmt),
	    xact, yact, win->area[0].xsize);
	DBG(1, ">>ysize:%d>>xvir:%d>>yvir:%d>>xpos:%d>>ypos:%d>>\n",
	    win->area[0].ysize, xvir, yvir, xpos, ypos);
	return 0;
}

static int rk3368_lcdc_set_par(struct rk_lcdc_driver *dev_drv, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = NULL;
	struct rk_screen *screen = dev_drv->cur_screen;

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}
	win = dev_drv->win[win_id];
	switch (win_id) {
	case 0:
		win_0_1_set_par(lcdc_dev, screen, win);
		break;
	case 1:
		win_0_1_set_par(lcdc_dev, screen, win);
		break;
	case 2:
		win_2_3_set_par(lcdc_dev, screen, win);
		break;
	case 3:
		win_2_3_set_par(lcdc_dev, screen, win);
		break;
	case 4:
		hwc_set_par(lcdc_dev, screen, win);
		break;
	default:
		dev_err(dev_drv->dev, "unsupported win number:%d\n", win_id);
		break;
	}
	return 0;
}

static int rk3368_lcdc_ioctl(struct rk_lcdc_driver *dev_drv, unsigned int cmd,
			     unsigned long arg, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
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
		rk3368_lcdc_clr_key_cfg(dev_drv);
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

static int rk3368_lcdc_get_backlight_device(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev = container_of(dev_drv,
						    struct lcdc_device, driver);
	struct device_node *backlight;
	struct property *prop;
	u32 brightness_levels[256];
	u32 length, max, last;

	if (lcdc_dev->backlight)
		return 0;
	backlight = of_parse_phandle(lcdc_dev->dev->of_node, "backlight", 0);
	if (backlight) {
		lcdc_dev->backlight = of_find_backlight_by_node(backlight);
		if (!lcdc_dev->backlight)
			dev_info(lcdc_dev->dev, "No find backlight device\n");
	} else {
		dev_info(lcdc_dev->dev, "No find backlight device node\n");
	}
	prop = of_find_property(backlight, "brightness-levels", &length);
	if (!prop)
		return -EINVAL;
	max = length / sizeof(u32);
	last = max - 1;
	if (!of_property_read_u32_array(backlight, "brightness-levels",
					brightness_levels, max)) {
		if (brightness_levels[0] > brightness_levels[last])
			dev_drv->cabc_pwm_pol = 1;/*negative*/
		else
			dev_drv->cabc_pwm_pol = 0;/*positive*/
	} else {
		dev_info(lcdc_dev->dev, "Can not read brightness-levels value\n");
	}
	return 0;
}

static int rk3368_lcdc_early_suspend(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 intr_clr_reg;

	if (lcdc_dev->soc_type == VOP_FULL_RK3366)
		intr_clr_reg = INTR_CLEAR_RK3366;
	else
		intr_clr_reg = INTR_CLEAR_RK3368;

	if (dev_drv->suspend_flag)
		return 0;
	/* close the backlight */
	/*rk3368_lcdc_get_backlight_device(dev_drv);
	if (lcdc_dev->backlight) {
		lcdc_dev->backlight->props.fb_blank = FB_BLANK_POWERDOWN;
		backlight_update_status(lcdc_dev->backlight);
	}*/

	dev_drv->suspend_flag = 1;
	flush_kthread_worker(&dev_drv->update_regs_worker);

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
		dev_drv->trsm_ops->disable();

	spin_lock(&lcdc_dev->reg_lock);
	if (likely(lcdc_dev->clk_on)) {
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_BLANK_EN,
			     v_DSP_BLANK_EN(1));
		lcdc_msk_reg(lcdc_dev,
			     intr_clr_reg, m_FS_INTR_CLR | m_LINE_FLAG0_INTR_CLR,
			     v_FS_INTR_CLR(1) | v_LINE_FLAG0_INTR_CLR(1));
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_OUT_ZERO,
			     v_DSP_OUT_ZERO(1));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN, v_STANDBY_EN(1));
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
	rk3368_lcdc_clk_disable(lcdc_dev);
	rk_disp_pwr_disable(dev_drv);
	return 0;
}

static int rk3368_lcdc_early_resume(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (!dev_drv->suspend_flag)
		return 0;
	rk_disp_pwr_enable(dev_drv);

	if (1/*lcdc_dev->atv_layer_cnt*/) {
		rk3368_lcdc_clk_enable(lcdc_dev);
		rk3368_lcdc_reg_restore(lcdc_dev);

		spin_lock(&lcdc_dev->reg_lock);
		if (dev_drv->cur_screen->dsp_lut)
			rk3368_lcdc_set_lut(dev_drv,
					    dev_drv->cur_screen->dsp_lut);
		if (dev_drv->cur_screen->cabc_lut && dev_drv->cabc_mode)
			rk3368_set_cabc_lut(dev_drv,
					    dev_drv->cur_screen->cabc_lut);

		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_OUT_ZERO,
			     v_DSP_OUT_ZERO(0));
		lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN, v_STANDBY_EN(0));
		lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_BLANK_EN,
			     v_DSP_BLANK_EN(0));
		lcdc_cfg_done(lcdc_dev);

		if (dev_drv->iommu_enabled) {
			/* win address maybe effect after next frame start,
			 * but mmu maybe effect right now, so we delay 50ms
			 */
			mdelay(50);
			if (dev_drv->mmu_dev)
				rockchip_iovmm_activate(dev_drv->dev);
		}

		spin_unlock(&lcdc_dev->reg_lock);
	}
	dev_drv->suspend_flag = 0;

	if (dev_drv->trsm_ops && dev_drv->trsm_ops->enable)
		dev_drv->trsm_ops->enable();
	mdelay(100);
	return 0;
}

static int rk3368_lcdc_blank(struct rk_lcdc_driver *dev_drv,
			     int win_id, int blank_mode)
{
	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		rk3368_lcdc_early_resume(dev_drv);
		break;
	case FB_BLANK_NORMAL:
		rk3368_lcdc_early_suspend(dev_drv);
		break;
	default:
		rk3368_lcdc_early_suspend(dev_drv);
		break;
	}

	dev_info(dev_drv->dev, "blank mode:%d\n", blank_mode);

	return 0;
}

static int rk3368_lcdc_get_win_state(struct rk_lcdc_driver *dev_drv,
				     int win_id, int area_id)
{
	struct lcdc_device *lcdc_dev =
		container_of(dev_drv, struct lcdc_device, driver);
	u32 win_ctrl = 0;
	u32 area_status = 0, state = 0;

	switch (win_id) {
	case 0:
		win_ctrl = lcdc_readl(lcdc_dev, WIN0_CTRL0);
		area_status = win_ctrl & m_WIN0_EN;
		break;
	case 1:
		win_ctrl = lcdc_readl(lcdc_dev, WIN1_CTRL0);
		area_status = win_ctrl & m_WIN1_EN;
		break;
	case 2:
		win_ctrl = lcdc_readl(lcdc_dev, WIN2_CTRL0);
		if (area_id == 0)
			area_status = win_ctrl & (m_WIN2_MST0_EN | m_WIN2_EN);
		if (area_id == 1)
			area_status = win_ctrl & m_WIN2_MST1_EN;
		if (area_id == 2)
			area_status = win_ctrl & m_WIN2_MST2_EN;
		if (area_id == 3)
			area_status = win_ctrl & m_WIN2_MST3_EN;
		break;
	case 3:
		win_ctrl = lcdc_readl(lcdc_dev, WIN3_CTRL0);
		if (area_id == 0)
			area_status = win_ctrl & (m_WIN3_MST0_EN | m_WIN3_EN);
		if (area_id == 1)
			area_status = win_ctrl & m_WIN3_MST1_EN;
		if (area_id == 2)
			area_status = win_ctrl & m_WIN3_MST2_EN;
		if (area_id == 3)
			area_status = win_ctrl & m_WIN3_MST3_EN;
		break;
	case 4:
		win_ctrl = lcdc_readl(lcdc_dev, HWC_CTRL0);
		area_status = win_ctrl & m_HWC_EN;
		break;
	default:
		pr_err("!!!%s,win[%d]area[%d],unsupport!!!\n",
		       __func__, win_id, area_id);
		break;
	}

	state = (area_status > 0) ? 1 : 0;
	return state;
}

static int rk3368_lcdc_get_area_num(struct rk_lcdc_driver *dev_drv,
				    unsigned int *area_support)
{
	area_support[0] = 1;
	area_support[1] = 1;
	area_support[2] = 4;
	area_support[3] = 4;

	return 0;
}

/*overlay will be do at regupdate*/
static int rk3368_lcdc_ovl_mgr(struct rk_lcdc_driver *dev_drv, int swap,
			       bool set)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_lcdc_win *win = NULL;
	int i, ovl = 0;
	unsigned int mask, val;
	int z_order_num = 0;
	int layer0_sel = 0, layer1_sel = 1, layer2_sel = 2, layer3_sel = 3;

	if (swap == 0) {
		for (i = 0; i < 4; i++) {
			win = dev_drv->win[i];
			if (win->state == 1)
				z_order_num++;
		}
		for (i = 0; i < 4; i++) {
			win = dev_drv->win[i];
			if (win->state == 0)
				win->z_order = z_order_num++;
			switch (win->z_order) {
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
	} else {
		layer0_sel = swap % 10;
		layer1_sel = swap / 10 % 10;
		layer2_sel = swap / 100 % 10;
		layer3_sel = swap / 1000;
	}

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (set) {
			mask = m_DSP_LAYER0_SEL | m_DSP_LAYER1_SEL |
			    m_DSP_LAYER2_SEL | m_DSP_LAYER3_SEL;
			val = v_DSP_LAYER0_SEL(layer0_sel) |
			    v_DSP_LAYER1_SEL(layer1_sel) |
			    v_DSP_LAYER2_SEL(layer2_sel) |
			    v_DSP_LAYER3_SEL(layer3_sel);
			lcdc_msk_reg(lcdc_dev, DSP_CTRL1, mask, val);
		} else {
			layer0_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1,
						   m_DSP_LAYER0_SEL);
			layer1_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1,
						   m_DSP_LAYER1_SEL);
			layer2_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1,
						   m_DSP_LAYER2_SEL);
			layer3_sel = lcdc_read_bit(lcdc_dev, DSP_CTRL1,
						   m_DSP_LAYER3_SEL);
			ovl = layer3_sel * 1000 + layer2_sel * 100 +
			    layer1_sel * 10 + layer0_sel;
		}
	} else {
		ovl = -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);

	return ovl;
}

static char *rk3368_lcdc_format_to_string(int format, char *fmt)
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
static ssize_t rk3368_lcdc_get_disp_info(struct rk_lcdc_driver *dev_drv,
					 char *buf, int win_id)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
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
	char format_w2_0[9] = "NULL";
	char format_w2_1[9] = "NULL";
	char format_w2_2[9] = "NULL";
	char format_w2_3[9] = "NULL";
	char format_w3_0[9] = "NULL";
	char format_w3_1[9] = "NULL";
	char format_w3_2[9] = "NULL";
	char format_w3_3[9] = "NULL";
	char dsp_buf[100];
	u32 win_ctrl, zorder, vir_info, act_info, dsp_info, dsp_st;
	u32 y_factor, uv_factor;
	u8 layer0_sel, layer1_sel, layer2_sel, layer3_sel;
	u8 w0_state, w1_state, w2_state, w3_state;
	u8 w2_0_state, w2_1_state, w2_2_state, w2_3_state;
	u8 w3_0_state, w3_1_state, w3_2_state, w3_3_state;

	u32 w0_vir_y, w0_vir_uv, w0_act_x, w0_act_y, w0_dsp_x, w0_dsp_y;
	u32 w0_st_x = h_pw_bp, w0_st_y = v_pw_bp;
	u32 w1_vir_y, w1_vir_uv, w1_act_x, w1_act_y, w1_dsp_x, w1_dsp_y;
	u32 w1_st_x = h_pw_bp, w1_st_y = v_pw_bp;
	u32 w0_y_h_fac, w0_y_v_fac, w0_uv_h_fac, w0_uv_v_fac;
	u32 w1_y_h_fac, w1_y_v_fac, w1_uv_h_fac, w1_uv_v_fac;

	u32 w2_0_vir_y, w2_1_vir_y, w2_2_vir_y, w2_3_vir_y;
	u32 w2_0_dsp_x, w2_1_dsp_x, w2_2_dsp_x, w2_3_dsp_x;
	u32 w2_0_dsp_y, w2_1_dsp_y, w2_2_dsp_y, w2_3_dsp_y;
	u32 w2_0_st_x = h_pw_bp, w2_1_st_x = h_pw_bp;
	u32 w2_2_st_x = h_pw_bp, w2_3_st_x = h_pw_bp;
	u32 w2_0_st_y = v_pw_bp, w2_1_st_y = v_pw_bp;
	u32 w2_2_st_y = v_pw_bp, w2_3_st_y = v_pw_bp;

	u32 w3_0_vir_y, w3_1_vir_y, w3_2_vir_y, w3_3_vir_y;
	u32 w3_0_dsp_x, w3_1_dsp_x, w3_2_dsp_x, w3_3_dsp_x;
	u32 w3_0_dsp_y, w3_1_dsp_y, w3_2_dsp_y, w3_3_dsp_y;
	u32 w3_0_st_x = h_pw_bp, w3_1_st_x = h_pw_bp;
	u32 w3_2_st_x = h_pw_bp, w3_3_st_x = h_pw_bp;
	u32 w3_0_st_y = v_pw_bp, w3_1_st_y = v_pw_bp;
	u32 w3_2_st_y = v_pw_bp, w3_3_st_y = v_pw_bp;
	u32 dclk_freq;
	int size = 0;

	dclk_freq = screen->mode.pixclock;
	/*rk3368_lcdc_reg_dump(dev_drv); */

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		zorder = lcdc_readl(lcdc_dev, DSP_CTRL1);
		layer0_sel = (zorder & m_DSP_LAYER0_SEL) >> 8;
		layer1_sel = (zorder & m_DSP_LAYER1_SEL) >> 10;
		layer2_sel = (zorder & m_DSP_LAYER2_SEL) >> 12;
		layer3_sel = (zorder & m_DSP_LAYER3_SEL) >> 14;
		/*WIN0 */
		win_ctrl = lcdc_readl(lcdc_dev, WIN0_CTRL0);
		w0_state = win_ctrl & m_WIN0_EN;
		fmt_id = (win_ctrl & m_WIN0_DATA_FMT) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w0);
		vir_info = lcdc_readl(lcdc_dev, WIN0_VIR);
		act_info = lcdc_readl(lcdc_dev, WIN0_ACT_INFO);
		dsp_info = lcdc_readl(lcdc_dev, WIN0_DSP_INFO);
		dsp_st = lcdc_readl(lcdc_dev, WIN0_DSP_ST);
		y_factor = lcdc_readl(lcdc_dev, WIN0_SCL_FACTOR_YRGB);
		uv_factor = lcdc_readl(lcdc_dev, WIN0_SCL_FACTOR_CBR);
		w0_vir_y = vir_info & m_WIN0_VIR_STRIDE;
		w0_vir_uv = (vir_info & m_WIN0_VIR_STRIDE_UV) >> 16;
		w0_act_x = (act_info & m_WIN0_ACT_WIDTH) + 1;
		w0_act_y = ((act_info & m_WIN0_ACT_HEIGHT) >> 16) + 1;
		w0_dsp_x = (dsp_info & m_WIN0_DSP_WIDTH) + 1;
		w0_dsp_y = ((dsp_info & m_WIN0_DSP_HEIGHT) >> 16) + 1;
		if (w0_state) {
			w0_st_x = dsp_st & m_WIN0_DSP_XST;
			w0_st_y = (dsp_st & m_WIN0_DSP_YST) >> 16;
		}
		w0_y_h_fac = y_factor & m_WIN0_HS_FACTOR_YRGB;
		w0_y_v_fac = (y_factor & m_WIN0_VS_FACTOR_YRGB) >> 16;
		w0_uv_h_fac = uv_factor & m_WIN0_HS_FACTOR_CBR;
		w0_uv_v_fac = (uv_factor & m_WIN0_VS_FACTOR_CBR) >> 16;

		/*WIN1 */
		win_ctrl = lcdc_readl(lcdc_dev, WIN1_CTRL0);
		w1_state = win_ctrl & m_WIN1_EN;
		fmt_id = (win_ctrl & m_WIN1_DATA_FMT) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w1);
		vir_info = lcdc_readl(lcdc_dev, WIN1_VIR);
		act_info = lcdc_readl(lcdc_dev, WIN1_ACT_INFO);
		dsp_info = lcdc_readl(lcdc_dev, WIN1_DSP_INFO);
		dsp_st = lcdc_readl(lcdc_dev, WIN1_DSP_ST);
		y_factor = lcdc_readl(lcdc_dev, WIN1_SCL_FACTOR_YRGB);
		uv_factor = lcdc_readl(lcdc_dev, WIN1_SCL_FACTOR_CBR);
		w1_vir_y = vir_info & m_WIN1_VIR_STRIDE;
		w1_vir_uv = (vir_info & m_WIN1_VIR_STRIDE_UV) >> 16;
		w1_act_x = (act_info & m_WIN1_ACT_WIDTH) + 1;
		w1_act_y = ((act_info & m_WIN1_ACT_HEIGHT) >> 16) + 1;
		w1_dsp_x = (dsp_info & m_WIN1_DSP_WIDTH) + 1;
		w1_dsp_y = ((dsp_info & m_WIN1_DSP_HEIGHT) >> 16) + 1;
		if (w1_state) {
			w1_st_x = dsp_st & m_WIN1_DSP_XST;
			w1_st_y = (dsp_st & m_WIN1_DSP_YST) >> 16;
		}
		w1_y_h_fac = y_factor & m_WIN1_HS_FACTOR_YRGB;
		w1_y_v_fac = (y_factor & m_WIN1_VS_FACTOR_YRGB) >> 16;
		w1_uv_h_fac = uv_factor & m_WIN1_HS_FACTOR_CBR;
		w1_uv_v_fac = (uv_factor & m_WIN1_VS_FACTOR_CBR) >> 16;
		/*WIN2 */
		win_ctrl = lcdc_readl(lcdc_dev, WIN2_CTRL0);
		w2_state = win_ctrl & m_WIN2_EN;
		w2_0_state = (win_ctrl & 0x10) >> 4;
		w2_1_state = (win_ctrl & 0x100) >> 8;
		w2_2_state = (win_ctrl & 0x1000) >> 12;
		w2_3_state = (win_ctrl & 0x10000) >> 16;
		vir_info = lcdc_readl(lcdc_dev, WIN2_VIR0_1);
		w2_0_vir_y = vir_info & m_WIN2_VIR_STRIDE0;
		w2_1_vir_y = (vir_info & m_WIN2_VIR_STRIDE1) >> 16;
		vir_info = lcdc_readl(lcdc_dev, WIN2_VIR2_3);
		w2_2_vir_y = vir_info & m_WIN2_VIR_STRIDE2;
		w2_3_vir_y = (vir_info & m_WIN2_VIR_STRIDE3) >> 16;

		fmt_id = (win_ctrl & m_WIN2_DATA_FMT0) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w2_0);
		fmt_id = (win_ctrl & m_WIN2_DATA_FMT1) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w2_1);
		fmt_id = (win_ctrl & m_WIN2_DATA_FMT2) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w2_2);
		fmt_id = (win_ctrl & m_WIN2_DATA_FMT3) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w2_3);

		dsp_info = lcdc_readl(lcdc_dev, WIN2_DSP_INFO0);
		dsp_st = lcdc_readl(lcdc_dev, WIN2_DSP_ST0);
		w2_0_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH0) + 1;
		w2_0_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT0) >> 16) + 1;
		if (w2_0_state) {
			w2_0_st_x = dsp_st & m_WIN2_DSP_XST0;
			w2_0_st_y = (dsp_st & m_WIN2_DSP_YST0) >> 16;
		}
		dsp_info = lcdc_readl(lcdc_dev, WIN2_DSP_INFO1);
		dsp_st = lcdc_readl(lcdc_dev, WIN2_DSP_ST1);
		w2_1_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH1) + 1;
		w2_1_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT1) >> 16) + 1;
		if (w2_1_state) {
			w2_1_st_x = dsp_st & m_WIN2_DSP_XST1;
			w2_1_st_y = (dsp_st & m_WIN2_DSP_YST1) >> 16;
		}
		dsp_info = lcdc_readl(lcdc_dev, WIN2_DSP_INFO2);
		dsp_st = lcdc_readl(lcdc_dev, WIN2_DSP_ST2);
		w2_2_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH2) + 1;
		w2_2_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT2) >> 16) + 1;
		if (w2_2_state) {
			w2_2_st_x = dsp_st & m_WIN2_DSP_XST2;
			w2_2_st_y = (dsp_st & m_WIN2_DSP_YST2) >> 16;
		}
		dsp_info = lcdc_readl(lcdc_dev, WIN2_DSP_INFO3);
		dsp_st = lcdc_readl(lcdc_dev, WIN2_DSP_ST3);
		w2_3_dsp_x = (dsp_info & m_WIN2_DSP_WIDTH3) + 1;
		w2_3_dsp_y = ((dsp_info & m_WIN2_DSP_HEIGHT3) >> 16) + 1;
		if (w2_3_state) {
			w2_3_st_x = dsp_st & m_WIN2_DSP_XST3;
			w2_3_st_y = (dsp_st & m_WIN2_DSP_YST3) >> 16;
		}

		/*WIN3 */
		win_ctrl = lcdc_readl(lcdc_dev, WIN3_CTRL0);
		w3_state = win_ctrl & m_WIN3_EN;
		w3_0_state = (win_ctrl & m_WIN3_MST0_EN) >> 4;
		w3_1_state = (win_ctrl & m_WIN3_MST1_EN) >> 8;
		w3_2_state = (win_ctrl & m_WIN3_MST2_EN) >> 12;
		w3_3_state = (win_ctrl & m_WIN3_MST3_EN) >> 16;
		vir_info = lcdc_readl(lcdc_dev, WIN3_VIR0_1);
		w3_0_vir_y = vir_info & m_WIN3_VIR_STRIDE0;
		w3_1_vir_y = (vir_info & m_WIN3_VIR_STRIDE1) >> 16;
		vir_info = lcdc_readl(lcdc_dev, WIN3_VIR2_3);
		w3_2_vir_y = vir_info & m_WIN3_VIR_STRIDE2;
		w3_3_vir_y = (vir_info & m_WIN3_VIR_STRIDE3) >> 16;
		fmt_id = (win_ctrl & m_WIN3_DATA_FMT0) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w3_0);
		fmt_id = (win_ctrl & m_WIN3_DATA_FMT1) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w3_1);
		fmt_id = (win_ctrl & m_WIN3_DATA_FMT2) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w3_2);
		fmt_id = (win_ctrl & m_WIN3_DATA_FMT3) >> 1;
		rk3368_lcdc_format_to_string(fmt_id, format_w3_3);
		dsp_info = lcdc_readl(lcdc_dev, WIN3_DSP_INFO0);
		dsp_st = lcdc_readl(lcdc_dev, WIN3_DSP_ST0);
		w3_0_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH0) + 1;
		w3_0_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT0) >> 16) + 1;
		if (w3_0_state) {
			w3_0_st_x = dsp_st & m_WIN3_DSP_XST0;
			w3_0_st_y = (dsp_st & m_WIN3_DSP_YST0) >> 16;
		}

		dsp_info = lcdc_readl(lcdc_dev, WIN3_DSP_INFO1);
		dsp_st = lcdc_readl(lcdc_dev, WIN3_DSP_ST1);
		w3_1_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH1) + 1;
		w3_1_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT1) >> 16) + 1;
		if (w3_1_state) {
			w3_1_st_x = dsp_st & m_WIN3_DSP_XST1;
			w3_1_st_y = (dsp_st & m_WIN3_DSP_YST1) >> 16;
		}

		dsp_info = lcdc_readl(lcdc_dev, WIN3_DSP_INFO2);
		dsp_st = lcdc_readl(lcdc_dev, WIN3_DSP_ST2);
		w3_2_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH2) + 1;
		w3_2_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT2) >> 16) + 1;
		if (w3_2_state) {
			w3_2_st_x = dsp_st & m_WIN3_DSP_XST2;
			w3_2_st_y = (dsp_st & m_WIN3_DSP_YST2) >> 16;
		}

		dsp_info = lcdc_readl(lcdc_dev, WIN3_DSP_INFO3);
		dsp_st = lcdc_readl(lcdc_dev, WIN3_DSP_ST3);
		w3_3_dsp_x = (dsp_info & m_WIN3_DSP_WIDTH3) + 1;
		w3_3_dsp_y = ((dsp_info & m_WIN3_DSP_HEIGHT3) >> 16) + 1;
		if (w3_3_state) {
			w3_3_st_x = dsp_st & m_WIN3_DSP_XST3;
			w3_3_st_y = (dsp_st & m_WIN3_DSP_YST3) >> 16;
		}

	} else {
		spin_unlock(&lcdc_dev->reg_lock);
		return -EPERM;
	}
	spin_unlock(&lcdc_dev->reg_lock);
	size += snprintf(dsp_buf, 80,
		"z-order:\n  win[%d]\n  win[%d]\n  win[%d]\n  win[%d]\n",
		layer3_sel, layer2_sel, layer1_sel, layer0_sel);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	/*win0*/
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
		 w0_st_x-h_pw_bp, w0_st_y-v_pw_bp, w0_y_h_fac, w0_y_v_fac);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 "uv_h_fac:%5d, uv_v_fac:%5d\n  y_addr:0x%08x,    uv_addr:0x%08x\n",
		 w0_uv_h_fac, w0_uv_v_fac, lcdc_readl(lcdc_dev, WIN0_YRGB_MST),
		 lcdc_readl(lcdc_dev, WIN0_CBR_MST));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*win1*/
	size += snprintf(dsp_buf, 80,
		 "win1:\n  state:%d, fmt:%7s\n  y_vir:%4d, uv_vir:%4d,",
		 w1_state, format_w1, w1_vir_y, w1_vir_uv);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 " x_act  :%5d, y_act  :%5d, dsp_x   :%5d, dsp_y   :%5d\n",
		 w1_act_x, w1_act_y, w1_dsp_x, w1_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 "  x_st :%4d, y_st  :%4d, y_h_fac:%5d, y_v_fac:%5d, ",
		 w1_st_x-h_pw_bp, w1_st_y-v_pw_bp, w1_y_h_fac, w1_y_v_fac);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	size += snprintf(dsp_buf, 80,
		 "uv_h_fac:%5d, uv_v_fac:%5d\n  y_addr:0x%08x,    uv_addr:0x%08x\n",
		 w1_uv_h_fac, w1_uv_v_fac, lcdc_readl(lcdc_dev, WIN1_YRGB_MST),
		 lcdc_readl(lcdc_dev, WIN1_CBR_MST));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*win2*/
	size += snprintf(dsp_buf, 80,
		 "win2:\n  state:%d\n",
		 w2_state);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	/*area 0*/
	size += snprintf(dsp_buf, 80,
		 "  area0: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w2_0_state, format_w2_0, w2_0_dsp_x, w2_0_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w2_0_st_x - h_pw_bp, w2_0_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN2_MST0));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*area 1*/
	size += snprintf(dsp_buf, 80,
		 "  area1: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w2_1_state, format_w2_1, w2_1_dsp_x, w2_1_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w2_1_st_x - h_pw_bp, w2_1_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN2_MST1));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*area 2*/
	size += snprintf(dsp_buf, 80,
		 "  area2: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w2_2_state, format_w2_2, w2_2_dsp_x, w2_2_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w2_2_st_x - h_pw_bp, w2_2_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN2_MST2));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*area 3*/
	size += snprintf(dsp_buf, 80,
		 "  area3: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w2_3_state, format_w2_3, w2_3_dsp_x, w2_3_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w2_3_st_x - h_pw_bp, w2_3_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN2_MST3));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*win3*/
	size += snprintf(dsp_buf, 80,
		 "win3:\n  state:%d\n",
		 w3_state);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	/*area 0*/
	size += snprintf(dsp_buf, 80,
		 "  area0: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w3_0_state, format_w3_0, w3_0_dsp_x, w3_0_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w3_0_st_x - h_pw_bp, w3_0_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN3_MST0));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*area 1*/
	size += snprintf(dsp_buf, 80,
		 "  area1: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w3_1_state, format_w3_1, w3_1_dsp_x, w3_1_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w3_1_st_x - h_pw_bp, w3_1_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN3_MST1));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*area 2*/
	size += snprintf(dsp_buf, 80,
		 "  area2: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w3_2_state, format_w3_2, w3_2_dsp_x, w3_2_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w3_2_st_x - h_pw_bp, w3_2_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN3_MST2));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	/*area 3*/
	size += snprintf(dsp_buf, 80,
		 "  area3: state:%d, fmt:%7s, dsp_x:%4d, dsp_y:%4d,",
		 w3_3_state, format_w3_3, w3_3_dsp_x, w3_3_dsp_y);
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));
	size += snprintf(dsp_buf, 80,
		 " x_st:%4d, y_st:%4d, y_addr:0x%08x\n",
		 w3_3_st_x - h_pw_bp, w3_3_st_y - v_pw_bp,
		 lcdc_readl(lcdc_dev, WIN3_MST3));
	strcat(buf, dsp_buf);
	memset(dsp_buf, 0, sizeof(dsp_buf));

	return size;
}

static int rk3368_lcdc_fps_mgr(struct rk_lcdc_driver *dev_drv, int fps,
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
	lcdc_dev->pixclock = pixclock;
	dev_drv->pixclock = lcdc_dev->pixclock;
	fps = rk_fb_calc_fps(screen, pixclock);
	screen->ft = 1000 / fps;	/*one frame time in ms */

	if (set)
		dev_info(dev_drv->dev, "%s:dclk:%lu,fps:%d\n", __func__,
			 clk_get_rate(lcdc_dev->dclk), fps);

	return fps;
}

static int rk3368_fb_win_remap(struct rk_lcdc_driver *dev_drv, u16 order)
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

static int rk3368_lcdc_get_win_id(struct rk_lcdc_driver *dev_drv,
				  const char *id)
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

static int rk3368_lcdc_config_done(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	int i;
	unsigned int mask, val, fbdc_en = 0;
	struct rk_lcdc_win *win = NULL;
	u32 line_scane_num, dsp_vs_st_f1;

	if (lcdc_dev->driver.cur_screen->mode.vmode & FB_VMODE_INTERLACED) {
		dsp_vs_st_f1 = lcdc_readl(lcdc_dev, DSP_VS_ST_END_F1) >> 16;
		for (i = 0; i < 1000; i++) {
			line_scane_num =
				lcdc_readl(lcdc_dev, SCAN_LINE_NUM) & 0x1fff;
			if (line_scane_num > dsp_vs_st_f1 + 1)
				udelay(50);
			else
				break;
		}
	}

	spin_lock(&lcdc_dev->reg_lock);
	rk3368_lcdc_post_cfg(dev_drv);
	lcdc_msk_reg(lcdc_dev, SYS_CTRL, m_STANDBY_EN,
		     v_STANDBY_EN(lcdc_dev->standby));
	for (i = 0; i < 4; i++) {
		win = dev_drv->win[i];
		fbdc_en |= win->area[0].fbdc_en;
		if ((win->state == 0) && (win->last_state == 1)) {
			switch (win->id) {
			case 0:
				/*lcdc_writel(lcdc_dev,WIN0_CTRL1,0x0);
				   for rk3288 to fix hw bug? */
				mask = m_WIN0_EN;
				val = v_WIN0_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN0_CTRL0, mask, val);
				break;
			case 1:
				/*lcdc_writel(lcdc_dev,WIN1_CTRL1,0x0);
				   for rk3288 to fix hw bug? */
				mask = m_WIN1_EN;
				val = v_WIN1_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN1_CTRL0, mask, val);
				break;
			case 2:
				mask = m_WIN2_EN | m_WIN2_MST0_EN |
				    m_WIN2_MST1_EN |
				    m_WIN2_MST2_EN | m_WIN2_MST3_EN;
				val = v_WIN2_EN(0) | v_WIN2_MST0_EN(0) |
				    v_WIN2_MST1_EN(0) |
				    v_WIN2_MST2_EN(0) | v_WIN2_MST3_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN2_CTRL0, mask, val);
				break;
			case 3:
				mask = m_WIN3_EN | m_WIN3_MST0_EN |
				    m_WIN3_MST1_EN |
				    m_WIN3_MST2_EN | m_WIN3_MST3_EN;
				val = v_WIN3_EN(0) | v_WIN3_MST0_EN(0) |
				    v_WIN3_MST1_EN(0) |
				    v_WIN3_MST2_EN(0) | v_WIN3_MST3_EN(0);
				lcdc_msk_reg(lcdc_dev, WIN3_CTRL0, mask, val);
				break;
			case 4:
				mask = m_HWC_EN;
				val = v_HWC_EN(0);
				lcdc_msk_reg(lcdc_dev, HWC_CTRL0, mask, val);
				break;
			default:
				break;
			}
		}
		win->last_state = win->state;
	}
	if (lcdc_dev->soc_type == VOP_FULL_RK3368) {
		mask = m_IFBDC_CTRL_FBDC_EN;
		val = v_IFBDC_CTRL_FBDC_EN(fbdc_en);
		lcdc_msk_reg(lcdc_dev, IFBDC_CTRL, mask, val);
	}
	lcdc_cfg_done(lcdc_dev);
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3368_lcdc_set_irq_to_cpu(struct rk_lcdc_driver *dev_drv,
				      int enable)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	if (enable)
		enable_irq(lcdc_dev->irq);
	else
		disable_irq(lcdc_dev->irq);
	return 0;
}

int rk3368_lcdc_poll_vblank(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 int_reg_val;
	int ret;
	u32 intr_status_reg, intr_clear_reg;

	if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
		intr_status_reg = INTR_STATUS_RK3366;
		intr_clear_reg = INTR_CLEAR_RK3366;
	} else {
		intr_status_reg = INTR_STATUS_RK3368;
		intr_clear_reg = INTR_CLEAR_RK3368;
	}

	if (lcdc_dev->clk_on && (!dev_drv->suspend_flag)) {
		int_reg_val = lcdc_readl(lcdc_dev, intr_status_reg);
		if (int_reg_val & m_LINE_FLAG0_INTR_STS) {
			lcdc_dev->driver.frame_time.last_framedone_t =
			    lcdc_dev->driver.frame_time.framedone_t;
			lcdc_dev->driver.frame_time.framedone_t = cpu_clock(0);
			lcdc_msk_reg(lcdc_dev, intr_clear_reg,
				     m_LINE_FLAG0_INTR_CLR,
				     v_LINE_FLAG0_INTR_CLR(1));
			ret = RK_LF_STATUS_FC;
		} else {
			ret = RK_LF_STATUS_FR;
		}
	} else {
		ret = RK_LF_STATUS_NC;
	}

	return ret;
}

static int rk3368_lcdc_get_dsp_addr(struct rk_lcdc_driver *dev_drv,
				    unsigned int dsp_addr[][4])
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		dsp_addr[0][0] = lcdc_readl(lcdc_dev, WIN0_YRGB_MST);
		dsp_addr[1][0] = lcdc_readl(lcdc_dev, WIN1_YRGB_MST);
		dsp_addr[2][0] = lcdc_readl(lcdc_dev, WIN2_MST0);
		dsp_addr[2][1] = lcdc_readl(lcdc_dev, WIN2_MST1);
		dsp_addr[2][2] = lcdc_readl(lcdc_dev, WIN2_MST2);
		dsp_addr[2][3] = lcdc_readl(lcdc_dev, WIN2_MST3);
		dsp_addr[3][0] = lcdc_readl(lcdc_dev, WIN3_MST0);
		dsp_addr[3][1] = lcdc_readl(lcdc_dev, WIN3_MST1);
		dsp_addr[3][2] = lcdc_readl(lcdc_dev, WIN3_MST2);
		dsp_addr[3][3] = lcdc_readl(lcdc_dev, WIN3_MST3);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3368_lcdc_set_dsp_cabc(struct rk_lcdc_driver *dev_drv,
				    int mode, int calc, int up,
				    int down, int global)
{
	struct lcdc_device *lcdc_dev =
		container_of(dev_drv, struct lcdc_device, driver);
	struct rk_screen *screen = dev_drv->cur_screen;
	u32 total_pixel, calc_pixel, stage_up, stage_down;
	u32 pixel_num, global_dn;
	u32 mask = 0, val = 0;
	int *cabc_lut = NULL;

	if (screen->type == SCREEN_HDMI && screen->type == SCREEN_TVOUT) {
		pr_err("screen type is %d, not support cabc\n", screen->type);
		return 0;
	} else if (!screen->cabc_lut) {
		pr_err("screen cabc lut not config, so not open cabc\n");
		return 0;
	} else {
		cabc_lut = screen->cabc_lut;
	}

	if (mode == 0) {
		spin_lock(&lcdc_dev->reg_lock);
		if (lcdc_dev->clk_on) {
			lcdc_msk_reg(lcdc_dev, CABC_CTRL0,
				     m_CABC_EN, v_CABC_EN(0));
			lcdc_cfg_done(lcdc_dev);
		}
		pr_info("mode = 0, close cabc\n");
		dev_drv->cabc_mode = mode;
		spin_unlock(&lcdc_dev->reg_lock);
		return 0;
	}
	if (dev_drv->cabc_mode == 0)
		rk3368_set_cabc_lut(dev_drv, dev_drv->cur_screen->cabc_lut);

	total_pixel = screen->mode.xres * screen->mode.yres;
	pixel_num = 1000 - calc;
	calc_pixel = (total_pixel * pixel_num) / 1000;
	stage_up = up;
	stage_down = down;
	global_dn = global;
	pr_info("enable cabc:mode=%d, calc=%d, up=%d, down=%d, global=%d\n",
		mode, calc, stage_up, stage_down, global_dn);

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		mask = m_CABC_EN | m_CABC_HANDLE_EN | m_PWM_CONFIG_MODE |
			m_CABC_CALC_PIXEL_NUM;
		val = v_CABC_EN(1) | v_CABC_HANDLE_EN(1) |
			v_PWM_CONFIG_MODE(STAGE_BY_STAGE) |
			v_CABC_CALC_PIXEL_NUM(calc_pixel);
		lcdc_msk_reg(lcdc_dev, CABC_CTRL0, mask, val);

		mask = m_CABC_LUT_EN | m_CABC_TOTAL_PIXEL_NUM;
		val = v_CABC_LUT_EN(1) | v_CABC_TOTAL_PIXEL_NUM(total_pixel);
		lcdc_msk_reg(lcdc_dev, CABC_CTRL1, mask, val);

		mask = m_CABC_STAGE_DOWN | m_CABC_STAGE_UP |
			m_CABC_STAGE_MODE | m_MAX_SCALE_CFG_VALUE |
			m_MAX_SCALE_CFG_ENABLE;
		val = v_CABC_STAGE_DOWN(stage_down) |
			v_CABC_STAGE_UP(stage_up) |
			v_CABC_STAGE_MODE(0) | v_MAX_SCALE_CFG_VALUE(1) |
			v_MAX_SCALE_CFG_ENABLE(0);
		lcdc_msk_reg(lcdc_dev, CABC_CTRL2, mask, val);

		mask = m_CABC_GLOBAL_DN | m_CABC_GLOBAL_DN_LIMIT_EN;
		val = v_CABC_GLOBAL_DN(global_dn) |
			v_CABC_GLOBAL_DN_LIMIT_EN(1);
		lcdc_msk_reg(lcdc_dev, CABC_CTRL3, mask, val);
		lcdc_cfg_done(lcdc_dev);
		dev_drv->cabc_mode = mode;
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
static int rk3368_lcdc_get_bcsh_hue(struct rk_lcdc_driver *dev_drv,
				    bcsh_hue_mode mode)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val = 0;

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

static int rk3368_lcdc_set_bcsh_hue(struct rk_lcdc_driver *dev_drv,
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

static int rk3368_lcdc_set_bcsh_bcs(struct rk_lcdc_driver *dev_drv,
				    bcsh_bcs_mode mode, int value)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask = 0, val = 0;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		switch (mode) {
		case BRIGHTNESS:
			/*user: from 0 to 255,typical is 128,
			 *vop,6bit: from 0 to 64, typical is 32*/
			value /= 4;
			if (value < 0x20)
				value += 0x20;
			else if (value >= 0x20)
				value = value - 0x20;
			mask = m_BCSH_BRIGHTNESS;
			val = v_BCSH_BRIGHTNESS(value);
			break;
		case CONTRAST:
			/*user: from 0 to 510,typical is 256
			 *vop,9bit, from 0 to 511,typical is 256*/
			value = 512 - value;
			mask = m_BCSH_CONTRAST;
			val = v_BCSH_CONTRAST(value);
			break;
		case SAT_CON:
			/*from 0 to 1024,typical is 512
			 *vop,9bit, from 0 to 512, typical is 256*/
			value /= 2;
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

static int rk3368_lcdc_get_bcsh_bcs(struct rk_lcdc_driver *dev_drv,
				    bcsh_bcs_mode mode)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val = 0;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		val = lcdc_readl(lcdc_dev, BCSH_BCS);
		switch (mode) {
		case BRIGHTNESS:
			val &= m_BCSH_BRIGHTNESS;
			if (val >= 0x20)
				val -= 0x20;
			else
				val += 0x20;
			val <<= 2;
			break;
		case CONTRAST:
			val &= m_BCSH_CONTRAST;
			val >>= 8;
			break;
		case SAT_CON:
			val &= m_BCSH_SAT_CON;
			val >>= 20;
			val <<= 1;
			break;
		default:
			break;
		}
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return val;
}

static int rk3368_lcdc_open_bcsh(struct rk_lcdc_driver *dev_drv, bool open)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 mask, val;

	spin_lock(&lcdc_dev->reg_lock);
	if (lcdc_dev->clk_on) {
		if (open) {
			lcdc_writel(lcdc_dev, BCSH_COLOR_BAR, 0x1);
			lcdc_writel(lcdc_dev, BCSH_BCS, 0xd0010000);
			lcdc_writel(lcdc_dev, BCSH_H, 0x01000000);
			dev_drv->bcsh.enable = 1;
		} else {
			mask = m_BCSH_EN;
			val = v_BCSH_EN(0);
			lcdc_msk_reg(lcdc_dev, BCSH_COLOR_BAR, mask, val);
			dev_drv->bcsh.enable = 0;
		}
		rk3368_lcdc_bcsh_path_sel(dev_drv);
		lcdc_cfg_done(lcdc_dev);
	}
	spin_unlock(&lcdc_dev->reg_lock);
	return 0;
}

static int rk3368_lcdc_set_bcsh(struct rk_lcdc_driver *dev_drv, bool enable)
{
	if (!enable || !dev_drv->bcsh.enable) {
		rk3368_lcdc_open_bcsh(dev_drv, false);
		return 0;
	}

	if (dev_drv->bcsh.brightness <= 255 ||
	    dev_drv->bcsh.contrast <= 510 ||
	    dev_drv->bcsh.sat_con <= 1015 ||
	    (dev_drv->bcsh.sin_hue <= 511 && dev_drv->bcsh.cos_hue <= 511)) {
		rk3368_lcdc_open_bcsh(dev_drv, true);
		if (dev_drv->bcsh.brightness <= 255)
			rk3368_lcdc_set_bcsh_bcs(dev_drv, BRIGHTNESS,
						 dev_drv->bcsh.brightness);
		if (dev_drv->bcsh.contrast <= 510)
			rk3368_lcdc_set_bcsh_bcs(dev_drv, CONTRAST,
						 dev_drv->bcsh.contrast);
		if (dev_drv->bcsh.sat_con <= 1015)
			rk3368_lcdc_set_bcsh_bcs(dev_drv, SAT_CON,
						 dev_drv->bcsh.sat_con);
		if (dev_drv->bcsh.sin_hue <= 511 &&
		    dev_drv->bcsh.cos_hue <= 511)
			rk3368_lcdc_set_bcsh_hue(dev_drv,
						 dev_drv->bcsh.sin_hue,
						 dev_drv->bcsh.cos_hue);
	}
	return 0;
}

static int __maybe_unused
rk3368_lcdc_dsp_black(struct rk_lcdc_driver *dev_drv, int enable)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (enable) {
		spin_lock(&lcdc_dev->reg_lock);
		if (likely(lcdc_dev->clk_on)) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_BLACK_EN,
				     v_DSP_BLACK_EN(1));
			lcdc_cfg_done(lcdc_dev);
		}
		spin_unlock(&lcdc_dev->reg_lock);
	} else {
		spin_lock(&lcdc_dev->reg_lock);
		if (likely(lcdc_dev->clk_on)) {
			lcdc_msk_reg(lcdc_dev, DSP_CTRL0, m_DSP_BLACK_EN,
				     v_DSP_BLACK_EN(0));

			lcdc_cfg_done(lcdc_dev);
		}
		spin_unlock(&lcdc_dev->reg_lock);
	}

	return 0;
}


static int rk3368_lcdc_wait_frame_start(struct rk_lcdc_driver *dev_drv,
					int enable)
{
	u32 line_scane_num, vsync_end, vact_end;
	u32 interlace_mode;

	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}
	if (0 == enable) {
		interlace_mode = lcdc_read_bit(lcdc_dev, DSP_CTRL0,
					       m_DSP_INTERLACE);
		if (interlace_mode) {
			vsync_end = lcdc_readl(lcdc_dev, DSP_VS_ST_END_F1) &
					m_DSP_VS_END_F1;
			vact_end = lcdc_readl(lcdc_dev, DSP_VACT_ST_END_F1) &
					m_DSP_VACT_END_F1;
		} else {
			vsync_end = lcdc_readl(lcdc_dev, DSP_VTOTAL_VS_END) &
					m_DSP_VS_PW;
			vact_end = lcdc_readl(lcdc_dev, DSP_VACT_ST_END) &
					m_DSP_VACT_END;
		}
		while (1) {
			line_scane_num = lcdc_readl(lcdc_dev, SCAN_LINE_NUM) &
					0x1fff;
			if ((line_scane_num > vsync_end) &&
			    (line_scane_num <= vact_end - 100))
				break;
		}
		return 0;
	} else if (1 == enable) {
		line_scane_num = lcdc_readl(lcdc_dev, SCAN_LINE_NUM) & 0x1fff;
		return line_scane_num;
	}

	return 0;
}

static int rk3368_lcdc_backlight_close(struct rk_lcdc_driver *dev_drv,
				       int enable)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}
	rk3368_lcdc_get_backlight_device(dev_drv);

	if (enable) {
		/* close the backlight */
		if (lcdc_dev->backlight) {
			lcdc_dev->backlight->props.power = FB_BLANK_POWERDOWN;
			backlight_update_status(lcdc_dev->backlight);
		}
		if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
			dev_drv->trsm_ops->disable();
	} else {
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

static int rk3368_lcdc_set_overscan(struct rk_lcdc_driver *dev_drv,
				    struct overscan *overscan)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}
	/*rk3368_lcdc_post_cfg(dev_drv);*/

	return 0;
}

static int rk3368_lcdc_extern_func(struct rk_lcdc_driver *dev_drv,
				   int cmd)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	u32 val;

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}

	switch (cmd) {
	case GET_PAGE_FAULT:
		val = lcdc_readl(lcdc_dev, MMU_INT_RAWSTAT);
		if ((val & 0x1) == 1) {
			if ((val & 0x2) == 1)
				pr_info("val=0x%x,vop iommu bus error\n", val);
			else
				return 1;
		}
		break;
	case CLR_PAGE_FAULT:
		lcdc_writel(lcdc_dev, MMU_INT_CLEAR, 0x3);
		break;
	case UNMASK_PAGE_FAULT:
		lcdc_writel(lcdc_dev, MMU_INT_MASK, 0x2);
		break;
	default:
		break;
	}

	return 0;
}

static int rk3368_lcdc_set_wb(struct rk_lcdc_driver *dev_drv)
{
	struct lcdc_device *lcdc_dev =
	    container_of(dev_drv, struct lcdc_device, driver);
	struct rk_fb_reg_wb_data *wb_data;
	u32 src_w, src_h, dst_w, dst_h, fmt_cfg;
	u32 xscale_en = 0, x_scale_fac = 0, y_throw = 0;
	u32 csc_mode = 0, rgb2yuv = 0, dither_en = 0;

	if (unlikely(!lcdc_dev->clk_on)) {
		pr_info("%s,clk_on = %d\n", __func__, lcdc_dev->clk_on);
		return 0;
	}
	wb_data = &dev_drv->wb_data;
	if ((wb_data->xsize == 0) || (wb_data->ysize == 0))
		return 0;

	src_w = dev_drv->cur_screen->mode.xres;
	src_h = dev_drv->cur_screen->mode.yres;
	dst_w = wb_data->xsize;
	dst_h = wb_data->ysize;
	if (!IS_ALIGNED(dst_w, RK3366_WB_ALIGN))
		pr_info("dst_w: %d not align 16 pixel\n", dst_w);

	if (src_w > dst_w)
		xscale_en = 1;
	else if (src_w < dst_w)
		dst_w = src_w;
	else
		xscale_en = 0;
	if (wb_data->state && xscale_en)
		x_scale_fac = GET_SCALE_FACTOR_BILI_DN(src_w, dst_w);
	if ((src_h >= 2 * dst_h) && (dst_h != 0))
		y_throw = 1;
	else
		y_throw = 0;
	switch (wb_data->data_format) {
	case XRGB888:
	case XBGR888:
		fmt_cfg = 0;
		break;
	case RGB888:
	case BGR888:
		fmt_cfg = 1;
		break;
	case RGB565:
	case BGR565:
		fmt_cfg = 2;
		dither_en = 1;
		break;
	case YUV420:
		fmt_cfg = 4;
		if (dev_drv->overlay_mode == VOP_RGB_DOMAIN)
			rgb2yuv = 1;
		if ((src_w < 1280) && (src_h < 720))
			csc_mode = VOP_R2Y_CSC_BT601;
		else
			csc_mode = VOP_R2Y_CSC_BT709;
		break;
	default:
		fmt_cfg = 0;
		pr_info("unsupport fmt: %d\n", wb_data->data_format);
		break;
	}
	spin_lock(&lcdc_dev->reg_lock);
	lcdc_msk_reg(lcdc_dev, WB_CTRL0,
		     m_WB_EN | m_WB_FMT | m_WB_XPSD_BIL_EN |
		     m_WB_YTHROW_EN | m_WB_RGB2YUV_EN | m_WB_RGB2YUV_MODE |
		     m_WB_DITHER_EN,
		     v_WB_EN(wb_data->state) | v_WB_FMT(fmt_cfg) |
		     v_WB_XPSD_BIL_EN(xscale_en) |
		     v_WB_YTHROW_EN(y_throw) | v_WB_RGB2YUV_EN(rgb2yuv) |
		     v_WB_RGB2YUV_MODE(csc_mode) | v_WB_DITHER_EN(dither_en));
	lcdc_msk_reg(lcdc_dev, WB_CTRL1,
		     m_WB_WIDTH | m_WB_XPSD_BIL_FACTOR,
		     v_WB_WIDTH(dst_w) |
		     v_WB_XPSD_BIL_FACTOR(x_scale_fac));
	lcdc_writel(lcdc_dev, WB_YRGB_MST, wb_data->smem_start);
	lcdc_writel(lcdc_dev, WB_CBR_MST, wb_data->cbr_start);
	spin_unlock(&lcdc_dev->reg_lock);

	return 0;
}

static struct rk_lcdc_drv_ops lcdc_drv_ops = {
	.open = rk3368_lcdc_open,
	.win_direct_en = rk3368_lcdc_win_direct_en,
	.load_screen = rk3368_load_screen,
	.get_dspbuf_info = rk3368_get_dspbuf_info,
	.post_dspbuf = rk3368_post_dspbuf,
	.set_par = rk3368_lcdc_set_par,
	.pan_display = rk3368_lcdc_pan_display,
	.direct_set_addr = rk3368_lcdc_direct_set_win_addr,
	/*.lcdc_reg_update = rk3368_lcdc_reg_update,*/
	.blank = rk3368_lcdc_blank,
	.ioctl = rk3368_lcdc_ioctl,
	.suspend = rk3368_lcdc_early_suspend,
	.resume = rk3368_lcdc_early_resume,
	.get_win_state = rk3368_lcdc_get_win_state,
	.area_support_num = rk3368_lcdc_get_area_num,
	.ovl_mgr = rk3368_lcdc_ovl_mgr,
	.get_disp_info = rk3368_lcdc_get_disp_info,
	.fps_mgr = rk3368_lcdc_fps_mgr,
	.fb_get_win_id = rk3368_lcdc_get_win_id,
	.fb_win_remap = rk3368_fb_win_remap,
	.set_dsp_lut = rk3368_lcdc_set_lut,
	.set_cabc_lut = rk3368_set_cabc_lut,
	.poll_vblank = rk3368_lcdc_poll_vblank,
	.get_dsp_addr = rk3368_lcdc_get_dsp_addr,
	.set_dsp_cabc = rk3368_lcdc_set_dsp_cabc,
	.set_dsp_bcsh_hue = rk3368_lcdc_set_bcsh_hue,
	.set_dsp_bcsh_bcs = rk3368_lcdc_set_bcsh_bcs,
	.get_dsp_bcsh_hue = rk3368_lcdc_get_bcsh_hue,
	.get_dsp_bcsh_bcs = rk3368_lcdc_get_bcsh_bcs,
	.open_bcsh = rk3368_lcdc_open_bcsh,
	.dump_reg = rk3368_lcdc_reg_dump,
	.cfg_done = rk3368_lcdc_config_done,
	.set_irq_to_cpu = rk3368_lcdc_set_irq_to_cpu,
	/*.dsp_black = rk3368_lcdc_dsp_black,*/
	.backlight_close = rk3368_lcdc_backlight_close,
	.mmu_en    = rk3368_lcdc_mmu_en,
	.set_overscan   = rk3368_lcdc_set_overscan,
	.extern_func	= rk3368_lcdc_extern_func,
	.wait_frame_start = rk3368_lcdc_wait_frame_start,
	.set_wb = rk3368_lcdc_set_wb,
};

#ifdef LCDC_IRQ_EMPTY_DEBUG
static int rk3368_lcdc_parse_irq(struct lcdc_device *lcdc_dev,
				 unsigned int intr_status)
{
	u32 intr_clr_reg;

	if (lcdc_dev->soc_type == VOP_FULL_RK3366)
		intr_clr_reg = INTR_CLEAR_RK3366;
	else
		intr_clr_reg = INTR_CLEAR_RK3368;

	if (intr_status & m_WIN0_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_WIN0_EMPTY_INTR_CLR,
			     v_WIN0_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "win0 empty irq!");
	} else if (intr_status & m_WIN1_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_WIN1_EMPTY_INTR_CLR,
			     v_WIN1_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "win1 empty irq!");
	} else if (intr_status & m_WIN2_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_WIN2_EMPTY_INTR_CLR,
			     v_WIN2_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "win2 empty irq!");
	} else if (intr_status & m_WIN3_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_WIN3_EMPTY_INTR_CLR,
			     v_WIN3_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "win3 empty irq!");
	} else if (intr_status & m_HWC_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_HWC_EMPTY_INTR_CLR,
			     v_HWC_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "HWC empty irq!");
	} else if (intr_status & m_POST_BUF_EMPTY_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_POST_BUF_EMPTY_INTR_CLR,
			     v_POST_BUF_EMPTY_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "post buf empty irq!");
	} else if (intr_status & m_PWM_GEN_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_PWM_GEN_INTR_CLR,
			     v_PWM_GEN_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "PWM gen irq!");
	}
	return 0;
}
#endif

static irqreturn_t rk3368_lcdc_isr(int irq, void *dev_id)
{
	struct lcdc_device *lcdc_dev = (struct lcdc_device *)dev_id;
	ktime_t timestamp = ktime_get();
	u32 intr_status;
	u32 line_scane_num, dsp_vs_st_f1;
	struct rk_screen *screen = lcdc_dev->driver.cur_screen;
	u32 intr_en_reg, intr_clr_reg, intr_status_reg;

	if (lcdc_dev->soc_type == VOP_FULL_RK3366) {
		intr_status_reg = INTR_STATUS_RK3366;
		intr_clr_reg = INTR_CLEAR_RK3366;
		intr_en_reg = INTR_EN_RK3366;
	} else {
		intr_status_reg = INTR_STATUS_RK3368;
		intr_clr_reg = INTR_CLEAR_RK3368;
		intr_en_reg = INTR_EN_RK3368;
	}

	intr_status = lcdc_readl(lcdc_dev, intr_status_reg);
	if (intr_status & m_FS_INTR_STS) {
		timestamp = ktime_get();
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_FS_INTR_CLR,
			     v_FS_INTR_CLR(1));
		line_scane_num = lcdc_readl(lcdc_dev, SCAN_LINE_NUM) & 0x1fff;
		dsp_vs_st_f1 = lcdc_readl(lcdc_dev, DSP_VS_ST_END_F1) >> 16;
		/*if(lcdc_dev->driver.wait_fs){ */
		if (0) {
			spin_lock(&(lcdc_dev->driver.cpl_lock));
			complete(&(lcdc_dev->driver.frame_done));
			spin_unlock(&(lcdc_dev->driver.cpl_lock));
		}
		lcdc_dev->driver.vsync_info.timestamp = timestamp;
		if ((lcdc_dev->soc_type == VOP_FULL_RK3366) &&
		    (lcdc_dev->driver.wb_data.state)) {
			if (lcdc_read_bit(lcdc_dev, WB_CTRL0, m_WB_EN)) {
				lcdc_msk_reg(lcdc_dev, WB_CTRL0,
					     m_WB_EN, v_WB_EN(0));
				lcdc_cfg_done(lcdc_dev);
				lcdc_dev->driver.wb_data.state = 0;
			}
		}
		wake_up_interruptible_all(&lcdc_dev->driver.vsync_info.wait);
		if (!(screen->mode.vmode & FB_VMODE_INTERLACED) ||
		    (line_scane_num >= dsp_vs_st_f1)) {
			lcdc_dev->driver.vsync_info.timestamp = timestamp;
			wake_up_interruptible_all(
				&lcdc_dev->driver.vsync_info.wait);
		}
	} else if (intr_status & m_LINE_FLAG0_INTR_STS) {
		lcdc_dev->driver.frame_time.last_framedone_t =
			lcdc_dev->driver.frame_time.framedone_t;
		lcdc_dev->driver.frame_time.framedone_t = cpu_clock(0);
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_LINE_FLAG0_INTR_CLR,
			     v_LINE_FLAG0_INTR_CLR(1));
	} else if (intr_status & m_LINE_FLAG1_INTR_STS) {
		/*line flag1 */
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_LINE_FLAG1_INTR_CLR,
			     v_LINE_FLAG1_INTR_CLR(1));
	} else if (intr_status & m_FS_NEW_INTR_STS) {
		/*new frame start */
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_FS_NEW_INTR_CLR,
			     v_FS_NEW_INTR_CLR(1));
	} else if (intr_status & m_BUS_ERROR_INTR_STS) {
		lcdc_msk_reg(lcdc_dev, intr_clr_reg, m_BUS_ERROR_INTR_CLR,
			     v_BUS_ERROR_INTR_CLR(1));
		dev_warn(lcdc_dev->dev, "bus error!");
	}

	/* for win empty debug */
#ifdef LCDC_IRQ_EMPTY_DEBUG
	rk3368_lcdc_parse_irq(lcdc_dev, intr_status);
#endif
	return IRQ_HANDLED;
}

#if defined(CONFIG_PM)
static int rk3368_lcdc_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int rk3368_lcdc_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define rk3368_lcdc_suspend NULL
#define rk3368_lcdc_resume  NULL
#endif

static int rk3368_lcdc_parse_dt(struct lcdc_device *lcdc_dev)
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
		/*default set it as 3.xv power supply */
		lcdc_dev->pwr18 = false;
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

	if (of_property_read_u32(np, "rockchip,iommu-enabled", &val))
		dev_drv->iommu_enabled = 0;
	else
		dev_drv->iommu_enabled = val;
	return 0;
}

static int rk3368_lcdc_probe(struct platform_device *pdev)
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
	lcdc_dev = devm_kzalloc(dev, sizeof(struct lcdc_device), GFP_KERNEL);
	if (!lcdc_dev) {
		dev_err(&pdev->dev, "rk3368 lcdc device kmalloc fail!");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, lcdc_dev);
	lcdc_dev->dev = dev;
	rk3368_lcdc_parse_dt(lcdc_dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	/* enable power domain */
	pm_runtime_enable(dev);
#endif
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lcdc_dev->reg_phy_base = res->start;
	lcdc_dev->len = resource_size(res);

	lcdc_dev->regs = devm_ioremap(&pdev->dev, res->start,
				      resource_size(res));
	if (IS_ERR(lcdc_dev->regs))
		return PTR_ERR(lcdc_dev->regs);
	else
		dev_info(dev, "lcdc_dev->regs=0x%lx\n", (long)lcdc_dev->regs);

	lcdc_dev->regsbak = devm_kzalloc(dev, lcdc_dev->len, GFP_KERNEL);
	if (IS_ERR(lcdc_dev->regsbak))
		return PTR_ERR(lcdc_dev->regsbak);
	lcdc_dev->dsp_lut_addr_base = (lcdc_dev->regs + GAMMA_LUT_ADDR);
	lcdc_dev->cabc_lut_addr_base = (lcdc_dev->regs + CABC_GAMMA_LUT_ADDR);
	lcdc_dev->grf_base =
		syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(lcdc_dev->grf_base)) {
		dev_err(&pdev->dev, "can't find lcdc grf property\n");
		lcdc_dev->grf_base = NULL;
	}
	lcdc_dev->pmugrf_base =
		syscon_regmap_lookup_by_phandle(np, "rockchip,pmugrf");
	if (IS_ERR(lcdc_dev->pmugrf_base)) {
		dev_err(&pdev->dev, "can't find lcdc pmu grf property\n");
		lcdc_dev->pmugrf_base = NULL;
	}

	lcdc_dev->cru_base =
		syscon_regmap_lookup_by_phandle(np, "rockchip,cru");
	if (IS_ERR(lcdc_dev->cru_base)) {
		dev_err(&pdev->dev, "can't find lcdc cru_base property\n");
		lcdc_dev->cru_base = NULL;
	}

	lcdc_dev->id = 0;
	dev_set_name(lcdc_dev->dev, "lcdc%d", lcdc_dev->id);
	dev_drv = &lcdc_dev->driver;
	dev_drv->dev = dev;
	dev_drv->prop = prop;
	dev_drv->id = lcdc_dev->id;
	dev_drv->ops = &lcdc_drv_ops;
	dev_drv->lcdc_win_num = ARRAY_SIZE(lcdc_win);
	dev_drv->reserved_fb = 1;/*only need reserved 1 buffer*/
	spin_lock_init(&lcdc_dev->reg_lock);

	lcdc_dev->irq = platform_get_irq(pdev, 0);
	if (lcdc_dev->irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ for lcdc%d\n",
			lcdc_dev->id);
		return -ENXIO;
	}

	ret = devm_request_irq(dev, lcdc_dev->irq, rk3368_lcdc_isr,
			       IRQF_SHARED,
			       dev_name(dev), lcdc_dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot requeset irq %d - err %d\n",
			lcdc_dev->irq, ret);
		return ret;
	}

	if (dev_drv->iommu_enabled) {
		if (lcdc_dev->id == 0) {
			strcpy(dev_drv->mmu_dts_name,
			       VOPB_IOMMU_COMPATIBLE_NAME);
		} else {
			strcpy(dev_drv->mmu_dts_name,
			       VOPL_IOMMU_COMPATIBLE_NAME);
		}
	}

	ret = rk_fb_register(dev_drv, lcdc_win, lcdc_dev->id);
	if (ret < 0) {
		dev_err(dev, "register fb for lcdc%d failed!\n", lcdc_dev->id);
		return ret;
	}
	if (lcdc_dev->soc_type == VOP_FULL_RK3366)
		dev_drv->property.feature |= SUPPORT_WRITE_BACK;
	else if (lcdc_dev->soc_type == VOP_FULL_RK3368)
		dev_drv->property.feature |= SUPPORT_IFBDC;
	dev_drv->property.feature |= SUPPORT_VOP_IDENTIFY |
				SUPPORT_YUV420_OUTPUT;
	dev_drv->property.max_output_x = 4096;
	dev_drv->property.max_output_y = 2160;
	lcdc_dev->screen = dev_drv->screen0;
	dev_info(dev, "lcdc%d probe ok, iommu %s\n",
		 lcdc_dev->id, dev_drv->iommu_enabled ? "enabled" : "disabled");

	return 0;
}

static int rk3368_lcdc_remove(struct platform_device *pdev)
{
	return 0;
}

static void rk3368_lcdc_shutdown(struct platform_device *pdev)
{
	struct lcdc_device *lcdc_dev = platform_get_drvdata(pdev);
	struct rk_lcdc_driver *dev_drv = &lcdc_dev->driver;
#if 1
	dev_drv->suspend_flag = 1;
	mdelay(100);
	flush_kthread_worker(&dev_drv->update_regs_worker);
	kthread_stop(dev_drv->update_regs_thread);
	rk3368_lcdc_deint(lcdc_dev);
	/*if (dev_drv->trsm_ops && dev_drv->trsm_ops->disable)
		dev_drv->trsm_ops->disable();*/

	rk3368_lcdc_clk_disable(lcdc_dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	pm_runtime_disable(lcdc_dev->dev);
#endif
	rk_disp_pwr_disable(dev_drv);
#else
	rk3368_lcdc_early_suspend(&lcdc_dev->driver);
	rk3368_lcdc_deint(lcdc_dev);
#endif
}

#if defined(CONFIG_OF)
static const struct of_device_id rk3368_lcdc_dt_ids[] = {
	{.compatible = "rockchip,rk3368-lcdc",},
	{.compatible = "rockchip,rk3366-lcdc-big",},
	{}
};
#endif

static struct platform_driver rk3368_lcdc_driver = {
	.probe = rk3368_lcdc_probe,
	.remove = rk3368_lcdc_remove,
	.driver = {
		   .name = "rk3368-lcdc",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk3368_lcdc_dt_ids),
		   },
	.suspend = rk3368_lcdc_suspend,
	.resume = rk3368_lcdc_resume,
	.shutdown = rk3368_lcdc_shutdown,
};

static int __init rk3368_lcdc_module_init(void)
{
	return platform_driver_register(&rk3368_lcdc_driver);
}

static void __exit rk3368_lcdc_module_exit(void)
{
	platform_driver_unregister(&rk3368_lcdc_driver);
}

fs_initcall(rk3368_lcdc_module_init);
module_exit(rk3368_lcdc_module_exit);
