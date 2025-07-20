/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies. */

#ifndef __ML5_FS_TTC_H__
#define __ML5_FS_TTC_H__

#include <linux/mlx5/fs.h>

enum mlx5_traffic_types {
	MLX5_TT_IPV4_TCP,
	MLX5_TT_IPV6_TCP,
	MLX5_TT_IPV4_UDP,
	MLX5_TT_IPV6_UDP,
	MLX5_TT_IPV4_IPSEC_AH,
	MLX5_TT_IPV6_IPSEC_AH,
	MLX5_TT_IPV4_IPSEC_ESP,
	MLX5_TT_IPV6_IPSEC_ESP,
	MLX5_TT_IPV4,
	MLX5_TT_IPV6,
	MLX5_TT_ANY,
	MLX5_NUM_TT,
	MLX5_NUM_INDIR_TIRS = MLX5_TT_ANY,
};

enum mlx5_tunnel_types {
	MLX5_TT_IPV4_GRE,
	MLX5_TT_IPV6_GRE,
	MLX5_TT_IPV4_IPIP,
	MLX5_TT_IPV6_IPIP,
	MLX5_TT_IPV4_IPV6,
	MLX5_TT_IPV6_IPV6,
	MLX5_NUM_TUNNEL_TT,
};

struct mlx5_ttc_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_destination default_dest;
};

struct mlx5_ttc_table;

struct ttc_params {
	enum mlx5_flow_namespace_type ns_type;
	struct mlx5_flow_table_attr ft_attr;
	struct mlx5_flow_destination dests[MLX5_NUM_TT];
	DECLARE_BITMAP(ignore_dests, MLX5_NUM_TT);
	bool   inner_ttc;
	DECLARE_BITMAP(ignore_tunnel_dests, MLX5_NUM_TUNNEL_TT);
	struct mlx5_flow_destination tunnel_dests[MLX5_NUM_TUNNEL_TT];
};

const char *mlx5_ttc_get_name(enum mlx5_traffic_types tt);
struct mlx5_flow_table *mlx5_get_ttc_flow_table(struct mlx5_ttc_table *ttc);

struct mlx5_ttc_table *mlx5_create_ttc_table(struct mlx5_core_dev *dev,
					     struct ttc_params *params);
void mlx5_destroy_ttc_table(struct mlx5_ttc_table *ttc);

struct mlx5_ttc_table *mlx5_create_inner_ttc_table(struct mlx5_core_dev *dev,
						   struct ttc_params *params);

int mlx5_ttc_fwd_dest(struct mlx5_ttc_table *ttc, enum mlx5_traffic_types type,
		      struct mlx5_flow_destination *new_dest);
struct mlx5_flow_destination
mlx5_ttc_get_default_dest(struct mlx5_ttc_table *ttc,
			  enum mlx5_traffic_types type);
int mlx5_ttc_fwd_default_dest(struct mlx5_ttc_table *ttc,
			      enum mlx5_traffic_types type);

bool mlx5_tunnel_inner_ft_supported(struct mlx5_core_dev *mdev);
u8 mlx5_get_proto_by_tunnel_type(enum mlx5_tunnel_types tt);

#endif /* __MLX5_FS_TTC_H__ */
