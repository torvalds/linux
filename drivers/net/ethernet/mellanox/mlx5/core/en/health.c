// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Mellanox Technologies.

#include "health.h"
#include "lib/eq.h"
#include "lib/mlx5.h"

void mlx5e_health_fmsg_named_obj_nest_start(struct devlink_fmsg *fmsg, char *name)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_obj_nest_start(fmsg);
}

void mlx5e_health_fmsg_named_obj_nest_end(struct devlink_fmsg *fmsg)
{
	devlink_fmsg_obj_nest_end(fmsg);
	devlink_fmsg_pair_nest_end(fmsg);
}

void mlx5e_health_cq_diag_fmsg(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg)
{
	u32 out[MLX5_ST_SZ_DW(query_cq_out)] = {};
	u8 hw_status;
	void *cqc;

	mlx5_core_query_cq(cq->mdev, &cq->mcq, out);
	cqc = MLX5_ADDR_OF(query_cq_out, out, cq_context);
	hw_status = MLX5_GET(cqc, cqc, status);

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "CQ");
	devlink_fmsg_u32_pair_put(fmsg, "cqn", cq->mcq.cqn);
	devlink_fmsg_u8_pair_put(fmsg, "HW status", hw_status);
	devlink_fmsg_u32_pair_put(fmsg, "ci", mlx5_cqwq_get_ci(&cq->wq));
	devlink_fmsg_u32_pair_put(fmsg, "size", mlx5_cqwq_get_size(&cq->wq));
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

void mlx5e_health_cq_common_diag_fmsg(struct mlx5e_cq *cq, struct devlink_fmsg *fmsg)
{
	u8 cq_log_stride;
	u32 cq_sz;

	cq_sz = mlx5_cqwq_get_size(&cq->wq);
	cq_log_stride = mlx5_cqwq_get_log_stride_size(&cq->wq);

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "CQ");
	devlink_fmsg_u64_pair_put(fmsg, "stride size", BIT(cq_log_stride));
	devlink_fmsg_u32_pair_put(fmsg, "size", cq_sz);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

void mlx5e_health_eq_diag_fmsg(struct mlx5_eq_comp *eq, struct devlink_fmsg *fmsg)
{
	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "EQ");
	devlink_fmsg_u8_pair_put(fmsg, "eqn", eq->core.eqn);
	devlink_fmsg_u32_pair_put(fmsg, "irqn", eq->core.irqn);
	devlink_fmsg_u32_pair_put(fmsg, "vecidx", eq->core.vecidx);
	devlink_fmsg_u32_pair_put(fmsg, "ci", eq->core.cons_index);
	devlink_fmsg_u32_pair_put(fmsg, "size", eq_get_size(&eq->core));
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

void mlx5e_health_create_reporters(struct mlx5e_priv *priv)
{
	mlx5e_reporter_tx_create(priv);
	mlx5e_reporter_rx_create(priv);
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

int mlx5e_health_sq_to_ready(struct mlx5_core_dev *mdev, struct net_device *dev, u32 sqn)
{
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

int mlx5e_health_channel_eq_recover(struct net_device *dev, struct mlx5_eq_comp *eq,
				    struct mlx5e_ch_stats *stats)
{
	u32 eqe_count;

	netdev_err(dev, "EQ 0x%x: Cons = 0x%x, irqn = 0x%x\n",
		   eq->core.eqn, eq->core.cons_index, eq->core.irqn);

	eqe_count = mlx5_eq_poll_irq_disabled(eq);
	if (!eqe_count)
		return -EIO;

	netdev_err(dev, "Recovered %d eqes on EQ 0x%x\n",
		   eqe_count, eq->core.eqn);

	stats->eq_rearm++;
	return 0;
}

int mlx5e_health_report(struct mlx5e_priv *priv,
			struct devlink_health_reporter *reporter, char *err_str,
			struct mlx5e_err_ctx *err_ctx)
{
	netdev_err(priv->netdev, "%s\n", err_str);

	if (!reporter)
		return err_ctx->recover(err_ctx->ctx);

	return devlink_health_report(reporter, err_str, err_ctx);
}

#define MLX5_HEALTH_DEVLINK_MAX_SIZE 1024
static void mlx5e_health_rsc_fmsg_binary(struct devlink_fmsg *fmsg,
					 const void *value, u32 value_len)

{
	u32 data_size;
	u32 offset;

	for (offset = 0; offset < value_len; offset += data_size) {
		data_size = value_len - offset;
		if (data_size > MLX5_HEALTH_DEVLINK_MAX_SIZE)
			data_size = MLX5_HEALTH_DEVLINK_MAX_SIZE;
		devlink_fmsg_binary_put(fmsg, value + offset, data_size);
	}
}

int mlx5e_health_rsc_fmsg_dump(struct mlx5e_priv *priv, struct mlx5_rsc_key *key,
			       struct devlink_fmsg *fmsg)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_rsc_dump_cmd *cmd;
	int cmd_err, err = 0;
	struct page *page;
	int size;

	if (IS_ERR_OR_NULL(mdev->rsc_dump))
		return -EOPNOTSUPP;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	devlink_fmsg_binary_pair_nest_start(fmsg, "data");

	cmd = mlx5_rsc_dump_cmd_create(mdev, key);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto free_page;
	}

	do {
		cmd_err = mlx5_rsc_dump_next(mdev, cmd, page, &size);
		if (cmd_err < 0) {
			err = cmd_err;
			goto destroy_cmd;
		}

		mlx5e_health_rsc_fmsg_binary(fmsg, page_address(page), size);
	} while (cmd_err > 0);

destroy_cmd:
	mlx5_rsc_dump_cmd_destroy(cmd);
	devlink_fmsg_binary_pair_nest_end(fmsg);
free_page:
	__free_page(page);
	return err;
}

void mlx5e_health_queue_dump(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
			     int queue_idx, char *lbl)
{
	struct mlx5_rsc_key key = {};

	key.rsc = MLX5_SGMT_TYPE_FULL_QPC;
	key.index1 = queue_idx;
	key.size = PAGE_SIZE;
	key.num_of_obj1 = 1;

	devlink_fmsg_obj_nest_start(fmsg);
	mlx5e_health_fmsg_named_obj_nest_start(fmsg, lbl);
	devlink_fmsg_u32_pair_put(fmsg, "index", queue_idx);
	mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	devlink_fmsg_obj_nest_end(fmsg);
}
