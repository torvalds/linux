// SPDX-License-Identifier: GPL-2.0+
/*
 * NEC NL8048HL11 Panel Driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Based on the omapdrm-specific panel-nec-nl8048hl11 driver
 *
 * Copyright (C) 2010 Texas Instruments Incorporated
 * Author: Erik Gilling <konkers@android.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/spi/spi.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nl8048_panel {
	struct drm_panel panel;

	struct spi_device *spi;
	struct gpio_desc *reset_gpio;
};

#define to_nl8048_device(p) container_of(p, struct nl8048_panel, panel)

static int nl8048_write(struct nl8048_panel *lcd, unsigned char addr,
			unsigned char value)
{
	u8 data[4] = { value, 0x01, addr, 0x00 };
	int ret;

	ret = spi_write(lcd->spi, data, sizeof(data));
	if (ret)
		dev_err(&lcd->spi->dev, "SPI write to %u failed: %d\n",
			addr, ret);

	return ret;
}

static int nl8048_init(struct nl8048_panel *lcd)
{
	static const struct {
		unsigned char addr;
		unsigned char data;
	} nl8048_init_seq[] = {
		{   3, 0x01 }, {   0, 0x00 }, {   1, 0x01 }, {   4, 0x00 },
		{   5, 0x14 }, {   6, 0x24 }, {  16, 0xd7 }, {  17, 0x00 },
		{  18, 0x00 }, {  19, 0x55 }, {  20, 0x01 }, {  21, 0x70 },
		{  22, 0x1e }, {  23, 0x25 }, {  24, 0x25 }, {  25, 0x02 },
		{  26, 0x02 }, {  27, 0xa0 }, {  32, 0x2f }, {  33, 0x0f },
		{  34, 0x0f }, {  35, 0x0f }, {  36, 0x0f }, {  37, 0x0f },
		{  38, 0x0f }, {  39, 0x00 }, {  40, 0x02 }, {  41, 0x02 },
		{  42, 0x02 }, {  43, 0x0f }, {  44, 0x0f }, {  45, 0x0f },
		{  46, 0x0f }, {  47, 0x0f }, {  48, 0x0f }, {  49, 0x0f },
		{  50, 0x00 }, {  51, 0x02 }, {  52, 0x02 }, {  53, 0x02 },
		{  80, 0x0c }, {  83, 0x42 }, {  84, 0x42 }, {  85, 0x41 },
		{  86, 0x14 }, {  89, 0x88 }, {  90, 0x01 }, {  91, 0x00 },
		{  92, 0x02 }, {  93, 0x0c }, {  94, 0x1c }, {  95, 0x27 },
		{  98, 0x49 }, {  99, 0x27 }, { 102, 0x76 }, { 103, 0x27 },
		{ 112, 0x01 }, { 113, 0x0e }, { 114, 0x02 }, { 115, 0x0c },
		{ 118, 0x0c }, { 121, 0x30 }, { 130, 0x00 }, { 131, 0x00 },
		{ 132, 0xfc }, { 134, 0x00 }, { 136, 0x00 }, { 138, 0x00 },
		{ 139, 0x00 }, { 140, 0x00 }, { 141, 0xfc }, { 143, 0x00 },
		{ 145, 0x00 }, { 147, 0x00 }, { 148, 0x00 }, { 149, 0x00 },
		{ 150, 0xfc }, { 152, 0x00 }, { 154, 0x00 }, { 156, 0x00 },
		{ 157, 0x00 },
	};

	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(nl8048_init_seq); ++i) {
		ret = nl8048_write(lcd, nl8048_init_seq[i].addr,
				   nl8048_init_seq[i].data);
		if (ret < 0)
			return ret;
	}

	udelay(20);

	return nl8048_write(lcd, 2, 0x00);
}

static int nl8048_disable(struct drm_panel *panel)
{
	struct nl8048_panel *lcd = to_nl8048_device(panel);

	gpiod_set_value_cansleep(lcd->reset_gpio, 0);

	return 0;
}

static int nl8048_enable(struct drm_panel *panel)
{
	struct nl8048_panel *lcd = to_nl8048_device(panel);

	gpiod_set_value_cansleep(lcd->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode nl8048_mode = {
	/*  NEC PIX Clock Ratings MIN:21.8MHz TYP:23.8MHz MAX:25.7MHz */
	.clock	= 23800,
	.hdisplay = 800,
	.hsync_start = 800 + 6,
	.hsync_end = 800 + 6 + 1,
	.htotal = 800 + 6 + 1 + 4,
	.vdisplay = 480,
	.vsync_start = 480 + 3,
	.vsync_end = 480 + 3 + 1,
	.vtotal = 480 + 3 + 1 + 4,
	.vrefresh = 60,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 89,
	.height_mm = 53,
};

static int nl8048_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &nl8048_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = nl8048_mode.width_mm;
	connector->display_info.height_mm = nl8048_mode.height_mm;
	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_HIGH
					  | DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE
					  | DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE;

	return 1;
}

static const struct drm_panel_funcs nl8048_funcs = {
	.disable = nl8048_disable,
	.enable = nl8048_enable,
	.get_modes = nl8048_get_modes,
};

static int __maybe_unused nl8048_suspend(struct device *dev)
{
	struct nl8048_panel *lcd = dev_get_drvdata(dev);

	nl8048_write(lcd, 2, 0x01);
	msleep(40);

	return 0;
}

static int __maybe_unused nl8048_resume(struct device *dev)
{
	struct nl8048_panel *lcd = dev_get_drvdata(dev);

	/* Reinitialize the panel. */
	spi_setup(lcd->spi);
	nl8048_write(lcd, 2, 0x00);
	nl8048_init(lcd);

	return 0;
}

static SIMPLE_DEV_PM_OPS(nl8048_pm_ops, nl8048_suspend, nl8048_resume);

static int nl8048_probe(struct spi_device *spi)
{
	struct nl8048_panel *lcd;
	int ret;

	lcd = devm_kzalloc(&spi->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	spi_set_drvdata(spi, lcd);
	lcd->spi = spi;

	lcd->reset_gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->reset_gpio)) {
		dev_err(&spi->dev, "failed to parse reset gpio\n");
		return PTR_ERR(lcd->reset_gpio);
	}

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 32;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "failed to setup SPI: %d\n", ret);
		return ret;
	}

	ret = nl8048_init(lcd);
	if (ret < 0)
		return ret;

	drm_panel_init(&lcd->panel, &lcd->spi->dev, &nl8048_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	return drm_panel_add(&lcd->panel);
}

static int nl8048_remove(struct spi_device *spi)
{
	struct nl8048_panel *lcd = spi_get_drvdata(spi);

	drm_panel_remove(&lcd->panel);
	drm_panel_disable(&lcd->panel);
	drm_panel_unprepare(&lcd->panel);

	return 0;
}

static const struct of_device_id nl8048_of_match[] = {
	{ .compatible = "nec,nl8048hl11", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, nl8048_of_match);

static const struct spi_device_id nl8048_ids[] = {
	{ "nl8048hl11", 0 },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(spi, nl8048_ids);

static struct spi_driver nl8048_driver = {
	.probe		= nl8048_probe,
	.remove		= nl8048_remove,
	.id_table	= nl8048_ids,
	.driver		= {
		.name	= "panel-nec-nl8048hl11",
		.pm	= &nl8048_pm_ops,
		.of_match_table = nl8048_of_match,
	},
};

module_spi_driver(nl8048_driver);

MODULE_AUTHOR("Erik Gilling <konkers@android.com>");
MODULE_DESCRIPTION("NEC-NL8048HL11 Driver");
MODULE_LICENSE("GPL");
