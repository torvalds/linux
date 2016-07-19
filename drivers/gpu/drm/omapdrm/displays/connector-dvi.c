/*
 * Generic DVI Connector driver
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <drm/drm_edid.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

static const struct omap_video_timings dvic_default_timings = {
	.x_res		= 640,
	.y_res		= 480,

	.pixelclock	= 23500000,

	.hfp		= 48,
	.hsw		= 32,
	.hbp		= 80,

	.vfp		= 3,
	.vsw		= 4,
	.vbp		= 7,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
	.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
};

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	struct omap_video_timings timings;

	struct i2c_adapter *i2c_adapter;
};

#define to_panel_data(x) container_of(x, struct panel_drv_data, dssdev)

static int dvic_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (omapdss_device_is_connected(dssdev))
		return 0;

	r = in->ops.dvi->connect(in, dssdev);
	if (r)
		return r;

	return 0;
}

static void dvic_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dvi->disconnect(in, dssdev);
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

	in->ops.dvi->set_timings(in, &ddata->timings);

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
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->timings = *timings;
	dssdev->panel.timings = *timings;

	in->ops.dvi->set_timings(in, timings);
}

static void dvic_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*timings = ddata->timings;
}

static int dvic_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dvi->check_timings(in, timings);
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

	if (!ddata->i2c_adapter)
		return true;

	r = dvic_ddc_read(ddata->i2c_adapter, &out, 1, 0);

	return r == 0;
}

static struct omap_dss_driver dvic_driver = {
	.connect	= dvic_connect,
	.disconnect	= dvic_disconnect,

	.enable		= dvic_enable,
	.disable	= dvic_disable,

	.set_timings	= dvic_set_timings,
	.get_timings	= dvic_get_timings,
	.check_timings	= dvic_check_timings,

	.get_resolution	= omapdss_default_get_resolution,

	.read_edid	= dvic_read_edid,
	.detect		= dvic_detect,
};

static int dvic_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;
	struct omap_dss_device *in;
	struct device_node *adapter_node;
	struct i2c_adapter *adapter;

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&pdev->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	adapter_node = of_parse_phandle(node, "ddc-i2c-bus", 0);
	if (adapter_node) {
		adapter = of_get_i2c_adapter_by_node(adapter_node);
		if (adapter == NULL) {
			dev_err(&pdev->dev, "failed to parse ddc-i2c-bus\n");
			omap_dss_put_device(ddata->in);
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

	if (!pdev->dev.of_node)
		return -ENODEV;

	r = dvic_probe_of(pdev);
	if (r)
		return r;

	ddata->timings = dvic_default_timings;

	dssdev = &ddata->dssdev;
	dssdev->driver = &dvic_driver;
	dssdev->dev = &pdev->dev;
	dssdev->type = OMAP_DISPLAY_TYPE_DVI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.timings = dvic_default_timings;

	r = omapdss_register_display(dssdev);
	if (r) {
		dev_err(&pdev->dev, "Failed to register panel\n");
		goto err_reg;
	}

	return 0;

err_reg:
	omap_dss_put_device(ddata->in);

	i2c_put_adapter(ddata->i2c_adapter);

	return r;
}

static int __exit dvic_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_display(&ddata->dssdev);

	dvic_disable(dssdev);
	dvic_disconnect(dssdev);

	omap_dss_put_device(in);

	i2c_put_adapter(ddata->i2c_adapter);

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
