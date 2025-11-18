// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2020 Mellanox Technologies

#include "en/ptp.h"
#include "en/health.h"
#include "en/txrx.h"
#include "en/params.h"
#include "en/fs_tt_redirect.h"
#include <linux/list.h>
#include <linux/spinlock.h>
#include <net/netdev_lock.h>

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

struct mlx5e_ptp_port_ts_cqe_tracker {
	u8 metadata_id;
	bool inuse : 1;
	struct list_head entry;
};

struct mlx5e_ptp_port_ts_cqe_list {
	struct mlx5e_ptp_port_ts_cqe_tracker *nodes;
	struct list_head tracker_list_head;
	/* Sync list operations in xmit and napi_poll contexts */
	spinlock_t tracker_list_lock;
};

static inline void
mlx5e_ptp_port_ts_cqe_list_add(struct mlx5e_ptp_port_ts_cqe_list *list, u8 metadata)
{
	struct mlx5e_ptp_port_ts_cqe_tracker *tracker = &list->nodes[metadata];

	WARN_ON_ONCE(tracker->inuse);
	tracker->inuse = true;
	spin_lock_bh(&list->tracker_list_lock);
	list_add_tail(&tracker->entry, &list->tracker_list_head);
	spin_unlock_bh(&list->tracker_list_lock);
}

static void
mlx5e_ptp_port_ts_cqe_list_remove(struct mlx5e_ptp_port_ts_cqe_list *list, u8 metadata)
{
	struct mlx5e_ptp_port_ts_cqe_tracker *tracker = &list->nodes[metadata];

	WARN_ON_ONCE(!tracker->inuse);
	tracker->inuse = false;
	spin_lock_bh(&list->tracker_list_lock);
	list_del(&tracker->entry);
	spin_unlock_bh(&list->tracker_list_lock);
}

void mlx5e_ptpsq_track_metadata(struct mlx5e_ptpsq *ptpsq, u8 metadata)
{
	mlx5e_ptp_port_ts_cqe_list_add(ptpsq->ts_cqe_pending_list, metadata);
}

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

static struct sk_buff *
mlx5e_ptp_metadata_map_lookup(struct mlx5e_ptp_metadata_map *map, u16 metadata)
{
	return map->data[metadata];
}

static struct sk_buff *
mlx5e_ptp_metadata_map_remove(struct mlx5e_ptp_metadata_map *map, u16 metadata)
{
	struct sk_buff *skb;

	skb = map->data[metadata];
	map->data[metadata] = NULL;

	return skb;
}

static bool mlx5e_ptp_metadata_map_unhealthy(struct mlx5e_ptp_metadata_map *map)
{
	/* Considered beginning unhealthy state if size * 15 / 2^4 cannot be reclaimed. */
	return map->undelivered_counter > (map->capacity >> 4) * 15;
}

static void mlx5e_ptpsq_mark_ts_cqes_undelivered(struct mlx5e_ptpsq *ptpsq,
						 ktime_t port_tstamp)
{
	struct mlx5e_ptp_port_ts_cqe_list *cqe_list = ptpsq->ts_cqe_pending_list;
	ktime_t timeout = ns_to_ktime(MLX5E_PTP_TS_CQE_UNDELIVERED_TIMEOUT);
	struct mlx5e_ptp_metadata_map *metadata_map = &ptpsq->metadata_map;
	struct mlx5e_ptp_port_ts_cqe_tracker *pos, *n;

	spin_lock_bh(&cqe_list->tracker_list_lock);
	list_for_each_entry_safe(pos, n, &cqe_list->tracker_list_head, entry) {
		struct sk_buff *skb =
			mlx5e_ptp_metadata_map_lookup(metadata_map, pos->metadata_id);
		ktime_t dma_tstamp = mlx5e_skb_cb_get_hwts(skb)->cqe_hwtstamp;

		if (!dma_tstamp ||
		    ktime_after(ktime_add(dma_tstamp, timeout), port_tstamp))
			break;

		metadata_map->undelivered_counter++;
		WARN_ON_ONCE(!pos->inuse);
		pos->inuse = false;
		list_del(&pos->entry);
		ptpsq->cq_stats->lost_cqe++;
	}
	spin_unlock_bh(&cqe_list->tracker_list_lock);
}

#define PTP_WQE_CTR2IDX(val) ((val) & ptpsq->ts_cqe_ctr_mask)

static void mlx5e_ptp_handle_ts_cqe(struct mlx5e_ptpsq *ptpsq,
				    struct mlx5_cqe64 *cqe,
				    u8 *md_buff,
				    u8 *md_buff_sz,
				    int budget)
{
	struct mlx5e_ptp_port_ts_cqe_list *pending_cqe_list = ptpsq->ts_cqe_pending_list;
	u8 metadata_id = PTP_WQE_CTR2IDX(be16_to_cpu(cqe->wqe_counter));
	bool is_err_cqe = !!MLX5E_RX_ERR_CQE(cqe);
	struct mlx5e_txqsq *sq = &ptpsq->txqsq;
	struct sk_buff *skb;
	ktime_t hwtstamp;

	if (likely(pending_cqe_list->nodes[metadata_id].inuse)) {
		mlx5e_ptp_port_ts_cqe_list_remove(pending_cqe_list, metadata_id);
	} else {
		/* Reclaim space in the unlikely event CQE was delivered after
		 * marking it late.
		 */
		ptpsq->metadata_map.undelivered_counter--;
		ptpsq->cq_stats->late_cqe++;
	}

	skb = mlx5e_ptp_metadata_map_remove(&ptpsq->metadata_map, metadata_id);

	if (unlikely(is_err_cqe)) {
		ptpsq->cq_stats->err_cqe++;
		goto out;
	}

	hwtstamp = mlx5e_cqe_ts_to_ns(sq->ptp_cyc2time, sq->clock, get_cqe_ts(cqe));
	mlx5e_skb_cb_hwtstamp_handler(skb, MLX5E_SKB_CB_PORT_HWTSTAMP,
				      hwtstamp, ptpsq->cq_stats);
	ptpsq->cq_stats->cqe++;

	mlx5e_ptpsq_mark_ts_cqes_undelivered(ptpsq, hwtstamp);
out:
	napi_consume_skb(skb, budget);
	md_buff[(*md_buff_sz)++] = metadata_id;
	if (unlikely(mlx5e_ptp_metadata_map_unhealthy(&ptpsq->metadata_map)) &&
	    !test_and_set_bit(MLX5E_SQ_STATE_RECOVERING, &sq->state))
		queue_work(ptpsq->txqsq.priv->wq, &ptpsq->report_unhealthy_work);
}

static bool mlx5e_ptp_poll_ts_cq(struct mlx5e_cq *cq, int napi_budget)
{
	struct mlx5e_ptpsq *ptpsq = container_of(cq, struct mlx5e_ptpsq, ts_cq);
	int budget = min(napi_budget, MLX5E_TX_CQ_POLL_BUDGET);
	u8 metadata_buff[MLX5E_TX_CQ_POLL_BUDGET];
	u8 metadata_buff_sz = 0;
	struct mlx5_cqwq *cqwq;
	struct mlx5_cqe64 *cqe;
	int work_done = 0;

	cqwq = &cq->wq;

	if (unlikely(!test_bit(MLX5E_SQ_STATE_ENABLED, &ptpsq->txqsq.state)))
		return false;

	cqe = mlx5_cqwq_get_cqe(cqwq);
	if (!cqe)
		return false;

	do {
		mlx5_cqwq_pop(cqwq);

		mlx5e_ptp_handle_ts_cqe(ptpsq, cqe,
					metadata_buff, &metadata_buff_sz, napi_budget);
	} while ((++work_done < budget) && (cqe = mlx5_cqwq_get_cqe(cqwq)));

	mlx5_cqwq_update_db_record(cqwq);

	/* ensure cq space is freed before enabling more cqes */
	wmb();

	while (metadata_buff_sz > 0)
		mlx5e_ptp_metadata_fifo_push(&ptpsq->metadata_freelist,
					     metadata_buff[--metadata_buff_sz]);

	mlx5e_txqsq_wake(&ptpsq->txqsq);

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
	sq->clock     = mdev->clock;
	sq->mkey_be   = c->mkey_be;
	sq->netdev    = c->netdev;
	sq->priv      = c->priv;
	sq->mdev      = mdev;
	sq->ch_ix     = MLX5E_PTP_CHANNEL_IX;
	sq->txq_ix    = txq_ix;
	sq->uar_map   = c->bfreg->map;
	sq->min_inline_mode = params->tx_min_inline_mode;
	sq->hw_mtu    = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	sq->stats     = &c->priv->ptp_stats.sq[tc];
	sq->ptpsq     = ptpsq;
	INIT_WORK(&sq->recover_work, mlx5e_tx_err_cqe_work);
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
	struct mlx5e_ptp_metadata_fifo *metadata_freelist = &ptpsq->metadata_freelist;
	struct mlx5e_ptp_metadata_map *metadata_map = &ptpsq->metadata_map;
	struct mlx5e_ptp_port_ts_cqe_list *cqe_list;
	int db_sz;
	int md;

	cqe_list = kvzalloc_node(sizeof(*ptpsq->ts_cqe_pending_list), GFP_KERNEL, numa);
	if (!cqe_list)
		return -ENOMEM;
	ptpsq->ts_cqe_pending_list = cqe_list;

	db_sz = min_t(u32, mlx5_wq_cyc_get_size(&ptpsq->txqsq.wq),
		      1 << MLX5_CAP_GEN_2(ptpsq->txqsq.mdev,
					  ts_cqe_metadata_size2wqe_counter));
	ptpsq->ts_cqe_ctr_mask = db_sz - 1;

	cqe_list->nodes = kvzalloc_node(array_size(db_sz, sizeof(*cqe_list->nodes)),
					GFP_KERNEL, numa);
	if (!cqe_list->nodes)
		goto free_cqe_list;
	INIT_LIST_HEAD(&cqe_list->tracker_list_head);
	spin_lock_init(&cqe_list->tracker_list_lock);

	metadata_freelist->data =
		kvzalloc_node(array_size(db_sz, sizeof(*metadata_freelist->data)),
			      GFP_KERNEL, numa);
	if (!metadata_freelist->data)
		goto free_cqe_list_nodes;
	metadata_freelist->mask = ptpsq->ts_cqe_ctr_mask;

	for (md = 0; md < db_sz; ++md) {
		cqe_list->nodes[md].metadata_id = md;
		metadata_freelist->data[md] = md;
	}
	metadata_freelist->pc = db_sz;

	metadata_map->data =
		kvzalloc_node(array_size(db_sz, sizeof(*metadata_map->data)),
			      GFP_KERNEL, numa);
	if (!metadata_map->data)
		goto free_metadata_freelist;
	metadata_map->capacity = db_sz;

	return 0;

free_metadata_freelist:
	kvfree(metadata_freelist->data);
free_cqe_list_nodes:
	kvfree(cqe_list->nodes);
free_cqe_list:
	kvfree(cqe_list);
	return -ENOMEM;
}

static void mlx5e_ptp_drain_metadata_map(struct mlx5e_ptp_metadata_map *map)
{
	int idx;

	for (idx = 0; idx < map->capacity; ++idx) {
		struct sk_buff *skb = map->data[idx];

		dev_kfree_skb_any(skb);
	}
}

static void mlx5e_ptp_free_traffic_db(struct mlx5e_ptpsq *ptpsq)
{
	mlx5e_ptp_drain_metadata_map(&ptpsq->metadata_map);
	kvfree(ptpsq->metadata_map.data);
	kvfree(ptpsq->metadata_freelist.data);
	kvfree(ptpsq->ts_cqe_pending_list->nodes);
	kvfree(ptpsq->ts_cqe_pending_list);
}

static void mlx5e_ptpsq_unhealthy_work(struct work_struct *work)
{
	struct mlx5e_ptpsq *ptpsq =
		container_of(work, struct mlx5e_ptpsq, report_unhealthy_work);
	struct mlx5e_txqsq *sq = &ptpsq->txqsq;

	/* Recovering the PTP SQ means re-enabling NAPI, which requires the
	 * netdev instance lock. However, SQ closing has to wait for this work
	 * task to finish while also holding the same lock. So either get the
	 * lock or find that the SQ is no longer enabled and thus this work is
	 * not relevant anymore.
	 */
	while (!netdev_trylock(sq->netdev)) {
		if (!test_bit(MLX5E_SQ_STATE_ENABLED, &sq->state))
			return;
		msleep(20);
	}

	mlx5e_reporter_tx_ptpsq_unhealthy(ptpsq);
	netdev_unlock(sq->netdev);
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
	csp.uar_page = c->bfreg->index;

	err = mlx5e_create_sq_rdy(c->mdev, sqp, &csp, 0, &txqsq->sqn);
	if (err)
		goto err_free_txqsq;

	err = mlx5e_ptp_alloc_traffic_db(ptpsq, dev_to_node(mlx5_core_dma_dev(c->mdev)));
	if (err)
		goto err_free_txqsq;

	INIT_WORK(&ptpsq->report_unhealthy_work, mlx5e_ptpsq_unhealthy_work);

	return 0;

err_free_txqsq:
	mlx5e_free_txqsq(txqsq);

	return err;
}

static void mlx5e_ptp_close_txqsq(struct mlx5e_ptpsq *ptpsq)
{
	struct mlx5e_txqsq *sq = &ptpsq->txqsq;
	struct mlx5_core_dev *mdev = sq->mdev;

	if (current_work() != &ptpsq->report_unhealthy_work)
		cancel_work_sync(&ptpsq->report_unhealthy_work);
	mlx5e_ptp_free_traffic_db(ptpsq);
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
		u32 tisn;

		tisn = mlx5e_profile_get_tisn(c->mdev, c->priv, c->priv->profile,
					      c->lag_port, tc);
		err = mlx5e_ptp_open_txqsq(c, tisn, txq_ix, cparams, tc, &c->ptpsq[tc]);
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

	ccp.netdev   = c->netdev;
	ccp.wq       = c->priv->wq;
	ccp.node     = dev_to_node(mlx5_core_dma_dev(c->mdev));
	ccp.ch_stats = c->stats;
	ccp.napi     = &c->napi;
	ccp.ix       = MLX5E_PTP_CHANNEL_IX;
	ccp.uar      = c->bfreg->up;

	cq_param = &cparams->txq_sq_param.cqp;

	for (tc = 0; tc < num_tc; tc++) {
		struct mlx5e_cq *cq = &c->ptpsq[tc].txqsq.cq;

		err = mlx5e_open_cq(c->mdev, ptp_moder, cq_param, &ccp, cq);
		if (err)
			goto out_err_txqsq_cq;
	}

	for (tc = 0; tc < num_tc; tc++) {
		struct mlx5e_cq *cq = &c->ptpsq[tc].ts_cq;
		struct mlx5e_ptpsq *ptpsq = &c->ptpsq[tc];

		err = mlx5e_open_cq(c->mdev, ptp_moder, cq_param, &ccp, cq);
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

	ccp.netdev   = c->netdev;
	ccp.wq       = c->priv->wq;
	ccp.node     = dev_to_node(mlx5_core_dma_dev(c->mdev));
	ccp.ch_stats = c->stats;
	ccp.napi     = &c->napi;
	ccp.ix       = MLX5E_PTP_CHANNEL_IX;
	ccp.uar      = c->bfreg->up;

	cq_param = &cparams->rq_param.cqp;

	return mlx5e_open_cq(c->mdev, ptp_moder, cq_param, &ccp, cq);
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
				     struct mlx5e_ptp_params *ptp_params)
{
	struct mlx5e_rq_param *rq_params = &ptp_params->rq_param;
	struct mlx5e_params *params = &ptp_params->params;

	params->rq_wq_type = MLX5_WQ_TYPE_CYCLIC;
	mlx5e_init_rq_type_params(mdev, params);
	params->sw_mtu = netdev->max_mtu;
	mlx5e_build_rq_param(mdev, params, NULL, rq_params);
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
		params->log_sq_size =
			min(MLX5_CAP_GEN_2(c->mdev, ts_cqe_metadata_size2wqe_counter),
			    MLX5E_PTP_MAX_LOG_SQ_SIZE);
		params->log_sq_size = min(params->log_sq_size, orig->log_sq_size);
		mlx5e_ptp_build_sq_param(c->mdev, params, &cparams->txq_sq_param);
	}
	/* RQ */
	if (test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		params->vlan_strip_disable = orig->vlan_strip_disable;
		mlx5e_ptp_build_rq_param(c->mdev, c->netdev, cparams);
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
	rq->clock        = mdev->clock;
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
	int err, sd_ix;
	u16 q_counter;

	err = mlx5e_init_ptp_rq(c, params, &c->rq);
	if (err)
		return err;

	sd_ix = mlx5_sd_ch_ix_get_dev_ix(c->mdev, MLX5E_PTP_CHANNEL_IX);
	q_counter = c->priv->q_counter[sd_ix];
	return mlx5e_open_rq(params, rq_param, NULL, node, q_counter, &c->rq);
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

static void mlx5e_ptp_rx_unset_fs(struct mlx5e_flow_steering *fs)
{
	struct mlx5e_ptp_fs *ptp_fs = mlx5e_fs_get_ptp(fs);

	if (!ptp_fs->valid)
		return;

	mlx5e_fs_tt_redirect_del_rule(ptp_fs->l2_rule);
	mlx5e_fs_tt_redirect_any_destroy(fs);

	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v6_rule);
	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v4_rule);
	mlx5e_fs_tt_redirect_udp_destroy(fs);
	ptp_fs->valid = false;
}

static int mlx5e_ptp_rx_set_fs(struct mlx5e_priv *priv)
{
	u32 tirn = mlx5e_rx_res_get_tirn_ptp(priv->rx_res);
	struct mlx5e_flow_steering *fs = priv->fs;
	struct mlx5_flow_handle *rule;
	struct mlx5e_ptp_fs *ptp_fs;
	int err;

	ptp_fs = mlx5e_fs_get_ptp(fs);
	if (ptp_fs->valid)
		return 0;

	err = mlx5e_fs_tt_redirect_udp_create(fs);
	if (err)
		goto out_free;

	rule = mlx5e_fs_tt_redirect_udp_add_rule(fs, MLX5_TT_IPV4_UDP,
						 tirn, PTP_EV_PORT);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto out_destroy_fs_udp;
	}
	ptp_fs->udp_v4_rule = rule;

	rule = mlx5e_fs_tt_redirect_udp_add_rule(fs, MLX5_TT_IPV6_UDP,
						 tirn, PTP_EV_PORT);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto out_destroy_udp_v4_rule;
	}
	ptp_fs->udp_v6_rule = rule;

	err = mlx5e_fs_tt_redirect_any_create(fs);
	if (err)
		goto out_destroy_udp_v6_rule;

	rule = mlx5e_fs_tt_redirect_any_add_rule(fs, tirn, ETH_P_1588);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		goto out_destroy_fs_any;
	}
	ptp_fs->l2_rule = rule;
	ptp_fs->valid = true;

	return 0;

out_destroy_fs_any:
	mlx5e_fs_tt_redirect_any_destroy(fs);
out_destroy_udp_v6_rule:
	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v6_rule);
out_destroy_udp_v4_rule:
	mlx5e_fs_tt_redirect_del_rule(ptp_fs->udp_v4_rule);
out_destroy_fs_udp:
	mlx5e_fs_tt_redirect_udp_destroy(fs);
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
	if (!c || !cparams) {
		err = -ENOMEM;
		goto err_free;
	}

	c->priv     = priv;
	c->mdev     = priv->mdev;
	c->tstamp   = &priv->tstamp;
	c->pdev     = mlx5_core_dma_dev(priv->mdev);
	c->netdev   = priv->netdev;
	c->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.hw_objs.mkey);
	c->num_tc   = mlx5e_get_dcb_num_tc(params);
	c->stats    = &priv->ptp_stats.ch;
	c->lag_port = lag_port;
	c->bfreg    = &mdev->priv.bfreg;

	err = mlx5e_ptp_set_state(c, params);
	if (err)
		goto err_free;

	netif_napi_add_locked(netdev, &c->napi, mlx5e_ptp_napi_poll);

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
	netif_napi_del_locked(&c->napi);
err_free:
	kvfree(cparams);
	kvfree(c);
	return err;
}

void mlx5e_ptp_close(struct mlx5e_ptp *c)
{
	mlx5e_ptp_close_queues(c);
	netif_napi_del_locked(&c->napi);

	kvfree(c);
}

void mlx5e_ptp_activate_channel(struct mlx5e_ptp *c)
{
	int tc;

	napi_enable_locked(&c->napi);

	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		for (tc = 0; tc < c->num_tc; tc++)
			mlx5e_activate_txqsq(&c->ptpsq[tc].txqsq);
	}
	if (test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		mlx5e_ptp_rx_set_fs(c->priv);
		mlx5e_activate_rq(&c->rq);
		netif_queue_set_napi(c->netdev, c->rq.ix, NETDEV_QUEUE_TYPE_RX, &c->napi);
	}
	mlx5e_trigger_napi_sched(&c->napi);
}

void mlx5e_ptp_deactivate_channel(struct mlx5e_ptp *c)
{
	int tc;

	if (test_bit(MLX5E_PTP_STATE_RX, c->state)) {
		netif_queue_set_napi(c->netdev, c->rq.ix, NETDEV_QUEUE_TYPE_RX, NULL);
		mlx5e_deactivate_rq(&c->rq);
	}

	if (test_bit(MLX5E_PTP_STATE_TX, c->state)) {
		for (tc = 0; tc < c->num_tc; tc++)
			mlx5e_deactivate_txqsq(&c->ptpsq[tc].txqsq);
	}

	napi_disable_locked(&c->napi);
}

int mlx5e_ptp_get_rqn(struct mlx5e_ptp *c, u32 *rqn)
{
	if (!c || !test_bit(MLX5E_PTP_STATE_RX, c->state))
		return -EINVAL;

	*rqn = c->rq.rqn;
	return 0;
}

int mlx5e_ptp_alloc_rx_fs(struct mlx5e_flow_steering *fs,
			  const struct mlx5e_profile *profile)
{
	struct mlx5e_ptp_fs *ptp_fs;

	if (!mlx5e_profile_feature_cap(profile, PTP_RX))
		return 0;

	ptp_fs = kzalloc(sizeof(*ptp_fs), GFP_KERNEL);
	if (!ptp_fs)
		return -ENOMEM;
	mlx5e_fs_set_ptp(fs, ptp_fs);

	return 0;
}

void mlx5e_ptp_free_rx_fs(struct mlx5e_flow_steering *fs,
			  const struct mlx5e_profile *profile)
{
	struct mlx5e_ptp_fs *ptp_fs = mlx5e_fs_get_ptp(fs);

	if (!mlx5e_profile_feature_cap(profile, PTP_RX))
		return;

	mlx5e_ptp_rx_unset_fs(fs);
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
	mlx5e_ptp_rx_unset_fs(priv->fs);
	return 0;
}
