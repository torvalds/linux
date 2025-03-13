// SPDX-License-Identifier: GPL-2.0-only
/*
 * DDIC CH13726A MIPI-DSI panel driver
 * Copyright (c) 2024, Teguh Sobirin <teguh@sobir.in>.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct ch13726a_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[4];
	struct gpio_desc *reset_gpio;
	const struct drm_display_mode *display_mode;
	enum drm_panel_orientation orientation;
	bool prepared;
};

static inline struct ch13726a_panel *to_ch13726a_panel(struct drm_panel *panel)
{
	return container_of(panel, struct ch13726a_panel, panel);
}

static void ch13726a_reset(struct ch13726a_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
}

static int ch13726a_on(struct ch13726a_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(20);

	return 0;
}

static int ch13726a_disable(struct drm_panel *panel)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}

	msleep(100);

	return 0;
}

static int ch13726a_prepare(struct drm_panel *panel)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ch13726a_reset(ctx);

	ret = ch13726a_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	msleep(28);

	ctx->prepared = true;

	return 0;
}

static int ch13726a_unprepare(struct drm_panel *panel)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);

	if (!ctx->prepared)
		return 0;

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode rpmini_display_mode = {
	.clock = (960 + 28 + 4 + 36) * (1280 + 16 + 4 + 8) * 60 / 1000,
	.hdisplay = 960,
	.hsync_start = 960 + 28,
	.hsync_end = 960 + 28 + 4,
	.htotal = 960 + 28 + 4 + 36,
	.vdisplay = 1280,
	.vsync_start = 1280 + 16,
	.vsync_end = 1280 + 16 + 4,
	.vtotal = 1280 + 16 + 4 + 8,
	.width_mm = 65,
	.height_mm = 75,
};

static const struct drm_display_mode rp5_display_mode = {
	.clock = (1080 + 12 + 4 + 12) * (1920 + 12 + 4 + 12) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 12,
	.hsync_end = 1080 + 12 + 4,
	.htotal = 1080 + 12 + 4 + 12,
	.vdisplay = 1920,
	.vsync_start = 1920 + 12,
	.vsync_end = 1920 + 12 + 4,
	.vtotal = 1920 + 12 + 4 + 12,
	.width_mm = 68,
	.height_mm = 121,
};

static int ch13726a_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->display_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static enum drm_panel_orientation ch13726a_get_orientation(struct drm_panel *panel)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs ch13726a_panel_funcs = {
	.prepare = ch13726a_prepare,
	.unprepare = ch13726a_unprepare,
	.disable = ch13726a_disable,
	.get_modes = ch13726a_get_modes,
	.get_orientation = ch13726a_get_orientation,
};

static int ch13726a_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops ch13726a_bl_ops = {
	.update_status = ch13726a_bl_update_status,
};

static struct backlight_device *
ch13726a_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &ch13726a_bl_ops, &props);
}

static int ch13726a_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ch13726a_panel *ctx;
	int rotation;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->display_mode = of_device_get_match_data(dev);

	ctx->supplies[0].supply = "vdd1v2";
	ctx->supplies[1].supply = "vddio";
	ctx->supplies[2].supply = "vdd";
	ctx->supplies[3].supply = "avdd";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ret = of_property_read_u32(dev->of_node, "rotation", &rotation);
	if (ret == -EINVAL) {
		ctx->orientation = DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
	}

	if (rotation == 0)
		ctx->orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL;
	else if (rotation == 90)
		ctx->orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP;
	else if (rotation == 180)
		ctx->orientation = DRM_MODE_PANEL_ORIENTATION_BOTTOM_UP;
	else if (rotation == 270)
		ctx->orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_BURST |
					MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &ch13726a_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ch13726a_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void ch13726a_remove(struct mipi_dsi_device *dsi)
{
	struct ch13726a_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ch13726a_of_match[] = {
	{ .compatible = "ch13726a,rpmini", .data = &rpmini_display_mode },
	{ .compatible = "ch13726a,rp5", .data = &rp5_display_mode },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ch13726a_of_match);

static struct mipi_dsi_driver ch13726a_driver = {
	.probe = ch13726a_probe,
	.remove = ch13726a_remove,
	.driver = {
		.name = "panel-ch13726a-amoled",
		.of_match_table = ch13726a_of_match,
	},
};
module_mipi_dsi_driver(ch13726a_driver);

MODULE_DESCRIPTION("DRM driver for CH13726A DSI panels");
MODULE_LICENSE("GPL");
