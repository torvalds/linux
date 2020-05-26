/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_KTLS_H__
#define __MLX5E_KTLS_H__

#include "en.h"

#ifdef CONFIG_MLX5_EN_TLS

void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv);

#else

static inline void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv)
{
}

#endif

#endif /* __MLX5E_TLS_H__ */
