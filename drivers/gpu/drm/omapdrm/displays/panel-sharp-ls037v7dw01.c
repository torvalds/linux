/*
 * LCD panel driver for Sharp LS037V7DW01
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;
	struct regulator *vcc;

	int data_lines;

	struct videomode vm;

	struct gpio_desc *resb_gpio;	/* low = reset active min 20 us */
	struct gpio_desc *ini_gpio;	/* high = power on */
	struct gpio_desc *mo_gpio;	/* low = 480x640, high = 240x320 */
	struct gpio_desc *lr_gpio;	/* high = conventional horizontal scanning */
	struct gpio_desc *ud_gpio;	/* high = conventional vertical scanning */
};

static const struct videomode sharp_ls_vm = {
	.hactive = 480,
	.vactive = 640,

	.pixelclock	= 19200000,

	.hsync_len	= 2,
	.hfront_porch	= 1,
	.hback_porch	= 28,

	.vsync_len	= 1,
	.vfront_porch	= 1,
	.vback_porch	= 1,

	.flags		= DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
			  DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_SYNC_NEGEDGE |
			  DISPLAY_FLAGS_PIXDATA_POSEDGE,
	/*
	 * Note: According to the panel documentation:
	 * DATA needs to be driven on the FALLING edge
	 */
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

static int sharp_ls_connect(struct omap_dss_device *dssdev)
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

static void sharp_ls_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dpi->disconnect(in, dssdev);
}

static int sharp_ls_enable(struct omap_dss_device *dssdev)
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

	if (ddata->vcc) {
		r = regulator_enable(ddata->vcc);
		if (r != 0)
			return r;
	}

	r = in->ops.dpi->enable(in);
	if (r) {
		regulator_disable(ddata->vcc);
		return r;
	}

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	if (ddata->resb_gpio)
		gpiod_set_value_cansleep(ddata->resb_gpio, 1);

	if (ddata->ini_gpio)
		gpiod_set_value_cansleep(ddata->ini_gpio, 1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void sharp_ls_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (ddata->ini_gpio)
		gpiod_set_value_cansleep(ddata->ini_gpio, 0);

	if (ddata->resb_gpio)
		gpiod_set_value_cansleep(ddata->resb_gpio, 0);

	/* wait at least 5 vsyncs after disabling the LCD */

	msleep(100);

	in->ops.dpi->disable(in);

	if (ddata->vcc)
		regulator_disable(ddata->vcc);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void sharp_ls_set_timings(struct omap_dss_device *dssdev,
				 struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->vm = *vm;
	dssdev->panel.vm = *vm;

	in->ops.dpi->set_timings(in, vm);
}

static void sharp_ls_get_timings(struct omap_dss_device *dssdev,
				 struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static int sharp_ls_check_timings(struct omap_dss_device *dssdev,
				  struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dpi->check_timings(in, vm);
}

static struct omap_dss_driver sharp_ls_ops = {
	.connect	= sharp_ls_connect,
	.disconnect	= sharp_ls_disconnect,

	.enable		= sharp_ls_enable,
	.disable	= sharp_ls_disable,

	.set_timings	= sharp_ls_set_timings,
	.get_timings	= sharp_ls_get_timings,
	.check_timings	= sharp_ls_check_timings,

	.get_resolution	= omapdss_default_get_resolution,
};

static  int sharp_ls_get_gpio_of(struct device *dev, int index, int val,
	const char *desc, struct gpio_desc **gpiod)
{
	struct gpio_desc *gd;

	*gpiod = NULL;

	gd = devm_gpiod_get_index(dev, desc, index, GPIOD_OUT_LOW);
	if (IS_ERR(gd))
		return PTR_ERR(gd);

	*gpiod = gd;
	return 0;
}

static int sharp_ls_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct omap_dss_device *in;
	int r;

	ddata->vcc = devm_regulator_get(&pdev->dev, "envdd");
	if (IS_ERR(ddata->vcc)) {
		dev_err(&pdev->dev, "failed to get regulator\n");
		return PTR_ERR(ddata->vcc);
	}

	/* lcd INI */
	r = sharp_ls_get_gpio_of(&pdev->dev, 0, 0, "enable", &ddata->ini_gpio);
	if (r)
		return r;

	/* lcd RESB */
	r = sharp_ls_get_gpio_of(&pdev->dev, 0, 0, "reset", &ddata->resb_gpio);
	if (r)
		return r;

	/* lcd MO */
	r = sharp_ls_get_gpio_of(&pdev->dev, 0, 0, "mode", &ddata->mo_gpio);
	if (r)
		return r;

	/* lcd LR */
	r = sharp_ls_get_gpio_of(&pdev->dev, 1, 1, "mode", &ddata->lr_gpio);
	if (r)
		return r;

	/* lcd UD */
	r = sharp_ls_get_gpio_of(&pdev->dev, 2, 1, "mode", &ddata->ud_gpio);
	if (r)
		return r;

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int sharp_ls_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	if (!pdev->dev.of_node)
		return -ENODEV;

	r = sharp_ls_probe_of(pdev);
	if (r)
		return r;

	ddata->vm = sharp_ls_vm;

	dssdev = &ddata->dssdev;
	dssdev->dev = &pdev->dev;
	dssdev->driver = &sharp_ls_ops;
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
	omap_dss_put_device(ddata->in);
	return r;
}

static int __exit sharp_ls_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_display(dssdev);

	sharp_ls_disable(dssdev);
	sharp_ls_disconnect(dssdev);

	omap_dss_put_device(in);

	return 0;
}

static const struct of_device_id sharp_ls_of_match[] = {
	{ .compatible = "omapdss,sharp,ls037v7dw01", },
	{},
};

MODULE_DEVICE_TABLE(of, sharp_ls_of_match);

static struct platform_driver sharp_ls_driver = {
	.probe = sharp_ls_probe,
	.remove = __exit_p(sharp_ls_remove),
	.driver = {
		.name = "panel-sharp-ls037v7dw01",
		.of_match_table = sharp_ls_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(sharp_ls_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Sharp LS037V7DW01 Panel Driver");
MODULE_LICENSE("GPL");
