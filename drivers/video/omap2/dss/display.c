/*
 * linux/drivers/video/omap2/dss/display.c
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
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

#define DSS_SUBSYS_NAME "DISPLAY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>

#include <video/omapdss.h>
#include "dss.h"
#include "dss_features.h"

void omapdss_default_get_resolution(struct omap_dss_device *dssdev,
			u16 *xres, u16 *yres)
{
	*xres = dssdev->panel.timings.x_res;
	*yres = dssdev->panel.timings.y_res;
}
EXPORT_SYMBOL(omapdss_default_get_resolution);

int omapdss_default_get_recommended_bpp(struct omap_dss_device *dssdev)
{
	switch (dssdev->type) {
	case OMAP_DISPLAY_TYPE_DPI:
		if (dssdev->phy.dpi.data_lines == 24)
			return 24;
		else
			return 16;

	case OMAP_DISPLAY_TYPE_DBI:
		if (dssdev->ctrl.pixel_size == 24)
			return 24;
		else
			return 16;
	case OMAP_DISPLAY_TYPE_DSI:
		if (dsi_get_pixel_size(dssdev->panel.dsi_pix_fmt) > 16)
			return 24;
		else
			return 16;
	case OMAP_DISPLAY_TYPE_VENC:
	case OMAP_DISPLAY_TYPE_SDI:
	case OMAP_DISPLAY_TYPE_HDMI:
		return 24;
	default:
		BUG();
		return 0;
	}
}
EXPORT_SYMBOL(omapdss_default_get_recommended_bpp);

void omapdss_default_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	*timings = dssdev->panel.timings;
}
EXPORT_SYMBOL(omapdss_default_get_timings);

int dss_suspend_all_devices(void)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev) {
		if (!dssdev->driver)
			continue;

		if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
			dssdev->driver->disable(dssdev);
			dssdev->activate_after_resume = true;
		} else {
			dssdev->activate_after_resume = false;
		}
	}

	return 0;
}

int dss_resume_all_devices(void)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev) {
		if (!dssdev->driver)
			continue;

		if (dssdev->activate_after_resume) {
			dssdev->driver->enable(dssdev);
			dssdev->activate_after_resume = false;
		}
	}

	return 0;
}

void dss_disable_all_devices(void)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev) {
		if (!dssdev->driver)
			continue;

		if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
			dssdev->driver->disable(dssdev);
	}
}

static LIST_HEAD(panel_list);
static DEFINE_MUTEX(panel_list_mutex);
static int disp_num_counter;

int omapdss_register_display(struct omap_dss_device *dssdev)
{
	struct omap_dss_driver *drv = dssdev->driver;

	snprintf(dssdev->alias, sizeof(dssdev->alias),
			"display%d", disp_num_counter++);

	if (drv && drv->get_resolution == NULL)
		drv->get_resolution = omapdss_default_get_resolution;
	if (drv && drv->get_recommended_bpp == NULL)
		drv->get_recommended_bpp = omapdss_default_get_recommended_bpp;
	if (drv && drv->get_timings == NULL)
		drv->get_timings = omapdss_default_get_timings;

	mutex_lock(&panel_list_mutex);
	list_add_tail(&dssdev->panel_list, &panel_list);
	mutex_unlock(&panel_list_mutex);
	return 0;
}
EXPORT_SYMBOL(omapdss_register_display);

void omapdss_unregister_display(struct omap_dss_device *dssdev)
{
	mutex_lock(&panel_list_mutex);
	list_del(&dssdev->panel_list);
	mutex_unlock(&panel_list_mutex);
}
EXPORT_SYMBOL(omapdss_unregister_display);

void omap_dss_get_device(struct omap_dss_device *dssdev)
{
	get_device(dssdev->dev);
}
EXPORT_SYMBOL(omap_dss_get_device);

void omap_dss_put_device(struct omap_dss_device *dssdev)
{
	put_device(dssdev->dev);
}
EXPORT_SYMBOL(omap_dss_put_device);

/*
 * ref count of the found device is incremented.
 * ref count of from-device is decremented.
 */
struct omap_dss_device *omap_dss_get_next_device(struct omap_dss_device *from)
{
	struct list_head *l;
	struct omap_dss_device *dssdev;

	mutex_lock(&panel_list_mutex);

	if (list_empty(&panel_list)) {
		dssdev = NULL;
		goto out;
	}

	if (from == NULL) {
		dssdev = list_first_entry(&panel_list, struct omap_dss_device,
				panel_list);
		omap_dss_get_device(dssdev);
		goto out;
	}

	omap_dss_put_device(from);

	list_for_each(l, &panel_list) {
		dssdev = list_entry(l, struct omap_dss_device, panel_list);
		if (dssdev == from) {
			if (list_is_last(l, &panel_list)) {
				dssdev = NULL;
				goto out;
			}

			dssdev = list_entry(l->next, struct omap_dss_device,
					panel_list);
			omap_dss_get_device(dssdev);
			goto out;
		}
	}

	WARN(1, "'from' dssdev not found\n");

	dssdev = NULL;
out:
	mutex_unlock(&panel_list_mutex);
	return dssdev;
}
EXPORT_SYMBOL(omap_dss_get_next_device);

struct omap_dss_device *omap_dss_find_device(void *data,
		int (*match)(struct omap_dss_device *dssdev, void *data))
{
	struct omap_dss_device *dssdev = NULL;

	while ((dssdev = omap_dss_get_next_device(dssdev)) != NULL) {
		if (match(dssdev, data))
			return dssdev;
	}

	return NULL;
}
EXPORT_SYMBOL(omap_dss_find_device);

void videomode_to_omap_video_timings(const struct videomode *vm,
		struct omap_video_timings *ovt)
{
	memset(ovt, 0, sizeof(*ovt));

	ovt->pixel_clock = vm->pixelclock / 1000;
	ovt->x_res = vm->hactive;
	ovt->hbp = vm->hback_porch;
	ovt->hfp = vm->hfront_porch;
	ovt->hsw = vm->hsync_len;
	ovt->y_res = vm->vactive;
	ovt->vbp = vm->vback_porch;
	ovt->vfp = vm->vfront_porch;
	ovt->vsw = vm->vsync_len;

	ovt->vsync_level = vm->flags & DISPLAY_FLAGS_VSYNC_HIGH ?
		OMAPDSS_SIG_ACTIVE_HIGH :
		OMAPDSS_SIG_ACTIVE_LOW;
	ovt->hsync_level = vm->flags & DISPLAY_FLAGS_HSYNC_HIGH ?
		OMAPDSS_SIG_ACTIVE_HIGH :
		OMAPDSS_SIG_ACTIVE_LOW;
	ovt->de_level = vm->flags & DISPLAY_FLAGS_DE_HIGH ?
		OMAPDSS_SIG_ACTIVE_HIGH :
		OMAPDSS_SIG_ACTIVE_HIGH;
	ovt->data_pclk_edge = vm->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE ?
		OMAPDSS_DRIVE_SIG_RISING_EDGE :
		OMAPDSS_DRIVE_SIG_FALLING_EDGE;

	ovt->sync_pclk_edge = OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES;
}
EXPORT_SYMBOL(videomode_to_omap_video_timings);

void omap_video_timings_to_videomode(const struct omap_video_timings *ovt,
		struct videomode *vm)
{
	memset(vm, 0, sizeof(*vm));

	vm->pixelclock = ovt->pixel_clock * 1000;

	vm->hactive = ovt->x_res;
	vm->hback_porch = ovt->hbp;
	vm->hfront_porch = ovt->hfp;
	vm->hsync_len = ovt->hsw;
	vm->vactive = ovt->y_res;
	vm->vback_porch = ovt->vbp;
	vm->vfront_porch = ovt->vfp;
	vm->vsync_len = ovt->vsw;

	if (ovt->hsync_level == OMAPDSS_SIG_ACTIVE_HIGH)
		vm->flags |= DISPLAY_FLAGS_HSYNC_HIGH;
	else
		vm->flags |= DISPLAY_FLAGS_HSYNC_LOW;

	if (ovt->vsync_level == OMAPDSS_SIG_ACTIVE_HIGH)
		vm->flags |= DISPLAY_FLAGS_VSYNC_HIGH;
	else
		vm->flags |= DISPLAY_FLAGS_VSYNC_LOW;

	if (ovt->de_level == OMAPDSS_SIG_ACTIVE_HIGH)
		vm->flags |= DISPLAY_FLAGS_DE_HIGH;
	else
		vm->flags |= DISPLAY_FLAGS_DE_LOW;

	if (ovt->data_pclk_edge == OMAPDSS_DRIVE_SIG_RISING_EDGE)
		vm->flags |= DISPLAY_FLAGS_PIXDATA_POSEDGE;
	else
		vm->flags |= DISPLAY_FLAGS_PIXDATA_NEGEDGE;
}
EXPORT_SYMBOL(omap_video_timings_to_videomode);
