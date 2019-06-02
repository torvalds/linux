// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include "omapdss.h"

struct omap_dss_device *
omapdss_of_find_connected_device(struct device_node *node, unsigned int port)
{
	struct device_node *remote_node;
	struct omap_dss_device *dssdev;

	remote_node = of_graph_get_remote_node(node, port, 0);
	if (!remote_node)
		return NULL;

	dssdev = omapdss_find_device_by_node(remote_node);
	of_node_put(remote_node);

	return dssdev ? dssdev : ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL_GPL(omapdss_of_find_connected_device);
