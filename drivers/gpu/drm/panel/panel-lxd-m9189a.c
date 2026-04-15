// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree.
 * Copyright (c) 2024 Luca Weiss <luca.weiss@fairphone.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/* Manufacturer specific DSI commands */
#define EK79007AD3_GAMMA1		0x80
#define EK79007AD3_GAMMA2		0x81
#define EK79007AD3_GAMMA3		0x82
#define EK79007AD3_GAMMA4		0x83
#define EK79007AD3_GAMMA5		0x84
#define EK79007AD3_GAMMA6		0x85
#define EK79007AD3_GAMMA7		0x86
#define EK79007AD3_PANEL_CTRL3		0xB2

struct m9189_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *standby_gpio;
};

static inline struct m9189_panel *to_m9189_panel(struct drm_panel *panel)
{
	return container_of(panel, struct m9189_panel, panel);
}

static void m9189_reset(struct m9189_panel *m9189)
{
	gpiod_set_value_cansleep(m9189->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(m9189->reset_gpio, 1);
	msleep(30);
	gpiod_set_value_cansleep(m9189->reset_gpio, 0);
	msleep(55);
}

static int m9189_on(struct m9189_panel *m9189)
{
	struct mipi_dsi_multi_context ctx = { .dsi = m9189->dsi };

	ctx.dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* Gamma 2.2 */
	mipi_dsi_dcs_write_seq_multi(&ctx, EK79007AD3_GAMMA1, 0x48);
	mipi_dsi_dcs_write_seq_multi(&ctx, EK79007AD3_GAMMA2, 0xB8);
	mipi_dsi_dcs_write_seq_multi(&ctx, EK79007AD3_GAMMA3, 0x88);
	mipi_dsi_dcs_write_seq_multi(&ctx, EK79007AD3_GAMMA4, 0x88);
	mipi_dsi_dcs_write_seq_multi(&ctx, EK79007AD3_GAMMA5, 0x58);
	mipi_dsi_dcs_write_seq_multi(&ctx, EK79007AD3_GAMMA6, 0xD2);
	mipi_dsi_dcs_write_seq_multi(&ctx, EK79007AD3_GAMMA7, 0x88);
	mipi_dsi_msleep(&ctx, 50);

	/* 4 Lanes */
	mipi_dsi_generic_write_multi(&ctx, (u8[]){ EK79007AD3_PANEL_CTRL3, 0x70 }, 2);

	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	return ctx.accum_err;
}

static int m9189_disable(struct drm_panel *panel)
{
	struct m9189_panel *m9189 = to_m9189_panel(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = m9189->dsi };

	ctx.dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	gpiod_set_value_cansleep(m9189->standby_gpio, 1);

	return ctx.accum_err;
}

static int m9189_prepare(struct drm_panel *panel)
{
	struct m9189_panel *m9189 = to_m9189_panel(panel);
	struct device *dev = &m9189->dsi->dev;
	int ret;

	ret = regulator_enable(m9189->supply);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(m9189->standby_gpio, 0);
	msleep(20);
	m9189_reset(m9189);

	ret = m9189_on(m9189);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(m9189->reset_gpio, 1);
		regulator_disable(m9189->supply);
		return ret;
	}

	return 0;
}

static int m9189_unprepare(struct drm_panel *panel)
{
	struct m9189_panel *m9189 = to_m9189_panel(panel);

	gpiod_set_value_cansleep(m9189->standby_gpio, 1);
	msleep(50);

	gpiod_set_value_cansleep(m9189->reset_gpio, 1);
	regulator_disable(m9189->supply);

	return 0;
}

static const struct drm_display_mode m9189_mode = {
	.clock = (1024 + 160 + 160 + 10) * (600 + 12 + 23 + 1) * 60 / 1000,
	.hdisplay = 1024,
	.hsync_start = 1024 + 160,
	.hsync_end = 1024 + 160 + 160,
	.htotal = 1024 + 160 + 160 + 10,
	.vdisplay = 600,
	.vsync_start = 600 + 12,
	.vsync_end = 600 + 12 + 23,
	.vtotal = 600 + 12 + 23 + 1,
	.width_mm = 154,
	.height_mm = 86,
};

static int m9189_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &m9189_mode);
}

static const struct drm_panel_funcs m9189_panel_funcs = {
	.prepare = m9189_prepare,
	.unprepare = m9189_unprepare,
	.disable = m9189_disable,
	.get_modes = m9189_get_modes,
};

static int lxd_m9189_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct m9189_panel *m9189;
	int ret;

	m9189 = devm_kzalloc(dev, sizeof(*m9189), GFP_KERNEL);
	if (!m9189)
		return -ENOMEM;

	m9189->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(m9189->supply))
		return dev_err_probe(dev, PTR_ERR(m9189->supply),
				     "Failed to get power-supply\n");

	m9189->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(m9189->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(m9189->reset_gpio),
				     "Failed to get reset-gpios\n");

	m9189->standby_gpio = devm_gpiod_get(dev, "standby", GPIOD_OUT_LOW);
	if (IS_ERR(m9189->standby_gpio))
		return dev_err_probe(dev, PTR_ERR(m9189->standby_gpio),
				     "Failed to get standby-gpios\n");

	m9189->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, m9189);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST;

	drm_panel_init(&m9189->panel, dev, &m9189_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	m9189->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&m9189->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&m9189->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
		drm_panel_remove(&m9189->panel);
		return ret;
	}

	return 0;
}

static void lxd_m9189_remove(struct mipi_dsi_device *dsi)
{
	struct m9189_panel *m9189 = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&m9189->panel);
}

static const struct of_device_id lxd_m9189_of_match[] = {
	{ .compatible = "lxd,m9189a" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lxd_m9189_of_match);

static struct mipi_dsi_driver lxd_m9189_driver = {
	.probe = lxd_m9189_probe,
	.remove = lxd_m9189_remove,
	.driver = {
		.name = "panel-lxd-m9189a",
		.of_match_table = lxd_m9189_of_match,
	},
};
module_mipi_dsi_driver(lxd_m9189_driver);

MODULE_DESCRIPTION("DRM driver for LXD M9189A MIPI-DSI panels");
MODULE_LICENSE("GPL");
