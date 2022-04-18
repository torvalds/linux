// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice_pf_vsi_vlan_ops.h"
#include "ice_vf_vsi_vlan_ops.h"
#include "ice_lib.h"
#include "ice.h"

static int
op_unsupported_vlan_arg(struct ice_vsi * __always_unused vsi,
			struct ice_vlan * __always_unused vlan)
{
	return -EOPNOTSUPP;
}

static int
op_unsupported_tpid_arg(struct ice_vsi *__always_unused vsi,
			u16 __always_unused tpid)
{
	return -EOPNOTSUPP;
}

static int op_unsupported(struct ice_vsi *__always_unused vsi)
{
	return -EOPNOTSUPP;
}

/* If any new ops are added to the VSI VLAN ops interface then an unsupported
 * implementation should be set here.
 */
static struct ice_vsi_vlan_ops ops_unsupported = {
	.add_vlan = op_unsupported_vlan_arg,
	.del_vlan = op_unsupported_vlan_arg,
	.ena_stripping = op_unsupported_tpid_arg,
	.dis_stripping = op_unsupported,
	.ena_insertion = op_unsupported_tpid_arg,
	.dis_insertion = op_unsupported,
	.ena_rx_filtering = op_unsupported,
	.dis_rx_filtering = op_unsupported,
	.ena_tx_filtering = op_unsupported,
	.dis_tx_filtering = op_unsupported,
	.set_port_vlan = op_unsupported_vlan_arg,
};

/**
 * ice_vsi_init_unsupported_vlan_ops - init all VSI VLAN ops to unsupported
 * @vsi: VSI to initialize VSI VLAN ops to unsupported for
 *
 * By default all inner and outer VSI VLAN ops return -EOPNOTSUPP. This was done
 * as oppsed to leaving the ops null to prevent unexpected crashes. Instead if
 * an unsupported VSI VLAN op is called it will just return -EOPNOTSUPP.
 *
 */
static void ice_vsi_init_unsupported_vlan_ops(struct ice_vsi *vsi)
{
	vsi->outer_vlan_ops = ops_unsupported;
	vsi->inner_vlan_ops = ops_unsupported;
}

/**
 * ice_vsi_init_vlan_ops - initialize type specific VSI VLAN ops
 * @vsi: VSI to initialize ops for
 *
 * If any VSI types are added and/or require different ops than the PF or VF VSI
 * then they will have to add a case here to handle that. Also, VSI type
 * specific files should be added in the same manner that was done for PF VSI.
 */
void ice_vsi_init_vlan_ops(struct ice_vsi *vsi)
{
	/* Initialize all VSI types to have unsupported VSI VLAN ops */
	ice_vsi_init_unsupported_vlan_ops(vsi);

	switch (vsi->type) {
	case ICE_VSI_PF:
	case ICE_VSI_SWITCHDEV_CTRL:
		ice_pf_vsi_init_vlan_ops(vsi);
		break;
	case ICE_VSI_VF:
		ice_vf_vsi_init_vlan_ops(vsi);
		break;
	default:
		dev_dbg(ice_pf_to_dev(vsi->back), "%s does not support VLAN operations\n",
			ice_vsi_type_str(vsi->type));
		break;
	}
}

/**
 * ice_get_compat_vsi_vlan_ops - Get VSI VLAN ops based on VLAN mode
 * @vsi: VSI used to get the VSI VLAN ops
 *
 * This function is meant to be used when the caller doesn't know which VLAN ops
 * to use (i.e. inner or outer). This allows backward compatibility for VLANs
 * since most of the Outer VSI VLAN functins are not supported when
 * the device is configured in Single VLAN Mode (SVM).
 */
struct ice_vsi_vlan_ops *ice_get_compat_vsi_vlan_ops(struct ice_vsi *vsi)
{
	if (ice_is_dvm_ena(&vsi->back->hw))
		return &vsi->outer_vlan_ops;
	else
		return &vsi->inner_vlan_ops;
}
