/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5E_KTLS_H__
#define __MLX5E_KTLS_H__

#include "en.h"

#ifdef CONFIG_MLX5_EN_TLS

void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv);
int mlx5e_ktls_init_rx(struct mlx5e_priv *priv);
void mlx5e_ktls_cleanup_rx(struct mlx5e_priv *priv);
int mlx5e_ktls_set_feature_rx(struct net_device *netdev, bool enable);
#else

static inline void mlx5e_ktls_build_netdev(struct mlx5e_priv *priv)
{
}

static inline int mlx5e_ktls_init_rx(struct mlx5e_priv *priv)
{
	return 0;
}

static inline void mlx5e_ktls_cleanup_rx(struct mlx5e_priv *priv)
{
}

static inline int mlx5e_ktls_set_feature_rx(struct net_device *netdev, bool enable)
{
	netdev_warn(netdev, "kTLS is not supported\n");
	return -EOPNOTSUPP;
}

#endif

#endif /* __MLX5E_TLS_H__ */
