/*
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
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
#include <linux/of_graph.h>
#include <linux/seq_file.h>

#include "omapdss.h"

static struct device_node *
dss_of_port_get_parent_device(struct device_node *port)
{
	struct device_node *np;
	int i;

	if (!port)
		return NULL;

	np = of_get_parent(port);

	for (i = 0; i < 2 && np; ++i) {
		struct property *prop;

		prop = of_find_property(np, "compatible", NULL);

		if (prop)
			return np;

		np = of_get_next_parent(np);
	}

	return NULL;
}

struct omap_dss_device *
omapdss_of_find_connected_device(struct device_node *node, unsigned int port)
{
	struct device_node *src_node;
	struct device_node *src_port;
	struct device_node *ep;
	struct omap_dss_device *src;
	u32 port_number = 0;

	/* Get the endpoint... */
	ep = of_graph_get_endpoint_by_regs(node, port, 0);
	if (!ep)
		return NULL;

	/* ... and its remote port... */
	src_port = of_graph_get_remote_port(ep);
	of_node_put(ep);
	if (!src_port)
		return NULL;

	/* ... and the remote port's number and parent... */
	of_property_read_u32(src_port, "reg", &port_number);
	src_node = dss_of_port_get_parent_device(src_port);
	of_node_put(src_port);
	if (!src_node)
		return ERR_PTR(-EINVAL);

	/* ... and finally the connected device. */
	src = omapdss_find_device_by_port(src_node, port_number);
	of_node_put(src_node);

	return src ? src : ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL_GPL(omapdss_of_find_connected_device);
