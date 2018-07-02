/*
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/backlight.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct panel_desc {
	const struct drm_display_mode *mode;
	unsigned int bpc;
	struct {
		unsigned int width;
		unsigned int height;
	} size;

	unsigned long flags;
	enum mipi_dsi_pixel_format format;
	unsigned int lanes;
	const char * const *supply_names;
	unsigned int num_supplies;
	unsigned int sleep_mode_delay;
	unsigned int power_down_delay;
};

struct innolux_panel {
	struct drm_panel base;
	struct mipi_dsi_device *link;
	const struct panel_desc *desc;

	struct backlight_device *backlight;
	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;
	struct gpio_desc *enable_gpio;

	bool prepared;
	bool enabled;
};

static inline struct innolux_panel *to_innolux_panel(struct drm_panel *panel)
{
	return container_of(panel, struct innolux_panel, base);
}

static int innolux_panel_disable(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int err;

	if (!innolux->enabled)
		return 0;

	backlight_disable(innolux->backlight);

	err = mipi_dsi_dcs_set_display_off(innolux->link);
	if (err < 0)
		DRM_DEV_ERROR(panel->dev, "failed to set display off: %d\n",
			      err);

	innolux->enabled = false;

	return 0;
}

static int innolux_panel_unprepare(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int err;

	if (!innolux->prepared)
		return 0;

	err = mipi_dsi_dcs_enter_sleep_mode(innolux->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to enter sleep mode: %d\n",
			      err);
		return err;
	}

	if (innolux->desc->sleep_mode_delay)
		msleep(innolux->desc->sleep_mode_delay);

	gpiod_set_value_cansleep(innolux->enable_gpio, 0);

	if (innolux->desc->power_down_delay)
		msleep(innolux->desc->power_down_delay);

	err = regulator_bulk_disable(innolux->desc->num_supplies,
				     innolux->supplies);
	if (err < 0)
		return err;

	innolux->prepared = false;

	return 0;
}

static int innolux_panel_prepare(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int err;

	if (innolux->prepared)
		return 0;

	gpiod_set_value_cansleep(innolux->enable_gpio, 0);

	err = regulator_bulk_enable(innolux->desc->num_supplies,
				    innolux->supplies);
	if (err < 0)
		return err;

	/* T2: 15ms - 1000ms */
	usleep_range(15000, 16000);

	gpiod_set_value_cansleep(innolux->enable_gpio, 1);

	/* T4: 15ms - 1000ms */
	usleep_range(15000, 16000);

	err = mipi_dsi_dcs_exit_sleep_mode(innolux->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to exit sleep mode: %d\n",
			      err);
		goto poweroff;
	}

	/* T6: 120ms - 1000ms*/
	msleep(120);

	err = mipi_dsi_dcs_set_display_on(innolux->link);
	if (err < 0) {
		DRM_DEV_ERROR(panel->dev, "failed to set display on: %d\n",
			      err);
		goto poweroff;
	}

	/* T7: 5ms */
	usleep_range(5000, 6000);

	innolux->prepared = true;

	return 0;

poweroff:
	gpiod_set_value_cansleep(innolux->enable_gpio, 0);
	regulator_bulk_disable(innolux->desc->num_supplies, innolux->supplies);

	return err;
}

static int innolux_panel_enable(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	int ret;

	if (innolux->enabled)
		return 0;

	ret = backlight_enable(innolux->backlight);
	if (ret) {
		DRM_DEV_ERROR(panel->drm->dev,
			      "Failed to enable backlight %d\n", ret);
		return ret;
	}

	innolux->enabled = true;

	return 0;
}

static const char * const innolux_p079zca_supply_names[] = {
	"power",
};

static const struct drm_display_mode innolux_p079zca_mode = {
	.clock = 56900,
	.hdisplay = 768,
	.hsync_start = 768 + 40,
	.hsync_end = 768 + 40 + 40,
	.htotal = 768 + 40 + 40 + 40,
	.vdisplay = 1024,
	.vsync_start = 1024 + 20,
	.vsync_end = 1024 + 20 + 4,
	.vtotal = 1024 + 20 + 4 + 20,
	.vrefresh = 60,
};

static const struct panel_desc innolux_p079zca_panel_desc = {
	.mode = &innolux_p079zca_mode,
	.bpc = 8,
	.size = {
		.width = 120,
		.height = 160,
	},
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		 MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.lanes = 4,
	.supply_names = innolux_p079zca_supply_names,
	.num_supplies = ARRAY_SIZE(innolux_p079zca_supply_names),
	.power_down_delay = 80, /* T8: 80ms - 1000ms */
};

static int innolux_panel_get_modes(struct drm_panel *panel)
{
	struct innolux_panel *innolux = to_innolux_panel(panel);
	const struct drm_display_mode *m = innolux->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, m);
	if (!mode) {
		DRM_DEV_ERROR(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			      m->hdisplay, m->vdisplay, m->vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm =
			innolux->desc->size.width;
	panel->connector->display_info.height_mm =
			innolux->desc->size.height;
	panel->connector->display_info.bpc = innolux->desc->bpc;

	return 1;
}

static const struct drm_panel_funcs innolux_panel_funcs = {
	.disable = innolux_panel_disable,
	.unprepare = innolux_panel_unprepare,
	.prepare = innolux_panel_prepare,
	.enable = innolux_panel_enable,
	.get_modes = innolux_panel_get_modes,
};

static const struct of_device_id innolux_of_match[] = {
	{ .compatible = "innolux,p079zca",
	  .data = &innolux_p079zca_panel_desc
	},
	{ }
};
MODULE_DEVICE_TABLE(of, innolux_of_match);

static int innolux_panel_add(struct mipi_dsi_device *dsi,
			     const struct panel_desc *desc)
{
	struct innolux_panel *innolux;
	struct device *dev = &dsi->dev;
	int err, i;

	innolux = devm_kzalloc(dev, sizeof(*innolux), GFP_KERNEL);
	if (!innolux)
		return -ENOMEM;

	innolux->desc = desc;

	innolux->supplies = devm_kcalloc(dev, desc->num_supplies,
					 sizeof(*innolux->supplies),
					 GFP_KERNEL);
	if (!innolux->supplies)
		return -ENOMEM;

	for (i = 0; i < desc->num_supplies; i++)
		innolux->supplies[i].supply = desc->supply_names[i];

	err = devm_regulator_bulk_get(dev, desc->num_supplies,
				      innolux->supplies);
	if (err < 0)
		return err;

	innolux->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						       GPIOD_OUT_HIGH);
	if (IS_ERR(innolux->enable_gpio)) {
		err = PTR_ERR(innolux->enable_gpio);
		dev_dbg(dev, "failed to get enable gpio: %d\n", err);
		innolux->enable_gpio = NULL;
	}

	innolux->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(innolux->backlight))
		return PTR_ERR(innolux->backlight);

	drm_panel_init(&innolux->base);
	innolux->base.funcs = &innolux_panel_funcs;
	innolux->base.dev = dev;

	err = drm_panel_add(&innolux->base);
	if (err < 0)
		return err;

	mipi_dsi_set_drvdata(dsi, innolux);
	innolux->link = dsi;

	return 0;
}

static void innolux_panel_del(struct innolux_panel *innolux)
{
	if (innolux->base.dev)
		drm_panel_remove(&innolux->base);
}

static int innolux_panel_probe(struct mipi_dsi_device *dsi)
{
	const struct panel_desc *desc;
	const struct of_device_id *id;
	int err;

	id = of_match_node(innolux_of_match, dsi->dev.of_node);
	if (!id)
		return -ENODEV;

	desc = id->data;
	dsi->mode_flags = desc->flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	err = innolux_panel_add(dsi, desc);
	if (err < 0)
		return err;

	return mipi_dsi_attach(dsi);
}

static int innolux_panel_remove(struct mipi_dsi_device *dsi)
{
	struct innolux_panel *innolux = mipi_dsi_get_drvdata(dsi);
	int err;

	err = innolux_panel_unprepare(&innolux->base);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to unprepare panel: %d\n",
			      err);

	err = innolux_panel_disable(&innolux->base);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to disable panel: %d\n", err);

	err = mipi_dsi_detach(dsi);
	if (err < 0)
		DRM_DEV_ERROR(&dsi->dev, "failed to detach from DSI host: %d\n",
			      err);

	innolux_panel_del(innolux);

	return 0;
}

static void innolux_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct innolux_panel *innolux = mipi_dsi_get_drvdata(dsi);

	innolux_panel_unprepare(&innolux->base);
	innolux_panel_disable(&innolux->base);
}

static struct mipi_dsi_driver innolux_panel_driver = {
	.driver = {
		.name = "panel-innolux-p079zca",
		.of_match_table = innolux_of_match,
	},
	.probe = innolux_panel_probe,
	.remove = innolux_panel_remove,
	.shutdown = innolux_panel_shutdown,
};
module_mipi_dsi_driver(innolux_panel_driver);

MODULE_AUTHOR("Chris Zhong <zyw@rock-chips.com>");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("Innolux P079ZCA panel driver");
MODULE_LICENSE("GPL v2");
