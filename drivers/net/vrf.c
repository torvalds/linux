/*
 * vrf.c: device driver to encapsulate a VRF space
 *
 * Copyright (c) 2015 Cumulus Networks. All rights reserved.
 * Copyright (c) 2015 Shrijeet Mukherjee <shm@cumulusnetworks.com>
 * Copyright (c) 2015 David Ahern <dsa@cumulusnetworks.com>
 *
 * Based on dummy, team and ipvlan drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/netfilter.h>
#include <linux/rtnetlink.h>
#include <net/rtnetlink.h>
#include <linux/u64_stats_sync.h>
#include <linux/hashtable.h>

#include <linux/inetdevice.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/ip_fib.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/route.h>
#include <net/addrconf.h>
#include <net/l3mdev.h>
#include <net/fib_rules.h>

#define DRV_NAME	"vrf"
#define DRV_VERSION	"1.0"

#define FIB_RULE_PREF  1000       /* default preference for FIB rules */
static bool add_fib_rules = true;

struct net_vrf {
	struct rtable __rcu	*rth;
	struct rtable __rcu	*rth_local;
	struct rt6_info	__rcu	*rt6;
	struct rt6_info	__rcu	*rt6_local;
	u32                     tb_id;
};

struct pcpu_dstats {
	u64			tx_pkts;
	u64			tx_bytes;
	u64			tx_drps;
	u64			rx_pkts;
	u64			rx_bytes;
	u64			rx_drps;
	struct u64_stats_sync	syncp;
};

static void vrf_rx_stats(struct net_device *dev, int len)
{
	struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

	u64_stats_update_begin(&dstats->syncp);
	dstats->rx_pkts++;
	dstats->rx_bytes += len;
	u64_stats_update_end(&dstats->syncp);
}

static void vrf_tx_error(struct net_device *vrf_dev, struct sk_buff *skb)
{
	vrf_dev->stats.tx_errors++;
	kfree_skb(skb);
}

static struct rtnl_link_stats64 *vrf_get_stats64(struct net_device *dev,
						 struct rtnl_link_stats64 *stats)
{
	int i;

	for_each_possible_cpu(i) {
		const struct pcpu_dstats *dstats;
		u64 tbytes, tpkts, tdrops, rbytes, rpkts;
		unsigned int start;

		dstats = per_cpu_ptr(dev->dstats, i);
		do {
			start = u64_stats_fetch_begin_irq(&dstats->syncp);
			tbytes = dstats->tx_bytes;
			tpkts = dstats->tx_pkts;
			tdrops = dstats->tx_drps;
			rbytes = dstats->rx_bytes;
			rpkts = dstats->rx_pkts;
		} while (u64_stats_fetch_retry_irq(&dstats->syncp, start));
		stats->tx_bytes += tbytes;
		stats->tx_packets += tpkts;
		stats->tx_dropped += tdrops;
		stats->rx_bytes += rbytes;
		stats->rx_packets += rpkts;
	}
	return stats;
}

/* Local traffic destined to local address. Reinsert the packet to rx
 * path, similar to loopback handling.
 */
static int vrf_local_xmit(struct sk_buff *skb, struct net_device *dev,
			  struct dst_entry *dst)
{
	int len = skb->len;

	skb_orphan(skb);

	skb_dst_set(skb, dst);
	skb_dst_force(skb);

	/* set pkt_type to avoid skb hitting packet taps twice -
	 * once on Tx and again in Rx processing
	 */
	skb->pkt_type = PACKET_LOOPBACK;

	skb->protocol = eth_type_trans(skb, dev);

	if (likely(netif_rx(skb) == NET_RX_SUCCESS))
		vrf_rx_stats(dev, len);
	else
		this_cpu_inc(dev->dstats->rx_drps);

	return NETDEV_TX_OK;
}

#if IS_ENABLED(CONFIG_IPV6)
static int vrf_ip6_local_out(struct net *net, struct sock *sk,
			     struct sk_buff *skb)
{
	int err;

	err = nf_hook(NFPROTO_IPV6, NF_INET_LOCAL_OUT, net,
		      sk, skb, NULL, skb_dst(skb)->dev, dst_output);

	if (likely(err == 1))
		err = dst_output(net, sk, skb);

	return err;
}

static netdev_tx_t vrf_process_v6_outbound(struct sk_buff *skb,
					   struct net_device *dev)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct net *net = dev_net(skb->dev);
	struct flowi6 fl6 = {
		/* needed to match OIF rule */
		.flowi6_oif = dev->ifindex,
		.flowi6_iif = LOOPBACK_IFINDEX,
		.daddr = iph->daddr,
		.saddr = iph->saddr,
		.flowlabel = ip6_flowinfo(iph),
		.flowi6_mark = skb->mark,
		.flowi6_proto = iph->nexthdr,
		.flowi6_flags = FLOWI_FLAG_SKIP_NH_OIF,
	};
	int ret = NET_XMIT_DROP;
	struct dst_entry *dst;
	struct dst_entry *dst_null = &net->ipv6.ip6_null_entry->dst;

	dst = ip6_route_output(net, NULL, &fl6);
	if (dst == dst_null)
		goto err;

	skb_dst_drop(skb);

	/* if dst.dev is loopback or the VRF device again this is locally
	 * originated traffic destined to a local address. Short circuit
	 * to Rx path using our local dst
	 */
	if (dst->dev == net->loopback_dev || dst->dev == dev) {
		struct net_vrf *vrf = netdev_priv(dev);
		struct rt6_info *rt6_local;

		/* release looked up dst and use cached local dst */
		dst_release(dst);

		rcu_read_lock();

		rt6_local = rcu_dereference(vrf->rt6_local);
		if (unlikely(!rt6_local)) {
			rcu_read_unlock();
			goto err;
		}

		/* Ordering issue: cached local dst is created on newlink
		 * before the IPv6 initialization. Using the local dst
		 * requires rt6i_idev to be set so make sure it is.
		 */
		if (unlikely(!rt6_local->rt6i_idev)) {
			rt6_local->rt6i_idev = in6_dev_get(dev);
			if (!rt6_local->rt6i_idev) {
				rcu_read_unlock();
				goto err;
			}
		}

		dst = &rt6_local->dst;
		dst_hold(dst);

		rcu_read_unlock();

		return vrf_local_xmit(skb, dev, &rt6_local->dst);
	}

	skb_dst_set(skb, dst);

	/* strip the ethernet header added for pass through VRF device */
	__skb_pull(skb, skb_network_offset(skb));

	ret = vrf_ip6_local_out(net, skb->sk, skb);
	if (unlikely(net_xmit_eval(ret)))
		dev->stats.tx_errors++;
	else
		ret = NET_XMIT_SUCCESS;

	return ret;
err:
	vrf_tx_error(dev, skb);
	return NET_XMIT_DROP;
}
#else
static netdev_tx_t vrf_process_v6_outbound(struct sk_buff *skb,
					   struct net_device *dev)
{
	vrf_tx_error(dev, skb);
	return NET_XMIT_DROP;
}
#endif

/* based on ip_local_out; can't use it b/c the dst is switched pointing to us */
static int vrf_ip_local_out(struct net *net, struct sock *sk,
			    struct sk_buff *skb)
{
	int err;

	err = nf_hook(NFPROTO_IPV4, NF_INET_LOCAL_OUT, net, sk,
		      skb, NULL, skb_dst(skb)->dev, dst_output);
	if (likely(err == 1))
		err = dst_output(net, sk, skb);

	return err;
}

static netdev_tx_t vrf_process_v4_outbound(struct sk_buff *skb,
					   struct net_device *vrf_dev)
{
	struct iphdr *ip4h = ip_hdr(skb);
	int ret = NET_XMIT_DROP;
	struct flowi4 fl4 = {
		/* needed to match OIF rule */
		.flowi4_oif = vrf_dev->ifindex,
		.flowi4_iif = LOOPBACK_IFINDEX,
		.flowi4_tos = RT_TOS(ip4h->tos),
		.flowi4_flags = FLOWI_FLAG_ANYSRC | FLOWI_FLAG_SKIP_NH_OIF,
		.flowi4_proto = ip4h->protocol,
		.daddr = ip4h->daddr,
		.saddr = ip4h->saddr,
	};
	struct net *net = dev_net(vrf_dev);
	struct rtable *rt;

	rt = ip_route_output_flow(net, &fl4, NULL);
	if (IS_ERR(rt))
		goto err;

	if (rt->rt_type != RTN_UNICAST && rt->rt_type != RTN_LOCAL) {
		ip_rt_put(rt);
		goto err;
	}

	skb_dst_drop(skb);

	/* if dst.dev is loopback or the VRF device again this is locally
	 * originated traffic destined to a local address. Short circuit
	 * to Rx path using our local dst
	 */
	if (rt->dst.dev == net->loopback_dev || rt->dst.dev == vrf_dev) {
		struct net_vrf *vrf = netdev_priv(vrf_dev);
		struct rtable *rth_local;
		struct dst_entry *dst = NULL;

		ip_rt_put(rt);

		rcu_read_lock();

		rth_local = rcu_dereference(vrf->rth_local);
		if (likely(rth_local)) {
			dst = &rth_local->dst;
			dst_hold(dst);
		}

		rcu_read_unlock();

		if (unlikely(!dst))
			goto err;

		return vrf_local_xmit(skb, vrf_dev, dst);
	}

	skb_dst_set(skb, &rt->dst);

	/* strip the ethernet header added for pass through VRF device */
	__skb_pull(skb, skb_network_offset(skb));

	if (!ip4h->saddr) {
		ip4h->saddr = inet_select_addr(skb_dst(skb)->dev, 0,
					       RT_SCOPE_LINK);
	}

	ret = vrf_ip_local_out(dev_net(skb_dst(skb)->dev), skb->sk, skb);
	if (unlikely(net_xmit_eval(ret)))
		vrf_dev->stats.tx_errors++;
	else
		ret = NET_XMIT_SUCCESS;

out:
	return ret;
err:
	vrf_tx_error(vrf_dev, skb);
	goto out;
}

static netdev_tx_t is_ip_tx_frame(struct sk_buff *skb, struct net_device *dev)
{
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		return vrf_process_v4_outbound(skb, dev);
	case htons(ETH_P_IPV6):
		return vrf_process_v6_outbound(skb, dev);
	default:
		vrf_tx_error(dev, skb);
		return NET_XMIT_DROP;
	}
}

static netdev_tx_t vrf_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int len = skb->len;
	netdev_tx_t ret = is_ip_tx_frame(skb, dev);

	if (likely(ret == NET_XMIT_SUCCESS || ret == NET_XMIT_CN)) {
		struct pcpu_dstats *dstats = this_cpu_ptr(dev->dstats);

		u64_stats_update_begin(&dstats->syncp);
		dstats->tx_pkts++;
		dstats->tx_bytes += len;
		u64_stats_update_end(&dstats->syncp);
	} else {
		this_cpu_inc(dev->dstats->tx_drps);
	}

	return ret;
}

#if IS_ENABLED(CONFIG_IPV6)
/* modelled after ip6_finish_output2 */
static int vrf_finish_output6(struct net *net, struct sock *sk,
			      struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct net_device *dev = dst->dev;
	struct neighbour *neigh;
	struct in6_addr *nexthop;
	int ret;

	nf_reset(skb);

	skb->protocol = htons(ETH_P_IPV6);
	skb->dev = dev;

	rcu_read_lock_bh();
	nexthop = rt6_nexthop((struct rt6_info *)dst, &ipv6_hdr(skb)->daddr);
	neigh = __ipv6_neigh_lookup_noref(dst->dev, nexthop);
	if (unlikely(!neigh))
		neigh = __neigh_create(&nd_tbl, nexthop, dst->dev, false);
	if (!IS_ERR(neigh)) {
		ret = dst_neigh_output(dst, neigh, skb);
		rcu_read_unlock_bh();
		return ret;
	}
	rcu_read_unlock_bh();

	IP6_INC_STATS(dev_net(dst->dev),
		      ip6_dst_idev(dst), IPSTATS_MIB_OUTNOROUTES);
	kfree_skb(skb);
	return -EINVAL;
}

/* modelled after ip6_output */
static int vrf_output6(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	return NF_HOOK_COND(NFPROTO_IPV6, NF_INET_POST_ROUTING,
			    net, sk, skb, NULL, skb_dst(skb)->dev,
			    vrf_finish_output6,
			    !(IP6CB(skb)->flags & IP6SKB_REROUTED));
}

/* set dst on skb to send packet to us via dev_xmit path. Allows
 * packet to go through device based features such as qdisc, netfilter
 * hooks and packet sockets with skb->dev set to vrf device.
 */
static struct sk_buff *vrf_ip6_out(struct net_device *vrf_dev,
				   struct sock *sk,
				   struct sk_buff *skb)
{
	struct net_vrf *vrf = netdev_priv(vrf_dev);
	struct dst_entry *dst = NULL;
	struct rt6_info *rt6;

	/* don't divert link scope packets */
	if (rt6_need_strict(&ipv6_hdr(skb)->daddr))
		return skb;

	rcu_read_lock();

	rt6 = rcu_dereference(vrf->rt6);
	if (likely(rt6)) {
		dst = &rt6->dst;
		dst_hold(dst);
	}

	rcu_read_unlock();

	if (unlikely(!dst)) {
		vrf_tx_error(vrf_dev, skb);
		return NULL;
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);

	return skb;
}

/* holding rtnl */
static void vrf_rt6_release(struct net_device *dev, struct net_vrf *vrf)
{
	struct rt6_info *rt6 = rtnl_dereference(vrf->rt6);
	struct rt6_info *rt6_local = rtnl_dereference(vrf->rt6_local);
	struct net *net = dev_net(dev);
	struct dst_entry *dst;

	RCU_INIT_POINTER(vrf->rt6, NULL);
	RCU_INIT_POINTER(vrf->rt6_local, NULL);
	synchronize_rcu();

	/* move dev in dst's to loopback so this VRF device can be deleted
	 * - based on dst_ifdown
	 */
	if (rt6) {
		dst = &rt6->dst;
		dev_put(dst->dev);
		dst->dev = net->loopback_dev;
		dev_hold(dst->dev);
		dst_release(dst);
	}

	if (rt6_local) {
		if (rt6_local->rt6i_idev) {
			in6_dev_put(rt6_local->rt6i_idev);
			rt6_local->rt6i_idev = NULL;
		}

		dst = &rt6_local->dst;
		dev_put(dst->dev);
		dst->dev = net->loopback_dev;
		dev_hold(dst->dev);
		dst_release(dst);
	}
}

static int vrf_rt6_create(struct net_device *dev)
{
	int flags = DST_HOST | DST_NOPOLICY | DST_NOXFRM | DST_NOCACHE;
	struct net_vrf *vrf = netdev_priv(dev);
	struct net *net = dev_net(dev);
	struct fib6_table *rt6i_table;
	struct rt6_info *rt6, *rt6_local;
	int rc = -ENOMEM;

	/* IPv6 can be CONFIG enabled and then disabled runtime */
	if (!ipv6_mod_enabled())
		return 0;

	rt6i_table = fib6_new_table(net, vrf->tb_id);
	if (!rt6i_table)
		goto out;

	/* create a dst for routing packets out a VRF device */
	rt6 = ip6_dst_alloc(net, dev, flags);
	if (!rt6)
		goto out;

	dst_hold(&rt6->dst);

	rt6->rt6i_table = rt6i_table;
	rt6->dst.output	= vrf_output6;

	/* create a dst for local routing - packets sent locally
	 * to local address via the VRF device as a loopback
	 */
	rt6_local = ip6_dst_alloc(net, dev, flags);
	if (!rt6_local) {
		dst_release(&rt6->dst);
		goto out;
	}

	dst_hold(&rt6_local->dst);

	rt6_local->rt6i_idev  = in6_dev_get(dev);
	rt6_local->rt6i_flags = RTF_UP | RTF_NONEXTHOP | RTF_LOCAL;
	rt6_local->rt6i_table = rt6i_table;
	rt6_local->dst.input  = ip6_input;

	rcu_assign_pointer(vrf->rt6, rt6);
	rcu_assign_pointer(vrf->rt6_local, rt6_local);

	rc = 0;
out:
	return rc;
}
#else
static struct sk_buff *vrf_ip6_out(struct net_device *vrf_dev,
				   struct sock *sk,
				   struct sk_buff *skb)
{
	return skb;
}

static void vrf_rt6_release(struct net_device *dev, struct net_vrf *vrf)
{
}

static int vrf_rt6_create(struct net_device *dev)
{
	return 0;
}
#endif

/* modelled after ip_finish_output2 */
static int vrf_finish_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct rtable *rt = (struct rtable *)dst;
	struct net_device *dev = dst->dev;
	unsigned int hh_len = LL_RESERVED_SPACE(dev);
	struct neighbour *neigh;
	u32 nexthop;
	int ret = -EINVAL;

	nf_reset(skb);

	/* Be paranoid, rather than too clever. */
	if (unlikely(skb_headroom(skb) < hh_len && dev->header_ops)) {
		struct sk_buff *skb2;

		skb2 = skb_realloc_headroom(skb, LL_RESERVED_SPACE(dev));
		if (!skb2) {
			ret = -ENOMEM;
			goto err;
		}
		if (skb->sk)
			skb_set_owner_w(skb2, skb->sk);

		consume_skb(skb);
		skb = skb2;
	}

	rcu_read_lock_bh();

	nexthop = (__force u32)rt_nexthop(rt, ip_hdr(skb)->daddr);
	neigh = __ipv4_neigh_lookup_noref(dev, nexthop);
	if (unlikely(!neigh))
		neigh = __neigh_create(&arp_tbl, &nexthop, dev, false);
	if (!IS_ERR(neigh))
		ret = dst_neigh_output(dst, neigh, skb);

	rcu_read_unlock_bh();
err:
	if (unlikely(ret < 0))
		vrf_tx_error(skb->dev, skb);
	return ret;
}

static int vrf_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct net_device *dev = skb_dst(skb)->dev;

	IP_UPD_PO_STATS(net, IPSTATS_MIB_OUT, skb->len);

	skb->dev = dev;
	skb->protocol = htons(ETH_P_IP);

	return NF_HOOK_COND(NFPROTO_IPV4, NF_INET_POST_ROUTING,
			    net, sk, skb, NULL, dev,
			    vrf_finish_output,
			    !(IPCB(skb)->flags & IPSKB_REROUTED));
}

/* set dst on skb to send packet to us via dev_xmit path. Allows
 * packet to go through device based features such as qdisc, netfilter
 * hooks and packet sockets with skb->dev set to vrf device.
 */
static struct sk_buff *vrf_ip_out(struct net_device *vrf_dev,
				  struct sock *sk,
				  struct sk_buff *skb)
{
	struct net_vrf *vrf = netdev_priv(vrf_dev);
	struct dst_entry *dst = NULL;
	struct rtable *rth;

	rcu_read_lock();

	rth = rcu_dereference(vrf->rth);
	if (likely(rth)) {
		dst = &rth->dst;
		dst_hold(dst);
	}

	rcu_read_unlock();

	if (unlikely(!dst)) {
		vrf_tx_error(vrf_dev, skb);
		return NULL;
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);

	return skb;
}

/* called with rcu lock held */
static struct sk_buff *vrf_l3_out(struct net_device *vrf_dev,
				  struct sock *sk,
				  struct sk_buff *skb,
				  u16 proto)
{
	switch (proto) {
	case AF_INET:
		return vrf_ip_out(vrf_dev, sk, skb);
	case AF_INET6:
		return vrf_ip6_out(vrf_dev, sk, skb);
	}

	return skb;
}

/* holding rtnl */
static void vrf_rtable_release(struct net_device *dev, struct net_vrf *vrf)
{
	struct rtable *rth = rtnl_dereference(vrf->rth);
	struct rtable *rth_local = rtnl_dereference(vrf->rth_local);
	struct net *net = dev_net(dev);
	struct dst_entry *dst;

	RCU_INIT_POINTER(vrf->rth, NULL);
	RCU_INIT_POINTER(vrf->rth_local, NULL);
	synchronize_rcu();

	/* move dev in dst's to loopback so this VRF device can be deleted
	 * - based on dst_ifdown
	 */
	if (rth) {
		dst = &rth->dst;
		dev_put(dst->dev);
		dst->dev = net->loopback_dev;
		dev_hold(dst->dev);
		dst_release(dst);
	}

	if (rth_local) {
		dst = &rth_local->dst;
		dev_put(dst->dev);
		dst->dev = net->loopback_dev;
		dev_hold(dst->dev);
		dst_release(dst);
	}
}

static int vrf_rtable_create(struct net_device *dev)
{
	struct net_vrf *vrf = netdev_priv(dev);
	struct rtable *rth, *rth_local;

	if (!fib_new_table(dev_net(dev), vrf->tb_id))
		return -ENOMEM;

	/* create a dst for routing packets out through a VRF device */
	rth = rt_dst_alloc(dev, 0, RTN_UNICAST, 1, 1, 0);
	if (!rth)
		return -ENOMEM;

	/* create a dst for local ingress routing - packets sent locally
	 * to local address via the VRF device as a loopback
	 */
	rth_local = rt_dst_alloc(dev, RTCF_LOCAL, RTN_LOCAL, 1, 1, 0);
	if (!rth_local) {
		dst_release(&rth->dst);
		return -ENOMEM;
	}

	rth->dst.output	= vrf_output;
	rth->rt_table_id = vrf->tb_id;

	rth_local->rt_table_id = vrf->tb_id;

	rcu_assign_pointer(vrf->rth, rth);
	rcu_assign_pointer(vrf->rth_local, rth_local);

	return 0;
}

/**************************** device handling ********************/

/* cycle interface to flush neighbor cache and move routes across tables */
static void cycle_netdev(struct net_device *dev)
{
	unsigned int flags = dev->flags;
	int ret;

	if (!netif_running(dev))
		return;

	ret = dev_change_flags(dev, flags & ~IFF_UP);
	if (ret >= 0)
		ret = dev_change_flags(dev, flags);

	if (ret < 0) {
		netdev_err(dev,
			   "Failed to cycle device %s; route tables might be wrong!\n",
			   dev->name);
	}
}

static int do_vrf_add_slave(struct net_device *dev, struct net_device *port_dev)
{
	int ret;

	ret = netdev_master_upper_dev_link(port_dev, dev, NULL, NULL);
	if (ret < 0)
		return ret;

	port_dev->priv_flags |= IFF_L3MDEV_SLAVE;
	cycle_netdev(port_dev);

	return 0;
}

static int vrf_add_slave(struct net_device *dev, struct net_device *port_dev)
{
	if (netif_is_l3_master(port_dev) || netif_is_l3_slave(port_dev))
		return -EINVAL;

	return do_vrf_add_slave(dev, port_dev);
}

/* inverse of do_vrf_add_slave */
static int do_vrf_del_slave(struct net_device *dev, struct net_device *port_dev)
{
	netdev_upper_dev_unlink(port_dev, dev);
	port_dev->priv_flags &= ~IFF_L3MDEV_SLAVE;

	cycle_netdev(port_dev);

	return 0;
}

static int vrf_del_slave(struct net_device *dev, struct net_device *port_dev)
{
	return do_vrf_del_slave(dev, port_dev);
}

static void vrf_dev_uninit(struct net_device *dev)
{
	struct net_vrf *vrf = netdev_priv(dev);
	struct net_device *port_dev;
	struct list_head *iter;

	vrf_rtable_release(dev, vrf);
	vrf_rt6_release(dev, vrf);

	netdev_for_each_lower_dev(dev, port_dev, iter)
		vrf_del_slave(dev, port_dev);

	free_percpu(dev->dstats);
	dev->dstats = NULL;
}

static int vrf_dev_init(struct net_device *dev)
{
	struct net_vrf *vrf = netdev_priv(dev);

	dev->dstats = netdev_alloc_pcpu_stats(struct pcpu_dstats);
	if (!dev->dstats)
		goto out_nomem;

	/* create the default dst which points back to us */
	if (vrf_rtable_create(dev) != 0)
		goto out_stats;

	if (vrf_rt6_create(dev) != 0)
		goto out_rth;

	dev->flags = IFF_MASTER | IFF_NOARP;

	/* MTU is irrelevant for VRF device; set to 64k similar to lo */
	dev->mtu = 64 * 1024;

	/* similarly, oper state is irrelevant; set to up to avoid confusion */
	dev->operstate = IF_OPER_UP;
	netdev_lockdep_set_classes(dev);
	return 0;

out_rth:
	vrf_rtable_release(dev, vrf);
out_stats:
	free_percpu(dev->dstats);
	dev->dstats = NULL;
out_nomem:
	return -ENOMEM;
}

static const struct net_device_ops vrf_netdev_ops = {
	.ndo_init		= vrf_dev_init,
	.ndo_uninit		= vrf_dev_uninit,
	.ndo_start_xmit		= vrf_xmit,
	.ndo_get_stats64	= vrf_get_stats64,
	.ndo_add_slave		= vrf_add_slave,
	.ndo_del_slave		= vrf_del_slave,
};

static u32 vrf_fib_table(const struct net_device *dev)
{
	struct net_vrf *vrf = netdev_priv(dev);

	return vrf->tb_id;
}

static int vrf_rcv_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}

static struct sk_buff *vrf_rcv_nfhook(u8 pf, unsigned int hook,
				      struct sk_buff *skb,
				      struct net_device *dev)
{
	struct net *net = dev_net(dev);

	if (nf_hook(pf, hook, net, NULL, skb, dev, NULL, vrf_rcv_finish) != 1)
		skb = NULL;    /* kfree_skb(skb) handled by nf code */

	return skb;
}

#if IS_ENABLED(CONFIG_IPV6)
/* neighbor handling is done with actual device; do not want
 * to flip skb->dev for those ndisc packets. This really fails
 * for multiple next protocols (e.g., NEXTHDR_HOP). But it is
 * a start.
 */
static bool ipv6_ndisc_frame(const struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	bool rc = false;

	if (iph->nexthdr == NEXTHDR_ICMP) {
		const struct icmp6hdr *icmph;
		struct icmp6hdr _icmph;

		icmph = skb_header_pointer(skb, sizeof(*iph),
					   sizeof(_icmph), &_icmph);
		if (!icmph)
			goto out;

		switch (icmph->icmp6_type) {
		case NDISC_ROUTER_SOLICITATION:
		case NDISC_ROUTER_ADVERTISEMENT:
		case NDISC_NEIGHBOUR_SOLICITATION:
		case NDISC_NEIGHBOUR_ADVERTISEMENT:
		case NDISC_REDIRECT:
			rc = true;
			break;
		}
	}

out:
	return rc;
}

static struct rt6_info *vrf_ip6_route_lookup(struct net *net,
					     const struct net_device *dev,
					     struct flowi6 *fl6,
					     int ifindex,
					     int flags)
{
	struct net_vrf *vrf = netdev_priv(dev);
	struct fib6_table *table = NULL;
	struct rt6_info *rt6;

	rcu_read_lock();

	/* fib6_table does not have a refcnt and can not be freed */
	rt6 = rcu_dereference(vrf->rt6);
	if (likely(rt6))
		table = rt6->rt6i_table;

	rcu_read_unlock();

	if (!table)
		return NULL;

	return ip6_pol_route(net, table, ifindex, fl6, flags);
}

static void vrf_ip6_input_dst(struct sk_buff *skb, struct net_device *vrf_dev,
			      int ifindex)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct flowi6 fl6 = {
		.daddr          = iph->daddr,
		.saddr          = iph->saddr,
		.flowlabel      = ip6_flowinfo(iph),
		.flowi6_mark    = skb->mark,
		.flowi6_proto   = iph->nexthdr,
		.flowi6_iif     = ifindex,
	};
	struct net *net = dev_net(vrf_dev);
	struct rt6_info *rt6;

	rt6 = vrf_ip6_route_lookup(net, vrf_dev, &fl6, ifindex,
				   RT6_LOOKUP_F_HAS_SADDR | RT6_LOOKUP_F_IFACE);
	if (unlikely(!rt6))
		return;

	if (unlikely(&rt6->dst == &net->ipv6.ip6_null_entry->dst))
		return;

	skb_dst_set(skb, &rt6->dst);
}

static struct sk_buff *vrf_ip6_rcv(struct net_device *vrf_dev,
				   struct sk_buff *skb)
{
	int orig_iif = skb->skb_iif;
	bool need_strict;

	/* loopback traffic; do not push through packet taps again.
	 * Reset pkt_type for upper layers to process skb
	 */
	if (skb->pkt_type == PACKET_LOOPBACK) {
		skb->dev = vrf_dev;
		skb->skb_iif = vrf_dev->ifindex;
		IP6CB(skb)->flags |= IP6SKB_L3SLAVE;
		skb->pkt_type = PACKET_HOST;
		goto out;
	}

	/* if packet is NDISC or addressed to multicast or link-local
	 * then keep the ingress interface
	 */
	need_strict = rt6_need_strict(&ipv6_hdr(skb)->daddr);
	if (!ipv6_ndisc_frame(skb) && !need_strict) {
		vrf_rx_stats(vrf_dev, skb->len);
		skb->dev = vrf_dev;
		skb->skb_iif = vrf_dev->ifindex;

		skb_push(skb, skb->mac_len);
		dev_queue_xmit_nit(skb, vrf_dev);
		skb_pull(skb, skb->mac_len);

		IP6CB(skb)->flags |= IP6SKB_L3SLAVE;
	}

	if (need_strict)
		vrf_ip6_input_dst(skb, vrf_dev, orig_iif);

	skb = vrf_rcv_nfhook(NFPROTO_IPV6, NF_INET_PRE_ROUTING, skb, vrf_dev);
out:
	return skb;
}

#else
static struct sk_buff *vrf_ip6_rcv(struct net_device *vrf_dev,
				   struct sk_buff *skb)
{
	return skb;
}
#endif

static struct sk_buff *vrf_ip_rcv(struct net_device *vrf_dev,
				  struct sk_buff *skb)
{
	skb->dev = vrf_dev;
	skb->skb_iif = vrf_dev->ifindex;
	IPCB(skb)->flags |= IPSKB_L3SLAVE;

	/* loopback traffic; do not push through packet taps again.
	 * Reset pkt_type for upper layers to process skb
	 */
	if (skb->pkt_type == PACKET_LOOPBACK) {
		skb->pkt_type = PACKET_HOST;
		goto out;
	}

	vrf_rx_stats(vrf_dev, skb->len);

	skb_push(skb, skb->mac_len);
	dev_queue_xmit_nit(skb, vrf_dev);
	skb_pull(skb, skb->mac_len);

	skb = vrf_rcv_nfhook(NFPROTO_IPV4, NF_INET_PRE_ROUTING, skb, vrf_dev);
out:
	return skb;
}

/* called with rcu lock held */
static struct sk_buff *vrf_l3_rcv(struct net_device *vrf_dev,
				  struct sk_buff *skb,
				  u16 proto)
{
	switch (proto) {
	case AF_INET:
		return vrf_ip_rcv(vrf_dev, skb);
	case AF_INET6:
		return vrf_ip6_rcv(vrf_dev, skb);
	}

	return skb;
}

#if IS_ENABLED(CONFIG_IPV6)
/* send to link-local or multicast address via interface enslaved to
 * VRF device. Force lookup to VRF table without changing flow struct
 */
static struct dst_entry *vrf_link_scope_lookup(const struct net_device *dev,
					      struct flowi6 *fl6)
{
	struct net *net = dev_net(dev);
	int flags = RT6_LOOKUP_F_IFACE;
	struct dst_entry *dst = NULL;
	struct rt6_info *rt;

	/* VRF device does not have a link-local address and
	 * sending packets to link-local or mcast addresses over
	 * a VRF device does not make sense
	 */
	if (fl6->flowi6_oif == dev->ifindex) {
		dst = &net->ipv6.ip6_null_entry->dst;
		dst_hold(dst);
		return dst;
	}

	if (!ipv6_addr_any(&fl6->saddr))
		flags |= RT6_LOOKUP_F_HAS_SADDR;

	rt = vrf_ip6_route_lookup(net, dev, fl6, fl6->flowi6_oif, flags);
	if (rt)
		dst = &rt->dst;

	return dst;
}
#endif

static const struct l3mdev_ops vrf_l3mdev_ops = {
	.l3mdev_fib_table	= vrf_fib_table,
	.l3mdev_l3_rcv		= vrf_l3_rcv,
	.l3mdev_l3_out		= vrf_l3_out,
#if IS_ENABLED(CONFIG_IPV6)
	.l3mdev_link_scope_lookup = vrf_link_scope_lookup,
#endif
};

static void vrf_get_drvinfo(struct net_device *dev,
			    struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
}

static const struct ethtool_ops vrf_ethtool_ops = {
	.get_drvinfo	= vrf_get_drvinfo,
};

static inline size_t vrf_fib_rule_nl_size(void)
{
	size_t sz;

	sz  = NLMSG_ALIGN(sizeof(struct fib_rule_hdr));
	sz += nla_total_size(sizeof(u8));	/* FRA_L3MDEV */
	sz += nla_total_size(sizeof(u32));	/* FRA_PRIORITY */

	return sz;
}

static int vrf_fib_rule(const struct net_device *dev, __u8 family, bool add_it)
{
	struct fib_rule_hdr *frh;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	int err;

	if (family == AF_INET6 && !ipv6_mod_enabled())
		return 0;

	skb = nlmsg_new(vrf_fib_rule_nl_size(), GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	nlh = nlmsg_put(skb, 0, 0, 0, sizeof(*frh), 0);
	if (!nlh)
		goto nla_put_failure;

	/* rule only needs to appear once */
	nlh->nlmsg_flags |= NLM_F_EXCL;

	frh = nlmsg_data(nlh);
	memset(frh, 0, sizeof(*frh));
	frh->family = family;
	frh->action = FR_ACT_TO_TBL;

	if (nla_put_u32(skb, FRA_L3MDEV, 1))
		goto nla_put_failure;

	if (nla_put_u32(skb, FRA_PRIORITY, FIB_RULE_PREF))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);

	/* fib_nl_{new,del}rule handling looks for net from skb->sk */
	skb->sk = dev_net(dev)->rtnl;
	if (add_it) {
		err = fib_nl_newrule(skb, nlh);
		if (err == -EEXIST)
			err = 0;
	} else {
		err = fib_nl_delrule(skb, nlh);
		if (err == -ENOENT)
			err = 0;
	}
	nlmsg_free(skb);

	return err;

nla_put_failure:
	nlmsg_free(skb);

	return -EMSGSIZE;
}

static int vrf_add_fib_rules(const struct net_device *dev)
{
	int err;

	err = vrf_fib_rule(dev, AF_INET,  true);
	if (err < 0)
		goto out_err;

	err = vrf_fib_rule(dev, AF_INET6, true);
	if (err < 0)
		goto ipv6_err;

	return 0;

ipv6_err:
	vrf_fib_rule(dev, AF_INET,  false);

out_err:
	netdev_err(dev, "Failed to add FIB rules.\n");
	return err;
}

static void vrf_setup(struct net_device *dev)
{
	ether_setup(dev);

	/* Initialize the device structure. */
	dev->netdev_ops = &vrf_netdev_ops;
	dev->l3mdev_ops = &vrf_l3mdev_ops;
	dev->ethtool_ops = &vrf_ethtool_ops;
	dev->destructor = free_netdev;

	/* Fill in device structure with ethernet-generic values. */
	eth_hw_addr_random(dev);

	/* don't acquire vrf device's netif_tx_lock when transmitting */
	dev->features |= NETIF_F_LLTX;

	/* don't allow vrf devices to change network namespaces. */
	dev->features |= NETIF_F_NETNS_LOCAL;

	/* does not make sense for a VLAN to be added to a vrf device */
	dev->features   |= NETIF_F_VLAN_CHALLENGED;

	/* enable offload features */
	dev->features   |= NETIF_F_GSO_SOFTWARE;
	dev->features   |= NETIF_F_RXCSUM | NETIF_F_HW_CSUM;
	dev->features   |= NETIF_F_SG | NETIF_F_FRAGLIST | NETIF_F_HIGHDMA;

	dev->hw_features = dev->features;
	dev->hw_enc_features = dev->features;

	/* default to no qdisc; user can add if desired */
	dev->priv_flags |= IFF_NO_QUEUE;
}

static int vrf_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;
		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}
	return 0;
}

static void vrf_dellink(struct net_device *dev, struct list_head *head)
{
	unregister_netdevice_queue(dev, head);
}

static int vrf_newlink(struct net *src_net, struct net_device *dev,
		       struct nlattr *tb[], struct nlattr *data[])
{
	struct net_vrf *vrf = netdev_priv(dev);
	int err;

	if (!data || !data[IFLA_VRF_TABLE])
		return -EINVAL;

	vrf->tb_id = nla_get_u32(data[IFLA_VRF_TABLE]);
	if (vrf->tb_id == RT_TABLE_UNSPEC)
		return -EINVAL;

	dev->priv_flags |= IFF_L3MDEV_MASTER;

	err = register_netdevice(dev);
	if (err)
		goto out;

	if (add_fib_rules) {
		err = vrf_add_fib_rules(dev);
		if (err) {
			unregister_netdevice(dev);
			goto out;
		}
		add_fib_rules = false;
	}

out:
	return err;
}

static size_t vrf_nl_getsize(const struct net_device *dev)
{
	return nla_total_size(sizeof(u32));  /* IFLA_VRF_TABLE */
}

static int vrf_fillinfo(struct sk_buff *skb,
			const struct net_device *dev)
{
	struct net_vrf *vrf = netdev_priv(dev);

	return nla_put_u32(skb, IFLA_VRF_TABLE, vrf->tb_id);
}

static size_t vrf_get_slave_size(const struct net_device *bond_dev,
				 const struct net_device *slave_dev)
{
	return nla_total_size(sizeof(u32));  /* IFLA_VRF_PORT_TABLE */
}

static int vrf_fill_slave_info(struct sk_buff *skb,
			       const struct net_device *vrf_dev,
			       const struct net_device *slave_dev)
{
	struct net_vrf *vrf = netdev_priv(vrf_dev);

	if (nla_put_u32(skb, IFLA_VRF_PORT_TABLE, vrf->tb_id))
		return -EMSGSIZE;

	return 0;
}

static const struct nla_policy vrf_nl_policy[IFLA_VRF_MAX + 1] = {
	[IFLA_VRF_TABLE] = { .type = NLA_U32 },
};

static struct rtnl_link_ops vrf_link_ops __read_mostly = {
	.kind		= DRV_NAME,
	.priv_size	= sizeof(struct net_vrf),

	.get_size	= vrf_nl_getsize,
	.policy		= vrf_nl_policy,
	.validate	= vrf_validate,
	.fill_info	= vrf_fillinfo,

	.get_slave_size  = vrf_get_slave_size,
	.fill_slave_info = vrf_fill_slave_info,

	.newlink	= vrf_newlink,
	.dellink	= vrf_dellink,
	.setup		= vrf_setup,
	.maxtype	= IFLA_VRF_MAX,
};

static int vrf_device_event(struct notifier_block *unused,
			    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	/* only care about unregister events to drop slave references */
	if (event == NETDEV_UNREGISTER) {
		struct net_device *vrf_dev;

		if (!netif_is_l3_slave(dev))
			goto out;

		vrf_dev = netdev_master_upper_dev_get(dev);
		vrf_del_slave(vrf_dev, dev);
	}
out:
	return NOTIFY_DONE;
}

static struct notifier_block vrf_notifier_block __read_mostly = {
	.notifier_call = vrf_device_event,
};

static int __init vrf_init_module(void)
{
	int rc;

	register_netdevice_notifier(&vrf_notifier_block);

	rc = rtnl_link_register(&vrf_link_ops);
	if (rc < 0)
		goto error;

	return 0;

error:
	unregister_netdevice_notifier(&vrf_notifier_block);
	return rc;
}

module_init(vrf_init_module);
MODULE_AUTHOR("Shrijeet Mukherjee, David Ahern");
MODULE_DESCRIPTION("Device driver to instantiate VRF domains");
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK(DRV_NAME);
MODULE_VERSION(DRV_VERSION);
