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

static int generate_ip_tun_hdr(char buf[],
			       __u8 *ip_proto,
			       struct mlx5e_encap_entry *r)
{
	const struct ip_tunnel_key *tun_key = &r->tun_info->key;
	struct udphdr *udp = (struct udphdr *)(buf);
	struct mpls_shim_hdr *mpls;
	u32 tun_id;

	tun_id = be32_to_cpu(tunnel_id_to_key32(tun_key->tun_id));
	mpls = (struct mpls_shim_hdr *)(udp + 1);
	*ip_proto = IPPROTO_UDP;

	udp->dest = tun_key->tp_dst;
	*mpls = mpls_entry_encode(tun_id, tun_key->ttl, tun_key->tos, true);

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
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct flow_match_enc_keyid enc_keyid;
	struct flow_match_mpls match;
	void *misc2_c;
	void *misc2_v;

	misc2_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
			       misc_parameters_2);
	misc2_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
			       misc_parameters_2);

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_MPLS))
		return 0;

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID))
		return 0;

	flow_rule_match_enc_keyid(rule, &enc_keyid);

	if (!enc_keyid.mask->keyid)
		return 0;

	if (!MLX5_CAP_ETH(priv->mdev, tunnel_stateless_mpls_over_udp) &&
	    !(MLX5_CAP_GEN(priv->mdev, flex_parser_protocols) & MLX5_FLEX_PROTO_CW_MPLS_UDP))
		return -EOPNOTSUPP;

	flow_rule_match_mpls(rule, &match);

	/* Only support matching the first LSE */
	if (match.mask->used_lses != 1)
		return -EOPNOTSUPP;

	MLX5_SET(fte_match_set_misc2, misc2_c,
		 outer_first_mpls_over_udp.mpls_label,
		 match.mask->ls[0].mpls_label);
	MLX5_SET(fte_match_set_misc2, misc2_v,
		 outer_first_mpls_over_udp.mpls_label,
		 match.key->ls[0].mpls_label);

	MLX5_SET(fte_match_set_misc2, misc2_c,
		 outer_first_mpls_over_udp.mpls_exp,
		 match.mask->ls[0].mpls_tc);
	MLX5_SET(fte_match_set_misc2, misc2_v,
		 outer_first_mpls_over_udp.mpls_exp, match.key->ls[0].mpls_tc);

	MLX5_SET(fte_match_set_misc2, misc2_c,
		 outer_first_mpls_over_udp.mpls_s_bos,
		 match.mask->ls[0].mpls_bos);
	MLX5_SET(fte_match_set_misc2, misc2_v,
		 outer_first_mpls_over_udp.mpls_s_bos,
		 match.key->ls[0].mpls_bos);

	MLX5_SET(fte_match_set_misc2, misc2_c,
		 outer_first_mpls_over_udp.mpls_ttl,
		 match.mask->ls[0].mpls_ttl);
	MLX5_SET(fte_match_set_misc2, misc2_v,
		 outer_first_mpls_over_udp.mpls_ttl,
		 match.key->ls[0].mpls_ttl);
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;

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
