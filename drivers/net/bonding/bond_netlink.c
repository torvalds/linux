/*
 * drivers/net/bond/bond_netlink.c - Netlink interface for bonding
 * Copyright (c) 2013 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2013 Scott Feldman <sfeldma@cumulusnetworks.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <net/netlink.h>
#include <net/rtnetlink.h>
#include "bonding.h"

static const struct nla_policy bond_policy[IFLA_BOND_MAX + 1] = {
	[IFLA_BOND_MODE]		= { .type = NLA_U8 },
	[IFLA_BOND_ACTIVE_SLAVE]	= { .type = NLA_U32 },
	[IFLA_BOND_MIIMON]		= { .type = NLA_U32 },
};

static int bond_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static int bond_changelink(struct net_device *bond_dev,
			   struct nlattr *tb[], struct nlattr *data[])
{
	struct bonding *bond = netdev_priv(bond_dev);
	int err;

	if (!data)
		return 0;

	if (data[IFLA_BOND_MODE]) {
		int mode = nla_get_u8(data[IFLA_BOND_MODE]);

		err = bond_option_mode_set(bond, mode);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_ACTIVE_SLAVE]) {
		int ifindex = nla_get_u32(data[IFLA_BOND_ACTIVE_SLAVE]);
		struct net_device *slave_dev;

		if (ifindex == 0) {
			slave_dev = NULL;
		} else {
			slave_dev = __dev_get_by_index(dev_net(bond_dev),
						       ifindex);
			if (!slave_dev)
				return -ENODEV;
		}
		err = bond_option_active_slave_set(bond, slave_dev);
		if (err)
			return err;
	}
	if (data[IFLA_BOND_MIIMON]) {
		int miimon = nla_get_u32(data[IFLA_BOND_MIIMON]);

		err = bond_option_miimon_set(bond, miimon);
		if (err)
			return err;
	}
	return 0;
}

static int bond_newlink(struct net *src_net, struct net_device *bond_dev,
			struct nlattr *tb[], struct nlattr *data[])
{
	int err;

	err = bond_changelink(bond_dev, tb, data);
	if (err < 0)
		return err;

	return register_netdevice(bond_dev);
}

static size_t bond_get_size(const struct net_device *bond_dev)
{
	return nla_total_size(sizeof(u8)) +	/* IFLA_BOND_MODE */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_ACTIVE_SLAVE */
		nla_total_size(sizeof(u32)) +	/* IFLA_BOND_MIIMON */
		0;
}

static int bond_fill_info(struct sk_buff *skb,
			  const struct net_device *bond_dev)
{
	struct bonding *bond = netdev_priv(bond_dev);
	struct net_device *slave_dev = bond_option_active_slave_get(bond);

	if (nla_put_u8(skb, IFLA_BOND_MODE, bond->params.mode))
		goto nla_put_failure;

	if (slave_dev &&
	    nla_put_u32(skb, IFLA_BOND_ACTIVE_SLAVE, slave_dev->ifindex))
		goto nla_put_failure;

	if (nla_put_u32(skb, IFLA_BOND_MIIMON, bond->params.miimon))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

struct rtnl_link_ops bond_link_ops __read_mostly = {
	.kind			= "bond",
	.priv_size		= sizeof(struct bonding),
	.setup			= bond_setup,
	.maxtype		= IFLA_BOND_MAX,
	.policy			= bond_policy,
	.validate		= bond_validate,
	.newlink		= bond_newlink,
	.changelink		= bond_changelink,
	.get_size		= bond_get_size,
	.fill_info		= bond_fill_info,
	.get_num_tx_queues	= bond_get_num_tx_queues,
	.get_num_rx_queues	= bond_get_num_tx_queues, /* Use the same number
							     as for TX queues */
};

int __init bond_netlink_init(void)
{
	return rtnl_link_register(&bond_link_ops);
}

void bond_netlink_fini(void)
{
	rtnl_link_unregister(&bond_link_ops);
}

MODULE_ALIAS_RTNL_LINK("bond");
