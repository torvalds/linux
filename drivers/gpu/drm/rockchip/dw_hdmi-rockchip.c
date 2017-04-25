/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>

#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>
#include <drm/bridge/dw_hdmi.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_vop.h"

#define RK3288_GRF_SOC_CON6		0x025C
#define RK3288_HDMI_LCDC_SEL		BIT(4)
#define RK3399_GRF_SOC_CON20		0x6250
#define RK3399_HDMI_LCDC_SEL		BIT(6)

#define HIWORD_UPDATE(val, mask)	(val | (mask) << 16)

struct rockchip_hdmi {
	struct device *dev;
	struct regmap *regmap;
	struct drm_encoder encoder;
	enum dw_hdmi_devtype dev_type;
	struct clk *vpll_clk;
	struct clk *grf_clk;
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
		rockchip_phy_config[i].term = (u16)config[i * 4 + 1];
		rockchip_phy_config[i].sym_ctr = (u16)config[i * 4 + 2];
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

	ret = clk_prepare_enable(hdmi->vpll_clk);
	if (ret) {
		dev_err(hdmi->dev, "Failed to enable HDMI vpll: %d\n", ret);
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

	/*
	 * Pixel clocks we support are always < 2GHz and so fit in an
	 * int.  We should make sure source rate does too so we don't get
	 * overflow when we multiply by 1000.
	 */
	if (mode->clock > INT_MAX / 1000)
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

static const struct drm_encoder_funcs dw_hdmi_rockchip_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static void dw_hdmi_rockchip_encoder_disable(struct drm_encoder *encoder)
{
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

	clk_set_rate(hdmi->vpll_clk,
		     crtc->state->adjusted_mode.crtc_clock * 1000);

	switch (hdmi->dev_type) {
	case RK3288_HDMI:
		lcdsel_grf_reg = RK3288_GRF_SOC_CON6;
		lcdsel_mask = RK3288_HDMI_LCDC_SEL;
		break;
	case RK3399_HDMI:
		lcdsel_grf_reg = RK3399_GRF_SOC_CON20;
		lcdsel_mask = RK3399_HDMI_LCDC_SEL;
		break;
	default:
		return;
	};

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

	clk_disable_unprepare(hdmi->grf_clk);
}

static int
dw_hdmi_rockchip_encoder_atomic_check(struct drm_encoder *encoder,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state)
{
	struct rockchip_crtc_state *s = to_rockchip_crtc_state(crtc_state);

	if (crtc_state->mode.flags & DRM_MODE_FLAG_420_MASK) {
		s->output_mode = ROCKCHIP_OUT_MODE_YUV420;
		s->bus_format = MEDIA_BUS_FMT_YUV8_1X24;
	} else {
		s->output_mode = ROCKCHIP_OUT_MODE_AAAA;
		s->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	}
	s->output_type = DRM_MODE_CONNECTOR_HDMIA;

	return 0;
}

static const struct drm_encoder_helper_funcs dw_hdmi_rockchip_encoder_helper_funcs = {
	.enable     = dw_hdmi_rockchip_encoder_enable,
	.disable    = dw_hdmi_rockchip_encoder_disable,
	.atomic_check = dw_hdmi_rockchip_encoder_atomic_check,
};

static const struct dw_hdmi_plat_data rk3288_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3288_HDMI,
	.tmds_n_table = rockchip_werid_tmds_n_table,
};

static const struct dw_hdmi_plat_data rk3368_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3368_HDMI,
};

static const struct dw_hdmi_plat_data rk3399_hdmi_drv_data = {
	.mode_valid = dw_hdmi_rockchip_mode_valid,
	.mpll_cfg   = rockchip_mpll_cfg,
	.cur_ctr    = rockchip_cur_ctr,
	.phy_config = rockchip_phy_config,
	.dev_type   = RK3399_HDMI,
};

static const struct of_device_id dw_hdmi_rockchip_dt_ids[] = {
	{ .compatible = "rockchip,rk3288-dw-hdmi",
	  .data = &rk3288_hdmi_drv_data
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
	const struct dw_hdmi_plat_data *plat_data;
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
	plat_data = match->data;
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

static int dw_hdmi_rockchip_suspend(struct device *dev)
{
	dw_hdmi_suspend(dev);
	pm_runtime_put_sync(dev);

	return 0;
}

static int dw_hdmi_rockchip_resume(struct device *dev)
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
