/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 NVIDIA CORPORATION. All rights reserved. */

#ifndef	_DR_STE_
#define	_DR_STE_

#include "dr_types.h"

#define STE_IPV4 0x1
#define STE_IPV6 0x2
#define STE_TCP 0x1
#define STE_UDP 0x2
#define STE_SPI 0x3
#define IP_VERSION_IPV4 0x4
#define IP_VERSION_IPV6 0x6
#define STE_SVLAN 0x1
#define STE_CVLAN 0x2
#define HDR_LEN_L2_MACS   0xC
#define HDR_LEN_L2_VLAN   0x4
#define HDR_LEN_L2_ETHER  0x2
#define HDR_LEN_L2        (HDR_LEN_L2_MACS + HDR_LEN_L2_ETHER)
#define HDR_LEN_L2_W_VLAN (HDR_LEN_L2 + HDR_LEN_L2_VLAN)

/* Set to STE a specific value using DR_STE_SET */
#define DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, value) do { \
	if ((spec)->s_fname) { \
		MLX5_SET(ste_##lookup_type, tag, t_fname, value); \
		(spec)->s_fname = 0; \
	} \
} while (0)

/* Set to STE spec->s_fname to tag->t_fname set spec->s_fname as used */
#define DR_STE_SET_TAG(lookup_type, tag, t_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, spec->s_fname)

/* Set to STE -1 to tag->t_fname and set spec->s_fname as used */
#define DR_STE_SET_ONES(lookup_type, tag, t_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, -1)

#define DR_STE_SET_TCP_FLAGS(lookup_type, tag, spec) do { \
	MLX5_SET(ste_##lookup_type, tag, tcp_ns, !!((spec)->tcp_flags & (1 << 8))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_cwr, !!((spec)->tcp_flags & (1 << 7))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_ece, !!((spec)->tcp_flags & (1 << 6))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_urg, !!((spec)->tcp_flags & (1 << 5))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_ack, !!((spec)->tcp_flags & (1 << 4))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_psh, !!((spec)->tcp_flags & (1 << 3))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_rst, !!((spec)->tcp_flags & (1 << 2))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_syn, !!((spec)->tcp_flags & (1 << 1))); \
	MLX5_SET(ste_##lookup_type, tag, tcp_fin, !!((spec)->tcp_flags & (1 << 0))); \
} while (0)

#define DR_STE_SET_MPLS(lookup_type, mask, in_out, tag) do { \
	struct mlx5dr_match_misc2 *_mask = mask; \
	u8 *_tag = tag; \
	DR_STE_SET_TAG(lookup_type, _tag, mpls0_label, _mask, \
		       in_out##_first_mpls_label);\
	DR_STE_SET_TAG(lookup_type, _tag, mpls0_s_bos, _mask, \
		       in_out##_first_mpls_s_bos); \
	DR_STE_SET_TAG(lookup_type, _tag, mpls0_exp, _mask, \
		       in_out##_first_mpls_exp); \
	DR_STE_SET_TAG(lookup_type, _tag, mpls0_ttl, _mask, \
		       in_out##_first_mpls_ttl); \
} while (0)

#define DR_STE_SET_FLEX_PARSER_FIELD(tag, fname, caps, spec) do { \
	u8 parser_id = (caps)->flex_parser_id_##fname; \
	u8 *parser_ptr = dr_ste_calc_flex_parser_offset(tag, parser_id); \
	*(__be32 *)parser_ptr = cpu_to_be32((spec)->fname);\
	(spec)->fname = 0;\
} while (0)

#define DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(_misc) (\
	(_misc)->outer_first_mpls_over_gre_label || \
	(_misc)->outer_first_mpls_over_gre_exp || \
	(_misc)->outer_first_mpls_over_gre_s_bos || \
	(_misc)->outer_first_mpls_over_gre_ttl)

#define DR_STE_IS_OUTER_MPLS_OVER_UDP_SET(_misc) (\
	(_misc)->outer_first_mpls_over_udp_label || \
	(_misc)->outer_first_mpls_over_udp_exp || \
	(_misc)->outer_first_mpls_over_udp_s_bos || \
	(_misc)->outer_first_mpls_over_udp_ttl)

enum dr_ste_action_modify_type_l3 {
	DR_STE_ACTION_MDFY_TYPE_L3_NONE	= 0x0,
	DR_STE_ACTION_MDFY_TYPE_L3_IPV4	= 0x1,
	DR_STE_ACTION_MDFY_TYPE_L3_IPV6	= 0x2,
};

enum dr_ste_action_modify_type_l4 {
	DR_STE_ACTION_MDFY_TYPE_L4_NONE	= 0x0,
	DR_STE_ACTION_MDFY_TYPE_L4_TCP	= 0x1,
	DR_STE_ACTION_MDFY_TYPE_L4_UDP	= 0x2,
};

enum {
	HDR_MPLS_OFFSET_LABEL	= 12,
	HDR_MPLS_OFFSET_EXP	= 9,
	HDR_MPLS_OFFSET_S_BOS	= 8,
	HDR_MPLS_OFFSET_TTL	= 0,
};

u16 mlx5dr_ste_conv_bit_to_byte_mask(u8 *bit_mask);

static inline u8 *
dr_ste_calc_flex_parser_offset(u8 *tag, u8 parser_id)
{
	/* Calculate tag byte offset based on flex parser id */
	return tag + 4 * (3 - (parser_id % 4));
}

#define DR_STE_CTX_BUILDER(fname) \
	((*build_##fname##_init)(struct mlx5dr_ste_build *sb, \
				 struct mlx5dr_match_param *mask))

struct mlx5dr_ste_ctx {
	/* Builders */
	void DR_STE_CTX_BUILDER(eth_l2_src_dst);
	void DR_STE_CTX_BUILDER(eth_l3_ipv6_src);
	void DR_STE_CTX_BUILDER(eth_l3_ipv6_dst);
	void DR_STE_CTX_BUILDER(eth_l3_ipv4_5_tuple);
	void DR_STE_CTX_BUILDER(eth_l2_src);
	void DR_STE_CTX_BUILDER(eth_l2_dst);
	void DR_STE_CTX_BUILDER(eth_l2_tnl);
	void DR_STE_CTX_BUILDER(eth_l3_ipv4_misc);
	void DR_STE_CTX_BUILDER(eth_ipv6_l3_l4);
	void DR_STE_CTX_BUILDER(mpls);
	void DR_STE_CTX_BUILDER(tnl_gre);
	void DR_STE_CTX_BUILDER(tnl_mpls);
	void DR_STE_CTX_BUILDER(tnl_mpls_over_gre);
	void DR_STE_CTX_BUILDER(tnl_mpls_over_udp);
	void DR_STE_CTX_BUILDER(icmp);
	void DR_STE_CTX_BUILDER(general_purpose);
	void DR_STE_CTX_BUILDER(eth_l4_misc);
	void DR_STE_CTX_BUILDER(tnl_vxlan_gpe);
	void DR_STE_CTX_BUILDER(tnl_geneve);
	void DR_STE_CTX_BUILDER(tnl_geneve_tlv_opt);
	void DR_STE_CTX_BUILDER(tnl_geneve_tlv_opt_exist);
	void DR_STE_CTX_BUILDER(register_0);
	void DR_STE_CTX_BUILDER(register_1);
	void DR_STE_CTX_BUILDER(src_gvmi_qpn);
	void DR_STE_CTX_BUILDER(flex_parser_0);
	void DR_STE_CTX_BUILDER(flex_parser_1);
	void DR_STE_CTX_BUILDER(tnl_gtpu);
	void DR_STE_CTX_BUILDER(tnl_header_0_1);
	void DR_STE_CTX_BUILDER(tnl_gtpu_flex_parser_0);
	void DR_STE_CTX_BUILDER(tnl_gtpu_flex_parser_1);

	/* Getters and Setters */
	void (*ste_init)(u8 *hw_ste_p, u16 lu_type,
			 bool is_rx, u16 gvmi);
	void (*set_next_lu_type)(u8 *hw_ste_p, u16 lu_type);
	u16  (*get_next_lu_type)(u8 *hw_ste_p);
	bool (*is_miss_addr_set)(u8 *hw_ste_p);
	void (*set_miss_addr)(u8 *hw_ste_p, u64 miss_addr);
	u64  (*get_miss_addr)(u8 *hw_ste_p);
	void (*set_hit_addr)(u8 *hw_ste_p, u64 icm_addr, u32 ht_size);
	void (*set_byte_mask)(u8 *hw_ste_p, u16 byte_mask);
	u16  (*get_byte_mask)(u8 *hw_ste_p);

	/* Actions */
	u32 actions_caps;
	void (*set_actions_rx)(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_domain *dmn,
			       u8 *action_type_set,
			       u32 actions_caps,
			       u8 *hw_ste_arr,
			       struct mlx5dr_ste_actions_attr *attr,
			       u32 *added_stes);
	void (*set_actions_tx)(struct mlx5dr_ste_ctx *ste_ctx,
			       struct mlx5dr_domain *dmn,
			       u8 *action_type_set,
			       u32 actions_caps,
			       u8 *hw_ste_arr,
			       struct mlx5dr_ste_actions_attr *attr,
			       u32 *added_stes);
	u32 modify_field_arr_sz;
	const struct mlx5dr_ste_action_modify_field *modify_field_arr;
	void (*set_action_set)(u8 *hw_action,
			       u8 hw_field,
			       u8 shifter,
			       u8 length,
			       u32 data);
	void (*set_action_add)(u8 *hw_action,
			       u8 hw_field,
			       u8 shifter,
			       u8 length,
			       u32 data);
	void (*set_action_copy)(u8 *hw_action,
				u8 dst_hw_field,
				u8 dst_shifter,
				u8 dst_len,
				u8 src_hw_field,
				u8 src_shifter);
	int (*set_action_decap_l3_list)(void *data,
					u32 data_sz,
					u8 *hw_action,
					u32 hw_action_sz,
					u16 *used_hw_action_num);
	int (*alloc_modify_hdr_chunk)(struct mlx5dr_action *action);
	void (*dealloc_modify_hdr_chunk)(struct mlx5dr_action *action);
	/* Actions bit set */
	void (*set_encap)(u8 *hw_ste_p, u8 *d_action,
			  u32 reformat_id, int size);
	void (*set_push_vlan)(u8 *ste, u8 *d_action,
			      u32 vlan_hdr);
	void (*set_pop_vlan)(u8 *hw_ste_p, u8 *s_action,
			     u8 vlans_num);
	void (*set_rx_decap)(u8 *hw_ste_p, u8 *s_action);
	void (*set_encap_l3)(u8 *hw_ste_p, u8 *frst_s_action,
			     u8 *scnd_d_action, u32 reformat_id,
			     int size);
	void (*set_insert_hdr)(u8 *hw_ste_p, u8 *d_action, u32 reformat_id,
			       u8 anchor, u8 offset, int size);
	void (*set_remove_hdr)(u8 *hw_ste_p, u8 *s_action, u8 anchor,
			       u8 offset, int size);
	/* Send */
	void (*prepare_for_postsend)(u8 *hw_ste_p, u32 ste_size);
};

struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx_v0(void);
struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx_v1(void);
struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx_v2(void);
struct mlx5dr_ste_ctx *mlx5dr_ste_get_ctx_v3(void);

#endif  /* _DR_STE_ */
