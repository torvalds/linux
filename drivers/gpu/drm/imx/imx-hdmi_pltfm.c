/* Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 *
 * derived from imx-hdmi.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <video/imx-ipu-v3.h>
#include <linux/regmap.h>
#include <drm/drm_of.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>

#include "imx-drm.h"
#include "imx-hdmi.h"

struct imx_hdmi_priv {
	struct device *dev;
	struct drm_encoder encoder;
	struct regmap *regmap;
};

static const struct mpll_config imx_mpll_cfg[] = {
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
		~0UL, {
			{ 0x00a0, 0x000a },
			{ 0x2001, 0x000f },
			{ 0x4002, 0x000f },
		},
	}
};

static const struct curr_ctrl imx_cur_ctr[] = {
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
	}
};

static const struct sym_term imx_sym_term[] = {
	/*pixelclk   symbol   term*/
	{ 148500000, 0x800d, 0x0005 },
	{ ~0UL,      0x0000, 0x0000 }
};

static int imx_hdmi_parse_dt(struct imx_hdmi_priv *hdmi)
{
	struct device_node *np = hdmi->dev->of_node;

	hdmi->regmap = syscon_regmap_lookup_by_phandle(np, "gpr");
	if (IS_ERR(hdmi->regmap)) {
		dev_err(hdmi->dev, "Unable to get gpr\n");
		return PTR_ERR(hdmi->regmap);
	}

	return 0;
}

static void imx_hdmi_encoder_disable(struct drm_encoder *encoder)
{
}

static bool imx_hdmi_encoder_mode_fixup(struct drm_encoder *encoder,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adj_mode)
{
	return true;
}

static void imx_hdmi_encoder_mode_set(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adj_mode)
{
}

static void imx_hdmi_encoder_commit(struct drm_encoder *encoder)
{
	struct imx_hdmi_priv *hdmi = container_of(encoder,
						  struct imx_hdmi_priv,
						  encoder);
	int mux = imx_drm_encoder_get_mux_id(hdmi->dev->of_node, encoder);

	regmap_update_bits(hdmi->regmap, IOMUXC_GPR3,
			   IMX6Q_GPR3_HDMI_MUX_CTL_MASK,
			   mux << IMX6Q_GPR3_HDMI_MUX_CTL_SHIFT);
}

static void imx_hdmi_encoder_prepare(struct drm_encoder *encoder)
{
	imx_drm_panel_format(encoder, V4L2_PIX_FMT_RGB24);
}

static struct drm_encoder_helper_funcs imx_hdmi_encoder_helper_funcs = {
	.mode_fixup = imx_hdmi_encoder_mode_fixup,
	.mode_set   = imx_hdmi_encoder_mode_set,
	.prepare    = imx_hdmi_encoder_prepare,
	.commit     = imx_hdmi_encoder_commit,
	.disable    = imx_hdmi_encoder_disable,
};

static struct drm_encoder_funcs imx_hdmi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static struct imx_hdmi_plat_data imx6q_hdmi_drv_data = {
	.mpll_cfg = imx_mpll_cfg,
	.cur_ctr  = imx_cur_ctr,
	.sym_term = imx_sym_term,
	.dev_type = IMX6Q_HDMI,
};

static struct imx_hdmi_plat_data imx6dl_hdmi_drv_data = {
	.mpll_cfg = imx_mpll_cfg,
	.cur_ctr  = imx_cur_ctr,
	.sym_term = imx_sym_term,
	.dev_type = IMX6DL_HDMI,
};

static const struct of_device_id imx_hdmi_dt_ids[] = {
	{ .compatible = "fsl,imx6q-hdmi",
	  .data = &imx6q_hdmi_drv_data
	}, {
	  .compatible = "fsl,imx6dl-hdmi",
	  .data = &imx6dl_hdmi_drv_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, imx_hdmi_dt_ids);

static int imx_hdmi_pltfm_bind(struct device *dev, struct device *master,
			       void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	const struct imx_hdmi_plat_data *plat_data;
	const struct of_device_id *match;
	struct drm_device *drm = data;
	struct drm_encoder *encoder;
	struct imx_hdmi_priv *hdmi;
	struct resource *iores;
	int irq;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hdmi = devm_kzalloc(&pdev->dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	match = of_match_node(imx_hdmi_dt_ids, pdev->dev.of_node);
	plat_data = match->data;
	hdmi->dev = &pdev->dev;
	encoder = &hdmi->encoder;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -ENXIO;

	platform_set_drvdata(pdev, hdmi);

	encoder->possible_crtcs = drm_of_find_possible_crtcs(drm, dev->of_node);
	/*
	 * If we failed to find the CRTC(s) which this encoder is
	 * supposed to be connected to, it's because the CRTC has
	 * not been registered yet.  Defer probing, and hope that
	 * the required CRTC is added later.
	 */
	if (encoder->possible_crtcs == 0)
		return -EPROBE_DEFER;

	ret = imx_hdmi_parse_dt(hdmi);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &imx_hdmi_encoder_helper_funcs);
	drm_encoder_init(drm, encoder, &imx_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS);

	return imx_hdmi_bind(dev, master, data, encoder, iores, irq, plat_data);
}

static void imx_hdmi_pltfm_unbind(struct device *dev, struct device *master,
				  void *data)
{
	return imx_hdmi_unbind(dev, master, data);
}

static const struct component_ops imx_hdmi_ops = {
	.bind	= imx_hdmi_pltfm_bind,
	.unbind	= imx_hdmi_pltfm_unbind,
};

static int imx_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &imx_hdmi_ops);
}

static int imx_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &imx_hdmi_ops);

	return 0;
}

static struct platform_driver imx_hdmi_pltfm_driver = {
	.probe  = imx_hdmi_probe,
	.remove = imx_hdmi_remove,
	.driver = {
		.name = "hdmi-imx",
		.of_match_table = imx_hdmi_dt_ids,
	},
};

module_platform_driver(imx_hdmi_pltfm_driver);

MODULE_AUTHOR("Andy Yan <andy.yan@rock-chips.com>");
MODULE_AUTHOR("Yakir Yang <ykk@rock-chips.com>");
MODULE_DESCRIPTION("IMX6 Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hdmi-imx");
