// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for panels based on Himax HX8394 controller, such as:
 *
 * - HannStar HSD060BHW4 5.99" MIPI-DSI panel
 *
 * Copyright (C) 2021 Kamil Trzciński
 *
 * Based on drivers/gpu/drm/panel/panel-sitronix-st7703.c
 * Copyright (C) Purism SPC 2019
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DRV_NAME "panel-himax-hx8394"

/* Manufacturer specific commands sent via DSI, listed in HX8394-F datasheet */
#define HX8394_CMD_SETSEQUENCE	  0xb0
#define HX8394_CMD_SETPOWER	  0xb1
#define HX8394_CMD_SETDISP	  0xb2
#define HX8394_CMD_SETCYC	  0xb4
#define HX8394_CMD_SETVCOM	  0xb6
#define HX8394_CMD_SETTE	  0xb7
#define HX8394_CMD_SETSENSOR	  0xb8
#define HX8394_CMD_SETEXTC	  0xb9
#define HX8394_CMD_SETMIPI	  0xba
#define HX8394_CMD_SETOTP	  0xbb
#define HX8394_CMD_SETREGBANK	  0xbd
#define HX8394_CMD_UNKNOWN5	  0xbf
#define HX8394_CMD_UNKNOWN1	  0xc0
#define HX8394_CMD_SETDGCLUT	  0xc1
#define HX8394_CMD_SETID	  0xc3
#define HX8394_CMD_SETDDB	  0xc4
#define HX8394_CMD_UNKNOWN2	  0xc6
#define HX8394_CMD_SETCABC	  0xc9
#define HX8394_CMD_SETCABCGAIN	  0xca
#define HX8394_CMD_SETPANEL	  0xcc
#define HX8394_CMD_SETOFFSET	  0xd2
#define HX8394_CMD_SETGIP0	  0xd3
#define HX8394_CMD_UNKNOWN3	  0xd4
#define HX8394_CMD_SETGIP1	  0xd5
#define HX8394_CMD_SETGIP2	  0xd6
#define HX8394_CMD_SETGPO	  0xd6
#define HX8394_CMD_UNKNOWN4	  0xd8
#define HX8394_CMD_SETSCALING	  0xdd
#define HX8394_CMD_SETIDLE	  0xdf
#define HX8394_CMD_SETGAMMA	  0xe0
#define HX8394_CMD_SETCHEMODE_DYN 0xe4
#define HX8394_CMD_SETCHE	  0xe5
#define HX8394_CMD_SETCESEL	  0xe6
#define HX8394_CMD_SET_SP_CMD	  0xe9
#define HX8394_CMD_SETREADINDEX	  0xfe
#define HX8394_CMD_GETSPIREAD	  0xff

struct hx8394 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *vcc;
	struct regulator *iovcc;
	enum drm_panel_orientation orientation;

	const struct hx8394_panel_desc *desc;
};

struct hx8394_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	int (*init_sequence)(struct hx8394 *ctx);
};

static inline struct hx8394 *panel_to_hx8394(struct drm_panel *panel)
{
	return container_of(panel, struct hx8394, panel);
}

static int hsd060bhw4_init_sequence(struct hx8394 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/* 5.19.8 SETEXTC: Set extension command (B9h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETEXTC,
			       0xff, 0x83, 0x94);

	/* 5.19.2 SETPOWER: Set power (B1h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETPOWER,
			       0x48, 0x11, 0x71, 0x09, 0x32, 0x24, 0x71, 0x31, 0x55, 0x30);

	/* 5.19.9 SETMIPI: Set MIPI control (BAh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETMIPI,
			       0x63, 0x03, 0x68, 0x6b, 0xb2, 0xc0);

	/* 5.19.3 SETDISP: Set display related register (B2h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETDISP,
			       0x00, 0x80, 0x78, 0x0c, 0x07);

	/* 5.19.4 SETCYC: Set display waveform cycles (B4h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETCYC,
			       0x12, 0x63, 0x12, 0x63, 0x12, 0x63, 0x01, 0x0c, 0x7c, 0x55,
			       0x00, 0x3f, 0x12, 0x6b, 0x12, 0x6b, 0x12, 0x6b, 0x01, 0x0c,
			       0x7c);

	/* 5.19.19 SETGIP0: Set GIP Option0 (D3h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGIP0,
			       0x00, 0x00, 0x00, 0x00, 0x3c, 0x1c, 0x00, 0x00, 0x32, 0x10,
			       0x09, 0x00, 0x09, 0x32, 0x15, 0xad, 0x05, 0xad, 0x32, 0x00,
			       0x00, 0x00, 0x00, 0x37, 0x03, 0x0b, 0x0b, 0x37, 0x00, 0x00,
			       0x00, 0x0c, 0x40);

	/* 5.19.20 Set GIP Option1 (D5h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGIP1,
			       0x19, 0x19, 0x18, 0x18, 0x1b, 0x1b, 0x1a, 0x1a, 0x00, 0x01,
			       0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x20, 0x21, 0x18, 0x18,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0x24, 0x25, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18);

	/* 5.19.21 Set GIP Option2 (D6h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGIP2,
			       0x18, 0x18, 0x19, 0x19, 0x1b, 0x1b, 0x1a, 0x1a, 0x07, 0x06,
			       0x05, 0x04, 0x03, 0x02, 0x01, 0x00, 0x25, 0x24, 0x18, 0x18,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0x21, 0x20, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18);

	/* 5.19.25 SETGAMMA: Set gamma curve related setting (E0h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGAMMA,
			       0x00, 0x04, 0x0c, 0x12, 0x14, 0x18, 0x1a, 0x18, 0x31, 0x3f,
			       0x4d, 0x4c, 0x54, 0x65, 0x6b, 0x70, 0x7f, 0x82, 0x7e, 0x8a,
			       0x99, 0x4a, 0x48, 0x49, 0x4b, 0x4a, 0x4c, 0x4b, 0x7f, 0x00,
			       0x04, 0x0c, 0x11, 0x13, 0x17, 0x1a, 0x18, 0x31,
			       0x3f, 0x4d, 0x4c, 0x54, 0x65, 0x6b, 0x70, 0x7f,
			       0x82, 0x7e, 0x8a, 0x99, 0x4a, 0x48, 0x49, 0x4b,
			       0x4a, 0x4c, 0x4b, 0x7f);

	/* 5.19.17 SETPANEL (CCh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETPANEL,
			       0x0b);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN1,
			       0x1f, 0x31);

	/* 5.19.5 SETVCOM: Set VCOM voltage (B6h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETVCOM,
			       0x7d, 0x7d);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN3,
			       0x02);

	/* 5.19.11 Set register bank (BDh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETREGBANK,
			       0x01);

	/* 5.19.2 SETPOWER: Set power (B1h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETPOWER,
			       0x00);

	/* 5.19.11 Set register bank (BDh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETREGBANK,
			       0x00);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN3,
			       0xed);

	return 0;
}

static const struct drm_display_mode hsd060bhw4_mode = {
	.hdisplay    = 720,
	.hsync_start = 720 + 40,
	.hsync_end   = 720 + 40 + 46,
	.htotal	     = 720 + 40 + 46 + 40,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 9,
	.vsync_end   = 1440 + 9 + 7,
	.vtotal	     = 1440 + 9 + 7 + 7,
	.clock	     = 74250,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 68,
	.height_mm   = 136,
};

static const struct hx8394_panel_desc hsd060bhw4_desc = {
	.mode = &hsd060bhw4_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = hsd060bhw4_init_sequence,
};

static int powkiddy_x55_init_sequence(struct hx8394 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/* 5.19.8 SETEXTC: Set extension command (B9h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETEXTC,
			       0xff, 0x83, 0x94);

	/* 5.19.9 SETMIPI: Set MIPI control (BAh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETMIPI,
			       0x63, 0x03, 0x68, 0x6b, 0xb2, 0xc0);

	/* 5.19.2 SETPOWER: Set power (B1h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETPOWER,
			       0x48, 0x12, 0x72, 0x09, 0x32, 0x54, 0x71, 0x71, 0x57, 0x47);

	/* 5.19.3 SETDISP: Set display related register (B2h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETDISP,
			       0x00, 0x80, 0x64, 0x2c, 0x16, 0x2f);

	/* 5.19.4 SETCYC: Set display waveform cycles (B4h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETCYC,
			       0x73, 0x74, 0x73, 0x74, 0x73, 0x74, 0x01, 0x0c, 0x86, 0x75,
			       0x00, 0x3f, 0x73, 0x74, 0x73, 0x74, 0x73, 0x74, 0x01, 0x0c,
			       0x86);

	/* 5.19.5 SETVCOM: Set VCOM voltage (B6h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETVCOM,
			       0x6e, 0x6e);

	/* 5.19.19 SETGIP0: Set GIP Option0 (D3h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGIP0,
			       0x00, 0x00, 0x07, 0x07, 0x40, 0x07, 0x0c, 0x00, 0x08, 0x10,
			       0x08, 0x00, 0x08, 0x54, 0x15, 0x0a, 0x05, 0x0a, 0x02, 0x15,
			       0x06, 0x05, 0x06, 0x47, 0x44, 0x0a, 0x0a, 0x4b, 0x10, 0x07,
			       0x07, 0x0c, 0x40);

	/* 5.19.20 Set GIP Option1 (D5h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGIP1,
			       0x1c, 0x1c, 0x1d, 0x1d, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
			       0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x24, 0x25, 0x18, 0x18,
			       0x26, 0x27, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x20, 0x21,
			       0x18, 0x18, 0x18, 0x18);

	/* 5.19.21 Set GIP Option2 (D6h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGIP2,
			       0x1c, 0x1c, 0x1d, 0x1d, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
			       0x01, 0x00, 0x0b, 0x0a, 0x09, 0x08, 0x21, 0x20, 0x18, 0x18,
			       0x27, 0x26, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
			       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x25, 0x24,
			       0x18, 0x18, 0x18, 0x18);

	/* 5.19.25 SETGAMMA: Set gamma curve related setting (E0h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETGAMMA,
			       0x00, 0x0a, 0x15, 0x1b, 0x1e, 0x21, 0x24, 0x22, 0x47, 0x56,
			       0x65, 0x66, 0x6e, 0x82, 0x88, 0x8b, 0x9a, 0x9d, 0x98, 0xa8,
			       0xb9, 0x5d, 0x5c, 0x61, 0x66, 0x6a, 0x6f, 0x7f, 0x7f, 0x00,
			       0x0a, 0x15, 0x1b, 0x1e, 0x21, 0x24, 0x22, 0x47, 0x56, 0x65,
			       0x65, 0x6e, 0x81, 0x87, 0x8b, 0x98, 0x9d, 0x99, 0xa8, 0xba,
			       0x5d, 0x5d, 0x62, 0x67, 0x6b, 0x72, 0x7f, 0x7f);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN1,
			       0x1f, 0x31);

	/* 5.19.17 SETPANEL (CCh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETPANEL,
			       0x0b);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN3,
			       0x02);

	/* 5.19.11 Set register bank (BDh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETREGBANK,
			       0x02);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN4,
			       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			       0xff, 0xff);

	/* 5.19.11 Set register bank (BDh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETREGBANK,
			       0x00);

	/* 5.19.11 Set register bank (BDh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETREGBANK,
			       0x01);

	/* 5.19.2 SETPOWER: Set power (B1h) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETPOWER,
			       0x00);

	/* 5.19.11 Set register bank (BDh) */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_SETREGBANK,
			       0x00);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN5,
			       0x40, 0x81, 0x50, 0x00, 0x1a, 0xfc, 0x01);

	/* Unknown command, not listed in the HX8394-F datasheet */
	mipi_dsi_dcs_write_seq(dsi, HX8394_CMD_UNKNOWN2,
			       0xed);

	return 0;
}

static const struct drm_display_mode powkiddy_x55_mode = {
	.hdisplay	= 720,
	.hsync_start	= 720 + 44,
	.hsync_end	= 720 + 44 + 20,
	.htotal		= 720 + 44 + 20 + 20,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 12,
	.vsync_end	= 1280 + 12 + 10,
	.vtotal		= 1280 + 12 + 10 + 10,
	.clock		= 63290,
	.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm	= 67,
	.height_mm	= 121,
};

static const struct hx8394_panel_desc powkiddy_x55_desc = {
	.mode = &powkiddy_x55_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = powkiddy_x55_init_sequence,
};

static int hx8394_enable(struct drm_panel *panel)
{
	struct hx8394 *ctx = panel_to_hx8394(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = ctx->desc->init_sequence(ctx);
	if (ret) {
		dev_err(ctx->dev, "Panel init sequence failed: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret) {
		dev_err(ctx->dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	/* Panel is operational 120 msec after reset */
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret) {
		dev_err(ctx->dev, "Failed to turn on the display: %d\n", ret);
		goto sleep_in;
	}

	return 0;

sleep_in:
	/* This will probably fail, but let's try orderly power off anyway. */
	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (!ret)
		msleep(50);

	return ret;
}

static int hx8394_disable(struct drm_panel *panel)
{
	struct hx8394 *ctx = panel_to_hx8394(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret) {
		dev_err(ctx->dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}

	msleep(50); /* about 3 frames */

	return 0;
}

static int hx8394_unprepare(struct drm_panel *panel)
{
	struct hx8394 *ctx = panel_to_hx8394(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vcc);

	return 0;
}

static int hx8394_prepare(struct drm_panel *panel)
{
	struct hx8394 *ctx = panel_to_hx8394(panel);
	int ret;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	ret = regulator_enable(ctx->vcc);
	if (ret) {
		dev_err(ctx->dev, "Failed to enable vcc supply: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(ctx->iovcc);
	if (ret) {
		dev_err(ctx->dev, "Failed to enable iovcc supply: %d\n", ret);
		goto disable_vcc;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	msleep(180);

	return 0;

disable_vcc:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->vcc);
	return ret;
}

static int hx8394_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct hx8394 *ctx = panel_to_hx8394(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(ctx->dev, "Failed to add mode %ux%u@%u\n",
			ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static enum drm_panel_orientation hx8394_get_orientation(struct drm_panel *panel)
{
	struct hx8394 *ctx = panel_to_hx8394(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs hx8394_drm_funcs = {
	.disable   = hx8394_disable,
	.unprepare = hx8394_unprepare,
	.prepare   = hx8394_prepare,
	.enable	   = hx8394_enable,
	.get_modes = hx8394_get_modes,
	.get_orientation = hx8394_get_orientation,
};

static int hx8394_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct hx8394 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset gpio\n");

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
		return ret;
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->desc = of_device_get_match_data(dev);

	dsi->mode_flags = ctx->desc->mode_flags;
	dsi->format = ctx->desc->format;
	dsi->lanes = ctx->desc->lanes;

	ctx->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(ctx->vcc))
		return dev_err_probe(dev, PTR_ERR(ctx->vcc),
				     "Failed to request vcc regulator\n");

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc))
		return dev_err_probe(dev, PTR_ERR(ctx->iovcc),
				     "Failed to request iovcc regulator\n");

	drm_panel_init(&ctx->panel, dev, &hx8394_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err_probe(dev, ret, "mipi_dsi_attach failed\n");
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_dbg(dev, "%ux%u@%u %ubpp dsi %udl - ready\n",
		ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
		drm_mode_vrefresh(ctx->desc->mode),
		mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes);

	return 0;
}

static void hx8394_remove(struct mipi_dsi_device *dsi)
{
	struct hx8394 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id hx8394_of_match[] = {
	{ .compatible = "hannstar,hsd060bhw4", .data = &hsd060bhw4_desc },
	{ .compatible = "powkiddy,x55-panel", .data = &powkiddy_x55_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hx8394_of_match);

static struct mipi_dsi_driver hx8394_driver = {
	.probe	= hx8394_probe,
	.remove = hx8394_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = hx8394_of_match,
	},
};
module_mipi_dsi_driver(hx8394_driver);

MODULE_AUTHOR("Kamil Trzciński <ayufan@ayufan.eu>");
MODULE_DESCRIPTION("DRM driver for Himax HX8394 based MIPI DSI panels");
MODULE_LICENSE("GPL");
