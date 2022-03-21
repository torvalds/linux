/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#include "health.h"

static int mlx5e_wait_for_sq_flush(struct mlx5e_txqsq *sq)
{
	unsigned long exp_time = jiffies +
				 msecs_to_jiffies(MLX5E_REPORTER_FLUSH_TIMEOUT_MSEC);

	while (time_before(jiffies, exp_time)) {
		if (sq->cc == sq->pc)
			return 0;

		msleep(20);
	}

	netdev_err(sq->channel->netdev,
		   "Wait for SQ 0x%x flush timeout (sq cc = 0x%x, sq pc = 0x%x)\n",
		   sq->sqn, sq->cc, sq->pc);

	return -ETIMEDOUT;
}

static void mlx5e_reset_txqsq_cc_pc(struct mlx5e_txqsq *sq)
{
	WARN_ONCE(sq->cc != sq->pc,
		  "SQ 0x%x: cc (0x%x) != pc (0x%x)\n",
		  sq->sqn, sq->cc, sq->pc);
	sq->cc = 0;
	sq->dma_fifo_cc = 0;
	sq->pc = 0;
}

static int mlx5e_tx_reporter_err_cqe_recover(void *ctx)
{
	struct mlx5_core_dev *mdev;
	struct net_device *dev;
	struct mlx5e_txqsq *sq;
	u8 state;
	int err;

	sq = ctx;
	mdev = sq->channel->mdev;
	dev = sq->channel->netdev;

	if (!test_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state))
		return 0;

	err = mlx5_core_query_sq_state(mdev, sq->sqn, &state);
	if (err) {
		netdev_err(dev, "Failed to query SQ 0x%x state. err = %d\n",
			   sq->sqn, err);
		goto out;
	}

	if (state != MLX5_SQC_STATE_ERR)
		goto out;

	mlx5e_tx_disable_queue(sq->txq);

	err = mlx5e_wait_for_sq_flush(sq);
	if (err)
		goto out;

	/* At this point, no new packets will arrive from the stack as TXQ is
	 * marked with QUEUE_STATE_DRV_XOFF. In addition, NAPI cleared all
	 * pending WQEs. SQ can safely reset the SQ.
	 */

	err = mlx5e_health_sq_to_ready(sq->channel, sq->sqn);
	if (err)
		goto out;

	mlx5e_reset_txqsq_cc_pc(sq);
	sq->stats->recover++;
	clear_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state);
	mlx5e_activate_txqsq(sq);

	return 0;
out:
	clear_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state);
	return err;
}

struct mlx5e_tx_timeout_ctx {
	struct mlx5e_txqsq *sq;
	signed int status;
};

static int mlx5e_tx_reporter_timeout_recover(void *ctx)
{
	struct mlx5e_tx_timeout_ctx *to_ctx;
	struct mlx5e_priv *priv;
	struct mlx5_eq_comp *eq;
	struct mlx5e_txqsq *sq;
	int err;

	to_ctx = ctx;
	sq = to_ctx->sq;
	eq = sq->cq.mcq.eq;
	priv = sq->channel->priv;
	err = mlx5e_health_channel_eq_recover(eq, sq->channel);
	if (!err) {
		to_ctx->status = 0; /* this sq recovered */
		return err;
	}

	err = mlx5e_safe_reopen_channels(priv);
	if (!err) {
		to_ctx->status = 1; /* all channels recovered */
		return err;
	}

	to_ctx->status = err;
	clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
	netdev_err(priv->netdev,
		   "mlx5e_safe_reopen_channels failed recovering from a tx_timeout, err(%d).\n",
		   err);

	return err;
}

/* state lock cannot be grabbed within this function.
 * It can cause a dead lock or a read-after-free.
 */
static int mlx5e_tx_reporter_recover_from_ctx(struct mlx5e_err_ctx *err_ctx)
{
	return err_ctx->recover(err_ctx->ctx);
}

static int mlx5e_tx_reporter_recover(struct devlink_health_reporter *reporter,
				     void *context,
				     struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_err_ctx *err_ctx = context;

	return err_ctx ? mlx5e_tx_reporter_recover_from_ctx(err_ctx) :
			 mlx5e_health_recover_channels(priv);
}

static int
mlx5e_tx_reporter_build_diagnose_output(struct devlink_fmsg *fmsg,
					struct mlx5e_txqsq *sq, int tc)
{
	struct mlx5e_priv *priv = sq->channel->priv;
	bool stopped = netif_xmit_stopped(sq->txq);
	u8 state;
	int err;

	err = mlx5_core_query_sq_state(priv->mdev, sq->sqn, &state);
	if (err)
		return err;

	err = devlink_fmsg_obj_nest_start(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "channel ix", sq->ch_ix);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "tc", tc);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "txq ix", sq->txq_ix);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "sqn", sq->sqn);
	if (err)
		return err;

	err = devlink_fmsg_u8_pair_put(fmsg, "HW state", state);
	if (err)
		return err;

	err = devlink_fmsg_bool_pair_put(fmsg, "stopped", stopped);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "cc", sq->cc);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "pc", sq->pc);
	if (err)
		return err;

	err = mlx5e_health_cq_diag_fmsg(&sq->cq, fmsg);
	if (err)
		return err;

	err = mlx5e_health_eq_diag_fmsg(sq->cq.mcq.eq, fmsg);
	if (err)
		return err;

	err = devlink_fmsg_obj_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}

static int mlx5e_tx_reporter_diagnose(struct devlink_health_reporter *reporter,
				      struct devlink_fmsg *fmsg,
				      struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_txqsq *generic_sq = priv->txq2sq[0];
	u32 sq_stride, sq_sz;

	int i, tc, err = 0;

	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	sq_sz = mlx5_wq_cyc_get_size(&generic_sq->wq);
	sq_stride = MLX5_SEND_WQE_BB;

	err = mlx5e_health_fmsg_named_obj_nest_start(fmsg, "Common Config");
	if (err)
		goto unlock;

	err = mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SQ");
	if (err)
		goto unlock;

	err = devlink_fmsg_u64_pair_put(fmsg, "stride size", sq_stride);
	if (err)
		goto unlock;

	err = devlink_fmsg_u32_pair_put(fmsg, "size", sq_sz);
	if (err)
		goto unlock;

	err = mlx5e_health_cq_common_diag_fmsg(&generic_sq->cq, fmsg);
	if (err)
		goto unlock;

	err = mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	if (err)
		goto unlock;

	err = mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	if (err)
		goto unlock;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "SQs");
	if (err)
		goto unlock;

	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		for (tc = 0; tc < priv->channels.params.num_tc; tc++) {
			struct mlx5e_txqsq *sq = &c->sq[tc];

			err = mlx5e_tx_reporter_build_diagnose_output(fmsg, sq, tc);
			if (err)
				goto unlock;
		}
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		goto unlock;

unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_tx_reporter_dump_sq(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
				     void *ctx)
{
	struct mlx5_rsc_key key = {};
	struct mlx5e_txqsq *sq = ctx;
	int err;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	err = mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SX Slice");
	if (err)
		return err;

	key.size = PAGE_SIZE;
	key.rsc = MLX5_SGMT_TYPE_SX_SLICE_ALL;
	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SQ");
	if (err)
		return err;

	err = mlx5e_health_fmsg_named_obj_nest_start(fmsg, "QPC");
	if (err)
		return err;

	key.rsc = MLX5_SGMT_TYPE_FULL_QPC;
	key.index1 = sq->sqn;
	key.num_of_obj1 = 1;

	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = mlx5e_health_fmsg_named_obj_nest_start(fmsg, "send_buff");
	if (err)
		return err;

	key.rsc = MLX5_SGMT_TYPE_SND_BUFF;
	key.num_of_obj2 = MLX5_RSC_DUMP_ALL;
	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	if (err)
		return err;

	return mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

static int mlx5e_tx_reporter_timeout_dump(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
					  void *ctx)
{
	struct mlx5e_tx_timeout_ctx *to_ctx = ctx;

	return mlx5e_tx_reporter_dump_sq(priv, fmsg, to_ctx->sq);
}

static int mlx5e_tx_reporter_dump_all_sqs(struct mlx5e_priv *priv,
					  struct devlink_fmsg *fmsg)
{
	struct mlx5_rsc_key key = {};
	int i, tc, err;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	err = mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SX Slice");
	if (err)
		return err;

	key.size = PAGE_SIZE;
	key.rsc = MLX5_SGMT_TYPE_SX_SLICE_ALL;
	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "SQs");
	if (err)
		return err;

	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		for (tc = 0; tc < priv->channels.params.num_tc; tc++) {
			struct mlx5e_txqsq *sq = &c->sq[tc];

			err = mlx5e_health_queue_dump(priv, fmsg, sq->sqn, "SQ");
			if (err)
				return err;
		}
	}
	return devlink_fmsg_arr_pair_nest_end(fmsg);
}

static int mlx5e_tx_reporter_dump_from_ctx(struct mlx5e_priv *priv,
					   struct mlx5e_err_ctx *err_ctx,
					   struct devlink_fmsg *fmsg)
{
	return err_ctx->dump(priv, fmsg, err_ctx->ctx);
}

static int mlx5e_tx_reporter_dump(struct devlink_health_reporter *reporter,
				  struct devlink_fmsg *fmsg, void *context,
				  struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_err_ctx *err_ctx = context;

	return err_ctx ? mlx5e_tx_reporter_dump_from_ctx(priv, err_ctx, fmsg) :
			 mlx5e_tx_reporter_dump_all_sqs(priv, fmsg);
}

void mlx5e_reporter_tx_err_cqe(struct mlx5e_txqsq *sq)
{
	struct mlx5e_priv *priv = sq->channel->priv;
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_err_ctx err_ctx = {};

	err_ctx.ctx = sq;
	err_ctx.recover = mlx5e_tx_reporter_err_cqe_recover;
	err_ctx.dump = mlx5e_tx_reporter_dump_sq;
	snprintf(err_str, sizeof(err_str), "ERR CQE on SQ: 0x%x", sq->sqn);

	mlx5e_health_report(priv, priv->tx_reporter, err_str, &err_ctx);
}

int mlx5e_reporter_tx_timeout(struct mlx5e_txqsq *sq)
{
	struct mlx5e_priv *priv = sq->channel->priv;
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_tx_timeout_ctx to_ctx = {};
	struct mlx5e_err_ctx err_ctx = {};

	to_ctx.sq = sq;
	err_ctx.ctx = &to_ctx;
	err_ctx.recover = mlx5e_tx_reporter_timeout_recover;
	err_ctx.dump = mlx5e_tx_reporter_timeout_dump;
	snprintf(err_str, sizeof(err_str),
		 "TX timeout on queue: %d, SQ: 0x%x, CQ: 0x%x, SQ Cons: 0x%x SQ Prod: 0x%x, usecs since last trans: %u",
		 sq->channel->ix, sq->sqn, sq->cq.mcq.cqn, sq->cc, sq->pc,
		 jiffies_to_usecs(jiffies - sq->txq->trans_start));

	mlx5e_health_report(priv, priv->tx_reporter, err_str, &err_ctx);
	return to_ctx.status;
}

static const struct devlink_health_reporter_ops mlx5_tx_reporter_ops = {
		.name = "tx",
		.recover = mlx5e_tx_reporter_recover,
		.diagnose = mlx5e_tx_reporter_diagnose,
		.dump = mlx5e_tx_reporter_dump,
};

#define MLX5_REPORTER_TX_GRACEFUL_PERIOD 500

void mlx5e_reporter_tx_create(struct mlx5e_priv *priv)
{
	struct devlink_health_reporter *reporter;

	reporter = devlink_port_health_reporter_create(&priv->dl_port, &mlx5_tx_reporter_ops,
						       MLX5_REPORTER_TX_GRACEFUL_PERIOD, priv);
	if (IS_ERR(reporter)) {
		netdev_warn(priv->netdev,
			    "Failed to create tx reporter, err = %ld\n",
			    PTR_ERR(reporter));
		return;
	}
	priv->tx_reporter = reporter;
}

void mlx5e_reporter_tx_destroy(struct mlx5e_priv *priv)
{
	if (!priv->tx_reporter)
		return;

	devlink_port_health_reporter_destroy(priv->tx_reporter);
}
