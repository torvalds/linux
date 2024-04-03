// SPDX-License-Identifier: GPL-2.0-only

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct tm5p5_nt35596 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
};

static inline struct tm5p5_nt35596 *to_tm5p5_nt35596(struct drm_panel *panel)
{
	return container_of(panel, struct tm5p5_nt35596, panel);
}

static void tm5p5_nt35596_reset(struct tm5p5_nt35596 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(15000, 16000);
}

static int tm5p5_nt35596_on(struct tm5p5_nt35596 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	mipi_dsi_generic_write_seq(dsi, 0xff, 0x05);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xc5, 0x31);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x04);
	mipi_dsi_generic_write_seq(dsi, 0x01, 0x84);
	mipi_dsi_generic_write_seq(dsi, 0x05, 0x25);
	mipi_dsi_generic_write_seq(dsi, 0x06, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0x07, 0x20);
	mipi_dsi_generic_write_seq(dsi, 0x08, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0x09, 0x08);
	mipi_dsi_generic_write_seq(dsi, 0x0a, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0x0b, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0x0c, 0x10);
	mipi_dsi_generic_write_seq(dsi, 0x0d, 0x14);
	mipi_dsi_generic_write_seq(dsi, 0x0e, 0x14);
	mipi_dsi_generic_write_seq(dsi, 0x0f, 0x14);
	mipi_dsi_generic_write_seq(dsi, 0x10, 0x14);
	mipi_dsi_generic_write_seq(dsi, 0x11, 0x14);
	mipi_dsi_generic_write_seq(dsi, 0x12, 0x14);
	mipi_dsi_generic_write_seq(dsi, 0x17, 0xf3);
	mipi_dsi_generic_write_seq(dsi, 0x18, 0xc0);
	mipi_dsi_generic_write_seq(dsi, 0x19, 0xc0);
	mipi_dsi_generic_write_seq(dsi, 0x1a, 0xc0);
	mipi_dsi_generic_write_seq(dsi, 0x1b, 0xb3);
	mipi_dsi_generic_write_seq(dsi, 0x1c, 0xb3);
	mipi_dsi_generic_write_seq(dsi, 0x1d, 0xb3);
	mipi_dsi_generic_write_seq(dsi, 0x1e, 0xb3);
	mipi_dsi_generic_write_seq(dsi, 0x1f, 0xb3);
	mipi_dsi_generic_write_seq(dsi, 0x20, 0xb3);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xff, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xfb, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0x35, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xd3, 0x06);
	mipi_dsi_generic_write_seq(dsi, 0xd4, 0x04);
	mipi_dsi_generic_write_seq(dsi, 0x5e, 0x0d);
	mipi_dsi_generic_write_seq(dsi, 0x11, 0x00);
	msleep(100);
	mipi_dsi_generic_write_seq(dsi, 0x29, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0x53, 0x24);

	return 0;
}

static int tm5p5_nt35596_off(struct tm5p5_nt35596 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(60);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x4f, 0x01);

	return 0;
}

static int tm5p5_nt35596_prepare(struct drm_panel *panel)
{
	struct tm5p5_nt35596 *ctx = to_tm5p5_nt35596(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	tm5p5_nt35596_reset(ctx);

	ret = tm5p5_nt35596_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies),
				       ctx->supplies);
		return ret;
	}

	return 0;
}

static int tm5p5_nt35596_unprepare(struct drm_panel *panel)
{
	struct tm5p5_nt35596 *ctx = to_tm5p5_nt35596(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = tm5p5_nt35596_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies),
			       ctx->supplies);

	return 0;
}

static const struct drm_display_mode tm5p5_nt35596_mode = {
	.clock = (1080 + 100 + 8 + 16) * (1920 + 4 + 2 + 4) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 100,
	.hsync_end = 1080 + 100 + 8,
	.htotal = 1080 + 100 + 8 + 16,
	.vdisplay = 1920,
	.vsync_start = 1920 + 4,
	.vsync_end = 1920 + 4 + 2,
	.vtotal = 1920 + 4 + 2 + 4,
	.width_mm = 68,
	.height_mm = 121,
};

static int tm5p5_nt35596_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &tm5p5_nt35596_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs tm5p5_nt35596_panel_funcs = {
	.prepare = tm5p5_nt35596_prepare,
	.unprepare = tm5p5_nt35596_unprepare,
	.get_modes = tm5p5_nt35596_get_modes,
};

static int tm5p5_nt35596_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int tm5p5_nt35596_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = bl->props.brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static const struct backlight_ops tm5p5_nt35596_bl_ops = {
	.update_status = tm5p5_nt35596_bl_update_status,
	.get_brightness = tm5p5_nt35596_bl_get_brightness,
};

static struct backlight_device *
tm5p5_nt35596_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &tm5p5_nt35596_bl_ops, &props);
}

static int tm5p5_nt35596_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct tm5p5_nt35596 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vdd";
	ctx->supplies[1].supply = "vddio";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		ret = PTR_ERR(ctx->reset_gpio);
		dev_err(dev, "Failed to get reset-gpios: %d\n", ret);
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &tm5p5_nt35596_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = tm5p5_nt35596_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight)) {
		ret = PTR_ERR(ctx->panel.backlight);
		dev_err(dev, "Failed to create backlight: %d\n", ret);
		return ret;
	}

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static void tm5p5_nt35596_remove(struct mipi_dsi_device *dsi)
{
	struct tm5p5_nt35596 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev,
			"Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id tm5p5_nt35596_of_match[] = {
	{ .compatible = "asus,z00t-tm5p5-n35596" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tm5p5_nt35596_of_match);

static struct mipi_dsi_driver tm5p5_nt35596_driver = {
	.probe = tm5p5_nt35596_probe,
	.remove = tm5p5_nt35596_remove,
	.driver = {
		.name = "panel-tm5p5-nt35596",
		.of_match_table = tm5p5_nt35596_of_match,
	},
};
module_mipi_dsi_driver(tm5p5_nt35596_driver);

MODULE_AUTHOR("Konrad Dybcio <konradybcio@gmail.com>");
MODULE_DESCRIPTION("DRM driver for tm5p5 nt35596 1080p video mode dsi panel");
MODULE_LICENSE("GPL v2");
