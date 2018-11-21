/*
 * Copyright (C) 2015 Red Hat
 * Copyright (C) 2015 Sony Mobile Communications Inc.
 * Author: Werner Johansson <werner.johansson@sonymobile.com>
 *
 * Based on AUO panel driver by Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/backlight.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>

/*
 * When power is turned off to this panel a minimum off time of 500ms has to be
 * observed before powering back on as there's no external reset pin. Keep
 * track of earliest wakeup time and delay subsequent prepare call accordingly
 */
#define MIN_POFF_MS (500)

struct wuxga_nt_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct backlight_device *backlight;
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

	if (wuxga_nt->backlight) {
		wuxga_nt->backlight->props.power = FB_BLANK_POWERDOWN;
		wuxga_nt->backlight->props.state |= BL_CORE_FBBLANK;
		bl_ret = backlight_update_status(wuxga_nt->backlight);
	}

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

	if (wuxga_nt->backlight) {
		wuxga_nt->backlight->props.power = FB_BLANK_UNBLANK;
		wuxga_nt->backlight->props.state &= ~BL_CORE_FBBLANK;
		backlight_update_status(wuxga_nt->backlight);
	}

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
	.vrefresh = 60,
};

static int wuxga_nt_panel_get_modes(struct drm_panel *panel)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(panel->drm, &default_mode);
	if (!mode) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
				default_mode.hdisplay, default_mode.vdisplay,
				default_mode.vrefresh);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(panel->connector, mode);

	panel->connector->display_info.width_mm = 217;
	panel->connector->display_info.height_mm = 136;

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
	struct device_node *np;
	int ret;

	wuxga_nt->mode = &default_mode;

	wuxga_nt->supply = devm_regulator_get(dev, "power");
	if (IS_ERR(wuxga_nt->supply))
		return PTR_ERR(wuxga_nt->supply);

	np = of_parse_phandle(dev->of_node, "backlight", 0);
	if (np) {
		wuxga_nt->backlight = of_find_backlight_by_node(np);
		of_node_put(np);

		if (!wuxga_nt->backlight)
			return -EPROBE_DEFER;
	}

	drm_panel_init(&wuxga_nt->base);
	wuxga_nt->base.funcs = &wuxga_nt_panel_funcs;
	wuxga_nt->base.dev = &wuxga_nt->dsi->dev;

	ret = drm_panel_add(&wuxga_nt->base);
	if (ret < 0)
		goto put_backlight;

	return 0;

put_backlight:
	if (wuxga_nt->backlight)
		put_device(&wuxga_nt->backlight->dev);

	return ret;
}

static void wuxga_nt_panel_del(struct wuxga_nt_panel *wuxga_nt)
{
	if (wuxga_nt->base.dev)
		drm_panel_remove(&wuxga_nt->base);

	if (wuxga_nt->backlight)
		put_device(&wuxga_nt->backlight->dev);
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

	return mipi_dsi_attach(dsi);
}

static int wuxga_nt_panel_remove(struct mipi_dsi_device *dsi)
{
	struct wuxga_nt_panel *wuxga_nt = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = wuxga_nt_panel_disable(&wuxga_nt->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_detach(&wuxga_nt->base);
	wuxga_nt_panel_del(wuxga_nt);

	return 0;
}

static void wuxga_nt_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct wuxga_nt_panel *wuxga_nt = mipi_dsi_get_drvdata(dsi);

	wuxga_nt_panel_disable(&wuxga_nt->base);
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
