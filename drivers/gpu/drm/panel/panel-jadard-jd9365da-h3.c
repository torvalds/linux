// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Radxa Limited
 * Copyright (c) 2022 Edgeble AI Technologies Pvt. Ltd.
 *
 * Author:
 * - Jagan Teki <jagan@amarulasolutions.com>
 * - Stephen Chen <stephen@radxa.com>
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#define JD9365DA_INIT_CMD_LEN		2

struct jadard_init_cmd {
	u8 data[JD9365DA_INIT_CMD_LEN];
};

struct jadard_panel_desc {
	const struct drm_display_mode mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	const struct jadard_init_cmd *init_cmds;
	u32 num_init_cmds;
};

struct jadard {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct jadard_panel_desc *desc;

	struct regulator *vdd;
	struct regulator *vccio;
	struct gpio_desc *reset;
};

static inline struct jadard *panel_to_jadard(struct drm_panel *panel)
{
	return container_of(panel, struct jadard, panel);
}

static int jadard_enable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct jadard *jadard = panel_to_jadard(panel);
	const struct jadard_panel_desc *desc = jadard->desc;
	struct mipi_dsi_device *dsi = jadard->dsi;
	unsigned int i;
	int err;

	msleep(10);

	for (i = 0; i < desc->num_init_cmds; i++) {
		const struct jadard_init_cmd *cmd = &desc->init_cmds[i];

		err = mipi_dsi_dcs_write_buffer(dsi, cmd->data, JD9365DA_INIT_CMD_LEN);
		if (err < 0)
			return err;
	}

	msleep(120);

	err = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (err < 0)
		DRM_DEV_ERROR(dev, "failed to exit sleep mode ret = %d\n", err);

	err =  mipi_dsi_dcs_set_display_on(dsi);
	if (err < 0)
		DRM_DEV_ERROR(dev, "failed to set display on ret = %d\n", err);

	return 0;
}

static int jadard_disable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct jadard *jadard = panel_to_jadard(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(jadard->dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(jadard->dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int jadard_prepare(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);
	int ret;

	ret = regulator_enable(jadard->vccio);
	if (ret)
		return ret;

	ret = regulator_enable(jadard->vdd);
	if (ret)
		return ret;

	gpiod_set_value(jadard->reset, 1);
	msleep(5);

	gpiod_set_value(jadard->reset, 0);
	msleep(10);

	gpiod_set_value(jadard->reset, 1);
	msleep(120);

	return 0;
}

static int jadard_unprepare(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);

	gpiod_set_value(jadard->reset, 1);
	msleep(120);

	regulator_disable(jadard->vdd);
	regulator_disable(jadard->vccio);

	return 0;
}

static int jadard_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct jadard *jadard = panel_to_jadard(panel);
	const struct drm_display_mode *desc_mode = &jadard->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		DRM_DEV_ERROR(&jadard->dsi->dev, "failed to add mode %ux%ux@%u\n",
			      desc_mode->hdisplay, desc_mode->vdisplay,
			      drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs jadard_funcs = {
	.disable = jadard_disable,
	.unprepare = jadard_unprepare,
	.prepare = jadard_prepare,
	.enable = jadard_enable,
	.get_modes = jadard_get_modes,
};

static const struct jadard_init_cmd radxa_display_8hd_ad002_init_cmds[] = {
	{ .data = { 0xE0, 0x00 } },
	{ .data = { 0xE1, 0x93 } },
	{ .data = { 0xE2, 0x65 } },
	{ .data = { 0xE3, 0xF8 } },
	{ .data = { 0x80, 0x03 } },
	{ .data = { 0xE0, 0x01 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x01, 0x7E } },
	{ .data = { 0x03, 0x00 } },
	{ .data = { 0x04, 0x65 } },
	{ .data = { 0x0C, 0x74 } },
	{ .data = { 0x17, 0x00 } },
	{ .data = { 0x18, 0xB7 } },
	{ .data = { 0x19, 0x00 } },
	{ .data = { 0x1A, 0x00 } },
	{ .data = { 0x1B, 0xB7 } },
	{ .data = { 0x1C, 0x00 } },
	{ .data = { 0x24, 0xFE } },
	{ .data = { 0x37, 0x19 } },
	{ .data = { 0x38, 0x05 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x3A, 0x01 } },
	{ .data = { 0x3B, 0x01 } },
	{ .data = { 0x3C, 0x70 } },
	{ .data = { 0x3D, 0xFF } },
	{ .data = { 0x3E, 0xFF } },
	{ .data = { 0x3F, 0xFF } },
	{ .data = { 0x40, 0x06 } },
	{ .data = { 0x41, 0xA0 } },
	{ .data = { 0x43, 0x1E } },
	{ .data = { 0x44, 0x0F } },
	{ .data = { 0x45, 0x28 } },
	{ .data = { 0x4B, 0x04 } },
	{ .data = { 0x55, 0x02 } },
	{ .data = { 0x56, 0x01 } },
	{ .data = { 0x57, 0xA9 } },
	{ .data = { 0x58, 0x0A } },
	{ .data = { 0x59, 0x0A } },
	{ .data = { 0x5A, 0x37 } },
	{ .data = { 0x5B, 0x19 } },
	{ .data = { 0x5D, 0x78 } },
	{ .data = { 0x5E, 0x63 } },
	{ .data = { 0x5F, 0x54 } },
	{ .data = { 0x60, 0x49 } },
	{ .data = { 0x61, 0x45 } },
	{ .data = { 0x62, 0x38 } },
	{ .data = { 0x63, 0x3D } },
	{ .data = { 0x64, 0x28 } },
	{ .data = { 0x65, 0x43 } },
	{ .data = { 0x66, 0x41 } },
	{ .data = { 0x67, 0x43 } },
	{ .data = { 0x68, 0x62 } },
	{ .data = { 0x69, 0x50 } },
	{ .data = { 0x6A, 0x57 } },
	{ .data = { 0x6B, 0x49 } },
	{ .data = { 0x6C, 0x44 } },
	{ .data = { 0x6D, 0x37 } },
	{ .data = { 0x6E, 0x23 } },
	{ .data = { 0x6F, 0x10 } },
	{ .data = { 0x70, 0x78 } },
	{ .data = { 0x71, 0x63 } },
	{ .data = { 0x72, 0x54 } },
	{ .data = { 0x73, 0x49 } },
	{ .data = { 0x74, 0x45 } },
	{ .data = { 0x75, 0x38 } },
	{ .data = { 0x76, 0x3D } },
	{ .data = { 0x77, 0x28 } },
	{ .data = { 0x78, 0x43 } },
	{ .data = { 0x79, 0x41 } },
	{ .data = { 0x7A, 0x43 } },
	{ .data = { 0x7B, 0x62 } },
	{ .data = { 0x7C, 0x50 } },
	{ .data = { 0x7D, 0x57 } },
	{ .data = { 0x7E, 0x49 } },
	{ .data = { 0x7F, 0x44 } },
	{ .data = { 0x80, 0x37 } },
	{ .data = { 0x81, 0x23 } },
	{ .data = { 0x82, 0x10 } },
	{ .data = { 0xE0, 0x02 } },
	{ .data = { 0x00, 0x47 } },
	{ .data = { 0x01, 0x47 } },
	{ .data = { 0x02, 0x45 } },
	{ .data = { 0x03, 0x45 } },
	{ .data = { 0x04, 0x4B } },
	{ .data = { 0x05, 0x4B } },
	{ .data = { 0x06, 0x49 } },
	{ .data = { 0x07, 0x49 } },
	{ .data = { 0x08, 0x41 } },
	{ .data = { 0x09, 0x1F } },
	{ .data = { 0x0A, 0x1F } },
	{ .data = { 0x0B, 0x1F } },
	{ .data = { 0x0C, 0x1F } },
	{ .data = { 0x0D, 0x1F } },
	{ .data = { 0x0E, 0x1F } },
	{ .data = { 0x0F, 0x5F } },
	{ .data = { 0x10, 0x5F } },
	{ .data = { 0x11, 0x57 } },
	{ .data = { 0x12, 0x77 } },
	{ .data = { 0x13, 0x35 } },
	{ .data = { 0x14, 0x1F } },
	{ .data = { 0x15, 0x1F } },
	{ .data = { 0x16, 0x46 } },
	{ .data = { 0x17, 0x46 } },
	{ .data = { 0x18, 0x44 } },
	{ .data = { 0x19, 0x44 } },
	{ .data = { 0x1A, 0x4A } },
	{ .data = { 0x1B, 0x4A } },
	{ .data = { 0x1C, 0x48 } },
	{ .data = { 0x1D, 0x48 } },
	{ .data = { 0x1E, 0x40 } },
	{ .data = { 0x1F, 0x1F } },
	{ .data = { 0x20, 0x1F } },
	{ .data = { 0x21, 0x1F } },
	{ .data = { 0x22, 0x1F } },
	{ .data = { 0x23, 0x1F } },
	{ .data = { 0x24, 0x1F } },
	{ .data = { 0x25, 0x5F } },
	{ .data = { 0x26, 0x5F } },
	{ .data = { 0x27, 0x57 } },
	{ .data = { 0x28, 0x77 } },
	{ .data = { 0x29, 0x35 } },
	{ .data = { 0x2A, 0x1F } },
	{ .data = { 0x2B, 0x1F } },
	{ .data = { 0x58, 0x40 } },
	{ .data = { 0x59, 0x00 } },
	{ .data = { 0x5A, 0x00 } },
	{ .data = { 0x5B, 0x10 } },
	{ .data = { 0x5C, 0x06 } },
	{ .data = { 0x5D, 0x40 } },
	{ .data = { 0x5E, 0x01 } },
	{ .data = { 0x5F, 0x02 } },
	{ .data = { 0x60, 0x30 } },
	{ .data = { 0x61, 0x01 } },
	{ .data = { 0x62, 0x02 } },
	{ .data = { 0x63, 0x03 } },
	{ .data = { 0x64, 0x6B } },
	{ .data = { 0x65, 0x05 } },
	{ .data = { 0x66, 0x0C } },
	{ .data = { 0x67, 0x73 } },
	{ .data = { 0x68, 0x09 } },
	{ .data = { 0x69, 0x03 } },
	{ .data = { 0x6A, 0x56 } },
	{ .data = { 0x6B, 0x08 } },
	{ .data = { 0x6C, 0x00 } },
	{ .data = { 0x6D, 0x04 } },
	{ .data = { 0x6E, 0x04 } },
	{ .data = { 0x6F, 0x88 } },
	{ .data = { 0x70, 0x00 } },
	{ .data = { 0x71, 0x00 } },
	{ .data = { 0x72, 0x06 } },
	{ .data = { 0x73, 0x7B } },
	{ .data = { 0x74, 0x00 } },
	{ .data = { 0x75, 0xF8 } },
	{ .data = { 0x76, 0x00 } },
	{ .data = { 0x77, 0xD5 } },
	{ .data = { 0x78, 0x2E } },
	{ .data = { 0x79, 0x12 } },
	{ .data = { 0x7A, 0x03 } },
	{ .data = { 0x7B, 0x00 } },
	{ .data = { 0x7C, 0x00 } },
	{ .data = { 0x7D, 0x03 } },
	{ .data = { 0x7E, 0x7B } },
	{ .data = { 0xE0, 0x04 } },
	{ .data = { 0x00, 0x0E } },
	{ .data = { 0x02, 0xB3 } },
	{ .data = { 0x09, 0x60 } },
	{ .data = { 0x0E, 0x2A } },
	{ .data = { 0x36, 0x59 } },
	{ .data = { 0xE0, 0x00 } },
};

static const struct jadard_panel_desc radxa_display_8hd_ad002_desc = {
	.mode = {
		.clock		= 70000,

		.hdisplay	= 800,
		.hsync_start	= 800 + 40,
		.hsync_end	= 800 + 40 + 18,
		.htotal		= 800 + 40 + 18 + 20,

		.vdisplay	= 1280,
		.vsync_start	= 1280 + 20,
		.vsync_end	= 1280 + 20 + 4,
		.vtotal		= 1280 + 20 + 4 + 20,

		.width_mm	= 127,
		.height_mm	= 199,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.init_cmds = radxa_display_8hd_ad002_init_cmds,
	.num_init_cmds = ARRAY_SIZE(radxa_display_8hd_ad002_init_cmds),
};

static const struct jadard_init_cmd cz101b4001_init_cmds[] = {
	{ .data = { 0xE0, 0x00 } },
	{ .data = { 0xE1, 0x93 } },
	{ .data = { 0xE2, 0x65 } },
	{ .data = { 0xE3, 0xF8 } },
	{ .data = { 0x80, 0x03 } },
	{ .data = { 0xE0, 0x01 } },
	{ .data = { 0x00, 0x00 } },
	{ .data = { 0x01, 0x3B } },
	{ .data = { 0x0C, 0x74 } },
	{ .data = { 0x17, 0x00 } },
	{ .data = { 0x18, 0xAF } },
	{ .data = { 0x19, 0x00 } },
	{ .data = { 0x1A, 0x00 } },
	{ .data = { 0x1B, 0xAF } },
	{ .data = { 0x1C, 0x00 } },
	{ .data = { 0x35, 0x26 } },
	{ .data = { 0x37, 0x09 } },
	{ .data = { 0x38, 0x04 } },
	{ .data = { 0x39, 0x00 } },
	{ .data = { 0x3A, 0x01 } },
	{ .data = { 0x3C, 0x78 } },
	{ .data = { 0x3D, 0xFF } },
	{ .data = { 0x3E, 0xFF } },
	{ .data = { 0x3F, 0x7F } },
	{ .data = { 0x40, 0x06 } },
	{ .data = { 0x41, 0xA0 } },
	{ .data = { 0x42, 0x81 } },
	{ .data = { 0x43, 0x14 } },
	{ .data = { 0x44, 0x23 } },
	{ .data = { 0x45, 0x28 } },
	{ .data = { 0x55, 0x02 } },
	{ .data = { 0x57, 0x69 } },
	{ .data = { 0x59, 0x0A } },
	{ .data = { 0x5A, 0x2A } },
	{ .data = { 0x5B, 0x17 } },
	{ .data = { 0x5D, 0x7F } },
	{ .data = { 0x5E, 0x6B } },
	{ .data = { 0x5F, 0x5C } },
	{ .data = { 0x60, 0x4F } },
	{ .data = { 0x61, 0x4D } },
	{ .data = { 0x62, 0x3F } },
	{ .data = { 0x63, 0x42 } },
	{ .data = { 0x64, 0x2B } },
	{ .data = { 0x65, 0x44 } },
	{ .data = { 0x66, 0x43 } },
	{ .data = { 0x67, 0x43 } },
	{ .data = { 0x68, 0x63 } },
	{ .data = { 0x69, 0x52 } },
	{ .data = { 0x6A, 0x5A } },
	{ .data = { 0x6B, 0x4F } },
	{ .data = { 0x6C, 0x4E } },
	{ .data = { 0x6D, 0x20 } },
	{ .data = { 0x6E, 0x0F } },
	{ .data = { 0x6F, 0x00 } },
	{ .data = { 0x70, 0x7F } },
	{ .data = { 0x71, 0x6B } },
	{ .data = { 0x72, 0x5C } },
	{ .data = { 0x73, 0x4F } },
	{ .data = { 0x74, 0x4D } },
	{ .data = { 0x75, 0x3F } },
	{ .data = { 0x76, 0x42 } },
	{ .data = { 0x77, 0x2B } },
	{ .data = { 0x78, 0x44 } },
	{ .data = { 0x79, 0x43 } },
	{ .data = { 0x7A, 0x43 } },
	{ .data = { 0x7B, 0x63 } },
	{ .data = { 0x7C, 0x52 } },
	{ .data = { 0x7D, 0x5A } },
	{ .data = { 0x7E, 0x4F } },
	{ .data = { 0x7F, 0x4E } },
	{ .data = { 0x80, 0x20 } },
	{ .data = { 0x81, 0x0F } },
	{ .data = { 0x82, 0x00 } },
	{ .data = { 0xE0, 0x02 } },
	{ .data = { 0x00, 0x02 } },
	{ .data = { 0x01, 0x02 } },
	{ .data = { 0x02, 0x00 } },
	{ .data = { 0x03, 0x00 } },
	{ .data = { 0x04, 0x1E } },
	{ .data = { 0x05, 0x1E } },
	{ .data = { 0x06, 0x1F } },
	{ .data = { 0x07, 0x1F } },
	{ .data = { 0x08, 0x1F } },
	{ .data = { 0x09, 0x17 } },
	{ .data = { 0x0A, 0x17 } },
	{ .data = { 0x0B, 0x37 } },
	{ .data = { 0x0C, 0x37 } },
	{ .data = { 0x0D, 0x47 } },
	{ .data = { 0x0E, 0x47 } },
	{ .data = { 0x0F, 0x45 } },
	{ .data = { 0x10, 0x45 } },
	{ .data = { 0x11, 0x4B } },
	{ .data = { 0x12, 0x4B } },
	{ .data = { 0x13, 0x49 } },
	{ .data = { 0x14, 0x49 } },
	{ .data = { 0x15, 0x1F } },
	{ .data = { 0x16, 0x01 } },
	{ .data = { 0x17, 0x01 } },
	{ .data = { 0x18, 0x00 } },
	{ .data = { 0x19, 0x00 } },
	{ .data = { 0x1A, 0x1E } },
	{ .data = { 0x1B, 0x1E } },
	{ .data = { 0x1C, 0x1F } },
	{ .data = { 0x1D, 0x1F } },
	{ .data = { 0x1E, 0x1F } },
	{ .data = { 0x1F, 0x17 } },
	{ .data = { 0x20, 0x17 } },
	{ .data = { 0x21, 0x37 } },
	{ .data = { 0x22, 0x37 } },
	{ .data = { 0x23, 0x46 } },
	{ .data = { 0x24, 0x46 } },
	{ .data = { 0x25, 0x44 } },
	{ .data = { 0x26, 0x44 } },
	{ .data = { 0x27, 0x4A } },
	{ .data = { 0x28, 0x4A } },
	{ .data = { 0x29, 0x48 } },
	{ .data = { 0x2A, 0x48 } },
	{ .data = { 0x2B, 0x1F } },
	{ .data = { 0x2C, 0x01 } },
	{ .data = { 0x2D, 0x01 } },
	{ .data = { 0x2E, 0x00 } },
	{ .data = { 0x2F, 0x00 } },
	{ .data = { 0x30, 0x1F } },
	{ .data = { 0x31, 0x1F } },
	{ .data = { 0x32, 0x1E } },
	{ .data = { 0x33, 0x1E } },
	{ .data = { 0x34, 0x1F } },
	{ .data = { 0x35, 0x17 } },
	{ .data = { 0x36, 0x17 } },
	{ .data = { 0x37, 0x37 } },
	{ .data = { 0x38, 0x37 } },
	{ .data = { 0x39, 0x08 } },
	{ .data = { 0x3A, 0x08 } },
	{ .data = { 0x3B, 0x0A } },
	{ .data = { 0x3C, 0x0A } },
	{ .data = { 0x3D, 0x04 } },
	{ .data = { 0x3E, 0x04 } },
	{ .data = { 0x3F, 0x06 } },
	{ .data = { 0x40, 0x06 } },
	{ .data = { 0x41, 0x1F } },
	{ .data = { 0x42, 0x02 } },
	{ .data = { 0x43, 0x02 } },
	{ .data = { 0x44, 0x00 } },
	{ .data = { 0x45, 0x00 } },
	{ .data = { 0x46, 0x1F } },
	{ .data = { 0x47, 0x1F } },
	{ .data = { 0x48, 0x1E } },
	{ .data = { 0x49, 0x1E } },
	{ .data = { 0x4A, 0x1F } },
	{ .data = { 0x4B, 0x17 } },
	{ .data = { 0x4C, 0x17 } },
	{ .data = { 0x4D, 0x37 } },
	{ .data = { 0x4E, 0x37 } },
	{ .data = { 0x4F, 0x09 } },
	{ .data = { 0x50, 0x09 } },
	{ .data = { 0x51, 0x0B } },
	{ .data = { 0x52, 0x0B } },
	{ .data = { 0x53, 0x05 } },
	{ .data = { 0x54, 0x05 } },
	{ .data = { 0x55, 0x07 } },
	{ .data = { 0x56, 0x07 } },
	{ .data = { 0x57, 0x1F } },
	{ .data = { 0x58, 0x40 } },
	{ .data = { 0x5B, 0x30 } },
	{ .data = { 0x5C, 0x16 } },
	{ .data = { 0x5D, 0x34 } },
	{ .data = { 0x5E, 0x05 } },
	{ .data = { 0x5F, 0x02 } },
	{ .data = { 0x63, 0x00 } },
	{ .data = { 0x64, 0x6A } },
	{ .data = { 0x67, 0x73 } },
	{ .data = { 0x68, 0x1D } },
	{ .data = { 0x69, 0x08 } },
	{ .data = { 0x6A, 0x6A } },
	{ .data = { 0x6B, 0x08 } },
	{ .data = { 0x6C, 0x00 } },
	{ .data = { 0x6D, 0x00 } },
	{ .data = { 0x6E, 0x00 } },
	{ .data = { 0x6F, 0x88 } },
	{ .data = { 0x75, 0xFF } },
	{ .data = { 0x77, 0xDD } },
	{ .data = { 0x78, 0x3F } },
	{ .data = { 0x79, 0x15 } },
	{ .data = { 0x7A, 0x17 } },
	{ .data = { 0x7D, 0x14 } },
	{ .data = { 0x7E, 0x82 } },
	{ .data = { 0xE0, 0x04 } },
	{ .data = { 0x00, 0x0E } },
	{ .data = { 0x02, 0xB3 } },
	{ .data = { 0x09, 0x61 } },
	{ .data = { 0x0E, 0x48 } },
	{ .data = { 0xE0, 0x00 } },
	{ .data = { 0xE6, 0x02 } },
	{ .data = { 0xE7, 0x0C } },
};

static const struct jadard_panel_desc cz101b4001_desc = {
	.mode = {
		.clock		= 70000,

		.hdisplay	= 800,
		.hsync_start	= 800 + 40,
		.hsync_end	= 800 + 40 + 18,
		.htotal		= 800 + 40 + 18 + 20,

		.vdisplay	= 1280,
		.vsync_start	= 1280 + 20,
		.vsync_end	= 1280 + 20 + 4,
		.vtotal		= 1280 + 20 + 4 + 20,

		.width_mm	= 62,
		.height_mm	= 110,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.init_cmds = cz101b4001_init_cmds,
	.num_init_cmds = ARRAY_SIZE(cz101b4001_init_cmds),
};

static int jadard_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct jadard_panel_desc *desc;
	struct jadard *jadard;
	int ret;

	jadard = devm_kzalloc(&dsi->dev, sizeof(*jadard), GFP_KERNEL);
	if (!jadard)
		return -ENOMEM;

	desc = of_device_get_match_data(dev);
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	jadard->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(jadard->reset)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get our reset GPIO\n");
		return PTR_ERR(jadard->reset);
	}

	jadard->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(jadard->vdd)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get vdd regulator\n");
		return PTR_ERR(jadard->vdd);
	}

	jadard->vccio = devm_regulator_get(dev, "vccio");
	if (IS_ERR(jadard->vccio)) {
		DRM_DEV_ERROR(&dsi->dev, "failed to get vccio regulator\n");
		return PTR_ERR(jadard->vccio);
	}

	drm_panel_init(&jadard->panel, dev, &jadard_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&jadard->panel);
	if (ret)
		return ret;

	drm_panel_add(&jadard->panel);

	mipi_dsi_set_drvdata(dsi, jadard);
	jadard->dsi = dsi;
	jadard->desc = desc;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&jadard->panel);

	return ret;
}

static void jadard_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct jadard *jadard = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&jadard->panel);
}

static const struct of_device_id jadard_of_match[] = {
	{
		.compatible = "chongzhou,cz101b4001",
		.data = &cz101b4001_desc
	},
	{
		.compatible = "radxa,display-10hd-ad001",
		.data = &cz101b4001_desc
	},
	{
		.compatible = "radxa,display-8hd-ad002",
		.data = &radxa_display_8hd_ad002_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jadard_of_match);

static struct mipi_dsi_driver jadard_driver = {
	.probe = jadard_dsi_probe,
	.remove = jadard_dsi_remove,
	.driver = {
		.name = "jadard-jd9365da",
		.of_match_table = jadard_of_match,
	},
};
module_mipi_dsi_driver(jadard_driver);

MODULE_AUTHOR("Jagan Teki <jagan@edgeble.ai>");
MODULE_AUTHOR("Stephen Chen <stephen@radxa.com>");
MODULE_DESCRIPTION("Jadard JD9365DA-H3 WXGA DSI panel");
MODULE_LICENSE("GPL");
