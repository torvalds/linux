// SPDX-License-Identifier: GPL-2.0
/*
 * Novatek NT39016 TFT LCD panel driver
 *
 * Copyright (C) 2017, Maarten ter Huurne <maarten@treewalker.org>
 * Copyright (C) 2019, Paul Cercueil <paul@crapouillou.net>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

enum nt39016_regs {
	NT39016_REG_SYSTEM,
	NT39016_REG_TIMING,
	NT39016_REG_OP,
	NT39016_REG_DATA_IN,
	NT39016_REG_SRC_TIMING_DELAY,
	NT39016_REG_GATE_TIMING_DELAY,
	NT39016_REG_RESERVED,
	NT39016_REG_INITIAL_FUNC,
	NT39016_REG_CONTRAST,
	NT39016_REG_BRIGHTNESS,
	NT39016_REG_HUE_SATURATION,
	NT39016_REG_RB_SUBCONTRAST,
	NT39016_REG_R_SUBBRIGHTNESS,
	NT39016_REG_B_SUBBRIGHTNESS,
	NT39016_REG_VCOMDC,
	NT39016_REG_VCOMAC,
	NT39016_REG_VGAM2,
	NT39016_REG_VGAM34,
	NT39016_REG_VGAM56,
	NT39016_REG_VCOMDC_TRIM = 0x1e,
	NT39016_REG_DISPLAY_MODE = 0x20,
};

#define NT39016_SYSTEM_RESET_N	BIT(0)
#define NT39016_SYSTEM_STANDBY	BIT(1)

struct nt39016_panel_info {
	const struct drm_display_mode *display_modes;
	unsigned int num_modes;
	u16 width_mm, height_mm;
	u32 bus_format, bus_flags;
};

struct nt39016 {
	struct drm_panel drm_panel;
	struct device *dev;
	struct regmap *map;
	struct regulator *supply;
	const struct nt39016_panel_info *panel_info;

	struct gpio_desc *reset_gpio;

	struct backlight_device *backlight;
};

static inline struct nt39016 *to_nt39016(struct drm_panel *panel)
{
	return container_of(panel, struct nt39016, drm_panel);
}

#define RV(REG, VAL) { .reg = (REG), .def = (VAL), .delay_us = 2 }
static const struct reg_sequence nt39016_panel_regs[] = {
	RV(NT39016_REG_SYSTEM, 0x00),
	RV(NT39016_REG_TIMING, 0x00),
	RV(NT39016_REG_OP, 0x03),
	RV(NT39016_REG_DATA_IN, 0xCC),
	RV(NT39016_REG_SRC_TIMING_DELAY, 0x46),
	RV(NT39016_REG_GATE_TIMING_DELAY, 0x05),
	RV(NT39016_REG_RESERVED, 0x00),
	RV(NT39016_REG_INITIAL_FUNC, 0x00),
	RV(NT39016_REG_CONTRAST, 0x08),
	RV(NT39016_REG_BRIGHTNESS, 0x40),
	RV(NT39016_REG_HUE_SATURATION, 0x88),
	RV(NT39016_REG_RB_SUBCONTRAST, 0x88),
	RV(NT39016_REG_R_SUBBRIGHTNESS, 0x20),
	RV(NT39016_REG_B_SUBBRIGHTNESS, 0x20),
	RV(NT39016_REG_VCOMDC, 0x67),
	RV(NT39016_REG_VCOMAC, 0xA4),
	RV(NT39016_REG_VGAM2, 0x04),
	RV(NT39016_REG_VGAM34, 0x24),
	RV(NT39016_REG_VGAM56, 0x24),
	RV(NT39016_REG_DISPLAY_MODE, 0x00),
};

#undef RV

static const struct regmap_range nt39016_regmap_no_ranges[] = {
	regmap_reg_range(0x13, 0x1D),
	regmap_reg_range(0x1F, 0x1F),
};

static const struct regmap_access_table nt39016_regmap_access_table = {
	.no_ranges = nt39016_regmap_no_ranges,
	.n_no_ranges = ARRAY_SIZE(nt39016_regmap_no_ranges),
};

static const struct regmap_config nt39016_regmap_config = {
	.reg_bits = 6,
	.pad_bits = 2,
	.val_bits = 8,

	.max_register = NT39016_REG_DISPLAY_MODE,
	.wr_table = &nt39016_regmap_access_table,
	.write_flag_mask = 0x02,

	.cache_type = REGCACHE_FLAT,
};

static int nt39016_prepare(struct drm_panel *drm_panel)
{
	struct nt39016 *panel = to_nt39016(drm_panel);
	int err;

	err = regulator_enable(panel->supply);
	if (err) {
		dev_err(panel->dev, "Failed to enable power supply: %d", err);
		return err;
	}

	/*
	 * Reset the NT39016.
	 * The documentation says the reset pulse should be at least 40 us to
	 * pass the glitch filter, but when testing I see some resets fail and
	 * some succeed when using a 70 us delay, so we use 100 us instead.
	 */
	gpiod_set_value_cansleep(panel->reset_gpio, 1);
	usleep_range(100, 1000);
	gpiod_set_value_cansleep(panel->reset_gpio, 0);
	udelay(2);

	/* Init all registers. */
	err = regmap_multi_reg_write(panel->map, nt39016_panel_regs,
				     ARRAY_SIZE(nt39016_panel_regs));
	if (err) {
		dev_err(panel->dev, "Failed to init registers: %d", err);
		goto err_disable_regulator;
	}

	return 0;

err_disable_regulator:
	regulator_disable(panel->supply);
	return err;
}

static int nt39016_unprepare(struct drm_panel *drm_panel)
{
	struct nt39016 *panel = to_nt39016(drm_panel);

	gpiod_set_value_cansleep(panel->reset_gpio, 1);

	regulator_disable(panel->supply);

	return 0;
}

static int nt39016_enable(struct drm_panel *drm_panel)
{
	struct nt39016 *panel = to_nt39016(drm_panel);
	int ret;

	ret = regmap_write(panel->map, NT39016_REG_SYSTEM,
			   NT39016_SYSTEM_RESET_N | NT39016_SYSTEM_STANDBY);
	if (ret) {
		dev_err(panel->dev, "Unable to enable panel: %d", ret);
		return ret;
	}

	if (panel->backlight) {
		/* Wait for the picture to be ready before enabling backlight */
		msleep(150);

		ret = backlight_enable(panel->backlight);
	}

	return ret;
}

static int nt39016_disable(struct drm_panel *drm_panel)
{
	struct nt39016 *panel = to_nt39016(drm_panel);
	int err;

	backlight_disable(panel->backlight);

	err = regmap_write(panel->map, NT39016_REG_SYSTEM,
			   NT39016_SYSTEM_RESET_N);
	if (err) {
		dev_err(panel->dev, "Unable to disable panel: %d", err);
		return err;
	}

	return 0;
}

static int nt39016_get_modes(struct drm_panel *drm_panel,
			     struct drm_connector *connector)
{
	struct nt39016 *panel = to_nt39016(drm_panel);
	const struct nt39016_panel_info *panel_info = panel->panel_info;
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

static const struct drm_panel_funcs nt39016_funcs = {
	.prepare	= nt39016_prepare,
	.unprepare	= nt39016_unprepare,
	.enable		= nt39016_enable,
	.disable	= nt39016_disable,
	.get_modes	= nt39016_get_modes,
};

static int nt39016_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct nt39016 *panel;
	int err;

	panel = devm_kzalloc(dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	panel->dev = dev;
	spi_set_drvdata(spi, panel);

	panel->panel_info = of_device_get_match_data(dev);
	if (!panel->panel_info)
		return -EINVAL;

	panel->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(panel->supply)) {
		dev_err(dev, "Failed to get power supply");
		return PTR_ERR(panel->supply);
	}

	panel->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(panel->reset_gpio)) {
		dev_err(dev, "Failed to get reset GPIO");
		return PTR_ERR(panel->reset_gpio);
	}

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_3 | SPI_3WIRE;
	err = spi_setup(spi);
	if (err) {
		dev_err(dev, "Failed to setup SPI");
		return err;
	}

	panel->map = devm_regmap_init_spi(spi, &nt39016_regmap_config);
	if (IS_ERR(panel->map)) {
		dev_err(dev, "Failed to init regmap");
		return PTR_ERR(panel->map);
	}

	panel->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(panel->backlight)) {
		err = PTR_ERR(panel->backlight);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get backlight handle");
		return err;
	}

	drm_panel_init(&panel->drm_panel, dev, &nt39016_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	err = drm_panel_add(&panel->drm_panel);
	if (err < 0) {
		dev_err(dev, "Failed to register panel");
		return err;
	}

	return 0;
}

static int nt39016_remove(struct spi_device *spi)
{
	struct nt39016 *panel = spi_get_drvdata(spi);

	drm_panel_remove(&panel->drm_panel);

	nt39016_disable(&panel->drm_panel);
	nt39016_unprepare(&panel->drm_panel);

	return 0;
}

static const struct drm_display_mode kd035g6_display_modes[] = {
	{	/* 60 Hz */
		.clock = 6000,
		.hdisplay = 320,
		.hsync_start = 320 + 10,
		.hsync_end = 320 + 10 + 50,
		.htotal = 320 + 10 + 50 + 20,
		.vdisplay = 240,
		.vsync_start = 240 + 5,
		.vsync_end = 240 + 5 + 1,
		.vtotal = 240 + 5 + 1 + 4,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
	{	/* 50 Hz */
		.clock = 5400,
		.hdisplay = 320,
		.hsync_start = 320 + 42,
		.hsync_end = 320 + 42 + 50,
		.htotal = 320 + 42 + 50 + 20,
		.vdisplay = 240,
		.vsync_start = 240 + 5,
		.vsync_end = 240 + 5 + 1,
		.vtotal = 240 + 5 + 1 + 4,
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	},
};

static const struct nt39016_panel_info kd035g6_info = {
	.display_modes = kd035g6_display_modes,
	.num_modes = ARRAY_SIZE(kd035g6_display_modes),
	.width_mm = 71,
	.height_mm = 53,
	.bus_format = MEDIA_BUS_FMT_RGB888_1X24,
	.bus_flags = DRM_BUS_FLAG_PIXDATA_NEGEDGE,
};

static const struct of_device_id nt39016_of_match[] = {
	{ .compatible = "kingdisplay,kd035g6-54nt", .data = &kd035g6_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt39016_of_match);

static struct spi_driver nt39016_driver = {
	.driver = {
		.name = "nt39016",
		.of_match_table = nt39016_of_match,
	},
	.probe = nt39016_probe,
	.remove = nt39016_remove,
};

module_spi_driver(nt39016_driver);

MODULE_AUTHOR("Maarten ter Huurne <maarten@treewalker.org>");
MODULE_AUTHOR("Paul Cercueil <paul@crapouillou.net>");
MODULE_LICENSE("GPL v2");
