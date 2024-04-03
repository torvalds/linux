// SPDX-License-Identifier: GPL-2.0
/*
 * Orisetech OTA5601A TFT LCD panel driver
 *
 * Copyright (C) 2021, Christophe Branchereau <cbranchereau@gmail.com>
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define OTA5601A_CTL 0x01
#define OTA5601A_CTL_OFF 0x00
#define OTA5601A_CTL_ON BIT(0)

struct ota5601a_panel_info {
	const struct drm_display_mode *display_modes;
	unsigned int num_modes;
	u16 width_mm, height_mm;
	u32 bus_format, bus_flags;
};

struct ota5601a {
	struct drm_panel drm_panel;
	struct regmap *map;
	struct regulator *supply;
	const struct ota5601a_panel_info *panel_info;

	struct gpio_desc *reset_gpio;
};

static inline struct ota5601a *to_ota5601a(struct drm_panel *panel)
{
	return container_of(panel, struct ota5601a, drm_panel);
}

static const struct reg_sequence ota5601a_panel_regs[] = {
	{ 0xfd, 0x00 }, /* Page Shift */
	{ 0x02, 0x00 }, /* Reset */

	{ 0x18, 0x00 }, /* Interface Sel: RGB 24 Bits */
	{ 0x34, 0x20 }, /* Undocumented */

	{ 0x0c, 0x01 }, /* Contrast set by CMD1 == within page 0x00 */
	{ 0x0d, 0x48 }, /* R Brightness */
	{ 0x0e, 0x48 }, /* G Brightness */
	{ 0x0f, 0x48 }, /* B Brightness */
	{ 0x07, 0x40 }, /* R Contrast */
	{ 0x08, 0x33 }, /* G Contrast */
	{ 0x09, 0x3a }, /* B Contrast */

	{ 0x16, 0x01 }, /* NTSC Sel */
	{ 0x19, 0x8d }, /* VBLK */
	{ 0x1a, 0x28 }, /* HBLK */
	{ 0x1c, 0x00 }, /* Scan Shift Dir. */

	{ 0xfd, 0xc5 }, /* Page Shift */
	{ 0x82, 0x0c }, /* PWR_CTRL Pump */
	{ 0xa2, 0xb4 }, /* PWR_CTRL VGH/VGL */

	{ 0xfd, 0xc4 }, /* Page Shift - What follows is listed as "RGB 24bit Timing Set" */
	{ 0x82, 0x45 },

	{ 0xfd, 0xc1 },
	{ 0x91, 0x02 },

	{ 0xfd, 0xc0 },
	{ 0xa1, 0x01 },
	{ 0xa2, 0x1f },
	{ 0xa3, 0x0b },
	{ 0xa4, 0x38 },
	{ 0xa5, 0x00 },
	{ 0xa6, 0x0a },
	{ 0xa7, 0x38 },
	{ 0xa8, 0x00 },
	{ 0xa9, 0x0a },
	{ 0xaa, 0x37 },

	{ 0xfd, 0xce },
	{ 0x81, 0x18 },
	{ 0x82, 0x43 },
	{ 0x83, 0x43 },
	{ 0x91, 0x06 },
	{ 0x93, 0x38 },
	{ 0x94, 0x02 },
	{ 0x95, 0x06 },
	{ 0x97, 0x38 },
	{ 0x98, 0x02 },
	{ 0x99, 0x06 },
	{ 0x9b, 0x38 },
	{ 0x9c, 0x02 },

	{ 0xfd, 0x00 }, /* Page Shift */
};

static const struct regmap_config ota5601a_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int ota5601a_prepare(struct drm_panel *drm_panel)
{
	struct ota5601a *panel = to_ota5601a(drm_panel);
	int err;

	err = regulator_enable(panel->supply);
	if (err) {
		dev_err(drm_panel->dev, "Failed to enable power supply: %d\n", err);
		return err;
	}

	/* Reset to be held low for 10us min according to the doc, 10ms before sending commands */
	gpiod_set_value_cansleep(panel->reset_gpio, 1);
	usleep_range(10, 30);
	gpiod_set_value_cansleep(panel->reset_gpio, 0);
	usleep_range(10000, 20000);

	/* Init all registers. */
	err = regmap_multi_reg_write(panel->map, ota5601a_panel_regs,
				     ARRAY_SIZE(ota5601a_panel_regs));
	if (err) {
		dev_err(drm_panel->dev, "Failed to init registers: %d\n", err);
		goto err_disable_regulator;
	}

	msleep(120);

	return 0;

err_disable_regulator:
	regulator_disable(panel->supply);
	return err;
}

static int ota5601a_unprepare(struct drm_panel *drm_panel)
{
	struct ota5601a *panel = to_ota5601a(drm_panel);

	gpiod_set_value_cansleep(panel->reset_gpio, 1);

	regulator_disable(panel->supply);

	return 0;
}

static int ota5601a_enable(struct drm_panel *drm_panel)
{
	struct ota5601a *panel = to_ota5601a(drm_panel);
	int err;

	err = regmap_write(panel->map, OTA5601A_CTL, OTA5601A_CTL_ON);

	if (err) {
		dev_err(drm_panel->dev, "Unable to enable panel: %d\n", err);
		return err;
	}

	if (drm_panel->backlight) {
		/* Wait for the picture to be ready before enabling backlight */
		msleep(120);
	}

	return 0;
}

static int ota5601a_disable(struct drm_panel *drm_panel)
{
	struct ota5601a *panel = to_ota5601a(drm_panel);
	int err;

	err = regmap_write(panel->map, OTA5601A_CTL, OTA5601A_CTL_OFF);

	if (err) {
		dev_err(drm_panel->dev, "Unable to disable panel: %d\n", err);
		return err;
	}

	return 0;
}

static int ota5601a_get_modes(struct drm_panel *drm_panel,
			      struct drm_connector *connector)
{
	struct ota5601a *panel = to_ota5601a(drm_panel);
	const struct ota5601a_panel_info *panel_info = panel->panel_info;
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

static const struct drm_panel_funcs ota5601a_funcs = {
	.prepare	= ota5601a_prepare,
	.unprepare	= ota5601a_unprepare,
	.enable		= ota5601a_enable,
	.disable	= ota5601a_disable,
	.get_modes	= ota5601a_get_modes,
};

static int ota5601a_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct device *dev = &spi->dev;
	struct ota5601a *panel;
	int err;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	spi_set_drvdata(spi, panel);

	panel->panel_info = (const struct ota5601a_panel_info *)id->driver_data;
	if (!panel->panel_info)
		return -EINVAL;

	panel->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(panel->supply)) {
		dev_err(dev, "Failed to get power supply\n");
		return PTR_ERR(panel->supply);
	}

	panel->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(panel->reset_gpio)) {
		dev_err(dev, "Failed to get reset GPIO\n");
		return PTR_ERR(panel->reset_gpio);
	}

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_3 | SPI_3WIRE;
	err = spi_setup(spi);
	if (err) {
		dev_err(dev, "Failed to setup SPI\n");
		return err;
	}

	panel->map = devm_regmap_init_spi(spi, &ota5601a_regmap_config);
	if (IS_ERR(panel->map)) {
		dev_err(dev, "Failed to init regmap\n");
		return PTR_ERR(panel->map);
	}

	drm_panel_init(&panel->drm_panel, dev, &ota5601a_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	err = drm_panel_of_backlight(&panel->drm_panel);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get backlight handle\n");
		return err;
	}

	drm_panel_add(&panel->drm_panel);

	return 0;
}

static void ota5601a_remove(struct spi_device *spi)
{
	struct ota5601a *panel = spi_get_drvdata(spi);

	drm_panel_remove(&panel->drm_panel);

	ota5601a_disable(&panel->drm_panel);
	ota5601a_unprepare(&panel->drm_panel);
}

static const struct drm_display_mode gpt3_display_modes[] = {
	{ /* 60 Hz */
		.clock = 27000,
		.hdisplay = 640,
		.hsync_start = 640 + 220,
		.hsync_end = 640 + 220 + 20,
		.htotal = 640 + 220 + 20 + 20,
		.vdisplay = 480,
		.vsync_start = 480 + 7,
		.vsync_end = 480 + 7 + 6,
		.vtotal = 480 + 7 + 6 + 7,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},

	{ /* 50 Hz */
		.clock = 24000,
		.hdisplay = 640,
		.hsync_start = 640 + 280,
		.hsync_end = 640 + 280 + 20,
		.htotal = 640 + 280 + 20 + 20,
		.vdisplay = 480,
		.vsync_start = 480 + 7,
		.vsync_end = 480 + 7 + 6,
		.vtotal = 480 + 7 + 6 + 7,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct ota5601a_panel_info gpt3_info = {
	.display_modes = gpt3_display_modes,
	.num_modes = ARRAY_SIZE(gpt3_display_modes),
	.width_mm = 71,
	.height_mm = 51,
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
};

static const struct spi_device_id gpt3_id[] = {
	{ "gpt3", (kernel_ulong_t)&gpt3_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, gpt3_id);

static const struct of_device_id ota5601a_of_match[] = {
	{ .compatible = "focaltech,gpt3" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ota5601a_of_match);

static struct spi_driver ota5601a_driver = {
	.driver = {
		.name = "ota5601a",
		.of_match_table = ota5601a_of_match,
	},
	.id_table = gpt3_id,
	.probe = ota5601a_probe,
	.remove = ota5601a_remove,
};

module_spi_driver(ota5601a_driver);

MODULE_AUTHOR("Christophe Branchereau <cbranchereau@gmail.com>");
MODULE_LICENSE("GPL");
