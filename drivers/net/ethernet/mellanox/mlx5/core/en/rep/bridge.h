/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021 Mellanox Technologies. */

#ifndef __MLX5_EN_REP_BRIDGE__
#define __MLX5_EN_REP_BRIDGE__

#include "en.h"

#if IS_ENABLED(CONFIG_MLX5_BRIDGE)

void mlx5e_rep_bridge_init(struct mlx5e_priv *priv);
void mlx5e_rep_bridge_cleanup(struct mlx5e_priv *priv);

#else /* CONFIG_MLX5_BRIDGE */

static inline void mlx5e_rep_bridge_init(struct mlx5e_priv *priv) {}
static inline void mlx5e_rep_bridge_cleanup(struct mlx5e_priv *priv) {}

#endif /* CONFIG_MLX5_BRIDGE */

#endif /* __MLX5_EN_REP_BRIDGE__ */
