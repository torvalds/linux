/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_XSK_TX_H__
#define __MLX5_EN_XSK_TX_H__

#include "en.h"
#include <net/xdp_sock_drv.h>

/* TX data path */

int mlx5e_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags);

bool mlx5e_xsk_tx(struct mlx5e_xdpsq *sq, unsigned int budget);

static inline void mlx5e_xsk_update_tx_wakeup(struct mlx5e_xdpsq *sq)
{
	if (!xsk_uses_need_wakeup(sq->xsk_pool))
		return;

	if (sq->pc != sq->cc)
		xsk_clear_tx_need_wakeup(sq->xsk_pool);
	else
		xsk_set_tx_need_wakeup(sq->xsk_pool);
}

#endif /* __MLX5_EN_XSK_TX_H__ */
