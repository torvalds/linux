/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef	_DR_STE_V1_
#define	_DR_STE_V1_

#include "dr_types.h"
#include "dr_ste.h"

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
void dr_ste_v1_set_actions_tx(struct mlx5dr_domain *dmn, u8 *action_type_set,
			      u32 actions_caps, u8 *last_ste,
			      struct mlx5dr_ste_actions_attr *attr, u32 *added_stes);
void dr_ste_v1_set_actions_rx(struct mlx5dr_domain *dmn, u8 *action_type_set,
			      u32 actions_caps, u8 *last_ste,
			      struct mlx5dr_ste_actions_attr *attr, u32 *added_stes);
void dr_ste_v1_set_action_set(u8 *d_action, u8 hw_field, u8 shifter,
			      u8 length, u32 data);
void dr_ste_v1_set_action_add(u8 *d_action, u8 hw_field, u8 shifter,
			      u8 length, u32 data);
void dr_ste_v1_set_action_copy(u8 *d_action, u8 dst_hw_field, u8 dst_shifter,
			       u8 dst_len, u8 src_hw_field, u8 src_shifter);
int dr_ste_v1_set_action_decap_l3_list(void *data, u32 data_sz, u8 *hw_action,
				       u32 hw_action_sz, u16 *used_hw_action_num);
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
