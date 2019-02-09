/*
 * Generic DVI Connector driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <drm/drm_edid.h>

#include "../dss/omapdss.h"

static const struct videomode dvic_default_vm = {
	.hactive	= 640,
	.vactive	= 480,

	.pixelclock	= 23500000,

	.hfront_porch	= 48,
	.hsync_len	= 32,
	.hback_porch	= 80,

	.vfront_porch	= 3,
	.vsync_len	= 4,
	.vback_porch	= 7,

	.flags		= DISPLAY_FLAGS_HSYNC_HIGH | DISPLAY_FLAGS_VSYNC_HIGH |
			  DISPLAY_FLAGS_SYNC_NEGEDGE | DISPLAY_FLAGS_DE_HIGH |
			  DISPLAY_FLAGS_PIXDATA_POSEDGE,
};

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	struct videomode vm;

	struct i2c_adapter *i2c_adapter;

	struct gpio_desc *hpd_gpio;

	void (*hpd_cb)(void *cb_data, enum drm_connector_status status);
	void *hpd_cb_data;
	bool hpd_enabled;
	/* mutex for hpd fields above */
	struct mutex hpd_lock;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int dvic_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in;
	int r;

	if (omapdss_device_is_connected(dssdev))
		return 0;

	in = omapdss_of_find_source_for_first_ep(dssdev->dev->of_node);
	if (IS_ERR(in)) {
		dev_err(dssdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	r = in->ops.dvi->connect(in, dssdev);
	if (r) {
		omap_dss_put_device(in);
		return r;
	}

	ddata->in = in;
	return 0;
}

static void dvic_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dvi->disconnect(in, dssdev);

	omap_dss_put_device(in);
	ddata->in = NULL;
}

static int dvic_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	in->ops.dvi->set_timings(in, &ddata->vm);

	r = in->ops.dvi->enable(in);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void dvic_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	in->ops.dvi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void dvic_set_timings(struct omap_dss_device *dssdev,
			     struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->vm = *vm;
	dssdev->panel.vm = *vm;

	in->ops.dvi->set_timings(in, vm);
}

static void dvic_get_timings(struct omap_dss_device *dssdev,
			     struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static int dvic_check_timings(struct omap_dss_device *dssdev,
			      struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dvi->check_timings(in, vm);
}

static int dvic_ddc_read(struct i2c_adapter *adapter,
		unsigned char *buf, u16 count, u8 offset)
{
	int r, retries;

	for (retries = 3; retries > 0; retries--) {
		struct i2c_msg msgs[] = {
			{
				.addr   = DDC_ADDR,
				.flags  = 0,
				.len    = 1,
				.buf    = &offset,
			}, {
				.addr   = DDC_ADDR,
				.flags  = I2C_M_RD,
				.len    = count,
				.buf    = buf,
			}
		};

		r = i2c_transfer(adapter, msgs, 2);
		if (r == 2)
			return 0;

		if (r != -EAGAIN)
			break;
	}

	return r < 0 ? r : -EIO;
}

static int dvic_read_edid(struct omap_dss_device *dssdev,
		u8 *edid, int len)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	int r, l, bytes_read;

	if (ddata->hpd_gpio && !gpiod_get_value_cansleep(ddata->hpd_gpio))
		return -ENODEV;

	if (!ddata->i2c_adapter)
		return -ENODEV;

	l = min(EDID_LENGTH, len);
	r = dvic_ddc_read(ddata->i2c_adapter, edid, l, 0);
	if (r)
		return r;

	bytes_read = l;

	/* if there are extensions, read second block */
	if (len > EDID_LENGTH && edid[0x7e] > 0) {
		l = min(EDID_LENGTH, len - EDID_LENGTH);

		r = dvic_ddc_read(ddata->i2c_adapter, edid + EDID_LENGTH,
				l, EDID_LENGTH);
		if (r)
			return r;

		bytes_read += l;
	}

	return bytes_read;
}

static bool dvic_detect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	unsigned char out;
	int r;

	if (ddata->hpd_gpio)
		return gpiod_get_value_cansleep(ddata->hpd_gpio);

	if (!ddata->i2c_adapter)
		return true;

	r = dvic_ddc_read(ddata->i2c_adapter, &out, 1, 0);

	return r == 0;
}

static int dvic_register_hpd_cb(struct omap_dss_device *dssdev,
				 void (*cb)(void *cb_data,
					    enum drm_connector_status status),
				 void *cb_data)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (!ddata->hpd_gpio)
		return -ENOTSUPP;

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_cb = cb;
	ddata->hpd_cb_data = cb_data;
	mutex_unlock(&ddata->hpd_lock);
	return 0;
}

static void dvic_unregister_hpd_cb(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (!ddata->hpd_gpio)
		return;

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_cb = NULL;
	ddata->hpd_cb_data = NULL;
	mutex_unlock(&ddata->hpd_lock);
}

static void dvic_enable_hpd(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (!ddata->hpd_gpio)
		return;

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_enabled = true;
	mutex_unlock(&ddata->hpd_lock);
}

static void dvic_disable_hpd(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (!ddata->hpd_gpio)
		return;

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_enabled = false;
	mutex_unlock(&ddata->hpd_lock);
}

static struct omap_dss_driver dvic_driver = {
	.connect	= dvic_connect,
	.disconnect	= dvic_disconnect,

	.enable		= dvic_enable,
	.disable	= dvic_disable,

	.set_timings	= dvic_set_timings,
	.get_timings	= dvic_get_timings,
	.check_timings	= dvic_check_timings,

	.read_edid	= dvic_read_edid,
	.detect		= dvic_detect,

	.register_hpd_cb	= dvic_register_hpd_cb,
	.unregister_hpd_cb	= dvic_unregister_hpd_cb,
	.enable_hpd		= dvic_enable_hpd,
	.disable_hpd		= dvic_disable_hpd,
};

static irqreturn_t dvic_hpd_isr(int irq, void *data)
{
	struct panel_drv_data *ddata = data;

	mutex_lock(&ddata->hpd_lock);
	if (ddata->hpd_enabled && ddata->hpd_cb) {
		enum drm_connector_status status;

		if (dvic_detect(&ddata->dssdev))
			status = connector_status_connected;
		else
			status = connector_status_disconnected;

		ddata->hpd_cb(ddata->hpd_cb_data, status);
	}
	mutex_unlock(&ddata->hpd_lock);

	return IRQ_HANDLED;
}

static int dvic_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct device_node *adapter_node;
	struct i2c_adapter *adapter;
	struct gpio_desc *gpio;
	int r;

	gpio = devm_gpiod_get_optional(&pdev->dev, "hpd", GPIOD_IN);
	if (IS_ERR(gpio)) {
		dev_err(&pdev->dev, "failed to parse HPD gpio\n");
		return PTR_ERR(gpio);
	}

	ddata->hpd_gpio = gpio;

	mutex_init(&ddata->hpd_lock);

	if (ddata->hpd_gpio) {
		r = devm_request_threaded_irq(&pdev->dev,
			gpiod_to_irq(ddata->hpd_gpio), NULL, dvic_hpd_isr,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			"DVI HPD", ddata);
		if (r)
			return r;
	}

	adapter_node = of_parse_phandle(node, "ddc-i2c-bus", 0);
	if (adapter_node) {
		adapter = of_get_i2c_adapter_by_node(adapter_node);
		of_node_put(adapter_node);
		if (adapter == NULL) {
			dev_err(&pdev->dev, "failed to parse ddc-i2c-bus\n");
			return -EPROBE_DEFER;
		}

		ddata->i2c_adapter = adapter;
	}

	return 0;
}

static int dvic_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	r = dvic_probe_of(pdev);
	if (r)
		return r;

	ddata->vm = dvic_default_vm;

	dssdev = &ddata->dssdev;
	dssdev->driver = &dvic_driver;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_DVI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.vm = dvic_default_vm;

	r = omapdss_register_display(dssdev);
	if (r) {
		dev_err(&pdev->dev, "Failed to register panel\n");
		goto err_reg;
	}

	return 0;

err_reg:
	i2c_put_adapter(ddata->i2c_adapter);
	mutex_destroy(&ddata->hpd_lock);

	return r;
}

static int __exit dvic_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_unregister_display(&ddata->dssdev);

	dvic_disable(dssdev);
	dvic_disconnect(dssdev);

	i2c_put_adapter(ddata->i2c_adapter);

	mutex_destroy(&ddata->hpd_lock);

	return 0;
}

static const struct of_device_id dvic_of_match[] = {
	{ .compatible = "omapdss,dvi-connector", },
	{},
};

MODULE_DEVICE_TABLE(of, dvic_of_match);

static struct platform_driver dvi_connector_driver = {
	.probe	= dvic_probe,
	.remove	= __exit_p(dvic_remove),
	.driver	= {
		.name	= "connector-dvi",
		.of_match_table = dvic_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(dvi_connector_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Generic DVI Connector driver");
MODULE_LICENSE("GPL");
