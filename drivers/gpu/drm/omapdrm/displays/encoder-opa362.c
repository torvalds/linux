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

	struct videomode vm;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int opa362_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct omap_dss_device *src;
	int r;

	src = omapdss_of_find_source_for_first_ep(dssdev->dev->of_node);
	if (IS_ERR(src)) {
		dev_err(dssdev->dev, "failed to find video source\n");
		return PTR_ERR(src);
	}

	r = omapdss_device_connect(src, dssdev);
	if (r) {
		omapdss_device_put(src);
		return r;
	}

	return 0;
}

static void opa362_disconnect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	omapdss_device_disconnect(src, &ddata->dssdev);

	omapdss_device_put(src);
}

static int opa362_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;
	int r;

	dev_dbg(dssdev->dev, "enable\n");

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	src->ops->set_timings(src, &ddata->vm);

	r = src->ops->enable(src);
	if (r)
		return r;

	if (ddata->enable_gpio)
		gpiod_set_value_cansleep(ddata->enable_gpio, 1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void opa362_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	dev_dbg(dssdev->dev, "disable\n");

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (ddata->enable_gpio)
		gpiod_set_value_cansleep(ddata->enable_gpio, 0);

	src->ops->disable(src);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void opa362_set_timings(struct omap_dss_device *dssdev,
			       struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	dev_dbg(dssdev->dev, "set_timings\n");

	ddata->vm = *vm;

	src->ops->set_timings(src, vm);
}

static int opa362_check_timings(struct omap_dss_device *dssdev,
				struct videomode *vm)
{
	struct omap_dss_device *src = dssdev->src;

	dev_dbg(dssdev->dev, "check_timings\n");

	return src->ops->check_timings(src, vm);
}

static const struct omap_dss_device_ops opa362_ops = {
	.connect	= opa362_connect,
	.disconnect	= opa362_disconnect,
	.enable		= opa362_enable,
	.disable	= opa362_disable,
	.check_timings	= opa362_check_timings,
	.set_timings	= opa362_set_timings,
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
	dssdev->output_type = OMAP_DISPLAY_TYPE_VENC;
	dssdev->owner = THIS_MODULE;

	omapdss_device_register(dssdev);

	return 0;
}

static int __exit opa362_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_device_unregister(&ddata->dssdev);

	WARN_ON(omapdss_device_is_enabled(dssdev));
	if (omapdss_device_is_enabled(dssdev))
		opa362_disable(dssdev);

	WARN_ON(omapdss_device_is_connected(dssdev));
	if (omapdss_device_is_connected(dssdev))
		omapdss_device_disconnect(dssdev, dssdev->dst);

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
