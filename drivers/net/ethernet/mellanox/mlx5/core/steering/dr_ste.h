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

/* Set to STE a specific value using DR_STE_SET */
#define DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, value) do { \
	if ((spec)->s_fname) { \
		MLX5_SET(ste_##lookup_type, tag, t_fname, value); \
		(spec)->s_fname = 0; \
	} \
} while (0)

/* Set to STE spec->s_fname to tag->t_fname */
#define DR_STE_SET_TAG(lookup_type, tag, t_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, spec->s_fname)

/* Set to STE -1 to bit_mask->bm_fname and set spec->s_fname as used */
#define DR_STE_SET_MASK(lookup_type, bit_mask, bm_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, bit_mask, bm_fname, spec, s_fname, -1)

/* Set to STE spec->s_fname to bit_mask->bm_fname and set spec->s_fname as used */
#define DR_STE_SET_MASK_V(lookup_type, bit_mask, bm_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, bit_mask, bm_fname, spec, s_fname, (spec)->s_fname)

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

#define DR_STE_SET_MPLS_MASK(lookup_type, mask, in_out, bit_mask) do { \
	DR_STE_SET_MASK_V(lookup_type, bit_mask, mpls0_label, mask, \
			  in_out##_first_mpls_label);\
	DR_STE_SET_MASK_V(lookup_type, bit_mask, mpls0_s_bos, mask, \
			  in_out##_first_mpls_s_bos); \
	DR_STE_SET_MASK_V(lookup_type, bit_mask, mpls0_exp, mask, \
			  in_out##_first_mpls_exp); \
	DR_STE_SET_MASK_V(lookup_type, bit_mask, mpls0_ttl, mask, \
			  in_out##_first_mpls_ttl); \
} while (0)

#define DR_STE_SET_MPLS_TAG(lookup_type, mask, in_out, tag) do { \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_label, mask, \
		       in_out##_first_mpls_label);\
	DR_STE_SET_TAG(lookup_type, tag, mpls0_s_bos, mask, \
		       in_out##_first_mpls_s_bos); \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_exp, mask, \
		       in_out##_first_mpls_exp); \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_ttl, mask, \
		       in_out##_first_mpls_ttl); \
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

u16 mlx5dr_ste_conv_bit_to_byte_mask(u8 *bit_mask);

#define DR_STE_CTX_BUILDER(fname) \
	((*build_##fname##_init)(struct mlx5dr_ste_build *sb, \
				 struct mlx5dr_match_param *mask))

struct mlx5dr_ste_ctx {
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
	int  DR_STE_CTX_BUILDER(icmp);
	void DR_STE_CTX_BUILDER(general_purpose);
	void DR_STE_CTX_BUILDER(eth_l4_misc);
	void DR_STE_CTX_BUILDER(tnl_vxlan_gpe);
	void DR_STE_CTX_BUILDER(tnl_geneve);
	void DR_STE_CTX_BUILDER(register_0);
	void DR_STE_CTX_BUILDER(register_1);
	void DR_STE_CTX_BUILDER(src_gvmi_qpn);
};

extern struct mlx5dr_ste_ctx ste_ctx_v0;

#endif  /* _DR_STE_ */
