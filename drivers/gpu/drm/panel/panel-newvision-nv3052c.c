// SPDX-License-Identifier: GPL-2.0
/*
 * NewVision NV3052C IPS LCD panel driver
 *
 * Copyright (C) 2020, Paul Cercueil <paul@crapouillou.net>
 * Copyright (C) 2022, Christophe Branchereau <cbranchereau@gmail.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>
#include <drm/drm_mipi_dbi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct nv3052c_panel_info {
	const struct drm_display_mode *display_modes;
	unsigned int num_modes;
	u16 width_mm, height_mm;
	u32 bus_format, bus_flags;
};

struct nv3052c {
	struct device *dev;
	struct drm_panel panel;
	struct mipi_dbi dbi;
	const struct nv3052c_panel_info *panel_info;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
};

struct nv3052c_reg {
	u8 cmd;
	u8 val;
};

static const struct nv3052c_reg nv3052c_panel_regs[] = {
	{ 0xff, 0x30 },
	{ 0xff, 0x52 },
	{ 0xff, 0x01 },
	{ 0xe3, 0x00 },
	{ 0x40, 0x00 },
	{ 0x03, 0x40 },
	{ 0x04, 0x00 },
	{ 0x05, 0x03 },
	{ 0x08, 0x00 },
	{ 0x09, 0x07 },
	{ 0x0a, 0x01 },
	{ 0x0b, 0x32 },
	{ 0x0c, 0x32 },
	{ 0x0d, 0x0b },
	{ 0x0e, 0x00 },
	{ 0x23, 0xa0 },
	{ 0x24, 0x0c },
	{ 0x25, 0x06 },
	{ 0x26, 0x14 },
	{ 0x27, 0x14 },
	{ 0x38, 0xcc },
	{ 0x39, 0xd7 },
	{ 0x3a, 0x4a },
	{ 0x28, 0x40 },
	{ 0x29, 0x01 },
	{ 0x2a, 0xdf },
	{ 0x49, 0x3c },
	{ 0x91, 0x77 },
	{ 0x92, 0x77 },
	{ 0xa0, 0x55 },
	{ 0xa1, 0x50 },
	{ 0xa4, 0x9c },
	{ 0xa7, 0x02 },
	{ 0xa8, 0x01 },
	{ 0xa9, 0x01 },
	{ 0xaa, 0xfc },
	{ 0xab, 0x28 },
	{ 0xac, 0x06 },
	{ 0xad, 0x06 },
	{ 0xae, 0x06 },
	{ 0xaf, 0x03 },
	{ 0xb0, 0x08 },
	{ 0xb1, 0x26 },
	{ 0xb2, 0x28 },
	{ 0xb3, 0x28 },
	{ 0xb4, 0x33 },
	{ 0xb5, 0x08 },
	{ 0xb6, 0x26 },
	{ 0xb7, 0x08 },
	{ 0xb8, 0x26 },
	{ 0xf0, 0x00 },
	{ 0xf6, 0xc0 },
	{ 0xff, 0x30 },
	{ 0xff, 0x52 },
	{ 0xff, 0x02 },
	{ 0xb0, 0x0b },
	{ 0xb1, 0x16 },
	{ 0xb2, 0x17 },
	{ 0xb3, 0x2c },
	{ 0xb4, 0x32 },
	{ 0xb5, 0x3b },
	{ 0xb6, 0x29 },
	{ 0xb7, 0x40 },
	{ 0xb8, 0x0d },
	{ 0xb9, 0x05 },
	{ 0xba, 0x12 },
	{ 0xbb, 0x10 },
	{ 0xbc, 0x12 },
	{ 0xbd, 0x15 },
	{ 0xbe, 0x19 },
	{ 0xbf, 0x0e },
	{ 0xc0, 0x16 },
	{ 0xc1, 0x0a },
	{ 0xd0, 0x0c },
	{ 0xd1, 0x17 },
	{ 0xd2, 0x14 },
	{ 0xd3, 0x2e },
	{ 0xd4, 0x32 },
	{ 0xd5, 0x3c },
	{ 0xd6, 0x22 },
	{ 0xd7, 0x3d },
	{ 0xd8, 0x0d },
	{ 0xd9, 0x07 },
	{ 0xda, 0x13 },
	{ 0xdb, 0x13 },
	{ 0xdc, 0x11 },
	{ 0xdd, 0x15 },
	{ 0xde, 0x19 },
	{ 0xdf, 0x10 },
	{ 0xe0, 0x17 },
	{ 0xe1, 0x0a },
	{ 0xff, 0x30 },
	{ 0xff, 0x52 },
	{ 0xff, 0x03 },
	{ 0x00, 0x2a },
	{ 0x01, 0x2a },
	{ 0x02, 0x2a },
	{ 0x03, 0x2a },
	{ 0x04, 0x61 },
	{ 0x05, 0x80 },
	{ 0x06, 0xc7 },
	{ 0x07, 0x01 },
	{ 0x08, 0x03 },
	{ 0x09, 0x04 },
	{ 0x70, 0x22 },
	{ 0x71, 0x80 },
	{ 0x30, 0x2a },
	{ 0x31, 0x2a },
	{ 0x32, 0x2a },
	{ 0x33, 0x2a },
	{ 0x34, 0x61 },
	{ 0x35, 0xc5 },
	{ 0x36, 0x80 },
	{ 0x37, 0x23 },
	{ 0x40, 0x03 },
	{ 0x41, 0x04 },
	{ 0x42, 0x05 },
	{ 0x43, 0x06 },
	{ 0x44, 0x11 },
	{ 0x45, 0xe8 },
	{ 0x46, 0xe9 },
	{ 0x47, 0x11 },
	{ 0x48, 0xea },
	{ 0x49, 0xeb },
	{ 0x50, 0x07 },
	{ 0x51, 0x08 },
	{ 0x52, 0x09 },
	{ 0x53, 0x0a },
	{ 0x54, 0x11 },
	{ 0x55, 0xec },
	{ 0x56, 0xed },
	{ 0x57, 0x11 },
	{ 0x58, 0xef },
	{ 0x59, 0xf0 },
	{ 0xb1, 0x01 },
	{ 0xb4, 0x15 },
	{ 0xb5, 0x16 },
	{ 0xb6, 0x09 },
	{ 0xb7, 0x0f },
	{ 0xb8, 0x0d },
	{ 0xb9, 0x0b },
	{ 0xba, 0x00 },
	{ 0xc7, 0x02 },
	{ 0xca, 0x17 },
	{ 0xcb, 0x18 },
	{ 0xcc, 0x0a },
	{ 0xcd, 0x10 },
	{ 0xce, 0x0e },
	{ 0xcf, 0x0c },
	{ 0xd0, 0x00 },
	{ 0x81, 0x00 },
	{ 0x84, 0x15 },
	{ 0x85, 0x16 },
	{ 0x86, 0x10 },
	{ 0x87, 0x0a },
	{ 0x88, 0x0c },
	{ 0x89, 0x0e },
	{ 0x8a, 0x02 },
	{ 0x97, 0x00 },
	{ 0x9a, 0x17 },
	{ 0x9b, 0x18 },
	{ 0x9c, 0x0f },
	{ 0x9d, 0x09 },
	{ 0x9e, 0x0b },
	{ 0x9f, 0x0d },
	{ 0xa0, 0x01 },
	{ 0xff, 0x30 },
	{ 0xff, 0x52 },
	{ 0xff, 0x02 },
	{ 0x01, 0x01 },
	{ 0x02, 0xda },
	{ 0x03, 0xba },
	{ 0x04, 0xa8 },
	{ 0x05, 0x9a },
	{ 0x06, 0x70 },
	{ 0x07, 0xff },
	{ 0x08, 0x91 },
	{ 0x09, 0x90 },
	{ 0x0a, 0xff },
	{ 0x0b, 0x8f },
	{ 0x0c, 0x60 },
	{ 0x0d, 0x58 },
	{ 0x0e, 0x48 },
	{ 0x0f, 0x38 },
	{ 0x10, 0x2b },
	{ 0xff, 0x30 },
	{ 0xff, 0x52 },
	{ 0xff, 0x00 },
	{ 0x36, 0x0a },
};

static inline struct nv3052c *to_nv3052c(struct drm_panel *panel)
{
	return container_of(panel, struct nv3052c, panel);
}

static int nv3052c_prepare(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	struct mipi_dbi *dbi = &priv->dbi;
	unsigned int i;
	int err;

	err = regulator_enable(priv->supply);
	if (err) {
		dev_err(priv->dev, "Failed to enable power supply: %d\n", err);
		return err;
	}

	/* Reset the chip */
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(10, 1000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(5000, 20000);

	for (i = 0; i < ARRAY_SIZE(nv3052c_panel_regs); i++) {
		err = mipi_dbi_command(dbi, nv3052c_panel_regs[i].cmd,
				       nv3052c_panel_regs[i].val);

		if (err) {
			dev_err(priv->dev, "Unable to set register: %d\n", err);
			goto err_disable_regulator;
		}
	}

	err = mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	if (err) {
		dev_err(priv->dev, "Unable to exit sleep mode: %d\n", err);
		goto err_disable_regulator;
	}

	return 0;

err_disable_regulator:
	regulator_disable(priv->supply);
	return err;
}

static int nv3052c_unprepare(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	struct mipi_dbi *dbi = &priv->dbi;
	int err;

	err = mipi_dbi_command(dbi, MIPI_DCS_ENTER_SLEEP_MODE);
	if (err)
		dev_err(priv->dev, "Unable to enter sleep mode: %d\n", err);

	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_disable(priv->supply);

	return 0;
}

static int nv3052c_enable(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	struct mipi_dbi *dbi = &priv->dbi;
	int err;

	err = mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);
	if (err) {
		dev_err(priv->dev, "Unable to enable display: %d\n", err);
		return err;
	}

	if (panel->backlight) {
		/* Wait for the picture to be ready before enabling backlight */
		msleep(120);
	}

	return 0;
}

static int nv3052c_disable(struct drm_panel *panel)
{
	struct nv3052c *priv = to_nv3052c(panel);
	struct mipi_dbi *dbi = &priv->dbi;
	int err;

	err = mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_OFF);
	if (err) {
		dev_err(priv->dev, "Unable to disable display: %d\n", err);
		return err;
	}

	return 0;
}

static int nv3052c_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct nv3052c *priv = to_nv3052c(panel);
	const struct nv3052c_panel_info *panel_info = priv->panel_info;
	struct drm_display_mode *mode;
	unsigned int i;

	for (i = 0; i < panel_info->num_modes; i++) {
		mode = drm_mode_duplicate(connector->dev,
					  &panel_info->display_modes[i]);
		if (!mode)
			return -ENOMEM;

		drm_mode_set_name(mode);

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (panel_info->num_modes == 1)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = panel_info->width_mm;
	connector->display_info.height_mm = panel_info->height_mm;

	drm_display_info_set_bus_formats(&connector->display_info,
					 &panel_info->bus_format, 1);
	connector->display_info.bus_flags = panel_info->bus_flags;

	return panel_info->num_modes;
}

static const struct drm_panel_funcs nv3052c_funcs = {
	.prepare	= nv3052c_prepare,
	.unprepare	= nv3052c_unprepare,
	.enable		= nv3052c_enable,
	.disable	= nv3052c_disable,
	.get_modes	= nv3052c_get_modes,
};

static int nv3052c_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct nv3052c *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	priv->panel_info = of_device_get_match_data(dev);
	if (!priv->panel_info)
		return -EINVAL;

	priv->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(priv->supply))
		return dev_err_probe(dev, PTR_ERR(priv->supply), "Failed to get power supply\n");

	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio), "Failed to get reset GPIO\n");

	err = mipi_dbi_spi_init(spi, &priv->dbi, NULL);
	if (err)
		return dev_err_probe(dev, err, "MIPI DBI init failed\n");

	priv->dbi.read_commands = NULL;

	spi_set_drvdata(spi, priv);

	drm_panel_init(&priv->panel, dev, &nv3052c_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	err = drm_panel_of_backlight(&priv->panel);
	if (err)
		return dev_err_probe(dev, err, "Failed to attach backlight\n");

	drm_panel_add(&priv->panel);

	return 0;
}

static void nv3052c_remove(struct spi_device *spi)
{
	struct nv3052c *priv = spi_get_drvdata(spi);

	drm_panel_remove(&priv->panel);
	drm_panel_disable(&priv->panel);
	drm_panel_unprepare(&priv->panel);
}

static const struct drm_display_mode ltk035c5444t_modes[] = {
	{ /* 60 Hz */
		.clock = 24000,
		.hdisplay = 640,
		.hsync_start = 640 + 96,
		.hsync_end = 640 + 96 + 16,
		.htotal = 640 + 96 + 16 + 48,
		.vdisplay = 480,
		.vsync_start = 480 + 5,
		.vsync_end = 480 + 5 + 2,
		.vtotal = 480 + 5 + 2 + 13,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{ /* 50 Hz */
		.clock = 18000,
		.hdisplay = 640,
		.hsync_start = 640 + 39,
		.hsync_end = 640 + 39 + 2,
		.htotal = 640 + 39 + 2 + 39,
		.vdisplay = 480,
		.vsync_start = 480 + 5,
		.vsync_end = 480 + 5 + 2,
		.vtotal = 480 + 5 + 2 + 13,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct nv3052c_panel_info ltk035c5444t_panel_info = {
	.display_modes = ltk035c5444t_modes,
	.num_modes = ARRAY_SIZE(ltk035c5444t_modes),
	.width_mm = 77,
	.height_mm = 64,
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
};

static const struct of_device_id nv3052c_of_match[] = {
	{ .compatible = "leadtek,ltk035c5444t", .data = &ltk035c5444t_panel_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nv3052c_of_match);

static struct spi_driver nv3052c_driver = {
	.driver = {
		.name = "nv3052c",
		.of_match_table = nv3052c_of_match,
	},
	.probe = nv3052c_probe,
	.remove = nv3052c_remove,
};
module_spi_driver(nv3052c_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_AUTHOR("Christophe Branchereau <cbranchereau@gmail.com>");
MODULE_LICENSE("GPL v2");
