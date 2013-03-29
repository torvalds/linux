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
#include <linux/if_vlan.h>
#include <linux/if_link.h>
#include <linux/if_macvlan.h>
#include <linux/hash.h>
#include <net/rtnetlink.h>
#include <net/xfrm.h>

#define MACVLAN_HASH_SIZE	(1 << BITS_PER_BYTE)

struct macvlan_port {
	struct net_device	*dev;
	struct hlist_head	vlan_hash[MACVLAN_HASH_SIZE];
	struct list_head	vlans;
	struct rcu_head		rcu;
	bool 			passthru;
	int			count;
};

static void macvlan_port_destroy(struct net_device *dev);

#define macvlan_port_get_rcu(dev) \
	((struct macvlan_port *) rcu_dereference(dev->rx_handler_data))
#define macvlan_port_get(dev) ((struct macvlan_port *) dev->rx_handler_data)
#define macvlan_port_exists(dev) (dev->priv_flags & IFF_MACVLAN_PORT)

static struct macvlan_dev *macvlan_hash_lookup(const struct macvlan_port *port,
					       const unsigned char *addr)
{
	struct macvlan_dev *vlan;

	hlist_for_each_entry_rcu(vlan, &port->vlan_hash[addr[5]], hlist) {
		if (ether_addr_equal_64bits(vlan->dev->dev_addr, addr))
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

static void macvlan_hash_del(struct macvlan_dev *vlan, bool sync)
{
	hlist_del_rcu(&vlan->hlist);
	if (sync)
		synchronize_rcu();
}

static void macvlan_hash_change_addr(struct macvlan_dev *vlan,
					const unsigned char *addr)
{
	macvlan_hash_del(vlan, true);
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
	if (ether_addr_equal_64bits(port->dev->dev_addr, addr))
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
	if (ether_addr_equal_64bits(eth->h_dest, dev->broadcast))
		skb->pkt_type = PACKET_BROADCAST;
	else
		skb->pkt_type = PACKET_MULTICAST;

	return vlan->receive(skb);
}

static u32 macvlan_hash_mix(const struct macvlan_dev *vlan)
{
	return (u32)(((unsigned long)vlan) >> L1_CACHE_SHIFT);
}


static unsigned int mc_hash(const struct macvlan_dev *vlan,
			    const unsigned char *addr)
{
	u32 val = __get_unaligned_cpu32(addr + 2);

	val ^= macvlan_hash_mix(vlan);
	return hash_32(val, MACVLAN_MC_FILTER_BITS);
}

static void macvlan_broadcast(struct sk_buff *skb,
			      const struct macvlan_port *port,
			      struct net_device *src,
			      enum macvlan_mode mode)
{
	const struct ethhdr *eth = eth_hdr(skb);
	const struct macvlan_dev *vlan;
	struct sk_buff *nskb;
	unsigned int i;
	int err;
	unsigned int hash;

	if (skb->protocol == htons(ETH_P_PAUSE))
		return;

	for (i = 0; i < MACVLAN_HASH_SIZE; i++) {
		hlist_for_each_entry_rcu(vlan, &port->vlan_hash[i], hlist) {
			if (vlan->dev == src || !(vlan->mode & mode))
				continue;

			hash = mc_hash(vlan, eth->h_dest);
			if (!test_bit(hash, vlan->mc_filter))
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
static rx_handler_result_t macvlan_handle_frame(struct sk_buff **pskb)
{
	struct macvlan_port *port;
	struct sk_buff *skb = *pskb;
	const struct ethhdr *eth = eth_hdr(skb);
	const struct macvlan_dev *vlan;
	const struct macvlan_dev *src;
	struct net_device *dev;
	unsigned int len = 0;
	int ret = NET_RX_DROP;

	port = macvlan_port_get_rcu(skb->dev);
	if (is_multicast_ether_addr(eth->h_dest)) {
		skb = ip_check_defrag(skb, IP_DEFRAG_MACVLAN);
		if (!skb)
			return RX_HANDLER_CONSUMED;
		eth = eth_hdr(skb);
		src = macvlan_hash_lookup(port, eth->h_source);
		if (!src)
			/* frame comes from an external address */
			macvlan_broadcast(skb, port, NULL,
					  MACVLAN_MODE_PRIVATE |
					  MACVLAN_MODE_VEPA    |
					  MACVLAN_MODE_PASSTHRU|
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
		else {
			/* forward to original port. */
			vlan = src;
			ret = macvlan_broadcast_one(skb, vlan, eth, 0);
			goto out;
		}

		return RX_HANDLER_PASS;
	}

	if (port->passthru)
		vlan = list_first_entry(&port->vlans, struct macvlan_dev, list);
	else
		vlan = macvlan_hash_lookup(port, eth->h_dest);
	if (vlan == NULL)
		return RX_HANDLER_PASS;

	dev = vlan->dev;
	if (unlikely(!(dev->flags & IFF_UP))) {
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}
	len = skb->len + ETH_HLEN;
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	skb->dev = dev;
	skb->pkt_type = PACKET_HOST;

	ret = vlan->receive(skb);

out:
	macvlan_count_rx(vlan, len, ret == NET_RX_SUCCESS, 0);
	return RX_HANDLER_CONSUMED;
}

static int macvlan_queue_xmit(struct sk_buff *skb, struct net_device *dev)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);
	const struct macvlan_port *port = vlan->port;
	const struct macvlan_dev *dest;
	__u8 ip_summed = skb->ip_summed;

	if (vlan->mode == MACVLAN_MODE_BRIDGE) {
		const struct ethhdr *eth = (void *)skb->data;
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		/* send to other bridge ports directly */
		if (is_multicast_ether_addr(eth->h_dest)) {
			macvlan_broadcast(skb, port, dev, MACVLAN_MODE_BRIDGE);
			goto xmit_world;
		}

		dest = macvlan_hash_lookup(port, eth->h_dest);
		if (dest && dest->mode == MACVLAN_MODE_BRIDGE) {
			/* send to lowerdev first for its network taps */
			dev_forward_skb(vlan->lowerdev, skb);

			return NET_XMIT_SUCCESS;
		}
	}

xmit_world:
	skb->ip_summed = ip_summed;
	skb->dev = vlan->lowerdev;
	return dev_queue_xmit(skb);
}

netdev_tx_t macvlan_start_xmit(struct sk_buff *skb,
			       struct net_device *dev)
{
	unsigned int len = skb->len;
	int ret;
	const struct macvlan_dev *vlan = netdev_priv(dev);

	ret = macvlan_queue_xmit(skb, dev);
	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
		struct macvlan_pcpu_stats *pcpu_stats;

		pcpu_stats = this_cpu_ptr(vlan->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->tx_packets++;
		pcpu_stats->tx_bytes += len;
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(vlan->pcpu_stats->tx_dropped);
	}
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

	if (vlan->port->passthru) {
		if (!(vlan->flags & MACVLAN_FLAG_NOPROMISC))
			dev_set_promiscuity(lowerdev, 1);
		goto hash_add;
	}

	err = -EBUSY;
	if (macvlan_addr_busy(vlan->port, dev->dev_addr))
		goto out;

	err = dev_uc_add(lowerdev, dev->dev_addr);
	if (err < 0)
		goto out;
	if (dev->flags & IFF_ALLMULTI) {
		err = dev_set_allmulti(lowerdev, 1);
		if (err < 0)
			goto del_unicast;
	}

hash_add:
	macvlan_hash_add(vlan);
	return 0;

del_unicast:
	dev_uc_del(lowerdev, dev->dev_addr);
out:
	return err;
}

static int macvlan_stop(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	dev_uc_unsync(lowerdev, dev);
	dev_mc_unsync(lowerdev, dev);

	if (vlan->port->passthru) {
		if (!(vlan->flags & MACVLAN_FLAG_NOPROMISC))
			dev_set_promiscuity(lowerdev, -1);
		goto hash_del;
	}

	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(lowerdev, -1);

	dev_uc_del(lowerdev, dev->dev_addr);

hash_del:
	macvlan_hash_del(vlan, !dev->dismantle);
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

		err = dev_uc_add(lowerdev, addr->sa_data);
		if (err)
			return err;

		dev_uc_del(lowerdev, dev->dev_addr);

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

static void macvlan_set_mac_lists(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		bitmap_fill(vlan->mc_filter, MACVLAN_MC_FILTER_SZ);
	} else {
		struct netdev_hw_addr *ha;
		DECLARE_BITMAP(filter, MACVLAN_MC_FILTER_SZ);

		bitmap_zero(filter, MACVLAN_MC_FILTER_SZ);
		netdev_for_each_mc_addr(ha, dev) {
			__set_bit(mc_hash(vlan, ha->addr), filter);
		}

		__set_bit(mc_hash(vlan, dev->broadcast), filter);

		bitmap_copy(vlan->mc_filter, filter, MACVLAN_MC_FILTER_SZ);
	}
	dev_uc_sync(vlan->lowerdev, dev);
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
	 NETIF_F_TSO_ECN | NETIF_F_TSO6 | NETIF_F_GRO | NETIF_F_RXCSUM | \
	 NETIF_F_HW_VLAN_FILTER)

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
	dev->features		|= NETIF_F_LLTX;
	dev->gso_max_size	= lowerdev->gso_max_size;
	dev->iflink		= lowerdev->ifindex;
	dev->hard_header_len	= lowerdev->hard_header_len;

	macvlan_set_lockdep_class(dev);

	vlan->pcpu_stats = alloc_percpu(struct macvlan_pcpu_stats);
	if (!vlan->pcpu_stats)
		return -ENOMEM;

	return 0;
}

static void macvlan_uninit(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct macvlan_port *port = vlan->port;

	free_percpu(vlan->pcpu_stats);

	port->count -= 1;
	if (!port->count)
		macvlan_port_destroy(port->dev);
}

static struct rtnl_link_stats64 *macvlan_dev_get_stats64(struct net_device *dev,
							 struct rtnl_link_stats64 *stats)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	if (vlan->pcpu_stats) {
		struct macvlan_pcpu_stats *p;
		u64 rx_packets, rx_bytes, rx_multicast, tx_packets, tx_bytes;
		u32 rx_errors = 0, tx_dropped = 0;
		unsigned int start;
		int i;

		for_each_possible_cpu(i) {
			p = per_cpu_ptr(vlan->pcpu_stats, i);
			do {
				start = u64_stats_fetch_begin_bh(&p->syncp);
				rx_packets	= p->rx_packets;
				rx_bytes	= p->rx_bytes;
				rx_multicast	= p->rx_multicast;
				tx_packets	= p->tx_packets;
				tx_bytes	= p->tx_bytes;
			} while (u64_stats_fetch_retry_bh(&p->syncp, start));

			stats->rx_packets	+= rx_packets;
			stats->rx_bytes		+= rx_bytes;
			stats->multicast	+= rx_multicast;
			stats->tx_packets	+= tx_packets;
			stats->tx_bytes		+= tx_bytes;
			/* rx_errors & tx_dropped are u32, updated
			 * without syncp protection.
			 */
			rx_errors	+= p->rx_errors;
			tx_dropped	+= p->tx_dropped;
		}
		stats->rx_errors	= rx_errors;
		stats->rx_dropped	= rx_errors;
		stats->tx_dropped	= tx_dropped;
	}
	return stats;
}

static int macvlan_vlan_rx_add_vid(struct net_device *dev,
				    unsigned short vid)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	return vlan_vid_add(lowerdev, vid);
}

static int macvlan_vlan_rx_kill_vid(struct net_device *dev,
				     unsigned short vid)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	vlan_vid_del(lowerdev, vid);
	return 0;
}

static int macvlan_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev,
			   const unsigned char *addr,
			   u16 flags)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	int err = -EINVAL;

	if (!vlan->port->passthru)
		return -EOPNOTSUPP;

	if (is_unicast_ether_addr(addr))
		err = dev_uc_add_excl(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_add_excl(dev, addr);

	return err;
}

static int macvlan_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev,
			   const unsigned char *addr)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	int err = -EINVAL;

	if (!vlan->port->passthru)
		return -EOPNOTSUPP;

	if (is_unicast_ether_addr(addr))
		err = dev_uc_del(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_del(dev, addr);

	return err;
}

static void macvlan_ethtool_get_drvinfo(struct net_device *dev,
					struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, "macvlan", sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, "0.1", sizeof(drvinfo->version));
}

static int macvlan_ethtool_get_settings(struct net_device *dev,
					struct ethtool_cmd *cmd)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);

	return __ethtool_get_settings(vlan->lowerdev, cmd);
}

static const struct ethtool_ops macvlan_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
	.get_settings		= macvlan_ethtool_get_settings,
	.get_drvinfo		= macvlan_ethtool_get_drvinfo,
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
	.ndo_set_rx_mode	= macvlan_set_mac_lists,
	.ndo_get_stats64	= macvlan_dev_get_stats64,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_vlan_rx_add_vid	= macvlan_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= macvlan_vlan_rx_kill_vid,
	.ndo_fdb_add		= macvlan_fdb_add,
	.ndo_fdb_del		= macvlan_fdb_del,
	.ndo_fdb_dump		= ndo_dflt_fdb_dump,
};

void macvlan_common_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->priv_flags	       &= ~(IFF_XMIT_DST_RELEASE | IFF_TX_SKB_SHARING);
	dev->netdev_ops		= &macvlan_netdev_ops;
	dev->destructor		= free_netdev;
	dev->header_ops		= &macvlan_hard_header_ops,
	dev->ethtool_ops	= &macvlan_ethtool_ops;
}
EXPORT_SYMBOL_GPL(macvlan_common_setup);

static void macvlan_setup(struct net_device *dev)
{
	macvlan_common_setup(dev);
	dev->tx_queue_len	= 0;
}

static int macvlan_port_create(struct net_device *dev)
{
	struct macvlan_port *port;
	unsigned int i;
	int err;

	if (dev->type != ARPHRD_ETHER || dev->flags & IFF_LOOPBACK)
		return -EINVAL;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (port == NULL)
		return -ENOMEM;

	port->passthru = false;
	port->dev = dev;
	INIT_LIST_HEAD(&port->vlans);
	for (i = 0; i < MACVLAN_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&port->vlan_hash[i]);

	err = netdev_rx_handler_register(dev, macvlan_handle_frame, port);
	if (err)
		kfree(port);
	else
		dev->priv_flags |= IFF_MACVLAN_PORT;
	return err;
}

static void macvlan_port_destroy(struct net_device *dev)
{
	struct macvlan_port *port = macvlan_port_get(dev);

	dev->priv_flags &= ~IFF_MACVLAN_PORT;
	netdev_rx_handler_unregister(dev);
	kfree_rcu(port, rcu);
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
		case MACVLAN_MODE_PASSTHRU:
			break;
		default:
			return -EINVAL;
		}
	}
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
		eth_hw_addr_random(dev);

	if (!macvlan_port_exists(lowerdev)) {
		err = macvlan_port_create(lowerdev);
		if (err < 0)
			return err;
	}
	port = macvlan_port_get(lowerdev);

	/* Only 1 macvlan device can be created in passthru mode */
	if (port->passthru)
		return -EINVAL;

	vlan->lowerdev = lowerdev;
	vlan->dev      = dev;
	vlan->port     = port;
	vlan->receive  = receive;
	vlan->forward  = forward;

	vlan->mode     = MACVLAN_MODE_VEPA;
	if (data && data[IFLA_MACVLAN_MODE])
		vlan->mode = nla_get_u32(data[IFLA_MACVLAN_MODE]);

	if (data && data[IFLA_MACVLAN_FLAGS])
		vlan->flags = nla_get_u16(data[IFLA_MACVLAN_FLAGS]);

	if (vlan->mode == MACVLAN_MODE_PASSTHRU) {
		if (port->count)
			return -EINVAL;
		port->passthru = true;
		memcpy(dev->dev_addr, lowerdev->dev_addr, ETH_ALEN);
	}

	err = netdev_upper_dev_link(lowerdev, dev);
	if (err)
		goto destroy_port;

	port->count += 1;
	err = register_netdevice(dev);
	if (err < 0)
		goto upper_dev_unlink;

	list_add_tail(&vlan->list, &port->vlans);
	netif_stacked_transfer_operstate(lowerdev, dev);

	return 0;

upper_dev_unlink:
	netdev_upper_dev_unlink(lowerdev, dev);
destroy_port:
	port->count -= 1;
	if (!port->count)
		macvlan_port_destroy(lowerdev);

	return err;
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

	list_del(&vlan->list);
	unregister_netdevice_queue(dev, head);
	netdev_upper_dev_unlink(vlan->lowerdev, dev);
}
EXPORT_SYMBOL_GPL(macvlan_dellink);

static int macvlan_changelink(struct net_device *dev,
		struct nlattr *tb[], struct nlattr *data[])
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	if (data && data[IFLA_MACVLAN_MODE])
		vlan->mode = nla_get_u32(data[IFLA_MACVLAN_MODE]);
	if (data && data[IFLA_MACVLAN_FLAGS]) {
		__u16 flags = nla_get_u16(data[IFLA_MACVLAN_FLAGS]);
		bool promisc = (flags ^ vlan->flags) & MACVLAN_FLAG_NOPROMISC;

		if (promisc && (flags & MACVLAN_FLAG_NOPROMISC))
			dev_set_promiscuity(vlan->lowerdev, -1);
		else if (promisc && !(flags & MACVLAN_FLAG_NOPROMISC))
			dev_set_promiscuity(vlan->lowerdev, 1);
		vlan->flags = flags;
	}
	return 0;
}

static size_t macvlan_get_size(const struct net_device *dev)
{
	return (0
		+ nla_total_size(4) /* IFLA_MACVLAN_MODE */
		+ nla_total_size(2) /* IFLA_MACVLAN_FLAGS */
		);
}

static int macvlan_fill_info(struct sk_buff *skb,
				const struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	if (nla_put_u32(skb, IFLA_MACVLAN_MODE, vlan->mode))
		goto nla_put_failure;
	if (nla_put_u16(skb, IFLA_MACVLAN_FLAGS, vlan->flags))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy macvlan_policy[IFLA_MACVLAN_MAX + 1] = {
	[IFLA_MACVLAN_MODE]  = { .type = NLA_U32 },
	[IFLA_MACVLAN_FLAGS] = { .type = NLA_U16 },
};

int macvlan_link_register(struct rtnl_link_ops *ops)
{
	/* common fields */
	ops->priv_size		= sizeof(struct macvlan_dev);
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
	.setup		= macvlan_setup,
	.newlink	= macvlan_newlink,
	.dellink	= macvlan_dellink,
};

static int macvlan_device_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct macvlan_dev *vlan, *next;
	struct macvlan_port *port;
	LIST_HEAD(list_kill);

	if (!macvlan_port_exists(dev))
		return NOTIFY_DONE;

	port = macvlan_port_get(dev);

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
		/* twiddle thumbs on netns device moves */
		if (dev->reg_state != NETREG_UNREGISTERING)
			break;

		list_for_each_entry_safe(vlan, next, &port->vlans, list)
			vlan->dev->rtnl_link_ops->dellink(vlan->dev, &list_kill);
		unregister_netdevice_many(&list_kill);
		list_del(&list_kill);
		break;
	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid underlaying device to change its type. */
		return NOTIFY_BAD;
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

	err = macvlan_link_register(&macvlan_link_ops);
	if (err < 0)
		goto err1;
	return 0;
err1:
	unregister_netdevice_notifier(&macvlan_notifier_block);
	return err;
}

static void __exit macvlan_cleanup_module(void)
{
	rtnl_link_unregister(&macvlan_link_ops);
	unregister_netdevice_notifier(&macvlan_notifier_block);
}

module_init(macvlan_init_module);
module_exit(macvlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("Driver for MAC address based VLANs");
MODULE_ALIAS_RTNL_LINK("macvlan");
