// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2023, Linaro Limited

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/display/drm_dsc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct visionox_vtdr6130 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data visionox_vtdr6130_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vci" },
	{ .supply = "vdd" },
};

static inline struct visionox_vtdr6130 *to_visionox_vtdr6130(struct drm_panel *panel)
{
	return container_of(panel, struct visionox_vtdr6130, panel);
}

static void visionox_vtdr6130_reset(struct visionox_vtdr6130 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int visionox_vtdr6130_on(struct visionox_vtdr6130 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx,
				     MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx,
				     MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x00,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x12, 0x00, 0x00, 0xab,
				     0x30, 0x80, 0x09, 0x60, 0x04, 0x38, 0x00,
				     0x28, 0x02, 0x1c, 0x02, 0x1c, 0x02, 0x00,
				     0x02, 0x0e, 0x00, 0x20, 0x03, 0xdd, 0x00,
				     0x07, 0x00, 0x0c, 0x02, 0x77, 0x02, 0x8b,
				     0x18, 0x00, 0x10, 0xf0, 0x07, 0x10, 0x20,
				     0x00, 0x06, 0x0f, 0x0f, 0x33, 0x0e, 0x1c,
				     0x2a, 0x38, 0x46, 0x54, 0x62, 0x69, 0x70,
				     0x77, 0x79, 0x7b, 0x7d, 0x7e, 0x02, 0x02,
				     0x22, 0x00, 0x2a, 0x40, 0x2a, 0xbe, 0x3a,
				     0xfc, 0x3a, 0xfa, 0x3a, 0xf8, 0x3b, 0x38,
				     0x3b, 0x78, 0x3b, 0xb6, 0x4b, 0xb6, 0x4b,
				     0xf4, 0x4b, 0xf4, 0x6c, 0x34, 0x84, 0x74,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xaa, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x01, 0x38, 0x00, 0x14,
				     0x00, 0x1c, 0x00, 0x01, 0x66, 0x00, 0x14,
				     0x00, 0x14, 0x00, 0x01, 0x66, 0x00, 0x14,
				     0x05, 0xcc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xaa, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x09, 0x11, 0x09, 0x11,
				     0x08, 0xc1, 0x07, 0xfa, 0x05, 0xa4, 0x00,
				     0x3c, 0x00, 0x34, 0x00, 0x24, 0x00, 0x0c,
				     0x00, 0x0c, 0x04, 0x00, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xaa, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x03, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x00, 0x33, 0x00, 0x00,
				     0x00, 0x3e, 0x00, 0x00, 0x00, 0x3e, 0x00,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x00, 0x09, 0x09, 0x09,
				     0x09, 0x09, 0x09, 0x06, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x00, 0x00, 0x08, 0x09,
				     0x09, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbc, 0x10, 0x00, 0x00, 0x06,
				     0x11, 0x09, 0x3b, 0x09, 0x47, 0x09, 0x47,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x10, 0x10, 0x00, 0x08,
				     0x22, 0x09, 0x19, 0x09, 0x25, 0x09, 0x25,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfa, 0x08, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xaa, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0x82);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x51, 0x83);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x9a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0x00);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static void visionox_vtdr6130_off(struct visionox_vtdr6130 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
}

static int visionox_vtdr6130_prepare(struct drm_panel *panel)
{
	struct visionox_vtdr6130 *ctx = to_visionox_vtdr6130(panel);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(visionox_vtdr6130_supplies),
				    ctx->supplies);
	if (ret < 0)
		return ret;

	visionox_vtdr6130_reset(ctx);

	ret = visionox_vtdr6130_on(ctx);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(visionox_vtdr6130_supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int visionox_vtdr6130_unprepare(struct drm_panel *panel)
{
	struct visionox_vtdr6130 *ctx = to_visionox_vtdr6130(panel);

	visionox_vtdr6130_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(visionox_vtdr6130_supplies),
			       ctx->supplies);

	return 0;
}

static const struct drm_display_mode visionox_vtdr6130_mode = {
	.clock = (1080 + 20 + 2 + 20) * (2400 + 20 + 2 + 18) * 144 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 20,
	.hsync_end = 1080 + 20 + 2,
	.htotal = 1080 + 20 + 2 + 20,
	.vdisplay = 2400,
	.vsync_start = 2400 + 20,
	.vsync_end = 2400 + 20 + 2,
	.vtotal = 2400 + 20 + 2 + 18,
	.width_mm = 71,
	.height_mm = 157,
};

static int visionox_vtdr6130_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &visionox_vtdr6130_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs visionox_vtdr6130_panel_funcs = {
	.prepare = visionox_vtdr6130_prepare,
	.unprepare = visionox_vtdr6130_unprepare,
	.get_modes = visionox_vtdr6130_get_modes,
};

static int visionox_vtdr6130_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	return mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
}

static const struct backlight_ops visionox_vtdr6130_bl_ops = {
	.update_status = visionox_vtdr6130_bl_update_status,
};

static struct backlight_device *
visionox_vtdr6130_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &visionox_vtdr6130_bl_ops, &props);
}

static int visionox_vtdr6130_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct visionox_vtdr6130 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(&dsi->dev,
					    ARRAY_SIZE(visionox_vtdr6130_supplies),
					    visionox_vtdr6130_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;
	ctx->panel.prepare_prev_first = true;

	drm_panel_init(&ctx->panel, dev, &visionox_vtdr6130_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = visionox_vtdr6130_create_backlight(dsi);
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

static void visionox_vtdr6130_remove(struct mipi_dsi_device *dsi)
{
	struct visionox_vtdr6130 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id visionox_vtdr6130_of_match[] = {
	{ .compatible = "visionox,vtdr6130" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, visionox_vtdr6130_of_match);

static struct mipi_dsi_driver visionox_vtdr6130_driver = {
	.probe = visionox_vtdr6130_probe,
	.remove = visionox_vtdr6130_remove,
	.driver = {
		.name = "panel-visionox-vtdr6130",
		.of_match_table = visionox_vtdr6130_of_match,
	},
};
module_mipi_dsi_driver(visionox_vtdr6130_driver);

MODULE_AUTHOR("Neil Armstrong <neil.armstrong@linaro.org>");
MODULE_DESCRIPTION("Panel driver for the Visionox VTDR6130 AMOLED DSI panel");
MODULE_LICENSE("GPL");
