// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014, Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/hw_bitfield.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_edid.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

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
	{ 74250000,  0x8009, 0x0004, 0x0272},
	{ 165000000, 0x802b, 0x0004, 0x0209},
	{ 297000000, 0x8039, 0x0005, 0x028d},
	{ 594000000, 0x8039, 0x0000, 0x019d},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static int rockchip_hdmi_parse_dt(struct rockchip_hdmi *hdmi)
{
	struct device_node *np = hdmi->dev->of_node;
	int ret;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get rockchip,grf\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->ref_clk = devm_clk_get_optional_enabled(hdmi->dev, "ref");
	if (!hdmi->ref_clk)
		hdmi->ref_clk = devm_clk_get_optional_enabled(hdmi->dev, "vpll");

	if (IS_ERR(hdmi->ref_clk)) {
		ret = PTR_ERR(hdmi->ref_clk);
		return dev_err_probe(hdmi->dev, ret, "failed to get reference clock\n");
	}

	hdmi->grf_clk = devm_clk_get_optional(hdmi->dev, "grf");
	if (IS_ERR(hdmi->grf_clk)) {
		ret = PTR_ERR(hdmi->grf_clk);
		return dev_err_probe(hdmi->dev, ret, "failed to get grf clock\n");
	}

	ret = devm_regulator_get_enable(hdmi->dev, "avdd-0v9");
	if (ret)
		return ret;

	ret = devm_regulator_get_enable(hdmi->dev, "avdd-1v8");

	return ret;
}

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

static void dw_hdmi_rockchip_encoder_disable(struct drm_encoder *encoder)
{
}

static bool
dw_hdmi_rockchip_encoder_mode_fixup(struct drm_encoder *encoder,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adj_mode)
{
	return true;
}

static void dw_hdmi_rockchip_encoder_mode_set(struct drm_encoder *encoder,
					      struct drm_display_mode *mode,
					      struct drm_display_mode *adj_mode)
{
	struct rockchip_hdmi *hdmi = to_rockchip_hdmi(encoder);

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

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	return 0;
}

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.mode_fixup = dw_hdmi_rockchip_encoder_mode_fixup,
	.mode_set   = dw_hdmi_rockchip_encoder_mode_set,
	.enable     = dw_hdmi_rockchip_encoder_enable,
	.disable    = dw_hdmi_rockchip_encoder_disable,
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
	int ret;

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

	hdmi->dev = &pdev->dev;
	hdmi->plat_data = plat_data;
	hdmi->chip_data = plat_data->phy_data;
	plat_data->phy_data = hdmi;
	plat_data->priv_data = hdmi;
	encoder = &hdmi->encoder.encoder;

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	rockchip_drm_encoder_set_crtc_endpoint_id(&hdmi->encoder,
						  dev->of_node, 0, 0);

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
		return dev_err_probe(hdmi->dev, ret, "Unable to parse OF data\n");
	}

	hdmi->phy = devm_phy_optional_get(dev, "hdmi");
	if (IS_ERR(hdmi->phy)) {
		ret = PTR_ERR(hdmi->phy);
		return dev_err_probe(hdmi->dev, ret, "failed to get phy\n");
	}

	if (hdmi->phy) {
		struct of_phandle_args clkspec;

		clkspec.np = hdmi->phy->dev.of_node;
		hdmi->hdmiphy_clk = of_clk_get_from_provider(&clkspec);
		if (IS_ERR(hdmi->hdmiphy_clk))
			hdmi->hdmiphy_clk = NULL;
	}

	if (hdmi->chip_data == &rk3568_chip_data) {
		regmap_write(hdmi->regmap, RK3568_GRF_VO_CON1,
			     FIELD_PREP_WM16(RK3568_HDMI_SDAIN_MSK, 1) |
			     FIELD_PREP_WM16(RK3568_HDMI_SCLIN_MSK, 1));
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
		goto err_bind;
	}

	return 0;

err_bind:
	drm_encoder_cleanup(encoder);

	return ret;
}

static void dw_hdmi_rockchip_unbind(struct device *dev, struct device *master,
				    void *data)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_unbind(hdmi->hdmi);
	drm_encoder_cleanup(&hdmi->encoder.encoder);
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

static int __maybe_unused dw_hdmi_rockchip_resume(struct device *dev)
{
	struct rockchip_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_resume(hdmi->hdmi);

	return 0;
}

static const struct dev_pm_ops dw_hdmi_rockchip_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, dw_hdmi_rockchip_resume)
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
