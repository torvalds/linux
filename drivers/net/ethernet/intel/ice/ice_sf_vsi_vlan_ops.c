// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023, Intel Corporation. */

#include "ice_vsi_vlan_ops.h"
#include "ice_vsi_vlan_lib.h"
#include "ice_vlan_mode.h"
#include "ice.h"
#include "ice_sf_vsi_vlan_ops.h"

void ice_sf_vsi_init_vlan_ops(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops;

	if (ice_is_dvm_ena(&vsi->back->hw))
		vlan_ops = &vsi->outer_vlan_ops;
	else
		vlan_ops = &vsi->inner_vlan_ops;

	vlan_ops->add_vlan = ice_vsi_add_vlan;
	vlan_ops->del_vlan = ice_vsi_del_vlan;
}
