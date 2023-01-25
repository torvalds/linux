// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021 Linaro Ltd.
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
 *   Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct sharp_ls060 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator *vddi_supply;
	struct regulator *vddh_supply;
	struct regulator *avdd_supply;
	struct regulator *avee_supply;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static inline struct sharp_ls060 *to_sharp_ls060(struct drm_panel *panel)
{
	return container_of(panel, struct sharp_ls060, panel);
}

static void sharp_ls060_reset(struct sharp_ls060 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int sharp_ls060_on(struct sharp_ls060 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq(dsi, 0xbb, 0x13);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_MEMORY_START);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(50);

	return 0;
}

static int sharp_ls060_off(struct sharp_ls060 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	usleep_range(2000, 3000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(121);

	return 0;
}

static int sharp_ls060_prepare(struct drm_panel *panel)
{
	struct sharp_ls060 *ctx = to_sharp_ls060(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_enable(ctx->vddi_supply);
	if (ret < 0)
		return ret;

	ret = regulator_enable(ctx->avdd_supply);
	if (ret < 0)
		goto err_avdd;

	usleep_range(1000, 2000);

	ret = regulator_enable(ctx->avee_supply);
	if (ret < 0)
		goto err_avee;

	usleep_range(10000, 11000);

	ret = regulator_enable(ctx->vddh_supply);
	if (ret < 0)
		goto err_vddh;

	usleep_range(10000, 11000);

	sharp_ls060_reset(ctx);

	ret = sharp_ls060_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		goto err_on;
	}

	ctx->prepared = true;

	return 0;

err_on:
	regulator_disable(ctx->vddh_supply);

	usleep_range(10000, 11000);

err_vddh:
	regulator_disable(ctx->avee_supply);

err_avee:
	regulator_disable(ctx->avdd_supply);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

err_avdd:
	regulator_disable(ctx->vddi_supply);

	return ret;
}

static int sharp_ls060_unprepare(struct drm_panel *panel)
{
	struct sharp_ls060 *ctx = to_sharp_ls060(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = sharp_ls060_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	regulator_disable(ctx->vddh_supply);

	usleep_range(10000, 11000);

	regulator_disable(ctx->avee_supply);
	regulator_disable(ctx->avdd_supply);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	regulator_disable(ctx->vddi_supply);

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode sharp_ls060_mode = {
	.clock = (1080 + 96 + 16 + 64) * (1920 + 4 + 1 + 16) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 96,
	.hsync_end = 1080 + 96 + 16,
	.htotal = 1080 + 96 + 16 + 64,
	.vdisplay = 1920,
	.vsync_start = 1920 + 4,
	.vsync_end = 1920 + 4 + 1,
	.vtotal = 1920 + 4 + 1 + 16,
	.width_mm = 75,
	.height_mm = 132,
};

static int sharp_ls060_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &sharp_ls060_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs sharp_ls060_panel_funcs = {
	.prepare = sharp_ls060_prepare,
	.unprepare = sharp_ls060_unprepare,
	.get_modes = sharp_ls060_get_modes,
};

static int sharp_ls060_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct sharp_ls060 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->vddi_supply = devm_regulator_get(dev, "vddi");
	if (IS_ERR(ctx->vddi_supply))
		return PTR_ERR(ctx->vddi_supply);

	ctx->vddh_supply = devm_regulator_get(dev, "vddh");
	if (IS_ERR(ctx->vddh_supply))
		return PTR_ERR(ctx->vddh_supply);

	ctx->avdd_supply = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ctx->avdd_supply))
		return PTR_ERR(ctx->avdd_supply);

	ctx->avee_supply = devm_regulator_get(dev, "avee");
	if (IS_ERR(ctx->avee_supply))
		return PTR_ERR(ctx->avee_supply);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &sharp_ls060_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void sharp_ls060_remove(struct mipi_dsi_device *dsi)
{
	struct sharp_ls060 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id sharp_ls060t1sx01_of_match[] = {
	{ .compatible = "sharp,ls060t1sx01" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sharp_ls060t1sx01_of_match);

static struct mipi_dsi_driver sharp_ls060_driver = {
	.probe = sharp_ls060_probe,
	.remove = sharp_ls060_remove,
	.driver = {
		.name = "panel-sharp-ls060t1sx01",
		.of_match_table = sharp_ls060t1sx01_of_match,
	},
};
module_mipi_dsi_driver(sharp_ls060_driver);

MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_DESCRIPTION("DRM driver for Sharp LS060T1SX01 1080p video mode dsi panel");
MODULE_LICENSE("GPL v2");
