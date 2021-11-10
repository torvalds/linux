// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 NVIDIA CORPORATION. All rights reserved. */

#include <linux/types.h>
#include "mlx5_ifc_dr_ste_v1.h"
#include "dr_ste.h"

#define DR_STE_CALC_DFNR_TYPE(lookup_type, inner) \
	((inner) ? DR_STE_V1_LU_TYPE_##lookup_type##_I : \
		   DR_STE_V1_LU_TYPE_##lookup_type##_O)

enum dr_ste_v1_entry_format {
	DR_STE_V1_TYPE_BWC_BYTE	= 0x0,
	DR_STE_V1_TYPE_BWC_DW	= 0x1,
	DR_STE_V1_TYPE_MATCH	= 0x2,
};

/* Lookup type is built from 2B: [ Definer mode 1B ][ Definer index 1B ] */
enum {
	DR_STE_V1_LU_TYPE_NOP				= 0x0000,
	DR_STE_V1_LU_TYPE_ETHL2_TNL			= 0x0002,
	DR_STE_V1_LU_TYPE_IBL3_EXT			= 0x0102,
	DR_STE_V1_LU_TYPE_ETHL2_O			= 0x0003,
	DR_STE_V1_LU_TYPE_IBL4				= 0x0103,
	DR_STE_V1_LU_TYPE_ETHL2_I			= 0x0004,
	DR_STE_V1_LU_TYPE_SRC_QP_GVMI			= 0x0104,
	DR_STE_V1_LU_TYPE_ETHL2_SRC_O			= 0x0005,
	DR_STE_V1_LU_TYPE_ETHL2_HEADERS_O		= 0x0105,
	DR_STE_V1_LU_TYPE_ETHL2_SRC_I			= 0x0006,
	DR_STE_V1_LU_TYPE_ETHL2_HEADERS_I		= 0x0106,
	DR_STE_V1_LU_TYPE_ETHL3_IPV4_5_TUPLE_O		= 0x0007,
	DR_STE_V1_LU_TYPE_IPV6_DES_O			= 0x0107,
	DR_STE_V1_LU_TYPE_ETHL3_IPV4_5_TUPLE_I		= 0x0008,
	DR_STE_V1_LU_TYPE_IPV6_DES_I			= 0x0108,
	DR_STE_V1_LU_TYPE_ETHL4_O			= 0x0009,
	DR_STE_V1_LU_TYPE_IPV6_SRC_O			= 0x0109,
	DR_STE_V1_LU_TYPE_ETHL4_I			= 0x000a,
	DR_STE_V1_LU_TYPE_IPV6_SRC_I			= 0x010a,
	DR_STE_V1_LU_TYPE_ETHL2_SRC_DST_O		= 0x000b,
	DR_STE_V1_LU_TYPE_MPLS_O			= 0x010b,
	DR_STE_V1_LU_TYPE_ETHL2_SRC_DST_I		= 0x000c,
	DR_STE_V1_LU_TYPE_MPLS_I			= 0x010c,
	DR_STE_V1_LU_TYPE_ETHL3_IPV4_MISC_O		= 0x000d,
	DR_STE_V1_LU_TYPE_GRE				= 0x010d,
	DR_STE_V1_LU_TYPE_FLEX_PARSER_TNL_HEADER	= 0x000e,
	DR_STE_V1_LU_TYPE_GENERAL_PURPOSE		= 0x010e,
	DR_STE_V1_LU_TYPE_ETHL3_IPV4_MISC_I		= 0x000f,
	DR_STE_V1_LU_TYPE_STEERING_REGISTERS_0		= 0x010f,
	DR_STE_V1_LU_TYPE_STEERING_REGISTERS_1		= 0x0110,
	DR_STE_V1_LU_TYPE_FLEX_PARSER_0			= 0x0111,
	DR_STE_V1_LU_TYPE_FLEX_PARSER_1			= 0x0112,
	DR_STE_V1_LU_TYPE_ETHL4_MISC_O			= 0x0113,
	DR_STE_V1_LU_TYPE_ETHL4_MISC_I			= 0x0114,
	DR_STE_V1_LU_TYPE_INVALID			= 0x00ff,
	DR_STE_V1_LU_TYPE_DONT_CARE			= MLX5DR_STE_LU_TYPE_DONT_CARE,
};

enum dr_ste_v1_header_anchors {
	DR_STE_HEADER_ANCHOR_START_OUTER		= 0x00,
	DR_STE_HEADER_ANCHOR_1ST_VLAN			= 0x02,
	DR_STE_HEADER_ANCHOR_IPV6_IPV4			= 0x07,
	DR_STE_HEADER_ANCHOR_INNER_MAC			= 0x13,
	DR_STE_HEADER_ANCHOR_INNER_IPV6_IPV4		= 0x19,
};

enum dr_ste_v1_action_size {
	DR_STE_ACTION_SINGLE_SZ = 4,
	DR_STE_ACTION_DOUBLE_SZ = 8,
	DR_STE_ACTION_TRIPLE_SZ = 12,
};

enum dr_ste_v1_action_insert_ptr_attr {
	DR_STE_V1_ACTION_INSERT_PTR_ATTR_NONE = 0,  /* Regular push header (e.g. push vlan) */
	DR_STE_V1_ACTION_INSERT_PTR_ATTR_ENCAP = 1, /* Encapsulation / Tunneling */
	DR_STE_V1_ACTION_INSERT_PTR_ATTR_ESP = 2,   /* IPsec */
};

enum dr_ste_v1_action_id {
	DR_STE_V1_ACTION_ID_NOP				= 0x00,
	DR_STE_V1_ACTION_ID_COPY			= 0x05,
	DR_STE_V1_ACTION_ID_SET				= 0x06,
	DR_STE_V1_ACTION_ID_ADD				= 0x07,
	DR_STE_V1_ACTION_ID_REMOVE_BY_SIZE		= 0x08,
	DR_STE_V1_ACTION_ID_REMOVE_HEADER_TO_HEADER	= 0x09,
	DR_STE_V1_ACTION_ID_INSERT_INLINE		= 0x0a,
	DR_STE_V1_ACTION_ID_INSERT_POINTER		= 0x0b,
	DR_STE_V1_ACTION_ID_FLOW_TAG			= 0x0c,
	DR_STE_V1_ACTION_ID_QUEUE_ID_SEL		= 0x0d,
	DR_STE_V1_ACTION_ID_ACCELERATED_LIST		= 0x0e,
	DR_STE_V1_ACTION_ID_MODIFY_LIST			= 0x0f,
	DR_STE_V1_ACTION_ID_TRAILER			= 0x13,
	DR_STE_V1_ACTION_ID_COUNTER_ID			= 0x14,
	DR_STE_V1_ACTION_ID_MAX				= 0x21,
	/* use for special cases */
	DR_STE_V1_ACTION_ID_SPECIAL_ENCAP_L3		= 0x22,
};

enum {
	DR_STE_V1_ACTION_MDFY_FLD_L2_OUT_0		= 0x00,
	DR_STE_V1_ACTION_MDFY_FLD_L2_OUT_1		= 0x01,
	DR_STE_V1_ACTION_MDFY_FLD_L2_OUT_2		= 0x02,
	DR_STE_V1_ACTION_MDFY_FLD_SRC_L2_OUT_0		= 0x08,
	DR_STE_V1_ACTION_MDFY_FLD_SRC_L2_OUT_1		= 0x09,
	DR_STE_V1_ACTION_MDFY_FLD_L3_OUT_0		= 0x0e,
	DR_STE_V1_ACTION_MDFY_FLD_L4_OUT_0		= 0x18,
	DR_STE_V1_ACTION_MDFY_FLD_L4_OUT_1		= 0x19,
	DR_STE_V1_ACTION_MDFY_FLD_IPV4_OUT_0		= 0x40,
	DR_STE_V1_ACTION_MDFY_FLD_IPV4_OUT_1		= 0x41,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_0	= 0x44,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_1	= 0x45,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_2	= 0x46,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_3	= 0x47,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_0	= 0x4c,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_1	= 0x4d,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_2	= 0x4e,
	DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_3	= 0x4f,
	DR_STE_V1_ACTION_MDFY_FLD_TCP_MISC_0		= 0x5e,
	DR_STE_V1_ACTION_MDFY_FLD_TCP_MISC_1		= 0x5f,
	DR_STE_V1_ACTION_MDFY_FLD_CFG_HDR_0_0		= 0x6f,
	DR_STE_V1_ACTION_MDFY_FLD_CFG_HDR_0_1		= 0x70,
	DR_STE_V1_ACTION_MDFY_FLD_METADATA_2_CQE	= 0x7b,
	DR_STE_V1_ACTION_MDFY_FLD_GNRL_PURPOSE		= 0x7c,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_2		= 0x8c,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_3		= 0x8d,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_4		= 0x8e,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_5		= 0x8f,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_6		= 0x90,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_7		= 0x91,
};

static const struct mlx5dr_ste_action_modify_field dr_ste_v1_action_modify_field_arr[] = {
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_47_16] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_SRC_L2_OUT_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_15_0] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_SRC_L2_OUT_1, .start = 16, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_ETHERTYPE] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L2_OUT_1, .start = 0, .end = 15,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_47_16] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L2_OUT_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_15_0] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L2_OUT_1, .start = 16, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_DSCP] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L3_OUT_0, .start = 18, .end = 23,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_FLAGS] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L4_OUT_1, .start = 16, .end = 24,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SPORT] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L4_OUT_0, .start = 16, .end = 31,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_DPORT] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L4_OUT_0, .start = 0, .end = 15,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_TTL] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L3_OUT_0, .start = 8, .end = 15,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IPV6_HOPLIMIT] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L3_OUT_0, .start = 8, .end = 15,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_SPORT] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L4_OUT_0, .start = 16, .end = 31,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_DPORT] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L4_OUT_0, .start = 0, .end = 15,
		.l4_type = DR_STE_ACTION_MDFY_TYPE_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_127_96] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_0, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_95_64] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_1, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_63_32] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_2, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_31_0] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_SRC_OUT_3, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_127_96] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_0, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_95_64] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_1, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_63_32] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_2, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_31_0] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV6_DST_OUT_3, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV4] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV4_OUT_0, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV4] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_IPV4_OUT_1, .start = 0, .end = 31,
		.l3_type = DR_STE_ACTION_MDFY_TYPE_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_A] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_GNRL_PURPOSE, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_B] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_METADATA_2_CQE, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_0] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_REGISTER_6, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_1] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_REGISTER_7, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_2] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_REGISTER_4, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_3] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_REGISTER_5, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_4] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_REGISTER_2, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_METADATA_REG_C_5] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_REGISTER_3, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SEQ_NUM] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_TCP_MISC_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_ACK_NUM] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_TCP_MISC_1, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_FIRST_VID] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_L2_OUT_2, .start = 0, .end = 15,
	},
	[MLX5_ACTION_IN_FIELD_OUT_EMD_31_0] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_CFG_HDR_0_1, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_EMD_47_32] = {
		.hw_field = DR_STE_V1_ACTION_MDFY_FLD_CFG_HDR_0_0, .start = 0, .end = 15,
	},
};

static void dr_ste_v1_set_entry_type(u8 *hw_ste_p, u8 entry_type)
{
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, entry_format, entry_type);
}

static void dr_ste_v1_set_miss_addr(u8 *hw_ste_p, u64 miss_addr)
{
	u64 index = miss_addr >> 6;

	MLX5_SET(ste_match_bwc_v1, hw_ste_p, miss_address_39_32, index >> 26);
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, miss_address_31_6, index);
}

static u64 dr_ste_v1_get_miss_addr(u8 *hw_ste_p)
{
	u64 index =
		((u64)MLX5_GET(ste_match_bwc_v1, hw_ste_p, miss_address_31_6) |
		 ((u64)MLX5_GET(ste_match_bwc_v1, hw_ste_p, miss_address_39_32)) << 26);

	return index << 6;
}

static void dr_ste_v1_set_byte_mask(u8 *hw_ste_p, u16 byte_mask)
{
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, byte_mask, byte_mask);
}

static u16 dr_ste_v1_get_byte_mask(u8 *hw_ste_p)
{
	return MLX5_GET(ste_match_bwc_v1, hw_ste_p, byte_mask);
}

static void dr_ste_v1_set_lu_type(u8 *hw_ste_p, u16 lu_type)
{
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, entry_format, lu_type >> 8);
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, match_definer_ctx_idx, lu_type & 0xFF);
}

static void dr_ste_v1_set_next_lu_type(u8 *hw_ste_p, u16 lu_type)
{
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, next_entry_format, lu_type >> 8);
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, hash_definer_ctx_idx, lu_type & 0xFF);
}

static u16 dr_ste_v1_get_next_lu_type(u8 *hw_ste_p)
{
	u8 mode = MLX5_GET(ste_match_bwc_v1, hw_ste_p, next_entry_format);
	u8 index = MLX5_GET(ste_match_bwc_v1, hw_ste_p, hash_definer_ctx_idx);

	return (mode << 8 | index);
}

static void dr_ste_v1_set_hit_gvmi(u8 *hw_ste_p, u16 gvmi)
{
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, next_table_base_63_48, gvmi);
}

static void dr_ste_v1_set_hit_addr(u8 *hw_ste_p, u64 icm_addr, u32 ht_size)
{
	u64 index = (icm_addr >> 5) | ht_size;

	MLX5_SET(ste_match_bwc_v1, hw_ste_p, next_table_base_39_32_size, index >> 27);
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, next_table_base_31_5_size, index);
}

static void dr_ste_v1_init(u8 *hw_ste_p, u16 lu_type,
			   bool is_rx, u16 gvmi)
{
	dr_ste_v1_set_lu_type(hw_ste_p, lu_type);
	dr_ste_v1_set_next_lu_type(hw_ste_p, MLX5DR_STE_LU_TYPE_DONT_CARE);

	MLX5_SET(ste_match_bwc_v1, hw_ste_p, gvmi, gvmi);
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, next_table_base_63_48, gvmi);
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, miss_address_63_48, gvmi);
}

static void dr_ste_v1_prepare_for_postsend(u8 *hw_ste_p,
					   u32 ste_size)
{
	u8 *tag = hw_ste_p + DR_STE_SIZE_CTRL;
	u8 *mask = tag + DR_STE_SIZE_TAG;
	u8 tmp_tag[DR_STE_SIZE_TAG] = {};

	if (ste_size == DR_STE_SIZE_CTRL)
		return;

	WARN_ON(ste_size != DR_STE_SIZE);

	/* Backup tag */
	memcpy(tmp_tag, tag, DR_STE_SIZE_TAG);

	/* Swap mask and tag  both are the same size */
	memcpy(tag, mask, DR_STE_SIZE_MASK);
	memcpy(mask, tmp_tag, DR_STE_SIZE_TAG);
}

static void dr_ste_v1_set_rx_flow_tag(u8 *s_action, u32 flow_tag)
{
	MLX5_SET(ste_single_action_flow_tag_v1, s_action, action_id,
		 DR_STE_V1_ACTION_ID_FLOW_TAG);
	MLX5_SET(ste_single_action_flow_tag_v1, s_action, flow_tag, flow_tag);
}

static void dr_ste_v1_set_counter_id(u8 *hw_ste_p, u32 ctr_id)
{
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, counter_id, ctr_id);
}

static void dr_ste_v1_set_reparse(u8 *hw_ste_p)
{
	MLX5_SET(ste_match_bwc_v1, hw_ste_p, reparse, 1);
}

static void dr_ste_v1_set_encap(u8 *hw_ste_p, u8 *d_action,
				u32 reformat_id, int size)
{
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, action_id,
		 DR_STE_V1_ACTION_ID_INSERT_POINTER);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, size, size / 2);
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, pointer, reformat_id);
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, attributes,
		 DR_STE_V1_ACTION_INSERT_PTR_ATTR_ENCAP);
	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_set_insert_hdr(u8 *hw_ste_p, u8 *d_action,
				     u32 reformat_id,
				     u8 anchor, u8 offset,
				     int size)
{
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action,
		 action_id, DR_STE_V1_ACTION_ID_INSERT_POINTER);
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, start_anchor, anchor);

	/* The hardware expects here size and offset in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, size, size / 2);
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, start_offset, offset / 2);

	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, pointer, reformat_id);
	MLX5_SET(ste_double_action_insert_with_ptr_v1, d_action, attributes,
		 DR_STE_V1_ACTION_INSERT_PTR_ATTR_NONE);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_set_remove_hdr(u8 *hw_ste_p, u8 *s_action,
				     u8 anchor, u8 offset,
				     int size)
{
	MLX5_SET(ste_single_action_remove_header_size_v1, s_action,
		 action_id, DR_STE_V1_ACTION_ID_REMOVE_BY_SIZE);
	MLX5_SET(ste_single_action_remove_header_size_v1, s_action, start_anchor, anchor);

	/* The hardware expects here size and offset in words (2 byte) */
	MLX5_SET(ste_single_action_remove_header_size_v1, s_action, remove_size, size / 2);
	MLX5_SET(ste_single_action_remove_header_size_v1, s_action, start_offset, offset / 2);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_set_push_vlan(u8 *hw_ste_p, u8 *d_action,
				    u32 vlan_hdr)
{
	MLX5_SET(ste_double_action_insert_with_inline_v1, d_action,
		 action_id, DR_STE_V1_ACTION_ID_INSERT_INLINE);
	/* The hardware expects offset to vlan header in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_inline_v1, d_action,
		 start_offset, HDR_LEN_L2_MACS >> 1);
	MLX5_SET(ste_double_action_insert_with_inline_v1, d_action,
		 inline_data, vlan_hdr);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_set_pop_vlan(u8 *hw_ste_p, u8 *s_action, u8 vlans_num)
{
	MLX5_SET(ste_single_action_remove_header_size_v1, s_action,
		 action_id, DR_STE_V1_ACTION_ID_REMOVE_BY_SIZE);
	MLX5_SET(ste_single_action_remove_header_size_v1, s_action,
		 start_anchor, DR_STE_HEADER_ANCHOR_1ST_VLAN);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_single_action_remove_header_size_v1, s_action,
		 remove_size, (HDR_LEN_L2_VLAN >> 1) * vlans_num);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_set_encap_l3(u8 *hw_ste_p,
				   u8 *frst_s_action,
				   u8 *scnd_d_action,
				   u32 reformat_id,
				   int size)
{
	/* Remove L2 headers */
	MLX5_SET(ste_single_action_remove_header_v1, frst_s_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	MLX5_SET(ste_single_action_remove_header_v1, frst_s_action, end_anchor,
		 DR_STE_HEADER_ANCHOR_IPV6_IPV4);

	/* Encapsulate with given reformat ID */
	MLX5_SET(ste_double_action_insert_with_ptr_v1, scnd_d_action, action_id,
		 DR_STE_V1_ACTION_ID_INSERT_POINTER);
	/* The hardware expects here size in words (2 byte) */
	MLX5_SET(ste_double_action_insert_with_ptr_v1, scnd_d_action, size, size / 2);
	MLX5_SET(ste_double_action_insert_with_ptr_v1, scnd_d_action, pointer, reformat_id);
	MLX5_SET(ste_double_action_insert_with_ptr_v1, scnd_d_action, attributes,
		 DR_STE_V1_ACTION_INSERT_PTR_ATTR_ENCAP);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_set_rx_decap(u8 *hw_ste_p, u8 *s_action)
{
	MLX5_SET(ste_single_action_remove_header_v1, s_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	MLX5_SET(ste_single_action_remove_header_v1, s_action, decap, 1);
	MLX5_SET(ste_single_action_remove_header_v1, s_action, vni_to_cqe, 1);
	MLX5_SET(ste_single_action_remove_header_v1, s_action, end_anchor,
		 DR_STE_HEADER_ANCHOR_INNER_MAC);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_set_rewrite_actions(u8 *hw_ste_p,
					  u8 *s_action,
					  u16 num_of_actions,
					  u32 re_write_index)
{
	MLX5_SET(ste_single_action_modify_list_v1, s_action, action_id,
		 DR_STE_V1_ACTION_ID_MODIFY_LIST);
	MLX5_SET(ste_single_action_modify_list_v1, s_action, num_of_modify_actions,
		 num_of_actions);
	MLX5_SET(ste_single_action_modify_list_v1, s_action, modify_actions_ptr,
		 re_write_index);

	dr_ste_v1_set_reparse(hw_ste_p);
}

static void dr_ste_v1_arr_init_next_match(u8 **last_ste,
					  u32 *added_stes,
					  u16 gvmi)
{
	u8 *action;

	(*added_stes)++;
	*last_ste += DR_STE_SIZE;
	dr_ste_v1_init(*last_ste, MLX5DR_STE_LU_TYPE_DONT_CARE, 0, gvmi);
	dr_ste_v1_set_entry_type(*last_ste, DR_STE_V1_TYPE_MATCH);

	action = MLX5_ADDR_OF(ste_mask_and_match_v1, *last_ste, action);
	memset(action, 0, MLX5_FLD_SZ_BYTES(ste_mask_and_match_v1, action));
}

static void dr_ste_v1_set_actions_tx(struct mlx5dr_domain *dmn,
				     u8 *action_type_set,
				     u8 *last_ste,
				     struct mlx5dr_ste_actions_attr *attr,
				     u32 *added_stes)
{
	u8 *action = MLX5_ADDR_OF(ste_match_bwc_v1, last_ste, action);
	u8 action_sz = DR_STE_ACTION_DOUBLE_SZ;
	bool allow_modify_hdr = true;
	bool allow_encap = true;

	if (action_type_set[DR_ACTION_TYP_POP_VLAN]) {
		if (action_sz < DR_STE_ACTION_SINGLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes,
						      attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1,
					      last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}
		dr_ste_v1_set_pop_vlan(last_ste, action, attr->vlans.count);
		action_sz -= DR_STE_ACTION_SINGLE_SZ;
		action += DR_STE_ACTION_SINGLE_SZ;
		allow_modify_hdr = false;
	}

	if (action_type_set[DR_ACTION_TYP_CTR])
		dr_ste_v1_set_counter_id(last_ste, attr->ctr_id);

	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		if (!allow_modify_hdr || action_sz < DR_STE_ACTION_DOUBLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes,
						      attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1,
					      last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}
		dr_ste_v1_set_rewrite_actions(last_ste, action,
					      attr->modify_actions,
					      attr->modify_index);
		action_sz -= DR_STE_ACTION_DOUBLE_SZ;
		action += DR_STE_ACTION_DOUBLE_SZ;
		allow_encap = false;
	}

	if (action_type_set[DR_ACTION_TYP_PUSH_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			if (action_sz < DR_STE_ACTION_DOUBLE_SZ || !allow_encap) {
				dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
				action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
				action_sz = DR_STE_ACTION_TRIPLE_SZ;
				allow_encap = true;
			}
			dr_ste_v1_set_push_vlan(last_ste, action,
						attr->vlans.headers[i]);
			action_sz -= DR_STE_ACTION_DOUBLE_SZ;
			action += DR_STE_ACTION_DOUBLE_SZ;
		}
	}

	if (action_type_set[DR_ACTION_TYP_L2_TO_TNL_L2]) {
		if (!allow_encap || action_sz < DR_STE_ACTION_DOUBLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
			allow_encap = true;
		}
		dr_ste_v1_set_encap(last_ste, action,
				    attr->reformat.id,
				    attr->reformat.size);
		action_sz -= DR_STE_ACTION_DOUBLE_SZ;
		action += DR_STE_ACTION_DOUBLE_SZ;
	} else if (action_type_set[DR_ACTION_TYP_L2_TO_TNL_L3]) {
		u8 *d_action;

		if (action_sz < DR_STE_ACTION_TRIPLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}
		d_action = action + DR_STE_ACTION_SINGLE_SZ;

		dr_ste_v1_set_encap_l3(last_ste,
				       action, d_action,
				       attr->reformat.id,
				       attr->reformat.size);
		action_sz -= DR_STE_ACTION_TRIPLE_SZ;
		action += DR_STE_ACTION_TRIPLE_SZ;
	} else if (action_type_set[DR_ACTION_TYP_INSERT_HDR]) {
		if (!allow_encap || action_sz < DR_STE_ACTION_DOUBLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}
		dr_ste_v1_set_insert_hdr(last_ste, action,
					 attr->reformat.id,
					 attr->reformat.param_0,
					 attr->reformat.param_1,
					 attr->reformat.size);
		action_sz -= DR_STE_ACTION_DOUBLE_SZ;
		action += DR_STE_ACTION_DOUBLE_SZ;
	} else if (action_type_set[DR_ACTION_TYP_REMOVE_HDR]) {
		if (action_sz < DR_STE_ACTION_SINGLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}
		dr_ste_v1_set_remove_hdr(last_ste, action,
					 attr->reformat.param_0,
					 attr->reformat.param_1,
					 attr->reformat.size);
		action_sz -= DR_STE_ACTION_SINGLE_SZ;
		action += DR_STE_ACTION_SINGLE_SZ;
	}

	dr_ste_v1_set_hit_gvmi(last_ste, attr->hit_gvmi);
	dr_ste_v1_set_hit_addr(last_ste, attr->final_icm_addr, 1);
}

static void dr_ste_v1_set_actions_rx(struct mlx5dr_domain *dmn,
				     u8 *action_type_set,
				     u8 *last_ste,
				     struct mlx5dr_ste_actions_attr *attr,
				     u32 *added_stes)
{
	u8 *action = MLX5_ADDR_OF(ste_match_bwc_v1, last_ste, action);
	u8 action_sz = DR_STE_ACTION_DOUBLE_SZ;
	bool allow_modify_hdr = true;
	bool allow_ctr = true;

	if (action_type_set[DR_ACTION_TYP_TNL_L3_TO_L2]) {
		dr_ste_v1_set_rewrite_actions(last_ste, action,
					      attr->decap_actions,
					      attr->decap_index);
		action_sz -= DR_STE_ACTION_DOUBLE_SZ;
		action += DR_STE_ACTION_DOUBLE_SZ;
		allow_modify_hdr = false;
		allow_ctr = false;
	} else if (action_type_set[DR_ACTION_TYP_TNL_L2_TO_L2]) {
		dr_ste_v1_set_rx_decap(last_ste, action);
		action_sz -= DR_STE_ACTION_SINGLE_SZ;
		action += DR_STE_ACTION_SINGLE_SZ;
		allow_modify_hdr = false;
		allow_ctr = false;
	}

	if (action_type_set[DR_ACTION_TYP_TAG]) {
		if (action_sz < DR_STE_ACTION_SINGLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
			allow_modify_hdr = true;
			allow_ctr = true;
		}
		dr_ste_v1_set_rx_flow_tag(action, attr->flow_tag);
		action_sz -= DR_STE_ACTION_SINGLE_SZ;
		action += DR_STE_ACTION_SINGLE_SZ;
	}

	if (action_type_set[DR_ACTION_TYP_POP_VLAN]) {
		if (action_sz < DR_STE_ACTION_SINGLE_SZ ||
		    !allow_modify_hdr) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
			allow_modify_hdr = false;
			allow_ctr = false;
		}

		dr_ste_v1_set_pop_vlan(last_ste, action, attr->vlans.count);
		action_sz -= DR_STE_ACTION_SINGLE_SZ;
		action += DR_STE_ACTION_SINGLE_SZ;
	}

	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		/* Modify header and decapsulation must use different STEs */
		if (!allow_modify_hdr || action_sz < DR_STE_ACTION_DOUBLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
			allow_modify_hdr = true;
			allow_ctr = true;
		}
		dr_ste_v1_set_rewrite_actions(last_ste, action,
					      attr->modify_actions,
					      attr->modify_index);
		action_sz -= DR_STE_ACTION_DOUBLE_SZ;
		action += DR_STE_ACTION_DOUBLE_SZ;
	}

	if (action_type_set[DR_ACTION_TYP_PUSH_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			if (action_sz < DR_STE_ACTION_DOUBLE_SZ ||
			    !allow_modify_hdr) {
				dr_ste_v1_arr_init_next_match(&last_ste,
							      added_stes,
							      attr->gvmi);
				action = MLX5_ADDR_OF(ste_mask_and_match_v1,
						      last_ste, action);
				action_sz = DR_STE_ACTION_TRIPLE_SZ;
			}
			dr_ste_v1_set_push_vlan(last_ste, action,
						attr->vlans.headers[i]);
			action_sz -= DR_STE_ACTION_DOUBLE_SZ;
			action += DR_STE_ACTION_DOUBLE_SZ;
		}
	}

	if (action_type_set[DR_ACTION_TYP_CTR]) {
		/* Counter action set after decap and before insert_hdr
		 * to exclude decaped / encaped header respectively.
		 */
		if (!allow_ctr) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
			allow_modify_hdr = true;
			allow_ctr = false;
		}
		dr_ste_v1_set_counter_id(last_ste, attr->ctr_id);
	}

	if (action_type_set[DR_ACTION_TYP_L2_TO_TNL_L2]) {
		if (action_sz < DR_STE_ACTION_DOUBLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}
		dr_ste_v1_set_encap(last_ste, action,
				    attr->reformat.id,
				    attr->reformat.size);
		action_sz -= DR_STE_ACTION_DOUBLE_SZ;
		action += DR_STE_ACTION_DOUBLE_SZ;
		allow_modify_hdr = false;
	} else if (action_type_set[DR_ACTION_TYP_L2_TO_TNL_L3]) {
		u8 *d_action;

		if (action_sz < DR_STE_ACTION_TRIPLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}

		d_action = action + DR_STE_ACTION_SINGLE_SZ;

		dr_ste_v1_set_encap_l3(last_ste,
				       action, d_action,
				       attr->reformat.id,
				       attr->reformat.size);
		action_sz -= DR_STE_ACTION_TRIPLE_SZ;
		allow_modify_hdr = false;
	} else if (action_type_set[DR_ACTION_TYP_INSERT_HDR]) {
		/* Modify header, decap, and encap must use different STEs */
		if (!allow_modify_hdr || action_sz < DR_STE_ACTION_DOUBLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
		}
		dr_ste_v1_set_insert_hdr(last_ste, action,
					 attr->reformat.id,
					 attr->reformat.param_0,
					 attr->reformat.param_1,
					 attr->reformat.size);
		action_sz -= DR_STE_ACTION_DOUBLE_SZ;
		action += DR_STE_ACTION_DOUBLE_SZ;
		allow_modify_hdr = false;
	} else if (action_type_set[DR_ACTION_TYP_REMOVE_HDR]) {
		if (action_sz < DR_STE_ACTION_SINGLE_SZ) {
			dr_ste_v1_arr_init_next_match(&last_ste, added_stes, attr->gvmi);
			action = MLX5_ADDR_OF(ste_mask_and_match_v1, last_ste, action);
			action_sz = DR_STE_ACTION_TRIPLE_SZ;
			allow_modify_hdr = true;
			allow_ctr = true;
		}
		dr_ste_v1_set_remove_hdr(last_ste, action,
					 attr->reformat.param_0,
					 attr->reformat.param_1,
					 attr->reformat.size);
		action_sz -= DR_STE_ACTION_SINGLE_SZ;
		action += DR_STE_ACTION_SINGLE_SZ;
	}

	dr_ste_v1_set_hit_gvmi(last_ste, attr->hit_gvmi);
	dr_ste_v1_set_hit_addr(last_ste, attr->final_icm_addr, 1);
}

static void dr_ste_v1_set_action_set(u8 *d_action,
				     u8 hw_field,
				     u8 shifter,
				     u8 length,
				     u32 data)
{
	shifter += MLX5_MODIFY_HEADER_V1_QW_OFFSET;
	MLX5_SET(ste_double_action_set_v1, d_action, action_id, DR_STE_V1_ACTION_ID_SET);
	MLX5_SET(ste_double_action_set_v1, d_action, destination_dw_offset, hw_field);
	MLX5_SET(ste_double_action_set_v1, d_action, destination_left_shifter, shifter);
	MLX5_SET(ste_double_action_set_v1, d_action, destination_length, length);
	MLX5_SET(ste_double_action_set_v1, d_action, inline_data, data);
}

static void dr_ste_v1_set_action_add(u8 *d_action,
				     u8 hw_field,
				     u8 shifter,
				     u8 length,
				     u32 data)
{
	shifter += MLX5_MODIFY_HEADER_V1_QW_OFFSET;
	MLX5_SET(ste_double_action_add_v1, d_action, action_id, DR_STE_V1_ACTION_ID_ADD);
	MLX5_SET(ste_double_action_add_v1, d_action, destination_dw_offset, hw_field);
	MLX5_SET(ste_double_action_add_v1, d_action, destination_left_shifter, shifter);
	MLX5_SET(ste_double_action_add_v1, d_action, destination_length, length);
	MLX5_SET(ste_double_action_add_v1, d_action, add_value, data);
}

static void dr_ste_v1_set_action_copy(u8 *d_action,
				      u8 dst_hw_field,
				      u8 dst_shifter,
				      u8 dst_len,
				      u8 src_hw_field,
				      u8 src_shifter)
{
	dst_shifter += MLX5_MODIFY_HEADER_V1_QW_OFFSET;
	src_shifter += MLX5_MODIFY_HEADER_V1_QW_OFFSET;
	MLX5_SET(ste_double_action_copy_v1, d_action, action_id, DR_STE_V1_ACTION_ID_COPY);
	MLX5_SET(ste_double_action_copy_v1, d_action, destination_dw_offset, dst_hw_field);
	MLX5_SET(ste_double_action_copy_v1, d_action, destination_left_shifter, dst_shifter);
	MLX5_SET(ste_double_action_copy_v1, d_action, destination_length, dst_len);
	MLX5_SET(ste_double_action_copy_v1, d_action, source_dw_offset, src_hw_field);
	MLX5_SET(ste_double_action_copy_v1, d_action, source_right_shifter, src_shifter);
}

#define DR_STE_DECAP_L3_ACTION_NUM	8
#define DR_STE_L2_HDR_MAX_SZ		20

static int dr_ste_v1_set_action_decap_l3_list(void *data,
					      u32 data_sz,
					      u8 *hw_action,
					      u32 hw_action_sz,
					      u16 *used_hw_action_num)
{
	u8 padded_data[DR_STE_L2_HDR_MAX_SZ] = {};
	void *data_ptr = padded_data;
	u16 used_actions = 0;
	u32 inline_data_sz;
	u32 i;

	if (hw_action_sz / DR_STE_ACTION_DOUBLE_SZ < DR_STE_DECAP_L3_ACTION_NUM)
		return -EINVAL;

	inline_data_sz =
		MLX5_FLD_SZ_BYTES(ste_double_action_insert_with_inline_v1, inline_data);

	/* Add an alignment padding  */
	memcpy(padded_data + data_sz % inline_data_sz, data, data_sz);

	/* Remove L2L3 outer headers */
	MLX5_SET(ste_single_action_remove_header_v1, hw_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	MLX5_SET(ste_single_action_remove_header_v1, hw_action, decap, 1);
	MLX5_SET(ste_single_action_remove_header_v1, hw_action, vni_to_cqe, 1);
	MLX5_SET(ste_single_action_remove_header_v1, hw_action, end_anchor,
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

		MLX5_SET(ste_double_action_insert_with_inline_v1, hw_action, action_id,
			 DR_STE_V1_ACTION_ID_INSERT_INLINE);
		/* The hardware expects here offset to words (2 bytes) */
		MLX5_SET(ste_double_action_insert_with_inline_v1, hw_action, start_offset, 0);

		/* Copy bytes one by one to avoid endianness problem */
		addr_inline = MLX5_ADDR_OF(ste_double_action_insert_with_inline_v1,
					   hw_action, inline_data);
		memcpy(addr_inline, data_ptr - i * inline_data_sz, inline_data_sz);
		hw_action += DR_STE_ACTION_DOUBLE_SZ;
		used_actions++;
	}

	/* Remove first 2 extra bytes */
	MLX5_SET(ste_single_action_remove_header_size_v1, hw_action, action_id,
		 DR_STE_V1_ACTION_ID_REMOVE_BY_SIZE);
	MLX5_SET(ste_single_action_remove_header_size_v1, hw_action, start_offset, 0);
	/* The hardware expects here size in words (2 bytes) */
	MLX5_SET(ste_single_action_remove_header_size_v1, hw_action, remove_size, 1);
	used_actions++;

	*used_hw_action_num = used_actions;

	return 0;
}

static void dr_ste_v1_build_eth_l2_src_dst_bit_mask(struct mlx5dr_match_param *value,
						    bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_dst_v1, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, bit_mask, dmac_15_0, mask, dmac_15_0);

	DR_STE_SET_TAG(eth_l2_src_dst_v1, bit_mask, smac_47_16, mask, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, bit_mask, smac_15_0, mask, smac_15_0);

	DR_STE_SET_TAG(eth_l2_src_dst_v1, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_ONES(eth_l2_src_dst_v1, bit_mask, l3_type, mask, ip_version);

	if (mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst_v1, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
	} else if (mask->svlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst_v1, bit_mask, first_vlan_qualifier, -1);
		mask->svlan_tag = 0;
	}
}

static int dr_ste_v1_build_eth_l2_src_dst_tag(struct mlx5dr_match_param *value,
					      struct mlx5dr_ste_build *sb,
					      u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, dmac_15_0, spec, dmac_15_0);

	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, smac_47_16, spec, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, smac_15_0, spec, smac_15_0);

	if (spec->ip_version == IP_VERSION_IPV4) {
		MLX5_SET(ste_eth_l2_src_dst_v1, tag, l3_type, STE_IPV4);
		spec->ip_version = 0;
	} else if (spec->ip_version == IP_VERSION_IPV6) {
		MLX5_SET(ste_eth_l2_src_dst_v1, tag, l3_type, STE_IPV6);
		spec->ip_version = 0;
	} else if (spec->ip_version) {
		return -EINVAL;
	}

	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, first_priority, spec, first_prio);

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst_v1, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_src_dst_v1, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}
	return 0;
}

static void dr_ste_v1_build_eth_l2_src_dst_init(struct mlx5dr_ste_build *sb,
						struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l2_src_dst_bit_mask(mask, sb->inner, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(ETHL2_SRC_DST, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l2_src_dst_tag;
}

static int dr_ste_v1_build_eth_l3_ipv6_dst_tag(struct mlx5dr_match_param *value,
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

static void dr_ste_v1_build_eth_l3_ipv6_dst_init(struct mlx5dr_ste_build *sb,
						 struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l3_ipv6_dst_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(IPV6_DES, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l3_ipv6_dst_tag;
}

static int dr_ste_v1_build_eth_l3_ipv6_src_tag(struct mlx5dr_match_param *value,
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

static void dr_ste_v1_build_eth_l3_ipv6_src_init(struct mlx5dr_ste_build *sb,
						 struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l3_ipv6_src_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(IPV6_SRC, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l3_ipv6_src_tag;
}

static int dr_ste_v1_build_eth_l3_ipv4_5_tuple_tag(struct mlx5dr_match_param *value,
						   struct mlx5dr_ste_build *sb,
						   u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, destination_address, spec, dst_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, source_address, spec, src_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, destination_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, destination_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, source_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, source_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, ecn, spec, ip_ecn);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple_v1, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

static void dr_ste_v1_build_eth_l3_ipv4_5_tuple_init(struct mlx5dr_ste_build *sb,
						     struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l3_ipv4_5_tuple_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(ETHL3_IPV4_5_TUPLE, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l3_ipv4_5_tuple_tag;
}

static void dr_ste_v1_build_eth_l2_src_or_dst_bit_mask(struct mlx5dr_match_param *value,
						       bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_TAG(eth_l2_src_v1, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_TAG(eth_l2_src_v1, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_v1, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_TAG(eth_l2_src_v1, bit_mask, ip_fragmented, mask, frag); // ?
	DR_STE_SET_TAG(eth_l2_src_v1, bit_mask, l3_ethertype, mask, ethertype); // ?
	DR_STE_SET_ONES(eth_l2_src_v1, bit_mask, l3_type, mask, ip_version);

	if (mask->svlan_tag || mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_v1, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}

	if (inner) {
		if (misc_mask->inner_second_cvlan_tag ||
		    misc_mask->inner_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src_v1, bit_mask, second_vlan_qualifier, -1);
			misc_mask->inner_second_cvlan_tag = 0;
			misc_mask->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src_v1, bit_mask,
			       second_vlan_id, misc_mask, inner_second_vid);
		DR_STE_SET_TAG(eth_l2_src_v1, bit_mask,
			       second_cfi, misc_mask, inner_second_cfi);
		DR_STE_SET_TAG(eth_l2_src_v1, bit_mask,
			       second_priority, misc_mask, inner_second_prio);
	} else {
		if (misc_mask->outer_second_cvlan_tag ||
		    misc_mask->outer_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src_v1, bit_mask, second_vlan_qualifier, -1);
			misc_mask->outer_second_cvlan_tag = 0;
			misc_mask->outer_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src_v1, bit_mask,
			       second_vlan_id, misc_mask, outer_second_vid);
		DR_STE_SET_TAG(eth_l2_src_v1, bit_mask,
			       second_cfi, misc_mask, outer_second_cfi);
		DR_STE_SET_TAG(eth_l2_src_v1, bit_mask,
			       second_priority, misc_mask, outer_second_prio);
	}
}

static int dr_ste_v1_build_eth_l2_src_or_dst_tag(struct mlx5dr_match_param *value,
						 bool inner, u8 *tag)
{
	struct mlx5dr_match_spec *spec = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc_spec = &value->misc;

	DR_STE_SET_TAG(eth_l2_src_v1, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, l3_ethertype, spec, ethertype);

	if (spec->ip_version == IP_VERSION_IPV4) {
		MLX5_SET(ste_eth_l2_src_v1, tag, l3_type, STE_IPV4);
		spec->ip_version = 0;
	} else if (spec->ip_version == IP_VERSION_IPV6) {
		MLX5_SET(ste_eth_l2_src_v1, tag, l3_type, STE_IPV6);
		spec->ip_version = 0;
	} else if (spec->ip_version) {
		return -EINVAL;
	}

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_src_v1, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_src_v1, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (inner) {
		if (misc_spec->inner_second_cvlan_tag) {
			MLX5_SET(ste_eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->inner_second_cvlan_tag = 0;
		} else if (misc_spec->inner_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_vlan_id, misc_spec, inner_second_vid);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_cfi, misc_spec, inner_second_cfi);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_priority, misc_spec, inner_second_prio);
	} else {
		if (misc_spec->outer_second_cvlan_tag) {
			MLX5_SET(ste_eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->outer_second_cvlan_tag = 0;
		} else if (misc_spec->outer_second_svlan_tag) {
			MLX5_SET(ste_eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->outer_second_svlan_tag = 0;
		}
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_vlan_id, misc_spec, outer_second_vid);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_cfi, misc_spec, outer_second_cfi);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_priority, misc_spec, outer_second_prio);
	}

	return 0;
}

static void dr_ste_v1_build_eth_l2_src_bit_mask(struct mlx5dr_match_param *value,
						bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_v1, bit_mask, smac_47_16, mask, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src_v1, bit_mask, smac_15_0, mask, smac_15_0);

	dr_ste_v1_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_v1_build_eth_l2_src_tag(struct mlx5dr_match_param *value,
					  struct mlx5dr_ste_build *sb,
					  u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_v1, tag, smac_47_16, spec, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, smac_15_0, spec, smac_15_0);

	return dr_ste_v1_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void dr_ste_v1_build_eth_l2_src_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l2_src_bit_mask(mask, sb->inner, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(ETHL2_SRC, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l2_src_tag;
}

static void dr_ste_v1_build_eth_l2_dst_bit_mask(struct mlx5dr_match_param *value,
						bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_dst_v1, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_dst_v1, bit_mask, dmac_15_0, mask, dmac_15_0);

	dr_ste_v1_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_v1_build_eth_l2_dst_tag(struct mlx5dr_match_param *value,
					  struct mlx5dr_ste_build *sb,
					  u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_dst_v1, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_dst_v1, tag, dmac_15_0, spec, dmac_15_0);

	return dr_ste_v1_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void dr_ste_v1_build_eth_l2_dst_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l2_dst_bit_mask(mask, sb->inner, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(ETHL2, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l2_dst_tag;
}

static void dr_ste_v1_build_eth_l2_tnl_bit_mask(struct mlx5dr_match_param *value,
						bool inner, u8 *bit_mask)
{
	struct mlx5dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(eth_l2_tnl_v1, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_tnl_v1, bit_mask, dmac_15_0, mask, dmac_15_0);
	DR_STE_SET_TAG(eth_l2_tnl_v1, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_TAG(eth_l2_tnl_v1, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_TAG(eth_l2_tnl_v1, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_TAG(eth_l2_tnl_v1, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_TAG(eth_l2_tnl_v1, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_ONES(eth_l2_tnl_v1, bit_mask, l3_type, mask, ip_version);

	if (misc->vxlan_vni) {
		MLX5_SET(ste_eth_l2_tnl_v1, bit_mask,
			 l2_tunneling_network_id, (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (mask->svlan_tag || mask->cvlan_tag) {
		MLX5_SET(ste_eth_l2_tnl_v1, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}
}

static int dr_ste_v1_build_eth_l2_tnl_tag(struct mlx5dr_match_param *value,
					  struct mlx5dr_ste_build *sb,
					  u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, dmac_15_0, spec, dmac_15_0);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, l3_ethertype, spec, ethertype);

	if (misc->vxlan_vni) {
		MLX5_SET(ste_eth_l2_tnl_v1, tag, l2_tunneling_network_id,
			 (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (spec->cvlan_tag) {
		MLX5_SET(ste_eth_l2_tnl_v1, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		MLX5_SET(ste_eth_l2_tnl_v1, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (spec->ip_version == IP_VERSION_IPV4) {
		MLX5_SET(ste_eth_l2_tnl_v1, tag, l3_type, STE_IPV4);
		spec->ip_version = 0;
	} else if (spec->ip_version == IP_VERSION_IPV6) {
		MLX5_SET(ste_eth_l2_tnl_v1, tag, l3_type, STE_IPV6);
		spec->ip_version = 0;
	} else if (spec->ip_version) {
		return -EINVAL;
	}

	return 0;
}

static void dr_ste_v1_build_eth_l2_tnl_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l2_tnl_bit_mask(mask, sb->inner, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_ETHL2_TNL;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l2_tnl_tag;
}

static int dr_ste_v1_build_eth_l3_ipv4_misc_tag(struct mlx5dr_match_param *value,
						struct mlx5dr_ste_build *sb,
						u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_misc_v1, tag, time_to_live, spec, ttl_hoplimit);

	return 0;
}

static void dr_ste_v1_build_eth_l3_ipv4_misc_init(struct mlx5dr_ste_build *sb,
						  struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l3_ipv4_misc_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(ETHL3_IPV4_MISC, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l3_ipv4_misc_tag;
}

static int dr_ste_v1_build_eth_ipv6_l3_l4_tag(struct mlx5dr_match_param *value,
					      struct mlx5dr_ste_build *sb,
					      u8 *tag)
{
	struct mlx5dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(eth_l4_v1, tag, dst_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l4_v1, tag, src_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l4_v1, tag, dst_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l4_v1, tag, src_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l4_v1, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l4_v1, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l4_v1, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l4_v1, tag, ecn, spec, ip_ecn);
	DR_STE_SET_TAG(eth_l4_v1, tag, ipv6_hop_limit, spec, ttl_hoplimit);

	if (sb->inner)
		DR_STE_SET_TAG(eth_l4_v1, tag, flow_label, misc, inner_ipv6_flow_label);
	else
		DR_STE_SET_TAG(eth_l4_v1, tag, flow_label, misc, outer_ipv6_flow_label);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4_v1, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

static void dr_ste_v1_build_eth_ipv6_l3_l4_init(struct mlx5dr_ste_build *sb,
						struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_ipv6_l3_l4_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(ETHL4, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_ipv6_l3_l4_tag;
}

static int dr_ste_v1_build_mpls_tag(struct mlx5dr_match_param *value,
				    struct mlx5dr_ste_build *sb,
				    u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;

	if (sb->inner)
		DR_STE_SET_MPLS(mpls_v1, misc2, inner, tag);
	else
		DR_STE_SET_MPLS(mpls_v1, misc2, outer, tag);

	return 0;
}

static void dr_ste_v1_build_mpls_init(struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_mpls_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_CALC_DFNR_TYPE(MPLS, sb->inner);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_mpls_tag;
}

static int dr_ste_v1_build_tnl_gre_tag(struct mlx5dr_match_param *value,
				       struct mlx5dr_ste_build *sb,
				       u8 *tag)
{
	struct  mlx5dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(gre_v1, tag, gre_protocol, misc, gre_protocol);
	DR_STE_SET_TAG(gre_v1, tag, gre_k_present, misc, gre_k_present);
	DR_STE_SET_TAG(gre_v1, tag, gre_key_h, misc, gre_key_h);
	DR_STE_SET_TAG(gre_v1, tag, gre_key_l, misc, gre_key_l);

	DR_STE_SET_TAG(gre_v1, tag, gre_c_present, misc, gre_c_present);
	DR_STE_SET_TAG(gre_v1, tag, gre_s_present, misc, gre_s_present);

	return 0;
}

static void dr_ste_v1_build_tnl_gre_init(struct mlx5dr_ste_build *sb,
					 struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_tnl_gre_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_GRE;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_tnl_gre_tag;
}

static int dr_ste_v1_build_tnl_mpls_tag(struct mlx5dr_match_param *value,
					struct mlx5dr_ste_build *sb,
					u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;

	if (DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(misc2)) {
		DR_STE_SET_TAG(mpls_v1, tag, mpls0_label,
			       misc2, outer_first_mpls_over_gre_label);

		DR_STE_SET_TAG(mpls_v1, tag, mpls0_exp,
			       misc2, outer_first_mpls_over_gre_exp);

		DR_STE_SET_TAG(mpls_v1, tag, mpls0_s_bos,
			       misc2, outer_first_mpls_over_gre_s_bos);

		DR_STE_SET_TAG(mpls_v1, tag, mpls0_ttl,
			       misc2, outer_first_mpls_over_gre_ttl);
	} else {
		DR_STE_SET_TAG(mpls_v1, tag, mpls0_label,
			       misc2, outer_first_mpls_over_udp_label);

		DR_STE_SET_TAG(mpls_v1, tag, mpls0_exp,
			       misc2, outer_first_mpls_over_udp_exp);

		DR_STE_SET_TAG(mpls_v1, tag, mpls0_s_bos,
			       misc2, outer_first_mpls_over_udp_s_bos);

		DR_STE_SET_TAG(mpls_v1, tag, mpls0_ttl,
			       misc2, outer_first_mpls_over_udp_ttl);
	}

	return 0;
}

static void dr_ste_v1_build_tnl_mpls_init(struct mlx5dr_ste_build *sb,
					  struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_tnl_mpls_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_MPLS_I;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_tnl_mpls_tag;
}

static int dr_ste_v1_build_tnl_mpls_over_udp_tag(struct mlx5dr_match_param *value,
						 struct mlx5dr_ste_build *sb,
						 u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;
	u8 *parser_ptr;
	u8 parser_id;
	u32 mpls_hdr;

	mpls_hdr = misc2->outer_first_mpls_over_udp_label << HDR_MPLS_OFFSET_LABEL;
	misc2->outer_first_mpls_over_udp_label = 0;
	mpls_hdr |= misc2->outer_first_mpls_over_udp_exp << HDR_MPLS_OFFSET_EXP;
	misc2->outer_first_mpls_over_udp_exp = 0;
	mpls_hdr |= misc2->outer_first_mpls_over_udp_s_bos << HDR_MPLS_OFFSET_S_BOS;
	misc2->outer_first_mpls_over_udp_s_bos = 0;
	mpls_hdr |= misc2->outer_first_mpls_over_udp_ttl << HDR_MPLS_OFFSET_TTL;
	misc2->outer_first_mpls_over_udp_ttl = 0;

	parser_id = sb->caps->flex_parser_id_mpls_over_udp;
	parser_ptr = dr_ste_calc_flex_parser_offset(tag, parser_id);
	*(__be32 *)parser_ptr = cpu_to_be32(mpls_hdr);

	return 0;
}

static void dr_ste_v1_build_tnl_mpls_over_udp_init(struct mlx5dr_ste_build *sb,
						   struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_tnl_mpls_over_udp_tag(mask, sb, sb->bit_mask);

	/* STEs with lookup type FLEX_PARSER_{0/1} includes
	 * flex parsers_{0-3}/{4-7} respectively.
	 */
	sb->lu_type = sb->caps->flex_parser_id_mpls_over_udp > DR_STE_MAX_FLEX_0_ID ?
		      DR_STE_V1_LU_TYPE_FLEX_PARSER_1 :
		      DR_STE_V1_LU_TYPE_FLEX_PARSER_0;

	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_tnl_mpls_over_udp_tag;
}

static int dr_ste_v1_build_tnl_mpls_over_gre_tag(struct mlx5dr_match_param *value,
						 struct mlx5dr_ste_build *sb,
						 u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;
	u8 *parser_ptr;
	u8 parser_id;
	u32 mpls_hdr;

	mpls_hdr = misc2->outer_first_mpls_over_gre_label << HDR_MPLS_OFFSET_LABEL;
	misc2->outer_first_mpls_over_gre_label = 0;
	mpls_hdr |= misc2->outer_first_mpls_over_gre_exp << HDR_MPLS_OFFSET_EXP;
	misc2->outer_first_mpls_over_gre_exp = 0;
	mpls_hdr |= misc2->outer_first_mpls_over_gre_s_bos << HDR_MPLS_OFFSET_S_BOS;
	misc2->outer_first_mpls_over_gre_s_bos = 0;
	mpls_hdr |= misc2->outer_first_mpls_over_gre_ttl << HDR_MPLS_OFFSET_TTL;
	misc2->outer_first_mpls_over_gre_ttl = 0;

	parser_id = sb->caps->flex_parser_id_mpls_over_gre;
	parser_ptr = dr_ste_calc_flex_parser_offset(tag, parser_id);
	*(__be32 *)parser_ptr = cpu_to_be32(mpls_hdr);

	return 0;
}

static void dr_ste_v1_build_tnl_mpls_over_gre_init(struct mlx5dr_ste_build *sb,
						   struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_tnl_mpls_over_gre_tag(mask, sb, sb->bit_mask);

	/* STEs with lookup type FLEX_PARSER_{0/1} includes
	 * flex parsers_{0-3}/{4-7} respectively.
	 */
	sb->lu_type = sb->caps->flex_parser_id_mpls_over_gre > DR_STE_MAX_FLEX_0_ID ?
		      DR_STE_V1_LU_TYPE_FLEX_PARSER_1 :
		      DR_STE_V1_LU_TYPE_FLEX_PARSER_0;

	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_tnl_mpls_over_gre_tag;
}

static int dr_ste_v1_build_icmp_tag(struct mlx5dr_match_param *value,
				    struct mlx5dr_ste_build *sb,
				    u8 *tag)
{
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;
	bool is_ipv4 = DR_MASK_IS_ICMPV4_SET(misc3);
	u32 *icmp_header_data;
	u8 *icmp_type;
	u8 *icmp_code;

	if (is_ipv4) {
		icmp_header_data	= &misc3->icmpv4_header_data;
		icmp_type		= &misc3->icmpv4_type;
		icmp_code		= &misc3->icmpv4_code;
	} else {
		icmp_header_data	= &misc3->icmpv6_header_data;
		icmp_type		= &misc3->icmpv6_type;
		icmp_code		= &misc3->icmpv6_code;
	}

	MLX5_SET(ste_icmp_v1, tag, icmp_header_data, *icmp_header_data);
	MLX5_SET(ste_icmp_v1, tag, icmp_type, *icmp_type);
	MLX5_SET(ste_icmp_v1, tag, icmp_code, *icmp_code);

	*icmp_header_data = 0;
	*icmp_type = 0;
	*icmp_code = 0;

	return 0;
}

static void dr_ste_v1_build_icmp_init(struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_icmp_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_ETHL4_MISC_O;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_icmp_tag;
}

static int dr_ste_v1_build_general_purpose_tag(struct mlx5dr_match_param *value,
					       struct mlx5dr_ste_build *sb,
					       u8 *tag)
{
	struct mlx5dr_match_misc2 *misc2 = &value->misc2;

	DR_STE_SET_TAG(general_purpose, tag, general_purpose_lookup_field,
		       misc2, metadata_reg_a);

	return 0;
}

static void dr_ste_v1_build_general_purpose_init(struct mlx5dr_ste_build *sb,
						 struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_general_purpose_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_GENERAL_PURPOSE;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_general_purpose_tag;
}

static int dr_ste_v1_build_eth_l4_misc_tag(struct mlx5dr_match_param *value,
					   struct mlx5dr_ste_build *sb,
					   u8 *tag)
{
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;

	if (sb->inner) {
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, seq_num, misc3, inner_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, ack_num, misc3, inner_tcp_ack_num);
	} else {
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, seq_num, misc3, outer_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, ack_num, misc3, outer_tcp_ack_num);
	}

	return 0;
}

static void dr_ste_v1_build_eth_l4_misc_init(struct mlx5dr_ste_build *sb,
					     struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_eth_l4_misc_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_ETHL4_MISC_O;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_eth_l4_misc_tag;
}

static int
dr_ste_v1_build_flex_parser_tnl_vxlan_gpe_tag(struct mlx5dr_match_param *value,
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
dr_ste_v1_build_flex_parser_tnl_vxlan_gpe_init(struct mlx5dr_ste_build *sb,
					       struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_flex_parser_tnl_vxlan_gpe_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_FLEX_PARSER_TNL_HEADER;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_flex_parser_tnl_vxlan_gpe_tag;
}

static int
dr_ste_v1_build_flex_parser_tnl_geneve_tag(struct mlx5dr_match_param *value,
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
dr_ste_v1_build_flex_parser_tnl_geneve_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_flex_parser_tnl_geneve_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_FLEX_PARSER_TNL_HEADER;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_flex_parser_tnl_geneve_tag;
}

static int dr_ste_v1_build_register_0_tag(struct mlx5dr_match_param *value,
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

static void dr_ste_v1_build_register_0_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_register_0_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_STEERING_REGISTERS_0;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_register_0_tag;
}

static int dr_ste_v1_build_register_1_tag(struct mlx5dr_match_param *value,
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

static void dr_ste_v1_build_register_1_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_register_1_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_STEERING_REGISTERS_1;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_register_1_tag;
}

static void dr_ste_v1_build_src_gvmi_qpn_bit_mask(struct mlx5dr_match_param *value,
						  u8 *bit_mask)
{
	struct mlx5dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_ONES(src_gvmi_qp_v1, bit_mask, source_gvmi, misc_mask, source_port);
	DR_STE_SET_ONES(src_gvmi_qp_v1, bit_mask, source_qp, misc_mask, source_sqn);
	misc_mask->source_eswitch_owner_vhca_id = 0;
}

static int dr_ste_v1_build_src_gvmi_qpn_tag(struct mlx5dr_match_param *value,
					    struct mlx5dr_ste_build *sb,
					    u8 *tag)
{
	struct mlx5dr_match_misc *misc = &value->misc;
	struct mlx5dr_cmd_vport_cap *vport_cap;
	struct mlx5dr_domain *dmn = sb->dmn;
	struct mlx5dr_domain *vport_dmn;
	u8 *bit_mask = sb->bit_mask;

	DR_STE_SET_TAG(src_gvmi_qp_v1, tag, source_qp, misc, source_sqn);

	if (sb->vhca_id_valid) {
		/* Find port GVMI based on the eswitch_owner_vhca_id */
		if (misc->source_eswitch_owner_vhca_id == dmn->info.caps.gvmi)
			vport_dmn = dmn;
		else if (dmn->peer_dmn && (misc->source_eswitch_owner_vhca_id ==
					   dmn->peer_dmn->info.caps.gvmi))
			vport_dmn = dmn->peer_dmn;
		else
			return -EINVAL;

		misc->source_eswitch_owner_vhca_id = 0;
	} else {
		vport_dmn = dmn;
	}

	if (!MLX5_GET(ste_src_gvmi_qp_v1, bit_mask, source_gvmi))
		return 0;

	vport_cap = mlx5dr_domain_get_vport_cap(vport_dmn, misc->source_port);
	if (!vport_cap) {
		mlx5dr_err(dmn, "Vport 0x%x is disabled or invalid\n",
			   misc->source_port);
		return -EINVAL;
	}

	if (vport_cap->vport_gvmi)
		MLX5_SET(ste_src_gvmi_qp_v1, tag, source_gvmi, vport_cap->vport_gvmi);

	misc->source_port = 0;
	return 0;
}

static void dr_ste_v1_build_src_gvmi_qpn_init(struct mlx5dr_ste_build *sb,
					      struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_src_gvmi_qpn_bit_mask(mask, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_SRC_QP_GVMI;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_src_gvmi_qpn_tag;
}

static void dr_ste_v1_set_flex_parser(u32 *misc4_field_id,
				      u32 *misc4_field_value,
				      bool *parser_is_used,
				      u8 *tag)
{
	u32 id = *misc4_field_id;
	u8 *parser_ptr;

	if (id >= DR_NUM_OF_FLEX_PARSERS || parser_is_used[id])
		return;

	parser_is_used[id] = true;
	parser_ptr = dr_ste_calc_flex_parser_offset(tag, id);

	*(__be32 *)parser_ptr = cpu_to_be32(*misc4_field_value);
	*misc4_field_id = 0;
	*misc4_field_value = 0;
}

static int dr_ste_v1_build_felx_parser_tag(struct mlx5dr_match_param *value,
					   struct mlx5dr_ste_build *sb,
					   u8 *tag)
{
	struct mlx5dr_match_misc4 *misc_4_mask = &value->misc4;
	bool parser_is_used[DR_NUM_OF_FLEX_PARSERS] = {};

	dr_ste_v1_set_flex_parser(&misc_4_mask->prog_sample_field_id_0,
				  &misc_4_mask->prog_sample_field_value_0,
				  parser_is_used, tag);

	dr_ste_v1_set_flex_parser(&misc_4_mask->prog_sample_field_id_1,
				  &misc_4_mask->prog_sample_field_value_1,
				  parser_is_used, tag);

	dr_ste_v1_set_flex_parser(&misc_4_mask->prog_sample_field_id_2,
				  &misc_4_mask->prog_sample_field_value_2,
				  parser_is_used, tag);

	dr_ste_v1_set_flex_parser(&misc_4_mask->prog_sample_field_id_3,
				  &misc_4_mask->prog_sample_field_value_3,
				  parser_is_used, tag);

	return 0;
}

static void dr_ste_v1_build_flex_parser_0_init(struct mlx5dr_ste_build *sb,
					       struct mlx5dr_match_param *mask)
{
	sb->lu_type = DR_STE_V1_LU_TYPE_FLEX_PARSER_0;
	dr_ste_v1_build_felx_parser_tag(mask, sb, sb->bit_mask);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_felx_parser_tag;
}

static void dr_ste_v1_build_flex_parser_1_init(struct mlx5dr_ste_build *sb,
					       struct mlx5dr_match_param *mask)
{
	sb->lu_type = DR_STE_V1_LU_TYPE_FLEX_PARSER_1;
	dr_ste_v1_build_felx_parser_tag(mask, sb, sb->bit_mask);
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_felx_parser_tag;
}

static int
dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_tag(struct mlx5dr_match_param *value,
						   struct mlx5dr_ste_build *sb,
						   u8 *tag)
{
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;
	u8 parser_id = sb->caps->flex_parser_id_geneve_tlv_option_0;
	u8 *parser_ptr = dr_ste_calc_flex_parser_offset(tag, parser_id);

	MLX5_SET(ste_flex_parser_0, parser_ptr, flex_parser_3,
		 misc3->geneve_tlv_option_0_data);
	misc3->geneve_tlv_option_0_data = 0;

	return 0;
}

static void
dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_init(struct mlx5dr_ste_build *sb,
						    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_tag(mask, sb, sb->bit_mask);

	/* STEs with lookup type FLEX_PARSER_{0/1} includes
	 * flex parsers_{0-3}/{4-7} respectively.
	 */
	sb->lu_type = sb->caps->flex_parser_id_geneve_tlv_option_0 > 3 ?
		      DR_STE_V1_LU_TYPE_FLEX_PARSER_1 :
		      DR_STE_V1_LU_TYPE_FLEX_PARSER_0;

	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_tag;
}

static int dr_ste_v1_build_flex_parser_tnl_gtpu_tag(struct mlx5dr_match_param *value,
						    struct mlx5dr_ste_build *sb,
						    u8 *tag)
{
	struct mlx5dr_match_misc3 *misc3 = &value->misc3;

	DR_STE_SET_TAG(flex_parser_tnl_gtpu, tag, gtpu_msg_flags, misc3, gtpu_msg_flags);
	DR_STE_SET_TAG(flex_parser_tnl_gtpu, tag, gtpu_msg_type, misc3, gtpu_msg_type);
	DR_STE_SET_TAG(flex_parser_tnl_gtpu, tag, gtpu_teid, misc3, gtpu_teid);

	return 0;
}

static void dr_ste_v1_build_flex_parser_tnl_gtpu_init(struct mlx5dr_ste_build *sb,
						      struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_flex_parser_tnl_gtpu_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_FLEX_PARSER_TNL_HEADER;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_flex_parser_tnl_gtpu_tag;
}

static int
dr_ste_v1_build_tnl_gtpu_flex_parser_0_tag(struct mlx5dr_match_param *value,
					   struct mlx5dr_ste_build *sb,
					   u8 *tag)
{
	if (dr_is_flex_parser_0_id(sb->caps->flex_parser_id_gtpu_dw_0))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_dw_0, sb->caps, &value->misc3);
	if (dr_is_flex_parser_0_id(sb->caps->flex_parser_id_gtpu_teid))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_teid, sb->caps, &value->misc3);
	if (dr_is_flex_parser_0_id(sb->caps->flex_parser_id_gtpu_dw_2))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_dw_2, sb->caps, &value->misc3);
	if (dr_is_flex_parser_0_id(sb->caps->flex_parser_id_gtpu_first_ext_dw_0))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_first_ext_dw_0, sb->caps, &value->misc3);
	return 0;
}

static void
dr_ste_v1_build_tnl_gtpu_flex_parser_0_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_tnl_gtpu_flex_parser_0_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_FLEX_PARSER_0;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_tnl_gtpu_flex_parser_0_tag;
}

static int
dr_ste_v1_build_tnl_gtpu_flex_parser_1_tag(struct mlx5dr_match_param *value,
					   struct mlx5dr_ste_build *sb,
					   u8 *tag)
{
	if (dr_is_flex_parser_1_id(sb->caps->flex_parser_id_gtpu_dw_0))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_dw_0, sb->caps, &value->misc3);
	if (dr_is_flex_parser_1_id(sb->caps->flex_parser_id_gtpu_teid))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_teid, sb->caps, &value->misc3);
	if (dr_is_flex_parser_1_id(sb->caps->flex_parser_id_gtpu_dw_2))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_dw_2, sb->caps, &value->misc3);
	if (dr_is_flex_parser_1_id(sb->caps->flex_parser_id_gtpu_first_ext_dw_0))
		DR_STE_SET_FLEX_PARSER_FIELD(tag, gtpu_first_ext_dw_0, sb->caps, &value->misc3);
	return 0;
}

static void
dr_ste_v1_build_tnl_gtpu_flex_parser_1_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask)
{
	dr_ste_v1_build_tnl_gtpu_flex_parser_1_tag(mask, sb, sb->bit_mask);

	sb->lu_type = DR_STE_V1_LU_TYPE_FLEX_PARSER_1;
	sb->byte_mask = mlx5dr_ste_conv_bit_to_byte_mask(sb->bit_mask);
	sb->ste_build_tag_func = &dr_ste_v1_build_tnl_gtpu_flex_parser_1_tag;
}

struct mlx5dr_ste_ctx ste_ctx_v1 = {
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
	.build_register_0_init		= &dr_ste_v1_build_register_0_init,
	.build_register_1_init		= &dr_ste_v1_build_register_1_init,
	.build_src_gvmi_qpn_init	= &dr_ste_v1_build_src_gvmi_qpn_init,
	.build_flex_parser_0_init	= &dr_ste_v1_build_flex_parser_0_init,
	.build_flex_parser_1_init	= &dr_ste_v1_build_flex_parser_1_init,
	.build_tnl_gtpu_init		= &dr_ste_v1_build_flex_parser_tnl_gtpu_init,
	.build_tnl_gtpu_flex_parser_0_init = &dr_ste_v1_build_tnl_gtpu_flex_parser_0_init,
	.build_tnl_gtpu_flex_parser_1_init = &dr_ste_v1_build_tnl_gtpu_flex_parser_1_init,

	/* Getters and Setters */
	.ste_init			= &dr_ste_v1_init,
	.set_next_lu_type		= &dr_ste_v1_set_next_lu_type,
	.get_next_lu_type		= &dr_ste_v1_get_next_lu_type,
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
	.modify_field_arr_sz		= ARRAY_SIZE(dr_ste_v1_action_modify_field_arr),
	.modify_field_arr		= dr_ste_v1_action_modify_field_arr,
	.set_action_set			= &dr_ste_v1_set_action_set,
	.set_action_add			= &dr_ste_v1_set_action_add,
	.set_action_copy		= &dr_ste_v1_set_action_copy,
	.set_action_decap_l3_list	= &dr_ste_v1_set_action_decap_l3_list,
	/* Send */
	.prepare_for_postsend		= &dr_ste_v1_prepare_for_postsend,
};
