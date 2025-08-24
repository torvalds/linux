// SPDX-License-Identifier: GPL-2.0+
/*
 * MIPI-DSI Novatek NT35560-based panel controller.
 *
 * Supported panels include:
 * Sony ACX424AKM - a 480x854 AMOLED DSI panel
 * Sony ACX424AKP - a 480x864 AMOLED DSI panel
 *
 * Copyright (C) Linaro Ltd. 2019-2021
 * Author: Linus Walleij
 * Based on code and know-how from Marcus Lorentzon
 * Copyright (C) ST-Ericsson SA 2010
 * Based on code and know-how from Johan Olson and Joakim Wesslen
 * Copyright (C) Sony Ericsson Mobile Communications 2010
 */
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define NT35560_DCS_READ_ID1		0xDA
#define NT35560_DCS_READ_ID2		0xDB
#define NT35560_DCS_READ_ID3		0xDC
#define NT35560_DCS_SET_MDDI		0xAE

/*
 * Sony seems to use vendor ID 0x81
 */
#define DISPLAY_SONY_ACX424AKP_ID1	0x8103
#define DISPLAY_SONY_ACX424AKP_ID2	0x811a
#define DISPLAY_SONY_ACX424AKP_ID3	0x811b
/*
 * The fourth ID looks like a bug, vendor IDs begin at 0x80
 * and panel 00 ... seems like default values.
 */
#define DISPLAY_SONY_ACX424AKP_ID4	0x8000

struct nt35560_config {
	const struct drm_display_mode *vid_mode;
	const struct drm_display_mode *cmd_mode;
};

struct nt35560 {
	const struct nt35560_config *conf;
	struct drm_panel panel;
	struct device *dev;
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
	bool video_mode;
};

static const struct drm_display_mode sony_acx424akp_vid_mode = {
	.clock = 27234,
	.hdisplay = 480,
	.hsync_start = 480 + 15,
	.hsync_end = 480 + 15 + 0,
	.htotal = 480 + 15 + 0 + 15,
	.vdisplay = 864,
	.vsync_start = 864 + 14,
	.vsync_end = 864 + 14 + 1,
	.vtotal = 864 + 14 + 1 + 11,
	.width_mm = 48,
	.height_mm = 84,
	.flags = DRM_MODE_FLAG_PVSYNC,
};

/*
 * The timings are not very helpful as the display is used in
 * command mode using the maximum HS frequency.
 */
static const struct drm_display_mode sony_acx424akp_cmd_mode = {
	.clock = 35478,
	.hdisplay = 480,
	.hsync_start = 480 + 154,
	.hsync_end = 480 + 154 + 16,
	.htotal = 480 + 154 + 16 + 32,
	.vdisplay = 864,
	.vsync_start = 864 + 1,
	.vsync_end = 864 + 1 + 1,
	.vtotal = 864 + 1 + 1 + 1,
	/*
	 * Some desired refresh rate, experiments at the maximum "pixel"
	 * clock speed (HS clock 420 MHz) yields around 117Hz.
	 */
	.width_mm = 48,
	.height_mm = 84,
};

static const struct nt35560_config sony_acx424akp_data = {
	.vid_mode = &sony_acx424akp_vid_mode,
	.cmd_mode = &sony_acx424akp_cmd_mode,
};

static const struct drm_display_mode sony_acx424akm_vid_mode = {
	.clock = 27234,
	.hdisplay = 480,
	.hsync_start = 480 + 15,
	.hsync_end = 480 + 15 + 0,
	.htotal = 480 + 15 + 0 + 15,
	.vdisplay = 854,
	.vsync_start = 854 + 14,
	.vsync_end = 854 + 14 + 1,
	.vtotal = 854 + 14 + 1 + 11,
	.width_mm = 46,
	.height_mm = 82,
	.flags = DRM_MODE_FLAG_PVSYNC,
};

/*
 * The timings are not very helpful as the display is used in
 * command mode using the maximum HS frequency.
 */
static const struct drm_display_mode sony_acx424akm_cmd_mode = {
	.clock = 35478,
	.hdisplay = 480,
	.hsync_start = 480 + 154,
	.hsync_end = 480 + 154 + 16,
	.htotal = 480 + 154 + 16 + 32,
	.vdisplay = 854,
	.vsync_start = 854 + 1,
	.vsync_end = 854 + 1 + 1,
	.vtotal = 854 + 1 + 1 + 1,
	.width_mm = 46,
	.height_mm = 82,
};

static const struct nt35560_config sony_acx424akm_data = {
	.vid_mode = &sony_acx424akm_vid_mode,
	.cmd_mode = &sony_acx424akm_cmd_mode,
};

static inline struct nt35560 *panel_to_nt35560(struct drm_panel *panel)
{
	return container_of(panel, struct nt35560, panel);
}

#define FOSC			20 /* 20Mhz */
#define SCALE_FACTOR_NS_DIV_MHZ	1000

static int nt35560_set_brightness(struct backlight_device *bl)
{
	struct nt35560 *nt = bl_get_data(bl);
	struct mipi_dsi_multi_context dsi_ctx = {
		.dsi = to_mipi_dsi_device(nt->dev)
	};
	int duty_ns = bl->props.brightness;
	int period_ns = 1023;
	u8 pwm_ratio;
	u8 pwm_div;

	if (backlight_is_blank(bl)) {
		/* Disable backlight */
		mipi_dsi_dcs_write_seq_multi(&dsi_ctx,
					     MIPI_DCS_WRITE_CONTROL_DISPLAY,
					     0x00);
		return dsi_ctx.accum_err;
	}

	/* Calculate the PWM duty cycle in n/256's */
	pwm_ratio = max(((duty_ns * 256) / period_ns) - 1, 1);
	pwm_div = max(1,
		      ((FOSC * period_ns) / 256) /
		      SCALE_FACTOR_NS_DIV_MHZ);

	/* Set up PWM dutycycle ONE byte (differs from the standard) */
	dev_dbg(nt->dev, "calculated duty cycle %02x\n", pwm_ratio);

	/*
	 * Sequence to write PWMDIV:
	 *	address		data
	 *	0xF3		0xAA   CMD2 Unlock
	 *	0x00		0x01   Enter CMD2 page 0
	 *	0X7D		0x01   No reload MTP of CMD2 P1
	 *	0x22		PWMDIV
	 *	0x7F		0xAA   CMD2 page 1 lock
	 */
	mipi_dsi_dcs_write_var_seq_multi(&dsi_ctx,
					 MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
					 pwm_ratio);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf3, 0xaa);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x01);

	mipi_dsi_dcs_write_var_seq_multi(&dsi_ctx, 0x22, pwm_div);

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7f, 0xaa);

	/* Enable backlight */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);

	return dsi_ctx.accum_err;
}

static const struct backlight_ops nt35560_bl_ops = {
	.update_status = nt35560_set_brightness,
};

static const struct backlight_properties nt35560_bl_props = {
	.type = BACKLIGHT_RAW,
	.brightness = 512,
	.max_brightness = 1023,
};

static void nt35560_read_id(struct mipi_dsi_multi_context *dsi_ctx)
{
	struct device dev = dsi_ctx->dsi->dev;
	u8 vendor, version, panel;
	u16 val;

	mipi_dsi_dcs_read_multi(dsi_ctx, NT35560_DCS_READ_ID1, &vendor, 1);
	mipi_dsi_dcs_read_multi(dsi_ctx, NT35560_DCS_READ_ID2, &version, 1);
	mipi_dsi_dcs_read_multi(dsi_ctx, NT35560_DCS_READ_ID3, &panel, 1);

	if (dsi_ctx->accum_err < 0)
		return;

	if (vendor == 0x00) {
		dev_err(&dev, "device vendor ID is zero\n");
		dsi_ctx->accum_err = -ENODEV;
		return;
	}

	val = (vendor << 8) | panel;
	switch (val) {
	case DISPLAY_SONY_ACX424AKP_ID1:
	case DISPLAY_SONY_ACX424AKP_ID2:
	case DISPLAY_SONY_ACX424AKP_ID3:
	case DISPLAY_SONY_ACX424AKP_ID4:
		dev_info(&dev,
			 "MTP vendor: %02x, version: %02x, panel: %02x\n",
			 vendor, version, panel);
		break;
	default:
		dev_info(&dev,
			 "unknown vendor: %02x, version: %02x, panel: %02x\n",
			 vendor, version, panel);
		break;
	}
}

static int nt35560_power_on(struct nt35560 *nt)
{
	int ret;

	ret = regulator_enable(nt->supply);
	if (ret) {
		dev_err(nt->dev, "failed to enable supply (%d)\n", ret);
		return ret;
	}

	/* Assert RESET */
	gpiod_set_value_cansleep(nt->reset_gpio, 1);
	udelay(20);
	/* De-assert RESET */
	gpiod_set_value_cansleep(nt->reset_gpio, 0);
	usleep_range(11000, 20000);

	return 0;
}

static void nt35560_power_off(struct nt35560 *nt)
{
	/* Assert RESET */
	gpiod_set_value_cansleep(nt->reset_gpio, 1);
	usleep_range(11000, 20000);

	regulator_disable(nt->supply);
}

static int nt35560_prepare(struct drm_panel *panel)
{
	struct nt35560 *nt = panel_to_nt35560(panel);
	struct mipi_dsi_multi_context dsi_ctx = {
		.dsi = to_mipi_dsi_device(nt->dev)
	};
	int ret;

	ret = nt35560_power_on(nt);
	if (ret)
		return ret;

	nt35560_read_id(&dsi_ctx);

	/* Enable tearing mode: send TE (tearing effect) at VBLANK */
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx,
				       MIPI_DSI_DCS_TEAR_MODE_VBLANK);

	/*
	 * Set MDDI
	 *
	 * This presumably deactivates the Qualcomm MDDI interface and
	 * selects DSI, similar code is found in other drivers such as the
	 * Sharp LS043T1LE01.
	 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, NT35560_DCS_SET_MDDI, 3);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 140);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	if (nt->video_mode) {
		mipi_dsi_turn_on_peripheral_multi(&dsi_ctx);
	}

	if (dsi_ctx.accum_err < 0)
		nt35560_power_off(nt);
	return dsi_ctx.accum_err;
}

static int nt35560_unprepare(struct drm_panel *panel)
{
	struct nt35560 *nt = panel_to_nt35560(panel);
	struct mipi_dsi_multi_context dsi_ctx = {
		.dsi = to_mipi_dsi_device(nt->dev)
	};

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	if (dsi_ctx.accum_err < 0)
		return dsi_ctx.accum_err;

	msleep(85);

	nt35560_power_off(nt);

	return 0;
}


static int nt35560_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct nt35560 *nt = panel_to_nt35560(panel);
	const struct nt35560_config *conf = nt->conf;
	struct drm_display_mode *mode;

	if (nt->video_mode)
		mode = drm_mode_duplicate(connector->dev,
					  conf->vid_mode);
	else
		mode = drm_mode_duplicate(connector->dev,
					  conf->cmd_mode);
	if (!mode) {
		dev_err(panel->dev, "bad mode or failed to add mode\n");
		return -EINVAL;
	}
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	drm_mode_probed_add(connector, mode);

	return 1; /* Number of modes */
}

static const struct drm_panel_funcs nt35560_drm_funcs = {
	.unprepare = nt35560_unprepare,
	.prepare = nt35560_prepare,
	.get_modes = nt35560_get_modes,
};

static int nt35560_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct nt35560 *nt;
	int ret;

	nt = devm_drm_panel_alloc(dev, struct nt35560, panel,
				  &nt35560_drm_funcs,
				  DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(nt))
		return PTR_ERR(nt);

	nt->video_mode = of_property_read_bool(dev->of_node,
						"enforce-video-mode");

	mipi_dsi_set_drvdata(dsi, nt);
	nt->dev = dev;

	nt->conf = of_device_get_match_data(dev);
	if (!nt->conf) {
		dev_err(dev, "missing device configuration\n");
		return -ENODEV;
	}

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	/*
	 * FIXME: these come from the ST-Ericsson vendor driver for the
	 * HREF520 and seems to reflect limitations in the PLLs on that
	 * platform, if you have the datasheet, please cross-check the
	 * actual max rates.
	 */
	dsi->lp_rate = 19200000;
	dsi->hs_rate = 420160000;

	if (nt->video_mode)
		/* Burst mode using event for sync */
		dsi->mode_flags =
			MIPI_DSI_MODE_VIDEO |
			MIPI_DSI_MODE_VIDEO_BURST;
	else
		dsi->mode_flags =
			MIPI_DSI_CLOCK_NON_CONTINUOUS;

	nt->supply = devm_regulator_get(dev, "vddi");
	if (IS_ERR(nt->supply))
		return PTR_ERR(nt->supply);

	/* This asserts RESET by default */
	nt->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(nt->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(nt->reset_gpio),
				     "failed to request GPIO\n");

	nt->panel.backlight = devm_backlight_device_register(dev, "nt35560", dev, nt,
					&nt35560_bl_ops, &nt35560_bl_props);
	if (IS_ERR(nt->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(nt->panel.backlight),
				     "failed to register backlight device\n");

	drm_panel_add(&nt->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&nt->panel);
		return ret;
	}

	return 0;
}

static void nt35560_remove(struct mipi_dsi_device *dsi)
{
	struct nt35560 *nt = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&nt->panel);
}

static const struct of_device_id nt35560_of_match[] = {
	{
		.compatible = "sony,acx424akp",
		.data = &sony_acx424akp_data,
	},
	{
		.compatible = "sony,acx424akm",
		.data = &sony_acx424akm_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nt35560_of_match);

static struct mipi_dsi_driver nt35560_driver = {
	.probe = nt35560_probe,
	.remove = nt35560_remove,
	.driver = {
		.name = "panel-novatek-nt35560",
		.of_match_table = nt35560_of_match,
	},
};
module_mipi_dsi_driver(nt35560_driver);

MODULE_AUTHOR("Linus Wallei <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("MIPI-DSI Novatek NT35560 Panel Driver");
MODULE_LICENSE("GPL v2");
