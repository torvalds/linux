// SPDX-License-Identifier: GPL-2.0-only

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

static const struct regulator_bulk_data lg_ld070wx3_supplies[] = {
	{ .supply = "vdd" }, { .supply = "vcc" },
};

struct lg_ld070wx3 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct regulator_bulk_data *supplies;
};

static inline struct lg_ld070wx3 *to_lg_ld070wx3(struct drm_panel *panel)
{
	return container_of(panel, struct lg_ld070wx3, panel);
}

static int lg_ld070wx3_prepare(struct drm_panel *panel)
{
	struct lg_ld070wx3 *priv = to_lg_ld070wx3(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };
	struct device *dev = panel->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(lg_ld070wx3_supplies), priv->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to enable power supplies: %d\n", ret);
		return ret;
	}

	/*
	 * According to spec delay between enabling supply is 0,
	 * for regulators to reach required voltage ~5ms needed.
	 * MIPI interface signal for setup requires additional
	 * 110ms which in total results in 115ms.
	 */
	mdelay(115);

	mipi_dsi_dcs_soft_reset_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	/* Differential input impedance selection */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xae, 0x0b);

	/* Enter test mode 1 and 2*/
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xee, 0xea);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xef, 0x5f);

	/* Increased MIPI CLK driving ability */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xf2, 0x68);

	/* Exit test mode 1 and 2 */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xee, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0xef, 0x00);

	return ctx.accum_err;
}

static int lg_ld070wx3_unprepare(struct drm_panel *panel)
{
	struct lg_ld070wx3 *priv = to_lg_ld070wx3(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };

	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);

	msleep(50);

	regulator_bulk_disable(ARRAY_SIZE(lg_ld070wx3_supplies), priv->supplies);

	/* power supply must be off for at least 1s after panel disable */
	msleep(1000);

	return 0;
}

static const struct drm_display_mode lg_ld070wx3_mode = {
	.clock = (800 + 32 + 48 + 8) * (1280 + 5 + 3 + 1) * 60 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 32,
	.hsync_end = 800 + 32 + 48,
	.htotal = 800 + 32 + 48 + 8,
	.vdisplay = 1280,
	.vsync_start = 1280 + 5,
	.vsync_end = 1280 + 5 + 3,
	.vtotal = 1280 + 5 + 3 + 1,
	.width_mm = 94,
	.height_mm = 151,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int lg_ld070wx3_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &lg_ld070wx3_mode);
}

static const struct drm_panel_funcs lg_ld070wx3_panel_funcs = {
	.prepare = lg_ld070wx3_prepare,
	.unprepare = lg_ld070wx3_unprepare,
	.get_modes = lg_ld070wx3_get_modes,
};

static int lg_ld070wx3_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lg_ld070wx3 *priv;
	int ret;

	priv = devm_drm_panel_alloc(dev, struct lg_ld070wx3, panel,
				    &lg_ld070wx3_panel_funcs,
				    DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(lg_ld070wx3_supplies),
					    lg_ld070wx3_supplies, &priv->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get supplies\n");

	priv->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, priv);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM;

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

static void lg_ld070wx3_remove(struct mipi_dsi_device *dsi)
{
	struct lg_ld070wx3 *priv = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&priv->panel);
}

static const struct of_device_id lg_ld070wx3_of_match[] = {
	{ .compatible = "lg,ld070wx3-sl01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lg_ld070wx3_of_match);

static struct mipi_dsi_driver lg_ld070wx3_driver = {
	.driver = {
		.name = "panel-lg-ld070wx3",
		.of_match_table = lg_ld070wx3_of_match,
	},
	.probe = lg_ld070wx3_probe,
	.remove = lg_ld070wx3_remove,
};
module_mipi_dsi_driver(lg_ld070wx3_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("LG LD070WX3-SL01 DSI panel driver");
MODULE_LICENSE("GPL");
