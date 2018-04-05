/*
 * LG.Philips LB035Q02 LCD Panel driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 * Based on a driver by: Steve Sakoman <steve@sakoman.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>

#include "../dss/omapdss.h"

static const struct videomode lb035q02_vm = {
	.hactive = 320,
	.vactive = 240,

	.pixelclock	= 6500000,

	.hsync_len	= 2,
	.hfront_porch	= 20,
	.hback_porch	= 68,

	.vsync_len	= 2,
	.vfront_porch	= 4,
	.vback_porch	= 18,

	.flags		= DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW |
			  DISPLAY_FLAGS_DE_HIGH | DISPLAY_FLAGS_SYNC_NEGEDGE |
			  DISPLAY_FLAGS_PIXDATA_POSEDGE,
	/*
	 * Note: According to the panel documentation:
	 * DE is active LOW
	 * DATA needs to be driven on the FALLING edge
	 */
};

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	struct spi_device *spi;

	struct videomode vm;

	struct gpio_desc *enable_gpio;
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

static int lb035q02_write_reg(struct spi_device *spi, u8 reg, u16 val)
{
	struct spi_message msg;
	struct spi_transfer index_xfer = {
		.len		= 3,
		.cs_change	= 1,
	};
	struct spi_transfer value_xfer = {
		.len		= 3,
	};
	u8	buffer[16];

	spi_message_init(&msg);

	/* register index */
	buffer[0] = 0x70;
	buffer[1] = 0x00;
	buffer[2] = reg & 0x7f;
	index_xfer.tx_buf = buffer;
	spi_message_add_tail(&index_xfer, &msg);

	/* register value */
	buffer[4] = 0x72;
	buffer[5] = val >> 8;
	buffer[6] = val;
	value_xfer.tx_buf = buffer + 4;
	spi_message_add_tail(&value_xfer, &msg);

	return spi_sync(spi, &msg);
}

static void init_lb035q02_panel(struct spi_device *spi)
{
	/* Init sequence from page 28 of the lb035q02 spec */
	lb035q02_write_reg(spi, 0x01, 0x6300);
	lb035q02_write_reg(spi, 0x02, 0x0200);
	lb035q02_write_reg(spi, 0x03, 0x0177);
	lb035q02_write_reg(spi, 0x04, 0x04c7);
	lb035q02_write_reg(spi, 0x05, 0xffc0);
	lb035q02_write_reg(spi, 0x06, 0xe806);
	lb035q02_write_reg(spi, 0x0a, 0x4008);
	lb035q02_write_reg(spi, 0x0b, 0x0000);
	lb035q02_write_reg(spi, 0x0d, 0x0030);
	lb035q02_write_reg(spi, 0x0e, 0x2800);
	lb035q02_write_reg(spi, 0x0f, 0x0000);
	lb035q02_write_reg(spi, 0x16, 0x9f80);
	lb035q02_write_reg(spi, 0x17, 0x0a0f);
	lb035q02_write_reg(spi, 0x1e, 0x00c1);
	lb035q02_write_reg(spi, 0x30, 0x0300);
	lb035q02_write_reg(spi, 0x31, 0x0007);
	lb035q02_write_reg(spi, 0x32, 0x0000);
	lb035q02_write_reg(spi, 0x33, 0x0000);
	lb035q02_write_reg(spi, 0x34, 0x0707);
	lb035q02_write_reg(spi, 0x35, 0x0004);
	lb035q02_write_reg(spi, 0x36, 0x0302);
	lb035q02_write_reg(spi, 0x37, 0x0202);
	lb035q02_write_reg(spi, 0x3a, 0x0a0d);
	lb035q02_write_reg(spi, 0x3b, 0x0806);
}

static int lb035q02_connect(struct omap_dss_device *dssdev)
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

	r = in->ops.dpi->connect(in, dssdev);
	if (r) {
		omap_dss_put_device(in);
		return r;
	}

	init_lb035q02_panel(ddata->spi);

	ddata->in = in;
	return 0;
}

static void lb035q02_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dpi->disconnect(in, dssdev);

	omap_dss_put_device(in);
	ddata->in = NULL;
}

static int lb035q02_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (!omapdss_device_is_connected(dssdev))
		return -ENODEV;

	if (omapdss_device_is_enabled(dssdev))
		return 0;

	in->ops.dpi->set_timings(in, &ddata->vm);

	r = in->ops.dpi->enable(in);
	if (r)
		return r;

	if (ddata->enable_gpio)
		gpiod_set_value_cansleep(ddata->enable_gpio, 1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void lb035q02_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (ddata->enable_gpio)
		gpiod_set_value_cansleep(ddata->enable_gpio, 0);

	in->ops.dpi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void lb035q02_set_timings(struct omap_dss_device *dssdev,
				 struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->vm = *vm;
	dssdev->panel.vm = *vm;

	in->ops.dpi->set_timings(in, vm);
}

static void lb035q02_get_timings(struct omap_dss_device *dssdev,
				 struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*vm = ddata->vm;
}

static int lb035q02_check_timings(struct omap_dss_device *dssdev,
				  struct videomode *vm)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dpi->check_timings(in, vm);
}

static struct omap_dss_driver lb035q02_ops = {
	.connect	= lb035q02_connect,
	.disconnect	= lb035q02_disconnect,

	.enable		= lb035q02_enable,
	.disable	= lb035q02_disable,

	.set_timings	= lb035q02_set_timings,
	.get_timings	= lb035q02_get_timings,
	.check_timings	= lb035q02_check_timings,
};

static int lb035q02_probe_of(struct spi_device *spi)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&spi->dev);
	struct gpio_desc *gpio;

	gpio = devm_gpiod_get(&spi->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpio)) {
		dev_err(&spi->dev, "failed to parse enable gpio\n");
		return PTR_ERR(gpio);
	}

	ddata->enable_gpio = gpio;

	return 0;
}

static int lb035q02_panel_spi_probe(struct spi_device *spi)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&spi->dev, sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, ddata);

	ddata->spi = spi;

	r = lb035q02_probe_of(spi);
	if (r)
		return r;

	ddata->vm = lb035q02_vm;

	dssdev = &ddata->dssdev;
	dssdev->dev = &spi->dev;
	dssdev->driver = &lb035q02_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.vm = ddata->vm;

	r = omapdss_register_display(dssdev);
	if (r) {
		dev_err(&spi->dev, "Failed to register panel\n");
		return r;
	}

	return 0;
}

static int lb035q02_panel_spi_remove(struct spi_device *spi)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&spi->dev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_unregister_display(dssdev);

	lb035q02_disable(dssdev);
	lb035q02_disconnect(dssdev);

	return 0;
}

static const struct of_device_id lb035q02_of_match[] = {
	{ .compatible = "omapdss,lgphilips,lb035q02", },
	{},
};

MODULE_DEVICE_TABLE(of, lb035q02_of_match);

static struct spi_driver lb035q02_spi_driver = {
	.probe		= lb035q02_panel_spi_probe,
	.remove		= lb035q02_panel_spi_remove,
	.driver		= {
		.name	= "panel_lgphilips_lb035q02",
		.of_match_table = lb035q02_of_match,
		.suppress_bind_attrs = true,
	},
};

module_spi_driver(lb035q02_spi_driver);

MODULE_ALIAS("spi:lgphilips,lb035q02");
MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("LG.Philips LB035Q02 LCD Panel driver");
MODULE_LICENSE("GPL");
