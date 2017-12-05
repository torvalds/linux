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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/list.h>
#include "omapdss.h"

static bool dss_initialized;
static const struct dispc_ops *ops;

static struct list_head omapdss_comp_list;

struct omapdss_comp_node {
	struct list_head list;
	struct device_node *node;
	bool dss_core_component;
};

void omapdss_set_is_initialized(bool set)
{
	dss_initialized = set;
}
EXPORT_SYMBOL(omapdss_set_is_initialized);

bool omapdss_is_initialized(void)
{
	return dss_initialized;
}
EXPORT_SYMBOL(omapdss_is_initialized);

void dispc_set_ops(const struct dispc_ops *o)
{
	ops = o;
}
EXPORT_SYMBOL(dispc_set_ops);

const struct dispc_ops *dispc_get_ops(void)
{
	return ops;
}
EXPORT_SYMBOL(dispc_get_ops);

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
	if (omapdss_component_is_display(comp->node))
		return true;
	if (omapdss_component_is_output(comp->node))
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
