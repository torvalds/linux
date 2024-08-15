// SPDX-License-Identifier: GPL-2.0-only
/*
 * LG.Philips LB035Q02 LCD Panel driver
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 * Based on a driver by: Steve Sakoman <steve@sakoman.com>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/gpio.h>

#include <video/omapfb_dss.h>

static const struct omap_video_timings lb035q02_timings = {
	.x_res = 320,
	.y_res = 240,

	.pixelclock	= 6500000,

	.hsw		= 2,
	.hfp		= 20,
	.hbp		= 68,

	.vsw		= 2,
	.vfp		= 4,
	.vbp		= 18,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
	.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
};

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct omap_dss_device *in;

	struct spi_device *spi;

	int data_lines;

	struct omap_video_timings videomode;

	/* used for non-DT boot, to be removed */
	int backlight_gpio;

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
	struct omap_dss_device *in = ddata->in;
	int r;

	if (omapdss_device_is_connected(dssdev))
		return 0;

	r = in->ops.dpi->connect(in, dssdev);
	if (r)
		return r;

	init_lb035q02_panel(ddata->spi);

	return 0;
}

static void lb035q02_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dpi->disconnect(in, dssdev);
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

	if (ddata->data_lines)
		in->ops.dpi->set_data_lines(in, ddata->data_lines);
	in->ops.dpi->set_timings(in, &ddata->videomode);

	r = in->ops.dpi->enable(in);
	if (r)
		return r;

	if (ddata->enable_gpio)
		gpiod_set_value_cansleep(ddata->enable_gpio, 1);

	if (gpio_is_valid(ddata->backlight_gpio))
		gpio_set_value_cansleep(ddata->backlight_gpio, 1);

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

	if (gpio_is_valid(ddata->backlight_gpio))
		gpio_set_value_cansleep(ddata->backlight_gpio, 0);

	in->ops.dpi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void lb035q02_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->videomode = *timings;
	dssdev->panel.timings = *timings;

	in->ops.dpi->set_timings(in, timings);
}

static void lb035q02_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*timings = ddata->videomode;
}

static int lb035q02_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dpi->check_timings(in, timings);
}

static struct omap_dss_driver lb035q02_ops = {
	.connect	= lb035q02_connect,
	.disconnect	= lb035q02_disconnect,

	.enable		= lb035q02_enable,
	.disable	= lb035q02_disable,

	.set_timings	= lb035q02_set_timings,
	.get_timings	= lb035q02_get_timings,
	.check_timings	= lb035q02_check_timings,

	.get_resolution	= omapdss_default_get_resolution,
};

static int lb035q02_probe_of(struct spi_device *spi)
{
	struct device_node *node = spi->dev.of_node;
	struct panel_drv_data *ddata = spi_get_drvdata(spi);
	struct omap_dss_device *in;
	struct gpio_desc *gpio;

	gpio = devm_gpiod_get(&spi->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(gpio),
				     "failed to parse enable gpio\n");

	ddata->enable_gpio = gpio;

	ddata->backlight_gpio = -ENOENT;

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&spi->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int lb035q02_panel_spi_probe(struct spi_device *spi)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	if (!spi->dev.of_node)
		return -ENODEV;

	ddata = devm_kzalloc(&spi->dev, sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	spi_set_drvdata(spi, ddata);

	ddata->spi = spi;

	r = lb035q02_probe_of(spi);
	if (r)
		return r;

	if (gpio_is_valid(ddata->backlight_gpio)) {
		r = devm_gpio_request_one(&spi->dev, ddata->backlight_gpio,
				GPIOF_OUT_INIT_LOW, "panel backlight");
		if (r)
			goto err_gpio;
	}

	ddata->videomode = lb035q02_timings;

	dssdev = &ddata->dssdev;
	dssdev->dev = &spi->dev;
	dssdev->driver = &lb035q02_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.timings = ddata->videomode;
	dssdev->phy.dpi.data_lines = ddata->data_lines;

	r = omapdss_register_display(dssdev);
	if (r) {
		dev_err(&spi->dev, "Failed to register panel\n");
		goto err_reg;
	}

	return 0;

err_reg:
err_gpio:
	omap_dss_put_device(ddata->in);
	return r;
}

static void lb035q02_panel_spi_remove(struct spi_device *spi)
{
	struct panel_drv_data *ddata = spi_get_drvdata(spi);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	omapdss_unregister_display(dssdev);

	lb035q02_disable(dssdev);
	lb035q02_disconnect(dssdev);

	omap_dss_put_device(in);
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
