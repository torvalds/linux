/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5E_ACCEL_FS_TCP_H__
#define __MLX5E_ACCEL_FS_TCP_H__

#include "en.h"

#ifdef CONFIG_MLX5_EN_TLS
int mlx5e_accel_fs_tcp_create(struct mlx5e_priv *priv);
void mlx5e_accel_fs_tcp_destroy(struct mlx5e_priv *priv);
struct mlx5_flow_handle *mlx5e_accel_fs_add_sk(struct mlx5e_priv *priv,
					       struct sock *sk, u32 tirn,
					       uint32_t flow_tag);
void mlx5e_accel_fs_del_sk(struct mlx5_flow_handle *rule);
#else
static inline int mlx5e_accel_fs_tcp_create(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_accel_fs_tcp_destroy(struct mlx5e_priv *priv) {}
static inline struct mlx5_flow_handle *mlx5e_accel_fs_add_sk(struct mlx5e_priv *priv,
							     struct sock *sk, u32 tirn,
							     uint32_t flow_tag)
{ return ERR_PTR(-EOPNOTSUPP); }
static inline void mlx5e_accel_fs_del_sk(struct mlx5_flow_handle *rule) {}
#endif

#endif /* __MLX5E_ACCEL_FS_TCP_H__ */

