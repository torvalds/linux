// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice_vsi_vlan_ops.h"
#include "ice_vsi_vlan_lib.h"
#include "ice.h"
#include "ice_vf_vsi_vlan_ops.h"
#include "ice_virtchnl_pf.h"

static int
noop_vlan_arg(struct ice_vsi __always_unused *vsi,
	      struct ice_vlan __always_unused *vlan)
{
	return 0;
}

/**
 * ice_vf_vsi_init_vlan_ops - Initialize default VSI VLAN ops for VF VSI
 * @vsi: VF's VSI being configured
 */
void ice_vf_vsi_init_vlan_ops(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops;
	struct ice_pf *pf = vsi->back;
	struct ice_vf *vf;

	vf = &pf->vf[vsi->vf_id];

	if (ice_is_dvm_ena(&pf->hw)) {
		vlan_ops = &vsi->outer_vlan_ops;

		/* outer VLAN ops regardless of port VLAN config */
		vlan_ops->add_vlan = ice_vsi_add_vlan;
		vlan_ops->ena_rx_filtering = ice_vsi_ena_rx_vlan_filtering;
		vlan_ops->dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
		vlan_ops->ena_tx_filtering = ice_vsi_ena_tx_vlan_filtering;
		vlan_ops->dis_tx_filtering = ice_vsi_dis_tx_vlan_filtering;

		if (ice_vf_is_port_vlan_ena(vf)) {
			/* setup outer VLAN ops */
			vlan_ops->set_port_vlan = ice_vsi_set_outer_port_vlan;

			/* setup inner VLAN ops */
			vlan_ops = &vsi->inner_vlan_ops;
			vlan_ops->add_vlan = noop_vlan_arg;
			vlan_ops->del_vlan = noop_vlan_arg;
			vlan_ops->ena_stripping = ice_vsi_ena_inner_stripping;
			vlan_ops->dis_stripping = ice_vsi_dis_inner_stripping;
			vlan_ops->ena_insertion = ice_vsi_ena_inner_insertion;
			vlan_ops->dis_insertion = ice_vsi_dis_inner_insertion;
		}
	} else {
		vlan_ops = &vsi->inner_vlan_ops;

		/* inner VLAN ops regardless of port VLAN config */
		vlan_ops->add_vlan = ice_vsi_add_vlan;
		vlan_ops->ena_rx_filtering = ice_vsi_ena_rx_vlan_filtering;
		vlan_ops->dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
		vlan_ops->ena_tx_filtering = ice_vsi_ena_tx_vlan_filtering;
		vlan_ops->dis_tx_filtering = ice_vsi_dis_tx_vlan_filtering;

		if (ice_vf_is_port_vlan_ena(vf)) {
			vlan_ops->set_port_vlan = ice_vsi_set_inner_port_vlan;
		} else {
			vlan_ops->del_vlan = ice_vsi_del_vlan;
			vlan_ops->ena_stripping = ice_vsi_ena_inner_stripping;
			vlan_ops->dis_stripping = ice_vsi_dis_inner_stripping;
			vlan_ops->ena_insertion = ice_vsi_ena_inner_insertion;
			vlan_ops->dis_insertion = ice_vsi_dis_inner_insertion;
		}
	}
}
