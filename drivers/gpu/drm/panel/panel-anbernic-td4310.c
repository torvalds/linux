// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Anbernic panels with TD4310 panel controller.
 *
 * Copyright (C) 2026 Chris Morgan <macromorgan@hotmail.com>
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/mipi_display.h>

struct anbernic_panel_td4310_info {
	const struct drm_display_mode *display_mode;
	u16 width_mm;
	u16 height_mm;
	u32 bus_flags;
	unsigned long mode_flags;
	u32 format;
	u32 lanes;
	u16 prepare_delay;
	u16 reset_delay;
	u16 init_delay;
	u16 enable_delay;
	u16 disable_delay;
	u16 unprepare_delay;
};

struct anbernic_panel_td4310 {
	struct device *dev;
	struct mipi_dsi_device *dsi;
	struct drm_panel panel;
	const struct anbernic_panel_td4310_info *panel_info;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	struct regulator *vdd;
	enum drm_panel_orientation orientation;
};

static inline struct anbernic_panel_td4310 *panel_to_anbernic_panel_td4310(struct drm_panel *panel)
{
	return container_of(panel, struct anbernic_panel_td4310, panel);
}

static int panel_anbernic_td4310_prepare(struct drm_panel *panel)
{
	struct anbernic_panel_td4310 *ctx = panel_to_anbernic_panel_td4310(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	int ret;

	ret = regulator_enable(ctx->vdd);
	if (ret)
		return ret;

	ret = gpiod_set_value_cansleep(ctx->enable_gpio, 1);
	if (ret)
		goto err_enable;

	if (ctx->panel_info->enable_delay)
		mipi_dsi_msleep(&dsi_ctx, ctx->panel_info->enable_delay);

	ret = gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	if (ret)
		goto err_reset;

	mipi_dsi_msleep(&dsi_ctx, 10);

	ret = gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	if (ret)
		goto err_reset;

	if (ctx->panel_info->reset_delay)
		mipi_dsi_msleep(&dsi_ctx, ctx->panel_info->reset_delay);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, ctx->panel_info->prepare_delay);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, ctx->panel_info->prepare_delay);

	if (dsi_ctx.accum_err) {
		ret = dsi_ctx.accum_err;
		goto err_reset;
	}

	return 0;

err_reset:
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
err_enable:
	regulator_disable(ctx->vdd);
	return ret;
}

static int panel_anbernic_td4310_unprepare(struct drm_panel *panel)
{
	struct anbernic_panel_td4310 *ctx = panel_to_anbernic_panel_td4310(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, ctx->panel_info->unprepare_delay);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, ctx->panel_info->disable_delay);

	gpiod_set_value_cansleep(ctx->enable_gpio, 0);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_disable(ctx->vdd);

	return 0;
}

static int panel_anbernic_td4310_get_mode(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct anbernic_panel_td4310 *ctx = panel_to_anbernic_panel_td4310(panel);
	const struct anbernic_panel_td4310_info *panel_info = ctx->panel_info;

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = panel_info->width_mm;
	connector->display_info.height_mm = panel_info->height_mm;
	connector->display_info.bus_flags = panel_info->bus_flags;

	return drm_connector_helper_get_modes_fixed(connector, panel_info->display_mode);
}

static enum drm_panel_orientation panel_anbernic_td4310_get_orientation(struct drm_panel *panel)
{
	struct anbernic_panel_td4310 *ctx = panel_to_anbernic_panel_td4310(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs panel_anbernic_td4310_funcs = {
	.prepare = panel_anbernic_td4310_prepare,
	.unprepare = panel_anbernic_td4310_unprepare,
	.get_modes = panel_anbernic_td4310_get_mode,
	.get_orientation = panel_anbernic_td4310_get_orientation,
};

static int panel_anbernic_td4310_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct anbernic_panel_td4310 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct anbernic_panel_td4310, panel,
				   &panel_anbernic_td4310_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->dev = dev;

	ctx->panel_info = of_device_get_match_data(dev);
	if (!ctx->panel_info)
		return -EINVAL;

	ret = drm_of_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get panel orientation\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Cannot get reset gpio\n");

	ctx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "Cannot get enable gpio\n");

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(dev, PTR_ERR(ctx->vdd),
				     "Failed to request vdd regulator\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = ctx->panel_info->lanes;
	dsi->format = ctx->panel_info->format;
	dsi->mode_flags = ctx->panel_info->mode_flags;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	devm_drm_panel_add(dev, &ctx->panel);

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");

	return 0;
}

static const struct drm_display_mode anbernic_vitapro_mode = {
	.clock = 140020,
	.hdisplay = 1080,
	.hsync_start = 1080 + 50,
	.hsync_end = 1080 + 50 + 4,
	.htotal = 1080 + 50 + 4 + 50,
	.vdisplay = 1920,
	.vsync_start = 1920 + 15,
	.vsync_end = 1920 + 15 + 4,
	.vtotal = 1920 + 15 + 4 + 32,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static const struct anbernic_panel_td4310_info anbernic_vitapro_info = {
	.display_mode = &anbernic_vitapro_mode,
	.width_mm = 69,
	.height_mm = 121,
	.bus_flags = DRM_BUS_FLAG_DE_LOW | DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET |
		      MIPI_DSI_CLOCK_NON_CONTINUOUS,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.prepare_delay = 50,
	.reset_delay = 220,
	.enable_delay = 120,
	.disable_delay = 50,
	.unprepare_delay = 20,
};

static const struct of_device_id panel_anbernic_td4310_of_match[] = {
	{
		.compatible = "anbernic,panel-vita-pro",
		.data = &anbernic_vitapro_info,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, panel_anbernic_td4310_of_match);

static struct mipi_dsi_driver anbernic_panel_td4310_driver = {
	.driver = {
		.name = "panel-anbernic-td4310",
		.of_match_table = panel_anbernic_td4310_of_match,
	},
	.probe	= panel_anbernic_td4310_probe,
};
module_mipi_dsi_driver(anbernic_panel_td4310_driver);

MODULE_AUTHOR("Chris Morgan <macromorgan@hotmail.com>");
MODULE_DESCRIPTION("DRM driver for Anbernic TD4310 MIPI DSI panels");
MODULE_LICENSE("GPL");
