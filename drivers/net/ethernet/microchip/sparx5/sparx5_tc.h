/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_TC_H__
#define __SPARX5_TC_H__

#include <net/flow_offload.h>
#include <net/pkt_cls.h>
#include <linux/netdevice.h>

/* Controls how PORT_MASK is applied */
enum SPX5_PORT_MASK_MODE {
	SPX5_PMM_OR_DSTMASK,
	SPX5_PMM_AND_VLANMASK,
	SPX5_PMM_REPLACE_PGID,
	SPX5_PMM_REPLACE_ALL,
	SPX5_PMM_REDIR_PGID,
	SPX5_PMM_OR_PGID_MASK,
};

int sparx5_port_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			 void *type_data);

int sparx5_tc_matchall(struct net_device *ndev,
		       struct tc_cls_matchall_offload *tmo,
		       bool ingress);

int sparx5_tc_flower(struct net_device *ndev, struct flow_cls_offload *fco,
		     bool ingress);

#endif	/* __SPARX5_TC_H__ */
