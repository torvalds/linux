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
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>

struct lvds_codec {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *panel_bridge;
	struct drm_bridge_timings timings;
	struct regulator *vcc;
	struct gpio_desc *powerdown_gpio;
	u32 connector_type;
	unsigned int bus_format;
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
	int ret;

	ret = regulator_enable(lvds_codec->vcc);
	if (ret) {
		dev_err(lvds_codec->dev,
			"Failed to enable regulator \"vcc\": %d\n", ret);
		return;
	}

	if (lvds_codec->powerdown_gpio)
		gpiod_set_value_cansleep(lvds_codec->powerdown_gpio, 0);
}

static void lvds_codec_disable(struct drm_bridge *bridge)
{
	struct lvds_codec *lvds_codec = to_lvds_codec(bridge);
	int ret;

	if (lvds_codec->powerdown_gpio)
		gpiod_set_value_cansleep(lvds_codec->powerdown_gpio, 1);

	ret = regulator_disable(lvds_codec->vcc);
	if (ret)
		dev_err(lvds_codec->dev,
			"Failed to disable regulator \"vcc\": %d\n", ret);
}

static const struct drm_bridge_funcs funcs = {
	.attach = lvds_codec_attach,
	.enable = lvds_codec_enable,
	.disable = lvds_codec_disable,
};

#define MAX_INPUT_SEL_FORMATS 1
static u32 *
lvds_codec_atomic_get_input_bus_fmts(struct drm_bridge *bridge,
				     struct drm_bridge_state *bridge_state,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state,
				     u32 output_fmt,
				     unsigned int *num_input_fmts)
{
	struct lvds_codec *lvds_codec = to_lvds_codec(bridge);
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kcalloc(MAX_INPUT_SEL_FORMATS, sizeof(*input_fmts),
			     GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	input_fmts[0] = lvds_codec->bus_format;
	*num_input_fmts = MAX_INPUT_SEL_FORMATS;

	return input_fmts;
}

static const struct drm_bridge_funcs funcs_decoder = {
	.attach = lvds_codec_attach,
	.enable = lvds_codec_enable,
	.disable = lvds_codec_disable,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_get_input_bus_fmts = lvds_codec_atomic_get_input_bus_fmts,
};

static int lvds_codec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *panel_node;
	struct device_node *bus_node;
	struct drm_panel *panel;
	struct lvds_codec *lvds_codec;
	u32 val;
	int ret;

	lvds_codec = devm_kzalloc(dev, sizeof(*lvds_codec), GFP_KERNEL);
	if (!lvds_codec)
		return -ENOMEM;

	lvds_codec->dev = &pdev->dev;
	lvds_codec->connector_type = (uintptr_t)of_device_get_match_data(dev);

	lvds_codec->vcc = devm_regulator_get(lvds_codec->dev, "power");
	if (IS_ERR(lvds_codec->vcc))
		return dev_err_probe(dev, PTR_ERR(lvds_codec->vcc),
				     "Unable to get \"vcc\" supply\n");

	lvds_codec->powerdown_gpio = devm_gpiod_get_optional(dev, "powerdown",
							     GPIOD_OUT_HIGH);
	if (IS_ERR(lvds_codec->powerdown_gpio))
		return dev_err_probe(dev, PTR_ERR(lvds_codec->powerdown_gpio),
				     "powerdown GPIO failure\n");

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

	lvds_codec->bridge.funcs = &funcs;

	/*
	 * Decoder input LVDS format is a property of the decoder chip or even
	 * its strapping. Handle data-mapping the same way lvds-panel does. In
	 * case data-mapping is not present, do nothing, since there are still
	 * legacy bindings which do not specify this property.
	 */
	if (lvds_codec->connector_type != DRM_MODE_CONNECTOR_LVDS) {
		bus_node = of_graph_get_endpoint_by_regs(dev->of_node, 0, 0);
		if (!bus_node) {
			dev_dbg(dev, "bus DT node not found\n");
			return -ENXIO;
		}

		ret = drm_of_lvds_get_data_mapping(bus_node);
		of_node_put(bus_node);
		if (ret == -ENODEV) {
			dev_warn(dev, "missing 'data-mapping' DT property\n");
		} else if (ret < 0) {
			dev_err(dev, "invalid 'data-mapping' DT property\n");
			return ret;
		} else {
			lvds_codec->bus_format = ret;
			lvds_codec->bridge.funcs = &funcs_decoder;
		}
	}

	/*
	 * Encoder might sample data on different clock edge than the display,
	 * for example OnSemi FIN3385 has a dedicated strapping pin to select
	 * the sampling edge.
	 */
	if (lvds_codec->connector_type == DRM_MODE_CONNECTOR_LVDS &&
	    !of_property_read_u32(dev->of_node, "pclk-sample", &val)) {
		lvds_codec->timings.input_bus_flags = val ?
			DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE :
			DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE;
	}

	/*
	 * The panel_bridge bridge is attached to the panel's of_node,
	 * but we need a bridge attached to our of_node for our user
	 * to look up.
	 */
	lvds_codec->bridge.of_node = dev->of_node;
	lvds_codec->bridge.timings = &lvds_codec->timings;
	drm_bridge_add(&lvds_codec->bridge);

	platform_set_drvdata(pdev, lvds_codec);

	return 0;
}

static void lvds_codec_remove(struct platform_device *pdev)
{
	struct lvds_codec *lvds_codec = platform_get_drvdata(pdev);

	drm_bridge_remove(&lvds_codec->bridge);
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
	.remove_new = lvds_codec_remove,
	.driver		= {
		.name		= "lvds-codec",
		.of_match_table	= lvds_codec_match,
	},
};
module_platform_driver(lvds_codec_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("LVDS encoders and decoders");
MODULE_LICENSE("GPL");
