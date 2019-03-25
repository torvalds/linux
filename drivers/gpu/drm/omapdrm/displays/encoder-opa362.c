/*
 * OPA362 analog video amplifier with output/power control
 *
 * Copyright (C) 2014 Golden Delicious Computers
 * Author: H. Nikolaus Schaller <hns@goldelico.com>
 *
 * based on encoder-tfp410
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

	struct gpio_desc *enable_gpio;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int opa362_connect(struct omap_dss_device *src,
			  struct omap_dss_device *dst)
{
	return omapdss_device_connect(dst->dss, dst, dst->next);
}

static void opa362_disconnect(struct omap_dss_device *src,
			      struct omap_dss_device *dst)
{
	omapdss_device_disconnect(dst, dst->next);
}

static void opa362_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (ddata->enable_gpio)
		gpiod_set_value_cansleep(ddata->enable_gpio, 1);
}

static void opa362_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (ddata->enable_gpio)
		gpiod_set_value_cansleep(ddata->enable_gpio, 0);
}

static const struct omap_dss_device_ops opa362_ops = {
	.connect	= opa362_connect,
	.disconnect	= opa362_disconnect,
	.enable		= opa362_enable,
	.disable	= opa362_disable,
};

static int opa362_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	struct gpio_desc *gpio;

	dev_dbg(&pdev->dev, "probe\n");

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	gpio = devm_gpiod_get_optional(&pdev->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	ddata->enable_gpio = gpio;

	dssdev = &ddata->dssdev;
	dssdev->ops = &opa362_ops;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_VENC;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(1) | BIT(0);

	dssdev->next = omapdss_of_find_connected_device(pdev->dev.of_node, 1);
	if (IS_ERR(dssdev->next)) {
		if (PTR_ERR(dssdev->next) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to find video sink\n");
		return PTR_ERR(dssdev->next);
	}

	omapdss_device_register(dssdev);

	return 0;
}

static int __exit opa362_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	if (dssdev->next)
		omapdss_device_put(dssdev->next);
	omapdss_device_unregister(&ddata->dssdev);

	opa362_disable(dssdev);

	return 0;
}

static const struct of_device_id opa362_of_match[] = {
	{ .compatible = "omapdss,ti,opa362", },
	{},
};
MODULE_DEVICE_TABLE(of, opa362_of_match);

static struct platform_driver opa362_driver = {
	.probe	= opa362_probe,
	.remove	= __exit_p(opa362_remove),
	.driver	= {
		.name	= "amplifier-opa362",
		.of_match_table = opa362_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(opa362_driver);

MODULE_AUTHOR("H. Nikolaus Schaller <hns@goldelico.com>");
MODULE_DESCRIPTION("OPA362 analog video amplifier with output/power control");
MODULE_LICENSE("GPL v2");
