// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct tdo_tl070wsh30_panel {
	struct drm_panel base;
	struct mipi_dsi_device *link;

	struct regulator *supply;
	struct gpio_desc *reset_gpio;

	bool prepared;
};

static inline
struct tdo_tl070wsh30_panel *to_tdo_tl070wsh30_panel(struct drm_panel *panel)
{
	return container_of(panel, struct tdo_tl070wsh30_panel, base);
}

static int tdo_tl070wsh30_panel_prepare(struct drm_panel *panel)
{
	struct tdo_tl070wsh30_panel *tdo_tl070wsh30 = to_tdo_tl070wsh30_panel(panel);
	int err;

	if (tdo_tl070wsh30->prepared)
		return 0;

	err = regulator_enable(tdo_tl070wsh30->supply);
	if (err < 0)
		return err;

	usleep_range(10000, 11000);

	gpiod_set_value_cansleep(tdo_tl070wsh30->reset_gpio, 1);

	usleep_range(10000, 11000);

	gpiod_set_value_cansleep(tdo_tl070wsh30->reset_gpio, 0);

	msleep(200);

	err = mipi_dsi_dcs_exit_sleep_mode(tdo_tl070wsh30->link);
	if (err < 0) {
		dev_err(panel->dev, "failed to exit sleep mode: %d\n", err);
		regulator_disable(tdo_tl070wsh30->supply);
		return err;
	}

	msleep(200);

	err = mipi_dsi_dcs_set_display_on(tdo_tl070wsh30->link);
	if (err < 0) {
		dev_err(panel->dev, "failed to set display on: %d\n", err);
		regulator_disable(tdo_tl070wsh30->supply);
		return err;
	}

	msleep(20);

	tdo_tl070wsh30->prepared = true;

	return 0;
}

static int tdo_tl070wsh30_panel_unprepare(struct drm_panel *panel)
{
	struct tdo_tl070wsh30_panel *tdo_tl070wsh30 = to_tdo_tl070wsh30_panel(panel);
	int err;

	if (!tdo_tl070wsh30->prepared)
		return 0;

	err = mipi_dsi_dcs_set_display_off(tdo_tl070wsh30->link);
	if (err < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", err);

	usleep_range(10000, 11000);

	err = mipi_dsi_dcs_enter_sleep_mode(tdo_tl070wsh30->link);
	if (err < 0) {
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", err);
		return err;
	}

	usleep_range(10000, 11000);

	regulator_disable(tdo_tl070wsh30->supply);

	tdo_tl070wsh30->prepared = false;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 47250,
	.hdisplay = 1024,
	.hsync_start = 1024 + 46,
	.hsync_end = 1024 + 46 + 80,
	.htotal = 1024 + 46 + 80 + 100,
	.vdisplay = 600,
	.vsync_start = 600 + 5,
	.vsync_end = 600 + 5 + 5,
	.vtotal = 600 + 5 + 5 + 20,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
};

static int tdo_tl070wsh30_panel_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 154;
	connector->display_info.height_mm = 85;
	connector->display_info.bpc = 8;

	return 1;
}

static const struct drm_panel_funcs tdo_tl070wsh30_panel_funcs = {
	.unprepare = tdo_tl070wsh30_panel_unprepare,
	.prepare = tdo_tl070wsh30_panel_prepare,
	.get_modes = tdo_tl070wsh30_panel_get_modes,
};

static const struct of_device_id tdo_tl070wsh30_of_match[] = {
	{ .compatible = "tdo,tl070wsh30", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tdo_tl070wsh30_of_match);

static int tdo_tl070wsh30_panel_add(struct tdo_tl070wsh30_panel *tdo_tl070wsh30)
{
	struct device *dev = &tdo_tl070wsh30->link->dev;
	int err;

	tdo_tl070wsh30->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(tdo_tl070wsh30->supply))
		return PTR_ERR(tdo_tl070wsh30->supply);

	tdo_tl070wsh30->reset_gpio = devm_gpiod_get(dev, "reset",
						    GPIOD_OUT_LOW);
	if (IS_ERR(tdo_tl070wsh30->reset_gpio)) {
		err = PTR_ERR(tdo_tl070wsh30->reset_gpio);
		dev_dbg(dev, "failed to get reset gpio: %d\n", err);
		return err;
	}

	drm_panel_init(&tdo_tl070wsh30->base, &tdo_tl070wsh30->link->dev,
		       &tdo_tl070wsh30_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	err = drm_panel_of_backlight(&tdo_tl070wsh30->base);
	if (err)
		return err;

	drm_panel_add(&tdo_tl070wsh30->base);

	return 0;
}

static int tdo_tl070wsh30_panel_probe(struct mipi_dsi_device *dsi)
{
	struct tdo_tl070wsh30_panel *tdo_tl070wsh30;
	int err;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_LPM;

	tdo_tl070wsh30 = devm_kzalloc(&dsi->dev, sizeof(*tdo_tl070wsh30),
				    GFP_KERNEL);
	if (!tdo_tl070wsh30)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, tdo_tl070wsh30);
	tdo_tl070wsh30->link = dsi;

	err = tdo_tl070wsh30_panel_add(tdo_tl070wsh30);
	if (err < 0)
		return err;

	return mipi_dsi_attach(dsi);
}

static void tdo_tl070wsh30_panel_remove(struct mipi_dsi_device *dsi)
{
	struct tdo_tl070wsh30_panel *tdo_tl070wsh30 = mipi_dsi_get_drvdata(dsi);
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	drm_panel_remove(&tdo_tl070wsh30->base);
	drm_panel_disable(&tdo_tl070wsh30->base);
	drm_panel_unprepare(&tdo_tl070wsh30->base);
}

static void tdo_tl070wsh30_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct tdo_tl070wsh30_panel *tdo_tl070wsh30 = mipi_dsi_get_drvdata(dsi);

	drm_panel_disable(&tdo_tl070wsh30->base);
	drm_panel_unprepare(&tdo_tl070wsh30->base);
}

static struct mipi_dsi_driver tdo_tl070wsh30_panel_driver = {
	.driver = {
		.name = "panel-tdo-tl070wsh30",
		.of_match_table = tdo_tl070wsh30_of_match,
	},
	.probe = tdo_tl070wsh30_panel_probe,
	.remove = tdo_tl070wsh30_panel_remove,
	.shutdown = tdo_tl070wsh30_panel_shutdown,
};
module_mipi_dsi_driver(tdo_tl070wsh30_panel_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("TDO TL070WSH30 panel driver");
MODULE_LICENSE("GPL v2");
