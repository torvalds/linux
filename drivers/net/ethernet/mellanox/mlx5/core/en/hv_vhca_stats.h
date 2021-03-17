/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_STATS_VHCA_H__
#define __MLX5_EN_STATS_VHCA_H__
#include "en.h"

#if IS_ENABLED(CONFIG_PCI_HYPERV_INTERFACE)

int mlx5e_hv_vhca_stats_create(struct mlx5e_priv *priv);
void mlx5e_hv_vhca_stats_destroy(struct mlx5e_priv *priv);

#else

static inline int mlx5e_hv_vhca_stats_create(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5e_hv_vhca_stats_destroy(struct mlx5e_priv *priv)
{
}
#endif

#endif /* __MLX5_EN_STATS_VHCA_H__ */
