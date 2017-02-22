/*
 * Copyright (C) 2016 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

struct tfp410 {
	struct drm_bridge	bridge;
	struct drm_connector	connector;

	struct i2c_adapter	*ddc;

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
	struct edid *edid;
	int ret;

	if (!dvi->ddc)
		goto fallback;

	edid = drm_get_edid(connector, dvi->ddc);
	if (!edid) {
		DRM_INFO("EDID read failed. Fallback to standard modes\n");
		goto fallback;
	}

	drm_mode_connector_update_edid_property(connector, edid);

	return drm_add_edid_modes(connector, edid);
fallback:
	/* No EDID, fallback on the XGA standard modes */
	ret = drm_add_modes_noedid(connector, 1920, 1200);

	/* And prefer a mode pretty much anything can handle */
	drm_set_preferred_mode(connector, 1024, 768);

	return ret;
}

static const struct drm_connector_helper_funcs tfp410_con_helper_funcs = {
	.get_modes	= tfp410_get_modes,
};

static enum drm_connector_status
tfp410_connector_detect(struct drm_connector *connector, bool force)
{
	struct tfp410 *dvi = drm_connector_to_tfp410(connector);

	if (dvi->ddc) {
		if (drm_probe_ddc(dvi->ddc))
			return connector_status_connected;
		else
			return connector_status_disconnected;
	}

	return connector_status_unknown;
}

static const struct drm_connector_funcs tfp410_con_funcs = {
	.dpms			= drm_atomic_helper_connector_dpms,
	.detect			= tfp410_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int tfp410_attach(struct drm_bridge *bridge)
{
	struct tfp410 *dvi = drm_bridge_to_tfp410(bridge);
	int ret;

	if (!bridge->encoder) {
		dev_err(dvi->dev, "Missing encoder\n");
		return -ENODEV;
	}

	drm_connector_helper_add(&dvi->connector,
				 &tfp410_con_helper_funcs);
	ret = drm_connector_init(bridge->dev, &dvi->connector,
				 &tfp410_con_funcs, DRM_MODE_CONNECTOR_HDMIA);
	if (ret) {
		dev_err(dvi->dev, "drm_connector_init() failed: %d\n", ret);
		return ret;
	}

	drm_mode_connector_attach_encoder(&dvi->connector,
					  bridge->encoder);

	return 0;
}

static const struct drm_bridge_funcs tfp410_bridge_funcs = {
	.attach		= tfp410_attach,
};

static int tfp410_get_connector_ddc(struct tfp410 *dvi)
{
	struct device_node *ep = NULL, *connector_node = NULL;
	struct device_node *ddc_phandle = NULL;
	int ret = 0;

	/* port@1 is the connector node */
	ep = of_graph_get_endpoint_by_regs(dvi->dev->of_node, 1, -1);
	if (!ep)
		goto fail;

	connector_node = of_graph_get_remote_port_parent(ep);
	if (!connector_node)
		goto fail;

	ddc_phandle = of_parse_phandle(connector_node, "ddc-i2c-bus", 0);
	if (!ddc_phandle)
		goto fail;

	dvi->ddc = of_get_i2c_adapter_by_node(ddc_phandle);
	if (dvi->ddc)
		dev_info(dvi->dev, "Connector's ddc i2c bus found\n");
	else
		ret = -EPROBE_DEFER;

fail:
	of_node_put(ep);
	of_node_put(connector_node);
	of_node_put(ddc_phandle);
	return ret;
}

static int tfp410_init(struct device *dev)
{
	struct tfp410 *dvi;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device-tree data is missing\n");
		return -ENXIO;
	}

	dvi = devm_kzalloc(dev, sizeof(*dvi), GFP_KERNEL);
	if (!dvi)
		return -ENOMEM;
	dev_set_drvdata(dev, dvi);

	dvi->bridge.funcs = &tfp410_bridge_funcs;
	dvi->bridge.of_node = dev->of_node;
	dvi->dev = dev;

	ret = tfp410_get_connector_ddc(dvi);
	if (ret)
		goto fail;

	ret = drm_bridge_add(&dvi->bridge);
	if (ret) {
		dev_err(dev, "drm_bridge_add() failed: %d\n", ret);
		goto fail;
	}

	return 0;
fail:
	i2c_put_adapter(dvi->ddc);
	return ret;
}

static int tfp410_fini(struct device *dev)
{
	struct tfp410 *dvi = dev_get_drvdata(dev);

	drm_bridge_remove(&dvi->bridge);

	if (dvi->ddc)
		i2c_put_adapter(dvi->ddc);

	return 0;
}

static int tfp410_probe(struct platform_device *pdev)
{
	return tfp410_init(&pdev->dev);
}

static int tfp410_remove(struct platform_device *pdev)
{
	return tfp410_fini(&pdev->dev);
}

static const struct of_device_id tfp410_match[] = {
	{ .compatible = "ti,tfp410" },
	{},
};
MODULE_DEVICE_TABLE(of, tfp410_match);

struct platform_driver tfp410_platform_driver = {
	.probe	= tfp410_probe,
	.remove	= tfp410_remove,
	.driver	= {
		.name		= "tfp410-bridge",
		.of_match_table	= tfp410_match,
	},
};

#if IS_ENABLED(CONFIG_I2C)
/* There is currently no i2c functionality. */
static int tfp410_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int reg;

	if (!client->dev.of_node ||
	    of_property_read_u32(client->dev.of_node, "reg", &reg)) {
		dev_err(&client->dev,
			"Can't get i2c reg property from device-tree\n");
		return -ENXIO;
	}

	return tfp410_init(&client->dev);
}

static int tfp410_i2c_remove(struct i2c_client *client)
{
	return tfp410_fini(&client->dev);
}

static const struct i2c_device_id tfp410_i2c_ids[] = {
	{ "tfp410", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tfp410_i2c_ids);

static struct i2c_driver tfp410_i2c_driver = {
	.driver = {
		.name	= "tfp410",
		.of_match_table = of_match_ptr(tfp410_match),
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
