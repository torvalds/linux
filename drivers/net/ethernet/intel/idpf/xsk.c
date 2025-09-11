// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <net/libeth/xsk.h>

#include "idpf.h"
#include "xdp.h"
#include "xsk.h"

static void idpf_xsk_tx_timer(struct work_struct *work);

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
	struct idpf_tx_queue *txq;

	switch (type) {
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
LIBETH_XDP_DEFINE_END();

int idpf_xsk_pool_setup(struct idpf_vport *vport, struct netdev_bpf *bpf)
{
	struct xsk_buff_pool *pool = bpf->xsk.pool;
	u32 qid = bpf->xsk.queue_id;
	bool restart;
	int ret;

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
