/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_EN_CHANNELS_H__
#define __MLX5_EN_CHANNELS_H__

#include <linux/kernel.h>

struct mlx5e_channels;

unsigned int mlx5e_channels_get_num(struct mlx5e_channels *chs);
bool mlx5e_channels_is_xsk(struct mlx5e_channels *chs, unsigned int ix);
void mlx5e_channels_get_regular_rqn(struct mlx5e_channels *chs, unsigned int ix, u32 *rqn);
void mlx5e_channels_get_xsk_rqn(struct mlx5e_channels *chs, unsigned int ix, u32 *rqn);
bool mlx5e_channels_get_ptp_rqn(struct mlx5e_channels *chs, u32 *rqn);

#endif /* __MLX5_EN_CHANNELS_H__ */
