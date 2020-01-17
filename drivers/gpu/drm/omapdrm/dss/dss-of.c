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
omapdss_of_find_connected_device(struct device_yesde *yesde, unsigned int port)
{
	struct device_yesde *remote_yesde;
	struct omap_dss_device *dssdev;

	remote_yesde = of_graph_get_remote_yesde(yesde, port, 0);
	if (!remote_yesde)
		return NULL;

	dssdev = omapdss_find_device_by_yesde(remote_yesde);
	of_yesde_put(remote_yesde);

	return dssdev ? dssdev : ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL_GPL(omapdss_of_find_connected_device);
