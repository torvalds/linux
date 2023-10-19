// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020, Intel Corporation. */

#include "ice.h"
#include "ice_fltr.h"

/**
 * ice_fltr_free_list - free filter lists helper
 * @dev: pointer to the device struct
 * @h: pointer to the list head to be freed
 *
 * Helper function to free filter lists previously created using
 * ice_fltr_add_mac_to_list
 */
void ice_fltr_free_list(struct device *dev, struct list_head *h)
{
	struct ice_fltr_list_entry *e, *tmp;

	list_for_each_entry_safe(e, tmp, h, list_entry) {
		list_del(&e->list_entry);
		devm_kfree(dev, e);
	}
}

/**
 * ice_fltr_add_entry_to_list - allocate and add filter entry to list
 * @dev: pointer to device needed by alloc function
 * @info: filter info struct that gets added to the passed in list
 * @list: pointer to the list which contains MAC filters entry
 */
static int
ice_fltr_add_entry_to_list(struct device *dev, struct ice_fltr_info *info,
			   struct list_head *list)
{
	struct ice_fltr_list_entry *entry;

	entry = devm_kzalloc(dev, sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	entry->fltr_info = *info;

	INIT_LIST_HEAD(&entry->list_entry);
	list_add(&entry->list_entry, list);

	return 0;
}

/**
 * ice_fltr_set_vlan_vsi_promisc
 * @hw: pointer to the hardware structure
 * @vsi: the VSI being configured
 * @promisc_mask: mask of promiscuous config bits
 *
 * Set VSI with all associated VLANs to given promiscuous mode(s)
 */
int
ice_fltr_set_vlan_vsi_promisc(struct ice_hw *hw, struct ice_vsi *vsi,
			      u8 promisc_mask)
{
	struct ice_pf *pf = hw->back;
	int result;

	result = ice_set_vlan_vsi_promisc(hw, vsi->idx, promisc_mask, false);
	if (result && result != -EEXIST)
		dev_err(ice_pf_to_dev(pf),
			"Error setting promisc mode on VSI %i (rc=%d)\n",
			vsi->vsi_num, result);

	return result;
}

/**
 * ice_fltr_clear_vlan_vsi_promisc
 * @hw: pointer to the hardware structure
 * @vsi: the VSI being configured
 * @promisc_mask: mask of promiscuous config bits
 *
 * Clear VSI with all associated VLANs to given promiscuous mode(s)
 */
int
ice_fltr_clear_vlan_vsi_promisc(struct ice_hw *hw, struct ice_vsi *vsi,
				u8 promisc_mask)
{
	struct ice_pf *pf = hw->back;
	int result;

	result = ice_set_vlan_vsi_promisc(hw, vsi->idx, promisc_mask, true);
	if (result && result != -EEXIST)
		dev_err(ice_pf_to_dev(pf),
			"Error clearing promisc mode on VSI %i (rc=%d)\n",
			vsi->vsi_num, result);

	return result;
}

/**
 * ice_fltr_clear_vsi_promisc - clear specified promiscuous mode(s)
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle to clear mode
 * @promisc_mask: mask of promiscuous config bits to clear
 * @vid: VLAN ID to clear VLAN promiscuous
 */
int
ice_fltr_clear_vsi_promisc(struct ice_hw *hw, u16 vsi_handle, u8 promisc_mask,
			   u16 vid)
{
	struct ice_pf *pf = hw->back;
	int result;

	result = ice_clear_vsi_promisc(hw, vsi_handle, promisc_mask, vid);
	if (result && result != -EEXIST)
		dev_err(ice_pf_to_dev(pf),
			"Error clearing promisc mode on VSI %i for VID %u (rc=%d)\n",
			ice_get_hw_vsi_num(hw, vsi_handle), vid, result);

	return result;
}

/**
 * ice_fltr_set_vsi_promisc - set given VSI to given promiscuous mode(s)
 * @hw: pointer to the hardware structure
 * @vsi_handle: VSI handle to configure
 * @promisc_mask: mask of promiscuous config bits
 * @vid: VLAN ID to set VLAN promiscuous
 */
int
ice_fltr_set_vsi_promisc(struct ice_hw *hw, u16 vsi_handle, u8 promisc_mask,
			 u16 vid)
{
	struct ice_pf *pf = hw->back;
	int result;

	result = ice_set_vsi_promisc(hw, vsi_handle, promisc_mask, vid);
	if (result && result != -EEXIST)
		dev_err(ice_pf_to_dev(pf),
			"Error setting promisc mode on VSI %i for VID %u (rc=%d)\n",
			ice_get_hw_vsi_num(hw, vsi_handle), vid, result);

	return result;
}

/**
 * ice_fltr_add_mac_list - add list of MAC filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
int ice_fltr_add_mac_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_add_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_mac_list - remove list of MAC filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
int ice_fltr_remove_mac_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_remove_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_add_vlan_list - add list of VLAN filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static int ice_fltr_add_vlan_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_add_vlan(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_vlan_list - remove list of VLAN filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static int
ice_fltr_remove_vlan_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_remove_vlan(&vsi->back->hw, list);
}

/**
 * ice_fltr_add_eth_list - add list of ethertype filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static int ice_fltr_add_eth_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_add_eth_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_eth_list - remove list of ethertype filters
 * @vsi: pointer to VSI struct
 * @list: list of filters
 */
static int ice_fltr_remove_eth_list(struct ice_vsi *vsi, struct list_head *list)
{
	return ice_remove_eth_mac(&vsi->back->hw, list);
}

/**
 * ice_fltr_remove_all - remove all filters associated with VSI
 * @vsi: pointer to VSI struct
 */
void ice_fltr_remove_all(struct ice_vsi *vsi)
{
	ice_remove_vsi_fltr(&vsi->back->hw, vsi->idx);
}

/**
 * ice_fltr_add_mac_to_list - add MAC filter info to exsisting list
 * @vsi: pointer to VSI struct
 * @list: list to add filter info to
 * @mac: MAC address to add
 * @action: filter action
 */
int
ice_fltr_add_mac_to_list(struct ice_vsi *vsi, struct list_head *list,
			 const u8 *mac, enum ice_sw_fwd_act_type action)
{
	struct ice_fltr_info info = { 0 };

	info.flag = ICE_FLTR_TX;
	info.src_id = ICE_SRC_ID_VSI;
	info.lkup_type = ICE_SW_LKUP_MAC;
	info.fltr_act = action;
	info.vsi_handle = vsi->idx;

	ether_addr_copy(info.l_data.mac.mac_addr, mac);

	return ice_fltr_add_entry_to_list(ice_pf_to_dev(vsi->back), &info,
					  list);
}

/**
 * ice_fltr_add_vlan_to_list - add VLAN filter info to exsisting list
 * @vsi: pointer to VSI struct
 * @list: list to add filter info to
 * @vlan: VLAN filter details
 */
static int
ice_fltr_add_vlan_to_list(struct ice_vsi *vsi, struct list_head *list,
			  struct ice_vlan *vlan)
{
	struct ice_fltr_info info = { 0 };

	info.flag = ICE_FLTR_TX;
	info.src_id = ICE_SRC_ID_VSI;
	info.lkup_type = ICE_SW_LKUP_VLAN;
	info.fltr_act = ICE_FWD_TO_VSI;
	info.vsi_handle = vsi->idx;
	info.l_data.vlan.vlan_id = vlan->vid;
	info.l_data.vlan.tpid = vlan->tpid;
	info.l_data.vlan.tpid_valid = true;

	return ice_fltr_add_entry_to_list(ice_pf_to_dev(vsi->back), &info,
					  list);
}

/**
 * ice_fltr_add_eth_to_list - add ethertype filter info to exsisting list
 * @vsi: pointer to VSI struct
 * @list: list to add filter info to
 * @ethertype: ethertype of packet that matches filter
 * @flag: filter direction, Tx or Rx
 * @action: filter action
 */
static int
ice_fltr_add_eth_to_list(struct ice_vsi *vsi, struct list_head *list,
			 u16 ethertype, u16 flag,
			 enum ice_sw_fwd_act_type action)
{
	struct ice_fltr_info info = { 0 };

	info.flag = flag;
	info.lkup_type = ICE_SW_LKUP_ETHERTYPE;
	info.fltr_act = action;
	info.vsi_handle = vsi->idx;
	info.l_data.ethertype_mac.ethertype = ethertype;

	if (flag == ICE_FLTR_TX)
		info.src_id = ICE_SRC_ID_VSI;
	else
		info.src_id = ICE_SRC_ID_LPORT;

	return ice_fltr_add_entry_to_list(ice_pf_to_dev(vsi->back), &info,
					  list);
}

/**
 * ice_fltr_prepare_mac - add or remove MAC rule
 * @vsi: pointer to VSI struct
 * @mac: MAC address to add
 * @action: action to be performed on filter match
 * @mac_action: pointer to add or remove MAC function
 */
static int
ice_fltr_prepare_mac(struct ice_vsi *vsi, const u8 *mac,
		     enum ice_sw_fwd_act_type action,
		     int (*mac_action)(struct ice_vsi *, struct list_head *))
{
	LIST_HEAD(tmp_list);
	int result;

	if (ice_fltr_add_mac_to_list(vsi, &tmp_list, mac, action)) {
		ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
		return -ENOMEM;
	}

	result = mac_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_prepare_mac_and_broadcast - add or remove MAC and broadcast filter
 * @vsi: pointer to VSI struct
 * @mac: MAC address to add
 * @action: action to be performed on filter match
 * @mac_action: pointer to add or remove MAC function
 */
static int
ice_fltr_prepare_mac_and_broadcast(struct ice_vsi *vsi, const u8 *mac,
				   enum ice_sw_fwd_act_type action,
				   int(*mac_action)
				   (struct ice_vsi *, struct list_head *))
{
	u8 broadcast[ETH_ALEN];
	LIST_HEAD(tmp_list);
	int result;

	eth_broadcast_addr(broadcast);
	if (ice_fltr_add_mac_to_list(vsi, &tmp_list, mac, action) ||
	    ice_fltr_add_mac_to_list(vsi, &tmp_list, broadcast, action)) {
		ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
		return -ENOMEM;
	}

	result = mac_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_prepare_vlan - add or remove VLAN filter
 * @vsi: pointer to VSI struct
 * @vlan: VLAN filter details
 * @vlan_action: pointer to add or remove VLAN function
 */
static int
ice_fltr_prepare_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan,
		      int (*vlan_action)(struct ice_vsi *, struct list_head *))
{
	LIST_HEAD(tmp_list);
	int result;

	if (ice_fltr_add_vlan_to_list(vsi, &tmp_list, vlan))
		return -ENOMEM;

	result = vlan_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_prepare_eth - add or remove ethertype filter
 * @vsi: pointer to VSI struct
 * @ethertype: ethertype of packet to be filtered
 * @flag: direction of packet, Tx or Rx
 * @action: action to be performed on filter match
 * @eth_action: pointer to add or remove ethertype function
 */
static int
ice_fltr_prepare_eth(struct ice_vsi *vsi, u16 ethertype, u16 flag,
		     enum ice_sw_fwd_act_type action,
		     int (*eth_action)(struct ice_vsi *, struct list_head *))
{
	LIST_HEAD(tmp_list);
	int result;

	if (ice_fltr_add_eth_to_list(vsi, &tmp_list, ethertype, flag, action))
		return -ENOMEM;

	result = eth_action(vsi, &tmp_list);
	ice_fltr_free_list(ice_pf_to_dev(vsi->back), &tmp_list);
	return result;
}

/**
 * ice_fltr_add_mac - add single MAC filter
 * @vsi: pointer to VSI struct
 * @mac: MAC to add
 * @action: action to be performed on filter match
 */
int ice_fltr_add_mac(struct ice_vsi *vsi, const u8 *mac,
		     enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac(vsi, mac, action, ice_fltr_add_mac_list);
}

/**
 * ice_fltr_add_mac_and_broadcast - add single MAC and broadcast
 * @vsi: pointer to VSI struct
 * @mac: MAC to add
 * @action: action to be performed on filter match
 */
int
ice_fltr_add_mac_and_broadcast(struct ice_vsi *vsi, const u8 *mac,
			       enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac_and_broadcast(vsi, mac, action,
						  ice_fltr_add_mac_list);
}

/**
 * ice_fltr_remove_mac - remove MAC filter
 * @vsi: pointer to VSI struct
 * @mac: filter MAC to remove
 * @action: action to remove
 */
int ice_fltr_remove_mac(struct ice_vsi *vsi, const u8 *mac,
			enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_mac(vsi, mac, action, ice_fltr_remove_mac_list);
}

/**
 * ice_fltr_add_vlan - add single VLAN filter
 * @vsi: pointer to VSI struct
 * @vlan: VLAN filter details
 */
int ice_fltr_add_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	return ice_fltr_prepare_vlan(vsi, vlan, ice_fltr_add_vlan_list);
}

/**
 * ice_fltr_remove_vlan - remove VLAN filter
 * @vsi: pointer to VSI struct
 * @vlan: VLAN filter details
 */
int ice_fltr_remove_vlan(struct ice_vsi *vsi, struct ice_vlan *vlan)
{
	return ice_fltr_prepare_vlan(vsi, vlan, ice_fltr_remove_vlan_list);
}

/**
 * ice_fltr_add_eth - add specyfic ethertype filter
 * @vsi: pointer to VSI struct
 * @ethertype: ethertype of filter
 * @flag: direction of packet to be filtered, Tx or Rx
 * @action: action to be performed on filter match
 */
int ice_fltr_add_eth(struct ice_vsi *vsi, u16 ethertype, u16 flag,
		     enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_eth(vsi, ethertype, flag, action,
				    ice_fltr_add_eth_list);
}

/**
 * ice_fltr_remove_eth - remove ethertype filter
 * @vsi: pointer to VSI struct
 * @ethertype: ethertype of filter
 * @flag: direction of filter
 * @action: action to remove
 */
int ice_fltr_remove_eth(struct ice_vsi *vsi, u16 ethertype, u16 flag,
			enum ice_sw_fwd_act_type action)
{
	return ice_fltr_prepare_eth(vsi, ethertype, flag, action,
				    ice_fltr_remove_eth_list);
}
