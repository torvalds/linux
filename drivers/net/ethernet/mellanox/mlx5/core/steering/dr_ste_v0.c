// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 NVIDIA CORPORATION. All rights reserved. */

#include <linux/types.h>
#include <linux/crc32.h>
#include "dr_ste.h"

#define SVLAN_ETHERTYPE		0x88a8
#define DR_STE_ENABLE_FLOW_TAG	BIT(31)

enum dr_ste_v0_action_tunl {
	DR_STE_TUNL_ACTION_NONE		= 0,
	DR_STE_TUNL_ACTION_ENABLE	= 1,
	DR_STE_TUNL_ACTION_DECAP	= 2,
	DR_STE_TUNL_ACTION_L3_DECAP	= 3,
	DR_STE_TUNL_ACTION_POP_VLAN	= 4,
};

enum dr_ste_v0_action_type {
	DR_STE_ACTION_TYPE_PUSH_VLAN	= 1,
	DR_STE_ACTION_TYPE_ENCAP_L3	= 3,
	DR_STE_ACTION_TYPE_ENCAP	= 4,
};

enum dr_ste_v0_action_mdfy_op {
	DR_STE_ACTION_MDFY_OP_COPY	= 0x1,
	DR_STE_ACTION_MDFY_OP_SET	= 0x2,
	DR_STE_ACTION_MDFY_OP_ADD	= 0x3,
};

#define DR_STE_CALC_LU_TYPE(lookup_type, rx, inner) \
	((inner) ? DR_STE_V0_LU_TYPE_##lookup_type##_I : \
		   (rx) ? DR_STE_V0_LU_TYPE_##lookup_type##_D : \
			  DR_STE_V0_LU_TYPE_##lookup_type##_O)

enum {
	DR_STE_V0_LU_TYPE_NOP				= 0x00,
	DR_STE_V0_LU_TYPE_SRC_GVMI_AND_QP		= 0x05,
	DR_STE_V0_LU_TYPE_ETHL2_TUNNELING_I		= 0x0a,
	DR_STE_V0_LU_TYPE_ETHL2_DST_O			= 0x06,
	DR_STE_V0_LU_TYPE_ETHL2_DST_I			= 0x07,
	DR_STE_V0_LU_TYPE_ETHL2_DST_D			= 0x1b,
	DR_STE_V0_LU_TYPE_ETHL2_SRC_O			= 0x08,
	DR_STE_V0_LU_TYPE_ETHL2_SRC_I			= 0x09,
	DR_STE_V0_LU_TYPE_ETHL2_SRC_D			= 0x1c,
	DR_STE_V0_LU_TYPE_ETHL2_SRC_DST_O		= 0x36,
	DR_STE_V0_LU_TYPE_ETHL2_SRC_DST_I		= 0x37,
	DR_STE_V0_LU_TYPE_ETHL2_SRC_DST_D		= 0x38,
	DR_STE_V0_LU_TYPE_ETHL3_IPV6_DST_O		= 0x0d,
	DR_STE_V0_LU_TYPE_ETHL3_IPV6_DST_I		= 0x0e,
	DR_STE_V0_LU_TYPE_ETHL3_IPV6_DST_D		= 0x1e,
	DR_STE_V0_LU_TYPE_ETHL3_IPV6_SRC_O		= 0x0f,
	DR_STE_V0_LU_TYPE_ETHL3_IPV6_SRC_I		= 0x10,
	DR_STE_V0_LU_TYPE_ETHL3_IPV6_SRC_D		= 0x1f,
	DR_STE_V0_LU_TYPE_ETHL3_IPV4_5_TUPLE_O		= 0x11,
	DR_STE_V0_LU_TYPE_ETHL3_IPV4_5_TUPLE_I		= 0x12,
	DR_STE_V0_LU_TYPE_ETHL3_IPV4_5_TUPLE_D		= 0x20,
	DR_STE_V0_LU_TYPE_ETHL3_IPV4_MISC_O		= 0x29,
	DR_STE_V0_LU_TYPE_ETHL3_IPV4_MISC_I		= 0x2a,
	DR_STE_V0_LU_TYPE_ETHL3_IPV4_MISC_D		= 0x2b,
	DR_STE_V0_LU_TYPE_ETHL4_O			= 0x13,
	DR_STE_V0_LU_TYPE_ETHL4_I			= 0x14,
	DR_STE_V0_LU_TYPE_ETHL4_D			= 0x21,
	DR_STE_V0_LU_TYPE_ETHL4_MISC_O			= 0x2c,
	DR_STE_V0_LU_TYPE_ETHL4_MISC_I			= 0x2d,
	DR_STE_V0_LU_TYPE_ETHL4_MISC_D			= 0x2e,
	DR_STE_V0_LU_TYPE_MPLS_FIRST_O			= 0x15,
	DR_STE_V0_LU_TYPE_MPLS_FIRST_I			= 0x24,
	DR_STE_V0_LU_TYPE_MPLS_FIRST_D			= 0x25,
	DR_STE_V0_LU_TYPE_GRE				= 0x16,
	DR_STE_V0_LU_TYPE_FLEX_PARSER_0			= 0x22,
	DR_STE_V0_LU_TYPE_FLEX_PARSER_1			= 0x23,
	DR_STE_V0_LU_TYPE_FLEX_PARSER_TNL_HEADER	= 0x19,
	DR_STE_V0_LU_TYPE_GENERAL_PURPOSE		= 0x18,
	DR_STE_V0_LU_TYPE_STEERING_REGISTERS_0		= 0x2f,
	DR_STE_V0_LU_TYPE_STEERING_REGISTERS_1		= 0x30,
	DR_STE_V0_LU_TYPE_DONT_CARE			= MLX5DR_STE_LU_TYPE_DONT_CARE,
};

enum {
	DR_STE_V0_ACTION_MDFY_FLD_L2_0		= 0,
	DR_STE_V0_ACTION_MDFY_FLD_L2_1		= 1,
	DR_STE_V0_ACTION_MDFY_FLD_L2_2		= 2,
	DR_STE_V0_ACTION_MDFY_FLD_L3_0		= 3,
	DR_STE_V0_ACTION_MDFY_FLD_L3_1		= 4,
	DR_STE_V0_ACTION_MDFY_FLD_L3_2		= 5,
	DR_STE_V0_ACTION_MDFY_FLD_L3_3		= 6,
	DR_STE_V0_ACTION_MDFY_FLD_L3_4		= 7,
	DR_STE_V0_ACTION_MDFY_FLD_L4_0		= 8,
	DR_STE_V0_ACTION_MDFY_FLD_L4_1		= 9,
	DR_STE_V0_ACTION_MDFY_FLD_MPLS		= 10,
	DR_STE_V0_ACTION_MDFY_FLD_L2_TNL_0	= 11,
	DR_STE_V0_ACTION_MDFY_FLD_REG_0		= 12,
	DR_STE_V0_ACTION_MDFY_FLD_REG_1		= 13,
	DR_STE_V0_ACTION_MDFY_FLD_REG_2		= 14,
	DR_STE_V0_ACTION_MDFY_FLD_REG_3		= 15,
	DR_STE_V0_ACTION_MDFY_FLD_L4_2		= 16,
	DR_STE_V0_ACTION_MDFY_FLD_FLEX_0	= 17,
	DR_STE_V0_ACTION_MDFY_FLD_FLEX_1	= 18,
	DR_STE_V0_ACTION_MDFY_FLD_FLEX_2	= 19,
	DR_STE_V0_ACTION_MDFY_FLD_FLEX_3	= 20,
	DR_STE_V0_ACTION_MDFY_FLD_L2_TNL_1	= 21,
	DR_STE_V0_ACTION_MDFY_FLD_METADATA	= 22,
	DR_STE_V0_ACTION_MDFY_FLD_RESERVED	= 23,
};

static const struct mlx5dr_ste_action_modify_field dr_ste_v0_action_modify_field_arr[] = {
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_47_16] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L2_1, .start = 16, .end = 47,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_15_0] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L2_1, .start = 0, .end = 15,
	},
	[MLX5_ACTION_IN_FIELD_OUT_ETHERTYPE] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L2_2, .start = 32, .end = 47,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_47_16] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L2_0, .start = 16, .end = 47,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_15_0] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L2_0, .start = 0, .end = 15,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_DSCP] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_1, .start = 0, .end = 5,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_FLAGS] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L4_0, .start = 48, .end = 56,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SPORT] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L4_0, .start = 0, .end = 15,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_DPORT] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L4_0, .start = 16, .end = 31,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_TTL] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_1, .start = 8, .end = 15,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IPV6_HOPLIMIT] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_1, .start = 8, .end = 15,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_SPORT] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L4_0, .start = 0, .end = 15,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_DPORT] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L4_0, .start = 16, .end = 31,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_127_96] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_3, .start = 32, .end = 63,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_95_64] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_3, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_63_32] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_4, .start = 32, .end = 63,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_31_0] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_4, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_127_96] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_0, .start = 32, .end = 63,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_95_64] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_0, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_63_32] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_2, .start = 32, .end = 63,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_31_0] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_2, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV4] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_0, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV4] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L3_0, .start = 32, .end = 63,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_A] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_METADATA, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_B] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_METADATA, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_0] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_REG_0, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_1] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_REG_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_2] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_REG_1, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_3] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_REG_1, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_4] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_REG_2, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_5] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_REG_2, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SEQ_NUM] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L4_1, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_ACK_NUM] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L4_1, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_FIRST_VID] = {
		.hw_field = DR_STE_V0_ACTION_MDFY_FLD_L2_2, .start = 0, .end = 15,
	},
};

static void dr_ste_v0_set_entry_type(u8 *hw_ste_p, u8 entry_type)
{
	MLX5_SET(ste_general, hw_ste_p, entry_type, entry_type);
}

static u8 dr_ste_v0_get_entry_type(u8 *hw_ste_p)
{
	return MLX5_GET(ste_general, hw_ste_p, entry_type);
}

static void dr_ste_v0_set_miss_addr(u8 *hw_ste_p, u64 miss_addr)
{
	u64 index = miss_addr >> 6;

	/* Miss address for TX and RX STEs located in the same offsets */
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, miss_address_39_32, index >> 26);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, miss_address_31_6, index);
}

static u64 dr_ste_v0_get_miss_addr(u8 *hw_ste_p)
{
	u64 index =
		((u64)MLX5_GET(ste_rx_steering_mult, hw_ste_p, miss_address_31_6) |
		 ((u64)MLX5_GET(ste_rx_steering_mult, hw_ste_p, miss_address_39_32)) << 26);

	return index << 6;
}

static void dr_ste_v0_set_byte_mask(u8 *hw_ste_p, u16 byte_mask)
{
	MLX5_SET(ste_general, hw_ste_p, byte_mask, byte_mask);
}

static u16 dr_ste_v0_get_byte_mask(u8 *hw_ste_p)
{
	return MLX5_GET(ste_general, hw_ste_p, byte_mask);
}

static void dr_ste_v0_set_lu_type(u8 *hw_ste_p, u16 lu_type)
{
	MLX5_SET(ste_general, hw_ste_p, entry_sub_type, lu_type);
}

static void dr_ste_v0_set_next_lu_type(u8 *hw_ste_p, u16 lu_type)
{
	MLX5_SET(ste_general, hw_ste_p, next_lu_type, lu_type);
}

static u16 dr_ste_v0_get_next_lu_type(u8 *hw_ste_p)
{
	return MLX5_GET(ste_general, hw_ste_p, next_lu_type);
}

static void dr_ste_v0_set_hit_gvmi(u8 *hw_ste_p, u16 gvmi)
{
	MLX5_SET(ste_general, hw_ste_p, next_table_base_63_48, gvmi);
}

static void dr_ste_v0_set_hit_addr(u8 *hw_ste_p, u64 icm_addr, u32 ht_size)
{
	u64 index = (icm_addr >> 5) | ht_size;

	MLX5_SET(ste_general, hw_ste_p, next_table_base_39_32_size, index >> 27);
	MLX5_SET(ste_general, hw_ste_p, next_table_base_31_5_size, index);
}

static void dr_ste_v0_init(u8 *hw_ste_p, u16 lu_type,
			   u8 entry_type, u16 gvmi)
{
	dr_ste_v0_set_entry_type(hw_ste_p, entry_type);
	dr_ste_v0_set_lu_type(hw_ste_p, lu_type);
	dr_ste_v0_set_next_lu_type(hw_ste_p, MLX5DR_STE_LU_TYPE_DONT_CARE);

	/* Set GVMI once, this is the same for RX/TX
	 * bits 63_48 of next table base / miss address encode the next GVMI
	 */
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, gvmi, gvmi);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, next_table_base_63_48, gvmi);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, miss_address_63_48, gvmi);
}

static void dr_ste_v0_rx_set_flow_tag(u8 *hw_ste_p, u32 flow_tag)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, qp_list_pointer,
		 DR_STE_ENABLE_FLOW_TAG | flow_tag);
}

static void dr_ste_v0_set_counter_id(u8 *hw_ste_p, u32 ctr_id)
{
	/* This can be used for both rx_steering_mult and for sx_transmit */
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, counter_trigger_15_0, ctr_id);
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, counter_trigger_23_16, ctr_id >> 16);
}

static void dr_ste_v0_set_go_back_bit(u8 *hw_ste_p)
{
	MLX5_SET(ste_sx_transmit, hw_ste_p, go_back, 1);
}

static void dr_ste_v0_set_tx_push_vlan(u8 *hw_ste_p, u32 vlan_hdr,
				       bool go_back)
{
	MLX5_SET(ste_sx_transmit, hw_ste_p, action_type,
		 DR_STE_ACTION_TYPE_PUSH_VLAN);
	MLX5_SET(ste_sx_transmit, hw_ste_p, encap_pointer_vlan_data, vlan_hdr);
	/* Due to HW limitation we need to set this bit, otherwise reformat +
	 * push vlan will not work.
	 */
	if (go_back)
		dr_ste_v0_set_go_back_bit(hw_ste_p);
}

static void dr_ste_v0_set_tx_encap(void *hw_ste_p, u32 reformat_id,
				   int size, bool encap_l3)
{
	MLX5_SET(ste_sx_transmit, hw_ste_p, action_type,
		 encap_l3 ? DR_STE_ACTION_TYPE_ENCAP_L3 : DR_STE_ACTION_TYPE_ENCAP);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_sx_transmit, hw_ste_p, action_description, size / 2);
	MLX5_SET(ste_sx_transmit, hw_ste_p, encap_pointer_vlan_data, reformat_id);
}

static void dr_ste_v0_set_rx_decap(u8 *hw_ste_p)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, tunneling_action,
		 DR_STE_TUNL_ACTION_DECAP);
}

static void dr_ste_v0_set_rx_pop_vlan(u8 *hw_ste_p)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, tunneling_action,
		 DR_STE_TUNL_ACTION_POP_VLAN);
}

static void dr_ste_v0_set_rx_decap_l3(u8 *hw_ste_p, bool vlan)
{
	MLX5_SET(ste_rx_steering_mult, hw_ste_p, tunneling_action,
		 DR_STE_TUNL_ACTION_L3_DECAP);
	MLX5_SET(ste_modify_packet, hw_ste_p, action_description, vlan ? 1 : 0);
}

static void dr_ste_v0_set_rewrite_actions(u8 *hw_ste_p, u16 num_of_actions,
					  u32 re_write_index)
{
	MLX5_SET(ste_modify_packet, hw_ste_p, number_of_re_write_actions,
		 num_of_actions);
	MLX5_SET(ste_modify_packet, hw_ste_p, header_re_write_actions_pointer,
		 re_write_index);
}

static void dr_ste_v0_arr_init_next(u8 **last_ste,
				    u32 *added_stes,
				    enum mlx5dr_ste_entry_type entry_type,
				    u16 gvmi)
{
	(*added_stes)++;
	*last_ste += DR_STE_SIZE;
	dr_ste_v0_init(*last_ste, MLX5DR_STE_LU_TYPE_DONT_CARE,
		       entry_type, gvmi);
}

static void
dr_ste_v0_set_actions_tx(struct mlx5dr_domain *dmn,
			 u8 *action_type_set,
			 u8 *last_ste,
			 struct mlx5dr_ste_actions_attr *attr,
			 u32 *added_stes)
{
	bool encap = action_type_set[DR_ACTION_TYP_L2_TO_TNL_L2] ||
		action_type_set[DR_ACTION_TYP_L2_TO_TNL_L3];

	/* We want to make sure the modify header comes before L2
	 * encapsulation. The reason for that is that we support
	 * modify headers for outer headers only
	 */
	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		dr_ste_v0_set_entry_type(last_ste, MLX5DR_STE_TYPE_MODIFY_PKT);
		dr_ste_v0_set_rewrite_actions(last_ste,
					      attr->modify_actions,
					      attr->modify_index);
	}

	if (action_type_set[DR_ACTION_TYP_PUSH_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			if (i || action_type_set[DR_ACTION_TYP_MODIFY_HDR])
				dr_ste_v0_arr_init_next(&last_ste,
							added_stes,
							MLX5DR_STE_TYPE_TX,
							attr->gvmi);

			dr_ste_v0_set_tx_push_vlan(last_ste,
						   attr->vlans.headers[i],
						   encap);
		}
	}

	if (encap) {
		/* Modify header and encapsulation require a different STEs.
		 * Since modify header STE format doesn't support encapsulation
		 * tunneling_action.
		 */
		if (action_type_set[DR_ACTION_TYP_MODIFY_HDR] ||
		    action_type_set[DR_ACTION_TYP_PUSH_VLAN])
			dr_ste_v0_arr_init_next(&last_ste,
						added_stes,
						MLX5DR_STE_TYPE_TX,
						attr->gvmi);

		dr_ste_v0_set_tx_encap(last_ste,
				       attr->reformat_id,
				       attr->reformat_size,
				       action_type_set[DR_ACTION_TYP_L2_TO_TNL_L3]);
		/* Whenever prio_tag_required enabled, we can be sure that the
		 * previous table (ACL) already push vlan to our packet,
		 * And due to HW limitation we need to set this bit, otherwise
		 * push vlan + reformat will not work.
		 */
		if (MLX5_CAP_GEN(dmn->mdev, prio_tag_required))
			dr_ste_v0_set_go_back_bit(last_ste);
	}

	if (action_type_set[DR_ACTION_TYP_CTR])
		dr_ste_v0_set_counter_id(last_ste, attr->ctr_id);

	dr_ste_v0_set_hit_gvmi(last_ste, attr->hit_gvmi);
	dr_ste_v0_set_hit_addr(last_ste, attr->final_icm_addr, 1);
}

static void
dr_ste_v0_set_actions_rx(struct mlx5dr_domain *dmn,
			 u8 *action_type_set,
			 u8 *last_ste,
			 struct mlx5dr_ste_actions_attr *attr,
			 u32 *added_stes)
{
	if (action_type_set[DR_ACTION_TYP_CTR])
		dr_ste_v0_set_counter_id(last_ste, attr->ctr_id);

	if (action_type_set[DR_ACTION_TYP_TNL_L3_TO_L2]) {
		dr_ste_v0_set_entry_type(last_ste, MLX5DR_STE_TYPE_MODIFY_PKT);
		dr_ste_v0_set_rx_decap_l3(last_ste, attr->decap_with_vlan);
		dr_ste_v0_set_rewrite_actions(last_ste,
					      attr->decap_actions,
					      attr->decap_index);
	}

	if (action_type_set[DR_ACTION_TYP_TNL_L2_TO_L2])
		dr_ste_v0_set_rx_decap(last_ste);

	if (action_type_set[DR_ACTION_TYP_POP_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			if (i ||
			    action_type_set[DR_ACTION_TYP_TNL_L2_TO_L2] ||
			    action_type_set[DR_ACTION_TYP_TNL_L3_TO_L2])
				dr_ste_v0_arr_init_next(&last_ste,
							added_stes,
							MLX5DR_STE_TYPE_RX,
							attr->gvmi);

			dr_ste_v0_set_rx_pop_vlan(last_ste);
		}
	}

	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		if (dr_ste_v0_get_entry_type(last_ste) == MLX5DR_STE_TYPE_MODIFY_PKT)
			dr_ste_v0_arr_init_next(&last_ste,
						added_stes,
						MLX5DR_STE_TYPE_MODIFY_PKT,
						attr->gvmi);
		else
			dr_ste_v0_set_entry_type(last_ste, MLX5DR_STE_TYPE_MODIFY_PKT);

		dr_ste_v0_set_rewrite_actions(last_ste,
					      attr->modify_actions,
					      attr->modify_index);
	}

	if (action_type_set[DR_ACTION_TYP_TAG]) {
		if (dr_ste_v0_get_entry_type(last_ste) == MLX5DR_STE_TYPE_MODIFY_PKT)
			dr_ste_v0_arr_init_next(&last_ste,
						added_stes,
						MLX5DR_STE_TYPE_RX,
						attr->gvmi);

		dr_ste_v0_rx_set_flow_tag(last_ste, attr->flow_tag);
	}

	dr_ste_v0_set_hit_gvmi(last_ste, attr->hit_gvmi);
	dr_ste_v0_set_hit_addr(last_ste, attr->final_icm_addr, 1);
}

static void dr_ste_v0_set_action_set(u8 *hw_action,
				     u8 hw_field,
				     u8 shifter,
				     u8 length,
				     u32 data)
{
	length = (length == 32) ? 0 : length;
	MLX5_SET(dr_action_hw_set, hw_action, opcode, DR_STE_ACTION_MDFY_OP_SET);
	MLX5_SET(dr_action_hw_set, hw_action, destination_field_code, hw_field);
	MLX5_SET(dr_action_hw_set, hw_action, destination_left_shifter, shifter);
	MLX5_SET(dr_action_hw_set, hw_action, destination_length, length);
	MLX5_SET(dr_action_hw_set, hw_action, inline_data, data);
}

static void dr_ste_v0_set_action_add(u8 *hw_action,
				     u8 hw_field,
				     u8 shifter,
				     u8 length,
				     u32 data)
{
	length = (length == 32) ? 0 : length;
	MLX5_SET(dr_action_hw_set, hw_action, opcode, DR_STE_ACTION_MDFY_OP_ADD);
	MLX5_SET(dr_action_hw_set, hw_action, destination_field_code, hw_field);
	MLX5_SET(dr_action_hw_set, hw_action, destination_left_shifter, shifter);
	MLX5_SET(dr_action_hw_set, hw_action, destination_length, length);
	MLX5_SET(dr_action_hw_set, hw_action, inline_data, data);
}

static void dr_ste_v0_set_action_copy(u8 *hw_action,
				      u8 dst_hw_field,
				      u8 dst_shifter,
				      u8 dst_len,
				      u8 src_hw_field,
				      u8 src_shifter)
{
	MLX5_SET(dr_action_hw_copy, hw_action, opcode, DR_STE_ACTION_MDFY_OP_COPY);
	MLX5_SET(dr_action_hw_copy, hw_action, destination_field_code, dst_hw_field);
	MLX5_SET(dr_action_hw_copy, hw_action, destination_left_shifter, dst_shifter);
	MLX5_SET(dr_action_hw_copy, hw_action, destination_length, dst_len);
	MLX5_SET(dr_action_hw_copy, hw_action, source_field_code, src_hw_field);
	MLX5_SET(dr_action_hw_copy, hw_action, source_left_shifter, src_shifter);
}

#define DR_STE_DECAP_L3_MIN_ACTION_NUM	5

static int
dr_ste_v0_set_action_decap_l3_list(void *data, u32 data_sz,
				   u8 *hw_action, u32 hw_action_sz,
				   u16 *used_hw_action_num)
{
	struct mlx5_ifc_l2_hdr_bits *l2_hdr = data;
	u32 hw_action_num;
	int required_actions;
	u32 hdr_fld_4b;
	u16 hdr_fld_2b;
	u16 vlan_type;
	bool vlan;

	vlan = (data_sz != HDR_LEN_L2);
	hw_action_num = hw_action_sz / MLX5_ST_SZ_BYTES(dr_action_hw_set);
	required_actions = DR_STE_DECAP_L3_MIN_ACTION_NUM + !!vlan;

	if (hw_action_num < required_actions)
		return -ENOMEM;

	/* dmac_47_16 */
	MLX5_SET(dr_action_hw_set, hw_action,
		 opcode, DR_STE_ACTION_MDFY_OP_SET);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_length, 0);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_field_code, DR_STE_V0_ACTION_MDFY_FLD_L2_0);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_left_shifter, 16);
	hdr_fld_4b = MLX5_GET(l2_hdr, l2_hdr, dmac_47_16);
	MLX5_SET(dr_action_hw_set, hw_action,
		 inline_data, hdr_fld_4b);
	hw_action += MLX5_ST_SZ_BYTES(dr_action_hw_set);

	/* smac_47_16 */
	MLX5_SET(dr_action_hw_set, hw_action,
		 opcode, DR_STE_ACTION_MDFY_OP_SET);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_length, 0);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_field_code, DR_STE_V0_ACTION_MDFY_FLD_L2_1);
	MLX5_SET(dr_action_hw_set, hw_action, destination_left_shifter, 16);
	hdr_fld_4b = (MLX5_GET(l2_hdr, l2_hdr, smac_31_0) >> 16 |
		      MLX5_GET(l2_hdr, l2_hdr, smac_47_32) << 16);
	MLX5_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_4b);
	hw_action += MLX5_ST_SZ_BYTES(dr_action_hw_set);

	/* dmac_15_0 */
	MLX5_SET(dr_action_hw_set, hw_action,
		 opcode, DR_STE_ACTION_MDFY_OP_SET);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_length, 16);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_field_code, DR_STE_V0_ACTION_MDFY_FLD_L2_0);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_left_shifter, 0);
	hdr_fld_2b = MLX5_GET(l2_hdr, l2_hdr, dmac_15_0);
	MLX5_SET(dr_action_hw_set, hw_action,
		 inline_data, hdr_fld_2b);
	hw_action += MLX5_ST_SZ_BYTES(dr_action_hw_set);

	/* ethertype + (optional) vlan */
	MLX5_SET(dr_action_hw_set, hw_action,
		 opcode, DR_STE_ACTION_MDFY_OP_SET);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_field_code, DR_STE_V0_ACTION_MDFY_FLD_L2_2);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_left_shifter, 32);
	if (!vlan) {
		hdr_fld_2b = MLX5_GET(l2_hdr, l2_hdr, ethertype);
		MLX5_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_2b);
		MLX5_SET(dr_action_hw_set, hw_action, destination_length, 16);
	} else {
		hdr_fld_2b = MLX5_GET(l2_hdr, l2_hdr, ethertype);
		vlan_type = hdr_fld_2b == SVLAN_ETHERTYPE ? DR_STE_SVLAN : DR_STE_CVLAN;
		hdr_fld_2b = MLX5_GET(l2_hdr, l2_hdr, vlan);
		hdr_fld_4b = (vlan_type << 16) | hdr_fld_2b;
		MLX5_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_4b);
		MLX5_SET(dr_action_hw_set, hw_action, destination_length, 18);
	}
	hw_action += MLX5_ST_SZ_BYTES(dr_action_hw_set);

	/* smac_15_0 */
	MLX5_SET(dr_action_hw_set, hw_action,
		 opcode, DR_STE_ACTION_MDFY_OP_SET);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_length, 16);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_field_code, DR_STE_V0_ACTION_MDFY_FLD_L2_1);
	MLX5_SET(dr_action_hw_set, hw_action,
		 destination_left_shifter, 0);
	hdr_fld_2b = MLX5_GET(l2_hdr, l2_hdr, smac_31_0);
	MLX5_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_2b);
	hw_action += MLX5_ST_SZ_BYTES(dr_action_hw_set);

	if (vlan) {
		MLX5_SET(dr_action_hw_set, hw_action,
			 opcode, DR_STE_ACTION_MDFY_OP_SET);
		hdr_fld_2b = MLX5_GET(l2_hdr, l2_hdr, vlan_type);
		MLX5_SET(dr_action_hw_set, hw_action,
			 inline_data, hdr_fld_2b);
		MLX5_SET(dr_action_hw_set, hw_action,
			 destination_length, 16);
		MLX5_SET(dr_action_hw_set, hw_action,
			 destination_field_code, DR_STE_V0_ACTION_MDFY_FLD_L2_2);
		MLX5_SET(dr_action_hw_set, hw_action,
			 destination_left_shifter, 0);
	}

	*used_hw_action_num = required_actions;

	return 0;
}

static void
dr_ste_v0_build_eth_l2_src_dst_bit_mask(struct mlx5dr_match_param *value,
					bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_dst, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst, bit_mask, dmac_15_0, mask, dmac_15_0);

	if (mask->smac_47_16 || mask->smac_15_0) {
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, smac_47_32,
			 mask->smac_47_16 >> 16);
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, smac_31_0,
			 mask->smac_47_16 << 16 | mask->smac_15_0);
		mask->smac_47_16 = 0;
		mask->smac_15_0 = 0;
	}

	DR_STE_SET_TAG(eth_l2_src_dst, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_TAG(eth_l2_src_dst, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_dst, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_ONES(eth_l2_src_dst, bit_mask, l3_type, mask, ip_version);

	if (mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
	} else if (mask->svlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, bit_mask, first_vlan_qualifier, -1);
		mask->svlan_tag = 0;
	}
}

static int
dr_ste_v0_build_eth_l2_src_dst_tag(struct mlx5dr_match_param *value,
				   struct mlx5dr_ste_build *sb,
				   u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_dst, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, dmac_15_0, spec, dmac_15_0);

	if (spec->smac_47_16 || spec->smac_15_0) {
		MLX5_SET(ste_eth_l2_src_dst, tag, smac_47_32,
			 spec->smac_47_16 >> 16);
		MLX5_SET(ste_eth_l2_src_dst, tag, smac_31_0,
			 spec->smac_47_16 << 16 | spec->smac_15_0);
		spec->smac_47_16 = 0;
		spec->smac_15_0 = 0;
	}

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			MLX5_SET(ste_eth_l2_src_dst, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			MLX5_SET(ste_eth_l2_src_dst, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			return -EINVAL;
		}
	}

	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_priority, spec, first_prio);

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}
	return 0;
}

static void
dr_ste_v0_build_eth_l2_src_dst_init(struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l2_src_dst_bit_mask(mask, sb->inner, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL2_SRC_DST, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l2_src_dst_tag;
}

static int
dr_ste_v0_build_eth_l3_ipv6_dst_tag(struct mlx5dr_match_param *value,
				    struct mlx5dr_ste_build *sb,
				    u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_127_96, spec, dst_ip_127_96);
	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_95_64, spec, dst_ip_95_64);
	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_63_32, spec, dst_ip_63_32);
	DR_STE_SET_TAG(eth_l3_ipv6_dst, tag, dst_ip_31_0, spec, dst_ip_31_0);

	return 0;
}

static void
dr_ste_v0_build_eth_l3_ipv6_dst_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l3_ipv6_dst_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV6_DST, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l3_ipv6_dst_tag;
}

static int
dr_ste_v0_build_eth_l3_ipv6_src_tag(struct mlx5dr_match_param *value,
				    struct mlx5dr_ste_build *sb,
				    u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_127_96, spec, src_ip_127_96);
	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_95_64, spec, src_ip_95_64);
	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_63_32, spec, src_ip_63_32);
	DR_STE_SET_TAG(eth_l3_ipv6_src, tag, src_ip_31_0, spec, src_ip_31_0);

	return 0;
}

static void
dr_ste_v0_build_eth_l3_ipv6_src_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l3_ipv6_src_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV6_SRC, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l3_ipv6_src_tag;
}

static int
dr_ste_v0_build_eth_l3_ipv4_5_tuple_tag(struct mlx5dr_match_param *value,
					struct mlx5dr_ste_build *sb,
					u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_address, spec, dst_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_address, spec, src_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, ecn, spec, ip_ecn);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

static void
dr_ste_v0_build_eth_l3_ipv4_5_tuple_init(struct mlx5dr_ste_build *sb,
					 struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l3_ipv4_5_tuple_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV4_5_TUPLE, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l3_ipv4_5_tuple_tag;
}

static void
dr_ste_v0_build_eth_l2_src_or_dst_bit_mask(struct mlx5dr_match_param *value,
					   bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_TAG(eth_l2_src, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_TAG(eth_l2_src, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_TAG(eth_l2_src, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_TAG(eth_l2_src, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_TAG(eth_l2_src, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_ONES(eth_l2_src, bit_mask, l3_type, mask, ip_version);

	if (mask->svlan_tag || mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}

	if (inner) {
		if (misc_mask->inner_second_cvlan_tag ||
		    misc_mask->inner_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, bit_mask, second_vlan_qualifier, -1);
			misc_mask->inner_second_cvlan_tag = 0;
			misc_mask->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src, bit_mask,
			       second_vlan_id, misc_mask, inner_second_vid);
		DR_STE_SET_TAG(eth_l2_src, bit_mask,
			       second_cfi, misc_mask, inner_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, bit_mask,
			       second_priority, misc_mask, inner_second_prio);
	} else {
		if (misc_mask->outer_second_cvlan_tag ||
		    misc_mask->outer_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, bit_mask, second_vlan_qualifier, -1);
			misc_mask->outer_second_cvlan_tag = 0;
			misc_mask->outer_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src, bit_mask,
			       second_vlan_id, misc_mask, outer_second_vid);
		DR_STE_SET_TAG(eth_l2_src, bit_mask,
			       second_cfi, misc_mask, outer_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, bit_mask,
			       second_priority, misc_mask, outer_second_prio);
	}
}

static int
dr_ste_v0_build_eth_l2_src_or_dst_tag(struct mlx5dr_match_param *value,
				      bool inner, u8 *tag)
{
	struct mlx5dr_match_spec *spec = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc_spec = &value->misc;

	DR_STE_SET_TAG(eth_l2_src, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_src, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_src, tag, l3_ethertype, spec, ethertype);

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			MLX5_SET(ste_eth_l2_src, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			MLX5_SET(ste_eth_l2_src, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			return -EINVAL;
		}
	}

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_src, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (inner) {
		if (misc_spec->inner_second_cvlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->inner_second_cvlan_tag = 0;
		} else if (misc_spec->inner_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src, tag, second_vlan_id, misc_spec, inner_second_vid);
		DR_STE_SET_TAG(eth_l2_src, tag, second_cfi, misc_spec, inner_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, tag, second_priority, misc_spec, inner_second_prio);
	} else {
		if (misc_spec->outer_second_cvlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->outer_second_cvlan_tag = 0;
		} else if (misc_spec->outer_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->outer_second_svlan_tag = 0;
		}
		DR_STE_SET_TAG(eth_l2_src, tag, second_vlan_id, misc_spec, outer_second_vid);
		DR_STE_SET_TAG(eth_l2_src, tag, second_cfi, misc_spec, outer_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, tag, second_priority, misc_spec, outer_second_prio);
	}

	return 0;
}

static void
dr_ste_v0_build_eth_l2_src_bit_mask(struct mlx5dr_match_param *value,
				    bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src, bit_mask, smac_47_16, mask, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src, bit_mask, smac_15_0, mask, smac_15_0);

	dr_ste_v0_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int
dr_ste_v0_build_eth_l2_src_tag(struct mlx5dr_match_param *value,
			       struct mlx5dr_ste_build *sb,
			       u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src, tag, smac_47_16, spec, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src, tag, smac_15_0, spec, smac_15_0);

	return dr_ste_v0_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void
dr_ste_v0_build_eth_l2_src_init(struct mlx5dr_ste_build *sb,
				struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l2_src_bit_mask(mask, sb->inner, sb->bit_mask);
	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL2_SRC, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l2_src_tag;
}

static void
dr_ste_v0_build_eth_l2_dst_bit_mask(struct mlx5dr_match_param *value,
				    struct mlx5dr_ste_build *sb,
				    u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_dst, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_dst, bit_mask, dmac_15_0, mask, dmac_15_0);

	dr_ste_v0_build_eth_l2_src_or_dst_bit_mask(value, sb->inner, bit_mask);
}

static int
dr_ste_v0_build_eth_l2_dst_tag(struct mlx5dr_match_param *value,
			       struct mlx5dr_ste_build *sb,
			       u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_dst, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_dst, tag, dmac_15_0, spec, dmac_15_0);

	return dr_ste_v0_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void
dr_ste_v0_build_eth_l2_dst_init(struct mlx5dr_ste_build *sb,
				struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l2_dst_bit_mask(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL2_DST, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l2_dst_tag;
}

static void
dr_ste_v0_build_eth_l2_tnl_bit_mask(struct mlx5dr_match_param *value,
				    bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(eth_l2_tnl, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_tnl, bit_mask, dmac_15_0, mask, dmac_15_0);
	DR_STE_SET_TAG(eth_l2_tnl, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_TAG(eth_l2_tnl, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_TAG(eth_l2_tnl, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_TAG(eth_l2_tnl, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_TAG(eth_l2_tnl, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_ONES(eth_l2_tnl, bit_mask, l3_type, mask, ip_version);

	if (misc->vxlan_vni) {
		MLX5_SET(ste_eth_l2_tnl, bit_mask,
			 l2_tunneling_network_id, (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (mask->svlan_tag || mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_tnl, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}
}

static int
dr_ste_v0_build_eth_l2_tnl_tag(struct mlx5dr_match_param *value,
			       struct mlx5dr_ste_build *sb,
			       u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(eth_l2_tnl, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_tnl, tag, dmac_15_0, spec, dmac_15_0);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_tnl, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_tnl, tag, l3_ethertype, spec, ethertype);

	if (misc->vxlan_vni) {
		MLX5_SET(ste_eth_l2_tnl, tag, l2_tunneling_network_id,
			 (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_tnl, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_tnl, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			MLX5_SET(ste_eth_l2_tnl, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			MLX5_SET(ste_eth_l2_tnl, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			return -EINVAL;
		}
	}

	return 0;
}

static void
dr_ste_v0_build_eth_l2_tnl_init(struct mlx5dr_ste_build *sb,
				struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l2_tnl_bit_mask(mask, sb->inner, sb->bit_mask);

	sb->lu_type = DR_STE_V0_LU_TYPE_ETHL2_TUNNELING_I;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l2_tnl_tag;
}

static int
dr_ste_v0_build_eth_l3_ipv4_misc_tag(struct mlx5dr_match_param *value,
				     struct mlx5dr_ste_build *sb,
				     u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_misc, tag, time_to_live, spec, ttl_hoplimit);

	return 0;
}

static void
dr_ste_v0_build_eth_l3_ipv4_misc_init(struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l3_ipv4_misc_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL3_IPV4_MISC, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l3_ipv4_misc_tag;
}

static int
dr_ste_v0_build_eth_ipv6_l3_l4_tag(struct mlx5dr_match_param *value,
				   struct mlx5dr_ste_build *sb,
				   u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l4, tag, dst_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l4, tag, src_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l4, tag, dst_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l4, tag, src_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l4, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l4, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l4, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l4, tag, ecn, spec, ip_ecn);
	DR_STE_SET_TAG(eth_l4, tag, ipv6_hop_limit, spec, ttl_hoplimit);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

static void
dr_ste_v0_build_eth_ipv6_l3_l4_init(struct mlx5dr_ste_build *sb,
				    struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_ipv6_l3_l4_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL4, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_ipv6_l3_l4_tag;
}

static int
dr_ste_v0_build_mpls_tag(struct mlx5dr_match_param *value,
			 struct mlx5dr_ste_build *sb,
			 u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;

	if (sb->inner)
		DR_STE_SET_MPLS(mpls, misc2, inner, tag);
	else
		DR_STE_SET_MPLS(mpls, misc2, outer, tag);

	return 0;
}

static void
dr_ste_v0_build_mpls_init(struct mlx5dr_ste_build *sb,
			  struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_mpls_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(MPLS_FIRST, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_mpls_tag;
}

static int
dr_ste_v0_build_tnl_gre_tag(struct mlx5dr_match_param *value,
			    struct mlx5dr_ste_build *sb,
			    u8 *tag)
{
	struct  mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(gre, tag, gre_protocol, misc, gre_protocol);

	DR_STE_SET_TAG(gre, tag, gre_k_present, misc, gre_k_present);
	DR_STE_SET_TAG(gre, tag, gre_key_h, misc, gre_key_h);
	DR_STE_SET_TAG(gre, tag, gre_key_l, misc, gre_key_l);

	DR_STE_SET_TAG(gre, tag, gre_c_present, misc, gre_c_present);

	DR_STE_SET_TAG(gre, tag, gre_s_present, misc, gre_s_present);

	return 0;
}

static void
dr_ste_v0_build_tnl_gre_init(struct mlx5dr_ste_build *sb,
			     struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_tnl_gre_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V0_LU_TYPE_GRE;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_tnl_gre_tag;
}

static int
dr_ste_v0_build_tnl_mpls_tag(struct mlx5dr_match_param *value,
			     struct mlx5dr_ste_build *sb,
			     u8 *tag)
{
	struct mlx5dr_match_misc2 *misc_2 = &value->misc2;

	if (DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(misc_2)) {
		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_label,
			       misc_2, outer_first_mpls_over_gre_label);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_exp,
			       misc_2, outer_first_mpls_over_gre_exp);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_s_bos,
			       misc_2, outer_first_mpls_over_gre_s_bos);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_ttl,
			       misc_2, outer_first_mpls_over_gre_ttl);
	} else {
		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_label,
			       misc_2, outer_first_mpls_over_udp_label);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_exp,
			       misc_2, outer_first_mpls_over_udp_exp);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_s_bos,
			       misc_2, outer_first_mpls_over_udp_s_bos);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_ttl,
			       misc_2, outer_first_mpls_over_udp_ttl);
	}
	return 0;
}

static void
dr_ste_v0_build_tnl_mpls_init(struct mlx5dr_ste_build *sb,
			      struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_tnl_mpls_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V0_LU_TYPE_FLEX_PARSER_0;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_tnl_mpls_tag;
}

#define ICMP_TYPE_OFFSET_FIRST_DW	24
#define ICMP_CODE_OFFSET_FIRST_DW	16

static int
dr_ste_v0_build_icmp_tag(struct mlx5dr_match_param *value,
			 struct mlx5dr_ste_build *sb,
			 u8 *tag)
{
	struct mlx5dr_match_misc3 *misc_3 = &value->misc3;
	u32 *icmp_header_data;
	int dw0_location;
	int dw1_location;
	u8 *icmp_type;
	u8 *icmp_code;
	bool is_ipv4;

	is_ipv4 = DR_MASK_IS_ICMPV4_SET(misc_3);
	if (is_ipv4) {
		icmp_header_data	= &misc_3->icmpv4_header_data;
		icmp_type		= &misc_3->icmpv4_type;
		icmp_code		= &misc_3->icmpv4_code;
		dw0_location		= sb->caps->flex_parser_id_icmp_dw0;
		dw1_location		= sb->caps->flex_parser_id_icmp_dw1;
	} else {
		icmp_header_data	= &misc_3->icmpv6_header_data;
		icmp_type		= &misc_3->icmpv6_type;
		icmp_code		= &misc_3->icmpv6_code;
		dw0_location		= sb->caps->flex_parser_id_icmpv6_dw0;
		dw1_location		= sb->caps->flex_parser_id_icmpv6_dw1;
	}

	switch (dw0_location) {
	case 4:
		MLX5_SET(ste_flex_parser_1, tag, flex_parser_4,
			 (*icmp_type << ICMP_TYPE_OFFSET_FIRST_DW) |
			 (*icmp_code << ICMP_TYPE_OFFSET_FIRST_DW));

		*icmp_type = 0;
		*icmp_code = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (dw1_location) {
	case 5:
		MLX5_SET(ste_flex_parser_1, tag, flex_parser_5,
			 *icmp_header_data);
		*icmp_header_data = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
dr_ste_v0_build_icmp_init(struct mlx5dr_ste_build *sb,
			  struct mlx5dr_match_param *mask)
{
	int ret;

	ret = dr_ste_v0_build_icmp_tag(mask, sb, sb->bit_mask);
	if (ret)
		return ret;

	sb->lu_type = DR_STE_V0_LU_TYPE_FLEX_PARSER_1;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_icmp_tag;

	return 0;
}

static int
dr_ste_v0_build_general_purpose_tag(struct mlx5dr_match_param *value,
				    struct mlx5dr_ste_build *sb,
				    u8 *tag)
{
	struct mlx5dr_match_misc2 *misc_2 = &value->misc2;

	DR_STE_SET_TAG(general_purpose, tag, general_purpose_lookup_field,
		       misc_2, metadata_reg_a);

	return 0;
}

static void
dr_ste_v0_build_general_purpose_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_general_purpose_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V0_LU_TYPE_GENERAL_PURPOSE;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_general_purpose_tag;
}

static int
dr_ste_v0_build_eth_l4_misc_tag(struct mlx5dr_match_param *value,
				struct mlx5dr_ste_build *sb,
				u8 *tag)
{
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;

	if (sb->inner) {
		DR_STE_SET_TAG(eth_l4_misc, tag, seq_num, misc3, inner_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc, tag, ack_num, misc3, inner_tcp_ack_num);
	} else {
		DR_STE_SET_TAG(eth_l4_misc, tag, seq_num, misc3, outer_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc, tag, ack_num, misc3, outer_tcp_ack_num);
	}

	return 0;
}

static void
dr_ste_v0_build_eth_l4_misc_init(struct mlx5dr_ste_build *sb,
				 struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_eth_l4_misc_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_LU_TYPE(ETHL4_MISC, sb->rx, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_eth_l4_misc_tag;
}

static int
dr_ste_v0_build_flex_parser_tnl_vxlan_gpe_tag(struct mlx5dr_match_param *value,
					      struct mlx5dr_ste_build *sb,
					      u8 *tag)
{
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;

	DR_STE_SET_TAG(flex_parser_tnl_vxlan_gpe, tag,
		       outer_vxlan_gpe_flags, misc3,
		       outer_vxlan_gpe_flags);
	DR_STE_SET_TAG(flex_parser_tnl_vxlan_gpe, tag,
		       outer_vxlan_gpe_next_protocol, misc3,
		       outer_vxlan_gpe_next_protocol);
	DR_STE_SET_TAG(flex_parser_tnl_vxlan_gpe, tag,
		       outer_vxlan_gpe_vni, misc3,
		       outer_vxlan_gpe_vni);

	return 0;
}

static void
dr_ste_v0_build_flex_parser_tnl_vxlan_gpe_init(struct mlx5dr_ste_build *sb,
					       struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_flex_parser_tnl_vxlan_gpe_tag(mask, sb, sb->bit_mask);
	sb->lu_type = DR_STE_V0_LU_TYPE_FLEX_PARSER_TNL_HEADER;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_flex_parser_tnl_vxlan_gpe_tag;
}

static int
dr_ste_v0_build_flex_parser_tnl_geneve_tag(struct mlx5dr_match_param *value,
					   struct mlx5dr_ste_build *sb,
					   u8 *tag)
{
	struct mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_protocol_type, misc, geneve_protocol_type);
	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_oam, misc, geneve_oam);
	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_opt_len, misc, geneve_opt_len);
	DR_STE_SET_TAG(flex_parser_tnl_geneve, tag,
		       geneve_vni, misc, geneve_vni);

	return 0;
}

static void
dr_ste_v0_build_flex_parser_tnl_geneve_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_flex_parser_tnl_geneve_tag(mask, sb, sb->bit_mask);
	sb->lu_type = DR_STE_V0_LU_TYPE_FLEX_PARSER_TNL_HEADER;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_flex_parser_tnl_geneve_tag;
}

static int
dr_ste_v0_build_register_0_tag(struct mlx5dr_match_param *value,
			       struct mlx5dr_ste_build *sb,
			       u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;

	DR_STE_SET_TAG(register_0, tag, register_0_h, misc2, metadata_reg_c_0);
	DR_STE_SET_TAG(register_0, tag, register_0_l, misc2, metadata_reg_c_1);
	DR_STE_SET_TAG(register_0, tag, register_1_h, misc2, metadata_reg_c_2);
	DR_STE_SET_TAG(register_0, tag, register_1_l, misc2, metadata_reg_c_3);

	return 0;
}

static void
dr_ste_v0_build_register_0_init(struct mlx5dr_ste_build *sb,
				struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_register_0_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V0_LU_TYPE_STEERING_REGISTERS_0;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_register_0_tag;
}

static int
dr_ste_v0_build_register_1_tag(struct mlx5dr_match_param *value,
			       struct mlx5dr_ste_build *sb,
			       u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;

	DR_STE_SET_TAG(register_1, tag, register_2_h, misc2, metadata_reg_c_4);
	DR_STE_SET_TAG(register_1, tag, register_2_l, misc2, metadata_reg_c_5);
	DR_STE_SET_TAG(register_1, tag, register_3_h, misc2, metadata_reg_c_6);
	DR_STE_SET_TAG(register_1, tag, register_3_l, misc2, metadata_reg_c_7);

	return 0;
}

static void
dr_ste_v0_build_register_1_init(struct mlx5dr_ste_build *sb,
				struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_register_1_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V0_LU_TYPE_STEERING_REGISTERS_1;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_register_1_tag;
}

static void
dr_ste_v0_build_src_gvmi_qpn_bit_mask(struct mlx5dr_match_param *value,
				      u8 *bit_mask)
{
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_ONES(src_gvmi_qp, bit_mask, source_gvmi, misc_mask, source_port);
	DR_STE_SET_ONES(src_gvmi_qp, bit_mask, source_qp, misc_mask, source_sqn);
	misc_mask->source_eswitch_owner_vhca_id = 0;
}

static int
dr_ste_v0_build_src_gvmi_qpn_tag(struct mlx5dr_match_param *value,
				 struct mlx5dr_ste_build *sb,
				 u8 *tag)
{
	struct mlx5dr_match_misc *misc = &value->misc;
	struct mlx5dr_cmd_vport_cap *vport_cap;
	struct mlx5dr_domain *dmn = sb->dmn;
	struct mlx5dr_cmd_caps *caps;
	u8 *bit_mask = sb->bit_mask;
	bool source_gvmi_set;

	DR_STE_SET_TAG(src_gvmi_qp, tag, source_qp, misc, source_sqn);

	if (sb->vhca_id_valid) {
		/* Find port GVMI based on the eswitch_owner_vhca_id */
		if (misc->source_eswitch_owner_vhca_id == dmn->info.caps.gvmi)
			caps = &dmn->info.caps;
		else if (dmn->peer_dmn && (misc->source_eswitch_owner_vhca_id ==
					   dmn->peer_dmn->info.caps.gvmi))
			caps = &dmn->peer_dmn->info.caps;
		else
			return -EINVAL;

		misc->source_eswitch_owner_vhca_id = 0;
	} else {
		caps = &dmn->info.caps;
	}

	source_gvmi_set = MLX5_GET(ste_src_gvmi_qp, bit_mask, source_gvmi);
	if (source_gvmi_set) {
		vport_cap = mlx5dr_get_vport_cap(caps, misc->source_port);
		if (!vport_cap) {
			mlx5dr_err(dmn, "Vport 0x%x is invalid\n",
				   misc->source_port);
			return -EINVAL;
		}

		if (vport_cap->vport_gvmi)
			MLX5_SET(ste_src_gvmi_qp, tag, source_gvmi, vport_cap->vport_gvmi);

		misc->source_port = 0;
	}

	return 0;
}

static void
dr_ste_v0_build_src_gvmi_qpn_init(struct mlx5dr_ste_build *sb,
				  struct mlx5dr_match_param *mask)
{
	dr_ste_v0_build_src_gvmi_qpn_bit_mask(mask, sb->bit_mask);

	sb->lu_type = DR_STE_V0_LU_TYPE_SRC_GVMI_AND_QP;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v0_build_src_gvmi_qpn_tag;
}

struct mlx5dr_ste_ctx ste_ctx_v0 = {
	/* Builders */
	.build_eth_l2_src_dst_init	= &dr_ste_v0_build_eth_l2_src_dst_init,
	.build_eth_l3_ipv6_src_init	= &dr_ste_v0_build_eth_l3_ipv6_src_init,
	.build_eth_l3_ipv6_dst_init	= &dr_ste_v0_build_eth_l3_ipv6_dst_init,
	.build_eth_l3_ipv4_5_tuple_init	= &dr_ste_v0_build_eth_l3_ipv4_5_tuple_init,
	.build_eth_l2_src_init		= &dr_ste_v0_build_eth_l2_src_init,
	.build_eth_l2_dst_init		= &dr_ste_v0_build_eth_l2_dst_init,
	.build_eth_l2_tnl_init		= &dr_ste_v0_build_eth_l2_tnl_init,
	.build_eth_l3_ipv4_misc_init	= &dr_ste_v0_build_eth_l3_ipv4_misc_init,
	.build_eth_ipv6_l3_l4_init	= &dr_ste_v0_build_eth_ipv6_l3_l4_init,
	.build_mpls_init		= &dr_ste_v0_build_mpls_init,
	.build_tnl_gre_init		= &dr_ste_v0_build_tnl_gre_init,
	.build_tnl_mpls_init		= &dr_ste_v0_build_tnl_mpls_init,
	.build_icmp_init		= &dr_ste_v0_build_icmp_init,
	.build_general_purpose_init	= &dr_ste_v0_build_general_purpose_init,
	.build_eth_l4_misc_init		= &dr_ste_v0_build_eth_l4_misc_init,
	.build_tnl_vxlan_gpe_init	= &dr_ste_v0_build_flex_parser_tnl_vxlan_gpe_init,
	.build_tnl_geneve_init		= &dr_ste_v0_build_flex_parser_tnl_geneve_init,
	.build_register_0_init		= &dr_ste_v0_build_register_0_init,
	.build_register_1_init		= &dr_ste_v0_build_register_1_init,
	.build_src_gvmi_qpn_init	= &dr_ste_v0_build_src_gvmi_qpn_init,

	/* Getters and Setters */
	.ste_init			= &dr_ste_v0_init,
	.set_next_lu_type		= &dr_ste_v0_set_next_lu_type,
	.get_next_lu_type		= &dr_ste_v0_get_next_lu_type,
	.set_miss_addr			= &dr_ste_v0_set_miss_addr,
	.get_miss_addr			= &dr_ste_v0_get_miss_addr,
	.set_hit_addr			= &dr_ste_v0_set_hit_addr,
	.set_byte_mask			= &dr_ste_v0_set_byte_mask,
	.get_byte_mask			= &dr_ste_v0_get_byte_mask,

	/* Actions */
	.set_actions_rx			= &dr_ste_v0_set_actions_rx,
	.set_actions_tx			= &dr_ste_v0_set_actions_tx,
	.modify_field_arr_sz		= ARRAY_SIZE(dr_ste_v0_action_modify_field_arr),
	.modify_field_arr		= dr_ste_v0_action_modify_field_arr,
	.set_action_set			= &dr_ste_v0_set_action_set,
	.set_action_add			= &dr_ste_v0_set_action_add,
	.set_action_copy		= &dr_ste_v0_set_action_copy,
	.set_action_decap_l3_list	= &dr_ste_v0_set_action_decap_l3_list,
};
