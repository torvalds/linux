// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "dr_ste_v1.h"
#include "dr_ste_v2.h"

static struct mlx5dr_ste_ctx ste_ctx_v2 = {
	/* Builders */
	.build_eth_l2_src_dst_init	= &dr_ste_v1_build_eth_l2_src_dst_init,
	.build_eth_l3_ipv6_src_init	= &dr_ste_v1_build_eth_l3_ipv6_src_init,
	.build_eth_l3_ipv6_dst_init	= &dr_ste_v1_build_eth_l3_ipv6_dst_init,
	.build_eth_l3_ipv4_5_tuple_init	= &dr_ste_v1_build_eth_l3_ipv4_5_tuple_init,
	.build_eth_l2_src_init		= &dr_ste_v1_build_eth_l2_src_init,
	.build_eth_l2_dst_init		= &dr_ste_v1_build_eth_l2_dst_init,
	.build_eth_l2_tnl_init		= &dr_ste_v1_build_eth_l2_tnl_init,
	.build_eth_l3_ipv4_misc_init	= &dr_ste_v1_build_eth_l3_ipv4_misc_init,
	.build_eth_ipv6_l3_l4_init	= &dr_ste_v1_build_eth_ipv6_l3_l4_init,
	.build_mpls_init		= &dr_ste_v1_build_mpls_init,
	.build_tnl_gre_init		= &dr_ste_v1_build_tnl_gre_init,
	.build_tnl_mpls_init		= &dr_ste_v1_build_tnl_mpls_init,
	.build_tnl_mpls_over_udp_init	= &dr_ste_v1_build_tnl_mpls_over_udp_init,
	.build_tnl_mpls_over_gre_init	= &dr_ste_v1_build_tnl_mpls_over_gre_init,
	.build_icmp_init		= &dr_ste_v1_build_icmp_init,
	.build_general_purpose_init	= &dr_ste_v1_build_general_purpose_init,
	.build_eth_l4_misc_init		= &dr_ste_v1_build_eth_l4_misc_init,
	.build_tnl_vxlan_gpe_init	= &dr_ste_v1_build_flex_parser_tnl_vxlan_gpe_init,
	.build_tnl_geneve_init		= &dr_ste_v1_build_flex_parser_tnl_geneve_init,
	.build_tnl_geneve_tlv_opt_init	= &dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_init,
	.build_tnl_geneve_tlv_opt_exist_init =
				  &dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_exist_init,
	.build_register_0_init		= &dr_ste_v1_build_register_0_init,
	.build_register_1_init		= &dr_ste_v1_build_register_1_init,
	.build_src_gvmi_qpn_init	= &dr_ste_v1_build_src_gvmi_qpn_init,
	.build_flex_parser_0_init	= &dr_ste_v1_build_flex_parser_0_init,
	.build_flex_parser_1_init	= &dr_ste_v1_build_flex_parser_1_init,
	.build_tnl_gtpu_init		= &dr_ste_v1_build_flex_parser_tnl_gtpu_init,
	.build_tnl_header_0_1_init	= &dr_ste_v1_build_tnl_header_0_1_init,
	.build_tnl_gtpu_flex_parser_0_init = &dr_ste_v1_build_tnl_gtpu_flex_parser_0_init,
	.build_tnl_gtpu_flex_parser_1_init = &dr_ste_v1_build_tnl_gtpu_flex_parser_1_init,

	/* Getters and Setters */
	.ste_init			= &dr_ste_v1_init,
	.set_next_lu_type		= &dr_ste_v1_set_next_lu_type,
	.get_next_lu_type		= &dr_ste_v1_get_next_lu_type,
	.is_miss_addr_set		= &dr_ste_v1_is_miss_addr_set,
	.set_miss_addr			= &dr_ste_v1_set_miss_addr,
	.get_miss_addr			= &dr_ste_v1_get_miss_addr,
	.set_hit_addr			= &dr_ste_v1_set_hit_addr,
	.set_byte_mask			= &dr_ste_v1_set_byte_mask,
	.get_byte_mask			= &dr_ste_v1_get_byte_mask,

	/* Actions */
	.actions_caps			= DR_STE_CTX_ACTION_CAP_TX_POP |
					  DR_STE_CTX_ACTION_CAP_RX_PUSH |
					  DR_STE_CTX_ACTION_CAP_RX_ENCAP,
	.set_actions_rx			= &dr_ste_v1_set_actions_rx,
	.set_actions_tx			= &dr_ste_v1_set_actions_tx,
	.modify_field_arr_sz		= ARRAY_SIZE(dr_ste_v2_action_modify_field_arr),
	.modify_field_arr		= dr_ste_v2_action_modify_field_arr,
	.set_action_set			= &dr_ste_v1_set_action_set,
	.set_action_add			= &dr_ste_v1_set_action_add,
	.set_action_copy		= &dr_ste_v1_set_action_copy,
	.set_action_decap_l3_list	= &dr_ste_v1_set_action_decap_l3_list,
	.alloc_modify_hdr_chunk		= &dr_ste_v1_alloc_modify_hdr_ptrn_arg,
	.dealloc_modify_hdr_chunk	= &dr_ste_v1_free_modify_hdr_ptrn_arg,
	/* Actions bit set */
	.set_encap			= &dr_ste_v1_set_encap,
	.set_push_vlan			= &dr_ste_v1_set_push_vlan,
	.set_pop_vlan			= &dr_ste_v1_set_pop_vlan,
	.set_rx_decap			= &dr_ste_v1_set_rx_decap,
	.set_encap_l3			= &dr_ste_v1_set_encap_l3,
	.set_insert_hdr			= &dr_ste_v1_set_insert_hdr,
	.set_remove_hdr			= &dr_ste_v1_set_remove_hdr,
	/* Send */
	.prepare_for_postsend		= &dr_ste_v1_prepare_for_postsend,
};

struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx_v2(void)
{
	return &ste_ctx_v2;
}
