// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung S6D7AA0 MIPI-DSI TFT LCD controller drm_panel driver.
 *
 * Copyright (C) 2022 Artur Weber <aweber.kernel@gmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <video/mipi_display.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

/* Manufacturer command set */
#define MCS_BL_CTL		0xc3
#define MCS_OTP_RELOAD		0xd0
#define MCS_PASSWD1		0xf0
#define MCS_PASSWD2		0xf1
#define MCS_PASSWD3		0xfc

struct s6d7aa0 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	const struct s6d7aa0_panel_desc *desc;
};

struct s6d7aa0_panel_desc {
	unsigned int panel_type;
	int (*init_func)(struct s6d7aa0 *ctx);
	int (*off_func)(struct s6d7aa0 *ctx);
	const struct drm_display_mode *drm_mode;
	unsigned long mode_flags;
	u32 bus_flags;
	bool has_backlight;
	bool use_passwd3;
};

enum s6d7aa0_panels {
	S6D7AA0_PANEL_LSL080AL02,
	S6D7AA0_PANEL_LSL080AL03,
	S6D7AA0_PANEL_LTL101AT01,
};

static inline struct s6d7aa0 *panel_to_s6d7aa0(struct drm_panel *panel)
{
	return container_of(panel, struct s6d7aa0, panel);
}

static void s6d7aa0_reset(struct s6d7aa0 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(50);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(50);
}

static int s6d7aa0_lock(struct s6d7aa0 *ctx, bool lock)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	int ret = 0;

	if (lock) {
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD1, 0xa5, 0xa5);
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD2, 0xa5, 0xa5);
		if (ctx->desc->use_passwd3)
			mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD3, 0x5a, 0x5a);
	} else {
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD1, 0x5a, 0x5a);
		mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD2, 0x5a, 0x5a);
		if (ctx->desc->use_passwd3)
			mipi_dsi_dcs_write_seq(dsi, MCS_PASSWD3, 0xa5, 0xa5);
	}

	return ret;
}

static int s6d7aa0_on(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = ctx->desc->init_func(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int s6d7aa0_off(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ret = ctx->desc->off_func(ctx);
	if (ret < 0) {
		dev_err(dev, "Panel-specific off function failed: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	msleep(64);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	return 0;
}

static int s6d7aa0_prepare(struct drm_panel *panel)
{
	struct s6d7aa0 *ctx = panel_to_s6d7aa0(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	s6d7aa0_reset(ctx);

	ret = s6d7aa0_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int s6d7aa0_disable(struct drm_panel *panel)
{
	struct s6d7aa0 *ctx = panel_to_s6d7aa0(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = s6d7aa0_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	return 0;
}

static int s6d7aa0_unprepare(struct drm_panel *panel)
{
	struct s6d7aa0 *ctx = panel_to_s6d7aa0(panel);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

/* Backlight control code */

static int s6d7aa0_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static int s6d7aa0_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	return brightness & 0xff;
}

static const struct backlight_ops s6d7aa0_bl_ops = {
	.update_status = s6d7aa0_bl_update_status,
	.get_brightness = s6d7aa0_bl_get_brightness,
};

static struct backlight_device *
s6d7aa0_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &s6d7aa0_bl_ops, &props);
}

/* Initialization code and structures for LSL080AL02 panel */

static int s6d7aa0_lsl080al02_init(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	usleep_range(20000, 25000);

	ret = s6d7aa0_lock(ctx, false);
	if (ret < 0) {
		dev_err(dev, "Failed to unlock registers: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, MCS_OTP_RELOAD, 0x00, 0x10);
	usleep_range(1000, 1500);

	/* SEQ_B6_PARAM_8_R01 */
	mipi_dsi_dcs_write_seq(dsi, 0xb6, 0x10);

	/* BL_CTL_ON */
	mipi_dsi_dcs_write_seq(dsi, MCS_BL_CTL, 0x40, 0x00, 0x28);

	usleep_range(5000, 6000);

	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_ADDRESS_MODE, 0x04);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	msleep(120);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_ADDRESS_MODE, 0x00);

	ret = s6d7aa0_lock(ctx, true);
	if (ret < 0) {
		dev_err(dev, "Failed to lock registers: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int s6d7aa0_lsl080al02_off(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	/* BL_CTL_OFF */
	mipi_dsi_dcs_write_seq(dsi, MCS_BL_CTL, 0x40, 0x00, 0x20);

	return 0;
}

static const struct drm_display_mode s6d7aa0_lsl080al02_mode = {
	.clock = (800 + 16 + 4 + 140) * (1280 + 8 + 4 + 4) * 60 / 1000,
	.hdisplay = 800,
	.hsync_start = 800 + 16,
	.hsync_end = 800 + 16 + 4,
	.htotal = 800 + 16 + 4 + 140,
	.vdisplay = 1280,
	.vsync_start = 1280 + 8,
	.vsync_end = 1280 + 8 + 4,
	.vtotal = 1280 + 8 + 4 + 4,
	.width_mm = 108,
	.height_mm = 173,
};

static const struct s6d7aa0_panel_desc s6d7aa0_lsl080al02_desc = {
	.panel_type = S6D7AA0_PANEL_LSL080AL02,
	.init_func = s6d7aa0_lsl080al02_init,
	.off_func = s6d7aa0_lsl080al02_off,
	.drm_mode = &s6d7aa0_lsl080al02_mode,
	.mode_flags = MIPI_DSI_MODE_VSYNC_FLUSH | MIPI_DSI_MODE_VIDEO_NO_HFP,
	.bus_flags = DRM_BUS_FLAG_DE_HIGH,

	.has_backlight = false,
	.use_passwd3 = false,
};

/* Initialization code and structures for LSL080AL03 panel */

static int s6d7aa0_lsl080al03_init(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	usleep_range(20000, 25000);

	ret = s6d7aa0_lock(ctx, false);
	if (ret < 0) {
		dev_err(dev, "Failed to unlock registers: %d\n", ret);
		return ret;
	}

	if (ctx->desc->panel_type == S6D7AA0_PANEL_LSL080AL03) {
		mipi_dsi_dcs_write_seq(dsi, MCS_BL_CTL, 0xc7, 0x00, 0x29);
		mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x01, 0x4e, 0xa0);
		mipi_dsi_dcs_write_seq(dsi, 0xfd, 0x16, 0x10, 0x11, 0x23,
				       0x09);
		mipi_dsi_dcs_write_seq(dsi, 0xfe, 0x00, 0x02, 0x03, 0x21,
				       0x80, 0x78);
	} else if (ctx->desc->panel_type == S6D7AA0_PANEL_LTL101AT01) {
		mipi_dsi_dcs_write_seq(dsi, MCS_BL_CTL, 0x40, 0x00, 0x08);
		mipi_dsi_dcs_write_seq(dsi, 0xbc, 0x01, 0x4e, 0x0b);
		mipi_dsi_dcs_write_seq(dsi, 0xfd, 0x16, 0x10, 0x11, 0x23,
				       0x09);
		mipi_dsi_dcs_write_seq(dsi, 0xfe, 0x00, 0x02, 0x03, 0x21,
				       0x80, 0x68);
	}

	mipi_dsi_dcs_write_seq(dsi, 0xb3, 0x51);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x24);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x02, 0x08, 0x08);

	usleep_range(10000, 11000);

	mipi_dsi_dcs_write_seq(dsi, 0xc0, 0x80, 0x80, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xcd,
			       0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
			       0x2e, 0x2e, 0x2e, 0x2e, 0x2e);
	mipi_dsi_dcs_write_seq(dsi, 0xce,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xc1, 0x03);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	ret = s6d7aa0_lock(ctx, true);
	if (ret < 0) {
		dev_err(dev, "Failed to lock registers: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}

	return 0;
}

static int s6d7aa0_lsl080al03_off(struct s6d7aa0 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	mipi_dsi_dcs_write_seq(dsi, 0x22, 0x00);

	return 0;
}

static const struct drm_display_mode s6d7aa0_lsl080al03_mode = {
	.clock = (768 + 18 + 16 + 126) * (1024 + 8 + 2 + 6) * 60 / 1000,
	.hdisplay = 768,
	.hsync_start = 768 + 18,
	.hsync_end = 768 + 18 + 16,
	.htotal = 768 + 18 + 16 + 126,
	.vdisplay = 1024,
	.vsync_start = 1024 + 8,
	.vsync_end = 1024 + 8 + 2,
	.vtotal = 1024 + 8 + 2 + 6,
	.width_mm = 122,
	.height_mm = 163,
};

static const struct s6d7aa0_panel_desc s6d7aa0_lsl080al03_desc = {
	.panel_type = S6D7AA0_PANEL_LSL080AL03,
	.init_func = s6d7aa0_lsl080al03_init,
	.off_func = s6d7aa0_lsl080al03_off,
	.drm_mode = &s6d7aa0_lsl080al03_mode,
	.mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET,
	.bus_flags = 0,

	.has_backlight = true,
	.use_passwd3 = true,
};

/* Initialization structures for LTL101AT01 panel */

static const struct drm_display_mode s6d7aa0_ltl101at01_mode = {
	.clock = (768 + 96 + 16 + 184) * (1024 + 8 + 2 + 6) * 60 / 1000,
	.hdisplay = 768,
	.hsync_start = 768 + 96,
	.hsync_end = 768 + 96 + 16,
	.htotal = 768 + 96 + 16 + 184,
	.vdisplay = 1024,
	.vsync_start = 1024 + 8,
	.vsync_end = 1024 + 8 + 2,
	.vtotal = 1024 + 8 + 2 + 6,
	.width_mm = 148,
	.height_mm = 197,
};

static const struct s6d7aa0_panel_desc s6d7aa0_ltl101at01_desc = {
	.panel_type = S6D7AA0_PANEL_LTL101AT01,
	.init_func = s6d7aa0_lsl080al03_init, /* Similar init to LSL080AL03 */
	.off_func = s6d7aa0_lsl080al03_off,
	.drm_mode = &s6d7aa0_ltl101at01_mode,
	.mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET,
	.bus_flags = 0,

	.has_backlight = true,
	.use_passwd3 = true,
};

static int s6d7aa0_get_modes(struct drm_panel *panel,
					struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct s6d7aa0 *ctx;

	ctx = container_of(panel, struct s6d7aa0, panel);
	if (!ctx)
		return -EINVAL;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->drm_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags = ctx->desc->bus_flags;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs s6d7aa0_panel_funcs = {
	.disable = s6d7aa0_disable,
	.prepare = s6d7aa0_prepare,
	.unprepare = s6d7aa0_unprepare,
	.get_modes = s6d7aa0_get_modes,
};

static int s6d7aa0_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6d7aa0 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ctx->supplies[0].supply = "power";
	ctx->supplies[1].supply = "vmipi";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
					      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
		| ctx->desc->mode_flags;

	drm_panel_init(&ctx->panel, dev, &s6d7aa0_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	/* Use DSI-based backlight as fallback if available */
	if (ctx->desc->has_backlight && !ctx->panel.backlight) {
		ctx->panel.backlight = s6d7aa0_create_backlight(dsi);
		if (IS_ERR(ctx->panel.backlight))
			return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
					     "Failed to create backlight\n");
	}

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void s6d7aa0_remove(struct mipi_dsi_device *dsi)
{
	struct s6d7aa0 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id s6d7aa0_of_match[] = {
	{
		.compatible = "samsung,lsl080al02",
		.data = &s6d7aa0_lsl080al02_desc
	},
	{
		.compatible = "samsung,lsl080al03",
		.data = &s6d7aa0_lsl080al03_desc
	},
	{
		.compatible = "samsung,ltl101at01",
		.data = &s6d7aa0_ltl101at01_desc
	},
	{ /* sentinel */ }
};

static struct mipi_dsi_driver s6d7aa0_driver = {
	.probe = s6d7aa0_probe,
	.remove = s6d7aa0_remove,
	.driver = {
		.name = "panel-samsung-s6d7aa0",
		.of_match_table = s6d7aa0_of_match,
	},
};
module_mipi_dsi_driver(s6d7aa0_driver);

MODULE_AUTHOR("Artur Weber <aweber.kernel@gmail.com>");
MODULE_DESCRIPTION("Samsung S6D7AA0 MIPI-DSI LCD controller driver");
MODULE_LICENSE("GPL");
