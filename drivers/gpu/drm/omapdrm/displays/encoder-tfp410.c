/*
 * TFP410 DPI-to-DVI encoder driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;

	struct gpio_desc *pd_gpio;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int tfp410_connect(struct omap_dss_device *src,
			  struct omap_dss_device *dst)
{
	return omapdss_device_connect(dst->dss, dst, dst->next);
}

static void tfp410_disconnect(struct omap_dss_device *src,
			      struct omap_dss_device *dst)
{
	omapdss_device_disconnect(dst, dst->next);
}

static int tfp410_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;
	int r;

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	r = src->ops->enable(src);
	if (r)
		return r;

	if (ddata->pd_gpio)
		gpiod_set_value_cansleep(ddata->pd_gpio, 0);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void tfp410_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (ddata->pd_gpio)
		gpiod_set_value_cansleep(ddata->pd_gpio, 0);

	src->ops->disable(src);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static const struct omap_dss_device_ops tfp410_ops = {
	.connect	= tfp410_connect,
	.disconnect	= tfp410_disconnect,
	.enable		= tfp410_enable,
	.disable	= tfp410_disable,
};

static int tfp410_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	struct gpio_desc *gpio;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	/* Powerdown GPIO */
	gpio = devm_gpiod_get_optional(&pdev->dev, "powerdown", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to parse powerdown gpio\n");
		return PTR_ERR(gpio);
	}

	ddata->pd_gpio = gpio;

	dssdev = &ddata->dssdev;
	dssdev->ops = &tfp410_ops;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->output_type = OMAP_DISPLAY_TYPE_DVI;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(1) | BIT(0);
	dssdev->bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_SYNC_POSEDGE
			  | DRM_BUS_FLAG_PIXDATA_POSEDGE;

	dssdev->next = omapdss_of_find_connected_device(pdev->dev.of_node, 1);
	if (IS_ERR(dssdev->next)) {
		if (PTR_ERR(dssdev->next) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to find video sink\n");
		return PTR_ERR(dssdev->next);
	}

	omapdss_device_register(dssdev);

	return 0;
}

static int __exit tfp410_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	if (dssdev->next)
		omapdss_device_put(dssdev->next);
	omapdss_device_unregister(&ddata->dssdev);

	WARN_ON(omapdss_device_is_enabled(dssdev));
	if (omapdss_device_is_enabled(dssdev))
		tfp410_disable(dssdev);

	WARN_ON(omapdss_device_is_connected(dssdev));
	if (omapdss_device_is_connected(dssdev))
		omapdss_device_disconnect(NULL, dssdev);

	return 0;
}

static const struct of_device_id tfp410_of_match[] = {
	{ .compatible = "omapdss,ti,tfp410", },
	{},
};

MODULE_DEVICE_TABLE(of, tfp410_of_match);

static struct platform_driver tfp410_driver = {
	.probe	= tfp410_probe,
	.remove	= __exit_p(tfp410_remove),
	.driver	= {
		.name	= "tfp410",
		.of_match_table = tfp410_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(tfp410_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("TFP410 DPI to DVI encoder driver");
MODULE_LICENSE("GPL");
