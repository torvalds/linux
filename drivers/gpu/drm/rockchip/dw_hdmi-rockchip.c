// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014, Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/hw_bitfield.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include "rockchip_drm_drv.h"

#define RK3228_GRF_SOC_CON2		0x0408
#define RK3228_HDMI_SDAIN_MSK		BIT(14)
#define RK3228_HDMI_SCLIN_MSK		BIT(13)
#define RK3228_GRF_SOC_CON6		0x0418
#define RK3228_HDMI_HPD_VSEL		BIT(6)
#define RK3228_HDMI_SDA_VSEL		BIT(5)
#define RK3228_HDMI_SCL_VSEL		BIT(4)

#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)

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

/**
 * struct rockchip_hdmi_chip_data - splite the grf setting of kind of chips
 * @lcdsel_grf_reg: grf register offset of lcdc select
 * @lcdsel_big: reg value of selecting vop big for HDMI
 * @lcdsel_lit: reg value of selecting vop little for HDMI
 * @max_tmds_clock: maximum TMDS clock rate supported
 */
struct rockchip_hdmi_chip_data {
	int	lcdsel_grf_reg;
	u32	lcdsel_big;
	u32	lcdsel_lit;
	int	max_tmds_clock;
};

struct rockchip_hdmi {
	struct device *dev;
	struct regmap *regmap;
	struct rockchip_encoder encoder;
	const struct rockchip_hdmi_chip_data *chip_data;
	const struct dw_hdmi_plat_data *plat_data;
	struct clk *hdmiphy_clk;
	struct clk *ref_clk;
	struct clk *grf_clk;
	struct drm_bridge *bridge;
	struct dw_hdmi *hdmi;
	struct phy *phy;
};

static struct rockchip_hdmi *to_rockchip_hdmi(struct drm_encoder *encoder)
{
	struct rockchip_encoder *rkencoder = to_rockchip_encoder(encoder);

	return container_of(rkencoder, struct rockchip_hdmi, encoder);
}

static const struct dw_hdmi_mpll_config rockchip_mpll_cfg[] = {
	{
		30666000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40f3, 0x0000 },
		},
	}, {
		36800000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40a2, 0x0001 },
		},
	}, {
		46000000, {
			{ 0x00b3, 0x0000 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	}, {
		61333000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	}, {
		73600000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x4061, 0x0002 },
		},
	}, {
		92000000, {
			{ 0x0072, 0x0001 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	}, {
		122666000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	}, {
		147200000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4064, 0x0003 },
		},
	}, {
		184000000, {
			{ 0x0051, 0x0002 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	}, {
		226666000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	}, {
		272000000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	}, {
		340000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	}, {
		600000000, {
			{ 0x1a40, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	}, {
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
	}, {
		~0UL,      { 0x0000, 0x0000, 0x0000 },
	}
};

static const struct dw_hdmi_phy_config rockchip_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 74250000,  0x8009, 0x0004, 0x0272 },
	{ 165000000, 0x802b, 0x0004, 0x0209 },
	{ 297000000, 0x8039, 0x0005, 0x028d },
	{ 594000000, 0x8039, 0x0000, 0x019d },
	{ ~0UL,	     0x0000, 0x0000, 0x0000 },
};

static enum drm_mode_status
dw_hdmi_rockchip_mode_valid(struct dw_hdmi *dw_hdmi, void *data,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = data;
	int pclk = mode->clock * 1000;

	if (hdmi->chip_data->max_tmds_clock &&
	    mode->clock > hdmi->chip_data->max_tmds_clock)
		return MODE_CLOCK_HIGH;

	if (hdmi->ref_clk) {
		int rpclk = clk_round_rate(hdmi->ref_clk, pclk);

		if (rpclk < 0 || abs(rpclk - pclk) > pclk / 1000)
			return MODE_NOCLOCK;
	}

	if (hdmi->hdmiphy_clk) {
		int rpclk = clk_round_rate(hdmi->hdmiphy_clk, pclk);

		if (rpclk < 0 || abs(rpclk - pclk) > pclk / 1000)
			return MODE_NOCLOCK;
	}

	return MODE_OK;
}

static void
dw_hdmi_rockchip_encoder_atomic_mode_set(struct drm_encoder *encoder,
					 struct drm_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	struct drm_display_mode *adj_mode = &crtc_state->adjusted_mode;

	if (hdmi->phy && conn_state->hdmi.tmds_char_rate) {
		union phy_configure_opts opts = {};

		opts.hdmi.bpc = conn_state->hdmi.output_bpc;
		opts.hdmi.tmds_char_rate = conn_state->hdmi.tmds_char_rate;

		phy_configure(hdmi->phy, &opts);
	}

	clk_set_rate(hdmi->ref_clk, adj_mode->clock * 1000);
}

static void dw_hdmi_rockchip_encoder_enable(struct drm_encoder *encoder)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	u32 val;
	int ret;

	if (hdmi->chip_data->lcdsel_grf_reg < 0)
		return;

	ret = drm_of_encoder_active_endpoint_id(hdmi->dev->of_node, encoder);
	if (ret)
		val = hdmi->chip_data->lcdsel_lit;
	else
		val = hdmi->chip_data->lcdsel_big;

	ret = clk_prepare_enable(hdmi->grf_clk);
	if (ret < 0) {
		dev_err(hdmi->dev, "failed to enable grfclk %d\n", ret);
		return;
	}

	ret = regmap_write(hdmi->regmap, hdmi->chip_data->lcdsel_grf_reg, val);
	if (ret != 0)
		dev_err(hdmi->dev, "Could not write to GRF: %d\n", ret);

	clk_disable_unprepare(hdmi->grf_clk);
	dev_dbg(hdmi->dev, "vop %s output to hdmi\n", ret ? "LIT" : "BIG");
}

static u32 dw_hdmi_rockchip_get_bus_format(struct drm_encoder *encoder,
					   struct drm_connector_state *conn_state)
{
	struct drm_bridge *bridge __free(drm_bridge_put) = NULL;
	struct drm_bridge_state *bridge_state;

	bridge = drm_bridge_chain_get_first_bridge(encoder);
	if (!bridge)
		return 0;

	bridge_state = drm_atomic_get_bridge_state(conn_state->state, bridge);
	if (!bridge_state)
		return 0;

	if (bridge_state->input_bus_cfg.format != MEDIA_BUS_FMT_FIXED)
		return bridge_state->input_bus_cfg.format;

	return bridge_state->output_bus_cfg.format;
}

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);
	union phy_configure_opts opts = {};
	u32 bus_format;

	bus_format = dw_hdmi_rockchip_get_bus_format(encoder, conn_state);

	switch (bus_format) {
	case MEDIA_BUS_FMT_FIXED:
		bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		fallthrough;
	case MEDIA_BUS_FMT_RGB888_1X24:
	case MEDIA_BUS_FMT_RGB101010_1X30:
	case MEDIA_BUS_FMT_YUV8_1X24:
	case MEDIA_BUS_FMT_YUV10_1X30:
		s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
		break;
	case MEDIA_BUS_FMT_UYYVYY8_0_5X24:
	case MEDIA_BUS_FMT_UYYVYY10_0_5X30:
		s->output_mode = ROCKCHIP_OUT_MODE_YUV420;
		break;
	default:
		return -EINVAL;
	}

	s->output_type = DRM_MODE_CONNECTOR_HDMIA;
	s->bus_format = bus_format;

	if (!hdmi->phy || !conn_state->hdmi.tmds_char_rate)
		return 0;

	opts.hdmi.bpc = conn_state->hdmi.output_bpc;
	opts.hdmi.tmds_char_rate = conn_state->hdmi.tmds_char_rate;

	return phy_validate(hdmi->phy, PHY_MODE_HDMI, PHY_HDMI_MODE_TMDS, &opts);
}

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.atomic_mode_set = dw_hdmi_rockchip_encoder_atomic_mode_set,
	.enable = dw_hdmi_rockchip_encoder_enable,
	.atomic_check = dw_hdmi_rockchip_encoder_atomic_check,
};

static int dw_hdmi_rockchip_genphy_init(struct dw_hdmi *dw_hdmi, void *data,
					const struct drm_display_info *display,
					const struct drm_display_mode *mode)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_set_high_tmds_clock_ratio(dw_hdmi, display);

	return phy_power_on(hdmi->phy);
}

static void dw_hdmi_rockchip_genphy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	phy_power_off(hdmi->phy);
}

static void dw_hdmi_rk3228_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	regmap_write(hdmi->regmap, RK3228_GRF_SOC_CON6,
		     FIELD_PREP_WM16(RK3228_HDMI_HPD_VSEL, 1) |
		     FIELD_PREP_WM16(RK3228_HDMI_SDA_VSEL, 1) |
		     FIELD_PREP_WM16(RK3228_HDMI_SCL_VSEL, 1));

	regmap_write(hdmi->regmap, RK3228_GRF_SOC_CON2,
		     FIELD_PREP_WM16(RK3228_HDMI_SDAIN_MSK, 1) |
		     FIELD_PREP_WM16(RK3228_HDMI_SCLIN_MSK, 1));
}

static enum drm_connector_status
dw_hdmi_rk3328_read_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;
	enum drm_connector_status status;

	status = dw_hdmi_phy_read_hpd(dw_hdmi, data);

	if (status == connector_status_connected)
		regmap_write(hdmi->regmap, RK3328_GRF_SOC_CON4,
			     FIELD_PREP_WM16(RK3328_HDMI_SDA_5V, 1) |
			     FIELD_PREP_WM16(RK3328_HDMI_SCL_5V, 1));
	else
		regmap_write(hdmi->regmap, RK3328_GRF_SOC_CON4,
			     FIELD_PREP_WM16(RK3328_HDMI_SDA_5V, 0) |
			     FIELD_PREP_WM16(RK3328_HDMI_SCL_5V, 0));
	return status;
}

static void dw_hdmi_rk3328_setup_hpd(struct dw_hdmi *dw_hdmi, void *data)
{
	struct rockchip_hdmi *hdmi = (struct rockchip_hdmi *)data;

	dw_hdmi_phy_setup_hpd(dw_hdmi, data);

	/* Enable and map pins to 3V grf-controlled io-voltage */
	regmap_write(hdmi->regmap, RK3328_GRF_SOC_CON4,
		     FIELD_PREP_WM16(RK3328_HDMI_HPD_SARADC, 0) |
		     FIELD_PREP_WM16(RK3328_HDMI_CEC_5V, 0) |
		     FIELD_PREP_WM16(RK3328_HDMI_SDA_5V, 0) |
		     FIELD_PREP_WM16(RK3328_HDMI_SCL_5V, 0) |
		     FIELD_PREP_WM16(RK3328_HDMI_HPD_5V, 0));
	regmap_write(hdmi->regmap, RK3328_GRF_SOC_CON3,
		     FIELD_PREP_WM16(RK3328_HDMI_SDA5V_GRF, 0) |
		     FIELD_PREP_WM16(RK3328_HDMI_SCL5V_GRF, 0) |
		     FIELD_PREP_WM16(RK3328_HDMI_HPD5V_GRF, 0) |
		     FIELD_PREP_WM16(RK3328_HDMI_CEC5V_GRF, 0));
	regmap_write(hdmi->regmap, RK3328_GRF_SOC_CON2,
		     FIELD_PREP_WM16(RK3328_HDMI_SDAIN_MSK, 1) |
		     FIELD_PREP_WM16(RK3328_HDMI_SCLIN_MSK, 1) |
		     FIELD_PREP_WM16(RK3328_HDMI_HPD_IOE, 0));

	dw_hdmi_rk3328_read_hpd(dw_hdmi, data);
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
	.max_tmds_clock = 594000,
};

static const struct dw_hdmi_plat_data rk3228_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.phy_data = &rk3228_chip_data,
	.phy_ops = &rk3228_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
};

static struct rockchip_hdmi_chip_data rk3288_chip_data = {
	.lcdsel_grf_reg = RK3288_GRF_SOC_CON6,
	.lcdsel_big = FIELD_PREP_WM16_CONST(RK3288_HDMI_LCDC_SEL, 0),
	.lcdsel_lit = FIELD_PREP_WM16_CONST(RK3288_HDMI_LCDC_SEL, 1),
	.max_tmds_clock = 340000,
};

static const struct dw_hdmi_plat_data rk3288_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3288_chip_data,
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
	.max_tmds_clock = 594000,
};

static const struct dw_hdmi_plat_data rk3328_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.phy_data = &rk3328_chip_data,
	.phy_ops = &rk3328_hdmi_phy_ops,
	.phy_name = "inno_dw_hdmi_phy2",
	.phy_force_vendor = true,
	.use_drm_infoframe = true,
};

static struct rockchip_hdmi_chip_data rk3368_chip_data = {
	.lcdsel_grf_reg = -1,
};

static const struct dw_hdmi_plat_data rk3368_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3368_chip_data,
	.use_drm_infoframe = true,
};

static struct rockchip_hdmi_chip_data rk3399_chip_data = {
	.lcdsel_grf_reg = RK3399_GRF_SOC_CON20,
	.lcdsel_big = FIELD_PREP_WM16_CONST(RK3399_HDMI_LCDC_SEL, 0),
	.lcdsel_lit = FIELD_PREP_WM16_CONST(RK3399_HDMI_LCDC_SEL, 1),
	.max_tmds_clock = 594000,
};

static const struct dw_hdmi_plat_data rk3399_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3399_chip_data,
	.use_drm_infoframe = true,
};

static struct rockchip_hdmi_chip_data rk3568_chip_data = {
	.lcdsel_grf_reg = -1,
	.max_tmds_clock = 594000,
};

static const struct dw_hdmi_plat_data rk3568_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.phy_data = &rk3568_chip_data,
	.use_drm_infoframe = true,
	.output_port = 1,
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
	{ .compatible = "rockchip,rk3368-dw-hdmi",
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
	struct device_node *np = dev_of_node(dev);
	const struct dw_hdmi_plat_data *drv_data;
	struct dw_hdmi_plat_data *plat_data;
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct rockchip_hdmi *hdmi;
	int ret, index;

	if (!np)
		return -ENODEV;

	drv_data = of_device_get_match_data(dev);
	if (!drv_data)
		return -ENODEV;

	hdmi = drmm_kzalloc(drm, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	plat_data = drmm_kzalloc(drm, sizeof(*drv_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;
	memcpy(plat_data, drv_data, sizeof(*drv_data));

	hdmi->dev = dev;
	hdmi->plat_data = plat_data;
	hdmi->chip_data = plat_data->phy_data;
	plat_data->phy_data = hdmi;
	plat_data->priv_data = hdmi;
	encoder = &hdmi->encoder.encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, np);
	rockchip_drm_encoder_set_crtc_endpoint_id(&hdmi->encoder,
						  np, 0, 0);

	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "failed to find possible crtcs\n");

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap))
		return dev_err_probe(dev, PTR_ERR(hdmi->regmap),
				     "failed to get rockchip,grf\n");

	hdmi->ref_clk = devm_clk_get_optional_enabled(dev, "ref");
	if (!hdmi->ref_clk)
		hdmi->ref_clk = devm_clk_get_optional_enabled(dev, "vpll");

	if (IS_ERR(hdmi->ref_clk))
		return dev_err_probe(dev, PTR_ERR(hdmi->ref_clk),
				     "failed to get reference clock\n");

	hdmi->grf_clk = devm_clk_get_optional(dev, "grf");
	if (IS_ERR(hdmi->grf_clk))
		return dev_err_probe(dev, PTR_ERR(hdmi->grf_clk),
				     "failed to get grf clock\n");

	ret = devm_regulator_get_enable(dev, "avdd-0v9");
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable avdd-0v9\n");

	ret = devm_regulator_get_enable(dev, "avdd-1v8");
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable avdd-1v8\n");

	hdmi->phy = devm_phy_optional_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy))
		return dev_err_probe(dev, PTR_ERR(hdmi->phy),
				     "failed to get phy\n");

	index = of_property_match_string(np, "phy-names", "hdmi");
	if (index >= 0) {
		struct of_phandle_args clkspec;

		if (!of_parse_phandle_with_args(np, "phys", "#phy-cells", index,
						&clkspec)) {
			hdmi->hdmiphy_clk = of_clk_get_from_provider(&clkspec);
			of_node_put(clkspec.np);
			if (IS_ERR(hdmi->hdmiphy_clk))
				hdmi->hdmiphy_clk = NULL;
		}
	}

	if (hdmi->chip_data == &rk3568_chip_data) {
		regmap_write(hdmi->regmap, RK3568_GRF_VO_CON1,
			     FIELD_PREP_WM16(RK3568_HDMI_SDAIN_MSK, 1) |
			     FIELD_PREP_WM16(RK3568_HDMI_SCLIN_MSK, 1));
	}

	ret = drmm_encoder_init(drm, encoder, NULL, DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init encoder\n");

	drm_encoder_helper_add(encoder, &dw_hdmi_rockchip_encoder_helper_funcs);

	platform_set_drvdata(pdev, hdmi);

	hdmi->hdmi = dw_hdmi_bind(pdev, encoder, plat_data);
	if (IS_ERR(hdmi->hdmi))
		return dev_err_probe(dev, PTR_ERR(hdmi->hdmi),
				     "failed to probe dw-hdmi bridge\n");

	hdmi->bridge = of_drm_find_and_get_bridge(np);
	if (!hdmi->bridge) {
		dw_hdmi_unbind(hdmi->hdmi);
		return dev_err_probe(dev, -ENODEV,
				     "failed to find dw-hdmi bridge\n");
	}

	return 0;
}

static void dw_hdmi_rockchip_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	drm_bridge_put(hdmi->bridge);
	dw_hdmi_unbind(hdmi->hdmi);
}

static const struct component_ops dw_hdmi_rockchip_ops = {
	.bind	= dw_hdmi_rockchip_bind,
	.unbind	= dw_hdmi_rockchip_unbind,
};

static int dw_hdmi_rockchip_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &dw_hdmi_rockchip_ops);
}

static void dw_hdmi_rockchip_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &dw_hdmi_rockchip_ops);
}

static int __maybe_unused dw_hdmi_rockchip_resume_early(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	if (hdmi)
		dw_hdmi_resume(hdmi->hdmi);

	return 0;
}

static const struct dev_pm_ops dw_hdmi_rockchip_pm = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(NULL, dw_hdmi_rockchip_resume_early)
};

struct platform_driver dw_hdmi_rockchip_pltfm_driver = {
	.probe  = dw_hdmi_rockchip_probe,
	.remove = dw_hdmi_rockchip_remove,
	.driver = {
		.name = "dwhdmi-rockchip",
		.pm = &dw_hdmi_rockchip_pm,
		.of_match_table = dw_hdmi_rockchip_dt_ids,
	},
};
