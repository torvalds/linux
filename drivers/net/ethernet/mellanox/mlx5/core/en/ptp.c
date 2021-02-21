// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2020 Mellanox Technologies

#include "en/ptp.h"
#include "en/txrx.h"

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

static void mlx5e_ptp_handle_ts_cqe(struct mlx5e_ptpsq *ptpsq,
				    struct mlx5_cqe64 *cqe,
				    int budget)
{
	struct sk_buff *skb = mlx5e_skb_fifo_pop(&ptpsq->skb_fifo);
	struct mlx5e_txqsq *sq = &ptpsq->txqsq;
	ktime_t hwtstamp;

	if (unlikely(MLX5E_RX_ERR_CQE(cqe))) {
		ptpsq->cq_stats->err_cqe++;
		goto out;
	}

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
	struct mlx5e_port_ptp *c = container_of(napi, struct mlx5e_port_ptp,
						napi);
	struct mlx5e_ch_stats *ch_stats = c->stats;
	bool busy = false;
	int work_done = 0;
	int i;

	rcu_read_lock();

	ch_stats->poll++;

	for (i = 0; i < c->num_tc; i++) {
		busy |= mlx5e_poll_tx_cq(&c->ptpsq[i].txqsq.cq, budget);
		busy |= mlx5e_ptp_poll_ts_cq(&c->ptpsq[i].ts_cq, budget);
	}

	if (busy) {
		work_done = budget;
		goto out;
	}

	if (unlikely(!napi_complete_done(napi, work_done)))
		goto out;

	ch_stats->arm++;

	for (i = 0; i < c->num_tc; i++) {
		mlx5e_cq_arm(&c->ptpsq[i].txqsq.cq);
		mlx5e_cq_arm(&c->ptpsq[i].ts_cq);
	}

out:
	rcu_read_unlock();

	return work_done;
}

static int mlx5e_ptp_alloc_txqsq(struct mlx5e_port_ptp *c, int txq_ix,
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
	sq->tstamp    = c->tstamp;
	sq->clock     = &mdev->clock;
	sq->mkey_be   = c->mkey_be;
	sq->netdev    = c->netdev;
	sq->priv      = c->priv;
	sq->mdev      = mdev;
	sq->ch_ix     = c->ix;
	sq->txq_ix    = txq_ix;
	sq->uar_map   = mdev->mlx5e_res.bfreg.map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	sq->stats     = &c->priv->port_ptp_stats.sq[tc];
	sq->ptpsq     = ptpsq;
	INIT_WORK(&sq->recover_work, mlx5e_tx_err_cqe_work);
	if (!MLX5_CAP_ETH(mdev, wqe_vlan_insert))
		set_bit(MLX5E_SQ_STATE_VLAN_NEED_L2_INLINE, &sq->state);
	sq->stop_room = param->stop_room;
	sq->ptp_cyc2time = mlx5_is_real_time_sq(mdev) ?
			   mlx5_real_time_cyc2time :
			   mlx5_timecounter_cyc2time;

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

	ptpsq->skb_fifo.fifo = kvzalloc_node(array_size(wq_sz, sizeof(*ptpsq->skb_fifo.fifo)),
					     GFP_KERNEL, numa);
	if (!ptpsq->skb_fifo.fifo)
		return -ENOMEM;

	ptpsq->skb_fifo.pc   = &ptpsq->skb_fifo_pc;
	ptpsq->skb_fifo.cc   = &ptpsq->skb_fifo_cc;
	ptpsq->skb_fifo.mask = wq_sz - 1;

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

static int mlx5e_ptp_open_txqsq(struct mlx5e_port_ptp *c, u32 tisn,
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

static int mlx5e_ptp_open_txqsqs(struct mlx5e_port_ptp *c,
				 struct mlx5e_ptp_params *cparams)
{
	struct mlx5e_params *params = &cparams->params;
	int ix_base;
	int err;
	int tc;

	ix_base = params->num_tc * params->num_channels;

	for (tc = 0; tc < params->num_tc; tc++) {
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

static void mlx5e_ptp_close_txqsqs(struct mlx5e_port_ptp *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_ptp_close_txqsq(&c->ptpsq[tc]);
}

static int mlx5e_ptp_open_cqs(struct mlx5e_port_ptp *c,
			      struct mlx5e_ptp_params *cparams)
{
	struct mlx5e_params *params = &cparams->params;
	struct mlx5e_create_cq_param ccp = {};
	struct dim_cq_moder ptp_moder = {};
	struct mlx5e_cq_param *cq_param;
	int err;
	int tc;

	ccp.node     = dev_to_node(mlx5_core_dma_dev(c->mdev));
	ccp.ch_stats = c->stats;
	ccp.napi     = &c->napi;
	ccp.ix       = c->ix;

	cq_param = &cparams->txq_sq_param.cqp;

	for (tc = 0; tc < params->num_tc; tc++) {
		struct mlx5e_cq *cq = &c->ptpsq[tc].txqsq.cq;

		err = mlx5e_open_cq(c->priv, ptp_moder, cq_param, &ccp, cq);
		if (err)
			goto out_err_txqsq_cq;
	}

	for (tc = 0; tc < params->num_tc; tc++) {
		struct mlx5e_cq *cq = &c->ptpsq[tc].ts_cq;
		struct mlx5e_ptpsq *ptpsq = &c->ptpsq[tc];

		err = mlx5e_open_cq(c->priv, ptp_moder, cq_param, &ccp, cq);
		if (err)
			goto out_err_ts_cq;

		ptpsq->cq_stats = &c->priv->port_ptp_stats.cq[tc];
	}

	return 0;

out_err_ts_cq:
	for (--tc; tc >= 0; tc--)
		mlx5e_close_cq(&c->ptpsq[tc].ts_cq);
	tc = params->num_tc;
out_err_txqsq_cq:
	for (--tc; tc >= 0; tc--)
		mlx5e_close_cq(&c->ptpsq[tc].txqsq.cq);

	return err;
}

static void mlx5e_ptp_close_cqs(struct mlx5e_port_ptp *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_cq(&c->ptpsq[tc].ts_cq);

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_close_cq(&c->ptpsq[tc].txqsq.cq);
}

static void mlx5e_ptp_build_sq_param(struct mlx5e_priv *priv,
				     struct mlx5e_params *params,
				     struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq;

	mlx5e_build_sq_param_common(priv, param);

	wq = MLX5_ADDR_OF(sqc, sqc, wq);
	MLX5_SET(wq, wq, log_wq_sz, params->log_sq_size);
	param->stop_room = mlx5e_stop_room_for_wqe(MLX5_SEND_WQE_MAX_WQEBBS);
	mlx5e_build_tx_cq_param(priv, params, &param->cqp);
}

static void mlx5e_ptp_build_params(struct mlx5e_port_ptp *c,
				   struct mlx5e_ptp_params *cparams,
				   struct mlx5e_params *orig)
{
	struct mlx5e_params *params = &cparams->params;

	params->tx_min_inline_mode = orig->tx_min_inline_mode;
	params->num_channels = orig->num_channels;
	params->hard_mtu = orig->hard_mtu;
	params->sw_mtu = orig->sw_mtu;
	params->num_tc = orig->num_tc;

	/* SQ */
	params->log_sq_size = orig->log_sq_size;

	mlx5e_ptp_build_sq_param(c->priv, params, &cparams->txq_sq_param);
}

static int mlx5e_ptp_open_queues(struct mlx5e_port_ptp *c,
				 struct mlx5e_ptp_params *cparams)
{
	int err;

	err = mlx5e_ptp_open_cqs(c, cparams);
	if (err)
		return err;

	err = mlx5e_ptp_open_txqsqs(c, cparams);
	if (err)
		goto close_cqs;

	return 0;

close_cqs:
	mlx5e_ptp_close_cqs(c);

	return err;
}

static void mlx5e_ptp_close_queues(struct mlx5e_port_ptp *c)
{
	mlx5e_ptp_close_txqsqs(c);
	mlx5e_ptp_close_cqs(c);
}

int mlx5e_port_ptp_open(struct mlx5e_priv *priv, struct mlx5e_params *params,
			u8 lag_port, struct mlx5e_port_ptp **cp)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_ptp_params *cparams;
	struct mlx5e_port_ptp *c;
	unsigned int irq;
	int err;
	int eqn;

	err = mlx5_vector2eqn(priv->mdev, 0, &eqn, &irq);
	if (err)
		return err;

	c = kvzalloc_node(sizeof(*c), GFP_KERNEL, dev_to_node(mlx5_core_dma_dev(mdev)));
	cparams = kvzalloc(sizeof(*cparams), GFP_KERNEL);
	if (!c || !cparams)
		return -ENOMEM;

	c->priv     = priv;
	c->mdev     = priv->mdev;
	c->tstamp   = &priv->tstamp;
	c->ix       = 0;
	c->pdev     = mlx5_core_dma_dev(priv->mdev);
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.mkey.key);
	c->num_tc   = params->num_tc;
	c->stats    = &priv->port_ptp_stats.ch;
	c->lag_port = lag_port;

	netif_napi_add(netdev, &c->napi, mlx5e_ptp_napi_poll, 64);

	mlx5e_ptp_build_params(c, cparams, params);

	err = mlx5e_ptp_open_queues(c, cparams);
	if (unlikely(err))
		goto err_napi_del;

	*cp = c;

	kvfree(cparams);

	return 0;

err_napi_del:
	netif_napi_del(&c->napi);

	kvfree(cparams);
	kvfree(c);
	return err;
}

void mlx5e_port_ptp_close(struct mlx5e_port_ptp *c)
{
	mlx5e_ptp_close_queues(c);
	netif_napi_del(&c->napi);

	kvfree(c);
}

void mlx5e_ptp_activate_channel(struct mlx5e_port_ptp *c)
{
	int tc;

	napi_enable(&c->napi);

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_activate_txqsq(&c->ptpsq[tc].txqsq);
}

void mlx5e_ptp_deactivate_channel(struct mlx5e_port_ptp *c)
{
	int tc;

	for (tc = 0; tc < c->num_tc; tc++)
		mlx5e_deactivate_txqsq(&c->ptpsq[tc].txqsq);

	napi_disable(&c->napi);
}
