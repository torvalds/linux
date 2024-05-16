// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics R63353 Controller driver
 *
 * Copyright (C) 2020 BSH Hausgerate GmbH
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/media-bus-format.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

#define R63353_INSTR(...) { \
		.len = sizeof((u8[]) {__VA_ARGS__}), \
		.data = (u8[]){__VA_ARGS__} \
	}

struct r63353_instr {
	size_t len;
	const u8 *data;
};

static const struct r63353_instr sharp_ls068b3sx02_init[] = {
	R63353_INSTR(0x51, 0xff),
	R63353_INSTR(0x53, 0x0c),
	R63353_INSTR(0x55, 0x00),
	R63353_INSTR(0x84, 0x00),
	R63353_INSTR(0x29),
};

struct r63353_desc {
	const char *name;
	const struct r63353_instr *init;
	const size_t init_length;
	const struct drm_display_mode *mode;
	u32 width_mm;
	u32 height_mm;
};

struct r63353_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct gpio_desc *reset_gpio;
	struct regulator *dvdd;
	struct regulator *avdd;

	struct r63353_desc *pdata;
};

static inline struct r63353_panel *to_r63353_panel(struct drm_panel *panel)
{
	return container_of(panel, struct r63353_panel, base);
}

static int r63353_panel_power_on(struct r63353_panel *rpanel)
{
	struct mipi_dsi_device *dsi = rpanel->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = regulator_enable(rpanel->avdd);
	if (ret) {
		dev_err(dev, "Failed to enable avdd regulator (%d)\n", ret);
		return ret;
	}

	usleep_range(15000, 25000);

	ret = regulator_enable(rpanel->dvdd);
	if (ret) {
		dev_err(dev, "Failed to enable dvdd regulator (%d)\n", ret);
		regulator_disable(rpanel->avdd);
		return ret;
	}

	usleep_range(300000, 350000);
	gpiod_set_value(rpanel->reset_gpio, 1);
	usleep_range(15000, 25000);

	return 0;
}

static int r63353_panel_power_off(struct r63353_panel *rpanel)
{
	gpiod_set_value(rpanel->reset_gpio, 0);
	regulator_disable(rpanel->dvdd);
	regulator_disable(rpanel->avdd);

	return 0;
}

static int r63353_panel_activate(struct r63353_panel *rpanel)
{
	struct mipi_dsi_device *dsi = rpanel->dsi;
	struct device *dev = &dsi->dev;
	int i, ret;

	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to do Software Reset (%d)\n", ret);
		goto fail;
	}

	usleep_range(15000, 17000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode (%d)\n", ret);
		goto fail;
	}

	for (i = 0; i < rpanel->pdata->init_length; i++) {
		const struct r63353_instr *instr = &rpanel->pdata->init[i];

		ret = mipi_dsi_dcs_write_buffer(dsi, instr->data, instr->len);
		if (ret < 0)
			goto fail;
	}

	msleep(120);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode (%d)\n", ret);
		goto fail;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display ON (%d)\n", ret);
		goto fail;
	}

	return 0;

fail:
	gpiod_set_value(rpanel->reset_gpio, 0);

	return ret;
}

static int r63353_panel_prepare(struct drm_panel *panel)
{
	struct r63353_panel *rpanel = to_r63353_panel(panel);
	struct mipi_dsi_device *dsi = rpanel->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dev_dbg(dev, "Preparing\n");

	ret = r63353_panel_power_on(rpanel);
	if (ret)
		return ret;

	ret = r63353_panel_activate(rpanel);
	if (ret) {
		r63353_panel_power_off(rpanel);
		return ret;
	}

	dev_dbg(dev, "Prepared\n");
	return 0;
}

static int r63353_panel_deactivate(struct r63353_panel *rpanel)
{
	struct mipi_dsi_device *dsi = rpanel->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display OFF (%d)\n", ret);
		return ret;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int r63353_panel_unprepare(struct drm_panel *panel)
{
	struct r63353_panel *rpanel = to_r63353_panel(panel);

	r63353_panel_deactivate(rpanel);
	r63353_panel_power_off(rpanel);

	return 0;
}

static const struct drm_display_mode sharp_ls068b3sx02_timing = {
	.clock = 70000,
	.hdisplay = 640,
	.hsync_start = 640 + 35,
	.hsync_end = 640 + 35 + 2,
	.htotal = 640 + 35 + 2 + 150,
	.vdisplay = 1280,
	.vsync_start = 1280 + 2,
	.vsync_end = 1280 + 2 + 4,
	.vtotal = 1280 + 2 + 4 + 0,
};

static int r63353_panel_get_modes(struct drm_panel *panel,
				  struct drm_connector *connector)
{
	struct r63353_panel *rpanel = to_r63353_panel(panel);
	struct drm_display_mode *mode;
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	mode = drm_mode_duplicate(connector->dev, rpanel->pdata->mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = rpanel->pdata->width_mm;
	connector->display_info.height_mm = rpanel->pdata->height_mm;

	drm_display_info_set_bus_formats(&connector->display_info,
					 &bus_format, 1);

	return 1;
}

static const struct drm_panel_funcs r63353_panel_funcs = {
	.prepare = r63353_panel_prepare,
	.unprepare = r63353_panel_unprepare,
	.get_modes = r63353_panel_get_modes,
};

static int r63353_panel_probe(struct mipi_dsi_device *dsi)
{
	int ret = 0;
	struct device *dev = &dsi->dev;
	struct r63353_panel *panel;

	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, panel);
	panel->dsi = dsi;
	panel->pdata = (struct r63353_desc *)of_device_get_match_data(dev);

	dev_info(dev, "Panel %s\n", panel->pdata->name);

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE | MIPI_DSI_MODE_NO_EOT_PACKET;

	panel->dvdd = devm_regulator_get(dev, "dvdd");
	if (IS_ERR(panel->dvdd))
		return PTR_ERR(panel->dvdd);
	panel->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(panel->avdd))
		return PTR_ERR(panel->avdd);

	panel->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(panel->reset_gpio)) {
		dev_err(dev, "failed to get RESET GPIO\n");
		return PTR_ERR(panel->reset_gpio);
	}

	drm_panel_init(&panel->base, dev, &r63353_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	panel->base.prepare_prev_first = true;
	ret = drm_panel_of_backlight(&panel->base);
	if (ret)
		return ret;

	drm_panel_add(&panel->base);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed: %d\n", ret);
		drm_panel_remove(&panel->base);
		return ret;
	}

	return ret;
}

static void r63353_panel_remove(struct mipi_dsi_device *dsi)
{
	struct r63353_panel *rpanel = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(dev, "Failed to detach from host (%d)\n", ret);

	drm_panel_remove(&rpanel->base);
}

static void r63353_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct r63353_panel *rpanel = mipi_dsi_get_drvdata(dsi);

	r63353_panel_unprepare(&rpanel->base);
}

static const struct r63353_desc sharp_ls068b3sx02_data = {
	.name = "Sharp LS068B3SX02",
	.mode = &sharp_ls068b3sx02_timing,
	.init = sharp_ls068b3sx02_init,
	.init_length = ARRAY_SIZE(sharp_ls068b3sx02_init),
	.width_mm = 68,
	.height_mm = 159,
};

static const struct of_device_id r63353_of_match[] = {
	{ .compatible = "sharp,ls068b3sx02", .data = &sharp_ls068b3sx02_data },
	{ }
};

MODULE_DEVICE_TABLE(of, r63353_of_match);

static struct mipi_dsi_driver r63353_panel_driver = {
	.driver = {
		   .name = "r63353-dsi",
		   .of_match_table = r63353_of_match,
	},
	.probe = r63353_panel_probe,
	.remove = r63353_panel_remove,
	.shutdown = r63353_panel_shutdown,
};

module_mipi_dsi_driver(r63353_panel_driver);

MODULE_AUTHOR("Matthias Proske <Matthias.Proske@bshg.com>");
MODULE_AUTHOR("Michael Trimarchi <michael@amarulasolutions.com>");
MODULE_DESCRIPTION("Synaptics R63353 Controller Driver");
MODULE_LICENSE("GPL");
