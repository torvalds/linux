/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_XSK_UMEM_H__
#define __MLX5_EN_XSK_UMEM_H__

#include "en.h"

static inline struct xdp_umem *mlx5e_xsk_get_umem(struct mlx5e_params *params,
						  struct mlx5e_xsk *xsk, u16 ix)
{
	if (!xsk || !xsk->umems)
		return NULL;

	if (unlikely(ix >= params->num_channels))
		return NULL;

	return xsk->umems[ix];
}

struct mlx5e_xsk_param;
void mlx5e_build_xsk_param(struct xdp_umem *umem, struct mlx5e_xsk_param *xsk);

/* .ndo_bpf callback. */
int mlx5e_xsk_setup_umem(struct net_device *dev, struct xdp_umem *umem, u16 qid);

int mlx5e_xsk_resize_reuseq(struct xdp_umem *umem, u32 nentries);

u16 mlx5e_xsk_first_unused_channel(struct mlx5e_params *params, struct mlx5e_xsk *xsk);

#endif /* __MLX5_EN_XSK_UMEM_H__ */
