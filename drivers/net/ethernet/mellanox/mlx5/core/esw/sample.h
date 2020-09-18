/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#ifndef __MLX5_EN_TC_SAMPLE_H__
#define __MLX5_EN_TC_SAMPLE_H__

#include "en.h"

struct mlx5_sample_attr {
	u32 group_num;
	u32 rate;
	u32 trunc_size;
};

struct mlx5_esw_psample *
mlx5_esw_sample_init(struct mlx5e_priv *priv);

void
mlx5_esw_sample_cleanup(struct mlx5_esw_psample *esw_psample);

#endif /* __MLX5_EN_TC_SAMPLE_H__ */
