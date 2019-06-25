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
				      struct devlink_fmsg *fmsg)
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

static const struct devlink_health_reporter_ops mlx5_rx_reporter_ops = {
	.name = "rx",
	.diagnose = mlx5e_rx_reporter_diagnose,
};

int mlx5e_reporter_rx_create(struct mlx5e_priv *priv)
{
	struct devlink *devlink = priv_to_devlink(priv->mdev);
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_create(devlink,
						  &mlx5_rx_reporter_ops,
						  0, false, priv);
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
