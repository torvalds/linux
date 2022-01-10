// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2020 Mellanox Technologies

#include "en/ptp.h"
#include "en/txrx.h"
#include "en/params.h"
#include "en/fs_tt_redirect.h"

struct mlx5e_ptp_fs {
	struct mlx5_flow_handle *l2_rule;
	struct mlx5_flow_handle *udp_v4_rule;
	struct mlx5_flow_handle *udp_v6_rule;
	bool valid;
};

struct mlx5e_ptp_params {
	struct mlx5e_params params;
	struct mlx5e_sq_param txq_sq_param;
	struct mlx5e_rq_param rq_param;
};

struct mlx5e_skb_cb_hwtstamp {
	ktime_t cqe_hwtstamp;
	ktime_t port_hwtstamp;
};

void mlx5e_skb_cb_hwtstamp_init(struct sk_buff *skb)
{
	memset(skb->cb, 0, sizeof(struct mlx5e_skb_cb_hwtstamp));
}

static struct mlx5e_skb_cb_hwtstamp *mlx5e_skb_cb_get_hwts(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct mlx5e_skb_cb_hwtstamp) > sizeof(skb->cb));
	return (struct mlx5e_skb_cb_hwtstamp *)skb->cb;
}

static void mlx5e_skb_cb_hwtstamp_tx(struct sk_buff *skb,
				     struct mlx5e_ptp_cq_stats *cq_stats)
{
	struct skb_shared_hwtstamps hwts = {};
	ktime_t diff;

	diff = abs(mlx5e_skb_cb_get_hwts(skb)->port_hwtstamp -
		   mlx5e_skb_cb_get_hwts(skb)->cqe_hwtstamp);

	/* Maximal allowed diff is 1 / 128 second */
	if (diff > (NSEC_PER_SEC >> 7)) {
		cq_stats->abort++;
		cq_stats->abort_abs_diff_ns += diff;
		return;
	}

	hwts.hwtstamp = mlx5e_skb_cb_get_hwts(skb)->port_hwtstamp;
	skb_tstamp_tx(skb, &hwts);
}

void mlx5e_skb_cb_hwtstamp_handler(struct sk_buff *skb, int hwtstamp_type,
				   ktime_t hwtstamp,
				   struct mlx5e_ptp_cq_stats *cq_stats)
{
	switch (hwtstamp_type) {
	case (MLX5E_SKB_CB_CQE_HWTSTAMP):
		mlx5e_skb_cb_get_hwts(skb)->cqe_hwtstamp = hwtstamp;
		break;
	case (MLX5E_SKB_CB_PORT_HWTSTAMP):
		mlx5e_skb_cb_get_hwts(skb)->port_hwtstamp = hwtstamp;
		break;
	}

	/* If both CQEs arrive, check and report the port tstamp, and clear skb cb as
	 * skb soon to be released.
	 */
	if (!mlx5e_skb_cb_get_hwts(skb)->cqe_hwtstamp ||
	    !mlx5e_skb_cb_get_hwts(skb)->port_hwtstamp)
		return;

	mlx5e_skb_cb_hwtstamp_tx(skb, cq_stats);
	memset(skb->cb, 0, sizeof(struct mlx5e_skb_cb_hwtstamp));
}

#define PTP_WQE_CTR2IDX(val) ((val) & ptpsq->ts_cqe_ctr_mask)

static bool mlx5e_ptp_ts_cqe_drop(struct mlx5e_ptpsq *ptpsq, u16 skb_cc, u16 skb_id)
{
	return (ptpsq->ts_cqe_ctr_mask && (skb_cc != skb_id));
}

static void mlx5e_ptp_skb_fifo_ts_cqe_resync(struct mlx5e_ptpsq *ptpsq, u16 skb_cc, u16 skb_id)
{
	struct skb_shared_hwtstamps hwts = {};
	struct sk_buff *skb;

	ptpsq->cq_stats->resync_event++;

	while (skb_cc != skb_id) {
		skb = mlx5e_skb_fifo_pop(&ptpsq->skb_fifo);
		hwts.hwtstamp = mlx5e_skb_cb_get_hwts(skb)->cqe_hwtstamp;
		skb_tstamp_tx(skb, &hwts);
		ptpsq->cq_stats->resync_cqe++;
		skb_cc = PTP_WQE_CTR2IDX(ptpsq->skb_fifo_cc);
	}
}

static void mlx5e_ptp_handle_ts_cqe(struct mlx5e_ptpsq *ptpsq,
				    struct mlx5_cqe64 *cqe,
				    int budget)
{
	u16 skb_id = PTP_WQE_CTR2IDX(be16_to_cpu(cqe->wqe_counter));
	u16 skb_cc = PTP_WQE_CTR2IDX(ptpsq->skb_fifo_cc);
	struct mlx5e_txqsq *sq = &ptpsq->txqsq;
	struct sk_buff *skb;
	ktime_t hwtstamp;

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		skb = mlx5e_skb_fifo_pop(&ptpsq->skb_fifo);
		ptpsq->cq_stats->err_cqe++;
		goto out;
	}

	if (mlx5e_ptp_ts_cqe_drop(ptpsq, skb_cc, skb_id))
		mlx5e_ptp_skb_fifo_ts_cqe_resync(ptpsq, skb_cc, skb_id);

	skb = mlx5e_skb_fifo_pop(&ptpsq->skb_fifo);
	hwtstamp = mlx5e_cqe_ts_to_ns(sq->ptp_cyc2time, sq->clock, get_cqe_ts(cqe));
	mlx5e_skb_cb_hwtstamp_handler(skb, MLX5E_SKB_CB_PORT_HWTSTAMP,
				      hwtstamp, ptpsq->cq_stats);
	ptpsq->cq_stats->cqe++;

out:
	napi_consume_skb(skb, budget);
}

static bool mlx5e_ptp_poll_ts_cq(struct mlx5e_cq *cq, int budget)
{
	struct mlx5e_ptpsq *ptpsq = container_of(cq, struct mlx5e_ptpsq, ts_cq);
	struct mlx5_cqwq *cqwq = &cq->wq;
	struct mlx5_cqe64 *cqe;
	int work_done = 0;

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &ptpsq->txqsq.state)))
		return false;

	cqe = mlx5_cqwq_get_cqe(cqwq);
	if (!cqe)
		return false;

	do {
		mlx5_cqwq_pop(cqwq);

		mlx5e_ptp_handle_ts_cqe(ptpsq, cqe, budget);
	} while ((++work_done < budget) && (cqe = mlx5_cqwq_get_cqe(cqwq)));

	mlx5_cqwq_update_db_record(cqwq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	return work_done == budget;
}

static int mlx5e_ptp_napi_poll(struct napi_struct *napi, int budget)
{
	struct mlx5e_ptp *c = container_of(napi, struct mlx5e_ptp, napi);
	struct mlx5e_ch_stats *ch_stats = c->stats;
	struct mlx5e_rq *rq = &c->rq;
	bool busy = false;
	int work_done = 0;
	int i;

	rcu_read_lock();

	ch_stats->poll++;

	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		for (i = 0; i < c->num_tc; i++) {
			busy |= mlx5e_poll_tx_cq(&c->ptpsq[i].txqsq.cq, budget);
			busy |= mlx5e_ptp_poll_ts_cq(&c->ptpsq[i].ts_cq, budget);
		}
	}
	if (test_bit(MLX5E_PTP_STATE_RX, c->state) && likely(budget)) {
		work_done = mlx5e_poll_rx_cq(&rq->cq, budget);
		busy |= work_done == budget;
		busy |= INDIRECT_CALL_2(rq->post_wqes,
					mlx5e_post_rx_mpwqes,
					mlx5e_post_rx_wqes,
					rq);
	}

	if (busy) {
		work_done = budget;
		goto out;
	}

	if (unlikely(!napi_complete_done(napi, work_done)))
		goto out;

	ch_stats->arm++;

	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		for (i = 0; i < c->num_tc; i++) {
			mlx5e_cq_arm(&c->ptpsq[i].txqsq.cq);
			mlx5e_cq_arm(&c->ptpsq[i].ts_cq);
		}
	}
	if (test_bit(MLX5E_PTP_STATE_RX, c->state))
		mlx5e_cq_arm(&rq->cq);

out:
	rcu_read_unlock();

	return work_done;
}

static int mlx5e_ptp_alloc_txqsq(struct mlx5e_ptp *c, int txq_ix,
				 struct mlx5e_params *params,
				 struct mlx5e_sq_param *param,
				 struct mlx5e_txqsq *sq, int tc,
				 struct mlx5e_ptpsq *ptpsq)
{
	void *sqc_wq               = MLX5_ADDR_OF(sqc, param->sqc, wq);
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5_wq_cyc *wq = &sq->wq;
	int err;
	int node;

	sq->pdev      = c->pdev;
	sq->clock     = &mdev->clock;
	sq->mkey_be   = c->mkey_be;
	sq->netdev    = c->netdev;
	sq->priv      = c->priv;
	sq->mdev      = mdev;
	sq->ch_ix     = MLX5E_PTP_CHANNEL_IX;
	sq->txq_ix    = txq_ix;
	sq->uar_map   = mdev->mlx5e_res.hw_objs.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	sq->stats     = &c->priv->ptp_stats.sq[tc];
	sq->ptpsq     = ptpsq;
	INIT_WORK(&sq->recover_work, mlx5e_tx_err_cqe_work);
	if (!MLX5_CAP_ETH(mdev, wqe_vlan_insert))
		set_bit(MLX5E_SQ_STATE_VLAN_NEED_L2_INLINE, &sq->state);
	sq->stop_room = param->stop_room;
	sq->ptp_cyc2time = mlx5_sq_ts_translator(mdev);

	node = dev_to_node(mlx5_core_dma_dev(mdev));

	param->wq.db_numa_node = node;
	err = mlx5_wq_cyc_create(mdev, &param->wq, sqc_wq, wq, &sq->wq_ctrl);
	if (err)
		return err;
	wq->db    = &wq->db[MLX5_SND_DBR];

	err = mlx5e_alloc_txqsq_db(sq, node);
	if (err)
		goto err_sq_wq_destroy;

	return 0;

err_sq_wq_destroy:
	mlx5_wq_destroy(&sq->wq_ctrl);

	return err;
}

static void mlx5e_ptp_destroy_sq(struct mlx5_core_dev *mdev, u32 sqn)
{
	mlx5_core_destroy_sq(mdev, sqn);
}

static int mlx5e_ptp_alloc_traffic_db(struct mlx5e_ptpsq *ptpsq, int numa)
{
	int wq_sz = mlx5_wq_cyc_get_size(&ptpsq->txqsq.wq);
	struct mlx5_core_dev *mdev = ptpsq->txqsq.mdev;

	ptpsq->skb_fifo.fifo = kvzalloc_node(array_size(wq_sz, sizeof(*ptpsq->skb_fifo.fifo)),
					     GFP_KERNEL, numa);
	if (!ptpsq->skb_fifo.fifo)
		return -ENOMEM;

	ptpsq->skb_fifo.pc   = &ptpsq->skb_fifo_pc;
	ptpsq->skb_fifo.cc   = &ptpsq->skb_fifo_cc;
	ptpsq->skb_fifo.mask = wq_sz - 1;
	if (MLX5_CAP_GEN_2(mdev, ts_cqe_metadata_size2wqe_counter))
		ptpsq->ts_cqe_ctr_mask =
			(1 << MLX5_CAP_GEN_2(mdev, ts_cqe_metadata_size2wqe_counter)) - 1;
	return 0;
}

static void mlx5e_ptp_drain_skb_fifo(struct mlx5e_skb_fifo *skb_fifo)
{
	while (*skb_fifo->pc != *skb_fifo->cc) {
		struct sk_buff *skb = mlx5e_skb_fifo_pop(skb_fifo);

		dev_kfree_skb_any(skb);
	}
}

static void mlx5e_ptp_free_traffic_db(struct mlx5e_skb_fifo *skb_fifo)
{
	mlx5e_ptp_drain_skb_fifo(skb_fifo);
	kvfree(skb_fifo->fifo);
}

static int mlx5e_ptp_open_txqsq(struct mlx5e_ptp *c, u32 tisn,
				int txq_ix, struct mlx5e_ptp_params *cparams,
				int tc, struct mlx5e_ptpsq *ptpsq)
{
	struct mlx5e_sq_param *sqp = &cparams->txq_sq_param;
	struct mlx5e_txqsq *txqsq = &ptpsq->txqsq;
	struct mlx5e_create_sq_param csp = {};
	int err;

	err = mlx5e_ptp_alloc_txqsq(c, txq_ix, &cparams->params, sqp,
				    txqsq, tc, ptpsq);
	if (err)
		return err;

	csp.tisn            = tisn;
	csp.tis_lst_sz      = 1;
	csp.cqn             = txqsq->cq.mcq.cqn;
	csp.wq_ctrl         = &txqsq->wq_ctrl;
	csp.min_inline_mode = txqsq->min_inline_mode;
	csp.ts_cqe_to_dest_cqn = ptpsq->ts_cq.mcq.cqn;

	err = mlx5e_create_sq_rdy(c->mdev, sqp, &csp, 0, &txqsq->sqn);
	if (err)
		goto err_free_txqsq;

	err = mlx5e_ptp_alloc_traffic_db(ptpsq,
					 dev_to_node(mlx5_core_dma_dev(c->mdev)));
	if (err)
		goto err_free_txqsq;

	return 0;

err_free_txqsq:
	mlx5e_free_txqsq(txqsq);

	return err;
}

static void mlx5e_ptp_close_txqsq(struct mlx5e_ptpsq *ptpsq)
{
	struct mlx5e_txqsq *sq = &ptpsq->txqsq;
	struct mlx5_core_dev *mdev = sq->mdev;

	mlx5e_ptp_free_traffic_db(&ptpsq->skb_fifo);
	cancel_work_sync(&sq->recover_work);
	mlx5e_ptp_destroy_sq(mdev, sq->sqn);
	mlx5e_free_txqsq_descs(sq);
	mlx5e_free_txqsq(sq);
}

static int mlx5e_ptp_open_txqsqs(struct mlx5e_ptp *c,
				 struct mlx5e_ptp_params *cparams)
{
	struct mlx5e_params *params = &cparams->params;
	u8 num_tc = mlx5e_get_dcb_num_tc(params);
	int ix_base;
	int err;
	int tc;

	ix_base = num_tc * params->num_channels;

	for (tc = 0; tc < num_tc; tc++) {
		int txq_ix = ix_base + tc;

		err = mlx5e_ptp_open_txqsq(c, c->priv->tisn[c->lag_port][tc], txq_ix,
					   cparams, tc, &c->ptpsq[tc]);
		if (err)
			goto close_txqsq;
	}

	return 0;

close_txqsq:
	for (--tc; tc >= 0; tc--)
		mlx5e_ptp_close_txqsq(&c->ptpsq[tc]);

	return err;
}

static void mlx5e_ptp_close_txqsqs(struct mlx5e_ptp *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_ptp_close_txqsq(&c->ptpsq[tc]);
}

static int mlx5e_ptp_open_tx_cqs(struct mlx5e_ptp *c,
				 struct mlx5e_ptp_params *cparams)
{
	struct mlx5e_params *params = &cparams->params;
	struct mlx5e_create_cq_param ccp = {};
	struct dim_cq_moder ptp_moder = {};
	struct mlx5e_cq_param *cq_param;
	u8 num_tc;
	int err;
	int tc;

	num_tc = mlx5e_get_dcb_num_tc(params);

	ccp.node     = dev_to_node(mlx5_core_dma_dev(c->mdev));
	ccp.ch_stats = c->stats;
	ccp.napi     = &c->napi;
	ccp.ix       = MLX5E_PTP_CHANNEL_IX;

	cq_param = &cparams->txq_sq_param.cqp;

	for (tc = 0; tc < num_tc; tc++) {
		struct mlx5e_cq *cq = &c->ptpsq[tc].txqsq.cq;

		err = mlx5e_open_cq(c->priv, ptp_moder, cq_param, &ccp, cq);
		if (err)
			goto out_err_txqsq_cq;
	}

	for (tc = 0; tc < num_tc; tc++) {
		struct mlx5e_cq *cq = &c->ptpsq[tc].ts_cq;
		struct mlx5e_ptpsq *ptpsq = &c->ptpsq[tc];

		err = mlx5e_open_cq(c->priv, ptp_moder, cq_param, &ccp, cq);
		if (err)
			goto out_err_ts_cq;

		ptpsq->cq_stats = &c->priv->ptp_stats.cq[tc];
	}

	return 0;

out_err_ts_cq:
	for (--tc; tc >= 0; tc--)
		mlx5e_close_cq(&c->ptpsq[tc].ts_cq);
	tc = num_tc;
out_err_txqsq_cq:
	for (--tc; tc >= 0; tc--)
		mlx5e_close_cq(&c->ptpsq[tc].txqsq.cq);

	return err;
}

static int mlx5e_ptp_open_rx_cq(struct mlx5e_ptp *c,
				struct mlx5e_ptp_params *cparams)
{
	struct mlx5e_create_cq_param ccp = {};
	struct dim_cq_moder ptp_moder = {};
	struct mlx5e_cq_param *cq_param;
	struct mlx5e_cq *cq = &c->rq.cq;

	ccp.node     = dev_to_node(mlx5_core_dma_dev(c->mdev));
	ccp.ch_stats = c->stats;
	ccp.napi     = &c->napi;
	ccp.ix       = MLX5E_PTP_CHANNEL_IX;

	cq_param = &cparams->rq_param.cqp;

	return mlx5e_open_cq(c->priv, ptp_moder, cq_param, &ccp, cq);
}

static void mlx5e_ptp_close_tx_cqs(struct mlx5e_ptp *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_cq(&c->ptpsq[tc].ts_cq);

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_cq(&c->ptpsq[tc].txqsq.cq);
}

static void mlx5e_ptp_build_sq_param(struct mlx5_core_dev *mdev,
				     struct mlx5e_params *params,
				     struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq;

	mlx5e_build_sq_param_common(mdev, param);

	wq = MLX5_ADDR_OF(sqc, sqc, wq);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
	param->stop_room = mlx5e_stop_room_for_max_wqe(mdev);
	mlx5e_build_tx_cq_param(mdev, params, &param->cqp);
}

static void mlx5e_ptp_build_rq_param(struct mlx5_core_dev *mdev,
				     struct net_device *netdev,
				     u16 q_counter,
				     struct mlx5e_ptp_params *ptp_params)
{
	struct mlx5e_rq_param *rq_params = &ptp_params->rq_param;
	struct mlx5e_params *params = &ptp_params->params;

	params->rq_wq_type = MLX5_WQ_TYPE_CYCLIC;
	mlx5e_init_rq_type_params(mdev, params);
	params->sw_mtu = netdev->max_mtu;
	mlx5e_build_rq_param(mdev, params, NULL, q_counter, rq_params);
}

static void mlx5e_ptp_build_params(struct mlx5e_ptp *c,
				   struct mlx5e_ptp_params *cparams,
				   struct mlx5e_params *orig)
{
	struct mlx5e_params *params = &cparams->params;

	params->tx_min_inline_mode = orig->tx_min_inline_mode;
	params->num_channels = orig->num_channels;
	params->hard_mtu = orig->hard_mtu;
	params->sw_mtu = orig->sw_mtu;
	params->mqprio = orig->mqprio;

	/* SQ */
	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		params->log_sq_size = orig->log_sq_size;
		mlx5e_ptp_build_sq_param(c->mdev, params, &cparams->txq_sq_param);
	}
	/* RQ */
	if (test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		params->vlan_strip_disable = orig->vlan_strip_disable;
		mlx5e_ptp_build_rq_param(c->mdev, c->netdev, c->priv->q_counter, cparams);
	}
}

static int mlx5e_init_ptp_rq(struct mlx5e_ptp *c, struct mlx5e_params *params,
			     struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = c->mdev;
	struct mlx5e_priv *priv = c->priv;
	int err;

	rq->wq_type      = params->rq_wq_type;
	rq->pdev         = c->pdev;
	rq->netdev       = priv->netdev;
	rq->priv         = priv;
	rq->clock        = &mdev->clock;
	rq->tstamp       = &priv->tstamp;
	rq->mdev         = mdev;
	rq->hw_mtu       = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	rq->stats        = &c->priv->ptp_stats.rq;
	rq->ix           = MLX5E_PTP_CHANNEL_IX;
	rq->ptp_cyc2time = mlx5_rq_ts_translator(mdev);
	err = mlx5e_rq_set_handlers(rq, params, false);
	if (err)
		return err;

	return xdp_rxq_info_reg(&rq->xdp_rxq, rq->netdev, rq->ix, 0);
}

static int mlx5e_ptp_open_rq(struct mlx5e_ptp *c, struct mlx5e_params *params,
			     struct mlx5e_rq_param *rq_param)
{
	int node = dev_to_node(c->mdev->device);
	int err;

	err = mlx5e_init_ptp_rq(c, params, &c->rq);
	if (err)
		return err;

	return mlx5e_open_rq(params, rq_param, NULL, node, &c->rq);
}

static int mlx5e_ptp_open_queues(struct mlx5e_ptp *c,
				 struct mlx5e_ptp_params *cparams)
{
	int err;

	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		err = mlx5e_ptp_open_tx_cqs(c, cparams);
		if (err)
			return err;

		err = mlx5e_ptp_open_txqsqs(c, cparams);
		if (err)
			goto close_tx_cqs;
	}
	if (test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		err = mlx5e_ptp_open_rx_cq(c, cparams);
		if (err)
			goto close_txqsq;

		err = mlx5e_ptp_open_rq(c, &cparams->params, &cparams->rq_param);
		if (err)
			goto close_rx_cq;
	}
	return 0;

close_rx_cq:
	if (test_bit(MLX5E_PTP_STATE_RX, c->state))
		mlx5e_close_cq(&c->rq.cq);
close_txqsq:
	if (test_bit(MLX5E_PTP_STATE_TX, c->state))
		mlx5e_ptp_close_txqsqs(c);
close_tx_cqs:
	if (test_bit(MLX5E_PTP_STATE_TX, c->state))
		mlx5e_ptp_close_tx_cqs(c);

	return err;
}

static void mlx5e_ptp_close_queues(struct mlx5e_ptp *c)
{
	if (test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		mlx5e_close_rq(&c->rq);
		mlx5e_close_cq(&c->rq.cq);
	}
	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		mlx5e_ptp_close_txqsqs(c);
		mlx5e_ptp_close_tx_cqs(c);
	}
}

static int mlx5e_ptp_set_state(struct mlx5e_ptp *c, struct mlx5e_params *params)
{
	if (MLX5E_GET_PFLAG(params, MLX5E_PFLAG_TX_PORT_TS))
		__set_bit(MLX5E_PTP_STATE_TX, c->state);

	if (params->ptp_rx)
		__set_bit(MLX5E_PTP_STATE_RX, c->state);

	return bitmap_empty(c->state, MLX5E_PTP_STATE_NUM_STATES) ? -EINVAL : 0;
}

static void mlx5e_ptp_rx_unset_fs(struct mlx5e_priv *priv)
{
	struct mlx5e_ptp_fs *ptp_fs = mlx5e_fs_get_ptp(priv->fs);

	if (!ptp_fs->valid)
		return;

	mlx5e_fs_tt_redirect_del_rule(ptp_fs->l2_rule);
	mlx5e_fs_tt_redirect_any_destroy(priv);

	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v6_rule);
	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v4_rule);
	mlx5e_fs_tt_redirect_udp_destroy(priv);
	ptp_fs->valid = false;
}

static int mlx5e_ptp_rx_set_fs(struct mlx5e_priv *priv)
{
	struct mlx5e_ptp_fs *ptp_fs = mlx5e_fs_get_ptp(priv->fs);
	u32 tirn = mlx5e_rx_res_get_tirn_ptp(priv->rx_res);
	struct mlx5_flow_handle *rule;
	int err;

	if (ptp_fs->valid)
		return 0;

	err = mlx5e_fs_tt_redirect_udp_create(priv);
	if (err)
		goto out_free;

	rule = mlx5e_fs_tt_redirect_udp_add_rule(priv, MLX5_TT_IPV4_UDP,
						 tirn, PTP_EV_PORT);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto out_destroy_fs_udp;
	}
	ptp_fs->udp_v4_rule = rule;

	rule = mlx5e_fs_tt_redirect_udp_add_rule(priv, MLX5_TT_IPV6_UDP,
						 tirn, PTP_EV_PORT);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto out_destroy_udp_v4_rule;
	}
	ptp_fs->udp_v6_rule = rule;

	err = mlx5e_fs_tt_redirect_any_create(priv);
	if (err)
		goto out_destroy_udp_v6_rule;

	rule = mlx5e_fs_tt_redirect_any_add_rule(priv, tirn, ETH_P_1588);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto out_destroy_fs_any;
	}
	ptp_fs->l2_rule = rule;
	ptp_fs->valid = true;

	return 0;

out_destroy_fs_any:
	mlx5e_fs_tt_redirect_any_destroy(priv);
out_destroy_udp_v6_rule:
	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v6_rule);
out_destroy_udp_v4_rule:
	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v4_rule);
out_destroy_fs_udp:
	mlx5e_fs_tt_redirect_udp_destroy(priv);
out_free:
	return err;
}

int mlx5e_ptp_open(struct mlx5e_priv *priv, struct mlx5e_params *params,
		   u8 lag_port, struct mlx5e_ptp **cp)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_ptp_params *cparams;
	struct mlx5e_ptp *c;
	int err;


	c = kvzalloc_node(sizeof(*c), GFP_KERNEL, dev_to_node(mlx5_core_dma_dev(mdev)));
	cparams = kvzalloc(sizeof(*cparams), GFP_KERNEL);
	if (!c || !cparams)
		return -ENOMEM;

	c->priv     = priv;
	c->mdev     = priv->mdev;
	c->tstamp   = &priv->tstamp;
	c->pdev     = mlx5_core_dma_dev(priv->mdev);
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.hw_objs.mkey);
	c->num_tc   = mlx5e_get_dcb_num_tc(params);
	c->stats    = &priv->ptp_stats.ch;
	c->lag_port = lag_port;

	err = mlx5e_ptp_set_state(c, params);
	if (err)
		goto err_free;

	netif_napi_add(netdev, &c->napi, mlx5e_ptp_napi_poll, 64);

	mlx5e_ptp_build_params(c, cparams, params);

	err = mlx5e_ptp_open_queues(c, cparams);
	if (unlikely(err))
		goto err_napi_del;

	if (test_bit(MLX5E_PTP_STATE_RX, c->state))
		priv->rx_ptp_opened = true;

	*cp = c;

	kvfree(cparams);

	return 0;

err_napi_del:
	netif_napi_del(&c->napi);
err_free:
	kvfree(cparams);
	kvfree(c);
	return err;
}

void mlx5e_ptp_close(struct mlx5e_ptp *c)
{
	mlx5e_ptp_close_queues(c);
	netif_napi_del(&c->napi);

	kvfree(c);
}

void mlx5e_ptp_activate_channel(struct mlx5e_ptp *c)
{
	int tc;

	napi_enable(&c->napi);

	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		for (tc = 0; tc < c->num_tc; tc++)
			mlx5e_activate_txqsq(&c->ptpsq[tc].txqsq);
	}
	if (test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		mlx5e_ptp_rx_set_fs(c->priv);
		mlx5e_activate_rq(&c->rq);
		mlx5e_trigger_napi_sched(&c->napi);
	}
}

void mlx5e_ptp_deactivate_channel(struct mlx5e_ptp *c)
{
	int tc;

	if (test_bit(MLX5E_PTP_STATE_RX, c->state))
		mlx5e_deactivate_rq(&c->rq);

	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		for (tc = 0; tc < c->num_tc; tc++)
			mlx5e_deactivate_txqsq(&c->ptpsq[tc].txqsq);
	}

	napi_disable(&c->napi);
}

int mlx5e_ptp_get_rqn(struct mlx5e_ptp *c, u32 *rqn)
{
	if (!c || !test_bit(MLX5E_PTP_STATE_RX, c->state))
		return -EINVAL;

	*rqn = c->rq.rqn;
	return 0;
}

int mlx5e_ptp_alloc_rx_fs(struct mlx5e_priv *priv)
{
	struct mlx5e_ptp_fs *ptp_fs;

	if (!mlx5e_profile_feature_cap(priv->profile, PTP_RX))
		return 0;

	ptp_fs = kzalloc(sizeof(*ptp_fs), GFP_KERNEL);
	if (!ptp_fs)
		return -ENOMEM;
	mlx5e_fs_set_ptp(priv->fs, ptp_fs);

	return 0;
}

void mlx5e_ptp_free_rx_fs(struct mlx5e_priv *priv)
{
	struct mlx5e_ptp_fs *ptp_fs = mlx5e_fs_get_ptp(priv->fs);

	if (!mlx5e_profile_feature_cap(priv->profile, PTP_RX))
		return;

	mlx5e_ptp_rx_unset_fs(priv);
	kfree(ptp_fs);
}

int mlx5e_ptp_rx_manage_fs(struct mlx5e_priv *priv, bool set)
{
	struct mlx5e_ptp *c = priv->channels.ptp;

	if (!mlx5e_profile_feature_cap(priv->profile, PTP_RX))
		return 0;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	if (set) {
		if (!c || !test_bit(MLX5E_PTP_STATE_RX, c->state)) {
			netdev_WARN_ONCE(priv->netdev, "Don't try to add PTP RX-FS rules");
			return -EINVAL;
		}
		return mlx5e_ptp_rx_set_fs(priv);
	}
	/* set == false */
	if (c && test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		netdev_WARN_ONCE(priv->netdev, "Don't try to remove PTP RX-FS rules");
		return -EINVAL;
	}
	mlx5e_ptp_rx_unset_fs(priv);
	return 0;
}
