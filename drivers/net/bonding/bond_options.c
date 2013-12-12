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

static bool bond_mode_is_valid(int mode)
{
	int i;

	for (i = 0; bond_mode_tbl[i].modename; i++);

	return mode >= 0 && mode < i;
}

int bond_option_mode_set(struct bonding *bond, int mode)
{
	if (!bond_mode_is_valid(mode)) {
		pr_err("invalid mode value %d.\n", mode);
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
	read_lock(&bond->lock);
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
	read_unlock(&bond->lock);
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
