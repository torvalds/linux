/*
 * Analog TV Connector driver
 *
 * Copyright (C) 2013 Texas Instruments
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

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	struct device *dev;

	struct omap_video_timings timings;

	enum omap_dss_venc_type connector_type;
	bool invert_polarity;
};

static const struct omap_video_timings tvc_pal_timings = {
	.x_res		= 720,
	.y_res		= 574,
	.pixelclock	= 13500000,
	.hsw		= 64,
	.hfp		= 12,
	.hbp		= 68,
	.vsw		= 5,
	.vfp		= 5,
	.vbp		= 41,

	.interlace	= true,
};

static const struct of_device_id tvc_of_match[];

struct tvc_of_data {
	enum omap_dss_venc_type connector_type;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int tvc_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	dev_dbg(ddata->dev, "connect\n");

	if (omapdss_device_is_connected(dssdev))
		return 0;

	r = in->ops.atv->connect(in, dssdev);
	if (r)
		return r;

	return 0;
}

static void tvc_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	dev_dbg(ddata->dev, "disconnect\n");

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.atv->disconnect(in, dssdev);
}

static int tvc_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	dev_dbg(ddata->dev, "enable\n");

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	in->ops.atv->set_timings(in, &ddata->timings);

	if (!ddata->dev->of_node) {
		in->ops.atv->set_type(in, ddata->connector_type);

		in->ops.atv->invert_vid_out_polarity(in,
			ddata->invert_polarity);
	}

	r = in->ops.atv->enable(in);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return r;
}

static void tvc_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	dev_dbg(ddata->dev, "disable\n");

	if (!omapdss_device_is_enabled(dssdev))
		return;

	in->ops.atv->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void tvc_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->timings = *timings;
	dssdev->panel.timings = *timings;

	in->ops.atv->set_timings(in, timings);
}

static void tvc_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*timings = ddata->timings;
}

static int tvc_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.atv->check_timings(in, timings);
}

static u32 tvc_get_wss(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.atv->get_wss(in);
}

static int tvc_set_wss(struct omap_dss_device *dssdev, u32 wss)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.atv->set_wss(in, wss);
}

static struct omap_dss_driver tvc_driver = {
	.connect		= tvc_connect,
	.disconnect		= tvc_disconnect,

	.enable			= tvc_enable,
	.disable		= tvc_disable,

	.set_timings		= tvc_set_timings,
	.get_timings		= tvc_get_timings,
	.check_timings		= tvc_check_timings,

	.get_resolution		= omapdss_default_get_resolution,

	.get_wss		= tvc_get_wss,
	.set_wss		= tvc_set_wss,
};

static int tvc_probe_pdata(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct connector_atv_platform_data *pdata;
	struct omap_dss_device *in, *dssdev;

	pdata = dev_get_platdata(&pdev->dev);

	in = omap_dss_find_output(pdata->source);
	if (in == NULL) {
		dev_err(&pdev->dev, "Failed to find video source\n");
		return -EPROBE_DEFER;
	}

	ddata->in = in;

	ddata->connector_type = pdata->connector_type;
	ddata->invert_polarity = ddata->invert_polarity;

	dssdev = &ddata->dssdev;
	dssdev->name = pdata->name;

	return 0;
}

static int tvc_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct omap_dss_device *in;

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int tvc_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);
	ddata->dev = &pdev->dev;

	if (dev_get_platdata(&pdev->dev)) {
		r = tvc_probe_pdata(pdev);
		if (r)
			return r;
	} else if (pdev->dev.of_node) {
		r = tvc_probe_of(pdev);
		if (r)
			return r;
	} else {
		return -ENODEV;
	}

	ddata->timings = tvc_pal_timings;

	dssdev = &ddata->dssdev;
	dssdev->driver = &tvc_driver;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_VENC;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.timings = tvc_pal_timings;

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

static int __exit tvc_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_display(&ddata->dssdev);

	tvc_disable(dssdev);
	tvc_disconnect(dssdev);

	omap_dss_put_device(in);

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
		.owner	= THIS_MODULE,
		.of_match_table = tvc_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(tvc_connector_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Analog TV Connector driver");
MODULE_LICENSE("GPL");
