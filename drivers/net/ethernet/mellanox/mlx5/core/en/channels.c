// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "channels.h"
#include "en.h"
#include "en/ptp.h"

unsigned int mlx5e_channels_get_num(struct mlx5e_channels *chs)
{
	return chs->num;
}

void mlx5e_channels_get_regular_rqn(struct mlx5e_channels *chs, unsigned int ix, u32 *rqn)
{
	struct mlx5e_channel *c;

	WARN_ON(ix >= mlx5e_channels_get_num(chs));
	c = chs->c[ix];

	*rqn = c->rq.rqn;
}

bool mlx5e_channels_get_xsk_rqn(struct mlx5e_channels *chs, unsigned int ix, u32 *rqn)
{
	struct mlx5e_channel *c;

	WARN_ON(ix >= mlx5e_channels_get_num(chs));
	c = chs->c[ix];

	if (!test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
		return false;

	*rqn = c->xskrq.rqn;
	return true;
}

bool mlx5e_channels_get_ptp_rqn(struct mlx5e_channels *chs, u32 *rqn)
{
	struct mlx5e_ptp *c = chs->ptp;

	if (!c || !test_bit(MLX5E_PTP_STATE_RX, c->state))
		return false;

	*rqn = c->rq.rqn;
	return true;
}
