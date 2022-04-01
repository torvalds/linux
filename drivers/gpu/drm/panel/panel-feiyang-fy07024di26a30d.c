// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Amarula Solutions
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#define FEIYANG_INIT_CMD_LEN	2

struct feiyang {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct regulator	*dvdd;
	struct regulator	*avdd;
	struct gpio_desc	*reset;
};

static inline struct feiyang *panel_to_feiyang(struct drm_panel *panel)
{
	return container_of(panel, struct feiyang, panel);
}

struct feiyang_init_cmd {
	u8 data[FEIYANG_INIT_CMD_LEN];
};

static const struct feiyang_init_cmd feiyang_init_cmds[] = {
	{ .data = { 0x80, 0x58 } },
	{ .data = { 0x81, 0x47 } },
	{ .data = { 0x82, 0xD4 } },
	{ .data = { 0x83, 0x88 } },
	{ .data = { 0x84, 0xA9 } },
	{ .data = { 0x85, 0xC3 } },
	{ .data = { 0x86, 0x82 } },
};

static int feiyang_prepare(struct drm_panel *panel)
{
	struct feiyang *ctx = panel_to_feiyang(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	unsigned int i;
	int ret;

	ret = regulator_enable(ctx->dvdd);
	if (ret)
		return ret;

	/* T1 (dvdd start + dvdd rise) 0 < T1 <= 10ms */
	msleep(10);

	ret = regulator_enable(ctx->avdd);
	if (ret)
		return ret;

	/* T3 (dvdd rise + avdd start + avdd rise) T3 >= 20ms */
	msleep(20);

	gpiod_set_value(ctx->reset, 0);

	/*
	 * T5 + T6 (avdd rise + video & logic signal rise)
	 * T5 >= 10ms, 0 < T6 <= 10ms
	 */
	msleep(20);

	gpiod_set_value(ctx->reset, 1);

	/* T12 (video & logic signal rise + backlight rise) T12 >= 200ms */
	msleep(200);

	for (i = 0; i < ARRAY_SIZE(feiyang_init_cmds); i++) {
		const struct feiyang_init_cmd *cmd =
						&feiyang_init_cmds[i];

		ret = mipi_dsi_dcs_write_buffer(dsi, cmd->data,
						FEIYANG_INIT_CMD_LEN);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int feiyang_enable(struct drm_panel *panel)
{
	struct feiyang *ctx = panel_to_feiyang(panel);

	/* T12 (video & logic signal rise + backlight rise) T12 >= 200ms */
	msleep(200);

	mipi_dsi_dcs_set_display_on(ctx->dsi);

	return 0;
}

static int feiyang_disable(struct drm_panel *panel)
{
	struct feiyang *ctx = panel_to_feiyang(panel);

	return mipi_dsi_dcs_set_display_off(ctx->dsi);
}

static int feiyang_unprepare(struct drm_panel *panel)
{
	struct feiyang *ctx = panel_to_feiyang(panel);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0)
		dev_err(panel->dev, "failed to enter sleep mode: %d\n", ret);

	/* T13 (backlight fall + video & logic signal fall) T13 >= 200ms */
	msleep(200);

	gpiod_set_value(ctx->reset, 0);

	regulator_disable(ctx->avdd);

	/* T11 (dvdd rise to fall) 0 < T11 <= 10ms  */
	msleep(10);

	regulator_disable(ctx->dvdd);

	return 0;
}

static const struct drm_display_mode feiyang_default_mode = {
	.clock		= 55000,

	.hdisplay	= 1024,
	.hsync_start	= 1024 + 310,
	.hsync_end	= 1024 + 310 + 20,
	.htotal		= 1024 + 310 + 20 + 90,

	.vdisplay	= 600,
	.vsync_start	= 600 + 12,
	.vsync_end	= 600 + 12 + 2,
	.vtotal		= 600 + 12 + 2 + 21,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int feiyang_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	struct feiyang *ctx = panel_to_feiyang(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &feiyang_default_mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%u@%u\n",
			feiyang_default_mode.hdisplay,
			feiyang_default_mode.vdisplay,
			drm_mode_vrefresh(&feiyang_default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs feiyang_funcs = {
	.disable = feiyang_disable,
	.unprepare = feiyang_unprepare,
	.prepare = feiyang_prepare,
	.enable = feiyang_enable,
	.get_modes = feiyang_get_modes,
};

static int feiyang_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct feiyang *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel, &dsi->dev, &feiyang_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->dvdd = devm_regulator_get(&dsi->dev, "dvdd");
	if (IS_ERR(ctx->dvdd))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->dvdd),
				     "Couldn't get dvdd regulator\n");

	ctx->avdd = devm_regulator_get(&dsi->dev, "avdd");
	if (IS_ERR(ctx->avdd))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->avdd),
				     "Couldn't get avdd regulator\n");

	ctx->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->reset),
				     "Couldn't get our reset GPIO\n");

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static int feiyang_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct feiyang *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id feiyang_of_match[] = {
	{ .compatible = "feiyang,fy07024di26a30d", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, feiyang_of_match);

static struct mipi_dsi_driver feiyang_driver = {
	.probe = feiyang_dsi_probe,
	.remove = feiyang_dsi_remove,
	.driver = {
		.name = "feiyang-fy07024di26a30d",
		.of_match_table = feiyang_of_match,
	},
};
module_mipi_dsi_driver(feiyang_driver);

MODULE_AUTHOR("Jagan Teki <jagan@amarulasolutions.com>");
MODULE_DESCRIPTION("Feiyang FY07024DI26A30-D MIPI-DSI LCD panel");
MODULE_LICENSE("GPL");
