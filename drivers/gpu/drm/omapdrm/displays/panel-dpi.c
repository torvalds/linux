/*
 * Generic MIPI DPI Panel Driver
 *
 * Copyright (C) 2013 Texas Instruments
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
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include <video/omap-panel-data.h>
#include <video/of_display_timing.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	int data_lines;

	struct videomode vm;

	/* used for non-DT boot, to be removed */
	int backlight_gpio;

	struct gpio_desc *enable_gpio;
	struct regulator *vcc_supply;
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

static int panel_dpi_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (omapdss_device_is_connected(dssdev))
		return 0;

	r = in->ops.dpi->connect(in, dssdev);
	if (r)
		return r;

	return 0;
}

static void panel_dpi_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dpi->disconnect(in, dssdev);
}

static int panel_dpi_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	if (ddata->data_lines)
		in->ops.dpi->set_data_lines(in, ddata->data_lines);
	in->ops.dpi->set_timings(in, &ddata->vm);

	r = in->ops.dpi->enable(in);
	if (r)
		return r;

	r = regulator_enable(ddata->vcc_supply);
	if (r) {
		in->ops.dpi->disable(in);
		return r;
	}

	gpiod_set_value_cansleep(ddata->enable_gpio, 1);

	if (gpio_is_valid(ddata->backlight_gpio))
		gpio_set_value_cansleep(ddata->backlight_gpio, 1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void panel_dpi_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (gpio_is_valid(ddata->backlight_gpio))
		gpio_set_value_cansleep(ddata->backlight_gpio, 0);

	gpiod_set_value_cansleep(ddata->enable_gpio, 0);
	regulator_disable(ddata->vcc_supply);

	in->ops.dpi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void panel_dpi_set_timings(struct omap_dss_device *dssdev,
				  struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->vm = *vm;
	dssdev->panel.vm = *vm;

	in->ops.dpi->set_timings(in, vm);
}

static void panel_dpi_get_timings(struct omap_dss_device *dssdev,
				  struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static int panel_dpi_check_timings(struct omap_dss_device *dssdev,
				   struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dpi->check_timings(in, vm);
}

static struct omap_dss_driver panel_dpi_ops = {
	.connect	= panel_dpi_connect,
	.disconnect	= panel_dpi_disconnect,

	.enable		= panel_dpi_enable,
	.disable	= panel_dpi_disable,

	.set_timings	= panel_dpi_set_timings,
	.get_timings	= panel_dpi_get_timings,
	.check_timings	= panel_dpi_check_timings,

	.get_resolution	= omapdss_default_get_resolution,
};

static int panel_dpi_probe_pdata(struct platform_device *pdev)
{
	const struct panel_dpi_platform_data *pdata;
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev, *in;
	int r;

	pdata = dev_get_platdata(&pdev->dev);

	in = omap_dss_find_output(pdata->source);
	if (in == NULL) {
		dev_err(&pdev->dev, "failed to find video source '%s'\n",
				pdata->source);
		return -EPROBE_DEFER;
	}

	ddata->in = in;

	ddata->data_lines = pdata->data_lines;

	videomode_from_timing(pdata->display_timing, &ddata->vm);

	dssdev = &ddata->dssdev;
	dssdev->name = pdata->name;

	r = devm_gpio_request_one(&pdev->dev, pdata->enable_gpio,
					GPIOF_OUT_INIT_LOW, "panel enable");
	if (r)
		goto err_gpio;

	ddata->enable_gpio = gpio_to_desc(pdata->enable_gpio);

	ddata->backlight_gpio = pdata->backlight_gpio;

	return 0;

err_gpio:
	omap_dss_put_device(ddata->in);
	return r;
}

static int panel_dpi_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct omap_dss_device *in;
	int r;
	struct display_timing timing;
	struct gpio_desc *gpio;

	gpio = devm_gpiod_get_optional(&pdev->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	ddata->enable_gpio = gpio;

	/*
	 * Many different panels are supported by this driver and there are
	 * probably very different needs for their reset pins in regards to
	 * timing and order relative to the enable gpio. So for now it's just
	 * ensured that the reset line isn't active.
	 */
	gpio = devm_gpiod_get_optional(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	ddata->vcc_supply = devm_regulator_get(&pdev->dev, "vcc");
	if (IS_ERR(ddata->vcc_supply))
		return PTR_ERR(ddata->vcc_supply);

	ddata->backlight_gpio = -ENOENT;

	r = of_get_display_timing(node, "panel-timing", &timing);
	if (r) {
		dev_err(&pdev->dev, "failed to get video timing\n");
		return r;
	}

	videomode_from_timing(&timing, &ddata->vm);

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int panel_dpi_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	if (dev_get_platdata(&pdev->dev)) {
		r = panel_dpi_probe_pdata(pdev);
		if (r)
			return r;
	} else if (pdev->dev.of_node) {
		r = panel_dpi_probe_of(pdev);
		if (r)
			return r;
	} else {
		return -ENODEV;
	}

	if (gpio_is_valid(ddata->backlight_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->backlight_gpio,
				GPIOF_OUT_INIT_LOW, "panel backlight");
		if (r)
			goto err_gpio;
	}

	dssdev = &ddata->dssdev;
	dssdev->dev = &pdev->dev;
	dssdev->driver = &panel_dpi_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.vm = ddata->vm;
	dssdev->phy.dpi.data_lines = ddata->data_lines;

	r = omapdss_register_display(dssdev);
	if (r) {
		dev_err(&pdev->dev, "Failed to register panel\n");
		goto err_reg;
	}

	return 0;

err_reg:
err_gpio:
	omap_dss_put_device(ddata->in);
	return r;
}

static int __exit panel_dpi_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_display(dssdev);

	panel_dpi_disable(dssdev);
	panel_dpi_disconnect(dssdev);

	omap_dss_put_device(in);

	return 0;
}

static const struct of_device_id panel_dpi_of_match[] = {
	{ .compatible = "omapdss,panel-dpi", },
	{},
};

MODULE_DEVICE_TABLE(of, panel_dpi_of_match);

static struct platform_driver panel_dpi_driver = {
	.probe = panel_dpi_probe,
	.remove = __exit_p(panel_dpi_remove),
	.driver = {
		.name = "panel-dpi",
		.of_match_table = panel_dpi_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(panel_dpi_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Generic MIPI DPI Panel Driver");
MODULE_LICENSE("GPL");
