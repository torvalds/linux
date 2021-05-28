/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_ESW_QOS_H__
#define __MLX5_ESW_QOS_H__

#ifdef CONFIG_MLX5_ESWITCH

int mlx5_esw_qos_set_vport_rate(struct mlx5_eswitch *esw, struct mlx5_vport *evport,
				u32 max_rate, u32 min_rate);
void mlx5_esw_qos_create(struct mlx5_eswitch *esw);
void mlx5_esw_qos_destroy(struct mlx5_eswitch *esw);
int mlx5_esw_qos_vport_enable(struct mlx5_eswitch *esw, struct mlx5_vport *vport,
			      u32 max_rate, u32 bw_share);
void mlx5_esw_qos_vport_disable(struct mlx5_eswitch *esw, struct mlx5_vport *vport);

#endif

#endif
