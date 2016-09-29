/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <linux/kernel.h>
#include <linux/component.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/clk.h>

#include "mtk_dpi_regs.h"
#include "mtk_drm_ddp_comp.h"

enum mtk_dpi_out_bit_num {
	MTK_DPI_OUT_BIT_NUM_8BITS,
	MTK_DPI_OUT_BIT_NUM_10BITS,
	MTK_DPI_OUT_BIT_NUM_12BITS,
	MTK_DPI_OUT_BIT_NUM_16BITS
};

enum mtk_dpi_out_yc_map {
	MTK_DPI_OUT_YC_MAP_RGB,
	MTK_DPI_OUT_YC_MAP_CYCY,
	MTK_DPI_OUT_YC_MAP_YCYC,
	MTK_DPI_OUT_YC_MAP_CY,
	MTK_DPI_OUT_YC_MAP_YC
};

enum mtk_dpi_out_channel_swap {
	MTK_DPI_OUT_CHANNEL_SWAP_RGB,
	MTK_DPI_OUT_CHANNEL_SWAP_GBR,
	MTK_DPI_OUT_CHANNEL_SWAP_BRG,
	MTK_DPI_OUT_CHANNEL_SWAP_RBG,
	MTK_DPI_OUT_CHANNEL_SWAP_GRB,
	MTK_DPI_OUT_CHANNEL_SWAP_BGR
};

enum mtk_dpi_out_color_format {
	MTK_DPI_COLOR_FORMAT_RGB,
	MTK_DPI_COLOR_FORMAT_RGB_FULL,
	MTK_DPI_COLOR_FORMAT_YCBCR_444,
	MTK_DPI_COLOR_FORMAT_YCBCR_422,
	MTK_DPI_COLOR_FORMAT_XV_YCC,
	MTK_DPI_COLOR_FORMAT_YCBCR_444_FULL,
	MTK_DPI_COLOR_FORMAT_YCBCR_422_FULL
};

struct mtk_dpi {
	struct mtk_ddp_comp ddp_comp;
	struct drm_encoder encoder;
	void __iomem *regs;
	struct device *dev;
	struct clk *engine_clk;
	struct clk *pixel_clk;
	struct clk *tvd_clk;
	int irq;
	struct drm_display_mode mode;
	enum mtk_dpi_out_color_format color_format;
	enum mtk_dpi_out_yc_map yc_map;
	enum mtk_dpi_out_bit_num bit_num;
	enum mtk_dpi_out_channel_swap channel_swap;
	bool power_sta;
	u8 power_ctl;
};

static inline struct mtk_dpi *mtk_dpi_from_encoder(struct drm_encoder *e)
{
	return container_of(e, struct mtk_dpi, encoder);
}

enum mtk_dpi_polarity {
	MTK_DPI_POLARITY_RISING,
	MTK_DPI_POLARITY_FALLING,
};

enum mtk_dpi_power_ctl {
	DPI_POWER_START = BIT(0),
	DPI_POWER_ENABLE = BIT(1),
};

struct mtk_dpi_polarities {
	enum mtk_dpi_polarity de_pol;
	enum mtk_dpi_polarity ck_pol;
	enum mtk_dpi_polarity hsync_pol;
	enum mtk_dpi_polarity vsync_pol;
};

struct mtk_dpi_sync_param {
	u32 sync_width;
	u32 front_porch;
	u32 back_porch;
	bool shift_half_line;
};

struct mtk_dpi_yc_limit {
	u16 y_top;
	u16 y_bottom;
	u16 c_top;
	u16 c_bottom;
};

static void mtk_dpi_mask(struct mtk_dpi *dpi, u32 offset, u32 val, u32 mask)
{
	u32 tmp = readl(dpi->regs + offset) & ~mask;

	tmp |= (val & mask);
	writel(tmp, dpi->regs + offset);
}

static void mtk_dpi_sw_reset(struct mtk_dpi *dpi, bool reset)
{
	mtk_dpi_mask(dpi, DPI_RET, reset ? RST : 0, RST);
}

static void mtk_dpi_enable(struct mtk_dpi *dpi)
{
	mtk_dpi_mask(dpi, DPI_EN, EN, EN);
}

static void mtk_dpi_disable(struct mtk_dpi *dpi)
{
	mtk_dpi_mask(dpi, DPI_EN, 0, EN);
}

static void mtk_dpi_config_hsync(struct mtk_dpi *dpi,
				 struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_mask(dpi, DPI_TGEN_HWIDTH,
		     sync->sync_width << HPW, HPW_MASK);
	mtk_dpi_mask(dpi, DPI_TGEN_HPORCH,
		     sync->back_porch << HBP, HBP_MASK);
	mtk_dpi_mask(dpi, DPI_TGEN_HPORCH, sync->front_porch << HFP,
		     HFP_MASK);
}

static void mtk_dpi_config_vsync(struct mtk_dpi *dpi,
				 struct mtk_dpi_sync_param *sync,
				 u32 width_addr, u32 porch_addr)
{
	mtk_dpi_mask(dpi, width_addr,
		     sync->sync_width << VSYNC_WIDTH_SHIFT,
		     VSYNC_WIDTH_MASK);
	mtk_dpi_mask(dpi, width_addr,
		     sync->shift_half_line << VSYNC_HALF_LINE_SHIFT,
		     VSYNC_HALF_LINE_MASK);
	mtk_dpi_mask(dpi, porch_addr,
		     sync->back_porch << VSYNC_BACK_PORCH_SHIFT,
		     VSYNC_BACK_PORCH_MASK);
	mtk_dpi_mask(dpi, porch_addr,
		     sync->front_porch << VSYNC_FRONT_PORCH_SHIFT,
		     VSYNC_FRONT_PORCH_MASK);
}

static void mtk_dpi_config_vsync_lodd(struct mtk_dpi *dpi,
				      struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH, DPI_TGEN_VPORCH);
}

static void mtk_dpi_config_vsync_leven(struct mtk_dpi *dpi,
				       struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH_LEVEN,
			     DPI_TGEN_VPORCH_LEVEN);
}

static void mtk_dpi_config_vsync_rodd(struct mtk_dpi *dpi,
				      struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH_RODD,
			     DPI_TGEN_VPORCH_RODD);
}

static void mtk_dpi_config_vsync_reven(struct mtk_dpi *dpi,
				       struct mtk_dpi_sync_param *sync)
{
	mtk_dpi_config_vsync(dpi, sync, DPI_TGEN_VWIDTH_REVEN,
			     DPI_TGEN_VPORCH_REVEN);
}

static void mtk_dpi_config_pol(struct mtk_dpi *dpi,
			       struct mtk_dpi_polarities *dpi_pol)
{
	unsigned int pol;

	pol = (dpi_pol->ck_pol == MTK_DPI_POLARITY_RISING ? 0 : CK_POL) |
	      (dpi_pol->de_pol == MTK_DPI_POLARITY_RISING ? 0 : DE_POL) |
	      (dpi_pol->hsync_pol == MTK_DPI_POLARITY_RISING ? 0 : HSYNC_POL) |
	      (dpi_pol->vsync_pol == MTK_DPI_POLARITY_RISING ? 0 : VSYNC_POL);
	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, pol,
		     CK_POL | DE_POL | HSYNC_POL | VSYNC_POL);
}

static void mtk_dpi_config_3d(struct mtk_dpi *dpi, bool en_3d)
{
	mtk_dpi_mask(dpi, DPI_CON, en_3d ? TDFP_EN : 0, TDFP_EN);
}

static void mtk_dpi_config_interface(struct mtk_dpi *dpi, bool inter)
{
	mtk_dpi_mask(dpi, DPI_CON, inter ? INTL_EN : 0, INTL_EN);
}

static void mtk_dpi_config_fb_size(struct mtk_dpi *dpi, u32 width, u32 height)
{
	mtk_dpi_mask(dpi, DPI_SIZE, width << HSIZE, HSIZE_MASK);
	mtk_dpi_mask(dpi, DPI_SIZE, height << VSIZE, VSIZE_MASK);
}

static void mtk_dpi_config_channel_limit(struct mtk_dpi *dpi,
					 struct mtk_dpi_yc_limit *limit)
{
	mtk_dpi_mask(dpi, DPI_Y_LIMIT, limit->y_bottom << Y_LIMINT_BOT,
		     Y_LIMINT_BOT_MASK);
	mtk_dpi_mask(dpi, DPI_Y_LIMIT, limit->y_top << Y_LIMINT_TOP,
		     Y_LIMINT_TOP_MASK);
	mtk_dpi_mask(dpi, DPI_C_LIMIT, limit->c_bottom << C_LIMIT_BOT,
		     C_LIMIT_BOT_MASK);
	mtk_dpi_mask(dpi, DPI_C_LIMIT, limit->c_top << C_LIMIT_TOP,
		     C_LIMIT_TOP_MASK);
}

static void mtk_dpi_config_bit_num(struct mtk_dpi *dpi,
				   enum mtk_dpi_out_bit_num num)
{
	u32 val;

	switch (num) {
	case MTK_DPI_OUT_BIT_NUM_8BITS:
		val = OUT_BIT_8;
		break;
	case MTK_DPI_OUT_BIT_NUM_10BITS:
		val = OUT_BIT_10;
		break;
	case MTK_DPI_OUT_BIT_NUM_12BITS:
		val = OUT_BIT_12;
		break;
	case MTK_DPI_OUT_BIT_NUM_16BITS:
		val = OUT_BIT_16;
		break;
	default:
		val = OUT_BIT_8;
		break;
	}
	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, val << OUT_BIT,
		     OUT_BIT_MASK);
}

static void mtk_dpi_config_yc_map(struct mtk_dpi *dpi,
				  enum mtk_dpi_out_yc_map map)
{
	u32 val;

	switch (map) {
	case MTK_DPI_OUT_YC_MAP_RGB:
		val = YC_MAP_RGB;
		break;
	case MTK_DPI_OUT_YC_MAP_CYCY:
		val = YC_MAP_CYCY;
		break;
	case MTK_DPI_OUT_YC_MAP_YCYC:
		val = YC_MAP_YCYC;
		break;
	case MTK_DPI_OUT_YC_MAP_CY:
		val = YC_MAP_CY;
		break;
	case MTK_DPI_OUT_YC_MAP_YC:
		val = YC_MAP_YC;
		break;
	default:
		val = YC_MAP_RGB;
		break;
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, val << YC_MAP, YC_MAP_MASK);
}

static void mtk_dpi_config_channel_swap(struct mtk_dpi *dpi,
					enum mtk_dpi_out_channel_swap swap)
{
	u32 val;

	switch (swap) {
	case MTK_DPI_OUT_CHANNEL_SWAP_RGB:
		val = SWAP_RGB;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_GBR:
		val = SWAP_GBR;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_BRG:
		val = SWAP_BRG;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_RBG:
		val = SWAP_RBG;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_GRB:
		val = SWAP_GRB;
		break;
	case MTK_DPI_OUT_CHANNEL_SWAP_BGR:
		val = SWAP_BGR;
		break;
	default:
		val = SWAP_RGB;
		break;
	}

	mtk_dpi_mask(dpi, DPI_OUTPUT_SETTING, val << CH_SWAP, CH_SWAP_MASK);
}

static void mtk_dpi_config_yuv422_enable(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? YUV422_EN : 0, YUV422_EN);
}

static void mtk_dpi_config_csc_enable(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? CSC_ENABLE : 0, CSC_ENABLE);
}

static void mtk_dpi_config_swap_input(struct mtk_dpi *dpi, bool enable)
{
	mtk_dpi_mask(dpi, DPI_CON, enable ? IN_RB_SWAP : 0, IN_RB_SWAP);
}

static void mtk_dpi_config_2n_h_fre(struct mtk_dpi *dpi)
{
	mtk_dpi_mask(dpi, DPI_H_FRE_CON, H_FRE_2N, H_FRE_2N);
}

static void mtk_dpi_config_color_format(struct mtk_dpi *dpi,
					enum mtk_dpi_out_color_format format)
{
	if ((format == MTK_DPI_COLOR_FORMAT_YCBCR_444) ||
	    (format == MTK_DPI_COLOR_FORMAT_YCBCR_444_FULL)) {
		mtk_dpi_config_yuv422_enable(dpi, false);
		mtk_dpi_config_csc_enable(dpi, true);
		mtk_dpi_config_swap_input(dpi, false);
		mtk_dpi_config_channel_swap(dpi, MTK_DPI_OUT_CHANNEL_SWAP_BGR);
	} else if ((format == MTK_DPI_COLOR_FORMAT_YCBCR_422) ||
		   (format == MTK_DPI_COLOR_FORMAT_YCBCR_422_FULL)) {
		mtk_dpi_config_yuv422_enable(dpi, true);
		mtk_dpi_config_csc_enable(dpi, true);
		mtk_dpi_config_swap_input(dpi, true);
		mtk_dpi_config_channel_swap(dpi, MTK_DPI_OUT_CHANNEL_SWAP_RGB);
	} else {
		mtk_dpi_config_yuv422_enable(dpi, false);
		mtk_dpi_config_csc_enable(dpi, false);
		mtk_dpi_config_swap_input(dpi, false);
		mtk_dpi_config_channel_swap(dpi, MTK_DPI_OUT_CHANNEL_SWAP_RGB);
	}
}

static void mtk_dpi_power_off(struct mtk_dpi *dpi, enum mtk_dpi_power_ctl pctl)
{
	dpi->power_ctl &= ~pctl;

	if ((dpi->power_ctl & DPI_POWER_START) ||
	    (dpi->power_ctl & DPI_POWER_ENABLE))
		return;

	if (!dpi->power_sta)
		return;

	mtk_dpi_disable(dpi);
	clk_disable_unprepare(dpi->pixel_clk);
	clk_disable_unprepare(dpi->engine_clk);
	dpi->power_sta = false;
}

static int mtk_dpi_power_on(struct mtk_dpi *dpi, enum mtk_dpi_power_ctl pctl)
{
	int ret;

	dpi->power_ctl |= pctl;

	if (!(dpi->power_ctl & DPI_POWER_START) &&
	    !(dpi->power_ctl & DPI_POWER_ENABLE))
		return 0;

	if (dpi->power_sta)
		return 0;

	ret = clk_prepare_enable(dpi->engine_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable engine clock: %d\n", ret);
		goto err_eng;
	}

	ret = clk_prepare_enable(dpi->pixel_clk);
	if (ret) {
		dev_err(dpi->dev, "Failed to enable pixel clock: %d\n", ret);
		goto err_pixel;
	}

	mtk_dpi_enable(dpi);
	dpi->power_sta = true;
	return 0;

err_pixel:
	clk_disable_unprepare(dpi->engine_clk);
err_eng:
	dpi->power_ctl &= ~pctl;
	return ret;
}

static int mtk_dpi_set_display_mode(struct mtk_dpi *dpi,
				    struct drm_display_mode *mode)
{
	struct mtk_dpi_yc_limit limit;
	struct mtk_dpi_polarities dpi_pol;
	struct mtk_dpi_sync_param hsync;
	struct mtk_dpi_sync_param vsync_lodd = { 0 };
	struct mtk_dpi_sync_param vsync_leven = { 0 };
	struct mtk_dpi_sync_param vsync_rodd = { 0 };
	struct mtk_dpi_sync_param vsync_reven = { 0 };
	unsigned long pix_rate;
	unsigned long pll_rate;
	unsigned int factor;

	/* let pll_rate can fix the valid range of tvdpll (1G~2GHz) */
	pix_rate = 1000UL * mode->clock;
	if (mode->clock <= 27000)
		factor = 16 * 3;
	else if (mode->clock <= 84000)
		factor = 8 * 3;
	else if (mode->clock <= 167000)
		factor = 4 * 3;
	else
		factor = 2 * 3;
	pll_rate = pix_rate * factor;

	dev_dbg(dpi->dev, "Want PLL %lu Hz, pixel clock %lu Hz\n",
		pll_rate, pix_rate);

	clk_set_rate(dpi->tvd_clk, pll_rate);
	pll_rate = clk_get_rate(dpi->tvd_clk);

	pix_rate = pll_rate / factor;
	clk_set_rate(dpi->pixel_clk, pix_rate);
	pix_rate = clk_get_rate(dpi->pixel_clk);

	dev_dbg(dpi->dev, "Got  PLL %lu Hz, pixel clock %lu Hz\n",
		pll_rate, pix_rate);

	limit.c_bottom = 0x0010;
	limit.c_top = 0x0FE0;
	limit.y_bottom = 0x0010;
	limit.y_top = 0x0FE0;

	dpi_pol.ck_pol = MTK_DPI_POLARITY_FALLING;
	dpi_pol.de_pol = MTK_DPI_POLARITY_RISING;
	dpi_pol.hsync_pol = mode->flags & DRM_MODE_FLAG_PHSYNC ?
			    MTK_DPI_POLARITY_FALLING : MTK_DPI_POLARITY_RISING;
	dpi_pol.vsync_pol = mode->flags & DRM_MODE_FLAG_PVSYNC ?
			    MTK_DPI_POLARITY_FALLING : MTK_DPI_POLARITY_RISING;

	hsync.sync_width = mode->hsync_end - mode->hsync_start;
	hsync.back_porch = mode->htotal - mode->hsync_end;
	hsync.front_porch = mode->hsync_start - mode->hdisplay;
	hsync.shift_half_line = false;

	vsync_lodd.sync_width = mode->vsync_end - mode->vsync_start;
	vsync_lodd.back_porch = mode->vtotal - mode->vsync_end;
	vsync_lodd.front_porch = mode->vsync_start - mode->vdisplay;
	vsync_lodd.shift_half_line = false;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE &&
	    mode->flags & DRM_MODE_FLAG_3D_MASK) {
		vsync_leven = vsync_lodd;
		vsync_rodd = vsync_lodd;
		vsync_reven = vsync_lodd;
		vsync_leven.shift_half_line = true;
		vsync_reven.shift_half_line = true;
	} else if (mode->flags & DRM_MODE_FLAG_INTERLACE &&
		   !(mode->flags & DRM_MODE_FLAG_3D_MASK)) {
		vsync_leven = vsync_lodd;
		vsync_leven.shift_half_line = true;
	} else if (!(mode->flags & DRM_MODE_FLAG_INTERLACE) &&
		   mode->flags & DRM_MODE_FLAG_3D_MASK) {
		vsync_rodd = vsync_lodd;
	}
	mtk_dpi_sw_reset(dpi, true);
	mtk_dpi_config_pol(dpi, &dpi_pol);

	mtk_dpi_config_hsync(dpi, &hsync);
	mtk_dpi_config_vsync_lodd(dpi, &vsync_lodd);
	mtk_dpi_config_vsync_rodd(dpi, &vsync_rodd);
	mtk_dpi_config_vsync_leven(dpi, &vsync_leven);
	mtk_dpi_config_vsync_reven(dpi, &vsync_reven);

	mtk_dpi_config_3d(dpi, !!(mode->flags & DRM_MODE_FLAG_3D_MASK));
	mtk_dpi_config_interface(dpi, !!(mode->flags &
					 DRM_MODE_FLAG_INTERLACE));
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		mtk_dpi_config_fb_size(dpi, mode->hdisplay, mode->vdisplay / 2);
	else
		mtk_dpi_config_fb_size(dpi, mode->hdisplay, mode->vdisplay);

	mtk_dpi_config_channel_limit(dpi, &limit);
	mtk_dpi_config_bit_num(dpi, dpi->bit_num);
	mtk_dpi_config_channel_swap(dpi, dpi->channel_swap);
	mtk_dpi_config_yc_map(dpi, dpi->yc_map);
	mtk_dpi_config_color_format(dpi, dpi->color_format);
	mtk_dpi_config_2n_h_fre(dpi);
	mtk_dpi_sw_reset(dpi, false);

	return 0;
}

static void mtk_dpi_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs mtk_dpi_encoder_funcs = {
	.destroy = mtk_dpi_encoder_destroy,
};

static bool mtk_dpi_encoder_mode_fixup(struct drm_encoder *encoder,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mtk_dpi_encoder_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct mtk_dpi *dpi = mtk_dpi_from_encoder(encoder);

	drm_mode_copy(&dpi->mode, adjusted_mode);
}

static void mtk_dpi_encoder_disable(struct drm_encoder *encoder)
{
	struct mtk_dpi *dpi = mtk_dpi_from_encoder(encoder);

	mtk_dpi_power_off(dpi, DPI_POWER_ENABLE);
}

static void mtk_dpi_encoder_enable(struct drm_encoder *encoder)
{
	struct mtk_dpi *dpi = mtk_dpi_from_encoder(encoder);

	mtk_dpi_power_on(dpi, DPI_POWER_ENABLE);
	mtk_dpi_set_display_mode(dpi, &dpi->mode);
}

static int mtk_dpi_atomic_check(struct drm_encoder *encoder,
				struct drm_crtc_state *crtc_state,
				struct drm_connector_state *conn_state)
{
	return 0;
}

static const struct drm_encoder_helper_funcs mtk_dpi_encoder_helper_funcs = {
	.mode_fixup = mtk_dpi_encoder_mode_fixup,
	.mode_set = mtk_dpi_encoder_mode_set,
	.disable = mtk_dpi_encoder_disable,
	.enable = mtk_dpi_encoder_enable,
	.atomic_check = mtk_dpi_atomic_check,
};

static void mtk_dpi_start(struct mtk_ddp_comp *comp)
{
	struct mtk_dpi *dpi = container_of(comp, struct mtk_dpi, ddp_comp);

	mtk_dpi_power_on(dpi, DPI_POWER_START);
}

static void mtk_dpi_stop(struct mtk_ddp_comp *comp)
{
	struct mtk_dpi *dpi = container_of(comp, struct mtk_dpi, ddp_comp);

	mtk_dpi_power_off(dpi, DPI_POWER_START);
}

static const struct mtk_ddp_comp_funcs mtk_dpi_funcs = {
	.start = mtk_dpi_start,
	.stop = mtk_dpi_stop,
};

static int mtk_dpi_bind(struct device *dev, struct device *master, void *data)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	int ret;

	ret = mtk_ddp_comp_register(drm_dev, &dpi->ddp_comp);
	if (ret < 0) {
		dev_err(dev, "Failed to register component %s: %d\n",
			dev->of_node->full_name, ret);
		return ret;
	}

	ret = drm_encoder_init(drm_dev, &dpi->encoder, &mtk_dpi_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret) {
		dev_err(dev, "Failed to initialize decoder: %d\n", ret);
		goto err_unregister;
	}
	drm_encoder_helper_add(&dpi->encoder, &mtk_dpi_encoder_helper_funcs);

	/* Currently DPI0 is fixed to be driven by OVL1 */
	dpi->encoder.possible_crtcs = BIT(1);

	dpi->encoder.bridge->encoder = &dpi->encoder;
	ret = drm_bridge_attach(dpi->encoder.dev, dpi->encoder.bridge);
	if (ret) {
		dev_err(dev, "Failed to attach bridge: %d\n", ret);
		goto err_cleanup;
	}

	dpi->bit_num = MTK_DPI_OUT_BIT_NUM_8BITS;
	dpi->channel_swap = MTK_DPI_OUT_CHANNEL_SWAP_RGB;
	dpi->yc_map = MTK_DPI_OUT_YC_MAP_RGB;
	dpi->color_format = MTK_DPI_COLOR_FORMAT_RGB;

	return 0;

err_cleanup:
	drm_encoder_cleanup(&dpi->encoder);
err_unregister:
	mtk_ddp_comp_unregister(drm_dev, &dpi->ddp_comp);
	return ret;
}

static void mtk_dpi_unbind(struct device *dev, struct device *master,
			   void *data)
{
	struct mtk_dpi *dpi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;

	drm_encoder_cleanup(&dpi->encoder);
	mtk_ddp_comp_unregister(drm_dev, &dpi->ddp_comp);
}

static const struct component_ops mtk_dpi_component_ops = {
	.bind = mtk_dpi_bind,
	.unbind = mtk_dpi_unbind,
};

static int mtk_dpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_dpi *dpi;
	struct resource *mem;
	struct device_node *ep, *bridge_node = NULL;
	int comp_id;
	int ret;

	dpi = devm_kzalloc(dev, sizeof(*dpi), GFP_KERNEL);
	if (!dpi)
		return -ENOMEM;

	dpi->dev = dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dpi->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(dpi->regs)) {
		ret = PTR_ERR(dpi->regs);
		dev_err(dev, "Failed to ioremap mem resource: %d\n", ret);
		return ret;
	}

	dpi->engine_clk = devm_clk_get(dev, "engine");
	if (IS_ERR(dpi->engine_clk)) {
		ret = PTR_ERR(dpi->engine_clk);
		dev_err(dev, "Failed to get engine clock: %d\n", ret);
		return ret;
	}

	dpi->pixel_clk = devm_clk_get(dev, "pixel");
	if (IS_ERR(dpi->pixel_clk)) {
		ret = PTR_ERR(dpi->pixel_clk);
		dev_err(dev, "Failed to get pixel clock: %d\n", ret);
		return ret;
	}

	dpi->tvd_clk = devm_clk_get(dev, "pll");
	if (IS_ERR(dpi->tvd_clk)) {
		ret = PTR_ERR(dpi->tvd_clk);
		dev_err(dev, "Failed to get tvdpll clock: %d\n", ret);
		return ret;
	}

	dpi->irq = platform_get_irq(pdev, 0);
	if (dpi->irq <= 0) {
		dev_err(dev, "Failed to get irq: %d\n", dpi->irq);
		return -EINVAL;
	}

	ep = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (ep) {
		bridge_node = of_graph_get_remote_port_parent(ep);
		of_node_put(ep);
	}
	if (!bridge_node) {
		dev_err(dev, "Failed to find bridge node\n");
		return -ENODEV;
	}

	dev_info(dev, "Found bridge node: %s\n", bridge_node->full_name);

	dpi->encoder.bridge = of_drm_find_bridge(bridge_node);
	of_node_put(bridge_node);
	if (!dpi->encoder.bridge)
		return -EPROBE_DEFER;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DPI);
	if (comp_id < 0) {
		dev_err(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	ret = mtk_ddp_comp_init(dev, dev->of_node, &dpi->ddp_comp, comp_id,
				&mtk_dpi_funcs);
	if (ret) {
		dev_err(dev, "Failed to initialize component: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, dpi);

	ret = component_add(dev, &mtk_dpi_component_ops);
	if (ret) {
		dev_err(dev, "Failed to add component: %d\n", ret);
		return ret;
	}

	return 0;
}

static int mtk_dpi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_dpi_component_ops);

	return 0;
}

static const struct of_device_id mtk_dpi_of_ids[] = {
	{ .compatible = "mediatek,mt8173-dpi", },
	{}
};

struct platform_driver mtk_dpi_driver = {
	.probe = mtk_dpi_probe,
	.remove = mtk_dpi_remove,
	.driver = {
		.name = "mediatek-dpi",
		.of_match_table = mtk_dpi_of_ids,
	},
};
