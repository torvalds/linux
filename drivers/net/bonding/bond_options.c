/*
 * drivers/net/bond/bond_options.c - bonding options
 * Copyright (c) 2013 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2013 Scott Feldman <sfeldma@cumulusnetworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/rwlock.h>
#include <linux/rcupdate.h>
#include "bonding.h"

int bond_option_mode_set(struct bonding *bond, int mode)
{
	if (bond_parm_tbl_lookup(mode, bond_mode_tbl) < 0) {
		pr_err("%s: Ignoring invalid mode value %d.\n",
		       bond->dev->name, mode);
		return -EINVAL;
	}

	if (bond->dev->flags & IFF_UP) {
		pr_err("%s: unable to update mode because interface is up.\n",
		       bond->dev->name);
		return -EPERM;
	}

	if (bond_has_slaves(bond)) {
		pr_err("%s: unable to update mode because bond has slaves.\n",
			bond->dev->name);
		return -EPERM;
	}

	if (BOND_NO_USES_ARP(mode) && bond->params.arp_interval) {
		pr_info("%s: %s mode is incompatible with arp monitoring, start mii monitoring\n",
			bond->dev->name, bond_mode_tbl[mode].modename);
		/* disable arp monitoring */
		bond->params.arp_interval = 0;
		/* set miimon to default value */
		bond->params.miimon = BOND_DEFAULT_MIIMON;
		pr_info("%s: Setting MII monitoring interval to %d.\n",
			bond->dev->name, bond->params.miimon);
	}

	/* don't cache arp_validate between modes */
	bond->params.arp_validate = BOND_ARP_VALIDATE_NONE;
	bond->params.mode = mode;
	return 0;
}

static struct net_device *__bond_option_active_slave_get(struct bonding *bond,
							 struct slave *slave)
{
	return USES_PRIMARY(bond->params.mode) && slave ? slave->dev : NULL;
}

struct net_device *bond_option_active_slave_get_rcu(struct bonding *bond)
{
	struct slave *slave = rcu_dereference(bond->curr_active_slave);

	return __bond_option_active_slave_get(bond, slave);
}

struct net_device *bond_option_active_slave_get(struct bonding *bond)
{
	return __bond_option_active_slave_get(bond, bond->curr_active_slave);
}

int bond_option_active_slave_set(struct bonding *bond,
				 struct net_device *slave_dev)
{
	int ret = 0;

	if (slave_dev) {
		if (!netif_is_bond_slave(slave_dev)) {
			pr_err("Device %s is not bonding slave.\n",
			       slave_dev->name);
			return -EINVAL;
		}

		if (bond->dev != netdev_master_upper_dev_get(slave_dev)) {
			pr_err("%s: Device %s is not our slave.\n",
			       bond->dev->name, slave_dev->name);
			return -EINVAL;
		}
	}

	if (!USES_PRIMARY(bond->params.mode)) {
		pr_err("%s: Unable to change active slave; %s is in mode %d\n",
		       bond->dev->name, bond->dev->name, bond->params.mode);
		return -EINVAL;
	}

	block_netpoll_tx();
	write_lock_bh(&bond->curr_slave_lock);

	/* check to see if we are clearing active */
	if (!slave_dev) {
		pr_info("%s: Clearing current active slave.\n",
		bond->dev->name);
		rcu_assign_pointer(bond->curr_active_slave, NULL);
		bond_select_active_slave(bond);
	} else {
		struct slave *old_active = bond->curr_active_slave;
		struct slave *new_active = bond_slave_get_rtnl(slave_dev);

		BUG_ON(!new_active);

		if (new_active == old_active) {
			/* do nothing */
			pr_info("%s: %s is already the current active slave.\n",
				bond->dev->name, new_active->dev->name);
		} else {
			if (old_active && (new_active->link == BOND_LINK_UP) &&
			    IS_UP(new_active->dev)) {
				pr_info("%s: Setting %s as active slave.\n",
					bond->dev->name, new_active->dev->name);
				bond_change_active_slave(bond, new_active);
			} else {
				pr_err("%s: Could not set %s as active slave; either %s is down or the link is down.\n",
				       bond->dev->name, new_active->dev->name,
				       new_active->dev->name);
				ret = -EINVAL;
			}
		}
	}

	write_unlock_bh(&bond->curr_slave_lock);
	unblock_netpoll_tx();
	return ret;
}

int bond_option_miimon_set(struct bonding *bond, int miimon)
{
	if (miimon < 0) {
		pr_err("%s: Invalid miimon value %d not in range %d-%d; rejected.\n",
		       bond->dev->name, miimon, 0, INT_MAX);
		return -EINVAL;
	}
	pr_info("%s: Setting MII monitoring interval to %d.\n",
		bond->dev->name, miimon);
	bond->params.miimon = miimon;
	if (bond->params.updelay)
		pr_info("%s: Note: Updating updelay (to %d) since it is a multiple of the miimon value.\n",
			bond->dev->name,
			bond->params.updelay * bond->params.miimon);
	if (bond->params.downdelay)
		pr_info("%s: Note: Updating downdelay (to %d) since it is a multiple of the miimon value.\n",
			bond->dev->name,
			bond->params.downdelay * bond->params.miimon);
	if (miimon && bond->params.arp_interval) {
		pr_info("%s: MII monitoring cannot be used with ARP monitoring. Disabling ARP monitoring...\n",
			bond->dev->name);
		bond->params.arp_interval = 0;
		if (bond->params.arp_validate)
			bond->params.arp_validate = BOND_ARP_VALIDATE_NONE;
	}
	if (bond->dev->flags & IFF_UP) {
		/* If the interface is up, we may need to fire off
		 * the MII timer. If the interface is down, the
		 * timer will get fired off when the open function
		 * is called.
		 */
		if (!miimon) {
			cancel_delayed_work_sync(&bond->mii_work);
		} else {
			cancel_delayed_work_sync(&bond->arp_work);
			queue_delayed_work(bond->wq, &bond->mii_work, 0);
		}
	}
	return 0;
}

int bond_option_updelay_set(struct bonding *bond, int updelay)
{
	if (!(bond->params.miimon)) {
		pr_err("%s: Unable to set up delay as MII monitoring is disabled\n",
		       bond->dev->name);
		return -EPERM;
	}

	if (updelay < 0) {
		pr_err("%s: Invalid up delay value %d not in range %d-%d; rejected.\n",
		       bond->dev->name, updelay, 0, INT_MAX);
		return -EINVAL;
	} else {
		if ((updelay % bond->params.miimon) != 0) {
			pr_warn("%s: Warning: up delay (%d) is not a multiple of miimon (%d), updelay rounded to %d ms\n",
				bond->dev->name, updelay,
				bond->params.miimon,
				(updelay / bond->params.miimon) *
				bond->params.miimon);
		}
		bond->params.updelay = updelay / bond->params.miimon;
		pr_info("%s: Setting up delay to %d.\n",
			bond->dev->name,
			bond->params.updelay * bond->params.miimon);
	}

	return 0;
}

int bond_option_downdelay_set(struct bonding *bond, int downdelay)
{
	if (!(bond->params.miimon)) {
		pr_err("%s: Unable to set down delay as MII monitoring is disabled\n",
		       bond->dev->name);
		return -EPERM;
	}

	if (downdelay < 0) {
		pr_err("%s: Invalid down delay value %d not in range %d-%d; rejected.\n",
		       bond->dev->name, downdelay, 0, INT_MAX);
		return -EINVAL;
	} else {
		if ((downdelay % bond->params.miimon) != 0) {
			pr_warn("%s: Warning: down delay (%d) is not a multiple of miimon (%d), delay rounded to %d ms\n",
				bond->dev->name, downdelay,
				bond->params.miimon,
				(downdelay / bond->params.miimon) *
				bond->params.miimon);
		}
		bond->params.downdelay = downdelay / bond->params.miimon;
		pr_info("%s: Setting down delay to %d.\n",
			bond->dev->name,
			bond->params.downdelay * bond->params.miimon);
	}

	return 0;
}

int bond_option_use_carrier_set(struct bonding *bond, int use_carrier)
{
	if ((use_carrier == 0) || (use_carrier == 1)) {
		bond->params.use_carrier = use_carrier;
		pr_info("%s: Setting use_carrier to %d.\n",
			bond->dev->name, use_carrier);
	} else {
		pr_info("%s: Ignoring invalid use_carrier value %d.\n",
			bond->dev->name, use_carrier);
	}

	return 0;
}

int bond_option_arp_interval_set(struct bonding *bond, int arp_interval)
{
	if (arp_interval < 0) {
		pr_err("%s: Invalid arp_interval value %d not in range 0-%d; rejected.\n",
		       bond->dev->name, arp_interval, INT_MAX);
		return -EINVAL;
	}
	if (BOND_NO_USES_ARP(bond->params.mode)) {
		pr_info("%s: ARP monitoring cannot be used with ALB/TLB/802.3ad. Only MII monitoring is supported on %s.\n",
			bond->dev->name, bond->dev->name);
		return -EINVAL;
	}
	pr_info("%s: Setting ARP monitoring interval to %d.\n",
		bond->dev->name, arp_interval);
	bond->params.arp_interval = arp_interval;
	if (arp_interval) {
		if (bond->params.miimon) {
			pr_info("%s: ARP monitoring cannot be used with MII monitoring. %s Disabling MII monitoring.\n",
				bond->dev->name, bond->dev->name);
			bond->params.miimon = 0;
		}
		if (!bond->params.arp_targets[0])
			pr_info("%s: ARP monitoring has been set up, but no ARP targets have been specified.\n",
				bond->dev->name);
	}
	if (bond->dev->flags & IFF_UP) {
		/* If the interface is up, we may need to fire off
		 * the ARP timer.  If the interface is down, the
		 * timer will get fired off when the open function
		 * is called.
		 */
		if (!arp_interval) {
			if (bond->params.arp_validate)
				bond->recv_probe = NULL;
			cancel_delayed_work_sync(&bond->arp_work);
		} else {
			/* arp_validate can be set only in active-backup mode */
			if (bond->params.arp_validate)
				bond->recv_probe = bond_arp_rcv;
			cancel_delayed_work_sync(&bond->mii_work);
			queue_delayed_work(bond->wq, &bond->arp_work, 0);
		}
	}

	return 0;
}

static void _bond_options_arp_ip_target_set(struct bonding *bond, int slot,
					    __be32 target,
					    unsigned long last_rx)
{
	__be32 *targets = bond->params.arp_targets;
	struct list_head *iter;
	struct slave *slave;

	if (slot >= 0 && slot < BOND_MAX_ARP_TARGETS) {
		bond_for_each_slave(bond, slave, iter)
			slave->target_last_arp_rx[slot] = last_rx;
		targets[slot] = target;
	}
}

static int _bond_option_arp_ip_target_add(struct bonding *bond, __be32 target)
{
	__be32 *targets = bond->params.arp_targets;
	int ind;

	if (IS_IP_TARGET_UNUSABLE_ADDRESS(target)) {
		pr_err("%s: invalid ARP target %pI4 specified for addition\n",
		       bond->dev->name, &target);
		return -EINVAL;
	}

	if (bond_get_targets_ip(targets, target) != -1) { /* dup */
		pr_err("%s: ARP target %pI4 is already present\n",
		       bond->dev->name, &target);
		return -EINVAL;
	}

	ind = bond_get_targets_ip(targets, 0); /* first free slot */
	if (ind == -1) {
		pr_err("%s: ARP target table is full!\n",
		       bond->dev->name);
		return -EINVAL;
	}

	pr_info("%s: adding ARP target %pI4.\n", bond->dev->name, &target);

	_bond_options_arp_ip_target_set(bond, ind, target, jiffies);

	return 0;
}

int bond_option_arp_ip_target_add(struct bonding *bond, __be32 target)
{
	int ret;

	/* not to race with bond_arp_rcv */
	write_lock_bh(&bond->lock);
	ret = _bond_option_arp_ip_target_add(bond, target);
	write_unlock_bh(&bond->lock);

	return ret;
}

int bond_option_arp_ip_target_rem(struct bonding *bond, __be32 target)
{
	__be32 *targets = bond->params.arp_targets;
	struct list_head *iter;
	struct slave *slave;
	unsigned long *targets_rx;
	int ind, i;

	if (IS_IP_TARGET_UNUSABLE_ADDRESS(target)) {
		pr_err("%s: invalid ARP target %pI4 specified for removal\n",
		       bond->dev->name, &target);
		return -EINVAL;
	}

	ind = bond_get_targets_ip(targets, target);
	if (ind == -1) {
		pr_err("%s: unable to remove nonexistent ARP target %pI4.\n",
		       bond->dev->name, &target);
		return -EINVAL;
	}

	if (ind == 0 && !targets[1] && bond->params.arp_interval)
		pr_warn("%s: removing last arp target with arp_interval on\n",
			bond->dev->name);

	pr_info("%s: removing ARP target %pI4.\n", bond->dev->name,
		&target);

	/* not to race with bond_arp_rcv */
	write_lock_bh(&bond->lock);

	bond_for_each_slave(bond, slave, iter) {
		targets_rx = slave->target_last_arp_rx;
		for (i = ind; (i < BOND_MAX_ARP_TARGETS-1) && targets[i+1]; i++)
			targets_rx[i] = targets_rx[i+1];
		targets_rx[i] = 0;
	}
	for (i = ind; (i < BOND_MAX_ARP_TARGETS-1) && targets[i+1]; i++)
		targets[i] = targets[i+1];
	targets[i] = 0;

	write_unlock_bh(&bond->lock);

	return 0;
}

int bond_option_arp_ip_targets_set(struct bonding *bond, __be32 *targets,
				   int count)
{
	int i, ret = 0;

	/* not to race with bond_arp_rcv */
	write_lock_bh(&bond->lock);

	/* clear table */
	for (i = 0; i < BOND_MAX_ARP_TARGETS; i++)
		_bond_options_arp_ip_target_set(bond, i, 0, 0);

	if (count == 0 && bond->params.arp_interval)
		pr_warn("%s: removing last arp target with arp_interval on\n",
			bond->dev->name);

	for (i = 0; i < count; i++) {
		ret = _bond_option_arp_ip_target_add(bond, targets[i]);
		if (ret)
			break;
	}

	write_unlock_bh(&bond->lock);
	return ret;
}

int bond_option_arp_validate_set(struct bonding *bond, int arp_validate)
{
	if (bond_parm_tbl_lookup(arp_validate, arp_validate_tbl) < 0) {
		pr_err("%s: Ignoring invalid arp_validate value %d.\n",
		       bond->dev->name, arp_validate);
		return -EINVAL;
	}

	if (bond->params.mode != BOND_MODE_ACTIVEBACKUP) {
		pr_err("%s: arp_validate only supported in active-backup mode.\n",
		       bond->dev->name);
		return -EINVAL;
	}

	pr_info("%s: setting arp_validate to %s (%d).\n",
		bond->dev->name, arp_validate_tbl[arp_validate].modename,
		arp_validate);

	if (bond->dev->flags & IFF_UP) {
		if (!arp_validate)
			bond->recv_probe = NULL;
		else if (bond->params.arp_interval)
			bond->recv_probe = bond_arp_rcv;
	}
	bond->params.arp_validate = arp_validate;

	return 0;
}

int bond_option_arp_all_targets_set(struct bonding *bond, int arp_all_targets)
{
	if (bond_parm_tbl_lookup(arp_all_targets, arp_all_targets_tbl) < 0) {
		pr_err("%s: Ignoring invalid arp_all_targets value %d.\n",
		       bond->dev->name, arp_all_targets);
		return -EINVAL;
	}

	pr_info("%s: setting arp_all_targets to %s (%d).\n",
		bond->dev->name, arp_all_targets_tbl[arp_all_targets].modename,
		arp_all_targets);

	bond->params.arp_all_targets = arp_all_targets;

	return 0;
}

int bond_option_primary_set(struct bonding *bond, const char *primary)
{
	struct list_head *iter;
	struct slave *slave;
	int err = 0;

	block_netpoll_tx();
	read_lock(&bond->lock);
	write_lock_bh(&bond->curr_slave_lock);

	if (!USES_PRIMARY(bond->params.mode)) {
		pr_err("%s: Unable to set primary slave; %s is in mode %d\n",
		       bond->dev->name, bond->dev->name, bond->params.mode);
		err = -EINVAL;
		goto out;
	}

	/* check to see if we are clearing primary */
	if (!strlen(primary)) {
		pr_info("%s: Setting primary slave to None.\n",
			bond->dev->name);
		bond->primary_slave = NULL;
		memset(bond->params.primary, 0, sizeof(bond->params.primary));
		bond_select_active_slave(bond);
		goto out;
	}

	bond_for_each_slave(bond, slave, iter) {
		if (strncmp(slave->dev->name, primary, IFNAMSIZ) == 0) {
			pr_info("%s: Setting %s as primary slave.\n",
				bond->dev->name, slave->dev->name);
			bond->primary_slave = slave;
			strcpy(bond->params.primary, slave->dev->name);
			bond_select_active_slave(bond);
			goto out;
		}
	}

	strncpy(bond->params.primary, primary, IFNAMSIZ);
	bond->params.primary[IFNAMSIZ - 1] = 0;

	pr_info("%s: Recording %s as primary, but it has not been enslaved to %s yet.\n",
		bond->dev->name, primary, bond->dev->name);

out:
	write_unlock_bh(&bond->curr_slave_lock);
	read_unlock(&bond->lock);
	unblock_netpoll_tx();

	return err;
}

int bond_option_primary_reselect_set(struct bonding *bond, int primary_reselect)
{
	if (bond_parm_tbl_lookup(primary_reselect, pri_reselect_tbl) < 0) {
		pr_err("%s: Ignoring invalid primary_reselect value %d.\n",
		       bond->dev->name, primary_reselect);
		return -EINVAL;
	}

	bond->params.primary_reselect = primary_reselect;
	pr_info("%s: setting primary_reselect to %s (%d).\n",
		bond->dev->name, pri_reselect_tbl[primary_reselect].modename,
		primary_reselect);

	block_netpoll_tx();
	write_lock_bh(&bond->curr_slave_lock);
	bond_select_active_slave(bond);
	write_unlock_bh(&bond->curr_slave_lock);
	unblock_netpoll_tx();

	return 0;
}

int bond_option_fail_over_mac_set(struct bonding *bond, int fail_over_mac)
{
	if (bond_parm_tbl_lookup(fail_over_mac, fail_over_mac_tbl) < 0) {
		pr_err("%s: Ignoring invalid fail_over_mac value %d.\n",
		       bond->dev->name, fail_over_mac);
		return -EINVAL;
	}

	if (bond_has_slaves(bond)) {
		pr_err("%s: Can't alter fail_over_mac with slaves in bond.\n",
		       bond->dev->name);
		return -EPERM;
	}

	bond->params.fail_over_mac = fail_over_mac;
	pr_info("%s: Setting fail_over_mac to %s (%d).\n",
		bond->dev->name, fail_over_mac_tbl[fail_over_mac].modename,
		fail_over_mac);

	return 0;
}

int bond_option_xmit_hash_policy_set(struct bonding *bond, int xmit_hash_policy)
{
	if (bond_parm_tbl_lookup(xmit_hash_policy, xmit_hashtype_tbl) < 0) {
		pr_err("%s: Ignoring invalid xmit_hash_policy value %d.\n",
		       bond->dev->name, xmit_hash_policy);
		return -EINVAL;
	}

	bond->params.xmit_policy = xmit_hash_policy;
	pr_info("%s: setting xmit hash policy to %s (%d).\n",
		bond->dev->name,
		xmit_hashtype_tbl[xmit_hash_policy].modename, xmit_hash_policy);

	return 0;
}

int bond_option_resend_igmp_set(struct bonding *bond, int resend_igmp)
{
	if (resend_igmp < 0 || resend_igmp > 255) {
		pr_err("%s: Invalid resend_igmp value %d not in range 0-255; rejected.\n",
		       bond->dev->name, resend_igmp);
		return -EINVAL;
	}

	bond->params.resend_igmp = resend_igmp;
	pr_info("%s: Setting resend_igmp to %d.\n",
		bond->dev->name, resend_igmp);

	return 0;
}

int bond_option_num_peer_notif_set(struct bonding *bond, int num_peer_notif)
{
	bond->params.num_peer_notif = num_peer_notif;
	return 0;
}

int bond_option_all_slaves_active_set(struct bonding *bond,
				      int all_slaves_active)
{
	struct list_head *iter;
	struct slave *slave;

	if (all_slaves_active == bond->params.all_slaves_active)
		return 0;

	if ((all_slaves_active == 0) || (all_slaves_active == 1)) {
		bond->params.all_slaves_active = all_slaves_active;
	} else {
		pr_info("%s: Ignoring invalid all_slaves_active value %d.\n",
			bond->dev->name, all_slaves_active);
		return -EINVAL;
	}

	bond_for_each_slave(bond, slave, iter) {
		if (!bond_is_active_slave(slave)) {
			if (all_slaves_active)
				slave->inactive = 0;
			else
				slave->inactive = 1;
		}
	}

	return 0;
}

int bond_option_min_links_set(struct bonding *bond, int min_links)
{
	pr_info("%s: Setting min links value to %u\n",
		bond->dev->name, min_links);
	bond->params.min_links = min_links;

	return 0;
}

int bond_option_lp_interval_set(struct bonding *bond, int lp_interval)
{
	if (lp_interval <= 0) {
		pr_err("%s: lp_interval must be between 1 and %d\n",
		       bond->dev->name, INT_MAX);
		return -EINVAL;
	}

	bond->params.lp_interval = lp_interval;

	return 0;
}

int bond_option_packets_per_slave_set(struct bonding *bond,
				      int packets_per_slave)
{
	if (packets_per_slave < 0 || packets_per_slave > USHRT_MAX) {
		pr_err("%s: packets_per_slave must be between 0 and %u\n",
		       bond->dev->name, USHRT_MAX);
		return -EINVAL;
	}

	if (bond->params.mode != BOND_MODE_ROUNDROBIN)
		pr_warn("%s: Warning: packets_per_slave has effect only in balance-rr mode\n",
			bond->dev->name);

	bond->params.packets_per_slave = packets_per_slave;
	if (packets_per_slave > 0) {
		bond->params.reciprocal_packets_per_slave =
			reciprocal_value(packets_per_slave);
	} else {
		/* reciprocal_packets_per_slave is unused if
		 * packets_per_slave is 0 or 1, just initialize it
		 */
		bond->params.reciprocal_packets_per_slave =
			(struct reciprocal_value) { 0 };
	}

	return 0;
}

int bond_option_lacp_rate_set(struct bonding *bond, int lacp_rate)
{
	if (bond_parm_tbl_lookup(lacp_rate, bond_lacp_tbl) < 0) {
		pr_err("%s: Ignoring invalid LACP rate value %d.\n",
		       bond->dev->name, lacp_rate);
		return -EINVAL;
	}

	if (bond->dev->flags & IFF_UP) {
		pr_err("%s: Unable to update LACP rate because interface is up.\n",
		       bond->dev->name);
		return -EPERM;
	}

	if (bond->params.mode != BOND_MODE_8023AD) {
		pr_err("%s: Unable to update LACP rate because bond is not in 802.3ad mode.\n",
		       bond->dev->name);
		return -EPERM;
	}

	bond->params.lacp_fast = lacp_rate;
	bond_3ad_update_lacp_rate(bond);
	pr_info("%s: Setting LACP rate to %s (%d).\n",
		bond->dev->name, bond_lacp_tbl[lacp_rate].modename,
		lacp_rate);

	return 0;
}

int bond_option_ad_select_set(struct bonding *bond, int ad_select)
{
	if (bond_parm_tbl_lookup(ad_select, ad_select_tbl) < 0) {
		pr_err("%s: Ignoring invalid ad_select value %d.\n",
		       bond->dev->name, ad_select);
		return -EINVAL;
	}

	if (bond->dev->flags & IFF_UP) {
		pr_err("%s: Unable to update ad_select because interface is up.\n",
		       bond->dev->name);
		return -EPERM;
	}

	bond->params.ad_select = ad_select;
	pr_info("%s: Setting ad_select to %s (%d).\n",
		bond->dev->name, ad_select_tbl[ad_select].modename,
		ad_select);

	return 0;
}
