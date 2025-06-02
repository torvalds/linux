/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef	_DR_STE_V1_
#define	_DR_STE_V1_

#include "dr_types.h"
#include "dr_ste.h"

#define DR_STE_DECAP_L3_ACTION_NUM	8
#define DR_STE_L2_HDR_MAX_SZ		20
#define DR_STE_CALC_DFNR_TYPE(lookup_type, inner) \
	((inner) ? DR_STE_V1_LU_TYPE_##lookup_type##_I : \
		   DR_STE_V1_LU_TYPE_##lookup_type##_O)

enum dr_ste_v1_entry_format {
	DR_STE_V1_TYPE_BWC_BYTE	= 0x0,
	DR_STE_V1_TYPE_BWC_DW	= 0x1,
	DR_STE_V1_TYPE_MATCH	= 0x2,
	DR_STE_V1_TYPE_MATCH_RANGES = 0x7,
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
	DR_STE_V1_LU_TYPE_FLEX_PARSER_OK		= 0x0011,
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
	DR_STE_V1_ACTION_ID_ASO				= 0x12,
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
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_2_0		= 0x8c,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_2_1		= 0x8d,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_1_0		= 0x8e,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_1_1		= 0x8f,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_0_0		= 0x90,
	DR_STE_V1_ACTION_MDFY_FLD_REGISTER_0_1		= 0x91,
};

enum dr_ste_v1_aso_ctx_type {
	DR_STE_V1_ASO_CTX_TYPE_POLICERS = 0x2,
};

bool dr_ste_v1_is_miss_addr_set(u8 *hw_ste_p);
void dr_ste_v1_set_miss_addr(u8 *hw_ste_p, u64 miss_addr);
u64 dr_ste_v1_get_miss_addr(u8 *hw_ste_p);
void dr_ste_v1_set_byte_mask(u8 *hw_ste_p, u16 byte_mask);
u16 dr_ste_v1_get_byte_mask(u8 *hw_ste_p);
void dr_ste_v1_set_next_lu_type(u8 *hw_ste_p, u16 lu_type);
u16 dr_ste_v1_get_next_lu_type(u8 *hw_ste_p);
void dr_ste_v1_set_hit_addr(u8 *hw_ste_p, u64 icm_addr, u32 ht_size);
void dr_ste_v1_init(u8 *hw_ste_p, u16 lu_type, bool is_rx, u16 gvmi);
void dr_ste_v1_prepare_for_postsend(u8 *hw_ste_p, u32 ste_size);
void dr_ste_v1_set_reparse(u8 *hw_ste_p);
void dr_ste_v1_set_encap(u8 *hw_ste_p, u8 *d_action, u32 reformat_id, int size);
void dr_ste_v1_set_push_vlan(u8 *hw_ste_p, u8 *d_action, u32 vlan_hdr);
void dr_ste_v1_set_pop_vlan(u8 *hw_ste_p, u8 *s_action, u8 vlans_num);
void dr_ste_v1_set_encap_l3(u8 *hw_ste_p, u8 *frst_s_action, u8 *scnd_d_action,
			    u32 reformat_id, int size);
void dr_ste_v1_set_rx_decap(u8 *hw_ste_p, u8 *s_action);
void dr_ste_v1_set_insert_hdr(u8 *hw_ste_p, u8 *d_action, u32 reformat_id,
			      u8 anchor, u8 offset, int size);
void dr_ste_v1_set_remove_hdr(u8 *hw_ste_p, u8 *s_action, u8 anchor,
			      u8 offset, int size);
void dr_ste_v1_set_actions_tx(struct mlx5dr_ste_ctx *ste_ctx, struct mlx5dr_domain *dmn,
			      u8 *action_type_set, u32 actions_caps, u8 *last_ste,
			      struct mlx5dr_ste_actions_attr *attr, u32 *added_stes);
void dr_ste_v1_set_actions_rx(struct mlx5dr_ste_ctx *ste_ctx, struct mlx5dr_domain *dmn,
			      u8 *action_type_set, u32 actions_caps, u8 *last_ste,
			      struct mlx5dr_ste_actions_attr *attr, u32 *added_stes);
void dr_ste_v1_set_action_set(u8 *d_action, u8 hw_field, u8 shifter,
			      u8 length, u32 data);
void dr_ste_v1_set_action_add(u8 *d_action, u8 hw_field, u8 shifter,
			      u8 length, u32 data);
void dr_ste_v1_set_action_copy(u8 *d_action, u8 dst_hw_field, u8 dst_shifter,
			       u8 dst_len, u8 src_hw_field, u8 src_shifter);
int dr_ste_v1_set_action_decap_l3_list(void *data, u32 data_sz, u8 *hw_action,
				       u32 hw_action_sz, u16 *used_hw_action_num);
int dr_ste_v1_alloc_modify_hdr_ptrn_arg(struct mlx5dr_action *action);
void dr_ste_v1_free_modify_hdr_ptrn_arg(struct mlx5dr_action *action);
void dr_ste_v1_build_eth_l2_src_dst_init(struct mlx5dr_ste_build *sb,
					 struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l3_ipv6_dst_init(struct mlx5dr_ste_build *sb,
					  struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l3_ipv6_src_init(struct mlx5dr_ste_build *sb,
					  struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l3_ipv4_5_tuple_init(struct mlx5dr_ste_build *sb,
					      struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l2_src_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l2_dst_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l2_tnl_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l3_ipv4_misc_init(struct mlx5dr_ste_build *sb,
					   struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_ipv6_l3_l4_init(struct mlx5dr_ste_build *sb,
					 struct mlx5dr_match_param *mask);
void dr_ste_v1_build_mpls_init(struct mlx5dr_ste_build *sb,
			       struct mlx5dr_match_param *mask);
void dr_ste_v1_build_tnl_gre_init(struct mlx5dr_ste_build *sb,
				  struct mlx5dr_match_param *mask);
void dr_ste_v1_build_tnl_mpls_init(struct mlx5dr_ste_build *sb,
				   struct mlx5dr_match_param *mask);
void dr_ste_v1_build_tnl_mpls_over_udp_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask);
void dr_ste_v1_build_tnl_mpls_over_gre_init(struct mlx5dr_ste_build *sb,
					    struct mlx5dr_match_param *mask);
void dr_ste_v1_build_icmp_init(struct mlx5dr_ste_build *sb,
			       struct mlx5dr_match_param *mask);
void dr_ste_v1_build_general_purpose_init(struct mlx5dr_ste_build *sb,
					  struct mlx5dr_match_param *mask);
void dr_ste_v1_build_eth_l4_misc_init(struct mlx5dr_ste_build *sb,
				      struct mlx5dr_match_param *mask);
void dr_ste_v1_build_flex_parser_tnl_vxlan_gpe_init(struct mlx5dr_ste_build *sb,
						    struct mlx5dr_match_param *mask);
void dr_ste_v1_build_flex_parser_tnl_geneve_init(struct mlx5dr_ste_build *sb,
						 struct mlx5dr_match_param *mask);
void dr_ste_v1_build_tnl_header_0_1_init(struct mlx5dr_ste_build *sb,
					 struct mlx5dr_match_param *mask);
void dr_ste_v1_build_register_0_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask);
void dr_ste_v1_build_register_1_init(struct mlx5dr_ste_build *sb,
				     struct mlx5dr_match_param *mask);
void dr_ste_v1_build_src_gvmi_qpn_init(struct mlx5dr_ste_build *sb,
				       struct mlx5dr_match_param *mask);
void dr_ste_v1_build_flex_parser_0_init(struct mlx5dr_ste_build *sb,
					struct mlx5dr_match_param *mask);
void dr_ste_v1_build_flex_parser_1_init(struct mlx5dr_ste_build *sb,
					struct mlx5dr_match_param *mask);
void dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_init(struct mlx5dr_ste_build *sb,
							 struct mlx5dr_match_param *mask);
void dr_ste_v1_build_flex_parser_tnl_geneve_tlv_opt_exist_init(struct mlx5dr_ste_build *sb,
							       struct mlx5dr_match_param *mask);
void dr_ste_v1_build_flex_parser_tnl_gtpu_init(struct mlx5dr_ste_build *sb,
					       struct mlx5dr_match_param *mask);
void dr_ste_v1_build_tnl_gtpu_flex_parser_0_init(struct mlx5dr_ste_build *sb,
						 struct mlx5dr_match_param *mask);
void dr_ste_v1_build_tnl_gtpu_flex_parser_1_init(struct mlx5dr_ste_build *sb,
						 struct mlx5dr_match_param *mask);

#endif  /* _DR_STE_V1_ */
