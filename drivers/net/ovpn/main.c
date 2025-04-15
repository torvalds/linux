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
#include <linux/inetdevice.h>
#include <net/ip.h>
#include <net/rtnetlink.h>
#include <uapi/linux/if_arp.h>

#include "ovpnpriv.h"
#include "main.h"
#include "netlink.h"
#include "io.h"
#include "peer.h"
#include "proto.h"

static const struct net_device_ops ovpn_netdev_ops = {
	.ndo_start_xmit		= ovpn_net_xmit,
};

static const struct device_type ovpn_type = {
	.name = OVPN_FAMILY_NAME,
};

static const struct nla_policy ovpn_policy[IFLA_OVPN_MAX + 1] = {
	[IFLA_OVPN_MODE] = NLA_POLICY_RANGE(NLA_U8, OVPN_MODE_P2P,
					    OVPN_MODE_MP),
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

static void ovpn_setup(struct net_device *dev)
{
	netdev_features_t feat = NETIF_F_SG | NETIF_F_GSO |
				 NETIF_F_GSO_SOFTWARE | NETIF_F_HIGHDMA;

	dev->needs_free_netdev = true;

	dev->pcpu_stat_type = NETDEV_PCPU_STAT_DSTATS;

	dev->netdev_ops = &ovpn_netdev_ops;

	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->mtu = ETH_DATA_LEN - OVPN_HEAD_ROOM;
	dev->min_mtu = IPV4_MIN_MTU;
	dev->max_mtu = IP_MAX_MTU - OVPN_HEAD_ROOM;

	dev->type = ARPHRD_NONE;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->priv_flags |= IFF_NO_QUEUE;

	dev->lltx = true;
	dev->features |= feat;
	dev->hw_features |= feat;
	dev->hw_enc_features |= feat;

	dev->needed_headroom = ALIGN(OVPN_HEAD_ROOM, 4);
	dev->needed_tailroom = OVPN_MAX_PADDING;

	SET_NETDEV_DEVTYPE(dev, &ovpn_type);
}

static int ovpn_newlink(struct net_device *dev,
			struct rtnl_newlink_params *params,
			struct netlink_ext_ack *extack)
{
	struct ovpn_priv *ovpn = netdev_priv(dev);
	struct nlattr **data = params->data;
	enum ovpn_mode mode = OVPN_MODE_P2P;

	if (data && data[IFLA_OVPN_MODE]) {
		mode = nla_get_u8(data[IFLA_OVPN_MODE]);
		netdev_dbg(dev, "setting device mode: %u\n", mode);
	}

	ovpn->dev = dev;
	ovpn->mode = mode;
	spin_lock_init(&ovpn->lock);

	/* Set carrier explicitly after registration, this way state is
	 * clearly defined.
	 *
	 * In case of MP interfaces we keep the carrier always on.
	 *
	 * Carrier for P2P interfaces is initially off and it is then
	 * switched on and off when the remote peer is added or deleted.
	 */
	if (ovpn->mode == OVPN_MODE_MP)
		netif_carrier_on(dev);
	else
		netif_carrier_off(dev);

	return register_netdevice(dev);
}

static void ovpn_dellink(struct net_device *dev, struct list_head *head)
{
	struct ovpn_priv *ovpn = netdev_priv(dev);

	if (ovpn->mode == OVPN_MODE_P2P)
		ovpn_peer_release_p2p(ovpn, NULL,
				      OVPN_DEL_PEER_REASON_TEARDOWN);

	unregister_netdevice_queue(dev, head);
}

static int ovpn_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ovpn_priv *ovpn = netdev_priv(dev);

	if (nla_put_u8(skb, IFLA_OVPN_MODE, ovpn->mode))
		return -EMSGSIZE;

	return 0;
}

static struct rtnl_link_ops ovpn_link_ops = {
	.kind = "ovpn",
	.netns_refund = false,
	.priv_size = sizeof(struct ovpn_priv),
	.setup = ovpn_setup,
	.policy = ovpn_policy,
	.maxtype = IFLA_OVPN_MAX,
	.newlink = ovpn_newlink,
	.dellink = ovpn_dellink,
	.fill_info = ovpn_fill_info,
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
