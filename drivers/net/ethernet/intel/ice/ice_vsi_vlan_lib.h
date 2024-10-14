/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_VSI_VLAN_LIB_H_
#define _ICE_VSI_VLAN_LIB_H_

#include <linux/types.h>
#include "ice_vlan.h"

struct ice_vsi_vlan_info {
	u8 sw_flags2;
	u8 inner_vlan_flags;
	u8 outer_vlan_flags;
};

struct ice_vsi;

int ice_vsi_add_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan);
int ice_vsi_del_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan);

int ice_vsi_ena_inner_stripping(struct ice_vsi *vsi, u16 tpid);
int ice_vsi_dis_inner_stripping(struct ice_vsi *vsi);
int ice_vsi_ena_inner_insertion(struct ice_vsi *vsi, u16 tpid);
int ice_vsi_dis_inner_insertion(struct ice_vsi *vsi);
int ice_vsi_set_inner_port_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan);
int ice_vsi_clear_inner_port_vlan(struct ice_vsi *vsi);

int ice_vsi_ena_rx_vlan_filtering(struct ice_vsi *vsi);
int ice_vsi_dis_rx_vlan_filtering(struct ice_vsi *vsi);
int ice_vsi_ena_tx_vlan_filtering(struct ice_vsi *vsi);
int ice_vsi_dis_tx_vlan_filtering(struct ice_vsi *vsi);

int ice_vsi_ena_outer_stripping(struct ice_vsi *vsi, u16 tpid);
int ice_vsi_dis_outer_stripping(struct ice_vsi *vsi);
int ice_vsi_ena_outer_insertion(struct ice_vsi *vsi, u16 tpid);
int ice_vsi_dis_outer_insertion(struct ice_vsi *vsi);
int ice_vsi_set_outer_port_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan);
int ice_vsi_clear_outer_port_vlan(struct ice_vsi *vsi);
int ice_vsi_clear_port_vlan(struct ice_vsi *vsi);

#endif /* _ICE_VSI_VLAN_LIB_H_ */
