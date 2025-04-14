// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
 *   Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *   Copyright (c) 2025, Alexander Baransky <sanyapilot496@gmail.com>
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

struct visionox_g2647fb105 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data visionox_g2647fb105_supplies[] = {
	{ .supply = "vdd3p3" },
	{ .supply = "vddio" },
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline
struct visionox_g2647fb105 *to_visionox_g2647fb105(struct drm_panel *panel)
{
	return container_of(panel, struct visionox_g2647fb105, panel);
}

static void visionox_g2647fb105_reset(struct visionox_g2647fb105 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int visionox_g2647fb105_on(struct visionox_g2647fb105 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbf, 0xbb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0xdd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0xd0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfe, 0x00);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 100);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int visionox_g2647fb105_off(struct visionox_g2647fb105 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static int visionox_g2647fb105_prepare(struct drm_panel *panel)
{
	struct visionox_g2647fb105 *ctx = to_visionox_g2647fb105(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(visionox_g2647fb105_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	visionox_g2647fb105_reset(ctx);

	ret = visionox_g2647fb105_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		return ret;
	}

	return 0;
}

static int visionox_g2647fb105_unprepare(struct drm_panel *panel)
{
	struct visionox_g2647fb105 *ctx = to_visionox_g2647fb105(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = visionox_g2647fb105_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(visionox_g2647fb105_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode visionox_g2647fb105_mode = {
	.clock = (1080 + 28 + 4 + 36) * (2340 + 8 + 4 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 28,
	.hsync_end = 1080 + 28 + 4,
	.htotal = 1080 + 28 + 4 + 36,
	.vdisplay = 2340,
	.vsync_start = 2340 + 8,
	.vsync_end = 2340 + 8 + 4,
	.vtotal = 2340 + 8 + 4 + 4,
	.width_mm = 69,
	.height_mm = 149,
};

static int visionox_g2647fb105_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &visionox_g2647fb105_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs visionox_g2647fb105_panel_funcs = {
	.prepare = visionox_g2647fb105_prepare,
	.unprepare = visionox_g2647fb105_unprepare,
	.get_modes = visionox_g2647fb105_get_modes,
};

static int visionox_g2647fb105_bl_update_status(struct backlight_device *bl)
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

static const struct backlight_ops visionox_g2647fb105_bl_ops = {
	.update_status = visionox_g2647fb105_bl_update_status,
};

static struct backlight_device *
visionox_g2647fb105_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &visionox_g2647fb105_bl_ops, &props);
}

static int visionox_g2647fb105_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct visionox_g2647fb105 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(visionox_g2647fb105_supplies),
					    visionox_g2647fb105_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	drm_panel_init(&ctx->panel, dev, &visionox_g2647fb105_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = visionox_g2647fb105_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void visionox_g2647fb105_remove(struct mipi_dsi_device *dsi)
{
	struct visionox_g2647fb105 *ctx = mipi_dsi_get_drvdata(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id visionox_g2647fb105_of_match[] = {
	{ .compatible = "visionox,g2647fb105" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, visionox_g2647fb105_of_match);

static struct mipi_dsi_driver visionox_g2647fb105_driver = {
	.probe = visionox_g2647fb105_probe,
	.remove = visionox_g2647fb105_remove,
	.driver = {
		.name = "panel-visionox-g2647fb105",
		.of_match_table = visionox_g2647fb105_of_match,
	},
};
module_mipi_dsi_driver(visionox_g2647fb105_driver);

MODULE_AUTHOR("Alexander Baransky <sanyapilot496@gmail.com>");
MODULE_DESCRIPTION("DRM driver for Visionox G2647FB105 AMOLED DSI panel");
MODULE_LICENSE("GPL");
