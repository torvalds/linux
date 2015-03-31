/* Copyright (c) 2014 Mahesh Bandewar <maheshb@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include "ipvlan.h"

static u32 ipvlan_jhash_secret __read_mostly;

void ipvlan_init_secret(void)
{
	net_get_random_once(&ipvlan_jhash_secret, sizeof(ipvlan_jhash_secret));
}

static void ipvlan_count_rx(const struct ipvl_dev *ipvlan,
			    unsigned int len, bool success, bool mcast)
{
	if (!ipvlan)
		return;

	if (likely(success)) {
		struct ipvl_pcpu_stats *pcptr;

		pcptr = this_cpu_ptr(ipvlan->pcpu_stats);
		u64_stats_update_begin(&pcptr->syncp);
		pcptr->rx_pkts++;
		pcptr->rx_bytes += len;
		if (mcast)
			pcptr->rx_mcast++;
		u64_stats_update_end(&pcptr->syncp);
	} else {
		this_cpu_inc(ipvlan->pcpu_stats->rx_errs);
	}
}

static u8 ipvlan_get_v6_hash(const void *iaddr)
{
	const struct in6_addr *ip6_addr = iaddr;

	return __ipv6_addr_jhash(ip6_addr, ipvlan_jhash_secret) &
	       IPVLAN_HASH_MASK;
}

static u8 ipvlan_get_v4_hash(const void *iaddr)
{
	const struct in_addr *ip4_addr = iaddr;

	return jhash_1word(ip4_addr->s_addr, ipvlan_jhash_secret) &
	       IPVLAN_HASH_MASK;
}

struct ipvl_addr *ipvlan_ht_addr_lookup(const struct ipvl_port *port,
					const void *iaddr, bool is_v6)
{
	struct ipvl_addr *addr;
	u8 hash;

	hash = is_v6 ? ipvlan_get_v6_hash(iaddr) :
	       ipvlan_get_v4_hash(iaddr);
	hlist_for_each_entry_rcu(addr, &port->hlhead[hash], hlnode) {
		if (is_v6 && addr->atype == IPVL_IPV6 &&
		    ipv6_addr_equal(&addr->ip6addr, iaddr))
			return addr;
		else if (!is_v6 && addr->atype == IPVL_IPV4 &&
			 addr->ip4addr.s_addr ==
				((struct in_addr *)iaddr)->s_addr)
			return addr;
	}
	return NULL;
}

void ipvlan_ht_addr_add(struct ipvl_dev *ipvlan, struct ipvl_addr *addr)
{
	struct ipvl_port *port = ipvlan->port;
	u8 hash;

	hash = (addr->atype == IPVL_IPV6) ?
	       ipvlan_get_v6_hash(&addr->ip6addr) :
	       ipvlan_get_v4_hash(&addr->ip4addr);
	if (hlist_unhashed(&addr->hlnode))
		hlist_add_head_rcu(&addr->hlnode, &port->hlhead[hash]);
}

void ipvlan_ht_addr_del(struct ipvl_addr *addr, bool sync)
{
	hlist_del_init_rcu(&addr->hlnode);
	if (sync)
		synchronize_rcu();
}

struct ipvl_addr *ipvlan_find_addr(const struct ipvl_dev *ipvlan,
				   const void *iaddr, bool is_v6)
{
	struct ipvl_addr *addr;

	list_for_each_entry(addr, &ipvlan->addrs, anode) {
		if ((is_v6 && addr->atype == IPVL_IPV6 &&
		    ipv6_addr_equal(&addr->ip6addr, iaddr)) ||
		    (!is_v6 && addr->atype == IPVL_IPV4 &&
		    addr->ip4addr.s_addr == ((struct in_addr *)iaddr)->s_addr))
			return addr;
	}
	return NULL;
}

bool ipvlan_addr_busy(struct ipvl_port *port, void *iaddr, bool is_v6)
{
	struct ipvl_dev *ipvlan;

	ASSERT_RTNL();

	list_for_each_entry(ipvlan, &port->ipvlans, pnode) {
		if (ipvlan_find_addr(ipvlan, iaddr, is_v6))
			return true;
	}
	return false;
}

static void *ipvlan_get_L3_hdr(struct sk_buff *skb, int *type)
{
	void *lyr3h = NULL;

	switch (skb->protocol) {
	case htons(ETH_P_ARP): {
		struct arphdr *arph;

		if (unlikely(!pskb_may_pull(skb, sizeof(*arph))))
			return NULL;

		arph = arp_hdr(skb);
		*type = IPVL_ARP;
		lyr3h = arph;
		break;
	}
	case htons(ETH_P_IP): {
		u32 pktlen;
		struct iphdr *ip4h;

		if (unlikely(!pskb_may_pull(skb, sizeof(*ip4h))))
			return NULL;

		ip4h = ip_hdr(skb);
		pktlen = ntohs(ip4h->tot_len);
		if (ip4h->ihl < 5 || ip4h->version != 4)
			return NULL;
		if (skb->len < pktlen || pktlen < (ip4h->ihl * 4))
			return NULL;

		*type = IPVL_IPV4;
		lyr3h = ip4h;
		break;
	}
	case htons(ETH_P_IPV6): {
		struct ipv6hdr *ip6h;

		if (unlikely(!pskb_may_pull(skb, sizeof(*ip6h))))
			return NULL;

		ip6h = ipv6_hdr(skb);
		if (ip6h->version != 6)
			return NULL;

		*type = IPVL_IPV6;
		lyr3h = ip6h;
		/* Only Neighbour Solicitation pkts need different treatment */
		if (ipv6_addr_any(&ip6h->saddr) &&
		    ip6h->nexthdr == NEXTHDR_ICMP) {
			*type = IPVL_ICMPV6;
			lyr3h = ip6h + 1;
		}
		break;
	}
	default:
		return NULL;
	}

	return lyr3h;
}

unsigned int ipvlan_mac_hash(const unsigned char *addr)
{
	u32 hash = jhash_1word(__get_unaligned_cpu32(addr+2),
			       ipvlan_jhash_secret);

	return hash & IPVLAN_MAC_FILTER_MASK;
}

static void ipvlan_multicast_frame(struct ipvl_port *port, struct sk_buff *skb,
				   const struct ipvl_dev *in_dev, bool local)
{
	struct ethhdr *eth = eth_hdr(skb);
	struct ipvl_dev *ipvlan;
	struct sk_buff *nskb;
	unsigned int len;
	unsigned int mac_hash;
	int ret;

	if (skb->protocol == htons(ETH_P_PAUSE))
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(ipvlan, &port->ipvlans, pnode) {
		if (local && (ipvlan == in_dev))
			continue;

		mac_hash = ipvlan_mac_hash(eth->h_dest);
		if (!test_bit(mac_hash, ipvlan->mac_filters))
			continue;

		ret = NET_RX_DROP;
		len = skb->len + ETH_HLEN;
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (!nskb)
			goto mcast_acct;

		if (ether_addr_equal(eth->h_dest, ipvlan->phy_dev->broadcast))
			nskb->pkt_type = PACKET_BROADCAST;
		else
			nskb->pkt_type = PACKET_MULTICAST;

		nskb->dev = ipvlan->dev;
		if (local)
			ret = dev_forward_skb(ipvlan->dev, nskb);
		else
			ret = netif_rx(nskb);
mcast_acct:
		ipvlan_count_rx(ipvlan, len, ret == NET_RX_SUCCESS, true);
	}
	rcu_read_unlock();

	/* Locally generated? ...Forward a copy to the main-device as
	 * well. On the RX side we'll ignore it (wont give it to any
	 * of the virtual devices.
	 */
	if (local) {
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (nskb) {
			if (ether_addr_equal(eth->h_dest, port->dev->broadcast))
				nskb->pkt_type = PACKET_BROADCAST;
			else
				nskb->pkt_type = PACKET_MULTICAST;

			dev_forward_skb(port->dev, nskb);
		}
	}
}

static int ipvlan_rcv_frame(struct ipvl_addr *addr, struct sk_buff *skb,
			    bool local)
{
	struct ipvl_dev *ipvlan = addr->master;
	struct net_device *dev = ipvlan->dev;
	unsigned int len;
	rx_handler_result_t ret = RX_HANDLER_CONSUMED;
	bool success = false;

	len = skb->len + ETH_HLEN;
	if (unlikely(!(dev->flags & IFF_UP))) {
		kfree_skb(skb);
		goto out;
	}

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		goto out;

	skb->dev = dev;
	skb->pkt_type = PACKET_HOST;

	if (local) {
		if (dev_forward_skb(ipvlan->dev, skb) == NET_RX_SUCCESS)
			success = true;
	} else {
		ret = RX_HANDLER_ANOTHER;
		success = true;
	}

out:
	ipvlan_count_rx(ipvlan, len, success, false);
	return ret;
}

static struct ipvl_addr *ipvlan_addr_lookup(struct ipvl_port *port,
					    void *lyr3h, int addr_type,
					    bool use_dest)
{
	struct ipvl_addr *addr = NULL;

	if (addr_type == IPVL_IPV6) {
		struct ipv6hdr *ip6h;
		struct in6_addr *i6addr;

		ip6h = (struct ipv6hdr *)lyr3h;
		i6addr = use_dest ? &ip6h->daddr : &ip6h->saddr;
		addr = ipvlan_ht_addr_lookup(port, i6addr, true);
	} else if (addr_type == IPVL_ICMPV6) {
		struct nd_msg *ndmh;
		struct in6_addr *i6addr;

		/* Make sure that the NeighborSolicitation ICMPv6 packets
		 * are handled to avoid DAD issue.
		 */
		ndmh = (struct nd_msg *)lyr3h;
		if (ndmh->icmph.icmp6_type == NDISC_NEIGHBOUR_SOLICITATION) {
			i6addr = &ndmh->target;
			addr = ipvlan_ht_addr_lookup(port, i6addr, true);
		}
	} else if (addr_type == IPVL_IPV4) {
		struct iphdr *ip4h;
		__be32 *i4addr;

		ip4h = (struct iphdr *)lyr3h;
		i4addr = use_dest ? &ip4h->daddr : &ip4h->saddr;
		addr = ipvlan_ht_addr_lookup(port, i4addr, false);
	} else if (addr_type == IPVL_ARP) {
		struct arphdr *arph;
		unsigned char *arp_ptr;
		__be32 dip;

		arph = (struct arphdr *)lyr3h;
		arp_ptr = (unsigned char *)(arph + 1);
		if (use_dest)
			arp_ptr += (2 * port->dev->addr_len) + 4;
		else
			arp_ptr += port->dev->addr_len;

		memcpy(&dip, arp_ptr, 4);
		addr = ipvlan_ht_addr_lookup(port, &dip, false);
	}

	return addr;
}

static int ipvlan_process_v4_outbound(struct sk_buff *skb)
{
	const struct iphdr *ip4h = ip_hdr(skb);
	struct net_device *dev = skb->dev;
	struct rtable *rt;
	int err, ret = NET_XMIT_DROP;
	struct flowi4 fl4 = {
		.flowi4_oif = dev->iflink,
		.flowi4_tos = RT_TOS(ip4h->tos),
		.flowi4_flags = FLOWI_FLAG_ANYSRC,
		.daddr = ip4h->daddr,
		.saddr = ip4h->saddr,
	};

	rt = ip_route_output_flow(dev_net(dev), &fl4, NULL);
	if (IS_ERR(rt))
		goto err;

	if (rt->rt_type != RTN_UNICAST && rt->rt_type != RTN_LOCAL) {
		ip_rt_put(rt);
		goto err;
	}
	skb_dst_drop(skb);
	skb_dst_set(skb, &rt->dst);
	err = ip_local_out(skb);
	if (unlikely(net_xmit_eval(err)))
		dev->stats.tx_errors++;
	else
		ret = NET_XMIT_SUCCESS;
	goto out;
err:
	dev->stats.tx_errors++;
	kfree_skb(skb);
out:
	return ret;
}

static int ipvlan_process_v6_outbound(struct sk_buff *skb)
{
	const struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct net_device *dev = skb->dev;
	struct dst_entry *dst;
	int err, ret = NET_XMIT_DROP;
	struct flowi6 fl6 = {
		.flowi6_iif = skb->dev->ifindex,
		.daddr = ip6h->daddr,
		.saddr = ip6h->saddr,
		.flowi6_flags = FLOWI_FLAG_ANYSRC,
		.flowlabel = ip6_flowinfo(ip6h),
		.flowi6_mark = skb->mark,
		.flowi6_proto = ip6h->nexthdr,
	};

	dst = ip6_route_output(dev_net(dev), NULL, &fl6);
	if (dst->error) {
		ret = dst->error;
		dst_release(dst);
		goto err;
	}
	skb_dst_drop(skb);
	skb_dst_set(skb, dst);
	err = ip6_local_out(skb);
	if (unlikely(net_xmit_eval(err)))
		dev->stats.tx_errors++;
	else
		ret = NET_XMIT_SUCCESS;
	goto out;
err:
	dev->stats.tx_errors++;
	kfree_skb(skb);
out:
	return ret;
}

static int ipvlan_process_outbound(struct sk_buff *skb,
				   const struct ipvl_dev *ipvlan)
{
	struct ethhdr *ethh = eth_hdr(skb);
	int ret = NET_XMIT_DROP;

	/* In this mode we dont care about multicast and broadcast traffic */
	if (is_multicast_ether_addr(ethh->h_dest)) {
		pr_warn_ratelimited("Dropped {multi|broad}cast of type= [%x]\n",
				    ntohs(skb->protocol));
		kfree_skb(skb);
		goto out;
	}

	/* The ipvlan is a pseudo-L2 device, so the packets that we receive
	 * will have L2; which need to discarded and processed further
	 * in the net-ns of the main-device.
	 */
	if (skb_mac_header_was_set(skb)) {
		skb_pull(skb, sizeof(*ethh));
		skb->mac_header = (typeof(skb->mac_header))~0U;
		skb_reset_network_header(skb);
	}

	if (skb->protocol == htons(ETH_P_IPV6))
		ret = ipvlan_process_v6_outbound(skb);
	else if (skb->protocol == htons(ETH_P_IP))
		ret = ipvlan_process_v4_outbound(skb);
	else {
		pr_warn_ratelimited("Dropped outbound packet type=%x\n",
				    ntohs(skb->protocol));
		kfree_skb(skb);
	}
out:
	return ret;
}

static int ipvlan_xmit_mode_l3(struct sk_buff *skb, struct net_device *dev)
{
	const struct ipvl_dev *ipvlan = netdev_priv(dev);
	void *lyr3h;
	struct ipvl_addr *addr;
	int addr_type;

	lyr3h = ipvlan_get_L3_hdr(skb, &addr_type);
	if (!lyr3h)
		goto out;

	addr = ipvlan_addr_lookup(ipvlan->port, lyr3h, addr_type, true);
	if (addr)
		return ipvlan_rcv_frame(addr, skb, true);

out:
	skb->dev = ipvlan->phy_dev;
	return ipvlan_process_outbound(skb, ipvlan);
}

static int ipvlan_xmit_mode_l2(struct sk_buff *skb, struct net_device *dev)
{
	const struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ethhdr *eth = eth_hdr(skb);
	struct ipvl_addr *addr;
	void *lyr3h;
	int addr_type;

	if (ether_addr_equal(eth->h_dest, eth->h_source)) {
		lyr3h = ipvlan_get_L3_hdr(skb, &addr_type);
		if (lyr3h) {
			addr = ipvlan_addr_lookup(ipvlan->port, lyr3h, addr_type, true);
			if (addr)
				return ipvlan_rcv_frame(addr, skb, true);
		}
		skb = skb_share_check(skb, GFP_ATOMIC);
		if (!skb)
			return NET_XMIT_DROP;

		/* Packet definitely does not belong to any of the
		 * virtual devices, but the dest is local. So forward
		 * the skb for the main-dev. At the RX side we just return
		 * RX_PASS for it to be processed further on the stack.
		 */
		return dev_forward_skb(ipvlan->phy_dev, skb);

	} else if (is_multicast_ether_addr(eth->h_dest)) {
		u8 ip_summed = skb->ip_summed;

		skb->ip_summed = CHECKSUM_UNNECESSARY;
		ipvlan_multicast_frame(ipvlan->port, skb, ipvlan, true);
		skb->ip_summed = ip_summed;
	}

	skb->dev = ipvlan->phy_dev;
	return dev_queue_xmit(skb);
}

int ipvlan_queue_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ipvl_dev *ipvlan = netdev_priv(dev);
	struct ipvl_port *port = ipvlan_port_get_rcu(ipvlan->phy_dev);

	if (!port)
		goto out;

	if (unlikely(!pskb_may_pull(skb, sizeof(struct ethhdr))))
		goto out;

	switch(port->mode) {
	case IPVLAN_MODE_L2:
		return ipvlan_xmit_mode_l2(skb, dev);
	case IPVLAN_MODE_L3:
		return ipvlan_xmit_mode_l3(skb, dev);
	}

	/* Should not reach here */
	WARN_ONCE(true, "ipvlan_queue_xmit() called for mode = [%hx]\n",
			  port->mode);
out:
	kfree_skb(skb);
	return NET_XMIT_DROP;
}

static bool ipvlan_external_frame(struct sk_buff *skb, struct ipvl_port *port)
{
	struct ethhdr *eth = eth_hdr(skb);
	struct ipvl_addr *addr;
	void *lyr3h;
	int addr_type;

	if (ether_addr_equal(eth->h_source, skb->dev->dev_addr)) {
		lyr3h = ipvlan_get_L3_hdr(skb, &addr_type);
		if (!lyr3h)
			return true;

		addr = ipvlan_addr_lookup(port, lyr3h, addr_type, false);
		if (addr)
			return false;
	}

	return true;
}

static rx_handler_result_t ipvlan_handle_mode_l3(struct sk_buff **pskb,
						 struct ipvl_port *port)
{
	void *lyr3h;
	int addr_type;
	struct ipvl_addr *addr;
	struct sk_buff *skb = *pskb;
	rx_handler_result_t ret = RX_HANDLER_PASS;

	lyr3h = ipvlan_get_L3_hdr(skb, &addr_type);
	if (!lyr3h)
		goto out;

	addr = ipvlan_addr_lookup(port, lyr3h, addr_type, true);
	if (addr)
		ret = ipvlan_rcv_frame(addr, skb, false);

out:
	return ret;
}

static rx_handler_result_t ipvlan_handle_mode_l2(struct sk_buff **pskb,
						 struct ipvl_port *port)
{
	struct sk_buff *skb = *pskb;
	struct ethhdr *eth = eth_hdr(skb);
	rx_handler_result_t ret = RX_HANDLER_PASS;
	void *lyr3h;
	int addr_type;

	if (is_multicast_ether_addr(eth->h_dest)) {
		if (ipvlan_external_frame(skb, port))
			ipvlan_multicast_frame(port, skb, NULL, false);
	} else {
		struct ipvl_addr *addr;

		lyr3h = ipvlan_get_L3_hdr(skb, &addr_type);
		if (!lyr3h)
			return ret;

		addr = ipvlan_addr_lookup(port, lyr3h, addr_type, true);
		if (addr)
			ret = ipvlan_rcv_frame(addr, skb, false);
	}

	return ret;
}

rx_handler_result_t ipvlan_handle_frame(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct ipvl_port *port = ipvlan_port_get_rcu(skb->dev);

	if (!port)
		return RX_HANDLER_PASS;

	switch (port->mode) {
	case IPVLAN_MODE_L2:
		return ipvlan_handle_mode_l2(pskb, port);
	case IPVLAN_MODE_L3:
		return ipvlan_handle_mode_l3(pskb, port);
	}

	/* Should not reach here */
	WARN_ONCE(true, "ipvlan_handle_frame() called for mode = [%hx]\n",
			  port->mode);
	kfree_skb(skb);
	return NET_RX_DROP;
}
