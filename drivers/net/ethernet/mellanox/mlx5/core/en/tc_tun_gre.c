// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/gre.h>
#include "en/tc_tun.h"

static bool mlx5e_tc_tun_can_offload_gretap(struct mlx5e_priv *priv)
{
	return !!MLX5_CAP_ESW(priv->mdev, nvgre_encap_decap);
}

static int mlx5e_tc_tun_calc_hlen_gretap(struct mlx5e_encap_entry *e)
{
	return gre_calc_hlen(e->tun_info->key.tun_flags);
}

static int mlx5e_tc_tun_init_encap_attr_gretap(struct net_device *tunnel_dev,
					       struct mlx5e_priv *priv,
					       struct mlx5e_encap_entry *e,
					       struct netlink_ext_ack *extack)
{
	e->tunnel = &gre_tunnel;
	e->reformat_type = MLX5_REFORMAT_TYPE_L2_TO_NVGRE;
	return 0;
}

static int mlx5e_gen_ip_tunnel_header_gretap(char buf[],
					     __u8 *ip_proto,
					     struct mlx5e_encap_entry *e)
{
	const struct ip_tunnel_key *tun_key  = &e->tun_info->key;
	struct gre_base_hdr *greh = (struct gre_base_hdr *)(buf);
	__be32 tun_id = tunnel_id_to_key32(tun_key->tun_id);
	int hdr_len;

	*ip_proto = IPPROTO_GRE;

	/* the HW does not calculate GRE csum or sequences */
	if (tun_key->tun_flags & (TUNNEL_CSUM | TUNNEL_SEQ))
		return -EOPNOTSUPP;

	greh->protocol = htons(ETH_P_TEB);

	/* GRE key */
	hdr_len	= mlx5e_tc_tun_calc_hlen_gretap(e);
	greh->flags = gre_tnl_flags_to_gre_flags(tun_key->tun_flags);
	if (tun_key->tun_flags & TUNNEL_KEY) {
		__be32 *ptr = (__be32 *)(((u8 *)greh) + hdr_len - 4);
		*ptr = tun_id;
	}

	return 0;
}

static int mlx5e_tc_tun_parse_gretap(struct mlx5e_priv *priv,
				     struct mlx5_flow_spec *spec,
				     struct flow_cls_offload *f,
				     void *headers_c,
				     void *headers_v)
{
	void *misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, IPPROTO_GRE);

	/* gre protocol */
	MLX5_SET_TO_ONES(fte_match_set_misc, misc_c, gre_protocol);
	MLX5_SET(fte_match_set_misc, misc_v, gre_protocol, ETH_P_TEB);

	/* gre key */
	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid enc_keyid;

		flow_rule_match_enc_keyid(rule, &enc_keyid);
		MLX5_SET(fte_match_set_misc, misc_c,
			 gre_key.key, be32_to_cpu(enc_keyid.mask->keyid));
		MLX5_SET(fte_match_set_misc, misc_v,
			 gre_key.key, be32_to_cpu(enc_keyid.key->keyid));
	}

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;

	return 0;
}

struct mlx5e_tc_tunnel gre_tunnel = {
	.tunnel_type          = MLX5E_TC_TUNNEL_TYPE_GRETAP,
	.match_level          = MLX5_MATCH_L3,
	.can_offload          = mlx5e_tc_tun_can_offload_gretap,
	.calc_hlen            = mlx5e_tc_tun_calc_hlen_gretap,
	.init_encap_attr      = mlx5e_tc_tun_init_encap_attr_gretap,
	.generate_ip_tun_hdr  = mlx5e_gen_ip_tunnel_header_gretap,
	.parse_udp_ports      = NULL,
	.parse_tunnel         = mlx5e_tc_tun_parse_gretap,
	.encap_info_equal     = mlx5e_tc_tun_encap_info_equal_generic,
};
