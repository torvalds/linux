// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 *		James Yonan <james@openvpn.net>
 */

#include <linux/genetlink.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/rtnetlink.h>
#include <uapi/linux/ovpn.h>

#include "ovpnpriv.h"
#include "main.h"
#include "netlink.h"

static const struct net_device_ops ovpn_netdev_ops = {
};

/**
 * ovpn_dev_is_valid - check if the netdevice is of type 'ovpn'
 * @dev: the interface to check
 *
 * Return: whether the netdevice is of type 'ovpn'
 */
bool ovpn_dev_is_valid(const struct net_device *dev)
{
	return dev->netdev_ops == &ovpn_netdev_ops;
}

static int ovpn_newlink(struct net_device *dev,
			struct rtnl_newlink_params *params,
			struct netlink_ext_ack *extack)
{
	return -EOPNOTSUPP;
}

static struct rtnl_link_ops ovpn_link_ops = {
	.kind = "ovpn",
	.netns_refund = false,
	.newlink = ovpn_newlink,
	.dellink = unregister_netdevice_queue,
};

static int __init ovpn_init(void)
{
	int err = rtnl_link_register(&ovpn_link_ops);

	if (err) {
		pr_err("ovpn: can't register rtnl link ops: %d\n", err);
		return err;
	}

	err = ovpn_nl_register();
	if (err) {
		pr_err("ovpn: can't register netlink family: %d\n", err);
		goto unreg_rtnl;
	}

	return 0;

unreg_rtnl:
	rtnl_link_unregister(&ovpn_link_ops);
	return err;
}

static __exit void ovpn_cleanup(void)
{
	ovpn_nl_unregister();
	rtnl_link_unregister(&ovpn_link_ops);

	rcu_barrier();
}

module_init(ovpn_init);
module_exit(ovpn_cleanup);

MODULE_DESCRIPTION("OpenVPN data channel offload (ovpn)");
MODULE_AUTHOR("Antonio Quartulli <antonio@openvpn.net>");
MODULE_LICENSE("GPL");
