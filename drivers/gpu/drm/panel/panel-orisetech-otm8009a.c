/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */
#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <video/mipi_display.h>

#define DRV_NAME "orisetech_otm8009a"

#define OTM8009A_BACKLIGHT_DEFAULT	240
#define OTM8009A_BACKLIGHT_MAX		255

/* Manufacturer Command Set */
#define MCS_ADRSFT	0x0000	/* Address Shift Function */
#define MCS_PANSET	0xB3A6	/* Panel Type Setting */
#define MCS_SD_CTRL	0xC0A2	/* Source Driver Timing Setting */
#define MCS_P_DRV_M	0xC0B4	/* Panel Driving Mode */
#define MCS_OSC_ADJ	0xC181	/* Oscillator Adjustment for Idle/Normal mode */
#define MCS_RGB_VID_SET	0xC1A1	/* RGB Video Mode Setting */
#define MCS_SD_PCH_CTRL	0xC480	/* Source Driver Precharge Control */
#define MCS_NO_DOC1	0xC48A	/* Command not documented */
#define MCS_PWR_CTRL1	0xC580	/* Power Control Setting 1 */
#define MCS_PWR_CTRL2	0xC590	/* Power Control Setting 2 for Normal Mode */
#define MCS_PWR_CTRL4	0xC5B0	/* Power Control Setting 4 for DC Voltage */
#define MCS_PANCTRLSET1	0xCB80	/* Panel Control Setting 1 */
#define MCS_PANCTRLSET2	0xCB90	/* Panel Control Setting 2 */
#define MCS_PANCTRLSET3	0xCBA0	/* Panel Control Setting 3 */
#define MCS_PANCTRLSET4	0xCBB0	/* Panel Control Setting 4 */
#define MCS_PANCTRLSET5	0xCBC0	/* Panel Control Setting 5 */
#define MCS_PANCTRLSET6	0xCBD0	/* Panel Control Setting 6 */
#define MCS_PANCTRLSET7	0xCBE0	/* Panel Control Setting 7 */
#define MCS_PANCTRLSET8	0xCBF0	/* Panel Control Setting 8 */
#define MCS_PANU2D1	0xCC80	/* Panel U2D Setting 1 */
#define MCS_PANU2D2	0xCC90	/* Panel U2D Setting 2 */
#define MCS_PANU2D3	0xCCA0	/* Panel U2D Setting 3 */
#define MCS_PAND2U1	0xCCB0	/* Panel D2U Setting 1 */
#define MCS_PAND2U2	0xCCC0	/* Panel D2U Setting 2 */
#define MCS_PAND2U3	0xCCD0	/* Panel D2U Setting 3 */
#define MCS_GOAVST	0xCE80	/* GOA VST Setting */
#define MCS_GOACLKA1	0xCEA0	/* GOA CLKA1 Setting */
#define MCS_GOACLKA3	0xCEB0	/* GOA CLKA3 Setting */
#define MCS_GOAECLK	0xCFC0	/* GOA ECLK Setting */
#define MCS_NO_DOC2	0xCFD0	/* Command not documented */
#define MCS_GVDDSET	0xD800	/* GVDD/NGVDD */
#define MCS_VCOMDC	0xD900	/* VCOM Voltage Setting */
#define MCS_GMCT2_2P	0xE100	/* Gamma Correction 2.2+ Setting */
#define MCS_GMCT2_2N	0xE200	/* Gamma Correction 2.2- Setting */
#define MCS_NO_DOC3	0xF5B6	/* Command not documented */
#define MCS_CMD2_ENA1	0xFF00	/* Enable Access Command2 "CMD2" */
#define MCS_CMD2_ENA2	0xFF80	/* Enable Access Orise Command2 */

struct otm8009a {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *bl_dev;
	struct gpio_desc *reset_gpio;
	bool prepared;
	bool enabled;
};

static const struct drm_display_mode default_mode = {
	.clock = 32729,
	.hdisplay = 480,
	.hsync_start = 480 + 120,
	.hsync_end = 480 + 120 + 63,
	.htotal = 480 + 120 + 63 + 120,
	.vdisplay = 800,
	.vsync_start = 800 + 12,
	.vsync_end = 800 + 12 + 12,
	.vtotal = 800 + 12 + 12 + 12,
	.vrefresh = 50,
	.flags = 0,
	.width_mm = 52,
	.height_mm = 86,
};

static inline struct otm8009a *panel_to_otm8009a(struct drm_panel *panel)
{
	return container_of(panel, struct otm8009a, panel);
}

static void otm8009a_dcs_write_buf(struct otm8009a *ctx, const void *data,
				   size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	if (mipi_dsi_dcs_write_buffer(dsi, data, len) < 0)
		DRM_WARN("mipi dsi dcs write buffer failed\n");
}

#define dcs_write_seq(ctx, seq...)			\
({							\
	static const u8 d[] = { seq };			\
	otm8009a_dcs_write_buf(ctx, d, ARRAY_SIZE(d));	\
})

#define dcs_write_cmd_at(ctx, cmd, seq...)		\
({							\
	dcs_write_seq(ctx, MCS_ADRSFT, (cmd) & 0xFF);	\
	dcs_write_seq(ctx, (cmd) >> 8, seq);		\
})

static int otm8009a_init_sequence(struct otm8009a *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	/* Enter CMD2 */
	dcs_write_cmd_at(ctx, MCS_CMD2_ENA1, 0x80, 0x09, 0x01);

	/* Enter Orise Command2 */
	dcs_write_cmd_at(ctx, MCS_CMD2_ENA2, 0x80, 0x09);

	dcs_write_cmd_at(ctx, MCS_SD_PCH_CTRL, 0x30);
	mdelay(10);

	dcs_write_cmd_at(ctx, MCS_NO_DOC1, 0x40);
	mdelay(10);

	dcs_write_cmd_at(ctx, MCS_PWR_CTRL4 + 1, 0xA9);
	dcs_write_cmd_at(ctx, MCS_PWR_CTRL2 + 1, 0x34);
	dcs_write_cmd_at(ctx, MCS_P_DRV_M, 0x50);
	dcs_write_cmd_at(ctx, MCS_VCOMDC, 0x4E);
	dcs_write_cmd_at(ctx, MCS_OSC_ADJ, 0x66); /* 65Hz */
	dcs_write_cmd_at(ctx, MCS_PWR_CTRL2 + 2, 0x01);
	dcs_write_cmd_at(ctx, MCS_PWR_CTRL2 + 5, 0x34);
	dcs_write_cmd_at(ctx, MCS_PWR_CTRL2 + 4, 0x33);
	dcs_write_cmd_at(ctx, MCS_GVDDSET, 0x79, 0x79);
	dcs_write_cmd_at(ctx, MCS_SD_CTRL + 1, 0x1B);
	dcs_write_cmd_at(ctx, MCS_PWR_CTRL1 + 2, 0x83);
	dcs_write_cmd_at(ctx, MCS_SD_PCH_CTRL + 1, 0x83);
	dcs_write_cmd_at(ctx, MCS_RGB_VID_SET, 0x0E);
	dcs_write_cmd_at(ctx, MCS_PANSET, 0x00, 0x01);

	dcs_write_cmd_at(ctx, MCS_GOAVST, 0x85, 0x01, 0x00, 0x84, 0x01, 0x00);
	dcs_write_cmd_at(ctx, MCS_GOACLKA1, 0x18, 0x04, 0x03, 0x39, 0x00, 0x00,
			 0x00, 0x18, 0x03, 0x03, 0x3A, 0x00, 0x00, 0x00);
	dcs_write_cmd_at(ctx, MCS_GOACLKA3, 0x18, 0x02, 0x03, 0x3B, 0x00, 0x00,
			 0x00, 0x18, 0x01, 0x03, 0x3C, 0x00, 0x00, 0x00);
	dcs_write_cmd_at(ctx, MCS_GOAECLK, 0x01, 0x01, 0x20, 0x20, 0x00, 0x00,
			 0x01, 0x02, 0x00, 0x00);

	dcs_write_cmd_at(ctx, MCS_NO_DOC2, 0x00);

	dcs_write_cmd_at(ctx, MCS_PANCTRLSET1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	dcs_write_cmd_at(ctx, MCS_PANCTRLSET2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			 0, 0, 0, 0, 0);
	dcs_write_cmd_at(ctx, MCS_PANCTRLSET3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			 0, 0, 0, 0, 0);
	dcs_write_cmd_at(ctx, MCS_PANCTRLSET4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	dcs_write_cmd_at(ctx, MCS_PANCTRLSET5, 0, 4, 4, 4, 4, 4, 0, 0, 0, 0,
			 0, 0, 0, 0, 0);
	dcs_write_cmd_at(ctx, MCS_PANCTRLSET6, 0, 0, 0, 0, 0, 0, 4, 4, 4, 4,
			 4, 0, 0, 0, 0);
	dcs_write_cmd_at(ctx, MCS_PANCTRLSET7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	dcs_write_cmd_at(ctx, MCS_PANCTRLSET8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

	dcs_write_cmd_at(ctx, MCS_PANU2D1, 0x00, 0x26, 0x09, 0x0B, 0x01, 0x25,
			 0x00, 0x00, 0x00, 0x00);
	dcs_write_cmd_at(ctx, MCS_PANU2D2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x26, 0x0A, 0x0C, 0x02);
	dcs_write_cmd_at(ctx, MCS_PANU2D3, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	dcs_write_cmd_at(ctx, MCS_PAND2U1, 0x00, 0x25, 0x0C, 0x0A, 0x02, 0x26,
			 0x00, 0x00, 0x00, 0x00);
	dcs_write_cmd_at(ctx, MCS_PAND2U2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x25, 0x0B, 0x09, 0x01);
	dcs_write_cmd_at(ctx, MCS_PAND2U3, 0x26, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

	dcs_write_cmd_at(ctx, MCS_PWR_CTRL1 + 1, 0x66);

	dcs_write_cmd_at(ctx, MCS_NO_DOC3, 0x06);

	dcs_write_cmd_at(ctx, MCS_GMCT2_2P, 0x00, 0x09, 0x0F, 0x0E, 0x07, 0x10,
			 0x0B, 0x0A, 0x04, 0x07, 0x0B, 0x08, 0x0F, 0x10, 0x0A,
			 0x01);
	dcs_write_cmd_at(ctx, MCS_GMCT2_2N, 0x00, 0x09, 0x0F, 0x0E, 0x07, 0x10,
			 0x0B, 0x0A, 0x04, 0x07, 0x0B, 0x08, 0x0F, 0x10, 0x0A,
			 0x01);

	/* Exit CMD2 */
	dcs_write_cmd_at(ctx, MCS_CMD2_ENA1, 0xFF, 0xFF, 0xFF);

	ret = mipi_dsi_dcs_nop(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret)
		return ret;

	/* Wait for sleep out exit */
	mdelay(120);

	/* Default portrait 480x800 rgb24 */
	dcs_write_seq(ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x00);

	ret = mipi_dsi_dcs_set_column_address(dsi, 0,
					      default_mode.hdisplay - 1);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, default_mode.vdisplay - 1);
	if (ret)
		return ret;

	/* See otm8009a driver documentation for pixel format descriptions */
	ret = mipi_dsi_dcs_set_pixel_format(dsi, MIPI_DCS_PIXEL_FMT_24BIT |
					    MIPI_DCS_PIXEL_FMT_24BIT << 4);
	if (ret)
		return ret;

	/* Disable CABC feature */
	dcs_write_seq(ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_nop(dsi);
	if (ret)
		return ret;

	/* Send Command GRAM memory write (no parameters) */
	dcs_write_seq(ctx, MIPI_DCS_WRITE_MEMORY_START);

	return 0;
}

static int otm8009a_disable(struct drm_panel *panel)
{
	struct otm8009a *ctx = panel_to_otm8009a(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	if (!ctx->enabled)
		return 0; /* This is not an issue so we return 0 here */

	/* Power off the backlight. Note: end-user still controls brightness */
	ctx->bl_dev->props.power = FB_BLANK_POWERDOWN;
	ret = backlight_update_status(ctx->bl_dev);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret)
		return ret;

	msleep(120);

	ctx->enabled = false;

	return 0;
}

static int otm8009a_unprepare(struct drm_panel *panel)
{
	struct otm8009a *ctx = panel_to_otm8009a(panel);

	if (!ctx->prepared)
		return 0;

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
	}

	ctx->prepared = false;

	return 0;
}

static int otm8009a_prepare(struct drm_panel *panel)
{
	struct otm8009a *ctx = panel_to_otm8009a(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(20);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(100);
	}

	ret = otm8009a_init_sequence(ctx);
	if (ret)
		return ret;

	ctx->prepared = true;

	/*
	 * Power on the backlight. Note: end-user still controls brightness
	 * Note: ctx->prepared must be true before updating the backlight.
	 */
	ctx->bl_dev->props.power = FB_BLANK_UNBLANK;
	backlight_update_status(ctx->bl_dev);

	return 0;
}

static int otm8009a_enable(struct drm_panel *panel)
{
	struct otm8009a *ctx = panel_to_otm8009a(panel);

	ctx->enabled = true;

	return 0;
}

static int otm8009a_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		DRM_ERROR("failed to add mode %ux%ux@%u\n",
			  default_mode.hdisplay, default_mode.vdisplay,
			  default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = mode->width_mm;
	panel->connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static const struct drm_panel_funcs otm8009a_drm_funcs = {
	.disable   = otm8009a_disable,
	.unprepare = otm8009a_unprepare,
	.prepare   = otm8009a_prepare,
	.enable    = otm8009a_enable,
	.get_modes = otm8009a_get_modes,
};

/*
 * DSI-BASED BACKLIGHT
 */

static int otm8009a_backlight_update_status(struct backlight_device *bd)
{
	struct otm8009a *ctx = bl_get_data(bd);
	u8 data[2];

	if (!ctx->prepared) {
		DRM_DEBUG("lcd not ready yet for setting its backlight!\n");
		return -ENXIO;
	}

	if (bd->props.power <= FB_BLANK_NORMAL) {
		/* Power on the backlight with the requested brightness
		 * Note We can not use mipi_dsi_dcs_set_display_brightness()
		 * as otm8009a driver support only 8-bit brightness (1 param).
		 */
		data[0] = MIPI_DCS_SET_DISPLAY_BRIGHTNESS;
		data[1] = bd->props.brightness;
		otm8009a_dcs_write_buf(ctx, data, ARRAY_SIZE(data));

		/* set Brightness Control & Backlight on */
		data[1] = 0x24;

	} else {
		/* Power off the backlight: set Brightness Control & Bl off */
		data[1] = 0;
	}

	/* Update Brightness Control & Backlight */
	data[0] = MIPI_DCS_WRITE_CONTROL_DISPLAY;
	otm8009a_dcs_write_buf(ctx, data, ARRAY_SIZE(data));

	return 0;
}

static const struct backlight_ops otm8009a_backlight_ops = {
	.update_status = otm8009a_backlight_update_status,
};

static int otm8009a_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct otm8009a *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpio\n");
		return PTR_ERR(ctx->reset_gpio);
	}

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &otm8009a_drm_funcs;

	ctx->bl_dev = backlight_device_register(DRV_NAME "_backlight", dev, ctx,
						&otm8009a_backlight_ops, NULL);
	if (IS_ERR(ctx->bl_dev)) {
		dev_err(dev, "failed to register backlight device\n");
		return PTR_ERR(ctx->bl_dev);
	}

	ctx->bl_dev->props.max_brightness = OTM8009A_BACKLIGHT_MAX;
	ctx->bl_dev->props.brightness = OTM8009A_BACKLIGHT_DEFAULT;
	ctx->bl_dev->props.power = FB_BLANK_POWERDOWN;
	ctx->bl_dev->props.type = BACKLIGHT_RAW;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed. Is host ready?\n");
		drm_panel_remove(&ctx->panel);
		backlight_device_unregister(ctx->bl_dev);
		return ret;
	}

	DRM_INFO(DRV_NAME "_panel %ux%u@%u %ubpp dsi %udl - ready\n",
		 default_mode.hdisplay, default_mode.vdisplay,
		 default_mode.vrefresh,
		 mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes);

	return 0;
}

static int otm8009a_remove(struct mipi_dsi_device *dsi)
{
	struct otm8009a *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	backlight_device_unregister(ctx->bl_dev);

	return 0;
}

static const struct of_device_id orisetech_otm8009a_of_match[] = {
	{ .compatible = "orisetech,otm8009a" },
	{ }
};
MODULE_DEVICE_TABLE(of, orisetech_otm8009a_of_match);

static struct mipi_dsi_driver orisetech_otm8009a_driver = {
	.probe  = otm8009a_probe,
	.remove = otm8009a_remove,
	.driver = {
		.name = DRV_NAME "_panel",
		.of_match_table = orisetech_otm8009a_of_match,
	},
};
module_mipi_dsi_driver(orisetech_otm8009a_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@st.com>");
MODULE_DESCRIPTION("DRM driver for Orise Tech OTM8009A MIPI DSI panel");
MODULE_LICENSE("GPL v2");
