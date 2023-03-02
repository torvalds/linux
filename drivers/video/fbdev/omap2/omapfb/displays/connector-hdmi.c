// SPDX-License-Identifier: GPL-2.0-only
/*
 * HDMI Connector driver
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <drm/drm_edid.h>

#include <video/omapfb_dss.h>

static const struct omap_video_timings hdmic_default_timings = {
	.x_res		= 640,
	.y_res		= 480,
	.pixelclock	= 25175000,
	.hsw		= 96,
	.hfp		= 16,
	.hbp		= 48,
	.vsw		= 2,
	.vfp		= 11,
	.vbp		= 31,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,

	.interlace	= false,
};

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	struct device *dev;

	struct omap_video_timings timings;

	struct gpio_desc *hpd_gpio;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int hdmic_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	dev_dbg(ddata->dev, "connect\n");

	if (omapdss_device_is_connected(dssdev))
		return 0;

	return in->ops.hdmi->connect(in, dssdev);
}

static void hdmic_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	dev_dbg(ddata->dev, "disconnect\n");

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.hdmi->disconnect(in, dssdev);
}

static int hdmic_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	dev_dbg(ddata->dev, "enable\n");

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	in->ops.hdmi->set_timings(in, &ddata->timings);

	r = in->ops.hdmi->enable(in);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return r;
}

static void hdmic_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	dev_dbg(ddata->dev, "disable\n");

	if (!omapdss_device_is_enabled(dssdev))
		return;

	in->ops.hdmi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void hdmic_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->timings = *timings;
	dssdev->panel.timings = *timings;

	in->ops.hdmi->set_timings(in, timings);
}

static void hdmic_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*timings = ddata->timings;
}

static int hdmic_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->check_timings(in, timings);
}

static int hdmic_read_edid(struct omap_dss_device *dssdev,
		u8 *edid, int len)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->read_edid(in, edid, len);
}

static bool hdmic_detect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (ddata->hpd_gpio)
		return gpiod_get_value_cansleep(ddata->hpd_gpio);
	else
		return in->ops.hdmi->detect(in);
}

static int hdmic_set_hdmi_mode(struct omap_dss_device *dssdev, bool hdmi_mode)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->set_hdmi_mode(in, hdmi_mode);
}

static int hdmic_set_infoframe(struct omap_dss_device *dssdev,
		const struct hdmi_avi_infoframe *avi)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->set_infoframe(in, avi);
}

static struct omap_dss_driver hdmic_driver = {
	.connect		= hdmic_connect,
	.disconnect		= hdmic_disconnect,

	.enable			= hdmic_enable,
	.disable		= hdmic_disable,

	.set_timings		= hdmic_set_timings,
	.get_timings		= hdmic_get_timings,
	.check_timings		= hdmic_check_timings,

	.get_resolution		= omapdss_default_get_resolution,

	.read_edid		= hdmic_read_edid,
	.detect			= hdmic_detect,
	.set_hdmi_mode		= hdmic_set_hdmi_mode,
	.set_hdmi_infoframe	= hdmic_set_infoframe,
};

static int hdmic_probe(struct platform_device *pdev)
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

	ddata->hpd_gpio = devm_gpiod_get_optional(&pdev->dev, "hpd", GPIOD_IN);
	r = PTR_ERR_OR_ZERO(ddata->hpd_gpio);
	if (r)
		return r;

	gpiod_set_consumer_name(ddata->hpd_gpio, "hdmi_hpd");

	ddata->in = omapdss_of_find_source_for_first_ep(pdev->dev.of_node);
	r = PTR_ERR_OR_ZERO(ddata->in);
	if (r) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return r;
	}

	ddata->timings = hdmic_default_timings;

	dssdev = &ddata->dssdev;
	dssdev->driver = &hdmic_driver;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_HDMI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.timings = hdmic_default_timings;

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

static int __exit hdmic_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_display(&ddata->dssdev);

	hdmic_disable(dssdev);
	hdmic_disconnect(dssdev);

	omap_dss_put_device(in);

	return 0;
}

static const struct of_device_id hdmic_of_match[] = {
	{ .compatible = "omapdss,hdmi-connector", },
	{},
};

MODULE_DEVICE_TABLE(of, hdmic_of_match);

static struct platform_driver hdmi_connector_driver = {
	.probe	= hdmic_probe,
	.remove	= __exit_p(hdmic_remove),
	.driver	= {
		.name	= "connector-hdmi",
		.of_match_table = hdmic_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(hdmi_connector_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("HDMI Connector driver");
MODULE_LICENSE("GPL");
