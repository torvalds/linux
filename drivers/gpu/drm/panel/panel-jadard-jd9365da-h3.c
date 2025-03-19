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
#include <linux/of.h>
#include <linux/regulator/consumer.h>

struct jadard;

struct jadard_panel_desc {
	const struct drm_display_mode mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	int (*init)(struct jadard *jadard);
	bool lp11_before_reset;
	bool reset_before_power_off_vcioo;
	unsigned int vcioo_to_lp11_delay_ms;
	unsigned int lp11_to_reset_delay_ms;
	unsigned int backlight_off_to_display_off_delay_ms;
	unsigned int display_off_to_enter_sleep_delay_ms;
	unsigned int enter_sleep_to_reset_down_delay_ms;
};

struct jadard {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct jadard_panel_desc *desc;
	enum drm_panel_orientation orientation;
	struct regulator *vdd;
	struct regulator *vccio;
	struct gpio_desc *reset;
};

#define JD9365DA_DCS_SWITCH_PAGE	0xe0

#define jd9365da_switch_page(dsi_ctx, page) \
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, JD9365DA_DCS_SWITCH_PAGE, (page))

static void jadard_enable_standard_cmds(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe1, 0x93);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe2, 0x65);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe3, 0xf8);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x80, 0x03);
}

static inline struct jadard *panel_to_jadard(struct drm_panel *panel)
{
	return container_of(panel, struct jadard, panel);
}

static int jadard_disable(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = jadard->dsi };

	if (jadard->desc->backlight_off_to_display_off_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, jadard->desc->backlight_off_to_display_off_delay_ms);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);

	if (jadard->desc->display_off_to_enter_sleep_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, jadard->desc->display_off_to_enter_sleep_delay_ms);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	if (jadard->desc->enter_sleep_to_reset_down_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, jadard->desc->enter_sleep_to_reset_down_delay_ms);

	return dsi_ctx.accum_err;
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

	if (jadard->desc->vcioo_to_lp11_delay_ms)
		msleep(jadard->desc->vcioo_to_lp11_delay_ms);

	if (jadard->desc->lp11_before_reset) {
		ret = mipi_dsi_dcs_nop(jadard->dsi);
		if (ret)
			return ret;
	}

	if (jadard->desc->lp11_to_reset_delay_ms)
		msleep(jadard->desc->lp11_to_reset_delay_ms);

	gpiod_set_value(jadard->reset, 0);
	msleep(5);

	gpiod_set_value(jadard->reset, 1);
	msleep(10);

	gpiod_set_value(jadard->reset, 0);
	msleep(130);

	ret = jadard->desc->init(jadard);
	if (ret)
		return ret;

	return 0;
}

static int jadard_unprepare(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);

	gpiod_set_value(jadard->reset, 1);
	msleep(120);

	if (jadard->desc->reset_before_power_off_vcioo) {
		gpiod_set_value(jadard->reset, 0);

		usleep_range(1000, 2000);
	}

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

static enum drm_panel_orientation jadard_panel_get_orientation(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);

	return jadard->orientation;
}

static const struct drm_panel_funcs jadard_funcs = {
	.disable = jadard_disable,
	.unprepare = jadard_unprepare,
	.prepare = jadard_prepare,
	.get_modes = jadard_get_modes,
	.get_orientation = jadard_panel_get_orientation,
};

static int radxa_display_8hd_ad002_init_cmds(struct jadard *jadard)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = jadard->dsi };

	jd9365da_switch_page(&dsi_ctx, 0x00);
	jadard_enable_standard_cmds(&dsi_ctx);

	jd9365da_switch_page(&dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x7E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x65);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0C, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0xB7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1A, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1B, 0xB7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1C, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0xFE);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3A, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3B, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3C, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3D, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3E, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3F, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xA0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4B, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0xA9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5A, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5B, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5D, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5E, 0x63);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5F, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x3D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x62);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6A, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6B, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6C, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6D, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6E, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x63);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x3D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7A, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7B, 0x62);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7C, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7D, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7E, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7F, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x10);

	jd9365da_switch_page(&dsi_ctx, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x4B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x4B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0A, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0B, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0C, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0D, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0E, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0F, 0x5F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x5F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1A, 0x4A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1B, 0x4A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1C, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1D, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1E, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1F, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x5F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x5F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2A, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2B, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5A, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5B, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5C, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5D, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5E, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5F, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x6B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x0C);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6A, 0x56);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6B, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6C, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6D, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6E, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x7B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0xF8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0xD5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x2E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7A, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7B, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7C, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7D, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7E, 0x7B);

	jd9365da_switch_page(&dsi_ctx, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x0E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0xB3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0E, 0x2A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x59);

	jd9365da_switch_page(&dsi_ctx, 0x00);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
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
	.init = radxa_display_8hd_ad002_init_cmds,
};

static int cz101b4001_init_cmds(struct jadard *jadard)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = jadard->dsi };

	jd9365da_switch_page(&dsi_ctx, 0x00);
	jadard_enable_standard_cmds(&dsi_ctx);

	jd9365da_switch_page(&dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x3B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0C, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0xAF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1A, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1B, 0xAF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1C, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3A, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3C, 0x78);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3D, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3E, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3F, 0x7F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xA0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0x69);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5A, 0x2A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5B, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5D, 0x7F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5E, 0x6B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5F, 0x5C);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x4F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x4D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x3F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x2B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x63);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x52);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6A, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6B, 0x4F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6C, 0x4E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6D, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6E, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x7F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x6B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x5C);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x4F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x4D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x3F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x2B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7A, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7B, 0x63);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7C, 0x52);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7D, 0x5A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7E, 0x4F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7F, 0x4E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x0F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x00);

	jd9365da_switch_page(&dsi_ctx, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0A, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0B, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0C, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0D, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0E, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0F, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x4B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x4B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1A, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1B, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1C, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1D, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1E, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1F, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x4A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x4A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2A, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2B, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2C, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2D, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2E, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2F, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3A, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3B, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3C, 0x0A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3D, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3E, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3F, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0x1E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4A, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4B, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4C, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4D, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4E, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4F, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x0B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x0B);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0x1F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5B, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5C, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5D, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5E, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5F, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x6A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x1D);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6A, 0x6A);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6B, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6C, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6D, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6E, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6F, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0xFF);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0xDD);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x3F);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7A, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7D, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7E, 0x82);

	jd9365da_switch_page(&dsi_ctx, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x0E);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0xB3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x61);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0E, 0x48);

	jd9365da_switch_page(&dsi_ctx, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE6, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xE7, 0x0C);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
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
	.init = cz101b4001_init_cmds,
};

static int kingdisplay_kd101ne3_init_cmds(struct jadard *jadard)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = jadard->dsi };

	jd9365da_switch_page(&dsi_ctx, 0x00);
	jadard_enable_standard_cmds(&dsi_ctx);

	jd9365da_switch_page(&dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0xc7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0xc7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3a, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3c, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3d, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3e, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0x6a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x61);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x39);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6c, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x61);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7a, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7b, 0x54);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7c, 0x42);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x39);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x02);

	jd9365da_switch_page(&dsi_ctx, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x52);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0x4a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x53);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x51);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x4d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2c, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2d, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2e, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3a, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3c, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3d, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3e, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4c, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4e, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4f, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0xb4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0xbb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x2a);

	jd9365da_switch_page(&dsi_ctx, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0xb3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x61);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x48);

	jd9365da_switch_page(&dsi_ctx, 0x00);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
};

static const struct jadard_panel_desc kingdisplay_kd101ne3_40ti_desc = {
	.mode = {
		.clock		= (800 + 24 + 24 + 24) * (1280 + 30 + 4 + 8) * 60 / 1000,

		.hdisplay	= 800,
		.hsync_start	= 800 + 24,
		.hsync_end	= 800 + 24 + 24,
		.htotal		= 800 + 24 + 24 + 24,

		.vdisplay	= 1280,
		.vsync_start	= 1280 + 30,
		.vsync_end	= 1280 + 30 + 4,
		.vtotal		= 1280 + 30 + 4 + 8,

		.width_mm	= 135,
		.height_mm	= 216,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.init = kingdisplay_kd101ne3_init_cmds,
	.lp11_before_reset = true,
	.reset_before_power_off_vcioo = true,
	.vcioo_to_lp11_delay_ms = 5,
	.lp11_to_reset_delay_ms = 10,
	.backlight_off_to_display_off_delay_ms = 100,
	.display_off_to_enter_sleep_delay_ms = 50,
	.enter_sleep_to_reset_down_delay_ms = 100,
};

static int melfas_lmfbx101117480_init_cmds(struct jadard *jadard)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = jadard->dsi };

	jd9365da_switch_page(&dsi_ctx, 0x00);
	jadard_enable_standard_cmds(&dsi_ctx);

	jd9365da_switch_page(&dsi_ctx, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0xd7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0xd7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x70);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0xfd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3a, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3c, 0x7e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3d, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3e, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0xa0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x74);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0x6a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x59, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x56);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6c, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x70, 0x73);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x71, 0x56);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x72, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x73, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x74, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x79, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7a, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7b, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7c, 0x3e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7e, 0x36);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x02);

	jd9365da_switch_page(&dsi_ctx, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x52);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x01, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x03, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x04, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x05, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x06, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x07, 0x4e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x08, 0x4c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0a, 0x4a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0b, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0c, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0d, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x44);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0f, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x11, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x12, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x13, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x14, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x15, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x16, 0x53);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x17, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x18, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x19, 0x51);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1a, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1b, 0x57);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1c, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1d, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1e, 0x4d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x20, 0x4b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x21, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x22, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x23, 0x47);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x24, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x25, 0x41);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x26, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x27, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x29, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2a, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2c, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2d, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2e, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x30, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x31, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x32, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x33, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x34, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x37, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x38, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x39, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3a, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3c, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3d, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3e, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3f, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x40, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x41, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x42, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x43, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x45, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x46, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x47, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x48, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x49, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4a, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4b, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4c, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4d, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4e, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x4f, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x50, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x52, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x54, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x55, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x56, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x57, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x58, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5b, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5c, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5d, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x62, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x64, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x65, 0x75);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x66, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x67, 0xb4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x68, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x69, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6a, 0x6c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6b, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6e, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x75, 0xbb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x76, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x77, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x78, 0x2a);

	jd9365da_switch_page(&dsi_ctx, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x02, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x09, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x0e, 0x48);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x36, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2b, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2e, 0x03);

	jd9365da_switch_page(&dsi_ctx, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe7, 0x06);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
};

static const struct jadard_panel_desc melfas_lmfbx101117480_desc = {
	.mode = {
		.clock		= (800 + 24 + 24 + 24) * (1280 + 30 + 4 + 8) * 60 / 1000,

		.hdisplay	= 800,
		.hsync_start	= 800 + 24,
		.hsync_end	= 800 + 24 + 24,
		.htotal		= 800 + 24 + 24 + 24,

		.vdisplay	= 1280,
		.vsync_start	= 1280 + 30,
		.vsync_end	= 1280 + 30 + 4,
		.vtotal		= 1280 + 30 + 4 + 8,

		.width_mm	= 135,
		.height_mm	= 216,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.init = melfas_lmfbx101117480_init_cmds,
	.lp11_before_reset = true,
	.reset_before_power_off_vcioo = true,
	.vcioo_to_lp11_delay_ms = 5,
	.lp11_to_reset_delay_ms = 10,
	.backlight_off_to_display_off_delay_ms = 100,
	.display_off_to_enter_sleep_delay_ms = 50,
	.enter_sleep_to_reset_down_delay_ms = 100,
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

	jadard->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
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

	ret = of_drm_get_panel_orientation(dev->of_node, &jadard->orientation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get orientation\n");

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
		.compatible = "kingdisplay,kd101ne3-40ti",
		.data = &kingdisplay_kd101ne3_40ti_desc
	},
	{
		.compatible = "melfas,lmfbx101117480",
		.data = &melfas_lmfbx101117480_desc
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
