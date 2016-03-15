/*
 * NEC NL8048HL11 Panel driver
 *
 * Copyright (C) 2010 Texas Instruments Inc.
 * Author: Erik Gilling <konkers@android.com>
 * Converted to new DSS device model: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

struct panel_drv_data {
	struct omap_dss_device	dssdev;
	struct omap_dss_device *in;

	struct omap_video_timings videomode;

	int data_lines;

	int res_gpio;
	int qvga_gpio;

	struct spi_device *spi;
};

#define LCD_XRES		800
#define LCD_YRES		480
/*
 * NEC PIX Clock Ratings
 * MIN:21.8MHz TYP:23.8MHz MAX:25.7MHz
 */
#define LCD_PIXEL_CLOCK		23800000

static const struct {
	unsigned char addr;
	unsigned char dat;
} nec_8048_init_seq[] = {
	{ 3, 0x01 }, { 0, 0x00 }, { 1, 0x01 }, { 4, 0x00 }, { 5, 0x14 },
	{ 6, 0x24 }, { 16, 0xD7 }, { 17, 0x00 }, { 18, 0x00 }, { 19, 0x55 },
	{ 20, 0x01 }, { 21, 0x70 }, { 22, 0x1E }, { 23, 0x25 },	{ 24, 0x25 },
	{ 25, 0x02 }, { 26, 0x02 }, { 27, 0xA0 }, { 32, 0x2F }, { 33, 0x0F },
	{ 34, 0x0F }, { 35, 0x0F }, { 36, 0x0F }, { 37, 0x0F },	{ 38, 0x0F },
	{ 39, 0x00 }, { 40, 0x02 }, { 41, 0x02 }, { 42, 0x02 },	{ 43, 0x0F },
	{ 44, 0x0F }, { 45, 0x0F }, { 46, 0x0F }, { 47, 0x0F },	{ 48, 0x0F },
	{ 49, 0x0F }, { 50, 0x00 }, { 51, 0x02 }, { 52, 0x02 }, { 53, 0x02 },
	{ 80, 0x0C }, { 83, 0x42 }, { 84, 0x42 }, { 85, 0x41 },	{ 86, 0x14 },
	{ 89, 0x88 }, { 90, 0x01 }, { 91, 0x00 }, { 92, 0x02 },	{ 93, 0x0C },
	{ 94, 0x1C }, { 95, 0x27 }, { 98, 0x49 }, { 99, 0x27 }, { 102, 0x76 },
	{ 103, 0x27 }, { 112, 0x01 }, { 113, 0x0E }, { 114, 0x02 },
	{ 115, 0x0C }, { 118, 0x0C }, { 121, 0x30 }, { 130, 0x00 },
	{ 131, 0x00 }, { 132, 0xFC }, { 134, 0x00 }, { 136, 0x00 },
	{ 138, 0x00 }, { 139, 0x00 }, { 140, 0x00 }, { 141, 0xFC },
	{ 143, 0x00 }, { 145, 0x00 }, { 147, 0x00 }, { 148, 0x00 },
	{ 149, 0x00 }, { 150, 0xFC }, { 152, 0x00 }, { 154, 0x00 },
	{ 156, 0x00 }, { 157, 0x00 }, { 2, 0x00 },
};

static const struct omap_video_timings nec_8048_panel_timings = {
	.x_res		= LCD_XRES,
	.y_res		= LCD_YRES,
	.pixelclock	= LCD_PIXEL_CLOCK,
	.hfp		= 6,
	.hsw		= 1,
	.hbp		= 4,
	.vfp		= 3,
	.vsw		= 1,
	.vbp		= 4,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
	.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

static int nec_8048_spi_send(struct spi_device *spi, unsigned char reg_addr,
			unsigned char reg_data)
{
	int ret = 0;
	unsigned int cmd = 0, data = 0;

	cmd = 0x0000 | reg_addr; /* register address write */
	data = 0x0100 | reg_data; /* register data write */
	data = (cmd << 16) | data;

	ret = spi_write(spi, (unsigned char *)&data, 4);
	if (ret)
		pr_err("error in spi_write %x\n", data);

	return ret;
}

static int init_nec_8048_wvga_lcd(struct spi_device *spi)
{
	unsigned int i;
	/* Initialization Sequence */
	/* nec_8048_spi_send(spi, REG, VAL) */
	for (i = 0; i < (ARRAY_SIZE(nec_8048_init_seq) - 1); i++)
		nec_8048_spi_send(spi, nec_8048_init_seq[i].addr,
				nec_8048_init_seq[i].dat);
	udelay(20);
	nec_8048_spi_send(spi, nec_8048_init_seq[i].addr,
				nec_8048_init_seq[i].dat);
	return 0;
}

static int nec_8048_connect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;
	int r;

	if (omapdss_device_is_connected(dssdev))
		return 0;

	r = in->ops.dpi->connect(in, dssdev);
	if (r)
		return r;

	return 0;
}

static void nec_8048_disconnect(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_connected(dssdev))
		return;

	in->ops.dpi->disconnect(in, dssdev);
}

static int nec_8048_enable(struct omap_dss_device *dssdev)
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

	if (gpio_is_valid(ddata->res_gpio))
		gpio_set_value_cansleep(ddata->res_gpio, 1);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void nec_8048_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	if (!omapdss_device_is_enabled(dssdev))
		return;

	if (gpio_is_valid(ddata->res_gpio))
		gpio_set_value_cansleep(ddata->res_gpio, 0);

	in->ops.dpi->disable(in);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static void nec_8048_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	ddata->videomode = *timings;
	dssdev->panel.timings = *timings;

	in->ops.dpi->set_timings(in, timings);
}

static void nec_8048_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	*timings = ddata->videomode;
}

static int nec_8048_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	struct omap_dss_device *in = ddata->in;

	return in->ops.dpi->check_timings(in, timings);
}

static struct omap_dss_driver nec_8048_ops = {
	.connect	= nec_8048_connect,
	.disconnect	= nec_8048_disconnect,

	.enable		= nec_8048_enable,
	.disable	= nec_8048_disable,

	.set_timings	= nec_8048_set_timings,
	.get_timings	= nec_8048_get_timings,
	.check_timings	= nec_8048_check_timings,

	.get_resolution	= omapdss_default_get_resolution,
};


static int nec_8048_probe_pdata(struct spi_device *spi)
{
	const struct panel_nec_nl8048hl11_platform_data *pdata;
	struct panel_drv_data *ddata = dev_get_drvdata(&spi->dev);
	struct omap_dss_device *dssdev, *in;

	pdata = dev_get_platdata(&spi->dev);

	ddata->qvga_gpio = pdata->qvga_gpio;
	ddata->res_gpio = pdata->res_gpio;

	in = omap_dss_find_output(pdata->source);
	if (in == NULL) {
		dev_err(&spi->dev, "failed to find video source '%s'\n",
				pdata->source);
		return -EPROBE_DEFER;
	}
	ddata->in = in;

	ddata->data_lines = pdata->data_lines;

	dssdev = &ddata->dssdev;
	dssdev->name = pdata->name;

	return 0;
}

static int nec_8048_probe_of(struct spi_device *spi)
{
	struct device_node *node = spi->dev.of_node;
	struct panel_drv_data *ddata = dev_get_drvdata(&spi->dev);
	struct omap_dss_device *in;
	int gpio;

	gpio = of_get_named_gpio(node, "reset-gpios", 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(&spi->dev, "failed to parse enable gpio\n");
		return gpio;
	}
	ddata->res_gpio = gpio;

	/* XXX the panel spec doesn't mention any QVGA pin?? */
	ddata->qvga_gpio = -ENOENT;

	in = omapdss_of_find_source_for_first_ep(node);
	if (IS_ERR(in)) {
		dev_err(&spi->dev, "failed to find video source\n");
		return PTR_ERR(in);
	}

	ddata->in = in;

	return 0;
}

static int nec_8048_probe(struct spi_device *spi)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	dev_dbg(&spi->dev, "%s\n", __func__);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 32;

	r = spi_setup(spi);
	if (r < 0) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", r);
		return r;
	}

	init_nec_8048_wvga_lcd(spi);

	ddata = devm_kzalloc(&spi->dev, sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, ddata);

	ddata->spi = spi;

	if (dev_get_platdata(&spi->dev)) {
		r = nec_8048_probe_pdata(spi);
		if (r)
			return r;
	} else if (spi->dev.of_node) {
		r = nec_8048_probe_of(spi);
		if (r)
			return r;
	} else {
		return -ENODEV;
	}

	if (gpio_is_valid(ddata->qvga_gpio)) {
		r = devm_gpio_request_one(&spi->dev, ddata->qvga_gpio,
				GPIOF_OUT_INIT_HIGH, "lcd QVGA");
		if (r)
			goto err_gpio;
	}

	if (gpio_is_valid(ddata->res_gpio)) {
		r = devm_gpio_request_one(&spi->dev, ddata->res_gpio,
				GPIOF_OUT_INIT_LOW, "lcd RES");
		if (r)
			goto err_gpio;
	}

	ddata->videomode = nec_8048_panel_timings;

	dssdev = &ddata->dssdev;
	dssdev->dev = &spi->dev;
	dssdev->driver = &nec_8048_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->owner = THIS_MODULE;
	dssdev->panel.timings = ddata->videomode;

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

static int nec_8048_remove(struct spi_device *spi)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&spi->dev);
	struct omap_dss_device *dssdev = &ddata->dssdev;
	struct omap_dss_device *in = ddata->in;

	dev_dbg(&ddata->spi->dev, "%s\n", __func__);

	omapdss_unregister_display(dssdev);

	nec_8048_disable(dssdev);
	nec_8048_disconnect(dssdev);

	omap_dss_put_device(in);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int nec_8048_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	nec_8048_spi_send(spi, 2, 0x01);
	mdelay(40);

	return 0;
}

static int nec_8048_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);

	/* reinitialize the panel */
	spi_setup(spi);
	nec_8048_spi_send(spi, 2, 0x00);
	init_nec_8048_wvga_lcd(spi);

	return 0;
}
static SIMPLE_DEV_PM_OPS(nec_8048_pm_ops, nec_8048_suspend,
		nec_8048_resume);
#define NEC_8048_PM_OPS (&nec_8048_pm_ops)
#else
#define NEC_8048_PM_OPS NULL
#endif

static const struct of_device_id nec_8048_of_match[] = {
	{ .compatible = "omapdss,nec,nl8048hl11", },
	{},
};

MODULE_DEVICE_TABLE(of, nec_8048_of_match);

static struct spi_driver nec_8048_driver = {
	.driver = {
		.name	= "panel-nec-nl8048hl11",
		.pm	= NEC_8048_PM_OPS,
		.of_match_table = nec_8048_of_match,
		.suppress_bind_attrs = true,
	},
	.probe	= nec_8048_probe,
	.remove	= nec_8048_remove,
};

module_spi_driver(nec_8048_driver);

MODULE_ALIAS("spi:nec,nl8048hl11");
MODULE_AUTHOR("Erik Gilling <konkers@android.com>");
MODULE_DESCRIPTION("NEC-NL8048HL11 Driver");
MODULE_LICENSE("GPL");
