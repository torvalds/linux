// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Mellanox Technologies.

#include "health.h"
#include "lib/eq.h"

int mlx5e_reporter_named_obj_nest_start(struct devlink_fmsg *fmsg, char *name)
{
	int err;

	err = devlink_fmsg_pair_nest_start(fmsg, name);
	if (err)
		return err;

	err = devlink_fmsg_obj_nest_start(fmsg);
	if (err)
		return err;

	return 0;
}

int mlx5e_reporter_named_obj_nest_end(struct devlink_fmsg *fmsg)
{
	int err;

	err = devlink_fmsg_obj_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_pair_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}

int mlx5e_reporter_cq_diagnose(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg)
{
	struct mlx5e_priv *priv = cq->channel->priv;
	u32 out[MLX5_ST_SZ_DW(query_cq_out)] = {};
	u8 hw_status;
	void *cqc;
	int err;

	err = mlx5_core_query_cq(priv->mdev, &cq->mcq, out, sizeof(out));
	if (err)
		return err;

	cqc = MLX5_ADDR_OF(query_cq_out, out, cq_context);
	hw_status = MLX5_GET(cqc, cqc, status);

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "CQ");
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "cqn", cq->mcq.cqn);
	if (err)
		return err;

	err = devlink_fmsg_u8_pair_put(fmsg, "HW status", hw_status);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}

int mlx5e_reporter_cq_common_diagnose(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg)
{
	u8 cq_log_stride;
	u32 cq_sz;
	int err;

	cq_sz = mlx5_cqwq_get_size(&cq->wq);
	cq_log_stride = mlx5_cqwq_get_log_stride_size(&cq->wq);

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "CQ");
	if (err)
		return err;

	err = devlink_fmsg_u64_pair_put(fmsg, "stride size", BIT(cq_log_stride));
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "size", cq_sz);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}

int mlx5e_health_create_reporters(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_reporter_tx_create(priv);
	if (err)
		return err;

	err = mlx5e_reporter_rx_create(priv);
	if (err)
		return err;

	return 0;
}

void mlx5e_health_destroy_reporters(struct mlx5e_priv *priv)
{
	mlx5e_reporter_rx_destroy(priv);
	mlx5e_reporter_tx_destroy(priv);
}

void mlx5e_health_channels_update(struct mlx5e_priv *priv)
{
	if (priv->tx_reporter)
		devlink_health_reporter_state_update(priv->tx_reporter,
						     DEVLINK_HEALTH_REPORTER_STATE_HEALTHY);
	if (priv->rx_reporter)
		devlink_health_reporter_state_update(priv->rx_reporter,
						     DEVLINK_HEALTH_REPORTER_STATE_HEALTHY);
}

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
	netdev_err(priv->netdev, err_str);

	if (!reporter)
		return err_ctx->recover(&err_ctx->ctx);

	return devlink_health_report(reporter, err_str, err_ctx);
}
