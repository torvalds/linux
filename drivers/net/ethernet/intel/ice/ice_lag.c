// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

/* Link Aggregation code */

#include "ice.h"
#include "ice_lib.h"
#include "ice_lag.h"

#define ICE_LAG_RES_SHARED	BIT(14)
#define ICE_LAG_RES_VALID	BIT(15)

#define ICE_RECIPE_LEN			64
static const u8 ice_dflt_vsi_rcp[ICE_RECIPE_LEN] = {
	0x05, 0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0x85, 0, 0x01, 0, 0, 0, 0xff, 0xff, 0x08, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0x30, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/**
 * ice_lag_set_primary - set PF LAG state as Primary
 * @lag: LAG info struct
 */
static void ice_lag_set_primary(struct ice_lag *lag)
{
	struct ice_pf *pf = lag->pf;

	if (!pf)
		return;

	if (lag->role != ICE_LAG_UNSET && lag->role != ICE_LAG_BACKUP) {
		dev_warn(ice_pf_to_dev(pf), "%s: Attempt to be Primary, but incompatible state.\n",
			 netdev_name(lag->netdev));
		return;
	}

	lag->role = ICE_LAG_PRIMARY;
}

/**
 * ice_lag_set_backup - set PF LAG state to Backup
 * @lag: LAG info struct
 */
static void ice_lag_set_backup(struct ice_lag *lag)
{
	struct ice_pf *pf = lag->pf;

	if (!pf)
		return;

	if (lag->role != ICE_LAG_UNSET && lag->role != ICE_LAG_PRIMARY) {
		dev_dbg(ice_pf_to_dev(pf), "%s: Attempt to be Backup, but incompatible state\n",
			netdev_name(lag->netdev));
		return;
	}

	lag->role = ICE_LAG_BACKUP;
}

/**
 * ice_netdev_to_lag - return pointer to associated lag struct from netdev
 * @netdev: pointer to net_device struct to query
 */
static struct ice_lag *ice_netdev_to_lag(struct net_device *netdev)
{
	struct ice_netdev_priv *np;
	struct ice_vsi *vsi;

	if (!netif_is_ice(netdev))
		return NULL;

	np = netdev_priv(netdev);
	if (!np)
		return NULL;

	vsi = np->vsi;
	if (!vsi)
		return NULL;

	return vsi->back->lag;
}

/**
 * ice_lag_find_primary - returns pointer to primary interfaces lag struct
 * @lag: local interfaces lag struct
 */
static struct ice_lag *ice_lag_find_primary(struct ice_lag *lag)
{
	struct ice_lag *primary_lag = NULL;
	struct list_head *tmp;

	list_for_each(tmp, lag->netdev_head) {
		struct ice_lag_netdev_list *entry;
		struct ice_lag *tmp_lag;

		entry = list_entry(tmp, struct ice_lag_netdev_list, node);
		tmp_lag = ice_netdev_to_lag(entry->netdev);
		if (tmp_lag && tmp_lag->primary) {
			primary_lag = tmp_lag;
			break;
		}
	}

	return primary_lag;
}

/**
 * ice_lag_cfg_dflt_fltr - Add/Remove default VSI rule for LAG
 * @lag: lag struct for local interface
 * @add: boolean on whether we are adding filters
 */
static int
ice_lag_cfg_dflt_fltr(struct ice_lag *lag, bool add)
{
	struct ice_sw_rule_lkup_rx_tx *s_rule;
	u16 s_rule_sz, vsi_num;
	struct ice_hw *hw;
	u32 act, opc;
	u8 *eth_hdr;
	int err;

	hw = &lag->pf->hw;
	vsi_num = ice_get_hw_vsi_num(hw, 0);

	s_rule_sz = ICE_SW_RULE_RX_TX_ETH_HDR_SIZE(s_rule);
	s_rule = kzalloc(s_rule_sz, GFP_KERNEL);
	if (!s_rule) {
		dev_err(ice_pf_to_dev(lag->pf), "error allocating rule for LAG default VSI\n");
		return -ENOMEM;
	}

	if (add) {
		eth_hdr = s_rule->hdr_data;
		ice_fill_eth_hdr(eth_hdr);

		act = (vsi_num << ICE_SINGLE_ACT_VSI_ID_S) &
			ICE_SINGLE_ACT_VSI_ID_M;
		act |= ICE_SINGLE_ACT_VSI_FORWARDING |
			ICE_SINGLE_ACT_VALID_BIT | ICE_SINGLE_ACT_LAN_ENABLE;

		s_rule->hdr.type = cpu_to_le16(ICE_AQC_SW_RULES_T_LKUP_RX);
		s_rule->recipe_id = cpu_to_le16(lag->pf_recipe);
		s_rule->src = cpu_to_le16(hw->port_info->lport);
		s_rule->act = cpu_to_le32(act);
		s_rule->hdr_len = cpu_to_le16(DUMMY_ETH_HDR_LEN);
		opc = ice_aqc_opc_add_sw_rules;
	} else {
		s_rule->index = cpu_to_le16(lag->pf_rule_id);
		opc = ice_aqc_opc_remove_sw_rules;
	}

	err = ice_aq_sw_rules(&lag->pf->hw, s_rule, s_rule_sz, 1, opc, NULL);
	if (err)
		goto dflt_fltr_free;

	if (add)
		lag->pf_rule_id = le16_to_cpu(s_rule->index);
	else
		lag->pf_rule_id = 0;

dflt_fltr_free:
	kfree(s_rule);
	return err;
}

/**
 * ice_lag_cfg_pf_fltrs - set filters up for new active port
 * @lag: local interfaces lag struct
 * @ptr: opaque data containing notifier event
 */
static void
ice_lag_cfg_pf_fltrs(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_bonding_info *info;
	struct netdev_bonding_info *bonding_info;
	struct net_device *event_netdev;
	struct device *dev;

	event_netdev = netdev_notifier_info_to_dev(ptr);
	/* not for this netdev */
	if (event_netdev != lag->netdev)
		return;

	info = (struct netdev_notifier_bonding_info *)ptr;
	bonding_info = &info->bonding_info;
	dev = ice_pf_to_dev(lag->pf);

	/* interface not active - remove old default VSI rule */
	if (bonding_info->slave.state && lag->pf_rule_id) {
		if (ice_lag_cfg_dflt_fltr(lag, false))
			dev_err(dev, "Error removing old default VSI filter\n");
		return;
	}

	/* interface becoming active - add new default VSI rule */
	if (!bonding_info->slave.state && !lag->pf_rule_id)
		if (ice_lag_cfg_dflt_fltr(lag, true))
			dev_err(dev, "Error adding new default VSI filter\n");
}

/**
 * ice_display_lag_info - print LAG info
 * @lag: LAG info struct
 */
static void ice_display_lag_info(struct ice_lag *lag)
{
	const char *name, *upper, *role, *bonded, *primary;
	struct device *dev = &lag->pf->pdev->dev;

	name = lag->netdev ? netdev_name(lag->netdev) : "unset";
	upper = lag->upper_netdev ? netdev_name(lag->upper_netdev) : "unset";
	primary = lag->primary ? "TRUE" : "FALSE";
	bonded = lag->bonded ? "BONDED" : "UNBONDED";

	switch (lag->role) {
	case ICE_LAG_NONE:
		role = "NONE";
		break;
	case ICE_LAG_PRIMARY:
		role = "PRIMARY";
		break;
	case ICE_LAG_BACKUP:
		role = "BACKUP";
		break;
	case ICE_LAG_UNSET:
		role = "UNSET";
		break;
	default:
		role = "ERROR";
	}

	dev_dbg(dev, "%s %s, upper:%s, role:%s, primary:%s\n", name, bonded,
		upper, role, primary);
}

/**
 * ice_lag_move_vf_node_tc - move scheduling nodes for one VF on one TC
 * @lag: lag info struct
 * @oldport: lport of previous nodes location
 * @newport: lport of destination nodes location
 * @vsi_num: array index of VSI in PF space
 * @tc: traffic class to move
 */
static void
ice_lag_move_vf_node_tc(struct ice_lag *lag, u8 oldport, u8 newport,
			u16 vsi_num, u8 tc)
{
}

/**
 * ice_lag_move_single_vf_nodes - Move Tx scheduling nodes for single VF
 * @lag: primary interface LAG struct
 * @oldport: lport of previous interface
 * @newport: lport of destination interface
 * @vsi_num: SW index of VF's VSI
 */
static void
ice_lag_move_single_vf_nodes(struct ice_lag *lag, u8 oldport, u8 newport,
			     u16 vsi_num)
{
	u8 tc;

	ice_for_each_traffic_class(tc)
		ice_lag_move_vf_node_tc(lag, oldport, newport, vsi_num, tc);
}

/**
 * ice_lag_move_new_vf_nodes - Move Tx scheduling nodes for a VF if required
 * @vf: the VF to move Tx nodes for
 *
 * Called just after configuring new VF queues. Check whether the VF Tx
 * scheduling nodes need to be updated to fail over to the active port. If so,
 * move them now.
 */
void ice_lag_move_new_vf_nodes(struct ice_vf *vf)
{
}

/**
 * ice_lag_move_vf_nodes - move Tx scheduling nodes for all VFs to new port
 * @lag: lag info struct
 * @oldport: lport of previous interface
 * @newport: lport of destination interface
 */
static void ice_lag_move_vf_nodes(struct ice_lag *lag, u8 oldport, u8 newport)
{
	struct ice_pf *pf;
	int i;

	if (!lag->primary)
		return;

	pf = lag->pf;
	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && (pf->vsi[i]->type == ICE_VSI_VF ||
				   pf->vsi[i]->type == ICE_VSI_SWITCHDEV_CTRL))
			ice_lag_move_single_vf_nodes(lag, oldport, newport, i);
}

#define ICE_LAG_SRIOV_CP_RECIPE		10
#define ICE_LAG_SRIOV_TRAIN_PKT_LEN	16

/**
 * ice_lag_cfg_cp_fltr - configure filter for control packets
 * @lag: local interface's lag struct
 * @add: add or remove rule
 */
static void
ice_lag_cfg_cp_fltr(struct ice_lag *lag, bool add)
{
}

/**
 * ice_lag_info_event - handle NETDEV_BONDING_INFO event
 * @lag: LAG info struct
 * @ptr: opaque data pointer
 *
 * ptr is to be cast to (netdev_notifier_bonding_info *)
 */
static void ice_lag_info_event(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_bonding_info *info;
	struct netdev_bonding_info *bonding_info;
	struct net_device *event_netdev;
	const char *lag_netdev_name;

	event_netdev = netdev_notifier_info_to_dev(ptr);
	info = ptr;
	lag_netdev_name = netdev_name(lag->netdev);
	bonding_info = &info->bonding_info;

	if (event_netdev != lag->netdev || !lag->bonded || !lag->upper_netdev)
		return;

	if (bonding_info->master.bond_mode != BOND_MODE_ACTIVEBACKUP) {
		netdev_dbg(lag->netdev, "Bonding event recv, but mode not active/backup\n");
		goto lag_out;
	}

	if (strcmp(bonding_info->slave.slave_name, lag_netdev_name)) {
		netdev_dbg(lag->netdev, "Bonding event recv, but secondary info not for us\n");
		goto lag_out;
	}

	if (bonding_info->slave.state)
		ice_lag_set_backup(lag);
	else
		ice_lag_set_primary(lag);

lag_out:
	ice_display_lag_info(lag);
}

/**
 * ice_lag_reclaim_vf_tc - move scheduling nodes back to primary interface
 * @lag: primary interface lag struct
 * @src_hw: HW struct current node location
 * @vsi_num: VSI index in PF space
 * @tc: traffic class to move
 */
static void
ice_lag_reclaim_vf_tc(struct ice_lag *lag, struct ice_hw *src_hw, u16 vsi_num,
		      u8 tc)
{
}

/**
 * ice_lag_reclaim_vf_nodes - When interface leaving bond primary reclaims nodes
 * @lag: primary interface lag struct
 * @src_hw: HW struct for current node location
 */
static void
ice_lag_reclaim_vf_nodes(struct ice_lag *lag, struct ice_hw *src_hw)
{
	struct ice_pf *pf;
	int i, tc;

	if (!lag->primary || !src_hw)
		return;

	pf = lag->pf;
	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && (pf->vsi[i]->type == ICE_VSI_VF ||
				   pf->vsi[i]->type == ICE_VSI_SWITCHDEV_CTRL))
			ice_for_each_traffic_class(tc)
				ice_lag_reclaim_vf_tc(lag, src_hw, i, tc);
}

/**
 * ice_lag_link - handle LAG link event
 * @lag: LAG info struct
 */
static void ice_lag_link(struct ice_lag *lag)
{
	struct ice_pf *pf = lag->pf;

	if (lag->bonded)
		dev_warn(ice_pf_to_dev(pf), "%s Already part of a bond\n",
			 netdev_name(lag->netdev));

	lag->bonded = true;
	lag->role = ICE_LAG_UNSET;
}

/**
 * ice_lag_unlink - handle unlink event
 * @lag: LAG info struct
 */
static void ice_lag_unlink(struct ice_lag *lag)
{
	u8 pri_port, act_port, loc_port;
	struct ice_pf *pf = lag->pf;

	if (!lag->bonded) {
		netdev_dbg(lag->netdev, "bonding unlink event on non-LAG netdev\n");
		return;
	}

	if (lag->primary) {
		act_port = lag->active_port;
		pri_port = lag->pf->hw.port_info->lport;
		if (act_port != pri_port && act_port != ICE_LAG_INVALID_PORT)
			ice_lag_move_vf_nodes(lag, act_port, pri_port);
		lag->primary = false;
		lag->active_port = ICE_LAG_INVALID_PORT;
	} else {
		struct ice_lag *primary_lag;

		primary_lag = ice_lag_find_primary(lag);
		if (primary_lag) {
			act_port = primary_lag->active_port;
			pri_port = primary_lag->pf->hw.port_info->lport;
			loc_port = pf->hw.port_info->lport;
			if (act_port == loc_port &&
			    act_port != ICE_LAG_INVALID_PORT) {
				ice_lag_reclaim_vf_nodes(primary_lag,
							 &lag->pf->hw);
				primary_lag->active_port = ICE_LAG_INVALID_PORT;
			}
		}
	}

	lag->bonded = false;
	lag->role = ICE_LAG_NONE;
	lag->upper_netdev = NULL;
}

/**
 * ice_lag_link_unlink - helper function to call lag_link/unlink
 * @lag: lag info struct
 * @ptr: opaque pointer data
 */
static void ice_lag_link_unlink(struct ice_lag *lag, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info = ptr;

	if (netdev != lag->netdev)
		return;

	if (info->linking)
		ice_lag_link(lag);
	else
		ice_lag_unlink(lag);
}

/**
 * ice_lag_set_swid - set the SWID on secondary interface
 * @primary_swid: primary interface's SWID
 * @local_lag: local interfaces LAG struct
 * @link: Is this a linking activity
 *
 * If link is false, then primary_swid should be expected to not be valid
 */
static void
ice_lag_set_swid(u16 primary_swid, struct ice_lag *local_lag,
		 bool link)
{
}

/**
 * ice_lag_primary_swid - set/clear the SHARED attrib of primary's SWID
 * @lag: primary interfaces lag struct
 * @link: is this a linking activity
 *
 * Implement setting primary SWID as shared using 0x020B
 */
static void ice_lag_primary_swid(struct ice_lag *lag, bool link)
{
	struct ice_hw *hw;
	u16 swid;

	hw = &lag->pf->hw;
	swid = hw->port_info->sw_id;

	if (ice_share_res(hw, ICE_AQC_RES_TYPE_SWID, link, swid))
		dev_warn(ice_pf_to_dev(lag->pf), "Failure to set primary interface shared status\n");
}

/**
 * ice_lag_add_prune_list - Adds event_pf's VSI to primary's prune list
 * @lag: lag info struct
 * @event_pf: PF struct for VSI we are adding to primary's prune list
 */
static void ice_lag_add_prune_list(struct ice_lag *lag, struct ice_pf *event_pf)
{
}

/**
 * ice_lag_del_prune_list - Remove secondary's vsi from primary's prune list
 * @lag: primary interface's ice_lag struct
 * @event_pf: PF struct for unlinking interface
 */
static void ice_lag_del_prune_list(struct ice_lag *lag, struct ice_pf *event_pf)
{
}

/**
 * ice_lag_init_feature_support_flag - Check for NVM support for LAG
 * @pf: PF struct
 */
static void ice_lag_init_feature_support_flag(struct ice_pf *pf)
{
	struct ice_hw_common_caps *caps;

	caps = &pf->hw.dev_caps.common_cap;
	if (caps->roce_lag)
		ice_set_feature_support(pf, ICE_F_ROCE_LAG);
	else
		ice_clear_feature_support(pf, ICE_F_ROCE_LAG);

	if (caps->sriov_lag)
		ice_set_feature_support(pf, ICE_F_SRIOV_LAG);
	else
		ice_clear_feature_support(pf, ICE_F_SRIOV_LAG);
}

/**
 * ice_lag_changeupper_event - handle LAG changeupper event
 * @lag: LAG info struct
 * @ptr: opaque pointer data
 *
 * ptr is to be cast into netdev_notifier_changeupper_info
 */
static void ice_lag_changeupper_event(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct ice_lag *primary_lag;
	struct net_device *netdev;

	info = ptr;
	netdev = netdev_notifier_info_to_dev(ptr);

	/* not for this netdev */
	if (netdev != lag->netdev)
		return;

	primary_lag = ice_lag_find_primary(lag);
	if (info->linking) {
		lag->upper_netdev = info->upper_dev;
		/* If there is not already a primary interface in the LAG,
		 * then mark this one as primary.
		 */
		if (!primary_lag) {
			lag->primary = true;
			/* Configure primary's SWID to be shared */
			ice_lag_primary_swid(lag, true);
			primary_lag = lag;
		} else {
			u16 swid;

			swid = primary_lag->pf->hw.port_info->sw_id;
			ice_lag_set_swid(swid, lag, true);
			ice_lag_add_prune_list(primary_lag, lag->pf);
		}
		/* add filter for primary control packets */
		ice_lag_cfg_cp_fltr(lag, true);
	} else {
		if (!primary_lag && lag->primary)
			primary_lag = lag;

		if (primary_lag) {
			if (!lag->primary) {
				ice_lag_set_swid(0, lag, false);
			} else {
				ice_lag_primary_swid(lag, false);
				ice_lag_del_prune_list(primary_lag, lag->pf);
			}
			ice_lag_cfg_cp_fltr(lag, false);
		}
	}
}

/**
 * ice_lag_monitor_link - monitor interfaces entering/leaving the aggregate
 * @lag: lag info struct
 * @ptr: opaque data containing notifier event
 *
 * This function only operates after a primary has been set.
 */
static void ice_lag_monitor_link(struct ice_lag *lag, void *ptr)
{
}

/**
 * ice_lag_monitor_active - main PF keep track of which port is active
 * @lag: lag info struct
 * @ptr: opaque data containing notifier event
 *
 * This function is for the primary PF to monitor changes in which port is
 * active and handle changes for SRIOV VF functionality
 */
static void ice_lag_monitor_active(struct ice_lag *lag, void *ptr)
{
}

/**
 * ice_lag_chk_comp - evaluate bonded interface for feature support
 * @lag: lag info struct
 * @ptr: opaque data for netdev event info
 */
static bool
ice_lag_chk_comp(struct ice_lag *lag, void *ptr)
{
	return true;
}

/**
 * ice_lag_unregister - handle netdev unregister events
 * @lag: LAG info struct
 * @event_netdev: netdev struct for target of notifier event
 */
static void
ice_lag_unregister(struct ice_lag *lag, struct net_device *event_netdev)
{
}

/**
 * ice_lag_monitor_rdma - set and clear rdma functionality
 * @lag: pointer to lag struct
 * @ptr: opaque data for netdev event info
 */
static void
ice_lag_monitor_rdma(struct ice_lag *lag, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct net_device *netdev;

	info = ptr;
	netdev = netdev_notifier_info_to_dev(ptr);

	if (netdev != lag->netdev)
		return;

	if (info->linking)
		ice_clear_rdma_cap(lag->pf);
	else
		ice_set_rdma_cap(lag->pf);
}

/**
 * ice_lag_process_event - process a task assigned to the lag_wq
 * @work: pointer to work_struct
 */
static void ice_lag_process_event(struct work_struct *work)
{
	struct netdev_notifier_changeupper_info *info;
	struct ice_lag_work *lag_work;
	struct net_device *netdev;
	struct list_head *tmp, *n;
	struct ice_pf *pf;

	lag_work = container_of(work, struct ice_lag_work, lag_task);
	pf = lag_work->lag->pf;

	mutex_lock(&pf->lag_mutex);
	lag_work->lag->netdev_head = &lag_work->netdev_list.node;

	switch (lag_work->event) {
	case NETDEV_CHANGEUPPER:
		info = &lag_work->info.changeupper_info;
		if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG)) {
			ice_lag_monitor_link(lag_work->lag, info);
			ice_lag_changeupper_event(lag_work->lag, info);
			ice_lag_link_unlink(lag_work->lag, info);
		}
		ice_lag_monitor_rdma(lag_work->lag, info);
		break;
	case NETDEV_BONDING_INFO:
		if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG)) {
			if (!ice_lag_chk_comp(lag_work->lag,
					      &lag_work->info.bonding_info)) {
				goto lag_cleanup;
			}
			ice_lag_monitor_active(lag_work->lag,
					       &lag_work->info.bonding_info);
			ice_lag_cfg_pf_fltrs(lag_work->lag,
					     &lag_work->info.bonding_info);
		}
		ice_lag_info_event(lag_work->lag, &lag_work->info.bonding_info);
		break;
	case NETDEV_UNREGISTER:
		if (ice_is_feature_supported(pf, ICE_F_SRIOV_LAG)) {
			netdev = lag_work->info.bonding_info.info.dev;
			if (netdev == lag_work->lag->netdev &&
			    lag_work->lag->bonded)
				ice_lag_unregister(lag_work->lag, netdev);
		}
		break;
	default:
		break;
	}

lag_cleanup:
	/* cleanup resources allocated for this work item */
	list_for_each_safe(tmp, n, &lag_work->netdev_list.node) {
		struct ice_lag_netdev_list *entry;

		entry = list_entry(tmp, struct ice_lag_netdev_list, node);
		list_del(&entry->node);
		kfree(entry);
	}
	lag_work->lag->netdev_head = NULL;

	mutex_unlock(&pf->lag_mutex);

	kfree(lag_work);
}

/**
 * ice_lag_event_handler - handle LAG events from netdev
 * @notif_blk: notifier block registered by this netdev
 * @event: event type
 * @ptr: opaque data containing notifier event
 */
static int
ice_lag_event_handler(struct notifier_block *notif_blk, unsigned long event,
		      void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct net_device *upper_netdev;
	struct ice_lag_work *lag_work;
	struct ice_lag *lag;

	if (!netif_is_ice(netdev))
		return NOTIFY_DONE;

	if (event != NETDEV_CHANGEUPPER && event != NETDEV_BONDING_INFO &&
	    event != NETDEV_UNREGISTER)
		return NOTIFY_DONE;

	if (!(netdev->priv_flags & IFF_BONDING))
		return NOTIFY_DONE;

	lag = container_of(notif_blk, struct ice_lag, notif_block);
	if (!lag->netdev)
		return NOTIFY_DONE;

	if (!net_eq(dev_net(netdev), &init_net))
		return NOTIFY_DONE;

	/* This memory will be freed at the end of ice_lag_process_event */
	lag_work = kzalloc(sizeof(*lag_work), GFP_KERNEL);
	if (!lag_work)
		return -ENOMEM;

	lag_work->event_netdev = netdev;
	lag_work->lag = lag;
	lag_work->event = event;
	if (event == NETDEV_CHANGEUPPER) {
		struct netdev_notifier_changeupper_info *info;

		info = ptr;
		upper_netdev = info->upper_dev;
	} else {
		upper_netdev = netdev_master_upper_dev_get(netdev);
	}

	INIT_LIST_HEAD(&lag_work->netdev_list.node);
	if (upper_netdev) {
		struct ice_lag_netdev_list *nd_list;
		struct net_device *tmp_nd;

		rcu_read_lock();
		for_each_netdev_in_bond_rcu(upper_netdev, tmp_nd) {
			nd_list = kzalloc(sizeof(*nd_list), GFP_KERNEL);
			if (!nd_list)
				break;

			nd_list->netdev = tmp_nd;
			list_add(&nd_list->node, &lag_work->netdev_list.node);
		}
		rcu_read_unlock();
	}

	switch (event) {
	case NETDEV_CHANGEUPPER:
		lag_work->info.changeupper_info =
			*((struct netdev_notifier_changeupper_info *)ptr);
		break;
	case NETDEV_BONDING_INFO:
		lag_work->info.bonding_info =
			*((struct netdev_notifier_bonding_info *)ptr);
		break;
	default:
		lag_work->info.notifier_info =
			*((struct netdev_notifier_info *)ptr);
		break;
	}

	INIT_WORK(&lag_work->lag_task, ice_lag_process_event);
	queue_work(ice_lag_wq, &lag_work->lag_task);

	return NOTIFY_DONE;
}

/**
 * ice_register_lag_handler - register LAG handler on netdev
 * @lag: LAG struct
 */
static int ice_register_lag_handler(struct ice_lag *lag)
{
	struct device *dev = ice_pf_to_dev(lag->pf);
	struct notifier_block *notif_blk;

	notif_blk = &lag->notif_block;

	if (!notif_blk->notifier_call) {
		notif_blk->notifier_call = ice_lag_event_handler;
		if (register_netdevice_notifier(notif_blk)) {
			notif_blk->notifier_call = NULL;
			dev_err(dev, "FAIL register LAG event handler!\n");
			return -EINVAL;
		}
		dev_dbg(dev, "LAG event handler registered\n");
	}
	return 0;
}

/**
 * ice_unregister_lag_handler - unregister LAG handler on netdev
 * @lag: LAG struct
 */
static void ice_unregister_lag_handler(struct ice_lag *lag)
{
	struct device *dev = ice_pf_to_dev(lag->pf);
	struct notifier_block *notif_blk;

	notif_blk = &lag->notif_block;
	if (notif_blk->notifier_call) {
		unregister_netdevice_notifier(notif_blk);
		dev_dbg(dev, "LAG event handler unregistered\n");
	}
}

/**
 * ice_create_lag_recipe
 * @hw: pointer to HW struct
 * @base_recipe: recipe to base the new recipe on
 * @prio: priority for new recipe
 *
 * function returns 0 on error
 */
static u16 ice_create_lag_recipe(struct ice_hw *hw, const u8 *base_recipe,
				 u8 prio)
{
	u16 rid = 0;

	return rid;
}

/**
 * ice_init_lag - initialize support for LAG
 * @pf: PF struct
 *
 * Alloc memory for LAG structs and initialize the elements.
 * Memory will be freed in ice_deinit_lag
 */
int ice_init_lag(struct ice_pf *pf)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_lag *lag;
	struct ice_vsi *vsi;
	int err;

	ice_lag_init_feature_support_flag(pf);

	pf->lag = kzalloc(sizeof(*lag), GFP_KERNEL);
	if (!pf->lag)
		return -ENOMEM;
	lag = pf->lag;

	vsi = ice_get_main_vsi(pf);
	if (!vsi) {
		dev_err(dev, "couldn't get main vsi, link aggregation init fail\n");
		err = -EIO;
		goto lag_error;
	}

	lag->pf = pf;
	lag->netdev = vsi->netdev;
	lag->role = ICE_LAG_NONE;
	lag->bonded = false;
	lag->upper_netdev = NULL;
	lag->notif_block.notifier_call = NULL;

	err = ice_register_lag_handler(lag);
	if (err) {
		dev_warn(dev, "INIT LAG: Failed to register event handler\n");
		goto lag_error;
	}

	lag->pf_recipe = ice_create_lag_recipe(&pf->hw, ice_dflt_vsi_rcp, 1);
	if (!lag->pf_recipe) {
		err = -EINVAL;
		goto lag_error;
	}

	ice_display_lag_info(lag);

	dev_dbg(dev, "INIT LAG complete\n");
	return 0;

lag_error:
	kfree(lag);
	pf->lag = NULL;
	return err;
}

/**
 * ice_deinit_lag - Clean up LAG
 * @pf: PF struct
 *
 * Clean up kernel LAG info and free memory
 * This function is meant to only be called on driver remove/shutdown
 */
void ice_deinit_lag(struct ice_pf *pf)
{
	struct ice_lag *lag;

	lag = pf->lag;

	if (!lag)
		return;

	if (lag->pf)
		ice_unregister_lag_handler(lag);

	flush_workqueue(ice_lag_wq);

	ice_free_hw_res(&pf->hw, ICE_AQC_RES_TYPE_RECIPE, 1,
			&pf->lag->pf_recipe);

	kfree(lag);

	pf->lag = NULL;
}
