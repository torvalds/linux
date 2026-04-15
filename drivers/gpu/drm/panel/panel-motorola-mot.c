// SPDX-License-Identifier: GPL-2.0

#include <linux/array_size.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

static const struct regulator_bulk_data mot_panel_supplies[] = {
	{ .supply = "vddio" }, { .supply = "vdd" },
};

struct mot_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct gpio_desc *reset_gpio;

	struct regulator_bulk_data *supplies;
};

static inline struct mot_panel *to_mot_panel(struct drm_panel *panel)
{
	return container_of(panel, struct mot_panel, panel);
}

static void mot_panel_reset(struct mot_panel *priv)
{
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(50000, 51000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static void mot_es2(struct mipi_dsi_multi_context *ctx)
{
	mipi_dsi_generic_write_seq_multi(ctx, 0x55, 0x01);

	mipi_dsi_dcs_exit_sleep_mode_multi(ctx);
	mipi_dsi_msleep(ctx, 120);

	mipi_dsi_generic_write_seq_multi(ctx, 0xf4, 0x00, 0xbb, 0x46, 0x53, 0x0c, 0x49,
					 0x74, 0x29, 0x12, 0x15, 0x2f, 0x2f, 0x04);
	mipi_dsi_generic_write_seq_multi(ctx, 0xf8, 0x4b, 0x04, 0x10, 0x1a, 0x2c, 0x2c,
					 0x2c, 0x2c, 0x14, 0x12);

	mipi_dsi_generic_write_seq_multi(ctx, 0xb5, 0x03, 0x7f, 0x00, 0x80, 0xc7, 0x00);
	mipi_dsi_generic_write_seq_multi(ctx, 0xb7, 0x66, 0xf6, 0x46, 0x9f, 0x90, 0x99,
					 0xff, 0x80, 0x6d, 0x01);

	/* Gamma R */
	mipi_dsi_generic_write_seq_multi(ctx, 0xf9, 0x04);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfa, 0x00, 0x2f, 0x30, 0x12, 0x0e, 0x0c,
					 0x22, 0x27, 0x31, 0x2e, 0x07, 0x0f);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfb, 0x00, 0x2f, 0x30, 0x12, 0x0e, 0x0c,
					 0x22, 0x27, 0x31, 0x2e, 0x07, 0x0f);

	/* Gamma G */
	mipi_dsi_generic_write_seq_multi(ctx, 0xf9, 0x02);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfa, 0x00, 0x2f, 0x37, 0x15, 0x15, 0x11,
					 0x1f, 0x25, 0x2d, 0x2a, 0x05, 0x0f);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfb, 0x00, 0x2f, 0x37, 0x15, 0x15, 0x11,
					 0x1f, 0x25, 0x2d, 0x2a, 0x05, 0x0f);

	/* Gamma B */
	mipi_dsi_generic_write_seq_multi(ctx, 0xf9, 0x01);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfa, 0x00, 0x2f, 0x3f, 0x16, 0x1f, 0x15,
					 0x1f, 0x25, 0x2d, 0x2b, 0x06, 0x0b);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfb, 0x00, 0x2f, 0x3f, 0x16, 0x1f, 0x15,
					 0x1f, 0x25, 0x2d, 0x2b, 0x06, 0x0b);

	/* Gamma W */
	mipi_dsi_generic_write_seq_multi(ctx, 0xf9, 0x20);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfa, 0x00, 0x2f, 0x34, 0x15, 0x1a, 0x11,
					 0x1f, 0x23, 0x2d, 0x29, 0x02, 0x08);
	mipi_dsi_generic_write_seq_multi(ctx, 0xfb, 0x00, 0x2f, 0x34, 0x15, 0x1a, 0x11,
					 0x1f, 0x23, 0x2d, 0x29, 0x02, 0x08);

	mipi_dsi_generic_write_seq_multi(ctx, 0x53, 0x2c);
	mipi_dsi_generic_write_seq_multi(ctx, 0x35, 0x00);
}

static int mot_panel_prepare(struct drm_panel *panel)
{
	struct mot_panel *priv = to_mot_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };
	struct device *dev = panel->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(mot_panel_supplies), priv->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable power supplies: %d\n", ret);
		return ret;
	}

	mot_panel_reset(priv);

	mipi_dsi_generic_write_seq_multi(&ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&ctx, 0xf1, 0x5a, 0x5a);
	mipi_dsi_generic_write_seq_multi(&ctx, 0xd0, 0x8e);

	mot_es2(&ctx);

	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	return ctx.accum_err;
}

static int mot_panel_disable(struct drm_panel *panel)
{
	struct mot_panel *priv = to_mot_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 70);

	return ctx.accum_err;
}

static int mot_panel_unprepare(struct drm_panel *panel)
{
	struct mot_panel *priv = to_mot_panel(panel);

	usleep_range(10000, 11000);

	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(5000, 6000);

	regulator_bulk_disable(ARRAY_SIZE(mot_panel_supplies), priv->supplies);

	return 0;
}

static const struct drm_display_mode mot_panel_mode = {
	.clock = (540 + 32 + 32 + 16) * (960 + 12 + 12 + 8) * 60 / 1000,
	.hdisplay = 540,
	.hsync_start = 540 + 32,
	.hsync_end = 540 + 32 + 32,
	.htotal = 540 + 32 + 32 + 16,
	.vdisplay = 960,
	.vsync_start = 960 + 12,
	.vsync_end = 960 + 12 + 12,
	.vtotal = 960 + 12 + 12 + 8,
	.width_mm = 51,
	.height_mm = 91,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int mot_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &mot_panel_mode);
}

static const struct drm_panel_funcs mot_panel_panel_funcs = {
	.prepare = mot_panel_prepare,
	.disable = mot_panel_disable,
	.unprepare = mot_panel_unprepare,
	.get_modes = mot_panel_get_modes,
};

static int mot_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct mot_panel *priv;
	int ret;

	priv = devm_drm_panel_alloc(dev, struct mot_panel, panel,
				    &mot_panel_panel_funcs,
				    DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(mot_panel_supplies),
					    mot_panel_supplies, &priv->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get supplies\n");

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "failed to get reset gpios\n");

	priv->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, priv);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_LPM;

	ret = drm_panel_of_backlight(&priv->panel);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get backlight\n");

	drm_panel_add(&priv->panel);

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		drm_panel_remove(&priv->panel);
		return dev_err_probe(dev, ret, "failed to attach to DSI host\n");
	}

	return 0;
}

static void mot_panel_remove(struct mipi_dsi_device *dsi)
{
	struct mot_panel *priv = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&priv->panel);
}

static const struct of_device_id mot_panel_of_match[] = {
	{ .compatible = "motorola,mot-panel" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mot_panel_of_match);

static struct mipi_dsi_driver mot_panel_driver = {
	.driver = {
		.name = "panel-motorola-mot",
		.of_match_table = mot_panel_of_match,
	},
	.probe = mot_panel_probe,
	.remove = mot_panel_remove,
};
module_mipi_dsi_driver(mot_panel_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Motorola MOT panel driver");
MODULE_LICENSE("GPL");
