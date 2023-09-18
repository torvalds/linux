// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 InforceComputing
 * Copyright (C) 2016 Linaro Ltd
 * Copyright (C) 2023 BayLibre, SAS
 *
 * Authors:
 * - Vinay Simha BN <simhavcs@gmail.com>
 * - Sumit Semwal <sumit.semwal@linaro.org>
 * - Guillaume La Roque <glaroque@baylibre.com>
 *
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DSI_REG_MCAP	0xB0
#define DSI_REG_IS	0xB3 /* Interface Setting */
#define DSI_REG_IIS	0xB4 /* Interface ID Setting */
#define DSI_REG_CTRL	0xB6

enum {
	IOVCC = 0,
	POWER = 1
};

struct stk_panel {
	bool prepared;
	const struct drm_display_mode *mode;
	struct backlight_device *backlight;
	struct drm_panel base;
	struct gpio_desc *enable_gpio; /* Power IC supply enable */
	struct gpio_desc *reset_gpio; /* External reset */
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
};

static inline struct stk_panel *to_stk_panel(struct drm_panel *panel)
{
	return container_of(panel, struct stk_panel, base);
}

static int stk_panel_init(struct stk_panel *stk)
{
	struct mipi_dsi_device *dsi = stk->dsi;
	struct device *dev = &stk->dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to mipi_dsi_dcs_soft_reset: %d\n", ret);
		return ret;
	}
	mdelay(5);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to set exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	mipi_dsi_generic_write_seq(dsi, DSI_REG_MCAP, 0x04);

	/* Interface setting, video mode */
	mipi_dsi_generic_write_seq(dsi, DSI_REG_IS, 0x14, 0x08, 0x00, 0x22, 0x00);
	mipi_dsi_generic_write_seq(dsi, DSI_REG_IIS, 0x0C, 0x00);
	mipi_dsi_generic_write_seq(dsi, DSI_REG_CTRL, 0x3A, 0xD3);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x77);
	if (ret < 0) {
		dev_err(dev, "failed to write display brightness: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
			       MIPI_DCS_WRITE_MEMORY_START);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, 0x77);
	if (ret < 0) {
		dev_err(dev, "failed to set pixel format: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_column_address(dsi, 0, stk->mode->hdisplay - 1);
	if (ret < 0) {
		dev_err(dev, "failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, stk->mode->vdisplay - 1);
	if (ret < 0) {
		dev_err(dev, "failed to set page address: %d\n", ret);
		return ret;
	}

	return 0;
}

static int stk_panel_on(struct stk_panel *stk)
{
	struct mipi_dsi_device *dsi = stk->dsi;
	struct device *dev = &stk->dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		dev_err(dev, "failed to set display on: %d\n", ret);

	mdelay(20);

	return ret;
}

static void stk_panel_off(struct stk_panel *stk)
{
	struct mipi_dsi_device *dsi = stk->dsi;
	struct device *dev = &stk->dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(dev, "failed to enter sleep mode: %d\n", ret);

	msleep(100);
}

static int stk_panel_unprepare(struct drm_panel *panel)
{
	struct stk_panel *stk = to_stk_panel(panel);

	if (!stk->prepared)
		return 0;

	stk_panel_off(stk);
	regulator_bulk_disable(ARRAY_SIZE(stk->supplies), stk->supplies);
	gpiod_set_value(stk->reset_gpio, 0);
	gpiod_set_value(stk->enable_gpio, 1);

	stk->prepared = false;

	return 0;
}

static int stk_panel_prepare(struct drm_panel *panel)
{
	struct stk_panel *stk = to_stk_panel(panel);
	struct device *dev = &stk->dsi->dev;
	int ret;

	if (stk->prepared)
		return 0;

	gpiod_set_value(stk->reset_gpio, 0);
	gpiod_set_value(stk->enable_gpio, 0);
	ret = regulator_enable(stk->supplies[IOVCC].consumer);
	if (ret < 0)
		return ret;

	mdelay(8);
	ret = regulator_enable(stk->supplies[POWER].consumer);
	if (ret < 0)
		goto iovccoff;

	mdelay(20);
	gpiod_set_value(stk->enable_gpio, 1);
	mdelay(20);
	gpiod_set_value(stk->reset_gpio, 1);
	mdelay(10);
	ret = stk_panel_init(stk);
	if (ret < 0) {
		dev_err(dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

	ret = stk_panel_on(stk);
	if (ret < 0) {
		dev_err(dev, "failed to set panel on: %d\n", ret);
		goto poweroff;
	}

	stk->prepared = true;

	return 0;

poweroff:
	regulator_disable(stk->supplies[POWER].consumer);
iovccoff:
	regulator_disable(stk->supplies[IOVCC].consumer);
	gpiod_set_value(stk->reset_gpio, 0);
	gpiod_set_value(stk->enable_gpio, 0);

	return ret;
}

static const struct drm_display_mode default_mode = {
		.clock = 163204,
		.hdisplay = 1200,
		.hsync_start = 1200 + 144,
		.hsync_end = 1200 + 144 + 16,
		.htotal = 1200 + 144 + 16 + 45,
		.vdisplay = 1920,
		.vsync_start = 1920 + 8,
		.vsync_end = 1920 + 8 + 4,
		.vtotal = 1920 + 8 + 4 + 4,
		.width_mm = 95,
		.height_mm = 151,
};

static int stk_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);
	connector->display_info.width_mm = default_mode.width_mm;
	connector->display_info.height_mm = default_mode.height_mm;
	return 1;
}

static int dsi_dcs_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;
	u16 brightness;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	return brightness & 0xff;
}

static int dsi_dcs_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0) {
		dev_err(dev, "failed to set DSI control: %d\n", ret);
		return ret;
	}

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;
	return 0;
}

static const struct backlight_ops dsi_bl_ops = {
	.update_status = dsi_dcs_bl_update_status,
	.get_brightness = dsi_dcs_bl_get_brightness,
};

static struct backlight_device *
drm_panel_create_dsi_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &dsi_bl_ops, &props);
}

static const struct drm_panel_funcs stk_panel_funcs = {
	.unprepare = stk_panel_unprepare,
	.prepare = stk_panel_prepare,
	.get_modes = stk_panel_get_modes,
};

static const struct of_device_id stk_of_match[] = {
	{ .compatible = "startek,kd070fhfid015", },
	{ }
};
MODULE_DEVICE_TABLE(of, stk_of_match);

static int stk_panel_add(struct stk_panel *stk)
{
	struct device *dev = &stk->dsi->dev;
	int ret;

	stk->mode = &default_mode;

	stk->supplies[IOVCC].supply = "iovcc";
	stk->supplies[POWER].supply = "power";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(stk->supplies), stk->supplies);
	if (ret) {
		dev_err(dev, "regulator_bulk failed\n");
		return ret;
	}

	stk->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(stk->reset_gpio)) {
		ret = PTR_ERR(stk->reset_gpio);
		dev_err(dev, "cannot get reset-gpios %d\n", ret);
		return ret;
	}

	stk->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(stk->enable_gpio)) {
		ret = PTR_ERR(stk->enable_gpio);
		dev_err(dev, "cannot get enable-gpio %d\n", ret);
		return ret;
	}

	stk->backlight = drm_panel_create_dsi_backlight(stk->dsi);
	if (IS_ERR(stk->backlight)) {
		ret = PTR_ERR(stk->backlight);
		dev_err(dev, "failed to register backlight %d\n", ret);
		return ret;
	}

	drm_panel_init(&stk->base, &stk->dsi->dev, &stk_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&stk->base);

	return 0;
}

static int stk_panel_probe(struct mipi_dsi_device *dsi)
{
	struct stk_panel *stk;
	int ret;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = (MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM);

	stk = devm_kzalloc(&dsi->dev, sizeof(*stk), GFP_KERNEL);
	if (!stk)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, stk);

	stk->dsi = dsi;

	ret = stk_panel_add(stk);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&stk->base);

	return 0;
}

static void stk_panel_remove(struct mipi_dsi_device *dsi)
{
	struct stk_panel *stk = mipi_dsi_get_drvdata(dsi);
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n",
			err);

	drm_panel_remove(&stk->base);
}

static struct mipi_dsi_driver stk_panel_driver = {
	.driver = {
		.name = "panel-startek-kd070fhfid015",
		.of_match_table = stk_of_match,
	},
	.probe = stk_panel_probe,
	.remove = stk_panel_remove,
};
module_mipi_dsi_driver(stk_panel_driver);

MODULE_AUTHOR("Guillaume La Roque <glaroque@baylibre.com>");
MODULE_DESCRIPTION("STARTEK KD070FHFID015");
MODULE_LICENSE("GPL");
