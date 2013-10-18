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
