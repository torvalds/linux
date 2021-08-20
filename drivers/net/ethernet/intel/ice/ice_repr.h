/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_REPR_H_
#define _ICE_REPR_H_

#include <net/dst_metadata.h>
#include "ice.h"

struct ice_repr {
	struct ice_vsi *src_vsi;
	struct ice_vf *vf;
	struct ice_q_vector *q_vector;
	struct net_device *netdev;
	struct metadata_dst *dst;
};

int ice_repr_add_for_all_vfs(struct ice_pf *pf);
void ice_repr_rem_from_all_vfs(struct ice_pf *pf);

struct ice_repr *ice_netdev_to_repr(struct net_device *netdev);
bool ice_is_port_repr_netdev(struct net_device *netdev);
#endif
