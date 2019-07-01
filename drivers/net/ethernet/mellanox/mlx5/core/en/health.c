// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Mellanox Technologies.

#include "health.h"
#include "lib/eq.h"

int mlx5e_health_sq_to_ready(struct mlx5e_channel *channel, u32 sqn)
{
	struct mlx5_core_dev *mdev = channel->mdev;
	struct net_device *dev = channel->netdev;
	struct mlx5e_modify_sq_param msp = {};
	int err;

	msp.curr_state = MLX5_SQC_STATE_ERR;
	msp.next_state = MLX5_SQC_STATE_RST;

	err = mlx5e_modify_sq(mdev, sqn, &msp);
	if (err) {
		netdev_err(dev, "Failed to move sq 0x%x to reset\n", sqn);
		return err;
	}

	memset(&msp, 0, sizeof(msp));
	msp.curr_state = MLX5_SQC_STATE_RST;
	msp.next_state = MLX5_SQC_STATE_RDY;

	err = mlx5e_modify_sq(mdev, sqn, &msp);
	if (err) {
		netdev_err(dev, "Failed to move sq 0x%x to ready\n", sqn);
		return err;
	}

	return 0;
}

int mlx5e_health_recover_channels(struct mlx5e_priv *priv)
{
	int err = 0;

	rtnl_lock();
	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto out;

	err = mlx5e_safe_reopen_channels(priv);

out:
	mutex_unlock(&priv->state_lock);
	rtnl_unlock();

	return err;
}

int mlx5e_health_channel_eq_recover(struct mlx5_eq_comp *eq, struct mlx5e_channel *channel)
{
	u32 eqe_count;

	netdev_err(channel->netdev, "EQ 0x%x: Cons = 0x%x, irqn = 0x%x\n",
		   eq->core.eqn, eq->core.cons_index, eq->core.irqn);

	eqe_count = mlx5_eq_poll_irq_disabled(eq);
	if (!eqe_count)
		return -EIO;

	netdev_err(channel->netdev, "Recovered %d eqes on EQ 0x%x\n",
		   eqe_count, eq->core.eqn);

	channel->stats->eq_rearm++;
	return 0;
}

int mlx5e_health_report(struct mlx5e_priv *priv,
			struct devlink_health_reporter *reporter, char *err_str,
			struct mlx5e_err_ctx *err_ctx)
{
	if (!reporter) {
		netdev_err(priv->netdev, err_str);
		return err_ctx->recover(&err_ctx->ctx);
	}
	return devlink_health_report(reporter, err_str, err_ctx);
}
