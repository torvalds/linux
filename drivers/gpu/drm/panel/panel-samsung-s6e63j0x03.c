// SPDX-License-Identifier: GPL-2.0-only
/*
 * MIPI-DSI based S6E63J0X03 AMOLED lcd 1.63 inch panel driver.
 *
 * Copyright (c) 2014-2017 Samsung Electronics Co., Ltd
 *
 * Inki Dae <inki.dae@samsung.com>
 * Hoegeun Kwon <hoegeun.kwon@samsung.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define MCS_LEVEL2_KEY		0xf0
#define MCS_MTP_KEY		0xf1
#define MCS_MTP_SET3		0xd4

#define MAX_BRIGHTNESS		100
#define DEFAULT_BRIGHTNESS	80

#define NUM_GAMMA_STEPS		9
#define GAMMA_CMD_CNT		28

#define FIRST_COLUMN 20

struct s6e63j0x03 {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *bl_dev;

	struct regulator_bulk_data supplies[2];
	struct gpio_desc *reset_gpio;
};

static const struct drm_display_mode default_mode = {
	.clock = 4649,
	.hdisplay = 320,
	.hsync_start = 320 + 1,
	.hsync_end = 320 + 1 + 1,
	.htotal = 320 + 1 + 1 + 1,
	.vdisplay = 320,
	.vsync_start = 320 + 150,
	.vsync_end = 320 + 150 + 1,
	.vtotal = 320 + 150 + 1 + 2,
	.flags = 0,
};

static const unsigned char gamma_tbl[NUM_GAMMA_STEPS][GAMMA_CMD_CNT] = {
	{	/* Gamma 10 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x7f, 0x7f, 0x7f, 0x52, 0x6b, 0x6f, 0x26,
		0x28, 0x2d, 0x28, 0x26, 0x27, 0x33, 0x34, 0x32, 0x36, 0x36,
		0x35, 0x00, 0xab, 0x00, 0xae, 0x00, 0xbf
	},
	{	/* gamma 30 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x70, 0x7f, 0x7f, 0x4e, 0x64, 0x69, 0x26,
		0x27, 0x2a, 0x28, 0x29, 0x27, 0x31, 0x32, 0x31, 0x35, 0x34,
		0x35, 0x00, 0xc4, 0x00, 0xca, 0x00, 0xdc
	},
	{	/* gamma 60 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x65, 0x7b, 0x7d, 0x5f, 0x67, 0x68, 0x2a,
		0x28, 0x29, 0x28, 0x2a, 0x27, 0x31, 0x2f, 0x30, 0x34, 0x33,
		0x34, 0x00, 0xd9, 0x00, 0xe4, 0x00, 0xf5
	},
	{	/* gamma 90 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x4d, 0x6f, 0x71, 0x67, 0x6a, 0x6c, 0x29,
		0x28, 0x28, 0x28, 0x29, 0x27, 0x30, 0x2e, 0x30, 0x32, 0x31,
		0x31, 0x00, 0xea, 0x00, 0xf6, 0x01, 0x09
	},
	{	/* gamma 120 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x3d, 0x66, 0x68, 0x69, 0x69, 0x69, 0x28,
		0x28, 0x27, 0x28, 0x28, 0x27, 0x30, 0x2e, 0x2f, 0x31, 0x31,
		0x30, 0x00, 0xf9, 0x01, 0x05, 0x01, 0x1b
	},
	{	/* gamma 150 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x31, 0x51, 0x53, 0x66, 0x66, 0x67, 0x28,
		0x29, 0x27, 0x28, 0x27, 0x27, 0x2e, 0x2d, 0x2e, 0x31, 0x31,
		0x30, 0x01, 0x04, 0x01, 0x11, 0x01, 0x29
	},
	{	/* gamma 200 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x2f, 0x4f, 0x51, 0x67, 0x65, 0x65, 0x29,
		0x2a, 0x28, 0x27, 0x25, 0x26, 0x2d, 0x2c, 0x2c, 0x30, 0x30,
		0x30, 0x01, 0x14, 0x01, 0x23, 0x01, 0x3b
	},
	{	/* gamma 240 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x2c, 0x4d, 0x50, 0x65, 0x63, 0x64, 0x2a,
		0x2c, 0x29, 0x26, 0x24, 0x25, 0x2c, 0x2b, 0x2b, 0x30, 0x30,
		0x30, 0x01, 0x1e, 0x01, 0x2f, 0x01, 0x47
	},
	{	/* gamma 300 */
		MCS_MTP_SET3,
		0x00, 0x00, 0x00, 0x38, 0x61, 0x64, 0x65, 0x63, 0x64, 0x28,
		0x2a, 0x27, 0x26, 0x23, 0x25, 0x2b, 0x2b, 0x2a, 0x30, 0x2f,
		0x30, 0x01, 0x2d, 0x01, 0x3f, 0x01, 0x57
	}
};

static inline struct s6e63j0x03 *panel_to_s6e63j0x03(struct drm_panel *panel)
{
	return container_of(panel, struct s6e63j0x03, panel);
}

static inline ssize_t s6e63j0x03_dcs_write_seq(struct s6e63j0x03 *ctx,
					const void *seq, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	return mipi_dsi_dcs_write_buffer(dsi, seq, len);
}

#define s6e63j0x03_dcs_write_seq_static(ctx, seq...)			\
	({								\
		static const u8 d[] = { seq };				\
		s6e63j0x03_dcs_write_seq(ctx, d, ARRAY_SIZE(d));	\
	})

static inline int s6e63j0x03_enable_lv2_command(struct s6e63j0x03 *ctx)
{
	return s6e63j0x03_dcs_write_seq_static(ctx, MCS_LEVEL2_KEY, 0x5a, 0x5a);
}

static inline int s6e63j0x03_apply_mtp_key(struct s6e63j0x03 *ctx, bool on)
{
	if (on)
		return s6e63j0x03_dcs_write_seq_static(ctx,
				MCS_MTP_KEY, 0x5a, 0x5a);

	return s6e63j0x03_dcs_write_seq_static(ctx, MCS_MTP_KEY, 0xa5, 0xa5);
}

static int s6e63j0x03_power_on(struct s6e63j0x03 *ctx)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0)
		return ret;

	msleep(30);

	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(5000, 6000);

	return 0;
}

static int s6e63j0x03_power_off(struct s6e63j0x03 *ctx)
{
	return regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
}

static unsigned int s6e63j0x03_get_brightness_index(unsigned int brightness)
{
	unsigned int index;

	index = brightness / (MAX_BRIGHTNESS / NUM_GAMMA_STEPS);

	if (index >= NUM_GAMMA_STEPS)
		index = NUM_GAMMA_STEPS - 1;

	return index;
}

static int s6e63j0x03_update_gamma(struct s6e63j0x03 *ctx,
					unsigned int brightness)
{
	struct backlight_device *bl_dev = ctx->bl_dev;
	unsigned int index = s6e63j0x03_get_brightness_index(brightness);
	int ret;

	ret = s6e63j0x03_apply_mtp_key(ctx, true);
	if (ret < 0)
		return ret;

	ret = s6e63j0x03_dcs_write_seq(ctx, gamma_tbl[index], GAMMA_CMD_CNT);
	if (ret < 0)
		return ret;

	ret = s6e63j0x03_apply_mtp_key(ctx, false);
	if (ret < 0)
		return ret;

	bl_dev->props.brightness = brightness;

	return 0;
}

static int s6e63j0x03_set_brightness(struct backlight_device *bl_dev)
{
	struct s6e63j0x03 *ctx = bl_get_data(bl_dev);
	unsigned int brightness = bl_dev->props.brightness;

	return s6e63j0x03_update_gamma(ctx, brightness);
}

static const struct backlight_ops s6e63j0x03_bl_ops = {
	.update_status = s6e63j0x03_set_brightness,
};

static int s6e63j0x03_disable(struct drm_panel *panel)
{
	struct s6e63j0x03 *ctx = panel_to_s6e63j0x03(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		return ret;

	ctx->bl_dev->props.power = FB_BLANK_NORMAL;

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	msleep(120);

	return 0;
}

static int s6e63j0x03_unprepare(struct drm_panel *panel)
{
	struct s6e63j0x03 *ctx = panel_to_s6e63j0x03(panel);
	int ret;

	ret = s6e63j0x03_power_off(ctx);
	if (ret < 0)
		return ret;

	ctx->bl_dev->props.power = FB_BLANK_POWERDOWN;

	return 0;
}

static int s6e63j0x03_panel_init(struct s6e63j0x03 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = s6e63j0x03_enable_lv2_command(ctx);
	if (ret < 0)
		return ret;

	ret = s6e63j0x03_apply_mtp_key(ctx, true);
	if (ret < 0)
		return ret;

	/* set porch adjustment */
	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xf2, 0x1c, 0x28);
	if (ret < 0)
		return ret;

	/* set frame freq */
	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xb5, 0x00, 0x02, 0x00);
	if (ret < 0)
		return ret;

	/* set caset, paset */
	ret = mipi_dsi_dcs_set_column_address(dsi, FIRST_COLUMN,
		default_mode.hdisplay - 1 + FIRST_COLUMN);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, default_mode.vdisplay - 1);
	if (ret < 0)
		return ret;

	/* set ltps timming 0, 1 */
	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xf8, 0x08, 0x08, 0x08, 0x17,
		0x00, 0x2a, 0x02, 0x26, 0x00, 0x00, 0x02, 0x00, 0x00);
	if (ret < 0)
		return ret;

	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xf7, 0x02);
	if (ret < 0)
		return ret;

	/* set param pos te_edge */
	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xb0, 0x01);
	if (ret < 0)
		return ret;

	/* set te rising edge */
	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xe2, 0x0f);
	if (ret < 0)
		return ret;

	/* set param pos default */
	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xb0, 0x00);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0)
		return ret;

	ret = s6e63j0x03_apply_mtp_key(ctx, false);
	if (ret < 0)
		return ret;

	return 0;
}

static int s6e63j0x03_prepare(struct drm_panel *panel)
{
	struct s6e63j0x03 *ctx = panel_to_s6e63j0x03(panel);
	int ret;

	ret = s6e63j0x03_power_on(ctx);
	if (ret < 0)
		return ret;

	ret = s6e63j0x03_panel_init(ctx);
	if (ret < 0)
		goto err;

	ctx->bl_dev->props.power = FB_BLANK_NORMAL;

	return 0;

err:
	s6e63j0x03_power_off(ctx);
	return ret;
}

static int s6e63j0x03_enable(struct drm_panel *panel)
{
	struct s6e63j0x03 *ctx = panel_to_s6e63j0x03(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	msleep(120);

	ret = s6e63j0x03_apply_mtp_key(ctx, true);
	if (ret < 0)
		return ret;

	/* set elvss_cond */
	ret = s6e63j0x03_dcs_write_seq_static(ctx, 0xb1, 0x00, 0x09);
	if (ret < 0)
		return ret;

	/* set pos */
	ret = s6e63j0x03_dcs_write_seq_static(ctx,
		MIPI_DCS_SET_ADDRESS_MODE, 0x40);
	if (ret < 0)
		return ret;

	/* set default white brightness */
	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x00ff);
	if (ret < 0)
		return ret;

	/* set white ctrl */
	ret = s6e63j0x03_dcs_write_seq_static(ctx,
		MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	if (ret < 0)
		return ret;

	/* set acl off */
	ret = s6e63j0x03_dcs_write_seq_static(ctx,
		MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0)
		return ret;

	ret = s6e63j0x03_apply_mtp_key(ctx, false);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		return ret;

	ctx->bl_dev->props.power = FB_BLANK_UNBLANK;

	return 0;
}

static int s6e63j0x03_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 29;
	connector->display_info.height_mm = 29;

	return 1;
}

static const struct drm_panel_funcs s6e63j0x03_funcs = {
	.disable = s6e63j0x03_disable,
	.unprepare = s6e63j0x03_unprepare,
	.prepare = s6e63j0x03_prepare,
	.enable = s6e63j0x03_enable,
	.get_modes = s6e63j0x03_get_modes,
};

static int s6e63j0x03_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct s6e63j0x03 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct s6e63j0x03), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	dsi->lanes = 1;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET;

	ctx->supplies[0].supply = "vdd3";
	ctx->supplies[1].supply = "vci";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "failed to get regulators: %d\n", ret);
		return ret;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "cannot get reset-gpio: %ld\n",
				PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}

	drm_panel_init(&ctx->panel, dev, &s6e63j0x03_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->bl_dev = backlight_device_register("s6e63j0x03", dev, ctx,
						&s6e63j0x03_bl_ops, NULL);
	if (IS_ERR(ctx->bl_dev)) {
		dev_err(dev, "failed to register backlight device\n");
		return PTR_ERR(ctx->bl_dev);
	}

	ctx->bl_dev->props.max_brightness = MAX_BRIGHTNESS;
	ctx->bl_dev->props.brightness = DEFAULT_BRIGHTNESS;
	ctx->bl_dev->props.power = FB_BLANK_POWERDOWN;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		goto remove_panel;

	return ret;

remove_panel:
	drm_panel_remove(&ctx->panel);
	backlight_device_unregister(ctx->bl_dev);

	return ret;
}

static int s6e63j0x03_remove(struct mipi_dsi_device *dsi)
{
	struct s6e63j0x03 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	backlight_device_unregister(ctx->bl_dev);

	return 0;
}

static const struct of_device_id s6e63j0x03_of_match[] = {
	{ .compatible = "samsung,s6e63j0x03" },
	{ }
};
MODULE_DEVICE_TABLE(of, s6e63j0x03_of_match);

static struct mipi_dsi_driver s6e63j0x03_driver = {
	.probe = s6e63j0x03_probe,
	.remove = s6e63j0x03_remove,
	.driver = {
		.name = "panel_samsung_s6e63j0x03",
		.of_match_table = s6e63j0x03_of_match,
	},
};
module_mipi_dsi_driver(s6e63j0x03_driver);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Hoegeun Kwon <hoegeun.kwon@samsung.com>");
MODULE_DESCRIPTION("MIPI-DSI based s6e63j0x03 AMOLED LCD Panel Driver");
MODULE_LICENSE("GPL v2");
