// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct hx8399c_auo_53 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
};

static const struct regulator_bulk_data hx8399c_auo_53_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct hx8399c_auo_53 *to_hx8399c_auo_53(struct drm_panel *panel)
{
	return container_of(panel, struct hx8399c_auo_53, panel);
}

static void hx8399c_auo_53_reset(struct hx8399c_auo_53 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(120);
}

static int hx8399c_auo_53_on(struct hx8399c_auo_53 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb9, 0xff, 0x83, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb1,
					 0x02, 0x04, 0x6c, 0x8c, 0x01, 0x32,
					 0x33, 0x11, 0x11, 0x4d, 0x52, 0x56,
					 0x73, 0x02, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb2,
					 0x00, 0x80, 0x80, 0xae, 0x05, 0x07,
					 0x5a, 0x11, 0x10, 0x10, 0x00, 0x1e,
					 0x70, 0x03, 0xd4);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4,
					 0x00, 0xff, 0x56, 0x3c, 0x00, 0xab,
					 0x00, 0x00, 0x04, 0x00, 0x00, 0x02,
					 0x00, 0x2a, 0x02, 0x07, 0x09, 0x21,
					 0x03, 0x01, 0x00, 0x00, 0xac, 0x84,
					 0x56, 0x3c, 0x00, 0xab, 0x00, 0x00,
					 0x04, 0x00, 0x00, 0x02, 0x00, 0x2a,
					 0x02, 0x07, 0x09, 0x01, 0x00, 0x00,
					 0xac, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3,
					 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
					 0x10, 0x10, 0x00, 0x00, 0x03, 0x00,
					 0x03, 0x00, 0x07, 0x88, 0x07, 0x88,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x24,
					 0x02, 0x05, 0x05, 0x03, 0x00, 0x00,
					 0x00, 0x05, 0x40);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5,
					 0x20, 0x20, 0x19, 0x19, 0x18, 0x18,
					 0x02, 0x03, 0x00, 0x01, 0x24, 0x24,
					 0x18, 0x18, 0x18, 0x18, 0x24, 0x24,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x2f, 0x2f, 0x30, 0x30,
					 0x31, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6,
					 0x24, 0x24, 0x18, 0x18, 0x19, 0x19,
					 0x01, 0x00, 0x03, 0x02, 0x24, 0x24,
					 0x18, 0x18, 0x18, 0x18, 0x20, 0x20,
					 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
					 0x40, 0x40, 0x2f, 0x2f, 0x30, 0x30,
					 0x31, 0x31);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8,
					 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
					 0xaa, 0xaa, 0xaa, 0xba, 0xaa, 0xaa,
					 0xaa, 0xba, 0xaa, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x82, 0xea, 0xaa, 0xaa,
					 0x82, 0xea, 0xaa, 0xaa);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd8,
					 0xff, 0xff, 0xc0, 0x3f, 0xff, 0xff,
					 0xc0, 0x3f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbf,
					 0x40, 0x41, 0x50, 0x09, 0x1a, 0xc0,
					 0xcd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe0,
					 0x00, 0x12, 0x1d, 0x19, 0x39, 0x43,
					 0x50, 0x4a, 0x51, 0x5a, 0x5f, 0x63,
					 0x66, 0x6d, 0x74, 0x78, 0x7c, 0x85,
					 0x89, 0x95, 0x8c, 0x9e, 0xa5, 0x58,
					 0x55, 0x60, 0x6c, 0x00, 0x12, 0x1d,
					 0x19, 0x39, 0x43, 0x50, 0x4a, 0x51,
					 0x5a, 0x5f, 0x63, 0x66, 0x6d, 0x74,
					 0x78, 0x7c, 0x85, 0x89, 0x95, 0x8c,
					 0x9e, 0xa5, 0x58, 0x55, 0x60, 0x6c);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb6, 0x74, 0x74);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2, 0x33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x51, 0x0f, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x53, 0x24);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x55, 0x01);

	return dsi_ctx.accum_err;
}

static int hx8399c_auo_53_off(struct hx8399c_auo_53 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int hx8399c_auo_53_prepare(struct drm_panel *panel)
{
	struct hx8399c_auo_53 *ctx = to_hx8399c_auo_53(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(hx8399c_auo_53_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	hx8399c_auo_53_reset(ctx);

	ret = hx8399c_auo_53_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(hx8399c_auo_53_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int hx8399c_auo_53_unprepare(struct drm_panel *panel)
{
	struct hx8399c_auo_53 *ctx = to_hx8399c_auo_53(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = hx8399c_auo_53_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(hx8399c_auo_53_supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode hx8399c_auo_53_mode = {
	.clock = (1080 + 240 + 20 + 56) * (1920 + 9 + 4 + 3) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 240,
	.hsync_end = 1080 + 240 + 20,
	.htotal = 1080 + 240 + 20 + 56,
	.vdisplay = 1920,
	.vsync_start = 1920 + 9,
	.vsync_end = 1920 + 9 + 4,
	.vtotal = 1920 + 9 + 4 + 3,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int hx8399c_auo_53_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &hx8399c_auo_53_mode);
}

static const struct drm_panel_funcs hx8399c_auo_53_panel_funcs = {
	.prepare = hx8399c_auo_53_prepare,
	.unprepare = hx8399c_auo_53_unprepare,
	.get_modes = hx8399c_auo_53_get_modes,
};

static int hx8399c_auo_53_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct hx8399c_auo_53 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(hx8399c_auo_53_supplies),
					    hx8399c_auo_53_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &hx8399c_auo_53_panel_funcs,
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

static void hx8399c_auo_53_remove(struct mipi_dsi_device *dsi)
{
	struct hx8399c_auo_53 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx8399c_auo_53_of_match[] = {
	{ .compatible = "tenor,hx8399c_auo" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hx8399c_auo_53_of_match);

static struct mipi_dsi_driver hx8399c_auo_53_driver = {
	.probe = hx8399c_auo_53_probe,
	.remove = hx8399c_auo_53_remove,
	.driver = {
		.name = "panel-hx8399c-auo-53",
		.of_match_table = hx8399c_auo_53_of_match,
	},
};
module_mipi_dsi_driver(hx8399c_auo_53_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for hx8399c auo 53 1080p video mode dsi panel");
MODULE_LICENSE("GPL");
