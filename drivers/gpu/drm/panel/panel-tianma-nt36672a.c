// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct nt36672a_tianmaplus {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline
struct nt36672a_tianmaplus *to_nt36672a_tianmaplus(struct drm_panel *panel)
{
	return container_of(panel, struct nt36672a_tianmaplus, panel);
}

static void nt36672a_tianmaplus_reset(struct nt36672a_tianmaplus *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 16000);
}

static int nt36672a_tianmaplus_on(struct nt36672a_tianmaplus *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x96);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0xe4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_READ_PPS_CONTINUE,
				     0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaa, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xae, 0x8a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x01);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x03, 0x04);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00b8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x00);

	return dsi_ctx.accum_err;
}

static int nt36672a_tianmaplus_off(struct nt36672a_tianmaplus *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int nt36672a_tianmaplus_prepare(struct drm_panel *panel)
{
	struct nt36672a_tianmaplus *ctx = to_nt36672a_tianmaplus(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	nt36672a_tianmaplus_reset(ctx);

	ret = nt36672a_tianmaplus_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int nt36672a_tianmaplus_unprepare(struct drm_panel *panel)
{
	struct nt36672a_tianmaplus *ctx = to_nt36672a_tianmaplus(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = nt36672a_tianmaplus_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode nt36672a_tianmaplus_mode = {
	.clock = (1080 + 90 + 2 + 120) * (2340 + 10 + 3 + 8) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 90,
	.hsync_end = 1080 + 90 + 2,
	.htotal = 1080 + 90 + 2 + 120,
	.vdisplay = 2340,
	.vsync_start = 2340 + 10,
	.vsync_end = 2340 + 10 + 3,
	.vtotal = 2340 + 10 + 3 + 8,
	.width_mm = 67,
	.height_mm = 145,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int nt36672a_tianmaplus_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &nt36672a_tianmaplus_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	return 1;
}


static const struct drm_panel_funcs nt36672a_tianmaplus_panel_funcs = {
	.prepare = nt36672a_tianmaplus_prepare,
	.unprepare = nt36672a_tianmaplus_unprepare,
	.get_modes = nt36672a_tianmaplus_get_modes,
};

static int nt36672a_tianmaplus_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt36672a_tianmaplus *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct nt36672a_tianmaplus, panel,
				   &nt36672a_tianmaplus_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

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

static void nt36672a_tianmaplus_remove(struct mipi_dsi_device *dsi)
{
	struct nt36672a_tianmaplus *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id nt36672a_tianmaplus_of_match[] = {
	{ .compatible = "mdss,nt36672a-tianmaplus" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt36672a_tianmaplus_of_match);

static struct mipi_dsi_driver nt36672a_tianmaplus_driver = {
	.probe = nt36672a_tianmaplus_probe,
	.remove = nt36672a_tianmaplus_remove,
	.driver = {
		.name = "panel-nt36672a-tianmaplus",
		.of_match_table = nt36672a_tianmaplus_of_match,
	},
};
module_mipi_dsi_driver(nt36672a_tianmaplus_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for tianma nt36672a fhdplus video mode dsi panel");
MODULE_LICENSE("GPL");
