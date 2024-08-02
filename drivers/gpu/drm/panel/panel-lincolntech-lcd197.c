// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 BayLibre, SAS
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_device.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct lincoln_lcd197_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *supply;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *reset_gpio;
};

static inline
struct lincoln_lcd197_panel *to_lincoln_lcd197_panel(struct drm_panel *panel)
{
	return container_of(panel, struct lincoln_lcd197_panel, panel);
}

static int lincoln_lcd197_panel_prepare(struct drm_panel *panel)
{
	struct lincoln_lcd197_panel *lcd = to_lincoln_lcd197_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = lcd->dsi };
	int err;

	gpiod_set_value_cansleep(lcd->enable_gpio, 0);
	err = regulator_enable(lcd->supply);
	if (err < 0)
		return err;

	gpiod_set_value_cansleep(lcd->enable_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(lcd->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(lcd->reset_gpio, 0);
	msleep(50);

	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb9, 0xff, 0x83, 0x99);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd2, 0x55);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb1, 0x02, 0x04, 0x70, 0x90, 0x01,
			       0x32, 0x33, 0x11, 0x11, 0x4d, 0x57, 0x56, 0x73,
			       0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb2, 0x00, 0x80, 0x80, 0xae, 0x0a,
			       0x0e, 0x75, 0x11, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb4, 0x00, 0xff, 0x04, 0xa4, 0x02,
			       0xa0, 0x00, 0x00, 0x10, 0x00, 0x00, 0x02, 0x00,
			       0x24, 0x02, 0x04, 0x0a, 0x21, 0x03, 0x00, 0x00,
			       0x08, 0xa6, 0x88, 0x04, 0xa4, 0x02, 0xa0, 0x00,
			       0x00, 0x10, 0x00, 0x00, 0x02, 0x00, 0x24, 0x02,
			       0x04, 0x0a, 0x00, 0x00, 0x08, 0xa6, 0x00, 0x08,
			       0x11);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd3, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x18, 0x18, 0x32, 0x10, 0x09, 0x00, 0x09,
			       0x32, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x11, 0x00, 0x02, 0x02, 0x03, 0x00,
			       0x00, 0x00, 0x0a, 0x40);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd5, 0x18, 0x18, 0x18, 0x18, 0x21,
			       0x20, 0x18, 0x18, 0x19, 0x19, 0x19, 0x19, 0x18,
			       0x18, 0x18, 0x18, 0x03, 0x02, 0x01, 0x00, 0x2f,
			       0x2f, 0x30, 0x30, 0x31, 0x31, 0x18, 0x18, 0x18,
			       0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd6, 0x18, 0x18, 0x18, 0x18, 0x20,
			       0x21, 0x19, 0x19, 0x18, 0x18, 0x19, 0x19, 0x18,
			       0x18, 0x18, 0x18, 0x00, 0x01, 0x02, 0x03, 0x2f,
			       0x2f, 0x30, 0x30, 0x31, 0x31, 0x18, 0x18, 0x18,
			       0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x0a, 0xbe, 0xfa, 0xa0, 0x0a,
			       0xbe, 0xfa, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x0f, 0xff, 0xff, 0xe0, 0x0f,
			       0xff, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbd, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xd8, 0x0f, 0xff, 0xff, 0xe0, 0x0f,
			       0xff, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xe0, 0x01, 0x11, 0x1c, 0x17, 0x39,
			       0x43, 0x54, 0x51, 0x5a, 0x64, 0x6c, 0x74, 0x7a,
			       0x83, 0x8d, 0x92, 0x99, 0xa4, 0xa9, 0xb4, 0xaa,
			       0xba, 0xbe, 0x63, 0x5e, 0x69, 0x73, 0x01, 0x11,
			       0x1c, 0x17, 0x39, 0x43, 0x54, 0x51, 0x5a, 0x64,
			       0x6c, 0x74, 0x7a, 0x83, 0x8d, 0x92, 0x99, 0xa4,
			       0xa7, 0xb2, 0xa9, 0xba, 0xbe, 0x63, 0x5e, 0x69,
			       0x73);
	mipi_dsi_usleep_range(&ctx, 200, 300);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xb6, 0x92, 0x92);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xcc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xbf, 0x40, 0x41, 0x50, 0x49);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc6, 0xff, 0xf9);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xc0, 0x25, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x02);
	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	if (ctx.accum_err) {
		gpiod_set_value_cansleep(lcd->enable_gpio, 0);
		gpiod_set_value_cansleep(lcd->reset_gpio, 1);
		regulator_disable(lcd->supply);
	}

	return ctx.accum_err;
}

static int lincoln_lcd197_panel_unprepare(struct drm_panel *panel)
{
	struct lincoln_lcd197_panel *lcd = to_lincoln_lcd197_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = lcd->dsi };

	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_usleep_range(&ctx, 5000, 6000);
	gpiod_set_value_cansleep(lcd->enable_gpio, 0);
	gpiod_set_value_cansleep(lcd->reset_gpio, 1);
	regulator_disable(lcd->supply);

	return ctx.accum_err;
}

static int lincoln_lcd197_panel_enable(struct drm_panel *panel)
{
	struct lincoln_lcd197_panel *lcd = to_lincoln_lcd197_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = lcd->dsi };

	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	return ctx.accum_err;
}

static int lincoln_lcd197_panel_disable(struct drm_panel *panel)
{
	struct lincoln_lcd197_panel *lcd = to_lincoln_lcd197_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = lcd->dsi };

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_msleep(&ctx, 50);

	return ctx.accum_err;
}

static const struct drm_display_mode lcd197_mode = {
	.clock = 154002,
	.hdisplay = 1080,
	.hsync_start = 1080 + 20,
	.hsync_end = 1080 + 20 + 6,
	.htotal = 1080 + 204,
	.vdisplay = 1920,
	.vsync_start = 1920 + 4,
	.vsync_end = 1920 + 4 + 4,
	.vtotal = 1920 + 79,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 79,
	.height_mm = 125,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int lincoln_lcd197_panel_get_modes(struct drm_panel *panel,
					  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &lcd197_mode);
}

static const struct drm_panel_funcs lincoln_lcd197_panel_funcs = {
	.prepare = lincoln_lcd197_panel_prepare,
	.unprepare = lincoln_lcd197_panel_unprepare,
	.enable = lincoln_lcd197_panel_enable,
	.disable = lincoln_lcd197_panel_disable,
	.get_modes = lincoln_lcd197_panel_get_modes,
};

static int lincoln_lcd197_panel_probe(struct mipi_dsi_device *dsi)
{
	struct lincoln_lcd197_panel *lcd;
	struct device *dev = &dsi->dev;
	int err;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = (MIPI_DSI_MODE_VIDEO |
			   MIPI_DSI_MODE_VIDEO_BURST);

	lcd = devm_kzalloc(&dsi->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, lcd);
	lcd->dsi = dsi;

	lcd->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(lcd->supply))
		return dev_err_probe(dev, PTR_ERR(lcd->supply),
				     "failed to get power supply");

	lcd->enable_gpio = devm_gpiod_get(dev, "enable",
					  GPIOD_OUT_HIGH);
	if (IS_ERR(lcd->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(lcd->enable_gpio),
				     "failed to get enable gpio");

	lcd->reset_gpio = devm_gpiod_get(dev, "reset",
					  GPIOD_OUT_HIGH);
	if (IS_ERR(lcd->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(lcd->reset_gpio),
				     "failed to get reset gpio");

	drm_panel_init(&lcd->panel, dev,
		       &lincoln_lcd197_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	err = drm_panel_of_backlight(&lcd->panel);
	if (err)
		return err;

	drm_panel_add(&lcd->panel);
	err = mipi_dsi_attach(dsi);
	if (err)
		drm_panel_remove(&lcd->panel);

	return err;
}

static void lincoln_lcd197_panel_remove(struct mipi_dsi_device *dsi)
{
	struct lincoln_lcd197_panel *lcd = mipi_dsi_get_drvdata(dsi);
	int err;

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", err);

	drm_panel_remove(&lcd->panel);
}

static const struct of_device_id lincoln_lcd197_of_match[] = {
	{ .compatible = "lincolntech,lcd197", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lincoln_lcd197_of_match);

static struct mipi_dsi_driver lincoln_lcd197_panel_driver = {
	.driver = {
		.name = "panel-lincolntech-lcd197",
		.of_match_table = lincoln_lcd197_of_match,
	},
	.probe = lincoln_lcd197_panel_probe,
	.remove = lincoln_lcd197_panel_remove,
};
module_mipi_dsi_driver(lincoln_lcd197_panel_driver);

MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_DESCRIPTION("Lincoln Technologies LCD197 panel driver");
MODULE_LICENSE("GPL");
