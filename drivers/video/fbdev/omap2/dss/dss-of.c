/*
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/seq_file.h>

#include <video/omapdss.h>

struct device_node *
omapdss_of_get_next_port(const struct device_node *parent,
			 struct device_node *prev)
{
	struct device_node *port = NULL;

	if (!parent)
		return NULL;

	if (!prev) {
		struct device_node *ports;
		/*
		 * It's the first call, we have to find a port subnode
		 * within this node or within an optional 'ports' node.
		 */
		ports = of_get_child_by_name(parent, "ports");
		if (ports)
			parent = ports;

		port = of_get_child_by_name(parent, "port");

		/* release the 'ports' node */
		of_node_put(ports);
	} else {
		struct device_node *ports;

		ports = of_get_parent(prev);
		if (!ports)
			return NULL;

		do {
			port = of_get_next_child(ports, prev);
			if (!port) {
				of_node_put(ports);
				return NULL;
			}
			prev = port;
		} while (of_node_cmp(port->name, "port") != 0);
	}

	return port;
}
EXPORT_SYMBOL_GPL(omapdss_of_get_next_port);

struct device_node *
omapdss_of_get_next_endpoint(const struct device_node *parent,
			     struct device_node *prev)
{
	struct device_node *ep = NULL;

	if (!parent)
		return NULL;

	do {
		ep = of_get_next_child(parent, prev);
		if (!ep)
			return NULL;
		prev = ep;
	} while (of_node_cmp(ep->name, "endpoint") != 0);

	return ep;
}
EXPORT_SYMBOL_GPL(omapdss_of_get_next_endpoint);

static struct device_node *
omapdss_of_get_remote_device_node(const struct device_node *node)
{
	struct device_node *np;
	int i;

	np = of_parse_phandle(node, "remote-endpoint", 0);

	if (!np)
		return NULL;

	np = of_get_next_parent(np);

	for (i = 0; i < 3 && np; ++i) {
		struct property *prop;

		prop = of_find_property(np, "compatible", NULL);

		if (prop)
			return np;

		np = of_get_next_parent(np);
	}

	return NULL;
}

struct device_node *
omapdss_of_get_first_endpoint(const struct device_node *parent)
{
	struct device_node *port, *ep;

	port = omapdss_of_get_next_port(parent, NULL);

	if (!port)
		return NULL;

	ep = omapdss_of_get_next_endpoint(port, NULL);

	of_node_put(port);

	return ep;
}
EXPORT_SYMBOL_GPL(omapdss_of_get_first_endpoint);

struct omap_dss_device *
omapdss_of_find_source_for_first_ep(struct device_node *node)
{
	struct device_node *ep;
	struct device_node *src_node;
	struct omap_dss_device *src;

	ep = omapdss_of_get_first_endpoint(node);
	if (!ep)
		return ERR_PTR(-EINVAL);

	src_node = omapdss_of_get_remote_device_node(ep);

	of_node_put(ep);

	if (!src_node)
		return ERR_PTR(-EINVAL);

	src = omap_dss_find_output_by_node(src_node);

	of_node_put(src_node);

	if (!src)
		return ERR_PTR(-EPROBE_DEFER);

	return src;
}
EXPORT_SYMBOL_GPL(omapdss_of_find_source_for_first_ep);
