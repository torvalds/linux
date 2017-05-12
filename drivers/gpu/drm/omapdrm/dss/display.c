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
#include <linux/of.h>

#include "omapdss.h"

void omapdss_default_get_resolution(struct omap_dss_device *dssdev,
			u16 *xres, u16 *yres)
{
	*xres = dssdev->panel.vm.hactive;
	*yres = dssdev->panel.vm.vactive;
}
EXPORT_SYMBOL(omapdss_default_get_resolution);

void omapdss_default_get_timings(struct omap_dss_device *dssdev,
				 struct videomode *vm)
{
	*vm = dssdev->panel.vm;
}
EXPORT_SYMBOL(omapdss_default_get_timings);

static LIST_HEAD(panel_list);
static DEFINE_MUTEX(panel_list_mutex);
static int disp_num_counter;

int omapdss_register_display(struct omap_dss_device *dssdev)
{
	struct omap_dss_driver *drv = dssdev->driver;
	struct list_head *cur;
	int id;

	/*
	 * Note: this presumes that all displays either have an DT alias, or
	 * none has.
	 */
	id = of_alias_get_id(dssdev->dev->of_node, "display");
	if (id < 0)
		id = disp_num_counter++;

	snprintf(dssdev->alias, sizeof(dssdev->alias), "display%d", id);

	/* Use 'label' property for name, if it exists */
	of_property_read_string(dssdev->dev->of_node, "label", &dssdev->name);

	if (dssdev->name == NULL)
		dssdev->name = dssdev->alias;

	if (drv && drv->get_resolution == NULL)
		drv->get_resolution = omapdss_default_get_resolution;
	if (drv && drv->get_timings == NULL)
		drv->get_timings = omapdss_default_get_timings;

	mutex_lock(&panel_list_mutex);
	list_for_each(cur, &panel_list) {
		struct omap_dss_device *ldev = list_entry(cur,
							 struct omap_dss_device,
							 panel_list);
		if (strcmp(ldev->alias, dssdev->alias) > 0)
			break;
	}
	list_add_tail(&dssdev->panel_list, cur);
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

bool omapdss_component_is_display(struct device_node *node)
{
	struct omap_dss_device *dssdev;
	bool found = false;

	mutex_lock(&panel_list_mutex);
	list_for_each_entry(dssdev, &panel_list, panel_list) {
		if (dssdev->dev->of_node == node) {
			found = true;
			goto out;
		}
	}
out:
	mutex_unlock(&panel_list_mutex);
	return found;
}
EXPORT_SYMBOL(omapdss_component_is_display);

struct omap_dss_device *omap_dss_get_device(struct omap_dss_device *dssdev)
{
	if (!try_module_get(dssdev->owner))
		return NULL;

	if (get_device(dssdev->dev) == NULL) {
		module_put(dssdev->owner);
		return NULL;
	}

	return dssdev;
}
EXPORT_SYMBOL(omap_dss_get_device);

void omap_dss_put_device(struct omap_dss_device *dssdev)
{
	put_device(dssdev->dev);
	module_put(dssdev->owner);
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
