// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "dr_ste_v1.h"
#include "dr_ste_v2.h"

static void dr_ste_v3_set_encap(u8 *hw_ste_p, u8 *d_action,
				u32 reformat_id, int size)
{
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action, action_id,
		 DR_STE_V1_ACTION_ID_INSERT_POINTER);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action, size, size / 2);
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action, pointer, reformat_id);
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action, attributes,
		 DR_STE_V1_ACTION_INSERT_PTR_ATTR_ENCAP);
	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v3_set_push_vlan(u8 *ste, u8 *d_action,
				    u32 vlan_hdr)
{
	MLX5_SET(ste_double_action_insert_with_inline_v3, d_action, action_id,
		 DR_STE_V1_ACTION_ID_INSERT_INLINE);
	/* The hardware expects here offset to vlan header in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_inline_v3, d_action, start_offset,
		 HDR_LEN_L2_MACS >> 1);
	MLX5_SET(ste_double_action_insert_with_inline_v3, d_action, inline_data, vlan_hdr);
	dr_ste_v1_set_reparse(ste);
}

static void dr_ste_v3_set_pop_vlan(u8 *hw_ste_p, u8 *s_action,
				   u8 vlans_num)
{
	MLX5_SET(ste_single_action_remove_header_size_v3, s_action,
		 action_id, DR_STE_V1_ACTION_ID_REMOVE_BY_SIZE);
	MLX5_SET(ste_single_action_remove_header_size_v3, s_action,
		 start_anchor, DR_STE_HEADER_ANCHOR_1ST_VLAN);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_single_action_remove_header_size_v3, s_action,
		 remove_size, (HDR_LEN_L2_VLAN >> 1) * vlans_num);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v3_set_encap_l3(u8 *hw_ste_p,
				   u8 *frst_s_action,
				   u8 *scnd_d_action,
				   u32 reformat_id,
				   int size)
{
	/* Remove L2 headers */
	MLX5_SET(ste_single_action_remove_header_v3, frst_s_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	MLX5_SET(ste_single_action_remove_header_v3, frst_s_action, end_anchor,
		 DR_STE_HEADER_ANCHOR_IPV6_IPV4);

	/* Encapsulate with given reformat ID */
	MLX5_SET(ste_double_action_insert_with_ptr_v3, scnd_d_action, action_id,
		 DR_STE_V1_ACTION_ID_INSERT_POINTER);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_ptr_v3, scnd_d_action, size, size / 2);
	MLX5_SET(ste_double_action_insert_with_ptr_v3, scnd_d_action, pointer, reformat_id);
	MLX5_SET(ste_double_action_insert_with_ptr_v3, scnd_d_action, attributes,
		 DR_STE_V1_ACTION_INSERT_PTR_ATTR_ENCAP);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v3_set_rx_decap(u8 *hw_ste_p, u8 *s_action)
{
	MLX5_SET(ste_single_action_remove_header_v3, s_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	MLX5_SET(ste_single_action_remove_header_v3, s_action, decap, 1);
	MLX5_SET(ste_single_action_remove_header_v3, s_action, vni_to_cqe, 1);
	MLX5_SET(ste_single_action_remove_header_v3, s_action, end_anchor,
		 DR_STE_HEADER_ANCHOR_INNER_MAC);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v3_set_insert_hdr(u8 *hw_ste_p, u8 *d_action,
				     u32 reformat_id, u8 anchor,
				     u8 offset, int size)
{
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action,
		 action_id, DR_STE_V1_ACTION_ID_INSERT_POINTER);
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action,
		 start_anchor, anchor);

	/* The hardware expects here size and offset in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action,
		 size, size / 2);
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action,
		 start_offset, offset / 2);

	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action,
		 pointer, reformat_id);
	MLX5_SET(ste_double_action_insert_with_ptr_v3, d_action,
		 attributes, DR_STE_V1_ACTION_INSERT_PTR_ATTR_NONE);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v3_set_remove_hdr(u8 *hw_ste_p, u8 *s_action,
				     u8 anchor, u8 offset, int size)
{
	MLX5_SET(ste_single_action_remove_header_size_v3, s_action,
		 action_id, DR_STE_V1_ACTION_ID_REMOVE_BY_SIZE);
	MLX5_SET(ste_single_action_remove_header_size_v3, s_action,
		 start_anchor, anchor);

	/* The hardware expects here size and offset in words (2 byte) */
	MLX5_SET(ste_single_action_remove_header_size_v3, s_action,
		 remove_size, size / 2);
	MLX5_SET(ste_single_action_remove_header_size_v3, s_action,
		 start_offset, offset / 2);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static int
dr_ste_v3_set_action_decap_l3_list(void *data, u32 data_sz,
				   u8 *hw_action, u32 hw_action_sz,
				   uint16_t *used_hw_action_num)
{
	u8 padded_data[DR_STE_L2_HDR_MAX_SZ] = {};
	void *data_ptr = padded_data;
	u16 used_actions = 0;
	u32 inline_data_sz;
	u32 i;

	if (hw_action_sz / DR_STE_ACTION_DOUBLE_SZ < DR_STE_DECAP_L3_ACTION_NUM)
		return -EINVAL;

	inline_data_sz =
		MLX5_FLD_SZ_BYTES(ste_double_action_insert_with_inline_v3, inline_data);

	/* Add an alignment padding  */
	memcpy(padded_data + data_sz % inline_data_sz, data, data_sz);

	/* Remove L2L3 outer headers */
	MLX5_SET(ste_single_action_remove_header_v3, hw_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	MLX5_SET(ste_single_action_remove_header_v3, hw_action, decap, 1);
	MLX5_SET(ste_single_action_remove_header_v3, hw_action, vni_to_cqe, 1);
	MLX5_SET(ste_single_action_remove_header_v3, hw_action, end_anchor,
		 DR_STE_HEADER_ANCHOR_INNER_IPV6_IPV4);
	hw_action += DR_STE_ACTION_DOUBLE_SZ;
	used_actions++; /* Remove and NOP are a single double action */

	/* Point to the last dword of the header */
	data_ptr += (data_sz / inline_data_sz) * inline_data_sz;

	/* Add the new header using inline action 4Byte at a time, the header
	 * is added in reversed order to the beginning of the packet to avoid
	 * incorrect parsing by the HW. Since header is 14B or 18B an extra
	 * two bytes are padded and later removed.
	 */
	for (i = 0; i < data_sz / inline_data_sz + 1; i++) {
		void *addr_inline;

		MLX5_SET(ste_double_action_insert_with_inline_v3, hw_action, action_id,
			 DR_STE_V1_ACTION_ID_INSERT_INLINE);
		/* The hardware expects here offset to words (2 bytes) */
		MLX5_SET(ste_double_action_insert_with_inline_v3, hw_action, start_offset, 0);

		/* Copy bytes one by one to avoid endianness problem */
		addr_inline = MLX5_ADDR_OF(ste_double_action_insert_with_inline_v3,
					   hw_action, inline_data);
		memcpy(addr_inline, data_ptr - i * inline_data_sz, inline_data_sz);
		hw_action += DR_STE_ACTION_DOUBLE_SZ;
		used_actions++;
	}

	/* Remove first 2 extra bytes */
	MLX5_SET(ste_single_action_remove_header_size_v3, hw_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_BY_SIZE);
	MLX5_SET(ste_single_action_remove_header_size_v3, hw_action, start_offset, 0);
	/* The hardware expects here size in words (2 bytes) */
	MLX5_SET(ste_single_action_remove_header_size_v3, hw_action, remove_size, 1);
	used_actions++;

	*used_hw_action_num = used_actions;

	return 0;
}

static struct mlx5dr_ste_ctx ste_ctx_v3 = {
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
	.set_action_decap_l3_list	= &dr_ste_v3_set_action_decap_l3_list,
	.alloc_modify_hdr_chunk		= &dr_ste_v1_alloc_modify_hdr_ptrn_arg,
	.dealloc_modify_hdr_chunk	= &dr_ste_v1_free_modify_hdr_ptrn_arg,
	/* Actions bit set */
	.set_encap			= &dr_ste_v3_set_encap,
	.set_push_vlan			= &dr_ste_v3_set_push_vlan,
	.set_pop_vlan			= &dr_ste_v3_set_pop_vlan,
	.set_rx_decap			= &dr_ste_v3_set_rx_decap,
	.set_encap_l3			= &dr_ste_v3_set_encap_l3,
	.set_insert_hdr			= &dr_ste_v3_set_insert_hdr,
	.set_remove_hdr			= &dr_ste_v3_set_remove_hdr,
	/* Send */
	.prepare_for_postsend		= &dr_ste_v1_prepare_for_postsend,
};

struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx_v3(void)
{
	return &ste_ctx_v3;
}
