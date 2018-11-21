/*
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
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
#include <linux/of.h>

#include "omapdss.h"

static LIST_HEAD(output_list);
static DEFINE_MUTEX(output_lock);

int omapdss_output_set_device(struct omap_dss_device *out,
		struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&output_lock);

	if (out->dst) {
		dev_err(out->dev,
			"output already has device %s connected to it\n",
			out->dst->name);
		r = -EINVAL;
		goto err;
	}

	if (out->output_type != dssdev->type) {
		dev_err(out->dev, "output type and display type don't match\n");
		r = -EINVAL;
		goto err;
	}

	out->dst = dssdev;
	dssdev->src = out;

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

	if (!out->dst) {
		dev_err(out->dev,
			"output doesn't have a device connected to it\n");
		r = -EINVAL;
		goto err;
	}

	if (out->dst->state != OMAP_DSS_DISPLAY_DISABLED) {
		dev_err(out->dev,
			"device %s is not disabled, cannot unset device\n",
			out->dst->name);
		r = -EINVAL;
		goto err;
	}

	out->dst->src = NULL;
	out->dst = NULL;

	mutex_unlock(&output_lock);

	return 0;
err:
	mutex_unlock(&output_lock);

	return r;
}
EXPORT_SYMBOL(omapdss_output_unset_device);

int omapdss_register_output(struct omap_dss_device *out)
{
	list_add_tail(&out->list, &output_list);
	return 0;
}
EXPORT_SYMBOL(omapdss_register_output);

void omapdss_unregister_output(struct omap_dss_device *out)
{
	list_del(&out->list);
}
EXPORT_SYMBOL(omapdss_unregister_output);

bool omapdss_component_is_output(struct device_node *node)
{
	struct omap_dss_device *out;

	list_for_each_entry(out, &output_list, list) {
		if (out->dev->of_node == node)
			return true;
	}

	return false;
}
EXPORT_SYMBOL(omapdss_component_is_output);

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

struct omap_dss_device *omap_dss_find_output_by_port_node(struct device_node *port)
{
	struct device_node *src_node;
	struct omap_dss_device *out;
	u32 reg;

	src_node = dss_of_port_get_parent_device(port);
	if (!src_node)
		return NULL;

	reg = dss_of_port_get_port_number(port);

	list_for_each_entry(out, &output_list, list) {
		if (out->dev->of_node == src_node && out->port_num == reg) {
			of_node_put(src_node);
			return omap_dss_get_device(out);
		}
	}

	of_node_put(src_node);

	return NULL;
}

struct omap_dss_device *omapdss_find_output_from_display(struct omap_dss_device *dssdev)
{
	while (dssdev->src)
		dssdev = dssdev->src;

	if (dssdev->id != 0)
		return omap_dss_get_device(dssdev);

	return NULL;
}
EXPORT_SYMBOL(omapdss_find_output_from_display);

static const struct dss_mgr_ops *dss_mgr_ops;
static struct omap_drm_private *dss_mgr_ops_priv;

int dss_install_mgr_ops(const struct dss_mgr_ops *mgr_ops,
			struct omap_drm_private *priv)
{
	if (dss_mgr_ops)
		return -EBUSY;

	dss_mgr_ops = mgr_ops;
	dss_mgr_ops_priv = priv;

	return 0;
}
EXPORT_SYMBOL(dss_install_mgr_ops);

void dss_uninstall_mgr_ops(void)
{
	dss_mgr_ops = NULL;
	dss_mgr_ops_priv = NULL;
}
EXPORT_SYMBOL(dss_uninstall_mgr_ops);

int dss_mgr_connect(struct omap_dss_device *dssdev, struct omap_dss_device *dst)
{
	return dss_mgr_ops->connect(dss_mgr_ops_priv,
				    dssdev->dispc_channel, dst);
}
EXPORT_SYMBOL(dss_mgr_connect);

void dss_mgr_disconnect(struct omap_dss_device *dssdev,
			struct omap_dss_device *dst)
{
	dss_mgr_ops->disconnect(dss_mgr_ops_priv, dssdev->dispc_channel, dst);
}
EXPORT_SYMBOL(dss_mgr_disconnect);

void dss_mgr_set_timings(struct omap_dss_device *dssdev,
			 const struct videomode *vm)
{
	dss_mgr_ops->set_timings(dss_mgr_ops_priv, dssdev->dispc_channel, vm);
}
EXPORT_SYMBOL(dss_mgr_set_timings);

void dss_mgr_set_lcd_config(struct omap_dss_device *dssdev,
		const struct dss_lcd_mgr_config *config)
{
	dss_mgr_ops->set_lcd_config(dss_mgr_ops_priv,
				    dssdev->dispc_channel, config);
}
EXPORT_SYMBOL(dss_mgr_set_lcd_config);

int dss_mgr_enable(struct omap_dss_device *dssdev)
{
	return dss_mgr_ops->enable(dss_mgr_ops_priv, dssdev->dispc_channel);
}
EXPORT_SYMBOL(dss_mgr_enable);

void dss_mgr_disable(struct omap_dss_device *dssdev)
{
	dss_mgr_ops->disable(dss_mgr_ops_priv, dssdev->dispc_channel);
}
EXPORT_SYMBOL(dss_mgr_disable);

void dss_mgr_start_update(struct omap_dss_device *dssdev)
{
	dss_mgr_ops->start_update(dss_mgr_ops_priv, dssdev->dispc_channel);
}
EXPORT_SYMBOL(dss_mgr_start_update);

int dss_mgr_register_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data)
{
	return dss_mgr_ops->register_framedone_handler(dss_mgr_ops_priv,
						       dssdev->dispc_channel,
						       handler, data);
}
EXPORT_SYMBOL(dss_mgr_register_framedone_handler);

void dss_mgr_unregister_framedone_handler(struct omap_dss_device *dssdev,
		void (*handler)(void *), void *data)
{
	dss_mgr_ops->unregister_framedone_handler(dss_mgr_ops_priv,
						  dssdev->dispc_channel,
						  handler, data);
}
EXPORT_SYMBOL(dss_mgr_unregister_framedone_handler);
