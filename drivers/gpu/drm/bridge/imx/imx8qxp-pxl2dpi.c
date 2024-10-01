// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2020 NXP
 */

#include <linux/firmware/imx/svc/misc.h>
#include <linux/media-bus-format.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>

#include <dt-bindings/firmware/imx/rsrc.h>

#define PXL2DPI_CTRL	0x40
#define  CFG1_16BIT	0x0
#define  CFG2_16BIT	0x1
#define  CFG3_16BIT	0x2
#define  CFG1_18BIT	0x3
#define  CFG2_18BIT	0x4
#define  CFG_24BIT	0x5

#define DRIVER_NAME	"imx8qxp-pxl2dpi"

struct imx8qxp_pxl2dpi {
	struct regmap *regmap;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_bridge *companion;
	struct device *dev;
	struct imx_sc_ipc *ipc_handle;
	u32 sc_resource;
	u32 in_bus_format;
	u32 out_bus_format;
	u32 pl_sel;
};

#define bridge_to_p2d(b)	container_of(b, struct imx8qxp_pxl2dpi, bridge)

static int imx8qxp_pxl2dpi_bridge_attach(struct drm_bridge *bridge,
					 enum drm_bridge_attach_flags flags)
{
	struct imx8qxp_pxl2dpi *p2d = bridge->driver_private;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)) {
		DRM_DEV_ERROR(p2d->dev,
			      "do not support creating a drm_connector\n");
		return -EINVAL;
	}

	return drm_bridge_attach(bridge->encoder,
				 p2d->next_bridge, bridge,
				 DRM_BRIDGE_ATTACH_NO_CONNECTOR);
}

static int
imx8qxp_pxl2dpi_bridge_atomic_check(struct drm_bridge *bridge,
				    struct drm_bridge_state *bridge_state,
				    struct drm_crtc_state *crtc_state,
				    struct drm_connector_state *conn_state)
{
	struct imx8qxp_pxl2dpi *p2d = bridge->driver_private;

	p2d->in_bus_format = bridge_state->input_bus_cfg.format;
	p2d->out_bus_format = bridge_state->output_bus_cfg.format;

	return 0;
}

static void
imx8qxp_pxl2dpi_bridge_mode_set(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode)
{
	struct imx8qxp_pxl2dpi *p2d = bridge->driver_private;
	struct imx8qxp_pxl2dpi *companion_p2d;
	int ret;

	ret = pm_runtime_get_sync(p2d->dev);
	if (ret < 0)
		DRM_DEV_ERROR(p2d->dev,
			      "failed to get runtime PM sync: %d\n", ret);

	ret = imx_sc_misc_set_control(p2d->ipc_handle, p2d->sc_resource,
				      IMX_SC_C_PXL_LINK_SEL, p2d->pl_sel);
	if (ret)
		DRM_DEV_ERROR(p2d->dev,
			      "failed to set pixel link selection(%u): %d\n",
							p2d->pl_sel, ret);

	switch (p2d->out_bus_format) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		regmap_write(p2d->regmap, PXL2DPI_CTRL, CFG_24BIT);
		break;
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
		regmap_write(p2d->regmap, PXL2DPI_CTRL, CFG2_18BIT);
		break;
	default:
		DRM_DEV_ERROR(p2d->dev,
			      "unsupported output bus format 0x%08x\n",
							p2d->out_bus_format);
	}

	if (p2d->companion) {
		companion_p2d = bridge_to_p2d(p2d->companion);

		companion_p2d->in_bus_format = p2d->in_bus_format;
		companion_p2d->out_bus_format = p2d->out_bus_format;

		p2d->companion->funcs->mode_set(p2d->companion, mode,
							adjusted_mode);
	}
}

static void
imx8qxp_pxl2dpi_bridge_atomic_disable(struct drm_bridge *bridge,
				      struct drm_bridge_state *old_bridge_state)
{
	struct imx8qxp_pxl2dpi *p2d = bridge->driver_private;
	int ret;

	ret = pm_runtime_put(p2d->dev);
	if (ret < 0)
		DRM_DEV_ERROR(p2d->dev, "failed to put runtime PM: %d\n", ret);

	if (p2d->companion)
		p2d->companion->funcs->atomic_disable(p2d->companion,
							old_bridge_state);
}

static const u32 imx8qxp_pxl2dpi_bus_output_fmts[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X24_CPADHI,
};

static bool imx8qxp_pxl2dpi_bus_output_fmt_supported(u32 fmt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(imx8qxp_pxl2dpi_bus_output_fmts); i++) {
		if (imx8qxp_pxl2dpi_bus_output_fmts[i] == fmt)
			return true;
	}

	return false;
}

static u32 *
imx8qxp_pxl2dpi_bridge_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
						 struct drm_bridge_state *bridge_state,
						 struct drm_crtc_state *crtc_state,
						 struct drm_connector_state *conn_state,
						 u32 output_fmt,
						 unsigned int *num_input_fmts)
{
	u32 *input_fmts;

	if (!imx8qxp_pxl2dpi_bus_output_fmt_supported(output_fmt))
		return NULL;

	*num_input_fmts = 1;

	input_fmts = kmalloc(sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	switch (output_fmt) {
	case MEDIA_BUS_FMT_RGB888_1X24:
		input_fmts[0] = MEDIA_BUS_FMT_RGB888_1X36_CPADLO;
		break;
	case MEDIA_BUS_FMT_RGB666_1X24_CPADHI:
		input_fmts[0] = MEDIA_BUS_FMT_RGB666_1X36_CPADLO;
		break;
	default:
		kfree(input_fmts);
		input_fmts = NULL;
		break;
	}

	return input_fmts;
}

static u32 *
imx8qxp_pxl2dpi_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
						  struct drm_bridge_state *bridge_state,
						  struct drm_crtc_state *crtc_state,
						  struct drm_connector_state *conn_state,
						  unsigned int *num_output_fmts)
{
	*num_output_fmts = ARRAY_SIZE(imx8qxp_pxl2dpi_bus_output_fmts);
	return kmemdup(imx8qxp_pxl2dpi_bus_output_fmts,
			sizeof(imx8qxp_pxl2dpi_bus_output_fmts), GFP_KERNEL);
}

static const struct drm_bridge_funcs imx8qxp_pxl2dpi_bridge_funcs = {
	.atomic_duplicate_state	= drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_bridge_destroy_state,
	.atomic_reset		= drm_atomic_helper_bridge_reset,
	.attach			= imx8qxp_pxl2dpi_bridge_attach,
	.atomic_check		= imx8qxp_pxl2dpi_bridge_atomic_check,
	.mode_set		= imx8qxp_pxl2dpi_bridge_mode_set,
	.atomic_disable		= imx8qxp_pxl2dpi_bridge_atomic_disable,
	.atomic_get_input_bus_fmts =
			imx8qxp_pxl2dpi_bridge_atomic_get_input_bus_fmts,
	.atomic_get_output_bus_fmts =
			imx8qxp_pxl2dpi_bridge_atomic_get_output_bus_fmts,
};

static struct device_node *
imx8qxp_pxl2dpi_get_available_ep_from_port(struct imx8qxp_pxl2dpi *p2d,
					   u32 port_id)
{
	struct device_node *port, *ep;
	int ep_cnt;

	port = of_graph_get_port_by_id(p2d->dev->of_node, port_id);
	if (!port) {
		DRM_DEV_ERROR(p2d->dev, "failed to get port@%u\n", port_id);
		return ERR_PTR(-ENODEV);
	}

	ep_cnt = of_get_available_child_count(port);
	if (ep_cnt == 0) {
		DRM_DEV_ERROR(p2d->dev, "no available endpoints of port@%u\n",
			      port_id);
		ep = ERR_PTR(-ENODEV);
		goto out;
	} else if (ep_cnt > 1) {
		DRM_DEV_ERROR(p2d->dev,
			      "invalid available endpoints of port@%u\n",
			      port_id);
		ep = ERR_PTR(-EINVAL);
		goto out;
	}

	ep = of_get_next_available_child(port, NULL);
	if (!ep) {
		DRM_DEV_ERROR(p2d->dev,
			      "failed to get available endpoint of port@%u\n",
			      port_id);
		ep = ERR_PTR(-ENODEV);
		goto out;
	}
out:
	of_node_put(port);
	return ep;
}

static struct drm_bridge *
imx8qxp_pxl2dpi_find_next_bridge(struct imx8qxp_pxl2dpi *p2d)
{
	struct device_node *ep, *remote;
	struct drm_bridge *next_bridge;
	int ret;

	ep = imx8qxp_pxl2dpi_get_available_ep_from_port(p2d, 1);
	if (IS_ERR(ep)) {
		ret = PTR_ERR(ep);
		return ERR_PTR(ret);
	}

	remote = of_graph_get_remote_port_parent(ep);
	if (!remote || !of_device_is_available(remote)) {
		DRM_DEV_ERROR(p2d->dev, "no available remote\n");
		next_bridge = ERR_PTR(-ENODEV);
		goto out;
	} else if (!of_device_is_available(remote->parent)) {
		DRM_DEV_ERROR(p2d->dev, "remote parent is not available\n");
		next_bridge = ERR_PTR(-ENODEV);
		goto out;
	}

	next_bridge = of_drm_find_bridge(remote);
	if (!next_bridge) {
		next_bridge = ERR_PTR(-EPROBE_DEFER);
		goto out;
	}
out:
	of_node_put(remote);
	of_node_put(ep);

	return next_bridge;
}

static int imx8qxp_pxl2dpi_set_pixel_link_sel(struct imx8qxp_pxl2dpi *p2d)
{
	struct device_node *ep;
	struct of_endpoint endpoint;
	int ret;

	ep = imx8qxp_pxl2dpi_get_available_ep_from_port(p2d, 0);
	if (IS_ERR(ep))
		return PTR_ERR(ep);

	ret = of_graph_parse_endpoint(ep, &endpoint);
	if (ret) {
		DRM_DEV_ERROR(p2d->dev,
			      "failed to parse endpoint of port@0: %d\n", ret);
		goto out;
	}

	p2d->pl_sel = endpoint.id;
out:
	of_node_put(ep);

	return ret;
}

static int imx8qxp_pxl2dpi_parse_dt_companion(struct imx8qxp_pxl2dpi *p2d)
{
	struct imx8qxp_pxl2dpi *companion_p2d;
	struct device *dev = p2d->dev;
	struct device_node *companion;
	struct device_node *port1, *port2;
	const struct of_device_id *match;
	int dual_link;
	int ret = 0;

	/* Locate the companion PXL2DPI for dual-link operation, if any. */
	companion = of_parse_phandle(dev->of_node, "fsl,companion-pxl2dpi", 0);
	if (!companion)
		return 0;

	if (!of_device_is_available(companion)) {
		DRM_DEV_ERROR(dev, "companion PXL2DPI is not available\n");
		ret = -ENODEV;
		goto out;
	}

	/*
	 * Sanity check: the companion bridge must have the same compatible
	 * string.
	 */
	match = of_match_device(dev->driver->of_match_table, dev);
	if (!of_device_is_compatible(companion, match->compatible)) {
		DRM_DEV_ERROR(dev, "companion PXL2DPI is incompatible\n");
		ret = -ENXIO;
		goto out;
	}

	p2d->companion = of_drm_find_bridge(companion);
	if (!p2d->companion) {
		ret = -EPROBE_DEFER;
		DRM_DEV_DEBUG_DRIVER(p2d->dev,
				     "failed to find companion bridge: %d\n",
				     ret);
		goto out;
	}

	companion_p2d = bridge_to_p2d(p2d->companion);

	/*
	 * We need to work out if the sink is expecting us to function in
	 * dual-link mode.  We do this by looking at the DT port nodes that
	 * the next bridges are connected to.  If they are marked as expecting
	 * even pixels and odd pixels than we need to use the companion PXL2DPI.
	 */
	port1 = of_graph_get_port_by_id(p2d->next_bridge->of_node, 1);
	port2 = of_graph_get_port_by_id(companion_p2d->next_bridge->of_node, 1);
	dual_link = drm_of_lvds_get_dual_link_pixel_order(port1, port2);
	of_node_put(port1);
	of_node_put(port2);

	if (dual_link < 0) {
		ret = dual_link;
		DRM_DEV_ERROR(dev, "failed to get dual link pixel order: %d\n",
			      ret);
		goto out;
	}

	DRM_DEV_DEBUG_DRIVER(dev,
			     "dual-link configuration detected (companion bridge %pOF)\n",
			     companion);
out:
	of_node_put(companion);
	return ret;
}

static int imx8qxp_pxl2dpi_bridge_probe(struct platform_device *pdev)
{
	struct imx8qxp_pxl2dpi *p2d;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	p2d = devm_kzalloc(dev, sizeof(*p2d), GFP_KERNEL);
	if (!p2d)
		return -ENOMEM;

	p2d->regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(p2d->regmap)) {
		ret = PTR_ERR(p2d->regmap);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to get regmap: %d\n", ret);
		return ret;
	}

	ret = imx_scu_get_handle(&p2d->ipc_handle);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to get SCU ipc handle: %d\n",
				      ret);
		return ret;
	}

	p2d->dev = dev;

	ret = of_property_read_u32(np, "fsl,sc-resource", &p2d->sc_resource);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to get SC resource %d\n", ret);
		return ret;
	}

	p2d->next_bridge = imx8qxp_pxl2dpi_find_next_bridge(p2d);
	if (IS_ERR(p2d->next_bridge)) {
		ret = PTR_ERR(p2d->next_bridge);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to find next bridge: %d\n",
				      ret);
		return ret;
	}

	ret = imx8qxp_pxl2dpi_set_pixel_link_sel(p2d);
	if (ret)
		return ret;

	ret = imx8qxp_pxl2dpi_parse_dt_companion(p2d);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, p2d);
	pm_runtime_enable(dev);

	p2d->bridge.driver_private = p2d;
	p2d->bridge.funcs = &imx8qxp_pxl2dpi_bridge_funcs;
	p2d->bridge.of_node = np;

	drm_bridge_add(&p2d->bridge);

	return ret;
}

static void imx8qxp_pxl2dpi_bridge_remove(struct platform_device *pdev)
{
	struct imx8qxp_pxl2dpi *p2d = platform_get_drvdata(pdev);

	drm_bridge_remove(&p2d->bridge);

	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id imx8qxp_pxl2dpi_dt_ids[] = {
	{ .compatible = "fsl,imx8qxp-pxl2dpi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx8qxp_pxl2dpi_dt_ids);

static struct platform_driver imx8qxp_pxl2dpi_bridge_driver = {
	.probe	= imx8qxp_pxl2dpi_bridge_probe,
	.remove_new = imx8qxp_pxl2dpi_bridge_remove,
	.driver	= {
		.of_match_table = imx8qxp_pxl2dpi_dt_ids,
		.name = DRIVER_NAME,
	},
};
module_platform_driver(imx8qxp_pxl2dpi_bridge_driver);

MODULE_DESCRIPTION("i.MX8QXP pixel link to DPI bridge driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
