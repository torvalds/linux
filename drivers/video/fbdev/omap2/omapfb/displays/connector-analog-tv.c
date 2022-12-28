// SPDX-License-Identifier: GPL-2.0-only
/*
 * Analog TV Connector driver
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <video/omapfb_dss.h>

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	struct device *dev;

	struct omap_video_timings timings;

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

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int tvc_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	dev_dbg(ddata->dev, "connect\n");

	if (omapdss_device_is_connected(dssdev))
		return 0;

	return in->ops.atv->connect(in, dssdev);
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
		in->ops.atv->set_type(in, OMAP_DSS_VENC_TYPE_COMPOSITE);

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

static int tvc_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	if (!pdev->dev.of_node)
		return -ENODEV;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);
	ddata->dev = &pdev->dev;

	ddata->in = omapdss_of_find_source_for_first_ep(pdev->dev.of_node);
	r = PTR_ERR_OR_ZERO(ddata->in);
	if (r) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return r;
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
		.of_match_table = tvc_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(tvc_connector_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Analog TV Connector driver");
MODULE_LICENSE("GPL");
