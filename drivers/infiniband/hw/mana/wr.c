// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

#define MAX_WR_SGL_NUM (2)

static int mana_ib_post_rq(struct mana_ib_qp *qp, const struct ib_recv_wr *wr)
{
	struct ib_sge mana_ib_dummy_sge = {.addr = 1, .length = 0, .lkey = 0};
	struct mana_ib_queue *ib_rq = mana_qp_get_rq(qp);
	struct gdma_posted_wqe_info wqe_info = {0};
	struct gdma_queue *queue = ib_rq->kmem;
	struct gdma_wqe_request wqe_req = {0};
	struct shadow_wqe_header *shadow_wqe;
	int err;

	if (shadow_queue_full(&qp->shadow_rq))
		return -EINVAL;

	if (wr->num_sge > MAX_RX_WQE_SGL_ENTRIES)
		return -EINVAL;

	wqe_req.num_sge = wr->num_sge;
	wqe_req.ib_sgl = wr->sg_list;
	wqe_req.flags = GDMA_WR_IB_SGL;

	if (wr->num_sge == 0) {
		wqe_req.ib_sgl = &mana_ib_dummy_sge;
		wqe_req.num_sge = 1;
	}

	err = mana_gd_post_work_request(queue, &wqe_req, &wqe_info);
	if (err)
		return err;

	shadow_wqe = shadow_queue_producer_entry(&qp->shadow_rq);
	memset(shadow_wqe, 0, sizeof(*shadow_wqe));
	shadow_wqe->wr_id = wr->wr_id;
	shadow_wqe->wqe_size_in_bu = wqe_info.wqe_size_in_bu;
	shadow_wqe->wqe_offset_or_psn = wqe_info.wqe_offset;
	shadow_queue_advance_producer(&qp->shadow_rq);

	return 0;
}

static int mana_ib_post_recv_ud(struct mana_ib_qp *qp, const struct ib_recv_wr *wr,
				const struct ib_recv_wr **bad_wr)
{
	struct mana_ib_dev *mdev = container_of(qp->ibqp.device, struct mana_ib_dev, ib_dev);
	struct mana_ib_queue *ib_rq = mana_qp_get_rq(qp);
	struct gdma_queue *rq = ib_rq->kmem;
	bool ring_rq = false;
	int err = 0;

	for (; wr; wr = wr->next) {
		err = mana_ib_post_rq(qp, wr);
		if (unlikely(err)) {
			*bad_wr = wr;
			break;
		}
		ring_rq = true;
	}

	if (ring_rq)
		mana_gd_wq_ring_doorbell(mdev_to_gc(mdev), rq);

	return err;
}

int mana_ib_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr)
{
	struct mana_ib_qp *qp = container_of(ibqp, struct mana_ib_qp, ibqp);

	switch (ibqp->qp_type) {
	case IB_QPT_UD:
	case IB_QPT_GSI:
		return mana_ib_post_recv_ud(qp, wr, bad_wr);
	default:
		/* Unsupported QP type */
		*bad_wr = wr;
		return -EINVAL;
	}
}

static int mana_ib_post_send_ud_one(struct mana_ib_qp *qp, const struct ib_ud_wr *wr)
{
	struct mana_ib_dev *mdev = container_of(qp->ibqp.device, struct mana_ib_dev, ib_dev);
	struct mana_ib_ah *ah = container_of(wr->ah, struct mana_ib_ah, ibah);
	struct net_device *ndev = mana_ib_get_netdev(&mdev->ib_dev, qp->port);
	struct gdma_queue *queue = qp->ud_qp.queues[MANA_UD_SEND_QUEUE].kmem;
	struct gdma_sge gdma_sgl[MAX_WR_SGL_NUM + 1];
	struct gdma_posted_wqe_info wqe_info = {0};
	struct gdma_wqe_request wqe_req = {0};
	struct rdma_send_oob send_oob = {0};
	struct shadow_wqe_header *shadow_wqe;
	bool signaled = qp->sq_sig_all || (wr->wr.send_flags & IB_SEND_SIGNALED);
	int err, i;

	if (!ndev)
		return -EINVAL;

	if (wr->wr.opcode != IB_WR_SEND)
		return -EINVAL;

	if (shadow_queue_full(&qp->shadow_sq))
		return -EINVAL;

	if (wr->wr.num_sge > MAX_WR_SGL_NUM)
		return -EINVAL;

	gdma_sgl[0].address = ah->dma_handle;
	gdma_sgl[0].mem_key = qp->ibqp.pd->local_dma_lkey;
	gdma_sgl[0].size = sizeof(struct mana_ib_av);
	for (i = 0; i < wr->wr.num_sge; ++i) {
		gdma_sgl[i + 1].address = wr->wr.sg_list[i].addr;
		gdma_sgl[i + 1].mem_key = wr->wr.sg_list[i].lkey;
		gdma_sgl[i + 1].size = wr->wr.sg_list[i].length;
	}

	wqe_req.num_sge = wr->wr.num_sge + 1;
	wqe_req.sgl = gdma_sgl;
	wqe_req.inline_oob_size = INLINE_OOB_LARGE_SIZE;
	wqe_req.inline_oob_data = &send_oob;
	wqe_req.flags = GDMA_WR_OOB_IN_SGL;
	wqe_req.client_data_unit = ib_mtu_enum_to_int(ib_mtu_int_to_enum(ndev->mtu));

	send_oob.wqe_type = WQE_TYPE_UD_SEND;
	send_oob.fence = !!(wr->wr.send_flags & IB_SEND_FENCE);
	send_oob.signaled = signaled;
	send_oob.solicited = !!(wr->wr.send_flags & IB_SEND_SOLICITED);
	send_oob.psn = qp->sq_psn;
	send_oob.ssn_or_rqpn = wr->remote_qpn;
	send_oob.ud_send.remote_qkey =
		qp->ibqp.qp_type == IB_QPT_GSI ? IB_QP1_QKEY : wr->remote_qkey;

	err = mana_gd_post_work_request(queue, &wqe_req, &wqe_info);
	if (err)
		return err;

	qp->sq_psn++;
	shadow_wqe = shadow_queue_producer_entry(&qp->shadow_sq);
	memset(shadow_wqe, 0, sizeof(*shadow_wqe));
	shadow_wqe->wr_id = wr->wr.wr_id;
	shadow_wqe->flags = signaled ? 0 : MANA_WQ_NO_SIGNAL_WC;
	shadow_wqe->wqe_size_in_bu = wqe_info.wqe_size_in_bu;
	shadow_wqe->wqe_offset_or_psn = wqe_info.wqe_offset;
	shadow_queue_advance_producer(&qp->shadow_sq);

	return 0;
}

static int mana_ib_post_send_ud(struct mana_ib_qp *qp, const struct ib_send_wr *wr,
				const struct ib_send_wr **bad_wr)
{
	struct mana_ib_dev *mdev = container_of(qp->ibqp.device, struct mana_ib_dev, ib_dev);
	struct mana_ib_cq *cq = container_of(qp->ibqp.send_cq, struct mana_ib_cq, ibcq);
	struct gdma_queue *queue = qp->ud_qp.queues[MANA_UD_SEND_QUEUE].kmem;
	unsigned long flags;
	bool ring_sq = false;
	bool flush_send;
	int err = 0;

	for (; wr; wr = wr->next) {
		err = mana_ib_post_send_ud_one(qp, ud_wr(wr));
		if (unlikely(err)) {
			*bad_wr = wr;
			break;
		}
		ring_sq = true;
	}

	if (ring_sq) {
		mana_gd_wq_ring_doorbell(mdev_to_gc(mdev), queue);

		spin_lock_irqsave(&cq->cq_lock, flags);
		flush_send = !list_empty(&qp->send_err_node);
		spin_unlock_irqrestore(&cq->cq_lock, flags);

		if (flush_send && cq->ibcq.comp_handler)
			cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
	}

	return err;
}

int mana_ib_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		      const struct ib_send_wr **bad_wr)
{
	struct mana_ib_qp *qp = container_of(ibqp, struct mana_ib_qp, ibqp);

	switch (ibqp->qp_type) {
	case IB_QPT_UD:
	case IB_QPT_GSI:
		return mana_ib_post_send_ud(qp, wr, bad_wr);
	default:
		/* Unsupported QP type */
		*bad_wr = wr;
		return -EINVAL;
	}
}
