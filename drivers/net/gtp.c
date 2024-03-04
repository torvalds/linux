// SPDX-License-Identifier: GPL-2.0-or-later
/* GTP according to GSM TS 09.60 / 3GPP TS 29.060
 *
 * (C) 2012-2014 by sysmocom - s.f.m.c. GmbH
 * (C) 2016 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * Author: Harald Welte <hwelte@sysmocom.de>
 *	   Pablo Neira Ayuso <pablo@netfilter.org>
 *	   Andreas Schultz <aschultz@travelping.com>
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
	struct in_addr		peer_addr_ip4;

	struct sock		*sk;
	struct net_device       *dev;

	atomic_t		tx_seq;
	struct rcu_head		rcu_head;
};

/* One instance of the GTP device. */
struct gtp_dev {
	struct list_head	list;

	struct sock		*sk0;
	struct sock		*sk1u;
	u8			sk_created;

	struct net_device	*dev;
	struct net		*net;

	unsigned int		role;
	unsigned int		hash_size;
	struct hlist_head	*tid_hash;
	struct hlist_head	*addr_hash;

	u8			restart_count;
};

struct echo_info {
	struct in_addr		ms_addr_ip4;
	struct in_addr		peer_addr_ip4;
	u8			gtp_version;
};

static unsigned int gtp_net_id __read_mostly;

struct gtp_net {
	struct list_head gtp_dev_list;
};

static u32 gtp_h_initval;

static struct genl_family gtp_genl_family;

enum gtp_multicast_groups {
	GTP_GENL_MCGRP,
};

static const struct genl_multicast_group gtp_genl_mcgrps[] = {
	[GTP_GENL_MCGRP] = { .name = GTP_GENL_MCGRP_NAME },
};

static void pdp_context_delete(struct pdp_ctx *pctx);

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

static bool gtp_check_ms_ipv4(struct sk_buff *skb, struct pdp_ctx *pctx,
				  unsigned int hdrlen, unsigned int role)
{
	struct iphdr *iph;

	if (!pskb_may_pull(skb, hdrlen + sizeof(struct iphdr)))
		return false;

	iph = (struct iphdr *)(skb->data + hdrlen);

	if (role == GTP_ROLE_SGSN)
		return iph->daddr == pctx->ms_addr_ip4.s_addr;
	else
		return iph->saddr == pctx->ms_addr_ip4.s_addr;
}

/* Check if the inner IP address in this packet is assigned to any
 * existing mobile subscriber.
 */
static bool gtp_check_ms(struct sk_buff *skb, struct pdp_ctx *pctx,
			     unsigned int hdrlen, unsigned int role)
{
	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:
		return gtp_check_ms_ipv4(skb, pctx, hdrlen, role);
	}
	return false;
}

static int gtp_rx(struct pdp_ctx *pctx, struct sk_buff *skb,
			unsigned int hdrlen, unsigned int role)
{
	if (!gtp_check_ms(skb, pctx, hdrlen, role)) {
		netdev_dbg(pctx->dev, "No PDP ctx for this MS\n");
		return 1;
	}

	/* Get rid of the GTP + UDP headers. */
	if (iptunnel_pull_header(skb, hdrlen, skb->protocol,
			 !net_eq(sock_net(pctx->sk), dev_net(pctx->dev)))) {
		pctx->dev->stats.rx_length_errors++;
		goto err;
	}

	netdev_dbg(pctx->dev, "forwarding packet from GGSN to uplink\n");

	/* Now that the UDP and the GTP header have been removed, set up the
	 * new network header. This is required by the upper layer to
	 * calculate the transport header.
	 */
	skb_reset_network_header(skb);
	skb_reset_mac_header(skb);

	skb->dev = pctx->dev;

	dev_sw_netstats_rx_add(pctx->dev, skb->len);

	__netif_rx(skb);
	return 0;

err:
	pctx->dev->stats.rx_dropped++;
	return -1;
}

static struct rtable *ip4_route_output_gtp(struct flowi4 *fl4,
					   const struct sock *sk,
					   __be32 daddr, __be32 saddr)
{
	memset(fl4, 0, sizeof(*fl4));
	fl4->flowi4_oif		= sk->sk_bound_dev_if;
	fl4->daddr		= daddr;
	fl4->saddr		= saddr;
	fl4->flowi4_tos		= ip_sock_rt_tos(sk);
	fl4->flowi4_scope	= ip_sock_rt_scope(sk);
	fl4->flowi4_proto	= sk->sk_protocol;

	return ip_route_output_key(sock_net(sk), fl4);
}

/* GSM TS 09.60. 7.3
 * In all Path Management messages:
 * - TID: is not used and shall be set to 0.
 * - Flow Label is not used and shall be set to 0
 * In signalling messages:
 * - number: this field is not yet used in signalling messages.
 *   It shall be set to 255 by the sender and shall be ignored
 *   by the receiver
 * Returns true if the echo req was correct, false otherwise.
 */
static bool gtp0_validate_echo_hdr(struct gtp0_header *gtp0)
{
	return !(gtp0->tid || (gtp0->flags ^ 0x1e) ||
		gtp0->number != 0xff || gtp0->flow);
}

/* msg_type has to be GTP_ECHO_REQ or GTP_ECHO_RSP */
static void gtp0_build_echo_msg(struct gtp0_header *hdr, __u8 msg_type)
{
	int len_pkt, len_hdr;

	hdr->flags = 0x1e; /* v0, GTP-non-prime. */
	hdr->type = msg_type;
	/* GSM TS 09.60. 7.3 In all Path Management Flow Label and TID
	 * are not used and shall be set to 0.
	 */
	hdr->flow = 0;
	hdr->tid = 0;
	hdr->number = 0xff;
	hdr->spare[0] = 0xff;
	hdr->spare[1] = 0xff;
	hdr->spare[2] = 0xff;

	len_pkt = sizeof(struct gtp0_packet);
	len_hdr = sizeof(struct gtp0_header);

	if (msg_type == GTP_ECHO_RSP)
		hdr->length = htons(len_pkt - len_hdr);
	else
		hdr->length = 0;
}

static int gtp0_send_echo_resp(struct gtp_dev *gtp, struct sk_buff *skb)
{
	struct gtp0_packet *gtp_pkt;
	struct gtp0_header *gtp0;
	struct rtable *rt;
	struct flowi4 fl4;
	struct iphdr *iph;
	__be16 seq;

	gtp0 = (struct gtp0_header *)(skb->data + sizeof(struct udphdr));

	if (!gtp0_validate_echo_hdr(gtp0))
		return -1;

	seq = gtp0->seq;

	/* pull GTP and UDP headers */
	skb_pull_data(skb, sizeof(struct gtp0_header) + sizeof(struct udphdr));

	gtp_pkt = skb_push(skb, sizeof(struct gtp0_packet));
	memset(gtp_pkt, 0, sizeof(struct gtp0_packet));

	gtp0_build_echo_msg(&gtp_pkt->gtp0_h, GTP_ECHO_RSP);

	/* GSM TS 09.60. 7.3 The Sequence Number in a signalling response
	 * message shall be copied from the signalling request message
	 * that the GSN is replying to.
	 */
	gtp_pkt->gtp0_h.seq = seq;

	gtp_pkt->ie.tag = GTPIE_RECOVERY;
	gtp_pkt->ie.val = gtp->restart_count;

	iph = ip_hdr(skb);

	/* find route to the sender,
	 * src address becomes dst address and vice versa.
	 */
	rt = ip4_route_output_gtp(&fl4, gtp->sk0, iph->saddr, iph->daddr);
	if (IS_ERR(rt)) {
		netdev_dbg(gtp->dev, "no route for echo response from %pI4\n",
			   &iph->saddr);
		return -1;
	}

	udp_tunnel_xmit_skb(rt, gtp->sk0, skb,
			    fl4.saddr, fl4.daddr,
			    iph->tos,
			    ip4_dst_hoplimit(&rt->dst),
			    0,
			    htons(GTP0_PORT), htons(GTP0_PORT),
			    !net_eq(sock_net(gtp->sk1u),
				    dev_net(gtp->dev)),
			    false);
	return 0;
}

static int gtp_genl_fill_echo(struct sk_buff *skb, u32 snd_portid, u32 snd_seq,
			      int flags, u32 type, struct echo_info echo)
{
	void *genlh;

	genlh = genlmsg_put(skb, snd_portid, snd_seq, &gtp_genl_family, flags,
			    type);
	if (!genlh)
		goto failure;

	if (nla_put_u32(skb, GTPA_VERSION, echo.gtp_version) ||
	    nla_put_be32(skb, GTPA_PEER_ADDRESS, echo.peer_addr_ip4.s_addr) ||
	    nla_put_be32(skb, GTPA_MS_ADDRESS, echo.ms_addr_ip4.s_addr))
		goto failure;

	genlmsg_end(skb, genlh);
	return 0;

failure:
	genlmsg_cancel(skb, genlh);
	return -EMSGSIZE;
}

static int gtp0_handle_echo_resp(struct gtp_dev *gtp, struct sk_buff *skb)
{
	struct gtp0_header *gtp0;
	struct echo_info echo;
	struct sk_buff *msg;
	struct iphdr *iph;
	int ret;

	gtp0 = (struct gtp0_header *)(skb->data + sizeof(struct udphdr));

	if (!gtp0_validate_echo_hdr(gtp0))
		return -1;

	iph = ip_hdr(skb);
	echo.ms_addr_ip4.s_addr = iph->daddr;
	echo.peer_addr_ip4.s_addr = iph->saddr;
	echo.gtp_version = GTP_V0;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	ret = gtp_genl_fill_echo(msg, 0, 0, 0, GTP_CMD_ECHOREQ, echo);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	return genlmsg_multicast_netns(&gtp_genl_family, dev_net(gtp->dev),
				       msg, 0, GTP_GENL_MCGRP, GFP_ATOMIC);
}

/* 1 means pass up to the stack, -1 means drop and 0 means decapsulated. */
static int gtp0_udp_encap_recv(struct gtp_dev *gtp, struct sk_buff *skb)
{
	unsigned int hdrlen = sizeof(struct udphdr) +
			      sizeof(struct gtp0_header);
	struct gtp0_header *gtp0;
	struct pdp_ctx *pctx;

	if (!pskb_may_pull(skb, hdrlen))
		return -1;

	gtp0 = (struct gtp0_header *)(skb->data + sizeof(struct udphdr));

	if ((gtp0->flags >> 5) != GTP_V0)
		return 1;

	/* If the sockets were created in kernel, it means that
	 * there is no daemon running in userspace which would
	 * handle echo request.
	 */
	if (gtp0->type == GTP_ECHO_REQ && gtp->sk_created)
		return gtp0_send_echo_resp(gtp, skb);

	if (gtp0->type == GTP_ECHO_RSP && gtp->sk_created)
		return gtp0_handle_echo_resp(gtp, skb);

	if (gtp0->type != GTP_TPDU)
		return 1;

	pctx = gtp0_pdp_find(gtp, be64_to_cpu(gtp0->tid));
	if (!pctx) {
		netdev_dbg(gtp->dev, "No PDP ctx to decap skb=%p\n", skb);
		return 1;
	}

	return gtp_rx(pctx, skb, hdrlen, gtp->role);
}

/* msg_type has to be GTP_ECHO_REQ or GTP_ECHO_RSP */
static void gtp1u_build_echo_msg(struct gtp1_header_long *hdr, __u8 msg_type)
{
	int len_pkt, len_hdr;

	/* S flag must be set to 1 */
	hdr->flags = 0x32; /* v1, GTP-non-prime. */
	hdr->type = msg_type;
	/* 3GPP TS 29.281 5.1 - TEID has to be set to 0 */
	hdr->tid = 0;

	/* seq, npdu and next should be counted to the length of the GTP packet
	 * that's why szie of gtp1_header should be subtracted,
	 * not size of gtp1_header_long.
	 */

	len_hdr = sizeof(struct gtp1_header);

	if (msg_type == GTP_ECHO_RSP) {
		len_pkt = sizeof(struct gtp1u_packet);
		hdr->length = htons(len_pkt - len_hdr);
	} else {
		/* GTP_ECHO_REQ does not carry GTP Information Element,
		 * the why gtp1_header_long is used here.
		 */
		len_pkt = sizeof(struct gtp1_header_long);
		hdr->length = htons(len_pkt - len_hdr);
	}
}

static int gtp1u_send_echo_resp(struct gtp_dev *gtp, struct sk_buff *skb)
{
	struct gtp1_header_long *gtp1u;
	struct gtp1u_packet *gtp_pkt;
	struct rtable *rt;
	struct flowi4 fl4;
	struct iphdr *iph;

	gtp1u = (struct gtp1_header_long *)(skb->data + sizeof(struct udphdr));

	/* 3GPP TS 29.281 5.1 - For the Echo Request, Echo Response,
	 * Error Indication and Supported Extension Headers Notification
	 * messages, the S flag shall be set to 1 and TEID shall be set to 0.
	 */
	if (!(gtp1u->flags & GTP1_F_SEQ) || gtp1u->tid)
		return -1;

	/* pull GTP and UDP headers */
	skb_pull_data(skb,
		      sizeof(struct gtp1_header_long) + sizeof(struct udphdr));

	gtp_pkt = skb_push(skb, sizeof(struct gtp1u_packet));
	memset(gtp_pkt, 0, sizeof(struct gtp1u_packet));

	gtp1u_build_echo_msg(&gtp_pkt->gtp1u_h, GTP_ECHO_RSP);

	/* 3GPP TS 29.281 7.7.2 - The Restart Counter value in the
	 * Recovery information element shall not be used, i.e. it shall
	 * be set to zero by the sender and shall be ignored by the receiver.
	 * The Recovery information element is mandatory due to backwards
	 * compatibility reasons.
	 */
	gtp_pkt->ie.tag = GTPIE_RECOVERY;
	gtp_pkt->ie.val = 0;

	iph = ip_hdr(skb);

	/* find route to the sender,
	 * src address becomes dst address and vice versa.
	 */
	rt = ip4_route_output_gtp(&fl4, gtp->sk1u, iph->saddr, iph->daddr);
	if (IS_ERR(rt)) {
		netdev_dbg(gtp->dev, "no route for echo response from %pI4\n",
			   &iph->saddr);
		return -1;
	}

	udp_tunnel_xmit_skb(rt, gtp->sk1u, skb,
			    fl4.saddr, fl4.daddr,
			    iph->tos,
			    ip4_dst_hoplimit(&rt->dst),
			    0,
			    htons(GTP1U_PORT), htons(GTP1U_PORT),
			    !net_eq(sock_net(gtp->sk1u),
				    dev_net(gtp->dev)),
			    false);
	return 0;
}

static int gtp1u_handle_echo_resp(struct gtp_dev *gtp, struct sk_buff *skb)
{
	struct gtp1_header_long *gtp1u;
	struct echo_info echo;
	struct sk_buff *msg;
	struct iphdr *iph;
	int ret;

	gtp1u = (struct gtp1_header_long *)(skb->data + sizeof(struct udphdr));

	/* 3GPP TS 29.281 5.1 - For the Echo Request, Echo Response,
	 * Error Indication and Supported Extension Headers Notification
	 * messages, the S flag shall be set to 1 and TEID shall be set to 0.
	 */
	if (!(gtp1u->flags & GTP1_F_SEQ) || gtp1u->tid)
		return -1;

	iph = ip_hdr(skb);
	echo.ms_addr_ip4.s_addr = iph->daddr;
	echo.peer_addr_ip4.s_addr = iph->saddr;
	echo.gtp_version = GTP_V1;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	ret = gtp_genl_fill_echo(msg, 0, 0, 0, GTP_CMD_ECHOREQ, echo);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	return genlmsg_multicast_netns(&gtp_genl_family, dev_net(gtp->dev),
				       msg, 0, GTP_GENL_MCGRP, GFP_ATOMIC);
}

static int gtp1u_udp_encap_recv(struct gtp_dev *gtp, struct sk_buff *skb)
{
	unsigned int hdrlen = sizeof(struct udphdr) +
			      sizeof(struct gtp1_header);
	struct gtp1_header *gtp1;
	struct pdp_ctx *pctx;

	if (!pskb_may_pull(skb, hdrlen))
		return -1;

	gtp1 = (struct gtp1_header *)(skb->data + sizeof(struct udphdr));

	if ((gtp1->flags >> 5) != GTP_V1)
		return 1;

	/* If the sockets were created in kernel, it means that
	 * there is no daemon running in userspace which would
	 * handle echo request.
	 */
	if (gtp1->type == GTP_ECHO_REQ && gtp->sk_created)
		return gtp1u_send_echo_resp(gtp, skb);

	if (gtp1->type == GTP_ECHO_RSP && gtp->sk_created)
		return gtp1u_handle_echo_resp(gtp, skb);

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

	pctx = gtp1_pdp_find(gtp, ntohl(gtp1->tid));
	if (!pctx) {
		netdev_dbg(gtp->dev, "No PDP ctx to decap skb=%p\n", skb);
		return 1;
	}

	return gtp_rx(pctx, skb, hdrlen, gtp->role);
}

static void __gtp_encap_destroy(struct sock *sk)
{
	struct gtp_dev *gtp;

	lock_sock(sk);
	gtp = sk->sk_user_data;
	if (gtp) {
		if (gtp->sk0 == sk)
			gtp->sk0 = NULL;
		else
			gtp->sk1u = NULL;
		WRITE_ONCE(udp_sk(sk)->encap_type, 0);
		rcu_assign_sk_user_data(sk, NULL);
		release_sock(sk);
		sock_put(sk);
		return;
	}
	release_sock(sk);
}

static void gtp_encap_destroy(struct sock *sk)
{
	rtnl_lock();
	__gtp_encap_destroy(sk);
	rtnl_unlock();
}

static void gtp_encap_disable_sock(struct sock *sk)
{
	if (!sk)
		return;

	__gtp_encap_destroy(sk);
}

static void gtp_encap_disable(struct gtp_dev *gtp)
{
	if (gtp->sk_created) {
		udp_tunnel_sock_release(gtp->sk0->sk_socket);
		udp_tunnel_sock_release(gtp->sk1u->sk_socket);
		gtp->sk_created = false;
		gtp->sk0 = NULL;
		gtp->sk1u = NULL;
	} else {
		gtp_encap_disable_sock(gtp->sk0);
		gtp_encap_disable_sock(gtp->sk1u);
	}
}

/* UDP encapsulation receive handler. See net/ipv4/udp.c.
 * Return codes: 0: success, <0: error, >0: pass up to userspace UDP socket.
 */
static int gtp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct gtp_dev *gtp;
	int ret = 0;

	gtp = rcu_dereference_sk_user_data(sk);
	if (!gtp)
		return 1;

	netdev_dbg(gtp->dev, "encap_recv sk=%p\n", sk);

	switch (READ_ONCE(udp_sk(sk)->encap_type)) {
	case UDP_ENCAP_GTP0:
		netdev_dbg(gtp->dev, "received GTP0 packet\n");
		ret = gtp0_udp_encap_recv(gtp, skb);
		break;
	case UDP_ENCAP_GTP1U:
		netdev_dbg(gtp->dev, "received GTP1U packet\n");
		ret = gtp1u_udp_encap_recv(gtp, skb);
		break;
	default:
		ret = -1; /* Shouldn't happen. */
	}

	switch (ret) {
	case 1:
		netdev_dbg(gtp->dev, "pass up to the process\n");
		break;
	case 0:
		break;
	case -1:
		netdev_dbg(gtp->dev, "GTP packet has been dropped\n");
		kfree_skb(skb);
		ret = 0;
		break;
	}

	return ret;
}

static int gtp_dev_init(struct net_device *dev)
{
	struct gtp_dev *gtp = netdev_priv(dev);

	gtp->dev = dev;

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
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

static inline void gtp0_push_header(struct sk_buff *skb, struct pdp_ctx *pctx)
{
	int payload_len = skb->len;
	struct gtp0_header *gtp0;

	gtp0 = skb_push(skb, sizeof(*gtp0));

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

	gtp1 = skb_push(skb, sizeof(*gtp1));

	/* Bits    8  7  6  5  4  3  2	1
	 *	  +--+--+--+--+--+--+--+--+
	 *	  |version |PT| 0| E| S|PN|
	 *	  +--+--+--+--+--+--+--+--+
	 *	    0  0  1  1	1  0  0  0
	 */
	gtp1->flags	= 0x30; /* v1, GTP-non-prime. */
	gtp1->type	= GTP_TPDU;
	gtp1->length	= htons(payload_len);
	gtp1->tid	= htonl(pctx->u.v1.o_tei);

	/* TODO: Support for extension header, sequence number and N-PDU.
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
	__be16 df;
	int mtu;

	/* Read the IP destination address and resolve the PDP context.
	 * Prepend PDP header with TEI/TID from PDP ctx.
	 */
	iph = ip_hdr(skb);
	if (gtp->role == GTP_ROLE_SGSN)
		pctx = ipv4_pdp_find(gtp, iph->saddr);
	else
		pctx = ipv4_pdp_find(gtp, iph->daddr);

	if (!pctx) {
		netdev_dbg(dev, "no PDP ctx found for %pI4, skip\n",
			   &iph->daddr);
		return -ENOENT;
	}
	netdev_dbg(dev, "found PDP context %p\n", pctx);

	rt = ip4_route_output_gtp(&fl4, pctx->sk, pctx->peer_addr_ip4.s_addr,
				  inet_sk(pctx->sk)->inet_saddr);
	if (IS_ERR(rt)) {
		netdev_dbg(dev, "no route to SSGN %pI4\n",
			   &pctx->peer_addr_ip4.s_addr);
		dev->stats.tx_carrier_errors++;
		goto err;
	}

	if (rt->dst.dev == dev) {
		netdev_dbg(dev, "circular route to SSGN %pI4\n",
			   &pctx->peer_addr_ip4.s_addr);
		dev->stats.collisions++;
		goto err_rt;
	}

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

	skb_dst_update_pmtu_no_confirm(skb, mtu);

	if (iph->frag_off & htons(IP_DF) &&
	    ((!skb_is_gso(skb) && skb->len > mtu) ||
	     (skb_is_gso(skb) && !skb_gso_validate_network_len(skb, mtu)))) {
		netdev_dbg(dev, "packet too big, fragmentation needed\n");
		icmp_ndo_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
			      htonl(mtu));
		goto err_rt;
	}

	gtp_set_pktinfo_ipv4(pktinfo, pctx->sk, iph, pctx, rt, &fl4, dev);
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
				    0,
				    pktinfo.gtph_port, pktinfo.gtph_port,
				    !net_eq(sock_net(pktinfo.pctx->sk),
					    dev_net(dev)),
				    false);
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
	.ndo_get_stats64	= dev_get_tstats64,
};

static const struct device_type gtp_type = {
	.name = "gtp",
};

static void gtp_link_setup(struct net_device *dev)
{
	unsigned int max_gtp_header_len = sizeof(struct iphdr) +
					  sizeof(struct udphdr) +
					  sizeof(struct gtp0_header);

	dev->netdev_ops		= &gtp_netdev_ops;
	dev->needs_free_netdev	= true;
	SET_NETDEV_DEVTYPE(dev, &gtp_type);

	dev->hard_header_len = 0;
	dev->addr_len = 0;
	dev->mtu = ETH_DATA_LEN - max_gtp_header_len;

	/* Zero header length. */
	dev->type = ARPHRD_NONE;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;

	dev->priv_flags	|= IFF_NO_QUEUE;
	dev->features	|= NETIF_F_LLTX;
	netif_keep_dst(dev);

	dev->needed_headroom	= LL_MAX_HEADER + max_gtp_header_len;
}

static int gtp_hashtable_new(struct gtp_dev *gtp, int hsize);
static int gtp_encap_enable(struct gtp_dev *gtp, struct nlattr *data[]);

static void gtp_destructor(struct net_device *dev)
{
	struct gtp_dev *gtp = netdev_priv(dev);

	kfree(gtp->addr_hash);
	kfree(gtp->tid_hash);
}

static struct sock *gtp_create_sock(int type, struct gtp_dev *gtp)
{
	struct udp_tunnel_sock_cfg tuncfg = {};
	struct udp_port_cfg udp_conf = {
		.local_ip.s_addr	= htonl(INADDR_ANY),
		.family			= AF_INET,
	};
	struct net *net = gtp->net;
	struct socket *sock;
	int err;

	if (type == UDP_ENCAP_GTP0)
		udp_conf.local_udp_port = htons(GTP0_PORT);
	else if (type == UDP_ENCAP_GTP1U)
		udp_conf.local_udp_port = htons(GTP1U_PORT);
	else
		return ERR_PTR(-EINVAL);

	err = udp_sock_create(net, &udp_conf, &sock);
	if (err)
		return ERR_PTR(err);

	tuncfg.sk_user_data = gtp;
	tuncfg.encap_type = type;
	tuncfg.encap_rcv = gtp_encap_recv;
	tuncfg.encap_destroy = NULL;

	setup_udp_tunnel_sock(net, sock, &tuncfg);

	return sock->sk;
}

static int gtp_create_sockets(struct gtp_dev *gtp, struct nlattr *data[])
{
	struct sock *sk1u = NULL;
	struct sock *sk0 = NULL;

	sk0 = gtp_create_sock(UDP_ENCAP_GTP0, gtp);
	if (IS_ERR(sk0))
		return PTR_ERR(sk0);

	sk1u = gtp_create_sock(UDP_ENCAP_GTP1U, gtp);
	if (IS_ERR(sk1u)) {
		udp_tunnel_sock_release(sk0->sk_socket);
		return PTR_ERR(sk1u);
	}

	gtp->sk_created = true;
	gtp->sk0 = sk0;
	gtp->sk1u = sk1u;

	return 0;
}

static int gtp_newlink(struct net *src_net, struct net_device *dev,
		       struct nlattr *tb[], struct nlattr *data[],
		       struct netlink_ext_ack *extack)
{
	unsigned int role = GTP_ROLE_GGSN;
	struct gtp_dev *gtp;
	struct gtp_net *gn;
	int hashsize, err;

	gtp = netdev_priv(dev);

	if (!data[IFLA_GTP_PDP_HASHSIZE]) {
		hashsize = 1024;
	} else {
		hashsize = nla_get_u32(data[IFLA_GTP_PDP_HASHSIZE]);
		if (!hashsize)
			hashsize = 1024;
	}

	if (data[IFLA_GTP_ROLE]) {
		role = nla_get_u32(data[IFLA_GTP_ROLE]);
		if (role > GTP_ROLE_SGSN)
			return -EINVAL;
	}
	gtp->role = role;

	if (!data[IFLA_GTP_RESTART_COUNT])
		gtp->restart_count = 0;
	else
		gtp->restart_count = nla_get_u8(data[IFLA_GTP_RESTART_COUNT]);

	gtp->net = src_net;

	err = gtp_hashtable_new(gtp, hashsize);
	if (err < 0)
		return err;

	if (data[IFLA_GTP_CREATE_SOCKETS])
		err = gtp_create_sockets(gtp, data);
	else
		err = gtp_encap_enable(gtp, data);
	if (err < 0)
		goto out_hashtable;

	err = register_netdevice(dev);
	if (err < 0) {
		netdev_dbg(dev, "failed to register new netdev %d\n", err);
		goto out_encap;
	}

	gn = net_generic(dev_net(dev), gtp_net_id);
	list_add_rcu(&gtp->list, &gn->gtp_dev_list);
	dev->priv_destructor = gtp_destructor;

	netdev_dbg(dev, "registered new GTP interface\n");

	return 0;

out_encap:
	gtp_encap_disable(gtp);
out_hashtable:
	kfree(gtp->addr_hash);
	kfree(gtp->tid_hash);
	return err;
}

static void gtp_dellink(struct net_device *dev, struct list_head *head)
{
	struct gtp_dev *gtp = netdev_priv(dev);
	struct pdp_ctx *pctx;
	int i;

	for (i = 0; i < gtp->hash_size; i++)
		hlist_for_each_entry_rcu(pctx, &gtp->tid_hash[i], hlist_tid)
			pdp_context_delete(pctx);

	list_del_rcu(&gtp->list);
	unregister_netdevice_queue(dev, head);
}

static const struct nla_policy gtp_policy[IFLA_GTP_MAX + 1] = {
	[IFLA_GTP_FD0]			= { .type = NLA_U32 },
	[IFLA_GTP_FD1]			= { .type = NLA_U32 },
	[IFLA_GTP_PDP_HASHSIZE]		= { .type = NLA_U32 },
	[IFLA_GTP_ROLE]			= { .type = NLA_U32 },
	[IFLA_GTP_CREATE_SOCKETS]	= { .type = NLA_U8 },
	[IFLA_GTP_RESTART_COUNT]	= { .type = NLA_U8 },
};

static int gtp_validate(struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	if (!data)
		return -EINVAL;

	return 0;
}

static size_t gtp_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(__u32)) + /* IFLA_GTP_PDP_HASHSIZE */
		nla_total_size(sizeof(__u32)) + /* IFLA_GTP_ROLE */
		nla_total_size(sizeof(__u8)); /* IFLA_GTP_RESTART_COUNT */
}

static int gtp_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct gtp_dev *gtp = netdev_priv(dev);

	if (nla_put_u32(skb, IFLA_GTP_PDP_HASHSIZE, gtp->hash_size))
		goto nla_put_failure;
	if (nla_put_u32(skb, IFLA_GTP_ROLE, gtp->role))
		goto nla_put_failure;
	if (nla_put_u8(skb, IFLA_GTP_RESTART_COUNT, gtp->restart_count))
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

static int gtp_hashtable_new(struct gtp_dev *gtp, int hsize)
{
	int i;

	gtp->addr_hash = kmalloc_array(hsize, sizeof(struct hlist_head),
				       GFP_KERNEL | __GFP_NOWARN);
	if (gtp->addr_hash == NULL)
		return -ENOMEM;

	gtp->tid_hash = kmalloc_array(hsize, sizeof(struct hlist_head),
				      GFP_KERNEL | __GFP_NOWARN);
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

static struct sock *gtp_encap_enable_socket(int fd, int type,
					    struct gtp_dev *gtp)
{
	struct udp_tunnel_sock_cfg tuncfg = {NULL};
	struct socket *sock;
	struct sock *sk;
	int err;

	pr_debug("enable gtp on %d, %d\n", fd, type);

	sock = sockfd_lookup(fd, &err);
	if (!sock) {
		pr_debug("gtp socket fd=%d not found\n", fd);
		return NULL;
	}

	sk = sock->sk;
	if (sk->sk_protocol != IPPROTO_UDP ||
	    sk->sk_type != SOCK_DGRAM ||
	    (sk->sk_family != AF_INET && sk->sk_family != AF_INET6)) {
		pr_debug("socket fd=%d not UDP\n", fd);
		sk = ERR_PTR(-EINVAL);
		goto out_sock;
	}

	lock_sock(sk);
	if (sk->sk_user_data) {
		sk = ERR_PTR(-EBUSY);
		goto out_rel_sock;
	}

	sock_hold(sk);

	tuncfg.sk_user_data = gtp;
	tuncfg.encap_type = type;
	tuncfg.encap_rcv = gtp_encap_recv;
	tuncfg.encap_destroy = gtp_encap_destroy;

	setup_udp_tunnel_sock(sock_net(sock->sk), sock, &tuncfg);

out_rel_sock:
	release_sock(sock->sk);
out_sock:
	sockfd_put(sock);
	return sk;
}

static int gtp_encap_enable(struct gtp_dev *gtp, struct nlattr *data[])
{
	struct sock *sk1u = NULL;
	struct sock *sk0 = NULL;

	if (!data[IFLA_GTP_FD0] && !data[IFLA_GTP_FD1])
		return -EINVAL;

	if (data[IFLA_GTP_FD0]) {
		u32 fd0 = nla_get_u32(data[IFLA_GTP_FD0]);

		sk0 = gtp_encap_enable_socket(fd0, UDP_ENCAP_GTP0, gtp);
		if (IS_ERR(sk0))
			return PTR_ERR(sk0);
	}

	if (data[IFLA_GTP_FD1]) {
		u32 fd1 = nla_get_u32(data[IFLA_GTP_FD1]);

		sk1u = gtp_encap_enable_socket(fd1, UDP_ENCAP_GTP1U, gtp);
		if (IS_ERR(sk1u)) {
			gtp_encap_disable_sock(sk0);
			return PTR_ERR(sk1u);
		}
	}

	gtp->sk0 = sk0;
	gtp->sk1u = sk1u;

	return 0;
}

static struct gtp_dev *gtp_find_dev(struct net *src_net, struct nlattr *nla[])
{
	struct gtp_dev *gtp = NULL;
	struct net_device *dev;
	struct net *net;

	/* Examine the link attributes and figure out which network namespace
	 * we are talking about.
	 */
	if (nla[GTPA_NET_NS_FD])
		net = get_net_ns_by_fd(nla_get_u32(nla[GTPA_NET_NS_FD]));
	else
		net = get_net(src_net);

	if (IS_ERR(net))
		return NULL;

	/* Check if there's an existing gtpX device to configure */
	dev = dev_get_by_index_rcu(net, nla_get_u32(nla[GTPA_LINK]));
	if (dev && dev->netdev_ops == &gtp_netdev_ops)
		gtp = netdev_priv(dev);

	put_net(net);
	return gtp;
}

static void ipv4_pdp_fill(struct pdp_ctx *pctx, struct genl_info *info)
{
	pctx->gtp_version = nla_get_u32(info->attrs[GTPA_VERSION]);
	pctx->af = AF_INET;
	pctx->peer_addr_ip4.s_addr =
		nla_get_be32(info->attrs[GTPA_PEER_ADDRESS]);
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

static struct pdp_ctx *gtp_pdp_add(struct gtp_dev *gtp, struct sock *sk,
				   struct genl_info *info)
{
	struct pdp_ctx *pctx, *pctx_tid = NULL;
	struct net_device *dev = gtp->dev;
	u32 hash_ms, hash_tid = 0;
	unsigned int version;
	bool found = false;
	__be32 ms_addr;

	ms_addr = nla_get_be32(info->attrs[GTPA_MS_ADDRESS]);
	hash_ms = ipv4_hashfn(ms_addr) % gtp->hash_size;
	version = nla_get_u32(info->attrs[GTPA_VERSION]);

	pctx = ipv4_pdp_find(gtp, ms_addr);
	if (pctx)
		found = true;
	if (version == GTP_V0)
		pctx_tid = gtp0_pdp_find(gtp,
					 nla_get_u64(info->attrs[GTPA_TID]));
	else if (version == GTP_V1)
		pctx_tid = gtp1_pdp_find(gtp,
					 nla_get_u32(info->attrs[GTPA_I_TEI]));
	if (pctx_tid)
		found = true;

	if (found) {
		if (info->nlhdr->nlmsg_flags & NLM_F_EXCL)
			return ERR_PTR(-EEXIST);
		if (info->nlhdr->nlmsg_flags & NLM_F_REPLACE)
			return ERR_PTR(-EOPNOTSUPP);

		if (pctx && pctx_tid)
			return ERR_PTR(-EEXIST);
		if (!pctx)
			pctx = pctx_tid;

		ipv4_pdp_fill(pctx, info);

		if (pctx->gtp_version == GTP_V0)
			netdev_dbg(dev, "GTPv0-U: update tunnel id = %llx (pdp %p)\n",
				   pctx->u.v0.tid, pctx);
		else if (pctx->gtp_version == GTP_V1)
			netdev_dbg(dev, "GTPv1-U: update tunnel id = %x/%x (pdp %p)\n",
				   pctx->u.v1.i_tei, pctx->u.v1.o_tei, pctx);

		return pctx;

	}

	pctx = kmalloc(sizeof(*pctx), GFP_ATOMIC);
	if (pctx == NULL)
		return ERR_PTR(-ENOMEM);

	sock_hold(sk);
	pctx->sk = sk;
	pctx->dev = gtp->dev;
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
			   pctx->u.v0.tid, &pctx->peer_addr_ip4,
			   &pctx->ms_addr_ip4, pctx);
		break;
	case GTP_V1:
		netdev_dbg(dev, "GTPv1-U: new PDP ctx id=%x/%x ssgn=%pI4 ms=%pI4 (pdp=%p)\n",
			   pctx->u.v1.i_tei, pctx->u.v1.o_tei,
			   &pctx->peer_addr_ip4, &pctx->ms_addr_ip4, pctx);
		break;
	}

	return pctx;
}

static void pdp_context_free(struct rcu_head *head)
{
	struct pdp_ctx *pctx = container_of(head, struct pdp_ctx, rcu_head);

	sock_put(pctx->sk);
	kfree(pctx);
}

static void pdp_context_delete(struct pdp_ctx *pctx)
{
	hlist_del_rcu(&pctx->hlist_tid);
	hlist_del_rcu(&pctx->hlist_addr);
	call_rcu(&pctx->rcu_head, pdp_context_free);
}

static int gtp_tunnel_notify(struct pdp_ctx *pctx, u8 cmd, gfp_t allocation);

static int gtp_genl_new_pdp(struct sk_buff *skb, struct genl_info *info)
{
	unsigned int version;
	struct pdp_ctx *pctx;
	struct gtp_dev *gtp;
	struct sock *sk;
	int err;

	if (!info->attrs[GTPA_VERSION] ||
	    !info->attrs[GTPA_LINK] ||
	    !info->attrs[GTPA_PEER_ADDRESS] ||
	    !info->attrs[GTPA_MS_ADDRESS])
		return -EINVAL;

	version = nla_get_u32(info->attrs[GTPA_VERSION]);

	switch (version) {
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

	rtnl_lock();

	gtp = gtp_find_dev(sock_net(skb->sk), info->attrs);
	if (!gtp) {
		err = -ENODEV;
		goto out_unlock;
	}

	if (version == GTP_V0)
		sk = gtp->sk0;
	else if (version == GTP_V1)
		sk = gtp->sk1u;
	else
		sk = NULL;

	if (!sk) {
		err = -ENODEV;
		goto out_unlock;
	}

	pctx = gtp_pdp_add(gtp, sk, info);
	if (IS_ERR(pctx)) {
		err = PTR_ERR(pctx);
	} else {
		gtp_tunnel_notify(pctx, GTP_CMD_NEWPDP, GFP_KERNEL);
		err = 0;
	}

out_unlock:
	rtnl_unlock();
	return err;
}

static struct pdp_ctx *gtp_find_pdp_by_link(struct net *net,
					    struct nlattr *nla[])
{
	struct gtp_dev *gtp;

	gtp = gtp_find_dev(net, nla);
	if (!gtp)
		return ERR_PTR(-ENODEV);

	if (nla[GTPA_MS_ADDRESS]) {
		__be32 ip = nla_get_be32(nla[GTPA_MS_ADDRESS]);

		return ipv4_pdp_find(gtp, ip);
	} else if (nla[GTPA_VERSION]) {
		u32 gtp_version = nla_get_u32(nla[GTPA_VERSION]);

		if (gtp_version == GTP_V0 && nla[GTPA_TID])
			return gtp0_pdp_find(gtp, nla_get_u64(nla[GTPA_TID]));
		else if (gtp_version == GTP_V1 && nla[GTPA_I_TEI])
			return gtp1_pdp_find(gtp, nla_get_u32(nla[GTPA_I_TEI]));
	}

	return ERR_PTR(-EINVAL);
}

static struct pdp_ctx *gtp_find_pdp(struct net *net, struct nlattr *nla[])
{
	struct pdp_ctx *pctx;

	if (nla[GTPA_LINK])
		pctx = gtp_find_pdp_by_link(net, nla);
	else
		pctx = ERR_PTR(-EINVAL);

	if (!pctx)
		pctx = ERR_PTR(-ENOENT);

	return pctx;
}

static int gtp_genl_del_pdp(struct sk_buff *skb, struct genl_info *info)
{
	struct pdp_ctx *pctx;
	int err = 0;

	if (!info->attrs[GTPA_VERSION])
		return -EINVAL;

	rcu_read_lock();

	pctx = gtp_find_pdp(sock_net(skb->sk), info->attrs);
	if (IS_ERR(pctx)) {
		err = PTR_ERR(pctx);
		goto out_unlock;
	}

	if (pctx->gtp_version == GTP_V0)
		netdev_dbg(pctx->dev, "GTPv0-U: deleting tunnel id = %llx (pdp %p)\n",
			   pctx->u.v0.tid, pctx);
	else if (pctx->gtp_version == GTP_V1)
		netdev_dbg(pctx->dev, "GTPv1-U: deleting tunnel id = %x/%x (pdp %p)\n",
			   pctx->u.v1.i_tei, pctx->u.v1.o_tei, pctx);

	gtp_tunnel_notify(pctx, GTP_CMD_DELPDP, GFP_ATOMIC);
	pdp_context_delete(pctx);

out_unlock:
	rcu_read_unlock();
	return err;
}

static int gtp_genl_fill_info(struct sk_buff *skb, u32 snd_portid, u32 snd_seq,
			      int flags, u32 type, struct pdp_ctx *pctx)
{
	void *genlh;

	genlh = genlmsg_put(skb, snd_portid, snd_seq, &gtp_genl_family, flags,
			    type);
	if (genlh == NULL)
		goto nlmsg_failure;

	if (nla_put_u32(skb, GTPA_VERSION, pctx->gtp_version) ||
	    nla_put_u32(skb, GTPA_LINK, pctx->dev->ifindex) ||
	    nla_put_be32(skb, GTPA_PEER_ADDRESS, pctx->peer_addr_ip4.s_addr) ||
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

static int gtp_tunnel_notify(struct pdp_ctx *pctx, u8 cmd, gfp_t allocation)
{
	struct sk_buff *msg;
	int ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, allocation);
	if (!msg)
		return -ENOMEM;

	ret = gtp_genl_fill_info(msg, 0, 0, 0, cmd, pctx);
	if (ret < 0) {
		nlmsg_free(msg);
		return ret;
	}

	ret = genlmsg_multicast_netns(&gtp_genl_family, dev_net(pctx->dev), msg,
				      0, GTP_GENL_MCGRP, GFP_ATOMIC);
	return ret;
}

static int gtp_genl_get_pdp(struct sk_buff *skb, struct genl_info *info)
{
	struct pdp_ctx *pctx = NULL;
	struct sk_buff *skb2;
	int err;

	if (!info->attrs[GTPA_VERSION])
		return -EINVAL;

	rcu_read_lock();

	pctx = gtp_find_pdp(sock_net(skb->sk), info->attrs);
	if (IS_ERR(pctx)) {
		err = PTR_ERR(pctx);
		goto err_unlock;
	}

	skb2 = genlmsg_new(NLMSG_GOODSIZE, GFP_ATOMIC);
	if (skb2 == NULL) {
		err = -ENOMEM;
		goto err_unlock;
	}

	err = gtp_genl_fill_info(skb2, NETLINK_CB(skb).portid, info->snd_seq,
				 0, info->nlhdr->nlmsg_type, pctx);
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
	int i, j, bucket = cb->args[0], skip = cb->args[1];
	struct net *net = sock_net(skb->sk);
	struct pdp_ctx *pctx;
	struct gtp_net *gn;

	gn = net_generic(net, gtp_net_id);

	if (cb->args[4])
		return 0;

	rcu_read_lock();
	list_for_each_entry_rcu(gtp, &gn->gtp_dev_list, list) {
		if (last_gtp && last_gtp != gtp)
			continue;
		else
			last_gtp = NULL;

		for (i = bucket; i < gtp->hash_size; i++) {
			j = 0;
			hlist_for_each_entry_rcu(pctx, &gtp->tid_hash[i],
						 hlist_tid) {
				if (j >= skip &&
				    gtp_genl_fill_info(skb,
					    NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq,
					    NLM_F_MULTI,
					    cb->nlh->nlmsg_type, pctx)) {
					cb->args[0] = i;
					cb->args[1] = j;
					cb->args[2] = (unsigned long)gtp;
					goto out;
				}
				j++;
			}
			skip = 0;
		}
		bucket = 0;
	}
	cb->args[4] = 1;
out:
	rcu_read_unlock();
	return skb->len;
}

static int gtp_genl_send_echo_req(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *skb_to_send;
	__be32 src_ip, dst_ip;
	unsigned int version;
	struct gtp_dev *gtp;
	struct flowi4 fl4;
	struct rtable *rt;
	struct sock *sk;
	__be16 port;
	int len;

	if (!info->attrs[GTPA_VERSION] ||
	    !info->attrs[GTPA_LINK] ||
	    !info->attrs[GTPA_PEER_ADDRESS] ||
	    !info->attrs[GTPA_MS_ADDRESS])
		return -EINVAL;

	version = nla_get_u32(info->attrs[GTPA_VERSION]);
	dst_ip = nla_get_be32(info->attrs[GTPA_PEER_ADDRESS]);
	src_ip = nla_get_be32(info->attrs[GTPA_MS_ADDRESS]);

	gtp = gtp_find_dev(sock_net(skb->sk), info->attrs);
	if (!gtp)
		return -ENODEV;

	if (!gtp->sk_created)
		return -EOPNOTSUPP;
	if (!(gtp->dev->flags & IFF_UP))
		return -ENETDOWN;

	if (version == GTP_V0) {
		struct gtp0_header *gtp0_h;

		len = LL_RESERVED_SPACE(gtp->dev) + sizeof(struct gtp0_header) +
			sizeof(struct iphdr) + sizeof(struct udphdr);

		skb_to_send = netdev_alloc_skb_ip_align(gtp->dev, len);
		if (!skb_to_send)
			return -ENOMEM;

		sk = gtp->sk0;
		port = htons(GTP0_PORT);

		gtp0_h = skb_push(skb_to_send, sizeof(struct gtp0_header));
		memset(gtp0_h, 0, sizeof(struct gtp0_header));
		gtp0_build_echo_msg(gtp0_h, GTP_ECHO_REQ);
	} else if (version == GTP_V1) {
		struct gtp1_header_long *gtp1u_h;

		len = LL_RESERVED_SPACE(gtp->dev) +
			sizeof(struct gtp1_header_long) +
			sizeof(struct iphdr) + sizeof(struct udphdr);

		skb_to_send = netdev_alloc_skb_ip_align(gtp->dev, len);
		if (!skb_to_send)
			return -ENOMEM;

		sk = gtp->sk1u;
		port = htons(GTP1U_PORT);

		gtp1u_h = skb_push(skb_to_send,
				   sizeof(struct gtp1_header_long));
		memset(gtp1u_h, 0, sizeof(struct gtp1_header_long));
		gtp1u_build_echo_msg(gtp1u_h, GTP_ECHO_REQ);
	} else {
		return -ENODEV;
	}

	rt = ip4_route_output_gtp(&fl4, sk, dst_ip, src_ip);
	if (IS_ERR(rt)) {
		netdev_dbg(gtp->dev, "no route for echo request to %pI4\n",
			   &dst_ip);
		kfree_skb(skb_to_send);
		return -ENODEV;
	}

	udp_tunnel_xmit_skb(rt, sk, skb_to_send,
			    fl4.saddr, fl4.daddr,
			    fl4.flowi4_tos,
			    ip4_dst_hoplimit(&rt->dst),
			    0,
			    port, port,
			    !net_eq(sock_net(sk),
				    dev_net(gtp->dev)),
			    false);
	return 0;
}

static const struct nla_policy gtp_genl_policy[GTPA_MAX + 1] = {
	[GTPA_LINK]		= { .type = NLA_U32, },
	[GTPA_VERSION]		= { .type = NLA_U32, },
	[GTPA_TID]		= { .type = NLA_U64, },
	[GTPA_PEER_ADDRESS]	= { .type = NLA_U32, },
	[GTPA_MS_ADDRESS]	= { .type = NLA_U32, },
	[GTPA_FLOW]		= { .type = NLA_U16, },
	[GTPA_NET_NS_FD]	= { .type = NLA_U32, },
	[GTPA_I_TEI]		= { .type = NLA_U32, },
	[GTPA_O_TEI]		= { .type = NLA_U32, },
};

static const struct genl_small_ops gtp_genl_ops[] = {
	{
		.cmd = GTP_CMD_NEWPDP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = gtp_genl_new_pdp,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = GTP_CMD_DELPDP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = gtp_genl_del_pdp,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = GTP_CMD_GETPDP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = gtp_genl_get_pdp,
		.dumpit = gtp_genl_dump_pdp,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = GTP_CMD_ECHOREQ,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = gtp_genl_send_echo_req,
		.flags = GENL_ADMIN_PERM,
	},
};

static struct genl_family gtp_genl_family __ro_after_init = {
	.name		= "gtp",
	.version	= 0,
	.hdrsize	= 0,
	.maxattr	= GTPA_MAX,
	.policy = gtp_genl_policy,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.small_ops	= gtp_genl_ops,
	.n_small_ops	= ARRAY_SIZE(gtp_genl_ops),
	.resv_start_op	= GTP_CMD_ECHOREQ + 1,
	.mcgrps		= gtp_genl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(gtp_genl_mcgrps),
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

	err = register_pernet_subsys(&gtp_net_ops);
	if (err < 0)
		goto error_out;

	err = rtnl_link_register(&gtp_link_ops);
	if (err < 0)
		goto unreg_pernet_subsys;

	err = genl_register_family(&gtp_genl_family);
	if (err < 0)
		goto unreg_rtnl_link;

	pr_info("GTP module loaded (pdp ctx size %zd bytes)\n",
		sizeof(struct pdp_ctx));
	return 0;

unreg_rtnl_link:
	rtnl_link_unregister(&gtp_link_ops);
unreg_pernet_subsys:
	unregister_pernet_subsys(&gtp_net_ops);
error_out:
	pr_err("error loading GTP module loaded\n");
	return err;
}
late_initcall(gtp_init);

static void __exit gtp_fini(void)
{
	genl_unregister_family(&gtp_genl_family);
	rtnl_link_unregister(&gtp_link_ops);
	unregister_pernet_subsys(&gtp_net_ops);

	pr_info("GTP module unloaded\n");
}
module_exit(gtp_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <hwelte@sysmocom.de>");
MODULE_DESCRIPTION("Interface driver for GTP encapsulated traffic");
MODULE_ALIAS_RTNL_LINK("gtp");
MODULE_ALIAS_GENL_FAMILY("gtp");
