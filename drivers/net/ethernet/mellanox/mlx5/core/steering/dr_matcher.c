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

static bool dr_mask_is_src_addr_set(struct mlx5dr_match_spec *spec)
{
	return (spec->src_ip_127_96 || spec->src_ip_95_64 ||
		spec->src_ip_63_32 || spec->src_ip_31_0);
}

static bool dr_mask_is_dst_addr_set(struct mlx5dr_match_spec *spec)
{
	return (spec->dst_ip_127_96 || spec->dst_ip_95_64 ||
		spec->dst_ip_63_32 || spec->dst_ip_31_0);
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

static bool dr_mask_is_gre_set(struct mlx5dr_match_misc *misc)
{
	return (misc->gre_key_h || misc->gre_key_l ||
		misc->gre_protocol || misc->gre_c_present ||
		misc->gre_k_present || misc->gre_s_present);
}

#define DR_MASK_IS_OUTER_MPLS_OVER_GRE_UDP_SET(_misc2, gre_udp) ( \
	(_misc2).outer_first_mpls_over_##gre_udp##_label || \
	(_misc2).outer_first_mpls_over_##gre_udp##_exp || \
	(_misc2).outer_first_mpls_over_##gre_udp##_s_bos || \
	(_misc2).outer_first_mpls_over_##gre_udp##_ttl)

#define DR_MASK_IS_FLEX_PARSER_0_SET(_misc2) ( \
	DR_MASK_IS_OUTER_MPLS_OVER_GRE_UDP_SET((_misc2), gre) || \
	DR_MASK_IS_OUTER_MPLS_OVER_GRE_UDP_SET((_misc2), udp))

static bool dr_mask_is_flex_parser_tnl_set(struct mlx5dr_match_misc3 *misc3)
{
	return (misc3->outer_vxlan_gpe_vni ||
		misc3->outer_vxlan_gpe_next_protocol ||
		misc3->outer_vxlan_gpe_flags);
}

static bool dr_mask_is_flex_parser_icmpv6_set(struct mlx5dr_match_misc3 *misc3)
{
	return (misc3->icmpv6_type || misc3->icmpv6_code ||
		misc3->icmpv6_header_data);
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

static bool
dr_matcher_supp_flex_parser_vxlan_gpe(struct mlx5dr_domain *dmn)
{
	return dmn->info.caps.flex_protocols &
	       MLX5_FLEX_PARSER_VXLAN_GPE_ENABLED;
}

int mlx5dr_matcher_select_builders(struct mlx5dr_matcher *matcher,
				   struct mlx5dr_matcher_rx_tx *nic_matcher,
				   bool ipv6)
{
	if (ipv6) {
		nic_matcher->ste_builder = nic_matcher->ste_builder6;
		nic_matcher->num_of_builders = nic_matcher->num_of_builders6;
	} else {
		nic_matcher->ste_builder = nic_matcher->ste_builder4;
		nic_matcher->num_of_builders = nic_matcher->num_of_builders4;
	}

	if (!nic_matcher->num_of_builders) {
		mlx5dr_dbg(matcher->tbl->dmn,
			   "Rule not supported on this matcher due to IP related fields\n");
		return -EINVAL;
	}

	return 0;
}

static int dr_matcher_set_ste_builders(struct mlx5dr_matcher *matcher,
				       struct mlx5dr_matcher_rx_tx *nic_matcher,
				       bool ipv6)
{
	struct mlx5dr_domain_rx_tx *nic_dmn = nic_matcher->nic_tbl->nic_dmn;
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	struct mlx5dr_match_param mask = {};
	struct mlx5dr_match_misc3 *misc3;
	struct mlx5dr_ste_build *sb;
	u8 *num_of_builders;
	bool inner, rx;
	int idx = 0;
	int ret, i;

	if (ipv6) {
		sb = nic_matcher->ste_builder6;
		num_of_builders = &nic_matcher->num_of_builders6;
	} else {
		sb = nic_matcher->ste_builder4;
		num_of_builders = &nic_matcher->num_of_builders4;
	}

	rx = nic_dmn->ste_type == MLX5DR_STE_TYPE_RX;

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

	ret = mlx5dr_ste_build_pre_check(dmn, matcher->match_criteria,
					 &matcher->mask, NULL);
	if (ret)
		return ret;

	/* Outer */
	if (matcher->match_criteria & (DR_MATCHER_CRITERIA_OUTER |
				       DR_MATCHER_CRITERIA_MISC |
				       DR_MATCHER_CRITERIA_MISC2 |
				       DR_MATCHER_CRITERIA_MISC3)) {
		inner = false;

		if (dr_mask_is_wqe_metadata_set(&mask.misc2))
			mlx5dr_ste_build_general_purpose(&sb[idx++], &mask, inner, rx);

		if (dr_mask_is_reg_c_0_3_set(&mask.misc2))
			mlx5dr_ste_build_register_0(&sb[idx++], &mask, inner, rx);

		if (dr_mask_is_reg_c_4_7_set(&mask.misc2))
			mlx5dr_ste_build_register_1(&sb[idx++], &mask, inner, rx);

		if (dr_mask_is_gvmi_or_qpn_set(&mask.misc) &&
		    (dmn->type == MLX5DR_DOMAIN_TYPE_FDB ||
		     dmn->type == MLX5DR_DOMAIN_TYPE_NIC_RX)) {
			ret = mlx5dr_ste_build_src_gvmi_qpn(&sb[idx++], &mask,
							    dmn, inner, rx);
			if (ret)
				return ret;
		}

		if (dr_mask_is_smac_set(&mask.outer) &&
		    dr_mask_is_dmac_set(&mask.outer)) {
			ret = mlx5dr_ste_build_eth_l2_src_des(&sb[idx++], &mask,
							      inner, rx);
			if (ret)
				return ret;
		}

		if (dr_mask_is_smac_set(&mask.outer))
			mlx5dr_ste_build_eth_l2_src(&sb[idx++], &mask, inner, rx);

		if (DR_MASK_IS_L2_DST(mask.outer, mask.misc, outer))
			mlx5dr_ste_build_eth_l2_dst(&sb[idx++], &mask, inner, rx);

		if (ipv6) {
			if (dr_mask_is_dst_addr_set(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv6_dst(&sb[idx++], &mask,
								 inner, rx);

			if (dr_mask_is_src_addr_set(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv6_src(&sb[idx++], &mask,
								 inner, rx);

			if (DR_MASK_IS_ETH_L4_SET(mask.outer, mask.misc, outer))
				mlx5dr_ste_build_ipv6_l3_l4(&sb[idx++], &mask,
							    inner, rx);
		} else {
			if (dr_mask_is_ipv4_5_tuple_set(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv4_5_tuple(&sb[idx++], &mask,
								     inner, rx);

			if (dr_mask_is_ttl_set(&mask.outer))
				mlx5dr_ste_build_eth_l3_ipv4_misc(&sb[idx++], &mask,
								  inner, rx);
		}

		if (dr_mask_is_flex_parser_tnl_set(&mask.misc3) &&
		    dr_matcher_supp_flex_parser_vxlan_gpe(dmn))
			mlx5dr_ste_build_flex_parser_tnl(&sb[idx++], &mask,
							 inner, rx);

		if (DR_MASK_IS_ETH_L4_MISC_SET(mask.misc3, outer))
			mlx5dr_ste_build_eth_l4_misc(&sb[idx++], &mask, inner, rx);

		if (DR_MASK_IS_FIRST_MPLS_SET(mask.misc2, outer))
			mlx5dr_ste_build_mpls(&sb[idx++], &mask, inner, rx);

		if (DR_MASK_IS_FLEX_PARSER_0_SET(mask.misc2))
			mlx5dr_ste_build_flex_parser_0(&sb[idx++], &mask,
						       inner, rx);

		misc3 = &mask.misc3;
		if ((DR_MASK_IS_FLEX_PARSER_ICMPV4_SET(misc3) &&
		     mlx5dr_matcher_supp_flex_parser_icmp_v4(&dmn->info.caps)) ||
		    (dr_mask_is_flex_parser_icmpv6_set(&mask.misc3) &&
		     mlx5dr_matcher_supp_flex_parser_icmp_v6(&dmn->info.caps))) {
			ret = mlx5dr_ste_build_flex_parser_1(&sb[idx++],
							     &mask, &dmn->info.caps,
							     inner, rx);
			if (ret)
				return ret;
		}
		if (dr_mask_is_gre_set(&mask.misc))
			mlx5dr_ste_build_gre(&sb[idx++], &mask, inner, rx);
	}

	/* Inner */
	if (matcher->match_criteria & (DR_MATCHER_CRITERIA_INNER |
				       DR_MATCHER_CRITERIA_MISC |
				       DR_MATCHER_CRITERIA_MISC2 |
				       DR_MATCHER_CRITERIA_MISC3)) {
		inner = true;

		if (dr_mask_is_eth_l2_tnl_set(&mask.misc))
			mlx5dr_ste_build_eth_l2_tnl(&sb[idx++], &mask, inner, rx);

		if (dr_mask_is_smac_set(&mask.inner) &&
		    dr_mask_is_dmac_set(&mask.inner)) {
			ret = mlx5dr_ste_build_eth_l2_src_des(&sb[idx++],
							      &mask, inner, rx);
			if (ret)
				return ret;
		}

		if (dr_mask_is_smac_set(&mask.inner))
			mlx5dr_ste_build_eth_l2_src(&sb[idx++], &mask, inner, rx);

		if (DR_MASK_IS_L2_DST(mask.inner, mask.misc, inner))
			mlx5dr_ste_build_eth_l2_dst(&sb[idx++], &mask, inner, rx);

		if (ipv6) {
			if (dr_mask_is_dst_addr_set(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv6_dst(&sb[idx++], &mask,
								 inner, rx);

			if (dr_mask_is_src_addr_set(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv6_src(&sb[idx++], &mask,
								 inner, rx);

			if (DR_MASK_IS_ETH_L4_SET(mask.inner, mask.misc, inner))
				mlx5dr_ste_build_ipv6_l3_l4(&sb[idx++], &mask,
							    inner, rx);
		} else {
			if (dr_mask_is_ipv4_5_tuple_set(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv4_5_tuple(&sb[idx++], &mask,
								     inner, rx);

			if (dr_mask_is_ttl_set(&mask.inner))
				mlx5dr_ste_build_eth_l3_ipv4_misc(&sb[idx++], &mask,
								  inner, rx);
		}

		if (DR_MASK_IS_ETH_L4_MISC_SET(mask.misc3, inner))
			mlx5dr_ste_build_eth_l4_misc(&sb[idx++], &mask, inner, rx);

		if (DR_MASK_IS_FIRST_MPLS_SET(mask.misc2, inner))
			mlx5dr_ste_build_mpls(&sb[idx++], &mask, inner, rx);

		if (DR_MASK_IS_FLEX_PARSER_0_SET(mask.misc2))
			mlx5dr_ste_build_flex_parser_0(&sb[idx++], &mask, inner, rx);
	}
	/* Empty matcher, takes all */
	if (matcher->match_criteria == DR_MATCHER_CRITERIA_EMPTY)
		mlx5dr_ste_build_empty_always_hit(&sb[idx++], rx);

	if (idx == 0) {
		mlx5dr_dbg(dmn, "Cannot generate any valid rules from mask\n");
		return -EINVAL;
	}

	/* Check that all mask fields were consumed */
	for (i = 0; i < sizeof(struct mlx5dr_match_param); i++) {
		if (((u8 *)&mask)[i] != 0) {
			mlx5dr_info(dmn, "Mask contains unsupported parameters\n");
			return -EOPNOTSUPP;
		}
	}

	*num_of_builders = idx;

	return 0;
}

static int dr_matcher_connect(struct mlx5dr_domain *dmn,
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

static int dr_matcher_add_to_tbl(struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_matcher *next_matcher, *prev_matcher, *tmp_matcher;
	struct mlx5dr_table *tbl = matcher->tbl;
	struct mlx5dr_domain *dmn = tbl->dmn;
	bool first = true;
	int ret;

	next_matcher = NULL;
	if (!list_empty(&tbl->matcher_list))
		list_for_each_entry(tmp_matcher, &tbl->matcher_list, matcher_list) {
			if (tmp_matcher->prio >= matcher->prio) {
				next_matcher = tmp_matcher;
				break;
			}
			first = false;
		}

	prev_matcher = NULL;
	if (next_matcher && !first)
		prev_matcher = list_prev_entry(next_matcher, matcher_list);
	else if (!first)
		prev_matcher = list_last_entry(&tbl->matcher_list,
					       struct mlx5dr_matcher,
					       matcher_list);

	if (dmn->type == MLX5DR_DOMAIN_TYPE_FDB ||
	    dmn->type == MLX5DR_DOMAIN_TYPE_NIC_RX) {
		ret = dr_matcher_connect(dmn, &matcher->rx,
					 next_matcher ? &next_matcher->rx : NULL,
					 prev_matcher ?	&prev_matcher->rx : NULL);
		if (ret)
			return ret;
	}

	if (dmn->type == MLX5DR_DOMAIN_TYPE_FDB ||
	    dmn->type == MLX5DR_DOMAIN_TYPE_NIC_TX) {
		ret = dr_matcher_connect(dmn, &matcher->tx,
					 next_matcher ? &next_matcher->tx : NULL,
					 prev_matcher ?	&prev_matcher->tx : NULL);
		if (ret)
			return ret;
	}

	if (prev_matcher)
		list_add(&matcher->matcher_list, &prev_matcher->matcher_list);
	else if (next_matcher)
		list_add_tail(&matcher->matcher_list,
			      &next_matcher->matcher_list);
	else
		list_add(&matcher->matcher_list, &tbl->matcher_list);

	return 0;
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

static int dr_matcher_init_nic(struct mlx5dr_matcher *matcher,
			       struct mlx5dr_matcher_rx_tx *nic_matcher)
{
	struct mlx5dr_domain *dmn = matcher->tbl->dmn;
	int ret, ret_v4, ret_v6;

	ret_v4 = dr_matcher_set_ste_builders(matcher, nic_matcher, false);
	ret_v6 = dr_matcher_set_ste_builders(matcher, nic_matcher, true);

	if (ret_v4 && ret_v6) {
		mlx5dr_dbg(dmn, "Cannot generate IPv4 or IPv6 rules with given mask\n");
		return -EINVAL;
	}

	if (!ret_v4)
		nic_matcher->ste_builder = nic_matcher->ste_builder4;
	else
		nic_matcher->ste_builder = nic_matcher->ste_builder6;

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

static int dr_matcher_init(struct mlx5dr_matcher *matcher,
			   struct mlx5dr_match_parameters *mask)
{
	struct mlx5dr_table *tbl = matcher->tbl;
	struct mlx5dr_domain *dmn = tbl->dmn;
	int ret;

	if (matcher->match_criteria >= DR_MATCHER_CRITERIA_MAX) {
		mlx5dr_info(dmn, "Invalid match criteria attribute\n");
		return -EINVAL;
	}

	if (mask) {
		if (mask->match_sz > sizeof(struct mlx5dr_match_param)) {
			mlx5dr_info(dmn, "Invalid match size attribute\n");
			return -EINVAL;
		}
		mlx5dr_ste_copy_param(matcher->match_criteria,
				      &matcher->mask, mask);
	}

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
		return -EINVAL;
	}

	return ret;
}

struct mlx5dr_matcher *
mlx5dr_matcher_create(struct mlx5dr_table *tbl,
		      u16 priority,
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
	INIT_LIST_HEAD(&matcher->matcher_list);

	mutex_lock(&tbl->dmn->mutex);

	ret = dr_matcher_init(matcher, mask);
	if (ret)
		goto free_matcher;

	ret = dr_matcher_add_to_tbl(matcher);
	if (ret)
		goto matcher_uninit;

	mutex_unlock(&tbl->dmn->mutex);

	return matcher;

matcher_uninit:
	dr_matcher_uninit(matcher);
free_matcher:
	mutex_unlock(&tbl->dmn->mutex);
	kfree(matcher);
dec_ref:
	refcount_dec(&tbl->refcount);
	return NULL;
}

static int dr_matcher_disconnect(struct mlx5dr_domain *dmn,
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

static int dr_matcher_remove_from_tbl(struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_matcher *prev_matcher, *next_matcher;
	struct mlx5dr_table *tbl = matcher->tbl;
	struct mlx5dr_domain *dmn = tbl->dmn;
	int ret = 0;

	if (list_is_last(&matcher->matcher_list, &tbl->matcher_list))
		next_matcher = NULL;
	else
		next_matcher = list_next_entry(matcher, matcher_list);

	if (matcher->matcher_list.prev == &tbl->matcher_list)
		prev_matcher = NULL;
	else
		prev_matcher = list_prev_entry(matcher, matcher_list);

	if (dmn->type == MLX5DR_DOMAIN_TYPE_FDB ||
	    dmn->type == MLX5DR_DOMAIN_TYPE_NIC_RX) {
		ret = dr_matcher_disconnect(dmn, &tbl->rx,
					    next_matcher ? &next_matcher->rx : NULL,
					    prev_matcher ? &prev_matcher->rx : NULL);
		if (ret)
			return ret;
	}

	if (dmn->type == MLX5DR_DOMAIN_TYPE_FDB ||
	    dmn->type == MLX5DR_DOMAIN_TYPE_NIC_TX) {
		ret = dr_matcher_disconnect(dmn, &tbl->tx,
					    next_matcher ? &next_matcher->tx : NULL,
					    prev_matcher ? &prev_matcher->tx : NULL);
		if (ret)
			return ret;
	}

	list_del(&matcher->matcher_list);

	return 0;
}

int mlx5dr_matcher_destroy(struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_table *tbl = matcher->tbl;

	if (refcount_read(&matcher->refcount) > 1)
		return -EBUSY;

	mutex_lock(&tbl->dmn->mutex);

	dr_matcher_remove_from_tbl(matcher);
	dr_matcher_uninit(matcher);
	refcount_dec(&matcher->tbl->refcount);

	mutex_unlock(&tbl->dmn->mutex);
	kfree(matcher);

	return 0;
}
