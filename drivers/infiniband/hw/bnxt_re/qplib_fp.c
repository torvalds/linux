/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Fast Path Operators
 */

#define dev_fmt(fmt) "QPLIB: " fmt

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/prefetch.h>
#include <linux/if_ether.h>

#include "roce_hsi.h"

#include "qplib_res.h"
#include "qplib_rcfw.h"
#include "qplib_sp.h"
#include "qplib_fp.h"

static void bnxt_qplib_arm_cq_enable(struct bnxt_qplib_cq *cq);
static void __clean_cq(struct bnxt_qplib_cq *cq, u64 qp);
static void bnxt_qplib_arm_srq(struct bnxt_qplib_srq *srq, u32 arm_type);

static void bnxt_qplib_cancel_phantom_processing(struct bnxt_qplib_qp *qp)
{
	qp->sq.condition = false;
	qp->sq.send_phantom = false;
	qp->sq.single = false;
}

/* Flush list */
static void __bnxt_qplib_add_flush_qp(struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_cq *scq, *rcq;

	scq = qp->scq;
	rcq = qp->rcq;

	if (!qp->sq.flushed) {
		dev_dbg(&scq->hwq.pdev->dev,
			"FP: Adding to SQ Flush list = %p\n", qp);
		bnxt_qplib_cancel_phantom_processing(qp);
		list_add_tail(&qp->sq_flush, &scq->sqf_head);
		qp->sq.flushed = true;
	}
	if (!qp->srq) {
		if (!qp->rq.flushed) {
			dev_dbg(&rcq->hwq.pdev->dev,
				"FP: Adding to RQ Flush list = %p\n", qp);
			list_add_tail(&qp->rq_flush, &rcq->rqf_head);
			qp->rq.flushed = true;
		}
	}
}

static void bnxt_qplib_acquire_cq_flush_locks(struct bnxt_qplib_qp *qp,
				       unsigned long *flags)
	__acquires(&qp->scq->flush_lock) __acquires(&qp->rcq->flush_lock)
{
	spin_lock_irqsave(&qp->scq->flush_lock, *flags);
	if (qp->scq == qp->rcq)
		__acquire(&qp->rcq->flush_lock);
	else
		spin_lock(&qp->rcq->flush_lock);
}

static void bnxt_qplib_release_cq_flush_locks(struct bnxt_qplib_qp *qp,
				       unsigned long *flags)
	__releases(&qp->scq->flush_lock) __releases(&qp->rcq->flush_lock)
{
	if (qp->scq == qp->rcq)
		__release(&qp->rcq->flush_lock);
	else
		spin_unlock(&qp->rcq->flush_lock);
	spin_unlock_irqrestore(&qp->scq->flush_lock, *flags);
}

void bnxt_qplib_add_flush_qp(struct bnxt_qplib_qp *qp)
{
	unsigned long flags;

	bnxt_qplib_acquire_cq_flush_locks(qp, &flags);
	__bnxt_qplib_add_flush_qp(qp);
	bnxt_qplib_release_cq_flush_locks(qp, &flags);
}

static void __bnxt_qplib_del_flush_qp(struct bnxt_qplib_qp *qp)
{
	if (qp->sq.flushed) {
		qp->sq.flushed = false;
		list_del(&qp->sq_flush);
	}
	if (!qp->srq) {
		if (qp->rq.flushed) {
			qp->rq.flushed = false;
			list_del(&qp->rq_flush);
		}
	}
}

void bnxt_qplib_clean_qp(struct bnxt_qplib_qp *qp)
{
	unsigned long flags;

	bnxt_qplib_acquire_cq_flush_locks(qp, &flags);
	__clean_cq(qp->scq, (u64)(unsigned long)qp);
	qp->sq.hwq.prod = 0;
	qp->sq.hwq.cons = 0;
	__clean_cq(qp->rcq, (u64)(unsigned long)qp);
	qp->rq.hwq.prod = 0;
	qp->rq.hwq.cons = 0;

	__bnxt_qplib_del_flush_qp(qp);
	bnxt_qplib_release_cq_flush_locks(qp, &flags);
}

static void bnxt_qpn_cqn_sched_task(struct work_struct *work)
{
	struct bnxt_qplib_nq_work *nq_work =
			container_of(work, struct bnxt_qplib_nq_work, work);

	struct bnxt_qplib_cq *cq = nq_work->cq;
	struct bnxt_qplib_nq *nq = nq_work->nq;

	if (cq && nq) {
		spin_lock_bh(&cq->compl_lock);
		if (atomic_read(&cq->arm_state) && nq->cqn_handler) {
			dev_dbg(&nq->pdev->dev,
				"%s:Trigger cq  = %p event nq = %p\n",
				__func__, cq, nq);
			nq->cqn_handler(nq, cq);
		}
		spin_unlock_bh(&cq->compl_lock);
	}
	kfree(nq_work);
}

static void bnxt_qplib_free_qp_hdr_buf(struct bnxt_qplib_res *res,
				       struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_q *rq = &qp->rq;
	struct bnxt_qplib_q *sq = &qp->sq;

	if (qp->rq_hdr_buf)
		dma_free_coherent(&res->pdev->dev,
				  rq->hwq.max_elements * qp->rq_hdr_buf_size,
				  qp->rq_hdr_buf, qp->rq_hdr_buf_map);
	if (qp->sq_hdr_buf)
		dma_free_coherent(&res->pdev->dev,
				  sq->hwq.max_elements * qp->sq_hdr_buf_size,
				  qp->sq_hdr_buf, qp->sq_hdr_buf_map);
	qp->rq_hdr_buf = NULL;
	qp->sq_hdr_buf = NULL;
	qp->rq_hdr_buf_map = 0;
	qp->sq_hdr_buf_map = 0;
	qp->sq_hdr_buf_size = 0;
	qp->rq_hdr_buf_size = 0;
}

static int bnxt_qplib_alloc_qp_hdr_buf(struct bnxt_qplib_res *res,
				       struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_q *rq = &qp->rq;
	struct bnxt_qplib_q *sq = &qp->sq;
	int rc = 0;

	if (qp->sq_hdr_buf_size && sq->hwq.max_elements) {
		qp->sq_hdr_buf = dma_alloc_coherent(&res->pdev->dev,
					sq->hwq.max_elements *
					qp->sq_hdr_buf_size,
					&qp->sq_hdr_buf_map, GFP_KERNEL);
		if (!qp->sq_hdr_buf) {
			rc = -ENOMEM;
			dev_err(&res->pdev->dev,
				"Failed to create sq_hdr_buf\n");
			goto fail;
		}
	}

	if (qp->rq_hdr_buf_size && rq->hwq.max_elements) {
		qp->rq_hdr_buf = dma_alloc_coherent(&res->pdev->dev,
						    rq->hwq.max_elements *
						    qp->rq_hdr_buf_size,
						    &qp->rq_hdr_buf_map,
						    GFP_KERNEL);
		if (!qp->rq_hdr_buf) {
			rc = -ENOMEM;
			dev_err(&res->pdev->dev,
				"Failed to create rq_hdr_buf\n");
			goto fail;
		}
	}
	return 0;

fail:
	bnxt_qplib_free_qp_hdr_buf(res, qp);
	return rc;
}

static void bnxt_qplib_service_nq(unsigned long data)
{
	struct bnxt_qplib_nq *nq = (struct bnxt_qplib_nq *)data;
	struct bnxt_qplib_hwq *hwq = &nq->hwq;
	struct nq_base *nqe, **nq_ptr;
	struct bnxt_qplib_cq *cq;
	int num_cqne_processed = 0;
	int num_srqne_processed = 0;
	u32 sw_cons, raw_cons;
	u16 type;
	int budget = nq->budget;
	uintptr_t q_handle;
	bool gen_p5 = bnxt_qplib_is_chip_gen_p5(nq->res->cctx);

	/* Service the NQ until empty */
	raw_cons = hwq->cons;
	while (budget--) {
		sw_cons = HWQ_CMP(raw_cons, hwq);
		nq_ptr = (struct nq_base **)hwq->pbl_ptr;
		nqe = &nq_ptr[NQE_PG(sw_cons)][NQE_IDX(sw_cons)];
		if (!NQE_CMP_VALID(nqe, raw_cons, hwq->max_elements))
			break;

		/*
		 * The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();

		type = le16_to_cpu(nqe->info10_type) & NQ_BASE_TYPE_MASK;
		switch (type) {
		case NQ_BASE_TYPE_CQ_NOTIFICATION:
		{
			struct nq_cn *nqcne = (struct nq_cn *)nqe;

			q_handle = le32_to_cpu(nqcne->cq_handle_low);
			q_handle |= (u64)le32_to_cpu(nqcne->cq_handle_high)
						     << 32;
			cq = (struct bnxt_qplib_cq *)(unsigned long)q_handle;
			bnxt_qplib_arm_cq_enable(cq);
			spin_lock_bh(&cq->compl_lock);
			atomic_set(&cq->arm_state, 0);
			if (!nq->cqn_handler(nq, (cq)))
				num_cqne_processed++;
			else
				dev_warn(&nq->pdev->dev,
					 "cqn - type 0x%x not handled\n", type);
			spin_unlock_bh(&cq->compl_lock);
			break;
		}
		case NQ_BASE_TYPE_SRQ_EVENT:
		{
			struct nq_srq_event *nqsrqe =
						(struct nq_srq_event *)nqe;

			q_handle = le32_to_cpu(nqsrqe->srq_handle_low);
			q_handle |= (u64)le32_to_cpu(nqsrqe->srq_handle_high)
				     << 32;
			bnxt_qplib_arm_srq((struct bnxt_qplib_srq *)q_handle,
					   DBC_DBC_TYPE_SRQ_ARMENA);
			if (!nq->srqn_handler(nq,
					      (struct bnxt_qplib_srq *)q_handle,
					      nqsrqe->event))
				num_srqne_processed++;
			else
				dev_warn(&nq->pdev->dev,
					 "SRQ event 0x%x not handled\n",
					 nqsrqe->event);
			break;
		}
		case NQ_BASE_TYPE_DBQ_EVENT:
			break;
		default:
			dev_warn(&nq->pdev->dev,
				 "nqe with type = 0x%x not handled\n", type);
			break;
		}
		raw_cons++;
	}
	if (hwq->cons != raw_cons) {
		hwq->cons = raw_cons;
		bnxt_qplib_ring_nq_db_rearm(nq->bar_reg_iomem, hwq->cons,
					    hwq->max_elements, nq->ring_id,
					    gen_p5);
	}
}

static irqreturn_t bnxt_qplib_nq_irq(int irq, void *dev_instance)
{
	struct bnxt_qplib_nq *nq = dev_instance;
	struct bnxt_qplib_hwq *hwq = &nq->hwq;
	struct nq_base **nq_ptr;
	u32 sw_cons;

	/* Prefetch the NQ element */
	sw_cons = HWQ_CMP(hwq->cons, hwq);
	nq_ptr = (struct nq_base **)nq->hwq.pbl_ptr;
	prefetch(&nq_ptr[NQE_PG(sw_cons)][NQE_IDX(sw_cons)]);

	/* Fan out to CPU affinitized kthreads? */
	tasklet_schedule(&nq->worker);

	return IRQ_HANDLED;
}

void bnxt_qplib_nq_stop_irq(struct bnxt_qplib_nq *nq, bool kill)
{
	bool gen_p5 = bnxt_qplib_is_chip_gen_p5(nq->res->cctx);
	tasklet_disable(&nq->worker);
	/* Mask h/w interrupt */
	bnxt_qplib_ring_nq_db(nq->bar_reg_iomem, nq->hwq.cons,
			      nq->hwq.max_elements, nq->ring_id, gen_p5);
	/* Sync with last running IRQ handler */
	synchronize_irq(nq->vector);
	if (kill)
		tasklet_kill(&nq->worker);
	if (nq->requested) {
		irq_set_affinity_hint(nq->vector, NULL);
		free_irq(nq->vector, nq);
		nq->requested = false;
	}
}

void bnxt_qplib_disable_nq(struct bnxt_qplib_nq *nq)
{
	if (nq->cqn_wq) {
		destroy_workqueue(nq->cqn_wq);
		nq->cqn_wq = NULL;
	}

	/* Make sure the HW is stopped! */
	if (nq->requested)
		bnxt_qplib_nq_stop_irq(nq, true);

	if (nq->bar_reg_iomem)
		iounmap(nq->bar_reg_iomem);
	nq->bar_reg_iomem = NULL;

	nq->cqn_handler = NULL;
	nq->srqn_handler = NULL;
	nq->vector = 0;
}

int bnxt_qplib_nq_start_irq(struct bnxt_qplib_nq *nq, int nq_indx,
			    int msix_vector, bool need_init)
{
	bool gen_p5 = bnxt_qplib_is_chip_gen_p5(nq->res->cctx);
	int rc;

	if (nq->requested)
		return -EFAULT;

	nq->vector = msix_vector;
	if (need_init)
		tasklet_init(&nq->worker, bnxt_qplib_service_nq,
			     (unsigned long)nq);
	else
		tasklet_enable(&nq->worker);

	snprintf(nq->name, sizeof(nq->name), "bnxt_qplib_nq-%d", nq_indx);
	rc = request_irq(nq->vector, bnxt_qplib_nq_irq, 0, nq->name, nq);
	if (rc)
		return rc;

	cpumask_clear(&nq->mask);
	cpumask_set_cpu(nq_indx, &nq->mask);
	rc = irq_set_affinity_hint(nq->vector, &nq->mask);
	if (rc) {
		dev_warn(&nq->pdev->dev,
			 "set affinity failed; vector: %d nq_idx: %d\n",
			 nq->vector, nq_indx);
	}
	nq->requested = true;
	bnxt_qplib_ring_nq_db_rearm(nq->bar_reg_iomem, nq->hwq.cons,
				    nq->hwq.max_elements, nq->ring_id, gen_p5);

	return rc;
}

int bnxt_qplib_enable_nq(struct pci_dev *pdev, struct bnxt_qplib_nq *nq,
			 int nq_idx, int msix_vector, int bar_reg_offset,
			 int (*cqn_handler)(struct bnxt_qplib_nq *nq,
					    struct bnxt_qplib_cq *),
			 int (*srqn_handler)(struct bnxt_qplib_nq *nq,
					     struct bnxt_qplib_srq *,
					     u8 event))
{
	resource_size_t nq_base;
	int rc = -1;

	if (cqn_handler)
		nq->cqn_handler = cqn_handler;

	if (srqn_handler)
		nq->srqn_handler = srqn_handler;

	/* Have a task to schedule CQ notifiers in post send case */
	nq->cqn_wq  = create_singlethread_workqueue("bnxt_qplib_nq");
	if (!nq->cqn_wq)
		return -ENOMEM;

	nq->bar_reg = NQ_CONS_PCI_BAR_REGION;
	nq->bar_reg_off = bar_reg_offset;
	nq_base = pci_resource_start(pdev, nq->bar_reg);
	if (!nq_base) {
		rc = -ENOMEM;
		goto fail;
	}
	/* Unconditionally map 8 bytes to support 57500 series */
	nq->bar_reg_iomem = ioremap_nocache(nq_base + nq->bar_reg_off, 8);
	if (!nq->bar_reg_iomem) {
		rc = -ENOMEM;
		goto fail;
	}

	rc = bnxt_qplib_nq_start_irq(nq, nq_idx, msix_vector, true);
	if (rc) {
		dev_err(&nq->pdev->dev,
			"Failed to request irq for nq-idx %d\n", nq_idx);
		goto fail;
	}

	return 0;
fail:
	bnxt_qplib_disable_nq(nq);
	return rc;
}

void bnxt_qplib_free_nq(struct bnxt_qplib_nq *nq)
{
	if (nq->hwq.max_elements) {
		bnxt_qplib_free_hwq(nq->pdev, &nq->hwq);
		nq->hwq.max_elements = 0;
	}
}

int bnxt_qplib_alloc_nq(struct pci_dev *pdev, struct bnxt_qplib_nq *nq)
{
	u8 hwq_type;

	nq->pdev = pdev;
	if (!nq->hwq.max_elements ||
	    nq->hwq.max_elements > BNXT_QPLIB_NQE_MAX_CNT)
		nq->hwq.max_elements = BNXT_QPLIB_NQE_MAX_CNT;
	hwq_type = bnxt_qplib_get_hwq_type(nq->res);
	if (bnxt_qplib_alloc_init_hwq(nq->pdev, &nq->hwq, NULL,
				      &nq->hwq.max_elements,
				      BNXT_QPLIB_MAX_NQE_ENTRY_SIZE, 0,
				      PAGE_SIZE, hwq_type))
		return -ENOMEM;

	nq->budget = 8;
	return 0;
}

/* SRQ */
static void bnxt_qplib_arm_srq(struct bnxt_qplib_srq *srq, u32 arm_type)
{
	struct bnxt_qplib_hwq *srq_hwq = &srq->hwq;
	void __iomem *db;
	u32 sw_prod;
	u64 val = 0;

	/* Ring DB */
	sw_prod = (arm_type == DBC_DBC_TYPE_SRQ_ARM) ?
		   srq->threshold : HWQ_CMP(srq_hwq->prod, srq_hwq);
	db = (arm_type == DBC_DBC_TYPE_SRQ_ARMENA) ? srq->dbr_base :
						     srq->dpi->dbr;
	val = ((srq->id << DBC_DBC_XID_SFT) & DBC_DBC_XID_MASK) | arm_type;
	val <<= 32;
	val |= (sw_prod << DBC_DBC_INDEX_SFT) & DBC_DBC_INDEX_MASK;
	writeq(val, db);
}

void bnxt_qplib_destroy_srq(struct bnxt_qplib_res *res,
			   struct bnxt_qplib_srq *srq)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_destroy_srq req;
	struct creq_destroy_srq_resp resp;
	u16 cmd_flags = 0;
	int rc;

	RCFW_CMD_PREP(req, DESTROY_SRQ, cmd_flags);

	/* Configure the request */
	req.srq_cid = cpu_to_le32(srq->id);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (struct cmdq_base *)&req,
					  (struct creq_base *)&resp, NULL, 0);
	kfree(srq->swq);
	if (rc)
		return;
	bnxt_qplib_free_hwq(res->pdev, &srq->hwq);
}

int bnxt_qplib_create_srq(struct bnxt_qplib_res *res,
			  struct bnxt_qplib_srq *srq)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_create_srq req;
	struct creq_create_srq_resp resp;
	struct bnxt_qplib_pbl *pbl;
	u16 cmd_flags = 0;
	int rc, idx;

	srq->hwq.max_elements = srq->max_wqe;
	rc = bnxt_qplib_alloc_init_hwq(res->pdev, &srq->hwq, &srq->sg_info,
				       &srq->hwq.max_elements,
				       BNXT_QPLIB_MAX_RQE_ENTRY_SIZE, 0,
				       PAGE_SIZE, HWQ_TYPE_QUEUE);
	if (rc)
		goto exit;

	srq->swq = kcalloc(srq->hwq.max_elements, sizeof(*srq->swq),
			   GFP_KERNEL);
	if (!srq->swq) {
		rc = -ENOMEM;
		goto fail;
	}

	RCFW_CMD_PREP(req, CREATE_SRQ, cmd_flags);

	/* Configure the request */
	req.dpi = cpu_to_le32(srq->dpi->dpi);
	req.srq_handle = cpu_to_le64((uintptr_t)srq);

	req.srq_size = cpu_to_le16((u16)srq->hwq.max_elements);
	pbl = &srq->hwq.pbl[PBL_LVL_0];
	req.pg_size_lvl = cpu_to_le16((((u16)srq->hwq.level &
				      CMDQ_CREATE_SRQ_LVL_MASK) <<
				      CMDQ_CREATE_SRQ_LVL_SFT) |
				      (pbl->pg_size == ROCE_PG_SIZE_4K ?
				       CMDQ_CREATE_SRQ_PG_SIZE_PG_4K :
				       pbl->pg_size == ROCE_PG_SIZE_8K ?
				       CMDQ_CREATE_SRQ_PG_SIZE_PG_8K :
				       pbl->pg_size == ROCE_PG_SIZE_64K ?
				       CMDQ_CREATE_SRQ_PG_SIZE_PG_64K :
				       pbl->pg_size == ROCE_PG_SIZE_2M ?
				       CMDQ_CREATE_SRQ_PG_SIZE_PG_2M :
				       pbl->pg_size == ROCE_PG_SIZE_8M ?
				       CMDQ_CREATE_SRQ_PG_SIZE_PG_8M :
				       pbl->pg_size == ROCE_PG_SIZE_1G ?
				       CMDQ_CREATE_SRQ_PG_SIZE_PG_1G :
				       CMDQ_CREATE_SRQ_PG_SIZE_PG_4K));
	req.pbl = cpu_to_le64(pbl->pg_map_arr[0]);
	req.pd_id = cpu_to_le32(srq->pd->id);
	req.eventq_id = cpu_to_le16(srq->eventq_hw_ring_id);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		goto fail;

	spin_lock_init(&srq->lock);
	srq->start_idx = 0;
	srq->last_idx = srq->hwq.max_elements - 1;
	for (idx = 0; idx < srq->hwq.max_elements; idx++)
		srq->swq[idx].next_idx = idx + 1;
	srq->swq[srq->last_idx].next_idx = -1;

	srq->id = le32_to_cpu(resp.xid);
	srq->dbr_base = res->dpi_tbl.dbr_bar_reg_iomem;
	if (srq->threshold)
		bnxt_qplib_arm_srq(srq, DBC_DBC_TYPE_SRQ_ARMENA);
	srq->arm_req = false;

	return 0;
fail:
	bnxt_qplib_free_hwq(res->pdev, &srq->hwq);
	kfree(srq->swq);
exit:
	return rc;
}

int bnxt_qplib_modify_srq(struct bnxt_qplib_res *res,
			  struct bnxt_qplib_srq *srq)
{
	struct bnxt_qplib_hwq *srq_hwq = &srq->hwq;
	u32 sw_prod, sw_cons, count = 0;

	sw_prod = HWQ_CMP(srq_hwq->prod, srq_hwq);
	sw_cons = HWQ_CMP(srq_hwq->cons, srq_hwq);

	count = sw_prod > sw_cons ? sw_prod - sw_cons :
				    srq_hwq->max_elements - sw_cons + sw_prod;
	if (count > srq->threshold) {
		srq->arm_req = false;
		bnxt_qplib_arm_srq(srq, DBC_DBC_TYPE_SRQ_ARM);
	} else {
		/* Deferred arming */
		srq->arm_req = true;
	}

	return 0;
}

int bnxt_qplib_query_srq(struct bnxt_qplib_res *res,
			 struct bnxt_qplib_srq *srq)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_query_srq req;
	struct creq_query_srq_resp resp;
	struct bnxt_qplib_rcfw_sbuf *sbuf;
	struct creq_query_srq_resp_sb *sb;
	u16 cmd_flags = 0;
	int rc = 0;

	RCFW_CMD_PREP(req, QUERY_SRQ, cmd_flags);
	req.srq_cid = cpu_to_le32(srq->id);

	/* Configure the request */
	sbuf = bnxt_qplib_rcfw_alloc_sbuf(rcfw, sizeof(*sb));
	if (!sbuf)
		return -ENOMEM;
	sb = sbuf->sb;
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  (void *)sbuf, 0);
	srq->threshold = le16_to_cpu(sb->srq_limit);
	bnxt_qplib_rcfw_free_sbuf(rcfw, sbuf);

	return rc;
}

int bnxt_qplib_post_srq_recv(struct bnxt_qplib_srq *srq,
			     struct bnxt_qplib_swqe *wqe)
{
	struct bnxt_qplib_hwq *srq_hwq = &srq->hwq;
	struct rq_wqe *srqe, **srqe_ptr;
	struct sq_sge *hw_sge;
	u32 sw_prod, sw_cons, count = 0;
	int i, rc = 0, next;

	spin_lock(&srq_hwq->lock);
	if (srq->start_idx == srq->last_idx) {
		dev_err(&srq_hwq->pdev->dev,
			"FP: SRQ (0x%x) is full!\n", srq->id);
		rc = -EINVAL;
		spin_unlock(&srq_hwq->lock);
		goto done;
	}
	next = srq->start_idx;
	srq->start_idx = srq->swq[next].next_idx;
	spin_unlock(&srq_hwq->lock);

	sw_prod = HWQ_CMP(srq_hwq->prod, srq_hwq);
	srqe_ptr = (struct rq_wqe **)srq_hwq->pbl_ptr;
	srqe = &srqe_ptr[RQE_PG(sw_prod)][RQE_IDX(sw_prod)];
	memset(srqe, 0, BNXT_QPLIB_MAX_RQE_ENTRY_SIZE);
	/* Calculate wqe_size16 and data_len */
	for (i = 0, hw_sge = (struct sq_sge *)srqe->data;
	     i < wqe->num_sge; i++, hw_sge++) {
		hw_sge->va_or_pa = cpu_to_le64(wqe->sg_list[i].addr);
		hw_sge->l_key = cpu_to_le32(wqe->sg_list[i].lkey);
		hw_sge->size = cpu_to_le32(wqe->sg_list[i].size);
	}
	srqe->wqe_type = wqe->type;
	srqe->flags = wqe->flags;
	srqe->wqe_size = wqe->num_sge +
			((offsetof(typeof(*srqe), data) + 15) >> 4);
	srqe->wr_id[0] = cpu_to_le32((u32)next);
	srq->swq[next].wr_id = wqe->wr_id;

	srq_hwq->prod++;

	spin_lock(&srq_hwq->lock);
	sw_prod = HWQ_CMP(srq_hwq->prod, srq_hwq);
	/* retaining srq_hwq->cons for this logic
	 * actually the lock is only required to
	 * read srq_hwq->cons.
	 */
	sw_cons = HWQ_CMP(srq_hwq->cons, srq_hwq);
	count = sw_prod > sw_cons ? sw_prod - sw_cons :
				    srq_hwq->max_elements - sw_cons + sw_prod;
	spin_unlock(&srq_hwq->lock);
	/* Ring DB */
	bnxt_qplib_arm_srq(srq, DBC_DBC_TYPE_SRQ);
	if (srq->arm_req == true && count > srq->threshold) {
		srq->arm_req = false;
		bnxt_qplib_arm_srq(srq, DBC_DBC_TYPE_SRQ_ARM);
	}
done:
	return rc;
}

/* QP */
int bnxt_qplib_create_qp1(struct bnxt_qplib_res *res, struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_create_qp1 req;
	struct creq_create_qp1_resp resp;
	struct bnxt_qplib_pbl *pbl;
	struct bnxt_qplib_q *sq = &qp->sq;
	struct bnxt_qplib_q *rq = &qp->rq;
	int rc;
	u16 cmd_flags = 0;
	u32 qp_flags = 0;

	RCFW_CMD_PREP(req, CREATE_QP1, cmd_flags);

	/* General */
	req.type = qp->type;
	req.dpi = cpu_to_le32(qp->dpi->dpi);
	req.qp_handle = cpu_to_le64(qp->qp_handle);

	/* SQ */
	sq->hwq.max_elements = sq->max_wqe;
	rc = bnxt_qplib_alloc_init_hwq(res->pdev, &sq->hwq, NULL,
				       &sq->hwq.max_elements,
				       BNXT_QPLIB_MAX_SQE_ENTRY_SIZE, 0,
				       PAGE_SIZE, HWQ_TYPE_QUEUE);
	if (rc)
		goto exit;

	sq->swq = kcalloc(sq->hwq.max_elements, sizeof(*sq->swq), GFP_KERNEL);
	if (!sq->swq) {
		rc = -ENOMEM;
		goto fail_sq;
	}
	pbl = &sq->hwq.pbl[PBL_LVL_0];
	req.sq_pbl = cpu_to_le64(pbl->pg_map_arr[0]);
	req.sq_pg_size_sq_lvl =
		((sq->hwq.level & CMDQ_CREATE_QP1_SQ_LVL_MASK)
				<<  CMDQ_CREATE_QP1_SQ_LVL_SFT) |
		(pbl->pg_size == ROCE_PG_SIZE_4K ?
				CMDQ_CREATE_QP1_SQ_PG_SIZE_PG_4K :
		 pbl->pg_size == ROCE_PG_SIZE_8K ?
				CMDQ_CREATE_QP1_SQ_PG_SIZE_PG_8K :
		 pbl->pg_size == ROCE_PG_SIZE_64K ?
				CMDQ_CREATE_QP1_SQ_PG_SIZE_PG_64K :
		 pbl->pg_size == ROCE_PG_SIZE_2M ?
				CMDQ_CREATE_QP1_SQ_PG_SIZE_PG_2M :
		 pbl->pg_size == ROCE_PG_SIZE_8M ?
				CMDQ_CREATE_QP1_SQ_PG_SIZE_PG_8M :
		 pbl->pg_size == ROCE_PG_SIZE_1G ?
				CMDQ_CREATE_QP1_SQ_PG_SIZE_PG_1G :
		 CMDQ_CREATE_QP1_SQ_PG_SIZE_PG_4K);

	if (qp->scq)
		req.scq_cid = cpu_to_le32(qp->scq->id);

	qp_flags |= CMDQ_CREATE_QP1_QP_FLAGS_RESERVED_LKEY_ENABLE;

	/* RQ */
	if (rq->max_wqe) {
		rq->hwq.max_elements = qp->rq.max_wqe;
		rc = bnxt_qplib_alloc_init_hwq(res->pdev, &rq->hwq, NULL,
					       &rq->hwq.max_elements,
					       BNXT_QPLIB_MAX_RQE_ENTRY_SIZE, 0,
					       PAGE_SIZE, HWQ_TYPE_QUEUE);
		if (rc)
			goto fail_sq;

		rq->swq = kcalloc(rq->hwq.max_elements, sizeof(*rq->swq),
				  GFP_KERNEL);
		if (!rq->swq) {
			rc = -ENOMEM;
			goto fail_rq;
		}
		pbl = &rq->hwq.pbl[PBL_LVL_0];
		req.rq_pbl = cpu_to_le64(pbl->pg_map_arr[0]);
		req.rq_pg_size_rq_lvl =
			((rq->hwq.level & CMDQ_CREATE_QP1_RQ_LVL_MASK) <<
			 CMDQ_CREATE_QP1_RQ_LVL_SFT) |
				(pbl->pg_size == ROCE_PG_SIZE_4K ?
					CMDQ_CREATE_QP1_RQ_PG_SIZE_PG_4K :
				 pbl->pg_size == ROCE_PG_SIZE_8K ?
					CMDQ_CREATE_QP1_RQ_PG_SIZE_PG_8K :
				 pbl->pg_size == ROCE_PG_SIZE_64K ?
					CMDQ_CREATE_QP1_RQ_PG_SIZE_PG_64K :
				 pbl->pg_size == ROCE_PG_SIZE_2M ?
					CMDQ_CREATE_QP1_RQ_PG_SIZE_PG_2M :
				 pbl->pg_size == ROCE_PG_SIZE_8M ?
					CMDQ_CREATE_QP1_RQ_PG_SIZE_PG_8M :
				 pbl->pg_size == ROCE_PG_SIZE_1G ?
					CMDQ_CREATE_QP1_RQ_PG_SIZE_PG_1G :
				 CMDQ_CREATE_QP1_RQ_PG_SIZE_PG_4K);
		if (qp->rcq)
			req.rcq_cid = cpu_to_le32(qp->rcq->id);
	}

	/* Header buffer - allow hdr_buf pass in */
	rc = bnxt_qplib_alloc_qp_hdr_buf(res, qp);
	if (rc) {
		rc = -ENOMEM;
		goto fail;
	}
	req.qp_flags = cpu_to_le32(qp_flags);
	req.sq_size = cpu_to_le32(sq->hwq.max_elements);
	req.rq_size = cpu_to_le32(rq->hwq.max_elements);

	req.sq_fwo_sq_sge =
		cpu_to_le16((sq->max_sge & CMDQ_CREATE_QP1_SQ_SGE_MASK) <<
			    CMDQ_CREATE_QP1_SQ_SGE_SFT);
	req.rq_fwo_rq_sge =
		cpu_to_le16((rq->max_sge & CMDQ_CREATE_QP1_RQ_SGE_MASK) <<
			    CMDQ_CREATE_QP1_RQ_SGE_SFT);

	req.pd_id = cpu_to_le32(qp->pd->id);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		goto fail;

	qp->id = le32_to_cpu(resp.xid);
	qp->cur_qp_state = CMDQ_MODIFY_QP_NEW_STATE_RESET;
	rcfw->qp_tbl[qp->id].qp_id = qp->id;
	rcfw->qp_tbl[qp->id].qp_handle = (void *)qp;

	return 0;

fail:
	bnxt_qplib_free_qp_hdr_buf(res, qp);
fail_rq:
	bnxt_qplib_free_hwq(res->pdev, &rq->hwq);
	kfree(rq->swq);
fail_sq:
	bnxt_qplib_free_hwq(res->pdev, &sq->hwq);
	kfree(sq->swq);
exit:
	return rc;
}

int bnxt_qplib_create_qp(struct bnxt_qplib_res *res, struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	unsigned long int psn_search, poff = 0;
	struct sq_psn_search **psn_search_ptr;
	struct bnxt_qplib_q *sq = &qp->sq;
	struct bnxt_qplib_q *rq = &qp->rq;
	int i, rc, req_size, psn_sz = 0;
	struct sq_send **hw_sq_send_ptr;
	struct creq_create_qp_resp resp;
	struct bnxt_qplib_hwq *xrrq;
	u16 cmd_flags = 0, max_ssge;
	struct cmdq_create_qp req;
	struct bnxt_qplib_pbl *pbl;
	u32 qp_flags = 0;
	u16 max_rsge;

	RCFW_CMD_PREP(req, CREATE_QP, cmd_flags);

	/* General */
	req.type = qp->type;
	req.dpi = cpu_to_le32(qp->dpi->dpi);
	req.qp_handle = cpu_to_le64(qp->qp_handle);

	/* SQ */
	if (qp->type == CMDQ_CREATE_QP_TYPE_RC) {
		psn_sz = bnxt_qplib_is_chip_gen_p5(res->cctx) ?
			 sizeof(struct sq_psn_search_ext) :
			 sizeof(struct sq_psn_search);
	}
	sq->hwq.max_elements = sq->max_wqe;
	rc = bnxt_qplib_alloc_init_hwq(res->pdev, &sq->hwq, &sq->sg_info,
				       &sq->hwq.max_elements,
				       BNXT_QPLIB_MAX_SQE_ENTRY_SIZE,
				       psn_sz,
				       PAGE_SIZE, HWQ_TYPE_QUEUE);
	if (rc)
		goto exit;

	sq->swq = kcalloc(sq->hwq.max_elements, sizeof(*sq->swq), GFP_KERNEL);
	if (!sq->swq) {
		rc = -ENOMEM;
		goto fail_sq;
	}
	hw_sq_send_ptr = (struct sq_send **)sq->hwq.pbl_ptr;
	if (psn_sz) {
		psn_search_ptr = (struct sq_psn_search **)
				  &hw_sq_send_ptr[get_sqe_pg
					(sq->hwq.max_elements)];
		psn_search = (unsigned long int)
			      &hw_sq_send_ptr[get_sqe_pg(sq->hwq.max_elements)]
			      [get_sqe_idx(sq->hwq.max_elements)];
		if (psn_search & ~PAGE_MASK) {
			/* If the psn_search does not start on a page boundary,
			 * then calculate the offset
			 */
			poff = (psn_search & ~PAGE_MASK) /
				BNXT_QPLIB_MAX_PSNE_ENTRY_SIZE;
		}
		for (i = 0; i < sq->hwq.max_elements; i++) {
			sq->swq[i].psn_search =
				&psn_search_ptr[get_psne_pg(i + poff)]
					       [get_psne_idx(i + poff)];
			/*psns_ext will be used only for P5 chips. */
			sq->swq[i].psn_ext =
				(struct sq_psn_search_ext *)
				&psn_search_ptr[get_psne_pg(i + poff)]
					       [get_psne_idx(i + poff)];
		}
	}
	pbl = &sq->hwq.pbl[PBL_LVL_0];
	req.sq_pbl = cpu_to_le64(pbl->pg_map_arr[0]);
	req.sq_pg_size_sq_lvl =
		((sq->hwq.level & CMDQ_CREATE_QP_SQ_LVL_MASK)
				 <<  CMDQ_CREATE_QP_SQ_LVL_SFT) |
		(pbl->pg_size == ROCE_PG_SIZE_4K ?
				CMDQ_CREATE_QP_SQ_PG_SIZE_PG_4K :
		 pbl->pg_size == ROCE_PG_SIZE_8K ?
				CMDQ_CREATE_QP_SQ_PG_SIZE_PG_8K :
		 pbl->pg_size == ROCE_PG_SIZE_64K ?
				CMDQ_CREATE_QP_SQ_PG_SIZE_PG_64K :
		 pbl->pg_size == ROCE_PG_SIZE_2M ?
				CMDQ_CREATE_QP_SQ_PG_SIZE_PG_2M :
		 pbl->pg_size == ROCE_PG_SIZE_8M ?
				CMDQ_CREATE_QP_SQ_PG_SIZE_PG_8M :
		 pbl->pg_size == ROCE_PG_SIZE_1G ?
				CMDQ_CREATE_QP_SQ_PG_SIZE_PG_1G :
		 CMDQ_CREATE_QP_SQ_PG_SIZE_PG_4K);

	if (qp->scq)
		req.scq_cid = cpu_to_le32(qp->scq->id);

	qp_flags |= CMDQ_CREATE_QP_QP_FLAGS_RESERVED_LKEY_ENABLE;
	qp_flags |= CMDQ_CREATE_QP_QP_FLAGS_FR_PMR_ENABLED;
	if (qp->sig_type)
		qp_flags |= CMDQ_CREATE_QP_QP_FLAGS_FORCE_COMPLETION;

	/* RQ */
	if (rq->max_wqe) {
		rq->hwq.max_elements = rq->max_wqe;
		rc = bnxt_qplib_alloc_init_hwq(res->pdev, &rq->hwq,
					       &rq->sg_info,
					       &rq->hwq.max_elements,
					       BNXT_QPLIB_MAX_RQE_ENTRY_SIZE, 0,
					       PAGE_SIZE, HWQ_TYPE_QUEUE);
		if (rc)
			goto fail_sq;

		rq->swq = kcalloc(rq->hwq.max_elements, sizeof(*rq->swq),
				  GFP_KERNEL);
		if (!rq->swq) {
			rc = -ENOMEM;
			goto fail_rq;
		}
		pbl = &rq->hwq.pbl[PBL_LVL_0];
		req.rq_pbl = cpu_to_le64(pbl->pg_map_arr[0]);
		req.rq_pg_size_rq_lvl =
			((rq->hwq.level & CMDQ_CREATE_QP_RQ_LVL_MASK) <<
			 CMDQ_CREATE_QP_RQ_LVL_SFT) |
				(pbl->pg_size == ROCE_PG_SIZE_4K ?
					CMDQ_CREATE_QP_RQ_PG_SIZE_PG_4K :
				 pbl->pg_size == ROCE_PG_SIZE_8K ?
					CMDQ_CREATE_QP_RQ_PG_SIZE_PG_8K :
				 pbl->pg_size == ROCE_PG_SIZE_64K ?
					CMDQ_CREATE_QP_RQ_PG_SIZE_PG_64K :
				 pbl->pg_size == ROCE_PG_SIZE_2M ?
					CMDQ_CREATE_QP_RQ_PG_SIZE_PG_2M :
				 pbl->pg_size == ROCE_PG_SIZE_8M ?
					CMDQ_CREATE_QP_RQ_PG_SIZE_PG_8M :
				 pbl->pg_size == ROCE_PG_SIZE_1G ?
					CMDQ_CREATE_QP_RQ_PG_SIZE_PG_1G :
				 CMDQ_CREATE_QP_RQ_PG_SIZE_PG_4K);
	} else {
		/* SRQ */
		if (qp->srq) {
			qp_flags |= CMDQ_CREATE_QP_QP_FLAGS_SRQ_USED;
			req.srq_cid = cpu_to_le32(qp->srq->id);
		}
	}

	if (qp->rcq)
		req.rcq_cid = cpu_to_le32(qp->rcq->id);
	req.qp_flags = cpu_to_le32(qp_flags);
	req.sq_size = cpu_to_le32(sq->hwq.max_elements);
	req.rq_size = cpu_to_le32(rq->hwq.max_elements);
	qp->sq_hdr_buf = NULL;
	qp->rq_hdr_buf = NULL;

	rc = bnxt_qplib_alloc_qp_hdr_buf(res, qp);
	if (rc)
		goto fail_rq;

	/* CTRL-22434: Irrespective of the requested SGE count on the SQ
	 * always create the QP with max send sges possible if the requested
	 * inline size is greater than 0.
	 */
	max_ssge = qp->max_inline_data ? 6 : sq->max_sge;
	req.sq_fwo_sq_sge = cpu_to_le16(
				((max_ssge & CMDQ_CREATE_QP_SQ_SGE_MASK)
				 << CMDQ_CREATE_QP_SQ_SGE_SFT) | 0);
	max_rsge = bnxt_qplib_is_chip_gen_p5(res->cctx) ? 6 : rq->max_sge;
	req.rq_fwo_rq_sge = cpu_to_le16(
				((max_rsge & CMDQ_CREATE_QP_RQ_SGE_MASK)
				 << CMDQ_CREATE_QP_RQ_SGE_SFT) | 0);
	/* ORRQ and IRRQ */
	if (psn_sz) {
		xrrq = &qp->orrq;
		xrrq->max_elements =
			ORD_LIMIT_TO_ORRQ_SLOTS(qp->max_rd_atomic);
		req_size = xrrq->max_elements *
			   BNXT_QPLIB_MAX_ORRQE_ENTRY_SIZE + PAGE_SIZE - 1;
		req_size &= ~(PAGE_SIZE - 1);
		rc = bnxt_qplib_alloc_init_hwq(res->pdev, xrrq, NULL,
					       &xrrq->max_elements,
					       BNXT_QPLIB_MAX_ORRQE_ENTRY_SIZE,
					       0, req_size, HWQ_TYPE_CTX);
		if (rc)
			goto fail_buf_free;
		pbl = &xrrq->pbl[PBL_LVL_0];
		req.orrq_addr = cpu_to_le64(pbl->pg_map_arr[0]);

		xrrq = &qp->irrq;
		xrrq->max_elements = IRD_LIMIT_TO_IRRQ_SLOTS(
						qp->max_dest_rd_atomic);
		req_size = xrrq->max_elements *
			   BNXT_QPLIB_MAX_IRRQE_ENTRY_SIZE + PAGE_SIZE - 1;
		req_size &= ~(PAGE_SIZE - 1);

		rc = bnxt_qplib_alloc_init_hwq(res->pdev, xrrq, NULL,
					       &xrrq->max_elements,
					       BNXT_QPLIB_MAX_IRRQE_ENTRY_SIZE,
					       0, req_size, HWQ_TYPE_CTX);
		if (rc)
			goto fail_orrq;

		pbl = &xrrq->pbl[PBL_LVL_0];
		req.irrq_addr = cpu_to_le64(pbl->pg_map_arr[0]);
	}
	req.pd_id = cpu_to_le32(qp->pd->id);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		goto fail;

	qp->id = le32_to_cpu(resp.xid);
	qp->cur_qp_state = CMDQ_MODIFY_QP_NEW_STATE_RESET;
	qp->cctx = res->cctx;
	INIT_LIST_HEAD(&qp->sq_flush);
	INIT_LIST_HEAD(&qp->rq_flush);
	rcfw->qp_tbl[qp->id].qp_id = qp->id;
	rcfw->qp_tbl[qp->id].qp_handle = (void *)qp;

	return 0;

fail:
	if (qp->irrq.max_elements)
		bnxt_qplib_free_hwq(res->pdev, &qp->irrq);
fail_orrq:
	if (qp->orrq.max_elements)
		bnxt_qplib_free_hwq(res->pdev, &qp->orrq);
fail_buf_free:
	bnxt_qplib_free_qp_hdr_buf(res, qp);
fail_rq:
	bnxt_qplib_free_hwq(res->pdev, &rq->hwq);
	kfree(rq->swq);
fail_sq:
	bnxt_qplib_free_hwq(res->pdev, &sq->hwq);
	kfree(sq->swq);
exit:
	return rc;
}

static void __modify_flags_from_init_state(struct bnxt_qplib_qp *qp)
{
	switch (qp->state) {
	case CMDQ_MODIFY_QP_NEW_STATE_RTR:
		/* INIT->RTR, configure the path_mtu to the default
		 * 2048 if not being requested
		 */
		if (!(qp->modify_flags &
		    CMDQ_MODIFY_QP_MODIFY_MASK_PATH_MTU)) {
			qp->modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_PATH_MTU;
			qp->path_mtu =
				CMDQ_MODIFY_QP_PATH_MTU_MTU_2048;
		}
		qp->modify_flags &=
			~CMDQ_MODIFY_QP_MODIFY_MASK_VLAN_ID;
		/* Bono FW require the max_dest_rd_atomic to be >= 1 */
		if (qp->max_dest_rd_atomic < 1)
			qp->max_dest_rd_atomic = 1;
		qp->modify_flags &= ~CMDQ_MODIFY_QP_MODIFY_MASK_SRC_MAC;
		/* Bono FW 20.6.5 requires SGID_INDEX configuration */
		if (!(qp->modify_flags &
		    CMDQ_MODIFY_QP_MODIFY_MASK_SGID_INDEX)) {
			qp->modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_SGID_INDEX;
			qp->ah.sgid_index = 0;
		}
		break;
	default:
		break;
	}
}

static void __modify_flags_from_rtr_state(struct bnxt_qplib_qp *qp)
{
	switch (qp->state) {
	case CMDQ_MODIFY_QP_NEW_STATE_RTS:
		/* Bono FW requires the max_rd_atomic to be >= 1 */
		if (qp->max_rd_atomic < 1)
			qp->max_rd_atomic = 1;
		/* Bono FW does not allow PKEY_INDEX,
		 * DGID, FLOW_LABEL, SGID_INDEX, HOP_LIMIT,
		 * TRAFFIC_CLASS, DEST_MAC, PATH_MTU, RQ_PSN,
		 * MIN_RNR_TIMER, MAX_DEST_RD_ATOMIC, DEST_QP_ID
		 * modification
		 */
		qp->modify_flags &=
			~(CMDQ_MODIFY_QP_MODIFY_MASK_PKEY |
			  CMDQ_MODIFY_QP_MODIFY_MASK_DGID |
			  CMDQ_MODIFY_QP_MODIFY_MASK_FLOW_LABEL |
			  CMDQ_MODIFY_QP_MODIFY_MASK_SGID_INDEX |
			  CMDQ_MODIFY_QP_MODIFY_MASK_HOP_LIMIT |
			  CMDQ_MODIFY_QP_MODIFY_MASK_TRAFFIC_CLASS |
			  CMDQ_MODIFY_QP_MODIFY_MASK_DEST_MAC |
			  CMDQ_MODIFY_QP_MODIFY_MASK_PATH_MTU |
			  CMDQ_MODIFY_QP_MODIFY_MASK_RQ_PSN |
			  CMDQ_MODIFY_QP_MODIFY_MASK_MIN_RNR_TIMER |
			  CMDQ_MODIFY_QP_MODIFY_MASK_MAX_DEST_RD_ATOMIC |
			  CMDQ_MODIFY_QP_MODIFY_MASK_DEST_QP_ID);
		break;
	default:
		break;
	}
}

static void __filter_modify_flags(struct bnxt_qplib_qp *qp)
{
	switch (qp->cur_qp_state) {
	case CMDQ_MODIFY_QP_NEW_STATE_RESET:
		break;
	case CMDQ_MODIFY_QP_NEW_STATE_INIT:
		__modify_flags_from_init_state(qp);
		break;
	case CMDQ_MODIFY_QP_NEW_STATE_RTR:
		__modify_flags_from_rtr_state(qp);
		break;
	case CMDQ_MODIFY_QP_NEW_STATE_RTS:
		break;
	case CMDQ_MODIFY_QP_NEW_STATE_SQD:
		break;
	case CMDQ_MODIFY_QP_NEW_STATE_SQE:
		break;
	case CMDQ_MODIFY_QP_NEW_STATE_ERR:
		break;
	default:
		break;
	}
}

int bnxt_qplib_modify_qp(struct bnxt_qplib_res *res, struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_modify_qp req;
	struct creq_modify_qp_resp resp;
	u16 cmd_flags = 0, pkey;
	u32 temp32[4];
	u32 bmask;
	int rc;

	RCFW_CMD_PREP(req, MODIFY_QP, cmd_flags);

	/* Filter out the qp_attr_mask based on the state->new transition */
	__filter_modify_flags(qp);
	bmask = qp->modify_flags;
	req.modify_mask = cpu_to_le32(qp->modify_flags);
	req.qp_cid = cpu_to_le32(qp->id);
	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_STATE) {
		req.network_type_en_sqd_async_notify_new_state =
				(qp->state & CMDQ_MODIFY_QP_NEW_STATE_MASK) |
				(qp->en_sqd_async_notify ?
					CMDQ_MODIFY_QP_EN_SQD_ASYNC_NOTIFY : 0);
	}
	req.network_type_en_sqd_async_notify_new_state |= qp->nw_type;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_ACCESS)
		req.access = qp->access;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_PKEY) {
		if (!bnxt_qplib_get_pkey(res, &res->pkey_tbl,
					 qp->pkey_index, &pkey))
			req.pkey = cpu_to_le16(pkey);
	}
	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_QKEY)
		req.qkey = cpu_to_le32(qp->qkey);

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_DGID) {
		memcpy(temp32, qp->ah.dgid.data, sizeof(struct bnxt_qplib_gid));
		req.dgid[0] = cpu_to_le32(temp32[0]);
		req.dgid[1] = cpu_to_le32(temp32[1]);
		req.dgid[2] = cpu_to_le32(temp32[2]);
		req.dgid[3] = cpu_to_le32(temp32[3]);
	}
	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_FLOW_LABEL)
		req.flow_label = cpu_to_le32(qp->ah.flow_label);

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_SGID_INDEX)
		req.sgid_index = cpu_to_le16(res->sgid_tbl.hw_id
					     [qp->ah.sgid_index]);

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_HOP_LIMIT)
		req.hop_limit = qp->ah.hop_limit;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_TRAFFIC_CLASS)
		req.traffic_class = qp->ah.traffic_class;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_DEST_MAC)
		memcpy(req.dest_mac, qp->ah.dmac, 6);

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_PATH_MTU)
		req.path_mtu = qp->path_mtu;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_TIMEOUT)
		req.timeout = qp->timeout;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_RETRY_CNT)
		req.retry_cnt = qp->retry_cnt;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_RNR_RETRY)
		req.rnr_retry = qp->rnr_retry;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_MIN_RNR_TIMER)
		req.min_rnr_timer = qp->min_rnr_timer;

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_RQ_PSN)
		req.rq_psn = cpu_to_le32(qp->rq.psn);

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_SQ_PSN)
		req.sq_psn = cpu_to_le32(qp->sq.psn);

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_MAX_RD_ATOMIC)
		req.max_rd_atomic =
			ORD_LIMIT_TO_ORRQ_SLOTS(qp->max_rd_atomic);

	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_MAX_DEST_RD_ATOMIC)
		req.max_dest_rd_atomic =
			IRD_LIMIT_TO_IRRQ_SLOTS(qp->max_dest_rd_atomic);

	req.sq_size = cpu_to_le32(qp->sq.hwq.max_elements);
	req.rq_size = cpu_to_le32(qp->rq.hwq.max_elements);
	req.sq_sge = cpu_to_le16(qp->sq.max_sge);
	req.rq_sge = cpu_to_le16(qp->rq.max_sge);
	req.max_inline_data = cpu_to_le32(qp->max_inline_data);
	if (bmask & CMDQ_MODIFY_QP_MODIFY_MASK_DEST_QP_ID)
		req.dest_qp_id = cpu_to_le32(qp->dest_qpn);

	req.vlan_pcp_vlan_dei_vlan_id = cpu_to_le16(qp->vlan_id);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		return rc;
	qp->cur_qp_state = qp->state;
	return 0;
}

int bnxt_qplib_query_qp(struct bnxt_qplib_res *res, struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_query_qp req;
	struct creq_query_qp_resp resp;
	struct bnxt_qplib_rcfw_sbuf *sbuf;
	struct creq_query_qp_resp_sb *sb;
	u16 cmd_flags = 0;
	u32 temp32[4];
	int i, rc = 0;

	RCFW_CMD_PREP(req, QUERY_QP, cmd_flags);

	sbuf = bnxt_qplib_rcfw_alloc_sbuf(rcfw, sizeof(*sb));
	if (!sbuf)
		return -ENOMEM;
	sb = sbuf->sb;

	req.qp_cid = cpu_to_le32(qp->id);
	req.resp_size = sizeof(*sb) / BNXT_QPLIB_CMDQE_UNITS;
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req, (void *)&resp,
					  (void *)sbuf, 0);
	if (rc)
		goto bail;
	/* Extract the context from the side buffer */
	qp->state = sb->en_sqd_async_notify_state &
			CREQ_QUERY_QP_RESP_SB_STATE_MASK;
	qp->en_sqd_async_notify = sb->en_sqd_async_notify_state &
				  CREQ_QUERY_QP_RESP_SB_EN_SQD_ASYNC_NOTIFY ?
				  true : false;
	qp->access = sb->access;
	qp->pkey_index = le16_to_cpu(sb->pkey);
	qp->qkey = le32_to_cpu(sb->qkey);

	temp32[0] = le32_to_cpu(sb->dgid[0]);
	temp32[1] = le32_to_cpu(sb->dgid[1]);
	temp32[2] = le32_to_cpu(sb->dgid[2]);
	temp32[3] = le32_to_cpu(sb->dgid[3]);
	memcpy(qp->ah.dgid.data, temp32, sizeof(qp->ah.dgid.data));

	qp->ah.flow_label = le32_to_cpu(sb->flow_label);

	qp->ah.sgid_index = 0;
	for (i = 0; i < res->sgid_tbl.max; i++) {
		if (res->sgid_tbl.hw_id[i] == le16_to_cpu(sb->sgid_index)) {
			qp->ah.sgid_index = i;
			break;
		}
	}
	if (i == res->sgid_tbl.max)
		dev_warn(&res->pdev->dev, "SGID not found??\n");

	qp->ah.hop_limit = sb->hop_limit;
	qp->ah.traffic_class = sb->traffic_class;
	memcpy(qp->ah.dmac, sb->dest_mac, 6);
	qp->ah.vlan_id = (le16_to_cpu(sb->path_mtu_dest_vlan_id) &
				CREQ_QUERY_QP_RESP_SB_VLAN_ID_MASK) >>
				CREQ_QUERY_QP_RESP_SB_VLAN_ID_SFT;
	qp->path_mtu = (le16_to_cpu(sb->path_mtu_dest_vlan_id) &
				    CREQ_QUERY_QP_RESP_SB_PATH_MTU_MASK) >>
				    CREQ_QUERY_QP_RESP_SB_PATH_MTU_SFT;
	qp->timeout = sb->timeout;
	qp->retry_cnt = sb->retry_cnt;
	qp->rnr_retry = sb->rnr_retry;
	qp->min_rnr_timer = sb->min_rnr_timer;
	qp->rq.psn = le32_to_cpu(sb->rq_psn);
	qp->max_rd_atomic = ORRQ_SLOTS_TO_ORD_LIMIT(sb->max_rd_atomic);
	qp->sq.psn = le32_to_cpu(sb->sq_psn);
	qp->max_dest_rd_atomic =
			IRRQ_SLOTS_TO_IRD_LIMIT(sb->max_dest_rd_atomic);
	qp->sq.max_wqe = qp->sq.hwq.max_elements;
	qp->rq.max_wqe = qp->rq.hwq.max_elements;
	qp->sq.max_sge = le16_to_cpu(sb->sq_sge);
	qp->rq.max_sge = le16_to_cpu(sb->rq_sge);
	qp->max_inline_data = le32_to_cpu(sb->max_inline_data);
	qp->dest_qpn = le32_to_cpu(sb->dest_qp_id);
	memcpy(qp->smac, sb->src_mac, 6);
	qp->vlan_id = le16_to_cpu(sb->vlan_pcp_vlan_dei_vlan_id);
bail:
	bnxt_qplib_rcfw_free_sbuf(rcfw, sbuf);
	return rc;
}

static void __clean_cq(struct bnxt_qplib_cq *cq, u64 qp)
{
	struct bnxt_qplib_hwq *cq_hwq = &cq->hwq;
	struct cq_base *hw_cqe, **hw_cqe_ptr;
	int i;

	for (i = 0; i < cq_hwq->max_elements; i++) {
		hw_cqe_ptr = (struct cq_base **)cq_hwq->pbl_ptr;
		hw_cqe = &hw_cqe_ptr[CQE_PG(i)][CQE_IDX(i)];
		if (!CQE_CMP_VALID(hw_cqe, i, cq_hwq->max_elements))
			continue;
		/*
		 * The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
		switch (hw_cqe->cqe_type_toggle & CQ_BASE_CQE_TYPE_MASK) {
		case CQ_BASE_CQE_TYPE_REQ:
		case CQ_BASE_CQE_TYPE_TERMINAL:
		{
			struct cq_req *cqe = (struct cq_req *)hw_cqe;

			if (qp == le64_to_cpu(cqe->qp_handle))
				cqe->qp_handle = 0;
			break;
		}
		case CQ_BASE_CQE_TYPE_RES_RC:
		case CQ_BASE_CQE_TYPE_RES_UD:
		case CQ_BASE_CQE_TYPE_RES_RAWETH_QP1:
		{
			struct cq_res_rc *cqe = (struct cq_res_rc *)hw_cqe;

			if (qp == le64_to_cpu(cqe->qp_handle))
				cqe->qp_handle = 0;
			break;
		}
		default:
			break;
		}
	}
}

int bnxt_qplib_destroy_qp(struct bnxt_qplib_res *res,
			  struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_destroy_qp req;
	struct creq_destroy_qp_resp resp;
	u16 cmd_flags = 0;
	int rc;

	rcfw->qp_tbl[qp->id].qp_id = BNXT_QPLIB_QP_ID_INVALID;
	rcfw->qp_tbl[qp->id].qp_handle = NULL;

	RCFW_CMD_PREP(req, DESTROY_QP, cmd_flags);

	req.qp_cid = cpu_to_le32(qp->id);
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc) {
		rcfw->qp_tbl[qp->id].qp_id = qp->id;
		rcfw->qp_tbl[qp->id].qp_handle = qp;
		return rc;
	}

	return 0;
}

void bnxt_qplib_free_qp_res(struct bnxt_qplib_res *res,
			    struct bnxt_qplib_qp *qp)
{
	bnxt_qplib_free_qp_hdr_buf(res, qp);
	bnxt_qplib_free_hwq(res->pdev, &qp->sq.hwq);
	kfree(qp->sq.swq);

	bnxt_qplib_free_hwq(res->pdev, &qp->rq.hwq);
	kfree(qp->rq.swq);

	if (qp->irrq.max_elements)
		bnxt_qplib_free_hwq(res->pdev, &qp->irrq);
	if (qp->orrq.max_elements)
		bnxt_qplib_free_hwq(res->pdev, &qp->orrq);

}

void *bnxt_qplib_get_qp1_sq_buf(struct bnxt_qplib_qp *qp,
				struct bnxt_qplib_sge *sge)
{
	struct bnxt_qplib_q *sq = &qp->sq;
	u32 sw_prod;

	memset(sge, 0, sizeof(*sge));

	if (qp->sq_hdr_buf) {
		sw_prod = HWQ_CMP(sq->hwq.prod, &sq->hwq);
		sge->addr = (dma_addr_t)(qp->sq_hdr_buf_map +
					 sw_prod * qp->sq_hdr_buf_size);
		sge->lkey = 0xFFFFFFFF;
		sge->size = qp->sq_hdr_buf_size;
		return qp->sq_hdr_buf + sw_prod * sge->size;
	}
	return NULL;
}

u32 bnxt_qplib_get_rq_prod_index(struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_q *rq = &qp->rq;

	return HWQ_CMP(rq->hwq.prod, &rq->hwq);
}

dma_addr_t bnxt_qplib_get_qp_buf_from_index(struct bnxt_qplib_qp *qp, u32 index)
{
	return (qp->rq_hdr_buf_map + index * qp->rq_hdr_buf_size);
}

void *bnxt_qplib_get_qp1_rq_buf(struct bnxt_qplib_qp *qp,
				struct bnxt_qplib_sge *sge)
{
	struct bnxt_qplib_q *rq = &qp->rq;
	u32 sw_prod;

	memset(sge, 0, sizeof(*sge));

	if (qp->rq_hdr_buf) {
		sw_prod = HWQ_CMP(rq->hwq.prod, &rq->hwq);
		sge->addr = (dma_addr_t)(qp->rq_hdr_buf_map +
					 sw_prod * qp->rq_hdr_buf_size);
		sge->lkey = 0xFFFFFFFF;
		sge->size = qp->rq_hdr_buf_size;
		return qp->rq_hdr_buf + sw_prod * sge->size;
	}
	return NULL;
}

void bnxt_qplib_post_send_db(struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_q *sq = &qp->sq;
	u32 sw_prod;
	u64 val = 0;

	val = (((qp->id << DBC_DBC_XID_SFT) & DBC_DBC_XID_MASK) |
	       DBC_DBC_TYPE_SQ);
	val <<= 32;
	sw_prod = HWQ_CMP(sq->hwq.prod, &sq->hwq);
	val |= (sw_prod << DBC_DBC_INDEX_SFT) & DBC_DBC_INDEX_MASK;
	/* Flush all the WQE writes to HW */
	writeq(val, qp->dpi->dbr);
}

int bnxt_qplib_post_send(struct bnxt_qplib_qp *qp,
			 struct bnxt_qplib_swqe *wqe)
{
	struct bnxt_qplib_q *sq = &qp->sq;
	struct bnxt_qplib_swq *swq;
	struct sq_send *hw_sq_send_hdr, **hw_sq_send_ptr;
	struct sq_sge *hw_sge;
	struct bnxt_qplib_nq_work *nq_work = NULL;
	bool sch_handler = false;
	u32 sw_prod;
	u8 wqe_size16;
	int i, rc = 0, data_len = 0, pkt_num = 0;
	__le32 temp32;

	if (qp->state != CMDQ_MODIFY_QP_NEW_STATE_RTS) {
		if (qp->state == CMDQ_MODIFY_QP_NEW_STATE_ERR) {
			sch_handler = true;
			dev_dbg(&sq->hwq.pdev->dev,
				"%s Error QP. Scheduling for poll_cq\n",
				__func__);
			goto queue_err;
		}
	}

	if (bnxt_qplib_queue_full(sq)) {
		dev_err(&sq->hwq.pdev->dev,
			"prod = %#x cons = %#x qdepth = %#x delta = %#x\n",
			sq->hwq.prod, sq->hwq.cons, sq->hwq.max_elements,
			sq->q_full_delta);
		rc = -ENOMEM;
		goto done;
	}
	sw_prod = HWQ_CMP(sq->hwq.prod, &sq->hwq);
	swq = &sq->swq[sw_prod];
	swq->wr_id = wqe->wr_id;
	swq->type = wqe->type;
	swq->flags = wqe->flags;
	if (qp->sig_type)
		swq->flags |= SQ_SEND_FLAGS_SIGNAL_COMP;
	swq->start_psn = sq->psn & BTH_PSN_MASK;

	hw_sq_send_ptr = (struct sq_send **)sq->hwq.pbl_ptr;
	hw_sq_send_hdr = &hw_sq_send_ptr[get_sqe_pg(sw_prod)]
					[get_sqe_idx(sw_prod)];

	memset(hw_sq_send_hdr, 0, BNXT_QPLIB_MAX_SQE_ENTRY_SIZE);

	if (wqe->flags & BNXT_QPLIB_SWQE_FLAGS_INLINE) {
		/* Copy the inline data */
		if (wqe->inline_len > BNXT_QPLIB_SWQE_MAX_INLINE_LENGTH) {
			dev_warn(&sq->hwq.pdev->dev,
				 "Inline data length > 96 detected\n");
			data_len = BNXT_QPLIB_SWQE_MAX_INLINE_LENGTH;
		} else {
			data_len = wqe->inline_len;
		}
		memcpy(hw_sq_send_hdr->data, wqe->inline_data, data_len);
		wqe_size16 = (data_len + 15) >> 4;
	} else {
		for (i = 0, hw_sge = (struct sq_sge *)hw_sq_send_hdr->data;
		     i < wqe->num_sge; i++, hw_sge++) {
			hw_sge->va_or_pa = cpu_to_le64(wqe->sg_list[i].addr);
			hw_sge->l_key = cpu_to_le32(wqe->sg_list[i].lkey);
			hw_sge->size = cpu_to_le32(wqe->sg_list[i].size);
			data_len += wqe->sg_list[i].size;
		}
		/* Each SGE entry = 1 WQE size16 */
		wqe_size16 = wqe->num_sge;
		/* HW requires wqe size has room for atleast one SGE even if
		 * none was supplied by ULP
		 */
		if (!wqe->num_sge)
			wqe_size16++;
	}

	/* Specifics */
	switch (wqe->type) {
	case BNXT_QPLIB_SWQE_TYPE_SEND:
		if (qp->type == CMDQ_CREATE_QP1_TYPE_GSI) {
			/* Assemble info for Raw Ethertype QPs */
			struct sq_send_raweth_qp1 *sqe =
				(struct sq_send_raweth_qp1 *)hw_sq_send_hdr;

			sqe->wqe_type = wqe->type;
			sqe->flags = wqe->flags;
			sqe->wqe_size = wqe_size16 +
				((offsetof(typeof(*sqe), data) + 15) >> 4);
			sqe->cfa_action = cpu_to_le16(wqe->rawqp1.cfa_action);
			sqe->lflags = cpu_to_le16(wqe->rawqp1.lflags);
			sqe->length = cpu_to_le32(data_len);
			sqe->cfa_meta = cpu_to_le32((wqe->rawqp1.cfa_meta &
				SQ_SEND_RAWETH_QP1_CFA_META_VLAN_VID_MASK) <<
				SQ_SEND_RAWETH_QP1_CFA_META_VLAN_VID_SFT);

			break;
		}
		/* fall thru */
	case BNXT_QPLIB_SWQE_TYPE_SEND_WITH_IMM:
	case BNXT_QPLIB_SWQE_TYPE_SEND_WITH_INV:
	{
		struct sq_send *sqe = (struct sq_send *)hw_sq_send_hdr;

		sqe->wqe_type = wqe->type;
		sqe->flags = wqe->flags;
		sqe->wqe_size = wqe_size16 +
				((offsetof(typeof(*sqe), data) + 15) >> 4);
		sqe->inv_key_or_imm_data = cpu_to_le32(
						wqe->send.inv_key);
		if (qp->type == CMDQ_CREATE_QP_TYPE_UD ||
		    qp->type == CMDQ_CREATE_QP_TYPE_GSI) {
			sqe->q_key = cpu_to_le32(wqe->send.q_key);
			sqe->dst_qp = cpu_to_le32(
					wqe->send.dst_qp & SQ_SEND_DST_QP_MASK);
			sqe->length = cpu_to_le32(data_len);
			sqe->avid = cpu_to_le32(wqe->send.avid &
						SQ_SEND_AVID_MASK);
			sq->psn = (sq->psn + 1) & BTH_PSN_MASK;
		} else {
			sqe->length = cpu_to_le32(data_len);
			sqe->dst_qp = 0;
			sqe->avid = 0;
			if (qp->mtu)
				pkt_num = (data_len + qp->mtu - 1) / qp->mtu;
			if (!pkt_num)
				pkt_num = 1;
			sq->psn = (sq->psn + pkt_num) & BTH_PSN_MASK;
		}
		break;
	}
	case BNXT_QPLIB_SWQE_TYPE_RDMA_WRITE:
	case BNXT_QPLIB_SWQE_TYPE_RDMA_WRITE_WITH_IMM:
	case BNXT_QPLIB_SWQE_TYPE_RDMA_READ:
	{
		struct sq_rdma *sqe = (struct sq_rdma *)hw_sq_send_hdr;

		sqe->wqe_type = wqe->type;
		sqe->flags = wqe->flags;
		sqe->wqe_size = wqe_size16 +
				((offsetof(typeof(*sqe), data) + 15) >> 4);
		sqe->imm_data = cpu_to_le32(wqe->rdma.inv_key);
		sqe->length = cpu_to_le32((u32)data_len);
		sqe->remote_va = cpu_to_le64(wqe->rdma.remote_va);
		sqe->remote_key = cpu_to_le32(wqe->rdma.r_key);
		if (qp->mtu)
			pkt_num = (data_len + qp->mtu - 1) / qp->mtu;
		if (!pkt_num)
			pkt_num = 1;
		sq->psn = (sq->psn + pkt_num) & BTH_PSN_MASK;
		break;
	}
	case BNXT_QPLIB_SWQE_TYPE_ATOMIC_CMP_AND_SWP:
	case BNXT_QPLIB_SWQE_TYPE_ATOMIC_FETCH_AND_ADD:
	{
		struct sq_atomic *sqe = (struct sq_atomic *)hw_sq_send_hdr;

		sqe->wqe_type = wqe->type;
		sqe->flags = wqe->flags;
		sqe->remote_key = cpu_to_le32(wqe->atomic.r_key);
		sqe->remote_va = cpu_to_le64(wqe->atomic.remote_va);
		sqe->swap_data = cpu_to_le64(wqe->atomic.swap_data);
		sqe->cmp_data = cpu_to_le64(wqe->atomic.cmp_data);
		if (qp->mtu)
			pkt_num = (data_len + qp->mtu - 1) / qp->mtu;
		if (!pkt_num)
			pkt_num = 1;
		sq->psn = (sq->psn + pkt_num) & BTH_PSN_MASK;
		break;
	}
	case BNXT_QPLIB_SWQE_TYPE_LOCAL_INV:
	{
		struct sq_localinvalidate *sqe =
				(struct sq_localinvalidate *)hw_sq_send_hdr;

		sqe->wqe_type = wqe->type;
		sqe->flags = wqe->flags;
		sqe->inv_l_key = cpu_to_le32(wqe->local_inv.inv_l_key);

		break;
	}
	case BNXT_QPLIB_SWQE_TYPE_FAST_REG_MR:
	{
		struct sq_fr_pmr *sqe = (struct sq_fr_pmr *)hw_sq_send_hdr;

		sqe->wqe_type = wqe->type;
		sqe->flags = wqe->flags;
		sqe->access_cntl = wqe->frmr.access_cntl |
				   SQ_FR_PMR_ACCESS_CNTL_LOCAL_WRITE;
		sqe->zero_based_page_size_log =
			(wqe->frmr.pg_sz_log & SQ_FR_PMR_PAGE_SIZE_LOG_MASK) <<
			SQ_FR_PMR_PAGE_SIZE_LOG_SFT |
			(wqe->frmr.zero_based ? SQ_FR_PMR_ZERO_BASED : 0);
		sqe->l_key = cpu_to_le32(wqe->frmr.l_key);
		temp32 = cpu_to_le32(wqe->frmr.length);
		memcpy(sqe->length, &temp32, sizeof(wqe->frmr.length));
		sqe->numlevels_pbl_page_size_log =
			((wqe->frmr.pbl_pg_sz_log <<
					SQ_FR_PMR_PBL_PAGE_SIZE_LOG_SFT) &
					SQ_FR_PMR_PBL_PAGE_SIZE_LOG_MASK) |
			((wqe->frmr.levels << SQ_FR_PMR_NUMLEVELS_SFT) &
					SQ_FR_PMR_NUMLEVELS_MASK);

		for (i = 0; i < wqe->frmr.page_list_len; i++)
			wqe->frmr.pbl_ptr[i] = cpu_to_le64(
						wqe->frmr.page_list[i] |
						PTU_PTE_VALID);
		sqe->pblptr = cpu_to_le64(wqe->frmr.pbl_dma_ptr);
		sqe->va = cpu_to_le64(wqe->frmr.va);

		break;
	}
	case BNXT_QPLIB_SWQE_TYPE_BIND_MW:
	{
		struct sq_bind *sqe = (struct sq_bind *)hw_sq_send_hdr;

		sqe->wqe_type = wqe->type;
		sqe->flags = wqe->flags;
		sqe->access_cntl = wqe->bind.access_cntl;
		sqe->mw_type_zero_based = wqe->bind.mw_type |
			(wqe->bind.zero_based ? SQ_BIND_ZERO_BASED : 0);
		sqe->parent_l_key = cpu_to_le32(wqe->bind.parent_l_key);
		sqe->l_key = cpu_to_le32(wqe->bind.r_key);
		sqe->va = cpu_to_le64(wqe->bind.va);
		temp32 = cpu_to_le32(wqe->bind.length);
		memcpy(&sqe->length, &temp32, sizeof(wqe->bind.length));
		break;
	}
	default:
		/* Bad wqe, return error */
		rc = -EINVAL;
		goto done;
	}
	swq->next_psn = sq->psn & BTH_PSN_MASK;
	if (swq->psn_search) {
		u32 opcd_spsn;
		u32 flg_npsn;

		opcd_spsn = ((swq->start_psn << SQ_PSN_SEARCH_START_PSN_SFT) &
			      SQ_PSN_SEARCH_START_PSN_MASK);
		opcd_spsn |= ((wqe->type << SQ_PSN_SEARCH_OPCODE_SFT) &
			       SQ_PSN_SEARCH_OPCODE_MASK);
		flg_npsn = ((swq->next_psn << SQ_PSN_SEARCH_NEXT_PSN_SFT) &
			     SQ_PSN_SEARCH_NEXT_PSN_MASK);
		if (bnxt_qplib_is_chip_gen_p5(qp->cctx)) {
			swq->psn_ext->opcode_start_psn =
						cpu_to_le32(opcd_spsn);
			swq->psn_ext->flags_next_psn =
						cpu_to_le32(flg_npsn);
		} else {
			swq->psn_search->opcode_start_psn =
						cpu_to_le32(opcd_spsn);
			swq->psn_search->flags_next_psn =
						cpu_to_le32(flg_npsn);
		}
	}
queue_err:
	if (sch_handler) {
		/* Store the ULP info in the software structures */
		sw_prod = HWQ_CMP(sq->hwq.prod, &sq->hwq);
		swq = &sq->swq[sw_prod];
		swq->wr_id = wqe->wr_id;
		swq->type = wqe->type;
		swq->flags = wqe->flags;
		if (qp->sig_type)
			swq->flags |= SQ_SEND_FLAGS_SIGNAL_COMP;
		swq->start_psn = sq->psn & BTH_PSN_MASK;
	}
	sq->hwq.prod++;
	qp->wqe_cnt++;

done:
	if (sch_handler) {
		nq_work = kzalloc(sizeof(*nq_work), GFP_ATOMIC);
		if (nq_work) {
			nq_work->cq = qp->scq;
			nq_work->nq = qp->scq->nq;
			INIT_WORK(&nq_work->work, bnxt_qpn_cqn_sched_task);
			queue_work(qp->scq->nq->cqn_wq, &nq_work->work);
		} else {
			dev_err(&sq->hwq.pdev->dev,
				"FP: Failed to allocate SQ nq_work!\n");
			rc = -ENOMEM;
		}
	}
	return rc;
}

void bnxt_qplib_post_recv_db(struct bnxt_qplib_qp *qp)
{
	struct bnxt_qplib_q *rq = &qp->rq;
	u32 sw_prod;
	u64 val = 0;

	val = (((qp->id << DBC_DBC_XID_SFT) & DBC_DBC_XID_MASK) |
	       DBC_DBC_TYPE_RQ);
	val <<= 32;
	sw_prod = HWQ_CMP(rq->hwq.prod, &rq->hwq);
	val |= (sw_prod << DBC_DBC_INDEX_SFT) & DBC_DBC_INDEX_MASK;
	/* Flush the writes to HW Rx WQE before the ringing Rx DB */
	writeq(val, qp->dpi->dbr);
}

int bnxt_qplib_post_recv(struct bnxt_qplib_qp *qp,
			 struct bnxt_qplib_swqe *wqe)
{
	struct bnxt_qplib_q *rq = &qp->rq;
	struct rq_wqe *rqe, **rqe_ptr;
	struct sq_sge *hw_sge;
	struct bnxt_qplib_nq_work *nq_work = NULL;
	bool sch_handler = false;
	u32 sw_prod;
	int i, rc = 0;

	if (qp->state == CMDQ_MODIFY_QP_NEW_STATE_ERR) {
		sch_handler = true;
		dev_dbg(&rq->hwq.pdev->dev,
			"%s: Error QP. Scheduling for poll_cq\n", __func__);
		goto queue_err;
	}
	if (bnxt_qplib_queue_full(rq)) {
		dev_err(&rq->hwq.pdev->dev,
			"FP: QP (0x%x) RQ is full!\n", qp->id);
		rc = -EINVAL;
		goto done;
	}
	sw_prod = HWQ_CMP(rq->hwq.prod, &rq->hwq);
	rq->swq[sw_prod].wr_id = wqe->wr_id;

	rqe_ptr = (struct rq_wqe **)rq->hwq.pbl_ptr;
	rqe = &rqe_ptr[RQE_PG(sw_prod)][RQE_IDX(sw_prod)];

	memset(rqe, 0, BNXT_QPLIB_MAX_RQE_ENTRY_SIZE);

	/* Calculate wqe_size16 and data_len */
	for (i = 0, hw_sge = (struct sq_sge *)rqe->data;
	     i < wqe->num_sge; i++, hw_sge++) {
		hw_sge->va_or_pa = cpu_to_le64(wqe->sg_list[i].addr);
		hw_sge->l_key = cpu_to_le32(wqe->sg_list[i].lkey);
		hw_sge->size = cpu_to_le32(wqe->sg_list[i].size);
	}
	rqe->wqe_type = wqe->type;
	rqe->flags = wqe->flags;
	rqe->wqe_size = wqe->num_sge +
			((offsetof(typeof(*rqe), data) + 15) >> 4);
	/* HW requires wqe size has room for atleast one SGE even if none
	 * was supplied by ULP
	 */
	if (!wqe->num_sge)
		rqe->wqe_size++;

	/* Supply the rqe->wr_id index to the wr_id_tbl for now */
	rqe->wr_id[0] = cpu_to_le32(sw_prod);

queue_err:
	if (sch_handler) {
		/* Store the ULP info in the software structures */
		sw_prod = HWQ_CMP(rq->hwq.prod, &rq->hwq);
		rq->swq[sw_prod].wr_id = wqe->wr_id;
	}

	rq->hwq.prod++;
	if (sch_handler) {
		nq_work = kzalloc(sizeof(*nq_work), GFP_ATOMIC);
		if (nq_work) {
			nq_work->cq = qp->rcq;
			nq_work->nq = qp->rcq->nq;
			INIT_WORK(&nq_work->work, bnxt_qpn_cqn_sched_task);
			queue_work(qp->rcq->nq->cqn_wq, &nq_work->work);
		} else {
			dev_err(&rq->hwq.pdev->dev,
				"FP: Failed to allocate RQ nq_work!\n");
			rc = -ENOMEM;
		}
	}
done:
	return rc;
}

/* CQ */

/* Spinlock must be held */
static void bnxt_qplib_arm_cq_enable(struct bnxt_qplib_cq *cq)
{
	u64 val = 0;

	val = ((cq->id << DBC_DBC_XID_SFT) & DBC_DBC_XID_MASK) |
	       DBC_DBC_TYPE_CQ_ARMENA;
	val <<= 32;
	/* Flush memory writes before enabling the CQ */
	writeq(val, cq->dbr_base);
}

static void bnxt_qplib_arm_cq(struct bnxt_qplib_cq *cq, u32 arm_type)
{
	struct bnxt_qplib_hwq *cq_hwq = &cq->hwq;
	u32 sw_cons;
	u64 val = 0;

	/* Ring DB */
	val = ((cq->id << DBC_DBC_XID_SFT) & DBC_DBC_XID_MASK) | arm_type;
	val <<= 32;
	sw_cons = HWQ_CMP(cq_hwq->cons, cq_hwq);
	val |= (sw_cons << DBC_DBC_INDEX_SFT) & DBC_DBC_INDEX_MASK;
	/* flush memory writes before arming the CQ */
	writeq(val, cq->dpi->dbr);
}

int bnxt_qplib_create_cq(struct bnxt_qplib_res *res, struct bnxt_qplib_cq *cq)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_create_cq req;
	struct creq_create_cq_resp resp;
	struct bnxt_qplib_pbl *pbl;
	u16 cmd_flags = 0;
	int rc;

	cq->hwq.max_elements = cq->max_wqe;
	rc = bnxt_qplib_alloc_init_hwq(res->pdev, &cq->hwq, &cq->sg_info,
				       &cq->hwq.max_elements,
				       BNXT_QPLIB_MAX_CQE_ENTRY_SIZE, 0,
				       PAGE_SIZE, HWQ_TYPE_QUEUE);
	if (rc)
		goto exit;

	RCFW_CMD_PREP(req, CREATE_CQ, cmd_flags);

	if (!cq->dpi) {
		dev_err(&rcfw->pdev->dev,
			"FP: CREATE_CQ failed due to NULL DPI\n");
		return -EINVAL;
	}
	req.dpi = cpu_to_le32(cq->dpi->dpi);
	req.cq_handle = cpu_to_le64(cq->cq_handle);

	req.cq_size = cpu_to_le32(cq->hwq.max_elements);
	pbl = &cq->hwq.pbl[PBL_LVL_0];
	req.pg_size_lvl = cpu_to_le32(
	    ((cq->hwq.level & CMDQ_CREATE_CQ_LVL_MASK) <<
						CMDQ_CREATE_CQ_LVL_SFT) |
	    (pbl->pg_size == ROCE_PG_SIZE_4K ? CMDQ_CREATE_CQ_PG_SIZE_PG_4K :
	     pbl->pg_size == ROCE_PG_SIZE_8K ? CMDQ_CREATE_CQ_PG_SIZE_PG_8K :
	     pbl->pg_size == ROCE_PG_SIZE_64K ? CMDQ_CREATE_CQ_PG_SIZE_PG_64K :
	     pbl->pg_size == ROCE_PG_SIZE_2M ? CMDQ_CREATE_CQ_PG_SIZE_PG_2M :
	     pbl->pg_size == ROCE_PG_SIZE_8M ? CMDQ_CREATE_CQ_PG_SIZE_PG_8M :
	     pbl->pg_size == ROCE_PG_SIZE_1G ? CMDQ_CREATE_CQ_PG_SIZE_PG_1G :
	     CMDQ_CREATE_CQ_PG_SIZE_PG_4K));

	req.pbl = cpu_to_le64(pbl->pg_map_arr[0]);

	req.cq_fco_cnq_id = cpu_to_le32(
			(cq->cnq_hw_ring_id & CMDQ_CREATE_CQ_CNQ_ID_MASK) <<
			 CMDQ_CREATE_CQ_CNQ_ID_SFT);

	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		goto fail;

	cq->id = le32_to_cpu(resp.xid);
	cq->dbr_base = res->dpi_tbl.dbr_bar_reg_iomem;
	cq->period = BNXT_QPLIB_QUEUE_START_PERIOD;
	init_waitqueue_head(&cq->waitq);
	INIT_LIST_HEAD(&cq->sqf_head);
	INIT_LIST_HEAD(&cq->rqf_head);
	spin_lock_init(&cq->compl_lock);
	spin_lock_init(&cq->flush_lock);

	bnxt_qplib_arm_cq_enable(cq);
	return 0;

fail:
	bnxt_qplib_free_hwq(res->pdev, &cq->hwq);
exit:
	return rc;
}

int bnxt_qplib_destroy_cq(struct bnxt_qplib_res *res, struct bnxt_qplib_cq *cq)
{
	struct bnxt_qplib_rcfw *rcfw = res->rcfw;
	struct cmdq_destroy_cq req;
	struct creq_destroy_cq_resp resp;
	u16 cmd_flags = 0;
	int rc;

	RCFW_CMD_PREP(req, DESTROY_CQ, cmd_flags);

	req.cq_cid = cpu_to_le32(cq->id);
	rc = bnxt_qplib_rcfw_send_message(rcfw, (void *)&req,
					  (void *)&resp, NULL, 0);
	if (rc)
		return rc;
	bnxt_qplib_free_hwq(res->pdev, &cq->hwq);
	return 0;
}

static int __flush_sq(struct bnxt_qplib_q *sq, struct bnxt_qplib_qp *qp,
		      struct bnxt_qplib_cqe **pcqe, int *budget)
{
	u32 sw_prod, sw_cons;
	struct bnxt_qplib_cqe *cqe;
	int rc = 0;

	/* Now complete all outstanding SQEs with FLUSHED_ERR */
	sw_prod = HWQ_CMP(sq->hwq.prod, &sq->hwq);
	cqe = *pcqe;
	while (*budget) {
		sw_cons = HWQ_CMP(sq->hwq.cons, &sq->hwq);
		if (sw_cons == sw_prod) {
			break;
		}
		/* Skip the FENCE WQE completions */
		if (sq->swq[sw_cons].wr_id == BNXT_QPLIB_FENCE_WRID) {
			bnxt_qplib_cancel_phantom_processing(qp);
			goto skip_compl;
		}
		memset(cqe, 0, sizeof(*cqe));
		cqe->status = CQ_REQ_STATUS_WORK_REQUEST_FLUSHED_ERR;
		cqe->opcode = CQ_BASE_CQE_TYPE_REQ;
		cqe->qp_handle = (u64)(unsigned long)qp;
		cqe->wr_id = sq->swq[sw_cons].wr_id;
		cqe->src_qp = qp->id;
		cqe->type = sq->swq[sw_cons].type;
		cqe++;
		(*budget)--;
skip_compl:
		sq->hwq.cons++;
	}
	*pcqe = cqe;
	if (!(*budget) && HWQ_CMP(sq->hwq.cons, &sq->hwq) != sw_prod)
		/* Out of budget */
		rc = -EAGAIN;

	return rc;
}

static int __flush_rq(struct bnxt_qplib_q *rq, struct bnxt_qplib_qp *qp,
		      struct bnxt_qplib_cqe **pcqe, int *budget)
{
	struct bnxt_qplib_cqe *cqe;
	u32 sw_prod, sw_cons;
	int rc = 0;
	int opcode = 0;

	switch (qp->type) {
	case CMDQ_CREATE_QP1_TYPE_GSI:
		opcode = CQ_BASE_CQE_TYPE_RES_RAWETH_QP1;
		break;
	case CMDQ_CREATE_QP_TYPE_RC:
		opcode = CQ_BASE_CQE_TYPE_RES_RC;
		break;
	case CMDQ_CREATE_QP_TYPE_UD:
	case CMDQ_CREATE_QP_TYPE_GSI:
		opcode = CQ_BASE_CQE_TYPE_RES_UD;
		break;
	}

	/* Flush the rest of the RQ */
	sw_prod = HWQ_CMP(rq->hwq.prod, &rq->hwq);
	cqe = *pcqe;
	while (*budget) {
		sw_cons = HWQ_CMP(rq->hwq.cons, &rq->hwq);
		if (sw_cons == sw_prod)
			break;
		memset(cqe, 0, sizeof(*cqe));
		cqe->status =
		    CQ_RES_RC_STATUS_WORK_REQUEST_FLUSHED_ERR;
		cqe->opcode = opcode;
		cqe->qp_handle = (unsigned long)qp;
		cqe->wr_id = rq->swq[sw_cons].wr_id;
		cqe++;
		(*budget)--;
		rq->hwq.cons++;
	}
	*pcqe = cqe;
	if (!*budget && HWQ_CMP(rq->hwq.cons, &rq->hwq) != sw_prod)
		/* Out of budget */
		rc = -EAGAIN;

	return rc;
}

void bnxt_qplib_mark_qp_error(void *qp_handle)
{
	struct bnxt_qplib_qp *qp = qp_handle;

	if (!qp)
		return;

	/* Must block new posting of SQ and RQ */
	qp->state = CMDQ_MODIFY_QP_NEW_STATE_ERR;
	bnxt_qplib_cancel_phantom_processing(qp);
}

/* Note: SQE is valid from sw_sq_cons up to cqe_sq_cons (exclusive)
 *       CQE is track from sw_cq_cons to max_element but valid only if VALID=1
 */
static int do_wa9060(struct bnxt_qplib_qp *qp, struct bnxt_qplib_cq *cq,
		     u32 cq_cons, u32 sw_sq_cons, u32 cqe_sq_cons)
{
	struct bnxt_qplib_q *sq = &qp->sq;
	struct bnxt_qplib_swq *swq;
	u32 peek_sw_cq_cons, peek_raw_cq_cons, peek_sq_cons_idx;
	struct cq_base *peek_hwcqe, **peek_hw_cqe_ptr;
	struct cq_req *peek_req_hwcqe;
	struct bnxt_qplib_qp *peek_qp;
	struct bnxt_qplib_q *peek_sq;
	int i, rc = 0;

	/* Normal mode */
	/* Check for the psn_search marking before completing */
	swq = &sq->swq[sw_sq_cons];
	if (swq->psn_search &&
	    le32_to_cpu(swq->psn_search->flags_next_psn) & 0x80000000) {
		/* Unmark */
		swq->psn_search->flags_next_psn = cpu_to_le32
			(le32_to_cpu(swq->psn_search->flags_next_psn)
				     & ~0x80000000);
		dev_dbg(&cq->hwq.pdev->dev,
			"FP: Process Req cq_cons=0x%x qp=0x%x sq cons sw=0x%x cqe=0x%x marked!\n",
			cq_cons, qp->id, sw_sq_cons, cqe_sq_cons);
		sq->condition = true;
		sq->send_phantom = true;

		/* TODO: Only ARM if the previous SQE is ARMALL */
		bnxt_qplib_arm_cq(cq, DBC_DBC_TYPE_CQ_ARMALL);

		rc = -EAGAIN;
		goto out;
	}
	if (sq->condition) {
		/* Peek at the completions */
		peek_raw_cq_cons = cq->hwq.cons;
		peek_sw_cq_cons = cq_cons;
		i = cq->hwq.max_elements;
		while (i--) {
			peek_sw_cq_cons = HWQ_CMP((peek_sw_cq_cons), &cq->hwq);
			peek_hw_cqe_ptr = (struct cq_base **)cq->hwq.pbl_ptr;
			peek_hwcqe = &peek_hw_cqe_ptr[CQE_PG(peek_sw_cq_cons)]
						     [CQE_IDX(peek_sw_cq_cons)];
			/* If the next hwcqe is VALID */
			if (CQE_CMP_VALID(peek_hwcqe, peek_raw_cq_cons,
					  cq->hwq.max_elements)) {
			/*
			 * The valid test of the entry must be done first before
			 * reading any further.
			 */
				dma_rmb();
				/* If the next hwcqe is a REQ */
				if ((peek_hwcqe->cqe_type_toggle &
				    CQ_BASE_CQE_TYPE_MASK) ==
				    CQ_BASE_CQE_TYPE_REQ) {
					peek_req_hwcqe = (struct cq_req *)
							 peek_hwcqe;
					peek_qp = (struct bnxt_qplib_qp *)
						((unsigned long)
						 le64_to_cpu
						 (peek_req_hwcqe->qp_handle));
					peek_sq = &peek_qp->sq;
					peek_sq_cons_idx = HWQ_CMP(le16_to_cpu(
						peek_req_hwcqe->sq_cons_idx) - 1
						, &sq->hwq);
					/* If the hwcqe's sq's wr_id matches */
					if (peek_sq == sq &&
					    sq->swq[peek_sq_cons_idx].wr_id ==
					    BNXT_QPLIB_FENCE_WRID) {
						/*
						 *  Unbreak only if the phantom
						 *  comes back
						 */
						dev_dbg(&cq->hwq.pdev->dev,
							"FP: Got Phantom CQE\n");
						sq->condition = false;
						sq->single = true;
						rc = 0;
						goto out;
					}
				}
				/* Valid but not the phantom, so keep looping */
			} else {
				/* Not valid yet, just exit and wait */
				rc = -EINVAL;
				goto out;
			}
			peek_sw_cq_cons++;
			peek_raw_cq_cons++;
		}
		dev_err(&cq->hwq.pdev->dev,
			"Should not have come here! cq_cons=0x%x qp=0x%x sq cons sw=0x%x hw=0x%x\n",
			cq_cons, qp->id, sw_sq_cons, cqe_sq_cons);
		rc = -EINVAL;
	}
out:
	return rc;
}

static int bnxt_qplib_cq_process_req(struct bnxt_qplib_cq *cq,
				     struct cq_req *hwcqe,
				     struct bnxt_qplib_cqe **pcqe, int *budget,
				     u32 cq_cons, struct bnxt_qplib_qp **lib_qp)
{
	struct bnxt_qplib_qp *qp;
	struct bnxt_qplib_q *sq;
	struct bnxt_qplib_cqe *cqe;
	u32 sw_sq_cons, cqe_sq_cons;
	struct bnxt_qplib_swq *swq;
	int rc = 0;

	qp = (struct bnxt_qplib_qp *)((unsigned long)
				      le64_to_cpu(hwcqe->qp_handle));
	if (!qp) {
		dev_err(&cq->hwq.pdev->dev,
			"FP: Process Req qp is NULL\n");
		return -EINVAL;
	}
	sq = &qp->sq;

	cqe_sq_cons = HWQ_CMP(le16_to_cpu(hwcqe->sq_cons_idx), &sq->hwq);
	if (cqe_sq_cons > sq->hwq.max_elements) {
		dev_err(&cq->hwq.pdev->dev,
			"FP: CQ Process req reported sq_cons_idx 0x%x which exceeded max 0x%x\n",
			cqe_sq_cons, sq->hwq.max_elements);
		return -EINVAL;
	}

	if (qp->sq.flushed) {
		dev_dbg(&cq->hwq.pdev->dev,
			"%s: QP in Flush QP = %p\n", __func__, qp);
		goto done;
	}
	/* Require to walk the sq's swq to fabricate CQEs for all previously
	 * signaled SWQEs due to CQE aggregation from the current sq cons
	 * to the cqe_sq_cons
	 */
	cqe = *pcqe;
	while (*budget) {
		sw_sq_cons = HWQ_CMP(sq->hwq.cons, &sq->hwq);
		if (sw_sq_cons == cqe_sq_cons)
			/* Done */
			break;

		swq = &sq->swq[sw_sq_cons];
		memset(cqe, 0, sizeof(*cqe));
		cqe->opcode = CQ_BASE_CQE_TYPE_REQ;
		cqe->qp_handle = (u64)(unsigned long)qp;
		cqe->src_qp = qp->id;
		cqe->wr_id = swq->wr_id;
		if (cqe->wr_id == BNXT_QPLIB_FENCE_WRID)
			goto skip;
		cqe->type = swq->type;

		/* For the last CQE, check for status.  For errors, regardless
		 * of the request being signaled or not, it must complete with
		 * the hwcqe error status
		 */
		if (HWQ_CMP((sw_sq_cons + 1), &sq->hwq) == cqe_sq_cons &&
		    hwcqe->status != CQ_REQ_STATUS_OK) {
			cqe->status = hwcqe->status;
			dev_err(&cq->hwq.pdev->dev,
				"FP: CQ Processed Req wr_id[%d] = 0x%llx with status 0x%x\n",
				sw_sq_cons, cqe->wr_id, cqe->status);
			cqe++;
			(*budget)--;
			bnxt_qplib_mark_qp_error(qp);
			/* Add qp to flush list of the CQ */
			bnxt_qplib_add_flush_qp(qp);
		} else {
			if (swq->flags & SQ_SEND_FLAGS_SIGNAL_COMP) {
				/* Before we complete, do WA 9060 */
				if (do_wa9060(qp, cq, cq_cons, sw_sq_cons,
					      cqe_sq_cons)) {
					*lib_qp = qp;
					goto out;
				}
				cqe->status = CQ_REQ_STATUS_OK;
				cqe++;
				(*budget)--;
			}
		}
skip:
		sq->hwq.cons++;
		if (sq->single)
			break;
	}
out:
	*pcqe = cqe;
	if (HWQ_CMP(sq->hwq.cons, &sq->hwq) != cqe_sq_cons) {
		/* Out of budget */
		rc = -EAGAIN;
		goto done;
	}
	/*
	 * Back to normal completion mode only after it has completed all of
	 * the WC for this CQE
	 */
	sq->single = false;
done:
	return rc;
}

static void bnxt_qplib_release_srqe(struct bnxt_qplib_srq *srq, u32 tag)
{
	spin_lock(&srq->hwq.lock);
	srq->swq[srq->last_idx].next_idx = (int)tag;
	srq->last_idx = (int)tag;
	srq->swq[srq->last_idx].next_idx = -1;
	srq->hwq.cons++; /* Support for SRQE counter */
	spin_unlock(&srq->hwq.lock);
}

static int bnxt_qplib_cq_process_res_rc(struct bnxt_qplib_cq *cq,
					struct cq_res_rc *hwcqe,
					struct bnxt_qplib_cqe **pcqe,
					int *budget)
{
	struct bnxt_qplib_qp *qp;
	struct bnxt_qplib_q *rq;
	struct bnxt_qplib_srq *srq;
	struct bnxt_qplib_cqe *cqe;
	u32 wr_id_idx;
	int rc = 0;

	qp = (struct bnxt_qplib_qp *)((unsigned long)
				      le64_to_cpu(hwcqe->qp_handle));
	if (!qp) {
		dev_err(&cq->hwq.pdev->dev, "process_cq RC qp is NULL\n");
		return -EINVAL;
	}
	if (qp->rq.flushed) {
		dev_dbg(&cq->hwq.pdev->dev,
			"%s: QP in Flush QP = %p\n", __func__, qp);
		goto done;
	}

	cqe = *pcqe;
	cqe->opcode = hwcqe->cqe_type_toggle & CQ_BASE_CQE_TYPE_MASK;
	cqe->length = le32_to_cpu(hwcqe->length);
	cqe->invrkey = le32_to_cpu(hwcqe->imm_data_or_inv_r_key);
	cqe->mr_handle = le64_to_cpu(hwcqe->mr_handle);
	cqe->flags = le16_to_cpu(hwcqe->flags);
	cqe->status = hwcqe->status;
	cqe->qp_handle = (u64)(unsigned long)qp;

	wr_id_idx = le32_to_cpu(hwcqe->srq_or_rq_wr_id) &
				CQ_RES_RC_SRQ_OR_RQ_WR_ID_MASK;
	if (cqe->flags & CQ_RES_RC_FLAGS_SRQ_SRQ) {
		srq = qp->srq;
		if (!srq)
			return -EINVAL;
		if (wr_id_idx >= srq->hwq.max_elements) {
			dev_err(&cq->hwq.pdev->dev,
				"FP: CQ Process RC wr_id idx 0x%x exceeded SRQ max 0x%x\n",
				wr_id_idx, srq->hwq.max_elements);
			return -EINVAL;
		}
		cqe->wr_id = srq->swq[wr_id_idx].wr_id;
		bnxt_qplib_release_srqe(srq, wr_id_idx);
		cqe++;
		(*budget)--;
		*pcqe = cqe;
	} else {
		rq = &qp->rq;
		if (wr_id_idx >= rq->hwq.max_elements) {
			dev_err(&cq->hwq.pdev->dev,
				"FP: CQ Process RC wr_id idx 0x%x exceeded RQ max 0x%x\n",
				wr_id_idx, rq->hwq.max_elements);
			return -EINVAL;
		}
		cqe->wr_id = rq->swq[wr_id_idx].wr_id;
		cqe++;
		(*budget)--;
		rq->hwq.cons++;
		*pcqe = cqe;

		if (hwcqe->status != CQ_RES_RC_STATUS_OK) {
			qp->state = CMDQ_MODIFY_QP_NEW_STATE_ERR;
			/* Add qp to flush list of the CQ */
			bnxt_qplib_add_flush_qp(qp);
		}
	}

done:
	return rc;
}

static int bnxt_qplib_cq_process_res_ud(struct bnxt_qplib_cq *cq,
					struct cq_res_ud *hwcqe,
					struct bnxt_qplib_cqe **pcqe,
					int *budget)
{
	struct bnxt_qplib_qp *qp;
	struct bnxt_qplib_q *rq;
	struct bnxt_qplib_srq *srq;
	struct bnxt_qplib_cqe *cqe;
	u32 wr_id_idx;
	int rc = 0;

	qp = (struct bnxt_qplib_qp *)((unsigned long)
				      le64_to_cpu(hwcqe->qp_handle));
	if (!qp) {
		dev_err(&cq->hwq.pdev->dev, "process_cq UD qp is NULL\n");
		return -EINVAL;
	}
	if (qp->rq.flushed) {
		dev_dbg(&cq->hwq.pdev->dev,
			"%s: QP in Flush QP = %p\n", __func__, qp);
		goto done;
	}
	cqe = *pcqe;
	cqe->opcode = hwcqe->cqe_type_toggle & CQ_BASE_CQE_TYPE_MASK;
	cqe->length = (u32)le16_to_cpu(hwcqe->length);
	cqe->cfa_meta = le16_to_cpu(hwcqe->cfa_metadata);
	cqe->invrkey = le32_to_cpu(hwcqe->imm_data);
	cqe->flags = le16_to_cpu(hwcqe->flags);
	cqe->status = hwcqe->status;
	cqe->qp_handle = (u64)(unsigned long)qp;
	/*FIXME: Endianness fix needed for smace */
	memcpy(cqe->smac, hwcqe->src_mac, ETH_ALEN);
	wr_id_idx = le32_to_cpu(hwcqe->src_qp_high_srq_or_rq_wr_id)
				& CQ_RES_UD_SRQ_OR_RQ_WR_ID_MASK;
	cqe->src_qp = le16_to_cpu(hwcqe->src_qp_low) |
				  ((le32_to_cpu(
				  hwcqe->src_qp_high_srq_or_rq_wr_id) &
				 CQ_RES_UD_SRC_QP_HIGH_MASK) >> 8);

	if (cqe->flags & CQ_RES_RC_FLAGS_SRQ_SRQ) {
		srq = qp->srq;
		if (!srq)
			return -EINVAL;

		if (wr_id_idx >= srq->hwq.max_elements) {
			dev_err(&cq->hwq.pdev->dev,
				"FP: CQ Process UD wr_id idx 0x%x exceeded SRQ max 0x%x\n",
				wr_id_idx, srq->hwq.max_elements);
			return -EINVAL;
		}
		cqe->wr_id = srq->swq[wr_id_idx].wr_id;
		bnxt_qplib_release_srqe(srq, wr_id_idx);
		cqe++;
		(*budget)--;
		*pcqe = cqe;
	} else {
		rq = &qp->rq;
		if (wr_id_idx >= rq->hwq.max_elements) {
			dev_err(&cq->hwq.pdev->dev,
				"FP: CQ Process UD wr_id idx 0x%x exceeded RQ max 0x%x\n",
				wr_id_idx, rq->hwq.max_elements);
			return -EINVAL;
		}

		cqe->wr_id = rq->swq[wr_id_idx].wr_id;
		cqe++;
		(*budget)--;
		rq->hwq.cons++;
		*pcqe = cqe;

		if (hwcqe->status != CQ_RES_RC_STATUS_OK) {
			qp->state = CMDQ_MODIFY_QP_NEW_STATE_ERR;
			/* Add qp to flush list of the CQ */
			bnxt_qplib_add_flush_qp(qp);
		}
	}
done:
	return rc;
}

bool bnxt_qplib_is_cq_empty(struct bnxt_qplib_cq *cq)
{
	struct cq_base *hw_cqe, **hw_cqe_ptr;
	u32 sw_cons, raw_cons;
	bool rc = true;

	raw_cons = cq->hwq.cons;
	sw_cons = HWQ_CMP(raw_cons, &cq->hwq);
	hw_cqe_ptr = (struct cq_base **)cq->hwq.pbl_ptr;
	hw_cqe = &hw_cqe_ptr[CQE_PG(sw_cons)][CQE_IDX(sw_cons)];

	 /* Check for Valid bit. If the CQE is valid, return false */
	rc = !CQE_CMP_VALID(hw_cqe, raw_cons, cq->hwq.max_elements);
	return rc;
}

static int bnxt_qplib_cq_process_res_raweth_qp1(struct bnxt_qplib_cq *cq,
						struct cq_res_raweth_qp1 *hwcqe,
						struct bnxt_qplib_cqe **pcqe,
						int *budget)
{
	struct bnxt_qplib_qp *qp;
	struct bnxt_qplib_q *rq;
	struct bnxt_qplib_srq *srq;
	struct bnxt_qplib_cqe *cqe;
	u32 wr_id_idx;
	int rc = 0;

	qp = (struct bnxt_qplib_qp *)((unsigned long)
				      le64_to_cpu(hwcqe->qp_handle));
	if (!qp) {
		dev_err(&cq->hwq.pdev->dev, "process_cq Raw/QP1 qp is NULL\n");
		return -EINVAL;
	}
	if (qp->rq.flushed) {
		dev_dbg(&cq->hwq.pdev->dev,
			"%s: QP in Flush QP = %p\n", __func__, qp);
		goto done;
	}
	cqe = *pcqe;
	cqe->opcode = hwcqe->cqe_type_toggle & CQ_BASE_CQE_TYPE_MASK;
	cqe->flags = le16_to_cpu(hwcqe->flags);
	cqe->qp_handle = (u64)(unsigned long)qp;

	wr_id_idx =
		le32_to_cpu(hwcqe->raweth_qp1_payload_offset_srq_or_rq_wr_id)
				& CQ_RES_RAWETH_QP1_SRQ_OR_RQ_WR_ID_MASK;
	cqe->src_qp = qp->id;
	if (qp->id == 1 && !cqe->length) {
		/* Add workaround for the length misdetection */
		cqe->length = 296;
	} else {
		cqe->length = le16_to_cpu(hwcqe->length);
	}
	cqe->pkey_index = qp->pkey_index;
	memcpy(cqe->smac, qp->smac, 6);

	cqe->raweth_qp1_flags = le16_to_cpu(hwcqe->raweth_qp1_flags);
	cqe->raweth_qp1_flags2 = le32_to_cpu(hwcqe->raweth_qp1_flags2);
	cqe->raweth_qp1_metadata = le32_to_cpu(hwcqe->raweth_qp1_metadata);

	if (cqe->flags & CQ_RES_RAWETH_QP1_FLAGS_SRQ_SRQ) {
		srq = qp->srq;
		if (!srq) {
			dev_err(&cq->hwq.pdev->dev,
				"FP: SRQ used but not defined??\n");
			return -EINVAL;
		}
		if (wr_id_idx >= srq->hwq.max_elements) {
			dev_err(&cq->hwq.pdev->dev,
				"FP: CQ Process Raw/QP1 wr_id idx 0x%x exceeded SRQ max 0x%x\n",
				wr_id_idx, srq->hwq.max_elements);
			return -EINVAL;
		}
		cqe->wr_id = srq->swq[wr_id_idx].wr_id;
		bnxt_qplib_release_srqe(srq, wr_id_idx);
		cqe++;
		(*budget)--;
		*pcqe = cqe;
	} else {
		rq = &qp->rq;
		if (wr_id_idx >= rq->hwq.max_elements) {
			dev_err(&cq->hwq.pdev->dev,
				"FP: CQ Process Raw/QP1 RQ wr_id idx 0x%x exceeded RQ max 0x%x\n",
				wr_id_idx, rq->hwq.max_elements);
			return -EINVAL;
		}
		cqe->wr_id = rq->swq[wr_id_idx].wr_id;
		cqe++;
		(*budget)--;
		rq->hwq.cons++;
		*pcqe = cqe;

		if (hwcqe->status != CQ_RES_RC_STATUS_OK) {
			qp->state = CMDQ_MODIFY_QP_NEW_STATE_ERR;
			/* Add qp to flush list of the CQ */
			bnxt_qplib_add_flush_qp(qp);
		}
	}

done:
	return rc;
}

static int bnxt_qplib_cq_process_terminal(struct bnxt_qplib_cq *cq,
					  struct cq_terminal *hwcqe,
					  struct bnxt_qplib_cqe **pcqe,
					  int *budget)
{
	struct bnxt_qplib_qp *qp;
	struct bnxt_qplib_q *sq, *rq;
	struct bnxt_qplib_cqe *cqe;
	u32 sw_cons = 0, cqe_cons;
	int rc = 0;

	/* Check the Status */
	if (hwcqe->status != CQ_TERMINAL_STATUS_OK)
		dev_warn(&cq->hwq.pdev->dev,
			 "FP: CQ Process Terminal Error status = 0x%x\n",
			 hwcqe->status);

	qp = (struct bnxt_qplib_qp *)((unsigned long)
				      le64_to_cpu(hwcqe->qp_handle));
	if (!qp) {
		dev_err(&cq->hwq.pdev->dev,
			"FP: CQ Process terminal qp is NULL\n");
		return -EINVAL;
	}

	/* Must block new posting of SQ and RQ */
	qp->state = CMDQ_MODIFY_QP_NEW_STATE_ERR;

	sq = &qp->sq;
	rq = &qp->rq;

	cqe_cons = le16_to_cpu(hwcqe->sq_cons_idx);
	if (cqe_cons == 0xFFFF)
		goto do_rq;

	if (cqe_cons > sq->hwq.max_elements) {
		dev_err(&cq->hwq.pdev->dev,
			"FP: CQ Process terminal reported sq_cons_idx 0x%x which exceeded max 0x%x\n",
			cqe_cons, sq->hwq.max_elements);
		goto do_rq;
	}

	if (qp->sq.flushed) {
		dev_dbg(&cq->hwq.pdev->dev,
			"%s: QP in Flush QP = %p\n", __func__, qp);
		goto sq_done;
	}

	/* Terminal CQE can also include aggregated successful CQEs prior.
	 * So we must complete all CQEs from the current sq's cons to the
	 * cq_cons with status OK
	 */
	cqe = *pcqe;
	while (*budget) {
		sw_cons = HWQ_CMP(sq->hwq.cons, &sq->hwq);
		if (sw_cons == cqe_cons)
			break;
		if (sq->swq[sw_cons].flags & SQ_SEND_FLAGS_SIGNAL_COMP) {
			memset(cqe, 0, sizeof(*cqe));
			cqe->status = CQ_REQ_STATUS_OK;
			cqe->opcode = CQ_BASE_CQE_TYPE_REQ;
			cqe->qp_handle = (u64)(unsigned long)qp;
			cqe->src_qp = qp->id;
			cqe->wr_id = sq->swq[sw_cons].wr_id;
			cqe->type = sq->swq[sw_cons].type;
			cqe++;
			(*budget)--;
		}
		sq->hwq.cons++;
	}
	*pcqe = cqe;
	if (!(*budget) && sw_cons != cqe_cons) {
		/* Out of budget */
		rc = -EAGAIN;
		goto sq_done;
	}
sq_done:
	if (rc)
		return rc;
do_rq:
	cqe_cons = le16_to_cpu(hwcqe->rq_cons_idx);
	if (cqe_cons == 0xFFFF) {
		goto done;
	} else if (cqe_cons > rq->hwq.max_elements) {
		dev_err(&cq->hwq.pdev->dev,
			"FP: CQ Processed terminal reported rq_cons_idx 0x%x exceeds max 0x%x\n",
			cqe_cons, rq->hwq.max_elements);
		goto done;
	}

	if (qp->rq.flushed) {
		dev_dbg(&cq->hwq.pdev->dev,
			"%s: QP in Flush QP = %p\n", __func__, qp);
		rc = 0;
		goto done;
	}

	/* Terminal CQE requires all posted RQEs to complete with FLUSHED_ERR
	 * from the current rq->cons to the rq->prod regardless what the
	 * rq->cons the terminal CQE indicates
	 */

	/* Add qp to flush list of the CQ */
	bnxt_qplib_add_flush_qp(qp);
done:
	return rc;
}

static int bnxt_qplib_cq_process_cutoff(struct bnxt_qplib_cq *cq,
					struct cq_cutoff *hwcqe)
{
	/* Check the Status */
	if (hwcqe->status != CQ_CUTOFF_STATUS_OK) {
		dev_err(&cq->hwq.pdev->dev,
			"FP: CQ Process Cutoff Error status = 0x%x\n",
			hwcqe->status);
		return -EINVAL;
	}
	clear_bit(CQ_FLAGS_RESIZE_IN_PROG, &cq->flags);
	wake_up_interruptible(&cq->waitq);

	return 0;
}

int bnxt_qplib_process_flush_list(struct bnxt_qplib_cq *cq,
				  struct bnxt_qplib_cqe *cqe,
				  int num_cqes)
{
	struct bnxt_qplib_qp *qp = NULL;
	u32 budget = num_cqes;
	unsigned long flags;

	spin_lock_irqsave(&cq->flush_lock, flags);
	list_for_each_entry(qp, &cq->sqf_head, sq_flush) {
		dev_dbg(&cq->hwq.pdev->dev, "FP: Flushing SQ QP= %p\n", qp);
		__flush_sq(&qp->sq, qp, &cqe, &budget);
	}

	list_for_each_entry(qp, &cq->rqf_head, rq_flush) {
		dev_dbg(&cq->hwq.pdev->dev, "FP: Flushing RQ QP= %p\n", qp);
		__flush_rq(&qp->rq, qp, &cqe, &budget);
	}
	spin_unlock_irqrestore(&cq->flush_lock, flags);

	return num_cqes - budget;
}

int bnxt_qplib_poll_cq(struct bnxt_qplib_cq *cq, struct bnxt_qplib_cqe *cqe,
		       int num_cqes, struct bnxt_qplib_qp **lib_qp)
{
	struct cq_base *hw_cqe, **hw_cqe_ptr;
	u32 sw_cons, raw_cons;
	int budget, rc = 0;

	raw_cons = cq->hwq.cons;
	budget = num_cqes;

	while (budget) {
		sw_cons = HWQ_CMP(raw_cons, &cq->hwq);
		hw_cqe_ptr = (struct cq_base **)cq->hwq.pbl_ptr;
		hw_cqe = &hw_cqe_ptr[CQE_PG(sw_cons)][CQE_IDX(sw_cons)];

		/* Check for Valid bit */
		if (!CQE_CMP_VALID(hw_cqe, raw_cons, cq->hwq.max_elements))
			break;

		/*
		 * The valid test of the entry must be done first before
		 * reading any further.
		 */
		dma_rmb();
		/* From the device's respective CQE format to qplib_wc*/
		switch (hw_cqe->cqe_type_toggle & CQ_BASE_CQE_TYPE_MASK) {
		case CQ_BASE_CQE_TYPE_REQ:
			rc = bnxt_qplib_cq_process_req(cq,
						       (struct cq_req *)hw_cqe,
						       &cqe, &budget,
						       sw_cons, lib_qp);
			break;
		case CQ_BASE_CQE_TYPE_RES_RC:
			rc = bnxt_qplib_cq_process_res_rc(cq,
							  (struct cq_res_rc *)
							  hw_cqe, &cqe,
							  &budget);
			break;
		case CQ_BASE_CQE_TYPE_RES_UD:
			rc = bnxt_qplib_cq_process_res_ud
					(cq, (struct cq_res_ud *)hw_cqe, &cqe,
					 &budget);
			break;
		case CQ_BASE_CQE_TYPE_RES_RAWETH_QP1:
			rc = bnxt_qplib_cq_process_res_raweth_qp1
					(cq, (struct cq_res_raweth_qp1 *)
					 hw_cqe, &cqe, &budget);
			break;
		case CQ_BASE_CQE_TYPE_TERMINAL:
			rc = bnxt_qplib_cq_process_terminal
					(cq, (struct cq_terminal *)hw_cqe,
					 &cqe, &budget);
			break;
		case CQ_BASE_CQE_TYPE_CUT_OFF:
			bnxt_qplib_cq_process_cutoff
					(cq, (struct cq_cutoff *)hw_cqe);
			/* Done processing this CQ */
			goto exit;
		default:
			dev_err(&cq->hwq.pdev->dev,
				"process_cq unknown type 0x%lx\n",
				hw_cqe->cqe_type_toggle &
				CQ_BASE_CQE_TYPE_MASK);
			rc = -EINVAL;
			break;
		}
		if (rc < 0) {
			if (rc == -EAGAIN)
				break;
			/* Error while processing the CQE, just skip to the
			 * next one
			 */
			dev_err(&cq->hwq.pdev->dev,
				"process_cqe error rc = 0x%x\n", rc);
		}
		raw_cons++;
	}
	if (cq->hwq.cons != raw_cons) {
		cq->hwq.cons = raw_cons;
		bnxt_qplib_arm_cq(cq, DBC_DBC_TYPE_CQ);
	}
exit:
	return num_cqes - budget;
}

void bnxt_qplib_req_notify_cq(struct bnxt_qplib_cq *cq, u32 arm_type)
{
	if (arm_type)
		bnxt_qplib_arm_cq(cq, arm_type);
	/* Using cq->arm_state variable to track whether to issue cq handler */
	atomic_set(&cq->arm_state, 1);
}

void bnxt_qplib_flush_cqn_wq(struct bnxt_qplib_qp *qp)
{
	flush_workqueue(qp->scq->nq->cqn_wq);
	if (qp->scq != qp->rcq)
		flush_workqueue(qp->rcq->nq->cqn_wq);
}
