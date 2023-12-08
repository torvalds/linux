// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 *
 * derived from imx-hdmi.c(renamed to bridge/dw_hdmi.c now)
 */

#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <video/imx-ipu-v3.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>

#include "imx-drm.h"

struct imx_hdmi;

struct imx_hdmi_encoder {
	struct drm_encoder encoder;
	struct imx_hdmi *hdmi;
};

struct imx_hdmi {
	struct device *dev;
	struct drm_bridge *bridge;
	struct dw_hdmi *hdmi;
	struct regmap *regmap;
};

static inline struct imx_hdmi *enc_to_imx_hdmi(struct drm_encoder *e)
{
	return container_of(e, struct imx_hdmi_encoder, encoder)->hdmi;
}

static const struct dw_hdmi_mpll_config imx_mpll_cfg[] = {
	{
		45250000, {
			{ 0x01e0, 0x0000 },
			{ 0x21e1, 0x0000 },
			{ 0x41e2, 0x0000 }
		},
	}, {
		92500000, {
			{ 0x0140, 0x0005 },
			{ 0x2141, 0x0005 },
			{ 0x4142, 0x0005 },
	},
	}, {
		148500000, {
			{ 0x00a0, 0x000a },
			{ 0x20a1, 0x000a },
			{ 0x40a2, 0x000a },
		},
	}, {
		216000000, {
			{ 0x00a0, 0x000a },
			{ 0x2001, 0x000f },
			{ 0x4002, 0x000f },
		},
	}, {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl imx_cur_ctr[] = {
	/*      pixelclk     bpp8    bpp10   bpp12 */
	{
		54000000, { 0x091c, 0x091c, 0x06dc },
	}, {
		58400000, { 0x091c, 0x06dc, 0x06dc },
	}, {
		72000000, { 0x06dc, 0x06dc, 0x091c },
	}, {
		74250000, { 0x06dc, 0x0b5c, 0x091c },
	}, {
		118800000, { 0x091c, 0x091c, 0x06dc },
	}, {
		216000000, { 0x06dc, 0x0b5c, 0x091c },
	}, {
		~0UL, { 0x0000, 0x0000, 0x0000 },
	},
};

/*
 * Resistance term 133Ohm Cfg
 * PREEMP config 0.00
 * TX/CK level 10
 */
static const struct dw_hdmi_phy_config imx_phy_config[] = {
	/*pixelclk   symbol   term   vlev */
	{ 216000000, 0x800d, 0x0005, 0x01ad},
	{ ~0UL,      0x0000, 0x0000, 0x0000}
};

static void dw_hdmi_imx_encoder_enable(struct drm_encoder *encoder)
{
	struct imx_hdmi *hdmi = enc_to_imx_hdmi(encoder);
	int mux = drm_of_encoder_active_port_id(hdmi->dev->of_node, encoder);

	regmap_update_bits(hdmi->regmap, IOMUXC_GPR3,
			   IMX6Q_GPR3_HDMI_MUX_CTL_MASK,
			   mux << IMX6Q_GPR3_HDMI_MUX_CTL_SHIFT);
}

static int dw_hdmi_imx_atomic_check(struct drm_encoder *encoder,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	struct imx_crtc_state *imx_crtc_state = to_imx_crtc_state(crtc_state);

	imx_crtc_state->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	imx_crtc_state->di_hsync_pin = 2;
	imx_crtc_state->di_vsync_pin = 3;

	return 0;
}

static const struct drm_encoder_helper_funcs dw_hdmi_imx_encoder_helper_funcs = {
	.enable     = dw_hdmi_imx_encoder_enable,
	.atomic_check = dw_hdmi_imx_atomic_check,
};

static enum drm_mode_status
imx6q_hdmi_mode_valid(struct dw_hdmi *hdmi, void *data,
		      const struct drm_display_info *info,
		      const struct drm_display_mode *mode)
{
	if (mode->clock < 13500)
		return MODE_CLOCK_LOW;
	/* FIXME: Hardware is capable of 266MHz, but setup data is missing. */
	if (mode->clock > 216000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static enum drm_mode_status
imx6dl_hdmi_mode_valid(struct dw_hdmi *hdmi, void *data,
		       const struct drm_display_info *info,
		       const struct drm_display_mode *mode)
{
	if (mode->clock < 13500)
		return MODE_CLOCK_LOW;
	/* FIXME: Hardware is capable of 270MHz, but setup data is missing. */
	if (mode->clock > 216000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct dw_hdmi_plat_data imx6q_hdmi_drv_data = {
	.mpll_cfg   = imx_mpll_cfg,
	.cur_ctr    = imx_cur_ctr,
	.phy_config = imx_phy_config,
	.mode_valid = imx6q_hdmi_mode_valid,
};

static struct dw_hdmi_plat_data imx6dl_hdmi_drv_data = {
	.mpll_cfg = imx_mpll_cfg,
	.cur_ctr  = imx_cur_ctr,
	.phy_config = imx_phy_config,
	.mode_valid = imx6dl_hdmi_mode_valid,
};

static const struct of_device_id dw_hdmi_imx_dt_ids[] = {
	{ .compatible = "fsl,imx6q-hdmi",
	  .data = &imx6q_hdmi_drv_data
	}, {
	  .compatible = "fsl,imx6dl-hdmi",
	  .data = &imx6dl_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_imx_dt_ids);

static int dw_hdmi_imx_bind(struct device *dev, struct device *master,
			    void *data)
{
	struct drm_device *drm = data;
	struct imx_hdmi_encoder *hdmi_encoder;
	struct drm_encoder *encoder;
	int ret;

	hdmi_encoder = drmm_simple_encoder_alloc(drm, struct imx_hdmi_encoder,
						 encoder, DRM_MODE_ENCODER_TMDS);
	if (IS_ERR(hdmi_encoder))
		return PTR_ERR(hdmi_encoder);

	hdmi_encoder->hdmi = dev_get_drvdata(dev);
	encoder = &hdmi_encoder->encoder;

	ret = imx_drm_encoder_parse_of(drm, encoder, dev->of_node);
	if (ret)
		return ret;

	drm_encoder_helper_add(encoder, &dw_hdmi_imx_encoder_helper_funcs);

	return drm_bridge_attach(encoder, hdmi_encoder->hdmi->bridge, NULL, 0);
}

static const struct component_ops dw_hdmi_imx_ops = {
	.bind	= dw_hdmi_imx_bind,
};

static int dw_hdmi_imx_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match = of_match_node(dw_hdmi_imx_dt_ids, np);
	struct imx_hdmi *hdmi;
	int ret;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	platform_set_drvdata(pdev, hdmi);
	hdmi->dev = &pdev->dev;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "gpr");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get gpr\n");
		return PTR_ERR(hdmi->regmap);
	}

	hdmi->hdmi = dw_hdmi_probe(pdev, match->data);
	if (IS_ERR(hdmi->hdmi))
		return PTR_ERR(hdmi->hdmi);

	hdmi->bridge = of_drm_find_bridge(np);
	if (!hdmi->bridge) {
		dev_err(hdmi->dev, "Unable to find bridge\n");
		dw_hdmi_remove(hdmi->hdmi);
		return -ENODEV;
	}

	ret = component_add(&pdev->dev, &dw_hdmi_imx_ops);
	if (ret)
		dw_hdmi_remove(hdmi->hdmi);

	return ret;
}

static int dw_hdmi_imx_remove(struct platform_device *pdev)
{
	struct imx_hdmi *hdmi = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &dw_hdmi_imx_ops);
	dw_hdmi_remove(hdmi->hdmi);

	return 0;
}

static struct platform_driver dw_hdmi_imx_platform_driver = {
	.probe  = dw_hdmi_imx_probe,
	.remove = dw_hdmi_imx_remove,
	.driver = {
		.name = "dwhdmi-imx",
		.of_match_table = dw_hdmi_imx_dt_ids,
	},
};

module_platform_driver(dw_hdmi_imx_platform_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_DESCRIPTION("IMX6 Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:dwhdmi-imx");
