// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#include "internal.h"

#define HWS_CLEAR_MATCH_PARAM(mask, field) \
	MLX5_SET(fte_match_param, (mask)->match_buf, field, 0)

#define HWS_SZ_MATCH_PARAM (MLX5_ST_SZ_DW_MATCH_PARAM * 4)

static const struct rhashtable_params hws_refcount_hash = {
	.key_len = sizeof_field(struct mlx5hws_bwc_complex_rule_hash_node,
				match_buf),
	.key_offset = offsetof(struct mlx5hws_bwc_complex_rule_hash_node,
			       match_buf),
	.head_offset = offsetof(struct mlx5hws_bwc_complex_rule_hash_node,
				hash_node),
	.automatic_shrinking = true,
	.min_size = 1,
};

bool mlx5hws_bwc_match_params_is_complex(struct mlx5hws_context *ctx,
					 u8 match_criteria_enable,
					 struct mlx5hws_match_parameters *mask)
{
	struct mlx5hws_definer match_layout = {0};
	struct mlx5hws_match_template *mt;
	bool is_complex = false;
	int ret;

	if (!match_criteria_enable)
		return false; /* empty matcher */

	mt = mlx5hws_match_template_create(ctx,
					   mask->match_buf,
					   mask->match_sz,
					   match_criteria_enable);
	if (!mt) {
		mlx5hws_err(ctx, "BWC: failed creating match template\n");
		return false;
	}

	ret = mlx5hws_definer_calc_layout(ctx, mt, &match_layout);
	if (ret) {
		/* The only case that we're interested in is E2BIG,
		 * which means that the match parameters need to be
		 * split into complex martcher.
		 * For all other cases (good or bad) - just return true
		 * and let the usual match creation path handle it,
		 * both for good and bad flows.
		 */
		if (ret == -E2BIG) {
			is_complex = true;
			mlx5hws_dbg(ctx, "Matcher definer layout: need complex matcher\n");
		} else {
			mlx5hws_err(ctx, "Failed to calculate matcher definer layout\n");
		}
	} else {
		kfree(mt->fc);
	}

	mlx5hws_match_template_destroy(mt);

	return is_complex;
}

static void
hws_bwc_matcher_complex_params_clear_fld(struct mlx5hws_context *ctx,
					 enum mlx5hws_definer_fname fname,
					 struct mlx5hws_match_parameters *mask)
{
	struct mlx5hws_cmd_query_caps *caps = ctx->caps;

	switch (fname) {
	case MLX5HWS_DEFINER_FNAME_ETH_TYPE_O:
	case MLX5HWS_DEFINER_FNAME_ETH_TYPE_I:
	case MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_O:
	case MLX5HWS_DEFINER_FNAME_ETH_L3_TYPE_I:
	case MLX5HWS_DEFINER_FNAME_IP_VERSION_O:
	case MLX5HWS_DEFINER_FNAME_IP_VERSION_I:
		/* Because of the strict requirements for IP address matching
		 * that require ethtype/ip_version matching as well, don't clear
		 * these fields - have them in both parts of the complex matcher
		 */
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.smac_47_16);
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.smac_47_16);
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.smac_15_0);
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.smac_15_0);
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.dmac_47_16);
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.dmac_47_16);
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.dmac_15_0);
		break;
	case MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.dmac_15_0);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_TYPE_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.cvlan_tag);
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.svlan_tag);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_TYPE_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.cvlan_tag);
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.svlan_tag);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_FIRST_PRIO_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.first_prio);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_FIRST_PRIO_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.first_prio);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_CFI_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.first_cfi);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_CFI_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.first_cfi);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_ID_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.first_vid);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_ID_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.first_vid);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_TYPE_O:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.outer_second_cvlan_tag);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.outer_second_svlan_tag);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_TYPE_I:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.inner_second_cvlan_tag);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.inner_second_svlan_tag);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_PRIO_O:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.outer_second_prio);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_PRIO_I:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.inner_second_prio);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_CFI_O:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.outer_second_cfi);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_CFI_I:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.inner_second_cfi);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_ID_O:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.outer_second_vid);
		break;
	case MLX5HWS_DEFINER_FNAME_VLAN_SECOND_ID_I:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.inner_second_vid);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV4_IHL_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.ipv4_ihl);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV4_IHL_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.ipv4_ihl);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_DSCP_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.ip_dscp);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_DSCP_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.ip_dscp);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_ECN_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.ip_ecn);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_ECN_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.ip_ecn);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_TTL_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.ttl_hoplimit);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_TTL_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.ttl_hoplimit);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV4_DST_O:
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV4_SRC_O:
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV4_DST_I:
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV4_SRC_I:
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_FRAG_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.frag);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_FRAG_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.frag);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV6_FLOW_LABEL_O:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.outer_ipv6_flow_label);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV6_FLOW_LABEL_I:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.inner_ipv6_flow_label);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_O:
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_O:
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_O:
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_O:
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_127_96);
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_95_64);
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_63_32);
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_O:
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_O:
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_O:
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_O:
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_127_96);
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_95_64);
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_63_32);
		HWS_CLEAR_MATCH_PARAM(mask,
				      outer_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_I:
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_I:
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_I:
	case MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_I:
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_127_96);
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_95_64);
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_63_32);
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.dst_ipv4_dst_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_I:
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_I:
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_I:
	case MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_I:
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_127_96);
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_95_64);
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_63_32);
		HWS_CLEAR_MATCH_PARAM(mask,
				      inner_headers.src_ipv4_src_ipv6.ipv6_simple_layout.ipv6_31_0);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_PROTOCOL_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.ip_protocol);
		break;
	case MLX5HWS_DEFINER_FNAME_IP_PROTOCOL_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.ip_protocol);
		break;
	case MLX5HWS_DEFINER_FNAME_L4_SPORT_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.tcp_sport);
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.udp_sport);
		break;
	case MLX5HWS_DEFINER_FNAME_L4_SPORT_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.tcp_dport);
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.udp_dport);
		break;
	case MLX5HWS_DEFINER_FNAME_L4_DPORT_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.tcp_dport);
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.udp_dport);
		break;
	case MLX5HWS_DEFINER_FNAME_L4_DPORT_I:
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.tcp_dport);
		HWS_CLEAR_MATCH_PARAM(mask, inner_headers.udp_dport);
		break;
	case MLX5HWS_DEFINER_FNAME_TCP_FLAGS_O:
		HWS_CLEAR_MATCH_PARAM(mask, outer_headers.tcp_flags);
		break;
	case MLX5HWS_DEFINER_FNAME_TCP_ACK_NUM:
	case MLX5HWS_DEFINER_FNAME_TCP_SEQ_NUM:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.outer_tcp_seq_num);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.outer_tcp_ack_num);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.inner_tcp_seq_num);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.inner_tcp_ack_num);
		break;
	case MLX5HWS_DEFINER_FNAME_GTP_TEID:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.gtpu_teid);
		break;
	case MLX5HWS_DEFINER_FNAME_GTP_MSG_TYPE:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.gtpu_msg_type);
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.gtpu_msg_flags);
		break;
	case MLX5HWS_DEFINER_FNAME_GTPU_FIRST_EXT_DW0:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.gtpu_first_ext_dw_0);
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.gtpu_dw_0);
		break;
	case MLX5HWS_DEFINER_FNAME_GTPU_DW2:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.gtpu_dw_2);
		break;
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_0:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_1:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_2:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_3:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_4:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_5:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_6:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER_7:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_2.outer_first_mpls_over_gre);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_2.outer_first_mpls_over_udp);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.geneve_tlv_option_0_data);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_4.prog_sample_field_id_0);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_4.prog_sample_field_value_0);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_4.prog_sample_field_value_1);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_4.prog_sample_field_id_2);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_4.prog_sample_field_value_2);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_4.prog_sample_field_id_3);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_4.prog_sample_field_value_3);
		break;
	case MLX5HWS_DEFINER_FNAME_VXLAN_VNI:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.vxlan_vni);
		break;
	case MLX5HWS_DEFINER_FNAME_VXLAN_GPE_FLAGS:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.outer_vxlan_gpe_flags);
		break;
	case MLX5HWS_DEFINER_FNAME_VXLAN_GPE_RSVD0:
		break;
	case MLX5HWS_DEFINER_FNAME_VXLAN_GPE_PROTO:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.outer_vxlan_gpe_next_protocol);
		break;
	case MLX5HWS_DEFINER_FNAME_VXLAN_GPE_VNI:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.outer_vxlan_gpe_vni);
		break;
	case MLX5HWS_DEFINER_FNAME_GENEVE_OPT_LEN:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.geneve_opt_len);
		break;
	case MLX5HWS_DEFINER_FNAME_GENEVE_OAM:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.geneve_oam);
		break;
	case MLX5HWS_DEFINER_FNAME_GENEVE_PROTO:
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.geneve_protocol_type);
		break;
	case MLX5HWS_DEFINER_FNAME_GENEVE_VNI:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.geneve_vni);
		break;
	case MLX5HWS_DEFINER_FNAME_SOURCE_QP:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.source_sqn);
		break;
	case MLX5HWS_DEFINER_FNAME_SOURCE_GVMI:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.source_port);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.source_eswitch_owner_vhca_id);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_0:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_c_0);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_1:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_c_1);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_2:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_c_2);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_3:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_c_3);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_4:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_c_4);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_5:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_c_5);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_7:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_c_7);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_A:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.metadata_reg_a);
		break;
	case MLX5HWS_DEFINER_FNAME_GRE_C:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.gre_c_present);
		break;
	case MLX5HWS_DEFINER_FNAME_GRE_K:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.gre_k_present);
		break;
	case MLX5HWS_DEFINER_FNAME_GRE_S:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.gre_s_present);
		break;
	case MLX5HWS_DEFINER_FNAME_GRE_PROTOCOL:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.gre_protocol);
		break;
	case MLX5HWS_DEFINER_FNAME_GRE_OPT_KEY:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters.gre_key.key);
		break;
	case MLX5HWS_DEFINER_FNAME_ICMP_DW1:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.icmp_header_data);
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.icmp_type);
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.icmp_code);
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters_3.icmpv6_header_data);
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.icmpv6_type);
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_3.icmpv6_code);
		break;
	case MLX5HWS_DEFINER_FNAME_MPLS0_O:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.outer_first_mpls);
		break;
	case MLX5HWS_DEFINER_FNAME_MPLS0_I:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_2.inner_first_mpls);
		break;
	case MLX5HWS_DEFINER_FNAME_TNL_HDR_0:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_5.tunnel_header_0);
		break;
	case MLX5HWS_DEFINER_FNAME_TNL_HDR_1:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_5.tunnel_header_1);
		break;
	case MLX5HWS_DEFINER_FNAME_TNL_HDR_2:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_5.tunnel_header_2);
		break;
	case MLX5HWS_DEFINER_FNAME_TNL_HDR_3:
		HWS_CLEAR_MATCH_PARAM(mask, misc_parameters_5.tunnel_header_3);
		break;
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER0_OK:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER1_OK:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER2_OK:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER3_OK:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER4_OK:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER5_OK:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER6_OK:
	case MLX5HWS_DEFINER_FNAME_FLEX_PARSER7_OK:
		/* assuming this is flex parser for geneve option */
		if ((fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER0_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 0) ||
		    (fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER1_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 1) ||
		    (fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER2_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 2) ||
		    (fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER3_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 3) ||
		    (fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER4_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 4) ||
		    (fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER5_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 5) ||
		    (fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER6_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 6) ||
		    (fname == MLX5HWS_DEFINER_FNAME_FLEX_PARSER7_OK &&
		     ctx->caps->flex_parser_id_geneve_tlv_option_0 != 7)) {
			mlx5hws_err(ctx,
				    "Complex params: unsupported field %s (%d), flex parser ID for geneve is %d\n",
				    mlx5hws_definer_fname_to_str(fname), fname,
				    caps->flex_parser_id_geneve_tlv_option_0);
			break;
		}
		HWS_CLEAR_MATCH_PARAM(mask,
				      misc_parameters.geneve_tlv_option_0_exist);
		break;
	case MLX5HWS_DEFINER_FNAME_REG_6:
	default:
		mlx5hws_err(ctx, "Complex params: unsupported field %s (%d)\n",
			    mlx5hws_definer_fname_to_str(fname), fname);
		break;
	}
}

static bool
hws_bwc_matcher_complex_params_comb_is_valid(struct mlx5hws_definer_fc *fc,
					     int fc_sz,
					     u32 combination_num)
{
	bool m1[MLX5HWS_DEFINER_FNAME_MAX] = {0};
	bool m2[MLX5HWS_DEFINER_FNAME_MAX] = {0};
	bool is_first_matcher;
	int i;

	for (i = 0; i < fc_sz; i++) {
		is_first_matcher = !(combination_num & BIT(i));
		if (is_first_matcher)
			m1[fc[i].fname] = true;
		else
			m2[fc[i].fname] = true;
	}

	/* Not all the fields can be split into separate matchers.
	 * Some should be together on the same matcher.
	 * For example, IPv6 parts - the whole IPv6 address should be on the
	 * same matcher in order for us to deduce if it's IPv6 or IPv4 address.
	 */
	if (m1[MLX5HWS_DEFINER_FNAME_IP_FRAG_O] &&
	    (m2[MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_O]))
		return false;

	if (m2[MLX5HWS_DEFINER_FNAME_IP_FRAG_O] &&
	    (m1[MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_O]))
		return false;

	if (m1[MLX5HWS_DEFINER_FNAME_IP_FRAG_I] &&
	    (m2[MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_I]))
		return false;

	if (m2[MLX5HWS_DEFINER_FNAME_IP_FRAG_I] &&
	    (m1[MLX5HWS_DEFINER_FNAME_ETH_SMAC_47_16_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_ETH_SMAC_15_0_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_ETH_DMAC_47_16_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_ETH_DMAC_15_0_I]))
		return false;

	/* Don't split outer IPv6 dest address. */
	if ((m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_O]) &&
	    (m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_O]))
		return false;

	/* Don't split outer IPv6 source address. */
	if ((m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_O] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_O]) &&
	    (m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_O] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_O]))
		return false;

	/* Don't split inner IPv6 dest address. */
	if ((m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_I]) &&
	    (m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_127_96_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_95_64_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_63_32_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_DST_31_0_I]))
		return false;

	/* Don't split inner IPv6 source address. */
	if ((m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_I] ||
	     m1[MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_I]) &&
	    (m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_127_96_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_95_64_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_63_32_I] ||
	     m2[MLX5HWS_DEFINER_FNAME_IPV6_SRC_31_0_I]))
		return false;

	/* Don't split GRE parameters. */
	if ((m1[MLX5HWS_DEFINER_FNAME_GRE_C] ||
	     m1[MLX5HWS_DEFINER_FNAME_GRE_K] ||
	     m1[MLX5HWS_DEFINER_FNAME_GRE_S] ||
	     m1[MLX5HWS_DEFINER_FNAME_GRE_PROTOCOL]) &&
	    (m2[MLX5HWS_DEFINER_FNAME_GRE_C] ||
	     m2[MLX5HWS_DEFINER_FNAME_GRE_K] ||
	     m2[MLX5HWS_DEFINER_FNAME_GRE_S] ||
	     m2[MLX5HWS_DEFINER_FNAME_GRE_PROTOCOL]))
		return false;

	/* Don't split TCP ack/seq numbers. */
	if ((m1[MLX5HWS_DEFINER_FNAME_TCP_ACK_NUM] ||
	     m1[MLX5HWS_DEFINER_FNAME_TCP_SEQ_NUM]) &&
	    (m2[MLX5HWS_DEFINER_FNAME_TCP_ACK_NUM] ||
	     m2[MLX5HWS_DEFINER_FNAME_TCP_SEQ_NUM]))
		return false;

	/* Don't split flex parser. */
	if ((m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_0] ||
	     m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_1] ||
	     m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_2] ||
	     m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_3] ||
	     m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_4] ||
	     m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_5] ||
	     m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_6] ||
	     m1[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_7]) &&
	    (m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_0] ||
	     m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_1] ||
	     m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_2] ||
	     m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_3] ||
	     m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_4] ||
	     m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_5] ||
	     m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_6] ||
	     m2[MLX5HWS_DEFINER_FNAME_FLEX_PARSER_7]))
		return false;

	return true;
}

static void
hws_bwc_matcher_complex_params_comb_create(struct mlx5hws_context *ctx,
					   struct mlx5hws_match_parameters *m,
					   struct mlx5hws_match_parameters *m1,
					   struct mlx5hws_match_parameters *m2,
					   struct mlx5hws_definer_fc *fc,
					   int fc_sz,
					   u32 combination_num)
{
	bool is_first_matcher;
	int i;

	memcpy(m1->match_buf, m->match_buf, m->match_sz);
	memcpy(m2->match_buf, m->match_buf, m->match_sz);

	for (i = 0; i < fc_sz; i++) {
		is_first_matcher = !(combination_num & BIT(i));
		hws_bwc_matcher_complex_params_clear_fld(ctx,
							 fc[i].fname,
							 is_first_matcher ?
							 m2 : m1);
	}

	MLX5_SET(fte_match_param, m2->match_buf,
		 misc_parameters_2.metadata_reg_c_6, -1);
}

static void
hws_bwc_matcher_complex_params_destroy(struct mlx5hws_match_parameters *mask_1,
				       struct mlx5hws_match_parameters *mask_2)
{
	kfree(mask_1->match_buf);
	kfree(mask_2->match_buf);
}

static int
hws_bwc_matcher_complex_params_create(struct mlx5hws_context *ctx,
				      u8 match_criteria,
				      struct mlx5hws_match_parameters *mask,
				      struct mlx5hws_match_parameters *mask_1,
				      struct mlx5hws_match_parameters *mask_2)
{
	struct mlx5hws_definer_fc *fc;
	u32 num_of_combinations;
	int fc_sz = 0;
	int res = 0;
	u32 i;

	if (MLX5_GET(fte_match_param, mask->match_buf,
		     misc_parameters_2.metadata_reg_c_6)) {
		mlx5hws_err(ctx, "Complex matcher: REG_C_6 matching is reserved\n");
		res = -EINVAL;
		goto out;
	}

	mask_1->match_buf = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param),
				    GFP_KERNEL);
	mask_2->match_buf = kzalloc(MLX5_ST_SZ_BYTES(fte_match_param),
				    GFP_KERNEL);
	if (!mask_1->match_buf || !mask_2->match_buf) {
		mlx5hws_err(ctx, "Complex matcher: failed to allocate match_param\n");
		res = -ENOMEM;
		goto free_params;
	}

	mask_1->match_sz = mask->match_sz;
	mask_2->match_sz = mask->match_sz;

	fc = mlx5hws_definer_conv_match_params_to_compressed_fc(ctx,
								match_criteria,
								mask->match_buf,
								&fc_sz);
	if (!fc) {
		res = -ENOMEM;
		goto free_params;
	}

	if (fc_sz >= sizeof(num_of_combinations) * BITS_PER_BYTE) {
		mlx5hws_err(ctx,
			    "Complex matcher: too many match parameters (%d)\n",
			    fc_sz);
		res = -EINVAL;
		goto free_fc;
	}

	/* We have list of all the match fields from the match parameter.
	 * Now try all the possibilities of splitting them into two match
	 * buffers and look for the supported combination.
	 */
	num_of_combinations = 1 << fc_sz;

	/* Start from combination at index 1 - we know that 0 is unsupported */
	for (i = 1; i < num_of_combinations; i++) {
		if (!hws_bwc_matcher_complex_params_comb_is_valid(fc, fc_sz, i))
			continue;

		hws_bwc_matcher_complex_params_comb_create(ctx,
							   mask, mask_1, mask_2,
							   fc, fc_sz, i);
		/* We now have two separate sets of match params.
		 * Check if each of them can be used in its own matcher.
		 */
		if (!mlx5hws_bwc_match_params_is_complex(ctx,
							 match_criteria,
							 mask_1) &&
		    !mlx5hws_bwc_match_params_is_complex(ctx,
							 match_criteria,
							 mask_2))
			break;
	}

	if (i == num_of_combinations) {
		/* We've scanned all the combinations, but to no avail */
		mlx5hws_err(ctx, "Complex matcher: couldn't find match params combination\n");
		res = -EINVAL;
		goto free_fc;
	}

	kfree(fc);
	return 0;

free_fc:
	kfree(fc);
free_params:
	hws_bwc_matcher_complex_params_destroy(mask_1, mask_2);
out:
	return res;
}

static int
hws_bwc_isolated_table_create(struct mlx5hws_bwc_matcher *bwc_matcher,
			      struct mlx5hws_table *table)
{
	struct mlx5hws_cmd_ft_modify_attr ft_attr = {0};
	struct mlx5hws_context *ctx = table->ctx;
	struct mlx5hws_table_attr tbl_attr = {0};
	struct mlx5hws_table *isolated_tbl;
	int ret = 0;

	tbl_attr.type = table->type;
	tbl_attr.level = table->level;

	bwc_matcher->complex->isolated_tbl =
		mlx5hws_table_create(ctx, &tbl_attr);
	isolated_tbl = bwc_matcher->complex->isolated_tbl;
	if (!isolated_tbl)
		return -EINVAL;

	/* Set the default miss of the isolated table to
	 * point to the end anchor of the original matcher.
	 */
	mlx5hws_cmd_set_attr_connect_miss_tbl(ctx,
					      isolated_tbl->fw_ft_type,
					      isolated_tbl->type,
					      &ft_attr);
	ft_attr.table_miss_id = bwc_matcher->matcher->end_ft_id;

	ret = mlx5hws_cmd_flow_table_modify(ctx->mdev,
					    &ft_attr,
					    isolated_tbl->ft_id);
	if (ret) {
		mlx5hws_err(ctx, "Failed setting isolated tbl default miss\n");
		goto destroy_tbl;
	}

	return 0;

destroy_tbl:
	mlx5hws_table_destroy(isolated_tbl);
	return ret;
}

static void hws_bwc_isolated_table_destroy(struct mlx5hws_table *isolated_tbl)
{
	/* This table is isolated - no table is pointing to it, no need to
	 * disconnect it from anywhere, it won't affect any other table's miss.
	 */
	mlx5hws_table_destroy(isolated_tbl);
}

static int
hws_bwc_isolated_matcher_create(struct mlx5hws_bwc_matcher *bwc_matcher,
				struct mlx5hws_table *table,
				u8 match_criteria_enable,
				struct mlx5hws_match_parameters *mask)
{
	struct mlx5hws_table *isolated_tbl = bwc_matcher->complex->isolated_tbl;
	struct mlx5hws_bwc_matcher *isolated_bwc_matcher;
	struct mlx5hws_context *ctx = table->ctx;
	int ret;

	isolated_bwc_matcher = kzalloc(sizeof(*bwc_matcher), GFP_KERNEL);
	if (!isolated_bwc_matcher)
		return -ENOMEM;

	bwc_matcher->complex->isolated_bwc_matcher = isolated_bwc_matcher;

	/* Isolated BWC matcher needs access to the first BWC matcher */
	isolated_bwc_matcher->complex_first_bwc_matcher = bwc_matcher;

	/* Isolated matcher needs to match on REG_C_6,
	 * so make sure its criteria bit is on.
	 */
	match_criteria_enable |= MLX5HWS_DEFINER_MATCH_CRITERIA_MISC2;

	ret = mlx5hws_bwc_matcher_create_simple(isolated_bwc_matcher,
						isolated_tbl,
						0,
						match_criteria_enable,
						mask,
						NULL);
	if (ret) {
		mlx5hws_err(ctx, "Complex matcher: failed creating isolated BWC matcher\n");
		goto free_matcher;
	}

	return 0;

free_matcher:
	kfree(bwc_matcher->complex->isolated_bwc_matcher);
	return ret;
}

static void
hws_bwc_isolated_matcher_destroy(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	mlx5hws_bwc_matcher_destroy_simple(bwc_matcher);
	kfree(bwc_matcher);
}

static int
hws_bwc_isolated_actions_create(struct mlx5hws_bwc_matcher *bwc_matcher,
				struct mlx5hws_table *table)
{
	struct mlx5hws_table *isolated_tbl = bwc_matcher->complex->isolated_tbl;
	u8 modify_hdr_action[MLX5_ST_SZ_BYTES(set_action_in)] = {0};
	struct mlx5hws_context *ctx = table->ctx;
	struct mlx5hws_action_mh_pattern ptrn;
	int ret = 0;

	/* Create action to jump to isolated table */

	bwc_matcher->complex->action_go_to_tbl =
		mlx5hws_action_create_dest_table(ctx,
						 isolated_tbl,
						 MLX5HWS_ACTION_FLAG_HWS_FDB);
	if (!bwc_matcher->complex->action_go_to_tbl) {
		mlx5hws_err(ctx, "Complex matcher: failed to create go-to-tbl action\n");
		return -EINVAL;
	}

	/* Create modify header action to set REG_C_6 */

	MLX5_SET(set_action_in, modify_hdr_action,
		 action_type, MLX5_MODIFICATION_TYPE_SET);
	MLX5_SET(set_action_in, modify_hdr_action,
		 field, MLX5_MODI_META_REG_C_6);
	MLX5_SET(set_action_in, modify_hdr_action,
		 length, 0); /* zero means length of 32 */
	MLX5_SET(set_action_in, modify_hdr_action, offset, 0);
	MLX5_SET(set_action_in, modify_hdr_action, data, 0);

	ptrn.data = (void *)modify_hdr_action;
	ptrn.sz = MLX5HWS_ACTION_DOUBLE_SIZE;

	bwc_matcher->complex->action_metadata =
		mlx5hws_action_create_modify_header(ctx, 1, &ptrn, 0,
						    MLX5HWS_ACTION_FLAG_HWS_FDB);
	if (!bwc_matcher->complex->action_metadata) {
		ret = -EINVAL;
		goto destroy_action_go_to_tbl;
	}

	/* Create last action */

	bwc_matcher->complex->action_last =
		mlx5hws_action_create_last(ctx, MLX5HWS_ACTION_FLAG_HWS_FDB);
	if (!bwc_matcher->complex->action_last) {
		mlx5hws_err(ctx, "Complex matcher: failed to create last action\n");
		ret = -EINVAL;
		goto destroy_action_metadata;
	}

	return 0;

destroy_action_metadata:
	mlx5hws_action_destroy(bwc_matcher->complex->action_metadata);
destroy_action_go_to_tbl:
	mlx5hws_action_destroy(bwc_matcher->complex->action_go_to_tbl);
	return ret;
}

static void
hws_bwc_isolated_actions_destroy(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	mlx5hws_action_destroy(bwc_matcher->complex->action_last);
	mlx5hws_action_destroy(bwc_matcher->complex->action_metadata);
	mlx5hws_action_destroy(bwc_matcher->complex->action_go_to_tbl);
}

int mlx5hws_bwc_matcher_create_complex(struct mlx5hws_bwc_matcher *bwc_matcher,
				       struct mlx5hws_table *table,
				       u32 priority,
				       u8 match_criteria_enable,
				       struct mlx5hws_match_parameters *mask)
{
	enum mlx5hws_action_type complex_init_action_types[3];
	struct mlx5hws_bwc_matcher *isolated_bwc_matcher;
	struct mlx5hws_match_parameters mask_1 = {0};
	struct mlx5hws_match_parameters mask_2 = {0};
	struct mlx5hws_context *ctx = table->ctx;
	int ret;

	ret = hws_bwc_matcher_complex_params_create(table->ctx,
						    match_criteria_enable,
						    mask, &mask_1, &mask_2);
	if (ret)
		goto err;

	bwc_matcher->complex =
		kzalloc(sizeof(*bwc_matcher->complex), GFP_KERNEL);
	if (!bwc_matcher->complex) {
		ret = -ENOMEM;
		goto free_masks;
	}

	ret = rhashtable_init(&bwc_matcher->complex->refcount_hash,
			      &hws_refcount_hash);
	if (ret) {
		mlx5hws_err(ctx, "Complex matcher: failed to initialize rhashtable\n");
		goto free_complex;
	}

	mutex_init(&bwc_matcher->complex->hash_lock);
	ida_init(&bwc_matcher->complex->metadata_ida);

	/* Create initial action template for the first matcher.
	 * Usually the initial AT is just dummy, but in case of complex
	 * matcher we know exactly which actions should it have.
	 */

	complex_init_action_types[0] = MLX5HWS_ACTION_TYP_MODIFY_HDR;
	complex_init_action_types[1] = MLX5HWS_ACTION_TYP_TBL;
	complex_init_action_types[2] = MLX5HWS_ACTION_TYP_LAST;

	/* Create the first matcher */

	ret = mlx5hws_bwc_matcher_create_simple(bwc_matcher,
						table,
						priority,
						match_criteria_enable,
						&mask_1,
						complex_init_action_types);
	if (ret)
		goto destroy_ida;

	/* Create isolated table to hold the second isolated matcher */

	ret = hws_bwc_isolated_table_create(bwc_matcher, table);
	if (ret) {
		mlx5hws_err(ctx, "Complex matcher: failed creating isolated table\n");
		goto destroy_first_matcher;
	}

	/* Now create the second BWC matcher - the isolated one */

	ret = hws_bwc_isolated_matcher_create(bwc_matcher, table,
					      match_criteria_enable, &mask_2);
	if (ret) {
		mlx5hws_err(ctx, "Complex matcher: failed creating isolated matcher\n");
		goto destroy_isolated_tbl;
	}

	/* Create action for isolated matcher's rules */

	ret = hws_bwc_isolated_actions_create(bwc_matcher, table);
	if (ret) {
		mlx5hws_err(ctx, "Complex matcher: failed creating isolated actions\n");
		goto destroy_isolated_matcher;
	}

	hws_bwc_matcher_complex_params_destroy(&mask_1, &mask_2);
	return 0;

destroy_isolated_matcher:
	isolated_bwc_matcher = bwc_matcher->complex->isolated_bwc_matcher;
	hws_bwc_isolated_matcher_destroy(isolated_bwc_matcher);
destroy_isolated_tbl:
	hws_bwc_isolated_table_destroy(bwc_matcher->complex->isolated_tbl);
destroy_first_matcher:
	mlx5hws_bwc_matcher_destroy_simple(bwc_matcher);
destroy_ida:
	ida_destroy(&bwc_matcher->complex->metadata_ida);
	mutex_destroy(&bwc_matcher->complex->hash_lock);
	rhashtable_destroy(&bwc_matcher->complex->refcount_hash);
free_complex:
	kfree(bwc_matcher->complex);
	bwc_matcher->complex = NULL;
free_masks:
	hws_bwc_matcher_complex_params_destroy(&mask_1, &mask_2);
err:
	return ret;
}

void
mlx5hws_bwc_matcher_destroy_complex(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_bwc_matcher *isolated_bwc_matcher =
		bwc_matcher->complex->isolated_bwc_matcher;

	hws_bwc_isolated_actions_destroy(bwc_matcher);
	hws_bwc_isolated_matcher_destroy(isolated_bwc_matcher);
	hws_bwc_isolated_table_destroy(bwc_matcher->complex->isolated_tbl);
	mlx5hws_bwc_matcher_destroy_simple(bwc_matcher);
	ida_destroy(&bwc_matcher->complex->metadata_ida);
	mutex_destroy(&bwc_matcher->complex->hash_lock);
	rhashtable_destroy(&bwc_matcher->complex->refcount_hash);
	kfree(bwc_matcher->complex);
	bwc_matcher->complex = NULL;
}

static void
hws_bwc_matcher_complex_hash_lock(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	mutex_lock(&bwc_matcher->complex->hash_lock);
}

static void
hws_bwc_matcher_complex_hash_unlock(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	mutex_unlock(&bwc_matcher->complex->hash_lock);
}

static int
hws_bwc_rule_complex_hash_node_get(struct mlx5hws_bwc_rule *bwc_rule,
				   struct mlx5hws_match_parameters *params)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_bwc_complex_rule_hash_node *node, *old_node;
	struct rhashtable *refcount_hash;
	int i;

	bwc_rule->complex_hash_node = NULL;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (unlikely(!node))
		return -ENOMEM;

	node->tag = ida_alloc(&bwc_matcher->complex->metadata_ida, GFP_KERNEL);
	refcount_set(&node->refcount, 1);

	/* Clear match buffer - turn off all the unrelated fields
	 * in accordance with the match params mask for the first
	 * matcher out of the two parts of the complex matcher.
	 * The resulting mask is the key for the hash.
	 */
	for (i = 0; i < MLX5_ST_SZ_DW_MATCH_PARAM; i++)
		node->match_buf[i] = params->match_buf[i] &
				     bwc_matcher->mt->match_param[i];

	refcount_hash = &bwc_matcher->complex->refcount_hash;
	old_node = rhashtable_lookup_get_insert_fast(refcount_hash,
						     &node->hash_node,
						     hws_refcount_hash);
	if (old_node) {
		/* Rule with the same tag already exists - update refcount */
		refcount_inc(&old_node->refcount);
		/* Let the new rule use the same tag as the existing rule.
		 * Note that we don't have any indication for the rule creation
		 * process that a rule with similar matching params already
		 * exists - no harm done when this rule is be overwritten by
		 * the same STE.
		 * There's some performance advantage in skipping such cases,
		 * so this is left for future optimizations.
		 */
		ida_free(&bwc_matcher->complex->metadata_ida, node->tag);
		kfree(node);
		node = old_node;
	}

	bwc_rule->complex_hash_node = node;
	return 0;
}

static void
hws_bwc_rule_complex_hash_node_put(struct mlx5hws_bwc_rule *bwc_rule,
				   bool *is_last_rule)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_bwc_complex_rule_hash_node *node;

	if (is_last_rule)
		*is_last_rule = false;

	node = bwc_rule->complex_hash_node;
	if (refcount_dec_and_test(&node->refcount)) {
		rhashtable_remove_fast(&bwc_matcher->complex->refcount_hash,
				       &node->hash_node,
				       hws_refcount_hash);
		ida_free(&bwc_matcher->complex->metadata_ida, node->tag);
		kfree(node);
		if (is_last_rule)
			*is_last_rule = true;
	}

	bwc_rule->complex_hash_node = NULL;
}

int mlx5hws_bwc_rule_create_complex(struct mlx5hws_bwc_rule *bwc_rule,
				    struct mlx5hws_match_parameters *params,
				    u32 flow_source,
				    struct mlx5hws_rule_action rule_actions[],
				    u16 bwc_queue_idx)
{
	struct mlx5hws_bwc_matcher *bwc_matcher = bwc_rule->bwc_matcher;
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	u8 modify_hdr_action[MLX5_ST_SZ_BYTES(set_action_in)] = {0};
	struct mlx5hws_rule_action rule_actions_1[3] = {0};
	struct mlx5hws_bwc_matcher *isolated_bwc_matcher;
	u32 *match_buf_2;
	u32 metadata_val;
	int ret = 0;

	isolated_bwc_matcher = bwc_matcher->complex->isolated_bwc_matcher;
	bwc_rule->isolated_bwc_rule =
		mlx5hws_bwc_rule_alloc(isolated_bwc_matcher);
	if (unlikely(!bwc_rule->isolated_bwc_rule))
		return -ENOMEM;

	hws_bwc_matcher_complex_hash_lock(bwc_matcher);

	/* Get a new hash node for this complex rule.
	 * If this is a unique set of match params for the first matcher,
	 * we will get a new hash node with newly allocated IDA.
	 * Otherwise we will get an existing node with IDA and updated refcount.
	 */
	ret = hws_bwc_rule_complex_hash_node_get(bwc_rule, params);
	if (unlikely(ret)) {
		mlx5hws_err(ctx, "Complex rule: failed getting RHT node for this rule\n");
		goto free_isolated_rule;
	}

	/* No need to clear match buffer's fields in accordance to what
	 * will actually be matched on first and second matchers.
	 * Both matchers were created with the appropriate masks
	 * and each of them holds the appropriate field copy array,
	 * so rule creation will use only the fields that will be copied
	 * in accordance with setters in field copy array.
	 * We do, however, need to temporary allocate match buffer
	 * for the second (isolated) rule in order to not modify
	 * user's match params buffer.
	 */

	match_buf_2 = kmemdup(params->match_buf,
			      MLX5_ST_SZ_BYTES(fte_match_param),
			      GFP_KERNEL);
	if (unlikely(!match_buf_2)) {
		mlx5hws_err(ctx, "Complex rule: failed allocating match_buf\n");
		ret = -ENOMEM;
		goto hash_node_put;
	}

	/* On 2nd matcher, use unique 32-bit ID as a matching tag */
	metadata_val = bwc_rule->complex_hash_node->tag;
	MLX5_SET(fte_match_param, match_buf_2,
		 misc_parameters_2.metadata_reg_c_6, metadata_val);

	/* Isolated rule's rule_actions contain all the original actions */
	ret = mlx5hws_bwc_rule_create_simple(bwc_rule->isolated_bwc_rule,
					     match_buf_2,
					     rule_actions,
					     flow_source,
					     bwc_queue_idx);
	kfree(match_buf_2);
	if (unlikely(ret)) {
		mlx5hws_err(ctx,
			    "Complex rule: failed creating isolated BWC rule (%d)\n",
			    ret);
		goto hash_node_put;
	}

	/* First rule's rule_actions contain setting metadata and
	 * jump to isolated table that contains the second matcher.
	 * Set metadata value to a unique value for this rule.
	 */

	MLX5_SET(set_action_in, modify_hdr_action,
		 action_type, MLX5_MODIFICATION_TYPE_SET);
	MLX5_SET(set_action_in, modify_hdr_action,
		 field, MLX5_MODI_META_REG_C_6);
	MLX5_SET(set_action_in, modify_hdr_action,
		 length, 0); /* zero means length of 32 */
	MLX5_SET(set_action_in, modify_hdr_action,
		 offset, 0);
	MLX5_SET(set_action_in, modify_hdr_action,
		 data, metadata_val);

	rule_actions_1[0].action = bwc_matcher->complex->action_metadata;
	rule_actions_1[0].modify_header.offset = 0;
	rule_actions_1[0].modify_header.data = modify_hdr_action;

	rule_actions_1[1].action = bwc_matcher->complex->action_go_to_tbl;
	rule_actions_1[2].action = bwc_matcher->complex->action_last;

	ret = mlx5hws_bwc_rule_create_simple(bwc_rule,
					     params->match_buf,
					     rule_actions_1,
					     flow_source,
					     bwc_queue_idx);

	if (unlikely(ret)) {
		mlx5hws_err(ctx,
			    "Complex rule: failed creating first BWC rule (%d)\n",
			    ret);
		goto destroy_isolated_rule;
	}

	hws_bwc_matcher_complex_hash_unlock(bwc_matcher);

	return 0;

destroy_isolated_rule:
	mlx5hws_bwc_rule_destroy_simple(bwc_rule->isolated_bwc_rule);
hash_node_put:
	hws_bwc_rule_complex_hash_node_put(bwc_rule, NULL);
free_isolated_rule:
	hws_bwc_matcher_complex_hash_unlock(bwc_matcher);
	mlx5hws_bwc_rule_free(bwc_rule->isolated_bwc_rule);
	return ret;
}

int mlx5hws_bwc_rule_destroy_complex(struct mlx5hws_bwc_rule *bwc_rule)
{
	struct mlx5hws_context *ctx = bwc_rule->bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_bwc_rule *isolated_bwc_rule;
	int ret_isolated, ret;
	bool is_last_rule;

	hws_bwc_matcher_complex_hash_lock(bwc_rule->bwc_matcher);

	hws_bwc_rule_complex_hash_node_put(bwc_rule, &is_last_rule);
	bwc_rule->rule->skip_delete = !is_last_rule;

	ret = mlx5hws_bwc_rule_destroy_simple(bwc_rule);
	if (unlikely(ret))
		mlx5hws_err(ctx, "BWC complex rule: failed destroying first rule\n");

	isolated_bwc_rule = bwc_rule->isolated_bwc_rule;
	ret_isolated = mlx5hws_bwc_rule_destroy_simple(isolated_bwc_rule);
	if (unlikely(ret_isolated))
		mlx5hws_err(ctx, "BWC complex rule: failed destroying second (isolated) rule\n");

	hws_bwc_matcher_complex_hash_unlock(bwc_rule->bwc_matcher);

	mlx5hws_bwc_rule_free(isolated_bwc_rule);

	return ret || ret_isolated;
}

static void
hws_bwc_matcher_clear_hash_rtcs(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_bwc_complex_rule_hash_node *node;
	struct rhashtable_iter iter;

	rhashtable_walk_enter(&bwc_matcher->complex->refcount_hash, &iter);
	rhashtable_walk_start(&iter);

	while ((node = rhashtable_walk_next(&iter)) != NULL) {
		if (IS_ERR(node))
			continue;
		node->rtc_valid = false;
	}

	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

int
mlx5hws_bwc_matcher_move_all_complex(struct mlx5hws_bwc_matcher *bwc_matcher)
{
	struct mlx5hws_context *ctx = bwc_matcher->matcher->tbl->ctx;
	struct mlx5hws_matcher *matcher = bwc_matcher->matcher;
	bool move_error = false, poll_error = false;
	u16 bwc_queues = mlx5hws_bwc_queues(ctx);
	struct mlx5hws_bwc_rule *tmp_bwc_rule;
	struct mlx5hws_rule_attr rule_attr;
	struct mlx5hws_table *isolated_tbl;
	struct mlx5hws_rule *tmp_rule;
	struct list_head *rules_list;
	u32 expected_completions = 1;
	u32 end_ft_id;
	int i, ret;

	/* We are rehashing the matcher that is the first part of the complex
	 * matcher. Need to update the isolated matcher to point to the end_ft
	 * of this new matcher. This needs to be done before moving any rules
	 * to prevent possible steering loops.
	 */
	isolated_tbl = bwc_matcher->complex->isolated_tbl;
	end_ft_id = bwc_matcher->matcher->resize_dst->end_ft_id;
	ret = mlx5hws_matcher_update_end_ft_isolated(isolated_tbl, end_ft_id);
	if (ret) {
		mlx5hws_err(ctx,
			    "Failed updating end_ft of isolated matcher (%d)\n",
			    ret);
		return ret;
	}

	hws_bwc_matcher_clear_hash_rtcs(bwc_matcher);

	mlx5hws_bwc_rule_fill_attr(bwc_matcher, 0, 0, &rule_attr);

	for (i = 0; i < bwc_queues; i++) {
		rules_list = &bwc_matcher->rules[i];
		if (list_empty(rules_list))
			continue;

		rule_attr.queue_id = mlx5hws_bwc_get_queue_id(ctx, i);

		list_for_each_entry(tmp_bwc_rule, rules_list, list_node) {
			/* Check if a rule with similar tag has already
			 * been moved.
			 */
			if (tmp_bwc_rule->complex_hash_node->rtc_valid) {
				/* This rule is a duplicate of rule with similar
				 * tag that has already been moved earlier.
				 * Just update this rule's RTCs.
				 */
				tmp_bwc_rule->rule->rtc_0 =
					tmp_bwc_rule->complex_hash_node->rtc_0;
				tmp_bwc_rule->rule->rtc_1 =
					tmp_bwc_rule->complex_hash_node->rtc_1;
				tmp_bwc_rule->rule->matcher =
					tmp_bwc_rule->rule->matcher->resize_dst;
				continue;
			}

			/* First time we're moving rule with this tag.
			 * Move it for real.
			 */
			tmp_rule = tmp_bwc_rule->rule;
			tmp_rule->skip_delete = false;
			ret = mlx5hws_matcher_resize_rule_move(matcher,
							       tmp_rule,
							       &rule_attr);
			if (unlikely(ret && !move_error)) {
				mlx5hws_err(ctx,
					    "Moving complex BWC rule failed (%d), attempting to move rest of the rules\n",
					    ret);
				move_error = true;
			}

			expected_completions = 1;
			ret = mlx5hws_bwc_queue_poll(ctx,
						     rule_attr.queue_id,
						     &expected_completions,
						     true);
			if (unlikely(ret && !poll_error)) {
				mlx5hws_err(ctx,
					    "Moving complex BWC rule: poll failed (%d), attempting to move rest of the rules\n",
					    ret);
				poll_error = true;
			}

			/* Done moving the rule to the new matcher,
			 * now update RTCs for all the duplicated rules.
			 */
			tmp_bwc_rule->complex_hash_node->rtc_0 =
				tmp_bwc_rule->rule->rtc_0;
			tmp_bwc_rule->complex_hash_node->rtc_1 =
				tmp_bwc_rule->rule->rtc_1;

			tmp_bwc_rule->complex_hash_node->rtc_valid = true;
		}
	}

	if (move_error || poll_error)
		ret = -EINVAL;

	return ret;
}
