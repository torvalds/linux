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

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>

#include "kirin_drm_drv.h"
#include "kirin_ade_reg.h"

#define to_ade_crtc(crtc) \
	container_of(crtc, struct ade_crtc, base)

struct ade_hw_ctx {
	void __iomem  *base;
	struct regmap *noc_regmap;
	struct clk *ade_core_clk;
	struct clk *media_noc_clk;
	struct clk *ade_pix_clk;
	struct reset_control *reset;
	bool power_on;
	int irq;
};

struct ade_crtc {
	struct drm_crtc base;
	struct ade_hw_ctx *ctx;
	bool enable;
	u32 out_format;
};

struct ade_data {
	struct ade_crtc acrtc;
	struct ade_hw_ctx ctx;
};

static void ade_update_reload_bit(void __iomem *base, u32 bit_num, u32 val)
{
	u32 bit_ofst, reg_num;

	bit_ofst = bit_num % 32;
	reg_num = bit_num / 32;

	ade_update_bits(base + ADE_RELOAD_DIS(reg_num), bit_ofst,
			MASK(1), !!val);
}

static u32 ade_read_reload_bit(void __iomem *base, u32 bit_num)
{
	u32 tmp, bit_ofst, reg_num;

	bit_ofst = bit_num % 32;
	reg_num = bit_num / 32;

	tmp = readl(base + ADE_RELOAD_DIS(reg_num));
	return !!(BIT(bit_ofst) & tmp);
}

static void ade_init(struct ade_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;

	/* enable clk gate */
	ade_update_bits(base + ADE_CTRL1, AUTO_CLK_GATE_EN_OFST,
			AUTO_CLK_GATE_EN, ADE_ENABLE);
	/* clear overlay */
	writel(0, base + ADE_OVLY1_TRANS_CFG);
	writel(0, base + ADE_OVLY_CTL);
	writel(0, base + ADE_OVLYX_CTL(ADE_OVLY2));
	/* clear reset and reload regs */
	writel(MASK(32), base + ADE_SOFT_RST_SEL(0));
	writel(MASK(32), base + ADE_SOFT_RST_SEL(1));
	writel(MASK(32), base + ADE_RELOAD_DIS(0));
	writel(MASK(32), base + ADE_RELOAD_DIS(1));
	/*
	 * for video mode, all the ade registers should
	 * become effective at frame end.
	 */
	ade_update_bits(base + ADE_CTRL, FRM_END_START_OFST,
			FRM_END_START_MASK, REG_EFFECTIVE_IN_ADEEN_FRMEND);
}

static void ade_set_pix_clk(struct ade_hw_ctx *ctx,
			    struct drm_display_mode *mode,
			    struct drm_display_mode *adj_mode)
{
	u32 clk_Hz = mode->clock * 1000;
	int ret;

	/*
	 * Success should be guaranteed in mode_valid call back,
	 * so failure shouldn't happen here
	 */
	ret = clk_set_rate(ctx->ade_pix_clk, clk_Hz);
	if (ret)
		DRM_ERROR("failed to set pixel clk %dHz (%d)\n", clk_Hz, ret);
	adj_mode->clock = clk_get_rate(ctx->ade_pix_clk) / 1000;
}

static void ade_ldi_set_mode(struct ade_crtc *acrtc,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adj_mode)
{
	struct ade_hw_ctx *ctx = acrtc->ctx;
	void __iomem *base = ctx->base;
	u32 width = mode->hdisplay;
	u32 height = mode->vdisplay;
	u32 hfp, hbp, hsw, vfp, vbp, vsw;
	u32 plr_flags;

	plr_flags = (mode->flags & DRM_MODE_FLAG_NVSYNC) ? FLAG_NVSYNC : 0;
	plr_flags |= (mode->flags & DRM_MODE_FLAG_NHSYNC) ? FLAG_NHSYNC : 0;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	hsw = mode->hsync_end - mode->hsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	vsw = mode->vsync_end - mode->vsync_start;
	if (vsw > 15) {
		DRM_DEBUG_DRIVER("vsw exceeded 15\n");
		vsw = 15;
	}

	writel((hbp << HBP_OFST) | hfp, base + LDI_HRZ_CTRL0);
	 /* the configured value is actual value - 1 */
	writel(hsw - 1, base + LDI_HRZ_CTRL1);
	writel((vbp << VBP_OFST) | vfp, base + LDI_VRT_CTRL0);
	 /* the configured value is actual value - 1 */
	writel(vsw - 1, base + LDI_VRT_CTRL1);
	 /* the configured value is actual value - 1 */
	writel(((height - 1) << VSIZE_OFST) | (width - 1),
	       base + LDI_DSP_SIZE);
	writel(plr_flags, base + LDI_PLR_CTRL);

	/* ctran6 setting */
	writel(CTRAN_BYPASS_ON, base + ADE_CTRAN_DIS(ADE_CTRAN6));
	 /* the configured value is actual value - 1 */
	writel(width * height - 1, base + ADE_CTRAN_IMAGE_SIZE(ADE_CTRAN6));
	ade_update_reload_bit(base, CTRAN_OFST + ADE_CTRAN6, 0);

	ade_set_pix_clk(ctx, mode, adj_mode);

	DRM_DEBUG_DRIVER("set mode: %dx%d\n", width, height);
}

static int ade_power_up(struct ade_hw_ctx *ctx)
{
	int ret;

	ret = clk_prepare_enable(ctx->media_noc_clk);
	if (ret) {
		DRM_ERROR("failed to enable media_noc_clk (%d)\n", ret);
		return ret;
	}

	ret = reset_control_deassert(ctx->reset);
	if (ret) {
		DRM_ERROR("failed to deassert reset\n");
		return ret;
	}

	ret = clk_prepare_enable(ctx->ade_core_clk);
	if (ret) {
		DRM_ERROR("failed to enable ade_core_clk (%d)\n", ret);
		return ret;
	}

	ade_init(ctx);
	ctx->power_on = true;
	return 0;
}

static void ade_power_down(struct ade_hw_ctx *ctx)
{
	void __iomem *base = ctx->base;

	writel(ADE_DISABLE, base + LDI_CTRL);
	/* dsi pixel off */
	writel(DSI_PCLK_OFF, base + LDI_HDMI_DSI_GT);

	clk_disable_unprepare(ctx->ade_core_clk);
	reset_control_assert(ctx->reset);
	clk_disable_unprepare(ctx->media_noc_clk);
	ctx->power_on = false;
}

static void ade_set_medianoc_qos(struct ade_crtc *acrtc)
{
	struct ade_hw_ctx *ctx = acrtc->ctx;
	struct regmap *map = ctx->noc_regmap;

	regmap_update_bits(map, ADE0_QOSGENERATOR_MODE,
			   QOSGENERATOR_MODE_MASK, BYPASS_MODE);
	regmap_update_bits(map, ADE0_QOSGENERATOR_EXTCONTROL,
			   SOCKET_QOS_EN, SOCKET_QOS_EN);

	regmap_update_bits(map, ADE1_QOSGENERATOR_MODE,
			   QOSGENERATOR_MODE_MASK, BYPASS_MODE);
	regmap_update_bits(map, ADE1_QOSGENERATOR_EXTCONTROL,
			   SOCKET_QOS_EN, SOCKET_QOS_EN);
}

static void ade_display_enable(struct ade_crtc *acrtc)
{
	struct ade_hw_ctx *ctx = acrtc->ctx;
	void __iomem *base = ctx->base;
	u32 out_fmt = acrtc->out_format;

	/* display source setting */
	writel(DISP_SRC_OVLY2, base + ADE_DISP_SRC_CFG);

	/* enable ade */
	writel(ADE_ENABLE, base + ADE_EN);
	/* enable ldi */
	writel(NORMAL_MODE, base + LDI_WORK_MODE);
	writel((out_fmt << BPP_OFST) | DATA_GATE_EN | LDI_EN,
	       base + LDI_CTRL);
	/* dsi pixel on */
	writel(DSI_PCLK_ON, base + LDI_HDMI_DSI_GT);
}

static void ade_crtc_enable(struct drm_crtc *crtc)
{
	struct ade_crtc *acrtc = to_ade_crtc(crtc);
	struct ade_hw_ctx *ctx = acrtc->ctx;
	int ret;

	if (acrtc->enable)
		return;

	if (!ctx->power_on) {
		ret = ade_power_up(ctx);
		if (ret)
			return;
	}

	ade_set_medianoc_qos(acrtc);
	ade_display_enable(acrtc);
	acrtc->enable = true;
}

static void ade_crtc_disable(struct drm_crtc *crtc)
{
	struct ade_crtc *acrtc = to_ade_crtc(crtc);
	struct ade_hw_ctx *ctx = acrtc->ctx;

	if (!acrtc->enable)
		return;

	ade_power_down(ctx);
	acrtc->enable = false;
}

static int ade_crtc_atomic_check(struct drm_crtc *crtc,
				 struct drm_crtc_state *state)
{
	/* do nothing */
	return 0;
}

static void ade_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct ade_crtc *acrtc = to_ade_crtc(crtc);
	struct ade_hw_ctx *ctx = acrtc->ctx;
	struct drm_display_mode *mode = &crtc->state->mode;
	struct drm_display_mode *adj_mode = &crtc->state->adjusted_mode;

	if (!ctx->power_on)
		(void)ade_power_up(ctx);
	ade_ldi_set_mode(acrtc, mode, adj_mode);
}

static void ade_crtc_atomic_begin(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_state)
{
	struct ade_crtc *acrtc = to_ade_crtc(crtc);
	struct ade_hw_ctx *ctx = acrtc->ctx;

	if (!ctx->power_on)
		(void)ade_power_up(ctx);
}

static void ade_crtc_atomic_flush(struct drm_crtc *crtc,
				  struct drm_crtc_state *old_state)

{
	struct ade_crtc *acrtc = to_ade_crtc(crtc);
	struct ade_hw_ctx *ctx = acrtc->ctx;
	void __iomem *base = ctx->base;

	/* only crtc is enabled regs take effect */
	if (acrtc->enable) {
		/* flush ade registers */
		writel(ADE_ENABLE, base + ADE_EN);
	}
}

static const struct drm_crtc_helper_funcs ade_crtc_helper_funcs = {
	.enable		= ade_crtc_enable,
	.disable	= ade_crtc_disable,
	.atomic_check	= ade_crtc_atomic_check,
	.mode_set_nofb	= ade_crtc_mode_set_nofb,
	.atomic_begin	= ade_crtc_atomic_begin,
	.atomic_flush	= ade_crtc_atomic_flush,
};

static const struct drm_crtc_funcs ade_crtc_funcs = {
	.destroy	= drm_crtc_cleanup,
	.set_config	= drm_atomic_helper_set_config,
	.page_flip	= drm_atomic_helper_page_flip,
	.reset		= drm_atomic_helper_crtc_reset,
	.set_property = drm_atomic_helper_crtc_set_property,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
};

static int ade_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
			 struct drm_plane *plane)
{
	struct kirin_drm_private *priv = dev->dev_private;
	struct device_node *port;
	int ret;

	/* set crtc port so that
	 * drm_of_find_possible_crtcs call works
	 */
	port = of_get_child_by_name(dev->dev->of_node, "port");
	if (!port) {
		DRM_ERROR("no port node found in %s\n",
			  dev->dev->of_node->full_name);
		return -EINVAL;
	}
	of_node_put(port);
	crtc->port = port;

	ret = drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					&ade_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("failed to init crtc.\n");
		return ret;
	}

	drm_crtc_helper_add(crtc, &ade_crtc_helper_funcs);
	priv->crtc[drm_crtc_index(crtc)] = crtc;

	return 0;
}

static int ade_dts_parse(struct platform_device *pdev, struct ade_hw_ctx *ctx)
{
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ctx->base)) {
		DRM_ERROR("failed to remap ade io base\n");
		return  PTR_ERR(ctx->base);
	}

	ctx->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(ctx->reset))
		return PTR_ERR(ctx->reset);

	ctx->noc_regmap =
		syscon_regmap_lookup_by_phandle(np, "hisilicon,noc-syscon");
	if (IS_ERR(ctx->noc_regmap)) {
		DRM_ERROR("failed to get noc regmap\n");
		return PTR_ERR(ctx->noc_regmap);
	}

	ctx->irq = platform_get_irq(pdev, 0);
	if (ctx->irq < 0) {
		DRM_ERROR("failed to get irq\n");
		return -ENODEV;
	}

	ctx->ade_core_clk = devm_clk_get(dev, "clk_ade_core");
	if (!ctx->ade_core_clk) {
		DRM_ERROR("failed to parse clk ADE_CORE\n");
		return -ENODEV;
	}

	ctx->media_noc_clk = devm_clk_get(dev, "clk_codec_jpeg");
	if (!ctx->media_noc_clk) {
		DRM_ERROR("failed to parse clk CODEC_JPEG\n");
	    return -ENODEV;
	}

	ctx->ade_pix_clk = devm_clk_get(dev, "clk_ade_pix");
	if (!ctx->ade_pix_clk) {
		DRM_ERROR("failed to parse clk ADE_PIX\n");
	    return -ENODEV;
	}

	return 0;
}

static int ade_drm_init(struct drm_device *dev)
{
	struct platform_device *pdev = dev->platformdev;
	struct ade_data *ade;
	struct ade_hw_ctx *ctx;
	struct ade_crtc *acrtc;
	int ret;

	ade = devm_kzalloc(dev->dev, sizeof(*ade), GFP_KERNEL);
	if (!ade) {
		DRM_ERROR("failed to alloc ade_data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, ade);

	ctx = &ade->ctx;
	acrtc = &ade->acrtc;
	acrtc->ctx = ctx;
	acrtc->out_format = LDI_OUT_RGB_888;

	ret = ade_dts_parse(pdev, ctx);
	if (ret)
		return ret;

	return 0;
}

static void ade_drm_cleanup(struct drm_device *dev)
{
	struct platform_device *pdev = dev->platformdev;
	struct ade_data *ade = platform_get_drvdata(pdev);
	struct drm_crtc *crtc = &ade->acrtc.base;

	drm_crtc_cleanup(crtc);
}

const struct kirin_dc_ops ade_dc_ops = {
	.init = ade_drm_init,
	.cleanup = ade_drm_cleanup
};
