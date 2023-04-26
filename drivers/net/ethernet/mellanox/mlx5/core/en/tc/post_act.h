/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_POST_ACTION_H__
#define __MLX5_POST_ACTION_H__

#include "en.h"
#include "lib/fs_chains.h"

struct mlx5_flow_attr;
struct mlx5e_priv;
struct mlx5e_tc_mod_hdr_acts;

struct mlx5e_post_act *
mlx5e_tc_post_act_init(struct mlx5e_priv *priv, struct mlx5_fs_chains *chains,
		       enum mlx5_flow_namespace_type ns_type);

void
mlx5e_tc_post_act_destroy(struct mlx5e_post_act *post_act);

struct mlx5e_post_act_handle *
mlx5e_tc_post_act_add(struct mlx5e_post_act *post_act, struct mlx5_flow_attr *post_attr);

void
mlx5e_tc_post_act_del(struct mlx5e_post_act *post_act, struct mlx5e_post_act_handle *handle);

int
mlx5e_tc_post_act_offload(struct mlx5e_post_act *post_act,
			  struct mlx5e_post_act_handle *handle);

void
mlx5e_tc_post_act_unoffload(struct mlx5e_post_act *post_act,
			    struct mlx5e_post_act_handle *handle);

struct mlx5_flow_table *
mlx5e_tc_post_act_get_ft(struct mlx5e_post_act *post_act);

int
mlx5e_tc_post_act_set_handle(struct mlx5_core_dev *dev,
			     struct mlx5e_post_act_handle *handle,
			     struct mlx5e_tc_mod_hdr_acts *acts);

#endif /* __MLX5_POST_ACTION_H__ */
