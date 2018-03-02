/*
 * TPD12S015 HDMI ESD protection & level shifter chip driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/mutex.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;
	void (*hpd_cb)(void *cb_data, enum drm_connector_status status);
	void *hpd_cb_data;
	bool hpd_enabled;
	struct mutex hpd_lock;

	struct gpio_desc *ct_cp_hpd_gpio;
	struct gpio_desc *ls_oe_gpio;
	struct gpio_desc *hpd_gpio;

	struct videomode vm;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int tpd_connect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src;
	int r;

	src = omapdss_of_find_source_for_first_ep(dssdev->dev->of_node);
	if (IS_ERR(src)) {
		dev_err(dssdev->dev, "failed to find video source\n");
		return PTR_ERR(src);
	}

	r = omapdss_device_connect(dssdev->dss, src, dssdev);
	if (r) {
		omapdss_device_put(src);
		return r;
	}

	gpiod_set_value_cansleep(ddata->ct_cp_hpd_gpio, 1);
	gpiod_set_value_cansleep(ddata->ls_oe_gpio, 1);

	/* DC-DC converter needs at max 300us to get to 90% of 5V */
	udelay(300);

	return 0;
}

static void tpd_disconnect(struct omap_dss_device *dssdev,
		struct omap_dss_device *dst)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	gpiod_set_value_cansleep(ddata->ct_cp_hpd_gpio, 0);
	gpiod_set_value_cansleep(ddata->ls_oe_gpio, 0);

	omapdss_device_disconnect(src, &ddata->dssdev);

	omapdss_device_put(src);
}

static int tpd_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;
	int r;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	src->ops->set_timings(src, &ddata->vm);

	r = src->ops->enable(src);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return r;
}

static void tpd_disable(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *src = dssdev->src;

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	src->ops->disable(src);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void tpd_set_timings(struct omap_dss_device *dssdev,
			    struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	ddata->vm = *vm;

	src->ops->set_timings(src, vm);
}

static int tpd_check_timings(struct omap_dss_device *dssdev,
			     struct videomode *vm)
{
	struct omap_dss_device *src = dssdev->src;

	return src->ops->check_timings(src, vm);
}

static int tpd_read_edid(struct omap_dss_device *dssdev,
		u8 *edid, int len)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;

	if (!gpiod_get_value_cansleep(ddata->hpd_gpio))
		return -ENODEV;

	return src->ops->hdmi.read_edid(src, edid, len);
}

static bool tpd_detect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *src = dssdev->src;
	bool connected = gpiod_get_value_cansleep(ddata->hpd_gpio);

	if (!connected && src->ops->hdmi.lost_hotplug)
		src->ops->hdmi.lost_hotplug(src);
	return connected;
}

static int tpd_register_hpd_cb(struct omap_dss_device *dssdev,
			       void (*cb)(void *cb_data,
					  enum drm_connector_status status),
			       void *cb_data)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_cb = cb;
	ddata->hpd_cb_data = cb_data;
	mutex_unlock(&ddata->hpd_lock);

	return 0;
}

static void tpd_unregister_hpd_cb(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_cb = NULL;
	ddata->hpd_cb_data = NULL;
	mutex_unlock(&ddata->hpd_lock);
}

static void tpd_enable_hpd(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_enabled = true;
	mutex_unlock(&ddata->hpd_lock);
}

static void tpd_disable_hpd(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_enabled = false;
	mutex_unlock(&ddata->hpd_lock);
}

static int tpd_set_infoframe(struct omap_dss_device *dssdev,
		const struct hdmi_avi_infoframe *avi)
{
	struct omap_dss_device *src = dssdev->src;

	return src->ops->hdmi.set_infoframe(src, avi);
}

static int tpd_set_hdmi_mode(struct omap_dss_device *dssdev,
		bool hdmi_mode)
{
	struct omap_dss_device *src = dssdev->src;

	return src->ops->hdmi.set_hdmi_mode(src, hdmi_mode);
}

static const struct omap_dss_device_ops tpd_ops = {
	.connect		= tpd_connect,
	.disconnect		= tpd_disconnect,
	.enable			= tpd_enable,
	.disable		= tpd_disable,
	.check_timings		= tpd_check_timings,
	.set_timings		= tpd_set_timings,

	.hdmi = {
		.read_edid		= tpd_read_edid,
		.detect			= tpd_detect,
		.register_hpd_cb	= tpd_register_hpd_cb,
		.unregister_hpd_cb	= tpd_unregister_hpd_cb,
		.enable_hpd		= tpd_enable_hpd,
		.disable_hpd		= tpd_disable_hpd,
		.set_infoframe		= tpd_set_infoframe,
		.set_hdmi_mode		= tpd_set_hdmi_mode,
	},
};

static irqreturn_t tpd_hpd_isr(int irq, void *data)
{
	struct panel_drv_data *ddata = data;

	mutex_lock(&ddata->hpd_lock);
	if (ddata->hpd_enabled && ddata->hpd_cb) {
		enum drm_connector_status status;

		if (tpd_detect(&ddata->dssdev))
			status = connector_status_connected;
		else
			status = connector_status_disconnected;

		ddata->hpd_cb(ddata->hpd_cb_data, status);
	}
	mutex_unlock(&ddata->hpd_lock);

	return IRQ_HANDLED;
}

static int tpd_probe(struct platform_device *pdev)
{
	struct omap_dss_device *dssdev;
	struct panel_drv_data *ddata;
	int r;
	struct gpio_desc *gpio;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	gpio = devm_gpiod_get_index_optional(&pdev->dev, NULL, 0,
		 GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	ddata->ct_cp_hpd_gpio = gpio;

	gpio = devm_gpiod_get_index_optional(&pdev->dev, NULL, 1,
		 GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	ddata->ls_oe_gpio = gpio;

	gpio = devm_gpiod_get_index(&pdev->dev, NULL, 2,
		GPIOD_IN);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	ddata->hpd_gpio = gpio;

	mutex_init(&ddata->hpd_lock);

	r = devm_request_threaded_irq(&pdev->dev, gpiod_to_irq(ddata->hpd_gpio),
		NULL, tpd_hpd_isr,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"tpd12s015 hpd", ddata);
	if (r)
		return r;

	dssdev = &ddata->dssdev;
	dssdev->ops = &tpd_ops;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_HDMI;
	dssdev->output_type = OMAP_DISPLAY_TYPE_HDMI;
	dssdev->owner = THIS_MODULE;
	dssdev->port_num = 1;

	omapdss_device_register(dssdev);

	return 0;
}

static int __exit tpd_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_device_unregister(&ddata->dssdev);

	WARN_ON(omapdss_device_is_enabled(dssdev));
	if (omapdss_device_is_enabled(dssdev))
		tpd_disable(dssdev);

	WARN_ON(omapdss_device_is_connected(dssdev));
	if (omapdss_device_is_connected(dssdev))
		omapdss_device_disconnect(dssdev, dssdev->dst);

	return 0;
}

static const struct of_device_id tpd_of_match[] = {
	{ .compatible = "omapdss,ti,tpd12s015", },
	{},
};

MODULE_DEVICE_TABLE(of, tpd_of_match);

static struct platform_driver tpd_driver = {
	.probe	= tpd_probe,
	.remove	= __exit_p(tpd_remove),
	.driver	= {
		.name	= "tpd12s015",
		.of_match_table = tpd_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(tpd_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("TPD12S015 driver");
MODULE_LICENSE("GPL");
