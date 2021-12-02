/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_VSI_VLAN_OPS_H_
#define _ICE_VSI_VLAN_OPS_H_

#include "ice_type.h"
#include "ice_vsi_vlan_lib.h"

struct ice_vsi;

struct ice_vsi_vlan_ops {
	int (*add_vlan)(struct ice_vsi *vsi, u16 vid, enum ice_sw_fwd_act_type action);
	int (*del_vlan)(struct ice_vsi *vsi, u16 vid);
	int (*ena_stripping)(struct ice_vsi *vsi);
	int (*dis_stripping)(struct ice_vsi *vsi);
	int (*ena_insertion)(struct ice_vsi *vsi);
	int (*dis_insertion)(struct ice_vsi *vsi);
	int (*ena_rx_filtering)(struct ice_vsi *vsi);
	int (*dis_rx_filtering)(struct ice_vsi *vsi);
	int (*ena_tx_filtering)(struct ice_vsi *vsi);
	int (*dis_tx_filtering)(struct ice_vsi *vsi);
	int (*set_port_vlan)(struct ice_vsi *vsi, u16 pvid_info);
};

void ice_vsi_init_vlan_ops(struct ice_vsi *vsi);

#endif /* _ICE_VSI_VLAN_OPS_H_ */
