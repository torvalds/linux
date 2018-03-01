/*
 * Analog TV Connector driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;

	struct device *dev;

	struct videomode vm;
};

static const struct videomode tvc_pal_vm = {
	.hactive	= 720,
	.vactive	= 574,
	.pixelclock	= 13500000,
	.hsync_len	= 64,
	.hfront_porch	= 12,
	.hback_porch	= 68,
	.vsync_len	= 5,
	.vfront_porch	= 5,
	.vback_porch	= 41,

	.flags		= DISPLAY_FLAGS_INTERLACED | DISPLAY_FLAGS_HSYNC_LOW |
			  DISPLAY_FLAGS_VSYNC_LOW,
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int tvc_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src;
	int r;

	src = omapdss_of_find_source_for_first_ep(ddata->dev->of_node);
	if (IS_ERR(src)) {
		dev_err(ddata->dev, "failed to find video source\n");
		return PTR_ERR(src);
	}

	r = omapdss_device_connect(src, dssdev);
	if (r) {
		omap_dss_put_device(src);
		return r;
	}

	return 0;
}

static void tvc_disconnect(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *src = dssdev->src;

	omapdss_device_disconnect(src, dssdev);

	omap_dss_put_device(src);
}

static int tvc_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;
	int r;

	dev_dbg(ddata->dev, "enable\n");

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	src->ops->set_timings(src, &ddata->vm);

	r = src->ops->enable(src);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return r;
}

static void tvc_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	dev_dbg(ddata->dev, "disable\n");

	if (!omapdss_device_is_enabled(dssdev))
		return;

	src->ops->disable(src);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void tvc_set_timings(struct omap_dss_device *dssdev,
			    struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	ddata->vm = *vm;

	src->ops->set_timings(src, vm);
}

static void tvc_get_timings(struct omap_dss_device *dssdev,
			    struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static int tvc_check_timings(struct omap_dss_device *dssdev,
			     struct videomode *vm)
{
	struct omap_dss_device *src = dssdev->src;

	return src->ops->check_timings(src, vm);
}

static const struct omap_dss_driver tvc_driver = {
	.connect		= tvc_connect,
	.disconnect		= tvc_disconnect,

	.enable			= tvc_enable,
	.disable		= tvc_disable,

	.set_timings		= tvc_set_timings,
	.get_timings		= tvc_get_timings,
	.check_timings		= tvc_check_timings,
};

static int tvc_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);
	ddata->dev = &pdev->dev;

	ddata->vm = tvc_pal_vm;

	dssdev = &ddata->dssdev;
	dssdev->driver = &tvc_driver;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_VENC;
	dssdev->owner = THIS_MODULE;

	omapdss_display_init(dssdev);
	omapdss_device_register(dssdev);

	return 0;
}

static int __exit tvc_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_device_unregister(&ddata->dssdev);

	tvc_disable(dssdev);
	omapdss_device_disconnect(dssdev, NULL);

	return 0;
}

static const struct of_device_id tvc_of_match[] = {
	{ .compatible = "omapdss,svideo-connector", },
	{ .compatible = "omapdss,composite-video-connector", },
	{},
};

MODULE_DEVICE_TABLE(of, tvc_of_match);

static struct platform_driver tvc_connector_driver = {
	.probe	= tvc_probe,
	.remove	= __exit_p(tvc_remove),
	.driver	= {
		.name	= "connector-analog-tv",
		.of_match_table = tvc_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(tvc_connector_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Analog TV Connector driver");
MODULE_LICENSE("GPL");
