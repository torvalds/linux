/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Mellanox Technologies. */

#include "health.h"
#include "en/ptp.h"
#include "en/devlink.h"
#include "lib/tout.h"

/* Keep this string array consistent with the MLX5E_SQ_STATE_* enums in en.h */
static const char * const sq_sw_state_type_name[] = {
	[MLX5E_SQ_STATE_ENABLED] = "enabled",
	[MLX5E_SQ_STATE_MPWQE] = "mpwqe",
	[MLX5E_SQ_STATE_RECOVERING] = "recovering",
	[MLX5E_SQ_STATE_IPSEC] = "ipsec",
	[MLX5E_SQ_STATE_DIM] = "dim",
	[MLX5E_SQ_STATE_VLAN_NEED_L2_INLINE] = "vlan_need_l2_inline",
	[MLX5E_SQ_STATE_PENDING_XSK_TX] = "pending_xsk_tx",
	[MLX5E_SQ_STATE_PENDING_TLS_RX_RESYNC] = "pending_tls_rx_resync",
	[MLX5E_SQ_STATE_XDP_MULTIBUF] = "xdp_multibuf",
};

static int mlx5e_wait_for_sq_flush(struct mlx5e_txqsq *sq)
{
	struct mlx5_core_dev *dev = sq->mdev;
	unsigned long exp_time;

	exp_time = jiffies + msecs_to_jiffies(mlx5_tout_ms(dev, FLUSH_ON_ERROR));

	while (time_before(jiffies, exp_time)) {
		if (sq->cc == sq->pc)
			return 0;

		msleep(20);
	}

	netdev_err(sq->netdev,
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

static void mlx5e_health_sq_put_sw_state(struct devlink_fmsg *fmsg, struct mlx5e_txqsq *sq)
{
	int i;

	BUILD_BUG_ON_MSG(ARRAY_SIZE(sq_sw_state_type_name) != MLX5E_NUM_SQ_STATES,
			 "sq_sw_state_type_name string array must be consistent with MLX5E_SQ_STATE_* enum in en.h");
	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SW State");

	for (i = 0; i < ARRAY_SIZE(sq_sw_state_type_name); ++i)
		devlink_fmsg_u32_pair_put(fmsg, sq_sw_state_type_name[i],
					  test_bit(i, &sq->state));

	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

static int mlx5e_tx_reporter_err_cqe_recover(void *ctx)
{
	struct mlx5_core_dev *mdev;
	struct net_device *dev;
	struct mlx5e_txqsq *sq;
	u8 state;
	int err;

	sq = ctx;
	mdev = sq->mdev;
	dev = sq->netdev;

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

	err = mlx5e_health_sq_to_ready(mdev, dev, sq->sqn);
	if (err)
		goto out;

	mlx5e_reset_txqsq_cc_pc(sq);
	sq->stats->recover++;
	clear_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state);
	mlx5e_activate_txqsq(sq);
	if (sq->channel)
		mlx5e_trigger_napi_icosq(sq->channel);
	else
		mlx5e_trigger_napi_sched(sq->cq.napi);

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
	priv = sq->priv;
	err = mlx5e_health_channel_eq_recover(sq->netdev, eq, sq->cq.ch_stats);
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

static int mlx5e_tx_reporter_ptpsq_unhealthy_recover(void *ctx)
{
	struct mlx5e_ptpsq *ptpsq = ctx;
	struct mlx5e_channels *chs;
	struct net_device *netdev;
	struct mlx5e_priv *priv;
	int carrier_ok;
	int err;

	if (!test_bit(MLX5E_SQ_STATE_RECOVERING, &ptpsq->txqsq.state))
		return 0;

	priv = ptpsq->txqsq.priv;

	mutex_lock(&priv->state_lock);
	chs = &priv->channels;
	netdev = priv->netdev;

	carrier_ok = netif_carrier_ok(netdev);
	netif_carrier_off(netdev);

	mlx5e_deactivate_priv_channels(priv);

	mlx5e_ptp_close(chs->ptp);
	err = mlx5e_ptp_open(priv, &chs->params, chs->c[0]->lag_port, &chs->ptp);

	mlx5e_activate_priv_channels(priv);

	/* return carrier back if needed */
	if (carrier_ok)
		netif_carrier_on(netdev);

	mutex_unlock(&priv->state_lock);

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

static void
mlx5e_tx_reporter_build_diagnose_output_sq_common(struct devlink_fmsg *fmsg,
						  struct mlx5e_txqsq *sq, int tc)
{
	bool stopped = netif_xmit_stopped(sq->txq);
	u8 state;
	int err;

	devlink_fmsg_u32_pair_put(fmsg, "tc", tc);
	devlink_fmsg_u32_pair_put(fmsg, "txq ix", sq->txq_ix);
	devlink_fmsg_u32_pair_put(fmsg, "sqn", sq->sqn);

	err = mlx5_core_query_sq_state(sq->mdev, sq->sqn, &state);
	if (!err)
		devlink_fmsg_u8_pair_put(fmsg, "HW state", state);

	devlink_fmsg_bool_pair_put(fmsg, "stopped", stopped);
	devlink_fmsg_u32_pair_put(fmsg, "cc", sq->cc);
	devlink_fmsg_u32_pair_put(fmsg, "pc", sq->pc);
	mlx5e_health_sq_put_sw_state(fmsg, sq);
	mlx5e_health_cq_diag_fmsg(&sq->cq, fmsg);
	mlx5e_health_eq_diag_fmsg(sq->cq.mcq.eq, fmsg);
}

static void
mlx5e_tx_reporter_build_diagnose_output(struct devlink_fmsg *fmsg,
					struct mlx5e_txqsq *sq, int tc)
{
	devlink_fmsg_obj_nest_start(fmsg);
	devlink_fmsg_u32_pair_put(fmsg, "channel ix", sq->ch_ix);
	mlx5e_tx_reporter_build_diagnose_output_sq_common(fmsg, sq, tc);
	devlink_fmsg_obj_nest_end(fmsg);
}

static void
mlx5e_tx_reporter_build_diagnose_output_ptpsq(struct devlink_fmsg *fmsg,
					      struct mlx5e_ptpsq *ptpsq, int tc)
{
	devlink_fmsg_obj_nest_start(fmsg);
	devlink_fmsg_string_pair_put(fmsg, "channel", "ptp");
	mlx5e_tx_reporter_build_diagnose_output_sq_common(fmsg, &ptpsq->txqsq, tc);
	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "Port TS");
	mlx5e_health_cq_diag_fmsg(&ptpsq->ts_cq, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	devlink_fmsg_obj_nest_end(fmsg);
}

static void
mlx5e_tx_reporter_diagnose_generic_txqsq(struct devlink_fmsg *fmsg,
					 struct mlx5e_txqsq *txqsq)
{
	bool real_time =  mlx5_is_real_time_sq(txqsq->mdev);
	u32 sq_sz = mlx5_wq_cyc_get_size(&txqsq->wq);
	u32 sq_stride = MLX5_SEND_WQE_BB;

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SQ");
	devlink_fmsg_u64_pair_put(fmsg, "stride size", sq_stride);
	devlink_fmsg_u32_pair_put(fmsg, "size", sq_sz);
	devlink_fmsg_string_pair_put(fmsg, "ts_format", real_time ? "RT" : "FRC");
	mlx5e_health_cq_common_diag_fmsg(&txqsq->cq, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

static void
mlx5e_tx_reporter_diagnose_generic_tx_port_ts(struct devlink_fmsg *fmsg,
					      struct mlx5e_ptpsq *ptpsq)
{
	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "Port TS");
	mlx5e_health_cq_common_diag_fmsg(&ptpsq->ts_cq, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

static void
mlx5e_tx_reporter_diagnose_common_config(struct devlink_health_reporter *reporter,
					 struct devlink_fmsg *fmsg)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_txqsq *generic_sq = priv->txq2sq[0];
	struct mlx5e_ptp *ptp_ch = priv->channels.ptp;
	struct mlx5e_ptpsq *generic_ptpsq;

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "Common Config");
	mlx5e_tx_reporter_diagnose_generic_txqsq(fmsg, generic_sq);

	if (!ptp_ch || !test_bit(MLX5E_PTP_STATE_TX, ptp_ch->state))
		goto out;

	generic_ptpsq = &ptp_ch->ptpsq[0];
	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "PTP");
	mlx5e_tx_reporter_diagnose_generic_txqsq(fmsg, &generic_ptpsq->txqsq);
	mlx5e_tx_reporter_diagnose_generic_tx_port_ts(fmsg, generic_ptpsq);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
out:
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
}

static int mlx5e_tx_reporter_diagnose(struct devlink_health_reporter *reporter,
				      struct devlink_fmsg *fmsg,
				      struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = devlink_health_reporter_priv(reporter);
	struct mlx5e_ptp *ptp_ch = priv->channels.ptp;

	int i, tc;

	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	mlx5e_tx_reporter_diagnose_common_config(reporter, fmsg);
	devlink_fmsg_arr_pair_nest_start(fmsg, "SQs");

	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		for (tc = 0; tc < mlx5e_get_dcb_num_tc(&priv->channels.params); tc++) {
			struct mlx5e_txqsq *sq = &c->sq[tc];

			mlx5e_tx_reporter_build_diagnose_output(fmsg, sq, tc);
		}
	}

	if (!ptp_ch || !test_bit(MLX5E_PTP_STATE_TX, ptp_ch->state))
		goto close_sqs_nest;

	for (tc = 0; tc < mlx5e_get_dcb_num_tc(&priv->channels.params); tc++)
		mlx5e_tx_reporter_build_diagnose_output_ptpsq(fmsg,
							      &ptp_ch->ptpsq[tc],
							      tc);

close_sqs_nest:
	devlink_fmsg_arr_pair_nest_end(fmsg);
unlock:
	mutex_unlock(&priv->state_lock);
	return 0;
}

static int mlx5e_tx_reporter_dump_sq(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
				     void *ctx)
{
	struct mlx5_rsc_key key = {};
	struct mlx5e_txqsq *sq = ctx;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SX Slice");
	key.size = PAGE_SIZE;
	key.rsc = MLX5_SGMT_TYPE_SX_SLICE_ALL;
	mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SQ");
	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "QPC");
	key.rsc = MLX5_SGMT_TYPE_FULL_QPC;
	key.index1 = sq->sqn;
	key.num_of_obj1 = 1;
	mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "send_buff");
	key.rsc = MLX5_SGMT_TYPE_SND_BUFF;
	key.num_of_obj2 = MLX5_RSC_DUMP_ALL;
	mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);

	mlx5e_health_fmsg_named_obj_nest_end(fmsg);

	return 0;
}

static int mlx5e_tx_reporter_timeout_dump(struct mlx5e_priv *priv, struct devlink_fmsg *fmsg,
					  void *ctx)
{
	struct mlx5e_tx_timeout_ctx *to_ctx = ctx;

	return mlx5e_tx_reporter_dump_sq(priv, fmsg, to_ctx->sq);
}

static int mlx5e_tx_reporter_ptpsq_unhealthy_dump(struct mlx5e_priv *priv,
						  struct devlink_fmsg *fmsg,
						  void *ctx)
{
	struct mlx5e_ptpsq *ptpsq = ctx;

	return mlx5e_tx_reporter_dump_sq(priv, fmsg, &ptpsq->txqsq);
}

static int mlx5e_tx_reporter_dump_all_sqs(struct mlx5e_priv *priv,
					  struct devlink_fmsg *fmsg)
{
	struct mlx5e_ptp *ptp_ch = priv->channels.ptp;
	struct mlx5_rsc_key key = {};
	int i, tc;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	mlx5e_health_fmsg_named_obj_nest_start(fmsg, "SX Slice");
	key.size = PAGE_SIZE;
	key.rsc = MLX5_SGMT_TYPE_SX_SLICE_ALL;
	mlx5e_health_rsc_fmsg_dump(priv, &key, fmsg);
	mlx5e_health_fmsg_named_obj_nest_end(fmsg);
	devlink_fmsg_arr_pair_nest_start(fmsg, "SQs");

	for (i = 0; i < priv->channels.num; i++) {
		struct mlx5e_channel *c = priv->channels.c[i];

		for (tc = 0; tc < mlx5e_get_dcb_num_tc(&priv->channels.params); tc++) {
			struct mlx5e_txqsq *sq = &c->sq[tc];

			mlx5e_health_queue_dump(priv, fmsg, sq->sqn, "SQ");
		}
	}

	if (ptp_ch && test_bit(MLX5E_PTP_STATE_TX, ptp_ch->state)) {
		for (tc = 0; tc < mlx5e_get_dcb_num_tc(&priv->channels.params); tc++) {
			struct mlx5e_txqsq *sq = &ptp_ch->ptpsq[tc].txqsq;

			mlx5e_health_queue_dump(priv, fmsg, sq->sqn, "PTP SQ");
		}
	}

	devlink_fmsg_arr_pair_nest_end(fmsg);
	return 0;
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
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_priv *priv = sq->priv;
	struct mlx5e_err_ctx err_ctx = {};

	err_ctx.ctx = sq;
	err_ctx.recover = mlx5e_tx_reporter_err_cqe_recover;
	err_ctx.dump = mlx5e_tx_reporter_dump_sq;
	snprintf(err_str, sizeof(err_str), "ERR CQE on SQ: 0x%x", sq->sqn);

	mlx5e_health_report(priv, priv->tx_reporter, err_str, &err_ctx);
}

int mlx5e_reporter_tx_timeout(struct mlx5e_txqsq *sq)
{
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_tx_timeout_ctx to_ctx = {};
	struct mlx5e_priv *priv = sq->priv;
	struct mlx5e_err_ctx err_ctx = {};

	to_ctx.sq = sq;
	err_ctx.ctx = &to_ctx;
	err_ctx.recover = mlx5e_tx_reporter_timeout_recover;
	err_ctx.dump = mlx5e_tx_reporter_timeout_dump;
	snprintf(err_str, sizeof(err_str),
		 "TX timeout on queue: %d, SQ: 0x%x, CQ: 0x%x, SQ Cons: 0x%x SQ Prod: 0x%x, usecs since last trans: %u",
		 sq->ch_ix, sq->sqn, sq->cq.mcq.cqn, sq->cc, sq->pc,
		 jiffies_to_usecs(jiffies - READ_ONCE(sq->txq->trans_start)));

	mlx5e_health_report(priv, priv->tx_reporter, err_str, &err_ctx);
	return to_ctx.status;
}

void mlx5e_reporter_tx_ptpsq_unhealthy(struct mlx5e_ptpsq *ptpsq)
{
	struct mlx5e_ptp_metadata_map *map = &ptpsq->metadata_map;
	char err_str[MLX5E_REPORTER_PER_Q_MAX_LEN];
	struct mlx5e_txqsq *txqsq = &ptpsq->txqsq;
	struct mlx5e_cq *ts_cq = &ptpsq->ts_cq;
	struct mlx5e_priv *priv = txqsq->priv;
	struct mlx5e_err_ctx err_ctx = {};

	err_ctx.ctx = ptpsq;
	err_ctx.recover = mlx5e_tx_reporter_ptpsq_unhealthy_recover;
	err_ctx.dump = mlx5e_tx_reporter_ptpsq_unhealthy_dump;
	snprintf(err_str, sizeof(err_str),
		 "Unhealthy TX port TS queue: %d, SQ: 0x%x, CQ: 0x%x, Undelivered CQEs: %u Map Capacity: %u",
		 txqsq->ch_ix, txqsq->sqn, ts_cq->mcq.cqn, map->undelivered_counter, map->capacity);

	mlx5e_health_report(priv, priv->tx_reporter, err_str, &err_ctx);
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

	reporter = devlink_port_health_reporter_create(priv->netdev->devlink_port,
						       &mlx5_tx_reporter_ops,
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

	devlink_health_reporter_destroy(priv->tx_reporter);
	priv->tx_reporter = NULL;
}
