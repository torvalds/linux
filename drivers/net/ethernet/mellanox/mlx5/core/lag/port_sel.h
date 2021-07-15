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

#endif /* __MLX5_LAG_FS_H__ */
