/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_FLOW_METER_H__
#define __MLX5_EN_FLOW_METER_H__

struct mlx5e_flow_meters;

struct mlx5e_flow_meters *
mlx5e_flow_meters_init(struct mlx5e_priv *priv,
		       enum mlx5_flow_namespace_type ns_type,
		       struct mlx5e_post_act *post_action);
void
mlx5e_flow_meters_cleanup(struct mlx5e_flow_meters *flow_meters);

#endif /* __MLX5_EN_FLOW_METER_H__ */
