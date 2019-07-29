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
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;
	void (*hpd_cb)(void *cb_data, enum drm_connector_status status);
	void *hpd_cb_data;
	struct mutex hpd_lock;

	struct device *dev;

	struct gpio_desc *hpd_gpio;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int hdmic_connect(struct omap_dss_device *src,
			 struct omap_dss_device *dst)
{
	return 0;
}

static void hdmic_disconnect(struct omap_dss_device *src,
			     struct omap_dss_device *dst)
{
}

static int hdmic_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;
	int r;

	dev_dbg(ddata->dev, "enable\n");

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	r = src->ops->enable(src);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return r;
}

static void hdmic_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	dev_dbg(ddata->dev, "disable\n");

	if (!omapdss_device_is_enabled(dssdev))
		return;

	src->ops->disable(src);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static bool hdmic_detect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	return gpiod_get_value_cansleep(ddata->hpd_gpio);
}

static void hdmic_register_hpd_cb(struct omap_dss_device *dssdev,
				  void (*cb)(void *cb_data,
					    enum drm_connector_status status),
				  void *cb_data)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_cb = cb;
	ddata->hpd_cb_data = cb_data;
	mutex_unlock(&ddata->hpd_lock);
}

static void hdmic_unregister_hpd_cb(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_cb = NULL;
	ddata->hpd_cb_data = NULL;
	mutex_unlock(&ddata->hpd_lock);
}

static const struct omap_dss_device_ops hdmic_ops = {
	.connect		= hdmic_connect,
	.disconnect		= hdmic_disconnect,

	.enable			= hdmic_enable,
	.disable		= hdmic_disable,

	.detect			= hdmic_detect,
	.register_hpd_cb	= hdmic_register_hpd_cb,
	.unregister_hpd_cb	= hdmic_unregister_hpd_cb,
};

static irqreturn_t hdmic_hpd_isr(int irq, void *data)
{
	struct panel_drv_data *ddata = data;

	mutex_lock(&ddata->hpd_lock);
	if (ddata->hpd_cb) {
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

static int hdmic_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	struct gpio_desc *gpio;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);
	ddata->dev = &pdev->dev;

	mutex_init(&ddata->hpd_lock);

	/* HPD GPIO */
	gpio = devm_gpiod_get_optional(&pdev->dev, "hpd", GPIOD_IN);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to parse HPD gpio\n");
		return PTR_ERR(gpio);
	}

	ddata->hpd_gpio = gpio;

	if (ddata->hpd_gpio) {
		r = devm_request_threaded_irq(&pdev->dev,
				gpiod_to_irq(ddata->hpd_gpio),
				NULL, hdmic_hpd_isr,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT,
				"hdmic hpd", ddata);
		if (r)
			return r;
	}

	dssdev = &ddata->dssdev;
	dssdev->ops = &hdmic_ops;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_HDMI;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(0);
	dssdev->ops_flags = ddata->hpd_gpio
			  ? OMAP_DSS_DEVICE_OP_DETECT | OMAP_DSS_DEVICE_OP_HPD
			  : 0;

	omapdss_display_init(dssdev);
	omapdss_device_register(dssdev);

	return 0;
}

static int __exit hdmic_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_device_unregister(&ddata->dssdev);

	hdmic_disable(dssdev);

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
