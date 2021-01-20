/*
 * vxcan.c - Virtual CAN Tunnel for cross namespace communication
 *
 * This code is derived from drivers/net/can/vcan.c for the virtual CAN
 * specific parts and from drivers/net/veth.c to implement the netlink API
 * for network interface pairs in a common and established way.
 *
 * Copyright (c) 2017 Oliver Hartkopp <socketcan@hartkopp.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/skb.h>
#include <linux/can/vxcan.h>
#include <linux/slab.h>
#include <net/rtnetlink.h>

#define DRV_NAME "vxcan"

MODULE_DESCRIPTION("Virtual CAN Tunnel");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oliver Hartkopp <socketcan@hartkopp.net>");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);

struct vxcan_priv {
	struct net_device __rcu	*peer;
};

static netdev_tx_t vxcan_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vxcan_priv *priv = netdev_priv(dev);
	struct net_device *peer;
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	struct net_device_stats *peerstats, *srcstats = &dev->stats;
	u8 len;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	rcu_read_lock();
	peer = rcu_dereference(priv->peer);
	if (unlikely(!peer)) {
		kfree_skb(skb);
		dev->stats.tx_dropped++;
		goto out_unlock;
	}

	skb = can_create_echo_skb(skb);
	if (!skb)
		goto out_unlock;

	/* reset CAN GW hop counter */
	skb->csum_start = 0;
	skb->pkt_type   = PACKET_BROADCAST;
	skb->dev        = peer;
	skb->ip_summed  = CHECKSUM_UNNECESSARY;

	len = cfd->len;
	if (netif_rx_ni(skb) == NET_RX_SUCCESS) {
		srcstats->tx_packets++;
		srcstats->tx_bytes += len;
		peerstats = &peer->stats;
		peerstats->rx_packets++;
		peerstats->rx_bytes += len;
	}

out_unlock:
	rcu_read_unlock();
	return NETDEV_TX_OK;
}


static int vxcan_open(struct net_device *dev)
{
	struct vxcan_priv *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);

	if (!peer)
		return -ENOTCONN;

	if (peer->flags & IFF_UP) {
		netif_carrier_on(dev);
		netif_carrier_on(peer);
	}
	return 0;
}

static int vxcan_close(struct net_device *dev)
{
	struct vxcan_priv *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);

	netif_carrier_off(dev);
	if (peer)
		netif_carrier_off(peer);

	return 0;
}

static int vxcan_get_iflink(const struct net_device *dev)
{
	struct vxcan_priv *priv = netdev_priv(dev);
	struct net_device *peer;
	int iflink;

	rcu_read_lock();
	peer = rcu_dereference(priv->peer);
	iflink = peer ? peer->ifindex : 0;
	rcu_read_unlock();

	return iflink;
}

static int vxcan_change_mtu(struct net_device *dev, int new_mtu)
{
	/* Do not allow changing the MTU while running */
	if (dev->flags & IFF_UP)
		return -EBUSY;

	if (new_mtu != CAN_MTU && new_mtu != CANFD_MTU)
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops vxcan_netdev_ops = {
	.ndo_open	= vxcan_open,
	.ndo_stop	= vxcan_close,
	.ndo_start_xmit	= vxcan_xmit,
	.ndo_get_iflink	= vxcan_get_iflink,
	.ndo_change_mtu = vxcan_change_mtu,
};

static void vxcan_setup(struct net_device *dev)
{
	dev->type		= ARPHRD_CAN;
	dev->mtu		= CANFD_MTU;
	dev->hard_header_len	= 0;
	dev->addr_len		= 0;
	dev->tx_queue_len	= 0;
	dev->flags		= (IFF_NOARP|IFF_ECHO);
	dev->netdev_ops		= &vxcan_netdev_ops;
	dev->needs_free_netdev	= true;
}

/* forward declaration for rtnl_create_link() */
static struct rtnl_link_ops vxcan_link_ops;

static int vxcan_newlink(struct net *net, struct net_device *dev,
			 struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	struct vxcan_priv *priv;
	struct net_device *peer;
	struct net *peer_net;

	struct nlattr *peer_tb[IFLA_MAX + 1], **tbp = tb;
	char ifname[IFNAMSIZ];
	unsigned char name_assign_type;
	struct ifinfomsg *ifmp = NULL;
	int err;

	/* register peer device */
	if (data && data[VXCAN_INFO_PEER]) {
		struct nlattr *nla_peer;

		nla_peer = data[VXCAN_INFO_PEER];
		ifmp = nla_data(nla_peer);
		err = rtnl_nla_parse_ifla(peer_tb,
					  nla_data(nla_peer) +
					  sizeof(struct ifinfomsg),
					  nla_len(nla_peer) -
					  sizeof(struct ifinfomsg),
					  NULL);
		if (err < 0)
			return err;

		tbp = peer_tb;
	}

	if (ifmp && tbp[IFLA_IFNAME]) {
		nla_strlcpy(ifname, tbp[IFLA_IFNAME], IFNAMSIZ);
		name_assign_type = NET_NAME_USER;
	} else {
		snprintf(ifname, IFNAMSIZ, DRV_NAME "%%d");
		name_assign_type = NET_NAME_ENUM;
	}

	peer_net = rtnl_link_get_net(net, tbp);
	if (IS_ERR(peer_net))
		return PTR_ERR(peer_net);

	peer = rtnl_create_link(peer_net, ifname, name_assign_type,
				&vxcan_link_ops, tbp);
	if (IS_ERR(peer)) {
		put_net(peer_net);
		return PTR_ERR(peer);
	}

	if (ifmp && dev->ifindex)
		peer->ifindex = ifmp->ifi_index;

	err = register_netdevice(peer);
	put_net(peer_net);
	peer_net = NULL;
	if (err < 0) {
		free_netdev(peer);
		return err;
	}

	netif_carrier_off(peer);

	err = rtnl_configure_link(peer, ifmp);
	if (err < 0)
		goto unregister_network_device;

	/* register first device */
	if (tb[IFLA_IFNAME])
		nla_strlcpy(dev->name, tb[IFLA_IFNAME], IFNAMSIZ);
	else
		snprintf(dev->name, IFNAMSIZ, DRV_NAME "%%d");

	err = register_netdevice(dev);
	if (err < 0)
		goto unregister_network_device;

	netif_carrier_off(dev);

	/* cross link the device pair */
	priv = netdev_priv(dev);
	rcu_assign_pointer(priv->peer, peer);

	priv = netdev_priv(peer);
	rcu_assign_pointer(priv->peer, dev);

	return 0;

unregister_network_device:
	unregister_netdevice(peer);
	return err;
}

static void vxcan_dellink(struct net_device *dev, struct list_head *head)
{
	struct vxcan_priv *priv;
	struct net_device *peer;

	priv = netdev_priv(dev);
	peer = rtnl_dereference(priv->peer);

	/* Note : dellink() is called from default_device_exit_batch(),
	 * before a rcu_synchronize() point. The devices are guaranteed
	 * not being freed before one RCU grace period.
	 */
	RCU_INIT_POINTER(priv->peer, NULL);
	unregister_netdevice_queue(dev, head);

	if (peer) {
		priv = netdev_priv(peer);
		RCU_INIT_POINTER(priv->peer, NULL);
		unregister_netdevice_queue(peer, head);
	}
}

static const struct nla_policy vxcan_policy[VXCAN_INFO_MAX + 1] = {
	[VXCAN_INFO_PEER] = { .len = sizeof(struct ifinfomsg) },
};

static struct net *vxcan_get_link_net(const struct net_device *dev)
{
	struct vxcan_priv *priv = netdev_priv(dev);
	struct net_device *peer = rtnl_dereference(priv->peer);

	return peer ? dev_net(peer) : dev_net(dev);
}

static struct rtnl_link_ops vxcan_link_ops = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct vxcan_priv),
	.setup		= vxcan_setup,
	.newlink	= vxcan_newlink,
	.dellink	= vxcan_dellink,
	.policy		= vxcan_policy,
	.maxtype	= VXCAN_INFO_MAX,
	.get_link_net	= vxcan_get_link_net,
};

static __init int vxcan_init(void)
{
	pr_info("vxcan: Virtual CAN Tunnel driver\n");

	return rtnl_link_register(&vxcan_link_ops);
}

static __exit void vxcan_exit(void)
{
	rtnl_link_unregister(&vxcan_link_ops);
}

module_init(vxcan_init);
module_exit(vxcan_exit);
