// SPDX-License-Identifier: GPL-2.0
/*
 * Innolux/Chimei EJ030NA TFT LCD panel driver
 *
 * Copyright (C) 2020, Paul Cercueil <paul@crapouillou.net>
 * Copyright (C) 2020, Christophe Branchereau <cbranchereau@gmail.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct ej030na_info {
	const struct drm_display_mode *display_modes;
	unsigned int num_modes;
	u16 width_mm, height_mm;
	u32 bus_format, bus_flags;
};

struct ej030na {
	struct drm_panel panel;
	struct spi_device *spi;
	struct regmap *map;

	const struct ej030na_info *panel_info;

	struct regulator *supply;
	struct gpio_desc *reset_gpio;
};

static inline struct ej030na *to_ej030na(struct drm_panel *panel)
{
	return container_of(panel, struct ej030na, panel);
}

static const struct reg_sequence ej030na_init_sequence[] = {
	{ 0x05, 0x1e },
	{ 0x05, 0x5c },
	{ 0x02, 0x14 },
	{ 0x03, 0x40 },
	{ 0x04, 0x07 },
	{ 0x06, 0x12 },
	{ 0x07, 0xd2 },
	{ 0x0c, 0x06 },
	{ 0x0d, 0x40 },
	{ 0x0e, 0x40 },
	{ 0x0f, 0x40 },
	{ 0x10, 0x40 },
	{ 0x11, 0x40 },
	{ 0x2f, 0x40 },
	{ 0x5a, 0x02 },

	{ 0x30, 0x07 },
	{ 0x31, 0x57 },
	{ 0x32, 0x53 },
	{ 0x33, 0x77 },
	{ 0x34, 0xb8 },
	{ 0x35, 0xbd },
	{ 0x36, 0xb8 },
	{ 0x37, 0xe7 },
	{ 0x38, 0x04 },
	{ 0x39, 0xff },

	{ 0x40, 0x0b },
	{ 0x41, 0xb8 },
	{ 0x42, 0xab },
	{ 0x43, 0xb9 },
	{ 0x44, 0x6a },
	{ 0x45, 0x56 },
	{ 0x46, 0x61 },
	{ 0x47, 0x08 },
	{ 0x48, 0x0f },
	{ 0x49, 0x0f },

	{ 0x2b, 0x01 },
};

static int ej030na_prepare(struct drm_panel *panel)
{
	struct ej030na *priv = to_ej030na(panel);
	struct device *dev = &priv->spi->dev;
	int err;

	err = regulator_enable(priv->supply);
	if (err) {
		dev_err(dev, "Failed to enable power supply: %d\n", err);
		return err;
	}

	/* Reset the chip */
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(50, 150);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(50, 150);

	err = regmap_multi_reg_write(priv->map, ej030na_init_sequence,
				     ARRAY_SIZE(ej030na_init_sequence));
	if (err) {
		dev_err(dev, "Failed to init registers: %d\n", err);
		goto err_disable_regulator;
	}

	msleep(120);

	return 0;

err_disable_regulator:
	regulator_disable(priv->supply);
	return err;
}

static int ej030na_unprepare(struct drm_panel *panel)
{
	struct ej030na *priv = to_ej030na(panel);

	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	regulator_disable(priv->supply);

	return 0;
}

static int ej030na_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct ej030na *priv = to_ej030na(panel);
	const struct ej030na_info *panel_info = priv->panel_info;
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

static const struct drm_panel_funcs ej030na_funcs = {
	.prepare	= ej030na_prepare,
	.unprepare	= ej030na_unprepare,
	.get_modes	= ej030na_get_modes,
};

static const struct regmap_config ej030na_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x5a,
};

static int ej030na_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct ej030na *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	spi_set_drvdata(spi, priv);

	priv->map = devm_regmap_init_spi(spi, &ej030na_regmap_config);
	if (IS_ERR(priv->map)) {
		dev_err(dev, "Unable to init regmap\n");
		return PTR_ERR(priv->map);
	}

	priv->panel_info = of_device_get_match_data(dev);
	if (!priv->panel_info)
		return -EINVAL;

	priv->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(priv->supply))
		return dev_err_probe(dev, PTR_ERR(priv->supply),
				     "Failed to get power supply\n");

	priv->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "Failed to get reset GPIO\n");

	drm_panel_init(&priv->panel, dev, &ej030na_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	err = drm_panel_of_backlight(&priv->panel);
	if (err)
		return err;

	drm_panel_add(&priv->panel);

	return 0;
}

static int ej030na_remove(struct spi_device *spi)
{
	struct ej030na *priv = spi_get_drvdata(spi);

	drm_panel_remove(&priv->panel);
	drm_panel_disable(&priv->panel);
	drm_panel_unprepare(&priv->panel);

	return 0;
}

static const struct drm_display_mode ej030na_modes[] = {
	{ /* 60 Hz */
		.clock = 14400,
		.hdisplay = 320,
		.hsync_start = 320 + 10,
		.hsync_end = 320 + 10 + 37,
		.htotal = 320 + 10 + 37 + 33,
		.vdisplay = 480,
		.vsync_start = 480 + 102,
		.vsync_end = 480 + 102 + 9 + 9,
		.vtotal = 480 + 102 + 9 + 9,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{ /* 50 Hz */
		.clock = 12000,
		.hdisplay = 320,
		.hsync_start = 320 + 10,
		.hsync_end = 320 + 10 + 37,
		.htotal = 320 + 10 + 37 + 33,
		.vdisplay = 480,
		.vsync_start = 480 + 102,
		.vsync_end = 480 + 102 + 9,
		.vtotal = 480 + 102 + 9 + 9,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct ej030na_info ej030na_info = {
	.display_modes = ej030na_modes,
	.num_modes = ARRAY_SIZE(ej030na_modes),
	.width_mm = 70,
	.height_mm = 51,
	.bus_format = MEDIA_BUS_FMT_RGB888_3X8_DELTA,
	.bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE | DRM_BUS_FLAG_DE_LOW,
};

static const struct of_device_id ej030na_of_match[] = {
	{ .compatible = "innolux,ej030na", .data = &ej030na_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ej030na_of_match);

static struct spi_driver ej030na_driver = {
	.driver = {
		.name = "panel-innolux-ej030na",
		.of_match_table = ej030na_of_match,
	},
	.probe = ej030na_probe,
	.remove = ej030na_remove,
};
module_spi_driver(ej030na_driver);

MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_AUTHOR("Christophe Branchereau <cbranchereau@gmail.com>");
MODULE_LICENSE("GPL v2");
