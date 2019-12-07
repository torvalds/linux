// SPDX-License-Identifier: GPL-2.0
/*
 * Rockteck jh057n00900 5.5" MIPI-DSI panel driver
 *
 * Copyright (C) Purism SPC 2019
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <linux/backlight.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <video/display_timing.h>
#include <video/mipi_display.h>

#define DRV_NAME "panel-rocktech-jh057n00900"

/* Manufacturer specific Commands send via DSI */
#define ST7703_CMD_ALL_PIXEL_OFF 0x22
#define ST7703_CMD_ALL_PIXEL_ON	 0x23
#define ST7703_CMD_SETDISP	 0xB2
#define ST7703_CMD_SETRGBIF	 0xB3
#define ST7703_CMD_SETCYC	 0xB4
#define ST7703_CMD_SETBGP	 0xB5
#define ST7703_CMD_SETVCOM	 0xB6
#define ST7703_CMD_SETOTP	 0xB7
#define ST7703_CMD_SETPOWER_EXT	 0xB8
#define ST7703_CMD_SETEXTC	 0xB9
#define ST7703_CMD_SETMIPI	 0xBA
#define ST7703_CMD_SETVDC	 0xBC
#define ST7703_CMD_UNKNOWN0	 0xBF
#define ST7703_CMD_SETSCR	 0xC0
#define ST7703_CMD_SETPOWER	 0xC1
#define ST7703_CMD_SETPANEL	 0xCC
#define ST7703_CMD_SETGAMMA	 0xE0
#define ST7703_CMD_SETEQ	 0xE3
#define ST7703_CMD_SETGIP1	 0xE9
#define ST7703_CMD_SETGIP2	 0xEA

struct jh057n {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct backlight_device *backlight;
	struct regulator *vcc;
	struct regulator *iovcc;
	bool prepared;

	struct dentry *debugfs;
};

static inline struct jh057n *panel_to_jh057n(struct drm_panel *panel)
{
	return container_of(panel, struct jh057n, panel);
}

#define dsi_generic_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_generic_write(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static int jh057n_init_sequence(struct jh057n *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	struct device *dev = ctx->dev;
	int ret;

	/*
	 * Init sequence was supplied by the panel vendor. Most of the commands
	 * resemble the ST7703 but the number of parameters often don't match
	 * so it's likely a clone.
	 */
	dsi_generic_write_seq(dsi, ST7703_CMD_SETEXTC,
			      0xF1, 0x12, 0x83);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETRGBIF,
			      0x10, 0x10, 0x05, 0x05, 0x03, 0xFF, 0x00, 0x00,
			      0x00, 0x00);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETSCR,
			      0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x08, 0x70,
			      0x00);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETVDC, 0x4E);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETPANEL, 0x0B);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETDISP, 0xF0, 0x12, 0x30);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETEQ,
			      0x07, 0x07, 0x0B, 0x0B, 0x03, 0x0B, 0x00, 0x00,
			      0x00, 0x00, 0xFF, 0x00, 0xC0, 0x10);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETBGP, 0x08, 0x08);
	msleep(20);

	dsi_generic_write_seq(dsi, ST7703_CMD_SETVCOM, 0x3F, 0x3F);
	dsi_generic_write_seq(dsi, ST7703_CMD_UNKNOWN0, 0x02, 0x11, 0x00);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETGIP1,
			      0x82, 0x10, 0x06, 0x05, 0x9E, 0x0A, 0xA5, 0x12,
			      0x31, 0x23, 0x37, 0x83, 0x04, 0xBC, 0x27, 0x38,
			      0x0C, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00,
			      0x03, 0x00, 0x00, 0x00, 0x75, 0x75, 0x31, 0x88,
			      0x88, 0x88, 0x88, 0x88, 0x88, 0x13, 0x88, 0x64,
			      0x64, 0x20, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
			      0x02, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETGIP2,
			      0x02, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x02, 0x46, 0x02, 0x88,
			      0x88, 0x88, 0x88, 0x88, 0x88, 0x64, 0x88, 0x13,
			      0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
			      0x75, 0x88, 0x23, 0x14, 0x00, 0x00, 0x02, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x0A,
			      0xA5, 0x00, 0x00, 0x00, 0x00);
	dsi_generic_write_seq(dsi, ST7703_CMD_SETGAMMA,
			      0x00, 0x09, 0x0E, 0x29, 0x2D, 0x3C, 0x41, 0x37,
			      0x07, 0x0B, 0x0D, 0x10, 0x11, 0x0F, 0x10, 0x11,
			      0x18, 0x00, 0x09, 0x0E, 0x29, 0x2D, 0x3C, 0x41,
			      0x37, 0x07, 0x0B, 0x0D, 0x10, 0x11, 0x0F, 0x10,
			      0x11, 0x18);
	msleep(20);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	/* Panel is operational 120 msec after reset */
	msleep(60);
	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	DRM_DEV_DEBUG_DRIVER(dev, "Panel init sequence done\n");
	return 0;
}

static int jh057n_enable(struct drm_panel *panel)
{
	struct jh057n *ctx = panel_to_jh057n(panel);
	int ret;

	ret = jh057n_init_sequence(ctx);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev, "Panel init sequence failed: %d\n",
			      ret);
		return ret;
	}

	return backlight_enable(ctx->backlight);
}

static int jh057n_disable(struct drm_panel *panel)
{
	struct jh057n *ctx = panel_to_jh057n(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	backlight_disable(ctx->backlight);
	return mipi_dsi_dcs_set_display_off(dsi);
}

static int jh057n_unprepare(struct drm_panel *panel)
{
	struct jh057n *ctx = panel_to_jh057n(panel);

	if (!ctx->prepared)
		return 0;

	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vcc);
	ctx->prepared = false;

	return 0;
}

static int jh057n_prepare(struct drm_panel *panel)
{
	struct jh057n *ctx = panel_to_jh057n(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	DRM_DEV_DEBUG_DRIVER(ctx->dev, "Resetting the panel\n");
	ret = regulator_enable(ctx->vcc);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to enable vcc supply: %d\n", ret);
		return ret;
	}
	ret = regulator_enable(ctx->iovcc);
	if (ret < 0) {
		DRM_DEV_ERROR(ctx->dev,
			      "Failed to enable iovcc supply: %d\n", ret);
		goto disable_vcc;
	}

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(20, 40);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);

	ctx->prepared = true;

	return 0;

disable_vcc:
	regulator_disable(ctx->vcc);
	return ret;
}

static const struct drm_display_mode default_mode = {
	.hdisplay    = 720,
	.hsync_start = 720 + 90,
	.hsync_end   = 720 + 90 + 20,
	.htotal	     = 720 + 90 + 20 + 20,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 20,
	.vsync_end   = 1440 + 20 + 4,
	.vtotal	     = 1440 + 20 + 4 + 12,
	.vrefresh    = 60,
	.clock	     = 75276,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 65,
	.height_mm   = 130,
};

static int jh057n_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct jh057n *ctx = panel_to_jh057n(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		DRM_DEV_ERROR(ctx->dev, "Failed to add mode %ux%u@%u\n",
			      default_mode.hdisplay, default_mode.vdisplay,
			      default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs jh057n_drm_funcs = {
	.disable   = jh057n_disable,
	.unprepare = jh057n_unprepare,
	.prepare   = jh057n_prepare,
	.enable	   = jh057n_enable,
	.get_modes = jh057n_get_modes,
};

static int allpixelson_set(void *data, u64 val)
{
	struct jh057n *ctx = data;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	DRM_DEV_DEBUG_DRIVER(ctx->dev, "Setting all pixels on\n");
	dsi_generic_write_seq(dsi, ST7703_CMD_ALL_PIXEL_ON);
	msleep(val * 1000);
	/* Reset the panel to get video back */
	drm_panel_disable(&ctx->panel);
	drm_panel_unprepare(&ctx->panel);
	drm_panel_prepare(&ctx->panel);
	drm_panel_enable(&ctx->panel);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(allpixelson_fops, NULL,
			allpixelson_set, "%llu\n");

static void jh057n_debugfs_init(struct jh057n *ctx)
{
	ctx->debugfs = debugfs_create_dir(DRV_NAME, NULL);

	debugfs_create_file("allpixelson", 0600, ctx->debugfs, ctx,
			    &allpixelson_fops);
}

static void jh057n_debugfs_remove(struct jh057n *ctx)
{
	debugfs_remove_recursive(ctx->debugfs);
	ctx->debugfs = NULL;
}

static int jh057n_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jh057n *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		DRM_DEV_ERROR(dev, "cannot get reset gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
		MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	ctx->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(ctx->backlight))
		return PTR_ERR(ctx->backlight);

	ctx->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(ctx->vcc)) {
		ret = PTR_ERR(ctx->vcc);
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev,
				      "Failed to request vcc regulator: %d\n",
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

	drm_panel_init(&ctx->panel, dev, &jh057n_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev,
			      "mipi_dsi_attach failed (%d). Is host ready?\n",
			      ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	DRM_DEV_INFO(dev, "%ux%u@%u %ubpp dsi %udl - ready\n",
		     default_mode.hdisplay, default_mode.vdisplay,
		     default_mode.vrefresh,
		     mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes);

	jh057n_debugfs_init(ctx);
	return 0;
}

static void jh057n_shutdown(struct mipi_dsi_device *dsi)
{
	struct jh057n *ctx = mipi_dsi_get_drvdata(dsi);
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

static int jh057n_remove(struct mipi_dsi_device *dsi)
{
	struct jh057n *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	jh057n_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(&dsi->dev, "Failed to detach from DSI host: %d\n",
			      ret);

	drm_panel_remove(&ctx->panel);

	jh057n_debugfs_remove(ctx);

	return 0;
}

static const struct of_device_id jh057n_of_match[] = {
	{ .compatible = "rocktech,jh057n00900" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jh057n_of_match);

static struct mipi_dsi_driver jh057n_driver = {
	.probe	= jh057n_probe,
	.remove = jh057n_remove,
	.shutdown = jh057n_shutdown,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = jh057n_of_match,
	},
};
module_mipi_dsi_driver(jh057n_driver);

MODULE_AUTHOR("Guido Günther <agx@sigxcpu.org>");
MODULE_DESCRIPTION("DRM driver for Rocktech JH057N00900 MIPI DSI panel");
MODULE_LICENSE("GPL v2");
