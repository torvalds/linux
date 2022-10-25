// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2007 Patrick McHardy <kaber@trash.net>
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
#include <linux/net_tstamp.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/if_link.h>
#include <linux/if_macvlan.h>
#include <linux/hash.h>
#include <linux/workqueue.h>
#include <net/rtnetlink.h>
#include <net/xfrm.h>
#include <linux/netpoll.h>
#include <linux/phy.h>

#define MACVLAN_HASH_BITS	8
#define MACVLAN_HASH_SIZE	(1<<MACVLAN_HASH_BITS)
#define MACVLAN_DEFAULT_BC_QUEUE_LEN	1000

#define MACVLAN_F_PASSTHRU	1
#define MACVLAN_F_ADDRCHANGE	2

struct macvlan_port {
	struct net_device	*dev;
	struct hlist_head	vlan_hash[MACVLAN_HASH_SIZE];
	struct list_head	vlans;
	struct sk_buff_head	bc_queue;
	struct work_struct	bc_work;
	u32			bc_queue_len_used;
	u32			flags;
	int			count;
	struct hlist_head	vlan_source_hash[MACVLAN_HASH_SIZE];
	DECLARE_BITMAP(mc_filter, MACVLAN_MC_FILTER_SZ);
	unsigned char           perm_addr[ETH_ALEN];
};

struct macvlan_source_entry {
	struct hlist_node	hlist;
	struct macvlan_dev	*vlan;
	unsigned char		addr[6+2] __aligned(sizeof(u16));
	struct rcu_head		rcu;
};

struct macvlan_skb_cb {
	const struct macvlan_dev *src;
};

#define MACVLAN_SKB_CB(__skb) ((struct macvlan_skb_cb *)&((__skb)->cb[0]))

static void macvlan_port_destroy(struct net_device *dev);
static void update_port_bc_queue_len(struct macvlan_port *port);

static inline bool macvlan_passthru(const struct macvlan_port *port)
{
	return port->flags & MACVLAN_F_PASSTHRU;
}

static inline void macvlan_set_passthru(struct macvlan_port *port)
{
	port->flags |= MACVLAN_F_PASSTHRU;
}

static inline bool macvlan_addr_change(const struct macvlan_port *port)
{
	return port->flags & MACVLAN_F_ADDRCHANGE;
}

static inline void macvlan_set_addr_change(struct macvlan_port *port)
{
	port->flags |= MACVLAN_F_ADDRCHANGE;
}

static inline void macvlan_clear_addr_change(struct macvlan_port *port)
{
	port->flags &= ~MACVLAN_F_ADDRCHANGE;
}

/* Hash Ethernet address */
static u32 macvlan_eth_hash(const unsigned char *addr)
{
	u64 value = get_unaligned((u64 *)addr);

	/* only want 6 bytes */
#ifdef __BIG_ENDIAN
	value >>= 16;
#else
	value <<= 16;
#endif
	return hash_64(value, MACVLAN_HASH_BITS);
}

static struct macvlan_port *macvlan_port_get_rcu(const struct net_device *dev)
{
	return rcu_dereference(dev->rx_handler_data);
}

static struct macvlan_port *macvlan_port_get_rtnl(const struct net_device *dev)
{
	return rtnl_dereference(dev->rx_handler_data);
}

static struct macvlan_dev *macvlan_hash_lookup(const struct macvlan_port *port,
					       const unsigned char *addr)
{
	struct macvlan_dev *vlan;
	u32 idx = macvlan_eth_hash(addr);

	hlist_for_each_entry_rcu(vlan, &port->vlan_hash[idx], hlist,
				 lockdep_rtnl_is_held()) {
		if (ether_addr_equal_64bits(vlan->dev->dev_addr, addr))
			return vlan;
	}
	return NULL;
}

static struct macvlan_source_entry *macvlan_hash_lookup_source(
	const struct macvlan_dev *vlan,
	const unsigned char *addr)
{
	struct macvlan_source_entry *entry;
	u32 idx = macvlan_eth_hash(addr);
	struct hlist_head *h = &vlan->port->vlan_source_hash[idx];

	hlist_for_each_entry_rcu(entry, h, hlist) {
		if (ether_addr_equal_64bits(entry->addr, addr) &&
		    entry->vlan == vlan)
			return entry;
	}
	return NULL;
}

static int macvlan_hash_add_source(struct macvlan_dev *vlan,
				   const unsigned char *addr)
{
	struct macvlan_port *port = vlan->port;
	struct macvlan_source_entry *entry;
	struct hlist_head *h;

	entry = macvlan_hash_lookup_source(vlan, addr);
	if (entry)
		return 0;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	ether_addr_copy(entry->addr, addr);
	entry->vlan = vlan;
	h = &port->vlan_source_hash[macvlan_eth_hash(addr)];
	hlist_add_head_rcu(&entry->hlist, h);
	vlan->macaddr_count++;

	return 0;
}

static void macvlan_hash_add(struct macvlan_dev *vlan)
{
	struct macvlan_port *port = vlan->port;
	const unsigned char *addr = vlan->dev->dev_addr;
	u32 idx = macvlan_eth_hash(addr);

	hlist_add_head_rcu(&vlan->hlist, &port->vlan_hash[idx]);
}

static void macvlan_hash_del_source(struct macvlan_source_entry *entry)
{
	hlist_del_rcu(&entry->hlist);
	kfree_rcu(entry, rcu);
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
	eth_hw_addr_set(vlan->dev, addr);
	macvlan_hash_add(vlan);
}

static bool macvlan_addr_busy(const struct macvlan_port *port,
			      const unsigned char *addr)
{
	/* Test to see if the specified address is
	 * currently in use by the underlying device or
	 * another macvlan.
	 */
	if (!macvlan_passthru(port) && !macvlan_addr_change(port) &&
	    ether_addr_equal_64bits(port->dev->dev_addr, addr))
		return true;

	if (macvlan_hash_lookup(port, addr))
		return true;

	return false;
}


static int macvlan_broadcast_one(struct sk_buff *skb,
				 const struct macvlan_dev *vlan,
				 const struct ethhdr *eth, bool local)
{
	struct net_device *dev = vlan->dev;

	if (local)
		return __dev_forward_skb(dev, skb);

	skb->dev = dev;
	if (ether_addr_equal_64bits(eth->h_dest, dev->broadcast))
		skb->pkt_type = PACKET_BROADCAST;
	else
		skb->pkt_type = PACKET_MULTICAST;

	return 0;
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

	hash_for_each_rcu(port->vlan_hash, i, vlan, hlist) {
		if (vlan->dev == src || !(vlan->mode & mode))
			continue;

		hash = mc_hash(vlan, eth->h_dest);
		if (!test_bit(hash, vlan->mc_filter))
			continue;

		err = NET_RX_DROP;
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (likely(nskb))
			err = macvlan_broadcast_one(nskb, vlan, eth,
					mode == MACVLAN_MODE_BRIDGE) ?:
			      netif_rx(nskb);
		macvlan_count_rx(vlan, skb->len + ETH_HLEN,
				 err == NET_RX_SUCCESS, true);
	}
}

static void macvlan_process_broadcast(struct work_struct *w)
{
	struct macvlan_port *port = container_of(w, struct macvlan_port,
						 bc_work);
	struct sk_buff *skb;
	struct sk_buff_head list;

	__skb_queue_head_init(&list);

	spin_lock_bh(&port->bc_queue.lock);
	skb_queue_splice_tail_init(&port->bc_queue, &list);
	spin_unlock_bh(&port->bc_queue.lock);

	while ((skb = __skb_dequeue(&list))) {
		const struct macvlan_dev *src = MACVLAN_SKB_CB(skb)->src;

		rcu_read_lock();

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
		else
			/*
			 * flood only to VEPA ports, bridge ports
			 * already saw the frame on the way out.
			 */
			macvlan_broadcast(skb, port, src->dev,
					  MACVLAN_MODE_VEPA);

		rcu_read_unlock();

		if (src)
			dev_put(src->dev);
		consume_skb(skb);

		cond_resched();
	}
}

static void macvlan_broadcast_enqueue(struct macvlan_port *port,
				      const struct macvlan_dev *src,
				      struct sk_buff *skb)
{
	struct sk_buff *nskb;
	int err = -ENOMEM;

	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb)
		goto err;

	MACVLAN_SKB_CB(nskb)->src = src;

	spin_lock(&port->bc_queue.lock);
	if (skb_queue_len(&port->bc_queue) < port->bc_queue_len_used) {
		if (src)
			dev_hold(src->dev);
		__skb_queue_tail(&port->bc_queue, nskb);
		err = 0;
	}
	spin_unlock(&port->bc_queue.lock);

	queue_work(system_unbound_wq, &port->bc_work);

	if (err)
		goto free_nskb;

	return;

free_nskb:
	kfree_skb(nskb);
err:
	dev_core_stats_rx_dropped_inc(skb->dev);
}

static void macvlan_flush_sources(struct macvlan_port *port,
				  struct macvlan_dev *vlan)
{
	struct macvlan_source_entry *entry;
	struct hlist_node *next;
	int i;

	hash_for_each_safe(port->vlan_source_hash, i, next, entry, hlist)
		if (entry->vlan == vlan)
			macvlan_hash_del_source(entry);

	vlan->macaddr_count = 0;
}

static void macvlan_forward_source_one(struct sk_buff *skb,
				       struct macvlan_dev *vlan)
{
	struct sk_buff *nskb;
	struct net_device *dev;
	int len;
	int ret;

	dev = vlan->dev;
	if (unlikely(!(dev->flags & IFF_UP)))
		return;

	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb)
		return;

	len = nskb->len + ETH_HLEN;
	nskb->dev = dev;

	if (ether_addr_equal_64bits(eth_hdr(skb)->h_dest, dev->dev_addr))
		nskb->pkt_type = PACKET_HOST;

	ret = __netif_rx(nskb);
	macvlan_count_rx(vlan, len, ret == NET_RX_SUCCESS, false);
}

static bool macvlan_forward_source(struct sk_buff *skb,
				   struct macvlan_port *port,
				   const unsigned char *addr)
{
	struct macvlan_source_entry *entry;
	u32 idx = macvlan_eth_hash(addr);
	struct hlist_head *h = &port->vlan_source_hash[idx];
	bool consume = false;

	hlist_for_each_entry_rcu(entry, h, hlist) {
		if (ether_addr_equal_64bits(entry->addr, addr)) {
			if (entry->vlan->flags & MACVLAN_FLAG_NODST)
				consume = true;
			macvlan_forward_source_one(skb, entry->vlan);
		}
	}

	return consume;
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
	int ret;
	rx_handler_result_t handle_res;

	/* Packets from dev_loopback_xmit() do not have L2 header, bail out */
	if (unlikely(skb->pkt_type == PACKET_LOOPBACK))
		return RX_HANDLER_PASS;

	port = macvlan_port_get_rcu(skb->dev);
	if (is_multicast_ether_addr(eth->h_dest)) {
		unsigned int hash;

		skb = ip_check_defrag(dev_net(skb->dev), skb, IP_DEFRAG_MACVLAN);
		if (!skb)
			return RX_HANDLER_CONSUMED;
		*pskb = skb;
		eth = eth_hdr(skb);
		if (macvlan_forward_source(skb, port, eth->h_source)) {
			kfree_skb(skb);
			return RX_HANDLER_CONSUMED;
		}
		src = macvlan_hash_lookup(port, eth->h_source);
		if (src && src->mode != MACVLAN_MODE_VEPA &&
		    src->mode != MACVLAN_MODE_BRIDGE) {
			/* forward to original port. */
			vlan = src;
			ret = macvlan_broadcast_one(skb, vlan, eth, 0) ?:
			      __netif_rx(skb);
			handle_res = RX_HANDLER_CONSUMED;
			goto out;
		}

		hash = mc_hash(NULL, eth->h_dest);
		if (test_bit(hash, port->mc_filter))
			macvlan_broadcast_enqueue(port, src, skb);

		return RX_HANDLER_PASS;
	}

	if (macvlan_forward_source(skb, port, eth->h_source)) {
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}
	if (macvlan_passthru(port))
		vlan = list_first_or_null_rcu(&port->vlans,
					      struct macvlan_dev, list);
	else
		vlan = macvlan_hash_lookup(port, eth->h_dest);
	if (!vlan || vlan->mode == MACVLAN_MODE_SOURCE)
		return RX_HANDLER_PASS;

	dev = vlan->dev;
	if (unlikely(!(dev->flags & IFF_UP))) {
		kfree_skb(skb);
		return RX_HANDLER_CONSUMED;
	}
	len = skb->len + ETH_HLEN;
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb) {
		ret = NET_RX_DROP;
		handle_res = RX_HANDLER_CONSUMED;
		goto out;
	}

	*pskb = skb;
	skb->dev = dev;
	skb->pkt_type = PACKET_HOST;

	ret = NET_RX_SUCCESS;
	handle_res = RX_HANDLER_ANOTHER;
out:
	macvlan_count_rx(vlan, len, ret == NET_RX_SUCCESS, false);
	return handle_res;
}

static int macvlan_queue_xmit(struct sk_buff *skb, struct net_device *dev)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);
	const struct macvlan_port *port = vlan->port;
	const struct macvlan_dev *dest;

	if (vlan->mode == MACVLAN_MODE_BRIDGE) {
		const struct ethhdr *eth = skb_eth_hdr(skb);

		/* send to other bridge ports directly */
		if (is_multicast_ether_addr(eth->h_dest)) {
			skb_reset_mac_header(skb);
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
	skb->dev = vlan->lowerdev;
	return dev_queue_xmit_accel(skb,
				    netdev_get_sb_channel(dev) ? dev : NULL);
}

static inline netdev_tx_t macvlan_netpoll_send_skb(struct macvlan_dev *vlan, struct sk_buff *skb)
{
#ifdef CONFIG_NET_POLL_CONTROLLER
	return netpoll_send_skb(vlan->netpoll, skb);
#else
	BUG();
	return NETDEV_TX_OK;
#endif
}

static netdev_tx_t macvlan_start_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	unsigned int len = skb->len;
	int ret;

	if (unlikely(netpoll_tx_running(dev)))
		return macvlan_netpoll_send_skb(vlan, skb);

	ret = macvlan_queue_xmit(skb, dev);

	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
		struct vlan_pcpu_stats *pcpu_stats;

		pcpu_stats = this_cpu_ptr(vlan->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		u64_stats_inc(&pcpu_stats->tx_packets);
		u64_stats_add(&pcpu_stats->tx_bytes, len);
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(vlan->pcpu_stats->tx_dropped);
	}
	return ret;
}

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
	.parse		= eth_header_parse,
	.cache		= eth_header_cache,
	.cache_update	= eth_header_cache_update,
};

static int macvlan_open(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;
	int err;

	if (macvlan_passthru(vlan->port)) {
		if (!(vlan->flags & MACVLAN_FLAG_NOPROMISC)) {
			err = dev_set_promiscuity(lowerdev, 1);
			if (err < 0)
				goto out;
		}
		goto hash_add;
	}

	err = -EADDRINUSE;
	if (macvlan_addr_busy(vlan->port, dev->dev_addr))
		goto out;

	/* Attempt to populate accel_priv which is used to offload the L2
	 * forwarding requests for unicast packets.
	 */
	if (lowerdev->features & NETIF_F_HW_L2FW_DOFFLOAD)
		vlan->accel_priv =
		      lowerdev->netdev_ops->ndo_dfwd_add_station(lowerdev, dev);

	/* If earlier attempt to offload failed, or accel_priv is not
	 * populated we must add the unicast address to the lower device.
	 */
	if (IS_ERR_OR_NULL(vlan->accel_priv)) {
		vlan->accel_priv = NULL;
		err = dev_uc_add(lowerdev, dev->dev_addr);
		if (err < 0)
			goto out;
	}

	if (dev->flags & IFF_ALLMULTI) {
		err = dev_set_allmulti(lowerdev, 1);
		if (err < 0)
			goto del_unicast;
	}

	if (dev->flags & IFF_PROMISC) {
		err = dev_set_promiscuity(lowerdev, 1);
		if (err < 0)
			goto clear_multi;
	}

hash_add:
	macvlan_hash_add(vlan);
	return 0;

clear_multi:
	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(lowerdev, -1);
del_unicast:
	if (vlan->accel_priv) {
		lowerdev->netdev_ops->ndo_dfwd_del_station(lowerdev,
							   vlan->accel_priv);
		vlan->accel_priv = NULL;
	} else {
		dev_uc_del(lowerdev, dev->dev_addr);
	}
out:
	return err;
}

static int macvlan_stop(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	if (vlan->accel_priv) {
		lowerdev->netdev_ops->ndo_dfwd_del_station(lowerdev,
							   vlan->accel_priv);
		vlan->accel_priv = NULL;
	}

	dev_uc_unsync(lowerdev, dev);
	dev_mc_unsync(lowerdev, dev);

	if (macvlan_passthru(vlan->port)) {
		if (!(vlan->flags & MACVLAN_FLAG_NOPROMISC))
			dev_set_promiscuity(lowerdev, -1);
		goto hash_del;
	}

	if (dev->flags & IFF_ALLMULTI)
		dev_set_allmulti(lowerdev, -1);

	if (dev->flags & IFF_PROMISC)
		dev_set_promiscuity(lowerdev, -1);

	dev_uc_del(lowerdev, dev->dev_addr);

hash_del:
	macvlan_hash_del(vlan, !dev->dismantle);
	return 0;
}

static int macvlan_sync_address(struct net_device *dev,
				const unsigned char *addr)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;
	struct macvlan_port *port = vlan->port;
	int err;

	if (!(dev->flags & IFF_UP)) {
		/* Just copy in the new address */
		eth_hw_addr_set(dev, addr);
	} else {
		/* Rehash and update the device filters */
		if (macvlan_addr_busy(vlan->port, addr))
			return -EADDRINUSE;

		if (!macvlan_passthru(port)) {
			err = dev_uc_add(lowerdev, addr);
			if (err)
				return err;

			dev_uc_del(lowerdev, dev->dev_addr);
		}

		macvlan_hash_change_addr(vlan, addr);
	}
	if (macvlan_passthru(port) && !macvlan_addr_change(port)) {
		/* Since addr_change isn't set, we are here due to lower
		 * device change.  Save the lower-dev address so we can
		 * restore it later.
		 */
		ether_addr_copy(vlan->port->perm_addr,
				lowerdev->dev_addr);
	}
	macvlan_clear_addr_change(port);
	return 0;
}

static int macvlan_set_mac_address(struct net_device *dev, void *p)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* If the addresses are the same, this is a no-op */
	if (ether_addr_equal(dev->dev_addr, addr->sa_data))
		return 0;

	if (vlan->mode == MACVLAN_MODE_PASSTHRU) {
		macvlan_set_addr_change(vlan->port);
		return dev_set_mac_address(vlan->lowerdev, addr, NULL);
	}

	if (macvlan_addr_busy(vlan->port, addr->sa_data))
		return -EADDRINUSE;

	return macvlan_sync_address(dev, addr->sa_data);
}

static void macvlan_change_rx_flags(struct net_device *dev, int change)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	if (dev->flags & IFF_UP) {
		if (change & IFF_ALLMULTI)
			dev_set_allmulti(lowerdev, dev->flags & IFF_ALLMULTI ? 1 : -1);
		if (change & IFF_PROMISC)
			dev_set_promiscuity(lowerdev,
					    dev->flags & IFF_PROMISC ? 1 : -1);

	}
}

static void macvlan_compute_filter(unsigned long *mc_filter,
				   struct net_device *dev,
				   struct macvlan_dev *vlan)
{
	if (dev->flags & (IFF_PROMISC | IFF_ALLMULTI)) {
		bitmap_fill(mc_filter, MACVLAN_MC_FILTER_SZ);
	} else {
		struct netdev_hw_addr *ha;
		DECLARE_BITMAP(filter, MACVLAN_MC_FILTER_SZ);

		bitmap_zero(filter, MACVLAN_MC_FILTER_SZ);
		netdev_for_each_mc_addr(ha, dev) {
			__set_bit(mc_hash(vlan, ha->addr), filter);
		}

		__set_bit(mc_hash(vlan, dev->broadcast), filter);

		bitmap_copy(mc_filter, filter, MACVLAN_MC_FILTER_SZ);
	}
}

static void macvlan_set_mac_lists(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	macvlan_compute_filter(vlan->mc_filter, dev, vlan);

	dev_uc_sync(vlan->lowerdev, dev);
	dev_mc_sync(vlan->lowerdev, dev);

	/* This is slightly inaccurate as we're including the subscription
	 * list of vlan->lowerdev too.
	 *
	 * Bug alert: This only works if everyone has the same broadcast
	 * address as lowerdev.  As soon as someone changes theirs this
	 * will break.
	 *
	 * However, this is already broken as when you change your broadcast
	 * address we don't get called.
	 *
	 * The solution is to maintain a list of broadcast addresses like
	 * we do for uc/mc, if you care.
	 */
	macvlan_compute_filter(vlan->port->mc_filter, vlan->lowerdev, NULL);
}

static int macvlan_change_mtu(struct net_device *dev, int new_mtu)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	if (vlan->lowerdev->mtu < new_mtu)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static int macvlan_eth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct net_device *real_dev = macvlan_dev_real_dev(dev);
	const struct net_device_ops *ops = real_dev->netdev_ops;
	struct ifreq ifrr;
	int err = -EOPNOTSUPP;

	strscpy(ifrr.ifr_name, real_dev->name, IFNAMSIZ);
	ifrr.ifr_ifru = ifr->ifr_ifru;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		if (!net_eq(dev_net(dev), &init_net))
			break;
		fallthrough;
	case SIOCGHWTSTAMP:
		if (netif_device_present(real_dev) && ops->ndo_eth_ioctl)
			err = ops->ndo_eth_ioctl(real_dev, &ifrr, cmd);
		break;
	}

	if (!err)
		ifr->ifr_ifru = ifrr.ifr_ifru;

	return err;
}

/*
 * macvlan network devices have devices nesting below it and are a special
 * "super class" of normal network devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key macvlan_netdev_addr_lock_key;

#define ALWAYS_ON_OFFLOADS \
	(NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_GSO_SOFTWARE | \
	 NETIF_F_GSO_ROBUST | NETIF_F_GSO_ENCAP_ALL)

#define ALWAYS_ON_FEATURES (ALWAYS_ON_OFFLOADS | NETIF_F_LLTX)

#define MACVLAN_FEATURES \
	(NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_HIGHDMA | NETIF_F_FRAGLIST | \
	 NETIF_F_GSO | NETIF_F_TSO | NETIF_F_LRO | \
	 NETIF_F_TSO_ECN | NETIF_F_TSO6 | NETIF_F_GRO | NETIF_F_RXCSUM | \
	 NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_VLAN_STAG_FILTER)

#define MACVLAN_STATE_MASK \
	((1<<__LINK_STATE_NOCARRIER) | (1<<__LINK_STATE_DORMANT))

static void macvlan_set_lockdep_class(struct net_device *dev)
{
	netdev_lockdep_set_classes(dev);
	lockdep_set_class(&dev->addr_list_lock,
			  &macvlan_netdev_addr_lock_key);
}

static int macvlan_init(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;
	struct macvlan_port *port = vlan->port;

	dev->state		= (dev->state & ~MACVLAN_STATE_MASK) |
				  (lowerdev->state & MACVLAN_STATE_MASK);
	dev->features 		= lowerdev->features & MACVLAN_FEATURES;
	dev->features		|= ALWAYS_ON_FEATURES;
	dev->hw_features	|= NETIF_F_LRO;
	dev->vlan_features	= lowerdev->vlan_features & MACVLAN_FEATURES;
	dev->vlan_features	|= ALWAYS_ON_OFFLOADS;
	dev->hw_enc_features    |= dev->features;
	netif_inherit_tso_max(dev, lowerdev);
	dev->hard_header_len	= lowerdev->hard_header_len;
	macvlan_set_lockdep_class(dev);

	vlan->pcpu_stats = netdev_alloc_pcpu_stats(struct vlan_pcpu_stats);
	if (!vlan->pcpu_stats)
		return -ENOMEM;

	port->count += 1;

	/* Get macvlan's reference to lowerdev */
	netdev_hold(lowerdev, &vlan->dev_tracker, GFP_KERNEL);

	return 0;
}

static void macvlan_uninit(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct macvlan_port *port = vlan->port;

	free_percpu(vlan->pcpu_stats);

	macvlan_flush_sources(port, vlan);
	port->count -= 1;
	if (!port->count)
		macvlan_port_destroy(port->dev);
}

static void macvlan_dev_get_stats64(struct net_device *dev,
				    struct rtnl_link_stats64 *stats)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	if (vlan->pcpu_stats) {
		struct vlan_pcpu_stats *p;
		u64 rx_packets, rx_bytes, rx_multicast, tx_packets, tx_bytes;
		u32 rx_errors = 0, tx_dropped = 0;
		unsigned int start;
		int i;

		for_each_possible_cpu(i) {
			p = per_cpu_ptr(vlan->pcpu_stats, i);
			do {
				start = u64_stats_fetch_begin_irq(&p->syncp);
				rx_packets	= u64_stats_read(&p->rx_packets);
				rx_bytes	= u64_stats_read(&p->rx_bytes);
				rx_multicast	= u64_stats_read(&p->rx_multicast);
				tx_packets	= u64_stats_read(&p->tx_packets);
				tx_bytes	= u64_stats_read(&p->tx_bytes);
			} while (u64_stats_fetch_retry_irq(&p->syncp, start));

			stats->rx_packets	+= rx_packets;
			stats->rx_bytes		+= rx_bytes;
			stats->multicast	+= rx_multicast;
			stats->tx_packets	+= tx_packets;
			stats->tx_bytes		+= tx_bytes;
			/* rx_errors & tx_dropped are u32, updated
			 * without syncp protection.
			 */
			rx_errors	+= READ_ONCE(p->rx_errors);
			tx_dropped	+= READ_ONCE(p->tx_dropped);
		}
		stats->rx_errors	= rx_errors;
		stats->rx_dropped	= rx_errors;
		stats->tx_dropped	= tx_dropped;
	}
}

static int macvlan_vlan_rx_add_vid(struct net_device *dev,
				   __be16 proto, u16 vid)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	return vlan_vid_add(lowerdev, proto, vid);
}

static int macvlan_vlan_rx_kill_vid(struct net_device *dev,
				    __be16 proto, u16 vid)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *lowerdev = vlan->lowerdev;

	vlan_vid_del(lowerdev, proto, vid);
	return 0;
}

static int macvlan_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev,
			   const unsigned char *addr, u16 vid,
			   u16 flags,
			   struct netlink_ext_ack *extack)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	int err = -EINVAL;

	/* Support unicast filter only on passthru devices.
	 * Multicast filter should be allowed on all devices.
	 */
	if (!macvlan_passthru(vlan->port) && is_unicast_ether_addr(addr))
		return -EOPNOTSUPP;

	if (flags & NLM_F_REPLACE)
		return -EOPNOTSUPP;

	if (is_unicast_ether_addr(addr))
		err = dev_uc_add_excl(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_add_excl(dev, addr);

	return err;
}

static int macvlan_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev,
			   const unsigned char *addr, u16 vid,
			   struct netlink_ext_ack *extack)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	int err = -EINVAL;

	/* Support unicast filter only on passthru devices.
	 * Multicast filter should be allowed on all devices.
	 */
	if (!macvlan_passthru(vlan->port) && is_unicast_ether_addr(addr))
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
	strscpy(drvinfo->driver, "macvlan", sizeof(drvinfo->driver));
	strscpy(drvinfo->version, "0.1", sizeof(drvinfo->version));
}

static int macvlan_ethtool_get_link_ksettings(struct net_device *dev,
					      struct ethtool_link_ksettings *cmd)
{
	const struct macvlan_dev *vlan = netdev_priv(dev);

	return __ethtool_get_link_ksettings(vlan->lowerdev, cmd);
}

static int macvlan_ethtool_get_ts_info(struct net_device *dev,
				       struct ethtool_ts_info *info)
{
	struct net_device *real_dev = macvlan_dev_real_dev(dev);
	const struct ethtool_ops *ops = real_dev->ethtool_ops;
	struct phy_device *phydev = real_dev->phydev;

	if (phy_has_tsinfo(phydev)) {
		return phy_ts_info(phydev, info);
	} else if (ops->get_ts_info) {
		return ops->get_ts_info(real_dev, info);
	} else {
		info->so_timestamping = SOF_TIMESTAMPING_RX_SOFTWARE |
			SOF_TIMESTAMPING_SOFTWARE;
		info->phc_index = -1;
	}

	return 0;
}

static netdev_features_t macvlan_fix_features(struct net_device *dev,
					      netdev_features_t features)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	netdev_features_t lowerdev_features = vlan->lowerdev->features;
	netdev_features_t mask;

	features |= NETIF_F_ALL_FOR_ALL;
	features &= (vlan->set_features | ~MACVLAN_FEATURES);
	mask = features;

	lowerdev_features &= (features | ~NETIF_F_LRO);
	features = netdev_increment_features(lowerdev_features, features, mask);
	features |= ALWAYS_ON_FEATURES;
	features &= (ALWAYS_ON_FEATURES | MACVLAN_FEATURES);

	return features;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void macvlan_dev_poll_controller(struct net_device *dev)
{
	return;
}

static int macvlan_dev_netpoll_setup(struct net_device *dev, struct netpoll_info *npinfo)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct net_device *real_dev = vlan->lowerdev;
	struct netpoll *netpoll;
	int err;

	netpoll = kzalloc(sizeof(*netpoll), GFP_KERNEL);
	err = -ENOMEM;
	if (!netpoll)
		goto out;

	err = __netpoll_setup(netpoll, real_dev);
	if (err) {
		kfree(netpoll);
		goto out;
	}

	vlan->netpoll = netpoll;

out:
	return err;
}

static void macvlan_dev_netpoll_cleanup(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct netpoll *netpoll = vlan->netpoll;

	if (!netpoll)
		return;

	vlan->netpoll = NULL;

	__netpoll_free(netpoll);
}
#endif	/* CONFIG_NET_POLL_CONTROLLER */

static int macvlan_dev_get_iflink(const struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	return vlan->lowerdev->ifindex;
}

static const struct ethtool_ops macvlan_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= macvlan_ethtool_get_link_ksettings,
	.get_drvinfo		= macvlan_ethtool_get_drvinfo,
	.get_ts_info		= macvlan_ethtool_get_ts_info,
};

static const struct net_device_ops macvlan_netdev_ops = {
	.ndo_init		= macvlan_init,
	.ndo_uninit		= macvlan_uninit,
	.ndo_open		= macvlan_open,
	.ndo_stop		= macvlan_stop,
	.ndo_start_xmit		= macvlan_start_xmit,
	.ndo_change_mtu		= macvlan_change_mtu,
	.ndo_eth_ioctl		= macvlan_eth_ioctl,
	.ndo_fix_features	= macvlan_fix_features,
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
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= macvlan_dev_poll_controller,
	.ndo_netpoll_setup	= macvlan_dev_netpoll_setup,
	.ndo_netpoll_cleanup	= macvlan_dev_netpoll_cleanup,
#endif
	.ndo_get_iflink		= macvlan_dev_get_iflink,
	.ndo_features_check	= passthru_features_check,
};

static void macvlan_dev_free(struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	/* Get rid of the macvlan's reference to lowerdev */
	netdev_put(vlan->lowerdev, &vlan->dev_tracker);
}

void macvlan_common_setup(struct net_device *dev)
{
	ether_setup(dev);

	/* ether_setup() has set dev->min_mtu to ETH_MIN_MTU. */
	dev->max_mtu		= ETH_MAX_MTU;
	dev->priv_flags	       &= ~IFF_TX_SKB_SHARING;
	netif_keep_dst(dev);
	dev->priv_flags	       |= IFF_UNICAST_FLT | IFF_CHANGE_PROTO_DOWN;
	dev->netdev_ops		= &macvlan_netdev_ops;
	dev->needs_free_netdev	= true;
	dev->priv_destructor	= macvlan_dev_free;
	dev->header_ops		= &macvlan_hard_header_ops;
	dev->ethtool_ops	= &macvlan_ethtool_ops;
}
EXPORT_SYMBOL_GPL(macvlan_common_setup);

static void macvlan_setup(struct net_device *dev)
{
	macvlan_common_setup(dev);
	dev->priv_flags |= IFF_NO_QUEUE;
}

static int macvlan_port_create(struct net_device *dev)
{
	struct macvlan_port *port;
	unsigned int i;
	int err;

	if (dev->type != ARPHRD_ETHER || dev->flags & IFF_LOOPBACK)
		return -EINVAL;

	if (netdev_is_rx_handler_busy(dev))
		return -EBUSY;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (port == NULL)
		return -ENOMEM;

	port->dev = dev;
	ether_addr_copy(port->perm_addr, dev->dev_addr);
	INIT_LIST_HEAD(&port->vlans);
	for (i = 0; i < MACVLAN_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&port->vlan_hash[i]);
	for (i = 0; i < MACVLAN_HASH_SIZE; i++)
		INIT_HLIST_HEAD(&port->vlan_source_hash[i]);

	port->bc_queue_len_used = 0;
	skb_queue_head_init(&port->bc_queue);
	INIT_WORK(&port->bc_work, macvlan_process_broadcast);

	err = netdev_rx_handler_register(dev, macvlan_handle_frame, port);
	if (err)
		kfree(port);
	else
		dev->priv_flags |= IFF_MACVLAN_PORT;
	return err;
}

static void macvlan_port_destroy(struct net_device *dev)
{
	struct macvlan_port *port = macvlan_port_get_rtnl(dev);
	struct sk_buff *skb;

	dev->priv_flags &= ~IFF_MACVLAN_PORT;
	netdev_rx_handler_unregister(dev);

	/* After this point, no packet can schedule bc_work anymore,
	 * but we need to cancel it and purge left skbs if any.
	 */
	cancel_work_sync(&port->bc_work);

	while ((skb = __skb_dequeue(&port->bc_queue))) {
		const struct macvlan_dev *src = MACVLAN_SKB_CB(skb)->src;

		if (src)
			dev_put(src->dev);

		kfree_skb(skb);
	}

	/* If the lower device address has been changed by passthru
	 * macvlan, put it back.
	 */
	if (macvlan_passthru(port) &&
	    !ether_addr_equal(port->dev->dev_addr, port->perm_addr)) {
		struct sockaddr sa;

		sa.sa_family = port->dev->type;
		memcpy(&sa.sa_data, port->perm_addr, port->dev->addr_len);
		dev_set_mac_address(port->dev, &sa, NULL);
	}

	kfree(port);
}

static int macvlan_validate(struct nlattr *tb[], struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	struct nlattr *nla, *head;
	int rem, len;

	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	if (!data)
		return 0;

	if (data[IFLA_MACVLAN_FLAGS] &&
	    nla_get_u16(data[IFLA_MACVLAN_FLAGS]) & ~(MACVLAN_FLAG_NOPROMISC |
						      MACVLAN_FLAG_NODST))
		return -EINVAL;

	if (data[IFLA_MACVLAN_MODE]) {
		switch (nla_get_u32(data[IFLA_MACVLAN_MODE])) {
		case MACVLAN_MODE_PRIVATE:
		case MACVLAN_MODE_VEPA:
		case MACVLAN_MODE_BRIDGE:
		case MACVLAN_MODE_PASSTHRU:
		case MACVLAN_MODE_SOURCE:
			break;
		default:
			return -EINVAL;
		}
	}

	if (data[IFLA_MACVLAN_MACADDR_MODE]) {
		switch (nla_get_u32(data[IFLA_MACVLAN_MACADDR_MODE])) {
		case MACVLAN_MACADDR_ADD:
		case MACVLAN_MACADDR_DEL:
		case MACVLAN_MACADDR_FLUSH:
		case MACVLAN_MACADDR_SET:
			break;
		default:
			return -EINVAL;
		}
	}

	if (data[IFLA_MACVLAN_MACADDR]) {
		if (nla_len(data[IFLA_MACVLAN_MACADDR]) != ETH_ALEN)
			return -EINVAL;

		if (!is_valid_ether_addr(nla_data(data[IFLA_MACVLAN_MACADDR])))
			return -EADDRNOTAVAIL;
	}

	if (data[IFLA_MACVLAN_MACADDR_DATA]) {
		head = nla_data(data[IFLA_MACVLAN_MACADDR_DATA]);
		len = nla_len(data[IFLA_MACVLAN_MACADDR_DATA]);

		nla_for_each_attr(nla, head, len, rem) {
			if (nla_type(nla) != IFLA_MACVLAN_MACADDR ||
			    nla_len(nla) != ETH_ALEN)
				return -EINVAL;

			if (!is_valid_ether_addr(nla_data(nla)))
				return -EADDRNOTAVAIL;
		}
	}

	if (data[IFLA_MACVLAN_MACADDR_COUNT])
		return -EINVAL;

	return 0;
}

/*
 * reconfigure list of remote source mac address
 * (only for macvlan devices in source mode)
 * Note regarding alignment: all netlink data is aligned to 4 Byte, which
 * suffices for both ether_addr_copy and ether_addr_equal_64bits usage.
 */
static int macvlan_changelink_sources(struct macvlan_dev *vlan, u32 mode,
				      struct nlattr *data[])
{
	char *addr = NULL;
	int ret, rem, len;
	struct nlattr *nla, *head;
	struct macvlan_source_entry *entry;

	if (data[IFLA_MACVLAN_MACADDR])
		addr = nla_data(data[IFLA_MACVLAN_MACADDR]);

	if (mode == MACVLAN_MACADDR_ADD) {
		if (!addr)
			return -EINVAL;

		return macvlan_hash_add_source(vlan, addr);

	} else if (mode == MACVLAN_MACADDR_DEL) {
		if (!addr)
			return -EINVAL;

		entry = macvlan_hash_lookup_source(vlan, addr);
		if (entry) {
			macvlan_hash_del_source(entry);
			vlan->macaddr_count--;
		}
	} else if (mode == MACVLAN_MACADDR_FLUSH) {
		macvlan_flush_sources(vlan->port, vlan);
	} else if (mode == MACVLAN_MACADDR_SET) {
		macvlan_flush_sources(vlan->port, vlan);

		if (addr) {
			ret = macvlan_hash_add_source(vlan, addr);
			if (ret)
				return ret;
		}

		if (!data[IFLA_MACVLAN_MACADDR_DATA])
			return 0;

		head = nla_data(data[IFLA_MACVLAN_MACADDR_DATA]);
		len = nla_len(data[IFLA_MACVLAN_MACADDR_DATA]);

		nla_for_each_attr(nla, head, len, rem) {
			addr = nla_data(nla);
			ret = macvlan_hash_add_source(vlan, addr);
			if (ret)
				return ret;
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

int macvlan_common_newlink(struct net *src_net, struct net_device *dev,
			   struct nlattr *tb[], struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct macvlan_port *port;
	struct net_device *lowerdev;
	int err;
	int macmode;
	bool create = false;

	if (!tb[IFLA_LINK])
		return -EINVAL;

	lowerdev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));
	if (lowerdev == NULL)
		return -ENODEV;

	/* When creating macvlans or macvtaps on top of other macvlans - use
	 * the real device as the lowerdev.
	 */
	if (netif_is_macvlan(lowerdev))
		lowerdev = macvlan_dev_real_dev(lowerdev);

	if (!tb[IFLA_MTU])
		dev->mtu = lowerdev->mtu;
	else if (dev->mtu > lowerdev->mtu)
		return -EINVAL;

	/* MTU range: 68 - lowerdev->max_mtu */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = lowerdev->max_mtu;

	if (!tb[IFLA_ADDRESS])
		eth_hw_addr_random(dev);

	if (!netif_is_macvlan_port(lowerdev)) {
		err = macvlan_port_create(lowerdev);
		if (err < 0)
			return err;
		create = true;
	}
	port = macvlan_port_get_rtnl(lowerdev);

	/* Only 1 macvlan device can be created in passthru mode */
	if (macvlan_passthru(port)) {
		/* The macvlan port must be not created this time,
		 * still goto destroy_macvlan_port for readability.
		 */
		err = -EINVAL;
		goto destroy_macvlan_port;
	}

	vlan->lowerdev = lowerdev;
	vlan->dev      = dev;
	vlan->port     = port;
	vlan->set_features = MACVLAN_FEATURES;

	vlan->mode     = MACVLAN_MODE_VEPA;
	if (data && data[IFLA_MACVLAN_MODE])
		vlan->mode = nla_get_u32(data[IFLA_MACVLAN_MODE]);

	if (data && data[IFLA_MACVLAN_FLAGS])
		vlan->flags = nla_get_u16(data[IFLA_MACVLAN_FLAGS]);

	if (vlan->mode == MACVLAN_MODE_PASSTHRU) {
		if (port->count) {
			err = -EINVAL;
			goto destroy_macvlan_port;
		}
		macvlan_set_passthru(port);
		eth_hw_addr_inherit(dev, lowerdev);
	}

	if (data && data[IFLA_MACVLAN_MACADDR_MODE]) {
		if (vlan->mode != MACVLAN_MODE_SOURCE) {
			err = -EINVAL;
			goto destroy_macvlan_port;
		}
		macmode = nla_get_u32(data[IFLA_MACVLAN_MACADDR_MODE]);
		err = macvlan_changelink_sources(vlan, macmode, data);
		if (err)
			goto destroy_macvlan_port;
	}

	vlan->bc_queue_len_req = MACVLAN_DEFAULT_BC_QUEUE_LEN;
	if (data && data[IFLA_MACVLAN_BC_QUEUE_LEN])
		vlan->bc_queue_len_req = nla_get_u32(data[IFLA_MACVLAN_BC_QUEUE_LEN]);

	err = register_netdevice(dev);
	if (err < 0)
		goto destroy_macvlan_port;

	dev->priv_flags |= IFF_MACVLAN;
	err = netdev_upper_dev_link(lowerdev, dev, extack);
	if (err)
		goto unregister_netdev;

	list_add_tail_rcu(&vlan->list, &port->vlans);
	update_port_bc_queue_len(vlan->port);
	netif_stacked_transfer_operstate(lowerdev, dev);
	linkwatch_fire_event(dev);

	return 0;

unregister_netdev:
	/* macvlan_uninit would free the macvlan port */
	unregister_netdevice(dev);
	return err;
destroy_macvlan_port:
	/* the macvlan port may be freed by macvlan_uninit when fail to register.
	 * so we destroy the macvlan port only when it's valid.
	 */
	if (create && macvlan_port_get_rtnl(lowerdev))
		macvlan_port_destroy(port->dev);
	return err;
}
EXPORT_SYMBOL_GPL(macvlan_common_newlink);

static int macvlan_newlink(struct net *src_net, struct net_device *dev,
			   struct nlattr *tb[], struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	return macvlan_common_newlink(src_net, dev, tb, data, extack);
}

void macvlan_dellink(struct net_device *dev, struct list_head *head)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	if (vlan->mode == MACVLAN_MODE_SOURCE)
		macvlan_flush_sources(vlan->port, vlan);
	list_del_rcu(&vlan->list);
	update_port_bc_queue_len(vlan->port);
	unregister_netdevice_queue(dev, head);
	netdev_upper_dev_unlink(vlan->lowerdev, dev);
}
EXPORT_SYMBOL_GPL(macvlan_dellink);

static int macvlan_changelink(struct net_device *dev,
			      struct nlattr *tb[], struct nlattr *data[],
			      struct netlink_ext_ack *extack)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	enum macvlan_mode mode;
	bool set_mode = false;
	enum macvlan_macaddr_mode macmode;
	int ret;

	/* Validate mode, but don't set yet: setting flags may fail. */
	if (data && data[IFLA_MACVLAN_MODE]) {
		set_mode = true;
		mode = nla_get_u32(data[IFLA_MACVLAN_MODE]);
		/* Passthrough mode can't be set or cleared dynamically */
		if ((mode == MACVLAN_MODE_PASSTHRU) !=
		    (vlan->mode == MACVLAN_MODE_PASSTHRU))
			return -EINVAL;
		if (vlan->mode == MACVLAN_MODE_SOURCE &&
		    vlan->mode != mode)
			macvlan_flush_sources(vlan->port, vlan);
	}

	if (data && data[IFLA_MACVLAN_FLAGS]) {
		__u16 flags = nla_get_u16(data[IFLA_MACVLAN_FLAGS]);
		bool promisc = (flags ^ vlan->flags) & MACVLAN_FLAG_NOPROMISC;
		if (macvlan_passthru(vlan->port) && promisc) {
			int err;

			if (flags & MACVLAN_FLAG_NOPROMISC)
				err = dev_set_promiscuity(vlan->lowerdev, -1);
			else
				err = dev_set_promiscuity(vlan->lowerdev, 1);
			if (err < 0)
				return err;
		}
		vlan->flags = flags;
	}

	if (data && data[IFLA_MACVLAN_BC_QUEUE_LEN]) {
		vlan->bc_queue_len_req = nla_get_u32(data[IFLA_MACVLAN_BC_QUEUE_LEN]);
		update_port_bc_queue_len(vlan->port);
	}

	if (set_mode)
		vlan->mode = mode;
	if (data && data[IFLA_MACVLAN_MACADDR_MODE]) {
		if (vlan->mode != MACVLAN_MODE_SOURCE)
			return -EINVAL;
		macmode = nla_get_u32(data[IFLA_MACVLAN_MACADDR_MODE]);
		ret = macvlan_changelink_sources(vlan, macmode, data);
		if (ret)
			return ret;
	}
	return 0;
}

static size_t macvlan_get_size_mac(const struct macvlan_dev *vlan)
{
	if (vlan->macaddr_count == 0)
		return 0;
	return nla_total_size(0) /* IFLA_MACVLAN_MACADDR_DATA */
		+ vlan->macaddr_count * nla_total_size(sizeof(u8) * ETH_ALEN);
}

static size_t macvlan_get_size(const struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);

	return (0
		+ nla_total_size(4) /* IFLA_MACVLAN_MODE */
		+ nla_total_size(2) /* IFLA_MACVLAN_FLAGS */
		+ nla_total_size(4) /* IFLA_MACVLAN_MACADDR_COUNT */
		+ macvlan_get_size_mac(vlan) /* IFLA_MACVLAN_MACADDR */
		+ nla_total_size(4) /* IFLA_MACVLAN_BC_QUEUE_LEN */
		+ nla_total_size(4) /* IFLA_MACVLAN_BC_QUEUE_LEN_USED */
		);
}

static int macvlan_fill_info_macaddr(struct sk_buff *skb,
				     const struct macvlan_dev *vlan,
				     const int i)
{
	struct hlist_head *h = &vlan->port->vlan_source_hash[i];
	struct macvlan_source_entry *entry;

	hlist_for_each_entry_rcu(entry, h, hlist) {
		if (entry->vlan != vlan)
			continue;
		if (nla_put(skb, IFLA_MACVLAN_MACADDR, ETH_ALEN, entry->addr))
			return 1;
	}
	return 0;
}

static int macvlan_fill_info(struct sk_buff *skb,
				const struct net_device *dev)
{
	struct macvlan_dev *vlan = netdev_priv(dev);
	struct macvlan_port *port = vlan->port;
	int i;
	struct nlattr *nest;

	if (nla_put_u32(skb, IFLA_MACVLAN_MODE, vlan->mode))
		goto nla_put_failure;
	if (nla_put_u16(skb, IFLA_MACVLAN_FLAGS, vlan->flags))
		goto nla_put_failure;
	if (nla_put_u32(skb, IFLA_MACVLAN_MACADDR_COUNT, vlan->macaddr_count))
		goto nla_put_failure;
	if (vlan->macaddr_count > 0) {
		nest = nla_nest_start_noflag(skb, IFLA_MACVLAN_MACADDR_DATA);
		if (nest == NULL)
			goto nla_put_failure;

		for (i = 0; i < MACVLAN_HASH_SIZE; i++) {
			if (macvlan_fill_info_macaddr(skb, vlan, i))
				goto nla_put_failure;
		}
		nla_nest_end(skb, nest);
	}
	if (nla_put_u32(skb, IFLA_MACVLAN_BC_QUEUE_LEN, vlan->bc_queue_len_req))
		goto nla_put_failure;
	if (nla_put_u32(skb, IFLA_MACVLAN_BC_QUEUE_LEN_USED, port->bc_queue_len_used))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy macvlan_policy[IFLA_MACVLAN_MAX + 1] = {
	[IFLA_MACVLAN_MODE]  = { .type = NLA_U32 },
	[IFLA_MACVLAN_FLAGS] = { .type = NLA_U16 },
	[IFLA_MACVLAN_MACADDR_MODE] = { .type = NLA_U32 },
	[IFLA_MACVLAN_MACADDR] = { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
	[IFLA_MACVLAN_MACADDR_DATA] = { .type = NLA_NESTED },
	[IFLA_MACVLAN_MACADDR_COUNT] = { .type = NLA_U32 },
	[IFLA_MACVLAN_BC_QUEUE_LEN] = { .type = NLA_U32 },
	[IFLA_MACVLAN_BC_QUEUE_LEN_USED] = { .type = NLA_REJECT },
};

int macvlan_link_register(struct rtnl_link_ops *ops)
{
	/* common fields */
	ops->validate		= macvlan_validate;
	ops->maxtype		= IFLA_MACVLAN_MAX;
	ops->policy		= macvlan_policy;
	ops->changelink		= macvlan_changelink;
	ops->get_size		= macvlan_get_size;
	ops->fill_info		= macvlan_fill_info;

	return rtnl_link_register(ops);
};
EXPORT_SYMBOL_GPL(macvlan_link_register);

static struct net *macvlan_get_link_net(const struct net_device *dev)
{
	return dev_net(macvlan_dev_real_dev(dev));
}

static struct rtnl_link_ops macvlan_link_ops = {
	.kind		= "macvlan",
	.setup		= macvlan_setup,
	.newlink	= macvlan_newlink,
	.dellink	= macvlan_dellink,
	.get_link_net	= macvlan_get_link_net,
	.priv_size      = sizeof(struct macvlan_dev),
};

static void update_port_bc_queue_len(struct macvlan_port *port)
{
	u32 max_bc_queue_len_req = 0;
	struct macvlan_dev *vlan;

	list_for_each_entry(vlan, &port->vlans, list) {
		if (vlan->bc_queue_len_req > max_bc_queue_len_req)
			max_bc_queue_len_req = vlan->bc_queue_len_req;
	}
	port->bc_queue_len_used = max_bc_queue_len_req;
}

static int macvlan_device_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct macvlan_dev *vlan, *next;
	struct macvlan_port *port;
	LIST_HEAD(list_kill);

	if (!netif_is_macvlan_port(dev))
		return NOTIFY_DONE;

	port = macvlan_port_get_rtnl(dev);

	switch (event) {
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_CHANGE:
		list_for_each_entry(vlan, &port->vlans, list)
			netif_stacked_transfer_operstate(vlan->lowerdev,
							 vlan->dev);
		break;
	case NETDEV_FEAT_CHANGE:
		list_for_each_entry(vlan, &port->vlans, list) {
			netif_inherit_tso_max(vlan->dev, dev);
			netdev_update_features(vlan->dev);
		}
		break;
	case NETDEV_CHANGEMTU:
		list_for_each_entry(vlan, &port->vlans, list) {
			if (vlan->dev->mtu <= dev->mtu)
				continue;
			dev_set_mtu(vlan->dev, dev->mtu);
		}
		break;
	case NETDEV_CHANGEADDR:
		if (!macvlan_passthru(port))
			return NOTIFY_DONE;

		vlan = list_first_entry_or_null(&port->vlans,
						struct macvlan_dev,
						list);

		if (vlan && macvlan_sync_address(vlan->dev, dev->dev_addr))
			return NOTIFY_BAD;

		break;
	case NETDEV_UNREGISTER:
		/* twiddle thumbs on netns device moves */
		if (dev->reg_state != NETREG_UNREGISTERING)
			break;

		list_for_each_entry_safe(vlan, next, &port->vlans, list)
			vlan->dev->rtnl_link_ops->dellink(vlan->dev, &list_kill);
		unregister_netdevice_many(&list_kill);
		break;
	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid underlying device to change its type. */
		return NOTIFY_BAD;

	case NETDEV_NOTIFY_PEERS:
	case NETDEV_BONDING_FAILOVER:
	case NETDEV_RESEND_IGMP:
		/* Propagate to all vlans */
		list_for_each_entry(vlan, &port->vlans, list)
			call_netdevice_notifiers(event, vlan->dev);
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
