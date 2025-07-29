// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"

int mana_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct uverbs_attr_bundle *attrs)
{
	struct ib_udata *udata = &attrs->driver_udata;
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct mana_ib_create_cq_resp resp = {};
	struct mana_ib_ucontext *mana_ucontext;
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_create_cq ucmd = {};
	struct mana_ib_dev *mdev;
	bool is_rnic_cq;
	u32 doorbell;
	u32 buf_size;
	int err;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	cq->comp_vector = attr->comp_vector % ibdev->num_comp_vectors;
	cq->cq_handle = INVALID_MANA_HANDLE;

	if (udata) {
		if (udata->inlen < offsetof(struct mana_ib_create_cq, flags))
			return -EINVAL;

		err = ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd), udata->inlen));
		if (err) {
			ibdev_dbg(ibdev, "Failed to copy from udata for create cq, %d\n", err);
			return err;
		}

		is_rnic_cq = !!(ucmd.flags & MANA_IB_CREATE_RNIC_CQ);

		if ((!is_rnic_cq && attr->cqe > mdev->adapter_caps.max_qp_wr) ||
		    attr->cqe > U32_MAX / COMP_ENTRY_SIZE) {
			ibdev_dbg(ibdev, "CQE %d exceeding limit\n", attr->cqe);
			return -EINVAL;
		}

		cq->cqe = attr->cqe;
		err = mana_ib_create_queue(mdev, ucmd.buf_addr, cq->cqe * COMP_ENTRY_SIZE,
					   &cq->queue);
		if (err) {
			ibdev_dbg(ibdev, "Failed to create queue for create cq, %d\n", err);
			return err;
		}

		mana_ucontext = rdma_udata_to_drv_context(udata, struct mana_ib_ucontext,
							  ibucontext);
		doorbell = mana_ucontext->doorbell;
	} else {
		is_rnic_cq = true;
		buf_size = MANA_PAGE_ALIGN(roundup_pow_of_two(attr->cqe * COMP_ENTRY_SIZE));
		cq->cqe = buf_size / COMP_ENTRY_SIZE;
		err = mana_ib_create_kernel_queue(mdev, buf_size, GDMA_CQ, &cq->queue);
		if (err) {
			ibdev_dbg(ibdev, "Failed to create kernel queue for create cq, %d\n", err);
			return err;
		}
		doorbell = mdev->gdma_dev->doorbell;
	}

	if (is_rnic_cq) {
		err = mana_ib_gd_create_cq(mdev, cq, doorbell);
		if (err) {
			ibdev_dbg(ibdev, "Failed to create RNIC cq, %d\n", err);
			goto err_destroy_queue;
		}

		err = mana_ib_install_cq_cb(mdev, cq);
		if (err) {
			ibdev_dbg(ibdev, "Failed to install cq callback, %d\n", err);
			goto err_destroy_rnic_cq;
		}
	}

	if (udata) {
		resp.cqid = cq->queue.id;
		err = ib_copy_to_udata(udata, &resp, min(sizeof(resp), udata->outlen));
		if (err) {
			ibdev_dbg(&mdev->ib_dev, "Failed to copy to udata, %d\n", err);
			goto err_remove_cq_cb;
		}
	}

	spin_lock_init(&cq->cq_lock);
	INIT_LIST_HEAD(&cq->list_send_qp);
	INIT_LIST_HEAD(&cq->list_recv_qp);

	return 0;

err_remove_cq_cb:
	mana_ib_remove_cq_cb(mdev, cq);
err_destroy_rnic_cq:
	mana_ib_gd_destroy_cq(mdev, cq);
err_destroy_queue:
	mana_ib_destroy_queue(mdev, &cq->queue);

	return err;
}

int mana_ib_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct ib_device *ibdev = ibcq->device;
	struct mana_ib_dev *mdev;

	mdev = container_of(ibdev, struct mana_ib_dev, ib_dev);

	mana_ib_remove_cq_cb(mdev, cq);

	/* Ignore return code as there is not much we can do about it.
	 * The error message is printed inside.
	 */
	mana_ib_gd_destroy_cq(mdev, cq);

	mana_ib_destroy_queue(mdev, &cq->queue);

	return 0;
}

static void mana_ib_cq_handler(void *ctx, struct gdma_queue *gdma_cq)
{
	struct mana_ib_cq *cq = ctx;

	if (cq->ibcq.comp_handler)
		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}

int mana_ib_install_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq)
{
	struct gdma_context *gc = mdev_to_gc(mdev);
	struct gdma_queue *gdma_cq;

	if (cq->queue.id >= gc->max_num_cqs)
		return -EINVAL;
	/* Create CQ table entry */
	WARN_ON(gc->cq_table[cq->queue.id]);
	if (cq->queue.kmem)
		gdma_cq = cq->queue.kmem;
	else
		gdma_cq = kzalloc(sizeof(*gdma_cq), GFP_KERNEL);
	if (!gdma_cq)
		return -ENOMEM;

	gdma_cq->cq.context = cq;
	gdma_cq->type = GDMA_CQ;
	gdma_cq->cq.callback = mana_ib_cq_handler;
	gdma_cq->id = cq->queue.id;
	gc->cq_table[cq->queue.id] = gdma_cq;
	return 0;
}

void mana_ib_remove_cq_cb(struct mana_ib_dev *mdev, struct mana_ib_cq *cq)
{
	struct gdma_context *gc = mdev_to_gc(mdev);

	if (cq->queue.id >= gc->max_num_cqs || cq->queue.id == INVALID_QUEUE_ID)
		return;

	if (cq->queue.kmem)
	/* Then it will be cleaned and removed by the mana */
		return;

	kfree(gc->cq_table[cq->queue.id]);
	gc->cq_table[cq->queue.id] = NULL;
}

int mana_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct gdma_queue *gdma_cq = cq->queue.kmem;

	if (!gdma_cq)
		return -EINVAL;

	mana_gd_ring_cq(gdma_cq, SET_ARM_BIT);
	return 0;
}

static inline void handle_ud_sq_cqe(struct mana_ib_qp *qp, struct gdma_comp *cqe)
{
	struct mana_rdma_cqe *rdma_cqe = (struct mana_rdma_cqe *)cqe->cqe_data;
	struct gdma_queue *wq = qp->ud_qp.queues[MANA_UD_SEND_QUEUE].kmem;
	struct ud_sq_shadow_wqe *shadow_wqe;

	shadow_wqe = shadow_queue_get_next_to_complete(&qp->shadow_sq);
	if (!shadow_wqe)
		return;

	shadow_wqe->header.error_code = rdma_cqe->ud_send.vendor_error;

	wq->tail += shadow_wqe->header.posted_wqe_size;
	shadow_queue_advance_next_to_complete(&qp->shadow_sq);
}

static inline void handle_ud_rq_cqe(struct mana_ib_qp *qp, struct gdma_comp *cqe)
{
	struct mana_rdma_cqe *rdma_cqe = (struct mana_rdma_cqe *)cqe->cqe_data;
	struct gdma_queue *wq = qp->ud_qp.queues[MANA_UD_RECV_QUEUE].kmem;
	struct ud_rq_shadow_wqe *shadow_wqe;

	shadow_wqe = shadow_queue_get_next_to_complete(&qp->shadow_rq);
	if (!shadow_wqe)
		return;

	shadow_wqe->byte_len = rdma_cqe->ud_recv.msg_len;
	shadow_wqe->src_qpn = rdma_cqe->ud_recv.src_qpn;
	shadow_wqe->header.error_code = IB_WC_SUCCESS;

	wq->tail += shadow_wqe->header.posted_wqe_size;
	shadow_queue_advance_next_to_complete(&qp->shadow_rq);
}

static void mana_handle_cqe(struct mana_ib_dev *mdev, struct gdma_comp *cqe)
{
	struct mana_ib_qp *qp = mana_get_qp_ref(mdev, cqe->wq_num, cqe->is_sq);

	if (!qp)
		return;

	if (qp->ibqp.qp_type == IB_QPT_GSI || qp->ibqp.qp_type == IB_QPT_UD) {
		if (cqe->is_sq)
			handle_ud_sq_cqe(qp, cqe);
		else
			handle_ud_rq_cqe(qp, cqe);
	}

	mana_put_qp_ref(qp);
}

static void fill_verbs_from_shadow_wqe(struct mana_ib_qp *qp, struct ib_wc *wc,
				       const struct shadow_wqe_header *shadow_wqe)
{
	const struct ud_rq_shadow_wqe *ud_wqe = (const struct ud_rq_shadow_wqe *)shadow_wqe;

	wc->wr_id = shadow_wqe->wr_id;
	wc->status = shadow_wqe->error_code;
	wc->opcode = shadow_wqe->opcode;
	wc->vendor_err = shadow_wqe->error_code;
	wc->wc_flags = 0;
	wc->qp = &qp->ibqp;
	wc->pkey_index = 0;

	if (shadow_wqe->opcode == IB_WC_RECV) {
		wc->byte_len = ud_wqe->byte_len;
		wc->src_qp = ud_wqe->src_qpn;
		wc->wc_flags |= IB_WC_GRH;
	}
}

static int mana_process_completions(struct mana_ib_cq *cq, int nwc, struct ib_wc *wc)
{
	struct shadow_wqe_header *shadow_wqe;
	struct mana_ib_qp *qp;
	int wc_index = 0;

	/* process send shadow queue completions  */
	list_for_each_entry(qp, &cq->list_send_qp, cq_send_list) {
		while ((shadow_wqe = shadow_queue_get_next_to_consume(&qp->shadow_sq))
				!= NULL) {
			if (wc_index >= nwc)
				goto out;

			fill_verbs_from_shadow_wqe(qp, &wc[wc_index], shadow_wqe);
			shadow_queue_advance_consumer(&qp->shadow_sq);
			wc_index++;
		}
	}

	/* process recv shadow queue completions */
	list_for_each_entry(qp, &cq->list_recv_qp, cq_recv_list) {
		while ((shadow_wqe = shadow_queue_get_next_to_consume(&qp->shadow_rq))
				!= NULL) {
			if (wc_index >= nwc)
				goto out;

			fill_verbs_from_shadow_wqe(qp, &wc[wc_index], shadow_wqe);
			shadow_queue_advance_consumer(&qp->shadow_rq);
			wc_index++;
		}
	}

out:
	return wc_index;
}

void mana_drain_gsi_sqs(struct mana_ib_dev *mdev)
{
	struct mana_ib_qp *qp = mana_get_qp_ref(mdev, MANA_GSI_QPN, false);
	struct ud_sq_shadow_wqe *shadow_wqe;
	struct mana_ib_cq *cq;
	unsigned long flags;

	if (!qp)
		return;

	cq = container_of(qp->ibqp.send_cq, struct mana_ib_cq, ibcq);

	spin_lock_irqsave(&cq->cq_lock, flags);
	while ((shadow_wqe = shadow_queue_get_next_to_complete(&qp->shadow_sq))
			!= NULL) {
		shadow_wqe->header.error_code = IB_WC_GENERAL_ERR;
		shadow_queue_advance_next_to_complete(&qp->shadow_sq);
	}
	spin_unlock_irqrestore(&cq->cq_lock, flags);

	if (cq->ibcq.comp_handler)
		cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);

	mana_put_qp_ref(qp);
}

int mana_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct mana_ib_cq *cq = container_of(ibcq, struct mana_ib_cq, ibcq);
	struct mana_ib_dev *mdev = container_of(ibcq->device, struct mana_ib_dev, ib_dev);
	struct gdma_queue *queue = cq->queue.kmem;
	struct gdma_comp gdma_cqe;
	unsigned long flags;
	int num_polled = 0;
	int comp_read, i;

	spin_lock_irqsave(&cq->cq_lock, flags);
	for (i = 0; i < num_entries; i++) {
		comp_read = mana_gd_poll_cq(queue, &gdma_cqe, 1);
		if (comp_read < 1)
			break;
		mana_handle_cqe(mdev, &gdma_cqe);
	}

	num_polled = mana_process_completions(cq, num_entries, wc);
	spin_unlock_irqrestore(&cq->cq_lock, flags);

	return num_polled;
}
