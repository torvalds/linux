// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/geneve.h>
#include "lib/geneve.h"
#include "en/tc_tun.h"

#define MLX5E_GENEVE_VER 0

static bool mlx5e_tc_tun_can_offload_geneve(struct mlx5e_priv *priv)
{
	return !!(MLX5_CAP_GEN(priv->mdev, flex_parser_protocols) & MLX5_FLEX_PROTO_GENEVE);
}

static int mlx5e_tc_tun_calc_hlen_geneve(struct mlx5e_encap_entry *e)
{
	return sizeof(struct udphdr) +
	       sizeof(struct genevehdr) +
	       e->tun_info->options_len;
}

static int mlx5e_tc_tun_check_udp_dport_geneve(struct mlx5e_priv *priv,
					       struct flow_cls_offload *f)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	struct flow_match_ports enc_ports;

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_PORTS))
		return -EOPNOTSUPP;

	flow_rule_match_enc_ports(rule, &enc_ports);

	/* Currently we support only default GENEVE
	 * port, so udp dst port must match.
	 */
	if (be16_to_cpu(enc_ports.key->dst) != GENEVE_UDP_PORT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matched UDP dst port is not registered as a GENEVE port");
		netdev_warn(priv->netdev,
			    "UDP port %d is not registered as a GENEVE port\n",
			    be16_to_cpu(enc_ports.key->dst));
		return -EOPNOTSUPP;
	}

	return 0;
}

static int mlx5e_tc_tun_parse_udp_ports_geneve(struct mlx5e_priv *priv,
					       struct mlx5_flow_spec *spec,
					       struct flow_cls_offload *f,
					       void *headers_c,
					       void *headers_v)
{
	int err;

	err = mlx5e_tc_tun_parse_udp_ports(priv, spec, f, headers_c, headers_v);
	if (err)
		return err;

	return mlx5e_tc_tun_check_udp_dport_geneve(priv, f);
}

static int mlx5e_tc_tun_init_encap_attr_geneve(struct net_device *tunnel_dev,
					       struct mlx5e_priv *priv,
					       struct mlx5e_encap_entry *e,
					       struct netlink_ext_ack *extack)
{
	e->tunnel = &geneve_tunnel;

	/* Reformat type for GENEVE encap is similar to VXLAN:
	 * in both cases the HW adds in the same place a
	 * defined encapsulation header that the SW provides.
	 */
	e->reformat_type = MLX5_REFORMAT_TYPE_L2_TO_VXLAN;
	return 0;
}

static void mlx5e_tunnel_id_to_vni(__be64 tun_id, __u8 *vni)
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

static int mlx5e_gen_ip_tunnel_header_geneve(char buf[],
					     __u8 *ip_proto,
					     struct mlx5e_encap_entry *e)
{
	const struct ip_tunnel_info *tun_info = e->tun_info;
	struct udphdr *udp = (struct udphdr *)(buf);
	struct genevehdr *geneveh;

	geneveh = (struct genevehdr *)((char *)udp + sizeof(struct udphdr));

	*ip_proto = IPPROTO_UDP;

	udp->dest = tun_info->key.tp_dst;

	memset(geneveh, 0, sizeof(*geneveh));
	geneveh->ver = MLX5E_GENEVE_VER;
	geneveh->opt_len = tun_info->options_len / 4;
	geneveh->oam = test_bit(IP_TUNNEL_OAM_BIT, tun_info->key.tun_flags);
	geneveh->critical = test_bit(IP_TUNNEL_CRIT_OPT_BIT,
				     tun_info->key.tun_flags);
	mlx5e_tunnel_id_to_vni(tun_info->key.tun_id, geneveh->vni);
	geneveh->proto_type = htons(ETH_P_TEB);

	if (test_bit(IP_TUNNEL_GENEVE_OPT_BIT, tun_info->key.tun_flags)) {
		if (!geneveh->opt_len)
			return -EOPNOTSUPP;
		ip_tunnel_info_opts_get(geneveh->options, tun_info);
	}

	return 0;
}

static int mlx5e_tc_tun_parse_geneve_vni(struct mlx5e_priv *priv,
					 struct mlx5_flow_spec *spec,
					 struct flow_cls_offload *f)
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

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, ft_field_support.outer_geneve_vni)) {
		NL_SET_ERR_MSG_MOD(extack, "Matching on GENEVE VNI is not supported");
		netdev_warn(priv->netdev, "Matching on GENEVE VNI is not supported\n");
		return -EOPNOTSUPP;
	}

	MLX5_SET(fte_match_set_misc, misc_c, geneve_vni, be32_to_cpu(enc_keyid.mask->keyid));
	MLX5_SET(fte_match_set_misc, misc_v, geneve_vni, be32_to_cpu(enc_keyid.key->keyid));

	return 0;
}

static int mlx5e_tc_tun_parse_geneve_options(struct mlx5e_priv *priv,
					     struct mlx5_flow_spec *spec,
					     struct flow_cls_offload *f)
{
	u8 max_tlv_option_data_len = MLX5_CAP_GEN(priv->mdev, max_geneve_tlv_option_data_len);
	u8 max_tlv_options = MLX5_CAP_GEN(priv->mdev, max_geneve_tlv_options);
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);
	struct netlink_ext_ack *extack = f->common.extack;
	void *misc_c, *misc_v, *misc_3_c, *misc_3_v;
	struct geneve_opt *option_key, *option_mask;
	__be32 opt_data_key = 0, opt_data_mask = 0;
	struct flow_match_enc_opts enc_opts;
	int res = 0;

	misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	misc_3_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters_3);
	misc_3_v = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters_3);

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_ENC_OPTS))
		return 0;

	flow_rule_match_enc_opts(rule, &enc_opts);

	if (memchr_inv(&enc_opts.mask->data, 0, sizeof(enc_opts.mask->data)) &&
	    !MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
					ft_field_support.geneve_tlv_option_0_data)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on GENEVE options is not supported");
		netdev_warn(priv->netdev,
			    "Matching on GENEVE options is not supported\n");
		return -EOPNOTSUPP;
	}

	/* make sure that we're talking about GENEVE options */

	if (enc_opts.key->dst_opt_type != IP_TUNNEL_GENEVE_OPT_BIT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on GENEVE options: option type is not GENEVE");
		netdev_warn(priv->netdev,
			    "Matching on GENEVE options: option type is not GENEVE\n");
		return -EOPNOTSUPP;
	}

	if (enc_opts.mask->len &&
	    !MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
					ft_field_support.outer_geneve_opt_len)) {
		NL_SET_ERR_MSG_MOD(extack, "Matching on GENEVE options len is not supported");
		netdev_warn(priv->netdev,
			    "Matching on GENEVE options len is not supported\n");
		return -EOPNOTSUPP;
	}

	/* max_geneve_tlv_option_data_len comes in multiples of 4 bytes, and it
	 * doesn't include the TLV option header. 'geneve_opt_len' is a total
	 * len of all the options, including the headers, also multiples of 4
	 * bytes. Len that comes from the dissector is in bytes.
	 */

	if ((enc_opts.key->len / 4) > ((max_tlv_option_data_len + 1) * max_tlv_options)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on GENEVE options: unsupported options len");
		netdev_warn(priv->netdev,
			    "Matching on GENEVE options: unsupported options len (len=%d)\n",
			    enc_opts.key->len);
		return -EOPNOTSUPP;
	}

	MLX5_SET(fte_match_set_misc, misc_c, geneve_opt_len, enc_opts.mask->len / 4);
	MLX5_SET(fte_match_set_misc, misc_v, geneve_opt_len, enc_opts.key->len / 4);

	/* we support matching on one option only, so just get it */
	option_key = (struct geneve_opt *)&enc_opts.key->data[0];
	option_mask = (struct geneve_opt *)&enc_opts.mask->data[0];

	if (option_mask->opt_class == 0 && option_mask->type == 0 &&
	    !memchr_inv(option_mask->opt_data, 0, option_mask->length * 4))
		return 0;

	if (option_key->length > max_tlv_option_data_len) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on GENEVE options: unsupported option len");
		netdev_warn(priv->netdev,
			    "Matching on GENEVE options: unsupported option len (key=%d, mask=%d)\n",
			    option_key->length, option_mask->length);
		return -EOPNOTSUPP;
	}

	/* data can't be all 0 - fail to offload such rule */
	if (!memchr_inv(option_key->opt_data, 0, option_key->length * 4)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on GENEVE options: can't match on 0 data field");
		netdev_warn(priv->netdev,
			    "Matching on GENEVE options: can't match on 0 data field\n");
		return -EOPNOTSUPP;
	}

	/* add new GENEVE TLV options object */
	res = mlx5_geneve_tlv_option_add(priv->mdev->geneve, option_key);
	if (res) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Matching on GENEVE options: failed creating TLV opt object");
		netdev_warn(priv->netdev,
			    "Matching on GENEVE options: failed creating TLV opt object (class:type:len = 0x%x:0x%x:%d)\n",
			    be16_to_cpu(option_key->opt_class),
			    option_key->type, option_key->length);
		return res;
	}

	/* In general, after creating the object, need to query it
	 * in order to check which option data to set in misc3.
	 * But we support only geneve_tlv_option_0_data, so no
	 * point querying at this stage.
	 */

	memcpy(&opt_data_key, option_key->opt_data, option_key->length * 4);
	memcpy(&opt_data_mask, option_mask->opt_data, option_mask->length * 4);
	MLX5_SET(fte_match_set_misc3, misc_3_v,
		 geneve_tlv_option_0_data, be32_to_cpu(opt_data_key));
	MLX5_SET(fte_match_set_misc3, misc_3_c,
		 geneve_tlv_option_0_data, be32_to_cpu(opt_data_mask));
	if (MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
				       ft_field_support.geneve_tlv_option_0_exist)) {
		MLX5_SET_TO_ONES(fte_match_set_misc, misc_c, geneve_tlv_option_0_exist);
		MLX5_SET_TO_ONES(fte_match_set_misc, misc_v, geneve_tlv_option_0_exist);
	}

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_3;

	return 0;
}

static int mlx5e_tc_tun_parse_geneve_params(struct mlx5e_priv *priv,
					    struct mlx5_flow_spec *spec,
					    struct flow_cls_offload *f)
{
	void *misc_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	void *misc_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,  misc_parameters);
	struct netlink_ext_ack *extack = f->common.extack;

	/* match on OAM - packets with OAM bit on should NOT be offloaded */

	if (!MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, ft_field_support.outer_geneve_oam)) {
		NL_SET_ERR_MSG_MOD(extack, "Matching on GENEVE OAM is not supported");
		netdev_warn(priv->netdev, "Matching on GENEVE OAM is not supported\n");
		return -EOPNOTSUPP;
	}
	MLX5_SET_TO_ONES(fte_match_set_misc, misc_c, geneve_oam);
	MLX5_SET(fte_match_set_misc, misc_v, geneve_oam, 0);

	/* Match on GENEVE protocol. We support only Transparent Eth Bridge. */

	if (MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev,
				       ft_field_support.outer_geneve_protocol_type)) {
		MLX5_SET_TO_ONES(fte_match_set_misc, misc_c, geneve_protocol_type);
		MLX5_SET(fte_match_set_misc, misc_v, geneve_protocol_type, ETH_P_TEB);
	}

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS;

	return 0;
}

static int mlx5e_tc_tun_parse_geneve(struct mlx5e_priv *priv,
				     struct mlx5_flow_spec *spec,
				     struct flow_cls_offload *f,
				     void *headers_c,
				     void *headers_v)
{
	int err;

	err = mlx5e_tc_tun_parse_geneve_params(priv, spec, f);
	if (err)
		return err;

	err = mlx5e_tc_tun_parse_geneve_vni(priv, spec, f);
	if (err)
		return err;

	return mlx5e_tc_tun_parse_geneve_options(priv, spec, f);
}

static bool mlx5e_tc_tun_encap_info_equal_geneve(struct mlx5e_encap_key *a,
						 struct mlx5e_encap_key *b)
{
	return mlx5e_tc_tun_encap_info_equal_options(a, b,
						     IP_TUNNEL_GENEVE_OPT_BIT);
}

struct mlx5e_tc_tunnel geneve_tunnel = {
	.tunnel_type          = MLX5E_TC_TUNNEL_TYPE_GENEVE,
	.match_level          = MLX5_MATCH_L4,
	.can_offload          = mlx5e_tc_tun_can_offload_geneve,
	.calc_hlen            = mlx5e_tc_tun_calc_hlen_geneve,
	.init_encap_attr      = mlx5e_tc_tun_init_encap_attr_geneve,
	.generate_ip_tun_hdr  = mlx5e_gen_ip_tunnel_header_geneve,
	.parse_udp_ports      = mlx5e_tc_tun_parse_udp_ports_geneve,
	.parse_tunnel         = mlx5e_tc_tun_parse_geneve,
	.encap_info_equal     = mlx5e_tc_tun_encap_info_equal_geneve,
};
