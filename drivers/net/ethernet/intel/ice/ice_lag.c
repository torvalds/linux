// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2021, Intel Corporation. */

/* Link Aggregation code */

#include "ice.h"
#include "ice_lag.h"

/**
 * ice_lag_nop_handler - no-op Rx handler to disable LAG
 * @pskb: pointer to skb pointer
 */
rx_handler_result_t ice_lag_nop_handler(struct sk_buff __always_unused **pskb)
{
	return RX_HANDLER_PASS;
}

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
 * ice_display_lag_info - print LAG info
 * @lag: LAG info struct
 */
static void ice_display_lag_info(struct ice_lag *lag)
{
	const char *name, *peer, *upper, *role, *bonded, *primary;
	struct device *dev = &lag->pf->pdev->dev;

	name = lag->netdev ? netdev_name(lag->netdev) : "unset";
	peer = lag->peer_netdev ? netdev_name(lag->peer_netdev) : "unset";
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

	dev_dbg(dev, "%s %s, peer:%s, upper:%s, role:%s, primary:%s\n", name,
		bonded, peer, upper, role, primary);
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
 * ice_lag_link - handle LAG link event
 * @lag: LAG info struct
 * @info: info from the netdev notifier
 */
static void
ice_lag_link(struct ice_lag *lag, struct netdev_notifier_changeupper_info *info)
{
	struct net_device *netdev_tmp, *upper = info->upper_dev;
	struct ice_pf *pf = lag->pf;
	int peers = 0;

	if (lag->bonded)
		dev_warn(ice_pf_to_dev(pf), "%s Already part of a bond\n",
			 netdev_name(lag->netdev));

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(upper, netdev_tmp)
		peers++;
	rcu_read_unlock();

	if (lag->upper_netdev != upper) {
		dev_hold(upper);
		lag->upper_netdev = upper;
	}

	ice_clear_sriov_cap(pf);
	ice_clear_rdma_cap(pf);

	lag->bonded = true;
	lag->role = ICE_LAG_UNSET;

	/* if this is the first element in an LAG mark as primary */
	lag->primary = !!(peers == 1);
}

/**
 * ice_lag_unlink - handle unlink event
 * @lag: LAG info struct
 * @info: info from netdev notification
 */
static void
ice_lag_unlink(struct ice_lag *lag,
	       struct netdev_notifier_changeupper_info *info)
{
	struct net_device *netdev_tmp, *upper = info->upper_dev;
	struct ice_pf *pf = lag->pf;
	bool found = false;

	if (!lag->bonded) {
		netdev_dbg(lag->netdev, "bonding unlink event on non-LAG netdev\n");
		return;
	}

	/* determine if we are in the new LAG config or not */
	rcu_read_lock();
	for_each_netdev_in_bond_rcu(upper, netdev_tmp) {
		if (netdev_tmp == lag->netdev) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();

	if (found)
		return;

	if (lag->upper_netdev) {
		dev_put(lag->upper_netdev);
		lag->upper_netdev = NULL;
	}

	lag->peer_netdev = NULL;
	ice_set_sriov_cap(pf);
	ice_set_rdma_cap(pf);
	lag->bonded = false;
	lag->role = ICE_LAG_NONE;
}

/**
 * ice_lag_unregister - handle netdev unregister events
 * @lag: LAG info struct
 * @netdev: netdev reporting the event
 */
static void ice_lag_unregister(struct ice_lag *lag, struct net_device *netdev)
{
	struct ice_pf *pf = lag->pf;

	/* check to see if this event is for this netdev
	 * check that we are in an aggregate
	 */
	if (netdev != lag->netdev || !lag->bonded)
		return;

	if (lag->upper_netdev) {
		dev_put(lag->upper_netdev);
		lag->upper_netdev = NULL;
		ice_set_sriov_cap(pf);
		ice_set_rdma_cap(pf);
	}
	/* perform some cleanup in case we come back */
	lag->bonded = false;
	lag->role = ICE_LAG_NONE;
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
	struct net_device *netdev;

	info = ptr;
	netdev = netdev_notifier_info_to_dev(ptr);

	/* not for this netdev */
	if (netdev != lag->netdev)
		return;

	if (!info->upper_dev) {
		netdev_dbg(netdev, "changeupper rcvd, but no upper defined\n");
		return;
	}

	netdev_dbg(netdev, "bonding %s\n", info->linking ? "LINK" : "UNLINK");

	if (!netif_is_lag_master(info->upper_dev)) {
		netdev_dbg(netdev, "changeupper rcvd, but not primary. bail\n");
		return;
	}

	if (info->linking)
		ice_lag_link(lag, info);
	else
		ice_lag_unlink(lag, info);

	ice_display_lag_info(lag);
}

/**
 * ice_lag_changelower_event - handle LAG changelower event
 * @lag: LAG info struct
 * @ptr: opaque data pointer
 *
 * ptr to be cast to netdev_notifier_changelowerstate_info
 */
static void ice_lag_changelower_event(struct ice_lag *lag, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);

	if (netdev != lag->netdev)
		return;

	netdev_dbg(netdev, "bonding info\n");

	if (!netif_is_lag_port(netdev))
		netdev_dbg(netdev, "CHANGELOWER rcvd, but netdev not in LAG. Bail\n");
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
	struct ice_lag *lag;

	lag = container_of(notif_blk, struct ice_lag, notif_block);

	if (!lag->netdev)
		return NOTIFY_DONE;

	/* Check that the netdev is in the working namespace */
	if (!net_eq(dev_net(netdev), &init_net))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		ice_lag_changeupper_event(lag, ptr);
		break;
	case NETDEV_CHANGELOWERSTATE:
		ice_lag_changelower_event(lag, ptr);
		break;
	case NETDEV_BONDING_INFO:
		ice_lag_info_event(lag, ptr);
		break;
	case NETDEV_UNREGISTER:
		ice_lag_unregister(lag, netdev);
		break;
	default:
		break;
	}

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
	lag->peer_netdev = NULL;
	lag->upper_netdev = NULL;
	lag->notif_block.notifier_call = NULL;

	err = ice_register_lag_handler(lag);
	if (err) {
		dev_warn(dev, "INIT LAG: Failed to register event handler\n");
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

	dev_put(lag->upper_netdev);

	dev_put(lag->peer_netdev);

	kfree(lag);

	pf->lag = NULL;
}
