/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __MLX5_EN_ACCEL_MACSEC_H__
#define __MLX5_EN_ACCEL_MACSEC_H__

#ifdef CONFIG_MLX5_EN_MACSEC

#include <linux/mlx5/driver.h>
#include <net/macsec.h>

struct mlx5e_priv;

void mlx5e_macsec_build_netdev(struct mlx5e_priv *priv);
int mlx5e_macsec_init(struct mlx5e_priv *priv);
void mlx5e_macsec_cleanup(struct mlx5e_priv *priv);

#else

static inline void mlx5e_macsec_build_netdev(struct mlx5e_priv *priv) {}
static inline int mlx5e_macsec_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_macsec_cleanup(struct mlx5e_priv *priv) {}

#endif  /* CONFIG_MLX5_EN_MACSEC */

#endif	/* __MLX5_ACCEL_EN_MACSEC_H__ */
