// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "rx_res.h"

static const struct mlx5e_rss_params_traffic_type rss_default_config[MLX5E_NUM_INDIR_TIRS] = {
	[MLX5E_TT_IPV4_TCP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = MLX5_L4_PROT_TYPE_TCP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV6_TCP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = MLX5_L4_PROT_TYPE_TCP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV4_UDP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = MLX5_L4_PROT_TYPE_UDP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV6_UDP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = MLX5_L4_PROT_TYPE_UDP,
		.rx_hash_fields = MLX5_HASH_IP_L4PORTS,
	},
	[MLX5E_TT_IPV4_IPSEC_AH] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV6_IPSEC_AH] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV4_IPSEC_ESP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV6_IPSEC_ESP] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP_IPSEC_SPI,
	},
	[MLX5E_TT_IPV4] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV4,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP,
	},
	[MLX5E_TT_IPV6] = {
		.l3_prot_type = MLX5_L3_PROT_TYPE_IPV6,
		.l4_prot_type = 0,
		.rx_hash_fields = MLX5_HASH_IP,
	},
};

struct mlx5e_rss_params_traffic_type
mlx5e_rss_get_default_tt_config(enum mlx5e_traffic_types tt)
{
	return rss_default_config[tt];
}

struct mlx5e_rss_params_traffic_type
mlx5e_rx_res_rss_get_current_tt_config(struct mlx5e_rx_res *res, enum mlx5e_traffic_types tt)
{
	struct mlx5e_rss_params_traffic_type rss_tt;

	rss_tt = mlx5e_rss_get_default_tt_config(tt);
	rss_tt.rx_hash_fields = res->rss_params.rx_hash_fields[tt];
	return rss_tt;
}
