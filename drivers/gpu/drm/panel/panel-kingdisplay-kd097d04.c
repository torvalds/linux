// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

struct kingdisplay_panel {
	struct drm_panel base;
	struct mipi_dsi_device *link;

	struct regulator *supply;
	struct gpio_desc *enable_gpio;

	bool prepared;
	bool enabled;
};

struct kingdisplay_panel_cmd {
	char cmd;
	char data;
};

/*
 * According to the discussion on
 * https://review.coreboot.org/#/c/coreboot/+/22472/
 * the panel init array is not part of the panels datasheet but instead
 * just came in this form from the panel vendor.
 */
static const struct kingdisplay_panel_cmd init_code[] = {
	/* voltage setting */
	{ 0xB0, 0x00 },
	{ 0xB2, 0x02 },
	{ 0xB3, 0x11 },
	{ 0xB4, 0x00 },
	{ 0xB6, 0x80 },
	/* VCOM disable */
	{ 0xB7, 0x02 },
	{ 0xB8, 0x80 },
	{ 0xBA, 0x43 },
	/* VCOM setting */
	{ 0xBB, 0x53 },
	/* VSP setting */
	{ 0xBC, 0x0A },
	/* VSN setting */
	{ 0xBD, 0x4A },
	/* VGH setting */
	{ 0xBE, 0x2F },
	/* VGL setting */
	{ 0xBF, 0x1A },
	{ 0xF0, 0x39 },
	{ 0xF1, 0x22 },
	/* Gamma setting */
	{ 0xB0, 0x02 },
	{ 0xC0, 0x00 },
	{ 0xC1, 0x01 },
	{ 0xC2, 0x0B },
	{ 0xC3, 0x15 },
	{ 0xC4, 0x22 },
	{ 0xC5, 0x11 },
	{ 0xC6, 0x15 },
	{ 0xC7, 0x19 },
	{ 0xC8, 0x1A },
	{ 0xC9, 0x16 },
	{ 0xCA, 0x18 },
	{ 0xCB, 0x13 },
	{ 0xCC, 0x18 },
	{ 0xCD, 0x13 },
	{ 0xCE, 0x1C },
	{ 0xCF, 0x19 },
	{ 0xD0, 0x21 },
	{ 0xD1, 0x2C },
	{ 0xD2, 0x2F },
	{ 0xD3, 0x30 },
	{ 0xD4, 0x19 },
	{ 0xD5, 0x1F },
	{ 0xD6, 0x00 },
	{ 0xD7, 0x01 },
	{ 0xD8, 0x0B },
	{ 0xD9, 0x15 },
	{ 0xDA, 0x22 },
	{ 0xDB, 0x11 },
	{ 0xDC, 0x15 },
	{ 0xDD, 0x19 },
	{ 0xDE, 0x1A },
	{ 0xDF, 0x16 },
	{ 0xE0, 0x18 },
	{ 0xE1, 0x13 },
	{ 0xE2, 0x18 },
	{ 0xE3, 0x13 },
	{ 0xE4, 0x1C },
	{ 0xE5, 0x19 },
	{ 0xE6, 0x21 },
	{ 0xE7, 0x2C },
	{ 0xE8, 0x2F },
	{ 0xE9, 0x30 },
	{ 0xEA, 0x19 },
	{ 0xEB, 0x1F },
	/* GOA MUX setting */
	{ 0xB0, 0x01 },
	{ 0xC0, 0x10 },
	{ 0xC1, 0x0F },
	{ 0xC2, 0x0E },
	{ 0xC3, 0x0D },
	{ 0xC4, 0x0C },
	{ 0xC5, 0x0B },
	{ 0xC6, 0x0A },
	{ 0xC7, 0x09 },
	{ 0xC8, 0x08 },
	{ 0xC9, 0x07 },
	{ 0xCA, 0x06 },
	{ 0xCB, 0x05 },
	{ 0xCC, 0x00 },
	{ 0xCD, 0x01 },
	{ 0xCE, 0x02 },
	{ 0xCF, 0x03 },
	{ 0xD0, 0x04 },
	{ 0xD6, 0x10 },
	{ 0xD7, 0x0F },
	{ 0xD8, 0x0E },
	{ 0xD9, 0x0D },
	{ 0xDA, 0x0C },
	{ 0xDB, 0x0B },
	{ 0xDC, 0x0A },
	{ 0xDD, 0x09 },
	{ 0xDE, 0x08 },
	{ 0xDF, 0x07 },
	{ 0xE0, 0x06 },
	{ 0xE1, 0x05 },
	{ 0xE2, 0x00 },
	{ 0xE3, 0x01 },
	{ 0xE4, 0x02 },
	{ 0xE5, 0x03 },
	{ 0xE6, 0x04 },
	{ 0xE7, 0x00 },
	{ 0xEC, 0xC0 },
	/* GOA timing setting */
	{ 0xB0, 0x03 },
	{ 0xC0, 0x01 },
	{ 0xC2, 0x6F },
	{ 0xC3, 0x6F },
	{ 0xC5, 0x36 },
	{ 0xC8, 0x08 },
	{ 0xC9, 0x04 },
	{ 0xCA, 0x41 },
	{ 0xCC, 0x43 },
	{ 0xCF, 0x60 },
	{ 0xD2, 0x04 },
	{ 0xD3, 0x04 },
	{ 0xD4, 0x03 },
	{ 0xD5, 0x02 },
	{ 0xD6, 0x01 },
	{ 0xD7, 0x00 },
	{ 0xDB, 0x01 },
	{ 0xDE, 0x36 },
	{ 0xE6, 0x6F },
	{ 0xE7, 0x6F },
	/* GOE setting */
	{ 0xB0, 0x06 },
	{ 0xB8, 0xA5 },
	{ 0xC0, 0xA5 },
	{ 0xD5, 0x3F },
};

static inline
struct kingdisplay_panel *to_kingdisplay_panel(struct drm_panel *panel)
{
	return container_of(panel, struct kingdisplay_panel, base);
}

static int kingdisplay_panel_disable(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);
	int err;

	if (!kingdisplay->enabled)
		return 0;

	err = mipi_dsi_dcs_set_display_off(kingdisplay->link);
	if (err < 0)
		DRM_DEV_ERROR(panel->dev, "failed to set display off: %d\n",
			      err);

	kingdisplay->enabled = false;

	return 0;
}

static int kingdisplay_panel_unprepare(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);
	int err;

	if (!kingdisplay->prepared)
		return 0;

	err = mipi_dsi_dcs_enter_sleep_mode(kingdisplay->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to enter sleep mode: %d\n",
			      err);
		return err;
	}

	/* T15: 120ms */
	msleep(120);

	gpiod_set_value_cansleep(kingdisplay->enable_gpio, 0);

	err = regulator_disable(kingdisplay->supply);
	if (err < 0)
		return err;

	kingdisplay->prepared = false;

	return 0;
}

static int kingdisplay_panel_prepare(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);
	int err, regulator_err;
	unsigned int i;

	if (kingdisplay->prepared)
		return 0;

	gpiod_set_value_cansleep(kingdisplay->enable_gpio, 0);

	err = regulator_enable(kingdisplay->supply);
	if (err < 0)
		return err;

	/* T2: 15ms */
	usleep_range(15000, 16000);

	gpiod_set_value_cansleep(kingdisplay->enable_gpio, 1);

	/* T4: 15ms */
	usleep_range(15000, 16000);

	for (i = 0; i < ARRAY_SIZE(init_code); i++) {
		err = mipi_dsi_generic_write(kingdisplay->link, &init_code[i],
					sizeof(struct kingdisplay_panel_cmd));
		if (err < 0) {
			DRM_DEV_ERROR(panel->dev, "failed write init cmds: %d\n",
				      err);
			goto poweroff;
		}
	}

	err = mipi_dsi_dcs_exit_sleep_mode(kingdisplay->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to exit sleep mode: %d\n",
			      err);
		goto poweroff;
	}

	/* T6: 120ms */
	msleep(120);

	err = mipi_dsi_dcs_set_display_on(kingdisplay->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to set display on: %d\n",
			      err);
		goto poweroff;
	}

	/* T7: 10ms */
	usleep_range(10000, 11000);

	kingdisplay->prepared = true;

	return 0;

poweroff:
	gpiod_set_value_cansleep(kingdisplay->enable_gpio, 0);

	regulator_err = regulator_disable(kingdisplay->supply);
	if (regulator_err)
		DRM_DEV_ERROR(panel->dev, "failed to disable regulator: %d\n",
			      regulator_err);

	return err;
}

static int kingdisplay_panel_enable(struct drm_panel *panel)
{
	struct kingdisplay_panel *kingdisplay = to_kingdisplay_panel(panel);

	if (kingdisplay->enabled)
		return 0;

	kingdisplay->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 229000,
	.hdisplay = 1536,
	.hsync_start = 1536 + 100,
	.hsync_end = 1536 + 100 + 24,
	.htotal = 1536 + 100 + 24 + 100,
	.vdisplay = 2048,
	.vsync_start = 2048 + 95,
	.vsync_end = 2048 + 95 + 2,
	.vtotal = 2048 + 95 + 2 + 23,
	.vrefresh = 60,
};

static int kingdisplay_panel_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		DRM_DEV_ERROR(panel->dev, "failed to add mode %ux%ux@%u\n",
			      default_mode.hdisplay, default_mode.vdisplay,
			      default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 147;
	connector->display_info.height_mm = 196;
	connector->display_info.bpc = 8;

	return 1;
}

static const struct drm_panel_funcs kingdisplay_panel_funcs = {
	.disable = kingdisplay_panel_disable,
	.unprepare = kingdisplay_panel_unprepare,
	.prepare = kingdisplay_panel_prepare,
	.enable = kingdisplay_panel_enable,
	.get_modes = kingdisplay_panel_get_modes,
};

static const struct of_device_id kingdisplay_of_match[] = {
	{ .compatible = "kingdisplay,kd097d04", },
	{ }
};
MODULE_DEVICE_TABLE(of, kingdisplay_of_match);

static int kingdisplay_panel_add(struct kingdisplay_panel *kingdisplay)
{
	struct device *dev = &kingdisplay->link->dev;
	int err;

	kingdisplay->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(kingdisplay->supply))
		return PTR_ERR(kingdisplay->supply);

	kingdisplay->enable_gpio = devm_gpiod_get_optional(dev, "enable",
							   GPIOD_OUT_HIGH);
	if (IS_ERR(kingdisplay->enable_gpio)) {
		err = PTR_ERR(kingdisplay->enable_gpio);
		dev_dbg(dev, "failed to get enable gpio: %d\n", err);
		kingdisplay->enable_gpio = NULL;
	}

	drm_panel_init(&kingdisplay->base, &kingdisplay->link->dev,
		       &kingdisplay_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	err = drm_panel_of_backlight(&kingdisplay->base);
	if (err)
		return err;

	return drm_panel_add(&kingdisplay->base);
}

static void kingdisplay_panel_del(struct kingdisplay_panel *kingdisplay)
{
	drm_panel_remove(&kingdisplay->base);
}

static int kingdisplay_panel_probe(struct mipi_dsi_device *dsi)
{
	struct kingdisplay_panel *kingdisplay;
	int err;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM;

	kingdisplay = devm_kzalloc(&dsi->dev, sizeof(*kingdisplay), GFP_KERNEL);
	if (!kingdisplay)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, kingdisplay);
	kingdisplay->link = dsi;

	err = kingdisplay_panel_add(kingdisplay);
	if (err < 0)
		return err;

	return mipi_dsi_attach(dsi);
}

static int kingdisplay_panel_remove(struct mipi_dsi_device *dsi)
{
	struct kingdisplay_panel *kingdisplay = mipi_dsi_get_drvdata(dsi);
	int err;

	err = drm_panel_unprepare(&kingdisplay->base);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to unprepare panel: %d\n",
			      err);

	err = drm_panel_disable(&kingdisplay->base);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to disable panel: %d\n", err);

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to detach from DSI host: %d\n",
			      err);

	kingdisplay_panel_del(kingdisplay);

	return 0;
}

static void kingdisplay_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct kingdisplay_panel *kingdisplay = mipi_dsi_get_drvdata(dsi);

	drm_panel_unprepare(&kingdisplay->base);
	drm_panel_disable(&kingdisplay->base);
}

static struct mipi_dsi_driver kingdisplay_panel_driver = {
	.driver = {
		.name = "panel-kingdisplay-kd097d04",
		.of_match_table = kingdisplay_of_match,
	},
	.probe = kingdisplay_panel_probe,
	.remove = kingdisplay_panel_remove,
	.shutdown = kingdisplay_panel_shutdown,
};
module_mipi_dsi_driver(kingdisplay_panel_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Nickey Yang <nickey.yang@rock-chips.com>");
MODULE_DESCRIPTION("kingdisplay KD097D04 panel driver");
MODULE_LICENSE("GPL v2");
