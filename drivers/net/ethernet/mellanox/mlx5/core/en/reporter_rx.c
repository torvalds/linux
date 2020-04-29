// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Mellanox Technologies.

#include "health.h"
#include "params.h"

static int mlx5e_query_rq_state(struct mlx5_core_dev *dev, u32 rqn, u8 *state)
{
	int outlen = MLX5_ST_SZ_BYTES(query_rq_out);
	void *out;
	void *rqc;
	int err;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_query_rq(dev, rqn, out);
	if (err)
		goto out;

	rqc = MLX5_ADDR_OF(query_rq_out, out, rq_context);
	*state = MLX5_GET(rqc, rqc, state);

out:
	kvfree(out);
	return err;
}

static int mlx5e_wait_for_icosq_flush(struct mlx5e_icosq *icosq)
{
	unsigned long exp_time = jiffies + msecs_to_jiffies(2000);

	while (time_before(jiffies, exp_time)) {
		if (icosq->cc == icosq->pc)
			return 0;

		msleep(20);
	}

	netdev_err(icosq->channel->netdev,
		   "Wait for ICOSQ 0x%x flush timeout (cc = 0x%x, pc = 0x%x)\n",
		   icosq->sqn, icosq->cc, icosq->pc);

	return -ETIMEDOUT;
}

static void mlx5e_reset_icosq_cc_pc(struct mlx5e_icosq *icosq)
{
	WARN_ONCE(icosq->cc != icosq->pc, "ICOSQ 0x%x: cc (0x%x) != pc (0x%x)\n",
		  icosq->sqn, icosq->cc, icosq->pc);
	icosq->cc = 0;
	icosq->pc = 0;
}

static int mlx5e_rx_reporter_err_icosq_cqe_recover(void *ctx)
{
	struct mlx5_core_dev *mdev;
	struct mlx5e_icosq *icosq;
	struct net_device *dev;
	struct mlx5e_rq *rq;
	u8 state;
	int err;

	icosq = ctx;
	rq = &icosq->channel->rq;
	mdev = icosq->channel->mdev;
	dev = icosq->channel->netdev;
	err = mlx5_core_query_sq_state(mdev, icosq->sqn, &state);
	if (err) {
		netdev_err(dev, "Failed to query ICOSQ 0x%x state. err = %d\n",
			   icosq->sqn, err);
		goto out;
	}

	if (state != MLX5_SQC_STATE_ERR)
		goto out;

	mlx5e_deactivate_rq(rq);
	err = mlx5e_wait_for_icosq_flush(icosq);
	if (err)
		goto out;

	mlx5e_deactivate_icosq(icosq);

	/* At this point, both the rq and the icosq are disabled */

	err = mlx5e_health_sq_to_ready(icosq->channel, icosq->sqn);
	if (err)
		goto out;

	mlx5e_reset_icosq_cc_pc(icosq);
	mlx5e_free_rx_in_progress_descs(rq);
	clear_bit(MLX5E_SQ_STATE_RECOVERING, &icosq->state);
	mlx5e_activate_icosq(icosq);
	mlx5e_activate_rq(rq);

	rq->stats->recover++;
	return 0;
out:
	clear_bit(MLX5E_SQ_STATE_RECOVERING, &icosq->state);
	return err;
}

static int mlx5e_rq_to_ready(struct mlx5e_rq *rq, int curr_state)
{
	struct net_device *dev = rq->netdev;
	int err;

	err = mlx5e_modify_rq_state(rq, curr_state, MLX5_RQC_STATE_RST);
	if (err) {
		netdev_err(dev, "Failed to move rq 0x%x to reset\n", rq->rqn);
		return err;
	}
	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err) {
		netdev_err(dev, "Failed to move rq 0x%x to ready\n", rq->rqn);
		return err;
	}

	return 0;
}

static int mlx5e_rx_reporter_err_rq_cqe_recover(void *ctx)
{
	struct mlx5_core_dev *mdev;
	struct net_device *dev;
	struct mlx5e_rq *rq;
	u8 state;
	int err;

	rq = ctx;
	mdev = rq->mdev;
	dev = rq->netdev;
	err = mlx5e_query_rq_state(mdev, rq->rqn, &state);
	if (err) {
		netdev_err(dev, "Failed to query RQ 0x%x state. err = %d\n",
			   rq->rqn, err);
		goto out;
	}

	if (state != MLX5_RQC_STATE_ERR)
		goto out;

	mlx5e_deactivate_rq(rq);
	mlx5e_free_rx_descs(rq);

	err = mlx5e_rq_to_ready(rq, MLX5_RQC_STATE_ERR);
	if (err)
		goto out;

	clear_bit(MLX5E_RQ_STATE_RECOVERING, &rq->state);
	mlx5e_activate_rq(rq);
	rq->stats->recover++;
	return 0;
out:
	clear_bit(MLX5E_RQ_STATE_RECOVERING, &rq->state);
	return err;
}

static int mlx5e_rx_reporter_timeout_recover(void *ctx)
{
	struct mlx5e_icosq *icosq;
	struct mlx5_eq_comp *eq;
	struct mlx5e_rq *rq;
	int err;

	rq = ctx;
	icosq = &rq->channel->icosq;
	eq = rq->cq.mcq.eq;
	err = mlx5e_health_channel_eq_recover(eq, rq->channel);
	if (err)
		clear_bit(MLX5E_SQ_STATE_ENABLED, &icosq->state);

	return err;
}

static int mlx5e_rx_reporter_recover_from_ctx(struct mlx5e_err_ctx *err_ctx)
{
	return err_ctx->recover(err_ctx->ctx);
}

static int mlx5e_rx_reporter_recover(struct devlink_health_reporter *reporter,
				     void *context,
				     struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_err_ctx *err_ctx = context;

	return err_ctx ? mlx5e_rx_reporter_recover_from_ctx(err_ctx) :
			 mlx5e_health_recover_channels(priv);
}

static int mlx5e_rx_reporter_build_diagnose_output(struct mlx5e_rq *rq,
						   struct devlink_fmsg *fmsg)
{
	struct mlx5e_priv *priv = rq->channel->priv;
	struct mlx5e_params *params;
	struct mlx5e_icosq *icosq;
	u8 icosq_hw_state;
	int wqes_sz;
	u8 hw_state;
	u16 wq_head;
	int err;

	params = &priv->channels.params;
	icosq = &rq->channel->icosq;
	err = mlx5e_query_rq_state(priv->mdev, rq->rqn, &hw_state);
	if (err)
		return err;

	err = mlx5_core_query_sq_state(priv->mdev, icosq->sqn, &icosq_hw_state);
	if (err)
		return err;

	wqes_sz = mlx5e_rqwq_get_cur_sz(rq);
	wq_head = params->rq_wq_type == MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ ?
		  rq->mpwqe.wq.head : mlx5_wq_cyc_get_head(&rq->wqe.wq);

	err = devlink_fmsg_obj_nest_start(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "channel ix", rq->channel->ix);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "rqn", rq->rqn);
	if (err)
		return err;

	err = devlink_fmsg_u8_pair_put(fmsg, "HW state", hw_state);
	if (err)
		return err;

	err = devlink_fmsg_u8_pair_put(fmsg, "SW state", rq->state);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "posted WQEs", wqes_sz);
	if (err)
		return err;

	err = devlink_fmsg_u32_pair_put(fmsg, "cc", wq_head);
	if (err)
		return err;

	err = devlink_fmsg_u8_pair_put(fmsg, "ICOSQ HW state", icosq_hw_state);
	if (err)
		return err;

	err = mlx5e_reporter_cq_diagnose(&rq->cq, fmsg);
	if (err)
		return err;

	err = devlink_fmsg_obj_nest_end(fmsg);
	if (err)
		return err;

	return 0;
}

static int mlx5e_rx_reporter_diagnose(struct devlink_health_reporter *reporter,
				      struct devlink_fmsg *fmsg,
				      struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5e_rq *generic_rq;
	u32 rq_stride, rq_sz;
	int i, err = 0;

	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	generic_rq = &priv->channels.c[0]->rq;
	rq_sz = mlx5e_rqwq_get_size(generic_rq);
	rq_stride = BIT(mlx5e_mpwqe_get_log_stride_size(priv->mdev, params, NULL));

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "Common config");
	if (err)
		goto unlock;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "RQ");
	if (err)
		goto unlock;

	err = devlink_fmsg_u8_pair_put(fmsg, "type", params->rq_wq_type);
	if (err)
		goto unlock;

	err = devlink_fmsg_u64_pair_put(fmsg, "stride size", rq_stride);
	if (err)
		goto unlock;

	err = devlink_fmsg_u32_pair_put(fmsg, "size", rq_sz);
	if (err)
		goto unlock;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		goto unlock;

	err = mlx5e_reporter_cq_common_diagnose(&generic_rq->cq, fmsg);
	if (err)
		goto unlock;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		goto unlock;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "RQs");
	if (err)
		goto unlock;

	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_rq *rq = &priv->channels.c[i]->rq;

		err = mlx5e_rx_reporter_build_diagnose_output(rq, fmsg);
		if (err)
			goto unlock;
	}
	err = devlink_fmsg_arr_pair_nest_end(fmsg);
	if (err)
		goto unlock;
unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_rx_reporter_dump_icosq(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
					void *ctx)
{
	struct mlx5e_txqsq *icosq = ctx;
	struct mlx5_rsc_key key = {};
	int err;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "SX Slice");
	if (err)
		return err;

	key.size = PAGE_SIZE;
	key.rsc = MLX5_SGMT_TYPE_SX_SLICE_ALL;
	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "ICOSQ");
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "QPC");
	if (err)
		return err;

	key.rsc = MLX5_SGMT_TYPE_FULL_QPC;
	key.index1 = icosq->sqn;
	key.num_of_obj1 = 1;

	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "send_buff");
	if (err)
		return err;

	key.rsc = MLX5_SGMT_TYPE_SND_BUFF;
	key.num_of_obj2 = MLX5_RSC_DUMP_ALL;

	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	return mlx5e_reporter_named_obj_nest_end(fmsg);
}

static int mlx5e_rx_reporter_dump_rq(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
				     void *ctx)
{
	struct mlx5_rsc_key key = {};
	struct mlx5e_rq *rq = ctx;
	int err;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "RX Slice");
	if (err)
		return err;

	key.size = PAGE_SIZE;
	key.rsc = MLX5_SGMT_TYPE_RX_SLICE_ALL;
	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "RQ");
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "QPC");
	if (err)
		return err;

	key.rsc = MLX5_SGMT_TYPE_FULL_QPC;
	key.index1 = rq->rqn;
	key.num_of_obj1 = 1;

	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "receive_buff");
	if (err)
		return err;

	key.rsc = MLX5_SGMT_TYPE_RCV_BUFF;
	key.num_of_obj2 = MLX5_RSC_DUMP_ALL;
	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	return mlx5e_reporter_named_obj_nest_end(fmsg);
}

static int mlx5e_rx_reporter_dump_all_rqs(struct mlx5e_priv *priv,
					  struct devlink_fmsg *fmsg)
{
	struct mlx5_rsc_key key = {};
	int i, err;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	err = mlx5e_reporter_named_obj_nest_start(fmsg, "RX Slice");
	if (err)
		return err;

	key.size = PAGE_SIZE;
	key.rsc = MLX5_SGMT_TYPE_RX_SLICE_ALL;
	err = mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	if (err)
		return err;

	err = mlx5e_reporter_named_obj_nest_end(fmsg);
	if (err)
		return err;

	err = devlink_fmsg_arr_pair_nest_start(fmsg, "RQs");
	if (err)
		return err;

	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_rq *rq = &priv->channels.c[i]->rq;

		err = mlx5e_health_queue_dump(priv, fmsg, rq->rqn, "RQ");
		if (err)
			return err;
	}

	return devlink_fmsg_arr_pair_nest_end(fmsg);
}

static int mlx5e_rx_reporter_dump_from_ctx(struct mlx5e_priv *priv,
					   struct mlx5e_err_ctx *err_ctx,
					   struct devlink_fmsg *fmsg)
{
	return err_ctx->dump(priv, fmsg, err_ctx->ctx);
}

static int mlx5e_rx_reporter_dump(struct devlink_health_reporter *reporter,
				  struct devlink_fmsg *fmsg, void *context,
				  struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_err_ctx *err_ctx = context;

	return err_ctx ? mlx5e_rx_reporter_dump_from_ctx(priv, err_ctx, fmsg) :
			 mlx5e_rx_reporter_dump_all_rqs(priv, fmsg);
}

void mlx5e_reporter_rx_timeout(struct mlx5e_rq *rq)
{
	struct mlx5e_icosq *icosq = &rq->channel->icosq;
	struct mlx5e_priv *priv = rq->channel->priv;
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_err_ctx err_ctx = {};

	err_ctx.ctx = rq;
	err_ctx.recover = mlx5e_rx_reporter_timeout_recover;
	err_ctx.dump = mlx5e_rx_reporter_dump_rq;
	snprintf(err_str, sizeof(err_str),
		 "RX timeout on channel: %d, ICOSQ: 0x%x RQ: 0x%x, CQ: 0x%x",
		 icosq->channel->ix, icosq->sqn, rq->rqn, rq->cq.mcq.cqn);

	mlx5e_health_report(priv, priv->rx_reporter, err_str, &err_ctx);
}

void mlx5e_reporter_rq_cqe_err(struct mlx5e_rq *rq)
{
	struct mlx5e_priv *priv = rq->channel->priv;
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_err_ctx err_ctx = {};

	err_ctx.ctx = rq;
	err_ctx.recover = mlx5e_rx_reporter_err_rq_cqe_recover;
	err_ctx.dump = mlx5e_rx_reporter_dump_rq;
	snprintf(err_str, sizeof(err_str), "ERR CQE on RQ: 0x%x", rq->rqn);

	mlx5e_health_report(priv, priv->rx_reporter, err_str, &err_ctx);
}

void mlx5e_reporter_icosq_cqe_err(struct mlx5e_icosq *icosq)
{
	struct mlx5e_priv *priv = icosq->channel->priv;
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_err_ctx err_ctx = {};

	err_ctx.ctx = icosq;
	err_ctx.recover = mlx5e_rx_reporter_err_icosq_cqe_recover;
	err_ctx.dump = mlx5e_rx_reporter_dump_icosq;
	snprintf(err_str, sizeof(err_str), "ERR CQE on ICOSQ: 0x%x", icosq->sqn);

	mlx5e_health_report(priv, priv->rx_reporter, err_str, &err_ctx);
}

static const struct devlink_health_reporter_ops mlx5_rx_reporter_ops = {
	.name = "rx",
	.recover = mlx5e_rx_reporter_recover,
	.diagnose = mlx5e_rx_reporter_diagnose,
	.dump = mlx5e_rx_reporter_dump,
};

#define MLX5E_REPORTER_RX_GRACEFUL_PERIOD 500

int mlx5e_reporter_rx_create(struct mlx5e_priv *priv)
{
	struct devlink *devlink = priv_to_devlink(priv->mdev);
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_create(devlink,
						  &mlx5_rx_reporter_ops,
						  MLX5E_REPORTER_RX_GRACEFUL_PERIOD,
						  priv);
	if (IS_ERR(reporter)) {
		netdev_warn(priv->netdev, "Failed to create rx reporter, err = %ld\n",
			    PTR_ERR(reporter));
		return PTR_ERR(reporter);
	}
	priv->rx_reporter = reporter;
	return 0;
}

void mlx5e_reporter_rx_destroy(struct mlx5e_priv *priv)
{
	if (!priv->rx_reporter)
		return;

	devlink_health_reporter_destroy(priv->rx_reporter);
}
