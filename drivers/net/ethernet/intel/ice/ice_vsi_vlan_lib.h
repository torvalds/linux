/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2021, Intel Corporation. */

#ifndef _ICE_VSI_VLAN_LIB_H_
#define _ICE_VSI_VLAN_LIB_H_

#include <linux/types.h>
#include "ice_vlan.h"

struct ice_vsi;

int ice_vsi_add_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan);
int ice_vsi_del_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan);

int ice_vsi_ena_stripping(struct ice_vsi *vsi);
int ice_vsi_dis_stripping(struct ice_vsi *vsi);
int ice_vsi_ena_insertion(struct ice_vsi *vsi);
int ice_vsi_dis_insertion(struct ice_vsi *vsi);
int ice_vsi_set_port_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan);

int ice_vsi_ena_rx_vlan_filtering(struct ice_vsi *vsi);
int ice_vsi_dis_rx_vlan_filtering(struct ice_vsi *vsi);
int ice_vsi_ena_tx_vlan_filtering(struct ice_vsi *vsi);
int ice_vsi_dis_tx_vlan_filtering(struct ice_vsi *vsi);

#endif /* _ICE_VSI_VLAN_LIB_H_ */
