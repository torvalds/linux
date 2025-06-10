// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 Collabora

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct raydium_rm67200_panel_info {
	struct drm_display_mode mode;
	const struct regulator_bulk_data *regulators;
	int num_regulators;
	void (*panel_setup)(struct mipi_dsi_multi_context *ctx);
};

struct raydium_rm67200 {
	struct drm_panel panel;
	const struct raydium_rm67200_panel_info *panel_info;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	int num_supplies;
};

static inline struct raydium_rm67200 *to_raydium_rm67200(struct drm_panel *panel)
{
	return container_of(panel, struct raydium_rm67200, panel);
}

static void raydium_rm67200_reset(struct raydium_rm67200 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(60);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(60);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(60);
}

static void raydium_rm67200_write(struct mipi_dsi_multi_context *ctx,
				  u8 arg1, u8 arg2)
{
	u8 d[] = { arg1, arg2 };

	mipi_dsi_generic_write_multi(ctx, d, ARRAY_SIZE(d));
}

static void w552793baa_setup(struct mipi_dsi_multi_context *ctx)
{
	raydium_rm67200_write(ctx, 0xfe, 0x21);
	raydium_rm67200_write(ctx, 0x04, 0x00);
	raydium_rm67200_write(ctx, 0x00, 0x64);
	raydium_rm67200_write(ctx, 0x2a, 0x00);
	raydium_rm67200_write(ctx, 0x26, 0x64);
	raydium_rm67200_write(ctx, 0x54, 0x00);
	raydium_rm67200_write(ctx, 0x50, 0x64);
	raydium_rm67200_write(ctx, 0x7b, 0x00);
	raydium_rm67200_write(ctx, 0x77, 0x64);
	raydium_rm67200_write(ctx, 0xa2, 0x00);
	raydium_rm67200_write(ctx, 0x9d, 0x64);
	raydium_rm67200_write(ctx, 0xc9, 0x00);
	raydium_rm67200_write(ctx, 0xc5, 0x64);
	raydium_rm67200_write(ctx, 0x01, 0x71);
	raydium_rm67200_write(ctx, 0x27, 0x71);
	raydium_rm67200_write(ctx, 0x51, 0x71);
	raydium_rm67200_write(ctx, 0x78, 0x71);
	raydium_rm67200_write(ctx, 0x9e, 0x71);
	raydium_rm67200_write(ctx, 0xc6, 0x71);
	raydium_rm67200_write(ctx, 0x02, 0x89);
	raydium_rm67200_write(ctx, 0x28, 0x89);
	raydium_rm67200_write(ctx, 0x52, 0x89);
	raydium_rm67200_write(ctx, 0x79, 0x89);
	raydium_rm67200_write(ctx, 0x9f, 0x89);
	raydium_rm67200_write(ctx, 0xc7, 0x89);
	raydium_rm67200_write(ctx, 0x03, 0x9e);
	raydium_rm67200_write(ctx, 0x29, 0x9e);
	raydium_rm67200_write(ctx, 0x53, 0x9e);
	raydium_rm67200_write(ctx, 0x7a, 0x9e);
	raydium_rm67200_write(ctx, 0xa0, 0x9e);
	raydium_rm67200_write(ctx, 0xc8, 0x9e);
	raydium_rm67200_write(ctx, 0x09, 0x00);
	raydium_rm67200_write(ctx, 0x05, 0xb0);
	raydium_rm67200_write(ctx, 0x31, 0x00);
	raydium_rm67200_write(ctx, 0x2b, 0xb0);
	raydium_rm67200_write(ctx, 0x5a, 0x00);
	raydium_rm67200_write(ctx, 0x55, 0xb0);
	raydium_rm67200_write(ctx, 0x80, 0x00);
	raydium_rm67200_write(ctx, 0x7c, 0xb0);
	raydium_rm67200_write(ctx, 0xa7, 0x00);
	raydium_rm67200_write(ctx, 0xa3, 0xb0);
	raydium_rm67200_write(ctx, 0xce, 0x00);
	raydium_rm67200_write(ctx, 0xca, 0xb0);
	raydium_rm67200_write(ctx, 0x06, 0xc0);
	raydium_rm67200_write(ctx, 0x2d, 0xc0);
	raydium_rm67200_write(ctx, 0x56, 0xc0);
	raydium_rm67200_write(ctx, 0x7d, 0xc0);
	raydium_rm67200_write(ctx, 0xa4, 0xc0);
	raydium_rm67200_write(ctx, 0xcb, 0xc0);
	raydium_rm67200_write(ctx, 0x07, 0xcf);
	raydium_rm67200_write(ctx, 0x2f, 0xcf);
	raydium_rm67200_write(ctx, 0x58, 0xcf);
	raydium_rm67200_write(ctx, 0x7e, 0xcf);
	raydium_rm67200_write(ctx, 0xa5, 0xcf);
	raydium_rm67200_write(ctx, 0xcc, 0xcf);
	raydium_rm67200_write(ctx, 0x08, 0xdd);
	raydium_rm67200_write(ctx, 0x30, 0xdd);
	raydium_rm67200_write(ctx, 0x59, 0xdd);
	raydium_rm67200_write(ctx, 0x7f, 0xdd);
	raydium_rm67200_write(ctx, 0xa6, 0xdd);
	raydium_rm67200_write(ctx, 0xcd, 0xdd);
	raydium_rm67200_write(ctx, 0x0e, 0x15);
	raydium_rm67200_write(ctx, 0x0a, 0xe9);
	raydium_rm67200_write(ctx, 0x36, 0x15);
	raydium_rm67200_write(ctx, 0x32, 0xe9);
	raydium_rm67200_write(ctx, 0x5f, 0x15);
	raydium_rm67200_write(ctx, 0x5b, 0xe9);
	raydium_rm67200_write(ctx, 0x85, 0x15);
	raydium_rm67200_write(ctx, 0x81, 0xe9);
	raydium_rm67200_write(ctx, 0xad, 0x15);
	raydium_rm67200_write(ctx, 0xa9, 0xe9);
	raydium_rm67200_write(ctx, 0xd3, 0x15);
	raydium_rm67200_write(ctx, 0xcf, 0xe9);
	raydium_rm67200_write(ctx, 0x0b, 0x14);
	raydium_rm67200_write(ctx, 0x33, 0x14);
	raydium_rm67200_write(ctx, 0x5c, 0x14);
	raydium_rm67200_write(ctx, 0x82, 0x14);
	raydium_rm67200_write(ctx, 0xaa, 0x14);
	raydium_rm67200_write(ctx, 0xd0, 0x14);
	raydium_rm67200_write(ctx, 0x0c, 0x36);
	raydium_rm67200_write(ctx, 0x34, 0x36);
	raydium_rm67200_write(ctx, 0x5d, 0x36);
	raydium_rm67200_write(ctx, 0x83, 0x36);
	raydium_rm67200_write(ctx, 0xab, 0x36);
	raydium_rm67200_write(ctx, 0xd1, 0x36);
	raydium_rm67200_write(ctx, 0x0d, 0x6b);
	raydium_rm67200_write(ctx, 0x35, 0x6b);
	raydium_rm67200_write(ctx, 0x5e, 0x6b);
	raydium_rm67200_write(ctx, 0x84, 0x6b);
	raydium_rm67200_write(ctx, 0xac, 0x6b);
	raydium_rm67200_write(ctx, 0xd2, 0x6b);
	raydium_rm67200_write(ctx, 0x13, 0x5a);
	raydium_rm67200_write(ctx, 0x0f, 0x94);
	raydium_rm67200_write(ctx, 0x3b, 0x5a);
	raydium_rm67200_write(ctx, 0x37, 0x94);
	raydium_rm67200_write(ctx, 0x64, 0x5a);
	raydium_rm67200_write(ctx, 0x60, 0x94);
	raydium_rm67200_write(ctx, 0x8a, 0x5a);
	raydium_rm67200_write(ctx, 0x86, 0x94);
	raydium_rm67200_write(ctx, 0xb2, 0x5a);
	raydium_rm67200_write(ctx, 0xae, 0x94);
	raydium_rm67200_write(ctx, 0xd8, 0x5a);
	raydium_rm67200_write(ctx, 0xd4, 0x94);
	raydium_rm67200_write(ctx, 0x10, 0xd1);
	raydium_rm67200_write(ctx, 0x38, 0xd1);
	raydium_rm67200_write(ctx, 0x61, 0xd1);
	raydium_rm67200_write(ctx, 0x87, 0xd1);
	raydium_rm67200_write(ctx, 0xaf, 0xd1);
	raydium_rm67200_write(ctx, 0xd5, 0xd1);
	raydium_rm67200_write(ctx, 0x11, 0x04);
	raydium_rm67200_write(ctx, 0x39, 0x04);
	raydium_rm67200_write(ctx, 0x62, 0x04);
	raydium_rm67200_write(ctx, 0x88, 0x04);
	raydium_rm67200_write(ctx, 0xb0, 0x04);
	raydium_rm67200_write(ctx, 0xd6, 0x04);
	raydium_rm67200_write(ctx, 0x12, 0x05);
	raydium_rm67200_write(ctx, 0x3a, 0x05);
	raydium_rm67200_write(ctx, 0x63, 0x05);
	raydium_rm67200_write(ctx, 0x89, 0x05);
	raydium_rm67200_write(ctx, 0xb1, 0x05);
	raydium_rm67200_write(ctx, 0xd7, 0x05);
	raydium_rm67200_write(ctx, 0x18, 0xaa);
	raydium_rm67200_write(ctx, 0x14, 0x36);
	raydium_rm67200_write(ctx, 0x42, 0xaa);
	raydium_rm67200_write(ctx, 0x3d, 0x36);
	raydium_rm67200_write(ctx, 0x69, 0xaa);
	raydium_rm67200_write(ctx, 0x65, 0x36);
	raydium_rm67200_write(ctx, 0x8f, 0xaa);
	raydium_rm67200_write(ctx, 0x8b, 0x36);
	raydium_rm67200_write(ctx, 0xb7, 0xaa);
	raydium_rm67200_write(ctx, 0xb3, 0x36);
	raydium_rm67200_write(ctx, 0xdd, 0xaa);
	raydium_rm67200_write(ctx, 0xd9, 0x36);
	raydium_rm67200_write(ctx, 0x15, 0x74);
	raydium_rm67200_write(ctx, 0x3f, 0x74);
	raydium_rm67200_write(ctx, 0x66, 0x74);
	raydium_rm67200_write(ctx, 0x8c, 0x74);
	raydium_rm67200_write(ctx, 0xb4, 0x74);
	raydium_rm67200_write(ctx, 0xda, 0x74);
	raydium_rm67200_write(ctx, 0x16, 0x9f);
	raydium_rm67200_write(ctx, 0x40, 0x9f);
	raydium_rm67200_write(ctx, 0x67, 0x9f);
	raydium_rm67200_write(ctx, 0x8d, 0x9f);
	raydium_rm67200_write(ctx, 0xb5, 0x9f);
	raydium_rm67200_write(ctx, 0xdb, 0x9f);
	raydium_rm67200_write(ctx, 0x17, 0xdc);
	raydium_rm67200_write(ctx, 0x41, 0xdc);
	raydium_rm67200_write(ctx, 0x68, 0xdc);
	raydium_rm67200_write(ctx, 0x8e, 0xdc);
	raydium_rm67200_write(ctx, 0xb6, 0xdc);
	raydium_rm67200_write(ctx, 0xdc, 0xdc);
	raydium_rm67200_write(ctx, 0x1d, 0xff);
	raydium_rm67200_write(ctx, 0x19, 0x03);
	raydium_rm67200_write(ctx, 0x47, 0xff);
	raydium_rm67200_write(ctx, 0x43, 0x03);
	raydium_rm67200_write(ctx, 0x6e, 0xff);
	raydium_rm67200_write(ctx, 0x6a, 0x03);
	raydium_rm67200_write(ctx, 0x94, 0xff);
	raydium_rm67200_write(ctx, 0x90, 0x03);
	raydium_rm67200_write(ctx, 0xbc, 0xff);
	raydium_rm67200_write(ctx, 0xb8, 0x03);
	raydium_rm67200_write(ctx, 0xe2, 0xff);
	raydium_rm67200_write(ctx, 0xde, 0x03);
	raydium_rm67200_write(ctx, 0x1a, 0x35);
	raydium_rm67200_write(ctx, 0x44, 0x35);
	raydium_rm67200_write(ctx, 0x6b, 0x35);
	raydium_rm67200_write(ctx, 0x91, 0x35);
	raydium_rm67200_write(ctx, 0xb9, 0x35);
	raydium_rm67200_write(ctx, 0xdf, 0x35);
	raydium_rm67200_write(ctx, 0x1b, 0x45);
	raydium_rm67200_write(ctx, 0x45, 0x45);
	raydium_rm67200_write(ctx, 0x6c, 0x45);
	raydium_rm67200_write(ctx, 0x92, 0x45);
	raydium_rm67200_write(ctx, 0xba, 0x45);
	raydium_rm67200_write(ctx, 0xe0, 0x45);
	raydium_rm67200_write(ctx, 0x1c, 0x55);
	raydium_rm67200_write(ctx, 0x46, 0x55);
	raydium_rm67200_write(ctx, 0x6d, 0x55);
	raydium_rm67200_write(ctx, 0x93, 0x55);
	raydium_rm67200_write(ctx, 0xbb, 0x55);
	raydium_rm67200_write(ctx, 0xe1, 0x55);
	raydium_rm67200_write(ctx, 0x22, 0xff);
	raydium_rm67200_write(ctx, 0x1e, 0x68);
	raydium_rm67200_write(ctx, 0x4c, 0xff);
	raydium_rm67200_write(ctx, 0x48, 0x68);
	raydium_rm67200_write(ctx, 0x73, 0xff);
	raydium_rm67200_write(ctx, 0x6f, 0x68);
	raydium_rm67200_write(ctx, 0x99, 0xff);
	raydium_rm67200_write(ctx, 0x95, 0x68);
	raydium_rm67200_write(ctx, 0xc1, 0xff);
	raydium_rm67200_write(ctx, 0xbd, 0x68);
	raydium_rm67200_write(ctx, 0xe7, 0xff);
	raydium_rm67200_write(ctx, 0xe3, 0x68);
	raydium_rm67200_write(ctx, 0x1f, 0x7e);
	raydium_rm67200_write(ctx, 0x49, 0x7e);
	raydium_rm67200_write(ctx, 0x70, 0x7e);
	raydium_rm67200_write(ctx, 0x96, 0x7e);
	raydium_rm67200_write(ctx, 0xbe, 0x7e);
	raydium_rm67200_write(ctx, 0xe4, 0x7e);
	raydium_rm67200_write(ctx, 0x20, 0x97);
	raydium_rm67200_write(ctx, 0x4a, 0x97);
	raydium_rm67200_write(ctx, 0x71, 0x97);
	raydium_rm67200_write(ctx, 0x97, 0x97);
	raydium_rm67200_write(ctx, 0xbf, 0x97);
	raydium_rm67200_write(ctx, 0xe5, 0x97);
	raydium_rm67200_write(ctx, 0x21, 0xb5);
	raydium_rm67200_write(ctx, 0x4b, 0xb5);
	raydium_rm67200_write(ctx, 0x72, 0xb5);
	raydium_rm67200_write(ctx, 0x98, 0xb5);
	raydium_rm67200_write(ctx, 0xc0, 0xb5);
	raydium_rm67200_write(ctx, 0xe6, 0xb5);
	raydium_rm67200_write(ctx, 0x25, 0xf0);
	raydium_rm67200_write(ctx, 0x23, 0xe8);
	raydium_rm67200_write(ctx, 0x4f, 0xf0);
	raydium_rm67200_write(ctx, 0x4d, 0xe8);
	raydium_rm67200_write(ctx, 0x76, 0xf0);
	raydium_rm67200_write(ctx, 0x74, 0xe8);
	raydium_rm67200_write(ctx, 0x9c, 0xf0);
	raydium_rm67200_write(ctx, 0x9a, 0xe8);
	raydium_rm67200_write(ctx, 0xc4, 0xf0);
	raydium_rm67200_write(ctx, 0xc2, 0xe8);
	raydium_rm67200_write(ctx, 0xea, 0xf0);
	raydium_rm67200_write(ctx, 0xe8, 0xe8);
	raydium_rm67200_write(ctx, 0x24, 0xff);
	raydium_rm67200_write(ctx, 0x4e, 0xff);
	raydium_rm67200_write(ctx, 0x75, 0xff);
	raydium_rm67200_write(ctx, 0x9b, 0xff);
	raydium_rm67200_write(ctx, 0xc3, 0xff);
	raydium_rm67200_write(ctx, 0xe9, 0xff);
	raydium_rm67200_write(ctx, 0xfe, 0x3d);
	raydium_rm67200_write(ctx, 0x00, 0x04);
	raydium_rm67200_write(ctx, 0xfe, 0x23);
	raydium_rm67200_write(ctx, 0x08, 0x82);
	raydium_rm67200_write(ctx, 0x0a, 0x00);
	raydium_rm67200_write(ctx, 0x0b, 0x00);
	raydium_rm67200_write(ctx, 0x0c, 0x01);
	raydium_rm67200_write(ctx, 0x16, 0x00);
	raydium_rm67200_write(ctx, 0x18, 0x02);
	raydium_rm67200_write(ctx, 0x1b, 0x04);
	raydium_rm67200_write(ctx, 0x19, 0x04);
	raydium_rm67200_write(ctx, 0x1c, 0x81);
	raydium_rm67200_write(ctx, 0x1f, 0x00);
	raydium_rm67200_write(ctx, 0x20, 0x03);
	raydium_rm67200_write(ctx, 0x23, 0x04);
	raydium_rm67200_write(ctx, 0x21, 0x01);
	raydium_rm67200_write(ctx, 0x54, 0x63);
	raydium_rm67200_write(ctx, 0x55, 0x54);
	raydium_rm67200_write(ctx, 0x6e, 0x45);
	raydium_rm67200_write(ctx, 0x6d, 0x36);
	raydium_rm67200_write(ctx, 0xfe, 0x3d);
	raydium_rm67200_write(ctx, 0x55, 0x78);
	raydium_rm67200_write(ctx, 0xfe, 0x20);
	raydium_rm67200_write(ctx, 0x26, 0x30);
	raydium_rm67200_write(ctx, 0xfe, 0x3d);
	raydium_rm67200_write(ctx, 0x20, 0x71);
	raydium_rm67200_write(ctx, 0x50, 0x8f);
	raydium_rm67200_write(ctx, 0x51, 0x8f);
	raydium_rm67200_write(ctx, 0xfe, 0x00);
	raydium_rm67200_write(ctx, 0x35, 0x00);
}

static int raydium_rm67200_prepare(struct drm_panel *panel)
{
	struct raydium_rm67200 *ctx = to_raydium_rm67200(panel);
	int ret;

	ret = regulator_bulk_enable(ctx->num_supplies, ctx->supplies);
	if (ret < 0)
		return ret;

	raydium_rm67200_reset(ctx);

	msleep(60);

	return 0;
}

static int raydium_rm67200_unprepare(struct drm_panel *panel)
{
	struct raydium_rm67200 *ctx = to_raydium_rm67200(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ctx->num_supplies, ctx->supplies);

	msleep(60);

	return 0;
}

static int raydium_rm67200_enable(struct drm_panel *panel)
{
	struct raydium_rm67200 *rm67200 = to_raydium_rm67200(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = rm67200->dsi };

	rm67200->panel_info->panel_setup(&ctx);
	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_msleep(&ctx, 30);

	return ctx.accum_err;
}

static int raydium_rm67200_disable(struct drm_panel *panel)
{
	struct raydium_rm67200 *rm67200 = to_raydium_rm67200(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = rm67200->dsi };

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 60);

	return ctx.accum_err;
}

static int raydium_rm67200_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct raydium_rm67200 *ctx = to_raydium_rm67200(panel);

	return drm_connector_helper_get_modes_fixed(connector, &ctx->panel_info->mode);
}

static const struct drm_panel_funcs raydium_rm67200_funcs = {
	.prepare = raydium_rm67200_prepare,
	.unprepare = raydium_rm67200_unprepare,
	.get_modes = raydium_rm67200_get_modes,
	.enable = raydium_rm67200_enable,
	.disable = raydium_rm67200_disable,
};

static int raydium_rm67200_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct raydium_rm67200 *ctx;
	int ret = 0;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->panel_info = device_get_match_data(dev);
	if (!ctx->panel_info)
		return -EINVAL;

	ctx->num_supplies = ctx->panel_info->num_regulators;
	ret = devm_regulator_bulk_get_const(&dsi->dev,
					    ctx->panel_info->num_regulators,
					    ctx->panel_info->regulators,
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM;
	ctx->panel.prepare_prev_first = true;

	drm_panel_init(&ctx->panel, dev, &raydium_rm67200_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
	}

	return ret;
}

static void raydium_rm67200_remove(struct mipi_dsi_device *dsi)
{
	struct raydium_rm67200 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct regulator_bulk_data w552793baa_regulators[] = {
	{ .supply = "vdd", },		/*  2.8V */
	{ .supply = "iovcc", },		/*  1.8V */
	{ .supply = "vsp", },		/* +5.5V */
	{ .supply = "vsn", },		/* -5.5V */
};

static const struct raydium_rm67200_panel_info w552793baa_info = {
	.mode = {
		.clock = 132000,
		.hdisplay = 1080,
		.hsync_start = 1095,
		.hsync_end = 1125,
		.htotal = 1129,
		.vdisplay = 1920,
		.vsync_start = 1935,
		.vsync_end = 1950,
		.vtotal = 1952,
		.width_mm = 68, /* 68.04mm */
		.height_mm = 121, /* 120.96mm */
		.type = DRM_MODE_TYPE_DRIVER,
	},
	.regulators = w552793baa_regulators,
	.num_regulators = ARRAY_SIZE(w552793baa_regulators),
	.panel_setup = w552793baa_setup,
};

static const struct of_device_id raydium_rm67200_of_match[] = {
	{ .compatible = "wanchanglong,w552793baa", .data = &w552793baa_info },
	{ /*sentinel*/ }
};
MODULE_DEVICE_TABLE(of, raydium_rm67200_of_match);

static struct mipi_dsi_driver raydium_rm67200_driver = {
	.probe = raydium_rm67200_probe,
	.remove = raydium_rm67200_remove,
	.driver = {
		.name = "panel-raydium-rm67200",
		.of_match_table = raydium_rm67200_of_match,
	},
};
module_mipi_dsi_driver(raydium_rm67200_driver);

MODULE_AUTHOR("Sebastian Reichel <sebastian.reichel@collabora.com>");
MODULE_DESCRIPTION("DRM driver for RM67200-equipped DSI panels");
MODULE_LICENSE("GPL");
