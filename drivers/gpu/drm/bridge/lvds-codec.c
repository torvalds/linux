// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019 Renesas Electronics Corporation
 * Copyright (C) 2016 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>

struct lvds_codec {
	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
	struct gpio_desc *powerdown_gpio;
	u32 connector_type;
};

static inline struct lvds_codec *to_lvds_codec(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lvds_codec, bridge);
}

static int lvds_codec_attach(struct drm_bridge *bridge,
			     enum drm_bridge_attach_flags flags)
{
	struct lvds_codec *lvds_codec = to_lvds_codec(bridge);

	return drm_bridge_attach(bridge->encoder, lvds_codec->panel_bridge,
				 bridge, flags);
}

static void lvds_codec_enable(struct drm_bridge *bridge)
{
	struct lvds_codec *lvds_codec = to_lvds_codec(bridge);

	if (lvds_codec->powerdown_gpio)
		gpiod_set_value_cansleep(lvds_codec->powerdown_gpio, 0);
}

static void lvds_codec_disable(struct drm_bridge *bridge)
{
	struct lvds_codec *lvds_codec = to_lvds_codec(bridge);

	if (lvds_codec->powerdown_gpio)
		gpiod_set_value_cansleep(lvds_codec->powerdown_gpio, 1);
}

static const struct drm_bridge_funcs funcs = {
	.attach = lvds_codec_attach,
	.enable = lvds_codec_enable,
	.disable = lvds_codec_disable,
};

static int lvds_codec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *panel_node;
	struct drm_panel *panel;
	struct lvds_codec *lvds_codec;

	lvds_codec = devm_kzalloc(dev, sizeof(*lvds_codec), GFP_KERNEL);
	if (!lvds_codec)
		return -ENOMEM;

	lvds_codec->connector_type = (uintptr_t)of_device_get_match_data(dev);
	lvds_codec->powerdown_gpio = devm_gpiod_get_optional(dev, "powerdown",
							     GPIOD_OUT_HIGH);
	if (IS_ERR(lvds_codec->powerdown_gpio)) {
		int err = PTR_ERR(lvds_codec->powerdown_gpio);

		if (err != -EPROBE_DEFER)
			dev_err(dev, "powerdown GPIO failure: %d\n", err);
		return err;
	}

	/* Locate the panel DT node. */
	panel_node = of_graph_get_remote_node(dev->of_node, 1, 0);
	if (!panel_node) {
		dev_dbg(dev, "panel DT node not found\n");
		return -ENXIO;
	}

	panel = of_drm_find_panel(panel_node);
	of_node_put(panel_node);
	if (IS_ERR(panel)) {
		dev_dbg(dev, "panel not found, deferring probe\n");
		return PTR_ERR(panel);
	}

	lvds_codec->panel_bridge =
		devm_drm_panel_bridge_add_typed(dev, panel,
						lvds_codec->connector_type);
	if (IS_ERR(lvds_codec->panel_bridge))
		return PTR_ERR(lvds_codec->panel_bridge);

	/*
	 * The panel_bridge bridge is attached to the panel's of_node,
	 * but we need a bridge attached to our of_node for our user
	 * to look up.
	 */
	lvds_codec->bridge.of_node = dev->of_node;
	lvds_codec->bridge.funcs = &funcs;
	drm_bridge_add(&lvds_codec->bridge);

	platform_set_drvdata(pdev, lvds_codec);

	return 0;
}

static int lvds_codec_remove(struct platform_device *pdev)
{
	struct lvds_codec *lvds_codec = platform_get_drvdata(pdev);

	drm_bridge_remove(&lvds_codec->bridge);

	return 0;
}

static const struct of_device_id lvds_codec_match[] = {
	{
		.compatible = "lvds-decoder",
		.data = (void *)DRM_MODE_CONNECTOR_DPI,
	},
	{
		.compatible = "lvds-encoder",
		.data = (void *)DRM_MODE_CONNECTOR_LVDS,
	},
	{
		.compatible = "thine,thc63lvdm83d",
		.data = (void *)DRM_MODE_CONNECTOR_LVDS,
	},
	{},
};
MODULE_DEVICE_TABLE(of, lvds_codec_match);

static struct platform_driver lvds_codec_driver = {
	.probe	= lvds_codec_probe,
	.remove	= lvds_codec_remove,
	.driver		= {
		.name		= "lvds-codec",
		.of_match_table	= lvds_codec_match,
	},
};
module_platform_driver(lvds_codec_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("LVDS encoders and decoders");
MODULE_LICENSE("GPL");
