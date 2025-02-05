// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2021, Mellanox Technologies inc. All rights reserved. */

#include "channels.h"
#include "en.h"
#include "en/dim.h"
#include "en/ptp.h"

unsigned int mlx5e_channels_get_num(struct mlx5e_channels *chs)
{
	return chs->num;
}

static struct mlx5e_channel *mlx5e_channels_get(struct mlx5e_channels *chs, unsigned int ix)
{
	WARN_ON_ONCE(ix >= mlx5e_channels_get_num(chs));
	return chs->c[ix];
}

bool mlx5e_channels_is_xsk(struct mlx5e_channels *chs, unsigned int ix)
{
	struct mlx5e_channel *c = mlx5e_channels_get(chs, ix);

	return test_bit(MLX5E_CHANNEL_STATE_XSK, c->state);
}

void mlx5e_channels_get_regular_rqn(struct mlx5e_channels *chs, unsigned int ix, u32 *rqn,
				    u32 *vhca_id)
{
	struct mlx5e_channel *c = mlx5e_channels_get(chs, ix);

	*rqn = c->rq.rqn;
	if (vhca_id)
		*vhca_id = MLX5_CAP_GEN(c->mdev, vhca_id);
}

void mlx5e_channels_get_xsk_rqn(struct mlx5e_channels *chs, unsigned int ix, u32 *rqn,
				u32 *vhca_id)
{
	struct mlx5e_channel *c = mlx5e_channels_get(chs, ix);

	WARN_ON_ONCE(!test_bit(MLX5E_CHANNEL_STATE_XSK, c->state));

	*rqn = c->xskrq.rqn;
	if (vhca_id)
		*vhca_id = MLX5_CAP_GEN(c->mdev, vhca_id);
}

bool mlx5e_channels_get_ptp_rqn(struct mlx5e_channels *chs, u32 *rqn)
{
	struct mlx5e_ptp *c = chs->ptp;

	if (!c || !test_bit(MLX5E_PTP_STATE_RX, c->state))
		return false;

	*rqn = c->rq.rqn;
	return true;
}

int mlx5e_channels_rx_change_dim(struct mlx5e_channels *chs, bool enable)
{
	int i;

	for (i = 0; i < chs->num; i++) {
		int err = mlx5e_dim_rx_change(&chs->c[i]->rq, enable);

		if (err)
			return err;
	}

	return 0;
}

int mlx5e_channels_tx_change_dim(struct mlx5e_channels *chs, bool enable)
{
	int i, tc;

	for (i = 0; i < chs->num; i++) {
		for (tc = 0; tc < mlx5e_get_dcb_num_tc(&chs->params); tc++) {
			int err = mlx5e_dim_tx_change(&chs->c[i]->sq[tc], enable);

			if (err)
				return err;
		}
	}

	return 0;
}

int mlx5e_channels_rx_toggle_dim(struct mlx5e_channels *chs)
{
	int i;

	for (i = 0; i < chs->num; i++) {
		/* If dim is enabled for the channel, reset the dim state so the
		 * collected statistics will be reset. This is useful for
		 * supporting legacy interfaces that allow things like changing
		 * the CQ period mode for all channels without disturbing
		 * individual channel configurations.
		 */
		if (chs->c[i]->rq.dim) {
			int err;

			mlx5e_dim_rx_change(&chs->c[i]->rq, false);
			err = mlx5e_dim_rx_change(&chs->c[i]->rq, true);
			if (err)
				return err;
		}
	}

	return 0;
}

int mlx5e_channels_tx_toggle_dim(struct mlx5e_channels *chs)
{
	int i, tc;

	for (i = 0; i < chs->num; i++) {
		for (tc = 0; tc < mlx5e_get_dcb_num_tc(&chs->params); tc++) {
			int err;

			/* If dim is enabled for the channel, reset the dim
			 * state so the collected statistics will be reset. This
			 * is useful for supporting legacy interfaces that allow
			 * things like changing the CQ period mode for all
			 * channels without disturbing individual channel
			 * configurations.
			 */
			if (!chs->c[i]->sq[tc].dim)
				continue;

			mlx5e_dim_tx_change(&chs->c[i]->sq[tc], false);
			err = mlx5e_dim_tx_change(&chs->c[i]->sq[tc], true);
			if (err)
				return err;
		}
	}

	return 0;
}
