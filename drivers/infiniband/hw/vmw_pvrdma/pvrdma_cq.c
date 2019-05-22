/*
 * Copyright (c) 2012-2016 VMware, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of EITHER the GNU General Public License
 * version 2 as published by the Free Software Foundation or the BSD
 * 2-Clause License. This program is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; WITHOUT EVEN THE IMPLIED
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License version 2 for more details at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program available in the file COPYING in the main
 * directory of this source tree.
 *
 * The BSD 2-Clause License
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <asm/page.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/uverbs_ioctl.h>

#include "pvrdma.h"

/**
 * pvrdma_req_notify_cq - request notification for a completion queue
 * @ibcq: the completion queue
 * @notify_flags: notification flags
 *
 * @return: 0 for success.
 */
int pvrdma_req_notify_cq(struct ib_cq *ibcq,
			 enum ib_cq_notify_flags notify_flags)
{
	struct pvrdma_dev *dev = to_vdev(ibcq->device);
	struct pvrdma_cq *cq = to_vcq(ibcq);
	u32 val = cq->cq_handle;
	unsigned long flags;
	int has_data = 0;

	val |= (notify_flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		PVRDMA_UAR_CQ_ARM_SOL : PVRDMA_UAR_CQ_ARM;

	spin_lock_irqsave(&cq->cq_lock, flags);

	pvrdma_write_uar_cq(dev, val);

	if (notify_flags & IB_CQ_REPORT_MISSED_EVENTS) {
		unsigned int head;

		has_data = pvrdma_idx_ring_has_data(&cq->ring_state->rx,
						    cq->ibcq.cqe, &head);
		if (unlikely(has_data == PVRDMA_INVALID_IDX))
			dev_err(&dev->pdev->dev, "CQ ring state invalid\n");
	}

	spin_unlock_irqrestore(&cq->cq_lock, flags);

	return has_data;
}

/**
 * pvrdma_create_cq - create completion queue
 * @ibdev: the device
 * @attr: completion queue attributes
 * @udata: user data
 *
 * @return: ib_cq completion queue pointer on success,
 *          otherwise returns negative errno.
 */
struct ib_cq *pvrdma_create_cq(struct ib_device *ibdev,
			       const struct ib_cq_init_attr *attr,
			       struct ib_udata *udata)
{
	int entries = attr->cqe;
	struct pvrdma_dev *dev = to_vdev(ibdev);
	struct pvrdma_cq *cq;
	int ret;
	int npages;
	unsigned long flags;
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_cq *cmd = &req.create_cq;
	struct pvrdma_cmd_create_cq_resp *resp = &rsp.create_cq_resp;
	struct pvrdma_create_cq_resp cq_resp = {0};
	struct pvrdma_create_cq ucmd;
	struct pvrdma_ucontext *context = rdma_udata_to_drv_context(
		udata, struct pvrdma_ucontext, ibucontext);

	BUILD_BUG_ON(sizeof(struct pvrdma_cqe) != 64);

	entries = roundup_pow_of_two(entries);
	if (entries < 1 || entries > dev->dsr->caps.max_cqe)
		return ERR_PTR(-EINVAL);

	if (!atomic_add_unless(&dev->num_cqs, 1, dev->dsr->caps.max_cq))
		return ERR_PTR(-ENOMEM);

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		atomic_dec(&dev->num_cqs);
		return ERR_PTR(-ENOMEM);
	}

	cq->ibcq.cqe = entries;
	cq->is_kernel = !udata;

	if (!cq->is_kernel) {
		if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd))) {
			ret = -EFAULT;
			goto err_cq;
		}

		cq->umem = ib_umem_get(udata, ucmd.buf_addr, ucmd.buf_size,
				       IB_ACCESS_LOCAL_WRITE, 1);
		if (IS_ERR(cq->umem)) {
			ret = PTR_ERR(cq->umem);
			goto err_cq;
		}

		npages = ib_umem_page_count(cq->umem);
	} else {
		/* One extra page for shared ring state */
		npages = 1 + (entries * sizeof(struct pvrdma_cqe) +
			      PAGE_SIZE - 1) / PAGE_SIZE;

		/* Skip header page. */
		cq->offset = PAGE_SIZE;
	}

	if (npages < 0 || npages > PVRDMA_PAGE_DIR_MAX_PAGES) {
		dev_warn(&dev->pdev->dev,
			 "overflow pages in completion queue\n");
		ret = -EINVAL;
		goto err_umem;
	}

	ret = pvrdma_page_dir_init(dev, &cq->pdir, npages, cq->is_kernel);
	if (ret) {
		dev_warn(&dev->pdev->dev,
			 "could not allocate page directory\n");
		goto err_umem;
	}

	/* Ring state is always the first page. Set in library for user cq. */
	if (cq->is_kernel)
		cq->ring_state = cq->pdir.pages[0];
	else
		pvrdma_page_dir_insert_umem(&cq->pdir, cq->umem, 0);

	refcount_set(&cq->refcnt, 1);
	init_completion(&cq->free);
	spin_lock_init(&cq->cq_lock);

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_CQ;
	cmd->nchunks = npages;
	cmd->ctx_handle = context ? context->ctx_handle : 0;
	cmd->cqe = entries;
	cmd->pdir_dma = cq->pdir.dir_dma;
	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_CREATE_CQ_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not create completion queue, error: %d\n", ret);
		goto err_page_dir;
	}

	cq->ibcq.cqe = resp->cqe;
	cq->cq_handle = resp->cq_handle;
	cq_resp.cqn = resp->cq_handle;
	spin_lock_irqsave(&dev->cq_tbl_lock, flags);
	dev->cq_tbl[cq->cq_handle % dev->dsr->caps.max_cq] = cq;
	spin_unlock_irqrestore(&dev->cq_tbl_lock, flags);

	if (!cq->is_kernel) {
		cq->uar = &context->uar;

		/* Copy udata back. */
		if (ib_copy_to_udata(udata, &cq_resp, sizeof(cq_resp))) {
			dev_warn(&dev->pdev->dev,
				 "failed to copy back udata\n");
			pvrdma_destroy_cq(&cq->ibcq, udata);
			return ERR_PTR(-EINVAL);
		}
	}

	return &cq->ibcq;

err_page_dir:
	pvrdma_page_dir_cleanup(dev, &cq->pdir);
err_umem:
	if (!cq->is_kernel)
		ib_umem_release(cq->umem);
err_cq:
	atomic_dec(&dev->num_cqs);
	kfree(cq);

	return ERR_PTR(ret);
}

static void pvrdma_free_cq(struct pvrdma_dev *dev, struct pvrdma_cq *cq)
{
	if (refcount_dec_and_test(&cq->refcnt))
		complete(&cq->free);
	wait_for_completion(&cq->free);

	if (!cq->is_kernel)
		ib_umem_release(cq->umem);

	pvrdma_page_dir_cleanup(dev, &cq->pdir);
	kfree(cq);
}

/**
 * pvrdma_destroy_cq - destroy completion queue
 * @cq: the completion queue to destroy.
 * @udata: user data or null for kernel object
 *
 * @return: 0 for success.
 */
int pvrdma_destroy_cq(struct ib_cq *cq, struct ib_udata *udata)
{
	struct pvrdma_cq *vcq = to_vcq(cq);
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_destroy_cq *cmd = &req.destroy_cq;
	struct pvrdma_dev *dev = to_vdev(cq->device);
	unsigned long flags;
	int ret;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_DESTROY_CQ;
	cmd->cq_handle = vcq->cq_handle;

	ret = pvrdma_cmd_post(dev, &req, NULL, 0);
	if (ret < 0)
		dev_warn(&dev->pdev->dev,
			 "could not destroy completion queue, error: %d\n",
			 ret);

	/* free cq's resources */
	spin_lock_irqsave(&dev->cq_tbl_lock, flags);
	dev->cq_tbl[vcq->cq_handle] = NULL;
	spin_unlock_irqrestore(&dev->cq_tbl_lock, flags);

	pvrdma_free_cq(dev, vcq);
	atomic_dec(&dev->num_cqs);

	return ret;
}

static inline struct pvrdma_cqe *get_cqe(struct pvrdma_cq *cq, int i)
{
	return (struct pvrdma_cqe *)pvrdma_page_dir_get_ptr(
					&cq->pdir,
					cq->offset +
					sizeof(struct pvrdma_cqe) * i);
}

void _pvrdma_flush_cqe(struct pvrdma_qp *qp, struct pvrdma_cq *cq)
{
	unsigned int head;
	int has_data;

	if (!cq->is_kernel)
		return;

	/* Lock held */
	has_data = pvrdma_idx_ring_has_data(&cq->ring_state->rx,
					    cq->ibcq.cqe, &head);
	if (unlikely(has_data > 0)) {
		int items;
		int curr;
		int tail = pvrdma_idx(&cq->ring_state->rx.prod_tail,
				      cq->ibcq.cqe);
		struct pvrdma_cqe *cqe;
		struct pvrdma_cqe *curr_cqe;

		items = (tail > head) ? (tail - head) :
			(cq->ibcq.cqe - head + tail);
		curr = --tail;
		while (items-- > 0) {
			if (curr < 0)
				curr = cq->ibcq.cqe - 1;
			if (tail < 0)
				tail = cq->ibcq.cqe - 1;
			curr_cqe = get_cqe(cq, curr);
			if ((curr_cqe->qp & 0xFFFF) != qp->qp_handle) {
				if (curr != tail) {
					cqe = get_cqe(cq, tail);
					*cqe = *curr_cqe;
				}
				tail--;
			} else {
				pvrdma_idx_ring_inc(
					&cq->ring_state->rx.cons_head,
					cq->ibcq.cqe);
			}
			curr--;
		}
	}
}

static int pvrdma_poll_one(struct pvrdma_cq *cq, struct pvrdma_qp **cur_qp,
			   struct ib_wc *wc)
{
	struct pvrdma_dev *dev = to_vdev(cq->ibcq.device);
	int has_data;
	unsigned int head;
	bool tried = false;
	struct pvrdma_cqe *cqe;

retry:
	has_data = pvrdma_idx_ring_has_data(&cq->ring_state->rx,
					    cq->ibcq.cqe, &head);
	if (has_data == 0) {
		if (tried)
			return -EAGAIN;

		pvrdma_write_uar_cq(dev, cq->cq_handle | PVRDMA_UAR_CQ_POLL);

		tried = true;
		goto retry;
	} else if (has_data == PVRDMA_INVALID_IDX) {
		dev_err(&dev->pdev->dev, "CQ ring state invalid\n");
		return -EAGAIN;
	}

	cqe = get_cqe(cq, head);

	/* Ensure cqe is valid. */
	rmb();
	if (dev->qp_tbl[cqe->qp & 0xffff])
		*cur_qp = (struct pvrdma_qp *)dev->qp_tbl[cqe->qp & 0xffff];
	else
		return -EAGAIN;

	wc->opcode = pvrdma_wc_opcode_to_ib(cqe->opcode);
	wc->status = pvrdma_wc_status_to_ib(cqe->status);
	wc->wr_id = cqe->wr_id;
	wc->qp = &(*cur_qp)->ibqp;
	wc->byte_len = cqe->byte_len;
	wc->ex.imm_data = cqe->imm_data;
	wc->src_qp = cqe->src_qp;
	wc->wc_flags = pvrdma_wc_flags_to_ib(cqe->wc_flags);
	wc->pkey_index = cqe->pkey_index;
	wc->slid = cqe->slid;
	wc->sl = cqe->sl;
	wc->dlid_path_bits = cqe->dlid_path_bits;
	wc->port_num = cqe->port_num;
	wc->vendor_err = cqe->vendor_err;
	wc->network_hdr_type = cqe->network_hdr_type;

	/* Update shared ring state */
	pvrdma_idx_ring_inc(&cq->ring_state->rx.cons_head, cq->ibcq.cqe);

	return 0;
}

/**
 * pvrdma_poll_cq - poll for work completion queue entries
 * @ibcq: completion queue
 * @num_entries: the maximum number of entries
 * @entry: pointer to work completion array
 *
 * @return: number of polled completion entries
 */
int pvrdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct pvrdma_cq *cq = to_vcq(ibcq);
	struct pvrdma_qp *cur_qp = NULL;
	unsigned long flags;
	int npolled;

	if (num_entries < 1 || wc == NULL)
		return 0;

	spin_lock_irqsave(&cq->cq_lock, flags);
	for (npolled = 0; npolled < num_entries; ++npolled) {
		if (pvrdma_poll_one(cq, &cur_qp, wc + npolled))
			break;
	}

	spin_unlock_irqrestore(&cq->cq_lock, flags);

	/* Ensure we do not return errors from poll_cq */
	return npolled;
}
