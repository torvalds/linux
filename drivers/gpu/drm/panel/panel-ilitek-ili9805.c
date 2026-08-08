// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 BSH Hausgerate GmbH
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

#define ILI9805_EXTCMD_CMD_SET_ENABLE_REG	(0xff)
#define ILI9805_SETEXTC_PARAMETER1		(0xff)
#define ILI9805_SETEXTC_PARAMETER2		(0x98)
#define ILI9805_SETEXTC_PARAMETER3		(0x05)

struct ili9805_desc {
	void (*init)(struct mipi_dsi_multi_context *ctx);
	const struct drm_display_mode *mode;
	u32 width_mm;
	u32 height_mm;
};

struct ili9805 {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;
	const struct ili9805_desc	*desc;

	struct regulator	*dvdd;
	struct regulator	*avdd;
	struct gpio_desc	*reset_gpio;
};

static void gpm1780a0_init(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, ILI9805_EXTCMD_CMD_SET_ENABLE_REG,
				     ILI9805_SETEXTC_PARAMETER1, ILI9805_SETEXTC_PARAMETER2,
				     ILI9805_SETEXTC_PARAMETER3);
	mipi_dsi_msleep(ctx, 100);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfd, 0x0f, 0x10, 0x44, 0x00);
	mipi_dsi_msleep(ctx, 100);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf8, 0x18, 0x02, 0x02, 0x18, 0x02, 0x02, 0x30, 0x00,
				     0x00, 0x30, 0x00, 0x00, 0x30, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb8, 0x62);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf1, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf2, 0x00, 0x58, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf3, 0x60, 0x83, 0x04);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0x04, 0x0f, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xeb, 0x08, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe0, 0x00, 0x08, 0x0d, 0x0e, 0x0e, 0x0d, 0x0a, 0x08,
				     0x04, 0x08, 0x0d, 0x0f, 0x0b, 0x1c, 0x14, 0x0a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe1, 0x00, 0x08, 0x0d, 0x0e, 0x0e, 0x0d, 0x0a, 0x08,
				     0x04, 0x08, 0x0d, 0x0f, 0x0b, 0x1c, 0x14, 0x0a);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc1, 0x13, 0x39, 0x19, 0x06);
	mipi_dsi_msleep(ctx, 10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc7, 0xe5);
	mipi_dsi_msleep(ctx, 10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb1, 0x00, 0x12, 0x14);
	mipi_dsi_msleep(ctx, 10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb4, 0x02);
	mipi_dsi_msleep(ctx, 10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbb, 0x14, 0x55);
	mipi_dsi_dcs_write_seq_multi(ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x08);
	mipi_dsi_dcs_set_pixel_format_multi(ctx, 0x77);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x20);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb6, 0x31, 0x00, 0xef);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xdf, 0x23);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb9, 0x02, 0x00);
}

static void tm041xdhg01_init(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_dcs_write_seq_multi(ctx, ILI9805_EXTCMD_CMD_SET_ENABLE_REG,
				     ILI9805_SETEXTC_PARAMETER1, ILI9805_SETEXTC_PARAMETER2,
				     ILI9805_SETEXTC_PARAMETER3);
	mipi_dsi_msleep(ctx, 100);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfd, 0x0f, 0x13, 0x44, 0x00);
	mipi_dsi_msleep(ctx, 100);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf8, 0x18, 0x02, 0x02, 0x18, 0x02, 0x02, 0x30, 0x01,
				     0x01, 0x30, 0x01, 0x01, 0x30, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb8, 0x74);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf1, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xf2, 0x00, 0x58, 0x40);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xfc, 0x04, 0x0f, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xeb, 0x08, 0x0f);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe0, 0x01, 0x0d, 0x15, 0x0e, 0x0f, 0x0f, 0x0b, 0x08,
				     0x04, 0x07, 0x0a, 0x0d, 0x0c, 0x15, 0x0f, 0x08);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xe1, 0x01, 0x0d, 0x15, 0x0e, 0x0f, 0x0f, 0x0b, 0x08,
				     0x04, 0x07, 0x0a, 0x0d, 0x0c, 0x15, 0x0f, 0x08);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc1, 0x15, 0x03, 0x03, 0x31);
	mipi_dsi_msleep(ctx, 10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb1, 0x00, 0x12, 0x14);
	mipi_dsi_msleep(ctx, 10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb4, 0x02);
	mipi_dsi_msleep(ctx, 10);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xbb, 0x14, 0x55);
	mipi_dsi_dcs_write_seq_multi(ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x0a);
	mipi_dsi_dcs_set_pixel_format_multi(ctx, 0x77);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x20);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb0, 0x00);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xb6, 0x01);
	mipi_dsi_dcs_write_seq_multi(ctx, 0xc2, 0x11);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x51, 0xff);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x53, 0x24);
	mipi_dsi_dcs_write_seq_multi(ctx, 0x55, 0x00);
}

static inline struct ili9805 *panel_to_ili9805(struct drm_panel *panel)
{
	return container_of(panel, struct ili9805, panel);
}

static int ili9805_power_on(struct ili9805 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = regulator_enable(ctx->avdd);
	if (ret) {
		dev_err(dev, "Failed to enable avdd regulator (%d)\n", ret);
		return ret;
	}

	ret = regulator_enable(ctx->dvdd);
	if (ret) {
		dev_err(dev, "Failed to enable dvdd regulator (%d)\n", ret);
		regulator_disable(ctx->avdd);
		return ret;
	}

	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5000, 10000);
	gpiod_set_value(ctx->reset_gpio, 1);
	msleep(120);

	return 0;
}

static void ili9805_power_off(struct ili9805 *ctx)
{
	gpiod_set_value(ctx->reset_gpio, 0);
	regulator_disable(ctx->dvdd);
	regulator_disable(ctx->avdd);
}

static int ili9805_activate(struct ili9805 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->desc->init(&dsi_ctx);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int ili9805_prepare(struct drm_panel *panel)
{
	struct ili9805 *ctx = panel_to_ili9805(panel);
	int ret;

	ret = ili9805_power_on(ctx);
	if (ret)
		return ret;

	ret = ili9805_activate(ctx);
	if (ret) {
		ili9805_power_off(ctx);
		return ret;
	}

	return 0;
}

static void ili9805_deactivate(struct ili9805 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 10000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
}

static int ili9805_unprepare(struct drm_panel *panel)
{
	struct ili9805 *ctx = panel_to_ili9805(panel);

	ili9805_deactivate(ctx);
	ili9805_power_off(ctx);

	return 0;
}

static const struct drm_display_mode gpm1780a0_timing = {
	.clock = 26227,

	.hdisplay = 480,
	.hsync_start = 480 + 10,
	.hsync_end = 480 + 10 + 2,
	.htotal = 480 + 10 + 2 + 36,

	.vdisplay = 480,
	.vsync_start = 480 + 2,
	.vsync_end = 480 + 10 + 4,
	.vtotal = 480 + 2 + 4 + 10,
};

static const struct drm_display_mode tm041xdhg01_timing = {
	.clock = 26227,

	.hdisplay = 480,
	.hsync_start = 480 + 10,
	.hsync_end = 480 + 10 + 2,
	.htotal = 480 + 10 + 2 + 36,

	.vdisplay = 768,
	.vsync_start = 768 + 2,
	.vsync_end = 768 + 10 + 4,
	.vtotal = 768 + 2 + 4 + 10,
};

static int ili9805_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct ili9805 *ctx = panel_to_ili9805(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@%u\n",
			ctx->desc->mode->hdisplay,
			ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs ili9805_funcs = {
	.prepare	= ili9805_prepare,
	.unprepare	= ili9805_unprepare,
	.get_modes	= ili9805_get_modes,
};

static int ili9805_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct ili9805 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(&dsi->dev, struct ili9805, panel,
				   &ili9805_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;
	ctx->desc = of_device_get_match_data(&dsi->dev);

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
		MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM |
		MIPI_DSI_MODE_VIDEO_SYNC_PULSE | MIPI_DSI_MODE_NO_EOT_PACKET;
	dsi->lanes = 2;

	ctx->dvdd = devm_regulator_get(&dsi->dev, "dvdd");
	if (IS_ERR(ctx->dvdd))
		return PTR_ERR(ctx->dvdd);
	ctx->avdd = devm_regulator_get(&dsi->dev, "avdd");
	if (IS_ERR(ctx->avdd))
		return PTR_ERR(ctx->avdd);

	ctx->reset_gpio = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->panel.prepare_prev_first = true;
	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(&dsi->dev, "mipi_dsi_attach failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void ili9805_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct ili9805 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n",
			ret);

	drm_panel_remove(&ctx->panel);
}

static const struct ili9805_desc gpm1780a0_desc = {
	.init = gpm1780a0_init,
	.mode = &gpm1780a0_timing,
	.width_mm = 65,
	.height_mm = 65,
};

static const struct ili9805_desc tm041xdhg01_desc = {
	.init = tm041xdhg01_init,
	.mode = &tm041xdhg01_timing,
	.width_mm = 42,
	.height_mm = 96,
};

static const struct of_device_id ili9805_of_match[] = {
	{ .compatible = "giantplus,gpm1790a0", .data = &gpm1780a0_desc },
	{ .compatible = "tianma,tm041xdhg01", .data = &tm041xdhg01_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9805_of_match);

static struct mipi_dsi_driver ili9805_dsi_driver = {
	.probe		= ili9805_dsi_probe,
	.remove		= ili9805_dsi_remove,
	.driver = {
		.name		= "ili9805-dsi",
		.of_match_table	= ili9805_of_match,
	},
};
module_mipi_dsi_driver(ili9805_dsi_driver);

MODULE_AUTHOR("Matthias Proske <Matthias.Proske@bshg.com>");
MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_DESCRIPTION("Ilitek ILI9805 Controller Driver");
MODULE_LICENSE("GPL");
