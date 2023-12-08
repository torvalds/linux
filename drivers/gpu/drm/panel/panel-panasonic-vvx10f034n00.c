// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Red Hat
 * Copyright (C) 2015 Sony Mobile Communications Inc.
 * Author: Werner Johansson <werner.johansson@sonymobile.com>
 *
 * Based on AUO panel driver by Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

/*
 * When power is turned off to this panel a minimum off time of 500ms has to be
 * observed before powering back on as there's no external reset pin. Keep
 * track of earliest wakeup time and delay subsequent prepare call accordingly
 */
#define MIN_POFF_MS (500)

struct wuxga_nt_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct regulator *supply;

	bool prepared;
	bool enabled;

	ktime_t earliest_wake;

	const struct drm_display_mode *mode;
};

static inline struct wuxga_nt_panel *to_wuxga_nt_panel(struct drm_panel *panel)
{
	return container_of(panel, struct wuxga_nt_panel, base);
}

static int wuxga_nt_panel_on(struct wuxga_nt_panel *wuxga_nt)
{
	return mipi_dsi_turn_on_peripheral(wuxga_nt->dsi);
}

static int wuxga_nt_panel_disable(struct drm_panel *panel)
{
	struct wuxga_nt_panel *wuxga_nt = to_wuxga_nt_panel(panel);
	int mipi_ret, bl_ret = 0;

	if (!wuxga_nt->enabled)
		return 0;

	mipi_ret = mipi_dsi_shutdown_peripheral(wuxga_nt->dsi);

	wuxga_nt->enabled = false;

	return mipi_ret ? mipi_ret : bl_ret;
}

static int wuxga_nt_panel_unprepare(struct drm_panel *panel)
{
	struct wuxga_nt_panel *wuxga_nt = to_wuxga_nt_panel(panel);

	if (!wuxga_nt->prepared)
		return 0;

	regulator_disable(wuxga_nt->supply);
	wuxga_nt->earliest_wake = ktime_add_ms(ktime_get_real(), MIN_POFF_MS);
	wuxga_nt->prepared = false;

	return 0;
}

static int wuxga_nt_panel_prepare(struct drm_panel *panel)
{
	struct wuxga_nt_panel *wuxga_nt = to_wuxga_nt_panel(panel);
	int ret;
	s64 enablewait;

	if (wuxga_nt->prepared)
		return 0;

	/*
	 * If the user re-enabled the panel before the required off-time then
	 * we need to wait the remaining period before re-enabling regulator
	 */
	enablewait = ktime_ms_delta(wuxga_nt->earliest_wake, ktime_get_real());

	/* Sanity check, this should never happen */
	if (enablewait > MIN_POFF_MS)
		enablewait = MIN_POFF_MS;

	if (enablewait > 0)
		msleep(enablewait);

	ret = regulator_enable(wuxga_nt->supply);
	if (ret < 0)
		return ret;

	/*
	 * A minimum delay of 250ms is required after power-up until commands
	 * can be sent
	 */
	msleep(250);

	ret = wuxga_nt_panel_on(wuxga_nt);
	if (ret < 0) {
		dev_err(panel->dev, "failed to set panel on: %d\n", ret);
		goto poweroff;
	}

	wuxga_nt->prepared = true;

	return 0;

poweroff:
	regulator_disable(wuxga_nt->supply);

	return ret;
}

static int wuxga_nt_panel_enable(struct drm_panel *panel)
{
	struct wuxga_nt_panel *wuxga_nt = to_wuxga_nt_panel(panel);

	if (wuxga_nt->enabled)
		return 0;

	wuxga_nt->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock = 164402,
	.hdisplay = 1920,
	.hsync_start = 1920 + 152,
	.hsync_end = 1920 + 152 + 52,
	.htotal = 1920 + 152 + 52 + 20,
	.vdisplay = 1200,
	.vsync_start = 1200 + 24,
	.vsync_end = 1200 + 24 + 6,
	.vtotal = 1200 + 24 + 6 + 48,
};

static int wuxga_nt_panel_get_modes(struct drm_panel *panel,
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

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 217;
	connector->display_info.height_mm = 136;

	return 1;
}

static const struct drm_panel_funcs wuxga_nt_panel_funcs = {
	.disable = wuxga_nt_panel_disable,
	.unprepare = wuxga_nt_panel_unprepare,
	.prepare = wuxga_nt_panel_prepare,
	.enable = wuxga_nt_panel_enable,
	.get_modes = wuxga_nt_panel_get_modes,
};

static const struct of_device_id wuxga_nt_of_match[] = {
	{ .compatible = "panasonic,vvx10f034n00", },
	{ }
};
MODULE_DEVICE_TABLE(of, wuxga_nt_of_match);

static int wuxga_nt_panel_add(struct wuxga_nt_panel *wuxga_nt)
{
	struct device *dev = &wuxga_nt->dsi->dev;
	int ret;

	wuxga_nt->mode = &default_mode;

	wuxga_nt->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(wuxga_nt->supply))
		return PTR_ERR(wuxga_nt->supply);

	drm_panel_init(&wuxga_nt->base, &wuxga_nt->dsi->dev,
		       &wuxga_nt_panel_funcs, DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&wuxga_nt->base);
	if (ret)
		return ret;

	drm_panel_add(&wuxga_nt->base);

	return 0;
}

static void wuxga_nt_panel_del(struct wuxga_nt_panel *wuxga_nt)
{
	if (wuxga_nt->base.dev)
		drm_panel_remove(&wuxga_nt->base);
}

static int wuxga_nt_panel_probe(struct mipi_dsi_device *dsi)
{
	struct wuxga_nt_panel *wuxga_nt;
	int ret;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			MIPI_DSI_MODE_VIDEO_HSE |
			MIPI_DSI_CLOCK_NON_CONTINUOUS |
			MIPI_DSI_MODE_LPM;

	wuxga_nt = devm_kzalloc(&dsi->dev, sizeof(*wuxga_nt), GFP_KERNEL);
	if (!wuxga_nt)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, wuxga_nt);

	wuxga_nt->dsi = dsi;

	ret = wuxga_nt_panel_add(wuxga_nt);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		wuxga_nt_panel_del(wuxga_nt);
		return ret;
	}

	return 0;
}

static void wuxga_nt_panel_remove(struct mipi_dsi_device *dsi)
{
	struct wuxga_nt_panel *wuxga_nt = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = drm_panel_disable(&wuxga_nt->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	wuxga_nt_panel_del(wuxga_nt);
}

static void wuxga_nt_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct wuxga_nt_panel *wuxga_nt = mipi_dsi_get_drvdata(dsi);

	drm_panel_disable(&wuxga_nt->base);
}

static struct mipi_dsi_driver wuxga_nt_panel_driver = {
	.driver = {
		.name = "panel-panasonic-vvx10f034n00",
		.of_match_table = wuxga_nt_of_match,
	},
	.probe = wuxga_nt_panel_probe,
	.remove = wuxga_nt_panel_remove,
	.shutdown = wuxga_nt_panel_shutdown,
};
module_mipi_dsi_driver(wuxga_nt_panel_driver);

MODULE_AUTHOR("Werner Johansson <werner.johansson@sonymobile.com>");
MODULE_DESCRIPTION("Panasonic VVX10F034N00 Novatek NT1397-based WUXGA (1920x1200) video mode panel driver");
MODULE_LICENSE("GPL v2");
