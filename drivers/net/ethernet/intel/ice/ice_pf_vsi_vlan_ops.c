// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice_vsi_vlan_ops.h"
#include "ice_vsi_vlan_lib.h"
#include "ice_vlan_mode.h"
#include "ice.h"
#include "ice_pf_vsi_vlan_ops.h"

void ice_pf_vsi_init_vlan_ops(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops;

	if (ice_is_dvm_ena(&vsi->back->hw)) {
		vlan_ops = &vsi->outer_vlan_ops;

		vlan_ops->add_vlan = ice_vsi_add_vlan;
		vlan_ops->del_vlan = ice_vsi_del_vlan;
		vlan_ops->ena_stripping = ice_vsi_ena_outer_stripping;
		vlan_ops->dis_stripping = ice_vsi_dis_outer_stripping;
		vlan_ops->ena_insertion = ice_vsi_ena_outer_insertion;
		vlan_ops->dis_insertion = ice_vsi_dis_outer_insertion;
		vlan_ops->ena_rx_filtering = ice_vsi_ena_rx_vlan_filtering;
		vlan_ops->dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
	} else {
		vlan_ops = &vsi->inner_vlan_ops;

		vlan_ops->add_vlan = ice_vsi_add_vlan;
		vlan_ops->del_vlan = ice_vsi_del_vlan;
		vlan_ops->ena_stripping = ice_vsi_ena_inner_stripping;
		vlan_ops->dis_stripping = ice_vsi_dis_inner_stripping;
		vlan_ops->ena_insertion = ice_vsi_ena_inner_insertion;
		vlan_ops->dis_insertion = ice_vsi_dis_inner_insertion;
		vlan_ops->ena_rx_filtering = ice_vsi_ena_rx_vlan_filtering;
		vlan_ops->dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
	}
}

