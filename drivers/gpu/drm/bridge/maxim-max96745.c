// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96745 GMSL2 Serializer with eDP1.4a/DP1.4 Input
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_of.h>
#include <drm/drm_connector.h>
#include <drm/drm_probe_helper.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/max96745.h>

struct max96745_bridge {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;

	struct device *dev;
	struct regmap *regmap;
};

#define to_max96745_bridge(x)	container_of(x, struct max96745_bridge, x)

static int max96745_bridge_attach(struct drm_bridge *bridge,
				  enum drm_bridge_attach_flags flags)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	int ret;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1, NULL,
					  &ser->next_bridge);
	if (ret)
		return ret;

	return drm_bridge_attach(bridge->encoder, ser->next_bridge, bridge, 0);
}

static void max96745_bridge_pre_enable(struct drm_bridge *bridge)
{
}

static void max96745_bridge_enable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	regmap_update_bits(ser->regmap, 0x0100, VID_TX_EN,
			   FIELD_PREP(VID_TX_EN, 1));
}

static void max96745_bridge_disable(struct drm_bridge *bridge)
{
	struct max96745_bridge *ser = to_max96745_bridge(bridge);

	regmap_update_bits(ser->regmap, 0x0100, VID_TX_EN,
			   FIELD_PREP(VID_TX_EN, 0));
}

static void max96745_bridge_post_disable(struct drm_bridge *bridge)
{
}

static enum drm_connector_status
max96745_bridge_detect(struct drm_bridge *bridge)
{
	struct drm_bridge *prev_bridge = drm_bridge_get_prev_bridge(bridge);
	struct max96745_bridge *ser = to_max96745_bridge(bridge);
	u32 val;

	if (prev_bridge) {
		if (prev_bridge->ops & DRM_BRIDGE_OP_DETECT) {
			if (drm_bridge_detect(prev_bridge) != connector_status_connected)
				return connector_status_disconnected;
		}
	}

	if (regmap_read(ser->regmap, 0x002a, &val))
		return connector_status_disconnected;

	if (FIELD_GET(LINK_LOCKED, val))
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static const struct drm_bridge_funcs max96745_bridge_funcs = {
	.attach = max96745_bridge_attach,
	.detect = max96745_bridge_detect,
	.pre_enable = max96745_bridge_pre_enable,
	.enable = max96745_bridge_enable,
	.disable = max96745_bridge_disable,
	.post_disable = max96745_bridge_post_disable,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int max96745_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96745_bridge *ser;

	ser = devm_kzalloc(dev, sizeof(*ser), GFP_KERNEL);
	if (!ser)
		return -ENOMEM;

	ser->dev = dev;
	platform_set_drvdata(pdev, ser);

	ser->regmap = dev_get_regmap(dev->parent, NULL);
	if (!ser->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	regmap_update_bits(ser->regmap, 0x7070, MAX_LANE_COUNT,
			   FIELD_PREP(MAX_LANE_COUNT, 0x04));
	regmap_update_bits(ser->regmap, 0x7074, MAX_LINK_RATE,
			   FIELD_PREP(MAX_LINK_RATE, 0x14));
	regmap_update_bits(ser->regmap, 0x7000, LINK_ENABLE,
			   FIELD_PREP(LINK_ENABLE, 1));

	ser->bridge.funcs = &max96745_bridge_funcs;
	ser->bridge.of_node = dev->of_node;
	ser->bridge.ops = DRM_BRIDGE_OP_DETECT;
	ser->bridge.type = DRM_MODE_CONNECTOR_LVDS;

	drm_bridge_add(&ser->bridge);

	return 0;
}

static int max96745_bridge_remove(struct platform_device *pdev)
{
	struct max96745_bridge *ser = platform_get_drvdata(pdev);

	drm_bridge_remove(&ser->bridge);

	return 0;
}

static const struct of_device_id max96745_bridge_of_match[] = {
	{ .compatible = "maxim,max96745-bridge", },
	{}
};
MODULE_DEVICE_TABLE(of, max96745_bridge_of_match);

static struct platform_driver max96745_bridge_driver = {
	.driver = {
		.name = "max96745-bridge",
		.of_match_table = of_match_ptr(max96745_bridge_of_match),
	},
	.probe = max96745_bridge_probe,
	.remove = max96745_bridge_remove,
};

module_platform_driver(max96745_bridge_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96745 GMSL2 Serializer with eDP1.4a/DP1.4 Input");
MODULE_LICENSE("GPL");
