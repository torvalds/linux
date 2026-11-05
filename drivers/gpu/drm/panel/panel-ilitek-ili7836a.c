// SPDX-License-Identifier: GPL-2.0-only

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/mipi_display.h>

static const struct regulator_bulk_data ili7836a_supplies[] = {
	{ .supply = "vddio", },
	{ .supply = "avdd", },
};

struct ili7836a_panel {
	struct drm_panel panel;
	struct drm_connector *connector;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	struct ili7836a_desc *desc;
	enum drm_panel_orientation orientation;
};

struct ili7836a_desc {
	unsigned int width_mm;
	unsigned int height_mm;
	unsigned int bpc;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
};

static inline struct ili7836a_panel *to_ili7836a_panel(struct drm_panel *panel)
{
	return container_of(panel, struct ili7836a_panel, panel);
}

static void ili7836a_reset(struct ili7836a_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int ili7836a_on(struct ili7836a_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x62);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa4, 0x38);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x15);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x20);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x01);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);

	return dsi_ctx.accum_err;
}

static int ili7836a_disable(struct drm_panel *panel)
{
	struct ili7836a_panel *ctx = to_ili7836a_panel(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int ili7836a_prepare(struct drm_panel *panel)
{
	struct ili7836a_panel *ctx = to_ili7836a_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ili7836a_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ili7836a_reset(ctx);

	ret = ili7836a_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ili7836a_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int ili7836a_unprepare(struct drm_panel *panel)
{
	struct ili7836a_panel *ctx = to_ili7836a_panel(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ili7836a_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode nova_modes[] = {
	{
		/* 120Hz */
		.clock = (1280 + 12 + 2 + 8) * (960 + 12 + 2 + 24) * 120 / 1000,
		.hdisplay = 1280,
		.hsync_start = 1280 + 12,
		.hsync_end = 1280 + 12 + 2,
		.htotal = 1280 + 12 + 2 + 8,
		.vdisplay = 960,
		.vsync_start = 960 + 12,
		.vsync_end = 960 + 12 + 2,
		.vtotal = 960 + 12 + 2 + 24,
	}
};

static struct ili7836a_desc nova_desc = {
	.modes = nova_modes,
	.num_modes = ARRAY_SIZE(nova_modes),
	.width_mm = 91,
	.height_mm = 68,
	.bpc = 8,
};

static int ili7836a_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct ili7836a_panel *ctx = to_ili7836a_panel(panel);

	return drm_connector_helper_get_modes_fixed(connector, ctx->desc->modes);
}

static enum drm_panel_orientation ili7836a_get_orientation(struct drm_panel *panel)
{
	struct ili7836a_panel *ctx = to_ili7836a_panel(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs ili7836a_panel_funcs = {
	.prepare = ili7836a_prepare,
	.unprepare = ili7836a_unprepare,
	.disable = ili7836a_disable,
	.get_modes = ili7836a_get_modes,
	.get_orientation = ili7836a_get_orientation,
};

static int ili7836a_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops ili7836a_bl_ops = {
	.update_status = ili7836a_bl_update_status,
};

static struct backlight_device *
ili7836a_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 3445,
		.max_brightness = 3445,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &ili7836a_bl_ops, &props);
}

static int ili7836a_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ili7836a_panel *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, __typeof(*ctx), panel,
				   &ili7836a_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->desc = (struct ili7836a_desc *)of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(ili7836a_supplies),
					    ili7836a_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ret = drm_of_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_HSE |
			  MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ili7836a_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret)
		return ret;

	return devm_mipi_dsi_attach(dev, dsi);
}

static const struct of_device_id ili7836a_of_match[] = {
	{ .compatible = "retroidpocket,nova-panel", .data = &nova_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ili7836a_of_match);

static struct mipi_dsi_driver ili7836a_driver = {
	.probe = ili7836a_probe,
	.driver = {
		.name = "panel-ili7836a-amoled",
		.of_match_table = ili7836a_of_match,
	},
};
module_mipi_dsi_driver(ili7836a_driver);

MODULE_DESCRIPTION("DRM driver for ILI7836A DSI panels");
MODULE_LICENSE("GPL");
