/* GTP according to GSM TS 09.60 / 3GPP TS 29.060
 *
 * (C) 2012-2014 by sysmocom - s.f.m.c. GmbH
 * (C) 2016 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * Author: Harald Welte <hwelte@sysmocom.de>
 *	   Pablo Neira Ayuso <pablo@netfilter.org>
 *	   Andreas Schultz <aschultz@travelping.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/rculist.h>
#include <linux/jhash.h>
#include <linux/if_tunnel.h>
#include <linux/net.h>
#include <linux/file.h>
#include <linux/gtp.h>

#include <net/net_namespace.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>
#include <net/icmp.h>
#include <net/xfrm.h>
#include <net/genetlink.h>
#include <net/netns/generic.h>
#include <net/gtp.h>

/* An active session for the subscriber. */
struct pdp_ctx {
	struct hlist_node	hlist_tid;
	struct hlist_node	hlist_addr;

	union {
		u64		tid;
		struct {
			u64	tid;
			u16	flow;
		} v0;
		struct {
			u32	i_tei;
			u32	o_tei;
		} v1;
	} u;
	u8			gtp_version;
	u16			af;

	struct in_addr		ms_addr_ip4;
	struct in_addr		sgsn_addr_ip4;

	atomic_t		tx_seq;
	struct rcu_head		rcu_head;
};

/* One instance of the GTP device. */
struct gtp_dev {
	struct list_head	list;

	struct socket		*sock0;
	struct socket		*sock1u;

	struct net		*net;
	struct net_device	*dev;

	unsigned int		hash_size;
	struct hlist_head	*tid_hash;
	struct hlist_head	*addr_hash;
};

static int gtp_net_id __read_mostly;

struct gtp_net {
	struct list_head gtp_dev_list;
};

static u32 gtp_h_initval;

static inline u32 gtp0_hashfn(u64 tid)
{
	u32 *tid32 = (u32 *) &tid;
	return jhash_2words(tid32[0], tid32[1], gtp_h_initval);
}

static inline u32 gtp1u_hashfn(u32 tid)
{
	return jhash_1word(tid, gtp_h_initval);
}

static inline u32 ipv4_hashfn(__be32 ip)
{
	return jhash_1word((__force u32)ip, gtp_h_initval);
}

/* Resolve a PDP context structure based on the 64bit TID. */
static struct pdp_ctx *gtp0_pdp_find(struct gtp_dev *gtp, u64 tid)
{
	struct hlist_head *head;
	struct pdp_ctx *pdp;

	head = &gtp->tid_hash[gtp0_hashfn(tid) % gtp->hash_size];

	hlist_for_each_entry_rcu(pdp, head, hlist_tid) {
		if (pdp->gtp_version == GTP_V0 &&
		    pdp->u.v0.tid == tid)
			return pdp;
	}
	return NULL;
}

/* Resolve a PDP context structure based on the 32bit TEI. */
static struct pdp_ctx *gtp1_pdp_find(struct gtp_dev *gtp, u32 tid)
{
	struct hlist_head *head;
	struct pdp_ctx *pdp;

	head = &gtp->tid_hash[gtp1u_hashfn(tid) % gtp->hash_size];

	hlist_for_each_entry_rcu(pdp, head, hlist_tid) {
		if (pdp->gtp_version == GTP_V1 &&
		    pdp->u.v1.i_tei == tid)
			return pdp;
	}
	return NULL;
}

/* Resolve a PDP context based on IPv4 address of MS. */
static struct pdp_ctx *ipv4_pdp_find(struct gtp_dev *gtp, __be32 ms_addr)
{
	struct hlist_head *head;
	struct pdp_ctx *pdp;

	head = &gtp->addr_hash[ipv4_hashfn(ms_addr) % gtp->hash_size];

	hlist_for_each_entry_rcu(pdp, head, hlist_addr) {
		if (pdp->af == AF_INET &&
		    pdp->ms_addr_ip4.s_addr == ms_addr)
			return pdp;
	}

	return NULL;
}

static bool gtp_check_src_ms_ipv4(struct sk_buff *skb, struct pdp_ctx *pctx,
				  unsigned int hdrlen)
{
	struct iphdr *iph;

	if (!pskb_may_pull(skb, hdrlen + sizeof(struct iphdr)))
		return false;

	iph = (struct iphdr *)(skb->data + hdrlen + sizeof(struct iphdr));

	return iph->saddr != pctx->ms_addr_ip4.s_addr;
}

/* Check if the inner IP source address in this packet is assigned to any
 * existing mobile subscriber.
 */
static bool gtp_check_src_ms(struct sk_buff *skb, struct pdp_ctx *pctx,
			     unsigned int hdrlen)
{
	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:
		return gtp_check_src_ms_ipv4(skb, pctx, hdrlen);
	}
	return false;
}

/* 1 means pass up to the stack, -1 means drop and 0 means decapsulated. */
static int gtp0_udp_encap_recv(struct gtp_dev *gtp, struct sk_buff *skb,
			       bool xnet)
{
	unsigned int hdrlen = sizeof(struct udphdr) +
			      sizeof(struct gtp0_header);
	struct gtp0_header *gtp0;
	struct pdp_ctx *pctx;
	int ret = 0;

	if (!pskb_may_pull(skb, hdrlen))
		return -1;

	gtp0 = (struct gtp0_header *)(skb->data + sizeof(struct udphdr));

	if ((gtp0->flags >> 5) != GTP_V0)
		return 1;

	if (gtp0->type != GTP_TPDU)
		return 1;

	rcu_read_lock();
	pctx = gtp0_pdp_find(gtp, be64_to_cpu(gtp0->tid));
	if (!pctx) {
		netdev_dbg(gtp->dev, "No PDP ctx to decap skb=%p\n", skb);
		ret = -1;
		goto out_rcu;
	}

	if (!gtp_check_src_ms(skb, pctx, hdrlen)) {
		netdev_dbg(gtp->dev, "No PDP ctx for this MS\n");
		ret = -1;
		goto out_rcu;
	}
	rcu_read_unlock();

	/* Get rid of the GTP + UDP headers. */
	return iptunnel_pull_header(skb, hdrlen, skb->protocol, xnet);
out_rcu:
	rcu_read_unlock();
	return ret;
}

static int gtp1u_udp_encap_recv(struct gtp_dev *gtp, struct sk_buff *skb,
				bool xnet)
{
	unsigned int hdrlen = sizeof(struct udphdr) +
			      sizeof(struct gtp1_header);
	struct gtp1_header *gtp1;
	struct pdp_ctx *pctx;
	int ret = 0;

	if (!pskb_may_pull(skb, hdrlen))
		return -1;

	gtp1 = (struct gtp1_header *)(skb->data + sizeof(struct udphdr));

	if ((gtp1->flags >> 5) != GTP_V1)
		return 1;

	if (gtp1->type != GTP_TPDU)
		return 1;

	/* From 29.060: "This field shall be present if and only if any one or
	 * more of the S, PN and E flags are set.".
	 *
	 * If any of the bit is set, then the remaining ones also have to be
	 * set.
	 */
	if (gtp1->flags & GTP1_F_MASK)
		hdrlen += 4;

	/* Make sure the header is larger enough, including extensions. */
	if (!pskb_may_pull(skb, hdrlen))
		return -1;

	gtp1 = (struct gtp1_header *)(skb->data + sizeof(struct udphdr));

	rcu_read_lock();
	pctx = gtp1_pdp_find(gtp, ntohl(gtp1->tid));
	if (!pctx) {
		netdev_dbg(gtp->dev, "No PDP ctx to decap skb=%p\n", skb);
		ret = -1;
		goto out_rcu;
	}

	if (!gtp_check_src_ms(skb, pctx, hdrlen)) {
		netdev_dbg(gtp->dev, "No PDP ctx for this MS\n");
		ret = -1;
		goto out_rcu;
	}
	rcu_read_unlock();

	/* Get rid of the GTP + UDP headers. */
	return iptunnel_pull_header(skb, hdrlen, skb->protocol, xnet);
out_rcu:
	rcu_read_unlock();
	return ret;
}

static void gtp_encap_disable(struct gtp_dev *gtp)
{
	if (gtp->sock0 && gtp->sock0->sk) {
		udp_sk(gtp->sock0->sk)->encap_type = 0;
		rcu_assign_sk_user_data(gtp->sock0->sk, NULL);
	}
	if (gtp->sock1u && gtp->sock1u->sk) {
		udp_sk(gtp->sock1u->sk)->encap_type = 0;
		rcu_assign_sk_user_data(gtp->sock1u->sk, NULL);
	}

	gtp->sock0 = NULL;
	gtp->sock1u = NULL;
}

static void gtp_encap_destroy(struct sock *sk)
{
	struct gtp_dev *gtp;

	gtp = rcu_dereference_sk_user_data(sk);
	if (gtp)
		gtp_encap_disable(gtp);
}

/* UDP encapsulation receive handler. See net/ipv4/udp.c.
 * Return codes: 0: success, <0: error, >0: pass up to userspace UDP socket.
 */
static int gtp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct pcpu_sw_netstats *stats;
	struct gtp_dev *gtp;
	bool xnet;
	int ret;

	gtp = rcu_dereference_sk_user_data(sk);
	if (!gtp)
		return 1;

	netdev_dbg(gtp->dev, "encap_recv sk=%p\n", sk);

	xnet = !net_eq(gtp->net, dev_net(gtp->dev));

	switch (udp_sk(sk)->encap_type) {
	case UDP_ENCAP_GTP0:
		netdev_dbg(gtp->dev, "received GTP0 packet\n");
		ret = gtp0_udp_encap_recv(gtp, skb, xnet);
		break;
	case UDP_ENCAP_GTP1U:
		netdev_dbg(gtp->dev, "received GTP1U packet\n");
		ret = gtp1u_udp_encap_recv(gtp, skb, xnet);
		break;
	default:
		ret = -1; /* Shouldn't happen. */
	}

	switch (ret) {
	case 1:
		netdev_dbg(gtp->dev, "pass up to the process\n");
		return 1;
	case 0:
		netdev_dbg(gtp->dev, "forwarding packet from GGSN to uplink\n");
		break;
	case -1:
		netdev_dbg(gtp->dev, "GTP packet has been dropped\n");
		kfree_skb(skb);
		return 0;
	}

	/* Now that the UDP and the GTP header have been removed, set up the
	 * new network header. This is required by the upper layer to
	 * calculate the transport header.
	 */
	skb_reset_network_header(skb);

	skb->dev = gtp->dev;

	stats = this_cpu_ptr(gtp->dev->tstats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += skb->len;
	u64_stats_update_end(&stats->syncp);

	netif_rx(skb);

	return 0;
}

static int gtp_dev_init(struct net_device *dev)
{
	struct gtp_dev *gtp = netdev_priv(dev);

	gtp->dev = dev;

	dev->tstats = alloc_percpu(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	return 0;
}

static void gtp_dev_uninit(struct net_device *dev)
{
	struct gtp_dev *gtp = netdev_priv(dev);

	gtp_encap_disable(gtp);
	free_percpu(dev->tstats);
}

static struct rtable *ip4_route_output_gtp(struct net *net, struct flowi4 *fl4,
					   const struct sock *sk, __be32 daddr)
{
	memset(fl4, 0, sizeof(*fl4));
	fl4->flowi4_oif		= sk->sk_bound_dev_if;
	fl4->daddr		= daddr;
	fl4->saddr		= inet_sk(sk)->inet_saddr;
	fl4->flowi4_tos		= RT_CONN_FLAGS(sk);
	fl4->flowi4_proto	= sk->sk_protocol;

	return ip_route_output_key(net, fl4);
}

static inline void gtp0_push_header(struct sk_buff *skb, struct pdp_ctx *pctx)
{
	int payload_len = skb->len;
	struct gtp0_header *gtp0;

	gtp0 = (struct gtp0_header *) skb_push(skb, sizeof(*gtp0));

	gtp0->flags	= 0x1e; /* v0, GTP-non-prime. */
	gtp0->type	= GTP_TPDU;
	gtp0->length	= htons(payload_len);
	gtp0->seq	= htons((atomic_inc_return(&pctx->tx_seq) - 1) % 0xffff);
	gtp0->flow	= htons(pctx->u.v0.flow);
	gtp0->number	= 0xff;
	gtp0->spare[0]	= gtp0->spare[1] = gtp0->spare[2] = 0xff;
	gtp0->tid	= cpu_to_be64(pctx->u.v0.tid);
}

static inline void gtp1_push_header(struct sk_buff *skb, struct pdp_ctx *pctx)
{
	int payload_len = skb->len;
	struct gtp1_header *gtp1;

	gtp1 = (struct gtp1_header *) skb_push(skb, sizeof(*gtp1));

	/* Bits    8  7  6  5  4  3  2	1
	 *	  +--+--+--+--+--+--+--+--+
	 *	  |version |PT| 1| E| S|PN|
	 *	  +--+--+--+--+--+--+--+--+
	 *	    0  0  1  1	1  0  0  0
	 */
	gtp1->flags	= 0x38; /* v1, GTP-non-prime. */
	gtp1->type	= GTP_TPDU;
	gtp1->length	= htons(payload_len);
	gtp1->tid	= htonl(pctx->u.v1.o_tei);

	/* TODO: Suppport for extension header, sequence number and N-PDU.
	 *	 Update the length field if any of them is available.
	 */
}

struct gtp_pktinfo {
	struct sock		*sk;
	struct iphdr		*iph;
	struct flowi4		fl4;
	struct rtable		*rt;
	struct pdp_ctx		*pctx;
	struct net_device	*dev;
	__be16			gtph_port;
};

static void gtp_push_header(struct sk_buff *skb, struct gtp_pktinfo *pktinfo)
{
	switch (pktinfo->pctx->gtp_version) {
	case GTP_V0:
		pktinfo->gtph_port = htons(GTP0_PORT);
		gtp0_push_header(skb, pktinfo->pctx);
		break;
	case GTP_V1:
		pktinfo->gtph_port = htons(GTP1U_PORT);
		gtp1_push_header(skb, pktinfo->pctx);
		break;
	}
}

static inline void gtp_set_pktinfo_ipv4(struct gtp_pktinfo *pktinfo,
					struct sock *sk, struct iphdr *iph,
					struct pdp_ctx *pctx, struct rtable *rt,
					struct flowi4 *fl4,
					struct net_device *dev)
{
	pktinfo->sk	= sk;
	pktinfo->iph	= iph;
	pktinfo->pctx	= pctx;
	pktinfo->rt	= rt;
	pktinfo->fl4	= *fl4;
	pktinfo->dev	= dev;
}

static int gtp_build_skb_ip4(struct sk_buff *skb, struct net_device *dev,
			     struct gtp_pktinfo *pktinfo)
{
	struct gtp_dev *gtp = netdev_priv(dev);
	struct pdp_ctx *pctx;
	struct rtable *rt;
	struct flowi4 fl4;
	struct iphdr *iph;
	struct sock *sk;
	__be16 df;
	int mtu;

	/* Read the IP destination address and resolve the PDP context.
	 * Prepend PDP header with TEI/TID from PDP ctx.
	 */
	iph = ip_hdr(skb);
	pctx = ipv4_pdp_find(gtp, iph->daddr);
	if (!pctx) {
		netdev_dbg(dev, "no PDP ctx found for %pI4, skip\n",
			   &iph->daddr);
		return -ENOENT;
	}
	netdev_dbg(dev, "found PDP context %p\n", pctx);

	switch (pctx->gtp_version) {
	case GTP_V0:
		if (gtp->sock0)
			sk = gtp->sock0->sk;
		else
			sk = NULL;
		break;
	case GTP_V1:
		if (gtp->sock1u)
			sk = gtp->sock1u->sk;
		else
			sk = NULL;
		break;
	default:
		return -ENOENT;
	}

	if (!sk) {
		netdev_dbg(dev, "no userspace socket is available, skip\n");
		return -ENOENT;
	}

	rt = ip4_route_output_gtp(sock_net(sk), &fl4, gtp->sock0->sk,
				  pctx->sgsn_addr_ip4.s_addr);
	if (IS_ERR(rt)) {
		netdev_dbg(dev, "no route to SSGN %pI4\n",
			   &pctx->sgsn_addr_ip4.s_addr);
		dev->stats.tx_carrier_errors++;
		goto err;
	}

	if (rt->dst.dev == dev) {
		netdev_dbg(dev, "circular route to SSGN %pI4\n",
			   &pctx->sgsn_addr_ip4.s_addr);
		dev->stats.collisions++;
		goto err_rt;
	}

	skb_dst_drop(skb);

	/* This is similar to tnl_update_pmtu(). */
	df = iph->frag_off;
	if (df) {
		mtu = dst_mtu(&rt->dst) - dev->hard_header_len -
			sizeof(struct iphdr) - sizeof(struct udphdr);
		switch (pctx->gtp_version) {
		case GTP_V0:
			mtu -= sizeof(struct gtp0_header);
			break;
		case GTP_V1:
			mtu -= sizeof(struct gtp1_header);
			break;
		}
	} else {
		mtu = dst_mtu(&rt->dst);
	}

	rt->dst.ops->update_pmtu(&rt->dst, NULL, skb, mtu);

	if (!skb_is_gso(skb) && (iph->frag_off & htons(IP_DF)) &&
	    mtu < ntohs(iph->tot_len)) {
		netdev_dbg(dev, "packet too big, fragmentation needed\n");
		memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
		icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
			  htonl(mtu));
		goto err_rt;
	}

	gtp_set_pktinfo_ipv4(pktinfo, sk, iph, pctx, rt, &fl4, dev);
	gtp_push_header(skb, pktinfo);

	return 0;
err_rt:
	ip_rt_put(rt);
err:
	return -EBADMSG;
}

static netdev_tx_t gtp_dev_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned int proto = ntohs(skb->protocol);
	struct gtp_pktinfo pktinfo;
	int err;

	/* Ensure there is sufficient headroom. */
	if (skb_cow_head(skb, dev->needed_headroom))
		goto tx_err;

	skb_reset_inner_headers(skb);

	/* PDP context lookups in gtp_build_skb_*() need rcu read-side lock. */
	rcu_read_lock();
	switch (proto) {
	case ETH_P_IP:
		err = gtp_build_skb_ip4(skb, dev, &pktinfo);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}
	rcu_read_unlock();

	if (err < 0)
		goto tx_err;

	switch (proto) {
	case ETH_P_IP:
		netdev_dbg(pktinfo.dev, "gtp -> IP src: %pI4 dst: %pI4\n",
			   &pktinfo.iph->saddr, &pktinfo.iph->daddr);
		udp_tunnel_xmit_skb(pktinfo.rt, pktinfo.sk, skb,
				    pktinfo.fl4.saddr, pktinfo.fl4.daddr,
				    pktinfo.iph->tos,
				    ip4_dst_hoplimit(&pktinfo.rt->dst),
				    htons(IP_DF),
				    pktinfo.gtph_port, pktinfo.gtph_port,
				    true, false);
		break;
	}

	return NETDEV_TX_OK;
tx_err:
	dev->stats.tx_errors++;
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops gtp_netdev_ops = {
	.ndo_init		= gtp_dev_init,
	.ndo_uninit		= gtp_dev_uninit,
	.ndo_start_xmit		= gtp_dev_xmit,
	.ndo_get_stats64	= ip_tunnel_get_stats64,
};

static void gtp_link_setup(struct net_device *dev)
{
	dev->netdev_ops		= &gtp_netdev_ops;
	dev->destructor		= free_netdev;

	dev->hard_header_len = 0;
	dev->addr_len = 0;

	/* Zero header length. */
	dev->type = ARPHRD_NONE;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;

	dev->priv_flags	|= IFF_NO_QUEUE;
	dev->features	|= NETIF_F_LLTX;
	netif_keep_dst(dev);

	/* Assume largest header, ie. GTPv0. */
	dev->needed_headroom	= LL_MAX_HEADER +
				  sizeof(struct iphdr) +
				  sizeof(struct udphdr) +
				  sizeof(struct gtp0_header);
}

static int gtp_hashtable_new(struct gtp_dev *gtp, int hsize);
static void gtp_hashtable_free(struct gtp_dev *gtp);
static int gtp_encap_enable(struct net_device *dev, struct gtp_dev *gtp,
			    int fd_gtp0, int fd_gtp1, struct net *src_net);

static int gtp_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[])
{
	int hashsize, err, fd0, fd1;
	struct gtp_dev *gtp;
	struct gtp_net *gn;

	if (!data[IFLA_GTP_FD0] || !data[IFLA_GTP_FD1])
		return -EINVAL;

	gtp = netdev_priv(dev);

	fd0 = nla_get_u32(data[IFLA_GTP_FD0]);
	fd1 = nla_get_u32(data[IFLA_GTP_FD1]);

	err = gtp_encap_enable(dev, gtp, fd0, fd1, src_net);
	if (err < 0)
		goto out_err;

	if (!data[IFLA_GTP_PDP_HASHSIZE])
		hashsize = 1024;
	else
		hashsize = nla_get_u32(data[IFLA_GTP_PDP_HASHSIZE]);

	err = gtp_hashtable_new(gtp, hashsize);
	if (err < 0)
		goto out_encap;

	err = register_netdevice(dev);
	if (err < 0) {
		netdev_dbg(dev, "failed to register new netdev %d\n", err);
		goto out_hashtable;
	}

	gn = net_generic(dev_net(dev), gtp_net_id);
	list_add_rcu(&gtp->list, &gn->gtp_dev_list);

	netdev_dbg(dev, "registered new GTP interface\n");

	return 0;

out_hashtable:
	gtp_hashtable_free(gtp);
out_encap:
	gtp_encap_disable(gtp);
out_err:
	return err;
}

static void gtp_dellink(struct net_device *dev, struct list_head *head)
{
	struct gtp_dev *gtp = netdev_priv(dev);

	gtp_encap_disable(gtp);
	gtp_hashtable_free(gtp);
	list_del_rcu(&gtp->list);
	unregister_netdevice_queue(dev, head);
}

static const struct nla_policy gtp_policy[IFLA_GTP_MAX + 1] = {
	[IFLA_GTP_FD0]			= { .type = NLA_U32 },
	[IFLA_GTP_FD1]			= { .type = NLA_U32 },
	[IFLA_GTP_PDP_HASHSIZE]		= { .type = NLA_U32 },
};

static int gtp_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (!data)
		return -EINVAL;

	return 0;
}

static size_t gtp_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(__u32));	/* IFLA_GTP_PDP_HASHSIZE */
}

static int gtp_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct gtp_dev *gtp = netdev_priv(dev);

	if (nla_put_u32(skb, IFLA_GTP_PDP_HASHSIZE, gtp->hash_size))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops gtp_link_ops __read_mostly = {
	.kind		= "gtp",
	.maxtype	= IFLA_GTP_MAX,
	.policy		= gtp_policy,
	.priv_size	= sizeof(struct gtp_dev),
	.setup		= gtp_link_setup,
	.validate	= gtp_validate,
	.newlink	= gtp_newlink,
	.dellink	= gtp_dellink,
	.get_size	= gtp_get_size,
	.fill_info	= gtp_fill_info,
};

static struct net *gtp_genl_get_net(struct net *src_net, struct nlattr *tb[])
{
	struct net *net;

	/* Examine the link attributes and figure out which network namespace
	 * we are talking about.
	 */
	if (tb[GTPA_NET_NS_FD])
		net = get_net_ns_by_fd(nla_get_u32(tb[GTPA_NET_NS_FD]));
	else
		net = get_net(src_net);

	return net;
}

static int gtp_hashtable_new(struct gtp_dev *gtp, int hsize)
{
	int i;

	gtp->addr_hash = kmalloc(sizeof(struct hlist_head) * hsize, GFP_KERNEL);
	if (gtp->addr_hash == NULL)
		return -ENOMEM;

	gtp->tid_hash = kmalloc(sizeof(struct hlist_head) * hsize, GFP_KERNEL);
	if (gtp->tid_hash == NULL)
		goto err1;

	gtp->hash_size = hsize;

	for (i = 0; i < hsize; i++) {
		INIT_HLIST_HEAD(&gtp->addr_hash[i]);
		INIT_HLIST_HEAD(&gtp->tid_hash[i]);
	}
	return 0;
err1:
	kfree(gtp->addr_hash);
	return -ENOMEM;
}

static void gtp_hashtable_free(struct gtp_dev *gtp)
{
	struct pdp_ctx *pctx;
	int i;

	for (i = 0; i < gtp->hash_size; i++) {
		hlist_for_each_entry_rcu(pctx, &gtp->tid_hash[i], hlist_tid) {
			hlist_del_rcu(&pctx->hlist_tid);
			hlist_del_rcu(&pctx->hlist_addr);
			kfree_rcu(pctx, rcu_head);
		}
	}
	synchronize_rcu();
	kfree(gtp->addr_hash);
	kfree(gtp->tid_hash);
}

static int gtp_encap_enable(struct net_device *dev, struct gtp_dev *gtp,
			    int fd_gtp0, int fd_gtp1, struct net *src_net)
{
	struct udp_tunnel_sock_cfg tuncfg = {NULL};
	struct socket *sock0, *sock1u;
	int err;

	netdev_dbg(dev, "enable gtp on %d, %d\n", fd_gtp0, fd_gtp1);

	sock0 = sockfd_lookup(fd_gtp0, &err);
	if (sock0 == NULL) {
		netdev_dbg(dev, "socket fd=%d not found (gtp0)\n", fd_gtp0);
		return -ENOENT;
	}

	if (sock0->sk->sk_protocol != IPPROTO_UDP) {
		netdev_dbg(dev, "socket fd=%d not UDP\n", fd_gtp0);
		err = -EINVAL;
		goto err1;
	}

	sock1u = sockfd_lookup(fd_gtp1, &err);
	if (sock1u == NULL) {
		netdev_dbg(dev, "socket fd=%d not found (gtp1u)\n", fd_gtp1);
		err = -ENOENT;
		goto err1;
	}

	if (sock1u->sk->sk_protocol != IPPROTO_UDP) {
		netdev_dbg(dev, "socket fd=%d not UDP\n", fd_gtp1);
		err = -EINVAL;
		goto err2;
	}

	netdev_dbg(dev, "enable gtp on %p, %p\n", sock0, sock1u);

	gtp->sock0 = sock0;
	gtp->sock1u = sock1u;
	gtp->net = src_net;

	tuncfg.sk_user_data = gtp;
	tuncfg.encap_rcv = gtp_encap_recv;
	tuncfg.encap_destroy = gtp_encap_destroy;

	tuncfg.encap_type = UDP_ENCAP_GTP0;
	setup_udp_tunnel_sock(sock_net(gtp->sock0->sk), gtp->sock0, &tuncfg);

	tuncfg.encap_type = UDP_ENCAP_GTP1U;
	setup_udp_tunnel_sock(sock_net(gtp->sock1u->sk), gtp->sock1u, &tuncfg);

	err = 0;
err2:
	sockfd_put(sock1u);
err1:
	sockfd_put(sock0);
	return err;
}

static struct net_device *gtp_find_dev(struct net *net, int ifindex)
{
	struct gtp_net *gn = net_generic(net, gtp_net_id);
	struct gtp_dev *gtp;

	list_for_each_entry_rcu(gtp, &gn->gtp_dev_list, list) {
		if (ifindex == gtp->dev->ifindex)
			return gtp->dev;
	}
	return NULL;
}

static void ipv4_pdp_fill(struct pdp_ctx *pctx, struct genl_info *info)
{
	pctx->gtp_version = nla_get_u32(info->attrs[GTPA_VERSION]);
	pctx->af = AF_INET;
	pctx->sgsn_addr_ip4.s_addr =
		nla_get_be32(info->attrs[GTPA_SGSN_ADDRESS]);
	pctx->ms_addr_ip4.s_addr =
		nla_get_be32(info->attrs[GTPA_MS_ADDRESS]);

	switch (pctx->gtp_version) {
	case GTP_V0:
		/* According to TS 09.60, sections 7.5.1 and 7.5.2, the flow
		 * label needs to be the same for uplink and downlink packets,
		 * so let's annotate this.
		 */
		pctx->u.v0.tid = nla_get_u64(info->attrs[GTPA_TID]);
		pctx->u.v0.flow = nla_get_u16(info->attrs[GTPA_FLOW]);
		break;
	case GTP_V1:
		pctx->u.v1.i_tei = nla_get_u32(info->attrs[GTPA_I_TEI]);
		pctx->u.v1.o_tei = nla_get_u32(info->attrs[GTPA_O_TEI]);
		break;
	default:
		break;
	}
}

static int ipv4_pdp_add(struct net_device *dev, struct genl_info *info)
{
	struct gtp_dev *gtp = netdev_priv(dev);
	u32 hash_ms, hash_tid = 0;
	struct pdp_ctx *pctx;
	bool found = false;
	__be32 ms_addr;

	ms_addr = nla_get_be32(info->attrs[GTPA_MS_ADDRESS]);
	hash_ms = ipv4_hashfn(ms_addr) % gtp->hash_size;

	hlist_for_each_entry_rcu(pctx, &gtp->addr_hash[hash_ms], hlist_addr) {
		if (pctx->ms_addr_ip4.s_addr == ms_addr) {
			found = true;
			break;
		}
	}

	if (found) {
		if (info->nlhdr->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;
		if (info->nlhdr->nlmsg_flags & NLM_F_REPLACE)
			return -EOPNOTSUPP;

		ipv4_pdp_fill(pctx, info);

		if (pctx->gtp_version == GTP_V0)
			netdev_dbg(dev, "GTPv0-U: update tunnel id = %llx (pdp %p)\n",
				   pctx->u.v0.tid, pctx);
		else if (pctx->gtp_version == GTP_V1)
			netdev_dbg(dev, "GTPv1-U: update tunnel id = %x/%x (pdp %p)\n",
				   pctx->u.v1.i_tei, pctx->u.v1.o_tei, pctx);

		return 0;

	}

	pctx = kmalloc(sizeof(struct pdp_ctx), GFP_KERNEL);
	if (pctx == NULL)
		return -ENOMEM;

	ipv4_pdp_fill(pctx, info);
	atomic_set(&pctx->tx_seq, 0);

	switch (pctx->gtp_version) {
	case GTP_V0:
		/* TS 09.60: "The flow label identifies unambiguously a GTP
		 * flow.". We use the tid for this instead, I cannot find a
		 * situation in which this doesn't unambiguosly identify the
		 * PDP context.
		 */
		hash_tid = gtp0_hashfn(pctx->u.v0.tid) % gtp->hash_size;
		break;
	case GTP_V1:
		hash_tid = gtp1u_hashfn(pctx->u.v1.i_tei) % gtp->hash_size;
		break;
	}

	hlist_add_head_rcu(&pctx->hlist_addr, &gtp->addr_hash[hash_ms]);
	hlist_add_head_rcu(&pctx->hlist_tid, &gtp->tid_hash[hash_tid]);

	switch (pctx->gtp_version) {
	case GTP_V0:
		netdev_dbg(dev, "GTPv0-U: new PDP ctx id=%llx ssgn=%pI4 ms=%pI4 (pdp=%p)\n",
			   pctx->u.v0.tid, &pctx->sgsn_addr_ip4,
			   &pctx->ms_addr_ip4, pctx);
		break;
	case GTP_V1:
		netdev_dbg(dev, "GTPv1-U: new PDP ctx id=%x/%x ssgn=%pI4 ms=%pI4 (pdp=%p)\n",
			   pctx->u.v1.i_tei, pctx->u.v1.o_tei,
			   &pctx->sgsn_addr_ip4, &pctx->ms_addr_ip4, pctx);
		break;
	}

	return 0;
}

static int gtp_genl_new_pdp(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct net *net;

	if (!info->attrs[GTPA_VERSION] ||
	    !info->attrs[GTPA_LINK] ||
	    !info->attrs[GTPA_SGSN_ADDRESS] ||
	    !info->attrs[GTPA_MS_ADDRESS])
		return -EINVAL;

	switch (nla_get_u32(info->attrs[GTPA_VERSION])) {
	case GTP_V0:
		if (!info->attrs[GTPA_TID] ||
		    !info->attrs[GTPA_FLOW])
			return -EINVAL;
		break;
	case GTP_V1:
		if (!info->attrs[GTPA_I_TEI] ||
		    !info->attrs[GTPA_O_TEI])
			return -EINVAL;
		break;

	default:
		return -EINVAL;
	}

	net = gtp_genl_get_net(sock_net(skb->sk), info->attrs);
	if (IS_ERR(net))
		return PTR_ERR(net);

	/* Check if there's an existing gtpX device to configure */
	dev = gtp_find_dev(net, nla_get_u32(info->attrs[GTPA_LINK]));
	if (dev == NULL) {
		put_net(net);
		return -ENODEV;
	}
	put_net(net);

	return ipv4_pdp_add(dev, info);
}

static int gtp_genl_del_pdp(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	struct pdp_ctx *pctx;
	struct gtp_dev *gtp;
	struct net *net;

	if (!info->attrs[GTPA_VERSION] ||
	    !info->attrs[GTPA_LINK])
		return -EINVAL;

	net = gtp_genl_get_net(sock_net(skb->sk), info->attrs);
	if (IS_ERR(net))
		return PTR_ERR(net);

	/* Check if there's an existing gtpX device to configure */
	dev = gtp_find_dev(net, nla_get_u32(info->attrs[GTPA_LINK]));
	if (dev == NULL) {
		put_net(net);
		return -ENODEV;
	}
	put_net(net);

	gtp = netdev_priv(dev);

	switch (nla_get_u32(info->attrs[GTPA_VERSION])) {
	case GTP_V0:
		if (!info->attrs[GTPA_TID])
			return -EINVAL;
		pctx = gtp0_pdp_find(gtp, nla_get_u64(info->attrs[GTPA_TID]));
		break;
	case GTP_V1:
		if (!info->attrs[GTPA_I_TEI])
			return -EINVAL;
		pctx = gtp1_pdp_find(gtp, nla_get_u64(info->attrs[GTPA_I_TEI]));
		break;

	default:
		return -EINVAL;
	}

	if (pctx == NULL)
		return -ENOENT;

	if (pctx->gtp_version == GTP_V0)
		netdev_dbg(dev, "GTPv0-U: deleting tunnel id = %llx (pdp %p)\n",
			   pctx->u.v0.tid, pctx);
	else if (pctx->gtp_version == GTP_V1)
		netdev_dbg(dev, "GTPv1-U: deleting tunnel id = %x/%x (pdp %p)\n",
			   pctx->u.v1.i_tei, pctx->u.v1.o_tei, pctx);

	hlist_del_rcu(&pctx->hlist_tid);
	hlist_del_rcu(&pctx->hlist_addr);
	kfree_rcu(pctx, rcu_head);

	return 0;
}

static struct genl_family gtp_genl_family;

static int gtp_genl_fill_info(struct sk_buff *skb, u32 snd_portid, u32 snd_seq,
			      u32 type, struct pdp_ctx *pctx)
{
	void *genlh;

	genlh = genlmsg_put(skb, snd_portid, snd_seq, &gtp_genl_family, 0,
			    type);
	if (genlh == NULL)
		goto nlmsg_failure;

	if (nla_put_u32(skb, GTPA_VERSION, pctx->gtp_version) ||
	    nla_put_be32(skb, GTPA_SGSN_ADDRESS, pctx->sgsn_addr_ip4.s_addr) ||
	    nla_put_be32(skb, GTPA_MS_ADDRESS, pctx->ms_addr_ip4.s_addr))
		goto nla_put_failure;

	switch (pctx->gtp_version) {
	case GTP_V0:
		if (nla_put_u64_64bit(skb, GTPA_TID, pctx->u.v0.tid, GTPA_PAD) ||
		    nla_put_u16(skb, GTPA_FLOW, pctx->u.v0.flow))
			goto nla_put_failure;
		break;
	case GTP_V1:
		if (nla_put_u32(skb, GTPA_I_TEI, pctx->u.v1.i_tei) ||
		    nla_put_u32(skb, GTPA_O_TEI, pctx->u.v1.o_tei))
			goto nla_put_failure;
		break;
	}
	genlmsg_end(skb, genlh);
	return 0;

nlmsg_failure:
nla_put_failure:
	genlmsg_cancel(skb, genlh);
	return -EMSGSIZE;
}

static int gtp_genl_get_pdp(struct sk_buff *skb, struct genl_info *info)
{
	struct pdp_ctx *pctx = NULL;
	struct net_device *dev;
	struct sk_buff *skb2;
	struct gtp_dev *gtp;
	u32 gtp_version;
	struct net *net;
	int err;

	if (!info->attrs[GTPA_VERSION] ||
	    !info->attrs[GTPA_LINK])
		return -EINVAL;

	gtp_version = nla_get_u32(info->attrs[GTPA_VERSION]);
	switch (gtp_version) {
	case GTP_V0:
	case GTP_V1:
		break;
	default:
		return -EINVAL;
	}

	net = gtp_genl_get_net(sock_net(skb->sk), info->attrs);
	if (IS_ERR(net))
		return PTR_ERR(net);

	/* Check if there's an existing gtpX device to configure */
	dev = gtp_find_dev(net, nla_get_u32(info->attrs[GTPA_LINK]));
	if (dev == NULL) {
		put_net(net);
		return -ENODEV;
	}
	put_net(net);

	gtp = netdev_priv(dev);

	rcu_read_lock();
	if (gtp_version == GTP_V0 &&
	    info->attrs[GTPA_TID]) {
		u64 tid = nla_get_u64(info->attrs[GTPA_TID]);

		pctx = gtp0_pdp_find(gtp, tid);
	} else if (gtp_version == GTP_V1 &&
		 info->attrs[GTPA_I_TEI]) {
		u32 tid = nla_get_u32(info->attrs[GTPA_I_TEI]);

		pctx = gtp1_pdp_find(gtp, tid);
	} else if (info->attrs[GTPA_MS_ADDRESS]) {
		__be32 ip = nla_get_be32(info->attrs[GTPA_MS_ADDRESS]);

		pctx = ipv4_pdp_find(gtp, ip);
	}

	if (pctx == NULL) {
		err = -ENOENT;
		goto err_unlock;
	}

	skb2 = genlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (skb2 == NULL) {
		err = -ENOMEM;
		goto err_unlock;
	}

	err = gtp_genl_fill_info(skb2, NETLINK_CB(skb).portid,
				 info->snd_seq, info->nlhdr->nlmsg_type, pctx);
	if (err < 0)
		goto err_unlock_free;

	rcu_read_unlock();
	return genlmsg_unicast(genl_info_net(info), skb2, info->snd_portid);

err_unlock_free:
	kfree_skb(skb2);
err_unlock:
	rcu_read_unlock();
	return err;
}

static int gtp_genl_dump_pdp(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct gtp_dev *last_gtp = (struct gtp_dev *)cb->args[2], *gtp;
	struct net *net = sock_net(skb->sk);
	struct gtp_net *gn = net_generic(net, gtp_net_id);
	unsigned long tid = cb->args[1];
	int i, k = cb->args[0], ret;
	struct pdp_ctx *pctx;

	if (cb->args[4])
		return 0;

	list_for_each_entry_rcu(gtp, &gn->gtp_dev_list, list) {
		if (last_gtp && last_gtp != gtp)
			continue;
		else
			last_gtp = NULL;

		for (i = k; i < gtp->hash_size; i++) {
			hlist_for_each_entry_rcu(pctx, &gtp->tid_hash[i], hlist_tid) {
				if (tid && tid != pctx->u.tid)
					continue;
				else
					tid = 0;

				ret = gtp_genl_fill_info(skb,
							 NETLINK_CB(cb->skb).portid,
							 cb->nlh->nlmsg_seq,
							 cb->nlh->nlmsg_type, pctx);
				if (ret < 0) {
					cb->args[0] = i;
					cb->args[1] = pctx->u.tid;
					cb->args[2] = (unsigned long)gtp;
					goto out;
				}
			}
		}
	}
	cb->args[4] = 1;
out:
	return skb->len;
}

static struct nla_policy gtp_genl_policy[GTPA_MAX + 1] = {
	[GTPA_LINK]		= { .type = NLA_U32, },
	[GTPA_VERSION]		= { .type = NLA_U32, },
	[GTPA_TID]		= { .type = NLA_U64, },
	[GTPA_SGSN_ADDRESS]	= { .type = NLA_U32, },
	[GTPA_MS_ADDRESS]	= { .type = NLA_U32, },
	[GTPA_FLOW]		= { .type = NLA_U16, },
	[GTPA_NET_NS_FD]	= { .type = NLA_U32, },
	[GTPA_I_TEI]		= { .type = NLA_U32, },
	[GTPA_O_TEI]		= { .type = NLA_U32, },
};

static const struct genl_ops gtp_genl_ops[] = {
	{
		.cmd = GTP_CMD_NEWPDP,
		.doit = gtp_genl_new_pdp,
		.policy = gtp_genl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = GTP_CMD_DELPDP,
		.doit = gtp_genl_del_pdp,
		.policy = gtp_genl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = GTP_CMD_GETPDP,
		.doit = gtp_genl_get_pdp,
		.dumpit = gtp_genl_dump_pdp,
		.policy = gtp_genl_policy,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family gtp_genl_family __ro_after_init = {
	.name		= "gtp",
	.version	= 0,
	.hdrsize	= 0,
	.maxattr	= GTPA_MAX,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.ops		= gtp_genl_ops,
	.n_ops		= ARRAY_SIZE(gtp_genl_ops),
};

static int __net_init gtp_net_init(struct net *net)
{
	struct gtp_net *gn = net_generic(net, gtp_net_id);

	INIT_LIST_HEAD(&gn->gtp_dev_list);
	return 0;
}

static void __net_exit gtp_net_exit(struct net *net)
{
	struct gtp_net *gn = net_generic(net, gtp_net_id);
	struct gtp_dev *gtp;
	LIST_HEAD(list);

	rtnl_lock();
	list_for_each_entry(gtp, &gn->gtp_dev_list, list)
		gtp_dellink(gtp->dev, &list);

	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations gtp_net_ops = {
	.init	= gtp_net_init,
	.exit	= gtp_net_exit,
	.id	= &gtp_net_id,
	.size	= sizeof(struct gtp_net),
};

static int __init gtp_init(void)
{
	int err;

	get_random_bytes(&gtp_h_initval, sizeof(gtp_h_initval));

	err = rtnl_link_register(&gtp_link_ops);
	if (err < 0)
		goto error_out;

	err = genl_register_family(&gtp_genl_family);
	if (err < 0)
		goto unreg_rtnl_link;

	err = register_pernet_subsys(&gtp_net_ops);
	if (err < 0)
		goto unreg_genl_family;

	pr_info("GTP module loaded (pdp ctx size %Zd bytes)\n",
		sizeof(struct pdp_ctx));
	return 0;

unreg_genl_family:
	genl_unregister_family(&gtp_genl_family);
unreg_rtnl_link:
	rtnl_link_unregister(&gtp_link_ops);
error_out:
	pr_err("error loading GTP module loaded\n");
	return err;
}
late_initcall(gtp_init);

static void __exit gtp_fini(void)
{
	unregister_pernet_subsys(&gtp_net_ops);
	genl_unregister_family(&gtp_genl_family);
	rtnl_link_unregister(&gtp_link_ops);

	pr_info("GTP module unloaded\n");
}
module_exit(gtp_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <hwelte@sysmocom.de>");
MODULE_DESCRIPTION("Interface driver for GTP encapsulated traffic");
MODULE_ALIAS_RTNL_LINK("gtp");
