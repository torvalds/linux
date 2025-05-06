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

#define R61307_MACP		0xb0 /* Manufacturer CMD Protect */
#define   R61307_MACP_ON	0x03
#define   R61307_MACP_OFF	0x04

#define R61307_INVERSION	0xc1
#define R61307_GAMMA_SET_A	0xc8 /* Gamma Setting A */
#define R61307_GAMMA_SET_B	0xc9 /* Gamma Setting B */
#define R61307_GAMMA_SET_C	0xca /* Gamma Setting C */
#define R61307_CONTRAST_SET	0xcc

struct renesas_r61307 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	struct regulator *vcc_supply;
	struct regulator *iovcc_supply;

	struct gpio_desc *reset_gpio;

	bool prepared;

	bool dig_cont_adj;
	bool inversion;
	u32 gamma;
};

static const u8 gamma_setting[][25] = {
	{ /* sentinel */ },
	{
		R61307_GAMMA_SET_A,
		0x00, 0x06, 0x0a, 0x0f,
		0x14, 0x1f, 0x1f, 0x17,
		0x12, 0x0c, 0x09, 0x06,
		0x00, 0x06, 0x0a, 0x0f,
		0x14, 0x1f, 0x1f, 0x17,
		0x12, 0x0c, 0x09, 0x06
	},
	{
		R61307_GAMMA_SET_A,
		0x00, 0x05, 0x0b, 0x0f,
		0x11, 0x1d, 0x20, 0x18,
		0x18, 0x09, 0x07, 0x06,
		0x00, 0x05, 0x0b, 0x0f,
		0x11, 0x1d, 0x20, 0x18,
		0x18, 0x09, 0x07, 0x06
	},
	{
		R61307_GAMMA_SET_A,
		0x0b, 0x0d, 0x10, 0x14,
		0x13, 0x1d, 0x20, 0x18,
		0x12, 0x09, 0x07, 0x06,
		0x0a, 0x0c, 0x10, 0x14,
		0x13, 0x1d, 0x20, 0x18,
		0x12, 0x09, 0x07, 0x06
	},
};

static inline struct renesas_r61307 *to_renesas_r61307(struct drm_panel *panel)
{
	return container_of(panel, struct renesas_r61307, panel);
}

static void renesas_r61307_reset(struct renesas_r61307 *priv)
{
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(2000, 3000);
}

static int renesas_r61307_prepare(struct drm_panel *panel)
{
	struct renesas_r61307 *priv = to_renesas_r61307(panel);
	struct device *dev = &priv->dsi->dev;
	int ret;

	if (priv->prepared)
		return 0;

	ret = regulator_enable(priv->vcc_supply);
	if (ret) {
		dev_err(dev, "failed to enable vcc power supply\n");
		return ret;
	}

	usleep_range(2000, 3000);

	ret = regulator_enable(priv->iovcc_supply);
	if (ret) {
		dev_err(dev, "failed to enable iovcc power supply\n");
		return ret;
	}

	usleep_range(2000, 3000);

	renesas_r61307_reset(priv);

	priv->prepared = true;
	return 0;
}

static int renesas_r61307_enable(struct drm_panel *panel)
{
	struct renesas_r61307 *priv = to_renesas_r61307(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };

	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 80);

	mipi_dsi_dcs_write_seq_multi(&ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	mipi_dsi_msleep(&ctx, 20);

	mipi_dsi_dcs_set_pixel_format_multi(&ctx, MIPI_DCS_PIXEL_FMT_24BIT << 4);

	/* MACP Off */
	mipi_dsi_generic_write_seq_multi(&ctx, R61307_MACP, R61307_MACP_OFF);

	if (priv->dig_cont_adj)
		mipi_dsi_generic_write_seq_multi(&ctx, R61307_CONTRAST_SET,
						 0xdc, 0xb4, 0xff);

	if (priv->gamma)
		mipi_dsi_generic_write_multi(&ctx, gamma_setting[priv->gamma],
					     sizeof(gamma_setting[priv->gamma]));

	if (priv->inversion)
		mipi_dsi_generic_write_seq_multi(&ctx, R61307_INVERSION,
						 0x00, 0x50, 0x03, 0x22,
						 0x16, 0x06, 0x60, 0x11);
	else
		mipi_dsi_generic_write_seq_multi(&ctx, R61307_INVERSION,
						 0x00, 0x10, 0x03, 0x22,
						 0x16, 0x06, 0x60, 0x01);

	/* MACP On */
	mipi_dsi_generic_write_seq_multi(&ctx, R61307_MACP, R61307_MACP_ON);

	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_msleep(&ctx, 50);

	return 0;
}

static int renesas_r61307_disable(struct drm_panel *panel)
{
	struct renesas_r61307 *priv = to_renesas_r61307(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = priv->dsi };

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	mipi_dsi_msleep(&ctx, 100);
	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);

	return 0;
}

static int renesas_r61307_unprepare(struct drm_panel *panel)
{
	struct renesas_r61307 *priv = to_renesas_r61307(panel);

	if (!priv->prepared)
		return 0;

	usleep_range(10000, 11000);

	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(5000, 6000);

	regulator_disable(priv->iovcc_supply);
	usleep_range(2000, 3000);
	regulator_disable(priv->vcc_supply);

	priv->prepared = false;
	return 0;
}

static const struct drm_display_mode renesas_r61307_mode = {
	.clock = (768 + 116 + 81 + 5) * (1024 + 24 + 8 + 2) * 60 / 1000,
	.hdisplay = 768,
	.hsync_start = 768 + 116,
	.hsync_end = 768 + 116 + 81,
	.htotal = 768 + 116 + 81 + 5,
	.vdisplay = 1024,
	.vsync_start = 1024 + 24,
	.vsync_end = 1024 + 24 + 8,
	.vtotal = 1024 + 24 + 8 + 2,
	.width_mm = 76,
	.height_mm = 101,
};

static int renesas_r61307_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &renesas_r61307_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs renesas_r61307_panel_funcs = {
	.prepare = renesas_r61307_prepare,
	.enable = renesas_r61307_enable,
	.disable = renesas_r61307_disable,
	.unprepare = renesas_r61307_unprepare,
	.get_modes = renesas_r61307_get_modes,
};

static int renesas_r61307_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct renesas_r61307 *priv;
	int ret;

	priv = devm_drm_panel_alloc(dev, struct renesas_r61307, panel,
				    &renesas_r61307_panel_funcs,
				    DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	priv->vcc_supply = devm_regulator_get(dev, "vcc");
	if (IS_ERR(priv->vcc_supply))
		return dev_err_probe(dev, PTR_ERR(priv->vcc_supply),
				     "Failed to get vcc-supply\n");

	priv->iovcc_supply = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(priv->iovcc_supply))
		return dev_err_probe(dev, PTR_ERR(priv->iovcc_supply),
				     "Failed to get iovcc-supply\n");

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "Failed to get reset gpios\n");

	if (device_property_read_bool(dev, "renesas,inversion"))
		priv->inversion = true;

	if (device_property_read_bool(dev, "renesas,contrast"))
		priv->dig_cont_adj = true;

	priv->gamma = 0;
	device_property_read_u32(dev, "renesas,gamma", &priv->gamma);

	priv->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, priv);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ret = drm_panel_of_backlight(&priv->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&priv->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret) {
		drm_panel_remove(&priv->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void renesas_r61307_remove(struct mipi_dsi_device *dsi)
{
	struct renesas_r61307 *priv = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&priv->panel);
}

static const struct of_device_id renesas_r61307_of_match[] = {
	{ .compatible = "hit,tx13d100vm0eaa" },
	{ .compatible = "koe,tx13d100vm0eaa" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, renesas_r61307_of_match);

static struct mipi_dsi_driver renesas_r61307_driver = {
	.probe = renesas_r61307_probe,
	.remove = renesas_r61307_remove,
	.driver = {
		.name = "panel-renesas-r61307",
		.of_match_table = renesas_r61307_of_match,
	},
};
module_mipi_dsi_driver(renesas_r61307_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Renesas R61307-based panel driver");
MODULE_LICENSE("GPL");
