// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 *		James Yonan <james@openvpn.net>
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <net/rtnetlink.h>

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

	return 0;
}

static __exit void ovpn_cleanup(void)
{
	rtnl_link_unregister(&ovpn_link_ops);

	rcu_barrier();
}

module_init(ovpn_init);
module_exit(ovpn_cleanup);

MODULE_DESCRIPTION("OpenVPN data channel offload (ovpn)");
MODULE_AUTHOR("Antonio Quartulli <antonio@openvpn.net>");
MODULE_LICENSE("GPL");
