/*
 * Copyright (C) 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_panel.h>

#include <linux/of_graph.h>

struct lvds_encoder {
	struct device *dev;

	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_panel *panel;
};

static inline struct lvds_encoder *
drm_bridge_to_lvds_encoder(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lvds_encoder, bridge);
}

static inline struct lvds_encoder *
drm_connector_to_lvds_encoder(struct drm_connector *connector)
{
	return container_of(connector, struct lvds_encoder, connector);
}

static int lvds_connector_get_modes(struct drm_connector *connector)
{
	struct lvds_encoder *lvds = drm_connector_to_lvds_encoder(connector);

	return drm_panel_get_modes(lvds->panel);
}

static const struct drm_connector_helper_funcs lvds_connector_helper_funcs = {
	.get_modes = lvds_connector_get_modes,
};

static const struct drm_connector_funcs lvds_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int lvds_encoder_attach(struct drm_bridge *bridge)
{
	struct lvds_encoder *lvds = drm_bridge_to_lvds_encoder(bridge);
	struct drm_connector *connector = &lvds->connector;
	int ret;

	if (!bridge->encoder) {
		DRM_ERROR("Missing encoder\n");
		return -ENODEV;
	}

	drm_connector_helper_add(connector, &lvds_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, connector, &lvds_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_mode_connector_attach_encoder(&lvds->connector, bridge->encoder);

	ret = drm_panel_attach(lvds->panel, &lvds->connector);
	if (ret < 0)
		return ret;

	return 0;
}

static void lvds_encoder_detach(struct drm_bridge *bridge)
{
	struct lvds_encoder *lvds = drm_bridge_to_lvds_encoder(bridge);

	drm_panel_detach(lvds->panel);
}

static void lvds_encoder_pre_enable(struct drm_bridge *bridge)
{
	struct lvds_encoder *lvds = drm_bridge_to_lvds_encoder(bridge);

	drm_panel_prepare(lvds->panel);
}

static void lvds_encoder_enable(struct drm_bridge *bridge)
{
	struct lvds_encoder *lvds = drm_bridge_to_lvds_encoder(bridge);

	drm_panel_enable(lvds->panel);
}

static void lvds_encoder_disable(struct drm_bridge *bridge)
{
	struct lvds_encoder *lvds = drm_bridge_to_lvds_encoder(bridge);

	drm_panel_disable(lvds->panel);
}

static void lvds_encoder_post_disable(struct drm_bridge *bridge)
{
	struct lvds_encoder *lvds = drm_bridge_to_lvds_encoder(bridge);

	drm_panel_unprepare(lvds->panel);
}

static const struct drm_bridge_funcs lvds_encoder_bridge_funcs = {
	.attach = lvds_encoder_attach,
	.detach = lvds_encoder_detach,
	.pre_enable = lvds_encoder_pre_enable,
	.enable = lvds_encoder_enable,
	.disable = lvds_encoder_disable,
	.post_disable = lvds_encoder_post_disable,
};

static int lvds_encoder_probe(struct platform_device *pdev)
{
	struct lvds_encoder *lvds;
	struct device_node *port;
	struct device_node *endpoint;
	struct device_node *panel;

	lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
	if (!lvds)
		return -ENOMEM;

	lvds->dev = &pdev->dev;
	platform_set_drvdata(pdev, lvds);

	lvds->bridge.funcs = &lvds_encoder_bridge_funcs;
	lvds->bridge.of_node = pdev->dev.of_node;

	/* Locate the panel DT node. */
	port = of_graph_get_port_by_id(pdev->dev.of_node, 1);
	if (!port) {
		dev_dbg(&pdev->dev, "port 1 not found\n");
		return -ENXIO;
	}

	endpoint = of_get_child_by_name(port, "endpoint");
	of_node_put(port);
	if (!endpoint) {
		dev_dbg(&pdev->dev, "no endpoint for port 1\n");
		return -ENXIO;
	}

	panel = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);
	if (!panel) {
		dev_dbg(&pdev->dev, "no remote endpoint for port 1\n");
		return -ENXIO;
	}

	lvds->panel = of_drm_find_panel(panel);
	of_node_put(panel);
	if (!lvds->panel) {
		dev_dbg(&pdev->dev, "panel not found, deferring probe\n");
		return -EPROBE_DEFER;
	}

	/* Register the bridge. */
	return drm_bridge_add(&lvds->bridge);
}

static int lvds_encoder_remove(struct platform_device *pdev)
{
	struct lvds_encoder *encoder = platform_get_drvdata(pdev);

	drm_bridge_remove(&encoder->bridge);

	return 0;
}

static const struct of_device_id lvds_encoder_match[] = {
	{ .compatible = "lvds-encoder" },
	{ .compatible = "thine,thc63lvdm83d" },
	{},
};
MODULE_DEVICE_TABLE(of, lvds_encoder_match);

static struct platform_driver lvds_encoder_driver = {
	.probe	= lvds_encoder_probe,
	.remove	= lvds_encoder_remove,
	.driver		= {
		.name		= "lvds-encoder",
		.of_match_table	= lvds_encoder_match,
	},
};
module_platform_driver(lvds_encoder_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Transparent parallel to LVDS encoder");
MODULE_LICENSE("GPL");
