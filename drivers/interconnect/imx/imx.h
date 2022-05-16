/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Interconnect framework driver for i.MX SoC
 *
 * Copyright (c) 2019, BayLibre
 * Copyright (c) 2019-2020, NXP
 * Author: Alexandre Bailon <abailon@baylibre.com>
 * Author: Leonard Crestez <leonard.crestez@nxp.com>
 */
#ifndef __DRIVERS_INTERCONNECT_IMX_H
#define __DRIVERS_INTERCONNECT_IMX_H

#include <linux/kernel.h>

#define IMX_ICC_MAX_LINKS	4

/*
 * struct imx_icc_node_adj - Describe a dynamic adjustable node
 */
struct imx_icc_node_adj_desc {
	unsigned int bw_mul, bw_div;
	const char *phandle_name;
	bool main_noc;
};

/*
 * struct imx_icc_node - Describe an interconnect node
 * @name: name of the node
 * @id: an unique id to identify the node
 * @links: an array of slaves' node id
 * @num_links: number of id defined in links
 */
struct imx_icc_node_desc {
	const char *name;
	u16 id;
	u16 links[IMX_ICC_MAX_LINKS];
	u16 num_links;
	const struct imx_icc_node_adj_desc *adj;
};

#define DEFINE_BUS_INTERCONNECT(_name, _id, _adj, ...)			\
	{								\
		.id = _id,						\
		.name = _name,						\
		.adj = _adj,						\
		.num_links = ARRAY_SIZE(((int[]){ __VA_ARGS__ })),	\
		.links = { __VA_ARGS__ },				\
	}

#define DEFINE_BUS_MASTER(_name, _id, _dest_id)				\
	DEFINE_BUS_INTERCONNECT(_name, _id, NULL, _dest_id)

#define DEFINE_BUS_SLAVE(_name, _id, _adj)				\
	DEFINE_BUS_INTERCONNECT(_name, _id, _adj)

int imx_icc_register(struct platform_device *pdev,
		     struct imx_icc_node_desc *nodes,
		     int nodes_count);
int imx_icc_unregister(struct platform_device *pdev);

#endif /* __DRIVERS_INTERCONNECT_IMX_H */
