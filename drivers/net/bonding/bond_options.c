/*
 * drivers/net/bond/bond_options.c - bonding options
 * Copyright (c) 2013 Jiri Pirko <jiri@resnulli.us>
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

	if (BOND_MODE_IS_LB(mode) && bond->params.arp_interval) {
		pr_err("%s: %s mode is incompatible with arp monitoring.\n",
		       bond->dev->name, bond_mode_tbl[mode].modename);
		return -EINVAL;
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
