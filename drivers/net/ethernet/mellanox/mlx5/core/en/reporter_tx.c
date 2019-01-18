/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#include <net/devlink.h>
#include "reporter.h"
#include "lib/eq.h"

#define MLX5E_TX_REPORTER_PER_SQ_MAX_LEN 256

struct mlx5e_tx_err_ctx {
	int (*recover)(struct mlx5e_txqsq *sq);
	struct mlx5e_txqsq *sq;
};

static int mlx5e_wait_for_sq_flush(struct mlx5e_txqsq *sq)
{
	unsigned long exp_time = jiffies + msecs_to_jiffies(2000);

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

static int mlx5e_sq_to_ready(struct mlx5e_txqsq *sq, int curr_state)
{
	struct mlx5_core_dev *mdev = sq->channel->mdev;
	struct net_device *dev = sq->channel->netdev;
	struct mlx5e_modify_sq_param msp = {0};
	int err;

	msp.curr_state = curr_state;
	msp.next_state = MLX5_SQC_STATE_RST;

	err = mlx5e_modify_sq(mdev, sq->sqn, &msp);
	if (err) {
		netdev_err(dev, "Failed to move sq 0x%x to reset\n", sq->sqn);
		return err;
	}

	memset(&msp, 0, sizeof(msp));
	msp.curr_state = MLX5_SQC_STATE_RST;
	msp.next_state = MLX5_SQC_STATE_RDY;

	err = mlx5e_modify_sq(mdev, sq->sqn, &msp);
	if (err) {
		netdev_err(dev, "Failed to move sq 0x%x to ready\n", sq->sqn);
		return err;
	}

	return 0;
}

static int mlx5e_tx_reporter_err_cqe_recover(struct mlx5e_txqsq *sq)
{
	struct mlx5_core_dev *mdev = sq->channel->mdev;
	struct net_device *dev = sq->channel->netdev;
	u8 state;
	int err;

	if (!test_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state))
		return 0;

	err = mlx5_core_query_sq_state(mdev, sq->sqn, &state);
	if (err) {
		netdev_err(dev, "Failed to query SQ 0x%x state. err = %d\n",
			   sq->sqn, err);
		return err;
	}

	if (state != MLX5_RQC_STATE_ERR) {
		netdev_err(dev, "SQ 0x%x not in ERROR state\n", sq->sqn);
		return -EINVAL;
	}

	mlx5e_tx_disable_queue(sq->txq);

	err = mlx5e_wait_for_sq_flush(sq);
	if (err)
		return err;

	/* At this point, no new packets will arrive from the stack as TXQ is
	 * marked with QUEUE_STATE_DRV_XOFF. In addition, NAPI cleared all
	 * pending WQEs.  SQ can safely reset the SQ.
	 */

	err = mlx5e_sq_to_ready(sq, state);
	if (err)
		return err;

	mlx5e_reset_txqsq_cc_pc(sq);
	sq->stats->recover++;
	mlx5e_activate_txqsq(sq);

	return 0;
}

void mlx5e_tx_reporter_err_cqe(struct mlx5e_txqsq *sq)
{
	char err_str[MLX5E_TX_REPORTER_PER_SQ_MAX_LEN];
	struct mlx5e_tx_err_ctx err_ctx = {0};

	err_ctx.sq       = sq;
	err_ctx.recover  = mlx5e_tx_reporter_err_cqe_recover;
	sprintf(err_str, "ERR CQE on SQ: 0x%x", sq->sqn);

	devlink_health_report(sq->channel->priv->tx_reporter, err_str,
			      &err_ctx);
}

static int mlx5e_tx_reporter_timeout_recover(struct mlx5e_txqsq *sq)
{
	struct mlx5_eq_comp *eq = sq->cq.mcq.eq;
	u32 eqe_count;

	netdev_err(sq->channel->netdev, "EQ 0x%x: Cons = 0x%x, irqn = 0x%x\n",
		   eq->core.eqn, eq->core.cons_index, eq->core.irqn);

	eqe_count = mlx5_eq_poll_irq_disabled(eq);
	if (!eqe_count) {
		clear_bit(MLX5E_SQ_STATE_ENABLED, &sq->state);
		return 1;
	}

	netdev_err(sq->channel->netdev, "Recover %d eqes on EQ 0x%x\n",
		   eqe_count, eq->core.eqn);
	sq->channel->stats->eq_rearm++;
	return 0;
}

void mlx5e_tx_reporter_timeout(struct mlx5e_txqsq *sq)
{
	struct mlx5e_tx_err_ctx err_ctx;
	char err_str[MLX5E_TX_REPORTER_PER_SQ_MAX_LEN];

	err_ctx.sq       = sq;
	err_ctx.recover  = mlx5e_tx_reporter_timeout_recover;
	sprintf(err_str,
		"TX timeout on queue: %d, SQ: 0x%x, CQ: 0x%x, SQ Cons: 0x%x SQ Prod: 0x%x, usecs since last trans: %u\n",
		sq->channel->ix, sq->sqn, sq->cq.mcq.cqn, sq->cc, sq->pc,
		jiffies_to_usecs(jiffies - sq->txq->trans_start));
	devlink_health_report(sq->channel->priv->tx_reporter, err_str,
			      &err_ctx);
}

/* state lock cannot be grabbed within this function.
 * It can cause a dead lock or a read-after-free.
 */
int mlx5e_tx_reporter_recover_from_ctx(struct mlx5e_tx_err_ctx *err_ctx)
{
	return err_ctx->recover(err_ctx->sq);
}

static int mlx5e_tx_reporter_recover_all(struct mlx5e_priv *priv)
{
	int err;

	mutex_lock(&priv->state_lock);
	mlx5e_close_locked(priv->netdev);
	err = mlx5e_open_locked(priv->netdev);
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_tx_reporter_recover(struct devlink_health_reporter *reporter,
				     void *context)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_tx_err_ctx *err_ctx = context;

	return err_ctx ? mlx5e_tx_reporter_recover_from_ctx(err_ctx) :
			 mlx5e_tx_reporter_recover_all(priv);
}

static int
mlx5e_tx_reporter_build_diagnose_output(struct devlink_health_buffer *buffer,
					u32 sqn, u8 state, u8 stopped)
{
	int err, i;
	int nest = 0;
	char name[20];

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT);
	if (err)
		goto buffer_error;
	nest++;

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_PAIR);
	if (err)
		goto buffer_error;
	nest++;

	sprintf(name, "SQ 0x%x", sqn);
	err = devlink_health_buffer_put_object_name(buffer, name);
	if (err)
		goto buffer_error;

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_VALUE);
	if (err)
		goto buffer_error;
	nest++;

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT);
	if (err)
		goto buffer_error;
	nest++;

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_PAIR);
	if (err)
		goto buffer_error;
	nest++;

	err = devlink_health_buffer_put_object_name(buffer, "HW state");
	if (err)
		goto buffer_error;

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_VALUE);
	if (err)
		goto buffer_error;
	nest++;

	err = devlink_health_buffer_put_value_u8(buffer, state);
	if (err)
		goto buffer_error;

	devlink_health_buffer_nest_end(buffer); /* DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_VALUE */
	nest--;

	devlink_health_buffer_nest_end(buffer); /* DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_PAIR */
	nest--;

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_PAIR);
	if (err)
		goto buffer_error;
	nest++;

	err = devlink_health_buffer_put_object_name(buffer, "stopped");
	if (err)
		goto buffer_error;

	err = devlink_health_buffer_nest_start(buffer,
					       DEVLINK_ATTR_HEALTH_BUFFER_OBJECT_VALUE);
	if (err)
		goto buffer_error;
	nest++;

	err = devlink_health_buffer_put_value_u8(buffer, stopped);
	if (err)
		goto buffer_error;

	for (i = 0; i < nest; i++)
		devlink_health_buffer_nest_end(buffer);

	return 0;

buffer_error:
	for (i = 0; i < nest; i++)
		devlink_health_buffer_nest_cancel(buffer);
	return err;
}

static int mlx5e_tx_reporter_diagnose(struct devlink_health_reporter *reporter,
				      struct devlink_health_buffer **buffers_array,
				      unsigned int buffer_size,
				      unsigned int num_buffers)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	unsigned int buff = 0;
	int i = 0, err = 0;

	if (buffer_size < MLX5E_TX_REPORTER_PER_SQ_MAX_LEN)
		return -ENOMEM;

	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		mutex_unlock(&priv->state_lock);
		return 0;
	}

	while (i < priv->channels.num * priv->channels.params.num_tc) {
		struct mlx5e_txqsq *sq = priv->txq2sq[i];
		u8 state;

		err = mlx5_core_query_sq_state(priv->mdev, sq->sqn, &state);
		if (err)
			break;

		err = mlx5e_tx_reporter_build_diagnose_output(buffers_array[buff],
							      sq->sqn, state,
							      netif_xmit_stopped(sq->txq));
		if (err) {
			if (++buff == num_buffers)
				break;
		} else {
			i++;
		}
	}

	mutex_unlock(&priv->state_lock);
	return err;
}

static const struct devlink_health_reporter_ops mlx5_tx_reporter_ops = {
		.name = "TX",
		.recover = mlx5e_tx_reporter_recover,
		.diagnose_size = MLX5E_MAX_NUM_CHANNELS * MLX5E_MAX_NUM_TC *
				 MLX5E_TX_REPORTER_PER_SQ_MAX_LEN,
		.diagnose = mlx5e_tx_reporter_diagnose,
		.dump_size = 0,
		.dump = NULL,
};

#define MLX5_REPORTER_TX_GRACEFUL_PERIOD 500
int mlx5e_tx_reporter_create(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct devlink *devlink = priv_to_devlink(mdev);

	priv->tx_reporter =
		devlink_health_reporter_create(devlink, &mlx5_tx_reporter_ops,
					       MLX5_REPORTER_TX_GRACEFUL_PERIOD,
					       true, priv);
	return PTR_ERR_OR_ZERO(priv->tx_reporter);
}

void mlx5e_tx_reporter_destroy(struct mlx5e_priv *priv)
{
	devlink_health_reporter_destroy(priv->tx_reporter);
}
