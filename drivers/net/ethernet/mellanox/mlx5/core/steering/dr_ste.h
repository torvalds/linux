/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 NVIDIA CORPORATION. All rights reserved. */

#ifndef	_DR_STE_
#define	_DR_STE_

#include "dr_types.h"

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

#endif  /* _DR_STE_ */
