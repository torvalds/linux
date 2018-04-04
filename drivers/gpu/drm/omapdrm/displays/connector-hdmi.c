/*
 * HDMI Connector driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>

#include <drm/drm_edid.h>

#include "../dss/omapdss.h"

static const struct videomode hdmic_default_vm = {
	.hactive	= 640,
	.vactive	= 480,
	.pixelclock	= 25175000,
	.hsync_len	= 96,
	.hfront_porch	= 16,
	.hback_porch	= 48,
	.vsync_len	= 2,
	.vfront_porch	= 11,
	.vback_porch	= 31,

	.flags		= DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW,
};

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;
	void (*hpd_cb)(void *cb_data, enum drm_connector_status status);
	void *hpd_cb_data;
	bool hpd_enabled;
	struct mutex hpd_lock;

	struct device *dev;

	struct videomode vm;

	int hpd_gpio;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int hdmic_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	dev_dbg(ddata->dev, "connect\n");

	if (omapdss_device_is_connected(dssdev))
		return 0;

	r = in->ops.hdmi->connect(in, dssdev);
	if (r)
		return r;

	return 0;
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

	in->ops.hdmi->set_timings(in, &ddata->vm);

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
			      struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->vm = *vm;
	dssdev->panel.vm = *vm;

	in->ops.hdmi->set_timings(in, vm);
}

static void hdmic_get_timings(struct omap_dss_device *dssdev,
			      struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static int hdmic_check_timings(struct omap_dss_device *dssdev,
			       struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.hdmi->check_timings(in, vm);
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
	bool connected;

	if (gpio_is_valid(ddata->hpd_gpio))
		connected = gpio_get_value_cansleep(ddata->hpd_gpio);
	else
		connected = in->ops.hdmi->detect(in);
	if (!connected && in->ops.hdmi->lost_hotplug)
		in->ops.hdmi->lost_hotplug(in);
	return connected;
}

static int hdmic_register_hpd_cb(struct omap_dss_device *dssdev,
				 void (*cb)(void *cb_data,
					    enum drm_connector_status status),
				 void *cb_data)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (gpio_is_valid(ddata->hpd_gpio)) {
		mutex_lock(&ddata->hpd_lock);
		ddata->hpd_cb = cb;
		ddata->hpd_cb_data = cb_data;
		mutex_unlock(&ddata->hpd_lock);
		return 0;
	} else if (in->ops.hdmi->register_hpd_cb) {
		return in->ops.hdmi->register_hpd_cb(in, cb, cb_data);
	}

	return -ENOTSUPP;
}

static void hdmic_unregister_hpd_cb(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (gpio_is_valid(ddata->hpd_gpio)) {
		mutex_lock(&ddata->hpd_lock);
		ddata->hpd_cb = NULL;
		ddata->hpd_cb_data = NULL;
		mutex_unlock(&ddata->hpd_lock);
	} else if (in->ops.hdmi->unregister_hpd_cb) {
		in->ops.hdmi->unregister_hpd_cb(in);
	}
}

static void hdmic_enable_hpd(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (gpio_is_valid(ddata->hpd_gpio)) {
		mutex_lock(&ddata->hpd_lock);
		ddata->hpd_enabled = true;
		mutex_unlock(&ddata->hpd_lock);
	} else if (in->ops.hdmi->enable_hpd) {
		in->ops.hdmi->enable_hpd(in);
	}
}

static void hdmic_disable_hpd(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (gpio_is_valid(ddata->hpd_gpio)) {
		mutex_lock(&ddata->hpd_lock);
		ddata->hpd_enabled = false;
		mutex_unlock(&ddata->hpd_lock);
	} else if (in->ops.hdmi->disable_hpd) {
		in->ops.hdmi->disable_hpd(in);
	}
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

	.read_edid		= hdmic_read_edid,
	.detect			= hdmic_detect,
	.register_hpd_cb	= hdmic_register_hpd_cb,
	.unregister_hpd_cb	= hdmic_unregister_hpd_cb,
	.enable_hpd		= hdmic_enable_hpd,
	.disable_hpd		= hdmic_disable_hpd,
	.set_hdmi_mode		= hdmic_set_hdmi_mode,
	.set_hdmi_infoframe	= hdmic_set_infoframe,
};

static irqreturn_t hdmic_hpd_isr(int irq, void *data)
{
	struct panel_drv_data *ddata = data;

	mutex_lock(&ddata->hpd_lock);
	if (ddata->hpd_enabled && ddata->hpd_cb) {
		enum drm_connector_status status;

		if (hdmic_detect(&ddata->dssdev))
			status = connector_status_connected;
		else
			status = connector_status_disconnected;

		ddata->hpd_cb(ddata->hpd_cb_data, status);
	}
	mutex_unlock(&ddata->hpd_lock);

	return IRQ_HANDLED;
}

static int hdmic_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct omap_dss_device *in;
	int gpio;

	/* HPD GPIO */
	gpio = of_get_named_gpio(node, "hpd-gpios", 0);
	if (gpio_is_valid(gpio))
		ddata->hpd_gpio = gpio;
	else
		ddata->hpd_gpio = -ENODEV;

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int hdmic_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);
	ddata->dev = &pdev->dev;

	if (!pdev->dev.of_node)
		return -ENODEV;

	r = hdmic_probe_of(pdev);
	if (r)
		return r;

	mutex_init(&ddata->hpd_lock);

	if (gpio_is_valid(ddata->hpd_gpio)) {
		r = devm_gpio_request_one(&pdev->dev, ddata->hpd_gpio,
				GPIOF_DIR_IN, "hdmi_hpd");
		if (r)
			goto err_reg;

		r = devm_request_threaded_irq(&pdev->dev,
				gpio_to_irq(ddata->hpd_gpio),
				NULL, hdmic_hpd_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT,
				"hdmic hpd", ddata);
		if (r)
			goto err_reg;
	}

	ddata->vm = hdmic_default_vm;

	dssdev = &ddata->dssdev;
	dssdev->driver = &hdmic_driver;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_HDMI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.vm = hdmic_default_vm;

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
