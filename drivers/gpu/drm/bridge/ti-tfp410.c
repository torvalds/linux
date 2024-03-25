// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#define HOTPLUG_DEBOUNCE_MS		1100

struct tfp410 {
	struct drm_bridge	bridge;
	struct drm_connector	connector;

	u32			bus_format;
	struct delayed_work	hpd_work;
	struct gpio_desc	*powerdown;

	struct drm_bridge_timings timings;
	struct drm_bridge	*next_bridge;

	struct device *dev;
};

static inline struct tfp410 *
drm_bridge_to_tfp410(struct drm_bridge *bridge)
{
	return container_of(bridge, struct tfp410, bridge);
}

static inline struct tfp410 *
drm_connector_to_tfp410(struct drm_connector *connector)
{
	return container_of(connector, struct tfp410, connector);
}

static int tfp410_get_modes(struct drm_connector *connector)
{
	struct tfp410 *dvi = drm_connector_to_tfp410(connector);
	const struct drm_edid *drm_edid;
	int ret;

	if (dvi->next_bridge->ops & DRM_BRIDGE_OP_EDID) {
		drm_edid = drm_bridge_edid_read(dvi->next_bridge, connector);
		if (!drm_edid)
			DRM_INFO("EDID read failed. Fallback to standard modes\n");
	} else {
		drm_edid = NULL;
	}

	drm_edid_connector_update(connector, drm_edid);

	if (!drm_edid) {
		/*
		 * No EDID, fallback on the XGA standard modes and prefer a mode
		 * pretty much anything can handle.
		 */
		ret = drm_add_modes_noedid(connector, 1920, 1200);
		drm_set_preferred_mode(connector, 1024, 768);
		return ret;
	}

	ret = drm_edid_connector_add_modes(connector);

	drm_edid_free(drm_edid);

	return ret;
}

static const struct drm_connector_helper_funcs tfp410_con_helper_funcs = {
	.get_modes	= tfp410_get_modes,
};

static enum drm_connector_status
tfp410_connector_detect(struct drm_connector *connector, bool force)
{
	struct tfp410 *dvi = drm_connector_to_tfp410(connector);

	return drm_bridge_detect(dvi->next_bridge);
}

static const struct drm_connector_funcs tfp410_con_funcs = {
	.detect			= tfp410_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static void tfp410_hpd_work_func(struct work_struct *work)
{
	struct tfp410 *dvi;

	dvi = container_of(work, struct tfp410, hpd_work.work);

	if (dvi->bridge.dev)
		drm_helper_hpd_irq_event(dvi->bridge.dev);
}

static void tfp410_hpd_callback(void *arg, enum drm_connector_status status)
{
	struct tfp410 *dvi = arg;

	mod_delayed_work(system_wq, &dvi->hpd_work,
			 msecs_to_jiffies(HOTPLUG_DEBOUNCE_MS));
}

static int tfp410_attach(struct drm_bridge *bridge,
			 enum drm_bridge_attach_flags flags)
{
	struct tfp410 *dvi = drm_bridge_to_tfp410(bridge);
	int ret;

	ret = drm_bridge_attach(bridge->encoder, dvi->next_bridge, bridge,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret < 0)
		return ret;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	if (!bridge->encoder) {
		dev_err(dvi->dev, "Missing encoder\n");
		return -ENODEV;
	}

	if (dvi->next_bridge->ops & DRM_BRIDGE_OP_DETECT)
		dvi->connector.polled = DRM_CONNECTOR_POLL_HPD;
	else
		dvi->connector.polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;

	if (dvi->next_bridge->ops & DRM_BRIDGE_OP_HPD) {
		INIT_DELAYED_WORK(&dvi->hpd_work, tfp410_hpd_work_func);
		drm_bridge_hpd_enable(dvi->next_bridge, tfp410_hpd_callback,
				      dvi);
	}

	drm_connector_helper_add(&dvi->connector,
				 &tfp410_con_helper_funcs);
	ret = drm_connector_init_with_ddc(bridge->dev, &dvi->connector,
					  &tfp410_con_funcs,
					  dvi->next_bridge->type,
					  dvi->next_bridge->ddc);
	if (ret) {
		dev_err(dvi->dev, "drm_connector_init_with_ddc() failed: %d\n",
			ret);
		return ret;
	}

	drm_display_info_set_bus_formats(&dvi->connector.display_info,
					 &dvi->bus_format, 1);

	drm_connector_attach_encoder(&dvi->connector, bridge->encoder);

	return 0;
}

static void tfp410_detach(struct drm_bridge *bridge)
{
	struct tfp410 *dvi = drm_bridge_to_tfp410(bridge);

	if (dvi->connector.dev && dvi->next_bridge->ops & DRM_BRIDGE_OP_HPD) {
		drm_bridge_hpd_disable(dvi->next_bridge);
		cancel_delayed_work_sync(&dvi->hpd_work);
	}
}

static void tfp410_enable(struct drm_bridge *bridge)
{
	struct tfp410 *dvi = drm_bridge_to_tfp410(bridge);

	gpiod_set_value_cansleep(dvi->powerdown, 0);
}

static void tfp410_disable(struct drm_bridge *bridge)
{
	struct tfp410 *dvi = drm_bridge_to_tfp410(bridge);

	gpiod_set_value_cansleep(dvi->powerdown, 1);
}

static enum drm_mode_status tfp410_mode_valid(struct drm_bridge *bridge,
					      const struct drm_display_info *info,
					      const struct drm_display_mode *mode)
{
	if (mode->clock < 25000)
		return MODE_CLOCK_LOW;

	if (mode->clock > 165000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static u32 *tfp410_get_input_bus_fmts(struct drm_bridge *bridge,
				      struct drm_bridge_state *bridge_state,
				      struct drm_crtc_state *crtc_state,
				      struct drm_connector_state *conn_state,
				      u32 output_fmt,
				      unsigned int *num_input_fmts)
{
	struct tfp410 *dvi = drm_bridge_to_tfp410(bridge);
	u32 *input_fmts;

	*num_input_fmts = 0;

	input_fmts = kzalloc(sizeof(*input_fmts), GFP_KERNEL);
	if (!input_fmts)
		return NULL;

	*num_input_fmts = 1;
	input_fmts[0] = dvi->bus_format;

	return input_fmts;
}

static int tfp410_atomic_check(struct drm_bridge *bridge,
			       struct drm_bridge_state *bridge_state,
			       struct drm_crtc_state *crtc_state,
			       struct drm_connector_state *conn_state)
{
	struct tfp410 *dvi = drm_bridge_to_tfp410(bridge);

	/*
	 * There might be flags negotiation supported in future.
	 * Set the bus flags in atomic_check statically for now.
	 */
	bridge_state->input_bus_cfg.flags = dvi->timings.input_bus_flags;

	return 0;
}

static const struct drm_bridge_funcs tfp410_bridge_funcs = {
	.attach		= tfp410_attach,
	.detach		= tfp410_detach,
	.enable		= tfp410_enable,
	.disable	= tfp410_disable,
	.mode_valid	= tfp410_mode_valid,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_get_input_bus_fmts = tfp410_get_input_bus_fmts,
	.atomic_check = tfp410_atomic_check,
};

static const struct drm_bridge_timings tfp410_default_timings = {
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE
			 | DRM_BUS_FLAG_DE_HIGH,
	.setup_time_ps = 1200,
	.hold_time_ps = 1300,
};

static int tfp410_parse_timings(struct tfp410 *dvi, bool i2c)
{
	struct drm_bridge_timings *timings = &dvi->timings;
	struct device_node *ep;
	u32 pclk_sample = 0;
	u32 bus_width = 24;
	u32 deskew = 0;

	/* Start with defaults. */
	*timings = tfp410_default_timings;

	if (i2c)
		/*
		 * In I2C mode timings are configured through the I2C interface.
		 * As the driver doesn't support I2C configuration yet, we just
		 * go with the defaults (BSEL=1, DSEL=1, DKEN=0, EDGE=1).
		 */
		return 0;

	/*
	 * In non-I2C mode, timings are configured through the BSEL, DSEL, DKEN
	 * and EDGE pins. They are specified in DT through endpoint properties
	 * and vendor-specific properties.
	 */
	ep = of_graph_get_endpoint_by_regs(dvi->dev->of_node, 0, 0);
	if (!ep)
		return -EINVAL;

	/* Get the sampling edge from the endpoint. */
	of_property_read_u32(ep, "pclk-sample", &pclk_sample);
	of_property_read_u32(ep, "bus-width", &bus_width);
	of_node_put(ep);

	timings->input_bus_flags = DRM_BUS_FLAG_DE_HIGH;

	switch (pclk_sample) {
	case 0:
		timings->input_bus_flags |= DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE
					 |  DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE;
		break;
	case 1:
		timings->input_bus_flags |= DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE
					 |  DRM_BUS_FLAG_SYNC_SAMPLE_POSEDGE;
		break;
	default:
		return -EINVAL;
	}

	switch (bus_width) {
	case 12:
		dvi->bus_format = MEDIA_BUS_FMT_RGB888_2X12_LE;
		break;
	case 24:
		dvi->bus_format = MEDIA_BUS_FMT_RGB888_1X24;
		break;
	default:
		return -EINVAL;
	}

	/* Get the setup and hold time from vendor-specific properties. */
	of_property_read_u32(dvi->dev->of_node, "ti,deskew", &deskew);
	if (deskew > 7)
		return -EINVAL;

	timings->setup_time_ps = 1200 - 350 * ((s32)deskew - 4);
	timings->hold_time_ps = max(0, 1300 + 350 * ((s32)deskew - 4));

	return 0;
}

static int tfp410_init(struct device *dev, bool i2c)
{
	struct device_node *node;
	struct tfp410 *dvi;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device-tree data is missing\n");
		return -ENXIO;
	}

	dvi = devm_kzalloc(dev, sizeof(*dvi), GFP_KERNEL);
	if (!dvi)
		return -ENOMEM;

	dvi->dev = dev;
	dev_set_drvdata(dev, dvi);

	dvi->bridge.funcs = &tfp410_bridge_funcs;
	dvi->bridge.of_node = dev->of_node;
	dvi->bridge.timings = &dvi->timings;
	dvi->bridge.type = DRM_MODE_CONNECTOR_DVID;

	ret = tfp410_parse_timings(dvi, i2c);
	if (ret)
		return ret;

	/* Get the next bridge, connected to port@1. */
	node = of_graph_get_remote_node(dev->of_node, 1, -1);
	if (!node)
		return -ENODEV;

	dvi->next_bridge = of_drm_find_bridge(node);
	of_node_put(node);

	if (!dvi->next_bridge)
		return -EPROBE_DEFER;

	/* Get the powerdown GPIO. */
	dvi->powerdown = devm_gpiod_get_optional(dev, "powerdown",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(dvi->powerdown)) {
		dev_err(dev, "failed to parse powerdown gpio\n");
		return PTR_ERR(dvi->powerdown);
	}

	/*  Register the DRM bridge. */
	drm_bridge_add(&dvi->bridge);

	return 0;
}

static void tfp410_fini(struct device *dev)
{
	struct tfp410 *dvi = dev_get_drvdata(dev);

	drm_bridge_remove(&dvi->bridge);
}

static int tfp410_probe(struct platform_device *pdev)
{
	return tfp410_init(&pdev->dev, false);
}

static void tfp410_remove(struct platform_device *pdev)
{
	tfp410_fini(&pdev->dev);
}

static const struct of_device_id tfp410_match[] = {
	{ .compatible = "ti,tfp410" },
	{},
};
MODULE_DEVICE_TABLE(of, tfp410_match);

static struct platform_driver tfp410_platform_driver = {
	.probe	= tfp410_probe,
	.remove_new = tfp410_remove,
	.driver	= {
		.name		= "tfp410-bridge",
		.of_match_table	= tfp410_match,
	},
};

#if IS_ENABLED(CONFIG_I2C)
/* There is currently no i2c functionality. */
static int tfp410_i2c_probe(struct i2c_client *client)
{
	int reg;

	if (!client->dev.of_node ||
	    of_property_read_u32(client->dev.of_node, "reg", &reg)) {
		dev_err(&client->dev,
			"Can't get i2c reg property from device-tree\n");
		return -ENXIO;
	}

	return tfp410_init(&client->dev, true);
}

static void tfp410_i2c_remove(struct i2c_client *client)
{
	tfp410_fini(&client->dev);
}

static const struct i2c_device_id tfp410_i2c_ids[] = {
	{ "tfp410", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tfp410_i2c_ids);

static struct i2c_driver tfp410_i2c_driver = {
	.driver = {
		.name	= "tfp410",
		.of_match_table = tfp410_match,
	},
	.id_table	= tfp410_i2c_ids,
	.probe		= tfp410_i2c_probe,
	.remove		= tfp410_i2c_remove,
};
#endif /* IS_ENABLED(CONFIG_I2C) */

static struct {
	uint i2c:1;
	uint platform:1;
}  tfp410_registered_driver;

static int __init tfp410_module_init(void)
{
	int ret;

#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&tfp410_i2c_driver);
	if (ret)
		pr_err("%s: registering i2c driver failed: %d",
		       __func__, ret);
	else
		tfp410_registered_driver.i2c = 1;
#endif

	ret = platform_driver_register(&tfp410_platform_driver);
	if (ret)
		pr_err("%s: registering platform driver failed: %d",
		       __func__, ret);
	else
		tfp410_registered_driver.platform = 1;

	if (tfp410_registered_driver.i2c ||
	    tfp410_registered_driver.platform)
		return 0;

	return ret;
}
module_init(tfp410_module_init);

static void __exit tfp410_module_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	if (tfp410_registered_driver.i2c)
		i2c_del_driver(&tfp410_i2c_driver);
#endif
	if (tfp410_registered_driver.platform)
		platform_driver_unregister(&tfp410_platform_driver);
}
module_exit(tfp410_module_exit);

MODULE_AUTHOR("Jyri Sarha <jsarha@ti.com>");
MODULE_DESCRIPTION("TI TFP410 DVI bridge driver");
MODULE_LICENSE("GPL");
