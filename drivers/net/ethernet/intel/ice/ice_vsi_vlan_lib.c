// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice_vsi_vlan_lib.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice.h"

/**
 * ice_vsi_add_vlan - default add VLAN implementation for all VSI types
 * @vsi: VSI being configured
 * @vlan: VLAN filter to add
 */
int ice_vsi_add_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	int err = 0;

	if (!ice_fltr_add_vlan(vsi, vlan)) {
		vsi->num_vlan++;
	} else {
		err = -ENODEV;
		dev_err(ice_pf_to_dev(vsi->back), "Failure Adding VLAN %d on VSI %i\n",
			vlan->vid, vsi->vsi_num);
	}

	return err;
}

/**
 * ice_vsi_del_vlan - default del VLAN implementation for all VSI types
 * @vsi: VSI being configured
 * @vlan: VLAN filter to delete
 */
int ice_vsi_del_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	struct ice_pf *pf = vsi->back;
	struct device *dev;
	int err;

	dev = ice_pf_to_dev(pf);

	err = ice_fltr_remove_vlan(vsi, vlan);
	if (!err) {
		vsi->num_vlan--;
	} else if (err == -ENOENT) {
		dev_dbg(dev, "Failed to remove VLAN %d on VSI %i, it does not exist\n",
			vlan->vid, vsi->vsi_num);
		err = 0;
	} else {
		dev_err(dev, "Error removing VLAN %d on VSI %i error: %d\n",
			vlan->vid, vsi->vsi_num, err);
	}

	return err;
}

/**
 * ice_vsi_manage_vlan_insertion - Manage VLAN insertion for the VSI for Tx
 * @vsi: the VSI being changed
 */
static int ice_vsi_manage_vlan_insertion(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	int err;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	/* Here we are configuring the VSI to let the driver add VLAN tags by
	 * setting vlan_flags to ICE_AQ_VSI_VLAN_MODE_ALL. The actual VLAN tag
	 * insertion happens in the Tx hot path, in ice_tx_map.
	 */
	ctxt->info.vlan_flags = ICE_AQ_VSI_VLAN_MODE_ALL;

	/* Preserve existing VLAN strip setting */
	ctxt->info.vlan_flags |= (vsi->info.vlan_flags &
				  ICE_AQ_VSI_VLAN_EMOD_M);

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for VLAN insert failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
		goto out;
	}

	vsi->info.vlan_flags = ctxt->info.vlan_flags;
out:
	kfree(ctxt);
	return err;
}

/**
 * ice_vsi_manage_vlan_stripping - Manage VLAN stripping for the VSI for Rx
 * @vsi: the VSI being changed
 * @ena: boolean value indicating if this is a enable or disable request
 */
static int ice_vsi_manage_vlan_stripping(struct ice_vsi *vsi, bool ena)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	int err;

	/* do not allow modifying VLAN stripping when a port VLAN is configured
	 * on this VSI
	 */
	if (vsi->info.pvid)
		return 0;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	/* Here we are configuring what the VSI should do with the VLAN tag in
	 * the Rx packet. We can either leave the tag in the packet or put it in
	 * the Rx descriptor.
	 */
	if (ena)
		/* Strip VLAN tag from Rx packet and put it in the desc */
		ctxt->info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_STR_BOTH;
	else
		/* Disable stripping. Leave tag in packet */
		ctxt->info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_NOTHING;

	/* Allow all packets untagged/tagged */
	ctxt->info.vlan_flags |= ICE_AQ_VSI_VLAN_MODE_ALL;

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for VLAN strip failed, ena = %d err %d aq_err %s\n",
			ena, err, ice_aq_str(hw->adminq.sq_last_status));
		goto out;
	}

	vsi->info.vlan_flags = ctxt->info.vlan_flags;
out:
	kfree(ctxt);
	return err;
}

int ice_vsi_ena_stripping(struct ice_vsi *vsi)
{
	return ice_vsi_manage_vlan_stripping(vsi, true);
}

int ice_vsi_dis_stripping(struct ice_vsi *vsi)
{
	return ice_vsi_manage_vlan_stripping(vsi, false);
}

int ice_vsi_ena_insertion(struct ice_vsi *vsi)
{
	return ice_vsi_manage_vlan_insertion(vsi);
}

int ice_vsi_dis_insertion(struct ice_vsi *vsi)
{
	return ice_vsi_manage_vlan_insertion(vsi);
}

/**
 * ice_vsi_manage_pvid - Enable or disable port VLAN for VSI
 * @vsi: the VSI to update
 * @pvid_info: VLAN ID and QoS used to set the PVID VSI context field
 * @enable: true for enable PVID false for disable
 */
static int ice_vsi_manage_pvid(struct ice_vsi *vsi, u16 pvid_info, bool enable)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_aqc_vsi_props *info;
	struct ice_vsi_ctx *ctxt;
	int ret;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info = vsi->info;
	info = &ctxt->info;
	if (enable) {
		info->vlan_flags = ICE_AQ_VSI_VLAN_MODE_UNTAGGED |
			ICE_AQ_VSI_PVLAN_INSERT_PVID |
			ICE_AQ_VSI_VLAN_EMOD_STR;
		info->sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	} else {
		info->vlan_flags = ICE_AQ_VSI_VLAN_EMOD_NOTHING |
			ICE_AQ_VSI_VLAN_MODE_ALL;
		info->sw_flags2 &= ~ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	}

	info->pvid = cpu_to_le16(pvid_info);
	info->valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID |
					   ICE_AQ_VSI_PROP_SW_VALID);

	ret = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (ret) {
		dev_info(ice_hw_to_dev(hw), "update VSI for port VLAN failed, err %d aq_err %s\n",
			 ret, ice_aq_str(hw->adminq.sq_last_status));
		goto out;
	}

	vsi->info.vlan_flags = info->vlan_flags;
	vsi->info.sw_flags2 = info->sw_flags2;
	vsi->info.pvid = info->pvid;
out:
	kfree(ctxt);
	return ret;
}

int ice_vsi_set_port_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	u16 port_vlan_info;

	if (vlan->prio > 7)
		return -EINVAL;

	port_vlan_info = vlan->vid | (vlan->prio << VLAN_PRIO_SHIFT);

	return ice_vsi_manage_pvid(vsi, port_vlan_info, true);
}

/**
 * ice_cfg_vlan_pruning - enable or disable VLAN pruning on the VSI
 * @vsi: VSI to enable or disable VLAN pruning on
 * @ena: set to true to enable VLAN pruning and false to disable it
 *
 * returns 0 if VSI is updated, negative otherwise
 */
static int ice_cfg_vlan_pruning(struct ice_vsi *vsi, bool ena)
{
	struct ice_vsi_ctx *ctxt;
	struct ice_pf *pf;
	int status;

	if (!vsi)
		return -EINVAL;

	/* Don't enable VLAN pruning if the netdev is currently in promiscuous
	 * mode. VLAN pruning will be enabled when the interface exits
	 * promiscuous mode if any VLAN filters are active.
	 */
	if (vsi->netdev && vsi->netdev->flags & IFF_PROMISC && ena)
		return 0;

	pf = vsi->back;
	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info = vsi->info;

	if (ena)
		ctxt->info.sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;
	else
		ctxt->info.sw_flags2 &= ~ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SW_VALID);

	status = ice_update_vsi(&pf->hw, vsi->idx, ctxt, NULL);
	if (status) {
		netdev_err(vsi->netdev, "%sabling VLAN pruning on VSI handle: %d, VSI HW ID: %d failed, err = %d, aq_err = %s\n",
			   ena ? "En" : "Dis", vsi->idx, vsi->vsi_num, status,
			   ice_aq_str(pf->hw.adminq.sq_last_status));
		goto err_out;
	}

	vsi->info.sw_flags2 = ctxt->info.sw_flags2;

	kfree(ctxt);
	return 0;

err_out:
	kfree(ctxt);
	return status;
}

int ice_vsi_ena_rx_vlan_filtering(struct ice_vsi *vsi)
{
	return ice_cfg_vlan_pruning(vsi, true);
}

int ice_vsi_dis_rx_vlan_filtering(struct ice_vsi *vsi)
{
	return ice_cfg_vlan_pruning(vsi, false);
}

static int ice_cfg_vlan_antispoof(struct ice_vsi *vsi, bool enable)
{
	struct ice_vsi_ctx *ctx;
	int err;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->info.sec_flags = vsi->info.sec_flags;
	ctx->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_SECURITY_VALID);

	if (enable)
		ctx->info.sec_flags |= ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA <<
			ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S;
	else
		ctx->info.sec_flags &= ~(ICE_AQ_VSI_SEC_TX_VLAN_PRUNE_ENA <<
					 ICE_AQ_VSI_SEC_TX_PRUNE_ENA_S);

	err = ice_update_vsi(&vsi->back->hw, vsi->idx, ctx, NULL);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "Failed to configure Tx VLAN anti-spoof %s for VSI %d, error %d\n",
			enable ? "ON" : "OFF", vsi->vsi_num, err);
	else
		vsi->info.sec_flags = ctx->info.sec_flags;

	kfree(ctx);

	return err;
}

int ice_vsi_ena_tx_vlan_filtering(struct ice_vsi *vsi)
{
	return ice_cfg_vlan_antispoof(vsi, true);
}

int ice_vsi_dis_tx_vlan_filtering(struct ice_vsi *vsi)
{
	return ice_cfg_vlan_antispoof(vsi, false);
}
