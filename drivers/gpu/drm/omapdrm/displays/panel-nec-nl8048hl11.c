// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NEC NL8048HL11 Panel driver
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Erik Gilling <konkers@android.com>
 * Converted to new DSS device model: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device	dssdev;

	struct videomode vm;

	struct gpio_desc *res_gpio;

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

static const struct videomode nec_8048_panel_vm = {
	.hactive	= LCD_XRES,
	.vactive	= LCD_YRES,
	.pixelclock	= LCD_PIXEL_CLOCK,
	.hfront_porch	= 6,
	.hsync_len	= 1,
	.hback_porch	= 4,
	.vfront_porch	= 3,
	.vsync_len	= 1,
	.vback_porch	= 4,

	.flags		= DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW,
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

static int nec_8048_connect(struct omap_dss_device *src,
			    struct omap_dss_device *dst)
{
	return 0;
}

static void nec_8048_disconnect(struct omap_dss_device *src,
				struct omap_dss_device *dst)
{
}

static void nec_8048_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	gpiod_set_value_cansleep(ddata->res_gpio, 1);
}

static void nec_8048_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	gpiod_set_value_cansleep(ddata->res_gpio, 0);
}

static int nec_8048_get_modes(struct omap_dss_device *dssdev,
			      struct drm_connector *connector)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	return omapdss_display_get_modes(connector, &ddata->vm);
}

static const struct omap_dss_device_ops nec_8048_ops = {
	.connect	= nec_8048_connect,
	.disconnect	= nec_8048_disconnect,

	.enable		= nec_8048_enable,
	.disable	= nec_8048_disable,

	.get_modes	= nec_8048_get_modes,
};

static int nec_8048_probe(struct spi_device *spi)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	struct gpio_desc *gpio;
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

	gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio)) {
		dev_err(&spi->dev, "failed to get reset gpio\n");
		return PTR_ERR(gpio);
	}

	ddata->res_gpio = gpio;

	ddata->vm = nec_8048_panel_vm;

	dssdev = &ddata->dssdev;
	dssdev->dev = &spi->dev;
	dssdev->ops = &nec_8048_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->display = true;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(0);
	dssdev->ops_flags = OMAP_DSS_DEVICE_OP_MODES;
	dssdev->bus_flags = DRM_BUS_FLAG_DE_HIGH
			  | DRM_BUS_FLAG_SYNC_DRIVE_POSEDGE
			  | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;

	omapdss_display_init(dssdev);
	omapdss_device_register(dssdev);

	return 0;
}

static int nec_8048_remove(struct spi_device *spi)
{
	struct panel_drv_data *ddata = dev_get_drvdata(&spi->dev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	dev_dbg(&ddata->spi->dev, "%s\n", __func__);

	omapdss_device_unregister(dssdev);

	nec_8048_disable(dssdev);

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
