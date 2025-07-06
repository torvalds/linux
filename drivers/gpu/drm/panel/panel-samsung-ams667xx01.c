// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Vitalii Skorkin <nikroksm@mail.ru>

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct ams667xx01 {
	struct drm_panel panel;
	struct drm_connector *connector;
	struct mipi_dsi_device *dsi;
	const struct panel_desc *desc;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
};

struct panel_desc {
	unsigned int width_mm;
	unsigned int height_mm;

	unsigned int bpc;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;

	const struct drm_display_mode *modes;
	unsigned int num_modes;
	const struct mipi_dsi_device_info dsi_info;
	int (*init_sequence)(struct ams667xx01 *ctx);

	struct drm_dsc_config* dsc;
};

static inline
struct ams667xx01 *to_ams667xx01(struct drm_panel *panel)
{
	return container_of(panel, struct ams667xx01, panel);
}

static void ams667xx01_reset(struct ams667xx01 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int ams667xx01_get_current_mode(struct ams667xx01 *ctx)
{
	struct drm_connector *connector = ctx->connector;
	struct drm_crtc_state *crtc_state;
	int i;

	/* Return the default (first) mode if no info available yet */
	if (!connector->state || !connector->state->crtc)
		return 0;

	crtc_state = connector->state->crtc->state;

	for (i = 0; i < ctx->desc->num_modes; i++) {
		if (drm_mode_match(&crtc_state->mode,
				   &ctx->desc->modes[i],
				   DRM_MODE_MATCH_TIMINGS | DRM_MODE_MATCH_CLOCK))
			return i;
	}

	return 0;
}

static int alioth_init_sequence(struct ams667xx01 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	int cur_mode = ams667xx01_get_current_mode(ctx);
	int cur_vrefresh = drm_mode_vrefresh(&ctx->desc->modes[cur_mode]);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e,
					 0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x09,
					 0x60, 0x04, 0x38, 0x00, 0x08, 0x02, 0x1c,
					 0x02, 0x1c, 0x02, 0x00, 0x02, 0x0e, 0x00,
					 0x20, 0x00, 0xbb, 0x00, 0x07, 0x00, 0x0c,
					 0x0d, 0xb7, 0x0c, 0xb7, 0x18, 0x00, 0x10,
					 0xf0, 0x03, 0x0c, 0x20, 0x00, 0x06, 0x0b,
					 0x0b, 0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46,
					 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
					 0x7d, 0x7e, 0x01, 0x02, 0x01, 0x00, 0x09,
					 0x40, 0x09, 0xbe, 0x19, 0xfc, 0x19, 0xfa,
					 0x19, 0xf8, 0x1a, 0x38, 0x1a, 0x78, 0x1a,
					 0xb6, 0x2a, 0xf6, 0x2b, 0x34, 0x2b, 0x74,
					 0x3b, 0x74, 0x6b, 0xf4, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x0437);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x095f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x02);
	if (cur_vrefresh == 120 || cur_vrefresh == 60) {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xec, 0x00, 0xc0, 0xc3, 0x43);
	} else {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xec, 0x00, 0xc2, 0xc2, 0x42);
	}
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xec, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x06);
	if (cur_vrefresh == 120 || cur_vrefresh == 60) {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0xd0);
	} else {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x10);
	}
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0xa6, 0x75, 0xa3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe9,
					 0x11, 0x75, 0xa6, 0x75, 0xa3, 0x8d, 0x06,
					 0x20, 0x8c, 0xa2, 0x4e, 0x00, 0x32, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x83, 0x00, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd4, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x16);
	if (cur_vrefresh == 120 || cur_vrefresh == 60) {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x2e);
	} else {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x6e);
	}
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 90);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x93);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
					 0x20);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x0000);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	if (cur_vrefresh == 120 || cur_vrefresh == 90) {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x10);
	} else {
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	}

	return dsi_ctx.accum_err;
}

static const struct drm_display_mode alioth_modes[] = {
	{
		.clock = (1080 + 16 + 8 + 8) * (2400 + 4 + 4 + 8) * 120 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 16,
		.hsync_end = 1080 + 16 + 8,
		.htotal = 1080 + 16 + 8 + 8,
		.vdisplay = 2400,
		.vsync_start = 2400 + 4,
		.vsync_end = 2400 + 4 + 4,
		.vtotal = 2400 + 4 + 4 + 8,
		.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	{
		.clock = (1080 + 16 + 8 + 8) * (2400 + 300 + 16 + 280) * 90 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 16,
		.hsync_end = 1080 + 16 + 8,
		.htotal = 1080 + 16 + 8 + 8,
		.vdisplay = 2400,
		.vsync_start = 2400 + 300,
		.vsync_end = 2400 + 300 + 16,
		.vtotal = 2400 + 300 + 16 + 280,
		.type = DRM_MODE_TYPE_DRIVER,
	},
// 60hz mode causes screen flickering
#if 0
	{
		.clock = (1080 + 16 + 8 + 8) * (2400 + 600 + 32 + 560) * 60 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 16,
		.hsync_end = 1080 + 16 + 8,
		.htotal = 1080 + 16 + 8 + 8,
		.vdisplay = 2400,
		.vsync_start = 2400 + 600,
		.vsync_end = 2400 + 600 + 32,
		.vtotal = 2400 + 600 + 32 + 560,
		.type = DRM_MODE_TYPE_DRIVER,
	},
#endif
};

static struct drm_dsc_config alioth_dsc_config = {
	.dsc_version_major = 1,
	.dsc_version_minor = 1,
	.slice_width = 540,
	.slice_height = 8,
	.slice_count = 2,
	.bits_per_component = 8,
	.bits_per_pixel = 8 << 4,
	.block_pred_enable = true,
};

static const struct panel_desc alioth_desc = {
	.modes = alioth_modes,
	.num_modes = ARRAY_SIZE(alioth_modes),
	.dsi_info = {
		.type = "alioth",
		.channel = 0,
		.node = NULL,
	},
	.width_mm = 75,
	.height_mm = 160,
	.bpc = 8,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_CLOCK_NON_CONTINUOUS |
				MIPI_DSI_MODE_LPM,
	.init_sequence = alioth_init_sequence,
	.dsc = &alioth_dsc_config,
};

static int ams667xx01_prepare(struct drm_panel *panel)
{
	struct ams667xx01 *ctx = to_ams667xx01(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ams667xx01_reset(ctx);

	ret = ctx->desc->init_sequence(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	drm_dsc_pps_payload_pack(&pps, ctx->desc->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		return ret;
	}

	msleep(28);

	return 0;
}

static int ams667xx01_enable(struct drm_panel *panel)
{
	struct ams667xx01 *ctx = to_ams667xx01(panel);
	struct drm_dsc_picture_parameter_set pps;
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	usleep_range(10000, 11000);

	drm_dsc_pps_payload_pack(&pps, ctx->desc->dsc);

	ret = mipi_dsi_picture_parameter_set(dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}

	msleep(28);

	return ret;
}

static int ams667xx01_disable(struct drm_panel *panel)
{
	struct ams667xx01 *ctx = to_ams667xx01(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", ret);

	msleep(20);

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", ret);

	msleep(120);

	return ret;
}

static int ams667xx01_unprepare(struct drm_panel *panel)
{
	struct ams667xx01 *ctx = to_ams667xx01(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static int ams667xx01_get_modes(struct drm_panel *panel,
					   struct drm_connector *connector)
{
	struct ams667xx01 *ctx = to_ams667xx01(panel);
	struct drm_display_mode *mode;
	unsigned int i;

	for (i = 0; i < ctx->desc->num_modes; i++) {
		const struct drm_display_mode *m = &ctx->desc->modes[i];

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = ctx->desc->width_mm;
	connector->display_info.height_mm = ctx->desc->height_mm;
	connector->display_info.bpc = ctx->desc->bpc;
	ctx->connector = connector;

	return ctx->desc->num_modes;
}

static const struct drm_panel_funcs ams667xx01_panel_funcs = {
	.prepare = ams667xx01_prepare,
	.enable = ams667xx01_enable,
	.disable = ams667xx01_disable,
	.unprepare = ams667xx01_unprepare,
	.get_modes = ams667xx01_get_modes,
};

static int ams667xx01_bl_update_status(struct backlight_device *bl)
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

static const struct backlight_ops ams667xx01_bl_ops = {
	.update_status = ams667xx01_bl_update_status,
};

static struct backlight_device *
ams667xx01_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 1023,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
						  &ams667xx01_bl_ops, &props);
}

static int ams667xx01_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct ams667xx01 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vci";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
					  ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
					 "Failed to get reset-gpios\n");

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	drm_panel_init(&ctx->panel, dev, &ams667xx01_panel_funcs,
			   DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = ams667xx01_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
					 "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	dsi->lanes = ctx->desc->lanes;
	dsi->format = ctx->desc->format;
	dsi->mode_flags = ctx->desc->mode_flags;

	dsi->dsc = ctx->desc->dsc;
	dsi->dsc_slice_per_pkt = 2;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void ams667xx01_remove(struct mipi_dsi_device *dsi)
{
	struct ams667xx01 *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id ams667xx01_of_match[] = {
	{
		.compatible = "xiaomi,alioth-ams667xx01",
		.data = &alioth_desc,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ams667xx01_of_match);

static struct mipi_dsi_driver ams667xx01_driver = {
	.probe = ams667xx01_probe,
	.remove = ams667xx01_remove,
	.driver = {
		.name = "panel-samsung-ams667xx01",
		.of_match_table = ams667xx01_of_match,
	},
};
module_mipi_dsi_driver(ams667xx01_driver);

MODULE_AUTHOR("Vitalii Skorkin <nikroksm@mail.ru>");
MODULE_DESCRIPTION("DRM driver for SAMSUNG AMS667XX01 dsi panel");
MODULE_LICENSE("GPL");
