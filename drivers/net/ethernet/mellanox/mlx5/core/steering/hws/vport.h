/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_VPORT_H_
#define MLX5HWS_VPORT_H_

int mlx5hws_vport_init_vports(struct mlx5hws_context *ctx);

void mlx5hws_vport_uninit_vports(struct mlx5hws_context *ctx);

int mlx5hws_vport_get_gvmi(struct mlx5hws_context *ctx, u16 vport, u16 *vport_gvmi);

#endif /* MLX5HWS_VPORT_H_ */
