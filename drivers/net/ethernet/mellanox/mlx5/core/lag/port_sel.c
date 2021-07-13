// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#include <linux/netdevice.h>
#include "lag.h"

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
