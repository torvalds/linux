/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_IPSEC_STEERING_H__
#define __MLX5_IPSEC_STEERING_H__

#include "en.h"
#include "ipsec.h"
#include "ipsec_offload.h"
#include "en/fs.h"

#ifdef CONFIG_MLX5_EN_IPSEC
void mlx5e_accel_ipsec_fs_cleanup(struct mlx5e_priv *priv);
int mlx5e_accel_ipsec_fs_init(struct mlx5e_priv *priv);
int mlx5e_accel_ipsec_fs_add_rule(struct mlx5e_priv *priv,
				  struct mlx5_accel_esp_xfrm_attrs *attrs,
				  u32 ipsec_obj_id,
				  struct mlx5e_ipsec_rule *ipsec_rule);
void mlx5e_accel_ipsec_fs_del_rule(struct mlx5e_priv *priv,
				   struct mlx5_accel_esp_xfrm_attrs *attrs,
				   struct mlx5e_ipsec_rule *ipsec_rule);
#else
static inline void mlx5e_accel_ipsec_fs_cleanup(struct mlx5e_priv *priv) {}
static inline int mlx5e_accel_ipsec_fs_init(struct mlx5e_priv *priv) { return 0; }
#endif
#endif /* __MLX5_IPSEC_STEERING_H__ */
