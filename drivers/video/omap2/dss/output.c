/*
 * Copyright (C) 2012 Texas Instruments Ltd
 * Author: Archit Taneja <archit@ti.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <video/omapdss.h>

#include "dss.h"

static LIST_HEAD(output_list);
static DEFINE_MUTEX(output_lock);

int omapdss_output_set_device(struct omap_dss_device *out,
		struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&output_lock);

	if (out->device) {
		DSSERR("output already has device %s connected to it\n",
			out->device->name);
		r = -EINVAL;
		goto err;
	}

	if (out->output_type != dssdev->type) {
		DSSERR("output type and display type don't match\n");
		r = -EINVAL;
		goto err;
	}

	out->device = dssdev;
	dssdev->output = out;

	mutex_unlock(&output_lock);

	return 0;
err:
	mutex_unlock(&output_lock);

	return r;
}
EXPORT_SYMBOL(omapdss_output_set_device);

int omapdss_output_unset_device(struct omap_dss_device *out)
{
	int r;

	mutex_lock(&output_lock);

	if (!out->device) {
		DSSERR("output doesn't have a device connected to it\n");
		r = -EINVAL;
		goto err;
	}

	if (out->device->state != OMAP_DSS_DISPLAY_DISABLED) {
		DSSERR("device %s is not disabled, cannot unset device\n",
				out->device->name);
		r = -EINVAL;
		goto err;
	}

	out->device->output = NULL;
	out->device = NULL;

	mutex_unlock(&output_lock);

	return 0;
err:
	mutex_unlock(&output_lock);

	return r;
}
EXPORT_SYMBOL(omapdss_output_unset_device);

void dss_register_output(struct omap_dss_device *out)
{
	list_add_tail(&out->list, &output_list);
}

void dss_unregister_output(struct omap_dss_device *out)
{
	list_del(&out->list);
}

struct omap_dss_device *omap_dss_get_output(enum omap_dss_output_id id)
{
	struct omap_dss_device *out;

	list_for_each_entry(out, &output_list, list) {
		if (out->id == id)
			return out;
	}

	return NULL;
}
EXPORT_SYMBOL(omap_dss_get_output);

struct omap_dss_device *omap_dss_find_output(const char *name)
{
	struct omap_dss_device *out;

	list_for_each_entry(out, &output_list, list) {
		if (strcmp(out->name, name) == 0)
			return omap_dss_get_device(out);
	}

	return NULL;
}
EXPORT_SYMBOL(omap_dss_find_output);

struct omap_dss_device *omap_dss_find_output_by_node(struct device_node *node)
{
	struct omap_dss_device *out;

	list_for_each_entry(out, &output_list, list) {
		if (out->dev->of_node == node)
			return omap_dss_get_device(out);
	}

	return NULL;
}
EXPORT_SYMBOL(omap_dss_find_output_by_node);

struct omap_dss_device *omapdss_find_output_from_display(struct omap_dss_device *dssdev)
{
	return omap_dss_get_device(dssdev->output);
}
EXPORT_SYMBOL(omapdss_find_output_from_display);

struct omap_overlay_manager *omapdss_find_mgr_from_display(struct omap_dss_device *dssdev)
{
	struct omap_dss_device *out;
	struct omap_overlay_manager *mgr;

	out = omapdss_find_output_from_display(dssdev);

	if (out == NULL)
		return NULL;

	mgr = out->manager;

	omap_dss_put_device(out);

	return mgr;
}
EXPORT_SYMBOL(omapdss_find_mgr_from_display);

static const struct dss_mgr_ops *dss_mgr_ops;

int dss_install_mgr_ops(const struct dss_mgr_ops *mgr_ops)
{
	if (dss_mgr_ops)
		return -EBUSY;

	dss_mgr_ops = mgr_ops;

	return 0;
}
EXPORT_SYMBOL(dss_install_mgr_ops);

void dss_uninstall_mgr_ops(void)
{
	dss_mgr_ops = NULL;
}
EXPORT_SYMBOL(dss_uninstall_mgr_ops);

int dss_mgr_connect(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst)
{
	return dss_mgr_ops->connect(mgr, dst);
}
EXPORT_SYMBOL(dss_mgr_connect);

void dss_mgr_disconnect(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dst)
{
	dss_mgr_ops->disconnect(mgr, dst);
}
EXPORT_SYMBOL(dss_mgr_disconnect);

void dss_mgr_set_timings(struct omap_overlay_manager *mgr,
		const struct omap_video_timings *timings)
{
	dss_mgr_ops->set_timings(mgr, timings);
}
EXPORT_SYMBOL(dss_mgr_set_timings);

void dss_mgr_set_lcd_config(struct omap_overlay_manager *mgr,
		const struct dss_lcd_mgr_config *config)
{
	dss_mgr_ops->set_lcd_config(mgr, config);
}
EXPORT_SYMBOL(dss_mgr_set_lcd_config);

int dss_mgr_enable(struct omap_overlay_manager *mgr)
{
	return dss_mgr_ops->enable(mgr);
}
EXPORT_SYMBOL(dss_mgr_enable);

void dss_mgr_disable(struct omap_overlay_manager *mgr)
{
	dss_mgr_ops->disable(mgr);
}
EXPORT_SYMBOL(dss_mgr_disable);

void dss_mgr_start_update(struct omap_overlay_manager *mgr)
{
	dss_mgr_ops->start_update(mgr);
}
EXPORT_SYMBOL(dss_mgr_start_update);

int dss_mgr_register_framedone_handler(struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data)
{
	return dss_mgr_ops->register_framedone_handler(mgr, handler, data);
}
EXPORT_SYMBOL(dss_mgr_register_framedone_handler);

void dss_mgr_unregister_framedone_handler(struct omap_overlay_manager *mgr,
		void (*handler)(void *), void *data)
{
	dss_mgr_ops->unregister_framedone_handler(mgr, handler, data);
}
EXPORT_SYMBOL(dss_mgr_unregister_framedone_handler);
