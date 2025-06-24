// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include "ionic_ibdev.h"

static int ionic_validate_qdesc(struct ionic_qdesc *q)
{
	if (!q->addr || !q->size || !q->mask ||
	    !q->depth_log2 || !q->stride_log2)
		return -EINVAL;

	if (q->addr & (PAGE_SIZE - 1))
		return -EINVAL;

	if (q->mask != BIT(q->depth_log2) - 1)
		return -EINVAL;

	if (q->size < BIT_ULL(q->depth_log2 + q->stride_log2))
		return -EINVAL;

	return 0;
}

static u32 ionic_get_eqid(struct ionic_ibdev *dev, u32 comp_vector, u8 udma_idx)
{
	/* EQ per vector per udma, and the first eqs reserved for async events.
	 * The rest of the vectors can be requested for completions.
	 */
	u32 comp_vec_count = dev->lif_cfg.eq_count / dev->lif_cfg.udma_count - 1;

	return (comp_vector % comp_vec_count + 1) * dev->lif_cfg.udma_count + udma_idx;
}

static int ionic_get_cqid(struct ionic_ibdev *dev, u32 *cqid, u8 udma_idx)
{
	unsigned int size, base, bound;
	int rc;

	size = dev->lif_cfg.cq_count / dev->lif_cfg.udma_count;
	base = size * udma_idx;
	bound = base + size;

	rc = ionic_resid_get_shared(&dev->inuse_cqid, base, bound);
	if (rc >= 0) {
		/* cq_base is zero or a multiple of two queue groups */
		*cqid = dev->lif_cfg.cq_base +
			ionic_bitid_to_qid(rc, dev->lif_cfg.udma_qgrp_shift,
					   dev->half_cqid_udma_shift);

		rc = 0;
	}

	return rc;
}

static void ionic_put_cqid(struct ionic_ibdev *dev, u32 cqid)
{
	u32 bitid = ionic_qid_to_bitid(cqid - dev->lif_cfg.cq_base,
				       dev->lif_cfg.udma_qgrp_shift,
				       dev->half_cqid_udma_shift);

	ionic_resid_put(&dev->inuse_cqid, bitid);
}

int ionic_create_cq_common(struct ionic_vcq *vcq,
			   struct ionic_tbl_buf *buf,
			   const struct ib_cq_init_attr *attr,
			   struct ionic_ctx *ctx,
			   struct ib_udata *udata,
			   struct ionic_qdesc *req_cq,
			   __u32 *resp_cqid,
			   int udma_idx)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(vcq->ibcq.device);
	struct ionic_cq *cq = &vcq->cq[udma_idx];
	void *entry;
	int rc;

	cq->vcq = vcq;

	if (attr->cqe < 1 || attr->cqe + IONIC_CQ_GRACE > 0xffff) {
		rc = -EINVAL;
		goto err_args;
	}

	rc = ionic_get_cqid(dev, &cq->cqid, udma_idx);
	if (rc)
		goto err_cqid;

	cq->eqid = ionic_get_eqid(dev, attr->comp_vector, udma_idx);

	spin_lock_init(&cq->lock);
	INIT_LIST_HEAD(&cq->poll_sq);
	INIT_LIST_HEAD(&cq->flush_sq);
	INIT_LIST_HEAD(&cq->flush_rq);

	if (udata) {
		rc = ionic_validate_qdesc(req_cq);
		if (rc)
			goto err_qdesc;

		cq->umem = ib_umem_get(&dev->ibdev, req_cq->addr, req_cq->size,
				       IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(cq->umem)) {
			rc = PTR_ERR(cq->umem);
			goto err_umem;
		}

		cq->q.ptr = NULL;
		cq->q.size = req_cq->size;
		cq->q.mask = req_cq->mask;
		cq->q.depth_log2 = req_cq->depth_log2;
		cq->q.stride_log2 = req_cq->stride_log2;

		*resp_cqid = cq->cqid;
	} else {
		rc = ionic_queue_init(&cq->q, dev->lif_cfg.hwdev,
				      attr->cqe + IONIC_CQ_GRACE,
				      sizeof(struct ionic_v1_cqe));
		if (rc)
			goto err_q_init;

		ionic_queue_dbell_init(&cq->q, cq->cqid);
		cq->color = true;
		cq->credit = cq->q.mask;
	}

	rc = ionic_pgtbl_init(dev, buf, cq->umem, cq->q.dma, 1, PAGE_SIZE);
	if (rc)
		goto err_pgtbl_init;

	init_completion(&cq->cq_rel_comp);
	kref_init(&cq->cq_kref);

	entry = xa_store_irq(&dev->cq_tbl, cq->cqid, cq, GFP_KERNEL);
	if (entry) {
		if (!xa_is_err(entry))
			rc = -EINVAL;
		else
			rc = xa_err(entry);

		goto err_xa;
	}

	return 0;

err_xa:
	ionic_pgtbl_unbuf(dev, buf);
err_pgtbl_init:
	if (!udata)
		ionic_queue_destroy(&cq->q, dev->lif_cfg.hwdev);
err_q_init:
	if (cq->umem)
		ib_umem_release(cq->umem);
err_umem:
err_qdesc:
	ionic_put_cqid(dev, cq->cqid);
err_cqid:
err_args:
	cq->vcq = NULL;

	return rc;
}

void ionic_destroy_cq_common(struct ionic_ibdev *dev, struct ionic_cq *cq)
{
	if (!cq->vcq)
		return;

	xa_erase_irq(&dev->cq_tbl, cq->cqid);
	synchronize_rcu();

	kref_put(&cq->cq_kref, ionic_cq_complete);
	wait_for_completion(&cq->cq_rel_comp);

	if (cq->umem)
		ib_umem_release(cq->umem);
	else
		ionic_queue_destroy(&cq->q, dev->lif_cfg.hwdev);

	ionic_put_cqid(dev, cq->cqid);

	cq->vcq = NULL;
}
