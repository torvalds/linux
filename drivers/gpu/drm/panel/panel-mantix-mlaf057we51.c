// SPDX-License-Identifier: GPL-2.0
/*
 * Mantix MLAF057WE51 5.7" MIPI-DSI panel driver
 *
 * Copyright (C) Purism SPC 2020
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DRV_NAME "panel-mantix-mlaf057we51"

/* Manufacturer specific Commands send via DSI */
#define MANTIX_CMD_OTP_STOP_RELOAD_MIPI 0x41
#define MANTIX_CMD_INT_CANCEL           0x4c
#define MANTIX_CMD_SPI_FINISH           0x90

struct mantix {
	struct device *dev;
	struct drm_panel panel;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *tp_rstn_gpio;

	struct regulator *avdd;
	struct regulator *avee;
	struct regulator *vddi;

	const struct drm_display_mode *default_mode;
};

static inline struct mantix *panel_to_mantix(struct drm_panel *panel)
{
	return container_of(panel, struct mantix, panel);
}

static void mantix_init_sequence(struct mipi_dsi_multi_context *dsi_ctx)
{
	/*
	 * Init sequence was supplied by the panel vendor.
	 */
	mipi_dsi_generic_write_seq_multi(dsi_ctx, MANTIX_CMD_OTP_STOP_RELOAD_MIPI, 0x5a);

	mipi_dsi_generic_write_seq_multi(dsi_ctx, MANTIX_CMD_INT_CANCEL, 0x03);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, MANTIX_CMD_OTP_STOP_RELOAD_MIPI, 0x5a, 0x03);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x80, 0xa9, 0x00);

	mipi_dsi_generic_write_seq_multi(dsi_ctx, MANTIX_CMD_OTP_STOP_RELOAD_MIPI, 0x5a, 0x09);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, 0x80, 0x64, 0x00, 0x64, 0x00, 0x00);
	mipi_dsi_msleep(dsi_ctx, 20);

	mipi_dsi_generic_write_seq_multi(dsi_ctx, MANTIX_CMD_SPI_FINISH, 0xa5);
	mipi_dsi_generic_write_seq_multi(dsi_ctx, MANTIX_CMD_OTP_STOP_RELOAD_MIPI, 0x00, 0x2f);
	mipi_dsi_msleep(dsi_ctx, 20);
}

static int mantix_enable(struct drm_panel *panel)
{
	struct mantix *ctx = panel_to_mantix(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mantix_init_sequence(&dsi_ctx);
	if (!dsi_ctx.accum_err)
		dev_dbg(ctx->dev, "Panel init sequence done\n");

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 12000);

	mipi_dsi_turn_on_peripheral_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int mantix_disable(struct drm_panel *panel)
{
	struct mantix *ctx = panel_to_mantix(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int mantix_unprepare(struct drm_panel *panel)
{
	struct mantix *ctx = panel_to_mantix(panel);

	gpiod_set_value_cansleep(ctx->tp_rstn_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_disable(ctx->avee);
	regulator_disable(ctx->avdd);
	/* T11 */
	usleep_range(5000, 6000);
	regulator_disable(ctx->vddi);
	/* T14 */
	msleep(50);

	return 0;
}

static int mantix_prepare(struct drm_panel *panel)
{
	struct mantix *ctx = panel_to_mantix(panel);
	int ret;

	/* Focaltech FT8006P, section 7.3.1 and 7.3.4 */
	dev_dbg(ctx->dev, "Resetting the panel\n");
	ret = regulator_enable(ctx->vddi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable vddi supply: %d\n", ret);
		return ret;
	}

	/* T1 + T2 */
	usleep_range(8000, 10000);

	ret = regulator_enable(ctx->avdd);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable avdd supply: %d\n", ret);
		return ret;
	}

	/* T2d */
	usleep_range(3500, 4000);
	ret = regulator_enable(ctx->avee);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable avee supply: %d\n", ret);
		return ret;
	}

	/* T3 + T4 + time for voltage to become stable: */
	usleep_range(6000, 7000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	gpiod_set_value_cansleep(ctx->tp_rstn_gpio, 0);

	/* T6 */
	msleep(50);

	return 0;
}

static const struct drm_display_mode default_mode_mantix = {
	.hdisplay    = 720,
	.hsync_start = 720 + 45,
	.hsync_end   = 720 + 45 + 14,
	.htotal	     = 720 + 45 + 14 + 25,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 130,
	.vsync_end   = 1440 + 130 + 8,
	.vtotal	     = 1440 + 130 + 8 + 106,
	.clock	     = 85298,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 65,
	.height_mm   = 130,
};

static const struct drm_display_mode default_mode_ys = {
	.hdisplay    = 720,
	.hsync_start = 720 + 45,
	.hsync_end   = 720 + 45 + 14,
	.htotal	     = 720 + 45 + 14 + 25,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 175,
	.vsync_end   = 1440 + 175 + 8,
	.vtotal	     = 1440 + 175 + 8 + 50,
	.clock	     = 85298,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 65,
	.height_mm   = 130,
};

static const u32 mantix_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
};

static int mantix_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct mantix *ctx = panel_to_mantix(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->default_mode);
	if (!mode) {
		dev_err(ctx->dev, "Failed to add mode %ux%u@%u\n",
			ctx->default_mode->hdisplay, ctx->default_mode->vdisplay,
			drm_mode_vrefresh(ctx->default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	drm_display_info_set_bus_formats(&connector->display_info,
					 mantix_bus_formats,
					 ARRAY_SIZE(mantix_bus_formats));

	return 1;
}

static const struct drm_panel_funcs mantix_drm_funcs = {
	.disable   = mantix_disable,
	.unprepare = mantix_unprepare,
	.prepare   = mantix_prepare,
	.enable	   = mantix_enable,
	.get_modes = mantix_get_modes,
};

static int mantix_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct mantix *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->default_mode = of_device_get_match_data(dev);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->tp_rstn_gpio = devm_gpiod_get(dev, "mantix,tp-rstn", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->tp_rstn_gpio)) {
		dev_err(dev, "cannot get tp-rstn gpio\n");
		return PTR_ERR(ctx->tp_rstn_gpio);
	}

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
		MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	ctx->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ctx->avdd))
		return dev_err_probe(dev, PTR_ERR(ctx->avdd), "Failed to request avdd regulator\n");

	ctx->avee = devm_regulator_get(dev, "avee");
	if (IS_ERR(ctx->avee))
		return dev_err_probe(dev, PTR_ERR(ctx->avee), "Failed to request avee regulator\n");

	ctx->vddi = devm_regulator_get(dev, "vddi");
	if (IS_ERR(ctx->vddi))
		return dev_err_probe(dev, PTR_ERR(ctx->vddi), "Failed to request vddi regulator\n");

	drm_panel_init(&ctx->panel, dev, &mantix_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed (%d). Is host ready?\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "%ux%u@%u %ubpp dsi %udl - ready\n",
		 ctx->default_mode->hdisplay, ctx->default_mode->vdisplay,
		 drm_mode_vrefresh(ctx->default_mode),
		 mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes);

	return 0;
}

static void mantix_shutdown(struct mipi_dsi_device *dsi)
{
	struct mantix *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_unprepare(&ctx->panel);
	drm_panel_disable(&ctx->panel);
}

static void mantix_remove(struct mipi_dsi_device *dsi)
{
	struct mantix *ctx = mipi_dsi_get_drvdata(dsi);

	mantix_shutdown(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id mantix_of_match[] = {
	{ .compatible = "mantix,mlaf057we51-x", .data = &default_mode_mantix },
	{ .compatible = "ys,ys57pss36bh5gq", .data = &default_mode_ys },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mantix_of_match);

static struct mipi_dsi_driver mantix_driver = {
	.probe	= mantix_probe,
	.remove = mantix_remove,
	.shutdown = mantix_shutdown,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = mantix_of_match,
	},
};
module_mipi_dsi_driver(mantix_driver);

MODULE_AUTHOR("Guido Günther <agx@sigxcpu.org>");
MODULE_DESCRIPTION("DRM driver for Mantix MLAF057WE51-X MIPI DSI panel");
MODULE_LICENSE("GPL v2");
