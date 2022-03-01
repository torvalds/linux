/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5_LAG_FS_H__
#define __MLX5_LAG_FS_H__

#include "lib/fs_ttc.h"

struct mlx5_lag_definer {
	struct mlx5_flow_definer *definer;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_handle *rules[MLX5_MAX_PORTS];
};

struct mlx5_lag_ttc {
	struct mlx5_ttc_table *ttc;
	struct mlx5_lag_definer *definers[MLX5_NUM_TT];
};

struct mlx5_lag_port_sel {
	DECLARE_BITMAP(tt_map, MLX5_NUM_TT);
	bool   tunnel;
	struct mlx5_lag_ttc outer;
	struct mlx5_lag_ttc inner;
};

#ifdef CONFIG_MLX5_ESWITCH

int mlx5_lag_port_sel_modify(struct mlx5_lag *ldev, u8 *ports);
void mlx5_lag_port_sel_destroy(struct mlx5_lag *ldev);
int mlx5_lag_port_sel_create(struct mlx5_lag *ldev,
			     enum netdev_lag_hash hash_type, u8 *ports);

#else /* CONFIG_MLX5_ESWITCH */
static inline int mlx5_lag_port_sel_create(struct mlx5_lag *ldev,
					   enum netdev_lag_hash hash_type,
					   u8 *ports)
{
	return 0;
}

static inline int mlx5_lag_port_sel_modify(struct mlx5_lag *ldev, u8 *ports)
{
	return 0;
}

static inline void mlx5_lag_port_sel_destroy(struct mlx5_lag *ldev) {}
#endif /* CONFIG_MLX5_ESWITCH */
#endif /* __MLX5_LAG_FS_H__ */
