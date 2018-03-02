/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
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

static LIST_HEAD(panel_list);
static DEFINE_MUTEX(panel_list_mutex);
static int disp_num_counter;

void omapdss_display_init(struct omap_dss_device *dssdev)
{
	int id;

	/*
	 * Note: this presumes that all displays either have an DT alias, or
	 * none has.
	 */
	id = of_alias_get_id(dssdev->dev->of_node, "display");
	if (id < 0)
		id = disp_num_counter++;

	dssdev->alias_id = id;

	/* Use 'label' property for name, if it exists */
	of_property_read_string(dssdev->dev->of_node, "label", &dssdev->name);

	if (dssdev->name == NULL)
		dssdev->name = devm_kasprintf(dssdev->dev, GFP_KERNEL,
					      "display%u", id);
}
EXPORT_SYMBOL_GPL(omapdss_display_init);

int omapdss_register_display(struct omap_dss_device *dssdev)
{
	mutex_lock(&panel_list_mutex);
	list_add_tail(&dssdev->panel_list, &panel_list);
	mutex_unlock(&panel_list_mutex);

	omapdss_device_register(dssdev);
	return 0;
}
EXPORT_SYMBOL(omapdss_register_display);

void omapdss_unregister_display(struct omap_dss_device *dssdev)
{
	mutex_lock(&panel_list_mutex);
	list_del(&dssdev->panel_list);
	mutex_unlock(&panel_list_mutex);

	omapdss_device_register(dssdev);
}
EXPORT_SYMBOL(omapdss_unregister_display);

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
