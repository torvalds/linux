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
#include <linux/of_gpio.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	int pd_gpio;

	struct videomode vm;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int tfp410_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (omapdss_device_is_connected(dssdev))
		return -EBUSY;

	r = in->ops.dpi->connect(in, dssdev);
	if (r)
		return r;

	dst->src = dssdev;
	dssdev->dst = dst;

	return 0;
}

static void tfp410_disconnect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	WARN_ON(!omapdss_device_is_connected(dssdev));
	if (!omapdss_device_is_connected(dssdev))
		return;

	WARN_ON(dst != dssdev->dst);
	if (dst != dssdev->dst)
		return;

	dst->src = NULL;
	dssdev->dst = NULL;

	in->ops.dpi->disconnect(in, &ddata->dssdev);
}

static int tfp410_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	in->ops.dpi->set_timings(in, &ddata->vm);

	r = in->ops.dpi->enable(in);
	if (r)
		return r;

	if (gpio_is_valid(ddata->pd_gpio))
		gpio_set_value_cansleep(ddata->pd_gpio, 1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void tfp410_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (gpio_is_valid(ddata->pd_gpio))
		gpio_set_value_cansleep(ddata->pd_gpio, 0);

	in->ops.dpi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void tfp410_fix_timings(struct videomode *vm)
{
	vm->flags |= DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_PIXDATA_POSEDGE |
		     DISPLAY_FLAGS_SYNC_POSEDGE;
}

static void tfp410_set_timings(struct omap_dss_device *dssdev,
			       struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	tfp410_fix_timings(vm);

	ddata->vm = *vm;
	dssdev->panel.vm = *vm;

	in->ops.dpi->set_timings(in, vm);
}

static void tfp410_get_timings(struct omap_dss_device *dssdev,
			       struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static int tfp410_check_timings(struct omap_dss_device *dssdev,
				struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	tfp410_fix_timings(vm);

	return in->ops.dpi->check_timings(in, vm);
}

static const struct omapdss_dvi_ops tfp410_dvi_ops = {
	.connect	= tfp410_connect,
	.disconnect	= tfp410_disconnect,

	.enable		= tfp410_enable,
	.disable	= tfp410_disable,

	.check_timings	= tfp410_check_timings,
	.set_timings	= tfp410_set_timings,
	.get_timings	= tfp410_get_timings,
};

static int tfp410_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct omap_dss_device *in;
	int gpio;

	gpio = of_get_named_gpio(node, "powerdown-gpios", 0);

	if (gpio_is_valid(gpio) || gpio == -ENOENT) {
		ddata->pd_gpio = gpio;
	} else {
		if (gpio != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to parse PD gpio\n");
		return gpio;
	}

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int tfp410_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	if (!pdev->dev.of_node)
		return -ENODEV;

	r = tfp410_probe_of(pdev);
	if (r)
		return r;

	if (gpio_is_valid(ddata->pd_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->pd_gpio,
				GPIOF_OUT_INIT_LOW, "tfp410 PD");
		if (r) {
			dev_err(&pdev->dev, "Failed to request PD GPIO %d\n",
					ddata->pd_gpio);
			goto err_gpio;
		}
	}

	dssdev = &ddata->dssdev;
	dssdev->ops.dvi = &tfp410_dvi_ops;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->output_type = OMAP_DISPLAY_TYPE_DVI;
	dssdev->owner = THIS_MODULE;
	dssdev->port_num = 1;

	r = omapdss_register_output(dssdev);
	if (r) {
		dev_err(&pdev->dev, "Failed to register output\n");
		goto err_reg;
	}

	return 0;
err_reg:
err_gpio:
	omap_dss_put_device(ddata->in);
	return r;
}

static int __exit tfp410_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_output(&ddata->dssdev);

	WARN_ON(omapdss_device_is_enabled(dssdev));
	if (omapdss_device_is_enabled(dssdev))
		tfp410_disable(dssdev);

	WARN_ON(omapdss_device_is_connected(dssdev));
	if (omapdss_device_is_connected(dssdev))
		tfp410_disconnect(dssdev, dssdev->dst);

	omap_dss_put_device(in);

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
