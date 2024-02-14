/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_XSK_TX_H__
#define __MLX5_EN_XSK_TX_H__

#include "en.h"

/* TX data path */

int mlx5e_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags);

bool mlx5e_xsk_tx(struct mlx5e_xdpsq *sq, unsigned int budget);

#endif /* __MLX5_EN_XSK_TX_H__ */
