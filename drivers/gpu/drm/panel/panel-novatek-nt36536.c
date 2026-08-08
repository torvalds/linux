// SPDX-License-Identifier: GPL-2.0-only
/*
 * Novatek NT36536 DriverIC panels driver
 * Copyright (c) 2026 Pengyu Luo <mitltlatltl@gmail.com>
 *
 * Based on the sample code which is generated with
 * linux-mdss-dsi-panel-driver-generator
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/device-id/of.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/mipi_display.h>

struct novatek {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	const struct panel_desc *desc;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;
	struct backlight_device *backlight;
};

struct panel_desc {
	unsigned int width_mm;
	unsigned int height_mm;
	unsigned int bpc;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	const struct drm_dsc_config *dsc_cfg;
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	int (*init_sequence)(struct mipi_dsi_multi_context *dsi_ctx);

	bool is_dual_dsi;
	bool has_dcs_backlight;
};

static const struct regulator_bulk_data novatek_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vsp" },
	{ .supply = "vsn" },
};

static inline struct novatek *to_novatek(struct drm_panel *panel)
{
	return container_of(panel, struct novatek, panel);
}

static inline struct mipi_dsi_device *to_primary_dsi(struct novatek *ctx)
{
	/* Sync on DSI1 for dual dsi */
	return ctx->desc->is_dual_dsi ? ctx->dsi[1] : ctx->dsi[0];
}

static void novatek_reset(struct novatek *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(11000, 12000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(3000, 4000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int novatek_prepare(struct drm_panel *panel)
{
	struct novatek *ctx = to_novatek(panel);
	struct mipi_dsi_device *dsi = to_primary_dsi(ctx);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	struct drm_dsc_picture_parameter_set pps;
	struct device *dev = &dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(novatek_supplies),
				    ctx->supplies);
	if (ret < 0)
		return ret;

	novatek_reset(ctx);

	ret = ctx->desc->init_sequence(&dsi_ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(novatek_supplies),
				       ctx->supplies);
		return ret;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	mipi_dsi_picture_parameter_set_multi(&dsi_ctx, &pps);
	mipi_dsi_compression_mode_multi(&dsi_ctx, true);
	mipi_dsi_msleep(&dsi_ctx, 28);

	return backlight_enable(ctx->backlight);
}

static int novatek_off(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_set_display_off_multi(dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(dsi_ctx);

	return dsi_ctx->accum_err;
}

static int novatek_unprepare(struct drm_panel *panel)
{
	struct novatek *ctx = to_novatek(panel);
	struct mipi_dsi_device *dsi = to_primary_dsi(ctx);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	struct device *dev = &dsi->dev;
	int ret;

	backlight_disable(ctx->backlight);

	ret = novatek_off(&dsi_ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(novatek_supplies), ctx->supplies);

	return 0;
}

static int novatek_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct novatek *ctx = to_novatek(panel);
	const struct panel_desc *desc = ctx->desc;
	int i;

	for (i = 0; i < desc->num_modes; i++) {
		const struct drm_display_mode *m = &desc->modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, drm_mode_vrefresh(m));
			return -ENOMEM;
		}

		mode->type = DRM_MODE_TYPE_DRIVER;
		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
	}

	connector->display_info.width_mm = desc->width_mm;
	connector->display_info.height_mm = desc->height_mm;
	connector->display_info.bpc = desc->bpc;

	return desc->num_modes;
}

static const struct drm_panel_funcs novatek_panel_funcs = {
	.prepare = novatek_prepare,
	.unprepare = novatek_unprepare,
	.get_modes = novatek_get_modes,
};

static int novatek_bl_update_status(struct backlight_device *bl)
{
	struct novatek *ctx = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);

	return mipi_dsi_dcs_set_display_brightness_large(to_primary_dsi(ctx),
							 brightness);
}

static const struct backlight_ops novatek_bl_ops = {
	.update_status = novatek_bl_update_status,
};

static struct backlight_device *novatek_create_backlight(struct novatek *ctx)
{
	struct mipi_dsi_device *dsi = to_primary_dsi(ctx);
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 512,
		.max_brightness = 4095,
		.scale = BACKLIGHT_SCALE_NON_LINEAR,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, ctx,
					      &novatek_bl_ops, &props);
}

static int csot_pp8807hb1_1_init_seq(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, MIPI_DCS_SET_PARTIAL_ROWS, 0x50);

	/* cabc */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x23);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x89, 0xa4);

	/* pen code */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcd, 0x8f, 0xa9, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xce, 0x8c, 0xa9, 0x00);

	/* esd init */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x27);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x79, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd0, 0x31);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd1, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2, 0x08);

	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0xd0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x31);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x09, 0xee);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x1c, 0x77);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x1d, 0x07);

	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0x00);

	/* IC transfer */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0xf0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe6, 0x02);

	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x3b,
				     0x03, 0x4e, 0x1a, 0x04, 0x04, 0x01, 0x80,
				     0x36, 0x36);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x60, 0x00);
	/* enable DSC */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x90, 0x03, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91,
				     0xab, 0xa8, 0x00, 0x14, 0xd2, 0x00, 0x00,
				     0x00, 0x01, 0xb9, 0x00, 0x06, 0x05, 0x7a,
				     0x05, 0xb8);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x92, 0x10, 0xe0);
	/* frame ctl */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2, 0x91, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb3, 0x40);
	mipi_dsi_dcs_exit_sleep_mode_multi(dsi_ctx);
	mipi_dsi_msleep(dsi_ctx, 105);
	mipi_dsi_dcs_set_display_on_multi(dsi_ctx);
	mipi_dsi_msleep(dsi_ctx, 40);

	return dsi_ctx->accum_err;
}

static struct drm_dsc_config csot_pp8807hb1_1_dsc_cfg = {
	.dsc_version_major = 1,
	.dsc_version_minor = 2,
	.slice_height = 20,
	.slice_width = 476,
	.slice_count = 2,
	.bits_per_component = 10,
	.bits_per_pixel = 8 << 4,
	.block_pred_enable = true,
};

static const struct drm_display_mode csot_pp8807hb1_1_modes[] = {
	/* 915552 KHz */
	{
		.clock = (952 + 138 + 16 + 16) * 2 * (3040 + 26 + 4 + 330) * 120 / 1000,
		.hdisplay = 952 * 2,
		.hsync_start = (952 + 138) * 2,
		.hsync_end = (952 + 138 + 16) * 2,
		.htotal = (952 + 138 + 16 + 16) * 2,
		.vdisplay = 3040,
		.vsync_start = 3040 + 26,
		.vsync_end = 3040 + 26 + 4,
		.vtotal = 3040 + 26 + 4 + 330,
	},
	{
		.clock = (952 + 138 + 16 + 16) * 2 * (3040 + 3426 + 4 + 330) * 60 / 1000,
		.hdisplay = 952 * 2,
		.hsync_start = (952 + 138) * 2,
		.hsync_end = (952 + 138 + 16) * 2,
		.htotal = (952 + 138 + 16 + 16) * 2,
		.vdisplay = 3040,
		.vsync_start = 3040 + 3426,
		.vsync_end = 3040 + 3426 + 4,
		.vtotal = 3040 + 3426 + 4 + 330,
	},
	{
		.clock = (952 + 138 + 16 + 16) * 2 * (3040 + 10226 + 4 + 330) * 30 / 1000,
		.hdisplay = 952 * 2,
		.hsync_start = (952 + 138) * 2,
		.hsync_end = (952 + 138 + 16) * 2,
		.htotal = (952 + 138 + 16 + 16) * 2,
		.vdisplay = 3040,
		.vsync_start = 3040 + 10226,
		.vsync_end = 3040 + 10226 + 4,
		.vtotal = 3040 + 10226 + 4 + 330,
	},
	/* 1064606.4 KHz */
	{
		.clock = (952 + 50 + 16 + 16) * 2 * (3040 + 26 + 4 + 50) * 165 / 1000,
		.hdisplay = 952 * 2,
		.hsync_start = (952 + 50) * 2,
		.hsync_end = (952 + 50 + 16) * 2,
		.htotal = (952 + 50 + 16 + 16) * 2,
		.vdisplay = 3040,
		.vsync_start = 3040 + 26,
		.vsync_end = 3040 + 26 + 4,
		.vtotal = 3040 + 26 + 4 + 50,
	},
	{
		.clock = (952 + 50 + 16 + 16) * 2 * (3040 + 481 + 4 + 50) * 144 / 1000,
		.hdisplay = 952 * 2,
		.hsync_start = (952 + 50) * 2,
		.hsync_end = (952 + 50 + 16) * 2,
		.htotal = (952 + 50 + 16 + 16) * 2,
		.vdisplay = 3040,
		.vsync_start = 3040 + 481,
		.vsync_end = 3040 + 481 + 4,
		.vtotal = 3040 + 481 + 4 + 50,
	},
	/* 737942.4 KHz */
	{
		.clock = (952 + 330 + 16 + 16) * 2 * (3040 + 26 + 4 + 50) * 90 / 1000,
		.hdisplay = 952 * 2,
		.hsync_start = (952 + 330) * 2,
		.hsync_end = (952 + 330 + 16) * 2,
		.htotal = (952 + 330 + 16 + 16) * 2,
		.vdisplay = 3040,
		.vsync_start = 3040 + 26,
		.vsync_end = 3040 + 26 + 4,
		.vtotal = 3040 + 26 + 4 + 50,
	},
};

static int novatek_probe(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_device_info dsi_info = {"dsi-secondary", 0, NULL};
	struct mipi_dsi_host *dsi1_host;
	struct device *dev = &dsi->dev;
	const struct panel_desc *desc;
	struct device_node *dsi1;
	struct novatek *ctx;
	int num_dsi = 1;
	int ret, i;

	ctx = devm_drm_panel_alloc(dev, struct novatek, panel,
				   &novatek_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(novatek_supplies),
					    novatek_supplies, &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -ENODEV;
	ctx->desc = desc;
	ctx->dsc = *desc->dsc_cfg;

	if (desc->is_dual_dsi) {
		num_dsi = 2;
		dsi1 = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
		if (!dsi1) {
			dev_err(dev, "cannot get secondary DSI node.\n");
			return -ENODEV;
		}

		dsi1_host = of_find_mipi_dsi_host_by_node(dsi1);
		of_node_put(dsi1);
		if (!dsi1_host)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "cannot get secondary DSI host\n");

		ctx->dsi[1] = devm_mipi_dsi_device_register_full(dev, dsi1_host,
								 &dsi_info);
		if (IS_ERR(ctx->dsi[1])) {
			dev_err(dev, "cannot get secondary DSI device\n");
			return PTR_ERR(ctx->dsi[1]);
		}

		mipi_dsi_set_drvdata(ctx->dsi[1], ctx);
	}

	ctx->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->panel.prepare_prev_first = true;

	ret = devm_drm_panel_add(dev, &ctx->panel);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to add panel\n");

	for (i = 0; i < num_dsi; i++) {
		ctx->dsi[i]->lanes = desc->lanes;
		ctx->dsi[i]->format = desc->format;
		ctx->dsi[i]->mode_flags = desc->mode_flags;
		ctx->dsi[i]->dsc = &ctx->dsc;
		ret = devm_mipi_dsi_attach(dev, ctx->dsi[i]);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to attach to DSI host\n");
	}

	if (desc->has_dcs_backlight) {
		ctx->backlight = novatek_create_backlight(ctx);
		if (IS_ERR(ctx->backlight))
			return dev_err_probe(dev, PTR_ERR(ctx->backlight),
					     "Failed to create backlight\n");
	} else {
		ret = drm_panel_of_backlight(&ctx->panel);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to get backlight\n");
	}

	return 0;
}

/* Model name: CSOT PP8807HB1-1 */
static const struct panel_desc csot_pp8807hb1_1_desc = {
	.width_mm = 118,
	.height_mm = 190,
	.bpc = 10, /* set this to 8 with RGB888 format to support 8-bit mode */
	.lanes = 3,
	.format = MIPI_DSI_FMT_RGB101010,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS |
		      MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_DSC_ALL_SLICES_IN_PKT,
	.dsc_cfg = &csot_pp8807hb1_1_dsc_cfg,
	.modes = csot_pp8807hb1_1_modes,
	.num_modes = ARRAY_SIZE(csot_pp8807hb1_1_modes),
	.init_sequence = csot_pp8807hb1_1_init_seq,
	.is_dual_dsi = true,
	.has_dcs_backlight = false,
};

static const struct of_device_id novatek_of_match[] = {
	{ .compatible = "csot,pp8807hb1-1", .data = &csot_pp8807hb1_1_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, novatek_of_match);

static struct mipi_dsi_driver novatek_driver = {
	.probe = novatek_probe,
	.driver = {
		.name = "panel-novatek-nt36536",
		.of_match_table = novatek_of_match,
	},
};
module_mipi_dsi_driver(novatek_driver);

MODULE_AUTHOR("Pengyu Luo <mitltlatltl@gmail.com>");
MODULE_DESCRIPTION("Novatek NT36536 DriverIC panels driver");
MODULE_LICENSE("GPL");
