// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP Display Subsystem Base
 *
 * Copyright (C) 2015-2017 Texas Instruments Incorporated - http://www.ti.com/
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

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

struct omap_dss_device *omapdss_find_device_by_node(struct device_node *node)
{
	struct omap_dss_device *dssdev;

	list_for_each_entry(dssdev, &omapdss_devices_list, list) {
		if (dssdev->dev->of_node == node)
			return omapdss_device_get(dssdev);
	}

	return NULL;
}

/*
 * Search for the next output device starting at @from. Release the reference to
 * the @from device, and acquire a reference to the returned device if found.
 */
struct omap_dss_device *omapdss_device_next_output(struct omap_dss_device *from)
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

		if (dssdev->id && (dssdev->next || dssdev->bridge))
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
EXPORT_SYMBOL(omapdss_device_next_output);

static bool omapdss_device_is_connected(struct omap_dss_device *dssdev)
{
	return dssdev->dss;
}

int omapdss_device_connect(struct dss_device *dss,
			   struct omap_dss_device *src,
			   struct omap_dss_device *dst)
{
	int ret;

	dev_dbg(&dss->pdev->dev, "connect(%s, %s)\n",
		src ? dev_name(src->dev) : "NULL",
		dst ? dev_name(dst->dev) : "NULL");

	if (!dst) {
		/*
		 * The destination is NULL when the source is connected to a
		 * bridge instead of a DSS device. Stop here, we will attach
		 * the bridge later when we will have a DRM encoder.
		 */
		return src && src->bridge ? 0 : -EINVAL;
	}

	if (omapdss_device_is_connected(dst))
		return -EBUSY;

	dst->dss = dss;

	if (dst->ops && dst->ops->connect) {
		ret = dst->ops->connect(src, dst);
		if (ret < 0) {
			dst->dss = NULL;
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(omapdss_device_connect);

void omapdss_device_disconnect(struct omap_dss_device *src,
			       struct omap_dss_device *dst)
{
	struct dss_device *dss = src ? src->dss : dst->dss;

	dev_dbg(&dss->pdev->dev, "disconnect(%s, %s)\n",
		src ? dev_name(src->dev) : "NULL",
		dst ? dev_name(dst->dev) : "NULL");

	if (!dst) {
		WARN_ON(!src->bridge);
		return;
	}

	if (!dst->id && !omapdss_device_is_connected(dst)) {
		WARN_ON(!dst->display);
		return;
	}

	WARN_ON(dst->state != OMAP_DSS_DISPLAY_DISABLED);

	if (dst->ops && dst->ops->disconnect)
		dst->ops->disconnect(src, dst);
	dst->dss = NULL;
}
EXPORT_SYMBOL_GPL(omapdss_device_disconnect);

void omapdss_device_enable(struct omap_dss_device *dssdev)
{
	if (!dssdev)
		return;

	if (dssdev->ops && dssdev->ops->enable)
		dssdev->ops->enable(dssdev);

	omapdss_device_enable(dssdev->next);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
}
EXPORT_SYMBOL_GPL(omapdss_device_enable);

void omapdss_device_disable(struct omap_dss_device *dssdev)
{
	if (!dssdev)
		return;

	omapdss_device_disable(dssdev->next);

	if (dssdev->ops && dssdev->ops->disable)
		dssdev->ops->disable(dssdev);
}
EXPORT_SYMBOL_GPL(omapdss_device_disable);

/* -----------------------------------------------------------------------------
 * Components Handling
 */

static struct list_head omapdss_comp_list;

struct omapdss_comp_node {
	struct list_head list;
	struct device_node *node;
	bool dss_core_component;
	const char *compat;
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
	struct omapdss_comp_node *comp;
	struct device_node *n;
	const char *compat;
	int ret;

	ret = of_property_read_string(node, "compatible", &compat);
	if (ret < 0)
		return;

	comp = devm_kzalloc(dev, sizeof(*comp), GFP_KERNEL);
	if (comp) {
		comp->node = node;
		comp->dss_core_component = dss_core;
		comp->compat = compat;
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

	for_each_available_child_of_node(dev->of_node, child)
		omapdss_walk_device(dev, child, true);
}
EXPORT_SYMBOL(omapdss_gather_components);

static bool omapdss_component_is_loaded(struct omapdss_comp_node *comp)
{
	if (comp->dss_core_component)
		return true;
	if (!strstarts(comp->compat, "omapdss,"))
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
