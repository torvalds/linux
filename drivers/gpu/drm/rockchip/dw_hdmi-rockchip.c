// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include <uapi/linux/videodev2.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define RK3228_GRF_SOC_CON2		0x0408
#define RK3228_HDMI_SDAIN_MSK		BIT(14)
#define RK3228_HDMI_SCLIN_MSK		BIT(13)
#define RK3228_GRF_SOC_CON6		0x0418
#define RK3228_HDMI_HPD_VSEL		BIT(6)
#define RK3228_HDMI_SDA_VSEL		BIT(5)
#define RK3228_HDMI_SCL_VSEL		BIT(4)

#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)
#define RK3288_GRF_SOC_CON16		0x03a8
#define RK3288_HDMI_LCDC0_YUV420	BIT(2)
#define RK3288_HDMI_LCDC1_YUV420	BIT(3)

#define RK3328_GRF_SOC_CON2		0x0408
#define RK3328_HDMI_SDAIN_MSK		BIT(11)
#define RK3328_HDMI_SCLIN_MSK		BIT(10)
#define RK3328_HDMI_HPD_IOE		BIT(2)
#define RK3328_GRF_SOC_CON3		0x040c
/* need to be unset if hdmi or i2c should control voltage */
#define RK3328_HDMI_SDA5V_GRF		BIT(15)
#define RK3328_HDMI_SCL5V_GRF		BIT(14)
#define RK3328_HDMI_HPD5V_GRF		BIT(13)
#define RK3328_HDMI_CEC5V_GRF		BIT(12)
#define RK3328_GRF_SOC_CON4		0x0410
#define RK3328_HDMI_HPD_SARADC		BIT(13)
#define RK3328_HDMI_CEC_5V		BIT(11)
#define RK3328_HDMI_SDA_5V		BIT(10)
#define RK3328_HDMI_SCL_5V		BIT(9)
#define RK3328_HDMI_HPD_5V		BIT(8)

#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		BIT(6)

#define RK3568_GRF_VO_CON1		0x0364
#define RK3568_HDMI_SDAIN_MSK		BIT(15)
#define RK3568_HDMI_SCLIN_MSK		BIT(14)

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)
#define RK_HDMI_COLORIMETRY_BT2020	(HDMI_COLORIMETRY_EXTENDED + \
					 HDMI_EXTENDED_COLORIMETRY_BT2020)

/**
 * struct rockchip_hdmi_chip_data - splite the grf setting of kind of chips
 * @lcdsel_grf_reg: grf register offset of lcdc select
 * @ddc_en_reg: grf register offset of hdmi ddc enable
 * @lcdsel_big: reg value of selecting vop big for HDMI
 * @lcdsel_lit: reg value of selecting vop little for HDMI
 */
struct rockchip_hdmi_chip_data {
	int	lcdsel_grf_reg;
	int	ddc_en_reg;
	u32	lcdsel_big;
	u32	lcdsel_lit;
};

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
	struct drm_encoder encoder;
	const struct rockchip_hdmi_chip_data *chip_data;
	struct clk *phyref_clk;
	struct clk *grf_clk;
	struct clk *hclk_vio;
	struct clk *hclk_vop;
	struct dw_hdmi *hdmi;
	struct phy *phy;

	u32 max_tmdsclk;
	bool unsupported_yuv_input;
	bool unsupported_deep_color;
	bool mode_changed;
	u8 id;

	unsigned long bus_format;
	unsigned long output_bus_format;
	unsigned long enc_out_encoding;
	int color_changed;

	struct drm_property *color_depth_property;
	struct drm_property *hdmi_output_property;
	struct drm_property *colordepth_capacity;
	struct drm_property *outputmode_capacity;
	struct drm_property *colorimetry_property;
	struct drm_property *quant_range;
	struct drm_property *hdr_panel_metadata_property;

	struct drm_property_blob *hdr_panel_blob_ptr;

	unsigned int colordepth;
	unsigned int colorimetry;
	unsigned int hdmi_quant_range;
	unsigned int phy_bus_width;
	enum drm_hdmi_output_type hdmi_output;
	struct rockchip_drm_sub_dev sub_dev;
};

#define to_rockchip_hdmi(x)	container_of(x, struct rockchip_hdmi, x)

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

static const struct dw_hdmi_mpll_config rockchip_rk3288w_mpll_cfg_420[] = {
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
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
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
	{ ~0UL,	     0x0000, 0x0000, 0x0000},
	{ ~0UL,      0x0000, 0x0000, 0x0000},
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
	int ret, val, phy_table_size;
	u32 *phy_config;
	struct device_node *np = hdmi->dev->of_node;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->phyref_clk = devm_clk_get(hdmi->dev, "vpll");
	if (PTR_ERR(hdmi->phyref_clk) == -ENOENT)
		hdmi->phyref_clk = devm_clk_get(hdmi->dev, "ref");

	if (PTR_ERR(hdmi->phyref_clk) == -ENOENT) {
		hdmi->phyref_clk = NULL;
	} else if (PTR_ERR(hdmi->phyref_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->phyref_clk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get grf clock\n");
		return PTR_ERR(hdmi->phyref_clk);
	}

	hdmi->grf_clk = devm_clk_get(hdmi->dev, "grf");
	if (PTR_ERR(hdmi->grf_clk) == -ENOENT) {
		hdmi->grf_clk = NULL;
	} else if (PTR_ERR(hdmi->grf_clk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->grf_clk)) {
		DRM_DEV_ERROR(hdmi->dev, "failed to get grf clock\n");
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

	hdmi->hclk_vop = devm_clk_get(hdmi->dev, "hclk");
	if (PTR_ERR(hdmi->hclk_vop) == -ENOENT) {
		hdmi->hclk_vop = NULL;
	} else if (PTR_ERR(hdmi->hclk_vop) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(hdmi->hclk_vop)) {
		dev_err(hdmi->dev, "failed to get hclk_vop clock\n");
		return PTR_ERR(hdmi->hclk_vop);
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
dw_hdmi_rockchip_mode_valid(struct drm_connector *connector, void *data,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct drm_encoder *encoder = connector->encoder;
	enum drm_mode_status status = MODE_OK;
	struct drm_device *dev = connector->dev;
	struct rockchip_drm_private *priv = dev->dev_private;
	struct drm_crtc *crtc;

	/*
	 * Pixel clocks we support are always < 2GHz and so fit in an
	 * int.  We should make sure source rate does too so we don't get
	 * overflow when we multiply by 1000.
	 */
	if (mode->clock > INT_MAX / 1000)
		return MODE_BAD;
	/*
	 * If sink max TMDS clock < 340MHz, we should check the mode pixel
	 * clock > 340MHz is YCbCr420 or not and whether the platform supports
	 * YCbCr420.
	 */
	if (mode->clock > 340000 &&
	    connector->display_info.max_tmds_clock < 340000 &&
	    (!drm_mode_is_420(&connector->display_info, mode) ||
	     !connector->ycbcr_420_allowed))
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

static void dw_hdmi_rockchip_encoder_disable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc->state);

	if (!hdmi->mode_changed)
		s->output_if &= ~VOP_OUTPUT_IF_HDMI0;
	/*
	 * when plug out hdmi it will be switch cvbs and then phy bus width
	 * must be set as 8
	 */
	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, 8);
}

static void dw_hdmi_rockchip_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	u32 val;
	int mux;
	int ret;

	if (WARN_ON(!crtc || !crtc->state))
		return;

	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, hdmi->phy_bus_width);

	clk_set_rate(hdmi->phyref_clk,
		     crtc->state->adjusted_mode.crtc_clock * 1000);

	if (hdmi->chip_data->lcdsel_grf_reg < 0)
		return;

	mux = drm_of_encoder_active_endpoint_id(hdmi->dev->of_node, encoder);
	if (mux)
		val = hdmi->chip_data->lcdsel_lit;
	else
		val = hdmi->chip_data->lcdsel_big;

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		DRM_DEV_ERROR(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return;
	}

	ret = regmap_write(hdmi->regmap, hdmi->chip_data->lcdsel_grf_reg, val);
	if (ret != 0)
		DRM_DEV_ERROR(hdmi->dev, "Could not write to GRF: %d\n", ret);

	if (hdmi->chip_data->lcdsel_grf_reg == RK3288_GRF_SOC_CON6) {
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
	DRM_DEV_DEBUG(hdmi->dev, "vop %s output to hdmi\n",
		      ret ? "LIT" : "BIG");
}

static void
dw_hdmi_rockchip_select_output(struct drm_connector_state *conn_state,
			       struct drm_crtc_state *crtc_state,
			       struct rockchip_hdmi *hdmi,
			       unsigned int *color_format,
			       unsigned int *output_mode,
			       unsigned long *bus_format,
			       unsigned int *bus_width,
			       unsigned long *enc_out_encoding,
			       unsigned int *eotf)
{
	struct drm_display_info *info = &conn_state->connector->display_info;
	struct drm_display_mode *mode = &crtc_state->mode;
	struct hdr_output_metadata *hdr_metadata;
	u32 vic = drm_match_cea_mode(mode);
	unsigned long tmdsclock, pixclock = mode->crtc_clock;
	unsigned int color_depth;
	bool support_dc = false;
	u32 max_tmds_clock = info->max_tmds_clock;
	int output_eotf;

	*color_format = DRM_HDMI_OUTPUT_DEFAULT_RGB;

	switch (hdmi->hdmi_output) {
	case DRM_HDMI_OUTPUT_YCBCR_HQ:
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = DRM_HDMI_OUTPUT_YCBCR444;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = DRM_HDMI_OUTPUT_YCBCR422;
		else if (conn_state->connector->ycbcr_420_allowed &&
			 drm_mode_is_420(info, mode) && pixclock >= 594000)
			*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		break;
	case DRM_HDMI_OUTPUT_YCBCR_LQ:
		if (conn_state->connector->ycbcr_420_allowed &&
		    drm_mode_is_420(info, mode) && pixclock >= 594000)
			*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = DRM_HDMI_OUTPUT_YCBCR422;
		else if (info->color_formats & DRM_COLOR_FORMAT_YCRCB444)
			*color_format = DRM_HDMI_OUTPUT_YCBCR444;
		break;
	case DRM_HDMI_OUTPUT_YCBCR420:
		if (conn_state->connector->ycbcr_420_allowed &&
		    drm_mode_is_420(info, mode) && pixclock >= 594000)
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
	    info->edid_hdmi_dc_modes &
	    (DRM_EDID_HDMI_DC_Y444 | DRM_EDID_HDMI_DC_30))
		support_dc = true;
	if (*color_format == DRM_HDMI_OUTPUT_YCBCR422)
		support_dc = true;
	if (*color_format == DRM_HDMI_OUTPUT_YCBCR420 &&
	    info->hdmi.y420_dc_modes & DRM_EDID_YCBCR420_DC_30)
		support_dc = true;

	if (hdmi->colordepth > 8 && support_dc)
		color_depth = 10;
	else
		color_depth = 8;

	*eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
	if (conn_state->hdr_output_metadata) {
		hdr_metadata = (struct hdr_output_metadata *)
			conn_state->hdr_output_metadata->data;
		output_eotf = hdr_metadata->hdmi_metadata_type1.eotf;
		if (output_eotf > HDMI_EOTF_TRADITIONAL_GAMMA_SDR &&
		    output_eotf <= HDMI_EOTF_BT_2100_HLG)
			*eotf = output_eotf;
	}

	if ((*eotf > HDMI_EOTF_TRADITIONAL_GAMMA_SDR &&
	     conn_state->connector->hdr_sink_metadata.hdmi_type1.eotf &
	     BIT(*eotf)) || (hdmi->colorimetry ==
	     RK_HDMI_COLORIMETRY_BT2020))
		*enc_out_encoding = V4L2_YCBCR_ENC_BT2020;
	else if ((vic == 6) || (vic == 7) || (vic == 21) || (vic == 22) ||
		 (vic == 2) || (vic == 3) || (vic == 17) || (vic == 18))
		*enc_out_encoding = V4L2_YCBCR_ENC_601;
	else
		*enc_out_encoding = V4L2_YCBCR_ENC_709;

	if (*enc_out_encoding == V4L2_YCBCR_ENC_BT2020) {
		/* BT2020 require color depth at lest 10bit */
		color_depth = 10;
		/* We prefer use YCbCr422 to send 10bit */
		if (info->color_formats & DRM_COLOR_FORMAT_YCRCB422)
			*color_format = DRM_HDMI_OUTPUT_YCBCR422;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		pixclock *= 2;
	if ((mode->flags & DRM_MODE_FLAG_3D_MASK) ==
		DRM_MODE_FLAG_3D_FRAME_PACKING)
		pixclock *= 2;

	if (*color_format == DRM_HDMI_OUTPUT_YCBCR422 || color_depth == 8)
		tmdsclock = pixclock;
	else
		tmdsclock = pixclock * (color_depth) / 8;

	if (*color_format == DRM_HDMI_OUTPUT_YCBCR420)
		tmdsclock /= 2;

	/* XXX: max_tmds_clock of some sink is 0, we think it is 340MHz. */
	if (!max_tmds_clock)
		max_tmds_clock = 340000;

	max_tmds_clock = min(max_tmds_clock, hdmi->max_tmdsclk);

	if (tmdsclock > max_tmds_clock) {
		if (max_tmds_clock >= 594000) {
			color_depth = 8;
		} else if (max_tmds_clock > 340000) {
			if (drm_mode_is_420(info, mode) || tmdsclock >= 594000)
				*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		} else {
			color_depth = 8;
			if (drm_mode_is_420(info, mode) || tmdsclock >= 594000)
				*color_format = DRM_HDMI_OUTPUT_YCBCR420;
		}
	}

	if (*color_format == DRM_HDMI_OUTPUT_YCBCR420) {
		*output_mode = ROCKCHIP_OUT_MODE_YUV420;
		if (color_depth > 8)
			*bus_format = MEDIA_BUS_FMT_UYYVYY10_0_5X30;
		else
			*bus_format = MEDIA_BUS_FMT_UYYVYY8_0_5X24;
		*bus_width = color_depth / 2;
	} else {
		*output_mode = ROCKCHIP_OUT_MODE_AAAA;
		if (color_depth > 8) {
			if (*color_format != DRM_HDMI_OUTPUT_DEFAULT_RGB &&
			    !hdmi->unsupported_yuv_input)
				*bus_format = MEDIA_BUS_FMT_YUV10_1X30;
			else
				*bus_format = MEDIA_BUS_FMT_RGB101010_1X30;
		} else {
			if (*color_format != DRM_HDMI_OUTPUT_DEFAULT_RGB &&
			    !hdmi->unsupported_yuv_input)
				*bus_format = MEDIA_BUS_FMT_YUV8_1X24;
			else
				*bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		}
		if (*color_format == DRM_HDMI_OUTPUT_YCBCR422)
			*bus_width = 8;
		else
			*bus_width = color_depth;
	}

	hdmi->bus_format = *bus_format;

	if (*color_format == DRM_HDMI_OUTPUT_YCBCR422) {
		if (color_depth == 12)
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY12_1X24;
		else if (color_depth == 10)
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY10_1X20;
		else
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY8_1X16;
	} else {
		hdmi->output_bus_format = *bus_format;
	}
}

static bool
dw_hdmi_rockchip_check_color(struct drm_connector_state *conn_state,
			     struct rockchip_hdmi *hdmi)
{
	struct drm_crtc_state *crtc_state = conn_state->crtc->state;
	unsigned int colorformat;
	unsigned long bus_format;
	unsigned long output_bus_format = hdmi->output_bus_format;
	unsigned long enc_out_encoding = hdmi->enc_out_encoding;
	unsigned int eotf, bus_width;
	unsigned int output_mode;

	dw_hdmi_rockchip_select_output(conn_state, crtc_state, hdmi,
				       &colorformat,
				       &output_mode, &bus_format, &bus_width,
				       &hdmi->enc_out_encoding, &eotf);

	if (output_bus_format != hdmi->output_bus_format ||
	    enc_out_encoding != hdmi->enc_out_encoding)
		return true;
	else
		return false;
}

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	unsigned int colorformat, bus_width;
	unsigned int output_mode;
	unsigned long bus_format;

	dw_hdmi_rockchip_select_output(conn_state, crtc_state, hdmi,
				       &colorformat,
				       &output_mode, &bus_format, &bus_width,
				       &hdmi->enc_out_encoding, &s->eotf);

	hdmi->phy_bus_width = bus_width;
	if (hdmi->phy)
		phy_set_bus_width(hdmi->phy, bus_width);

	s->output_type = DRM_MODE_CONNECTOR_HDMIA;
	s->output_if = VOP_OUTPUT_IF_HDMI0;

	s->output_mode = output_mode;
	s->bus_format = bus_format;
	hdmi->bus_format = s->bus_format;

	hdmi->mode_changed = crtc_state->mode_changed;

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

static unsigned long
dw_hdmi_rockchip_get_quant_range(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->hdmi_quant_range;
}

static struct drm_property *
dw_hdmi_rockchip_get_hdr_property(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->hdr_panel_metadata_property;
}

static struct drm_property_blob *
dw_hdmi_rockchip_get_hdr_blob(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	return hdmi->hdr_panel_blob_ptr;
}

static bool
dw_hdmi_rockchip_get_color_changed(void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	bool ret = false;

	if (hdmi->color_changed)
		ret = true;
	hdmi->color_changed = 0;

	return ret;
}

static const struct drm_prop_enum_list color_depth_enum_list[] = {
	{ 0, "Automatic" }, /* Prefer highest color depth */
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

static const struct drm_prop_enum_list quant_range_enum_list[] = {
	{ HDMI_QUANTIZATION_RANGE_DEFAULT, "default" },
	{ HDMI_QUANTIZATION_RANGE_LIMITED, "limit" },
	{ HDMI_QUANTIZATION_RANGE_FULL, "full" },
};

static const struct drm_prop_enum_list colorimetry_enum_list[] = {
	{ HDMI_COLORIMETRY_NONE, "None" },
	{ RK_HDMI_COLORIMETRY_BT2020, "ITU_2020" },
};

static void
dw_hdmi_rockchip_attach_properties(struct drm_connector *connector,
				   unsigned int color, int version,
				   void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_property *prop;
	struct rockchip_drm_private *private = connector->dev->dev_private;

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

	hdmi->bus_format = color;

	if (hdmi->hdmi_output == DRM_HDMI_OUTPUT_YCBCR422) {
		if (hdmi->colordepth == 12)
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY12_1X24;
		else if (hdmi->colordepth == 10)
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY10_1X20;
		else
			hdmi->output_bus_format = MEDIA_BUS_FMT_UYVY8_1X16;
	} else {
		hdmi->output_bus_format = hdmi->bus_format;
	}

	/* RK3368 does not support deep color mode */
	if (!hdmi->color_depth_property && !hdmi->unsupported_deep_color) {
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

	prop = drm_property_create_enum(connector->dev, 0,
					"hdmi_quant_range",
					quant_range_enum_list,
					ARRAY_SIZE(quant_range_enum_list));
	if (prop) {
		hdmi->quant_range = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}


	prop = drm_property_create(connector->dev,
				   DRM_MODE_PROP_BLOB |
				   DRM_MODE_PROP_IMMUTABLE,
				   "HDR_PANEL_METADATA", 0);
	if (prop) {
		hdmi->hdr_panel_metadata_property = prop;
		drm_object_attach_property(&connector->base, prop, 0);
	}

	prop = connector->dev->mode_config.hdr_output_metadata_property;
	if (version >= 0x211a)
		drm_object_attach_property(&connector->base, prop, 0);
	drm_object_attach_property(&connector->base, private->connector_id_prop, 0);
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

	if (hdmi->quant_range) {
		drm_property_destroy(connector->dev,
				     hdmi->quant_range);
		hdmi->quant_range = NULL;
	}

	if (hdmi->colorimetry_property) {
		drm_property_destroy(connector->dev,
				     hdmi->colorimetry_property);
		hdmi->colordepth_capacity = NULL;
	}

	if (hdmi->hdr_panel_metadata_property) {
		drm_property_destroy(connector->dev,
				     hdmi->hdr_panel_metadata_property);
		hdmi->hdr_panel_metadata_property = NULL;
	}
}

static int
dw_hdmi_rockchip_set_property(struct drm_connector *connector,
			      struct drm_connector_state *state,
			      struct drm_property *property,
			      u64 val,
			      void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_mode_config *config = &connector->dev->mode_config;

	if (property == hdmi->color_depth_property) {
		hdmi->colordepth = val;
		/* If hdmi is disconnected, state->crtc is null */
		if (!state->crtc)
			return 0;
		if (dw_hdmi_rockchip_check_color(state, hdmi))
			hdmi->color_changed++;
		return 0;
	} else if (property == hdmi->hdmi_output_property) {
		hdmi->hdmi_output = val;
		if (!state->crtc)
			return 0;
		if (dw_hdmi_rockchip_check_color(state, hdmi))
			hdmi->color_changed++;
		return 0;
	} else if (property == hdmi->quant_range) {
		hdmi->hdmi_quant_range = val;
		dw_hdmi_set_quant_range(hdmi->hdmi);
		return 0;
	} else if (property == config->hdr_output_metadata_property) {
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
			      u64 *val,
			      void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	struct drm_display_info *info = &connector->display_info;
	struct drm_mode_config *config = &connector->dev->mode_config;
	struct rockchip_drm_private *private = connector->dev->dev_private;

	if (property == hdmi->color_depth_property) {
		*val = hdmi->colordepth;
		return 0;
	} else if (property == hdmi->hdmi_output_property) {
		*val = hdmi->hdmi_output;
		return 0;
	} else if (property == hdmi->colordepth_capacity) {
		*val = BIT(ROCKCHIP_HDMI_DEPTH_8);
		/* RK3368 only support 8bit */
		if (hdmi->unsupported_deep_color)
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
	} else if (property == hdmi->quant_range) {
		*val = hdmi->hdmi_quant_range;
		return 0;
	} else if (property == config->hdr_output_metadata_property) {
		*val = state->hdr_output_metadata ?
			state->hdr_output_metadata->base.id : 0;
		return 0;
	} else if (property == hdmi->colorimetry_property) {
		*val = hdmi->colorimetry;
		return 0;
	} else if (property == private->connector_id_prop) {
		*val = hdmi->id;
		return 0;
	}

	DRM_ERROR("failed to get rockchip hdmi connector property\n");
	return -EINVAL;
}

static const struct dw_hdmi_property_ops dw_hdmi_rockchip_property_ops = {
	.attach_properties	= dw_hdmi_rockchip_attach_properties,
	.destroy_properties	= dw_hdmi_rockchip_destroy_properties,
	.set_property		= dw_hdmi_rockchip_set_property,
	.get_property		= dw_hdmi_rockchip_get_property,
};

static void dw_hdmi_rockchip_encoder_mode_set(struct drm_encoder *encoder,
					      struct drm_display_mode *mode,
					      struct drm_display_mode *adj)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);

	clk_set_rate(hdmi->phyref_clk, adj->crtc_clock * 1000);
}

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.enable     = dw_hdmi_rockchip_encoder_enable,
	.disable    = dw_hdmi_rockchip_encoder_disable,
	.atomic_check = dw_hdmi_rockchip_encoder_atomic_check,
	.mode_set = dw_hdmi_rockchip_encoder_mode_set,
};

static void
dw_hdmi_rockchip_genphy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	while (hdmi->phy->power_count > 0)
		phy_power_off(hdmi->phy);
}

static int
dw_hdmi_rockchip_genphy_init(struct dw_hdmi *dw_hdmi, void *data,
			     const struct drm_display_info *display,
			     const struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_rockchip_genphy_disable(dw_hdmi, data);
	dw_hdmi_set_high_tmds_clock_ratio(dw_hdmi, display);
	return phy_power_on(hdmi->phy);
}

static void dw_hdmi_rk3228_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	regmap_write(hdmi->regmap,
		RK3228_GRF_SOC_CON6,
		HIWORD_UPDATE(RK3228_HDMI_HPD_VSEL | RK3228_HDMI_SDA_VSEL |
			      RK3228_HDMI_SCL_VSEL,
			      RK3228_HDMI_HPD_VSEL | RK3228_HDMI_SDA_VSEL |
			      RK3228_HDMI_SCL_VSEL));

	regmap_write(hdmi->regmap,
		RK3228_GRF_SOC_CON2,
		HIWORD_UPDATE(RK3228_HDMI_SDAIN_MSK | RK3228_HDMI_SCLIN_MSK,
			      RK3228_HDMI_SDAIN_MSK | RK3228_HDMI_SCLIN_MSK));
}

static enum drm_connector_status
dw_hdmi_rk3328_read_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	enum drm_connector_status status;

	status = dw_hdmi_phy_read_hpd(dw_hdmi, data);

	if (status == connector_status_connected)
		regmap_write(hdmi->regmap,
			RK3328_GRF_SOC_CON4,
			HIWORD_UPDATE(RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V,
				      RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V));
	else
		regmap_write(hdmi->regmap,
			RK3328_GRF_SOC_CON4,
			HIWORD_UPDATE(0, RK3328_HDMI_SDA_5V |
					 RK3328_HDMI_SCL_5V));
	return status;
}

static void dw_hdmi_rk3328_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	/* Enable and map pins to 3V grf-controlled io-voltage */
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON4,
		HIWORD_UPDATE(0, RK3328_HDMI_HPD_SARADC | RK3328_HDMI_CEC_5V |
				 RK3328_HDMI_SDA_5V | RK3328_HDMI_SCL_5V |
				 RK3328_HDMI_HPD_5V));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON3,
		HIWORD_UPDATE(0, RK3328_HDMI_SDA5V_GRF | RK3328_HDMI_SCL5V_GRF |
				 RK3328_HDMI_HPD5V_GRF |
				 RK3328_HDMI_CEC5V_GRF));
	regmap_write(hdmi->regmap,
		RK3328_GRF_SOC_CON2,
		HIWORD_UPDATE(RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK,
			      RK3328_HDMI_SDAIN_MSK | RK3328_HDMI_SCLIN_MSK |
			      RK3328_HDMI_HPD_IOE));
}

static const struct dw_hdmi_phy_ops rk3228_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_rk3228_setup_hpd,
};

static struct rockchip_hdmi_chip_data rk3228_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3228_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg = rockchip_mpll_cfg,
	.cur_ctr = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3228_chip_data,
	.phy_ops = &rk3228_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.max_tmdsclk = 371250,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3288_chip_data = {
	.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
	.lcdsel_big = HIWORD_UPDATE(0, RK3288_HDMI_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3288_HDMI_LCDC_SEL, RK3288_HDMI_LCDC_SEL),
};

static const struct dw_hdmi_plat_data rk3288_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_rk3288w_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3288_chip_data,
	.tmds_n_table = rockchip_werid_tmds_n_table,
	.unsupported_yuv_input = true,
	.ycbcr_420_allowed = true,
};

static const struct dw_hdmi_phy_ops rk3328_hdmi_phy_ops = {
	.init		= dw_hdmi_rockchip_genphy_init,
	.disable	= dw_hdmi_rockchip_genphy_disable,
	.read_hpd	= dw_hdmi_rk3328_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_rk3328_setup_hpd,
};

static struct rockchip_hdmi_chip_data rk3328_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3328_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg = rockchip_mpll_cfg,
	.cur_ctr = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3328_chip_data,
	.phy_ops = &rk3328_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.use_drm_infoframe = true,
	.max_tmdsclk = 371250,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3368_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3368_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3368_chip_data,
	.unsupported_deep_color = true,
	.max_tmdsclk = 340000,
	.ycbcr_420_allowed = true,
};

static struct rockchip_hdmi_chip_data rk3399_chip_data = {
	.lcdsel_grf_reg = RK3399_GRF_SOC_CON20,
	.lcdsel_big = HIWORD_UPDATE(0, RK3399_HDMI_LCDC_SEL),
	.lcdsel_lit = HIWORD_UPDATE(RK3399_HDMI_LCDC_SEL, RK3399_HDMI_LCDC_SEL),
};

static const struct dw_hdmi_plat_data rk3399_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3399_chip_data,
	.use_drm_infoframe = true,
};

static struct rockchip_hdmi_chip_data rk3568_chip_data = {
	.lcdsel_grf_reg = -1,
	.ddc_en_reg = RK3568_GRF_VO_CON1,
};

static const struct dw_hdmi_plat_data rk3568_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.mpll_cfg_420 = rockchip_mpll_cfg_420,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3568_chip_data,
	.ycbcr_420_allowed = true,
	.use_drm_infoframe = true,
};

static const struct of_device_id dw_hdmi_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3228-dw-hdmi",
	  .data = &rk3228_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rk3288_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3328-dw-hdmi",
	  .data = &rk3328_hdmi_drv_data
	},
	{
	 .compatible = "rockchip,rk3368-dw-hdmi",
	 .data = &rk3368_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3399-dw-hdmi",
	  .data = &rk3399_hdmi_drv_data
	},
	{ .compatible = "rockchip,rk3568-dw-hdmi",
	  .data = &rk3568_hdmi_drv_data
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
	int ret, id;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	match = of_match_node(dw_hdmi_rockchip_dt_ids, pdev->dev.of_node);
	plat_data = devm_kmemdup(&pdev->dev, match->data,
					     sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	id = of_alias_get_id(dev->of_node, "hdmi");
	if (id < 0)
		id = 0;
	hdmi->id = id;
	hdmi->dev = &pdev->dev;
	hdmi->chip_data = plat_data->phy_data;
	plat_data->phy_data = hdmi;
	plat_data->get_input_bus_format =
		dw_hdmi_rockchip_get_input_bus_format;
	plat_data->get_output_bus_format =
		dw_hdmi_rockchip_get_output_bus_format;
	plat_data->get_enc_in_encoding =
		dw_hdmi_rockchip_get_enc_in_encoding;
	plat_data->get_enc_out_encoding =
		dw_hdmi_rockchip_get_enc_out_encoding;
	plat_data->get_quant_range =
		dw_hdmi_rockchip_get_quant_range;
	plat_data->get_hdr_property =
		dw_hdmi_rockchip_get_hdr_property;
	plat_data->get_hdr_blob =
		dw_hdmi_rockchip_get_hdr_blob;
	plat_data->get_color_changed =
		dw_hdmi_rockchip_get_color_changed;
	plat_data->property_ops = &dw_hdmi_rockchip_property_ops;

	encoder = &hdmi->encoder;

	encoder->possible_crtcs = rockchip_drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	if (!plat_data->max_tmdsclk)
		hdmi->max_tmdsclk = 594000;
	else
		hdmi->max_tmdsclk = plat_data->max_tmdsclk;

	hdmi->unsupported_yuv_input = plat_data->unsupported_yuv_input;
	hdmi->unsupported_deep_color = plat_data->unsupported_deep_color;

	ret = rockchip_hdmi_parse_dt(hdmi);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "Unable to parse OF data\n");
		return ret;
	}

	if (hdmi->chip_data->ddc_en_reg == RK3568_GRF_VO_CON1) {
		regmap_write(hdmi->regmap, RK3568_GRF_VO_CON1,
			     HIWORD_UPDATE(RK3568_HDMI_SDAIN_MSK |
					   RK3568_HDMI_SCLIN_MSK,
					   RK3568_HDMI_SDAIN_MSK |
					   RK3568_HDMI_SCLIN_MSK));
	}

	ret = clk_prepare_enable(hdmi->phyref_clk);
	if (ret) {
		DRM_DEV_ERROR(hdmi->dev, "Failed to enable HDMI vpll: %d\n",
			      ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hclk_vio);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI hclk_vio: %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(hdmi->hclk_vop);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI hclk_vop: %d\n",
			ret);
		return ret;
	}

	hdmi->phy = devm_phy_optional_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy)) {
		hdmi->phy = devm_phy_optional_get(dev, "hdmi_phy");
		if (IS_ERR(hdmi->phy)) {
			ret = PTR_ERR(hdmi->phy);
			if (ret != -EPROBE_DEFER)
				DRM_DEV_ERROR(hdmi->dev, "failed to get phy\n");
			return ret;
		}
	}

	drm_encoder_helper_add(encoder, &dw_hdmi_rockchip_encoder_helper_funcs);
	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);

	platform_set_drvdata(pdev, hdmi);

	hdmi->hdmi = dw_hdmi_bind(pdev, encoder, plat_data);

	/*
	 * If dw_hdmi_bind() fails we'll never call dw_hdmi_unbind(),
	 * which would have called the encoder cleanup.  Do it manually.
	 */
	if (IS_ERR(hdmi->hdmi)) {
		ret = PTR_ERR(hdmi->hdmi);
		drm_encoder_cleanup(encoder);
		clk_disable_unprepare(hdmi->phyref_clk);
		clk_disable_unprepare(hdmi->hclk_vop);
	}

	hdmi->sub_dev.connector = plat_data->connector;
	hdmi->sub_dev.of_node = dev->of_node;
	rockchip_drm_register_sub_dev(&hdmi->sub_dev);

	return ret;
}

static void dw_hdmi_rockchip_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	rockchip_drm_unregister_sub_dev(&hdmi->sub_dev);
	dw_hdmi_unbind(hdmi->hdmi);
	clk_disable_unprepare(hdmi->phyref_clk);
	clk_disable_unprepare(hdmi->hclk_vop);
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

static void dw_hdmi_rockchip_shutdown(struct platform_device *pdev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(&pdev->dev);

	if (!hdmi)
		return;

	dw_hdmi_suspend(hdmi->hdmi);
	pm_runtime_put_sync(&pdev->dev);
}

static int dw_hdmi_rockchip_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_rockchip_ops);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int dw_hdmi_rockchip_suspend(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_suspend(hdmi->hdmi);
	pm_runtime_put_sync(dev);

	return 0;
}

static int dw_hdmi_rockchip_resume(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_resume(hdmi->hdmi);
	pm_runtime_get_sync(dev);

	return 0;
}

static const struct dev_pm_ops dw_hdmi_rockchip_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_hdmi_rockchip_suspend,
				dw_hdmi_rockchip_resume)
};

struct platform_driver dw_hdmi_rockchip_pltfm_driver = {
	.probe  = dw_hdmi_rockchip_probe,
	.remove = dw_hdmi_rockchip_remove,
	.shutdown = dw_hdmi_rockchip_shutdown,
	.driver = {
		.name = "dwhdmi-rockchip",
		.pm = &dw_hdmi_rockchip_pm,
		.of_match_table = dw_hdmi_rockchip_dt_ids,
	},
};
