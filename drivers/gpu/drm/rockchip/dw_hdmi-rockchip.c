/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rockchip/cpu.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>
#include <drm/bridge/dw_hdmi.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define RK3228_GRF_SOC_CON2		0x0408
#define RK3228_DDC_MASK_EN		((3 << 13) | (3 << (13 + 16)))
#define RK3228_GRF_SOC_CON6		0x0418
#define RK3228_IO_3V_DOMAIN		((7 << 4) | (7 << (4 + 16)))

#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)
#define RK3288_GRF_SOC_CON16		0x03a8
#define RK3288_HDMI_LCDC0_YUV420	BIT(2)
#define RK3288_HDMI_LCDC1_YUV420	BIT(3)
#define RK3366_GRF_SOC_CON0		0x0400
#define RK3366_HDMI_LCDC_SEL		BIT(1)
#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		BIT(6)

#define RK3328_GRF_SOC_CON2		0x0408
#define RK3328_DDC_MASK_EN		((3 << 10) | (3 << (10 + 16)))
#define RK3328_GRF_SOC_CON3		0x040c
#define RK3328_IO_CTRL_BY_HDMI		(0xf0000000 | BIT(13) | BIT(12))
#define RK3328_GRF_SOC_CON4		0x0410
#define RK3328_IO_3V_DOMAIN		(7 << (9 + 16))
#define RK3328_IO_5V_DOMAIN		((7 << 9) | (3 << (9 + 16)))
#define RK3328_HPD_3V			(BIT(8 + 16) | BIT(13 + 16))

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)

/* HDMI output pixel format */
enum drm_hdmi_output_type {
	DRM_HDMI_OUTPUT_DEFAULT_RGB, /* default RGB */
	DRM_HDMI_OUTPUT_YCBCR444, /* YCBCR 444 */
	DRM_HDMI_OUTPUT_YCBCR422, /* YCBCR 422 */
	DRM_HDMI_OUTPUT_YCBCR420, /* YCBCR 420 */
	DRM_HDMI_OUTPUT_YCBCR_HQ, /* Highest subsampled YUV */
	DRM_HDMI_OUTPUT_YCBCR_LQ, /* Lowest subsampled YUV */
	DRM_HDMI_OUTPUT_INVALID, /* Guess what ? */
};

enum dw_hdmi_rockchip_color_depth {
	ROCKCHIP_HDMI_DEPTH_8,
	ROCKCHIP_HDMI_DEPTH_10,
	ROCKCHIP_HDMI_DEPTH_12,
	ROCKCHIP_HDMI_DEPTH_16,
	ROCKCHIP_HDMI_DEPTH_420_10,
	ROCKCHIP_HDMI_DEPTH_420_12,
	ROCKCHIP_HDMI_DEPTH_420_16
};

struct rockchip_hdmi {
	struct device *dev;
	struct regmap *regmap;
	void __iomem *hdmiphy;
	struct drm_encoder encoder;
	enum dw_hdmi_devtype dev_type;
	struct clk *vpll_clk;
	struct clk *grf_clk;
	struct clk *hclk_vio;
	struct clk *dclk;

	struct phy *phy;

	unsigned long bus_format;
	unsigned long output_bus_format;
	unsigned long enc_out_encoding;

	struct drm_property *color_depth_property;
	struct drm_property *hdmi_output_property;
	struct drm_property *colordepth_capacity;
	struct drm_property *outputmode_capacity;
	struct drm_property *colorimetry_property;

	unsigned int colordepth;
	unsigned int colorimetry;
	unsigned int phy_bus_width;
	enum drm_hdmi_output_type hdmi_output;
};

#define to_rockchip_hdmi(x)	container_of(x, struct rockchip_hdmi, x)

static void inno_dw_hdmi_phy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	while (hdmi->phy->power_count > 0)
		phy_power_off(hdmi->phy);
}

static int
inno_dw_hdmi_phy_init(struct dw_hdmi *dw_hdmi, void *data,
		      struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	inno_dw_hdmi_phy_disable(dw_hdmi, data);
	dw_hdmi_set_high_tmds_clock_ratio(dw_hdmi);
	return phy_power_on(hdmi->phy);
}

static enum drm_connector_status
inno_dw_hdmi_phy_read_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	enum drm_connector_status status;

	status = dw_hdmi_phy_read_hpd(dw_hdmi, data);

	if (hdmi->dev_type == RK3228_HDMI)
		return status;

	if (status == connector_status_connected)
		regmap_write(hdmi->regmap,
			     RK3328_GRF_SOC_CON4,
			     RK3328_IO_5V_DOMAIN);
	else
		regmap_write(hdmi->regmap,
			     RK3328_GRF_SOC_CON4,
			     RK3328_IO_3V_DOMAIN);
	return status;
}

static int inno_dw_hdmi_init(struct rockchip_hdmi *hdmi)
{
	int ret;

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		dev_err(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return -EPROBE_DEFER;
	}
	if (hdmi->dev_type == RK3328_HDMI) {
		/* Map HPD pin to 3V io */
		regmap_write(hdmi->regmap,
			     RK3328_GRF_SOC_CON4,
			     RK3328_IO_3V_DOMAIN |
			     RK3328_HPD_3V);
		/* Map ddc pin to 5V io */
		regmap_write(hdmi->regmap,
			     RK3328_GRF_SOC_CON3,
			     RK3328_IO_CTRL_BY_HDMI);
		regmap_write(hdmi->regmap,
			     RK3328_GRF_SOC_CON2,
			     RK3328_DDC_MASK_EN |
			     BIT(18));
	} else {
		regmap_write(hdmi->regmap, RK3228_GRF_SOC_CON2,
			     RK3228_DDC_MASK_EN);
		regmap_write(hdmi->regmap, RK3228_GRF_SOC_CON6,
			     RK3228_IO_3V_DOMAIN);
	}
	clk_disable_unprepare(hdmi->grf_clk);
	return 0;
}

/*
 * There are some rates that would be ranged for better clock jitter at
 * Chrome OS tree, like 25.175Mhz would range to 25.170732Mhz. But due
 * to the clock is aglined to KHz in struct drm_display_mode, this would
 * bring some inaccurate error if we still run the compute_n math, so
 * let's just code an const table for it until we can actually get the
 * right clock rate.
 */
static const struct dw_hdmi_audio_tmds_n rockchip_werid_tmds_n_table[] = {
	/* 25176471 for 25.175 MHz = 428000000 / 17. */
	{ .tmds = 25177000, .n_32k = 4352, .n_44k1 = 14994, .n_48k = 6528, },
	/* 57290323 for 57.284 MHz */
	{ .tmds = 57291000, .n_32k = 3968, .n_44k1 = 4557, .n_48k = 5952, },
	/* 74437500 for 74.44 MHz = 297750000 / 4 */
	{ .tmds = 74438000, .n_32k = 8192, .n_44k1 = 18816, .n_48k = 4096, },
	/* 118666667 for 118.68 MHz */
	{ .tmds = 118667000, .n_32k = 4224, .n_44k1 = 5292, .n_48k = 6336, },
	/* 121714286 for 121.75 MHz */
	{ .tmds = 121715000, .n_32k = 4480, .n_44k1 = 6174, .n_48k = 6272, },
	/* 136800000 for 136.75 MHz */
	{ .tmds = 136800000, .n_32k = 4096, .n_44k1 = 5684, .n_48k = 6144, },
	/* End of table */
	{ .tmds = 0,         .n_32k = 0,    .n_44k1 = 0,    .n_48k = 0, },
};

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg[] = {
	{
		30666000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40f3, 0x0000 },
		},
	},  {
		36800000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		46000000, {
			{ 0x00b3, 0x0000 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		61333000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		73600000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x4061, 0x0002 },
		},
	},  {
		92000000, {
			{ 0x0072, 0x0001 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		122666000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		147200000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4064, 0x0003 },
		},
	},  {
		184000000, {
			{ 0x0051, 0x0002 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		226666000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		272000000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		340000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x1a40, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg_420[] = {
	{
		30666000, {
			{ 0x00b7, 0x0000 },
			{ 0x2157, 0x0000 },
			{ 0x40f7, 0x0000 },
		},
	},  {
		92000000, {
			{ 0x00b7, 0x0000 },
			{ 0x2143, 0x0001 },
			{ 0x40a3, 0x0001 },
		},
	},  {
		184000000, {
			{ 0x0073, 0x0001 },
			{ 0x2146, 0x0002 },
			{ 0x4062, 0x0002 },
		},
	},  {
		340000000, {
			{ 0x0052, 0x0003 },
			{ 0x214d, 0x0003 },
			{ 0x4065, 0x0003 },
		},
	},  {
		600000000, {
			{ 0x0041, 0x0003 },
			{ 0x3b4d, 0x0003 },
			{ 0x5a65, 0x0003 },
		},
	},  {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl rockchip_cur_ctr[] = {
	/*      pixelclk    bpp8    bpp10   bpp12 */
	{
		600000000, { 0x0000, 0x0000, 0x0000 },
	},  {
		~0UL,      { 0x0000, 0x0000, 0x0000},
	}
};

static struct dw_hdmi_phy_config rockchip_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 165000000, 0x802b, 0x0004, 0x0209},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ 594000000, 0x8039, 0x0000, 0x019d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static int rockchip_hdmi_update_phy_table(struct rockchip_hdmi *hdmi,
					  u32 *config,
					  int phy_table_size)
{
	int i;

	if (phy_table_size > ARRAY_SIZE(rockchip_phy_config)) {
		dev_err(hdmi->dev, "phy table array number is out of range\n");
		return -E2BIG;
	}

	for (i = 0; i < phy_table_size; i++) {
		if (config[i * 4] != 0)
			rockchip_phy_config[i].mpixelclock = (u64)config[i * 4];
		else
			rockchip_phy_config[i].mpixelclock = ~0UL;
		rockchip_phy_config[i].sym_ctr = (u16)config[i * 4 + 1];
		rockchip_phy_config[i].term = (u16)config[i * 4 + 2];
		rockchip_phy_config[i].vlev_ctr = (u16)config[i * 4 + 3];
	}

	return 0;
}

static int rockchip_hdmi_parse_dt(struct rockchip_hdmi *hdmi)
{
	struct device_node *np = hdmi->dev->of_node;
	int ret, val, phy_table_size;
	u32 *phy_config;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->vpll_clk = devm_clk_get(hdmi->dev, "vpll");
	if (PTR_ERR(hdmi->vpll_clk) == -ENOENT) {
		hdmi->vpll_clk = NULL;
	} else if (PTR_ERR(hdmi->vpll_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->vpll_clk)) {
		dev_err(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->vpll_clk);
	}

	hdmi->grf_clk = devm_clk_get(hdmi->dev, "grf");
	if (PTR_ERR(hdmi->grf_clk) == -ENOENT) {
		hdmi->grf_clk = NULL;
	} else if (PTR_ERR(hdmi->grf_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->grf_clk)) {
		dev_err(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->grf_clk);
	}

	hdmi->hclk_vio = devm_clk_get(hdmi->dev, "hclk_vio");
	if (PTR_ERR(hdmi->hclk_vio) == -ENOENT) {
		hdmi->hclk_vio = NULL;
	} else if (PTR_ERR(hdmi->hclk_vio) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->hclk_vio)) {
		dev_err(hdmi->dev, "failed to get hclk_vio clock\n");
		return PTR_ERR(hdmi->hclk_vio);
	}

	hdmi->dclk = devm_clk_get(hdmi->dev, "dclk");
	if (PTR_ERR(hdmi->dclk) == -ENOENT) {
		hdmi->dclk = NULL;
	} else if (PTR_ERR(hdmi->dclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->dclk)) {
		dev_err(hdmi->dev, "failed to get dclk\n");
		return PTR_ERR(hdmi->dclk);
	}

	ret = clk_prepare_enable(hdmi->vpll_clk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI vpll: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hclk_vio);
	if (ret) {
		dev_err(hdmi->dev, "Failed to eanble HDMI hclk_vio: %d\n",
			ret);
		return ret;
	}

	if (of_get_property(np, "rockchip,phy-table", &val)) {
		phy_config = kmalloc(val, GFP_KERNEL);
		if (!phy_config) {
			/* use default table when kmalloc failed. */
			dev_err(hdmi->dev, "kmalloc phy table failed\n");

			return -ENOMEM;
		}
		phy_table_size = val / 16;
		of_property_read_u32_array(np, "rockchip,phy-table",
					   phy_config, val / sizeof(u32));
		ret = rockchip_hdmi_update_phy_table(hdmi, phy_config,
						     phy_table_size);
		if (ret) {
			kfree(phy_config);
			return ret;
		}
		kfree(phy_config);
	} else {
		dev_dbg(hdmi->dev, "use default hdmi phy table\n");
	}

	return 0;
}

static enum drm_mode_status
dw_hdmi_rockchip_mode_valid(struct drm_connector *connector,
			    struct drm_display_mode *mode)
{
	struct drm_encoder *encoder = connector->encoder;
	enum drm_mode_status status = MODE_OK;
	struct drm_device *dev = connector->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;
	struct rockchip_hdmi *hdmi;

	/*
	 * Pixel clocks we support are always < 2GHz and so fit in an
	 * int.  We should make sure source rate does too so we don't get
	 * overflow when we multiply by 1000.
	 */
	if (mode->clock > INT_MAX / 1000)
		return MODE_BAD;
	/*
	 * If sink max TMDS clock < 340MHz, we should check the mode pixel
	 * clock > 340MHz is YCbCr420 or not.
	 */
	if (mode->clock > 340000 &&
	    connector->display_info.max_tmds_clock < 340000 &&
	    !drm_mode_is_420(&connector->display_info, mode))
		return MODE_BAD;

	if (!encoder) {
		const struct drm_connector_helper_funcs *funcs;

		funcs = connector->helper_private;
		if (funcs->atomic_best_encoder)
			encoder = funcs->atomic_best_encoder(connector,
							     connector->state);
		else
			encoder = funcs->best_encoder(connector);
	}

	if (!encoder || !encoder->possible_crtcs)
		return MODE_BAD;

	hdmi = to_rockchip_hdmi(encoder);
	if (hdmi->dev_type == RK3368_HDMI && mode->clock > 340000 &&
	    !drm_mode_is_420(&connector->display_info, mode))
		return MODE_BAD;
	/*
	 * ensure all drm display mode can work, if someone want support more
	 * resolutions, please limit the possible_crtc, only connect to
	 * needed crtc.
	 */
	drm_for_each_crtc(crtc, connector->dev) {
		int pipe = drm_crtc_index(crtc);
		const struct rockchip_crtc_funcs *funcs =
						priv->crtc_funcs[pipe];

		if (!(encoder->possible_crtcs & drm_crtc_mask(crtc)))
			continue;
		if (!funcs || !funcs->mode_valid)
			continue;

		status = funcs->mode_valid(crtc, mode,
					   DRM_MODE_CONNECTOR_HDMIA);
		if (status != MODE_OK)
			return status;
	}

	return status;
}

static const struct drm_encoder_funcs dw_hdmi_rockchip_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void dw_hdmi_rockchip_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);

	/*
	 * when plug out hdmi it will be switch cvbs and then phy bus width
	 * must be set as 8
	 */
	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, 8);
	clk_disable_unprepare(hdmi->dclk);
}

static void dw_hdmi_rockchip_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	u32 lcdsel_grf_reg, lcdsel_mask;
	u32 val;
	int mux;
	int ret;

	if (WARN_ON(!crtc || !crtc->state))
		return;

	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, hdmi->phy_bus_width);

	clk_set_rate(hdmi->vpll_clk,
		     crtc->state->adjusted_mode.crtc_clock * 1000);

	clk_set_rate(hdmi->dclk, crtc->state->adjusted_mode.crtc_clock * 1000);
	clk_prepare_enable(hdmi->dclk);

	switch (hdmi->dev_type) {
	case RK3288_HDMI:
		lcdsel_grf_reg = RK3288_GRF_SOC_CON6;
		lcdsel_mask = RK3288_HDMI_LCDC_SEL;
		break;
	case RK3366_HDMI:
		lcdsel_grf_reg = RK3366_GRF_SOC_CON0;
		lcdsel_mask = RK3366_HDMI_LCDC_SEL;
		break;
	case RK3399_HDMI:
		lcdsel_grf_reg = RK3399_GRF_SOC_CON20;
		lcdsel_mask = RK3399_HDMI_LCDC_SEL;
		break;
	default:
		return;
	}

	mux = drm_of_encoder_active_endpoint_id(hdmi->dev->of_node, encoder);
	if (mux)
		val = HIWORD_UPDATE(lcdsel_mask, lcdsel_mask);
	else
		val = HIWORD_UPDATE(0, lcdsel_mask);

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		dev_err(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return;
	}

	regmap_write(hdmi->regmap, lcdsel_grf_reg, val);
	dev_dbg(hdmi->dev, "vop %s output to hdmi\n",
		(mux) ? "LIT" : "BIG");

	if (hdmi->dev_type == RK3288_HDMI) {
		struct rockchip_crtc_state *s =
				to_rockchip_crtc_state(crtc->state);
		u32 mode_mask = mux ? RK3288_HDMI_LCDC1_YUV420 :
					RK3288_HDMI_LCDC0_YUV420;

		if (s->output_mode == ROCKCHIP_OUT_MODE_YUV420)
			val = HIWORD_UPDATE(mode_mask, mode_mask);
		else
			val = HIWORD_UPDATE(0, mode_mask);

		regmap_write(hdmi->regmap, RK3288_GRF_SOC_CON16, val);
	}

	clk_disable_unprepare(hdmi->grf_clk);
}

static void
dw_hdmi_rockchip_select_output(struct drm_connector_state *conn_state,
			       struct drm_crtc_state *crtc_state,
			       struct rockchip_hdmi *hdmi,
			       unsigned int *color_format,
			       unsigned int *color_depth,
			       unsigned long *enc_out_encoding,
			       unsigned int *eotf)
{
	struct drm_display_info *info = &conn_state->connector->display_info;
	struct drm_display_mode *mode = &crtc_state->mode;
	struct hdr_static_metadata *hdr_metadata;
	u32 vic = drm_match_cea_mode(mode);
	unsigned long tmdsclock, pixclock = mode->crtc_clock;
	bool support_dc = false;
	int max_tmds_clock = info->max_tmds_clock;

	*color_format = DRM_HDMI_OUTPUT_DEFAULT_RGB;

	switch (hdmi->hdmi_output) {
	case DRM_HDMI_OUTPUT_YCBCR_HQ:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = DRM_HDMI_OUTPUT_YCBCR444;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = DRM_HDMI_OUTPUT_YCBCR422;
		else if (conn_state->connector->ycbcr_420_allowed &&
			 drm_mode_is_420(info, mode))
			*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		break;
	case DRM_HDMI_OUTPUT_YCBCR_LQ:
		if (conn_state->connector->ycbcr_420_allowed &&
		    drm_mode_is_420(info, mode))
			*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = DRM_HDMI_OUTPUT_YCBCR422;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = DRM_HDMI_OUTPUT_YCBCR444;
		break;
	case DRM_HDMI_OUTPUT_YCBCR420:
		if (conn_state->connector->ycbcr_420_allowed &&
		    drm_mode_is_420(info, mode))
			*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		break;
	case DRM_HDMI_OUTPUT_YCBCR422:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = DRM_HDMI_OUTPUT_YCBCR422;
		break;
	case DRM_HDMI_OUTPUT_YCBCR444:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = DRM_HDMI_OUTPUT_YCBCR444;
		break;
	case DRM_HDMI_OUTPUT_DEFAULT_RGB:
	default:
		break;
	}

	if (*color_format == DRM_HDMI_OUTPUT_DEFAULT_RGB &&
	    info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_30)
		support_dc = true;
	if (*color_format == DRM_HDMI_OUTPUT_YCBCR444 &&
	    info->edid_hdmi_dc_modes & (DRM_EDID_HDMI_DC_Y444 | DRM_EDID_HDMI_DC_30))
		support_dc = true;
	if (*color_format == DRM_HDMI_OUTPUT_YCBCR422)
		support_dc = true;
	if (*color_format == DRM_HDMI_OUTPUT_YCBCR420 &&
	    info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
		support_dc = true;

	if (hdmi->colordepth > 8 && support_dc)
		*color_depth = 10;
	else
		*color_depth = 8;

	*eotf = TRADITIONAL_GAMMA_SDR;
	if (conn_state->hdr_source_metadata_blob_ptr) {
		hdr_metadata = (struct hdr_static_metadata *)
			conn_state->hdr_source_metadata_blob_ptr->data;
		if (hdr_metadata->eotf > TRADITIONAL_GAMMA_HDR &&
		    hdr_metadata->eotf < FUTURE_EOTF)
			*eotf = hdr_metadata->eotf;
	}

	if ((*eotf > TRADITIONAL_GAMMA_HDR &&
	     info->hdmi.hdr_panel_metadata.eotf & BIT(*eotf)) ||
	    (hdmi->colorimetry == HDMI_EXTENDED_COLORIMETRY_BT2020 &&
	     info->hdmi.colorimetry & (BIT(6) | BIT(7))))
		*enc_out_encoding = V4L2_YCBCR_ENC_BT2020;
	else if ((vic == 6) || (vic == 7) || (vic == 21) || (vic == 22) ||
		 (vic == 2) || (vic == 3) || (vic == 17) || (vic == 18))
		*enc_out_encoding = V4L2_YCBCR_ENC_601;
	else
		*enc_out_encoding = V4L2_YCBCR_ENC_709;

	if (*enc_out_encoding == V4L2_YCBCR_ENC_BT2020) {
		/* BT2020 require color depth at lest 10bit */
		*color_depth = 10;
		/* We prefer use YCbCr422 to send 10bit */
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = DRM_HDMI_OUTPUT_YCBCR422;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		pixclock *= 2;
	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) ==
		DRM_MODE_FLAG_3D_FRAME_PACKING)
		pixclock *= 2;

	if (*color_format == DRM_HDMI_OUTPUT_YCBCR422 || *color_depth == 8)
		tmdsclock = pixclock;
	else
		tmdsclock = pixclock * (*color_depth) / 8;

	if (*color_format == DRM_HDMI_OUTPUT_YCBCR420)
		tmdsclock /= 2;

	/* XXX: max_tmds_clock of some sink is 0, we think it is 340MHz. */
	if (!max_tmds_clock)
		max_tmds_clock = 340000;

	switch (hdmi->dev_type) {
	case RK3368_HDMI:
		max_tmds_clock = min(max_tmds_clock, 340000);
		break;
	case RK3328_HDMI:
	case RK3228_HDMI:
		max_tmds_clock = min(max_tmds_clock, 371250);
		break;
	default:
		max_tmds_clock = min(max_tmds_clock, 594000);
		break;
	}

	if (tmdsclock > max_tmds_clock) {
		if (max_tmds_clock >= 594000) {
			*color_depth = 8;
		} else if (max_tmds_clock > 340000) {
			if (drm_mode_is_420(info, mode) || tmdsclock >= 594000)
				*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		} else {
			*color_depth = 8;
			if (drm_mode_is_420(info, mode) || tmdsclock >= 594000)
				*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		}
	}
}

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	unsigned int colordepth, colorformat, bus_width;

	dw_hdmi_rockchip_select_output(conn_state, crtc_state, hdmi,
				       &colorformat, &colordepth,
				       &hdmi->enc_out_encoding, &s->eotf);

	if (colorformat == DRM_HDMI_OUTPUT_YCBCR420) {
		s->output_mode = ROCKCHIP_OUT_MODE_YUV420;
		if (colordepth > 8)
			s->bus_format = MEDIA_BUS_FMT_UYYVYY10_0_5X30;
		else
			s->bus_format = MEDIA_BUS_FMT_UYYVYY8_0_5X24;
		bus_width = colordepth / 2;
	} else {
		s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
		if (colordepth > 8) {
			if (colorformat != DRM_HDMI_OUTPUT_DEFAULT_RGB &&
			    hdmi->dev_type != RK3288_HDMI)
				s->bus_format = MEDIA_BUS_FMT_YUV10_1X30;
			else
				s->bus_format = MEDIA_BUS_FMT_RGB101010_1X30;
		} else {
			if (colorformat != DRM_HDMI_OUTPUT_DEFAULT_RGB &&
			    hdmi->dev_type != RK3288_HDMI)
				s->bus_format = MEDIA_BUS_FMT_YUV8_1X24;
			else
				s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		}
		if (colorformat == DRM_HDMI_OUTPUT_YCBCR422)
			bus_width = 8;
		else
			bus_width = colordepth;
	}

	hdmi->phy_bus_width = bus_width;
	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, bus_width);

	s->output_type = DRM_MODE_CONNECTOR_HDMIA;
	s->tv_state = &conn_state->tv;

	hdmi->bus_format = s->bus_format;

	if (colorformat == DRM_HDMI_OUTPUT_YCBCR422) {
		if (colordepth == 12)
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY12_1X24;
		else if (colordepth == 10)
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY10_1X20;
		else
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY8_1X16;
	} else {
		hdmi->output_bus_format = s->bus_format;
	}

	if (hdmi->enc_out_encoding == V4L2_YCBCR_ENC_BT2020)
		s->color_space = V4L2_COLORSPACE_BT2020;
	else if (colorformat == DRM_HDMI_OUTPUT_DEFAULT_RGB)
		s->color_space = V4L2_COLORSPACE_DEFAULT;
	else if (hdmi->enc_out_encoding == V4L2_YCBCR_ENC_709)
		s->color_space = V4L2_COLORSPACE_REC709;
	else
		s->color_space = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static unsigned long
dw_hdmi_rockchip_get_input_bus_format(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->bus_format;
}

static unsigned long
dw_hdmi_rockchip_get_output_bus_format(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->output_bus_format;
}

static unsigned long
dw_hdmi_rockchip_get_enc_in_encoding(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->enc_out_encoding;
}

static unsigned long
dw_hdmi_rockchip_get_enc_out_encoding(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->enc_out_encoding;
}

static const struct drm_prop_enum_list color_depth_enum_list[] = {
	{ 0, "Automatic" }, /* Same as 24bit */
	{ 8, "24bit" },
	{ 10, "30bit" },
};

static const struct drm_prop_enum_list drm_hdmi_output_enum_list[] = {
	{ DRM_HDMI_OUTPUT_DEFAULT_RGB, "output_rgb" },
	{ DRM_HDMI_OUTPUT_YCBCR444, "output_ycbcr444" },
	{ DRM_HDMI_OUTPUT_YCBCR422, "output_ycbcr422" },
	{ DRM_HDMI_OUTPUT_YCBCR420, "output_ycbcr420" },
	{ DRM_HDMI_OUTPUT_YCBCR_HQ, "output_ycbcr_high_subsampling" },
	{ DRM_HDMI_OUTPUT_YCBCR_LQ, "output_ycbcr_low_subsampling" },
	{ DRM_HDMI_OUTPUT_INVALID, "invalid_output" },
};

static const struct drm_prop_enum_list colorimetry_enum_list[] = {
	{ HDMI_COLORIMETRY_NONE, "None" },
	{ HDMI_COLORIMETRY_EXTENDED + HDMI_EXTENDED_COLORIMETRY_BT2020,
	  "ITU_2020" },
};

static void
dw_hdmi_rockchip_attatch_properties(struct drm_connector *connector,
				    unsigned int color, int version,
				    void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_property *prop;

	switch (color) {
	case MEDIA_BUS_FMT_RGB101010_1X30:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_DEFAULT_RGB;
		hdmi->colordepth = 10;
		break;
	case MEDIA_BUS_FMT_YUV8_1X24:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_YCBCR444;
		hdmi->colordepth = 8;
		break;
	case MEDIA_BUS_FMT_YUV10_1X30:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_YCBCR444;
		hdmi->colordepth = 10;
		break;
	case MEDIA_BUS_FMT_UYVY10_1X20:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_YCBCR422;
		hdmi->colordepth = 10;
		break;
	case MEDIA_BUS_FMT_UYVY8_1X16:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_YCBCR422;
		hdmi->colordepth = 8;
		break;
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_YCBCR420;
		hdmi->colordepth = 8;
		break;
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_YCBCR420;
		hdmi->colordepth = 10;
		break;
	default:
		hdmi->hdmi_output = DRM_HDMI_OUTPUT_DEFAULT_RGB;
		hdmi->colordepth = 8;
	}

	/* RK3368 does not support deep color mode */
	if (!hdmi->color_depth_property && hdmi->dev_type != RK3368_HDMI) {
		prop = drm_property_create_enum(connector->dev, 0,
						"hdmi_output_depth",
						color_depth_enum_list,
						ARRAY_SIZE(color_depth_enum_list));
		if (prop) {
			hdmi->color_depth_property = prop;
			drm_object_attach_property(&connector->base, prop, 0);
		}
	}

	prop = drm_property_create_enum(connector->dev, 0, "hdmi_output_format",
					drm_hdmi_output_enum_list,
					ARRAY_SIZE(drm_hdmi_output_enum_list));
	if (prop) {
		hdmi->hdmi_output_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_enum(connector->dev, 0,
					"hdmi_output_colorimetry",
					colorimetry_enum_list,
					ARRAY_SIZE(colorimetry_enum_list));
	if (prop) {
		hdmi->colorimetry_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_range(connector->dev, 0,
					 "hdmi_color_depth_capacity",
					 0, 0xff);
	if (prop) {
		hdmi->colordepth_capacity = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = drm_property_create_range(connector->dev, 0,
					 "hdmi_output_mode_capacity",
					 0, 0xf);
	if (prop) {
		hdmi->outputmode_capacity = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = connector->dev->mode_config.hdr_source_metadata_property;
	if (version >= 0x211a)
		drm_object_attach_property(&connector->base, prop, 0);

	prop = connector->dev->mode_config.hdr_panel_metadata_property;
	drm_object_attach_property(&connector->base, prop, 0);
}

static void
dw_hdmi_rockchip_destroy_properties(struct drm_connector *connector,
				    void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	if (hdmi->color_depth_property) {
		drm_property_destroy(connector->dev,
				     hdmi->color_depth_property);
		hdmi->color_depth_property = NULL;
	}

	if (hdmi->hdmi_output_property) {
		drm_property_destroy(connector->dev,
				     hdmi->hdmi_output_property);
		hdmi->hdmi_output_property = NULL;
	}

	if (hdmi->colordepth_capacity) {
		drm_property_destroy(connector->dev,
				     hdmi->colordepth_capacity);
		hdmi->colordepth_capacity = NULL;
	}

	if (hdmi->outputmode_capacity) {
		drm_property_destroy(connector->dev,
				     hdmi->outputmode_capacity);
		hdmi->outputmode_capacity = NULL;
	}

	if (hdmi->colorimetry_property) {
		drm_property_destroy(connector->dev,
				     hdmi->colorimetry_property);
		hdmi->colordepth_capacity = NULL;
	}
}

static int
dw_hdmi_rockchip_set_property(struct drm_connector *connector,
			      struct drm_connector_state *state,
			      struct drm_property *property,
			      uint64_t val,
			      void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_mode_config *config = &connector->dev->mode_config;

	if (property == hdmi->color_depth_property) {
		hdmi->colordepth = val;
		return 0;
	} else if (property == hdmi->hdmi_output_property) {
		hdmi->hdmi_output = val;
		return 0;
	} else if (property == config->hdr_source_metadata_property) {
		return 0;
	} else if (property == hdmi->colorimetry_property) {
		hdmi->colorimetry = val;
		return 0;
	}

	DRM_ERROR("failed to set rockchip hdmi connector property\n");
	return -EINVAL;
}

static int
dw_hdmi_rockchip_get_property(struct drm_connector *connector,
			      const struct drm_connector_state *state,
			      struct drm_property *property,
			      uint64_t *val,
			      void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_display_info *info = &connector->display_info;
	struct drm_mode_config *config = &connector->dev->mode_config;
	const struct drm_connector_state *conn_state = connector->state;

	if (property == hdmi->color_depth_property) {
		*val = hdmi->colordepth;
		return 0;
	} else if (property == hdmi->hdmi_output_property) {
		*val = hdmi->hdmi_output;
		return 0;
	} else if (property == hdmi->colordepth_capacity) {
		*val = BIT(ROCKCHIP_HDMI_DEPTH_8);
		/* RK3368 only support 8bit */
		if (hdmi->dev_type == RK3368_HDMI)
			return 0;
		if (info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_30)
			*val |= BIT(ROCKCHIP_HDMI_DEPTH_10);
		if (info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_36)
			*val |= BIT(ROCKCHIP_HDMI_DEPTH_12);
		if (info->edid_hdmi_dc_modes & DRM_EDID_HDMI_DC_48)
			*val |= BIT(ROCKCHIP_HDMI_DEPTH_16);
		if (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
			*val |= BIT(ROCKCHIP_HDMI_DEPTH_420_10);
		if (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_36)
			*val |= BIT(ROCKCHIP_HDMI_DEPTH_420_12);
		if (info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_48)
			*val |= BIT(ROCKCHIP_HDMI_DEPTH_420_16);
		return 0;
	} else if (property == hdmi->outputmode_capacity) {
		*val = BIT(DRM_HDMI_OUTPUT_DEFAULT_RGB);
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*val |= BIT(DRM_HDMI_OUTPUT_YCBCR444);
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*val |= BIT(DRM_HDMI_OUTPUT_YCBCR422);
		if (connector->ycbcr_420_allowed &&
		    info->color_formats & DRM_COLOR_FORMAT_YCRCB420)
			*val |= BIT(DRM_HDMI_OUTPUT_YCBCR420);
		return 0;
	} else if (property == config->hdr_source_metadata_property) {
		*val = conn_state->blob_id;
		return 0;
	} else if (property == hdmi->colorimetry_property) {
		*val = hdmi->colorimetry;
		return 0;
	}

	DRM_ERROR("failed to get rockchip hdmi connector property\n");
	return -EINVAL;
}

static const struct dw_hdmi_property_ops dw_hdmi_rockchip_property_ops = {
	.attatch_properties	= dw_hdmi_rockchip_attatch_properties,
	.destroy_properties	= dw_hdmi_rockchip_destroy_properties,
	.set_property		= dw_hdmi_rockchip_set_property,
	.get_property		= dw_hdmi_rockchip_get_property,
};

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.enable     = dw_hdmi_rockchip_encoder_enable,
	.disable    = dw_hdmi_rockchip_encoder_disable,
	.atomic_check = dw_hdmi_rockchip_encoder_atomic_check,
};

static const struct dw_hdmi_phy_ops inno_dw_hdmi_phy_ops = {
	.init		= inno_dw_hdmi_phy_init,
	.disable	= inno_dw_hdmi_phy_disable,
	.read_hpd	= inno_dw_hdmi_phy_read_hpd,
};

static const struct dw_hdmi_plat_data rk3228_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.phy_ops    = &inno_dw_hdmi_phy_ops,
	.phy_name   = "inno_dw_hdmi_phy",
	.dev_type   = RK3228_HDMI,
};

static const struct dw_hdmi_plat_data rk3288_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3288_HDMI,
	.tmds_n_table = rockchip_werid_tmds_n_table,
};

static const struct dw_hdmi_plat_data rk3328_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.phy_ops    = &inno_dw_hdmi_phy_ops,
	.phy_name   = "inno_dw_hdmi_phy2",
	.dev_type   = RK3328_HDMI,
};

static const struct dw_hdmi_plat_data rk3366_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3366_HDMI,
};

static const struct dw_hdmi_plat_data rk3368_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3368_HDMI,
};

static const struct dw_hdmi_plat_data rk3399_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3399_HDMI,
};

static const struct of_device_id dw_hdmi_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3228-dw-hdmi",
	  .data = &rk3228_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rk3288_hdmi_drv_data
	},
	{
	  .compatible = "rockchip,rk3328-dw-hdmi",
	  .data = &rk3328_hdmi_drv_data
	},
	{
	 .compatible = "rockchip,rk3366-dw-hdmi",
	 .data = &rk3366_hdmi_drv_data
	},
	{
	 .compatible = "rockchip,rk3368-dw-hdmi",
	 .data = &rk3368_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3399-dw-hdmi",
	  .data = &rk3399_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_rockchip_dt_ids);

static int dw_hdmi_rockchip_bind(struct device *dev, struct device *master,
				 void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dw_hdmi_plat_data *plat_data;
	const struct of_device_id *match;
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct rockchip_hdmi *hdmi;
	struct resource *iores;
	int irq;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	match = of_match_node(dw_hdmi_rockchip_dt_ids, pdev->dev.of_node);
	plat_data = (struct dw_hdmi_plat_data *)match->data;
	hdmi->dev = &pdev->dev;
	hdmi->dev_type = plat_data->dev_type;
	encoder = &hdmi->encoder;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -ENXIO;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	ret = rockchip_hdmi_parse_dt(hdmi);
	if (ret) {
		dev_err(hdmi->dev, "Unable to parse OF data\n");
		return ret;
	}

	plat_data->phy_data = hdmi;
	plat_data->get_input_bus_format =
		dw_hdmi_rockchip_get_input_bus_format;
	plat_data->get_output_bus_format =
		dw_hdmi_rockchip_get_output_bus_format;
	plat_data->get_enc_in_encoding =
		dw_hdmi_rockchip_get_enc_in_encoding;
	plat_data->get_enc_out_encoding =
		dw_hdmi_rockchip_get_enc_out_encoding;
	plat_data->property_ops = &dw_hdmi_rockchip_property_ops;

	if (hdmi->dev_type == RK3328_HDMI || hdmi->dev_type == RK3228_HDMI) {
		hdmi->phy = devm_phy_get(dev, "hdmi_phy");
		if (IS_ERR(hdmi->phy)) {
			ret = PTR_ERR(hdmi->phy);
			dev_err(dev, "failed to get phy: %d\n", ret);
			return ret;
		}
		ret = inno_dw_hdmi_init(hdmi);
		if (ret < 0)
			return ret;
	}

	drm_encoder_helper_add(encoder, &dw_hdmi_rockchip_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &dw_hdmi_rockchip_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);

	ret = dw_hdmi_bind(dev, master, data, encoder, iores, irq, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (ret)
		drm_encoder_cleanup(encoder);

	return ret;
}

static void dw_hdmi_rockchip_unbind(struct device *dev, struct device *master,
				    void *data)
{
	return dw_hdmi_unbind(dev, master, data);
}

static const struct component_ops dw_hdmi_rockchip_ops = {
	.bind	= dw_hdmi_rockchip_bind,
	.unbind	= dw_hdmi_rockchip_unbind,
};

static int dw_hdmi_rockchip_probe(struct platform_device *pdev)
{
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	return component_add(&pdev->dev, &dw_hdmi_rockchip_ops);
}

static int dw_hdmi_rockchip_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_rockchip_ops);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused dw_hdmi_rockchip_suspend(struct device *dev)
{
	dw_hdmi_suspend(dev);
	pm_runtime_put_sync(dev);

	return 0;
}

static int __maybe_unused dw_hdmi_rockchip_resume(struct device *dev)
{
	pm_runtime_get_sync(dev);
	dw_hdmi_resume(dev);

	return  0;
}

static const struct dev_pm_ops dw_hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_hdmi_rockchip_suspend,
				dw_hdmi_rockchip_resume)
};

static struct platform_driver dw_hdmi_rockchip_pltfm_driver = {
	.probe  = dw_hdmi_rockchip_probe,
	.remove = dw_hdmi_rockchip_remove,
	.driver = {
		.name = "dwhdmi-rockchip",
		.of_match_table = dw_hdmi_rockchip_dt_ids,
		.pm = &dw_hdmi_pm_ops,
	},
};

module_platform_driver(dw_hdmi_rockchip_pltfm_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwhdmi-rockchip");
