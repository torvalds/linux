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
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	int data_lines;

	struct omap_video_timings videomode;

	int resb_gpio;
	int ini_gpio;
	int mo_gpio;
	int lr_gpio;
	int ud_gpio;
};

static const struct omap_video_timings sharp_ls_timings = {
	.x_res = 480,
	.y_res = 640,

	.pixelclock	= 19200000,

	.hsw		= 2,
	.hfp		= 1,
	.hbp		= 28,

	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 1,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
	.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
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

	in->ops.dpi->set_data_lines(in, ddata->data_lines);
	in->ops.dpi->set_timings(in, &ddata->videomode);

	r = in->ops.dpi->enable(in);
	if (r)
		return r;

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	if (gpio_is_valid(ddata->resb_gpio))
		gpio_set_value_cansleep(ddata->resb_gpio, 1);

	if (gpio_is_valid(ddata->ini_gpio))
		gpio_set_value_cansleep(ddata->ini_gpio, 1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void sharp_ls_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (gpio_is_valid(ddata->ini_gpio))
		gpio_set_value_cansleep(ddata->ini_gpio, 0);

	if (gpio_is_valid(ddata->resb_gpio))
		gpio_set_value_cansleep(ddata->resb_gpio, 0);

	/* wait at least 5 vsyncs after disabling the LCD */

	msleep(100);

	in->ops.dpi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void sharp_ls_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->videomode = *timings;
	dssdev->panel.timings = *timings;

	in->ops.dpi->set_timings(in, timings);
}

static void sharp_ls_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*timings = ddata->videomode;
}

static int sharp_ls_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dpi->check_timings(in, timings);
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

static int sharp_ls_probe_pdata(struct platform_device *pdev)
{
	const struct panel_sharp_ls037v7dw01_platform_data *pdata;
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev, *in;

	pdata = dev_get_platdata(&pdev->dev);

	in = omap_dss_find_output(pdata->source);
	if (in == NULL) {
		dev_err(&pdev->dev, "failed to find video source '%s'\n",
				pdata->source);
		return -EPROBE_DEFER;
	}

	ddata->in = in;

	ddata->data_lines = pdata->data_lines;

	dssdev = &ddata->dssdev;
	dssdev->name = pdata->name;

	ddata->resb_gpio = pdata->resb_gpio;
	ddata->ini_gpio = pdata->ini_gpio;
	ddata->mo_gpio = pdata->mo_gpio;
	ddata->lr_gpio = pdata->lr_gpio;
	ddata->ud_gpio = pdata->ud_gpio;

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

	if (dev_get_platdata(&pdev->dev)) {
		r = sharp_ls_probe_pdata(pdev);
		if (r)
			return r;
	} else {
		return -ENODEV;
	}

	if (gpio_is_valid(ddata->mo_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->mo_gpio,
				GPIOF_OUT_INIT_LOW, "lcd MO");
		if (r)
			goto err_gpio;
	}

	if (gpio_is_valid(ddata->lr_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->lr_gpio,
				GPIOF_OUT_INIT_HIGH, "lcd LR");
		if (r)
			goto err_gpio;
	}

	if (gpio_is_valid(ddata->ud_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->ud_gpio,
				GPIOF_OUT_INIT_HIGH, "lcd UD");
		if (r)
			goto err_gpio;
	}

	if (gpio_is_valid(ddata->resb_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->resb_gpio,
				GPIOF_OUT_INIT_LOW, "lcd RESB");
		if (r)
			goto err_gpio;
	}

	if (gpio_is_valid(ddata->ini_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->ini_gpio,
				GPIOF_OUT_INIT_LOW, "lcd INI");
		if (r)
			goto err_gpio;
	}

	ddata->videomode = sharp_ls_timings;

	dssdev = &ddata->dssdev;
	dssdev->dev = &pdev->dev;
	dssdev->driver = &sharp_ls_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.timings = ddata->videomode;
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

static struct platform_driver sharp_ls_driver = {
	.probe = sharp_ls_probe,
	.remove = __exit_p(sharp_ls_remove),
	.driver = {
		.name = "panel-sharp-ls037v7dw01",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(sharp_ls_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Sharp LS037V7DW01 Panel Driver");
MODULE_LICENSE("GPL");
