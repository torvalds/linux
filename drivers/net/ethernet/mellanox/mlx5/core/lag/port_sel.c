// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#include <linux/netdevice.h>
#include "lag.h"

static int mlx5_lag_set_definer_inner(u32 *match_definer_mask,
				      enum mlx5_traffic_types tt)
{
	int format_id;
	u8 *ipv6;

	switch (tt) {
	case MLX5_TT_IPV4_UDP:
	case MLX5_TT_IPV4_TCP:
		format_id = 23;
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_l4_sport);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_l4_dport);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_ip_src_addr);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_ip_dest_addr);
		break;
	case MLX5_TT_IPV4:
		format_id = 23;
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_l3_type);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_dmac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_dmac_15_0);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_smac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_smac_15_0);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_ip_src_addr);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_ip_dest_addr);
		break;
	case MLX5_TT_IPV6_TCP:
	case MLX5_TT_IPV6_UDP:
		format_id = 31;
		MLX5_SET_TO_ONES(match_definer_format_31, match_definer_mask,
				 inner_l4_sport);
		MLX5_SET_TO_ONES(match_definer_format_31, match_definer_mask,
				 inner_l4_dport);
		ipv6 = MLX5_ADDR_OF(match_definer_format_31, match_definer_mask,
				    inner_ip_dest_addr);
		memset(ipv6, 0xff, 16);
		ipv6 = MLX5_ADDR_OF(match_definer_format_31, match_definer_mask,
				    inner_ip_src_addr);
		memset(ipv6, 0xff, 16);
		break;
	case MLX5_TT_IPV6:
		format_id = 32;
		ipv6 = MLX5_ADDR_OF(match_definer_format_32, match_definer_mask,
				    inner_ip_dest_addr);
		memset(ipv6, 0xff, 16);
		ipv6 = MLX5_ADDR_OF(match_definer_format_32, match_definer_mask,
				    inner_ip_src_addr);
		memset(ipv6, 0xff, 16);
		MLX5_SET_TO_ONES(match_definer_format_32, match_definer_mask,
				 inner_dmac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_32, match_definer_mask,
				 inner_dmac_15_0);
		MLX5_SET_TO_ONES(match_definer_format_32, match_definer_mask,
				 inner_smac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_32, match_definer_mask,
				 inner_smac_15_0);
		break;
	default:
		format_id = 23;
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_l3_type);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_dmac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_dmac_15_0);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_smac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_23, match_definer_mask,
				 inner_smac_15_0);
		break;
	}

	return format_id;
}

static int mlx5_lag_set_definer(u32 *match_definer_mask,
				enum mlx5_traffic_types tt, bool tunnel,
				enum netdev_lag_hash hash)
{
	int format_id;
	u8 *ipv6;

	if (tunnel)
		return mlx5_lag_set_definer_inner(match_definer_mask, tt);

	switch (tt) {
	case MLX5_TT_IPV4_UDP:
	case MLX5_TT_IPV4_TCP:
		format_id = 22;
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_l4_sport);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_l4_dport);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_ip_src_addr);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_ip_dest_addr);
		break;
	case MLX5_TT_IPV4:
		format_id = 22;
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_l3_type);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_dmac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_dmac_15_0);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_smac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_smac_15_0);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_ip_src_addr);
		MLX5_SET_TO_ONES(match_definer_format_22, match_definer_mask,
				 outer_ip_dest_addr);
		break;
	case MLX5_TT_IPV6_TCP:
	case MLX5_TT_IPV6_UDP:
		format_id = 29;
		MLX5_SET_TO_ONES(match_definer_format_29, match_definer_mask,
				 outer_l4_sport);
		MLX5_SET_TO_ONES(match_definer_format_29, match_definer_mask,
				 outer_l4_dport);
		ipv6 = MLX5_ADDR_OF(match_definer_format_29, match_definer_mask,
				    outer_ip_dest_addr);
		memset(ipv6, 0xff, 16);
		ipv6 = MLX5_ADDR_OF(match_definer_format_29, match_definer_mask,
				    outer_ip_src_addr);
		memset(ipv6, 0xff, 16);
		break;
	case MLX5_TT_IPV6:
		format_id = 30;
		ipv6 = MLX5_ADDR_OF(match_definer_format_30, match_definer_mask,
				    outer_ip_dest_addr);
		memset(ipv6, 0xff, 16);
		ipv6 = MLX5_ADDR_OF(match_definer_format_30, match_definer_mask,
				    outer_ip_src_addr);
		memset(ipv6, 0xff, 16);
		MLX5_SET_TO_ONES(match_definer_format_30, match_definer_mask,
				 outer_dmac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_30, match_definer_mask,
				 outer_dmac_15_0);
		MLX5_SET_TO_ONES(match_definer_format_30, match_definer_mask,
				 outer_smac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_30, match_definer_mask,
				 outer_smac_15_0);
		break;
	default:
		format_id = 0;
		MLX5_SET_TO_ONES(match_definer_format_0, match_definer_mask,
				 outer_smac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_0, match_definer_mask,
				 outer_smac_15_0);

		if (hash == NETDEV_LAG_HASH_VLAN_SRCMAC) {
			MLX5_SET_TO_ONES(match_definer_format_0,
					 match_definer_mask,
					 outer_first_vlan_vid);
			break;
		}

		MLX5_SET_TO_ONES(match_definer_format_0, match_definer_mask,
				 outer_ethertype);
		MLX5_SET_TO_ONES(match_definer_format_0, match_definer_mask,
				 outer_dmac_47_16);
		MLX5_SET_TO_ONES(match_definer_format_0, match_definer_mask,
				 outer_dmac_15_0);
		break;
	}

	return format_id;
}

static void set_tt_map(struct mlx5_lag_port_sel *port_sel,
		       enum netdev_lag_hash hash)
{
	port_sel->tunnel = false;

	switch (hash) {
	case NETDEV_LAG_HASH_E34:
		port_sel->tunnel = true;
		fallthrough;
	case NETDEV_LAG_HASH_L34:
		set_bit(MLX5_TT_IPV4_TCP, port_sel->tt_map);
		set_bit(MLX5_TT_IPV4_UDP, port_sel->tt_map);
		set_bit(MLX5_TT_IPV6_TCP, port_sel->tt_map);
		set_bit(MLX5_TT_IPV6_UDP, port_sel->tt_map);
		set_bit(MLX5_TT_IPV4, port_sel->tt_map);
		set_bit(MLX5_TT_IPV6, port_sel->tt_map);
		set_bit(MLX5_TT_ANY, port_sel->tt_map);
		break;
	case NETDEV_LAG_HASH_E23:
		port_sel->tunnel = true;
		fallthrough;
	case NETDEV_LAG_HASH_L23:
		set_bit(MLX5_TT_IPV4, port_sel->tt_map);
		set_bit(MLX5_TT_IPV6, port_sel->tt_map);
		set_bit(MLX5_TT_ANY, port_sel->tt_map);
		break;
	default:
		set_bit(MLX5_TT_ANY, port_sel->tt_map);
		break;
	}
}
