/*
 * Copyright (C) 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>

#include <linux/of_graph.h>

static int lvds_encoder_probe(struct platform_device *pdev)
{
	struct device_node *port;
	struct device_node *endpoint;
	struct device_node *panel_node;
	struct drm_panel *panel;
	struct drm_bridge *bridge;

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

	panel_node = of_graph_get_remote_port_parent(endpoint);
	of_node_put(endpoint);
	if (!panel_node) {
		dev_dbg(&pdev->dev, "no remote endpoint for port 1\n");
		return -ENXIO;
	}

	panel = of_drm_find_panel(panel_node);
	of_node_put(panel_node);
	if (!panel) {
		dev_dbg(&pdev->dev, "panel not found, deferring probe\n");
		return -EPROBE_DEFER;
	}

	bridge = drm_panel_bridge_add(panel, DRM_MODE_CONNECTOR_LVDS);
	if (IS_ERR(bridge))
		return PTR_ERR(bridge);

	platform_set_drvdata(pdev, bridge);

	return 0;
}

static int lvds_encoder_remove(struct platform_device *pdev)
{
	struct drm_bridge *bridge = platform_get_drvdata(pdev);

	drm_bridge_remove(bridge);

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
