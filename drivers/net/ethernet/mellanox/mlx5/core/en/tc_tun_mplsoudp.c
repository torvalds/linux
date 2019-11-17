// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/bareudp.h>
#include <net/mpls.h>
#include "en/tc_tun.h"

static bool can_offload(struct mlx5e_priv *priv)
{
	return MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, reformat_l3_tunnel_to_l2);
}

static int calc_hlen(struct mlx5e_encap_entry *e)
{
	return sizeof(struct udphdr) + MPLS_HLEN;
}

static int init_encap_attr(struct net_device *tunnel_dev,
			   struct mlx5e_priv *priv,
			   struct mlx5e_encap_entry *e,
			   struct netlink_ext_ack *extack)
{
	e->tunnel = &mplsoudp_tunnel;
	e->reformat_type = MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL;
	return 0;
}

static inline __be32 mpls_label_id_field(__be32 label, u8 tos, u8 ttl)
{
	u32 res;

	/* mpls label is 32 bits long and construction as follows:
	 * 20 bits label
	 * 3 bits tos
	 * 1 bit bottom of stack. Since we support only one label, this bit is
	 *       always set.
	 * 8 bits TTL
	 */
	res = be32_to_cpu(label) << 12 | 1 << 8 | (tos & 7) <<  9 | ttl;
	return cpu_to_be32(res);
}

static int generate_ip_tun_hdr(char buf[],
			       __u8 *ip_proto,
			       struct mlx5e_encap_entry *r)
{
	const struct ip_tunnel_key *tun_key = &r->tun_info->key;
	__be32 tun_id = tunnel_id_to_key32(tun_key->tun_id);
	struct udphdr *udp = (struct udphdr *)(buf);
	struct mpls_shim_hdr *mpls;

	mpls = (struct mpls_shim_hdr *)(udp + 1);
	*ip_proto = IPPROTO_UDP;

	udp->dest = tun_key->tp_dst;
	mpls->label_stack_entry = mpls_label_id_field(tun_id, tun_key->tos, tun_key->ttl);

	return 0;
}

static int parse_udp_ports(struct mlx5e_priv *priv,
			   struct mlx5_flow_spec *spec,
			   struct flow_cls_offload *f,
			   void *headers_c,
			   void *headers_v)
{
	return mlx5e_tc_tun_parse_udp_ports(priv, spec, f, headers_c, headers_v);
}

static int parse_tunnel(struct mlx5e_priv *priv,
			struct mlx5_flow_spec *spec,
			struct flow_cls_offload *f,
			void *headers_c,
			void *headers_v)
{
	return 0;
}

struct mlx5e_tc_tunnel mplsoudp_tunnel = {
	.tunnel_type          = MLX5E_TC_TUNNEL_TYPE_MPLSOUDP,
	.match_level          = MLX5_MATCH_L4,
	.can_offload          = can_offload,
	.calc_hlen            = calc_hlen,
	.init_encap_attr      = init_encap_attr,
	.generate_ip_tun_hdr  = generate_ip_tun_hdr,
	.parse_udp_ports      = parse_udp_ports,
	.parse_tunnel         = parse_tunnel,
};
