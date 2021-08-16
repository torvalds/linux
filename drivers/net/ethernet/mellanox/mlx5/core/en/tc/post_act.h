/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_POST_ACTION_H__
#define __MLX5_POST_ACTION_H__

#include "en.h"
#include "lib/fs_chains.h"

struct mlx5e_post_act *
mlx5e_tc_post_act_init(struct mlx5e_priv *priv, struct mlx5_fs_chains *chains,
		       enum mlx5_flow_namespace_type ns_type);

void
mlx5e_tc_post_act_destroy(struct mlx5e_post_act *post_act);

#endif /* __MLX5_POST_ACTION_H__ */
