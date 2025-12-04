// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 XiaoMi, Inc.
 * Copyright (c) 2024 Svyatoslav Ryhel <clamor95@gmail.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

static const struct regulator_bulk_data sharp_supplies[] = {
	{ .supply = "avdd" }, { .supply = "vddio" },
	{ .supply = "vsp" }, { .supply = "vsn" },
};

struct sharp_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;

	const struct drm_display_mode *mode;
};

static inline struct sharp_panel *to_sharp_panel(struct drm_panel *panel)
{
	return container_of(panel, struct sharp_panel, panel);
}

static void sharp_panel_reset(struct sharp_panel *sharp)
{
	gpiod_set_value_cansleep(sharp->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(sharp->reset_gpio, 0);
	usleep_range(2000, 3000);
}

static int sharp_panel_prepare(struct drm_panel *panel)
{
	struct sharp_panel *sharp = to_sharp_panel(panel);
	struct device *dev = panel->dev;
	struct mipi_dsi_device *dsi0 = sharp->dsi[0];
	struct mipi_dsi_device *dsi1 = sharp->dsi[1];
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = NULL };
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(sharp_supplies), sharp->supplies);
	if (ret) {
		dev_err(dev, "error enabling regulators (%d)\n", ret);
		return ret;
	}

	msleep(24);

	if (sharp->reset_gpio)
		sharp_panel_reset(sharp);

	msleep(32);

	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, MIPI_DCS_EXIT_SLEEP_MODE);
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1,
					  MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0xff);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1,
					  MIPI_DCS_WRITE_POWER_SAVE, 0x01);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1,
					  MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x2c);

	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, MIPI_DCS_SET_DISPLAY_ON);

	return 0;
}

static int sharp_panel_unprepare(struct drm_panel *panel)
{
	struct sharp_panel *sharp = to_sharp_panel(panel);
	struct mipi_dsi_device *dsi0 = sharp->dsi[0];
	struct mipi_dsi_device *dsi1 = sharp->dsi[1];
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = NULL };

	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, MIPI_DCS_SET_DISPLAY_OFF);
	mipi_dsi_msleep(&dsi_ctx, 100);
	mipi_dsi_dual_dcs_write_seq_multi(&dsi_ctx, dsi0, dsi1, MIPI_DCS_ENTER_SLEEP_MODE);
	mipi_dsi_msleep(&dsi_ctx, 150);

	if (sharp->reset_gpio)
		gpiod_set_value_cansleep(sharp->reset_gpio, 1);

	return regulator_bulk_disable(ARRAY_SIZE(sharp_supplies), sharp->supplies);
}

static const struct drm_display_mode default_mode = {
	.clock = (1536 + 136 + 28 + 28) * (2048 + 14 + 8 + 2) * 60 / 1000,
	.hdisplay = 1536,
	.hsync_start = 1536 + 136,
	.hsync_end = 1536 + 136 + 28,
	.htotal = 1536 + 136 + 28 + 28,
	.vdisplay = 2048,
	.vsync_start = 2048 + 14,
	.vsync_end = 2048 + 14 + 8,
	.vtotal = 2048 + 14 + 8 + 2,
	.width_mm = 120,
	.height_mm = 160,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int sharp_panel_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &default_mode);
}

static const struct drm_panel_funcs sharp_panel_funcs = {
	.unprepare = sharp_panel_unprepare,
	.prepare = sharp_panel_prepare,
	.get_modes = sharp_panel_get_modes,
};

static int sharp_panel_probe(struct mipi_dsi_device *dsi)
{
	const struct mipi_dsi_device_info info = { "sharp-link1", 0, NULL };
	struct device *dev = &dsi->dev;
	struct device_node *dsi_r;
	struct mipi_dsi_host *dsi_r_host;
	struct sharp_panel *sharp;
	int i, ret;

	sharp = devm_drm_panel_alloc(dev, struct sharp_panel, panel,
				     &sharp_panel_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(sharp))
		return PTR_ERR(sharp);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(sharp_supplies),
					    sharp_supplies, &sharp->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get supplies\n");

	sharp->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sharp->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(sharp->reset_gpio),
				     "failed to get reset GPIO\n");

	/* Panel is always connected to two DSI hosts, DSI0 is left, DSI1 is right */
	dsi_r = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
	if (!dsi_r)
		return dev_err_probe(dev, -ENODEV, "failed to find second DSI host node\n");

	dsi_r_host = of_find_mipi_dsi_host_by_node(dsi_r);
	of_node_put(dsi_r);
	if (!dsi_r_host)
		return dev_err_probe(dev, -EPROBE_DEFER, "cannot get secondary DSI host\n");

	sharp->dsi[1] = devm_mipi_dsi_device_register_full(dev, dsi_r_host, &info);
	if (IS_ERR(sharp->dsi[1]))
		return dev_err_probe(dev, PTR_ERR(sharp->dsi[1]),
				     "second link registration failed\n");

	sharp->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, sharp);

	ret = drm_panel_of_backlight(&sharp->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&sharp->panel);

	for (i = 0; i < ARRAY_SIZE(sharp->dsi); i++) {
		if (!sharp->dsi[i])
			continue;

		sharp->dsi[i]->lanes = 4;
		sharp->dsi[i]->format = MIPI_DSI_FMT_RGB888;
		sharp->dsi[i]->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_LPM;

		ret = devm_mipi_dsi_attach(dev, sharp->dsi[i]);
		if (ret < 0) {
			drm_panel_remove(&sharp->panel);
			return dev_err_probe(dev, ret, "failed to attach to DSI%d\n", i);
		}
	}

	return 0;
}

static void sharp_panel_remove(struct mipi_dsi_device *dsi)
{
	struct sharp_panel *sharp = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&sharp->panel);
}

static const struct of_device_id sharp_of_match[] = {
	{ .compatible = "sharp,lq079l1sx01" },
	{ }
};
MODULE_DEVICE_TABLE(of, sharp_of_match);

static struct mipi_dsi_driver sharp_panel_driver = {
	.driver = {
		.name = "panel-sharp-lq079l1sx01",
		.of_match_table = sharp_of_match,
	},
	.probe = sharp_panel_probe,
	.remove = sharp_panel_remove,
};
module_mipi_dsi_driver(sharp_panel_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Sharp LQ079L1SX01 panel driver");
MODULE_LICENSE("GPL");
