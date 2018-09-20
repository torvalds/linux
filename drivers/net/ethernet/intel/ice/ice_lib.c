// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"

/**
 * ice_add_mac_to_list - Add a mac address filter entry to the list
 * @vsi: the VSI to be forwarded to
 * @add_list: pointer to the list which contains MAC filter entries
 * @macaddr: the MAC address to be added.
 *
 * Adds mac address filter entry to the temp list
 *
 * Returns 0 on success or ENOMEM on failure.
 */
int ice_add_mac_to_list(struct ice_vsi *vsi, struct list_head *add_list,
			const u8 *macaddr)
{
	struct ice_fltr_list_entry *tmp;
	struct ice_pf *pf = vsi->back;

	tmp = devm_kzalloc(&pf->pdev->dev, sizeof(*tmp), GFP_ATOMIC);
	if (!tmp)
		return -ENOMEM;

	tmp->fltr_info.flag = ICE_FLTR_TX;
	tmp->fltr_info.src = vsi->vsi_num;
	tmp->fltr_info.lkup_type = ICE_SW_LKUP_MAC;
	tmp->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	tmp->fltr_info.fwd_id.vsi_id = vsi->vsi_num;
	ether_addr_copy(tmp->fltr_info.l_data.mac.mac_addr, macaddr);

	INIT_LIST_HEAD(&tmp->list_entry);
	list_add(&tmp->list_entry, add_list);

	return 0;
}

/**
 * ice_update_eth_stats - Update VSI-specific ethernet statistics counters
 * @vsi: the VSI to be updated
 */
void ice_update_eth_stats(struct ice_vsi *vsi)
{
	struct ice_eth_stats *prev_es, *cur_es;
	struct ice_hw *hw = &vsi->back->hw;
	u16 vsi_num = vsi->vsi_num;    /* HW absolute index of a VSI */

	prev_es = &vsi->eth_stats_prev;
	cur_es = &vsi->eth_stats;

	ice_stat_update40(hw, GLV_GORCH(vsi_num), GLV_GORCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_bytes,
			  &cur_es->rx_bytes);

	ice_stat_update40(hw, GLV_UPRCH(vsi_num), GLV_UPRCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_unicast,
			  &cur_es->rx_unicast);

	ice_stat_update40(hw, GLV_MPRCH(vsi_num), GLV_MPRCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_multicast,
			  &cur_es->rx_multicast);

	ice_stat_update40(hw, GLV_BPRCH(vsi_num), GLV_BPRCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->rx_broadcast,
			  &cur_es->rx_broadcast);

	ice_stat_update32(hw, GLV_RDPC(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->rx_discards, &cur_es->rx_discards);

	ice_stat_update40(hw, GLV_GOTCH(vsi_num), GLV_GOTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_bytes,
			  &cur_es->tx_bytes);

	ice_stat_update40(hw, GLV_UPTCH(vsi_num), GLV_UPTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_unicast,
			  &cur_es->tx_unicast);

	ice_stat_update40(hw, GLV_MPTCH(vsi_num), GLV_MPTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_multicast,
			  &cur_es->tx_multicast);

	ice_stat_update40(hw, GLV_BPTCH(vsi_num), GLV_BPTCL(vsi_num),
			  vsi->stat_offsets_loaded, &prev_es->tx_broadcast,
			  &cur_es->tx_broadcast);

	ice_stat_update32(hw, GLV_TEPC(vsi_num), vsi->stat_offsets_loaded,
			  &prev_es->tx_errors, &cur_es->tx_errors);

	vsi->stat_offsets_loaded = true;
}

/**
 * ice_free_fltr_list - free filter lists helper
 * @dev: pointer to the device struct
 * @h: pointer to the list head to be freed
 *
 * Helper function to free filter lists previously created using
 * ice_add_mac_to_list
 */
void ice_free_fltr_list(struct device *dev, struct list_head *h)
{
	struct ice_fltr_list_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, h, list_entry) {
		list_del(&e->list_entry);
		devm_kfree(dev, e);
	}
}

/**
 * ice_vsi_add_vlan - Add VSI membership for given VLAN
 * @vsi: the VSI being configured
 * @vid: VLAN id to be added
 */
int ice_vsi_add_vlan(struct ice_vsi *vsi, u16 vid)
{
	struct ice_fltr_list_entry *tmp;
	struct ice_pf *pf = vsi->back;
	LIST_HEAD(tmp_add_list);
	enum ice_status status;
	int err = 0;

	tmp = devm_kzalloc(&pf->pdev->dev, sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tmp->fltr_info.lkup_type = ICE_SW_LKUP_VLAN;
	tmp->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	tmp->fltr_info.flag = ICE_FLTR_TX;
	tmp->fltr_info.src = vsi->vsi_num;
	tmp->fltr_info.fwd_id.vsi_id = vsi->vsi_num;
	tmp->fltr_info.l_data.vlan.vlan_id = vid;

	INIT_LIST_HEAD(&tmp->list_entry);
	list_add(&tmp->list_entry, &tmp_add_list);

	status = ice_add_vlan(&pf->hw, &tmp_add_list);
	if (status) {
		err = -ENODEV;
		dev_err(&pf->pdev->dev, "Failure Adding VLAN %d on VSI %i\n",
			vid, vsi->vsi_num);
	}

	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
	return err;
}

/**
 * ice_vsi_kill_vlan - Remove VSI membership for a given VLAN
 * @vsi: the VSI being configured
 * @vid: VLAN id to be removed
 *
 * Returns 0 on success and negative on failure
 */
int ice_vsi_kill_vlan(struct ice_vsi *vsi, u16 vid)
{
	struct ice_fltr_list_entry *list;
	struct ice_pf *pf = vsi->back;
	LIST_HEAD(tmp_add_list);
	int status = 0;

	list = devm_kzalloc(&pf->pdev->dev, sizeof(*list), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	list->fltr_info.lkup_type = ICE_SW_LKUP_VLAN;
	list->fltr_info.fwd_id.vsi_id = vsi->vsi_num;
	list->fltr_info.fltr_act = ICE_FWD_TO_VSI;
	list->fltr_info.l_data.vlan.vlan_id = vid;
	list->fltr_info.flag = ICE_FLTR_TX;
	list->fltr_info.src = vsi->vsi_num;

	INIT_LIST_HEAD(&list->list_entry);
	list_add(&list->list_entry, &tmp_add_list);

	if (ice_remove_vlan(&pf->hw, &tmp_add_list)) {
		dev_err(&pf->pdev->dev, "Error removing VLAN %d on vsi %i\n",
			vid, vsi->vsi_num);
		status = -EIO;
	}

	ice_free_fltr_list(&pf->pdev->dev, &tmp_add_list);
	return status;
}

/**
 * ice_vsi_manage_vlan_insertion - Manage VLAN insertion for the VSI for Tx
 * @vsi: the VSI being changed
 */
int ice_vsi_manage_vlan_insertion(struct ice_vsi *vsi)
{
	struct device *dev = &vsi->back->pdev->dev;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx ctxt = { 0 };
	enum ice_status status;

	/* Here we are configuring the VSI to let the driver add VLAN tags by
	 * setting vlan_flags to ICE_AQ_VSI_VLAN_MODE_ALL. The actual VLAN tag
	 * insertion happens in the Tx hot path, in ice_tx_map.
	 */
	ctxt.info.vlan_flags = ICE_AQ_VSI_VLAN_MODE_ALL;

	ctxt.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);
	ctxt.vsi_num = vsi->vsi_num;

	status = ice_aq_update_vsi(hw, &ctxt, NULL);
	if (status) {
		dev_err(dev, "update VSI for VLAN insert failed, err %d aq_err %d\n",
			status, hw->adminq.sq_last_status);
		return -EIO;
	}

	vsi->info.vlan_flags = ctxt.info.vlan_flags;
	return 0;
}

/**
 * ice_vsi_manage_vlan_stripping - Manage VLAN stripping for the VSI for Rx
 * @vsi: the VSI being changed
 * @ena: boolean value indicating if this is a enable or disable request
 */
int ice_vsi_manage_vlan_stripping(struct ice_vsi *vsi, bool ena)
{
	struct device *dev = &vsi->back->pdev->dev;
	struct ice_hw *hw = &vsi->back->hw;
	struct ice_vsi_ctx ctxt = { 0 };
	enum ice_status status;

	/* Here we are configuring what the VSI should do with the VLAN tag in
	 * the Rx packet. We can either leave the tag in the packet or put it in
	 * the Rx descriptor.
	 */
	if (ena) {
		/* Strip VLAN tag from Rx packet and put it in the desc */
		ctxt.info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_STR_BOTH;
	} else {
		/* Disable stripping. Leave tag in packet */
		ctxt.info.vlan_flags = ICE_AQ_VSI_VLAN_EMOD_NOTHING;
	}

	/* Allow all packets untagged/tagged */
	ctxt.info.vlan_flags |= ICE_AQ_VSI_VLAN_MODE_ALL;

	ctxt.info.valid_sections = cpu_to_le16(ICE_AQ_VSI_PROP_VLAN_VALID);
	ctxt.vsi_num = vsi->vsi_num;

	status = ice_aq_update_vsi(hw, &ctxt, NULL);
	if (status) {
		dev_err(dev, "update VSI for VLAN strip failed, ena = %d err %d aq_err %d\n",
			ena, status, hw->adminq.sq_last_status);
		return -EIO;
	}

	vsi->info.vlan_flags = ctxt.info.vlan_flags;
	return 0;
}
