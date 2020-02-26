// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Free Electrons
 * Copyright (C) 2015-2016 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

struct dumb_vga {
	struct drm_bridge	bridge;
	struct drm_connector	connector;

	struct i2c_adapter	*ddc;
	struct regulator	*vdd;
};

static inline struct dumb_vga *
drm_bridge_to_dumb_vga(struct drm_bridge *bridge)
{
	return container_of(bridge, struct dumb_vga, bridge);
}

static inline struct dumb_vga *
drm_connector_to_dumb_vga(struct drm_connector *connector)
{
	return container_of(connector, struct dumb_vga, connector);
}

static int dumb_vga_get_modes(struct drm_connector *connector)
{
	struct dumb_vga *vga = drm_connector_to_dumb_vga(connector);
	struct edid *edid;
	int ret;

	if (!vga->ddc)
		goto fallback;

	edid = drm_get_edid(connector, vga->ddc);
	if (!edid) {
		DRM_INFO("EDID readout failed, falling back to standard modes\n");
		goto fallback;
	}

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);
	return ret;

fallback:
	/*
	 * In case we cannot retrieve the EDIDs (broken or missing i2c
	 * bus), fallback on the XGA standards
	 */
	ret = drm_add_modes_noedid(connector, 1920, 1200);

	/* And prefer a mode pretty much anyone can handle */
	drm_set_preferred_mode(connector, 1024, 768);

	return ret;
}

static const struct drm_connector_helper_funcs dumb_vga_con_helper_funcs = {
	.get_modes	= dumb_vga_get_modes,
};

static enum drm_connector_status
dumb_vga_connector_detect(struct drm_connector *connector, bool force)
{
	struct dumb_vga *vga = drm_connector_to_dumb_vga(connector);

	/*
	 * Even if we have an I2C bus, we can't assume that the cable
	 * is disconnected if drm_probe_ddc fails. Some cables don't
	 * wire the DDC pins, or the I2C bus might not be working at
	 * all.
	 */
	if (vga->ddc && drm_probe_ddc(vga->ddc))
		return connector_status_connected;

	return connector_status_unknown;
}

static const struct drm_connector_funcs dumb_vga_con_funcs = {
	.detect			= dumb_vga_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int dumb_vga_attach(struct drm_bridge *bridge,
			   enum drm_bridge_attach_flags flags)
{
	struct dumb_vga *vga = drm_bridge_to_dumb_vga(bridge);
	int ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR) {
		DRM_ERROR("Fix bridge driver to make connector optional!");
		return -EINVAL;
	}

	if (!bridge->encoder) {
		DRM_ERROR("Missing encoder\n");
		return -ENODEV;
	}

	drm_connector_helper_add(&vga->connector,
				 &dumb_vga_con_helper_funcs);
	ret = drm_connector_init_with_ddc(bridge->dev, &vga->connector,
					  &dumb_vga_con_funcs,
					  DRM_MODE_CONNECTOR_VGA,
					  vga->ddc);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_attach_encoder(&vga->connector,
					  bridge->encoder);

	return 0;
}

static void dumb_vga_enable(struct drm_bridge *bridge)
{
	struct dumb_vga *vga = drm_bridge_to_dumb_vga(bridge);
	int ret = 0;

	if (vga->vdd)
		ret = regulator_enable(vga->vdd);

	if (ret)
		DRM_ERROR("Failed to enable vdd regulator: %d\n", ret);
}

static void dumb_vga_disable(struct drm_bridge *bridge)
{
	struct dumb_vga *vga = drm_bridge_to_dumb_vga(bridge);

	if (vga->vdd)
		regulator_disable(vga->vdd);
}

static const struct drm_bridge_funcs dumb_vga_bridge_funcs = {
	.attach		= dumb_vga_attach,
	.enable		= dumb_vga_enable,
	.disable	= dumb_vga_disable,
};

static struct i2c_adapter *dumb_vga_retrieve_ddc(struct device *dev)
{
	struct device_node *phandle, *remote;
	struct i2c_adapter *ddc;

	remote = of_graph_get_remote_node(dev->of_node, 1, -1);
	if (!remote)
		return ERR_PTR(-EINVAL);

	phandle = of_parse_phandle(remote, "ddc-i2c-bus", 0);
	of_node_put(remote);
	if (!phandle)
		return ERR_PTR(-ENODEV);

	ddc = of_get_i2c_adapter_by_node(phandle);
	of_node_put(phandle);
	if (!ddc)
		return ERR_PTR(-EPROBE_DEFER);

	return ddc;
}

static int dumb_vga_probe(struct platform_device *pdev)
{
	struct dumb_vga *vga;

	vga = devm_kzalloc(&pdev->dev, sizeof(*vga), GFP_KERNEL);
	if (!vga)
		return -ENOMEM;
	platform_set_drvdata(pdev, vga);

	vga->vdd = devm_regulator_get_optional(&pdev->dev, "vdd");
	if (IS_ERR(vga->vdd)) {
		int ret = PTR_ERR(vga->vdd);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		vga->vdd = NULL;
		dev_dbg(&pdev->dev, "No vdd regulator found: %d\n", ret);
	}

	vga->ddc = dumb_vga_retrieve_ddc(&pdev->dev);
	if (IS_ERR(vga->ddc)) {
		if (PTR_ERR(vga->ddc) == -ENODEV) {
			dev_dbg(&pdev->dev,
				"No i2c bus specified. Disabling EDID readout\n");
			vga->ddc = NULL;
		} else {
			dev_err(&pdev->dev, "Couldn't retrieve i2c bus\n");
			return PTR_ERR(vga->ddc);
		}
	}

	vga->bridge.funcs = &dumb_vga_bridge_funcs;
	vga->bridge.of_node = pdev->dev.of_node;
	vga->bridge.timings = of_device_get_match_data(&pdev->dev);

	drm_bridge_add(&vga->bridge);

	return 0;
}

static int dumb_vga_remove(struct platform_device *pdev)
{
	struct dumb_vga *vga = platform_get_drvdata(pdev);

	drm_bridge_remove(&vga->bridge);

	if (vga->ddc)
		i2c_put_adapter(vga->ddc);

	return 0;
}

/*
 * We assume the ADV7123 DAC is the "default" for historical reasons
 * Information taken from the ADV7123 datasheet, revision D.
 * NOTE: the ADV7123EP seems to have other timings and need a new timings
 * set if used.
 */
static const struct drm_bridge_timings default_dac_timings = {
	/* Timing specifications, datasheet page 7 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	.setup_time_ps = 500,
	.hold_time_ps = 1500,
};

/*
 * Information taken from the THS8134, THS8134A, THS8134B datasheet named
 * "SLVS205D", dated May 1990, revised March 2000.
 */
static const struct drm_bridge_timings ti_ths8134_dac_timings = {
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
static const struct drm_bridge_timings ti_ths8135_dac_timings = {
	/* From timing diagram, datasheet page 14 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	/* From datasheet, page 16 */
	.setup_time_ps = 2000,
	.hold_time_ps = 500,
};

static const struct of_device_id dumb_vga_match[] = {
	{
		.compatible = "dumb-vga-dac",
		.data = NULL,
	},
	{
		.compatible = "adi,adv7123",
		.data = &default_dac_timings,
	},
	{
		.compatible = "ti,ths8135",
		.data = &ti_ths8135_dac_timings,
	},
	{
		.compatible = "ti,ths8134",
		.data = &ti_ths8134_dac_timings,
	},
	{},
};
MODULE_DEVICE_TABLE(of, dumb_vga_match);

static struct platform_driver dumb_vga_driver = {
	.probe	= dumb_vga_probe,
	.remove	= dumb_vga_remove,
	.driver		= {
		.name		= "dumb-vga-dac",
		.of_match_table	= dumb_vga_match,
	},
};
module_platform_driver(dumb_vga_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Dumb VGA DAC bridge driver");
MODULE_LICENSE("GPL");
