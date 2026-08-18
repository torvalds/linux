// SPDX-License-Identifier: GPL-2.0-only
/*
 * ChipWealth CH13726A MIPI-DSI panel driver
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

static const struct regulator_bulk_data ch13726a_supplies[] = {
	{ .supply = "vdd1v2", },
	{ .supply = "vddio", },
	{ .supply = "vdd", },
	{ .supply = "avdd", },
};

struct ch13726a_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	struct ch13726a_desc *desc;
	enum drm_panel_orientation orientation;
};

struct ch13726a_desc {
	unsigned int width_mm;
	unsigned int height_mm;
	unsigned int bpc;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
};

static inline struct ch13726a_panel *to_ch13726a_panel(struct drm_panel *panel)
{
	return container_of(panel, struct ch13726a_panel, panel);
}

static void ch13726a_reset(struct ch13726a_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ch13726a_on(struct ch13726a_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xf0, 0x50);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9, 0x00);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ch13726a_disable(struct drm_panel *panel)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ch13726a_prepare(struct drm_panel *panel)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ch13726a_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ch13726a_reset(ctx);

	ret = ch13726a_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ch13726a_supplies), ctx->supplies);
		return ret;
	}

	msleep(28);

	return 0;
}

static int ch13726a_unprepare(struct drm_panel *panel)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ch13726a_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode thor_bottom_modes[] = {
	{
		/* 120Hz */
		.clock = (1080 + 28 + 4 + 36) * (1240 + 16 + 4 + 8) * 120 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 28,
		.hsync_end = 1080 + 28 + 4,
		.htotal = 1080 + 28 + 4 + 36,
		.vdisplay = 1240,
		.vsync_start = 1240 + 16,
		.vsync_end = 1240 + 16 + 4,
		.vtotal = 1240 + 16 + 4 + 8,
	},
	{
		/* 60Hz */
		.clock = (1080 + 28 + 4 + 36) * (1240 + 16 + 4 + 8) * 60 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 28,
		.hsync_end = 1080 + 28 + 4,
		.htotal = 1080 + 28 + 4 + 36,
		.vdisplay = 1240,
		.vsync_start = 1240 + 16,
		.vsync_end = 1240 + 16 + 4,
		.vtotal = 1240 + 16 + 4 + 8,
	}
};

static struct ch13726a_desc thor_bottom_desc = {
	.modes = thor_bottom_modes,
	.num_modes = ARRAY_SIZE(thor_bottom_modes),
	.width_mm = 65,
	.height_mm = 75,
	.bpc = 8,
};

static int ch13726a_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct ch13726a_panel *ctx = to_ch13726a_panel(panel);

	for (uint8_t i = 0; i < ctx->desc->num_modes; i++) {
		const struct drm_display_mode *m = &ctx->desc->modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(&ctx->dsi->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = ctx->desc->width_mm;
	connector->display_info.height_mm = ctx->desc->height_mm;
	connector->display_info.bpc = ctx->desc->bpc;

	return ctx->desc->num_modes;
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
	int ret;

	ctx = devm_drm_panel_alloc(dev, __typeof(*ctx), panel,
				   &ch13726a_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->desc = (struct ch13726a_desc *)of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(ch13726a_supplies),
					    ch13726a_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

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
	{ .compatible = "ayntec,thor-panel-bottom", .data = &thor_bottom_desc },
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
