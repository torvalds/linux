/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. */

#ifndef __MLX5_LAG_FS_H__
#define __MLX5_LAG_FS_H__

#include "lib/fs_ttc.h"

struct mlx5_lag_port_sel {
	DECLARE_BITMAP(tt_map, MLX5_NUM_TT);
	bool   tunnel;
};

#endif /* __MLX5_LAG_FS_H__ */
