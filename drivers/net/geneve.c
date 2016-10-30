/*
 * GENEVE: Generic Network Virtualization Encapsulation
 *
 * Copyright (c) 2015 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/hash.h>
#include <net/dst_metadata.h>
#include <net/gro_cells.h>
#include <net/rtnetlink.h>
#include <net/geneve.h>
#include <net/protocol.h>

#define GENEVE_NETDEV_VER	"0.6"

#define GENEVE_UDP_PORT		6081

#define GENEVE_N_VID		(1u << 24)
#define GENEVE_VID_MASK		(GENEVE_N_VID - 1)

#define VNI_HASH_BITS		10
#define VNI_HASH_SIZE		(1<<VNI_HASH_BITS)

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

#define GENEVE_VER 0
#define GENEVE_BASE_HLEN (sizeof(struct udphdr) + sizeof(struct genevehdr))

/* per-network namespace private data for this module */
struct geneve_net {
	struct list_head	geneve_list;
	struct list_head	sock_list;
};

static int geneve_net_id;

union geneve_addr {
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
	struct sockaddr sa;
};

static union geneve_addr geneve_remote_unspec = { .sa.sa_family = AF_UNSPEC, };

/* Pseudo network device */
struct geneve_dev {
	struct hlist_node  hlist;	/* vni hash table */
	struct net	   *net;	/* netns for packet i/o */
	struct net_device  *dev;	/* netdev for geneve tunnel */
	struct geneve_sock __rcu *sock4;	/* IPv4 socket used for geneve tunnel */
#if IS_ENABLED(CONFIG_IPV6)
	struct geneve_sock __rcu *sock6;	/* IPv6 socket used for geneve tunnel */
#endif
	u8                 vni[3];	/* virtual network ID for tunnel */
	u8                 ttl;		/* TTL override */
	u8                 tos;		/* TOS override */
	union geneve_addr  remote;	/* IP address for link partner */
	struct list_head   next;	/* geneve's per namespace list */
	__be32		   label;	/* IPv6 flowlabel override */
	__be16		   dst_port;
	bool		   collect_md;
	struct gro_cells   gro_cells;
	u32		   flags;
	struct dst_cache   dst_cache;
};

/* Geneve device flags */
#define GENEVE_F_UDP_ZERO_CSUM_TX	BIT(0)
#define GENEVE_F_UDP_ZERO_CSUM6_TX	BIT(1)
#define GENEVE_F_UDP_ZERO_CSUM6_RX	BIT(2)

struct geneve_sock {
	bool			collect_md;
	struct list_head	list;
	struct socket		*sock;
	struct rcu_head		rcu;
	int			refcnt;
	struct hlist_head	vni_list[VNI_HASH_SIZE];
	u32			flags;
};

static inline __u32 geneve_net_vni_hash(u8 vni[3])
{
	__u32 vnid;

	vnid = (vni[0] << 16) | (vni[1] << 8) | vni[2];
	return hash_32(vnid, VNI_HASH_BITS);
}

static __be64 vni_to_tunnel_id(const __u8 *vni)
{
#ifdef __BIG_ENDIAN
	return (vni[0] << 16) | (vni[1] << 8) | vni[2];
#else
	return (__force __be64)(((__force u64)vni[0] << 40) |
				((__force u64)vni[1] << 48) |
				((__force u64)vni[2] << 56));
#endif
}

static sa_family_t geneve_get_sk_family(struct geneve_sock *gs)
{
	return gs->sock->sk->sk_family;
}

static struct geneve_dev *geneve_lookup(struct geneve_sock *gs,
					__be32 addr, u8 vni[])
{
	struct hlist_head *vni_list_head;
	struct geneve_dev *geneve;
	__u32 hash;

	/* Find the device for this VNI */
	hash = geneve_net_vni_hash(vni);
	vni_list_head = &gs->vni_list[hash];
	hlist_for_each_entry_rcu(geneve, vni_list_head, hlist) {
		if (!memcmp(vni, geneve->vni, sizeof(geneve->vni)) &&
		    addr == geneve->remote.sin.sin_addr.s_addr)
			return geneve;
	}
	return NULL;
}

#if IS_ENABLED(CONFIG_IPV6)
static struct geneve_dev *geneve6_lookup(struct geneve_sock *gs,
					 struct in6_addr addr6, u8 vni[])
{
	struct hlist_head *vni_list_head;
	struct geneve_dev *geneve;
	__u32 hash;

	/* Find the device for this VNI */
	hash = geneve_net_vni_hash(vni);
	vni_list_head = &gs->vni_list[hash];
	hlist_for_each_entry_rcu(geneve, vni_list_head, hlist) {
		if (!memcmp(vni, geneve->vni, sizeof(geneve->vni)) &&
		    ipv6_addr_equal(&addr6, &geneve->remote.sin6.sin6_addr))
			return geneve;
	}
	return NULL;
}
#endif

static inline struct genevehdr *geneve_hdr(const struct sk_buff *skb)
{
	return (struct genevehdr *)(udp_hdr(skb) + 1);
}

static struct geneve_dev *geneve_lookup_skb(struct geneve_sock *gs,
					    struct sk_buff *skb)
{
	u8 *vni;
	__be32 addr;
	static u8 zero_vni[3];
#if IS_ENABLED(CONFIG_IPV6)
	static struct in6_addr zero_addr6;
#endif

	if (geneve_get_sk_family(gs) == AF_INET) {
		struct iphdr *iph;

		iph = ip_hdr(skb); /* outer IP header... */

		if (gs->collect_md) {
			vni = zero_vni;
			addr = 0;
		} else {
			vni = geneve_hdr(skb)->vni;
			addr = iph->saddr;
		}

		return geneve_lookup(gs, addr, vni);
#if IS_ENABLED(CONFIG_IPV6)
	} else if (geneve_get_sk_family(gs) == AF_INET6) {
		struct ipv6hdr *ip6h;
		struct in6_addr addr6;

		ip6h = ipv6_hdr(skb); /* outer IPv6 header... */

		if (gs->collect_md) {
			vni = zero_vni;
			addr6 = zero_addr6;
		} else {
			vni = geneve_hdr(skb)->vni;
			addr6 = ip6h->saddr;
		}

		return geneve6_lookup(gs, addr6, vni);
#endif
	}
	return NULL;
}

/* geneve receive/decap routine */
static void geneve_rx(struct geneve_dev *geneve, struct geneve_sock *gs,
		      struct sk_buff *skb)
{
	struct genevehdr *gnvh = geneve_hdr(skb);
	struct metadata_dst *tun_dst = NULL;
	struct pcpu_sw_netstats *stats;
	int err = 0;
	void *oiph;

	if (ip_tunnel_collect_metadata() || gs->collect_md) {
		__be16 flags;

		flags = TUNNEL_KEY | TUNNEL_GENEVE_OPT |
			(gnvh->oam ? TUNNEL_OAM : 0) |
			(gnvh->critical ? TUNNEL_CRIT_OPT : 0);

		tun_dst = udp_tun_rx_dst(skb, geneve_get_sk_family(gs), flags,
					 vni_to_tunnel_id(gnvh->vni),
					 gnvh->opt_len * 4);
		if (!tun_dst)
			goto drop;
		/* Update tunnel dst according to Geneve options. */
		ip_tunnel_info_opts_set(&tun_dst->u.tun_info,
					gnvh->options, gnvh->opt_len * 4);
	} else {
		/* Drop packets w/ critical options,
		 * since we don't support any...
		 */
		if (gnvh->critical)
			goto drop;
	}

	skb_reset_mac_header(skb);
	skb->protocol = eth_type_trans(skb, geneve->dev);
	skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);

	if (tun_dst)
		skb_dst_set(skb, &tun_dst->dst);

	/* Ignore packet loops (and multicast echo) */
	if (ether_addr_equal(eth_hdr(skb)->h_source, geneve->dev->dev_addr))
		goto drop;

	oiph = skb_network_header(skb);
	skb_reset_network_header(skb);

	if (geneve_get_sk_family(gs) == AF_INET)
		err = IP_ECN_decapsulate(oiph, skb);
#if IS_ENABLED(CONFIG_IPV6)
	else
		err = IP6_ECN_decapsulate(oiph, skb);
#endif

	if (unlikely(err)) {
		if (log_ecn_error) {
			if (geneve_get_sk_family(gs) == AF_INET)
				net_info_ratelimited("non-ECT from %pI4 "
						     "with TOS=%#x\n",
						     &((struct iphdr *)oiph)->saddr,
						     ((struct iphdr *)oiph)->tos);
#if IS_ENABLED(CONFIG_IPV6)
			else
				net_info_ratelimited("non-ECT from %pI6\n",
						     &((struct ipv6hdr *)oiph)->saddr);
#endif
		}
		if (err > 1) {
			++geneve->dev->stats.rx_frame_errors;
			++geneve->dev->stats.rx_errors;
			goto drop;
		}
	}

	stats = this_cpu_ptr(geneve->dev->tstats);
	u64_stats_update_begin(&stats->syncp);
	stats->rx_packets++;
	stats->rx_bytes += skb->len;
	u64_stats_update_end(&stats->syncp);

	gro_cells_receive(&geneve->gro_cells, skb);
	return;
drop:
	/* Consume bad packet */
	kfree_skb(skb);
}

/* Setup stats when device is created */
static int geneve_init(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	int err;

	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;

	err = gro_cells_init(&geneve->gro_cells, dev);
	if (err) {
		free_percpu(dev->tstats);
		return err;
	}

	err = dst_cache_init(&geneve->dst_cache, GFP_KERNEL);
	if (err) {
		free_percpu(dev->tstats);
		gro_cells_destroy(&geneve->gro_cells);
		return err;
	}

	return 0;
}

static void geneve_uninit(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);

	dst_cache_destroy(&geneve->dst_cache);
	gro_cells_destroy(&geneve->gro_cells);
	free_percpu(dev->tstats);
}

/* Callback from net/ipv4/udp.c to receive packets */
static int geneve_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct genevehdr *geneveh;
	struct geneve_dev *geneve;
	struct geneve_sock *gs;
	int opts_len;

	/* Need Geneve and inner Ethernet header to be present */
	if (unlikely(!pskb_may_pull(skb, GENEVE_BASE_HLEN)))
		goto drop;

	/* Return packets with reserved bits set */
	geneveh = geneve_hdr(skb);
	if (unlikely(geneveh->ver != GENEVE_VER))
		goto drop;

	if (unlikely(geneveh->proto_type != htons(ETH_P_TEB)))
		goto drop;

	gs = rcu_dereference_sk_user_data(sk);
	if (!gs)
		goto drop;

	geneve = geneve_lookup_skb(gs, skb);
	if (!geneve)
		goto drop;

	opts_len = geneveh->opt_len * 4;
	if (iptunnel_pull_header(skb, GENEVE_BASE_HLEN + opts_len,
				 htons(ETH_P_TEB),
				 !net_eq(geneve->net, dev_net(geneve->dev))))
		goto drop;

	geneve_rx(geneve, gs, skb);
	return 0;

drop:
	/* Consume bad packet */
	kfree_skb(skb);
	return 0;
}

static struct socket *geneve_create_sock(struct net *net, bool ipv6,
					 __be16 port, u32 flags)
{
	struct socket *sock;
	struct udp_port_cfg udp_conf;
	int err;

	memset(&udp_conf, 0, sizeof(udp_conf));

	if (ipv6) {
		udp_conf.family = AF_INET6;
		udp_conf.ipv6_v6only = 1;
		udp_conf.use_udp6_rx_checksums =
		    !(flags & GENEVE_F_UDP_ZERO_CSUM6_RX);
	} else {
		udp_conf.family = AF_INET;
		udp_conf.local_ip.s_addr = htonl(INADDR_ANY);
	}

	udp_conf.local_udp_port = port;

	/* Open UDP socket */
	err = udp_sock_create(net, &udp_conf, &sock);
	if (err < 0)
		return ERR_PTR(err);

	return sock;
}

static int geneve_hlen(struct genevehdr *gh)
{
	return sizeof(*gh) + gh->opt_len * 4;
}

static struct sk_buff **geneve_gro_receive(struct sock *sk,
					   struct sk_buff **head,
					   struct sk_buff *skb)
{
	struct sk_buff *p, **pp = NULL;
	struct genevehdr *gh, *gh2;
	unsigned int hlen, gh_len, off_gnv;
	const struct packet_offload *ptype;
	__be16 type;
	int flush = 1;

	off_gnv = skb_gro_offset(skb);
	hlen = off_gnv + sizeof(*gh);
	gh = skb_gro_header_fast(skb, off_gnv);
	if (skb_gro_header_hard(skb, hlen)) {
		gh = skb_gro_header_slow(skb, hlen, off_gnv);
		if (unlikely(!gh))
			goto out;
	}

	if (gh->ver != GENEVE_VER || gh->oam)
		goto out;
	gh_len = geneve_hlen(gh);

	hlen = off_gnv + gh_len;
	if (skb_gro_header_hard(skb, hlen)) {
		gh = skb_gro_header_slow(skb, hlen, off_gnv);
		if (unlikely(!gh))
			goto out;
	}

	for (p = *head; p; p = p->next) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		gh2 = (struct genevehdr *)(p->data + off_gnv);
		if (gh->opt_len != gh2->opt_len ||
		    memcmp(gh, gh2, gh_len)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	type = gh->proto_type;

	rcu_read_lock();
	ptype = gro_find_receive_by_type(type);
	if (!ptype)
		goto out_unlock;

	skb_gro_pull(skb, gh_len);
	skb_gro_postpull_rcsum(skb, gh, gh_len);
	pp = call_gro_receive(ptype->callbacks.gro_receive, head, skb);
	flush = 0;

out_unlock:
	rcu_read_unlock();
out:
	NAPI_GRO_CB(skb)->flush |= flush;

	return pp;
}

static int geneve_gro_complete(struct sock *sk, struct sk_buff *skb,
			       int nhoff)
{
	struct genevehdr *gh;
	struct packet_offload *ptype;
	__be16 type;
	int gh_len;
	int err = -ENOSYS;

	gh = (struct genevehdr *)(skb->data + nhoff);
	gh_len = geneve_hlen(gh);
	type = gh->proto_type;

	rcu_read_lock();
	ptype = gro_find_complete_by_type(type);
	if (ptype)
		err = ptype->callbacks.gro_complete(skb, nhoff + gh_len);

	rcu_read_unlock();

	skb_set_inner_mac_header(skb, nhoff + gh_len);

	return err;
}

/* Create new listen socket if needed */
static struct geneve_sock *geneve_socket_create(struct net *net, __be16 port,
						bool ipv6, u32 flags)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_sock *gs;
	struct socket *sock;
	struct udp_tunnel_sock_cfg tunnel_cfg;
	int h;

	gs = kzalloc(sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return ERR_PTR(-ENOMEM);

	sock = geneve_create_sock(net, ipv6, port, flags);
	if (IS_ERR(sock)) {
		kfree(gs);
		return ERR_CAST(sock);
	}

	gs->sock = sock;
	gs->refcnt = 1;
	for (h = 0; h < VNI_HASH_SIZE; ++h)
		INIT_HLIST_HEAD(&gs->vni_list[h]);

	/* Initialize the geneve udp offloads structure */
	udp_tunnel_notify_add_rx_port(gs->sock, UDP_TUNNEL_TYPE_GENEVE);

	/* Mark socket as an encapsulation socket */
	memset(&tunnel_cfg, 0, sizeof(tunnel_cfg));
	tunnel_cfg.sk_user_data = gs;
	tunnel_cfg.encap_type = 1;
	tunnel_cfg.gro_receive = geneve_gro_receive;
	tunnel_cfg.gro_complete = geneve_gro_complete;
	tunnel_cfg.encap_rcv = geneve_udp_encap_recv;
	tunnel_cfg.encap_destroy = NULL;
	setup_udp_tunnel_sock(net, sock, &tunnel_cfg);
	list_add(&gs->list, &gn->sock_list);
	return gs;
}

static void __geneve_sock_release(struct geneve_sock *gs)
{
	if (!gs || --gs->refcnt)
		return;

	list_del(&gs->list);
	udp_tunnel_notify_del_rx_port(gs->sock, UDP_TUNNEL_TYPE_GENEVE);
	udp_tunnel_sock_release(gs->sock);
	kfree_rcu(gs, rcu);
}

static void geneve_sock_release(struct geneve_dev *geneve)
{
	struct geneve_sock *gs4 = rtnl_dereference(geneve->sock4);
#if IS_ENABLED(CONFIG_IPV6)
	struct geneve_sock *gs6 = rtnl_dereference(geneve->sock6);

	rcu_assign_pointer(geneve->sock6, NULL);
#endif

	rcu_assign_pointer(geneve->sock4, NULL);
	synchronize_net();

	__geneve_sock_release(gs4);
#if IS_ENABLED(CONFIG_IPV6)
	__geneve_sock_release(gs6);
#endif
}

static struct geneve_sock *geneve_find_sock(struct geneve_net *gn,
					    sa_family_t family,
					    __be16 dst_port)
{
	struct geneve_sock *gs;

	list_for_each_entry(gs, &gn->sock_list, list) {
		if (inet_sk(gs->sock->sk)->inet_sport == dst_port &&
		    geneve_get_sk_family(gs) == family) {
			return gs;
		}
	}
	return NULL;
}

static int geneve_sock_add(struct geneve_dev *geneve, bool ipv6)
{
	struct net *net = geneve->net;
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_sock *gs;
	__u32 hash;

	gs = geneve_find_sock(gn, ipv6 ? AF_INET6 : AF_INET, geneve->dst_port);
	if (gs) {
		gs->refcnt++;
		goto out;
	}

	gs = geneve_socket_create(net, geneve->dst_port, ipv6, geneve->flags);
	if (IS_ERR(gs))
		return PTR_ERR(gs);

out:
	gs->collect_md = geneve->collect_md;
	gs->flags = geneve->flags;
#if IS_ENABLED(CONFIG_IPV6)
	if (ipv6)
		rcu_assign_pointer(geneve->sock6, gs);
	else
#endif
		rcu_assign_pointer(geneve->sock4, gs);

	hash = geneve_net_vni_hash(geneve->vni);
	hlist_add_head_rcu(&geneve->hlist, &gs->vni_list[hash]);
	return 0;
}

static int geneve_open(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	bool ipv6 = geneve->remote.sa.sa_family == AF_INET6;
	bool metadata = geneve->collect_md;
	int ret = 0;

#if IS_ENABLED(CONFIG_IPV6)
	if (ipv6 || metadata)
		ret = geneve_sock_add(geneve, true);
#endif
	if (!ret && (!ipv6 || metadata))
		ret = geneve_sock_add(geneve, false);
	if (ret < 0)
		geneve_sock_release(geneve);

	return ret;
}

static int geneve_stop(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);

	if (!hlist_unhashed(&geneve->hlist))
		hlist_del_rcu(&geneve->hlist);
	geneve_sock_release(geneve);
	return 0;
}

static void geneve_build_header(struct genevehdr *geneveh,
				__be16 tun_flags, u8 vni[3],
				u8 options_len, u8 *options)
{
	geneveh->ver = GENEVE_VER;
	geneveh->opt_len = options_len / 4;
	geneveh->oam = !!(tun_flags & TUNNEL_OAM);
	geneveh->critical = !!(tun_flags & TUNNEL_CRIT_OPT);
	geneveh->rsvd1 = 0;
	memcpy(geneveh->vni, vni, 3);
	geneveh->proto_type = htons(ETH_P_TEB);
	geneveh->rsvd2 = 0;

	memcpy(geneveh->options, options, options_len);
}

static int geneve_build_skb(struct rtable *rt, struct sk_buff *skb,
			    __be16 tun_flags, u8 vni[3], u8 opt_len, u8 *opt,
			    u32 flags, bool xnet)
{
	struct genevehdr *gnvh;
	int min_headroom;
	int err;
	bool udp_sum = !(flags & GENEVE_F_UDP_ZERO_CSUM_TX);

	skb_scrub_packet(skb, xnet);

	min_headroom = LL_RESERVED_SPACE(rt->dst.dev) + rt->dst.header_len
			+ GENEVE_BASE_HLEN + opt_len + sizeof(struct iphdr);
	err = skb_cow_head(skb, min_headroom);
	if (unlikely(err))
		goto free_rt;

	err = udp_tunnel_handle_offloads(skb, udp_sum);
	if (err)
		goto free_rt;

	gnvh = (struct genevehdr *)__skb_push(skb, sizeof(*gnvh) + opt_len);
	geneve_build_header(gnvh, tun_flags, vni, opt_len, opt);

	skb_set_inner_protocol(skb, htons(ETH_P_TEB));
	return 0;

free_rt:
	ip_rt_put(rt);
	return err;
}

#if IS_ENABLED(CONFIG_IPV6)
static int geneve6_build_skb(struct dst_entry *dst, struct sk_buff *skb,
			     __be16 tun_flags, u8 vni[3], u8 opt_len, u8 *opt,
			     u32 flags, bool xnet)
{
	struct genevehdr *gnvh;
	int min_headroom;
	int err;
	bool udp_sum = !(flags & GENEVE_F_UDP_ZERO_CSUM6_TX);

	skb_scrub_packet(skb, xnet);

	min_headroom = LL_RESERVED_SPACE(dst->dev) + dst->header_len
			+ GENEVE_BASE_HLEN + opt_len + sizeof(struct ipv6hdr);
	err = skb_cow_head(skb, min_headroom);
	if (unlikely(err))
		goto free_dst;

	err = udp_tunnel_handle_offloads(skb, udp_sum);
	if (err)
		goto free_dst;

	gnvh = (struct genevehdr *)__skb_push(skb, sizeof(*gnvh) + opt_len);
	geneve_build_header(gnvh, tun_flags, vni, opt_len, opt);

	skb_set_inner_protocol(skb, htons(ETH_P_TEB));
	return 0;

free_dst:
	dst_release(dst);
	return err;
}
#endif

static struct rtable *geneve_get_v4_rt(struct sk_buff *skb,
				       struct net_device *dev,
				       struct flowi4 *fl4,
				       struct ip_tunnel_info *info)
{
	bool use_cache = ip_tunnel_dst_cache_usable(skb, info);
	struct geneve_dev *geneve = netdev_priv(dev);
	struct dst_cache *dst_cache;
	struct rtable *rt = NULL;
	__u8 tos;

	if (!rcu_dereference(geneve->sock4))
		return ERR_PTR(-EIO);

	memset(fl4, 0, sizeof(*fl4));
	fl4->flowi4_mark = skb->mark;
	fl4->flowi4_proto = IPPROTO_UDP;

	if (info) {
		fl4->daddr = info->key.u.ipv4.dst;
		fl4->saddr = info->key.u.ipv4.src;
		fl4->flowi4_tos = RT_TOS(info->key.tos);
		dst_cache = &info->dst_cache;
	} else {
		tos = geneve->tos;
		if (tos == 1) {
			const struct iphdr *iip = ip_hdr(skb);

			tos = ip_tunnel_get_dsfield(iip, skb);
			use_cache = false;
		}

		fl4->flowi4_tos = RT_TOS(tos);
		fl4->daddr = geneve->remote.sin.sin_addr.s_addr;
		dst_cache = &geneve->dst_cache;
	}

	if (use_cache) {
		rt = dst_cache_get_ip4(dst_cache, &fl4->saddr);
		if (rt)
			return rt;
	}

	rt = ip_route_output_key(geneve->net, fl4);
	if (IS_ERR(rt)) {
		netdev_dbg(dev, "no route to %pI4\n", &fl4->daddr);
		return ERR_PTR(-ENETUNREACH);
	}
	if (rt->dst.dev == dev) { /* is this necessary? */
		netdev_dbg(dev, "circular route to %pI4\n", &fl4->daddr);
		ip_rt_put(rt);
		return ERR_PTR(-ELOOP);
	}
	if (use_cache)
		dst_cache_set_ip4(dst_cache, &rt->dst, fl4->saddr);
	return rt;
}

#if IS_ENABLED(CONFIG_IPV6)
static struct dst_entry *geneve_get_v6_dst(struct sk_buff *skb,
					   struct net_device *dev,
					   struct flowi6 *fl6,
					   struct ip_tunnel_info *info)
{
	bool use_cache = ip_tunnel_dst_cache_usable(skb, info);
	struct geneve_dev *geneve = netdev_priv(dev);
	struct dst_entry *dst = NULL;
	struct dst_cache *dst_cache;
	struct geneve_sock *gs6;
	__u8 prio;

	gs6 = rcu_dereference(geneve->sock6);
	if (!gs6)
		return ERR_PTR(-EIO);

	memset(fl6, 0, sizeof(*fl6));
	fl6->flowi6_mark = skb->mark;
	fl6->flowi6_proto = IPPROTO_UDP;

	if (info) {
		fl6->daddr = info->key.u.ipv6.dst;
		fl6->saddr = info->key.u.ipv6.src;
		fl6->flowlabel = ip6_make_flowinfo(RT_TOS(info->key.tos),
						   info->key.label);
		dst_cache = &info->dst_cache;
	} else {
		prio = geneve->tos;
		if (prio == 1) {
			const struct iphdr *iip = ip_hdr(skb);

			prio = ip_tunnel_get_dsfield(iip, skb);
			use_cache = false;
		}

		fl6->flowlabel = ip6_make_flowinfo(RT_TOS(prio),
						   geneve->label);
		fl6->daddr = geneve->remote.sin6.sin6_addr;
		dst_cache = &geneve->dst_cache;
	}

	if (use_cache) {
		dst = dst_cache_get_ip6(dst_cache, &fl6->saddr);
		if (dst)
			return dst;
	}

	if (ipv6_stub->ipv6_dst_lookup(geneve->net, gs6->sock->sk, &dst, fl6)) {
		netdev_dbg(dev, "no route to %pI6\n", &fl6->daddr);
		return ERR_PTR(-ENETUNREACH);
	}
	if (dst->dev == dev) { /* is this necessary? */
		netdev_dbg(dev, "circular route to %pI6\n", &fl6->daddr);
		dst_release(dst);
		return ERR_PTR(-ELOOP);
	}

	if (use_cache)
		dst_cache_set_ip6(dst_cache, dst, &fl6->saddr);
	return dst;
}
#endif

/* Convert 64 bit tunnel ID to 24 bit VNI. */
static void tunnel_id_to_vni(__be64 tun_id, __u8 *vni)
{
#ifdef __BIG_ENDIAN
	vni[0] = (__force __u8)(tun_id >> 16);
	vni[1] = (__force __u8)(tun_id >> 8);
	vni[2] = (__force __u8)tun_id;
#else
	vni[0] = (__force __u8)((__force u64)tun_id >> 40);
	vni[1] = (__force __u8)((__force u64)tun_id >> 48);
	vni[2] = (__force __u8)((__force u64)tun_id >> 56);
#endif
}

static netdev_tx_t geneve_xmit_skb(struct sk_buff *skb, struct net_device *dev,
				   struct ip_tunnel_info *info)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct geneve_sock *gs4;
	struct rtable *rt = NULL;
	const struct iphdr *iip; /* interior IP header */
	int err = -EINVAL;
	struct flowi4 fl4;
	__u8 tos, ttl;
	__be16 sport;
	__be16 df;
	bool xnet = !net_eq(geneve->net, dev_net(geneve->dev));
	u32 flags = geneve->flags;

	gs4 = rcu_dereference(geneve->sock4);
	if (!gs4)
		goto tx_error;

	if (geneve->collect_md) {
		if (unlikely(!info || !(info->mode & IP_TUNNEL_INFO_TX))) {
			netdev_dbg(dev, "no tunnel metadata\n");
			goto tx_error;
		}
		if (info && ip_tunnel_info_af(info) != AF_INET)
			goto tx_error;
	}

	rt = geneve_get_v4_rt(skb, dev, &fl4, info);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		goto tx_error;
	}

	sport = udp_flow_src_port(geneve->net, skb, 1, USHRT_MAX, true);
	skb_reset_mac_header(skb);

	iip = ip_hdr(skb);

	if (info) {
		const struct ip_tunnel_key *key = &info->key;
		u8 *opts = NULL;
		u8 vni[3];

		tunnel_id_to_vni(key->tun_id, vni);
		if (info->options_len)
			opts = ip_tunnel_info_opts(info);

		if (key->tun_flags & TUNNEL_CSUM)
			flags &= ~GENEVE_F_UDP_ZERO_CSUM_TX;
		else
			flags |= GENEVE_F_UDP_ZERO_CSUM_TX;

		err = geneve_build_skb(rt, skb, key->tun_flags, vni,
				       info->options_len, opts, flags, xnet);
		if (unlikely(err))
			goto tx_error;

		tos = ip_tunnel_ecn_encap(key->tos, iip, skb);
		ttl = key->ttl;
		df = key->tun_flags & TUNNEL_DONT_FRAGMENT ? htons(IP_DF) : 0;
	} else {
		err = geneve_build_skb(rt, skb, 0, geneve->vni,
				       0, NULL, flags, xnet);
		if (unlikely(err))
			goto tx_error;

		tos = ip_tunnel_ecn_encap(fl4.flowi4_tos, iip, skb);
		ttl = geneve->ttl;
		if (!ttl && IN_MULTICAST(ntohl(fl4.daddr)))
			ttl = 1;
		ttl = ttl ? : ip4_dst_hoplimit(&rt->dst);
		df = 0;
	}
	udp_tunnel_xmit_skb(rt, gs4->sock->sk, skb, fl4.saddr, fl4.daddr,
			    tos, ttl, df, sport, geneve->dst_port,
			    !net_eq(geneve->net, dev_net(geneve->dev)),
			    !!(flags & GENEVE_F_UDP_ZERO_CSUM_TX));

	return NETDEV_TX_OK;

tx_error:
	dev_kfree_skb(skb);

	if (err == -ELOOP)
		dev->stats.collisions++;
	else if (err == -ENETUNREACH)
		dev->stats.tx_carrier_errors++;

	dev->stats.tx_errors++;
	return NETDEV_TX_OK;
}

#if IS_ENABLED(CONFIG_IPV6)
static netdev_tx_t geneve6_xmit_skb(struct sk_buff *skb, struct net_device *dev,
				    struct ip_tunnel_info *info)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct dst_entry *dst = NULL;
	const struct iphdr *iip; /* interior IP header */
	struct geneve_sock *gs6;
	int err = -EINVAL;
	struct flowi6 fl6;
	__u8 prio, ttl;
	__be16 sport;
	__be32 label;
	bool xnet = !net_eq(geneve->net, dev_net(geneve->dev));
	u32 flags = geneve->flags;

	gs6 = rcu_dereference(geneve->sock6);
	if (!gs6)
		goto tx_error;

	if (geneve->collect_md) {
		if (unlikely(!info || !(info->mode & IP_TUNNEL_INFO_TX))) {
			netdev_dbg(dev, "no tunnel metadata\n");
			goto tx_error;
		}
	}

	dst = geneve_get_v6_dst(skb, dev, &fl6, info);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		goto tx_error;
	}

	sport = udp_flow_src_port(geneve->net, skb, 1, USHRT_MAX, true);
	skb_reset_mac_header(skb);

	iip = ip_hdr(skb);

	if (info) {
		const struct ip_tunnel_key *key = &info->key;
		u8 *opts = NULL;
		u8 vni[3];

		tunnel_id_to_vni(key->tun_id, vni);
		if (info->options_len)
			opts = ip_tunnel_info_opts(info);

		if (key->tun_flags & TUNNEL_CSUM)
			flags &= ~GENEVE_F_UDP_ZERO_CSUM6_TX;
		else
			flags |= GENEVE_F_UDP_ZERO_CSUM6_TX;

		err = geneve6_build_skb(dst, skb, key->tun_flags, vni,
					info->options_len, opts,
					flags, xnet);
		if (unlikely(err))
			goto tx_error;

		prio = ip_tunnel_ecn_encap(key->tos, iip, skb);
		ttl = key->ttl;
		label = info->key.label;
	} else {
		err = geneve6_build_skb(dst, skb, 0, geneve->vni,
					0, NULL, flags, xnet);
		if (unlikely(err))
			goto tx_error;

		prio = ip_tunnel_ecn_encap(ip6_tclass(fl6.flowlabel),
					   iip, skb);
		ttl = geneve->ttl;
		if (!ttl && ipv6_addr_is_multicast(&fl6.daddr))
			ttl = 1;
		ttl = ttl ? : ip6_dst_hoplimit(dst);
		label = geneve->label;
	}

	udp_tunnel6_xmit_skb(dst, gs6->sock->sk, skb, dev,
			     &fl6.saddr, &fl6.daddr, prio, ttl, label,
			     sport, geneve->dst_port,
			     !!(flags & GENEVE_F_UDP_ZERO_CSUM6_TX));
	return NETDEV_TX_OK;

tx_error:
	dev_kfree_skb(skb);

	if (err == -ELOOP)
		dev->stats.collisions++;
	else if (err == -ENETUNREACH)
		dev->stats.tx_carrier_errors++;

	dev->stats.tx_errors++;
	return NETDEV_TX_OK;
}
#endif

static netdev_tx_t geneve_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct ip_tunnel_info *info = NULL;

	if (geneve->collect_md)
		info = skb_tunnel_info(skb);

#if IS_ENABLED(CONFIG_IPV6)
	if ((info && ip_tunnel_info_af(info) == AF_INET6) ||
	    (!info && geneve->remote.sa.sa_family == AF_INET6))
		return geneve6_xmit_skb(skb, dev, info);
#endif
	return geneve_xmit_skb(skb, dev, info);
}

static int geneve_change_mtu(struct net_device *dev, int new_mtu)
{
	/* Only possible if called internally, ndo_change_mtu path's new_mtu
	 * is guaranteed to be between dev->min_mtu and dev->max_mtu.
	 */
	if (new_mtu > dev->max_mtu)
		new_mtu = dev->max_mtu;

	dev->mtu = new_mtu;
	return 0;
}

static int geneve_fill_metadata_dst(struct net_device *dev, struct sk_buff *skb)
{
	struct ip_tunnel_info *info = skb_tunnel_info(skb);
	struct geneve_dev *geneve = netdev_priv(dev);
	struct rtable *rt;
	struct flowi4 fl4;
#if IS_ENABLED(CONFIG_IPV6)
	struct dst_entry *dst;
	struct flowi6 fl6;
#endif

	if (ip_tunnel_info_af(info) == AF_INET) {
		rt = geneve_get_v4_rt(skb, dev, &fl4, info);
		if (IS_ERR(rt))
			return PTR_ERR(rt);

		ip_rt_put(rt);
		info->key.u.ipv4.src = fl4.saddr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (ip_tunnel_info_af(info) == AF_INET6) {
		dst = geneve_get_v6_dst(skb, dev, &fl6, info);
		if (IS_ERR(dst))
			return PTR_ERR(dst);

		dst_release(dst);
		info->key.u.ipv6.src = fl6.saddr;
#endif
	} else {
		return -EINVAL;
	}

	info->key.tp_src = udp_flow_src_port(geneve->net, skb,
					     1, USHRT_MAX, true);
	info->key.tp_dst = geneve->dst_port;
	return 0;
}

static const struct net_device_ops geneve_netdev_ops = {
	.ndo_init		= geneve_init,
	.ndo_uninit		= geneve_uninit,
	.ndo_open		= geneve_open,
	.ndo_stop		= geneve_stop,
	.ndo_start_xmit		= geneve_xmit,
	.ndo_get_stats64	= ip_tunnel_get_stats64,
	.ndo_change_mtu		= geneve_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_fill_metadata_dst	= geneve_fill_metadata_dst,
};

static void geneve_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->version, GENEVE_NETDEV_VER, sizeof(drvinfo->version));
	strlcpy(drvinfo->driver, "geneve", sizeof(drvinfo->driver));
}

static const struct ethtool_ops geneve_ethtool_ops = {
	.get_drvinfo	= geneve_get_drvinfo,
	.get_link	= ethtool_op_get_link,
};

/* Info for udev, that this is a virtual tunnel endpoint */
static struct device_type geneve_type = {
	.name = "geneve",
};

/* Calls the ndo_udp_tunnel_add of the caller in order to
 * supply the listening GENEVE udp ports. Callers are expected
 * to implement the ndo_udp_tunnel_add.
 */
static void geneve_push_rx_ports(struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_sock *gs;

	rcu_read_lock();
	list_for_each_entry_rcu(gs, &gn->sock_list, list)
		udp_tunnel_push_rx_port(dev, gs->sock,
					UDP_TUNNEL_TYPE_GENEVE);
	rcu_read_unlock();
}

/* Initialize the device structure. */
static void geneve_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->netdev_ops = &geneve_netdev_ops;
	dev->ethtool_ops = &geneve_ethtool_ops;
	dev->destructor = free_netdev;

	SET_NETDEV_DEVTYPE(dev, &geneve_type);

	dev->features    |= NETIF_F_LLTX;
	dev->features    |= NETIF_F_SG | NETIF_F_HW_CSUM;
	dev->features    |= NETIF_F_RXCSUM;
	dev->features    |= NETIF_F_GSO_SOFTWARE;

	dev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_GSO_SOFTWARE;

	/* MTU range: 68 - (something less than 65535) */
	dev->min_mtu = ETH_MIN_MTU;
	/* The max_mtu calculation does not take account of GENEVE
	 * options, to avoid excluding potentially valid
	 * configurations. This will be further reduced by IPvX hdr size.
	 */
	dev->max_mtu = IP_MAX_MTU - GENEVE_BASE_HLEN - dev->hard_header_len;

	netif_keep_dst(dev);
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->priv_flags |= IFF_LIVE_ADDR_CHANGE | IFF_NO_QUEUE;
	eth_hw_addr_random(dev);
}

static const struct nla_policy geneve_policy[IFLA_GENEVE_MAX + 1] = {
	[IFLA_GENEVE_ID]		= { .type = NLA_U32 },
	[IFLA_GENEVE_REMOTE]		= { .len = FIELD_SIZEOF(struct iphdr, daddr) },
	[IFLA_GENEVE_REMOTE6]		= { .len = sizeof(struct in6_addr) },
	[IFLA_GENEVE_TTL]		= { .type = NLA_U8 },
	[IFLA_GENEVE_TOS]		= { .type = NLA_U8 },
	[IFLA_GENEVE_LABEL]		= { .type = NLA_U32 },
	[IFLA_GENEVE_PORT]		= { .type = NLA_U16 },
	[IFLA_GENEVE_COLLECT_METADATA]	= { .type = NLA_FLAG },
	[IFLA_GENEVE_UDP_CSUM]		= { .type = NLA_U8 },
	[IFLA_GENEVE_UDP_ZERO_CSUM6_TX]	= { .type = NLA_U8 },
	[IFLA_GENEVE_UDP_ZERO_CSUM6_RX]	= { .type = NLA_U8 },
};

static int geneve_validate(struct nlattr *tb[], struct nlattr *data[])
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN)
			return -EINVAL;

		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS])))
			return -EADDRNOTAVAIL;
	}

	if (!data)
		return -EINVAL;

	if (data[IFLA_GENEVE_ID]) {
		__u32 vni =  nla_get_u32(data[IFLA_GENEVE_ID]);

		if (vni >= GENEVE_VID_MASK)
			return -ERANGE;
	}

	return 0;
}

static struct geneve_dev *geneve_find_dev(struct geneve_net *gn,
					  __be16 dst_port,
					  union geneve_addr *remote,
					  u8 vni[],
					  bool *tun_on_same_port,
					  bool *tun_collect_md)
{
	struct geneve_dev *geneve, *t;

	*tun_on_same_port = false;
	*tun_collect_md = false;
	t = NULL;
	list_for_each_entry(geneve, &gn->geneve_list, next) {
		if (geneve->dst_port == dst_port) {
			*tun_collect_md = geneve->collect_md;
			*tun_on_same_port = true;
		}
		if (!memcmp(vni, geneve->vni, sizeof(geneve->vni)) &&
		    !memcmp(remote, &geneve->remote, sizeof(geneve->remote)) &&
		    dst_port == geneve->dst_port)
			t = geneve;
	}
	return t;
}

static int geneve_configure(struct net *net, struct net_device *dev,
			    union geneve_addr *remote,
			    __u32 vni, __u8 ttl, __u8 tos, __be32 label,
			    __be16 dst_port, bool metadata, u32 flags)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_dev *t, *geneve = netdev_priv(dev);
	bool tun_collect_md, tun_on_same_port;
	int err, encap_len;

	if (!remote)
		return -EINVAL;
	if (metadata &&
	    (remote->sa.sa_family != AF_UNSPEC || vni || tos || ttl || label))
		return -EINVAL;

	geneve->net = net;
	geneve->dev = dev;

	geneve->vni[0] = (vni & 0x00ff0000) >> 16;
	geneve->vni[1] = (vni & 0x0000ff00) >> 8;
	geneve->vni[2] =  vni & 0x000000ff;

	if ((remote->sa.sa_family == AF_INET &&
	     IN_MULTICAST(ntohl(remote->sin.sin_addr.s_addr))) ||
	    (remote->sa.sa_family == AF_INET6 &&
	     ipv6_addr_is_multicast(&remote->sin6.sin6_addr)))
		return -EINVAL;
	if (label && remote->sa.sa_family != AF_INET6)
		return -EINVAL;

	geneve->remote = *remote;

	geneve->ttl = ttl;
	geneve->tos = tos;
	geneve->label = label;
	geneve->dst_port = dst_port;
	geneve->collect_md = metadata;
	geneve->flags = flags;

	t = geneve_find_dev(gn, dst_port, remote, geneve->vni,
			    &tun_on_same_port, &tun_collect_md);
	if (t)
		return -EBUSY;

	/* make enough headroom for basic scenario */
	encap_len = GENEVE_BASE_HLEN + ETH_HLEN;
	if (remote->sa.sa_family == AF_INET) {
		encap_len += sizeof(struct iphdr);
		dev->max_mtu -= sizeof(struct iphdr);
	} else {
		encap_len += sizeof(struct ipv6hdr);
		dev->max_mtu -= sizeof(struct ipv6hdr);
	}
	dev->needed_headroom = encap_len + ETH_HLEN;

	if (metadata) {
		if (tun_on_same_port)
			return -EPERM;
	} else {
		if (tun_collect_md)
			return -EPERM;
	}

	dst_cache_reset(&geneve->dst_cache);

	err = register_netdevice(dev);
	if (err)
		return err;

	list_add(&geneve->next, &gn->geneve_list);
	return 0;
}

static int geneve_newlink(struct net *net, struct net_device *dev,
			  struct nlattr *tb[], struct nlattr *data[])
{
	__be16 dst_port = htons(GENEVE_UDP_PORT);
	__u8 ttl = 0, tos = 0;
	bool metadata = false;
	union geneve_addr remote = geneve_remote_unspec;
	__be32 label = 0;
	__u32 vni = 0;
	u32 flags = 0;

	if (data[IFLA_GENEVE_REMOTE] && data[IFLA_GENEVE_REMOTE6])
		return -EINVAL;

	if (data[IFLA_GENEVE_REMOTE]) {
		remote.sa.sa_family = AF_INET;
		remote.sin.sin_addr.s_addr =
			nla_get_in_addr(data[IFLA_GENEVE_REMOTE]);
	}

	if (data[IFLA_GENEVE_REMOTE6]) {
		if (!IS_ENABLED(CONFIG_IPV6))
			return -EPFNOSUPPORT;

		remote.sa.sa_family = AF_INET6;
		remote.sin6.sin6_addr =
			nla_get_in6_addr(data[IFLA_GENEVE_REMOTE6]);

		if (ipv6_addr_type(&remote.sin6.sin6_addr) &
		    IPV6_ADDR_LINKLOCAL) {
			netdev_dbg(dev, "link-local remote is unsupported\n");
			return -EINVAL;
		}
	}

	if (data[IFLA_GENEVE_ID])
		vni = nla_get_u32(data[IFLA_GENEVE_ID]);

	if (data[IFLA_GENEVE_TTL])
		ttl = nla_get_u8(data[IFLA_GENEVE_TTL]);

	if (data[IFLA_GENEVE_TOS])
		tos = nla_get_u8(data[IFLA_GENEVE_TOS]);

	if (data[IFLA_GENEVE_LABEL])
		label = nla_get_be32(data[IFLA_GENEVE_LABEL]) &
			IPV6_FLOWLABEL_MASK;

	if (data[IFLA_GENEVE_PORT])
		dst_port = nla_get_be16(data[IFLA_GENEVE_PORT]);

	if (data[IFLA_GENEVE_COLLECT_METADATA])
		metadata = true;

	if (data[IFLA_GENEVE_UDP_CSUM] &&
	    !nla_get_u8(data[IFLA_GENEVE_UDP_CSUM]))
		flags |= GENEVE_F_UDP_ZERO_CSUM_TX;

	if (data[IFLA_GENEVE_UDP_ZERO_CSUM6_TX] &&
	    nla_get_u8(data[IFLA_GENEVE_UDP_ZERO_CSUM6_TX]))
		flags |= GENEVE_F_UDP_ZERO_CSUM6_TX;

	if (data[IFLA_GENEVE_UDP_ZERO_CSUM6_RX] &&
	    nla_get_u8(data[IFLA_GENEVE_UDP_ZERO_CSUM6_RX]))
		flags |= GENEVE_F_UDP_ZERO_CSUM6_RX;

	return geneve_configure(net, dev, &remote, vni, ttl, tos, label,
				dst_port, metadata, flags);
}

static void geneve_dellink(struct net_device *dev, struct list_head *head)
{
	struct geneve_dev *geneve = netdev_priv(dev);

	list_del(&geneve->next);
	unregister_netdevice_queue(dev, head);
}

static size_t geneve_get_size(const struct net_device *dev)
{
	return nla_total_size(sizeof(__u32)) +	/* IFLA_GENEVE_ID */
		nla_total_size(sizeof(struct in6_addr)) + /* IFLA_GENEVE_REMOTE{6} */
		nla_total_size(sizeof(__u8)) +  /* IFLA_GENEVE_TTL */
		nla_total_size(sizeof(__u8)) +  /* IFLA_GENEVE_TOS */
		nla_total_size(sizeof(__be32)) +  /* IFLA_GENEVE_LABEL */
		nla_total_size(sizeof(__be16)) +  /* IFLA_GENEVE_PORT */
		nla_total_size(0) +	 /* IFLA_GENEVE_COLLECT_METADATA */
		nla_total_size(sizeof(__u8)) + /* IFLA_GENEVE_UDP_CSUM */
		nla_total_size(sizeof(__u8)) + /* IFLA_GENEVE_UDP_ZERO_CSUM6_TX */
		nla_total_size(sizeof(__u8)) + /* IFLA_GENEVE_UDP_ZERO_CSUM6_RX */
		0;
}

static int geneve_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	__u32 vni;

	vni = (geneve->vni[0] << 16) | (geneve->vni[1] << 8) | geneve->vni[2];
	if (nla_put_u32(skb, IFLA_GENEVE_ID, vni))
		goto nla_put_failure;

	if (geneve->remote.sa.sa_family == AF_INET) {
		if (nla_put_in_addr(skb, IFLA_GENEVE_REMOTE,
				    geneve->remote.sin.sin_addr.s_addr))
			goto nla_put_failure;
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		if (nla_put_in6_addr(skb, IFLA_GENEVE_REMOTE6,
				     &geneve->remote.sin6.sin6_addr))
			goto nla_put_failure;
#endif
	}

	if (nla_put_u8(skb, IFLA_GENEVE_TTL, geneve->ttl) ||
	    nla_put_u8(skb, IFLA_GENEVE_TOS, geneve->tos) ||
	    nla_put_be32(skb, IFLA_GENEVE_LABEL, geneve->label))
		goto nla_put_failure;

	if (nla_put_be16(skb, IFLA_GENEVE_PORT, geneve->dst_port))
		goto nla_put_failure;

	if (geneve->collect_md) {
		if (nla_put_flag(skb, IFLA_GENEVE_COLLECT_METADATA))
			goto nla_put_failure;
	}

	if (nla_put_u8(skb, IFLA_GENEVE_UDP_CSUM,
		       !(geneve->flags & GENEVE_F_UDP_ZERO_CSUM_TX)) ||
	    nla_put_u8(skb, IFLA_GENEVE_UDP_ZERO_CSUM6_TX,
		       !!(geneve->flags & GENEVE_F_UDP_ZERO_CSUM6_TX)) ||
	    nla_put_u8(skb, IFLA_GENEVE_UDP_ZERO_CSUM6_RX,
		       !!(geneve->flags & GENEVE_F_UDP_ZERO_CSUM6_RX)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct rtnl_link_ops geneve_link_ops __read_mostly = {
	.kind		= "geneve",
	.maxtype	= IFLA_GENEVE_MAX,
	.policy		= geneve_policy,
	.priv_size	= sizeof(struct geneve_dev),
	.setup		= geneve_setup,
	.validate	= geneve_validate,
	.newlink	= geneve_newlink,
	.dellink	= geneve_dellink,
	.get_size	= geneve_get_size,
	.fill_info	= geneve_fill_info,
};

struct net_device *geneve_dev_create_fb(struct net *net, const char *name,
					u8 name_assign_type, u16 dst_port)
{
	struct nlattr *tb[IFLA_MAX + 1];
	struct net_device *dev;
	LIST_HEAD(list_kill);
	int err;

	memset(tb, 0, sizeof(tb));
	dev = rtnl_create_link(net, name, name_assign_type,
			       &geneve_link_ops, tb);
	if (IS_ERR(dev))
		return dev;

	err = geneve_configure(net, dev, &geneve_remote_unspec,
			       0, 0, 0, 0, htons(dst_port), true,
			       GENEVE_F_UDP_ZERO_CSUM6_RX);
	if (err) {
		free_netdev(dev);
		return ERR_PTR(err);
	}

	/* openvswitch users expect packet sizes to be unrestricted,
	 * so set the largest MTU we can.
	 */
	err = geneve_change_mtu(dev, IP_MAX_MTU);
	if (err)
		goto err;

	err = rtnl_configure_link(dev, NULL);
	if (err < 0)
		goto err;

	return dev;

 err:
	geneve_dellink(dev, &list_kill);
	unregister_netdevice_many(&list_kill);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(geneve_dev_create_fb);

static int geneve_netdevice_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (event == NETDEV_UDP_TUNNEL_PUSH_INFO)
		geneve_push_rx_ports(dev);

	return NOTIFY_DONE;
}

static struct notifier_block geneve_notifier_block __read_mostly = {
	.notifier_call = geneve_netdevice_event,
};

static __net_init int geneve_init_net(struct net *net)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);

	INIT_LIST_HEAD(&gn->geneve_list);
	INIT_LIST_HEAD(&gn->sock_list);
	return 0;
}

static void __net_exit geneve_exit_net(struct net *net)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_dev *geneve, *next;
	struct net_device *dev, *aux;
	LIST_HEAD(list);

	rtnl_lock();

	/* gather any geneve devices that were moved into this ns */
	for_each_netdev_safe(net, dev, aux)
		if (dev->rtnl_link_ops == &geneve_link_ops)
			unregister_netdevice_queue(dev, &list);

	/* now gather any other geneve devices that were created in this ns */
	list_for_each_entry_safe(geneve, next, &gn->geneve_list, next) {
		/* If geneve->dev is in the same netns, it was already added
		 * to the list by the previous loop.
		 */
		if (!net_eq(dev_net(geneve->dev), net))
			unregister_netdevice_queue(geneve->dev, &list);
	}

	/* unregister the devices gathered above */
	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations geneve_net_ops = {
	.init = geneve_init_net,
	.exit = geneve_exit_net,
	.id   = &geneve_net_id,
	.size = sizeof(struct geneve_net),
};

static int __init geneve_init_module(void)
{
	int rc;

	rc = register_pernet_subsys(&geneve_net_ops);
	if (rc)
		goto out1;

	rc = register_netdevice_notifier(&geneve_notifier_block);
	if (rc)
		goto out2;

	rc = rtnl_link_register(&geneve_link_ops);
	if (rc)
		goto out3;

	return 0;

out3:
	unregister_netdevice_notifier(&geneve_notifier_block);
out2:
	unregister_pernet_subsys(&geneve_net_ops);
out1:
	return rc;
}
late_initcall(geneve_init_module);

static void __exit geneve_cleanup_module(void)
{
	rtnl_link_unregister(&geneve_link_ops);
	unregister_netdevice_notifier(&geneve_notifier_block);
	unregister_pernet_subsys(&geneve_net_ops);
}
module_exit(geneve_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_VERSION(GENEVE_NETDEV_VER);
MODULE_AUTHOR("John W. Linville <linville@tuxdriver.com>");
MODULE_DESCRIPTION("Interface driver for GENEVE encapsulated traffic");
MODULE_ALIAS_RTNL_LINK("geneve");
