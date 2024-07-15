/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_REPR_H_
#define _ICE_REPR_H_

#include <net/dst_metadata.h>

struct ice_repr {
	struct ice_vsi *src_vsi;
	struct ice_vf *vf;
	struct ice_q_vector *q_vector;
	struct net_device *netdev;
	struct metadata_dst *dst;
	struct ice_esw_br_port *br_port;
	int q_id;
	u32 id;
	u8 parent_mac[ETH_ALEN];
#ifdef CONFIG_ICE_SWITCHDEV
	/* info about slow path rule */
	struct ice_rule_query_data sp_rule;
#endif
};

struct ice_repr *ice_repr_add_vf(struct ice_vf *vf);
void ice_repr_rem_vf(struct ice_repr *repr);

void ice_repr_start_tx_queues(struct ice_repr *repr);
void ice_repr_stop_tx_queues(struct ice_repr *repr);

void ice_repr_set_traffic_vsi(struct ice_repr *repr, struct ice_vsi *vsi);

struct ice_repr *ice_netdev_to_repr(struct net_device *netdev);
bool ice_is_port_repr_netdev(const struct net_device *netdev);

struct ice_repr *ice_repr_get_by_vsi(struct ice_vsi *vsi);
#endif
