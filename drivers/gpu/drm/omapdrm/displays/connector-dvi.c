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

struct panel_drv_data {
	struct omap_dss_device dssdev;

	struct i2c_adapter *i2c_adapter;

	struct gpio_desc *hpd_gpio;

	void (*hpd_cb)(void *cb_data, enum drm_connector_status status);
	void *hpd_cb_data;
	bool hpd_enabled;
	/* mutex for hpd fields above */
	struct mutex hpd_lock;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int dvic_connect(struct omap_dss_device *src,
			struct omap_dss_device *dst)
{
	return 0;
}

static void dvic_disconnect(struct omap_dss_device *src,
			    struct omap_dss_device *dst)
{
}

static int dvic_enable(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *src = dssdev->src;
	int r;

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	r = src->ops->enable(src);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void dvic_disable(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *src = dssdev->src;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	src->ops->disable(src);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
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

static void dvic_register_hpd_cb(struct omap_dss_device *dssdev,
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

static void dvic_unregister_hpd_cb(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	mutex_lock(&ddata->hpd_lock);
	ddata->hpd_cb = NULL;
	ddata->hpd_cb_data = NULL;
	mutex_unlock(&ddata->hpd_lock);
}

static const struct omap_dss_device_ops dvic_ops = {
	.connect	= dvic_connect,
	.disconnect	= dvic_disconnect,

	.enable		= dvic_enable,
	.disable	= dvic_disable,

	.read_edid	= dvic_read_edid,
	.detect		= dvic_detect,

	.register_hpd_cb	= dvic_register_hpd_cb,
	.unregister_hpd_cb	= dvic_unregister_hpd_cb,
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

	dssdev = &ddata->dssdev;
	dssdev->ops = &dvic_ops;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_DVI;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(0);

	if (ddata->hpd_gpio)
		dssdev->ops_flags |= OMAP_DSS_DEVICE_OP_DETECT
				  |  OMAP_DSS_DEVICE_OP_HPD;
	if (ddata->i2c_adapter)
		dssdev->ops_flags |= OMAP_DSS_DEVICE_OP_DETECT
				  |  OMAP_DSS_DEVICE_OP_EDID;

	omapdss_display_init(dssdev);
	omapdss_device_register(dssdev);

	return 0;
}

static int __exit dvic_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_device_unregister(&ddata->dssdev);

	dvic_disable(dssdev);

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
