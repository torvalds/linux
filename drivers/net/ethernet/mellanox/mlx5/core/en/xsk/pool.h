/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019-2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_EN_XSK_POOL_H__
#define __MLX5_EN_XSK_POOL_H__

#include "en.h"

static inline struct xsk_buff_pool *mlx5e_xsk_get_pool(struct mlx5e_params *params,
						       struct mlx5e_xsk *xsk, u16 ix)
{
	if (!xsk || !xsk->pools)
		return NULL;

	if (unlikely(ix >= params->num_channels))
		return NULL;

	return xsk->pools[ix];
}

struct mlx5e_xsk_param;
void mlx5e_build_xsk_param(struct xsk_buff_pool *pool, struct mlx5e_xsk_param *xsk);

/* .ndo_bpf callback. */
int mlx5e_xsk_setup_pool(struct net_device *dev, struct xsk_buff_pool *pool, u16 qid);

#endif /* __MLX5_EN_XSK_POOL_H__ */
