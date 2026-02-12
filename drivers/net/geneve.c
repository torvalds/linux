// SPDX-License-Identifier: GPL-2.0-only
/*
 * GENEVE: Generic Network Virtualization Encapsulation
 *
 * Copyright (c) 2015 Red Hat, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ethtool.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/hash.h>
#include <net/ipv6_stubs.h>
#include <net/dst_metadata.h>
#include <net/gro_cells.h>
#include <net/rtnetlink.h>
#include <net/geneve.h>
#include <net/gro.h>
#include <net/netdev_lock.h>
#include <net/protocol.h>

#define GENEVE_NETDEV_VER	"0.6"

#define GENEVE_N_VID		(1u << 24)
#define GENEVE_VID_MASK		(GENEVE_N_VID - 1)

#define VNI_HASH_BITS		10
#define VNI_HASH_SIZE		(1<<VNI_HASH_BITS)

static bool log_ecn_error = true;
module_param(log_ecn_error, bool, 0644);
MODULE_PARM_DESC(log_ecn_error, "Log packets received with corrupted ECN");

#define GENEVE_VER 0
#define GENEVE_BASE_HLEN (sizeof(struct udphdr) + sizeof(struct genevehdr))
#define GENEVE_IPV4_HLEN (ETH_HLEN + sizeof(struct iphdr) + GENEVE_BASE_HLEN)
#define GENEVE_IPV6_HLEN (ETH_HLEN + sizeof(struct ipv6hdr) + GENEVE_BASE_HLEN)

#define GENEVE_OPT_NETDEV_CLASS		0x100
#define GENEVE_OPT_GRO_HINT_SIZE	8
#define GENEVE_OPT_GRO_HINT_TYPE	1
#define GENEVE_OPT_GRO_HINT_LEN		1

struct geneve_opt_gro_hint {
	u8	inner_proto_id:2,
		nested_is_v6:1;
	u8	nested_nh_offset;
	u8	nested_tp_offset;
	u8	nested_hdr_len;
};

struct geneve_skb_cb {
	unsigned int	gro_hint_len;
	struct geneve_opt_gro_hint gro_hint;
};

#define GENEVE_SKB_CB(__skb)	((struct geneve_skb_cb *)&((__skb)->cb[0]))

/* per-network namespace private data for this module */
struct geneve_net {
	struct list_head	geneve_list;
	/* sock_list is protected by rtnl lock */
	struct list_head	sock_list;
};

static unsigned int geneve_net_id;

struct geneve_dev_node {
	struct hlist_node hlist;
	struct geneve_dev *geneve;
};

struct geneve_config {
	bool			collect_md;
	bool			use_udp6_rx_checksums;
	bool			ttl_inherit;
	bool			gro_hint;
	enum ifla_geneve_df	df;
	bool			inner_proto_inherit;
	u16			port_min;
	u16			port_max;

	/* Must be last --ends in a flexible-array member. */
	struct ip_tunnel_info	info;
};

/* Pseudo network device */
struct geneve_dev {
	struct geneve_dev_node hlist4;	/* vni hash table for IPv4 socket */
#if IS_ENABLED(CONFIG_IPV6)
	struct geneve_dev_node hlist6;	/* vni hash table for IPv6 socket */
#endif
	struct net	   *net;	/* netns for packet i/o */
	struct net_device  *dev;	/* netdev for geneve tunnel */
	struct geneve_sock __rcu *sock4;	/* IPv4 socket used for geneve tunnel */
#if IS_ENABLED(CONFIG_IPV6)
	struct geneve_sock __rcu *sock6;	/* IPv6 socket used for geneve tunnel */
#endif
	struct list_head   next;	/* geneve's per namespace list */
	struct gro_cells   gro_cells;
	struct geneve_config cfg;
};

struct geneve_sock {
	bool			collect_md;
	bool			gro_hint;
	struct list_head	list;
	struct socket		*sock;
	struct rcu_head		rcu;
	int			refcnt;
	struct hlist_head	vni_list[VNI_HASH_SIZE];
};

static const __be16 proto_id_map[] = { htons(ETH_P_TEB),
				       htons(ETH_P_IPV6),
				       htons(ETH_P_IP) };

static int proto_to_id(__be16 proto)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(proto_id_map); i++)
		if (proto_id_map[i] == proto)
			return i;

	return -1;
}

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

static bool eq_tun_id_and_vni(u8 *tun_id, u8 *vni)
{
	return !memcmp(vni, &tun_id[5], 3);
}

static sa_family_t geneve_get_sk_family(struct geneve_sock *gs)
{
	return gs->sock->sk->sk_family;
}

static struct geneve_dev *geneve_lookup(struct geneve_sock *gs,
					__be32 addr, u8 vni[])
{
	struct hlist_head *vni_list_head;
	struct geneve_dev_node *node;
	__u32 hash;

	/* Find the device for this VNI */
	hash = geneve_net_vni_hash(vni);
	vni_list_head = &gs->vni_list[hash];
	hlist_for_each_entry_rcu(node, vni_list_head, hlist) {
		if (eq_tun_id_and_vni((u8 *)&node->geneve->cfg.info.key.tun_id, vni) &&
		    addr == node->geneve->cfg.info.key.u.ipv4.dst)
			return node->geneve;
	}
	return NULL;
}

#if IS_ENABLED(CONFIG_IPV6)
static struct geneve_dev *geneve6_lookup(struct geneve_sock *gs,
					 struct in6_addr addr6, u8 vni[])
{
	struct hlist_head *vni_list_head;
	struct geneve_dev_node *node;
	__u32 hash;

	/* Find the device for this VNI */
	hash = geneve_net_vni_hash(vni);
	vni_list_head = &gs->vni_list[hash];
	hlist_for_each_entry_rcu(node, vni_list_head, hlist) {
		if (eq_tun_id_and_vni((u8 *)&node->geneve->cfg.info.key.tun_id, vni) &&
		    ipv6_addr_equal(&addr6, &node->geneve->cfg.info.key.u.ipv6.dst))
			return node->geneve;
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
	static u8 zero_vni[3];
	u8 *vni;

	if (geneve_get_sk_family(gs) == AF_INET) {
		struct iphdr *iph;
		__be32 addr;

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
		static struct in6_addr zero_addr6;
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
		      struct sk_buff *skb, const struct genevehdr *gnvh)
{
	struct metadata_dst *tun_dst = NULL;
	unsigned int len;
	int nh, err = 0;
	void *oiph;

	if (ip_tunnel_collect_metadata() || gs->collect_md) {
		IP_TUNNEL_DECLARE_FLAGS(flags) = { };

		__set_bit(IP_TUNNEL_KEY_BIT, flags);
		__assign_bit(IP_TUNNEL_OAM_BIT, flags, gnvh->oam);
		__assign_bit(IP_TUNNEL_CRIT_OPT_BIT, flags, gnvh->critical);

		tun_dst = udp_tun_rx_dst(skb, geneve_get_sk_family(gs), flags,
					 vni_to_tunnel_id(gnvh->vni),
					 gnvh->opt_len * 4);
		if (!tun_dst) {
			dev_dstats_rx_dropped(geneve->dev);
			goto drop;
		}
		/* Update tunnel dst according to Geneve options. */
		ip_tunnel_flags_zero(flags);
		__set_bit(IP_TUNNEL_GENEVE_OPT_BIT, flags);
		ip_tunnel_info_opts_set(&tun_dst->u.tun_info,
					gnvh->options, gnvh->opt_len * 4,
					flags);
	} else {
		/* Drop packets w/ critical options,
		 * since we don't support any...
		 */
		if (gnvh->critical) {
			DEV_STATS_INC(geneve->dev, rx_frame_errors);
			DEV_STATS_INC(geneve->dev, rx_errors);
			goto drop;
		}
	}

	if (tun_dst)
		skb_dst_set(skb, &tun_dst->dst);

	if (gnvh->proto_type == htons(ETH_P_TEB)) {
		skb_reset_mac_header(skb);
		skb->protocol = eth_type_trans(skb, geneve->dev);
		skb_postpull_rcsum(skb, eth_hdr(skb), ETH_HLEN);

		/* Ignore packet loops (and multicast echo) */
		if (ether_addr_equal(eth_hdr(skb)->h_source,
				     geneve->dev->dev_addr)) {
			DEV_STATS_INC(geneve->dev, rx_errors);
			goto drop;
		}
	} else {
		skb_reset_mac_header(skb);
		skb->dev = geneve->dev;
		skb->pkt_type = PACKET_HOST;
	}

	/* Save offset of outer header relative to skb->head,
	 * because we are going to reset the network header to the inner header
	 * and might change skb->head.
	 */
	nh = skb_network_header(skb) - skb->head;

	skb_reset_network_header(skb);

	if (!pskb_inet_may_pull(skb)) {
		DEV_STATS_INC(geneve->dev, rx_length_errors);
		DEV_STATS_INC(geneve->dev, rx_errors);
		goto drop;
	}

	/* Get the outer header. */
	oiph = skb->head + nh;

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
			DEV_STATS_INC(geneve->dev, rx_frame_errors);
			DEV_STATS_INC(geneve->dev, rx_errors);
			goto drop;
		}
	}

	/* Skip the additional GRO stage when hints are in use. */
	len = skb->len;
	if (skb->encapsulation)
		err = netif_rx(skb);
	else
		err = gro_cells_receive(&geneve->gro_cells, skb);
	if (likely(err == NET_RX_SUCCESS))
		dev_dstats_rx_add(geneve->dev, len);

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

	err = gro_cells_init(&geneve->gro_cells, dev);
	if (err)
		return err;

	err = dst_cache_init(&geneve->cfg.info.dst_cache, GFP_KERNEL);
	if (err) {
		gro_cells_destroy(&geneve->gro_cells);
		return err;
	}
	netdev_lockdep_set_classes(dev);
	return 0;
}

static void geneve_uninit(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);

	dst_cache_destroy(&geneve->cfg.info.dst_cache);
	gro_cells_destroy(&geneve->gro_cells);
}

static int geneve_hlen(const struct genevehdr *gh)
{
	return sizeof(*gh) + gh->opt_len * 4;
}

/*
 * Look for GRO hint in the genenve options; if not found or does not pass basic
 * sanitization return 0, otherwise the offset WRT the geneve hdr start.
 */
static unsigned int
geneve_opt_gro_hint_off(const struct genevehdr *gh, __be16 *type,
			unsigned int *gh_len)
{
	struct geneve_opt *opt = (void *)(gh + 1);
	unsigned int id, opt_len = gh->opt_len;
	struct geneve_opt_gro_hint *gro_hint;

	while (opt_len >= (GENEVE_OPT_GRO_HINT_SIZE >> 2)) {
		if (opt->opt_class == htons(GENEVE_OPT_NETDEV_CLASS) &&
		    opt->type == GENEVE_OPT_GRO_HINT_TYPE &&
		    opt->length == GENEVE_OPT_GRO_HINT_LEN)
			goto found;

		/* check for bad opt len */
		if (opt->length + 1 >= opt_len)
			return 0;

		/* next opt */
		opt_len -= opt->length + 1;
		opt = ((void *)opt) + ((opt->length + 1) << 2);
	}
	return 0;

found:
	gro_hint = (struct geneve_opt_gro_hint *)opt->opt_data;

	/*
	 * Sanitize the hinted hdrs: the nested transport is UDP and must fit
	 * the overall hinted hdr size.
	 */
	if (gro_hint->nested_tp_offset + sizeof(struct udphdr) >
	    gro_hint->nested_hdr_len)
		return 0;

	if (gro_hint->nested_nh_offset +
	    (gro_hint->nested_is_v6 ? sizeof(struct ipv6hdr) :
				      sizeof(struct iphdr)) >
	    gro_hint->nested_tp_offset)
		return 0;

	/* Allow only supported L2. */
	id = gro_hint->inner_proto_id;
	if (id >= ARRAY_SIZE(proto_id_map))
		return 0;

	*type = proto_id_map[id];
	*gh_len += gro_hint->nested_hdr_len;

	return (void *)gro_hint - (void *)gh;
}

static const struct geneve_opt_gro_hint *
geneve_opt_gro_hint(const struct genevehdr *gh, unsigned int hint_off)
{
	return (const struct geneve_opt_gro_hint *)((void *)gh + hint_off);
}

static unsigned int
geneve_sk_gro_hint_off(const struct sock *sk, const struct genevehdr *gh,
		       __be16 *type, unsigned int *gh_len)
{
	const struct geneve_sock *gs = rcu_dereference_sk_user_data(sk);

	if (!gs || !gs->gro_hint)
		return 0;
	return geneve_opt_gro_hint_off(gh, type, gh_len);
}

/* Validate the packet headers pointed by data WRT the provided hint */
static bool
geneve_opt_gro_hint_validate(void *data,
			     const struct geneve_opt_gro_hint *gro_hint)
{
	void *nested_nh = data + gro_hint->nested_nh_offset;
	struct iphdr *iph;

	if (gro_hint->nested_is_v6) {
		struct ipv6hdr *ipv6h = nested_nh;
		struct ipv6_opt_hdr *opth;
		int offset, len;

		if (ipv6h->nexthdr == IPPROTO_UDP)
			return true;

		offset = sizeof(*ipv6h) + gro_hint->nested_nh_offset;
		while (offset + sizeof(*opth) <= gro_hint->nested_tp_offset) {
			opth = data + offset;

			len = ipv6_optlen(opth);
			if (len + offset > gro_hint->nested_tp_offset)
				return false;
			if (opth->nexthdr == IPPROTO_UDP)
				return true;

			offset += len;
		}
		return false;
	}

	iph = nested_nh;
	if (*(u8 *)iph != 0x45 || ip_is_fragment(iph) ||
	    iph->protocol != IPPROTO_UDP || ip_fast_csum((u8 *)iph, 5))
		return false;

	return true;
}

/*
 * Validate the skb headers following the specified geneve hdr vs the
 * provided hint, including nested L4 checksum.
 * The caller already ensured that the relevant amount of data is available
 * in the linear part.
 */
static bool
geneve_opt_gro_hint_validate_csum(const struct sk_buff *skb,
				  const struct genevehdr *gh,
				  const struct geneve_opt_gro_hint *gro_hint)
{
	unsigned int plen, gh_len = geneve_hlen(gh);
	void *nested = (void *)gh + gh_len;
	struct udphdr *nested_uh;
	unsigned int nested_len;
	struct ipv6hdr *ipv6h;
	struct iphdr *iph;
	__wsum csum, psum;

	if (!geneve_opt_gro_hint_validate(nested, gro_hint))
		return false;

	/* Use GRO hints with nested csum only if the outer header has csum. */
	nested_uh = nested + gro_hint->nested_tp_offset;
	if (!nested_uh->check || skb->ip_summed == CHECKSUM_PARTIAL)
		return true;

	if (!NAPI_GRO_CB(skb)->csum_valid)
		return false;

	/* Compute the complete checksum up to the nested transport. */
	plen = gh_len + gro_hint->nested_tp_offset;
	csum = csum_sub(NAPI_GRO_CB(skb)->csum, csum_partial(gh, plen, 0));
	nested_len = skb_gro_len(skb) - plen;

	/* Compute the nested pseudo header csum. */
	ipv6h = nested + gro_hint->nested_nh_offset;
	iph = (struct iphdr *)ipv6h;
	psum = gro_hint->nested_is_v6 ?
	       ~csum_unfold(csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr,
					    nested_len, IPPROTO_UDP, 0)) :
	       csum_tcpudp_nofold(iph->saddr, iph->daddr,
				  nested_len, IPPROTO_UDP, 0);

	return !csum_fold(csum_add(psum, csum));
}

static int geneve_post_decap_hint(const struct sock *sk, struct sk_buff *skb,
				  unsigned int gh_len,
				  struct genevehdr **geneveh)
{
	const struct geneve_opt_gro_hint *gro_hint;
	unsigned int len, total_len, hint_off;
	struct ipv6hdr *ipv6h;
	struct iphdr *iph;
	struct udphdr *uh;
	__be16 p;

	hint_off = geneve_sk_gro_hint_off(sk, *geneveh, &p, &len);
	if (!hint_off)
		return 0;

	if (!skb_is_gso(skb))
		return 0;

	gro_hint = geneve_opt_gro_hint(*geneveh, hint_off);
	if (unlikely(!pskb_may_pull(skb, gro_hint->nested_hdr_len)))
		return -ENOMEM;

	*geneveh = geneve_hdr(skb);
	gro_hint = geneve_opt_gro_hint(*geneveh, hint_off);

	/*
	 * Validate hints from untrusted source before accessing
	 * the headers; csum will be checked later by the nested
	 * protocol rx path.
	 */
	if (unlikely(skb_shinfo(skb)->gso_type & SKB_GSO_DODGY &&
		     !geneve_opt_gro_hint_validate(skb->data, gro_hint)))
		return -EINVAL;

	ipv6h = (void *)skb->data + gro_hint->nested_nh_offset;
	iph = (struct iphdr *)ipv6h;
	total_len = skb->len - gro_hint->nested_nh_offset;
	if (total_len > GRO_LEGACY_MAX_SIZE)
		return -E2BIG;

	/*
	 * After stripping the outer encap, the packet still carries a
	 * tunnel encapsulation: the nested one.
	 */
	skb->encapsulation = 1;

	/* GSO expect a valid transpor header, move it to the current one. */
	skb_set_transport_header(skb, gro_hint->nested_tp_offset);

	/* Adjust the nested IP{6} hdr to actual GSO len. */
	if (gro_hint->nested_is_v6) {
		ipv6h->payload_len = htons(total_len - sizeof(*ipv6h));
	} else {
		__be16 old_len = iph->tot_len;

		iph->tot_len = htons(total_len);

		/* For IPv4 additionally adjust the nested csum. */
		csum_replace2(&iph->check, old_len, iph->tot_len);
		ip_send_check(iph);
	}

	/* Adjust the nested UDP header len and checksum. */
	uh = udp_hdr(skb);
	uh->len = htons(skb->len - gro_hint->nested_tp_offset);
	if (uh->check) {
		len = skb->len - gro_hint->nested_nh_offset;
		skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL_CSUM;
		if (gro_hint->nested_is_v6)
			uh->check = ~udp_v6_check(len, &ipv6h->saddr,
						  &ipv6h->daddr, 0);
		else
			uh->check = ~udp_v4_check(len, iph->saddr,
						  iph->daddr, 0);
	} else {
		skb_shinfo(skb)->gso_type |= SKB_GSO_UDP_TUNNEL;
	}
	return 0;
}

/* Callback from net/ipv4/udp.c to receive packets */
static int geneve_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct genevehdr *geneveh;
	struct geneve_dev *geneve;
	struct geneve_sock *gs;
	__be16 inner_proto;
	int opts_len;

	/* Need UDP and Geneve header to be present */
	if (unlikely(!pskb_may_pull(skb, GENEVE_BASE_HLEN)))
		goto drop;

	/* Return packets with reserved bits set */
	geneveh = geneve_hdr(skb);
	if (unlikely(geneveh->ver != GENEVE_VER))
		goto drop;

	gs = rcu_dereference_sk_user_data(sk);
	if (!gs)
		goto drop;

	geneve = geneve_lookup_skb(gs, skb);
	if (!geneve)
		goto drop;

	inner_proto = geneveh->proto_type;

	if (unlikely((!geneve->cfg.inner_proto_inherit &&
		      inner_proto != htons(ETH_P_TEB)))) {
		dev_dstats_rx_dropped(geneve->dev);
		goto drop;
	}

	opts_len = geneveh->opt_len * 4;
	if (iptunnel_pull_header(skb, GENEVE_BASE_HLEN + opts_len, inner_proto,
				 !net_eq(geneve->net, dev_net(geneve->dev)))) {
		dev_dstats_rx_dropped(geneve->dev);
		goto drop;
	}

	/*
	 * After hint processing, the transport header points to the inner one
	 * and we can't use anymore on geneve_hdr().
	 */
	geneveh = geneve_hdr(skb);
	if (geneve_post_decap_hint(sk, skb, sizeof(struct genevehdr) +
				   opts_len, &geneveh)) {
		DEV_STATS_INC(geneve->dev, rx_errors);
		goto drop;
	}

	geneve_rx(geneve, gs, skb, geneveh);
	return 0;

drop:
	/* Consume bad packet */
	kfree_skb(skb);
	return 0;
}

/* Callback from net/ipv{4,6}/udp.c to check that we have a tunnel for errors */
static int geneve_udp_encap_err_lookup(struct sock *sk, struct sk_buff *skb)
{
	struct genevehdr *geneveh;
	struct geneve_sock *gs;
	u8 zero_vni[3] = { 0 };
	u8 *vni = zero_vni;

	if (!pskb_may_pull(skb, skb_transport_offset(skb) + GENEVE_BASE_HLEN))
		return -EINVAL;

	geneveh = geneve_hdr(skb);
	if (geneveh->ver != GENEVE_VER)
		return -EINVAL;

	if (geneveh->proto_type != htons(ETH_P_TEB))
		return -EINVAL;

	gs = rcu_dereference_sk_user_data(sk);
	if (!gs)
		return -ENOENT;

	if (geneve_get_sk_family(gs) == AF_INET) {
		struct iphdr *iph = ip_hdr(skb);
		__be32 addr4 = 0;

		if (!gs->collect_md) {
			vni = geneve_hdr(skb)->vni;
			addr4 = iph->daddr;
		}

		return geneve_lookup(gs, addr4, vni) ? 0 : -ENOENT;
	}

#if IS_ENABLED(CONFIG_IPV6)
	if (geneve_get_sk_family(gs) == AF_INET6) {
		struct ipv6hdr *ip6h = ipv6_hdr(skb);
		struct in6_addr addr6;

		memset(&addr6, 0, sizeof(struct in6_addr));

		if (!gs->collect_md) {
			vni = geneve_hdr(skb)->vni;
			addr6 = ip6h->daddr;
		}

		return geneve6_lookup(gs, addr6, vni) ? 0 : -ENOENT;
	}
#endif

	return -EPFNOSUPPORT;
}

static struct socket *geneve_create_sock(struct net *net, bool ipv6,
					 __be16 port, bool ipv6_rx_csum)
{
	struct socket *sock;
	struct udp_port_cfg udp_conf;
	int err;

	memset(&udp_conf, 0, sizeof(udp_conf));

	if (ipv6) {
		udp_conf.family = AF_INET6;
		udp_conf.ipv6_v6only = 1;
		udp_conf.use_udp6_rx_checksums = ipv6_rx_csum;
	} else {
		udp_conf.family = AF_INET;
		udp_conf.local_ip.s_addr = htonl(INADDR_ANY);
	}

	udp_conf.local_udp_port = port;

	/* Open UDP socket */
	err = udp_sock_create(net, &udp_conf, &sock);
	if (err < 0)
		return ERR_PTR(err);

	udp_allow_gso(sock->sk);
	return sock;
}

static bool geneve_hdr_match(struct sk_buff *skb,
			     const struct genevehdr *gh,
			     const struct genevehdr *gh2,
			     unsigned int hint_off)
{
	const struct geneve_opt_gro_hint *gro_hint;
	void *nested, *nested2, *nh, *nh2;
	struct udphdr *udp, *udp2;
	unsigned int gh_len;

	/* Match the geneve hdr and options */
	if (gh->opt_len != gh2->opt_len)
		return false;

	gh_len = geneve_hlen(gh);
	if (memcmp(gh, gh2, gh_len))
		return false;

	if (!hint_off)
		return true;

	/*
	 * When gro is present consider the nested headers as part
	 * of the geneve options
	 */
	nested = (void *)gh + gh_len;
	nested2 = (void *)gh2 + gh_len;
	gro_hint = geneve_opt_gro_hint(gh, hint_off);
	if (!memcmp(nested, nested2, gro_hint->nested_hdr_len))
		return true;

	/*
	 * The nested headers differ; the packets can still belong to
	 * the same flow when IPs/proto/ports match; if so flushing is
	 * required.
	 */
	nh = nested + gro_hint->nested_nh_offset;
	nh2 = nested2 + gro_hint->nested_nh_offset;
	if (gro_hint->nested_is_v6) {
		struct ipv6hdr *iph = nh, *iph2 = nh2;
		unsigned int nested_nlen;
		__be32 first_word;

		first_word = *(__be32 *)iph ^ *(__be32 *)iph2;
		if ((first_word & htonl(0xF00FFFFF)) ||
		    !ipv6_addr_equal(&iph->saddr, &iph2->saddr) ||
		    !ipv6_addr_equal(&iph->daddr, &iph2->daddr) ||
		    iph->nexthdr != iph2->nexthdr)
			return false;

		nested_nlen = gro_hint->nested_tp_offset -
			      gro_hint->nested_nh_offset;
		if (nested_nlen > sizeof(struct ipv6hdr) &&
		    (memcmp(iph + 1, iph2 + 1,
			    nested_nlen - sizeof(struct ipv6hdr))))
			return false;
	} else {
		struct iphdr *iph = nh, *iph2 = nh2;

		if ((iph->protocol ^ iph2->protocol) |
		    ((__force u32)iph->saddr ^ (__force u32)iph2->saddr) |
		    ((__force u32)iph->daddr ^ (__force u32)iph2->daddr))
			return false;
	}

	udp = nested + gro_hint->nested_tp_offset;
	udp2 = nested2 + gro_hint->nested_tp_offset;
	if (udp->source != udp2->source || udp->dest != udp2->dest ||
	    udp->check != udp2->check)
		return false;

	NAPI_GRO_CB(skb)->flush = 1;
	return true;
}

static struct sk_buff *geneve_gro_receive(struct sock *sk,
					  struct list_head *head,
					  struct sk_buff *skb)
{
	unsigned int hlen, gh_len, off_gnv, hint_off;
	const struct geneve_opt_gro_hint *gro_hint;
	const struct packet_offload *ptype;
	struct genevehdr *gh, *gh2;
	struct sk_buff *pp = NULL;
	struct sk_buff *p;
	int flush = 1;
	__be16 type;

	off_gnv = skb_gro_offset(skb);
	hlen = off_gnv + sizeof(*gh);
	gh = skb_gro_header(skb, hlen, off_gnv);
	if (unlikely(!gh))
		goto out;

	if (gh->ver != GENEVE_VER || gh->oam)
		goto out;
	gh_len = geneve_hlen(gh);
	type = gh->proto_type;

	hlen = off_gnv + gh_len;
	if (!skb_gro_may_pull(skb, hlen)) {
		gh = skb_gro_header_slow(skb, hlen, off_gnv);
		if (unlikely(!gh))
			goto out;
	}

	/* The GRO hint/nested hdr could use a different ethernet type. */
	hint_off = geneve_sk_gro_hint_off(sk, gh, &type, &gh_len);
	if (hint_off) {

		/*
		 * If the hint is present, and nested hdr validation fails, do
		 * not attempt plain GRO: it will ignore inner hdrs and cause
		 * OoO.
		 */
		gh = skb_gro_header(skb, off_gnv + gh_len, off_gnv);
		if (unlikely(!gh))
			goto out;

		gro_hint = geneve_opt_gro_hint(gh, hint_off);
		if (!geneve_opt_gro_hint_validate_csum(skb, gh, gro_hint))
			goto out;
	}

	list_for_each_entry(p, head, list) {
		if (!NAPI_GRO_CB(p)->same_flow)
			continue;

		gh2 = (struct genevehdr *)(p->data + off_gnv);
		if (!geneve_hdr_match(skb, gh, gh2, hint_off)) {
			NAPI_GRO_CB(p)->same_flow = 0;
			continue;
		}
	}

	skb_gro_pull(skb, gh_len);
	skb_gro_postpull_rcsum(skb, gh, gh_len);
	if (likely(type == htons(ETH_P_TEB)))
		return call_gro_receive(eth_gro_receive, head, skb);

	ptype = gro_find_receive_by_type(type);
	if (!ptype)
		goto out;

	pp = call_gro_receive(ptype->callbacks.gro_receive, head, skb);
	flush = 0;

out:
	skb_gro_flush_final(skb, pp, flush);

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
	geneve_opt_gro_hint_off(gh, &type, &gh_len);

	/* since skb->encapsulation is set, eth_gro_complete() sets the inner mac header */
	if (likely(type == htons(ETH_P_TEB)))
		return eth_gro_complete(skb, nhoff + gh_len);

	ptype = gro_find_complete_by_type(type);
	if (ptype)
		err = ptype->callbacks.gro_complete(skb, nhoff + gh_len);

	skb_set_inner_mac_header(skb, nhoff + gh_len);

	return err;
}

/* Create new listen socket if needed */
static struct geneve_sock *geneve_socket_create(struct net *net, __be16 port,
						bool ipv6, bool ipv6_rx_csum)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_sock *gs;
	struct socket *sock;
	struct udp_tunnel_sock_cfg tunnel_cfg;
	int h;

	gs = kzalloc(sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return ERR_PTR(-ENOMEM);

	sock = geneve_create_sock(net, ipv6, port, ipv6_rx_csum);
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
	tunnel_cfg.encap_err_lookup = geneve_udp_encap_err_lookup;
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
					    __be16 dst_port,
					    bool gro_hint)
{
	struct geneve_sock *gs;

	list_for_each_entry(gs, &gn->sock_list, list) {
		if (inet_sk(gs->sock->sk)->inet_sport == dst_port &&
		    geneve_get_sk_family(gs) == family &&
		    gs->gro_hint == gro_hint) {
			return gs;
		}
	}
	return NULL;
}

static int geneve_sock_add(struct geneve_dev *geneve, bool ipv6)
{
	struct net *net = geneve->net;
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	bool gro_hint = geneve->cfg.gro_hint;
	struct geneve_dev_node *node;
	struct geneve_sock *gs;
	__u8 vni[3];
	__u32 hash;

	gs = geneve_find_sock(gn, ipv6 ? AF_INET6 : AF_INET,
			      geneve->cfg.info.key.tp_dst, gro_hint);
	if (gs) {
		gs->refcnt++;
		goto out;
	}

	gs = geneve_socket_create(net, geneve->cfg.info.key.tp_dst, ipv6,
				  geneve->cfg.use_udp6_rx_checksums);
	if (IS_ERR(gs))
		return PTR_ERR(gs);

out:
	gs->collect_md = geneve->cfg.collect_md;
	gs->gro_hint = gro_hint;
#if IS_ENABLED(CONFIG_IPV6)
	if (ipv6) {
		rcu_assign_pointer(geneve->sock6, gs);
		node = &geneve->hlist6;
	} else
#endif
	{
		rcu_assign_pointer(geneve->sock4, gs);
		node = &geneve->hlist4;
	}
	node->geneve = geneve;

	tunnel_id_to_vni(geneve->cfg.info.key.tun_id, vni);
	hash = geneve_net_vni_hash(vni);
	hlist_add_head_rcu(&node->hlist, &gs->vni_list[hash]);
	return 0;
}

static int geneve_open(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	bool metadata = geneve->cfg.collect_md;
	bool ipv4, ipv6;
	int ret = 0;

	ipv6 = geneve->cfg.info.mode & IP_TUNNEL_INFO_IPV6 || metadata;
	ipv4 = !ipv6 || metadata;
#if IS_ENABLED(CONFIG_IPV6)
	if (ipv6) {
		ret = geneve_sock_add(geneve, true);
		if (ret < 0 && ret != -EAFNOSUPPORT)
			ipv4 = false;
	}
#endif
	if (ipv4)
		ret = geneve_sock_add(geneve, false);
	if (ret < 0)
		geneve_sock_release(geneve);

	return ret;
}

static int geneve_stop(struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);

	hlist_del_init_rcu(&geneve->hlist4.hlist);
#if IS_ENABLED(CONFIG_IPV6)
	hlist_del_init_rcu(&geneve->hlist6.hlist);
#endif
	geneve_sock_release(geneve);
	return 0;
}

static void geneve_build_header(struct genevehdr *geneveh,
				const struct ip_tunnel_info *info,
				__be16 inner_proto)
{
	geneveh->ver = GENEVE_VER;
	geneveh->opt_len = info->options_len / 4;
	geneveh->oam = test_bit(IP_TUNNEL_OAM_BIT, info->key.tun_flags);
	geneveh->critical = test_bit(IP_TUNNEL_CRIT_OPT_BIT,
				     info->key.tun_flags);
	geneveh->rsvd1 = 0;
	tunnel_id_to_vni(info->key.tun_id, geneveh->vni);
	geneveh->proto_type = inner_proto;
	geneveh->rsvd2 = 0;

	if (test_bit(IP_TUNNEL_GENEVE_OPT_BIT, info->key.tun_flags))
		ip_tunnel_info_opts_get(geneveh->options, info);
}

static int geneve_build_gro_hint_opt(const struct geneve_dev *geneve,
				     struct sk_buff *skb)
{
	struct geneve_skb_cb *cb = GENEVE_SKB_CB(skb);
	struct geneve_opt_gro_hint *hint;
	unsigned int nhlen;
	bool nested_is_v6;
	int id;

	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(struct geneve_skb_cb));
	cb->gro_hint_len = 0;

	/* Try to add the GRO hint only in case of double encap. */
	if (!geneve->cfg.gro_hint || !skb->encapsulation)
		return 0;

	/*
	 * The nested headers must fit the geneve opt len fields and the
	 * nested encap must carry a nested transport (UDP) header.
	 */
	nhlen = skb_inner_mac_header(skb) - skb->data;
	if (nhlen > 255 || !skb_transport_header_was_set(skb) ||
	    skb->inner_protocol_type != ENCAP_TYPE_ETHER ||
	    (skb_transport_offset(skb) + sizeof(struct udphdr) > nhlen))
		return 0;

	id = proto_to_id(skb->inner_protocol);
	if (id < 0)
		return 0;

	nested_is_v6 = skb->protocol == htons(ETH_P_IPV6);
	if (nested_is_v6) {
		int start = skb_network_offset(skb) + sizeof(struct ipv6hdr);
		u8 proto = ipv6_hdr(skb)->nexthdr;
		__be16 foff;

		if (ipv6_skip_exthdr(skb, start, &proto, &foff) < 0 ||
		    proto != IPPROTO_UDP)
			return 0;
	} else {
		if (ip_hdr(skb)->protocol != IPPROTO_UDP)
			return 0;
	}

	hint = &cb->gro_hint;
	memset(hint, 0, sizeof(*hint));
	hint->inner_proto_id = id;
	hint->nested_is_v6 = skb->protocol == htons(ETH_P_IPV6);
	hint->nested_nh_offset = skb_network_offset(skb);
	hint->nested_tp_offset = skb_transport_offset(skb);
	hint->nested_hdr_len = nhlen;
	cb->gro_hint_len = GENEVE_OPT_GRO_HINT_SIZE;
	return GENEVE_OPT_GRO_HINT_SIZE;
}

static void geneve_put_gro_hint_opt(struct genevehdr *gnvh, int opt_size,
				    const struct geneve_opt_gro_hint *hint)
{
	struct geneve_opt *gro_opt;

	/* geneve_build_header() did not took in account the GRO hint. */
	gnvh->opt_len = (opt_size + GENEVE_OPT_GRO_HINT_SIZE) >> 2;

	gro_opt = (void *)(gnvh + 1) + opt_size;
	memset(gro_opt, 0, sizeof(*gro_opt));

	gro_opt->opt_class = htons(GENEVE_OPT_NETDEV_CLASS);
	gro_opt->type = GENEVE_OPT_GRO_HINT_TYPE;
	gro_opt->length = GENEVE_OPT_GRO_HINT_LEN;
	memcpy(gro_opt + 1, hint, sizeof(*hint));
}

static int geneve_build_skb(struct dst_entry *dst, struct sk_buff *skb,
			    const struct ip_tunnel_info *info,
			    const struct geneve_dev *geneve, int ip_hdr_len)
{
	bool udp_sum = test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags);
	bool inner_proto_inherit = geneve->cfg.inner_proto_inherit;
	bool xnet = !net_eq(geneve->net, dev_net(geneve->dev));
	struct geneve_skb_cb *cb = GENEVE_SKB_CB(skb);
	struct genevehdr *gnvh;
	__be16 inner_proto;
	bool double_encap;
	int min_headroom;
	int opt_size;
	int err;

	skb_reset_mac_header(skb);
	skb_scrub_packet(skb, xnet);

	opt_size =  info->options_len + cb->gro_hint_len;
	min_headroom = LL_RESERVED_SPACE(dst->dev) + dst->header_len +
		       GENEVE_BASE_HLEN + opt_size + ip_hdr_len;
	err = skb_cow_head(skb, min_headroom);
	if (unlikely(err))
		goto free_dst;

	double_encap = udp_tunnel_handle_partial(skb);
	err = udp_tunnel_handle_offloads(skb, udp_sum);
	if (err)
		goto free_dst;

	gnvh = __skb_push(skb, sizeof(*gnvh) + opt_size);
	inner_proto = inner_proto_inherit ? skb->protocol : htons(ETH_P_TEB);
	geneve_build_header(gnvh, info, inner_proto);

	if (cb->gro_hint_len)
		geneve_put_gro_hint_opt(gnvh, info->options_len, &cb->gro_hint);

	udp_tunnel_set_inner_protocol(skb, double_encap, inner_proto);
	return 0;

free_dst:
	dst_release(dst);
	return err;
}

static u8 geneve_get_dsfield(struct sk_buff *skb, struct net_device *dev,
			     const struct ip_tunnel_info *info,
			     bool *use_cache)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	u8 dsfield;

	dsfield = info->key.tos;
	if (dsfield == 1 && !geneve->cfg.collect_md) {
		dsfield = ip_tunnel_get_dsfield(ip_hdr(skb), skb);
		*use_cache = false;
	}

	return dsfield;
}

static int geneve_xmit_skb(struct sk_buff *skb, struct net_device *dev,
			   struct geneve_dev *geneve,
			   const struct ip_tunnel_info *info)
{
	struct geneve_sock *gs4 = rcu_dereference(geneve->sock4);
	const struct ip_tunnel_key *key = &info->key;
	struct rtable *rt;
	bool use_cache;
	__u8 tos, ttl;
	__be16 df = 0;
	__be32 saddr;
	__be16 sport;
	int err;

	if (skb_vlan_inet_prepare(skb, geneve->cfg.inner_proto_inherit))
		return -EINVAL;

	if (!gs4)
		return -EIO;

	use_cache = ip_tunnel_dst_cache_usable(skb, info);
	tos = geneve_get_dsfield(skb, dev, info, &use_cache);
	sport = udp_flow_src_port(geneve->net, skb,
				  geneve->cfg.port_min,
				  geneve->cfg.port_max, true);

	rt = udp_tunnel_dst_lookup(skb, dev, geneve->net, 0, &saddr,
				   &info->key,
				   sport, geneve->cfg.info.key.tp_dst, tos,
				   use_cache ?
				   (struct dst_cache *)&info->dst_cache : NULL);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	err = skb_tunnel_check_pmtu(skb, &rt->dst,
				    GENEVE_IPV4_HLEN + info->options_len +
				    geneve_build_gro_hint_opt(geneve, skb),
				    netif_is_any_bridge_port(dev));
	if (err < 0) {
		dst_release(&rt->dst);
		return err;
	} else if (err) {
		struct ip_tunnel_info *info;

		info = skb_tunnel_info(skb);
		if (info) {
			struct ip_tunnel_info *unclone;

			unclone = skb_tunnel_info_unclone(skb);
			if (unlikely(!unclone)) {
				dst_release(&rt->dst);
				return -ENOMEM;
			}

			unclone->key.u.ipv4.dst = saddr;
			unclone->key.u.ipv4.src = info->key.u.ipv4.dst;
		}

		if (!pskb_may_pull(skb, ETH_HLEN)) {
			dst_release(&rt->dst);
			return -EINVAL;
		}

		skb->protocol = eth_type_trans(skb, geneve->dev);
		__netif_rx(skb);
		dst_release(&rt->dst);
		return -EMSGSIZE;
	}

	tos = ip_tunnel_ecn_encap(tos, ip_hdr(skb), skb);
	if (geneve->cfg.collect_md) {
		ttl = key->ttl;

		df = test_bit(IP_TUNNEL_DONT_FRAGMENT_BIT, key->tun_flags) ?
		     htons(IP_DF) : 0;
	} else {
		if (geneve->cfg.ttl_inherit)
			ttl = ip_tunnel_get_ttl(ip_hdr(skb), skb);
		else
			ttl = key->ttl;
		ttl = ttl ? : ip4_dst_hoplimit(&rt->dst);

		if (geneve->cfg.df == GENEVE_DF_SET) {
			df = htons(IP_DF);
		} else if (geneve->cfg.df == GENEVE_DF_INHERIT) {
			struct ethhdr *eth = skb_eth_hdr(skb);

			if (ntohs(eth->h_proto) == ETH_P_IPV6) {
				df = htons(IP_DF);
			} else if (ntohs(eth->h_proto) == ETH_P_IP) {
				struct iphdr *iph = ip_hdr(skb);

				if (iph->frag_off & htons(IP_DF))
					df = htons(IP_DF);
			}
		}
	}

	err = geneve_build_skb(&rt->dst, skb, info, geneve,
			       sizeof(struct iphdr));
	if (unlikely(err))
		return err;

	udp_tunnel_xmit_skb(rt, gs4->sock->sk, skb, saddr, info->key.u.ipv4.dst,
			    tos, ttl, df, sport, geneve->cfg.info.key.tp_dst,
			    !net_eq(geneve->net, dev_net(geneve->dev)),
			    !test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags),
			    0);
	return 0;
}

#if IS_ENABLED(CONFIG_IPV6)
static int geneve6_xmit_skb(struct sk_buff *skb, struct net_device *dev,
			    struct geneve_dev *geneve,
			    const struct ip_tunnel_info *info)
{
	struct geneve_sock *gs6 = rcu_dereference(geneve->sock6);
	const struct ip_tunnel_key *key = &info->key;
	struct dst_entry *dst = NULL;
	struct in6_addr saddr;
	bool use_cache;
	__u8 prio, ttl;
	__be16 sport;
	int err;

	if (skb_vlan_inet_prepare(skb, geneve->cfg.inner_proto_inherit))
		return -EINVAL;

	if (!gs6)
		return -EIO;

	use_cache = ip_tunnel_dst_cache_usable(skb, info);
	prio = geneve_get_dsfield(skb, dev, info, &use_cache);
	sport = udp_flow_src_port(geneve->net, skb,
				  geneve->cfg.port_min,
				  geneve->cfg.port_max, true);

	dst = udp_tunnel6_dst_lookup(skb, dev, geneve->net, gs6->sock, 0,
				     &saddr, key, sport,
				     geneve->cfg.info.key.tp_dst, prio,
				     use_cache ?
				     (struct dst_cache *)&info->dst_cache : NULL);
	if (IS_ERR(dst))
		return PTR_ERR(dst);

	err = skb_tunnel_check_pmtu(skb, dst,
				    GENEVE_IPV6_HLEN + info->options_len +
				    geneve_build_gro_hint_opt(geneve, skb),
				    netif_is_any_bridge_port(dev));
	if (err < 0) {
		dst_release(dst);
		return err;
	} else if (err) {
		struct ip_tunnel_info *info = skb_tunnel_info(skb);

		if (info) {
			struct ip_tunnel_info *unclone;

			unclone = skb_tunnel_info_unclone(skb);
			if (unlikely(!unclone)) {
				dst_release(dst);
				return -ENOMEM;
			}

			unclone->key.u.ipv6.dst = saddr;
			unclone->key.u.ipv6.src = info->key.u.ipv6.dst;
		}

		if (!pskb_may_pull(skb, ETH_HLEN)) {
			dst_release(dst);
			return -EINVAL;
		}

		skb->protocol = eth_type_trans(skb, geneve->dev);
		__netif_rx(skb);
		dst_release(dst);
		return -EMSGSIZE;
	}

	prio = ip_tunnel_ecn_encap(prio, ip_hdr(skb), skb);
	if (geneve->cfg.collect_md) {
		ttl = key->ttl;
	} else {
		if (geneve->cfg.ttl_inherit)
			ttl = ip_tunnel_get_ttl(ip_hdr(skb), skb);
		else
			ttl = key->ttl;
		ttl = ttl ? : ip6_dst_hoplimit(dst);
	}
	err = geneve_build_skb(dst, skb, info, geneve, sizeof(struct ipv6hdr));
	if (unlikely(err))
		return err;

	udp_tunnel6_xmit_skb(dst, gs6->sock->sk, skb, dev,
			     &saddr, &key->u.ipv6.dst, prio, ttl,
			     info->key.label, sport, geneve->cfg.info.key.tp_dst,
			     !test_bit(IP_TUNNEL_CSUM_BIT,
				       info->key.tun_flags),
			     0);
	return 0;
}
#endif

static netdev_tx_t geneve_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct ip_tunnel_info *info = NULL;
	int err;

	if (geneve->cfg.collect_md) {
		info = skb_tunnel_info(skb);
		if (unlikely(!info || !(info->mode & IP_TUNNEL_INFO_TX))) {
			netdev_dbg(dev, "no tunnel metadata\n");
			dev_kfree_skb(skb);
			dev_dstats_tx_dropped(dev);
			return NETDEV_TX_OK;
		}
	} else {
		info = &geneve->cfg.info;
	}

	rcu_read_lock();
#if IS_ENABLED(CONFIG_IPV6)
	if (info->mode & IP_TUNNEL_INFO_IPV6)
		err = geneve6_xmit_skb(skb, dev, geneve, info);
	else
#endif
		err = geneve_xmit_skb(skb, dev, geneve, info);
	rcu_read_unlock();

	if (likely(!err))
		return NETDEV_TX_OK;

	if (err != -EMSGSIZE)
		dev_kfree_skb(skb);

	if (err == -ELOOP)
		DEV_STATS_INC(dev, collisions);
	else if (err == -ENETUNREACH)
		DEV_STATS_INC(dev, tx_carrier_errors);

	DEV_STATS_INC(dev, tx_errors);
	return NETDEV_TX_OK;
}

static int geneve_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu > dev->max_mtu)
		new_mtu = dev->max_mtu;
	else if (new_mtu < dev->min_mtu)
		new_mtu = dev->min_mtu;

	WRITE_ONCE(dev->mtu, new_mtu);
	return 0;
}

static int geneve_fill_metadata_dst(struct net_device *dev, struct sk_buff *skb)
{
	struct ip_tunnel_info *info = skb_tunnel_info(skb);
	struct geneve_dev *geneve = netdev_priv(dev);
	__be16 sport;

	if (ip_tunnel_info_af(info) == AF_INET) {
		struct rtable *rt;
		struct geneve_sock *gs4 = rcu_dereference(geneve->sock4);
		bool use_cache;
		__be32 saddr;
		u8 tos;

		if (!gs4)
			return -EIO;

		use_cache = ip_tunnel_dst_cache_usable(skb, info);
		tos = geneve_get_dsfield(skb, dev, info, &use_cache);
		sport = udp_flow_src_port(geneve->net, skb,
					  geneve->cfg.port_min,
					  geneve->cfg.port_max, true);

		rt = udp_tunnel_dst_lookup(skb, dev, geneve->net, 0, &saddr,
					   &info->key,
					   sport, geneve->cfg.info.key.tp_dst,
					   tos,
					   use_cache ? &info->dst_cache : NULL);
		if (IS_ERR(rt))
			return PTR_ERR(rt);

		ip_rt_put(rt);
		info->key.u.ipv4.src = saddr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (ip_tunnel_info_af(info) == AF_INET6) {
		struct dst_entry *dst;
		struct geneve_sock *gs6 = rcu_dereference(geneve->sock6);
		struct in6_addr saddr;
		bool use_cache;
		u8 prio;

		if (!gs6)
			return -EIO;

		use_cache = ip_tunnel_dst_cache_usable(skb, info);
		prio = geneve_get_dsfield(skb, dev, info, &use_cache);
		sport = udp_flow_src_port(geneve->net, skb,
					  geneve->cfg.port_min,
					  geneve->cfg.port_max, true);

		dst = udp_tunnel6_dst_lookup(skb, dev, geneve->net, gs6->sock, 0,
					     &saddr, &info->key, sport,
					     geneve->cfg.info.key.tp_dst, prio,
					     use_cache ? &info->dst_cache : NULL);
		if (IS_ERR(dst))
			return PTR_ERR(dst);

		dst_release(dst);
		info->key.u.ipv6.src = saddr;
#endif
	} else {
		return -EINVAL;
	}

	info->key.tp_src = sport;
	info->key.tp_dst = geneve->cfg.info.key.tp_dst;
	return 0;
}

static const struct net_device_ops geneve_netdev_ops = {
	.ndo_init		= geneve_init,
	.ndo_uninit		= geneve_uninit,
	.ndo_open		= geneve_open,
	.ndo_stop		= geneve_stop,
	.ndo_start_xmit		= geneve_xmit,
	.ndo_change_mtu		= geneve_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_fill_metadata_dst	= geneve_fill_metadata_dst,
};

static void geneve_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *drvinfo)
{
	strscpy(drvinfo->version, GENEVE_NETDEV_VER, sizeof(drvinfo->version));
	strscpy(drvinfo->driver, "geneve", sizeof(drvinfo->driver));
}

static const struct ethtool_ops geneve_ethtool_ops = {
	.get_drvinfo	= geneve_get_drvinfo,
	.get_link	= ethtool_op_get_link,
};

/* Info for udev, that this is a virtual tunnel endpoint */
static const struct device_type geneve_type = {
	.name = "geneve",
};

/* Calls the ndo_udp_tunnel_add of the caller in order to
 * supply the listening GENEVE udp ports. Callers are expected
 * to implement the ndo_udp_tunnel_add.
 */
static void geneve_offload_rx_ports(struct net_device *dev, bool push)
{
	struct net *net = dev_net(dev);
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_sock *gs;

	ASSERT_RTNL();

	list_for_each_entry(gs, &gn->sock_list, list) {
		if (push) {
			udp_tunnel_push_rx_port(dev, gs->sock,
						UDP_TUNNEL_TYPE_GENEVE);
		} else {
			udp_tunnel_drop_rx_port(dev, gs->sock,
						UDP_TUNNEL_TYPE_GENEVE);
		}
	}
}

/* Initialize the device structure. */
static void geneve_setup(struct net_device *dev)
{
	ether_setup(dev);

	dev->netdev_ops = &geneve_netdev_ops;
	dev->ethtool_ops = &geneve_ethtool_ops;
	dev->needs_free_netdev = true;

	SET_NETDEV_DEVTYPE(dev, &geneve_type);

	dev->features    |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_FRAGLIST;
	dev->features    |= NETIF_F_RXCSUM;
	dev->features    |= NETIF_F_GSO_SOFTWARE;

	/* Partial features are disabled by default. */
	dev->hw_features |= NETIF_F_SG | NETIF_F_HW_CSUM | NETIF_F_FRAGLIST;
	dev->hw_features |= NETIF_F_RXCSUM;
	dev->hw_features |= NETIF_F_GSO_SOFTWARE;
	dev->hw_features |= UDP_TUNNEL_PARTIAL_FEATURES;
	dev->hw_features |= NETIF_F_GSO_PARTIAL;

	dev->hw_enc_features = dev->hw_features;
	dev->gso_partial_features = UDP_TUNNEL_PARTIAL_FEATURES;
	dev->mangleid_features = NETIF_F_GSO_PARTIAL;

	dev->pcpu_stat_type = NETDEV_PCPU_STAT_DSTATS;
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
	dev->lltx = true;
	eth_hw_addr_random(dev);
}

static const struct nla_policy geneve_policy[IFLA_GENEVE_MAX + 1] = {
	[IFLA_GENEVE_UNSPEC]		= { .strict_start_type = IFLA_GENEVE_INNER_PROTO_INHERIT },
	[IFLA_GENEVE_ID]		= { .type = NLA_U32 },
	[IFLA_GENEVE_REMOTE]		= { .len = sizeof_field(struct iphdr, daddr) },
	[IFLA_GENEVE_REMOTE6]		= { .len = sizeof(struct in6_addr) },
	[IFLA_GENEVE_TTL]		= { .type = NLA_U8 },
	[IFLA_GENEVE_TOS]		= { .type = NLA_U8 },
	[IFLA_GENEVE_LABEL]		= { .type = NLA_U32 },
	[IFLA_GENEVE_PORT]		= { .type = NLA_U16 },
	[IFLA_GENEVE_COLLECT_METADATA]	= { .type = NLA_FLAG },
	[IFLA_GENEVE_UDP_CSUM]		= { .type = NLA_U8 },
	[IFLA_GENEVE_UDP_ZERO_CSUM6_TX]	= { .type = NLA_U8 },
	[IFLA_GENEVE_UDP_ZERO_CSUM6_RX]	= { .type = NLA_U8 },
	[IFLA_GENEVE_TTL_INHERIT]	= { .type = NLA_U8 },
	[IFLA_GENEVE_DF]		= { .type = NLA_U8 },
	[IFLA_GENEVE_INNER_PROTO_INHERIT]	= { .type = NLA_FLAG },
	[IFLA_GENEVE_PORT_RANGE]	= NLA_POLICY_EXACT_LEN(sizeof(struct ifla_geneve_port_range)),
	[IFLA_GENEVE_GRO_HINT]		= { .type = NLA_FLAG },
};

static int geneve_validate(struct nlattr *tb[], struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	if (tb[IFLA_ADDRESS]) {
		if (nla_len(tb[IFLA_ADDRESS]) != ETH_ALEN) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_ADDRESS],
					    "Provided link layer address is not Ethernet");
			return -EINVAL;
		}

		if (!is_valid_ether_addr(nla_data(tb[IFLA_ADDRESS]))) {
			NL_SET_ERR_MSG_ATTR(extack, tb[IFLA_ADDRESS],
					    "Provided Ethernet address is not unicast");
			return -EADDRNOTAVAIL;
		}
	}

	if (!data) {
		NL_SET_ERR_MSG(extack,
			       "Not enough attributes provided to perform the operation");
		return -EINVAL;
	}

	if (data[IFLA_GENEVE_ID]) {
		__u32 vni =  nla_get_u32(data[IFLA_GENEVE_ID]);

		if (vni >= GENEVE_N_VID) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_ID],
					    "Geneve ID must be lower than 16777216");
			return -ERANGE;
		}
	}

	if (data[IFLA_GENEVE_DF]) {
		enum ifla_geneve_df df = nla_get_u8(data[IFLA_GENEVE_DF]);

		if (df < 0 || df > GENEVE_DF_MAX) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_DF],
					    "Invalid DF attribute");
			return -EINVAL;
		}
	}

	if (data[IFLA_GENEVE_PORT_RANGE]) {
		const struct ifla_geneve_port_range *p;

		p = nla_data(data[IFLA_GENEVE_PORT_RANGE]);
		if (ntohs(p->high) < ntohs(p->low)) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_PORT_RANGE],
					    "Invalid source port range");
			return -EINVAL;
		}
	}

	return 0;
}

static struct geneve_dev *geneve_find_dev(struct geneve_net *gn,
					  const struct ip_tunnel_info *info,
					  bool *tun_on_same_port,
					  bool *tun_collect_md)
{
	struct geneve_dev *geneve, *t = NULL;

	*tun_on_same_port = false;
	*tun_collect_md = false;
	list_for_each_entry(geneve, &gn->geneve_list, next) {
		if (info->key.tp_dst == geneve->cfg.info.key.tp_dst) {
			*tun_collect_md = geneve->cfg.collect_md;
			*tun_on_same_port = true;
		}
		if (info->key.tun_id == geneve->cfg.info.key.tun_id &&
		    info->key.tp_dst == geneve->cfg.info.key.tp_dst &&
		    !memcmp(&info->key.u, &geneve->cfg.info.key.u, sizeof(info->key.u)))
			t = geneve;
	}
	return t;
}

static bool is_tnl_info_zero(const struct ip_tunnel_info *info)
{
	return !(info->key.tun_id || info->key.tos ||
		 !ip_tunnel_flags_empty(info->key.tun_flags) ||
		 info->key.ttl || info->key.label || info->key.tp_src ||
		 memchr_inv(&info->key.u, 0, sizeof(info->key.u)));
}

static bool geneve_dst_addr_equal(struct ip_tunnel_info *a,
				  struct ip_tunnel_info *b)
{
	if (ip_tunnel_info_af(a) == AF_INET)
		return a->key.u.ipv4.dst == b->key.u.ipv4.dst;
	else
		return ipv6_addr_equal(&a->key.u.ipv6.dst, &b->key.u.ipv6.dst);
}

static int geneve_configure(struct net *net, struct net_device *dev,
			    struct netlink_ext_ack *extack,
			    const struct geneve_config *cfg)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_dev *t, *geneve = netdev_priv(dev);
	const struct ip_tunnel_info *info = &cfg->info;
	bool tun_collect_md, tun_on_same_port;
	int err, encap_len;

	if (cfg->collect_md && !is_tnl_info_zero(info)) {
		NL_SET_ERR_MSG(extack,
			       "Device is externally controlled, so attributes (VNI, Port, and so on) must not be specified");
		return -EINVAL;
	}

	geneve->net = net;
	geneve->dev = dev;

	t = geneve_find_dev(gn, info, &tun_on_same_port, &tun_collect_md);
	if (t)
		return -EBUSY;

	/* make enough headroom for basic scenario */
	encap_len = GENEVE_BASE_HLEN + ETH_HLEN;
	if (!cfg->collect_md && ip_tunnel_info_af(info) == AF_INET) {
		encap_len += sizeof(struct iphdr);
		dev->max_mtu -= sizeof(struct iphdr);
	} else {
		encap_len += sizeof(struct ipv6hdr);
		dev->max_mtu -= sizeof(struct ipv6hdr);
	}
	dev->needed_headroom = encap_len + ETH_HLEN;

	if (cfg->collect_md) {
		if (tun_on_same_port) {
			NL_SET_ERR_MSG(extack,
				       "There can be only one externally controlled device on a destination port");
			return -EPERM;
		}
	} else {
		if (tun_collect_md) {
			NL_SET_ERR_MSG(extack,
				       "There already exists an externally controlled device on this destination port");
			return -EPERM;
		}
	}

	dst_cache_reset(&geneve->cfg.info.dst_cache);
	memcpy(&geneve->cfg, cfg, sizeof(*cfg));

	if (geneve->cfg.inner_proto_inherit) {
		dev->header_ops = NULL;
		dev->type = ARPHRD_NONE;
		dev->hard_header_len = 0;
		dev->addr_len = 0;
		dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	}

	err = register_netdevice(dev);
	if (err)
		return err;

	list_add(&geneve->next, &gn->geneve_list);
	return 0;
}

static void init_tnl_info(struct ip_tunnel_info *info, __u16 dst_port)
{
	memset(info, 0, sizeof(*info));
	info->key.tp_dst = htons(dst_port);
}

static int geneve_nl2info(struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack,
			  struct geneve_config *cfg, bool changelink)
{
	struct ip_tunnel_info *info = &cfg->info;
	int attrtype;

	if (data[IFLA_GENEVE_REMOTE] && data[IFLA_GENEVE_REMOTE6]) {
		NL_SET_ERR_MSG(extack,
			       "Cannot specify both IPv4 and IPv6 Remote addresses");
		return -EINVAL;
	}

	if (data[IFLA_GENEVE_REMOTE]) {
		if (changelink && (ip_tunnel_info_af(info) == AF_INET6)) {
			attrtype = IFLA_GENEVE_REMOTE;
			goto change_notsup;
		}

		info->key.u.ipv4.dst =
			nla_get_in_addr(data[IFLA_GENEVE_REMOTE]);

		if (ipv4_is_multicast(info->key.u.ipv4.dst)) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_REMOTE],
					    "Remote IPv4 address cannot be Multicast");
			return -EINVAL;
		}
	}

	if (data[IFLA_GENEVE_REMOTE6]) {
#if IS_ENABLED(CONFIG_IPV6)
		if (changelink && (ip_tunnel_info_af(info) == AF_INET)) {
			attrtype = IFLA_GENEVE_REMOTE6;
			goto change_notsup;
		}

		info->mode = IP_TUNNEL_INFO_IPV6;
		info->key.u.ipv6.dst =
			nla_get_in6_addr(data[IFLA_GENEVE_REMOTE6]);

		if (ipv6_addr_type(&info->key.u.ipv6.dst) &
		    IPV6_ADDR_LINKLOCAL) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_REMOTE6],
					    "Remote IPv6 address cannot be link-local");
			return -EINVAL;
		}
		if (ipv6_addr_is_multicast(&info->key.u.ipv6.dst)) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_REMOTE6],
					    "Remote IPv6 address cannot be Multicast");
			return -EINVAL;
		}
		__set_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags);
		cfg->use_udp6_rx_checksums = true;
#else
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_REMOTE6],
				    "IPv6 support not enabled in the kernel");
		return -EPFNOSUPPORT;
#endif
	}

	if (data[IFLA_GENEVE_ID]) {
		__u32 vni;
		__u8 tvni[3];
		__be64 tunid;

		vni = nla_get_u32(data[IFLA_GENEVE_ID]);
		tvni[0] = (vni & 0x00ff0000) >> 16;
		tvni[1] = (vni & 0x0000ff00) >> 8;
		tvni[2] =  vni & 0x000000ff;

		tunid = vni_to_tunnel_id(tvni);
		if (changelink && (tunid != info->key.tun_id)) {
			attrtype = IFLA_GENEVE_ID;
			goto change_notsup;
		}
		info->key.tun_id = tunid;
	}

	if (data[IFLA_GENEVE_TTL_INHERIT]) {
		if (nla_get_u8(data[IFLA_GENEVE_TTL_INHERIT]))
			cfg->ttl_inherit = true;
		else
			cfg->ttl_inherit = false;
	} else if (data[IFLA_GENEVE_TTL]) {
		info->key.ttl = nla_get_u8(data[IFLA_GENEVE_TTL]);
		cfg->ttl_inherit = false;
	}

	if (data[IFLA_GENEVE_TOS])
		info->key.tos = nla_get_u8(data[IFLA_GENEVE_TOS]);

	if (data[IFLA_GENEVE_DF])
		cfg->df = nla_get_u8(data[IFLA_GENEVE_DF]);

	if (data[IFLA_GENEVE_LABEL]) {
		info->key.label = nla_get_be32(data[IFLA_GENEVE_LABEL]) &
				  IPV6_FLOWLABEL_MASK;
		if (info->key.label && (!(info->mode & IP_TUNNEL_INFO_IPV6))) {
			NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_LABEL],
					    "Label attribute only applies for IPv6 Geneve devices");
			return -EINVAL;
		}
	}

	if (data[IFLA_GENEVE_PORT]) {
		if (changelink) {
			attrtype = IFLA_GENEVE_PORT;
			goto change_notsup;
		}
		info->key.tp_dst = nla_get_be16(data[IFLA_GENEVE_PORT]);
	}

	if (data[IFLA_GENEVE_PORT_RANGE]) {
		const struct ifla_geneve_port_range *p;

		if (changelink) {
			attrtype = IFLA_GENEVE_PORT_RANGE;
			goto change_notsup;
		}
		p = nla_data(data[IFLA_GENEVE_PORT_RANGE]);
		cfg->port_min = ntohs(p->low);
		cfg->port_max = ntohs(p->high);
	}

	if (data[IFLA_GENEVE_COLLECT_METADATA]) {
		if (changelink) {
			attrtype = IFLA_GENEVE_COLLECT_METADATA;
			goto change_notsup;
		}
		cfg->collect_md = true;
	}

	if (data[IFLA_GENEVE_UDP_CSUM]) {
		if (changelink) {
			attrtype = IFLA_GENEVE_UDP_CSUM;
			goto change_notsup;
		}
		if (nla_get_u8(data[IFLA_GENEVE_UDP_CSUM]))
			__set_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags);
	}

	if (data[IFLA_GENEVE_UDP_ZERO_CSUM6_TX]) {
#if IS_ENABLED(CONFIG_IPV6)
		if (changelink) {
			attrtype = IFLA_GENEVE_UDP_ZERO_CSUM6_TX;
			goto change_notsup;
		}
		if (nla_get_u8(data[IFLA_GENEVE_UDP_ZERO_CSUM6_TX]))
			__clear_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags);
#else
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_UDP_ZERO_CSUM6_TX],
				    "IPv6 support not enabled in the kernel");
		return -EPFNOSUPPORT;
#endif
	}

	if (data[IFLA_GENEVE_UDP_ZERO_CSUM6_RX]) {
#if IS_ENABLED(CONFIG_IPV6)
		if (changelink) {
			attrtype = IFLA_GENEVE_UDP_ZERO_CSUM6_RX;
			goto change_notsup;
		}
		if (nla_get_u8(data[IFLA_GENEVE_UDP_ZERO_CSUM6_RX]))
			cfg->use_udp6_rx_checksums = false;
#else
		NL_SET_ERR_MSG_ATTR(extack, data[IFLA_GENEVE_UDP_ZERO_CSUM6_RX],
				    "IPv6 support not enabled in the kernel");
		return -EPFNOSUPPORT;
#endif
	}

	if (data[IFLA_GENEVE_INNER_PROTO_INHERIT]) {
		if (changelink) {
			attrtype = IFLA_GENEVE_INNER_PROTO_INHERIT;
			goto change_notsup;
		}
		cfg->inner_proto_inherit = true;
	}

	if (data[IFLA_GENEVE_GRO_HINT]) {
		if (changelink) {
			attrtype = IFLA_GENEVE_GRO_HINT;
			goto change_notsup;
		}
		cfg->gro_hint = true;
	}

	return 0;
change_notsup:
	NL_SET_ERR_MSG_ATTR(extack, data[attrtype],
			    "Changing VNI, Port, endpoint IP address family, external, inner_proto_inherit, gro_hint and UDP checksum attributes are not supported");
	return -EOPNOTSUPP;
}

static void geneve_link_config(struct net_device *dev,
			       struct ip_tunnel_info *info, struct nlattr *tb[])
{
	struct geneve_dev *geneve = netdev_priv(dev);
	int ldev_mtu = 0;

	if (tb[IFLA_MTU]) {
		geneve_change_mtu(dev, nla_get_u32(tb[IFLA_MTU]));
		return;
	}

	switch (ip_tunnel_info_af(info)) {
	case AF_INET: {
		struct flowi4 fl4 = { .daddr = info->key.u.ipv4.dst };
		struct rtable *rt = ip_route_output_key(geneve->net, &fl4);

		if (!IS_ERR(rt) && rt->dst.dev) {
			ldev_mtu = rt->dst.dev->mtu - GENEVE_IPV4_HLEN;
			ip_rt_put(rt);
		}
		break;
	}
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6: {
		struct rt6_info *rt;

		if (!__in6_dev_get(dev))
			break;

		rt = rt6_lookup(geneve->net, &info->key.u.ipv6.dst, NULL, 0,
				NULL, 0);

		if (rt && rt->dst.dev)
			ldev_mtu = rt->dst.dev->mtu - GENEVE_IPV6_HLEN;
		ip6_rt_put(rt);
		break;
	}
#endif
	}

	if (ldev_mtu <= 0)
		return;

	geneve_change_mtu(dev, ldev_mtu - info->options_len);
}

static int geneve_newlink(struct net_device *dev,
			  struct rtnl_newlink_params *params,
			  struct netlink_ext_ack *extack)
{
	struct net *link_net = rtnl_newlink_link_net(params);
	struct nlattr **data = params->data;
	struct nlattr **tb = params->tb;
	struct geneve_config cfg = {
		.df = GENEVE_DF_UNSET,
		.use_udp6_rx_checksums = false,
		.ttl_inherit = false,
		.collect_md = false,
		.port_min = 1,
		.port_max = USHRT_MAX,
	};
	int err;

	init_tnl_info(&cfg.info, GENEVE_UDP_PORT);
	err = geneve_nl2info(tb, data, extack, &cfg, false);
	if (err)
		return err;

	err = geneve_configure(link_net, dev, extack, &cfg);
	if (err)
		return err;

	geneve_link_config(dev, &cfg.info, tb);

	return 0;
}

/* Quiesces the geneve device data path for both TX and RX.
 *
 * On transmit geneve checks for non-NULL geneve_sock before it proceeds.
 * So, if we set that socket to NULL under RCU and wait for synchronize_net()
 * to complete for the existing set of in-flight packets to be transmitted,
 * then we would have quiesced the transmit data path. All the future packets
 * will get dropped until we unquiesce the data path.
 *
 * On receive geneve dereference the geneve_sock stashed in the socket. So,
 * if we set that to NULL under RCU and wait for synchronize_net() to
 * complete, then we would have quiesced the receive data path.
 */
static void geneve_quiesce(struct geneve_dev *geneve, struct geneve_sock **gs4,
			   struct geneve_sock **gs6)
{
	*gs4 = rtnl_dereference(geneve->sock4);
	rcu_assign_pointer(geneve->sock4, NULL);
	if (*gs4)
		rcu_assign_sk_user_data((*gs4)->sock->sk, NULL);
#if IS_ENABLED(CONFIG_IPV6)
	*gs6 = rtnl_dereference(geneve->sock6);
	rcu_assign_pointer(geneve->sock6, NULL);
	if (*gs6)
		rcu_assign_sk_user_data((*gs6)->sock->sk, NULL);
#else
	*gs6 = NULL;
#endif
	synchronize_net();
}

/* Resumes the geneve device data path for both TX and RX. */
static void geneve_unquiesce(struct geneve_dev *geneve, struct geneve_sock *gs4,
			     struct geneve_sock __maybe_unused *gs6)
{
	rcu_assign_pointer(geneve->sock4, gs4);
	if (gs4)
		rcu_assign_sk_user_data(gs4->sock->sk, gs4);
#if IS_ENABLED(CONFIG_IPV6)
	rcu_assign_pointer(geneve->sock6, gs6);
	if (gs6)
		rcu_assign_sk_user_data(gs6->sock->sk, gs6);
#endif
	synchronize_net();
}

static int geneve_changelink(struct net_device *dev, struct nlattr *tb[],
			     struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct geneve_sock *gs4, *gs6;
	struct geneve_config cfg;
	int err;

	/* If the geneve device is configured for metadata (or externally
	 * controlled, for example, OVS), then nothing can be changed.
	 */
	if (geneve->cfg.collect_md)
		return -EOPNOTSUPP;

	/* Start with the existing info. */
	memcpy(&cfg, &geneve->cfg, sizeof(cfg));
	err = geneve_nl2info(tb, data, extack, &cfg, true);
	if (err)
		return err;

	if (!geneve_dst_addr_equal(&geneve->cfg.info, &cfg.info)) {
		dst_cache_reset(&cfg.info.dst_cache);
		geneve_link_config(dev, &cfg.info, tb);
	}

	geneve_quiesce(geneve, &gs4, &gs6);
	memcpy(&geneve->cfg, &cfg, sizeof(cfg));
	geneve_unquiesce(geneve, gs4, gs6);

	return 0;
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
		nla_total_size(sizeof(__u8)) +	/* IFLA_GENEVE_DF */
		nla_total_size(sizeof(__be32)) +  /* IFLA_GENEVE_LABEL */
		nla_total_size(sizeof(__be16)) +  /* IFLA_GENEVE_PORT */
		nla_total_size(0) +	 /* IFLA_GENEVE_COLLECT_METADATA */
		nla_total_size(sizeof(__u8)) + /* IFLA_GENEVE_UDP_CSUM */
		nla_total_size(sizeof(__u8)) + /* IFLA_GENEVE_UDP_ZERO_CSUM6_TX */
		nla_total_size(sizeof(__u8)) + /* IFLA_GENEVE_UDP_ZERO_CSUM6_RX */
		nla_total_size(sizeof(__u8)) + /* IFLA_GENEVE_TTL_INHERIT */
		nla_total_size(0) +	 /* IFLA_GENEVE_INNER_PROTO_INHERIT */
		nla_total_size(sizeof(struct ifla_geneve_port_range)) + /* IFLA_GENEVE_PORT_RANGE */
		nla_total_size(0) +	 /* IFLA_GENEVE_GRO_HINT */
		0;
}

static int geneve_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct geneve_dev *geneve = netdev_priv(dev);
	struct ip_tunnel_info *info = &geneve->cfg.info;
	bool ttl_inherit = geneve->cfg.ttl_inherit;
	bool metadata = geneve->cfg.collect_md;
	struct ifla_geneve_port_range ports = {
		.low	= htons(geneve->cfg.port_min),
		.high	= htons(geneve->cfg.port_max),
	};
	__u8 tmp_vni[3];
	__u32 vni;

	tunnel_id_to_vni(info->key.tun_id, tmp_vni);
	vni = (tmp_vni[0] << 16) | (tmp_vni[1] << 8) | tmp_vni[2];
	if (nla_put_u32(skb, IFLA_GENEVE_ID, vni))
		goto nla_put_failure;

	if (!metadata && ip_tunnel_info_af(info) == AF_INET) {
		if (nla_put_in_addr(skb, IFLA_GENEVE_REMOTE,
				    info->key.u.ipv4.dst))
			goto nla_put_failure;
		if (nla_put_u8(skb, IFLA_GENEVE_UDP_CSUM,
			       test_bit(IP_TUNNEL_CSUM_BIT,
					info->key.tun_flags)))
			goto nla_put_failure;

#if IS_ENABLED(CONFIG_IPV6)
	} else if (!metadata) {
		if (nla_put_in6_addr(skb, IFLA_GENEVE_REMOTE6,
				     &info->key.u.ipv6.dst))
			goto nla_put_failure;
		if (nla_put_u8(skb, IFLA_GENEVE_UDP_ZERO_CSUM6_TX,
			       !test_bit(IP_TUNNEL_CSUM_BIT,
					 info->key.tun_flags)))
			goto nla_put_failure;
#endif
	}

	if (nla_put_u8(skb, IFLA_GENEVE_TTL, info->key.ttl) ||
	    nla_put_u8(skb, IFLA_GENEVE_TOS, info->key.tos) ||
	    nla_put_be32(skb, IFLA_GENEVE_LABEL, info->key.label))
		goto nla_put_failure;

	if (nla_put_u8(skb, IFLA_GENEVE_DF, geneve->cfg.df))
		goto nla_put_failure;

	if (nla_put_be16(skb, IFLA_GENEVE_PORT, info->key.tp_dst))
		goto nla_put_failure;

	if (metadata && nla_put_flag(skb, IFLA_GENEVE_COLLECT_METADATA))
		goto nla_put_failure;

#if IS_ENABLED(CONFIG_IPV6)
	if (nla_put_u8(skb, IFLA_GENEVE_UDP_ZERO_CSUM6_RX,
		       !geneve->cfg.use_udp6_rx_checksums))
		goto nla_put_failure;
#endif

	if (nla_put_u8(skb, IFLA_GENEVE_TTL_INHERIT, ttl_inherit))
		goto nla_put_failure;

	if (geneve->cfg.inner_proto_inherit &&
	    nla_put_flag(skb, IFLA_GENEVE_INNER_PROTO_INHERIT))
		goto nla_put_failure;

	if (nla_put(skb, IFLA_GENEVE_PORT_RANGE, sizeof(ports), &ports))
		goto nla_put_failure;

	if (geneve->cfg.gro_hint &&
	    nla_put_flag(skb, IFLA_GENEVE_GRO_HINT))
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
	.changelink	= geneve_changelink,
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
	struct geneve_config cfg = {
		.df = GENEVE_DF_UNSET,
		.use_udp6_rx_checksums = true,
		.ttl_inherit = false,
		.collect_md = true,
		.port_min = 1,
		.port_max = USHRT_MAX,
	};

	memset(tb, 0, sizeof(tb));
	dev = rtnl_create_link(net, name, name_assign_type,
			       &geneve_link_ops, tb, NULL);
	if (IS_ERR(dev))
		return dev;

	init_tnl_info(&cfg.info, dst_port);
	err = geneve_configure(net, dev, NULL, &cfg);
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

	err = rtnl_configure_link(dev, NULL, 0, NULL);
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
		geneve_offload_rx_ports(dev, true);
	else if (event == NETDEV_UDP_TUNNEL_DROP_INFO)
		geneve_offload_rx_ports(dev, false);

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

static void __net_exit geneve_exit_rtnl_net(struct net *net,
					    struct list_head *dev_to_kill)
{
	struct geneve_net *gn = net_generic(net, geneve_net_id);
	struct geneve_dev *geneve, *next;

	list_for_each_entry_safe(geneve, next, &gn->geneve_list, next)
		geneve_dellink(geneve->dev, dev_to_kill);
}

static void __net_exit geneve_exit_net(struct net *net)
{
	const struct geneve_net *gn = net_generic(net, geneve_net_id);

	WARN_ON_ONCE(!list_empty(&gn->sock_list));
}

static struct pernet_operations geneve_net_ops = {
	.init = geneve_init_net,
	.exit_rtnl = geneve_exit_rtnl_net,
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
