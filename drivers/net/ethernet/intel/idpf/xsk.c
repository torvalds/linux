// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <net/libeth/xsk.h>

#include "idpf.h"
#include "xdp.h"
#include "xsk.h"

static void idpf_xsk_tx_timer(struct work_struct *work);

static void idpf_xsk_setup_rxq(const struct idpf_vport *vport,
			       struct idpf_rx_queue *rxq)
{
	struct xsk_buff_pool *pool;

	pool = xsk_get_pool_from_qid(vport->netdev, rxq->idx);
	if (!pool || !pool->dev || !xsk_buff_can_alloc(pool, 1))
		return;

	rxq->pool = pool;

	idpf_queue_set(XSK, rxq);
}

static void idpf_xsk_setup_bufq(const struct idpf_vport *vport,
				struct idpf_buf_queue *bufq)
{
	struct xsk_buff_pool *pool;
	u32 qid = U32_MAX;

	for (u32 i = 0; i < vport->num_rxq_grp; i++) {
		const struct idpf_rxq_group *grp = &vport->rxq_grps[i];

		for (u32 j = 0; j < vport->num_bufqs_per_qgrp; j++) {
			if (&grp->splitq.bufq_sets[j].bufq == bufq) {
				qid = grp->splitq.rxq_sets[0]->rxq.idx;
				goto setup;
			}
		}
	}

setup:
	pool = xsk_get_pool_from_qid(vport->netdev, qid);
	if (!pool || !pool->dev || !xsk_buff_can_alloc(pool, 1))
		return;

	bufq->pool = pool;

	idpf_queue_set(XSK, bufq);
}

static void idpf_xsk_setup_txq(const struct idpf_vport *vport,
			       struct idpf_tx_queue *txq)
{
	struct xsk_buff_pool *pool;
	u32 qid;

	idpf_queue_clear(XSK, txq);

	if (!idpf_queue_has(XDP, txq))
		return;

	qid = txq->idx - vport->xdp_txq_offset;

	pool = xsk_get_pool_from_qid(vport->netdev, qid);
	if (!pool || !pool->dev)
		return;

	txq->pool = pool;
	libeth_xdpsq_init_timer(txq->timer, txq, &txq->xdp_lock,
				idpf_xsk_tx_timer);

	idpf_queue_assign(NOIRQ, txq, xsk_uses_need_wakeup(pool));
	idpf_queue_set(XSK, txq);
}

static void idpf_xsk_setup_complq(const struct idpf_vport *vport,
				  struct idpf_compl_queue *complq)
{
	const struct xsk_buff_pool *pool;
	u32 qid;

	idpf_queue_clear(XSK, complq);

	if (!idpf_queue_has(XDP, complq))
		return;

	qid = complq->txq_grp->txqs[0]->idx - vport->xdp_txq_offset;

	pool = xsk_get_pool_from_qid(vport->netdev, qid);
	if (!pool || !pool->dev)
		return;

	idpf_queue_set(XSK, complq);
}

void idpf_xsk_setup_queue(const struct idpf_vport *vport, void *q,
			  enum virtchnl2_queue_type type)
{
	if (!idpf_xdp_enabled(vport))
		return;

	switch (type) {
	case VIRTCHNL2_QUEUE_TYPE_RX:
		idpf_xsk_setup_rxq(vport, q);
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		idpf_xsk_setup_bufq(vport, q);
		break;
	case VIRTCHNL2_QUEUE_TYPE_TX:
		idpf_xsk_setup_txq(vport, q);
		break;
	case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
		idpf_xsk_setup_complq(vport, q);
		break;
	default:
		break;
	}
}

void idpf_xsk_clear_queue(void *q, enum virtchnl2_queue_type type)
{
	struct idpf_compl_queue *complq;
	struct idpf_buf_queue *bufq;
	struct idpf_rx_queue *rxq;
	struct idpf_tx_queue *txq;

	switch (type) {
	case VIRTCHNL2_QUEUE_TYPE_RX:
		rxq = q;
		if (!idpf_queue_has_clear(XSK, rxq))
			return;

		rxq->pool = NULL;
		break;
	case VIRTCHNL2_QUEUE_TYPE_RX_BUFFER:
		bufq = q;
		if (!idpf_queue_has_clear(XSK, bufq))
			return;

		bufq->pool = NULL;
		break;
	case VIRTCHNL2_QUEUE_TYPE_TX:
		txq = q;
		if (!idpf_queue_has_clear(XSK, txq))
			return;

		idpf_queue_set(NOIRQ, txq);
		txq->dev = txq->netdev->dev.parent;
		break;
	case VIRTCHNL2_QUEUE_TYPE_TX_COMPLETION:
		complq = q;
		idpf_queue_clear(XSK, complq);
		break;
	default:
		break;
	}
}

void idpf_xsk_init_wakeup(struct idpf_q_vector *qv)
{
	libeth_xsk_init_wakeup(&qv->csd, &qv->napi);
}

void idpf_xsksq_clean(struct idpf_tx_queue *xdpsq)
{
	struct libeth_xdpsq_napi_stats ss = { };
	u32 ntc = xdpsq->next_to_clean;
	struct xdp_frame_bulk bq;
	struct libeth_cq_pp cp = {
		.dev	= xdpsq->pool->dev,
		.bq	= &bq,
		.xss	= &ss,
	};
	u32 xsk_frames = 0;

	xdp_frame_bulk_init(&bq);

	while (ntc != xdpsq->next_to_use) {
		struct libeth_sqe *sqe = &xdpsq->tx_buf[ntc];

		if (sqe->type)
			libeth_xdp_complete_tx(sqe, &cp);
		else
			xsk_frames++;

		if (unlikely(++ntc == xdpsq->desc_count))
			ntc = 0;
	}

	xdp_flush_frame_bulk(&bq);

	if (xsk_frames)
		xsk_tx_completed(xdpsq->pool, xsk_frames);
}

static noinline u32 idpf_xsksq_complete_slow(struct idpf_tx_queue *xdpsq,
					     u32 done)
{
	struct libeth_xdpsq_napi_stats ss = { };
	u32 ntc = xdpsq->next_to_clean;
	u32 cnt = xdpsq->desc_count;
	struct xdp_frame_bulk bq;
	struct libeth_cq_pp cp = {
		.dev	= xdpsq->pool->dev,
		.bq	= &bq,
		.xss	= &ss,
		.napi	= true,
	};
	u32 xsk_frames = 0;

	xdp_frame_bulk_init(&bq);

	for (u32 i = 0; likely(i < done); i++) {
		struct libeth_sqe *sqe = &xdpsq->tx_buf[ntc];

		if (sqe->type)
			libeth_xdp_complete_tx(sqe, &cp);
		else
			xsk_frames++;

		if (unlikely(++ntc == cnt))
			ntc = 0;
	}

	xdp_flush_frame_bulk(&bq);

	xdpsq->next_to_clean = ntc;
	xdpsq->xdp_tx -= cp.xdp_tx;

	return xsk_frames;
}

static __always_inline u32 idpf_xsksq_complete(void *_xdpsq, u32 budget)
{
	struct idpf_tx_queue *xdpsq = _xdpsq;
	u32 tx_ntc = xdpsq->next_to_clean;
	u32 tx_cnt = xdpsq->desc_count;
	u32 done_frames;
	u32 xsk_frames;

	done_frames = idpf_xdpsq_poll(xdpsq, budget);
	if (unlikely(!done_frames))
		return 0;

	if (likely(!xdpsq->xdp_tx)) {
		tx_ntc += done_frames;
		if (tx_ntc >= tx_cnt)
			tx_ntc -= tx_cnt;

		xdpsq->next_to_clean = tx_ntc;
		xsk_frames = done_frames;

		goto finalize;
	}

	xsk_frames = idpf_xsksq_complete_slow(xdpsq, done_frames);
	if (xsk_frames)
finalize:
		xsk_tx_completed(xdpsq->pool, xsk_frames);

	xdpsq->pending -= done_frames;

	return done_frames;
}

static u32 idpf_xsk_tx_prep(void *_xdpsq, struct libeth_xdpsq *sq)
{
	struct idpf_tx_queue *xdpsq = _xdpsq;
	u32 free;

	libeth_xdpsq_lock(&xdpsq->xdp_lock);

	free = xdpsq->desc_count - xdpsq->pending;
	if (free < xdpsq->thresh)
		free += idpf_xsksq_complete(xdpsq, xdpsq->thresh);

	*sq = (struct libeth_xdpsq){
		.pool		= xdpsq->pool,
		.sqes		= xdpsq->tx_buf,
		.descs		= xdpsq->desc_ring,
		.count		= xdpsq->desc_count,
		.lock		= &xdpsq->xdp_lock,
		.ntu		= &xdpsq->next_to_use,
		.pending	= &xdpsq->pending,
		.xdp_tx		= &xdpsq->xdp_tx,
	};

	return free;
}

static u32 idpf_xsk_xmit_prep(void *_xdpsq, struct libeth_xdpsq *sq)
{
	struct idpf_tx_queue *xdpsq = _xdpsq;

	*sq = (struct libeth_xdpsq){
		.pool		= xdpsq->pool,
		.sqes		= xdpsq->tx_buf,
		.descs		= xdpsq->desc_ring,
		.count		= xdpsq->desc_count,
		.lock		= &xdpsq->xdp_lock,
		.ntu		= &xdpsq->next_to_use,
		.pending	= &xdpsq->pending,
	};

	/*
	 * The queue is cleaned, the budget is already known, optimize out
	 * the second min() by passing the type limit.
	 */
	return U32_MAX;
}

bool idpf_xsk_xmit(struct idpf_tx_queue *xsksq)
{
	u32 free;

	libeth_xdpsq_lock(&xsksq->xdp_lock);

	free = xsksq->desc_count - xsksq->pending;
	if (free < xsksq->thresh)
		free += idpf_xsksq_complete(xsksq, xsksq->thresh);

	return libeth_xsk_xmit_do_bulk(xsksq->pool, xsksq,
				       min(free - 1, xsksq->thresh),
				       libeth_xsktmo, idpf_xsk_xmit_prep,
				       idpf_xdp_tx_xmit, idpf_xdp_tx_finalize);
}

LIBETH_XDP_DEFINE_START();
LIBETH_XDP_DEFINE_TIMER(static idpf_xsk_tx_timer, idpf_xsksq_complete);
LIBETH_XSK_DEFINE_FLUSH_TX(static idpf_xsk_tx_flush_bulk, idpf_xsk_tx_prep,
			   idpf_xdp_tx_xmit);
LIBETH_XSK_DEFINE_RUN(static idpf_xsk_run_pass, idpf_xsk_run_prog,
		      idpf_xsk_tx_flush_bulk, idpf_rx_process_skb_fields);
LIBETH_XSK_DEFINE_FINALIZE(static idpf_xsk_finalize_rx, idpf_xsk_tx_flush_bulk,
			   idpf_xdp_tx_finalize);
LIBETH_XDP_DEFINE_END();

static void idpf_xskfqe_init(const struct libeth_xskfq_fp *fq, u32 i)
{
	struct virtchnl2_splitq_rx_buf_desc *desc = fq->descs;

	desc = &desc[i];
#ifdef __LIBETH_WORD_ACCESS
	*(u64 *)&desc->qword0 = i;
#else
	desc->qword0.buf_id = cpu_to_le16(i);
#endif
	desc->pkt_addr = cpu_to_le64(libeth_xsk_buff_xdp_get_dma(fq->fqes[i]));
}

static bool idpf_xskfq_refill_thresh(struct idpf_buf_queue *bufq, u32 count)
{
	struct libeth_xskfq_fp fq = {
		.pool	= bufq->pool,
		.fqes	= bufq->xsk_buf,
		.descs	= bufq->split_buf,
		.ntu	= bufq->next_to_use,
		.count	= bufq->desc_count,
	};
	u32 done;

	done = libeth_xskfqe_alloc(&fq, count, idpf_xskfqe_init);
	writel(fq.ntu, bufq->tail);

	bufq->next_to_use = fq.ntu;
	bufq->pending -= done;

	return done == count;
}

static bool idpf_xskfq_refill(struct idpf_buf_queue *bufq)
{
	u32 count, rx_thresh = bufq->thresh;

	count = ALIGN_DOWN(bufq->pending - 1, rx_thresh);

	for (u32 i = 0; i < count; i += rx_thresh) {
		if (unlikely(!idpf_xskfq_refill_thresh(bufq, rx_thresh)))
			return false;
	}

	return true;
}

int idpf_xskfq_init(struct idpf_buf_queue *bufq)
{
	struct libeth_xskfq fq = {
		.pool	= bufq->pool,
		.count	= bufq->desc_count,
		.nid	= idpf_q_vector_to_mem(bufq->q_vector),
	};
	int ret;

	ret = libeth_xskfq_create(&fq);
	if (ret)
		return ret;

	bufq->xsk_buf = fq.fqes;
	bufq->pending = fq.pending;
	bufq->thresh = fq.thresh;
	bufq->rx_buf_size = fq.buf_len;

	if (!idpf_xskfq_refill(bufq))
		netdev_err(bufq->pool->netdev,
			   "failed to allocate XSk buffers for qid %d\n",
			   bufq->pool->queue_id);

	bufq->next_to_alloc = bufq->next_to_use;

	idpf_queue_clear(HSPLIT_EN, bufq);
	bufq->rx_hbuf_size = 0;

	return 0;
}

void idpf_xskfq_rel(struct idpf_buf_queue *bufq)
{
	struct libeth_xskfq fq = {
		.fqes	= bufq->xsk_buf,
	};

	libeth_xskfq_destroy(&fq);

	bufq->rx_buf_size = fq.buf_len;
	bufq->thresh = fq.thresh;
	bufq->pending = fq.pending;
}

struct idpf_xskfq_refill_set {
	struct {
		struct idpf_buf_queue	*q;
		u32			buf_id;
		u32			pending;
	} bufqs[IDPF_MAX_BUFQS_PER_RXQ_GRP];
};

static bool idpf_xskfq_refill_set(const struct idpf_xskfq_refill_set *set)
{
	bool ret = true;

	for (u32 i = 0; i < ARRAY_SIZE(set->bufqs); i++) {
		struct idpf_buf_queue *bufq = set->bufqs[i].q;
		u32 ntc;

		if (!bufq)
			continue;

		ntc = set->bufqs[i].buf_id;
		if (unlikely(++ntc == bufq->desc_count))
			ntc = 0;

		bufq->next_to_clean = ntc;
		bufq->pending += set->bufqs[i].pending;

		if (bufq->pending > bufq->thresh)
			ret &= idpf_xskfq_refill(bufq);
	}

	return ret;
}

int idpf_xskrq_poll(struct idpf_rx_queue *rxq, u32 budget)
{
	struct idpf_xskfq_refill_set set = { };
	struct libeth_rq_napi_stats rs = { };
	bool wake, gen, fail = false;
	u32 ntc = rxq->next_to_clean;
	struct libeth_xdp_buff *xdp;
	LIBETH_XDP_ONSTACK_BULK(bq);
	u32 cnt = rxq->desc_count;

	wake = xsk_uses_need_wakeup(rxq->pool);
	if (wake)
		xsk_clear_rx_need_wakeup(rxq->pool);

	gen = idpf_queue_has(GEN_CHK, rxq);

	libeth_xsk_tx_init_bulk(&bq, rxq->xdp_prog, rxq->xdp_rxq.dev,
				rxq->xdpsqs, rxq->num_xdp_txq);
	xdp = rxq->xsk;

	while (likely(rs.packets < budget)) {
		const struct virtchnl2_rx_flex_desc_adv_nic_3 *rx_desc;
		struct idpf_xdp_rx_desc desc __uninitialized;
		struct idpf_buf_queue *bufq;
		u32 bufq_id, buf_id;

		rx_desc = &rxq->rx[ntc].flex_adv_nic_3_wb;

		idpf_xdp_get_qw0(&desc, rx_desc);
		if (idpf_xdp_rx_gen(&desc) != gen)
			break;

		dma_rmb();

		bufq_id = idpf_xdp_rx_bufq(&desc);
		bufq = set.bufqs[bufq_id].q;
		if (!bufq) {
			bufq = &rxq->bufq_sets[bufq_id].bufq;
			set.bufqs[bufq_id].q = bufq;
		}

		idpf_xdp_get_qw1(&desc, rx_desc);
		buf_id = idpf_xdp_rx_buf(&desc);

		set.bufqs[bufq_id].buf_id = buf_id;
		set.bufqs[bufq_id].pending++;

		xdp = libeth_xsk_process_buff(xdp, bufq->xsk_buf[buf_id],
					      idpf_xdp_rx_len(&desc));

		if (unlikely(++ntc == cnt)) {
			ntc = 0;
			gen = !gen;
			idpf_queue_change(GEN_CHK, rxq);
		}

		if (!idpf_xdp_rx_eop(&desc) || unlikely(!xdp))
			continue;

		fail = !idpf_xsk_run_pass(xdp, &bq, rxq->napi, &rs, rx_desc);
		xdp = NULL;

		if (fail)
			break;
	}

	idpf_xsk_finalize_rx(&bq);

	rxq->next_to_clean = ntc;
	rxq->xsk = xdp;

	fail |= !idpf_xskfq_refill_set(&set);

	u64_stats_update_begin(&rxq->stats_sync);
	u64_stats_add(&rxq->q_stats.packets, rs.packets);
	u64_stats_add(&rxq->q_stats.bytes, rs.bytes);
	u64_stats_update_end(&rxq->stats_sync);

	if (!wake)
		return unlikely(fail) ? budget : rs.packets;

	if (unlikely(fail))
		xsk_set_rx_need_wakeup(rxq->pool);

	return rs.packets;
}

int idpf_xsk_pool_setup(struct idpf_vport *vport, struct netdev_bpf *bpf)
{
	struct xsk_buff_pool *pool = bpf->xsk.pool;
	u32 qid = bpf->xsk.queue_id;
	bool restart;
	int ret;

	if (pool && !IS_ALIGNED(xsk_pool_get_rx_frame_size(pool),
				LIBETH_RX_BUF_STRIDE)) {
		NL_SET_ERR_MSG_FMT_MOD(bpf->extack,
				       "%s: HW doesn't support frames sizes not aligned to %u (qid %u: %u)",
				       netdev_name(vport->netdev),
				       LIBETH_RX_BUF_STRIDE, qid,
				       xsk_pool_get_rx_frame_size(pool));
		return -EINVAL;
	}

	restart = idpf_xdp_enabled(vport) && netif_running(vport->netdev);
	if (!restart)
		goto pool;

	ret = idpf_qp_switch(vport, qid, false);
	if (ret) {
		NL_SET_ERR_MSG_FMT_MOD(bpf->extack,
				       "%s: failed to disable queue pair %u: %pe",
				       netdev_name(vport->netdev), qid,
				       ERR_PTR(ret));
		return ret;
	}

pool:
	ret = libeth_xsk_setup_pool(vport->netdev, qid, pool);
	if (ret) {
		NL_SET_ERR_MSG_FMT_MOD(bpf->extack,
				       "%s: failed to configure XSk pool for pair %u: %pe",
				       netdev_name(vport->netdev), qid,
				       ERR_PTR(ret));
		return ret;
	}

	if (!restart)
		return 0;

	ret = idpf_qp_switch(vport, qid, true);
	if (ret) {
		NL_SET_ERR_MSG_FMT_MOD(bpf->extack,
				       "%s: failed to enable queue pair %u: %pe",
				       netdev_name(vport->netdev), qid,
				       ERR_PTR(ret));
		goto err_dis;
	}

	return 0;

err_dis:
	libeth_xsk_setup_pool(vport->netdev, qid, false);

	return ret;
}

int idpf_xsk_wakeup(struct net_device *dev, u32 qid, u32 flags)
{
	const struct idpf_netdev_priv *np = netdev_priv(dev);
	const struct idpf_vport *vport = np->vport;
	struct idpf_q_vector *q_vector;

	if (unlikely(idpf_vport_ctrl_is_locked(dev)))
		return -EBUSY;

	if (unlikely(!vport->link_up))
		return -ENETDOWN;

	if (unlikely(!vport->num_xdp_txq))
		return -ENXIO;

	q_vector = idpf_find_rxq_vec(vport, qid);
	if (unlikely(!q_vector->xsksq))
		return -ENXIO;

	libeth_xsk_wakeup(&q_vector->csd, qid);

	return 0;
}
