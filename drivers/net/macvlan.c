/*
 * Copyright (c) 2007 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * The code this is based on carried the following copyright notice:
 * ---
 * (C) Copyright 2001-2006
 * Alex Zeffertt, Cambridge Broadband Ltd, ajz@cambridgebroadband.com
 * Re-worked by Ben Greear <greearb@candelatech.com>
 * ---
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/rculist.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>
#include <linux/if_link.h>
#include <linux/if_macvlan.h>
#include <net/rtnetlink.h>
#include <net/xfrm.h>

#define MACVLAN_HASH_SIZE	(1 << BITS_PER_BYTE)

struct macvlan_port {
	struct net_device	*dev;
	struct hlist_head	vlan_hash[MACVLAN_HASH_SIZE];
	struct list_head	vlans;
};

static struct macvlan_dev *macvlan_hash_lookup(const struct macvlan_port *port,
					       const unsigned char *addr)
{
	struct macvlan_dev *vlan;
	struct hlist_node *n;

	hlist_for_each_entry_rcu(vlan, n, &port->vlan_hash[addr[5]], hlist) {
		if (!compare_ether_addr_64bits(vlan->dev->dev_addr, addr))
			return vlan;
	}
	return NULL;
}

static void macvlan_hash_add(struct macvlan_dev *vlan)
{
	struct macvlan_port *port = vlan->port;
	const unsigned char *addr = vlan->dev->dev_addr;

	hlist_add_head_rcu(&vlan->hlist, &port->vlan_hash[addr[5]]);
}

static void macvlan_hash_del(struct macvlan_dev *vlan)
{
	hlist_del_rcu(&vlan->hlist);
	synchronize_rcu();
}

static void macvlan_hash_change_addr(struct macvlan_dev *vlan,
					const unsigned char *addr)
{
	macvlan_hash_del(vlan);
	/* Now that we are unhashed it is safe to change the device
	 * address without confusing packet delivery.
	 */
	memcpy(vlan->dev->dev_addr, addr, ETH_ALEN);
	macvlan_hash_add(vlan);
}

static int macvlan_addr_busy(const struct macvlan_port *port,
				const unsigned char *addr)
{
	/* Test to see if the specified multicast address is
	 * currently in use by the underlying device or
	 * another macvlan.
	 */
	if (!compare_ether_addr_64bits(port->dev->dev_addr, addr))
		return 1;

	if (macvlan_hash_lookup(port, addr))
		return 1;

	return 0;
}


static int macvlan_broadcast_one(struct sk_buff *skb,
				 const struct macvlan_dev *vlan,
				 const struct ethhdr *eth, bool local)
{
	struct net_device *dev = vlan->dev;
	if (!skb)
		return NET_RX_DROP;

	if (local)
		return vlan->forward(dev, skb);

	skb->dev = dev;
	if (!compare_ether_addr_64bits(eth->h_dest,
				       dev->broadcast))
		skb->pkt_type = PACKET_BROADCAST;
	else
		skb->pkt_type = PACKET_MULTICAST;

	return vlan->receive(skb);
}

static void macvlan_broadcast(struct sk_buff *skb,
			      const struct macvlan_port *port,
			      struct net_device *src,
			      enum macvlan_mode mode)
{
	const struct ethhdr *eth = eth_hdr(skb);
	const struct macvlan_dev *vlan;
	struct hlist_node *n;
	struct sk_buff *nskb;
	unsigned int i;
	int err;

	if (skb->protocol == htons(ETH_P_PAUSE))
		return;

	for (i = 0; i < MACVLAN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(vlan, n, &port->vlan_hash[i], hlist) {
			if (vlan->dev == src || !(vlan->mode & mode))
				continue;

			nskb = skb_clone(skb, GFP_ATOMIC);
			err = macvlan_broadcast_one(nskb, vlan, eth,
					 mode == MACVLAN_MODE_BRIDGE);
			macvlan_count_rx(vlan, skb->len + ETH_HLEN,
					 err == NET_RX_SUCCESS, 1);
		}
	}
}

/* called under rcu_read_lock() from netif_receive_skb */
static struct sk_buff *macvlan_handle_frame(struct sk_buff *skb)
{
	const struct ethhdr *eth = eth_hdr(skb);
	const struct macvlan_port *port;
	const struct macvlan_dev *vlan;
	const struct macvlan_dev *src;
	struct net_device *dev;
	unsigned int len;

	port = rcu_dereference(skb->dev->macvlan_port);
	if (port == NULL)
		return skb;

	if (is_multicast_ether_addr(eth->h_dest)) {
		src = macvlan_hash_lookup(port, eth->h_source);
		if (!src)
			/* frame comes from an external address */
			macvlan_broadcast(skb, port, NULL,
					  MACVLAN_MODE_PRIVATE |
					  MACVLAN_MODE_VEPA    |
					  MACVLAN_MODE_BRIDGE);
		else if (src->mode == MACVLAN_MODE_VEPA)
			/* flood to everyone except source */
			macvlan_broadcast(skb, port, src->dev,
					  MACVLAN_MODE_VEPA |
					  MACVLAN_MODE_BRIDGE);
		else if (src->mode == MACVLAN_MODE_BRIDGE)
			/*
			 * flood only to VEPA ports, bridge ports
			 * already saw the frame on the way out.
			 */
			macvlan_broadcast(skb, port, src->dev,
					  MACVLAN_MODE_VEPA);
		return skb;
	}

	vlan = macvlan_hash_lookup(port, eth->h_dest);
	if (vlan == NULL)
		return skb;

	dev = vlan->dev;
	if (unlikely(!(dev->flags & IFF_UP))) {
		kfree_skb(skb);
		return NULL;
	}
	len = skb->len + ETH_HLEN;
	skb = skb_share_check(skb, GFP_ATOMIC);
	macvlan_count_rx(vlan, len, skb != NULL, 0);
	if (!skb)
		return NULL;

	skb->dev = dev;
	skb->pkt_type = PACKET_HOST;

	vlan->receive(skb);
	return NULL;
}

static int macvlan_queue_xmit(struct sk_buff *skb, struct net_device *dev)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);
	const struct macvlan_port *port = vlan->port;
	const struct macvlan_dev *dest;

	if (vlan->mode == MACVLAN_MODE_BRIDGE) {
		const struct ethhdr *eth = (void *)skb->data;

		/* send to other bridge ports directly */
		if (is_multicast_ether_addr(eth->h_dest)) {
			macvlan_broadcast(skb, port, dev, MACVLAN_MODE_BRIDGE);
			goto xmit_world;
		}

		dest = macvlan_hash_lookup(port, eth->h_dest);
		if (dest && dest->mode == MACVLAN_MODE_BRIDGE) {
			unsigned int length = skb->len + ETH_HLEN;
			int ret = dest->forward(dest->dev, skb);
			macvlan_count_rx(dest, length,
					 ret == NET_RX_SUCCESS, 0);

			return NET_XMIT_SUCCESS;
		}
	}

xmit_world:
	skb_set_dev(skb, vlan->lowerdev);
	return dev_queue_xmit(skb);
}

netdev_tx_t macvlan_start_xmit(struct sk_buff *skb,
			       struct net_device *dev)
{
	int i = skb_get_queue_mapping(skb);
	struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
	unsigned int len = skb->len;
	int ret;

	ret = macvlan_queue_xmit(skb, dev);
	if (likely(ret == NET_XMIT_SUCCESS)) {
		txq->tx_packets++;
		txq->tx_bytes += len;
	} else
		txq->tx_dropped++;

	return ret;
}
EXPORT_SYMBOL_GPL(macvlan_start_xmit);

static int macvlan_hard_header(struct sk_buff *skb, struct net_device *dev,
			       unsigned short type, const void *daddr,
			       const void *saddr, unsigned len)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	return dev_hard_header(skb, lowerdev, type, daddr,
			       saddr ? : dev->dev_addr, len);
}

static const struct header_ops macvlan_hard_header_ops = {
	.create  	= macvlan_hard_header,
	.rebuild	= eth_rebuild_header,
	.parse		= eth_header_parse,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
};

static int macvlan_open(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;
	int err;

	err = -EBUSY;
	if (macvlan_addr_busy(vlan->port, dev->dev_addr))
		goto out;

	err = dev_unicast_add(lowerdev, dev->dev_addr);
	if (err < 0)
		goto out;
	if (dev->flags & IFF_ALLMULTI) {
		err = dev_set_allmulti(lowerdev, 1);
		if (err < 0)
			goto del_unicast;
	}
	macvlan_hash_add(vlan);
	return 0;

del_unicast:
	dev_unicast_delete(lowerdev, dev->dev_addr);
out:
	return err;
}

static int macvlan_stop(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	dev_mc_unsync(lowerdev, dev);
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(lowerdev, -1);

	dev_unicast_delete(lowerdev, dev->dev_addr);

	macvlan_hash_del(vlan);
	return 0;
}

static int macvlan_set_mac_address(struct net_device *dev, void *p)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	if (!(dev->flags & IFF_UP)) {
		/* Just copy in the new address */
		memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
	} else {
		/* Rehash and update the device filters */
		if (macvlan_addr_busy(vlan->port, addr->sa_data))
			return -EBUSY;

		err = dev_unicast_add(lowerdev, addr->sa_data);
		if (err)
			return err;

		dev_unicast_delete(lowerdev, dev->dev_addr);

		macvlan_hash_change_addr(vlan, addr->sa_data);
	}
	return 0;
}

static void macvlan_change_rx_flags(struct net_device *dev, int change)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	if (change & IFF_ALLMULTI)
		dev_set_allmulti(lowerdev, dev->flags & IFF_ALLMULTI ? 1 : -1);
}

static void macvlan_set_multicast_list(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	dev_mc_sync(vlan->lowerdev, dev);
}

static int macvlan_change_mtu(struct net_device *dev, int new_mtu)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	if (new_mtu < 68 || vlan->lowerdev->mtu < new_mtu)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

/*
 * macvlan network devices have devices nesting below it and are a special
 * "super class" of normal network devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key macvlan_netdev_xmit_lock_key;
static struct lock_class_key macvlan_netdev_addr_lock_key;

#define MACVLAN_FEATURES \
	(NETIF_F_SG | NETIF_F_ALL_CSUM | NETIF_F_HIGHDMA | NETIF_F_FRAGLIST | \
	 NETIF_F_GSO | NETIF_F_TSO | NETIF_F_UFO | NETIF_F_GSO_ROBUST | \
	 NETIF_F_TSO_ECN | NETIF_F_TSO6 | NETIF_F_GRO)

#define MACVLAN_STATE_MASK \
	((1<<__LINK_STATE_NOCARRIER) | (1<<__LINK_STATE_DORMANT))

static void macvlan_set_lockdep_class_one(struct net_device *dev,
					  struct netdev_queue *txq,
					  void *_unused)
{
	lockdep_set_class(&txq->_xmit_lock,
			  &macvlan_netdev_xmit_lock_key);
}

static void macvlan_set_lockdep_class(struct net_device *dev)
{
	lockdep_set_class(&dev->addr_list_lock,
			  &macvlan_netdev_addr_lock_key);
	netdev_for_each_tx_queue(dev, macvlan_set_lockdep_class_one, NULL);
}

static int macvlan_init(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	const struct net_device *lowerdev = vlan->lowerdev;

	dev->state		= (dev->state & ~MACVLAN_STATE_MASK) |
				  (lowerdev->state & MACVLAN_STATE_MASK);
	dev->features 		= lowerdev->features & MACVLAN_FEATURES;
	dev->gso_max_size	= lowerdev->gso_max_size;
	dev->iflink		= lowerdev->ifindex;
	dev->hard_header_len	= lowerdev->hard_header_len;

	macvlan_set_lockdep_class(dev);

	vlan->rx_stats = alloc_percpu(struct macvlan_rx_stats);
	if (!vlan->rx_stats)
		return -ENOMEM;

	return 0;
}

static void macvlan_uninit(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	free_percpu(vlan->rx_stats);
}

static struct net_device_stats *macvlan_dev_get_stats(struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	struct macvlan_dev *vlan = netdev_priv(dev);

	dev_txq_stats_fold(dev, stats);

	if (vlan->rx_stats) {
		struct macvlan_rx_stats *p, rx = {0};
		int i;

		for_each_possible_cpu(i) {
			p = per_cpu_ptr(vlan->rx_stats, i);
			rx.rx_packets += p->rx_packets;
			rx.rx_bytes   += p->rx_bytes;
			rx.rx_errors  += p->rx_errors;
			rx.multicast  += p->multicast;
		}
		stats->rx_packets = rx.rx_packets;
		stats->rx_bytes   = rx.rx_bytes;
		stats->rx_errors  = rx.rx_errors;
		stats->rx_dropped = rx.rx_errors;
		stats->multicast  = rx.multicast;
	}
	return stats;
}

static void macvlan_ethtool_get_drvinfo(struct net_device *dev,
					struct ethtool_drvinfo *drvinfo)
{
	snprintf(drvinfo->driver, 32, "macvlan");
	snprintf(drvinfo->version, 32, "0.1");
}

static u32 macvlan_ethtool_get_rx_csum(struct net_device *dev)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);
	return dev_ethtool_get_rx_csum(vlan->lowerdev);
}

static int macvlan_ethtool_get_settings(struct net_device *dev,
					struct ethtool_cmd *cmd)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);
	return dev_ethtool_get_settings(vlan->lowerdev, cmd);
}

static u32 macvlan_ethtool_get_flags(struct net_device *dev)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);
	return dev_ethtool_get_flags(vlan->lowerdev);
}

static const struct ethtool_ops macvlan_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
	.get_settings		= macvlan_ethtool_get_settings,
	.get_rx_csum		= macvlan_ethtool_get_rx_csum,
	.get_drvinfo		= macvlan_ethtool_get_drvinfo,
	.get_flags		= macvlan_ethtool_get_flags,
};

static const struct net_device_ops macvlan_netdev_ops = {
	.ndo_init		= macvlan_init,
	.ndo_uninit		= macvlan_uninit,
	.ndo_open		= macvlan_open,
	.ndo_stop		= macvlan_stop,
	.ndo_start_xmit		= macvlan_start_xmit,
	.ndo_change_mtu		= macvlan_change_mtu,
	.ndo_change_rx_flags	= macvlan_change_rx_flags,
	.ndo_set_mac_address	= macvlan_set_mac_address,
	.ndo_set_multicast_list	= macvlan_set_multicast_list,
	.ndo_get_stats		= macvlan_dev_get_stats,
	.ndo_validate_addr	= eth_validate_addr,
};

static void macvlan_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->priv_flags	       &= ~IFF_XMIT_DST_RELEASE;
	dev->netdev_ops		= &macvlan_netdev_ops;
	dev->destructor		= free_netdev;
	dev->header_ops		= &macvlan_hard_header_ops,
	dev->ethtool_ops	= &macvlan_ethtool_ops;
	dev->tx_queue_len	= 0;
}

static int macvlan_port_create(struct net_device *dev)
{
	struct macvlan_port *port;
	unsigned int i;

	if (dev->type != ARPHRD_ETHER || dev->flags & IFF_LOOPBACK)
		return -EINVAL;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (port == NULL)
		return -ENOMEM;

	port->dev = dev;
	INIT_LIST_HEAD(&port->vlans);
	for (i = 0; i < MACVLAN_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&port->vlan_hash[i]);
	rcu_assign_pointer(dev->macvlan_port, port);
	return 0;
}

static void macvlan_port_destroy(struct net_device *dev)
{
	struct macvlan_port *port = dev->macvlan_port;

	rcu_assign_pointer(dev->macvlan_port, NULL);
	synchronize_rcu();
	kfree(port);
}

static int macvlan_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	if (data && data[IFLA_MACVLAN_MODE]) {
		switch (nla_get_u32(data[IFLA_MACVLAN_MODE])) {
		case MACVLAN_MODE_PRIVATE:
		case MACVLAN_MODE_VEPA:
		case MACVLAN_MODE_BRIDGE:
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static int macvlan_get_tx_queues(struct net *net,
				 struct nlattr *tb[],
				 unsigned int *num_tx_queues,
				 unsigned int *real_num_tx_queues)
{
	struct net_device *real_dev;

	if (!tb[IFLA_LINK])
		return -EINVAL;

	real_dev = __dev_get_by_index(net, nla_get_u32(tb[IFLA_LINK]));
	if (!real_dev)
		return -ENODEV;

	*num_tx_queues      = real_dev->num_tx_queues;
	*real_num_tx_queues = real_dev->real_num_tx_queues;
	return 0;
}

int macvlan_common_newlink(struct net *src_net, struct net_device *dev,
			   struct nlattr *tb[], struct nlattr *data[],
			   int (*receive)(struct sk_buff *skb),
			   int (*forward)(struct net_device *dev,
					  struct sk_buff *skb))
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct macvlan_port *port;
	struct net_device *lowerdev;
	int err;

	if (!tb[IFLA_LINK])
		return -EINVAL;

	lowerdev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (lowerdev == NULL)
		return -ENODEV;

	/* When creating macvlans on top of other macvlans - use
	 * the real device as the lowerdev.
	 */
	if (lowerdev->rtnl_link_ops == dev->rtnl_link_ops) {
		struct macvlan_dev *lowervlan = netdev_priv(lowerdev);
		lowerdev = lowervlan->lowerdev;
	}

	if (!tb[IFLA_MTU])
		dev->mtu = lowerdev->mtu;
	else if (dev->mtu > lowerdev->mtu)
		return -EINVAL;

	if (!tb[IFLA_ADDRESS])
		random_ether_addr(dev->dev_addr);

	if (lowerdev->macvlan_port == NULL) {
		err = macvlan_port_create(lowerdev);
		if (err < 0)
			return err;
	}
	port = lowerdev->macvlan_port;

	vlan->lowerdev = lowerdev;
	vlan->dev      = dev;
	vlan->port     = port;
	vlan->receive  = receive;
	vlan->forward  = forward;

	vlan->mode     = MACVLAN_MODE_VEPA;
	if (data && data[IFLA_MACVLAN_MODE])
		vlan->mode = nla_get_u32(data[IFLA_MACVLAN_MODE]);

	err = register_netdevice(dev);
	if (err < 0)
		return err;

	list_add_tail(&vlan->list, &port->vlans);
	netif_stacked_transfer_operstate(lowerdev, dev);
	return 0;
}
EXPORT_SYMBOL_GPL(macvlan_common_newlink);

static int macvlan_newlink(struct net *src_net, struct net_device *dev,
			   struct nlattr *tb[], struct nlattr *data[])
{
	return macvlan_common_newlink(src_net, dev, tb, data,
				      netif_rx,
				      dev_forward_skb);
}

void macvlan_dellink(struct net_device *dev, struct list_head *head)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct macvlan_port *port = vlan->port;

	list_del(&vlan->list);
	unregister_netdevice_queue(dev, head);

	if (list_empty(&port->vlans))
		macvlan_port_destroy(port->dev);
}
EXPORT_SYMBOL_GPL(macvlan_dellink);

static int macvlan_changelink(struct net_device *dev,
		struct nlattr *tb[], struct nlattr *data[])
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	if (data && data[IFLA_MACVLAN_MODE])
		vlan->mode = nla_get_u32(data[IFLA_MACVLAN_MODE]);
	return 0;
}

static size_t macvlan_get_size(const struct net_device *dev)
{
	return nla_total_size(4);
}

static int macvlan_fill_info(struct sk_buff *skb,
				const struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	NLA_PUT_U32(skb, IFLA_MACVLAN_MODE, vlan->mode);
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy macvlan_policy[IFLA_MACVLAN_MAX + 1] = {
	[IFLA_MACVLAN_MODE] = { .type = NLA_U32 },
};

int macvlan_link_register(struct rtnl_link_ops *ops)
{
	/* common fields */
	ops->priv_size		= sizeof(struct macvlan_dev);
	ops->get_tx_queues	= macvlan_get_tx_queues;
	ops->setup		= macvlan_setup;
	ops->validate		= macvlan_validate;
	ops->maxtype		= IFLA_MACVLAN_MAX;
	ops->policy		= macvlan_policy;
	ops->changelink		= macvlan_changelink;
	ops->get_size		= macvlan_get_size;
	ops->fill_info		= macvlan_fill_info;

	return rtnl_link_register(ops);
};
EXPORT_SYMBOL_GPL(macvlan_link_register);

static struct rtnl_link_ops macvlan_link_ops = {
	.kind		= "macvlan",
	.newlink	= macvlan_newlink,
	.dellink	= macvlan_dellink,
};

static int macvlan_device_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct macvlan_dev *vlan, *next;
	struct macvlan_port *port;

	port = dev->macvlan_port;
	if (port == NULL)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGE:
		list_for_each_entry(vlan, &port->vlans, list)
			netif_stacked_transfer_operstate(vlan->lowerdev,
							 vlan->dev);
		break;
	case NETDEV_FEAT_CHANGE:
		list_for_each_entry(vlan, &port->vlans, list) {
			vlan->dev->features = dev->features & MACVLAN_FEATURES;
			vlan->dev->gso_max_size = dev->gso_max_size;
			netdev_features_change(vlan->dev);
		}
		break;
	case NETDEV_UNREGISTER:
		list_for_each_entry_safe(vlan, next, &port->vlans, list)
			vlan->dev->rtnl_link_ops->dellink(vlan->dev, NULL);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block macvlan_notifier_block __read_mostly = {
	.notifier_call	= macvlan_device_event,
};

static int __init macvlan_init_module(void)
{
	int err;

	register_netdevice_notifier(&macvlan_notifier_block);
	macvlan_handle_frame_hook = macvlan_handle_frame;

	err = macvlan_link_register(&macvlan_link_ops);
	if (err < 0)
		goto err1;
	return 0;
err1:
	macvlan_handle_frame_hook = NULL;
	unregister_netdevice_notifier(&macvlan_notifier_block);
	return err;
}

static void __exit macvlan_cleanup_module(void)
{
	rtnl_link_unregister(&macvlan_link_ops);
	macvlan_handle_frame_hook = NULL;
	unregister_netdevice_notifier(&macvlan_notifier_block);
}

module_init(macvlan_init_module);
module_exit(macvlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Driver for MAC address based VLANs");
MODULE_ALIAS_RTNL_LINK("macvlan");
