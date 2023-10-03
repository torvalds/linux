// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Free Electrons
 * Copyright (C) 2015-2016 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

struct simple_bridge_info {
	const struct drm_bridge_timings *timings;
	unsigned int connector_type;
};

struct simple_bridge {
	struct drm_bridge	bridge;
	struct drm_connector	connector;

	const struct simple_bridge_info *info;

	struct drm_bridge	*next_bridge;
	struct regulator	*vdd;
	struct gpio_desc	*enable;
};

static inline struct simple_bridge *
drm_bridge_to_simple_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct simple_bridge, bridge);
}

static inline struct simple_bridge *
drm_connector_to_simple_bridge(struct drm_connector *connector)
{
	return container_of(connector, struct simple_bridge, connector);
}

static int simple_bridge_get_modes(struct drm_connector *connector)
{
	struct simple_bridge *sbridge = drm_connector_to_simple_bridge(connector);
	struct edid *edid;
	int ret;

	if (sbridge->next_bridge->ops & DRM_BRIDGE_OP_EDID) {
		edid = drm_bridge_get_edid(sbridge->next_bridge, connector);
		if (!edid)
			DRM_INFO("EDID read failed. Fallback to standard modes\n");
	} else {
		edid = NULL;
	}

	if (!edid) {
		/*
		 * In case we cannot retrieve the EDIDs (missing or broken DDC
		 * bus from the next bridge), fallback on the XGA standards and
		 * prefer a mode pretty much anyone can handle.
		 */
		ret = drm_add_modes_noedid(connector, 1920, 1200);
		drm_set_preferred_mode(connector, 1024, 768);
		return ret;
	}

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return ret;
}

static const struct drm_connector_helper_funcs simple_bridge_con_helper_funcs = {
	.get_modes	= simple_bridge_get_modes,
};

static enum drm_connector_status
simple_bridge_connector_detect(struct drm_connector *connector, bool force)
{
	struct simple_bridge *sbridge = drm_connector_to_simple_bridge(connector);

	return drm_bridge_detect(sbridge->next_bridge);
}

static const struct drm_connector_funcs simple_bridge_con_funcs = {
	.detect			= simple_bridge_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int simple_bridge_attach(struct drm_bridge *bridge,
				enum drm_bridge_attach_flags flags)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	int ret;

	ret = drm_bridge_attach(bridge->encoder, sbridge->next_bridge, bridge,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret < 0)
		return ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	if (!bridge->encoder) {
		DRM_ERROR("Missing encoder\n");
		return -ENODEV;
	}

	drm_connector_helper_add(&sbridge->connector,
				 &simple_bridge_con_helper_funcs);
	ret = drm_connector_init_with_ddc(bridge->dev, &sbridge->connector,
					  &simple_bridge_con_funcs,
					  sbridge->info->connector_type,
					  sbridge->next_bridge->ddc);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_attach_encoder(&sbridge->connector, bridge->encoder);

	return 0;
}

static void simple_bridge_enable(struct drm_bridge *bridge)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	int ret;

	if (sbridge->vdd) {
		ret = regulator_enable(sbridge->vdd);
		if (ret)
			DRM_ERROR("Failed to enable vdd regulator: %d\n", ret);
	}

	gpiod_set_value_cansleep(sbridge->enable, 1);
}

static void simple_bridge_disable(struct drm_bridge *bridge)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);

	gpiod_set_value_cansleep(sbridge->enable, 0);

	if (sbridge->vdd)
		regulator_disable(sbridge->vdd);
}

static const struct drm_bridge_funcs simple_bridge_bridge_funcs = {
	.attach		= simple_bridge_attach,
	.enable		= simple_bridge_enable,
	.disable	= simple_bridge_disable,
};

static int simple_bridge_probe(struct platform_device *pdev)
{
	struct simple_bridge *sbridge;
	struct device_node *remote;

	sbridge = devm_kzalloc(&pdev->dev, sizeof(*sbridge), GFP_KERNEL);
	if (!sbridge)
		return -ENOMEM;
	platform_set_drvdata(pdev, sbridge);

	sbridge->info = of_device_get_match_data(&pdev->dev);

	/* Get the next bridge in the pipeline. */
	remote = of_graph_get_remote_node(pdev->dev.of_node, 1, -1);
	if (!remote)
		return -EINVAL;

	sbridge->next_bridge = of_drm_find_bridge(remote);
	of_node_put(remote);

	if (!sbridge->next_bridge) {
		dev_dbg(&pdev->dev, "Next bridge not found, deferring probe\n");
		return -EPROBE_DEFER;
	}

	/* Get the regulator and GPIO resources. */
	sbridge->vdd = devm_regulator_get_optional(&pdev->dev, "vdd");
	if (IS_ERR(sbridge->vdd)) {
		int ret = PTR_ERR(sbridge->vdd);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		sbridge->vdd = NULL;
		dev_dbg(&pdev->dev, "No vdd regulator found: %d\n", ret);
	}

	sbridge->enable = devm_gpiod_get_optional(&pdev->dev, "enable",
						  GPIOD_OUT_LOW);
	if (IS_ERR(sbridge->enable))
		return dev_err_probe(&pdev->dev, PTR_ERR(sbridge->enable),
				     "Unable to retrieve enable GPIO\n");

	/* Register the bridge. */
	sbridge->bridge.funcs = &simple_bridge_bridge_funcs;
	sbridge->bridge.of_node = pdev->dev.of_node;
	sbridge->bridge.timings = sbridge->info->timings;

	drm_bridge_add(&sbridge->bridge);

	return 0;
}

static void simple_bridge_remove(struct platform_device *pdev)
{
	struct simple_bridge *sbridge = platform_get_drvdata(pdev);

	drm_bridge_remove(&sbridge->bridge);
}

/*
 * We assume the ADV7123 DAC is the "default" for historical reasons
 * Information taken from the ADV7123 datasheet, revision D.
 * NOTE: the ADV7123EP seems to have other timings and need a new timings
 * set if used.
 */
static const struct drm_bridge_timings default_bridge_timings = {
	/* Timing specifications, datasheet page 7 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	.setup_time_ps = 500,
	.hold_time_ps = 1500,
};

/*
 * Information taken from the THS8134, THS8134A, THS8134B datasheet named
 * "SLVS205D", dated May 1990, revised March 2000.
 */
static const struct drm_bridge_timings ti_ths8134_bridge_timings = {
	/* From timing diagram, datasheet page 9 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	/* From datasheet, page 12 */
	.setup_time_ps = 3000,
	/* I guess this means latched input */
	.hold_time_ps = 0,
};

/*
 * Information taken from the THS8135 datasheet named "SLAS343B", dated
 * May 2001, revised April 2013.
 */
static const struct drm_bridge_timings ti_ths8135_bridge_timings = {
	/* From timing diagram, datasheet page 14 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	/* From datasheet, page 16 */
	.setup_time_ps = 2000,
	.hold_time_ps = 500,
};

static const struct of_device_id simple_bridge_match[] = {
	{
		.compatible = "dumb-vga-dac",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	}, {
		.compatible = "adi,adv7123",
		.data = &(const struct simple_bridge_info) {
			.timings = &default_bridge_timings,
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	}, {
		.compatible = "ti,opa362",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_Composite,
		},
	}, {
		.compatible = "ti,ths8135",
		.data = &(const struct simple_bridge_info) {
			.timings = &ti_ths8135_bridge_timings,
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	}, {
		.compatible = "ti,ths8134",
		.data = &(const struct simple_bridge_info) {
			.timings = &ti_ths8134_bridge_timings,
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	},
	{},
};
MODULE_DEVICE_TABLE(of, simple_bridge_match);

static struct platform_driver simple_bridge_driver = {
	.probe	= simple_bridge_probe,
	.remove_new = simple_bridge_remove,
	.driver		= {
		.name		= "simple-bridge",
		.of_match_table	= simple_bridge_match,
	},
};
module_platform_driver(simple_bridge_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Simple DRM bridge driver");
MODULE_LICENSE("GPL");
