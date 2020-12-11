// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2019 Texas Instruments Incorporated - https://www.ti.com
 *  Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

struct osd101t2587_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct regulator *supply;

	bool prepared;
	bool enabled;

	const struct drm_display_mode *default_mode;
};

static inline struct osd101t2587_panel *ti_osd_panel(struct drm_panel *panel)
{
	return container_of(panel, struct osd101t2587_panel, base);
}

static int osd101t2587_panel_disable(struct drm_panel *panel)
{
	struct osd101t2587_panel *osd101t2587 = ti_osd_panel(panel);
	int ret;

	if (!osd101t2587->enabled)
		return 0;

	ret = mipi_dsi_shutdown_peripheral(osd101t2587->dsi);

	osd101t2587->enabled = false;

	return ret;
}

static int osd101t2587_panel_unprepare(struct drm_panel *panel)
{
	struct osd101t2587_panel *osd101t2587 = ti_osd_panel(panel);

	if (!osd101t2587->prepared)
		return 0;

	regulator_disable(osd101t2587->supply);
	osd101t2587->prepared = false;

	return 0;
}

static int osd101t2587_panel_prepare(struct drm_panel *panel)
{
	struct osd101t2587_panel *osd101t2587 = ti_osd_panel(panel);
	int ret;

	if (osd101t2587->prepared)
		return 0;

	ret = regulator_enable(osd101t2587->supply);
	if (!ret)
		osd101t2587->prepared = true;

	return ret;
}

static int osd101t2587_panel_enable(struct drm_panel *panel)
{
	struct osd101t2587_panel *osd101t2587 = ti_osd_panel(panel);
	int ret;

	if (osd101t2587->enabled)
		return 0;

	ret = mipi_dsi_turn_on_peripheral(osd101t2587->dsi);
	if (ret)
		return ret;

	osd101t2587->enabled = true;

	return ret;
}

static const struct drm_display_mode default_mode_osd101t2587 = {
	.clock = 164400,
	.hdisplay = 1920,
	.hsync_start = 1920 + 152,
	.hsync_end = 1920 + 152 + 52,
	.htotal = 1920 + 152 + 52 + 20,
	.vdisplay = 1200,
	.vsync_start = 1200 + 24,
	.vsync_end = 1200 + 24 + 6,
	.vtotal = 1200 + 24 + 6 + 48,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
};

static int osd101t2587_panel_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	struct osd101t2587_panel *osd101t2587 = ti_osd_panel(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, osd101t2587->default_mode);
	if (!mode) {
		dev_err(panel->dev, "failed to add mode %ux%ux@%u\n",
			osd101t2587->default_mode->hdisplay,
			osd101t2587->default_mode->vdisplay,
			drm_mode_vrefresh(osd101t2587->default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 217;
	connector->display_info.height_mm = 136;

	return 1;
}

static const struct drm_panel_funcs osd101t2587_panel_funcs = {
	.disable = osd101t2587_panel_disable,
	.unprepare = osd101t2587_panel_unprepare,
	.prepare = osd101t2587_panel_prepare,
	.enable = osd101t2587_panel_enable,
	.get_modes = osd101t2587_panel_get_modes,
};

static const struct of_device_id osd101t2587_of_match[] = {
	{
		.compatible = "osddisplays,osd101t2587-53ts",
		.data = &default_mode_osd101t2587,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, osd101t2587_of_match);

static int osd101t2587_panel_add(struct osd101t2587_panel *osd101t2587)
{
	struct device *dev = &osd101t2587->dsi->dev;
	int ret;

	osd101t2587->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(osd101t2587->supply))
		return PTR_ERR(osd101t2587->supply);

	drm_panel_init(&osd101t2587->base, &osd101t2587->dsi->dev,
		       &osd101t2587_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&osd101t2587->base);
	if (ret)
		return ret;

	drm_panel_add(&osd101t2587->base);

	return 0;
}

static int osd101t2587_panel_probe(struct mipi_dsi_device *dsi)
{
	struct osd101t2587_panel *osd101t2587;
	const struct of_device_id *id;
	int ret;

	id = of_match_node(osd101t2587_of_match, dsi->dev.of_node);
	if (!id)
		return -ENODEV;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_EOT_PACKET;

	osd101t2587 = devm_kzalloc(&dsi->dev, sizeof(*osd101t2587), GFP_KERNEL);
	if (!osd101t2587)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, osd101t2587);

	osd101t2587->dsi = dsi;
	osd101t2587->default_mode = id->data;

	ret = osd101t2587_panel_add(osd101t2587);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&osd101t2587->base);

	return ret;
}

static int osd101t2587_panel_remove(struct mipi_dsi_device *dsi)
{
	struct osd101t2587_panel *osd101t2587 = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = drm_panel_disable(&osd101t2587->base);
	if (ret < 0)
		dev_warn(&dsi->dev, "failed to disable panel: %d\n", ret);

	drm_panel_unprepare(&osd101t2587->base);
	drm_panel_remove(&osd101t2587->base);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	return ret;
}

static void osd101t2587_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct osd101t2587_panel *osd101t2587 = mipi_dsi_get_drvdata(dsi);

	drm_panel_disable(&osd101t2587->base);
	drm_panel_unprepare(&osd101t2587->base);
}

static struct mipi_dsi_driver osd101t2587_panel_driver = {
	.driver = {
		.name = "panel-osd-osd101t2587-53ts",
		.of_match_table = osd101t2587_of_match,
	},
	.probe = osd101t2587_panel_probe,
	.remove = osd101t2587_panel_remove,
	.shutdown = osd101t2587_panel_shutdown,
};
module_mipi_dsi_driver(osd101t2587_panel_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("OSD101T2587-53TS DSI panel");
MODULE_LICENSE("GPL v2");
