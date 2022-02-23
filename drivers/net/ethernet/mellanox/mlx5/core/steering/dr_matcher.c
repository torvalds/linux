// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "dr_types.h"

static bool dr_mask_is_smac_set(struct mlx5dr_match_spec *spec)
{
	return (spec->smac_47_16 || spec->smac_15_0);
}

static bool dr_mask_is_dmac_set(struct mlx5dr_match_spec *spec)
{
	return (spec->dmac_47_16 || spec->dmac_15_0);
}

static bool dr_mask_is_l3_base_set(struct mlx5dr_match_spec *spec)
{
	return (spec->ip_protocol || spec->frag || spec->tcp_flags ||
		spec->ip_ecn || spec->ip_dscp);
}

static bool dr_mask_is_tcp_udp_base_set(struct mlx5dr_match_spec *spec)
{
	return (spec->tcp_sport || spec->tcp_dport ||
		spec->udp_sport || spec->udp_dport);
}

static bool dr_mask_is_ipv4_set(struct mlx5dr_match_spec *spec)
{
	return (spec->dst_ip_31_0 || spec->src_ip_31_0);
}

static bool dr_mask_is_ipv4_5_tuple_set(struct mlx5dr_match_spec *spec)
{
	return (dr_mask_is_l3_base_set(spec) ||
		dr_mask_is_tcp_udp_base_set(spec) ||
		dr_mask_is_ipv4_set(spec));
}

static bool dr_mask_is_eth_l2_tnl_set(struct mlx5dr_match_misc *misc)
{
	return misc->vxlan_vni;
}

static bool dr_mask_is_ttl_set(struct mlx5dr_match_spec *spec)
{
	return spec->ttl_hoplimit;
}

static bool dr_mask_is_ipv4_ihl_set(struct mlx5dr_match_spec *spec)
{
	return spec->ipv4_ihl;
}

#define DR_MASK_IS_L2_DST(_spec, _misc, _inner_outer) (_spec.first_vid || \
	(_spec).first_cfi || (_spec).first_prio || (_spec).cvlan_tag || \
	(_spec).svlan_tag || (_spec).dmac_47_16 || (_spec).dmac_15_0 || \
	(_spec).ethertype || (_spec).ip_version || \
	(_misc)._inner_outer##_second_vid || \
	(_misc)._inner_outer##_second_cfi || \
	(_misc)._inner_outer##_second_prio || \
	(_misc)._inner_outer##_second_cvlan_tag || \
	(_misc)._inner_outer##_second_svlan_tag)

#define DR_MASK_IS_ETH_L4_SET(_spec, _misc, _inner_outer) ( \
	dr_mask_is_l3_base_set(&(_spec)) || \
	dr_mask_is_tcp_udp_base_set(&(_spec)) || \
	dr_mask_is_ttl_set(&(_spec)) || \
	(_misc)._inner_outer##_ipv6_flow_label)

#define DR_MASK_IS_ETH_L4_MISC_SET(_misc3, _inner_outer) ( \
	(_misc3)._inner_outer##_tcp_seq_num || \
	(_misc3)._inner_outer##_tcp_ack_num)

#define DR_MASK_IS_FIRST_MPLS_SET(_misc2, _inner_outer) ( \
	(_misc2)._inner_outer##_first_mpls_label || \
	(_misc2)._inner_outer##_first_mpls_exp || \
	(_misc2)._inner_outer##_first_mpls_s_bos || \
	(_misc2)._inner_outer##_first_mpls_ttl)

static bool dr_mask_is_tnl_gre_set(struct mlx5dr_match_misc *misc)
{
	return (misc->gre_key_h || misc->gre_key_l ||
		misc->gre_protocol || misc->gre_c_present ||
		misc->gre_k_present || misc->gre_s_present);
}

#define DR_MASK_IS_OUTER_MPLS_OVER_GRE_SET(_misc) (\
	(_misc)->outer_first_mpls_over_gre_label || \
	(_misc)->outer_first_mpls_over_gre_exp || \
	(_misc)->outer_first_mpls_over_gre_s_bos || \
	(_misc)->outer_first_mpls_over_gre_ttl)

#define DR_MASK_IS_OUTER_MPLS_OVER_UDP_SET(_misc) (\
	(_misc)->outer_first_mpls_over_udp_label || \
	(_misc)->outer_first_mpls_over_udp_exp || \
	(_misc)->outer_first_mpls_over_udp_s_bos || \
	(_misc)->outer_first_mpls_over_udp_ttl)

static bool
dr_mask_is_vxlan_gpe_set(struct mlx5dr_match_misc3 *misc3)
{
	return (misc3->outer_vxlan_gpe_vni ||
		misc3->outer_vxlan_gpe_next_protocol ||
		misc3->outer_vxlan_gpe_flags);
}

static bool
dr_matcher_supp_vxlan_gpe(struct mlx5dr_cmd_caps *caps)
{
	return (caps->sw_format_ver >= MLX5_STEERING_FORMAT_CONNECTX_6DX) ||
	       (caps->flex_protocols & MLX5_FLEX_PARSER_VXLAN_GPE_ENABLED);
}

static bool
dr_mask_is_tnl_vxlan_gpe(struct mlx5dr_match_param *mask,
			 struct mlx5dr_domain *dmn)
{
	return dr_mask_is_vxlan_gpe_set(&mask->misc3) &&
	       dr_matcher_supp_vxlan_gpe(&dmn->info.caps);
}

static bool dr_mask_is_tnl_geneve_set(struct mlx5dr_match_misc *misc)
{
	return misc->geneve_vni ||
	       misc->geneve_oam ||
	       misc->geneve_protocol_type ||
	       misc->geneve_opt_len;
}

static bool dr_mask_is_tnl_geneve_tlv_opt(struct mlx5dr_match_misc3 *misc3)
{
	return misc3->geneve_tlv_option_0_data;
}

static bool
dr_matcher_supp_flex_parser_ok(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_parser_ok_bits_supp;
}

static bool dr_mask_is_tnl_geneve_tlv_opt_exist_set(struct mlx5dr_match_misc *misc,
						    struct mlx5dr_domain *dmn)
{
	return dr_matcher_supp_flex_parser_ok(&dmn->info.caps) &&
	       misc->geneve_tlv_option_0_exist;
}

static bool
dr_matcher_supp_tnl_geneve(struct mlx5dr_cmd_caps *caps)
{
	return (caps->sw_format_ver >= MLX5_STEERING_FORMAT_CONNECTX_6DX) ||
	       (caps->flex_protocols & MLX5_FLEX_PARSER_GENEVE_ENABLED);
}

static bool
dr_mask_is_tnl_geneve(struct mlx5dr_match_param *mask,
		      struct mlx5dr_domain *dmn)
{
	return dr_mask_is_tnl_geneve_set(&mask->misc) &&
	       dr_matcher_supp_tnl_geneve(&dmn->info.caps);
}

static bool dr_mask_is_tnl_gtpu_set(struct mlx5dr_match_misc3 *misc3)
{
	return misc3->gtpu_msg_flags || misc3->gtpu_msg_type || misc3->gtpu_teid;
}

static bool dr_matcher_supp_tnl_gtpu(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_ENABLED;
}

static bool dr_mask_is_tnl_gtpu(struct mlx5dr_match_param *mask,
				struct mlx5dr_domain *dmn)
{
	return dr_mask_is_tnl_gtpu_set(&mask->misc3) &&
	       dr_matcher_supp_tnl_gtpu(&dmn->info.caps);
}

static int dr_matcher_supp_tnl_gtpu_dw_0(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_DW_0_ENABLED;
}

static bool dr_mask_is_tnl_gtpu_dw_0(struct mlx5dr_match_param *mask,
				     struct mlx5dr_domain *dmn)
{
	return mask->misc3.gtpu_dw_0 &&
	       dr_matcher_supp_tnl_gtpu_dw_0(&dmn->info.caps);
}

static int dr_matcher_supp_tnl_gtpu_teid(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_TEID_ENABLED;
}

static bool dr_mask_is_tnl_gtpu_teid(struct mlx5dr_match_param *mask,
				     struct mlx5dr_domain *dmn)
{
	return mask->misc3.gtpu_teid &&
	       dr_matcher_supp_tnl_gtpu_teid(&dmn->info.caps);
}

static int dr_matcher_supp_tnl_gtpu_dw_2(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_DW_2_ENABLED;
}

static bool dr_mask_is_tnl_gtpu_dw_2(struct mlx5dr_match_param *mask,
				     struct mlx5dr_domain *dmn)
{
	return mask->misc3.gtpu_dw_2 &&
	       dr_matcher_supp_tnl_gtpu_dw_2(&dmn->info.caps);
}

static int dr_matcher_supp_tnl_gtpu_first_ext(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_protocols & MLX5_FLEX_PARSER_GTPU_FIRST_EXT_DW_0_ENABLED;
}

static bool dr_mask_is_tnl_gtpu_first_ext(struct mlx5dr_match_param *mask,
					  struct mlx5dr_domain *dmn)
{
	return mask->misc3.gtpu_first_ext_dw_0 &&
	       dr_matcher_supp_tnl_gtpu_first_ext(&dmn->info.caps);
}

static bool dr_mask_is_tnl_gtpu_flex_parser_0(struct mlx5dr_match_param *mask,
					      struct mlx5dr_domain *dmn)
{
	struct mlx5dr_cmd_caps *caps = &dmn->info.caps;

	return (dr_is_flex_parser_0_id(caps->flex_parser_id_gtpu_dw_0) &&
		dr_mask_is_tnl_gtpu_dw_0(mask, dmn)) ||
	       (dr_is_flex_parser_0_id(caps->flex_parser_id_gtpu_teid) &&
		dr_mask_is_tnl_gtpu_teid(mask, dmn)) ||
	       (dr_is_flex_parser_0_id(caps->flex_parser_id_gtpu_dw_2) &&
		dr_mask_is_tnl_gtpu_dw_2(mask, dmn)) ||
	       (dr_is_flex_parser_0_id(caps->flex_parser_id_gtpu_first_ext_dw_0) &&
		dr_mask_is_tnl_gtpu_first_ext(mask, dmn));
}

static bool dr_mask_is_tnl_gtpu_flex_parser_1(struct mlx5dr_match_param *mask,
					      struct mlx5dr_domain *dmn)
{
	struct mlx5dr_cmd_caps *caps = &dmn->info.caps;

	return (dr_is_flex_parser_1_id(caps->flex_parser_id_gtpu_dw_0) &&
		dr_mask_is_tnl_gtpu_dw_0(mask, dmn)) ||
	       (dr_is_flex_parser_1_id(caps->flex_parser_id_gtpu_teid) &&
		dr_mask_is_tnl_gtpu_teid(mask, dmn)) ||
	       (dr_is_flex_parser_1_id(caps->flex_parser_id_gtpu_dw_2) &&
		dr_mask_is_tnl_gtpu_dw_2(mask, dmn)) ||
	       (dr_is_flex_parser_1_id(caps->flex_parser_id_gtpu_first_ext_dw_0) &&
		dr_mask_is_tnl_gtpu_first_ext(mask, dmn));
}

static bool dr_mask_is_tnl_gtpu_any(struct mlx5dr_match_param *mask,
				    struct mlx5dr_domain *dmn)
{
	return dr_mask_is_tnl_gtpu_flex_parser_0(mask, dmn) ||
	       dr_mask_is_tnl_gtpu_flex_parser_1(mask, dmn) ||
	       dr_mask_is_tnl_gtpu(mask, dmn);
}

static int dr_matcher_supp_icmp_v4(struct mlx5dr_cmd_caps *caps)
{
	return (caps->sw_format_ver >= MLX5_STEERING_FORMAT_CONNECTX_6DX) ||
	       (caps->flex_protocols & MLX5_FLEX_PARSER_ICMP_V4_ENABLED);
}

static int dr_matcher_supp_icmp_v6(struct mlx5dr_cmd_caps *caps)
{
	return (caps->sw_format_ver >= MLX5_STEERING_FORMAT_CONNECTX_6DX) ||
	       (caps->flex_protocols & MLX5_FLEX_PARSER_ICMP_V6_ENABLED);
}

static bool dr_mask_is_icmpv6_set(struct mlx5dr_match_misc3 *misc3)
{
	return (misc3->icmpv6_type || misc3->icmpv6_code ||
		misc3->icmpv6_header_data);
}

static bool dr_mask_is_icmp(struct mlx5dr_match_param *mask,
			    struct mlx5dr_domain *dmn)
{
	if (DR_MASK_IS_ICMPV4_SET(&mask->misc3))
		return dr_matcher_supp_icmp_v4(&dmn->info.caps);
	else if (dr_mask_is_icmpv6_set(&mask->misc3))
		return dr_matcher_supp_icmp_v6(&dmn->info.caps);

	return false;
}

static bool dr_mask_is_wqe_metadata_set(struct mlx5dr_match_misc2 *misc2)
{
	return misc2->metadata_reg_a;
}

static bool dr_mask_is_reg_c_0_3_set(struct mlx5dr_match_misc2 *misc2)
{
	return (misc2->metadata_reg_c_0 || misc2->metadata_reg_c_1 ||
		misc2->metadata_reg_c_2 || misc2->metadata_reg_c_3);
}

static bool dr_mask_is_reg_c_4_7_set(struct mlx5dr_match_misc2 *misc2)
{
	return (misc2->metadata_reg_c_4 || misc2->metadata_reg_c_5 ||
		misc2->metadata_reg_c_6 || misc2->metadata_reg_c_7);
}

static bool dr_mask_is_gvmi_or_qpn_set(struct mlx5dr_match_misc *misc)
{
	return (misc->source_sqn || misc->source_port);
}

static bool dr_mask_is_flex_parser_id_0_3_set(u32 flex_parser_id,
					      u32 flex_parser_value)
{
	if (flex_parser_id)
		return flex_parser_id <= DR_STE_MAX_FLEX_0_ID;

	/* Using flex_parser 0 means that id is zero, thus value must be set. */
	return flex_parser_value;
}

static bool dr_mask_is_flex_parser_0_3_set(struct mlx5dr_match_misc4 *misc4)
{
	return (dr_mask_is_flex_parser_id_0_3_set(misc4->prog_sample_field_id_0,
						  misc4->prog_sample_field_value_0) ||
		dr_mask_is_flex_parser_id_0_3_set(misc4->prog_sample_field_id_1,
						  misc4->prog_sample_field_value_1) ||
		dr_mask_is_flex_parser_id_0_3_set(misc4->prog_sample_field_id_2,
						  misc4->prog_sample_field_value_2) ||
		dr_mask_is_flex_parser_id_0_3_set(misc4->prog_sample_field_id_3,
						  misc4->prog_sample_field_value_3));
}

static bool dr_mask_is_flex_parser_id_4_7_set(u32 flex_parser_id)
{
	return flex_parser_id > DR_STE_MAX_FLEX_0_ID &&
	       flex_parser_id <= DR_STE_MAX_FLEX_1_ID;
}

static bool dr_mask_is_flex_parser_4_7_set(struct mlx5dr_match_misc4 *misc4)
{
	return (dr_mask_is_flex_parser_id_4_7_set(misc4->prog_sample_field_id_0) ||
		dr_mask_is_flex_parser_id_4_7_set(misc4->prog_sample_field_id_1) ||
		dr_mask_is_flex_parser_id_4_7_set(misc4->prog_sample_field_id_2) ||
		dr_mask_is_flex_parser_id_4_7_set(misc4->prog_sample_field_id_3));
}

static int dr_matcher_supp_tnl_mpls_over_gre(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_protocols & MLX5_FLEX_PARSER_MPLS_OVER_GRE_ENABLED;
}

static bool dr_mask_is_tnl_mpls_over_gre(struct mlx5dr_match_param *mask,
					 struct mlx5dr_domain *dmn)
{
	return DR_MASK_IS_OUTER_MPLS_OVER_GRE_SET(&mask->misc2) &&
	       dr_matcher_supp_tnl_mpls_over_gre(&dmn->info.caps);
}

static int dr_matcher_supp_tnl_mpls_over_udp(struct mlx5dr_cmd_caps *caps)
{
	return caps->flex_protocols & MLX5_FLEX_PARSER_MPLS_OVER_UDP_ENABLED;
}

static bool dr_mask_is_tnl_mpls_over_udp(struct mlx5dr_match_param *mask,
					 struct mlx5dr_domain *dmn)
{
	return DR_MASK_IS_OUTER_MPLS_OVER_UDP_SET(&mask->misc2) &&
	       dr_matcher_supp_tnl_mpls_over_udp(&dmn->info.caps);
}

static bool dr_mask_is_tnl_header_0_1_set(struct mlx5dr_match_misc5 *misc5)
{
	return misc5->tunnel_header_0 || misc5->tunnel_header_1;
}

int mlx5dr_matcher_select_builders(struct mlx5dr_matcher *matcher,
				   struct mlx5dr_matcher_rx_tx *nic_matcher,
				   enum mlx5dr_ipv outer_ipv,
				   enum mlx5dr_ipv inner_ipv)
{
	nic_matcher->ste_builder =
		nic_matcher->ste_builder_arr[outer_ipv][inner_ipv];
	nic_matcher->num_of_builders =
		nic_matcher->num_of_builders_arr[outer_ipv][inner_ipv];

	if (!nic_matcher->num_of_builders) {
		mlx5dr_dbg(matcher->tbl->dmn,
			   "Rule not supported on this matcher due to IP related fields\n");
		return -EINVAL;
	}

	return 0;
}

static int dr_matcher_set_ste_builders(struct mlx5dr_matcher *matcher,
				       struct mlx5dr_matcher_rx_tx *nic_matcher,
				       enum mlx5dr_ipv outer_ipv,
				       enum mlx5dr_ipv inner_ipv)
{
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_matcher->nic_tbl->nic_dmn;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_ste_ctx *ste_ctx = dmn->ste_ctx;
	struct mlx5dr_match_param mask = {};
	bool allow_empty_match = false;
	struct mlx5dr_ste_build *sb;
	bool inner, rx;
	int idx = 0;
	int ret, i;

	sb = nic_matcher->ste_builder_arr[outer_ipv][inner_ipv];
	rx = nic_dmn->type == DR_DOMAIN_NIC_TYPE_RX;

	/* Create a temporary mask to track and clear used mask fields */
	if (matcher->match_criteria & DR_MATCHER_CRITERIA_OUTER)
		mask.outer = matcher->mask.outer;

	if (matcher->match_criteria & DR_MATCHER_CRITERIA_MISC)
		mask.misc = matcher->mask.misc;

	if (matcher->match_criteria & DR_MATCHER_CRITERIA_INNER)
		mask.inner = matcher->mask.inner;

	if (matcher->match_criteria & DR_MATCHER_CRITERIA_MISC2)
		mask.misc2 = matcher->mask.misc2;

	if (matcher->match_criteria & DR_MATCHER_CRITERIA_MISC3)
		mask.misc3 = matcher->mask.misc3;

	if (matcher->match_criteria & DR_MATCHER_CRITERIA_MISC4)
		mask.misc4 = matcher->mask.misc4;

	if (matcher->match_criteria & DR_MATCHER_CRITERIA_MISC5)
		mask.misc5 = matcher->mask.misc5;

	ret = mlx5dr_ste_build_pre_check(dmn, matcher->match_criteria,
					 &matcher->mask, NULL);
	if (ret)
		return ret;

	/* Optimize RX pipe by reducing source port match, since
	 * the FDB RX part is connected only to the wire.
	 */
	if (dmn->type == MLX5DR_DOMAIN_TYPE_FDB &&
	    rx && mask.misc.source_port) {
		mask.misc.source_port = 0;
		mask.misc.source_eswitch_owner_vhca_id = 0;
		allow_empty_match = true;
	}

	/* Outer */
	if (matcher->match_criteria & (DR_MATCHER_CRITERIA_OUTER |
				       DR_MATCHER_CRITERIA_MISC |
				       DR_MATCHER_CRITERIA_MISC2 |
				       DR_MATCHER_CRITERIA_MISC3 |
				       DR_MATCHER_CRITERIA_MISC5)) {
		inner = false;

		if (dr_mask_is_wqe_metadata_set(&mask.misc2))
			mlx5dr_ste_build_general_purpose(ste_ctx, &sb[idx++],
							 &mask, inner, rx);

		if (dr_mask_is_reg_c_0_3_set(&mask.misc2))
			mlx5dr_ste_build_register_0(ste_ctx, &sb[idx++],
						    &mask, inner, rx);

		if (dr_mask_is_reg_c_4_7_set(&mask.misc2))
			mlx5dr_ste_build_register_1(ste_ctx, &sb[idx++],
						    &mask, inner, rx);

		if (dr_mask_is_gvmi_or_qpn_set(&mask.misc) &&
		    (dmn->type == MLX5DR_DOMAIN_TYPE_FDB ||
		     dmn->type == MLX5DR_DOMAIN_TYPE_NIC_RX)) {
			mlx5dr_ste_build_src_gvmi_qpn(ste_ctx, &sb[idx++],
						      &mask, dmn, inner, rx);
		}

		if (dr_mask_is_smac_set(&mask.outer) &&
		    dr_mask_is_dmac_set(&mask.outer)) {
			mlx5dr_ste_build_eth_l2_src_dst(ste_ctx, &sb[idx++],
							&mask, inner, rx);
		}

		if (dr_mask_is_smac_set(&mask.outer))
			mlx5dr_ste_build_eth_l2_src(ste_ctx, &sb[idx++],
						    &mask, inner, rx);

		if (DR_MASK_IS_L2_DST(mask.outer, mask.misc, outer))
			mlx5dr_ste_build_eth_l2_dst(ste_ctx, &sb[idx++],
						    &mask, inner, rx);

		if (outer_ipv == DR_RULE_IPV6) {
			if (DR_MASK_IS_DST_IP_SET(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv6_dst(ste_ctx, &sb[idx++],
								 &mask, inner, rx);

			if (DR_MASK_IS_SRC_IP_SET(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv6_src(ste_ctx, &sb[idx++],
								 &mask, inner, rx);

			if (DR_MASK_IS_ETH_L4_SET(mask.outer, mask.misc, outer))
				mlx5dr_ste_build_eth_ipv6_l3_l4(ste_ctx, &sb[idx++],
								&mask, inner, rx);
		} else {
			if (dr_mask_is_ipv4_5_tuple_set(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv4_5_tuple(ste_ctx, &sb[idx++],
								     &mask, inner, rx);

			if (dr_mask_is_ttl_set(&mask.outer) ||
			    dr_mask_is_ipv4_ihl_set(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv4_misc(ste_ctx, &sb[idx++],
								  &mask, inner, rx);
		}

		if (dr_mask_is_tnl_vxlan_gpe(&mask, dmn))
			mlx5dr_ste_build_tnl_vxlan_gpe(ste_ctx, &sb[idx++],
						       &mask, inner, rx);
		else if (dr_mask_is_tnl_geneve(&mask, dmn)) {
			mlx5dr_ste_build_tnl_geneve(ste_ctx, &sb[idx++],
						    &mask, inner, rx);
			if (dr_mask_is_tnl_geneve_tlv_opt(&mask.misc3))
				mlx5dr_ste_build_tnl_geneve_tlv_opt(ste_ctx, &sb[idx++],
								    &mask, &dmn->info.caps,
								    inner, rx);
			if (dr_mask_is_tnl_geneve_tlv_opt_exist_set(&mask.misc, dmn))
				mlx5dr_ste_build_tnl_geneve_tlv_opt_exist(ste_ctx, &sb[idx++],
									  &mask, &dmn->info.caps,
									  inner, rx);
		} else if (dr_mask_is_tnl_gtpu_any(&mask, dmn)) {
			if (dr_mask_is_tnl_gtpu_flex_parser_0(&mask, dmn))
				mlx5dr_ste_build_tnl_gtpu_flex_parser_0(ste_ctx, &sb[idx++],
									&mask, &dmn->info.caps,
									inner, rx);

			if (dr_mask_is_tnl_gtpu_flex_parser_1(&mask, dmn))
				mlx5dr_ste_build_tnl_gtpu_flex_parser_1(ste_ctx, &sb[idx++],
									&mask, &dmn->info.caps,
									inner, rx);

			if (dr_mask_is_tnl_gtpu(&mask, dmn))
				mlx5dr_ste_build_tnl_gtpu(ste_ctx, &sb[idx++],
							  &mask, inner, rx);
		} else if (dr_mask_is_tnl_header_0_1_set(&mask.misc5)) {
			mlx5dr_ste_build_tnl_header_0_1(ste_ctx, &sb[idx++],
							&mask, inner, rx);
		}

		if (DR_MASK_IS_ETH_L4_MISC_SET(mask.misc3, outer))
			mlx5dr_ste_build_eth_l4_misc(ste_ctx, &sb[idx++],
						     &mask, inner, rx);

		if (DR_MASK_IS_FIRST_MPLS_SET(mask.misc2, outer))
			mlx5dr_ste_build_mpls(ste_ctx, &sb[idx++],
					      &mask, inner, rx);

		if (dr_mask_is_tnl_mpls_over_gre(&mask, dmn))
			mlx5dr_ste_build_tnl_mpls_over_gre(ste_ctx, &sb[idx++],
							   &mask, &dmn->info.caps,
							   inner, rx);
		else if (dr_mask_is_tnl_mpls_over_udp(&mask, dmn))
			mlx5dr_ste_build_tnl_mpls_over_udp(ste_ctx, &sb[idx++],
							   &mask, &dmn->info.caps,
							   inner, rx);

		if (dr_mask_is_icmp(&mask, dmn))
			mlx5dr_ste_build_icmp(ste_ctx, &sb[idx++],
					      &mask, &dmn->info.caps,
					      inner, rx);

		if (dr_mask_is_tnl_gre_set(&mask.misc))
			mlx5dr_ste_build_tnl_gre(ste_ctx, &sb[idx++],
						 &mask, inner, rx);
	}

	/* Inner */
	if (matcher->match_criteria & (DR_MATCHER_CRITERIA_INNER |
				       DR_MATCHER_CRITERIA_MISC |
				       DR_MATCHER_CRITERIA_MISC2 |
				       DR_MATCHER_CRITERIA_MISC3)) {
		inner = true;

		if (dr_mask_is_eth_l2_tnl_set(&mask.misc))
			mlx5dr_ste_build_eth_l2_tnl(ste_ctx, &sb[idx++],
						    &mask, inner, rx);

		if (dr_mask_is_smac_set(&mask.inner) &&
		    dr_mask_is_dmac_set(&mask.inner)) {
			mlx5dr_ste_build_eth_l2_src_dst(ste_ctx, &sb[idx++],
							&mask, inner, rx);
		}

		if (dr_mask_is_smac_set(&mask.inner))
			mlx5dr_ste_build_eth_l2_src(ste_ctx, &sb[idx++],
						    &mask, inner, rx);

		if (DR_MASK_IS_L2_DST(mask.inner, mask.misc, inner))
			mlx5dr_ste_build_eth_l2_dst(ste_ctx, &sb[idx++],
						    &mask, inner, rx);

		if (inner_ipv == DR_RULE_IPV6) {
			if (DR_MASK_IS_DST_IP_SET(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv6_dst(ste_ctx, &sb[idx++],
								 &mask, inner, rx);

			if (DR_MASK_IS_SRC_IP_SET(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv6_src(ste_ctx, &sb[idx++],
								 &mask, inner, rx);

			if (DR_MASK_IS_ETH_L4_SET(mask.inner, mask.misc, inner))
				mlx5dr_ste_build_eth_ipv6_l3_l4(ste_ctx, &sb[idx++],
								&mask, inner, rx);
		} else {
			if (dr_mask_is_ipv4_5_tuple_set(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv4_5_tuple(ste_ctx, &sb[idx++],
								     &mask, inner, rx);

			if (dr_mask_is_ttl_set(&mask.inner) ||
			    dr_mask_is_ipv4_ihl_set(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv4_misc(ste_ctx, &sb[idx++],
								  &mask, inner, rx);
		}

		if (DR_MASK_IS_ETH_L4_MISC_SET(mask.misc3, inner))
			mlx5dr_ste_build_eth_l4_misc(ste_ctx, &sb[idx++],
						     &mask, inner, rx);

		if (DR_MASK_IS_FIRST_MPLS_SET(mask.misc2, inner))
			mlx5dr_ste_build_mpls(ste_ctx, &sb[idx++],
					      &mask, inner, rx);

		if (dr_mask_is_tnl_mpls_over_gre(&mask, dmn))
			mlx5dr_ste_build_tnl_mpls_over_gre(ste_ctx, &sb[idx++],
							   &mask, &dmn->info.caps,
							   inner, rx);
		else if (dr_mask_is_tnl_mpls_over_udp(&mask, dmn))
			mlx5dr_ste_build_tnl_mpls_over_udp(ste_ctx, &sb[idx++],
							   &mask, &dmn->info.caps,
							   inner, rx);
	}

	if (matcher->match_criteria & DR_MATCHER_CRITERIA_MISC4) {
		if (dr_mask_is_flex_parser_0_3_set(&mask.misc4))
			mlx5dr_ste_build_flex_parser_0(ste_ctx, &sb[idx++],
						       &mask, false, rx);

		if (dr_mask_is_flex_parser_4_7_set(&mask.misc4))
			mlx5dr_ste_build_flex_parser_1(ste_ctx, &sb[idx++],
						       &mask, false, rx);
	}

	/* Empty matcher, takes all */
	if ((!idx && allow_empty_match) ||
	    matcher->match_criteria == DR_MATCHER_CRITERIA_EMPTY)
		mlx5dr_ste_build_empty_always_hit(&sb[idx++], rx);

	if (idx == 0) {
		mlx5dr_err(dmn, "Cannot generate any valid rules from mask\n");
		return -EINVAL;
	}

	/* Check that all mask fields were consumed */
	for (i = 0; i < sizeof(struct mlx5dr_match_param); i++) {
		if (((u8 *)&mask)[i] != 0) {
			mlx5dr_dbg(dmn, "Mask contains unsupported parameters\n");
			return -EOPNOTSUPP;
		}
	}

	nic_matcher->ste_builder = sb;
	nic_matcher->num_of_builders_arr[outer_ipv][inner_ipv] = idx;

	return 0;
}

static int dr_nic_matcher_connect(struct mlx5dr_domain *dmn,
				  struct mlx5dr_matcher_rx_tx *curr_nic_matcher,
				  struct mlx5dr_matcher_rx_tx *next_nic_matcher,
				  struct mlx5dr_matcher_rx_tx *prev_nic_matcher)
{
	struct mlx5dr_table_rx_tx *nic_tbl = curr_nic_matcher->nic_tbl;
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_tbl->nic_dmn;
	struct mlx5dr_htbl_connect_info info;
	struct mlx5dr_ste_htbl *prev_htbl;
	int ret;

	/* Connect end anchor hash table to next_htbl or to the default address */
	if (next_nic_matcher) {
		info.type = CONNECT_HIT;
		info.hit_next_htbl = next_nic_matcher->s_htbl;
	} else {
		info.type = CONNECT_MISS;
		info.miss_icm_addr = nic_tbl->default_icm_addr;
	}
	ret = mlx5dr_ste_htbl_init_and_postsend(dmn, nic_dmn,
						curr_nic_matcher->e_anchor,
						&info, info.type == CONNECT_HIT);
	if (ret)
		return ret;

	/* Connect start hash table to end anchor */
	info.type = CONNECT_MISS;
	info.miss_icm_addr = curr_nic_matcher->e_anchor->chunk->icm_addr;
	ret = mlx5dr_ste_htbl_init_and_postsend(dmn, nic_dmn,
						curr_nic_matcher->s_htbl,
						&info, false);
	if (ret)
		return ret;

	/* Connect previous hash table to matcher start hash table */
	if (prev_nic_matcher)
		prev_htbl = prev_nic_matcher->e_anchor;
	else
		prev_htbl = nic_tbl->s_anchor;

	info.type = CONNECT_HIT;
	info.hit_next_htbl = curr_nic_matcher->s_htbl;
	ret = mlx5dr_ste_htbl_init_and_postsend(dmn, nic_dmn, prev_htbl,
						&info, true);
	if (ret)
		return ret;

	/* Update the pointing ste and next hash table */
	curr_nic_matcher->s_htbl->pointing_ste = prev_htbl->ste_arr;
	prev_htbl->ste_arr[0].next_htbl = curr_nic_matcher->s_htbl;

	if (next_nic_matcher) {
		next_nic_matcher->s_htbl->pointing_ste = curr_nic_matcher->e_anchor->ste_arr;
		curr_nic_matcher->e_anchor->ste_arr[0].next_htbl = next_nic_matcher->s_htbl;
	}

	return 0;
}

int mlx5dr_matcher_add_to_tbl_nic(struct mlx5dr_domain *dmn,
				  struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	struct mlx5dr_matcher_rx_tx *next_nic_matcher, *prev_nic_matcher, *tmp_nic_matcher;
	struct mlx5dr_table_rx_tx *nic_tbl = nic_matcher->nic_tbl;
	bool first = true;
	int ret;

	/* If the nic matcher is already on its parent nic table list,
	 * then it is already connected to the chain of nic matchers.
	 */
	if (!list_empty(&nic_matcher->list_node))
		return 0;

	next_nic_matcher = NULL;
	list_for_each_entry(tmp_nic_matcher, &nic_tbl->nic_matcher_list, list_node) {
		if (tmp_nic_matcher->prio >= nic_matcher->prio) {
			next_nic_matcher = tmp_nic_matcher;
			break;
		}
		first = false;
	}

	prev_nic_matcher = NULL;
	if (next_nic_matcher && !first)
		prev_nic_matcher = list_prev_entry(next_nic_matcher, list_node);
	else if (!first)
		prev_nic_matcher = list_last_entry(&nic_tbl->nic_matcher_list,
						   struct mlx5dr_matcher_rx_tx,
						   list_node);

	ret = dr_nic_matcher_connect(dmn, nic_matcher,
				     next_nic_matcher, prev_nic_matcher);
	if (ret)
		return ret;

	if (prev_nic_matcher)
		list_add(&nic_matcher->list_node, &prev_nic_matcher->list_node);
	else if (next_nic_matcher)
		list_add_tail(&nic_matcher->list_node, &next_nic_matcher->list_node);
	else
		list_add(&nic_matcher->list_node, &nic_matcher->nic_tbl->nic_matcher_list);

	return ret;
}

static void dr_matcher_uninit_nic(struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	mlx5dr_htbl_put(nic_matcher->s_htbl);
	mlx5dr_htbl_put(nic_matcher->e_anchor);
}

static void dr_matcher_uninit_fdb(struct mlx5dr_matcher *matcher)
{
	dr_matcher_uninit_nic(&matcher->rx);
	dr_matcher_uninit_nic(&matcher->tx);
}

static void dr_matcher_uninit(struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;

	switch (dmn->type) {
	case MLX5DR_DOMAIN_TYPE_NIC_RX:
		dr_matcher_uninit_nic(&matcher->rx);
		break;
	case MLX5DR_DOMAIN_TYPE_NIC_TX:
		dr_matcher_uninit_nic(&matcher->tx);
		break;
	case MLX5DR_DOMAIN_TYPE_FDB:
		dr_matcher_uninit_fdb(matcher);
		break;
	default:
		WARN_ON(true);
		break;
	}
}

static int dr_matcher_set_all_ste_builders(struct mlx5dr_matcher *matcher,
					   struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;

	dr_matcher_set_ste_builders(matcher, nic_matcher, DR_RULE_IPV4, DR_RULE_IPV4);
	dr_matcher_set_ste_builders(matcher, nic_matcher, DR_RULE_IPV4, DR_RULE_IPV6);
	dr_matcher_set_ste_builders(matcher, nic_matcher, DR_RULE_IPV6, DR_RULE_IPV4);
	dr_matcher_set_ste_builders(matcher, nic_matcher, DR_RULE_IPV6, DR_RULE_IPV6);

	if (!nic_matcher->ste_builder) {
		mlx5dr_err(dmn, "Cannot generate IPv4 or IPv6 rules with given mask\n");
		return -EINVAL;
	}

	return 0;
}

static int dr_matcher_init_nic(struct mlx5dr_matcher *matcher,
			       struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	int ret;

	nic_matcher->prio = matcher->prio;
	INIT_LIST_HEAD(&nic_matcher->list_node);

	ret = dr_matcher_set_all_ste_builders(matcher, nic_matcher);
	if (ret)
		return ret;

	nic_matcher->e_anchor = mlx5dr_ste_htbl_alloc(dmn->ste_icm_pool,
						      DR_CHUNK_SIZE_1,
						      MLX5DR_STE_LU_TYPE_DONT_CARE,
						      0);
	if (!nic_matcher->e_anchor)
		return -ENOMEM;

	nic_matcher->s_htbl = mlx5dr_ste_htbl_alloc(dmn->ste_icm_pool,
						    DR_CHUNK_SIZE_1,
						    nic_matcher->ste_builder[0].lu_type,
						    nic_matcher->ste_builder[0].byte_mask);
	if (!nic_matcher->s_htbl) {
		ret = -ENOMEM;
		goto free_e_htbl;
	}

	/* make sure the tables exist while empty */
	mlx5dr_htbl_get(nic_matcher->s_htbl);
	mlx5dr_htbl_get(nic_matcher->e_anchor);

	return 0;

free_e_htbl:
	mlx5dr_ste_htbl_free(nic_matcher->e_anchor);
	return ret;
}

static int dr_matcher_init_fdb(struct mlx5dr_matcher *matcher)
{
	int ret;

	ret = dr_matcher_init_nic(matcher, &matcher->rx);
	if (ret)
		return ret;

	ret = dr_matcher_init_nic(matcher, &matcher->tx);
	if (ret)
		goto uninit_nic_rx;

	return 0;

uninit_nic_rx:
	dr_matcher_uninit_nic(&matcher->rx);
	return ret;
}

static int dr_matcher_copy_param(struct mlx5dr_matcher *matcher,
				 struct mlx5dr_match_parameters *mask)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_match_parameters consumed_mask;
	int i, ret = 0;

	if (matcher->match_criteria >= DR_MATCHER_CRITERIA_MAX) {
		mlx5dr_err(dmn, "Invalid match criteria attribute\n");
		return -EINVAL;
	}

	if (mask) {
		if (mask->match_sz > DR_SZ_MATCH_PARAM) {
			mlx5dr_err(dmn, "Invalid match size attribute\n");
			return -EINVAL;
		}

		consumed_mask.match_buf = kzalloc(mask->match_sz, GFP_KERNEL);
		if (!consumed_mask.match_buf)
			return -ENOMEM;

		consumed_mask.match_sz = mask->match_sz;
		memcpy(consumed_mask.match_buf, mask->match_buf, mask->match_sz);
		mlx5dr_ste_copy_param(matcher->match_criteria,
				      &matcher->mask, &consumed_mask, true);

		/* Check that all mask data was consumed */
		for (i = 0; i < consumed_mask.match_sz; i++) {
			if (!((u8 *)consumed_mask.match_buf)[i])
				continue;

			mlx5dr_dbg(dmn,
				   "Match param mask contains unsupported parameters\n");
			ret = -EOPNOTSUPP;
			break;
		}

		kfree(consumed_mask.match_buf);
	}

	return ret;
}

static int dr_matcher_init(struct mlx5dr_matcher *matcher,
			   struct mlx5dr_match_parameters *mask)
{
	struct mlx5dr_table *tbl = matcher->tbl;
	struct mlx5dr_domain *dmn = tbl->dmn;
	int ret;

	ret = dr_matcher_copy_param(matcher, mask);
	if (ret)
		return ret;

	switch (dmn->type) {
	case MLX5DR_DOMAIN_TYPE_NIC_RX:
		matcher->rx.nic_tbl = &tbl->rx;
		ret = dr_matcher_init_nic(matcher, &matcher->rx);
		break;
	case MLX5DR_DOMAIN_TYPE_NIC_TX:
		matcher->tx.nic_tbl = &tbl->tx;
		ret = dr_matcher_init_nic(matcher, &matcher->tx);
		break;
	case MLX5DR_DOMAIN_TYPE_FDB:
		matcher->rx.nic_tbl = &tbl->rx;
		matcher->tx.nic_tbl = &tbl->tx;
		ret = dr_matcher_init_fdb(matcher);
		break;
	default:
		WARN_ON(true);
		ret = -EINVAL;
	}

	return ret;
}

static void dr_matcher_add_to_dbg_list(struct mlx5dr_matcher *matcher)
{
	mutex_lock(&matcher->tbl->dmn->dump_info.dbg_mutex);
	list_add(&matcher->list_node, &matcher->tbl->matcher_list);
	mutex_unlock(&matcher->tbl->dmn->dump_info.dbg_mutex);
}

static void dr_matcher_remove_from_dbg_list(struct mlx5dr_matcher *matcher)
{
	mutex_lock(&matcher->tbl->dmn->dump_info.dbg_mutex);
	list_del(&matcher->list_node);
	mutex_unlock(&matcher->tbl->dmn->dump_info.dbg_mutex);
}

struct mlx5dr_matcher *
mlx5dr_matcher_create(struct mlx5dr_table *tbl,
		      u32 priority,
		      u8 match_criteria_enable,
		      struct mlx5dr_match_parameters *mask)
{
	struct mlx5dr_matcher *matcher;
	int ret;

	refcount_inc(&tbl->refcount);

	matcher = kzalloc(sizeof(*matcher), GFP_KERNEL);
	if (!matcher)
		goto dec_ref;

	matcher->tbl = tbl;
	matcher->prio = priority;
	matcher->match_criteria = match_criteria_enable;
	refcount_set(&matcher->refcount, 1);
	INIT_LIST_HEAD(&matcher->list_node);
	INIT_LIST_HEAD(&matcher->dbg_rule_list);

	mlx5dr_domain_lock(tbl->dmn);

	ret = dr_matcher_init(matcher, mask);
	if (ret)
		goto free_matcher;

	dr_matcher_add_to_dbg_list(matcher);

	mlx5dr_domain_unlock(tbl->dmn);

	return matcher;

free_matcher:
	mlx5dr_domain_unlock(tbl->dmn);
	kfree(matcher);
dec_ref:
	refcount_dec(&tbl->refcount);
	return NULL;
}

static int dr_matcher_disconnect_nic(struct mlx5dr_domain *dmn,
				     struct mlx5dr_table_rx_tx *nic_tbl,
				     struct mlx5dr_matcher_rx_tx *next_nic_matcher,
				     struct mlx5dr_matcher_rx_tx *prev_nic_matcher)
{
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_tbl->nic_dmn;
	struct mlx5dr_htbl_connect_info info;
	struct mlx5dr_ste_htbl *prev_anchor;

	if (prev_nic_matcher)
		prev_anchor = prev_nic_matcher->e_anchor;
	else
		prev_anchor = nic_tbl->s_anchor;

	/* Connect previous anchor hash table to next matcher or to the default address */
	if (next_nic_matcher) {
		info.type = CONNECT_HIT;
		info.hit_next_htbl = next_nic_matcher->s_htbl;
		next_nic_matcher->s_htbl->pointing_ste = prev_anchor->ste_arr;
		prev_anchor->ste_arr[0].next_htbl = next_nic_matcher->s_htbl;
	} else {
		info.type = CONNECT_MISS;
		info.miss_icm_addr = nic_tbl->default_icm_addr;
		prev_anchor->ste_arr[0].next_htbl = NULL;
	}

	return mlx5dr_ste_htbl_init_and_postsend(dmn, nic_dmn, prev_anchor,
						 &info, true);
}

int mlx5dr_matcher_remove_from_tbl_nic(struct mlx5dr_domain *dmn,
				       struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	struct mlx5dr_matcher_rx_tx *prev_nic_matcher, *next_nic_matcher;
	struct mlx5dr_table_rx_tx *nic_tbl = nic_matcher->nic_tbl;
	int ret;

	/* If the nic matcher is not on its parent nic table list,
	 * then it is detached - no need to disconnect it.
	 */
	if (list_empty(&nic_matcher->list_node))
		return 0;

	if (list_is_last(&nic_matcher->list_node, &nic_tbl->nic_matcher_list))
		next_nic_matcher = NULL;
	else
		next_nic_matcher = list_next_entry(nic_matcher, list_node);

	if (nic_matcher->list_node.prev == &nic_tbl->nic_matcher_list)
		prev_nic_matcher = NULL;
	else
		prev_nic_matcher = list_prev_entry(nic_matcher, list_node);

	ret = dr_matcher_disconnect_nic(dmn, nic_tbl, next_nic_matcher, prev_nic_matcher);
	if (ret)
		return ret;

	list_del_init(&nic_matcher->list_node);
	return 0;
}

int mlx5dr_matcher_destroy(struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_table *tbl = matcher->tbl;

	if (WARN_ON_ONCE(refcount_read(&matcher->refcount) > 1))
		return -EBUSY;

	mlx5dr_domain_lock(tbl->dmn);

	dr_matcher_remove_from_dbg_list(matcher);
	dr_matcher_uninit(matcher);
	refcount_dec(&matcher->tbl->refcount);

	mlx5dr_domain_unlock(tbl->dmn);
	kfree(matcher);

	return 0;
}
