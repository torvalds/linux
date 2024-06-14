// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2021, Intel Corporation. */

#include "ice_vsi_vlan_lib.h"
#include "ice_lib.h"
#include "ice_fltr.h"
#include "ice.h"

static void print_invalid_tpid(struct ice_vsi *vsi, u16 tpid)
{
	dev_err(ice_pf_to_dev(vsi->back), "%s %d specified invalid VLAN tpid 0x%04x\n",
		ice_vsi_type_str(vsi->type), vsi->idx, tpid);
}

/**
 * validate_vlan - check if the ice_vlan passed in is valid
 * @vsi: VSI used for printing error message
 * @vlan: ice_vlan structure to validate
 *
 * Return true if the VLAN TPID is valid or if the VLAN TPID is 0 and the VLAN
 * VID is 0, which allows for non-zero VLAN filters with the specified VLAN TPID
 * and untagged VLAN 0 filters to be added to the prune list respectively.
 */
static bool validate_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	if (vlan->tpid != ETH_P_8021Q && vlan->tpid != ETH_P_8021AD &&
	    vlan->tpid != ETH_P_QINQ1 && (vlan->tpid || vlan->vid)) {
		print_invalid_tpid(vsi, vlan->tpid);
		return false;
	}

	return true;
}

/**
 * ice_vsi_add_vlan - default add VLAN implementation for all VSI types
 * @vsi: VSI being configured
 * @vlan: VLAN filter to add
 */
int ice_vsi_add_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	int err;

	if (!validate_vlan(vsi, vlan))
		return -EINVAL;

	err = ice_fltr_add_vlan(vsi, vlan);
	if (!err)
		vsi->num_vlan++;
	else if (err == -EEXIST)
		err = 0;
	else
		dev_err(ice_pf_to_dev(vsi->back), "Failure Adding VLAN %d on VSI %i, status %d\n",
			vlan->vid, vsi->vsi_num, err);

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

	if (!validate_vlan(vsi, vlan))
		return -EINVAL;

	dev = ice_pf_to_dev(pf);

	err = ice_fltr_remove_vlan(vsi, vlan);
	if (!err)
		vsi->num_vlan--;
	else if (err == -ENOENT || err == -EBUSY)
		err = 0;
	else
		dev_err(dev, "Error removing VLAN %d on VSI %i error: %d\n",
			vlan->vid, vsi->vsi_num, err);

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
	 * setting inner_vlan_flags to ICE_AQ_VSI_INNER_VLAN_TX_MODE_ALL. The actual VLAN tag
	 * insertion happens in the Tx hot path, in ice_tx_map.
	 */
	ctxt->info.inner_vlan_flags = ICE_AQ_VSI_INNER_VLAN_TX_MODE_ALL;

	/* Preserve existing VLAN strip setting */
	ctxt->info.inner_vlan_flags |= (vsi->info.inner_vlan_flags &
					ICE_AQ_VSI_INNER_VLAN_EMODE_M);

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for VLAN insert failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
		goto out;
	}

	vsi->info.inner_vlan_flags = ctxt->info.inner_vlan_flags;
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
	u8 *ivf;
	int err;

	/* do not allow modifying VLAN stripping when a port VLAN is configured
	 * on this VSI
	 */
	if (vsi->info.port_based_inner_vlan)
		return 0;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ivf = &ctxt->info.inner_vlan_flags;

	/* Here we are configuring what the VSI should do with the VLAN tag in
	 * the Rx packet. We can either leave the tag in the packet or put it in
	 * the Rx descriptor.
	 */
	if (ena) {
		/* Strip VLAN tag from Rx packet and put it in the desc */
		*ivf = FIELD_PREP(ICE_AQ_VSI_INNER_VLAN_EMODE_M,
				  ICE_AQ_VSI_INNER_VLAN_EMODE_STR_BOTH);
	} else {
		/* Disable stripping. Leave tag in packet */
		*ivf = FIELD_PREP(ICE_AQ_VSI_INNER_VLAN_EMODE_M,
				  ICE_AQ_VSI_INNER_VLAN_EMODE_NOTHING);
	}

	/* Allow all packets untagged/tagged */
	*ivf |= ICE_AQ_VSI_INNER_VLAN_TX_MODE_ALL;

	ctxt->info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for VLAN strip failed, ena = %d err %d aq_err %s\n",
			ena, err, ice_aq_str(hw->adminq.sq_last_status));
		goto out;
	}

	vsi->info.inner_vlan_flags = ctxt->info.inner_vlan_flags;
out:
	kfree(ctxt);
	return err;
}

int ice_vsi_ena_inner_stripping(struct ice_vsi *vsi, const u16 tpid)
{
	if (tpid != ETH_P_8021Q) {
		print_invalid_tpid(vsi, tpid);
		return -EINVAL;
	}

	return ice_vsi_manage_vlan_stripping(vsi, true);
}

int ice_vsi_dis_inner_stripping(struct ice_vsi *vsi)
{
	return ice_vsi_manage_vlan_stripping(vsi, false);
}

int ice_vsi_ena_inner_insertion(struct ice_vsi *vsi, const u16 tpid)
{
	if (tpid != ETH_P_8021Q) {
		print_invalid_tpid(vsi, tpid);
		return -EINVAL;
	}

	return ice_vsi_manage_vlan_insertion(vsi);
}

int ice_vsi_dis_inner_insertion(struct ice_vsi *vsi)
{
	return ice_vsi_manage_vlan_insertion(vsi);
}

static void
ice_save_vlan_info(struct ice_aqc_vsi_props *info,
		   struct ice_vsi_vlan_info *vlan)
{
	vlan->sw_flags2 = info->sw_flags2;
	vlan->inner_vlan_flags = info->inner_vlan_flags;
	vlan->outer_vlan_flags = info->outer_vlan_flags;
}

static void
ice_restore_vlan_info(struct ice_aqc_vsi_props *info,
		      struct ice_vsi_vlan_info *vlan)
{
	info->sw_flags2 = vlan->sw_flags2;
	info->inner_vlan_flags = vlan->inner_vlan_flags;
	info->outer_vlan_flags = vlan->outer_vlan_flags;
}

/**
 * __ice_vsi_set_inner_port_vlan - set port VLAN VSI context settings to enable a port VLAN
 * @vsi: the VSI to update
 * @pvid_info: VLAN ID and QoS used to set the PVID VSI context field
 */
static int __ice_vsi_set_inner_port_vlan(struct ice_vsi *vsi, u16 pvid_info)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_aqc_vsi_props *info;
	struct ice_vsi_ctx *ctxt;
	int ret;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ice_save_vlan_info(&vsi->info, &vsi->vlan_info);
	ctxt->info = vsi->info;
	info = &ctxt->info;
	info->inner_vlan_flags = ICE_AQ_VSI_INNER_VLAN_TX_MODE_ACCEPTUNTAGGED |
		ICE_AQ_VSI_INNER_VLAN_INSERT_PVID |
		ICE_AQ_VSI_INNER_VLAN_EMODE_STR;
	info->sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;

	info->port_based_inner_vlan = cpu_to_le16(pvid_info);
	info->valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID |
					   ICE_AQ_VSI_PROP_SW_VALID);

	ret = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (ret) {
		dev_info(ice_hw_to_dev(hw), "update VSI for port VLAN failed, err %d aq_err %s\n",
			 ret, ice_aq_str(hw->adminq.sq_last_status));
		goto out;
	}

	vsi->info.inner_vlan_flags = info->inner_vlan_flags;
	vsi->info.sw_flags2 = info->sw_flags2;
	vsi->info.port_based_inner_vlan = info->port_based_inner_vlan;
out:
	kfree(ctxt);
	return ret;
}

int ice_vsi_set_inner_port_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	u16 port_vlan_info;

	if (vlan->tpid != ETH_P_8021Q)
		return -EINVAL;

	if (vlan->prio > 7)
		return -EINVAL;

	port_vlan_info = vlan->vid | (vlan->prio << VLAN_PRIO_SHIFT);

	return __ice_vsi_set_inner_port_vlan(vsi, port_vlan_info);
}

int ice_vsi_clear_inner_port_vlan(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_aqc_vsi_props *info;
	struct ice_vsi_ctx *ctxt;
	int ret;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ice_restore_vlan_info(&vsi->info, &vsi->vlan_info);
	vsi->info.port_based_inner_vlan = 0;
	ctxt->info = vsi->info;
	info = &ctxt->info;
	info->valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID |
					   ICE_AQ_VSI_PROP_SW_VALID);

	ret = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (ret)
		dev_err(ice_hw_to_dev(hw), "update VSI for port VLAN failed, err %d aq_err %s\n",
			ret, ice_aq_str(hw->adminq.sq_last_status));

	kfree(ctxt);
	return ret;
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

/**
 * tpid_to_vsi_outer_vlan_type - convert from TPID to VSI context based tag_type
 * @tpid: tpid used to translate into VSI context based tag_type
 * @tag_type: output variable to hold the VSI context based tag type
 */
static int tpid_to_vsi_outer_vlan_type(u16 tpid, u8 *tag_type)
{
	switch (tpid) {
	case ETH_P_8021Q:
		*tag_type = ICE_AQ_VSI_OUTER_TAG_VLAN_8100;
		break;
	case ETH_P_8021AD:
		*tag_type = ICE_AQ_VSI_OUTER_TAG_STAG;
		break;
	case ETH_P_QINQ1:
		*tag_type = ICE_AQ_VSI_OUTER_TAG_VLAN_9100;
		break;
	default:
		*tag_type = 0;
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_vsi_ena_outer_stripping - enable outer VLAN stripping
 * @vsi: VSI to configure
 * @tpid: TPID to enable outer VLAN stripping for
 *
 * Enable outer VLAN stripping via VSI context. This function should only be
 * used if DVM is supported. Also, this function should never be called directly
 * as it should be part of ice_vsi_vlan_ops if it's needed.
 *
 * Since the VSI context only supports a single TPID for insertion and
 * stripping, setting the TPID for stripping will affect the TPID for insertion.
 * Callers need to be aware of this limitation.
 *
 * Only modify outer VLAN stripping settings and the VLAN TPID. Outer VLAN
 * insertion settings are unmodified.
 *
 * This enables hardware to strip a VLAN tag with the specified TPID to be
 * stripped from the packet and placed in the receive descriptor.
 */
int ice_vsi_ena_outer_stripping(struct ice_vsi *vsi, u16 tpid)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	u8 tag_type;
	int err;

	/* do not allow modifying VLAN stripping when a port VLAN is configured
	 * on this VSI
	 */
	if (vsi->info.port_based_outer_vlan)
		return 0;

	if (tpid_to_vsi_outer_vlan_type(tpid, &tag_type))
		return -EINVAL;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info.valid_sections =
		cpu_to_le16(ICE_AQ_VSI_PROP_OUTER_TAG_VALID);
	/* clear current outer VLAN strip settings */
	ctxt->info.outer_vlan_flags = vsi->info.outer_vlan_flags &
		~(ICE_AQ_VSI_OUTER_VLAN_EMODE_M | ICE_AQ_VSI_OUTER_TAG_TYPE_M);
	ctxt->info.outer_vlan_flags |=
		/* we want EMODE_SHOW_BOTH, but that value is zero, so the line
		 * above clears it well enough that we don't need to try to set
		 * zero here, so just do the tag type
		 */
		 FIELD_PREP(ICE_AQ_VSI_OUTER_TAG_TYPE_M, tag_type);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for enabling outer VLAN stripping failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
	else
		vsi->info.outer_vlan_flags = ctxt->info.outer_vlan_flags;

	kfree(ctxt);
	return err;
}

/**
 * ice_vsi_dis_outer_stripping - disable outer VLAN stripping
 * @vsi: VSI to configure
 *
 * Disable outer VLAN stripping via VSI context. This function should only be
 * used if DVM is supported. Also, this function should never be called directly
 * as it should be part of ice_vsi_vlan_ops if it's needed.
 *
 * Only modify the outer VLAN stripping settings. The VLAN TPID and outer VLAN
 * insertion settings are unmodified.
 *
 * This tells the hardware to not strip any VLAN tagged packets, thus leaving
 * them in the packet. This enables software offloaded VLAN stripping and
 * disables hardware offloaded VLAN stripping.
 */
int ice_vsi_dis_outer_stripping(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	int err;

	if (vsi->info.port_based_outer_vlan)
		return 0;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info.valid_sections =
		cpu_to_le16(ICE_AQ_VSI_PROP_OUTER_TAG_VALID);
	/* clear current outer VLAN strip settings */
	ctxt->info.outer_vlan_flags = vsi->info.outer_vlan_flags &
		~ICE_AQ_VSI_OUTER_VLAN_EMODE_M;
	ctxt->info.outer_vlan_flags |= ICE_AQ_VSI_OUTER_VLAN_EMODE_NOTHING <<
		ICE_AQ_VSI_OUTER_VLAN_EMODE_S;

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for disabling outer VLAN stripping failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
	else
		vsi->info.outer_vlan_flags = ctxt->info.outer_vlan_flags;

	kfree(ctxt);
	return err;
}

/**
 * ice_vsi_ena_outer_insertion - enable outer VLAN insertion
 * @vsi: VSI to configure
 * @tpid: TPID to enable outer VLAN insertion for
 *
 * Enable outer VLAN insertion via VSI context. This function should only be
 * used if DVM is supported. Also, this function should never be called directly
 * as it should be part of ice_vsi_vlan_ops if it's needed.
 *
 * Since the VSI context only supports a single TPID for insertion and
 * stripping, setting the TPID for insertion will affect the TPID for stripping.
 * Callers need to be aware of this limitation.
 *
 * Only modify outer VLAN insertion settings and the VLAN TPID. Outer VLAN
 * stripping settings are unmodified.
 *
 * This allows a VLAN tag with the specified TPID to be inserted in the transmit
 * descriptor.
 */
int ice_vsi_ena_outer_insertion(struct ice_vsi *vsi, u16 tpid)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	u8 tag_type;
	int err;

	if (vsi->info.port_based_outer_vlan)
		return 0;

	if (tpid_to_vsi_outer_vlan_type(tpid, &tag_type))
		return -EINVAL;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info.valid_sections =
		cpu_to_le16(ICE_AQ_VSI_PROP_OUTER_TAG_VALID);
	/* clear current outer VLAN insertion settings */
	ctxt->info.outer_vlan_flags = vsi->info.outer_vlan_flags &
		~(ICE_AQ_VSI_OUTER_VLAN_PORT_BASED_INSERT |
		  ICE_AQ_VSI_OUTER_VLAN_BLOCK_TX_DESC |
		  ICE_AQ_VSI_OUTER_VLAN_TX_MODE_M |
		  ICE_AQ_VSI_OUTER_TAG_TYPE_M);
	ctxt->info.outer_vlan_flags |=
		FIELD_PREP(ICE_AQ_VSI_OUTER_VLAN_TX_MODE_M,
			   ICE_AQ_VSI_OUTER_VLAN_TX_MODE_ALL) |
		FIELD_PREP(ICE_AQ_VSI_OUTER_TAG_TYPE_M, tag_type);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for enabling outer VLAN insertion failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
	else
		vsi->info.outer_vlan_flags = ctxt->info.outer_vlan_flags;

	kfree(ctxt);
	return err;
}

/**
 * ice_vsi_dis_outer_insertion - disable outer VLAN insertion
 * @vsi: VSI to configure
 *
 * Disable outer VLAN insertion via VSI context. This function should only be
 * used if DVM is supported. Also, this function should never be called directly
 * as it should be part of ice_vsi_vlan_ops if it's needed.
 *
 * Only modify the outer VLAN insertion settings. The VLAN TPID and outer VLAN
 * settings are unmodified.
 *
 * This tells the hardware to not allow any VLAN tagged packets in the transmit
 * descriptor. This enables software offloaded VLAN insertion and disables
 * hardware offloaded VLAN insertion.
 */
int ice_vsi_dis_outer_insertion(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	int err;

	if (vsi->info.port_based_outer_vlan)
		return 0;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ctxt->info.valid_sections =
		cpu_to_le16(ICE_AQ_VSI_PROP_OUTER_TAG_VALID);
	/* clear current outer VLAN insertion settings */
	ctxt->info.outer_vlan_flags = vsi->info.outer_vlan_flags &
		~(ICE_AQ_VSI_OUTER_VLAN_PORT_BASED_INSERT |
		  ICE_AQ_VSI_OUTER_VLAN_TX_MODE_M);
	ctxt->info.outer_vlan_flags |=
		ICE_AQ_VSI_OUTER_VLAN_BLOCK_TX_DESC |
		FIELD_PREP(ICE_AQ_VSI_OUTER_VLAN_TX_MODE_M,
			   ICE_AQ_VSI_OUTER_VLAN_TX_MODE_ALL);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for disabling outer VLAN insertion failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
	else
		vsi->info.outer_vlan_flags = ctxt->info.outer_vlan_flags;

	kfree(ctxt);
	return err;
}

/**
 * __ice_vsi_set_outer_port_vlan - set the outer port VLAN and related settings
 * @vsi: VSI to configure
 * @vlan_info: packed u16 that contains the VLAN prio and ID
 * @tpid: TPID of the port VLAN
 *
 * Set the port VLAN prio, ID, and TPID.
 *
 * Enable VLAN pruning so the VSI doesn't receive any traffic that doesn't match
 * a VLAN prune rule. The caller should take care to add a VLAN prune rule that
 * matches the port VLAN ID and TPID.
 *
 * Tell hardware to strip outer VLAN tagged packets on receive and don't put
 * them in the receive descriptor. VSI(s) in port VLANs should not be aware of
 * the port VLAN ID or TPID they are assigned to.
 *
 * Tell hardware to prevent outer VLAN tag insertion on transmit and only allow
 * untagged outer packets from the transmit descriptor.
 *
 * Also, tell the hardware to insert the port VLAN on transmit.
 */
static int
__ice_vsi_set_outer_port_vlan(struct ice_vsi *vsi, u16 vlan_info, u16 tpid)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	u8 tag_type;
	int err;

	if (tpid_to_vsi_outer_vlan_type(tpid, &tag_type))
		return -EINVAL;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ice_save_vlan_info(&vsi->info, &vsi->vlan_info);
	ctxt->info = vsi->info;

	ctxt->info.sw_flags2 |= ICE_AQ_VSI_SW_FLAG_RX_VLAN_PRUNE_ENA;

	ctxt->info.port_based_outer_vlan = cpu_to_le16(vlan_info);
	ctxt->info.outer_vlan_flags =
		(ICE_AQ_VSI_OUTER_VLAN_EMODE_SHOW <<
		 ICE_AQ_VSI_OUTER_VLAN_EMODE_S) |
		FIELD_PREP(ICE_AQ_VSI_OUTER_TAG_TYPE_M, tag_type) |
		ICE_AQ_VSI_OUTER_VLAN_BLOCK_TX_DESC |
		(ICE_AQ_VSI_OUTER_VLAN_TX_MODE_ACCEPTUNTAGGED <<
		 ICE_AQ_VSI_OUTER_VLAN_TX_MODE_S) |
		ICE_AQ_VSI_OUTER_VLAN_PORT_BASED_INSERT;

	ctxt->info.valid_sections =
		cpu_to_le16(ICE_AQ_VSI_PROP_OUTER_TAG_VALID |
			    ICE_AQ_VSI_PROP_SW_VALID);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err) {
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for setting outer port based VLAN failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
	} else {
		vsi->info.port_based_outer_vlan = ctxt->info.port_based_outer_vlan;
		vsi->info.outer_vlan_flags = ctxt->info.outer_vlan_flags;
		vsi->info.sw_flags2 = ctxt->info.sw_flags2;
	}

	kfree(ctxt);
	return err;
}

/**
 * ice_vsi_set_outer_port_vlan - public version of __ice_vsi_set_outer_port_vlan
 * @vsi: VSI to configure
 * @vlan: ice_vlan structure used to set the port VLAN
 *
 * Set the outer port VLAN via VSI context. This function should only be
 * used if DVM is supported. Also, this function should never be called directly
 * as it should be part of ice_vsi_vlan_ops if it's needed.
 *
 * Use the ice_vlan structure passed in to set this VSI in a port VLAN.
 */
int ice_vsi_set_outer_port_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	u16 port_vlan_info;

	if (vlan->prio > (VLAN_PRIO_MASK >> VLAN_PRIO_SHIFT))
		return -EINVAL;

	port_vlan_info = vlan->vid | (vlan->prio << VLAN_PRIO_SHIFT);

	return __ice_vsi_set_outer_port_vlan(vsi, port_vlan_info, vlan->tpid);
}

/**
 * ice_vsi_clear_outer_port_vlan - clear outer port vlan
 * @vsi: VSI to configure
 *
 * The function is restoring previously set vlan config (saved in
 * vsi->vlan_info). Setting happens in port vlan configuration.
 */
int ice_vsi_clear_outer_port_vlan(struct ice_vsi *vsi)
{
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx *ctxt;
	int err;

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt)
		return -ENOMEM;

	ice_restore_vlan_info(&vsi->info, &vsi->vlan_info);
	vsi->info.port_based_outer_vlan = 0;
	ctxt->info = vsi->info;

	ctxt->info.valid_sections =
		cpu_to_le16(ICE_AQ_VSI_PROP_OUTER_TAG_VALID |
			    ICE_AQ_VSI_PROP_SW_VALID);

	err = ice_update_vsi(hw, vsi->idx, ctxt, NULL);
	if (err)
		dev_err(ice_pf_to_dev(vsi->back), "update VSI for clearing outer port based VLAN failed, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));

	kfree(ctxt);
	return err;
}
