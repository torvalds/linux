// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hisilicon Hi6220 SoC ADE(Advanced Display Engine)'s crtc&plane driver
 *
 * Copyright (c) 2016 Linaro Limited.
 * Copyright (c) 2014-2016 Hisilicon Limited.
 *
 * Author:
 *	Xinliang Liu <z.liuxinliang@hisilicon.com>
 *	Xinliang Liu <xinliang.liu@linaro.org>
 *	Xinwei Kong <kong.kongxinwei@hisilicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <video/display_timing.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include <drm/drm_drv.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_fourcc.h>

#include "kirin_drm_drv.h"
#include "kirin_dpe_reg.h"

#define DPE_WIDTH(width) ((width) - 1)
#define DPE_HEIGHT(height) ((height) - 1)

#define GET_FLUX_REQ_IN(max_depth) ((max_depth) * 50 / 100)
#define GET_FLUX_REQ_OUT(max_depth) ((max_depth) * 90 / 100)

#define DEFAULT_DPE_CORE_CLK_07V_RATE (400000000UL)
#define DPE_MAX_PXL0_CLK_144M (144000000UL)

#define DPE_UNSUPPORT (800)
#define RES_4K_PHONE (3840 * 2160)

enum dpe_ovl { DPE_OVL0 = 0, DPE_OVL_NUM };

enum dpe_channel {
	DPE_CH0 = 0, /* channel 1 for primary plane */
	DPE_CH_NUM
};

struct dpe_hw_ctx {
	void __iomem *base;
	void __iomem *noc_base;

	struct clk *dpe_axi_clk;
	struct clk *dpe_pclk_clk;
	struct clk *dpe_pri_clk;
	struct clk *dpe_pxl0_clk;
	struct clk *dpe_mmbuf_clk;

	bool power_on;
	int irq;

	struct drm_crtc *crtc;

	u32 hdisplay;
	u32 vdisplay;
};

static const struct kirin_format dpe_formats[] = {
	{ DRM_FORMAT_RGB565, DPE_RGB_565 },
	{ DRM_FORMAT_BGR565, DPE_BGR_565 },
	{ DRM_FORMAT_XRGB8888, DPE_RGBX_8888 },
	{ DRM_FORMAT_XBGR8888, DPE_BGRX_8888 },
	{ DRM_FORMAT_RGBA8888, DPE_RGBA_8888 },
	{ DRM_FORMAT_BGRA8888, DPE_BGRA_8888 },
	{ DRM_FORMAT_ARGB8888, DPE_BGRA_8888 },
	{ DRM_FORMAT_ABGR8888, DPE_RGBA_8888 },
};

static const u32 dpe_channel_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
};

static u32 dpe_pixel_dma_format_map[] = {
	DMA_PIXEL_FORMAT_RGB_565,
	DMA_PIXEL_FORMAT_XRGB_4444,
	DMA_PIXEL_FORMAT_ARGB_4444,
	DMA_PIXEL_FORMAT_XRGB_5551,
	DMA_PIXEL_FORMAT_ARGB_5551,
	DMA_PIXEL_FORMAT_XRGB_8888,
	DMA_PIXEL_FORMAT_ARGB_8888,
	DMA_PIXEL_FORMAT_RGB_565,
	DMA_PIXEL_FORMAT_XRGB_4444,
	DMA_PIXEL_FORMAT_ARGB_4444,
	DMA_PIXEL_FORMAT_XRGB_5551,
	DMA_PIXEL_FORMAT_ARGB_5551,
	DMA_PIXEL_FORMAT_XRGB_8888,
	DMA_PIXEL_FORMAT_ARGB_8888,
	DMA_PIXEL_FORMAT_YUYV_422_Pkg,
	DMA_PIXEL_FORMAT_YUV_422_SP_HP,
	DMA_PIXEL_FORMAT_YUV_422_SP_HP,
	DMA_PIXEL_FORMAT_YUV_420_SP_HP,
	DMA_PIXEL_FORMAT_YUV_420_SP_HP,
	DMA_PIXEL_FORMAT_YUV_422_P_HP,
	DMA_PIXEL_FORMAT_YUV_422_P_HP,
	DMA_PIXEL_FORMAT_YUV_420_P_HP,
	DMA_PIXEL_FORMAT_YUV_420_P_HP,
	DMA_PIXEL_FORMAT_YUYV_422_Pkg,
	DMA_PIXEL_FORMAT_YUYV_422_Pkg,
	DMA_PIXEL_FORMAT_YUYV_422_Pkg,
	DMA_PIXEL_FORMAT_YUYV_422_Pkg,
};

static u32 dpe_pixel_dfc_format_map[] = {
	DFC_PIXEL_FORMAT_RGB_565,
	DFC_PIXEL_FORMAT_XBGR_4444,
	DFC_PIXEL_FORMAT_ABGR_4444,
	DFC_PIXEL_FORMAT_XBGR_5551,
	DFC_PIXEL_FORMAT_ABGR_5551,
	DFC_PIXEL_FORMAT_XBGR_8888,
	DFC_PIXEL_FORMAT_ABGR_8888,
	DFC_PIXEL_FORMAT_BGR_565,
	DFC_PIXEL_FORMAT_XRGB_4444,
	DFC_PIXEL_FORMAT_ARGB_4444,
	DFC_PIXEL_FORMAT_XRGB_5551,
	DFC_PIXEL_FORMAT_ARGB_5551,
	DFC_PIXEL_FORMAT_XRGB_8888,
	DFC_PIXEL_FORMAT_ARGB_8888,
	DFC_PIXEL_FORMAT_YUYV422,
	DFC_PIXEL_FORMAT_YUYV422,
	DFC_PIXEL_FORMAT_YVYU422,
	DFC_PIXEL_FORMAT_YUYV422,
	DFC_PIXEL_FORMAT_YVYU422,
	DFC_PIXEL_FORMAT_YUYV422,
	DFC_PIXEL_FORMAT_YVYU422,
	DFC_PIXEL_FORMAT_YUYV422,
	DFC_PIXEL_FORMAT_YVYU422,
	DFC_PIXEL_FORMAT_YUYV422,
	DFC_PIXEL_FORMAT_UYVY422,
	DFC_PIXEL_FORMAT_YVYU422,
	DFC_PIXEL_FORMAT_VYUY422,
};

static u32 mid_array[DPE_CH_NUM] = {0xb};
static u32 aif_offset[DPE_CH_NUM] = {AIF0_CH0_OFFSET};
static u32 mif_offset[DPE_CH_NUM] = {MIF_CH0_OFFSET};
static u32 rdma_offset[DPE_CH_NUM] = {DPE_RCH_D0_DMA_OFFSET};
static u32 rdfc_offset[DPE_CH_NUM] = {DPE_RCH_D0_DFC_OFFSET};
static u32 dpe_smmu_chn_sid_num[DPE_CH_NUM] = {4};
static u32 dpe_smmu_smrx_idx[DPE_CH_NUM] = {0};
static u32 mctl_offset[DPE_OVL_NUM] = {DPE_MCTRL_CTL0_OFFSET};
static u32 ovl_offset[DPE_OVL_NUM] = {DPE_OVL0_OFFSET};

static u32 dpe_get_format(u32 pixel_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dpe_formats); i++)
		if (dpe_formats[i].pixel_format == pixel_format)
			return dpe_formats[i].hw_format;

	DRM_ERROR("Not found pixel format!!fourcc_format= %d\n", pixel_format);
	return DPE_UNSUPPORT;
}

static void dpe_set_reg(char __iomem *addr, u32 val, u8 bw, u8 bs)
{
	u32 mask = (1UL << bw) - 1UL;
	u32 tmp = 0;

	tmp = readl(addr);
	tmp &= ~(mask << bs);

	writel(tmp | ((val & mask) << bs), addr);
}

/* dpe mctl utils */
static void dpe_mctl_lock(struct dpe_hw_ctx *ctx)
{
	void __iomem *mctl_base = ctx->base + mctl_offset[DPE_OVL0];

	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX, 0x1, 1, 0);
}

static void dpe_mctl_unlock(struct dpe_hw_ctx *ctx)
{
	void __iomem *mctl_base = ctx->base + mctl_offset[DPE_OVL0];

	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX, 0x0, 1, 0);
}

static void dpe_mctl_init(struct dpe_hw_ctx *ctx)
{
	void __iomem *mctl_base = ctx->base + mctl_offset[DPE_OVL0];

	dpe_set_reg(mctl_base + MCTL_CTL_EN, 0x1, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_ITF, 0x1, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_DBG, 0xB13A00, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_TOP, 0x2, 32, 0);
}

static void dpe_qos_init(struct dpe_hw_ctx *ctx)
{
	void __iomem *noc_base = ctx->noc_base;

	writel(0x2, noc_base + 0x000c);
	writel(0x2, noc_base + 0x008c);
	writel(0x2, noc_base + 0x010c);
	writel(0x2, noc_base + 0x018c);
}

/* dpe ldi utils */
static void dpe_enable_ldi(struct dpe_hw_ctx *ctx)
{
	void __iomem *ldi_base = ctx->base + DPE_LDI0_OFFSET;

	dpe_set_reg(ldi_base + LDI_CTRL, 0x1, 1, 0);
}

/* interrupts utils */
static void dpe_interrupt_mask(struct dpe_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;
	u32 mask = ~0;

	writel(mask, base + GLB_CPU_PDP_INT_MSK);
	writel(mask, base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK);
	writel(mask, base + DPE_DPP_OFFSET + DPP_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_DPE_GLB_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_MCTL_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_WCH0_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_WCH1_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH0_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH1_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH2_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH3_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH4_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH5_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH6_INT_MSK);
	writel(mask, base + DPE_DBG_OFFSET + DBG_RCH7_INT_MSK);
}

static void dpe_interrupt_unmask(struct dpe_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;
	u32 unmask;

	unmask = ~0;
	unmask &= ~(BIT_DPP_INTS | BIT_ITF0_INTS | BIT_MMU_IRPT_NS);
	writel(unmask, base + GLB_CPU_PDP_INT_MSK);

	unmask = ~0;
	unmask &= ~(BIT_VSYNC | BIT_LDI_UNFLOW);
	writel(unmask, base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK);
}

static void dpe_interrupt_clear(struct dpe_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;
	u32 clear = ~0;

	writel(clear, base + GLB_CPU_PDP_INTS);
	writel(clear, base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INTS);
	writel(clear, base + DPE_DPP_OFFSET + DPP_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_MCTL_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_WCH0_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_WCH1_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH0_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH1_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH2_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH3_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH4_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH5_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH6_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_RCH7_INTS);
	writel(clear, base + DPE_DBG_OFFSET + DBG_DPE_GLB_INTS);
}

static void dpe_irq_enable(struct dpe_hw_ctx *ctx)
{
	enable_irq(ctx->irq);
}

static void dpe_clk_enable(struct dpe_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;

	writel(0x00000088, base + DPE_IFBC_OFFSET + IFBC_MEM_CTRL);
	writel(0x00000888, base + DPE_DSC_OFFSET + DSC_MEM_CTRL);
	writel(0x00000008, base + DPE_LDI0_OFFSET + LDI_MEM_CTRL);
	writel(0x00000008, base + DPE_DBUF0_OFFSET + DBUF_MEM_CTRL);
	writel(0x00000008, base + DPE_DPP_DITHER_OFFSET + DITHER_MEM_CTRL);
	writel(0x00000008, base + DPE_CMDLIST_OFFSET + CMD_MEM_CTRL);
	writel(0x00000088, base + DPE_RCH_VG0_SCL_OFFSET + SCF_COEF_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG0_SCL_OFFSET + SCF_LB_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG0_ARSR_OFFSET + ARSR2P_LB_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG0_DMA_OFFSET + VPP_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG0_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00008888, base + DPE_RCH_VG0_DMA_OFFSET + AFBCD_MEM_CTRL);
	writel(0x00000088, base + DPE_RCH_VG1_SCL_OFFSET + SCF_COEF_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG1_SCL_OFFSET + SCF_LB_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG1_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00008888, base + DPE_RCH_VG1_DMA_OFFSET + AFBCD_MEM_CTRL);
	writel(0x00000088, base + DPE_RCH_VG2_SCL_OFFSET + SCF_COEF_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG2_SCL_OFFSET + SCF_LB_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_VG2_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00000088, base + DPE_RCH_G0_SCL_OFFSET + SCF_COEF_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_G0_SCL_OFFSET + SCF_LB_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_G0_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00008888, base + DPE_RCH_G0_DMA_OFFSET + AFBCD_MEM_CTRL);
	writel(0x00000088, base + DPE_RCH_G1_SCL_OFFSET + SCF_COEF_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_G1_SCL_OFFSET + SCF_LB_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_G1_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00008888, base + DPE_RCH_G1_DMA_OFFSET + AFBCD_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_D0_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00008888, base + DPE_RCH_D0_DMA_OFFSET + AFBCD_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_D1_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_D2_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00000008, base + DPE_RCH_D3_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00000008, base + DPE_WCH0_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00000888, base + DPE_WCH0_DMA_OFFSET + AFBCE_MEM_CTRL);
	writel(0x00000008, base + DPE_WCH0_DMA_OFFSET + ROT_MEM_CTRL);
	writel(0x00000008, base + DPE_WCH1_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00000888, base + DPE_WCH1_DMA_OFFSET + AFBCE_MEM_CTRL);
	writel(0x00000008, base + DPE_WCH1_DMA_OFFSET + ROT_MEM_CTRL);
	writel(0x00000008, base + DPE_WCH2_DMA_OFFSET + DMA_BUF_MEM_CTRL);
	writel(0x00000008, base + DPE_WCH2_DMA_OFFSET + ROT_MEM_CTRL);
}

static int dpe_power_up(struct dpe_hw_ctx *ctx)
{
	int ret;

	if (ctx->power_on)
		return 0;

	/*peri clk enable */
	ret = clk_prepare_enable(ctx->dpe_pxl0_clk);
	if (ret) {
		DRM_ERROR("failed to enable dpe_pxl0_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(ctx->dpe_pri_clk);
	if (ret) {
		DRM_ERROR("failed to enable dpe_pri_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(ctx->dpe_pclk_clk);
	if (ret) {
		DRM_ERROR("failed to enable dpe_pclk_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(ctx->dpe_axi_clk);
	if (ret) {
		DRM_ERROR("failed to enable dpe_axi_clk (%d)\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(ctx->dpe_mmbuf_clk);
	if (ret) {
		DRM_ERROR("failed to enable dpe_mmbuf_clk (%d)\n", ret);
		return ret;
	}

	dpe_clk_enable(ctx);
	dpe_interrupt_mask(ctx);
	dpe_interrupt_clear(ctx);
	dpe_irq_enable(ctx);
	dpe_interrupt_unmask(ctx);

	ctx->power_on = true;
	return 0;
}

static void dpe_dpp_init(struct dpe_hw_ctx *ctx, struct drm_display_mode *mode,
			 struct drm_display_mode *adj_mode)
{
	void __iomem *dpp_base = ctx->base + DPE_DPP_OFFSET;

	writel((DPE_HEIGHT(mode->vdisplay) << 16) | DPE_WIDTH(mode->hdisplay),
	       dpp_base + DPP_IMG_SIZE_BEF_SR);
	writel((DPE_HEIGHT(mode->vdisplay) << 16) | DPE_WIDTH(mode->hdisplay),
	       dpp_base + DPP_IMG_SIZE_AFT_SR);
}

static void dpe_ovl_init(struct dpe_hw_ctx *ctx, u32 xres, u32 yres)
{
	void __iomem *mctl_sys_base = ctx->base + DPE_MCTRL_SYS_OFFSET;
	void __iomem *mctl_base = ctx->base + mctl_offset[DPE_OVL0];
	void __iomem *ovl0_base = ctx->base + ovl_offset[DPE_OVL0];

	dpe_set_reg(ovl0_base + OVL6_REG_DEFAULT, 0x1, 32, 0);
	dpe_set_reg(ovl0_base + OVL6_REG_DEFAULT, 0x0, 32, 0);
	dpe_set_reg(ovl0_base + OVL_SIZE, (xres - 1) | ((yres - 1) << 16), 32,
		    0);
	dpe_set_reg(ovl0_base + OVL_BG_COLOR, 0xFF000000, 32, 0);
	dpe_set_reg(ovl0_base + OVL_DST_STARTPOS, 0x0, 32, 0);
	dpe_set_reg(ovl0_base + OVL_DST_ENDPOS, (xres - 1) | ((yres - 1) << 16),
		    32, 0);
	dpe_set_reg(ovl0_base + OVL_GCFG, 0x10001, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_ITF, 0x1, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_DBUF, 0x1, 2, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_OV, 1 << DPE_OVL0, 4, 0);
	dpe_set_reg(mctl_sys_base + MCTL_RCH_OV0_SEL, 0x8, 4, 0);
	dpe_set_reg(mctl_sys_base + MCTL_OV0_FLUSH_EN, 0xd, 4, 0);
}

static void dpe_vesa_init(struct dpe_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;

	dpe_set_reg(base + DPE_LDI0_OFFSET + LDI_VESA_CLK_SEL, 0, 1, 0);
}

static int dpe_mipi_ifbc_get_rect(struct drm_rect *rect)
{
	u32 xres_div = XRES_DIV_1;
	u32 yres_div = YRES_DIV_1;

	if ((rect->x2 % xres_div) > 0)
		DRM_ERROR("xres(%d) is not division_h(%d) pixel aligned!\n",
			  rect->x2, xres_div);

	if ((rect->y2 % yres_div) > 0)
		DRM_ERROR("yres(%d) is not division_v(%d) pixel aligned!\n",
			  rect->y2, yres_div);

	rect->x2 /= xres_div;
	rect->y2 /= yres_div;

	return 0;
}

static void dpe_init_ldi_pxl_div(struct dpe_hw_ctx *ctx)
{
	void __iomem *ldi_base = ctx->base + DPE_LDI0_OFFSET;

	dpe_set_reg(ldi_base + LDI_PXL0_DIV2_GT_EN, PXL0_DIV2_GT_EN_CLOSE, 1,
		    0);
	dpe_set_reg(ldi_base + LDI_PXL0_DIV4_GT_EN, PXL0_DIV4_GT_EN_CLOSE, 1,
		    0);
	dpe_set_reg(ldi_base + LDI_PXL0_GT_EN, 0x1, 1, 0);
	dpe_set_reg(ldi_base + LDI_PXL0_DSI_GT_EN, PXL0_DSI_GT_EN_1, 2, 0);
	dpe_set_reg(ldi_base + LDI_PXL0_DIVXCFG, PXL0_DIVCFG_0, 3, 0);
}

static void dpe_dbuf_init(struct dpe_hw_ctx *ctx, struct drm_display_mode *mode,
			  struct drm_display_mode *adj_mode)
{
	void __iomem *dbuf_base = ctx->base + DPE_DBUF0_OFFSET;

	int sram_valid_num = 0;
	int sram_max_mem_depth = 0;
	int sram_min_support_depth = 0;

	u32 thd_rqos_in = 0;
	u32 thd_rqos_out = 0;
	u32 thd_wqos_in = 0;
	u32 thd_wqos_out = 0;
	u32 thd_cg_in = 0;
	u32 thd_cg_out = 0;
	u32 thd_wr_wait = 0;
	u32 thd_cg_hold = 0;
	u32 thd_flux_req_befdfs_in = 0;
	u32 thd_flux_req_befdfs_out = 0;
	u32 thd_flux_req_aftdfs_in = 0;
	u32 thd_flux_req_aftdfs_out = 0;
	u32 thd_dfs_ok = 0;
	u32 dfs_ok_mask = 0;
	u32 thd_flux_req_sw_en = 1;
	u32 hfp, hbp, hsw, vfp, vbp, vsw;

	int dfs_time_min = 0;
	int depth = 0;

	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	hsw = mode->hsync_end - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	vsw = mode->vsync_end - mode->vsync_start;

	dbuf_base = ctx->base + DPE_DBUF0_OFFSET;

	if (mode->hdisplay * mode->vdisplay >= RES_4K_PHONE)
		dfs_time_min = DFS_TIME_MIN_4K;
	else
		dfs_time_min = DFS_TIME_MIN;

	depth = DBUF0_DEPTH;

	thd_cg_out = (DFS_TIME * adj_mode->clock * 1000UL * mode->hdisplay) /
		     (((hsw + hbp + hfp) + mode->hdisplay) * 6 * 1000000UL);

	sram_valid_num = thd_cg_out / depth;
	thd_cg_in = (sram_valid_num + 1) * depth - 1;
	sram_max_mem_depth = (sram_valid_num + 1) * depth;

	thd_rqos_in = thd_cg_out * 85 / 100;
	thd_rqos_out = thd_cg_out;
	thd_flux_req_befdfs_in = GET_FLUX_REQ_IN(sram_max_mem_depth);
	thd_flux_req_befdfs_out = GET_FLUX_REQ_OUT(sram_max_mem_depth);

	sram_min_support_depth =
		dfs_time_min * mode->hdisplay /
		(1000000 / 60 / (mode->vdisplay + vbp + vfp + vsw) *
		 (DBUF_WIDTH_BIT / 3 / BITS_PER_BYTE));

	thd_flux_req_aftdfs_in = (sram_max_mem_depth - sram_min_support_depth);
	thd_flux_req_aftdfs_in = thd_flux_req_aftdfs_in / 3;
	thd_flux_req_aftdfs_out = 2 * thd_flux_req_aftdfs_in;
	thd_dfs_ok = thd_flux_req_befdfs_in;

	writel(mode->hdisplay * mode->vdisplay, dbuf_base + DBUF_FRM_SIZE);
	writel(DPE_WIDTH(mode->hdisplay), dbuf_base + DBUF_FRM_HSIZE);
	writel(sram_valid_num, dbuf_base + DBUF_SRAM_VALID_NUM);

	writel((thd_rqos_out << 16) | thd_rqos_in, dbuf_base + DBUF_THD_RQOS);
	writel((thd_wqos_out << 16) | thd_wqos_in, dbuf_base + DBUF_THD_WQOS);
	writel((thd_cg_out << 16) | thd_cg_in, dbuf_base + DBUF_THD_CG);
	writel((thd_cg_hold << 16) | thd_wr_wait, dbuf_base + DBUF_THD_OTHER);
	writel((thd_flux_req_befdfs_out << 16) | thd_flux_req_befdfs_in,
	       dbuf_base + DBUF_THD_FLUX_REQ_BEF);
	writel((thd_flux_req_aftdfs_out << 16) | thd_flux_req_aftdfs_in,
	       dbuf_base + DBUF_THD_FLUX_REQ_AFT);
	writel(thd_dfs_ok, dbuf_base + DBUF_THD_DFS_OK);
	writel((dfs_ok_mask << 1) | thd_flux_req_sw_en,
	       dbuf_base + DBUF_FLUX_REQ_CTRL);

	writel(0x1, dbuf_base + DBUF_DFS_LP_CTRL);
}

static void dpe_ldi_init(struct dpe_hw_ctx *ctx, struct drm_display_mode *mode,
			 struct drm_display_mode *adj_mode)
{
	void __iomem *ldi_base = ctx->base + DPE_LDI0_OFFSET;
	struct drm_rect rect = { 0, 0, 0, 0 };
	u32 hfp, hbp, hsw, vfp, vbp, vsw;
	u32 vsync_plr = 0;
	u32 hsync_plr = 0;
	u32 pixelclk_plr = 0;
	u32 data_en_plr = 0;

	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	hsw = mode->hsync_end - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	vsw = mode->vsync_end - mode->vsync_start;

	rect.x1 = 0;
	rect.y1 = 0;
	rect.x2 = mode->hdisplay;
	rect.y2 = mode->vdisplay;
	dpe_mipi_ifbc_get_rect(&rect);
	dpe_init_ldi_pxl_div(ctx);

	writel(hfp | ((hbp + DPE_WIDTH(hsw)) << 16),
	       ldi_base + LDI_DPI0_HRZ_CTRL0);
	writel(0, ldi_base + LDI_DPI0_HRZ_CTRL1);
	writel(DPE_WIDTH(rect.x2), ldi_base + LDI_DPI0_HRZ_CTRL2);
	writel(vfp | (vbp << 16), ldi_base + LDI_VRT_CTRL0);
	writel(DPE_HEIGHT(vsw), ldi_base + LDI_VRT_CTRL1);
	writel(DPE_HEIGHT(rect.y2), ldi_base + LDI_VRT_CTRL2);
	writel(vsync_plr | (hsync_plr << 1) | (pixelclk_plr << 2) |
		       (data_en_plr << 3),
	       ldi_base + LDI_PLR_CTRL);

	dpe_set_reg(ldi_base + LDI_CTRL, LCD_RGB888, 2, 3);
	dpe_set_reg(ldi_base + LDI_CTRL, LCD_RGB, 1, 13);

	writel(vfp, ldi_base + LDI_VINACT_MSK_LEN);
	writel(0x1, ldi_base + LDI_CMD_EVENT_SEL);

	dpe_set_reg(ldi_base + LDI_DSI_CMD_MOD_CTRL, 0x1, 1, 1);
	dpe_set_reg(ldi_base + LDI_WORK_MODE, 0x1, 1, 0);
	dpe_set_reg(ldi_base + LDI_CTRL, 0x0, 1, 0);
}

static void dpe_init(struct dpe_hw_ctx *ctx, struct drm_display_mode *mode,
		     struct drm_display_mode *adj_mode)
{
	dpe_dbuf_init(ctx, mode, adj_mode);
	dpe_dpp_init(ctx, mode, adj_mode);
	dpe_vesa_init(ctx);
	dpe_ldi_init(ctx, mode, adj_mode);
	dpe_qos_init(ctx);
	dpe_mctl_init(ctx);

	dpe_mctl_lock(ctx);
	dpe_ovl_init(ctx, mode->hdisplay, mode->vdisplay);
	dpe_mctl_unlock(ctx);

	//	dpe_enable_ldi(ctx);

	ctx->hdisplay = mode->hdisplay;
	ctx->vdisplay = mode->vdisplay;
	mdelay(60);
}

static void dpe_ldi_set_mode(struct dpe_hw_ctx *ctx,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adj_mode)
{
	int ret;
	u32 clk_Hz;

	switch (mode->clock) {
	case 148500:
		clk_Hz = 144000 * 1000UL;
		break;
	case 83496:
		clk_Hz = 80000 * 1000UL;
		break;
	case 74440:
		clk_Hz = 72000 * 1000UL;
		break;
	case 74250:
		clk_Hz = 72000 * 1000UL;
		break;
	default:
		clk_Hz = mode->clock * 1000UL;
	}

	ret = clk_set_rate(ctx->dpe_pxl0_clk, clk_Hz);
	if (ret)
		DRM_ERROR("failed to set pixel clk %dHz (%d)\n", clk_Hz, ret);

	adj_mode->clock = clk_get_rate(ctx->dpe_pxl0_clk) / 1000;
}

static int dpe_enable_vblank(struct drm_crtc *crtc)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct dpe_hw_ctx *ctx = kcrtc->hw_ctx;

	dpe_power_up(ctx);

	return 0;
}

static void dpe_disable_vblank(struct drm_crtc *crtc)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct dpe_hw_ctx *ctx = kcrtc->hw_ctx;

	if (!ctx->power_on) {
		DRM_ERROR("power is down! vblank disable fail\n");
		return;
	}
}

static void dpe_crtc_atomic_enable(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_state)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct dpe_hw_ctx *ctx = kcrtc->hw_ctx;
	int ret;

	if (kcrtc->enable)
		return;

	ret = dpe_power_up(ctx);
	if (ret)
		return;

	kcrtc->enable = true;
	drm_crtc_vblank_on(crtc);
}

static void dpe_crtc_atomic_disable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);

	if (!kcrtc->enable)
		return;

	drm_crtc_vblank_off(crtc);
	kcrtc->enable = false;
}

static void dpe_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct dpe_hw_ctx *ctx = kcrtc->hw_ctx;
	struct drm_display_mode *mode = &crtc->state->mode;
	struct drm_display_mode *adj_mode = &crtc->state->adjusted_mode;

	dpe_power_up(ctx);
	dpe_ldi_set_mode(ctx, mode, adj_mode);
	dpe_init(ctx, mode, adj_mode);
}

static void dpe_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_state)
{
	struct kirin_crtc *kcrtc = to_kirin_crtc(crtc);
	struct dpe_hw_ctx *ctx = kcrtc->hw_ctx;

	dpe_power_up(ctx);
}

static void dpe_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_state)

{
	struct drm_pending_vblank_event *event = crtc->state->event;

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0)
			drm_crtc_arm_vblank_event(crtc, event);
		else
			drm_crtc_send_vblank_event(crtc, event);
		spin_unlock_irq(&crtc->dev->event_lock);
	}
}

const struct drm_crtc_helper_funcs dpe_crtc_helper_funcs = {
	.atomic_enable = dpe_crtc_atomic_enable,
	.atomic_disable = dpe_crtc_atomic_disable,
	.mode_set_nofb = dpe_crtc_mode_set_nofb,
	.atomic_begin = dpe_crtc_atomic_begin,
	.atomic_flush = dpe_crtc_atomic_flush,
};

const struct drm_crtc_funcs dpe_crtc_funcs = {
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank = dpe_enable_vblank,
	.disable_vblank = dpe_disable_vblank,
};

static void dpe_unflow_handler(struct dpe_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;
	u32 tmp = 0;

	tmp = readl(base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK);
	tmp &= ~BIT_LDI_UNFLOW;

	writel(tmp, base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK);
}

static void dpe_mctl_ov_config(struct dpe_hw_ctx *ctx, u32 ch)
{
	void __iomem *mctl_base = ctx->base + mctl_offset[DPE_OVL0];

	dpe_set_reg(mctl_base + MCTL_CTL_EN, 0x1, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_TOP, 0x2, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_DBG, 0xB13A00, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_RCH0 + ch * 4, 0x1, 32, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_ITF, 0x1, 2, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_DBUF, 0x1, 2, 0);
	dpe_set_reg(mctl_base + MCTL_CTL_MUTEX_OV, 1 << DPE_OVL0, 4, 0);
}

static void dpe_mctl_sys_config(struct dpe_hw_ctx *ctx, u32 ch)
{
	void __iomem *mctl_sys_base = ctx->base + DPE_MCTRL_SYS_OFFSET;

	dpe_set_reg(mctl_sys_base + MCTL_RCH0_OV_OEN + ch * 4, (1 << 1) | 0x100,
		    32, 0);
	dpe_set_reg(mctl_sys_base + MCTL_RCH_OV0_SEL, 0x8, 4, 0);
	dpe_set_reg(mctl_sys_base + MCTL_RCH_OV0_SEL, ch, 4,
		    (DPE_OVL0 + 1) * 4);
	dpe_set_reg(mctl_sys_base + MCTL_OV0_FLUSH_EN, 0xd, 4, 0);
	dpe_set_reg(mctl_sys_base + MCTL_RCH0_FLUSH_EN + ch * 4, 0x1, 32, 0);
}

static void dpe_ovl_config(struct dpe_hw_ctx *ctx, const struct drm_rect *rect,
			   u32 xres, u32 yres)
{
	void __iomem *ovl0_base = ctx->base + ovl_offset[DPE_OVL0];

	dpe_set_reg(ovl0_base + OVL6_REG_DEFAULT, 0x1, 32, 0);
	dpe_set_reg(ovl0_base + OVL6_REG_DEFAULT, 0x0, 32, 0);
	dpe_set_reg(ovl0_base + OVL_SIZE, (xres - 1) | ((yres - 1) << 16), 32,
		    0);
	dpe_set_reg(ovl0_base + OVL_BG_COLOR, 0xFF000000, 32, 0);
	dpe_set_reg(ovl0_base + OVL_DST_STARTPOS, 0x0, 32, 0);
	dpe_set_reg(ovl0_base + OVL_DST_ENDPOS, (xres - 1) | ((yres - 1) << 16),
		    32, 0);
	dpe_set_reg(ovl0_base + OVL_GCFG, 0x10001, 32, 0);
	dpe_set_reg(ovl0_base + OVL_LAYER0_POS, (rect->x1) | ((rect->y1) << 16),
		    32, 0);
	dpe_set_reg(ovl0_base + OVL_LAYER0_SIZE,
		    (rect->x2) | ((rect->y2) << 16), 32, 0);
	dpe_set_reg(ovl0_base + OVL_LAYER0_ALPHA, 0x00ff40ff, 32, 0);
	dpe_set_reg(ovl0_base + OVL_LAYER0_CFG, 0x1, 1, 0);
}

static void dpe_rdma_config(struct dpe_hw_ctx *ctx, const struct drm_rect *rect,
			    u32 display_addr, u32 hal_format, u32 bpp, int ch)
{
	void __iomem *rdma_base = ctx->base + rdma_offset[ch];

	u32 aligned_pixel = 0;
	u32 rdma_oft_x0, rdma_oft_y0, rdma_oft_x1, rdma_oft_y1;
	u32 rdma_stride, rdma_format;
	u32 stretch_size_vrt = 0;
	u32 h_display = 0;

	aligned_pixel = DMA_ALIGN_BYTES / bpp;
	rdma_oft_x0 = rect->x1 / aligned_pixel;
	rdma_oft_y0 = rect->y1;
	rdma_oft_x1 = rect->x2 / aligned_pixel;
	rdma_oft_y1 = rect->y2;

	rdma_format = dpe_pixel_dma_format_map[hal_format];
	stretch_size_vrt = rdma_oft_y1 - rdma_oft_y0;

	h_display = (rect->x2 - rect->x1) + 1;
	rdma_stride = (h_display * bpp) / DMA_ALIGN_BYTES;

	dpe_set_reg(rdma_base + DMA_CH_REG_DEFAULT, 0x1, 32, 0);
	dpe_set_reg(rdma_base + DMA_CH_REG_DEFAULT, 0x0, 32, 0);

	dpe_set_reg(rdma_base + DMA_OFT_X0, rdma_oft_x0, 12, 0);
	dpe_set_reg(rdma_base + DMA_OFT_Y0, rdma_oft_y0, 16, 0);
	dpe_set_reg(rdma_base + DMA_OFT_X1, rdma_oft_x1, 12, 0);
	dpe_set_reg(rdma_base + DMA_OFT_Y1, rdma_oft_y1, 16, 0);
	dpe_set_reg(rdma_base + DMA_CTRL, rdma_format, 5, 3);
	dpe_set_reg(rdma_base + DMA_CTRL, 0x0, 1, 8);
	dpe_set_reg(rdma_base + DMA_STRETCH_SIZE_VRT, stretch_size_vrt, 32, 0);
	dpe_set_reg(rdma_base + DMA_DATA_ADDR0, display_addr, 32, 0);
	dpe_set_reg(rdma_base + DMA_STRIDE0, rdma_stride, 13, 0);
	dpe_set_reg(rdma_base + DMA_CH_CTL, 0x1, 1, 0);
}

static void dpe_rdfc_config(struct dpe_hw_ctx *ctx, const struct drm_rect *rect,
			    u32 hal_format, u32 bpp, int ch)
{
	void __iomem *rdfc_base = ctx->base + rdfc_offset[ch];

	u32 dfc_pix_in_num = 0;
	u32 size_hrz = 0;
	u32 size_vrt = 0;
	u32 dfc_fmt = 0;

	dfc_pix_in_num = (bpp <= 2) ? 0x1 : 0x0;
	size_hrz = rect->x2 - rect->x1;
	size_vrt = rect->y2 - rect->y1;

	dfc_fmt = dpe_pixel_dfc_format_map[hal_format];

	dpe_set_reg(rdfc_base + DFC_DISP_SIZE, (size_vrt | (size_hrz << 16)),
		    29, 0);
	dpe_set_reg(rdfc_base + DFC_PIX_IN_NUM, dfc_pix_in_num, 1, 0);
	dpe_set_reg(rdfc_base + DFC_DISP_FMT, dfc_fmt, 5, 1);
	dpe_set_reg(rdfc_base + DFC_CTL_CLIP_EN, 0x1, 1, 0);
	dpe_set_reg(rdfc_base + DFC_ICG_MODULE, 0x1, 1, 0);
}

static void dpe_aif_config(struct dpe_hw_ctx *ctx, u32 ch)
{
	void __iomem *aif_ch_base = ctx->base + aif_offset[ch];

	dpe_set_reg(aif_ch_base, 0x0, 1, 0);
	dpe_set_reg(aif_ch_base, mid_array[ch], 4, 4);
}

static void dpe_mif_config(struct dpe_hw_ctx *ctx, u32 ch)
{
	void __iomem *mif_ch_base = ctx->base + mif_offset[ch];

	dpe_set_reg(mif_ch_base + MIF_CTRL1, 0x1, 1, 5);
}

static void dpe_smmu_config_off(struct dpe_hw_ctx *ctx, u32 ch)
{
	void __iomem *smmu_base = ctx->base + DPE_SMMU_OFFSET;
	int i, index;

	for (i = 0; i < dpe_smmu_chn_sid_num[ch]; i++) {
		index = dpe_smmu_smrx_idx[ch] + i;
		dpe_set_reg(smmu_base + SMMU_SMRx_NS + index * 0x4, 1, 32, 0);
	}
}

static void dpe_update_channel(struct kirin_plane *kplane,
			       struct drm_framebuffer *fb, int crtc_x,
			       int crtc_y, unsigned int crtc_w,
			       unsigned int crtc_h, u32 src_x, u32 src_y,
			       u32 src_w, u32 src_h)
{
	struct dpe_hw_ctx *ctx = kplane->hw_ctx;
	struct drm_gem_cma_object *obj = drm_fb_cma_get_gem_obj(fb, 0);
	struct drm_rect rect;
	u32 bpp;
	u32 stride;
	u32 display_addr;
	u32 hal_fmt;
	u32 ch = DPE_CH0;

	bpp = fb->format->cpp[0];
	stride = fb->pitches[0];

	display_addr = (u32)obj->paddr + src_y * stride;

	rect.x1 = 0;
	rect.x2 = src_w - 1;
	rect.y1 = 0;
	rect.y2 = src_h - 1;
	hal_fmt = dpe_get_format(fb->format->format);

	dpe_mctl_lock(ctx);
	dpe_aif_config(ctx, ch);
	dpe_mif_config(ctx, ch);
	dpe_smmu_config_off(ctx, ch);

	dpe_rdma_config(ctx, &rect, display_addr, hal_fmt, bpp, ch);
	dpe_rdfc_config(ctx, &rect, hal_fmt, bpp, ch);
	dpe_ovl_config(ctx, &rect, ctx->hdisplay, ctx->vdisplay);

	dpe_mctl_ov_config(ctx, ch);
	dpe_mctl_sys_config(ctx, ch);
	dpe_mctl_unlock(ctx);
	dpe_unflow_handler(ctx);

	dpe_enable_ldi(ctx);
}

static void dpe_plane_atomic_update(struct drm_plane *plane,
				    struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct kirin_plane *kplane = to_kirin_plane(plane);

	if (!state->fb) {
		state->visible = false;
		return;
	}

	dpe_update_channel(kplane, state->fb, state->crtc_x, state->crtc_y,
			   state->crtc_w, state->crtc_h, state->src_x >> 16,
			   state->src_y >> 16, state->src_w >> 16,
			   state->src_h >> 16);
}

static int dpe_plane_atomic_check(struct drm_plane *plane,
				  struct drm_plane_state *state)
{
	struct drm_framebuffer *fb = state->fb;
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *crtc_state;
	u32 src_x = state->src_x >> 16;
	u32 src_y = state->src_y >> 16;
	u32 src_w = state->src_w >> 16;
	u32 src_h = state->src_h >> 16;
	int crtc_x = state->crtc_x;
	int crtc_y = state->crtc_y;
	u32 crtc_w = state->crtc_w;
	u32 crtc_h = state->crtc_h;
	u32 fmt;

	if (!crtc || !fb)
		return 0;

	fmt = dpe_get_format(fb->format->format);
	if (fmt == DPE_UNSUPPORT)
		return -EINVAL;

	crtc_state = drm_atomic_get_crtc_state(state->state, crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	if (src_w != crtc_w || src_h != crtc_h) {
		DRM_ERROR("Scale not support!!!\n");
		return -EINVAL;
	}

	if (src_x + src_w > fb->width || src_y + src_h > fb->height)
		return -EINVAL;

	if (crtc_x < 0 || crtc_y < 0)
		return -EINVAL;

	if (crtc_x + crtc_w > crtc_state->adjusted_mode.hdisplay ||
	    crtc_y + crtc_h > crtc_state->adjusted_mode.vdisplay)
		return -EINVAL;

	return 0;
}

const struct drm_plane_helper_funcs dpe_plane_helper_funcs = {
	.atomic_check = dpe_plane_atomic_check,
	.atomic_update = dpe_plane_atomic_update,
};

const struct drm_plane_funcs dpe_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static irqreturn_t dpe_irq_handler(int irq, void *data)
{
	struct dpe_hw_ctx *ctx = data;
	struct drm_crtc *crtc = ctx->crtc;
	void __iomem *base = ctx->base;

	u32 isr_s1 = 0;
	u32 isr_s2 = 0;
	u32 isr_s2_dpp = 0;
	u32 isr_s2_smmu = 0;
	u32 mask = 0;

	isr_s1 = readl(base + GLB_CPU_PDP_INTS);
	isr_s2 = readl(base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INTS);
	isr_s2_dpp = readl(base + DPE_DPP_OFFSET + DPP_INTS);
	isr_s2_smmu = readl(base + DPE_SMMU_OFFSET + SMMU_INTSTAT_NS);

	writel(isr_s2_smmu, base + DPE_SMMU_OFFSET + SMMU_INTCLR_NS);
	writel(isr_s2_dpp, base + DPE_DPP_OFFSET + DPP_INTS);
	writel(isr_s2, base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INTS);
	writel(isr_s1, base + GLB_CPU_PDP_INTS);

	isr_s1 &= ~(readl(base + GLB_CPU_PDP_INT_MSK));
	isr_s2 &= ~(readl(base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK));
	isr_s2_dpp &= ~(readl(base + DPE_DPP_OFFSET + DPP_INT_MSK));

	if (isr_s2 & BIT_VSYNC)
		drm_crtc_handle_vblank(crtc);

	if (isr_s2 & BIT_LDI_UNFLOW) {
		mask = readl(base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK);
		mask |= BIT_LDI_UNFLOW;
		writel(mask, base + DPE_LDI0_OFFSET + LDI_CPU_ITF_INT_MSK);

		DRM_ERROR("ldi underflow!\n");
	}

	return IRQ_HANDLED;
}

static void *dpe_hw_ctx_alloc(struct platform_device *pdev,
			      struct drm_crtc *crtc)
{
	struct dpe_hw_ctx *ctx = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	int ret = 0;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		DRM_ERROR("failed to alloc ade_hw_ctx\n");
		return ERR_PTR(-ENOMEM);
	}

	ctx->base = of_iomap(np, 0);
	if (!(ctx->base)) {
		DRM_ERROR("failed to get ade base resource.\n");
		return ERR_PTR(-ENXIO);
	}

	ctx->noc_base = of_iomap(np, 4);
	if (!(ctx->noc_base)) {
		DRM_ERROR("failed to get noc_base  resource.\n");
		return ERR_PTR(-ENXIO);
	}

	ctx->irq = irq_of_parse_and_map(np, 0);
	if (ctx->irq <= 0) {
		DRM_ERROR("failed to get irq_pdp resource.\n");
		return ERR_PTR(-ENXIO);
	}

	DRM_INFO("dpe irq = %d.", ctx->irq);

	ctx->dpe_mmbuf_clk = devm_clk_get(dev, "clk_dss_axi_mm");
	if (!ctx->dpe_mmbuf_clk) {
		DRM_ERROR("failed to parse dpe_mmbuf_clk\n");
		return ERR_PTR(-ENODEV);
	}

	ctx->dpe_axi_clk = devm_clk_get(dev, "aclk_dss");
	if (!ctx->dpe_axi_clk) {
		DRM_ERROR("failed to parse dpe_axi_clk\n");
		return ERR_PTR(-ENODEV);
	}

	ctx->dpe_pclk_clk = devm_clk_get(dev, "pclk_dss");
	if (!ctx->dpe_pclk_clk) {
		DRM_ERROR("failed to parse dpe_pclk_clk\n");
		return ERR_PTR(-ENODEV);
	}

	ctx->dpe_pri_clk = devm_clk_get(dev, "clk_edc0");
	if (!ctx->dpe_pri_clk) {
		DRM_ERROR("failed to parse dpe_pri_clk\n");
		return ERR_PTR(-ENODEV);
	}

	ret = clk_set_rate(ctx->dpe_pri_clk, DEFAULT_DPE_CORE_CLK_07V_RATE);
	if (ret < 0) {
		DRM_ERROR("dpe_pri_clk clk_set_rate(%lu) failed, error=%d!\n",
			  DEFAULT_DPE_CORE_CLK_07V_RATE, ret);
		return ERR_PTR(-EINVAL);
	}

	ctx->dpe_pxl0_clk = devm_clk_get(dev, "clk_ldi0");
	if (!ctx->dpe_pxl0_clk) {
		DRM_ERROR("failed to parse dpe_pxl0_clk\n");
		return ERR_PTR(-ENODEV);
	}

	ret = clk_set_rate(ctx->dpe_pxl0_clk, DPE_MAX_PXL0_CLK_144M);
	if (ret < 0) {
		DRM_ERROR("dpe_pxl0_clk clk_set_rate(%lu) failed, error=%d!\n",
			  DPE_MAX_PXL0_CLK_144M, ret);
		return ERR_PTR(-EINVAL);
	}

	ctx->crtc = crtc;
	ret = devm_request_irq(dev, ctx->irq, dpe_irq_handler, IRQF_SHARED,
			       dev->driver->name, ctx);
	if (ret)
		return ERR_PTR(-EIO);

	disable_irq(ctx->irq);

	return ctx;
}

static void dpe_hw_ctx_cleanup(void *hw_ctx)
{
}

extern void dsi_set_output_client(struct drm_device *dev);
static void kirin_fbdev_output_poll_changed(struct drm_device *dev)
{
	dsi_set_output_client(dev);
}

static const struct drm_mode_config_funcs dpe_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.output_poll_changed = kirin_fbdev_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_CMA_FOPS(kirin_drm_fops);
static struct drm_driver dpe_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET |
				  DRIVER_ATOMIC | DRIVER_RENDER,

	.date			= "20170309",
	.fops				= &kirin_drm_fops,
	.gem_free_object_unlocked	= drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.dumb_create		= drm_gem_cma_dumb_create_internal,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,

	.name			= "kirin",
	.desc			= "Hisilicon Kirin SoCs' DRM Driver",
	.major			= 1,
	.minor			= 0,
};

const struct kirin_drm_data dpe_driver_data = {
	.num_planes = DPE_CH_NUM,
	.prim_plane = DPE_CH0,

	.channel_formats = dpe_channel_formats,
	.channel_formats_cnt = ARRAY_SIZE(dpe_channel_formats),
	.config_max_width = 4096,
	.config_max_height = 4096,

	.driver = &dpe_driver,

	.crtc_helper_funcs = &dpe_crtc_helper_funcs,
	.crtc_funcs = &dpe_crtc_funcs,
	.plane_helper_funcs = &dpe_plane_helper_funcs,
	.plane_funcs = &dpe_plane_funcs,
	.mode_config_funcs = &dpe_mode_config_funcs,

	.alloc_hw_ctx = dpe_hw_ctx_alloc,
	.cleanup_hw_ctx = dpe_hw_ctx_cleanup,
};
