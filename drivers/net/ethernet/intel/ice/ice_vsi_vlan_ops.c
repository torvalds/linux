// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice_vsi_vlan_ops.h"
#include "ice.h"

void ice_vsi_init_vlan_ops(struct ice_vsi *vsi)
{
	vsi->vlan_ops.add_vlan = ice_vsi_add_vlan;
	vsi->vlan_ops.del_vlan = ice_vsi_del_vlan;
	vsi->vlan_ops.ena_stripping = ice_vsi_ena_inner_stripping;
	vsi->vlan_ops.dis_stripping = ice_vsi_dis_inner_stripping;
	vsi->vlan_ops.ena_insertion = ice_vsi_ena_inner_insertion;
	vsi->vlan_ops.dis_insertion = ice_vsi_dis_inner_insertion;
	vsi->vlan_ops.ena_rx_filtering = ice_vsi_ena_rx_vlan_filtering;
	vsi->vlan_ops.dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
	vsi->vlan_ops.ena_tx_filtering = ice_vsi_ena_tx_vlan_filtering;
	vsi->vlan_ops.dis_tx_filtering = ice_vsi_dis_tx_vlan_filtering;
	vsi->vlan_ops.set_port_vlan = ice_vsi_set_inner_port_vlan;
}
