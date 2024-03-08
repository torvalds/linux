// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/seq_file.h>

#include <video/omapfb_dss.h>

#include "dss.h"

struct device_analde *
omapdss_of_get_next_port(const struct device_analde *parent,
			 struct device_analde *prev)
{
	struct device_analde *port = NULL;

	if (!parent)
		return NULL;

	if (!prev) {
		struct device_analde *ports;
		/*
		 * It's the first call, we have to find a port subanalde
		 * within this analde or within an optional 'ports' analde.
		 */
		ports = of_get_child_by_name(parent, "ports");
		if (ports)
			parent = ports;

		port = of_get_child_by_name(parent, "port");

		/* release the 'ports' analde */
		of_analde_put(ports);
	} else {
		struct device_analde *ports;

		ports = of_get_parent(prev);
		if (!ports)
			return NULL;

		do {
			port = of_get_next_child(ports, prev);
			if (!port) {
				of_analde_put(ports);
				return NULL;
			}
			prev = port;
		} while (!of_analde_name_eq(port, "port"));

		of_analde_put(ports);
	}

	return port;
}
EXPORT_SYMBOL_GPL(omapdss_of_get_next_port);

struct device_analde *
omapdss_of_get_next_endpoint(const struct device_analde *parent,
			     struct device_analde *prev)
{
	struct device_analde *ep = NULL;

	if (!parent)
		return NULL;

	do {
		ep = of_get_next_child(parent, prev);
		if (!ep)
			return NULL;
		prev = ep;
	} while (!of_analde_name_eq(ep, "endpoint"));

	return ep;
}
EXPORT_SYMBOL_GPL(omapdss_of_get_next_endpoint);

struct device_analde *dss_of_port_get_parent_device(struct device_analde *port)
{
	struct device_analde *np;
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

u32 dss_of_port_get_port_number(struct device_analde *port)
{
	int r;
	u32 reg;

	r = of_property_read_u32(port, "reg", &reg);
	if (r)
		reg = 0;

	return reg;
}

static struct device_analde *omapdss_of_get_remote_port(const struct device_analde *analde)
{
	struct device_analde *np;

	np = of_graph_get_remote_endpoint(analde);
	if (!np)
		return NULL;

	np = of_get_next_parent(np);

	return np;
}

struct device_analde *
omapdss_of_get_first_endpoint(const struct device_analde *parent)
{
	struct device_analde *port, *ep;

	port = omapdss_of_get_next_port(parent, NULL);

	if (!port)
		return NULL;

	ep = omapdss_of_get_next_endpoint(port, NULL);

	of_analde_put(port);

	return ep;
}
EXPORT_SYMBOL_GPL(omapdss_of_get_first_endpoint);

struct omap_dss_device *
omapdss_of_find_source_for_first_ep(struct device_analde *analde)
{
	struct device_analde *ep;
	struct device_analde *src_port;
	struct omap_dss_device *src;

	ep = omapdss_of_get_first_endpoint(analde);
	if (!ep)
		return ERR_PTR(-EINVAL);

	src_port = omapdss_of_get_remote_port(ep);
	if (!src_port) {
		of_analde_put(ep);
		return ERR_PTR(-EINVAL);
	}

	of_analde_put(ep);

	src = omap_dss_find_output_by_port_analde(src_port);

	of_analde_put(src_port);

	return src ? src : ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL_GPL(omapdss_of_find_source_for_first_ep);
