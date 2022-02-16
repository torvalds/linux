// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice_vsi_vlan_ops.h"
#include "ice_vsi_vlan_lib.h"
#include "ice_vlan_mode.h"
#include "ice.h"
#include "ice_vf_vsi_vlan_ops.h"
#include "ice_virtchnl_pf.h"

static int
noop_vlan_arg(struct ice_vsi __always_unused *vsi,
	      struct ice_vlan __always_unused *vlan)
{
	return 0;
}

static int
noop_vlan(struct ice_vsi __always_unused *vsi)
{
	return 0;
}

/**
 * ice_vf_vsi_init_vlan_ops - Initialize default VSI VLAN ops for VF VSI
 * @vsi: VF's VSI being configured
 *
 * If Double VLAN Mode (DVM) is enabled, assume that the VF supports the new
 * VIRTCHNL_VF_VLAN_OFFLOAD_V2 capability and set up the VLAN ops accordingly.
 * If SVM is enabled maintain the same level of VLAN support previous to
 * VIRTCHNL_VF_VLAN_OFFLOAD_V2.
 */
void ice_vf_vsi_init_vlan_ops(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops;
	struct ice_pf *pf = vsi->back;
	struct ice_vf *vf = vsi->vf;

	if (WARN_ON(!vf))
		return;

	if (ice_is_dvm_ena(&pf->hw)) {
		vlan_ops = &vsi->outer_vlan_ops;

		/* outer VLAN ops regardless of port VLAN config */
		vlan_ops->add_vlan = ice_vsi_add_vlan;
		vlan_ops->dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
		vlan_ops->ena_tx_filtering = ice_vsi_ena_tx_vlan_filtering;
		vlan_ops->dis_tx_filtering = ice_vsi_dis_tx_vlan_filtering;

		if (ice_vf_is_port_vlan_ena(vf)) {
			/* setup outer VLAN ops */
			vlan_ops->set_port_vlan = ice_vsi_set_outer_port_vlan;
			vlan_ops->ena_rx_filtering =
				ice_vsi_ena_rx_vlan_filtering;

			/* setup inner VLAN ops */
			vlan_ops = &vsi->inner_vlan_ops;
			vlan_ops->add_vlan = noop_vlan_arg;
			vlan_ops->del_vlan = noop_vlan_arg;
			vlan_ops->ena_stripping = ice_vsi_ena_inner_stripping;
			vlan_ops->dis_stripping = ice_vsi_dis_inner_stripping;
			vlan_ops->ena_insertion = ice_vsi_ena_inner_insertion;
			vlan_ops->dis_insertion = ice_vsi_dis_inner_insertion;
		} else {
			if (!test_bit(ICE_FLAG_VF_VLAN_PRUNING, pf->flags))
				vlan_ops->ena_rx_filtering = noop_vlan;
			else
				vlan_ops->ena_rx_filtering =
					ice_vsi_ena_rx_vlan_filtering;

			vlan_ops->del_vlan = ice_vsi_del_vlan;
			vlan_ops->ena_stripping = ice_vsi_ena_outer_stripping;
			vlan_ops->dis_stripping = ice_vsi_dis_outer_stripping;
			vlan_ops->ena_insertion = ice_vsi_ena_outer_insertion;
			vlan_ops->dis_insertion = ice_vsi_dis_outer_insertion;

			/* setup inner VLAN ops */
			vlan_ops = &vsi->inner_vlan_ops;

			vlan_ops->ena_stripping = ice_vsi_ena_inner_stripping;
			vlan_ops->dis_stripping = ice_vsi_dis_inner_stripping;
			vlan_ops->ena_insertion = ice_vsi_ena_inner_insertion;
			vlan_ops->dis_insertion = ice_vsi_dis_inner_insertion;
		}
	} else {
		vlan_ops = &vsi->inner_vlan_ops;

		/* inner VLAN ops regardless of port VLAN config */
		vlan_ops->add_vlan = ice_vsi_add_vlan;
		vlan_ops->dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
		vlan_ops->ena_tx_filtering = ice_vsi_ena_tx_vlan_filtering;
		vlan_ops->dis_tx_filtering = ice_vsi_dis_tx_vlan_filtering;

		if (ice_vf_is_port_vlan_ena(vf)) {
			vlan_ops->set_port_vlan = ice_vsi_set_inner_port_vlan;
			vlan_ops->ena_rx_filtering =
				ice_vsi_ena_rx_vlan_filtering;
		} else {
			if (!test_bit(ICE_FLAG_VF_VLAN_PRUNING, pf->flags))
				vlan_ops->ena_rx_filtering = noop_vlan;
			else
				vlan_ops->ena_rx_filtering =
					ice_vsi_ena_rx_vlan_filtering;

			vlan_ops->del_vlan = ice_vsi_del_vlan;
			vlan_ops->ena_stripping = ice_vsi_ena_inner_stripping;
			vlan_ops->dis_stripping = ice_vsi_dis_inner_stripping;
			vlan_ops->ena_insertion = ice_vsi_ena_inner_insertion;
			vlan_ops->dis_insertion = ice_vsi_dis_inner_insertion;
		}
	}
}

/**
 * ice_vf_vsi_cfg_dvm_legacy_vlan_mode - Config VLAN mode for old VFs in DVM
 * @vsi: VF's VSI being configured
 *
 * This should only be called when Double VLAN Mode (DVM) is enabled, there
 * is not a port VLAN enabled on this VF, and the VF negotiates
 * VIRTCHNL_VF_OFFLOAD_VLAN.
 *
 * This function sets up the VF VSI's inner and outer ice_vsi_vlan_ops and also
 * initializes software only VLAN mode (i.e. allow all VLANs). Also, use no-op
 * implementations for any functions that may be called during the lifetime of
 * the VF so these methods do nothing and succeed.
 */
void ice_vf_vsi_cfg_dvm_legacy_vlan_mode(struct ice_vsi *vsi)
{
	struct ice_vsi_vlan_ops *vlan_ops;
	struct ice_vf *vf = vsi->vf;
	struct device *dev;

	if (WARN_ON(!vf))
		return;

	dev = ice_pf_to_dev(vf->pf);

	if (!ice_is_dvm_ena(&vsi->back->hw) || ice_vf_is_port_vlan_ena(vf))
		return;

	vlan_ops = &vsi->outer_vlan_ops;

	/* Rx VLAN filtering always disabled to allow software offloaded VLANs
	 * for VFs that only support VIRTCHNL_VF_OFFLOAD_VLAN and don't have a
	 * port VLAN configured
	 */
	vlan_ops->dis_rx_filtering = ice_vsi_dis_rx_vlan_filtering;
	/* Don't fail when attempting to enable Rx VLAN filtering */
	vlan_ops->ena_rx_filtering = noop_vlan;

	/* Tx VLAN filtering always disabled to allow software offloaded VLANs
	 * for VFs that only support VIRTCHNL_VF_OFFLOAD_VLAN and don't have a
	 * port VLAN configured
	 */
	vlan_ops->dis_tx_filtering = ice_vsi_dis_tx_vlan_filtering;
	/* Don't fail when attempting to enable Tx VLAN filtering */
	vlan_ops->ena_tx_filtering = noop_vlan;

	if (vlan_ops->dis_rx_filtering(vsi))
		dev_dbg(dev, "Failed to disable Rx VLAN filtering for old VF without VIRTCHNL_VF_OFFLOAD_VLAN_V2 support\n");
	if (vlan_ops->dis_tx_filtering(vsi))
		dev_dbg(dev, "Failed to disable Tx VLAN filtering for old VF without VIRTHCNL_VF_OFFLOAD_VLAN_V2 support\n");

	/* All outer VLAN offloads must be disabled */
	vlan_ops->dis_stripping = ice_vsi_dis_outer_stripping;
	vlan_ops->dis_insertion = ice_vsi_dis_outer_insertion;

	if (vlan_ops->dis_stripping(vsi))
		dev_dbg(dev, "Failed to disable outer VLAN stripping for old VF without VIRTCHNL_VF_OFFLOAD_VLAN_V2 support\n");

	if (vlan_ops->dis_insertion(vsi))
		dev_dbg(dev, "Failed to disable outer VLAN insertion for old VF without VIRTCHNL_VF_OFFLOAD_VLAN_V2 support\n");

	/* All inner VLAN offloads must be disabled */
	vlan_ops = &vsi->inner_vlan_ops;

	vlan_ops->dis_stripping = ice_vsi_dis_outer_stripping;
	vlan_ops->dis_insertion = ice_vsi_dis_outer_insertion;

	if (vlan_ops->dis_stripping(vsi))
		dev_dbg(dev, "Failed to disable inner VLAN stripping for old VF without VIRTCHNL_VF_OFFLOAD_VLAN_V2 support\n");

	if (vlan_ops->dis_insertion(vsi))
		dev_dbg(dev, "Failed to disable inner VLAN insertion for old VF without VIRTCHNL_VF_OFFLOAD_VLAN_V2 support\n");
}

/**
 * ice_vf_vsi_cfg_svm_legacy_vlan_mode - Config VLAN mode for old VFs in SVM
 * @vsi: VF's VSI being configured
 *
 * This should only be called when Single VLAN Mode (SVM) is enabled, there is
 * not a port VLAN enabled on this VF, and the VF negotiates
 * VIRTCHNL_VF_OFFLOAD_VLAN.
 *
 * All of the normal SVM VLAN ops are identical for this case. However, by
 * default Rx VLAN filtering should be turned off by default in this case.
 */
void ice_vf_vsi_cfg_svm_legacy_vlan_mode(struct ice_vsi *vsi)
{
	struct ice_vf *vf = vsi->vf;

	if (WARN_ON(!vf))
		return;

	if (ice_is_dvm_ena(&vsi->back->hw) || ice_vf_is_port_vlan_ena(vf))
		return;

	if (vsi->inner_vlan_ops.dis_rx_filtering(vsi))
		dev_dbg(ice_pf_to_dev(vf->pf), "Failed to disable Rx VLAN filtering for old VF with VIRTCHNL_VF_OFFLOAD_VLAN support\n");
}
