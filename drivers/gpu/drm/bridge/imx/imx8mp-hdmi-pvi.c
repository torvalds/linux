// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2022 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define HTX_PVI_CTRL			0x0
#define  PVI_CTRL_OP_VSYNC_POL		BIT(18)
#define  PVI_CTRL_OP_HSYNC_POL		BIT(17)
#define  PVI_CTRL_OP_DE_POL		BIT(16)
#define  PVI_CTRL_INP_VSYNC_POL		BIT(14)
#define  PVI_CTRL_INP_HSYNC_POL		BIT(13)
#define  PVI_CTRL_INP_DE_POL		BIT(12)
#define  PVI_CTRL_MODE_MASK		GENMASK(2, 1)
#define  PVI_CTRL_MODE_LCDIF		2
#define  PVI_CTRL_EN			BIT(0)

struct imx8mp_hdmi_pvi {
	struct drm_bridge	bridge;
	struct device		*dev;
	struct drm_bridge	*next_bridge;
	void __iomem		*regs;
};

static inline struct imx8mp_hdmi_pvi *
to_imx8mp_hdmi_pvi(struct drm_bridge *bridge)
{
	return container_of(bridge, struct imx8mp_hdmi_pvi, bridge);
}

static int imx8mp_hdmi_pvi_bridge_attach(struct drm_bridge *bridge,
					 enum drm_bridge_attach_flags flags)
{
	struct imx8mp_hdmi_pvi *pvi = to_imx8mp_hdmi_pvi(bridge);

	return drm_bridge_attach(bridge->encoder, pvi->next_bridge,
				 bridge, flags);
}

static void imx8mp_hdmi_pvi_bridge_enable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state)
{
	struct imx8mp_hdmi_pvi *pvi = to_imx8mp_hdmi_pvi(bridge);
	struct drm_connector_state *conn_state;
	struct drm_bridge_state *bridge_state;
	const struct drm_display_mode *mode;
	struct drm_crtc_state *crtc_state;
	struct drm_connector *connector;
	u32 bus_flags = 0, val;

	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	conn_state = drm_atomic_get_new_connector_state(state, connector);
	crtc_state = drm_atomic_get_new_crtc_state(state, conn_state->crtc);

	if (WARN_ON(pm_runtime_resume_and_get(pvi->dev)))
		return;

	mode = &crtc_state->adjusted_mode;

	val = FIELD_PREP(PVI_CTRL_MODE_MASK, PVI_CTRL_MODE_LCDIF) | PVI_CTRL_EN;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		val |= PVI_CTRL_OP_VSYNC_POL | PVI_CTRL_INP_VSYNC_POL;

	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		val |= PVI_CTRL_OP_HSYNC_POL | PVI_CTRL_INP_HSYNC_POL;

	if (pvi->next_bridge->timings)
		bus_flags = pvi->next_bridge->timings->input_bus_flags;
	else if (bridge_state)
		bus_flags = bridge_state->input_bus_cfg.flags;

	if (bus_flags & DRM_BUS_FLAG_DE_HIGH)
		val |= PVI_CTRL_OP_DE_POL | PVI_CTRL_INP_DE_POL;

	writel(val, pvi->regs + HTX_PVI_CTRL);
}

static void imx8mp_hdmi_pvi_bridge_disable(struct drm_bridge *bridge,
					   struct drm_atomic_state *state)
{
	struct imx8mp_hdmi_pvi *pvi = to_imx8mp_hdmi_pvi(bridge);

	writel(0x0, pvi->regs + HTX_PVI_CTRL);

	pm_runtime_put(pvi->dev);
}

static u32 *
imx8mp_hdmi_pvi_bridge_get_input_bus_fmts(struct drm_bridge *bridge,
					  struct drm_bridge_state *bridge_state,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state,
					  u32 output_fmt,
					  unsigned int *num_input_fmts)
{
	struct imx8mp_hdmi_pvi *pvi = to_imx8mp_hdmi_pvi(bridge);
	struct drm_bridge *next_bridge = pvi->next_bridge;
	struct drm_bridge_state *next_state;

	if (!next_bridge->funcs->atomic_get_input_bus_fmts)
		return NULL;

	next_state = drm_atomic_get_new_bridge_state(crtc_state->state,
						     next_bridge);

	return next_bridge->funcs->atomic_get_input_bus_fmts(next_bridge,
							     next_state,
							     crtc_state,
							     conn_state,
							     output_fmt,
							     num_input_fmts);
}

static const struct drm_bridge_funcs imx_hdmi_pvi_bridge_funcs = {
	.attach		= imx8mp_hdmi_pvi_bridge_attach,
	.atomic_enable	= imx8mp_hdmi_pvi_bridge_enable,
	.atomic_disable	= imx8mp_hdmi_pvi_bridge_disable,
	.atomic_get_input_bus_fmts = imx8mp_hdmi_pvi_bridge_get_input_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int imx8mp_hdmi_pvi_probe(struct platform_device *pdev)
{
	struct device_node *remote;
	struct imx8mp_hdmi_pvi *pvi;

	pvi = devm_kzalloc(&pdev->dev, sizeof(*pvi), GFP_KERNEL);
	if (!pvi)
		return -ENOMEM;

	platform_set_drvdata(pdev, pvi);
	pvi->dev = &pdev->dev;

	pvi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pvi->regs))
		return PTR_ERR(pvi->regs);

	/* Get the next bridge in the pipeline. */
	remote = of_graph_get_remote_node(pdev->dev.of_node, 1, -1);
	if (!remote)
		return -EINVAL;

	pvi->next_bridge = of_drm_find_bridge(remote);
	of_node_put(remote);

	if (!pvi->next_bridge)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "could not find next bridge\n");

	pm_runtime_enable(&pdev->dev);

	/* Register the bridge. */
	pvi->bridge.funcs = &imx_hdmi_pvi_bridge_funcs;
	pvi->bridge.of_node = pdev->dev.of_node;
	pvi->bridge.timings = pvi->next_bridge->timings;

	drm_bridge_add(&pvi->bridge);

	return 0;
}

static void imx8mp_hdmi_pvi_remove(struct platform_device *pdev)
{
	struct imx8mp_hdmi_pvi *pvi = platform_get_drvdata(pdev);

	drm_bridge_remove(&pvi->bridge);

	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id imx8mp_hdmi_pvi_match[] = {
	{
		.compatible = "fsl,imx8mp-hdmi-pvi",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, imx8mp_hdmi_pvi_match);

static struct platform_driver imx8mp_hdmi_pvi_driver = {
	.probe	= imx8mp_hdmi_pvi_probe,
	.remove = imx8mp_hdmi_pvi_remove,
	.driver		= {
		.name = "imx-hdmi-pvi",
		.of_match_table	= imx8mp_hdmi_pvi_match,
	},
};
module_platform_driver(imx8mp_hdmi_pvi_driver);

MODULE_DESCRIPTION("i.MX8MP HDMI TX Parallel Video Interface bridge driver");
MODULE_LICENSE("GPL");
