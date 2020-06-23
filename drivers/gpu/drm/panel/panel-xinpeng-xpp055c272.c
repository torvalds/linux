// SPDX-License-Identifier: GPL-2.0
/*
 * Xinpeng xpp055c272 5.5" MIPI-DSI panel driver
 * Copyright (C) 2019 Theobroma Systems Design und Consulting GmbH
 *
 * based on
 *
 * Rockteck jh057n00900 5.5" MIPI-DSI panel driver
 * Copyright (C) Purism SPC 2019
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

/* Manufacturer specific Commands send via DSI */
#define XPP055C272_CMD_ALL_PIXEL_OFF	0x22
#define XPP055C272_CMD_ALL_PIXEL_ON	0x23
#define XPP055C272_CMD_SETDISP		0xb2
#define XPP055C272_CMD_SETRGBIF		0xb3
#define XPP055C272_CMD_SETCYC		0xb4
#define XPP055C272_CMD_SETBGP		0xb5
#define XPP055C272_CMD_SETVCOM		0xb6
#define XPP055C272_CMD_SETOTP		0xb7
#define XPP055C272_CMD_SETPOWER_EXT	0xb8
#define XPP055C272_CMD_SETEXTC		0xb9
#define XPP055C272_CMD_SETMIPI		0xbA
#define XPP055C272_CMD_SETVDC		0xbc
#define XPP055C272_CMD_SETPCR		0xbf
#define XPP055C272_CMD_SETSCR		0xc0
#define XPP055C272_CMD_SETPOWER		0xc1
#define XPP055C272_CMD_SETECO		0xc6
#define XPP055C272_CMD_SETPANEL		0xcc
#define XPP055C272_CMD_SETGAMMA		0xe0
#define XPP055C272_CMD_SETEQ		0xe3
#define XPP055C272_CMD_SETGIP1		0xe9
#define XPP055C272_CMD_SETGIP2		0xea

struct xpp055c272 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *vci;
	struct regulator *iovcc;
	bool prepared;
};

static inline struct xpp055c272 *panel_to_xpp055c272(struct drm_panel *panel)
{
	return container_of(panel, struct xpp055c272, panel);
}

#define dsi_generic_write_seq(dsi, cmd, seq...) do {			\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write(dsi, cmd, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static int xpp055c272_init_sequence(struct xpp055c272 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = ctx->dev;

	/*
	 * Init sequence was supplied by the panel vendor without much
	 * documentation.
	 */
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETEXTC, 0xf1, 0x12, 0x83);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETMIPI,
			      0x33, 0x81, 0x05, 0xf9, 0x0e, 0x0e, 0x00, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x25,
			      0x00, 0x91, 0x0a, 0x00, 0x00, 0x02, 0x4f, 0x01,
			      0x00, 0x00, 0x37);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETPOWER_EXT, 0x25);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETPCR, 0x02, 0x11, 0x00);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETRGBIF,
			      0x0c, 0x10, 0x0a, 0x50, 0x03, 0xff, 0x00, 0x00,
			      0x00, 0x00);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETSCR,
			      0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x08, 0x70,
			      0x00);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETVDC, 0x46);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETPANEL, 0x0b);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETCYC, 0x80);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETDISP, 0xc8, 0x12, 0x30);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETEQ,
			      0x07, 0x07, 0x0B, 0x0B, 0x03, 0x0B, 0x00, 0x00,
			      0x00, 0x00, 0xFF, 0x00, 0xC0, 0x10);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETPOWER,
			      0x53, 0x00, 0x1e, 0x1e, 0x77, 0xe1, 0xcc, 0xdd,
			      0x67, 0x77, 0x33, 0x33);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETECO, 0x00, 0x00, 0xff,
			      0xff, 0x01, 0xff);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETBGP, 0x09, 0x09);
	msleep(20);

	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETVCOM, 0x87, 0x95);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETGIP1,
			      0xc2, 0x10, 0x05, 0x05, 0x10, 0x05, 0xa0, 0x12,
			      0x31, 0x23, 0x3f, 0x81, 0x0a, 0xa0, 0x37, 0x18,
			      0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x80,
			      0x01, 0x00, 0x00, 0x00, 0x48, 0xf8, 0x86, 0x42,
			      0x08, 0x88, 0x88, 0x80, 0x88, 0x88, 0x88, 0x58,
			      0xf8, 0x87, 0x53, 0x18, 0x88, 0x88, 0x81, 0x88,
			      0x88, 0x88, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETGIP2,
			      0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x1f, 0x88, 0x81, 0x35,
			      0x78, 0x88, 0x88, 0x85, 0x88, 0x88, 0x88, 0x0f,
			      0x88, 0x80, 0x24, 0x68, 0x88, 0x88, 0x84, 0x88,
			      0x88, 0x88, 0x23, 0x10, 0x00, 0x00, 0x1c, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x05,
			      0xa0, 0x00, 0x00, 0x00, 0x00);
	dsi_generic_write_seq(dsi, XPP055C272_CMD_SETGAMMA,
			      0x00, 0x06, 0x08, 0x2a, 0x31, 0x3f, 0x38, 0x36,
			      0x07, 0x0c, 0x0d, 0x11, 0x13, 0x12, 0x13, 0x11,
			      0x18, 0x00, 0x06, 0x08, 0x2a, 0x31, 0x3f, 0x38,
			      0x36, 0x07, 0x0c, 0x0d, 0x11, 0x13, 0x12, 0x13,
			      0x11, 0x18);

	msleep(60);

	DRM_DEV_DEBUG_DRIVER(dev, "Panel init sequence done\n");
	return 0;
}

static int xpp055c272_unprepare(struct drm_panel *panel)
{
	struct xpp055c272 *ctx = panel_to_xpp055c272(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(ctx->dev, "failed to set display off: %d\n",
			      ret);

	mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev, "failed to enter sleep mode: %d\n",
			      ret);
		return ret;
	}

	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vci);

	ctx->prepared = false;

	return 0;
}

static int xpp055c272_prepare(struct drm_panel *panel)
{
	struct xpp055c272 *ctx = panel_to_xpp055c272(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (ctx->prepared)
		return 0;

	DRM_DEV_DEBUG_DRIVER(ctx->dev, "Resetting the panel\n");
	ret = regulator_enable(ctx->vci);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to enable vci supply: %d\n", ret);
		return ret;
	}
	ret = regulator_enable(ctx->iovcc);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to enable iovcc supply: %d\n", ret);
		goto disable_vci;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	/* T6: 10us */
	usleep_range(10, 20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	/* T8: 20ms */
	msleep(20);

	ret = xpp055c272_init_sequence(ctx);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev, "Panel init sequence failed: %d\n",
			      ret);
		goto disable_iovcc;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev, "Failed to exit sleep mode: %d\n", ret);
		goto disable_iovcc;
	}

	/* T9: 120ms */
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev, "Failed to set display on: %d\n", ret);
		goto disable_iovcc;
	}

	msleep(50);

	ctx->prepared = true;

	return 0;

disable_iovcc:
	regulator_disable(ctx->iovcc);
disable_vci:
	regulator_disable(ctx->vci);
	return ret;
}

static const struct drm_display_mode default_mode = {
	.hdisplay	= 720,
	.hsync_start	= 720 + 40,
	.hsync_end	= 720 + 40 + 10,
	.htotal		= 720 + 40 + 10 + 40,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 22,
	.vsync_end	= 1280 + 22 + 4,
	.vtotal		= 1280 + 22 + 4 + 11,
	.clock		= 64000,
	.width_mm	= 68,
	.height_mm	= 121,
};

static int xpp055c272_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct xpp055c272 *ctx = panel_to_xpp055c272(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		DRM_DEV_ERROR(ctx->dev, "Failed to add mode %ux%u@%u\n",
			      default_mode.hdisplay, default_mode.vdisplay,
			      drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs xpp055c272_funcs = {
	.unprepare	= xpp055c272_unprepare,
	.prepare	= xpp055c272_prepare,
	.get_modes	= xpp055c272_get_modes,
};

static int xpp055c272_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct xpp055c272 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get reset gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	ctx->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ctx->vci)) {
		ret = PTR_ERR(ctx->vci);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev,
				      "Failed to request vci regulator: %d\n",
				      ret);
		return ret;
	}

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc)) {
		ret = PTR_ERR(ctx->iovcc);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev,
				      "Failed to request iovcc regulator: %d\n",
				      ret);
		return ret;
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET;

	drm_panel_init(&ctx->panel, &dsi->dev, &xpp055c272_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "mipi_dsi_attach failed: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void xpp055c272_shutdown(struct mipi_dsi_device *dsi)
{
	struct xpp055c272 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = drm_panel_unprepare(&ctx->panel);
	if (ret < 0)
		DRM_DEV_ERROR(&dsi->dev, "Failed to unprepare panel: %d\n",
			      ret);

	ret = drm_panel_disable(&ctx->panel);
	if (ret < 0)
		DRM_DEV_ERROR(&dsi->dev, "Failed to disable panel: %d\n",
			      ret);
}

static int xpp055c272_remove(struct mipi_dsi_device *dsi)
{
	struct xpp055c272 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	xpp055c272_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(&dsi->dev, "Failed to detach from DSI host: %d\n",
			      ret);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id xpp055c272_of_match[] = {
	{ .compatible = "xinpeng,xpp055c272" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, xpp055c272_of_match);

static struct mipi_dsi_driver xpp055c272_driver = {
	.driver = {
		.name = "panel-xinpeng-xpp055c272",
		.of_match_table = xpp055c272_of_match,
	},
	.probe	= xpp055c272_probe,
	.remove = xpp055c272_remove,
	.shutdown = xpp055c272_shutdown,
};
module_mipi_dsi_driver(xpp055c272_driver);

MODULE_AUTHOR("Heiko Stuebner <heiko.stuebner@theobroma-systems.com>");
MODULE_DESCRIPTION("DRM driver for Xinpeng xpp055c272 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
