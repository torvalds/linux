/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2025 NVIDIA Corporation & Affiliates */

#ifndef _MLX5_FS_HWS_
#define _MLX5_FS_HWS_

#include "mlx5hws.h"

struct mlx5_fs_hws_context {
	struct mlx5hws_context	*hws_ctx;
};

struct mlx5_fs_hws_table {
	struct mlx5hws_table *hws_table;
	bool miss_ft_set;
};

#ifdef CONFIG_MLX5_HW_STEERING

const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void);

#else

static inline const struct mlx5_flow_cmds *mlx5_fs_cmd_get_hws_cmds(void)
{
	return NULL;
}

#endif /* CONFIG_MLX5_HWS_STEERING */
#endif
