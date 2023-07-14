// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/ip_tunnels.h>
#include <net/vxlan.h>
#include "lib/vxlan.h"
#include "en/tc_tun.h"

static bool mlx5e_tc_tun_can_offload_vxlan(struct mlx5e_priv *priv)
{
	return !!MLX5_CAP_ESW(priv->mdev, vxlan_encap_decap);
}

static int mlx5e_tc_tun_calc_hlen_vxlan(struct mlx5e_encap_entry *e)
{
	return VXLAN_HLEN;
}

static int mlx5e_tc_tun_check_udp_dport_vxlan(struct mlx5e_priv *priv,
					      struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_match_ports enc_ports;

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS))
		return -EOPNOTSUPP;

	flow_rule_match_enc_ports(rule, &enc_ports);

	/* check the UDP destination port validity */

	if (!mlx5_vxlan_lookup_port(priv->mdev->vxlan,
				    be16_to_cpu(enc_ports.key->dst))) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matched UDP dst port is not registered as a VXLAN port");
		netdev_warn(priv->netdev,
			    "UDP port %d is not registered as a VXLAN port\n",
			    be16_to_cpu(enc_ports.key->dst));
		return -EOPNOTSUPP;
	}

	return 0;
}

static int mlx5e_tc_tun_parse_udp_ports_vxlan(struct mlx5e_priv *priv,
					      struct mlx5_flow_spec *spec,
					      struct flow_cls_offload *f,
					      void *headers_c,
					      void *headers_v)
{
	int err = 0;

	err = mlx5e_tc_tun_parse_udp_ports(priv, spec, f, headers_c, headers_v);
	if (err)
		return err;

	return mlx5e_tc_tun_check_udp_dport_vxlan(priv, f);
}

static int mlx5e_tc_tun_init_encap_attr_vxlan(struct net_device *tunnel_dev,
					      struct mlx5e_priv *priv,
					      struct mlx5e_encap_entry *e,
					      struct netlink_ext_ack *extack)
{
	int dst_port = be16_to_cpu(e->tun_info->key.tp_dst);

	e->tunnel = &vxlan_tunnel;

	if (!mlx5_vxlan_lookup_port(priv->mdev->vxlan, dst_port)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "vxlan udp dport was not registered with the HW");
		netdev_warn(priv->netdev,
			    "%d isn't an offloaded vxlan udp dport\n",
			    dst_port);
		return -EOPNOTSUPP;
	}

	e->reformat_type = MLX5_REFORMAT_TYPE_L2_TO_VXLAN;
	return 0;
}

static int mlx5e_gen_ip_tunnel_header_vxlan(char buf[],
					    __u8 *ip_proto,
					    struct mlx5e_encap_entry *e)
{
	const struct ip_tunnel_key *tun_key = &e->tun_info->key;
	__be32 tun_id = tunnel_id_to_key32(tun_key->tun_id);
	struct udphdr *udp = (struct udphdr *)(buf);
	const struct vxlan_metadata *md;
	struct vxlanhdr *vxh;

	if ((tun_key->tun_flags & TUNNEL_VXLAN_OPT) &&
	    e->tun_info->options_len != sizeof(*md))
		return -EOPNOTSUPP;
	vxh = (struct vxlanhdr *)((char *)udp + sizeof(struct udphdr));
	*ip_proto = IPPROTO_UDP;

	udp->dest = tun_key->tp_dst;
	vxh->vx_flags = VXLAN_HF_VNI;
	vxh->vx_vni = vxlan_vni_field(tun_id);
	if (tun_key->tun_flags & TUNNEL_VXLAN_OPT) {
		md = ip_tunnel_info_opts(e->tun_info);
		vxlan_build_gbp_hdr(vxh, md);
	}

	return 0;
}

static int mlx5e_tc_tun_parse_vxlan_gbp_option(struct mlx5e_priv *priv,
					       struct mlx5_flow_spec *spec,
					       struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_match_enc_opts enc_opts;
	void *misc5_c, *misc5_v;
	u32 *gbp, *gbp_mask;

	flow_rule_match_enc_opts(rule, &enc_opts);

	if (memchr_inv(&enc_opts.mask->data, 0, sizeof(enc_opts.mask->data)) &&
	    !MLX5_CAP_ESW_FT_FIELD_SUPPORT_2(priv->mdev, tunnel_header_0_1)) {
		NL_SET_ERR_MSG_MOD(extack, "Matching on VxLAN GBP is not supported");
		return -EOPNOTSUPP;
	}

	if (enc_opts.key->dst_opt_type != TUNNEL_VXLAN_OPT) {
		NL_SET_ERR_MSG_MOD(extack, "Wrong VxLAN option type: not GBP");
		return -EOPNOTSUPP;
	}

	if (enc_opts.key->len != sizeof(*gbp) ||
	    enc_opts.mask->len != sizeof(*gbp_mask)) {
		NL_SET_ERR_MSG_MOD(extack, "VxLAN GBP option/mask len is not 32 bits");
		return -EINVAL;
	}

	gbp = (u32 *)&enc_opts.key->data[0];
	gbp_mask = (u32 *)&enc_opts.mask->data[0];

	if (*gbp_mask & ~VXLAN_GBP_MASK) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "Wrong VxLAN GBP mask(0x%08X)\n", *gbp_mask);
		return -EINVAL;
	}

	misc5_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters_5);
	misc5_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters_5);
	MLX5_SET(fte_match_set_misc5, misc5_c, tunnel_header_0, *gbp_mask);
	MLX5_SET(fte_match_set_misc5, misc5_v, tunnel_header_0, *gbp);

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_5;

	return 0;
}

static int mlx5e_tc_tun_parse_vxlan(struct mlx5e_priv *priv,
				    struct mlx5_flow_spec *spec,
				    struct flow_cls_offload *f,
				    void *headers_c,
				    void *headers_v)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_match_enc_keyid enc_keyid;
	void *misc_c, *misc_v;

	misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_KEYID))
		return 0;

	flow_rule_match_enc_keyid(rule, &enc_keyid);

	if (!enc_keyid.mask->keyid)
		return 0;

	if (flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_OPTS)) {
		int err;

		err = mlx5e_tc_tun_parse_vxlan_gbp_option(priv, spec, f);
		if (err)
			return err;
	}

	/* match on VNI is required */

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
					ft_field_support.outer_vxlan_vni)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on VXLAN VNI is not supported");
		netdev_warn(priv->netdev,
			    "Matching on VXLAN VNI is not supported\n");
		return -EOPNOTSUPP;
	}

	MLX5_SET(fte_match_set_misc, misc_c, vxlan_vni,
		 be32_to_cpu(enc_keyid.mask->keyid));
	MLX5_SET(fte_match_set_misc, misc_v, vxlan_vni,
		 be32_to_cpu(enc_keyid.key->keyid));

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;

	return 0;
}

static bool mlx5e_tc_tun_encap_info_equal_vxlan(struct mlx5e_encap_key *a,
						struct mlx5e_encap_key *b)
{
	return mlx5e_tc_tun_encap_info_equal_options(a, b, TUNNEL_VXLAN_OPT);
}

static int mlx5e_tc_tun_get_remote_ifindex(struct net_device *mirred_dev)
{
	const struct vxlan_dev *vxlan = netdev_priv(mirred_dev);
	const struct vxlan_rdst *dst = &vxlan->default_dst;

	return dst->remote_ifindex;
}

struct mlx5e_tc_tunnel vxlan_tunnel = {
	.tunnel_type          = MLX5E_TC_TUNNEL_TYPE_VXLAN,
	.match_level          = MLX5_MATCH_L4,
	.can_offload          = mlx5e_tc_tun_can_offload_vxlan,
	.calc_hlen            = mlx5e_tc_tun_calc_hlen_vxlan,
	.init_encap_attr      = mlx5e_tc_tun_init_encap_attr_vxlan,
	.generate_ip_tun_hdr  = mlx5e_gen_ip_tunnel_header_vxlan,
	.parse_udp_ports      = mlx5e_tc_tun_parse_udp_ports_vxlan,
	.parse_tunnel         = mlx5e_tc_tun_parse_vxlan,
	.encap_info_equal     = mlx5e_tc_tun_encap_info_equal_vxlan,
	.get_remote_ifindex   = mlx5e_tc_tun_get_remote_ifindex,
};
