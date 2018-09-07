/*
 * OMAP Display Subsystem Base
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include "dss.h"
#include "omapdss.h"

static struct dss_device *dss_device;

struct dss_device *omapdss_get_dss(void)
{
	return dss_device;
}
EXPORT_SYMBOL(omapdss_get_dss);

void omapdss_set_dss(struct dss_device *dss)
{
	dss_device = dss;
}
EXPORT_SYMBOL(omapdss_set_dss);

struct dispc_device *dispc_get_dispc(struct dss_device *dss)
{
	return dss->dispc;
}
EXPORT_SYMBOL(dispc_get_dispc);

const struct dispc_ops *dispc_get_ops(struct dss_device *dss)
{
	return dss->dispc_ops;
}
EXPORT_SYMBOL(dispc_get_ops);


/* -----------------------------------------------------------------------------
 * OMAP DSS Devices Handling
 */

static LIST_HEAD(omapdss_devices_list);
static DEFINE_MUTEX(omapdss_devices_lock);

void omapdss_device_register(struct omap_dss_device *dssdev)
{
	mutex_lock(&omapdss_devices_lock);
	list_add_tail(&dssdev->list, &omapdss_devices_list);
	mutex_unlock(&omapdss_devices_lock);
}
EXPORT_SYMBOL_GPL(omapdss_device_register);

void omapdss_device_unregister(struct omap_dss_device *dssdev)
{
	mutex_lock(&omapdss_devices_lock);
	list_del(&dssdev->list);
	mutex_unlock(&omapdss_devices_lock);
}
EXPORT_SYMBOL_GPL(omapdss_device_unregister);

static bool omapdss_device_is_registered(struct device_node *node)
{
	struct omap_dss_device *dssdev;
	bool found = false;

	mutex_lock(&omapdss_devices_lock);

	list_for_each_entry(dssdev, &omapdss_devices_list, list) {
		if (dssdev->dev->of_node == node) {
			found = true;
			break;
		}
	}

	mutex_unlock(&omapdss_devices_lock);
	return found;
}

struct omap_dss_device *omapdss_device_get(struct omap_dss_device *dssdev)
{
	if (!try_module_get(dssdev->owner))
		return NULL;

	if (get_device(dssdev->dev) == NULL) {
		module_put(dssdev->owner);
		return NULL;
	}

	return dssdev;
}
EXPORT_SYMBOL(omapdss_device_get);

void omapdss_device_put(struct omap_dss_device *dssdev)
{
	put_device(dssdev->dev);
	module_put(dssdev->owner);
}
EXPORT_SYMBOL(omapdss_device_put);

struct omap_dss_device *omapdss_find_device_by_port(struct device_node *src,
						    unsigned int port)
{
	struct omap_dss_device *dssdev;

	list_for_each_entry(dssdev, &omapdss_devices_list, list) {
		if (dssdev->dev->of_node == src && dssdev->of_ports & BIT(port))
			return omapdss_device_get(dssdev);
	}

	return NULL;
}

/*
 * Search for the next device starting at @from. The type argument specfies
 * which device types to consider when searching. Searching for multiple types
 * is supported by and'ing their type flags. Release the reference to the @from
 * device, and acquire a reference to the returned device if found.
 */
struct omap_dss_device *omapdss_device_get_next(struct omap_dss_device *from,
						enum omap_dss_device_type type)
{
	struct omap_dss_device *dssdev;
	struct list_head *list;

	mutex_lock(&omapdss_devices_lock);

	if (list_empty(&omapdss_devices_list)) {
		dssdev = NULL;
		goto done;
	}

	/*
	 * Start from the from entry if given or from omapdss_devices_list
	 * otherwise.
	 */
	list = from ? &from->list : &omapdss_devices_list;

	list_for_each_entry(dssdev, list, list) {
		/*
		 * Stop if we reach the omapdss_devices_list, that's the end of
		 * the list.
		 */
		if (&dssdev->list == &omapdss_devices_list) {
			dssdev = NULL;
			goto done;
		}

		/*
		 * Accept display entities if the display type is requested,
		 * and output entities if the output type is requested.
		 */
		if ((type & OMAP_DSS_DEVICE_TYPE_DISPLAY) &&
		    !dssdev->output_type)
			goto done;
		if ((type & OMAP_DSS_DEVICE_TYPE_OUTPUT) && dssdev->id &&
		    dssdev->next)
			goto done;
	}

	dssdev = NULL;

done:
	if (from)
		omapdss_device_put(from);
	if (dssdev)
		omapdss_device_get(dssdev);

	mutex_unlock(&omapdss_devices_lock);
	return dssdev;
}
EXPORT_SYMBOL(omapdss_device_get_next);

int omapdss_device_connect(struct dss_device *dss,
			   struct omap_dss_device *src,
			   struct omap_dss_device *dst)
{
	int ret;

	dev_dbg(dst->dev, "connect\n");

	if (omapdss_device_is_connected(dst))
		return -EBUSY;

	dst->dss = dss;

	ret = dst->ops->connect(src, dst);
	if (ret < 0) {
		dst->dss = NULL;
		return ret;
	}

	if (src) {
		WARN_ON(src->dst);
		dst->src = src;
		src->dst = dst;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(omapdss_device_connect);

void omapdss_device_disconnect(struct omap_dss_device *src,
			       struct omap_dss_device *dst)
{
	dev_dbg(dst->dev, "disconnect\n");

	if (!dst->id && !omapdss_device_is_connected(dst)) {
		WARN_ON(dst->output_type);
		return;
	}

	if (src) {
		if (WARN_ON(dst != src->dst))
			return;

		dst->src = NULL;
		src->dst = NULL;
	}

	WARN_ON(dst->state != OMAP_DSS_DISPLAY_DISABLED);

	dst->ops->disconnect(src, dst);
	dst->dss = NULL;
}
EXPORT_SYMBOL_GPL(omapdss_device_disconnect);

/* -----------------------------------------------------------------------------
 * Components Handling
 */

static struct list_head omapdss_comp_list;

struct omapdss_comp_node {
	struct list_head list;
	struct device_node *node;
	bool dss_core_component;
};

static bool omapdss_list_contains(const struct device_node *node)
{
	struct omapdss_comp_node *comp;

	list_for_each_entry(comp, &omapdss_comp_list, list) {
		if (comp->node == node)
			return true;
	}

	return false;
}

static void omapdss_walk_device(struct device *dev, struct device_node *node,
				bool dss_core)
{
	struct device_node *n;
	struct omapdss_comp_node *comp = devm_kzalloc(dev, sizeof(*comp),
						      GFP_KERNEL);

	if (comp) {
		comp->node = node;
		comp->dss_core_component = dss_core;
		list_add(&comp->list, &omapdss_comp_list);
	}

	/*
	 * of_graph_get_remote_port_parent() prints an error if there is no
	 * port/ports node. To avoid that, check first that there's the node.
	 */
	n = of_get_child_by_name(node, "ports");
	if (!n)
		n = of_get_child_by_name(node, "port");
	if (!n)
		return;

	of_node_put(n);

	n = NULL;
	while ((n = of_graph_get_next_endpoint(node, n)) != NULL) {
		struct device_node *pn = of_graph_get_remote_port_parent(n);

		if (!pn)
			continue;

		if (!of_device_is_available(pn) || omapdss_list_contains(pn)) {
			of_node_put(pn);
			continue;
		}

		omapdss_walk_device(dev, pn, false);
	}
}

void omapdss_gather_components(struct device *dev)
{
	struct device_node *child;

	INIT_LIST_HEAD(&omapdss_comp_list);

	omapdss_walk_device(dev, dev->of_node, true);

	for_each_available_child_of_node(dev->of_node, child) {
		if (!of_find_property(child, "compatible", NULL))
			continue;

		omapdss_walk_device(dev, child, true);
	}
}
EXPORT_SYMBOL(omapdss_gather_components);

static bool omapdss_component_is_loaded(struct omapdss_comp_node *comp)
{
	if (comp->dss_core_component)
		return true;
	if (omapdss_device_is_registered(comp->node))
		return true;

	return false;
}

bool omapdss_stack_is_ready(void)
{
	struct omapdss_comp_node *comp;

	list_for_each_entry(comp, &omapdss_comp_list, list) {
		if (!omapdss_component_is_loaded(comp))
			return false;
	}

	return true;
}
EXPORT_SYMBOL(omapdss_stack_is_ready);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("OMAP Display Subsystem Base");
MODULE_LICENSE("GPL v2");
