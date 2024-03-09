// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct starry_800p {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data starry_800p_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct starry_800p *to_starry_800p(struct drm_panel *panel)
{
	return container_of(panel, struct starry_800p, panel);
}

static int starry_800p_on(struct starry_800p *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x93);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x65);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3, 0xf8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0xaf);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x00);
	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_MEMORY_CONTINUE,
				     0x7c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_3D_CONTROL, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_READ_MEMORY_CONTINUE,
				     0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_VSYNC_TIMING, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_GET_SCANLINE, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0x69);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x7c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
				     0x65);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6c, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x7c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x65);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7a, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7b, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7c, 0x3c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x4a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x4a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_CABC_MIN_BRIGHTNESS,
				     0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x6a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x6a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6c, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0xdd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7a, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x6a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x2b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_LUT, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_READ_MEMORY_START,
				     0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe7, 0x0c);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	return dsi_ctx.accum_err;
}

static int starry_800p_off(struct starry_800p *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int starry_800p_prepare(struct drm_panel *panel)
{
	struct starry_800p *ctx = to_starry_800p(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(starry_800p_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ret = starry_800p_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		regulator_bulk_disable(ARRAY_SIZE(starry_800p_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int starry_800p_unprepare(struct drm_panel *panel)
{
	struct starry_800p *ctx = to_starry_800p(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = starry_800p_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	regulator_bulk_disable(ARRAY_SIZE(starry_800p_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode starry_800p_mode = {
	.clock = (800 + 18 + 18 + 18) * (1280 + 16 + 4 + 4) * 60 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 18,
	.hsync_end = 800 + 18 + 18,
	.htotal = 800 + 18 + 18 + 18,
	.vdisplay = 1280,
	.vsync_start = 1280 + 16,
	.vsync_end = 1280 + 16 + 4,
	.vtotal = 1280 + 16 + 4 + 4,
	.width_mm = 135,
	.height_mm = 216,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int starry_800p_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &starry_800p_mode);
}

static const struct drm_panel_funcs starry_800p_panel_funcs = {
	.prepare = starry_800p_prepare,
	.unprepare = starry_800p_unprepare,
	.get_modes = starry_800p_get_modes,
};

static int starry_800p_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct starry_800p *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(starry_800p_supplies),
					    starry_800p_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &starry_800p_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void starry_800p_remove(struct mipi_dsi_device *dsi)
{
	struct starry_800p *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id starry_800p_of_match[] = {
	{ .compatible = "lenovo,cd-18781y-jd9365" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, starry_800p_of_match);

static struct mipi_dsi_driver starry_800p_driver = {
	.probe = starry_800p_probe,
	.remove = starry_800p_remove,
	.driver = {
		.name = "panel-starry-800p",
		.of_match_table = starry_800p_of_match,
	},
};
module_mipi_dsi_driver(starry_800p_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for JD9365 Starry 800P MDSS DSI");
MODULE_LICENSE("GPL");
