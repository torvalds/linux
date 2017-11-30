/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MVEBU_GICP_H__
#define __MVEBU_GICP_H__

#include <linux/types.h>

struct device_node;

int mvebu_gicp_get_doorbells(struct device_node *dn, phys_addr_t *setspi,
			     phys_addr_t *clrspi);

#endif /* __MVEBU_GICP_H__ */
