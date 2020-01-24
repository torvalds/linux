// SPDX-License-Identifier: GPL-2.0
/*
 * LG.Philips LB035Q02 LCD Panel Driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Based on the omapdrm-specific panel-lgphilips-lb035q02 driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * Based on a driver by: Steve Sakoman <steve@sakoman.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct lb035q02_device {
	struct drm_panel panel;

	struct spi_device *spi;
	struct gpio_desc *enable_gpio;
};

#define to_lb035q02_device(p) container_of(p, struct lb035q02_device, panel)

static int lb035q02_write(struct lb035q02_device *lcd, u16 reg, u16 val)
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

	return spi_sync(lcd->spi, &msg);
}

static int lb035q02_init(struct lb035q02_device *lcd)
{
	/* Init sequence from page 28 of the lb035q02 spec. */
	static const struct {
		u16 index;
		u16 value;
	} init_data[] = {
		{ 0x01, 0x6300 },
		{ 0x02, 0x0200 },
		{ 0x03, 0x0177 },
		{ 0x04, 0x04c7 },
		{ 0x05, 0xffc0 },
		{ 0x06, 0xe806 },
		{ 0x0a, 0x4008 },
		{ 0x0b, 0x0000 },
		{ 0x0d, 0x0030 },
		{ 0x0e, 0x2800 },
		{ 0x0f, 0x0000 },
		{ 0x16, 0x9f80 },
		{ 0x17, 0x0a0f },
		{ 0x1e, 0x00c1 },
		{ 0x30, 0x0300 },
		{ 0x31, 0x0007 },
		{ 0x32, 0x0000 },
		{ 0x33, 0x0000 },
		{ 0x34, 0x0707 },
		{ 0x35, 0x0004 },
		{ 0x36, 0x0302 },
		{ 0x37, 0x0202 },
		{ 0x3a, 0x0a0d },
		{ 0x3b, 0x0806 },
	};

	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(init_data); ++i) {
		ret = lb035q02_write(lcd, init_data[i].index,
				     init_data[i].value);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int lb035q02_disable(struct drm_panel *panel)
{
	struct lb035q02_device *lcd = to_lb035q02_device(panel);

	gpiod_set_value_cansleep(lcd->enable_gpio, 0);

	return 0;
}

static int lb035q02_enable(struct drm_panel *panel)
{
	struct lb035q02_device *lcd = to_lb035q02_device(panel);

	gpiod_set_value_cansleep(lcd->enable_gpio, 1);

	return 0;
}

static const struct drm_display_mode lb035q02_mode = {
	.clock = 6500,
	.hdisplay = 320,
	.hsync_start = 320 + 20,
	.hsync_end = 320 + 20 + 2,
	.htotal = 320 + 20 + 2 + 68,
	.vdisplay = 240,
	.vsync_start = 240 + 4,
	.vsync_end = 240 + 4 + 2,
	.vtotal = 240 + 4 + 2 + 18,
	.vrefresh = 60,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 70,
	.height_mm = 53,
};

static int lb035q02_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &lb035q02_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = lb035q02_mode.width_mm;
	connector->display_info.height_mm = lb035q02_mode.height_mm;
	/*
	 * FIXME: According to the datasheet pixel data is sampled on the
	 * rising edge of the clock, but the code running on the Gumstix Overo
	 * Palo35 indicates sampling on the negative edge. This should be
	 * tested on a real device.
	 */
	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_HIGH
					  | DRM_BUS_FLAG_SYNC_SAMPLE_POSEDGE
					  | DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE;

	return 1;
}

static const struct drm_panel_funcs lb035q02_funcs = {
	.disable = lb035q02_disable,
	.enable = lb035q02_enable,
	.get_modes = lb035q02_get_modes,
};

static int lb035q02_probe(struct spi_device *spi)
{
	struct lb035q02_device *lcd;
	int ret;

	lcd = devm_kzalloc(&spi->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	spi_set_drvdata(spi, lcd);
	lcd->spi = spi;

	lcd->enable_gpio = devm_gpiod_get(&spi->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->enable_gpio)) {
		dev_err(&spi->dev, "failed to parse enable gpio\n");
		return PTR_ERR(lcd->enable_gpio);
	}

	ret = lb035q02_init(lcd);
	if (ret < 0)
		return ret;

	drm_panel_init(&lcd->panel, &lcd->spi->dev, &lb035q02_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	return drm_panel_add(&lcd->panel);
}

static int lb035q02_remove(struct spi_device *spi)
{
	struct lb035q02_device *lcd = spi_get_drvdata(spi);

	drm_panel_remove(&lcd->panel);
	drm_panel_disable(&lcd->panel);

	return 0;
}

static const struct of_device_id lb035q02_of_match[] = {
	{ .compatible = "lgphilips,lb035q02", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, lb035q02_of_match);

static const struct spi_device_id lb035q02_ids[] = {
	{ "lb035q02", 0 },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(spi, lb035q02_ids);

static struct spi_driver lb035q02_driver = {
	.probe		= lb035q02_probe,
	.remove		= lb035q02_remove,
	.id_table	= lb035q02_ids,
	.driver		= {
		.name	= "panel-lg-lb035q02",
		.of_match_table = lb035q02_of_match,
	},
};

module_spi_driver(lb035q02_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("LG.Philips LB035Q02 LCD Panel driver");
MODULE_LICENSE("GPL");
