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

#include "pvrdma.h"

static inline void get_cqs(struct pvrdma_qp *qp, struct pvrdma_cq **send_cq,
			   struct pvrdma_cq **recv_cq)
{
	*send_cq = to_vcq(qp->ibqp.send_cq);
	*recv_cq = to_vcq(qp->ibqp.recv_cq);
}

static void pvrdma_lock_cqs(struct pvrdma_cq *scq, struct pvrdma_cq *rcq,
			    unsigned long *scq_flags,
			    unsigned long *rcq_flags)
	__acquires(scq->cq_lock) __acquires(rcq->cq_lock)
{
	if (scq == rcq) {
		spin_lock_irqsave(&scq->cq_lock, *scq_flags);
		__acquire(rcq->cq_lock);
	} else if (scq->cq_handle < rcq->cq_handle) {
		spin_lock_irqsave(&scq->cq_lock, *scq_flags);
		spin_lock_irqsave_nested(&rcq->cq_lock, *rcq_flags,
					 SINGLE_DEPTH_NESTING);
	} else {
		spin_lock_irqsave(&rcq->cq_lock, *rcq_flags);
		spin_lock_irqsave_nested(&scq->cq_lock, *scq_flags,
					 SINGLE_DEPTH_NESTING);
	}
}

static void pvrdma_unlock_cqs(struct pvrdma_cq *scq, struct pvrdma_cq *rcq,
			      unsigned long *scq_flags,
			      unsigned long *rcq_flags)
	__releases(scq->cq_lock) __releases(rcq->cq_lock)
{
	if (scq == rcq) {
		__release(rcq->cq_lock);
		spin_unlock_irqrestore(&scq->cq_lock, *scq_flags);
	} else if (scq->cq_handle < rcq->cq_handle) {
		spin_unlock_irqrestore(&rcq->cq_lock, *rcq_flags);
		spin_unlock_irqrestore(&scq->cq_lock, *scq_flags);
	} else {
		spin_unlock_irqrestore(&scq->cq_lock, *scq_flags);
		spin_unlock_irqrestore(&rcq->cq_lock, *rcq_flags);
	}
}

static void pvrdma_reset_qp(struct pvrdma_qp *qp)
{
	struct pvrdma_cq *scq, *rcq;
	unsigned long scq_flags, rcq_flags;

	/* Clean up cqes */
	get_cqs(qp, &scq, &rcq);
	pvrdma_lock_cqs(scq, rcq, &scq_flags, &rcq_flags);

	_pvrdma_flush_cqe(qp, scq);
	if (scq != rcq)
		_pvrdma_flush_cqe(qp, rcq);

	pvrdma_unlock_cqs(scq, rcq, &scq_flags, &rcq_flags);

	/*
	 * Reset queuepair. The checks are because usermode queuepairs won't
	 * have kernel ringstates.
	 */
	if (qp->rq.ring) {
		atomic_set(&qp->rq.ring->cons_head, 0);
		atomic_set(&qp->rq.ring->prod_tail, 0);
	}
	if (qp->sq.ring) {
		atomic_set(&qp->sq.ring->cons_head, 0);
		atomic_set(&qp->sq.ring->prod_tail, 0);
	}
}

static int pvrdma_set_rq_size(struct pvrdma_dev *dev,
			      struct ib_qp_cap *req_cap,
			      struct pvrdma_qp *qp)
{
	if (req_cap->max_recv_wr > dev->dsr->caps.max_qp_wr ||
	    req_cap->max_recv_sge > dev->dsr->caps.max_sge) {
		dev_warn(&dev->pdev->dev, "recv queue size invalid\n");
		return -EINVAL;
	}

	qp->rq.wqe_cnt = roundup_pow_of_two(max(1U, req_cap->max_recv_wr));
	qp->rq.max_sg = roundup_pow_of_two(max(1U, req_cap->max_recv_sge));

	/* Write back */
	req_cap->max_recv_wr = qp->rq.wqe_cnt;
	req_cap->max_recv_sge = qp->rq.max_sg;

	qp->rq.wqe_size = roundup_pow_of_two(sizeof(struct pvrdma_rq_wqe_hdr) +
					     sizeof(struct pvrdma_sge) *
					     qp->rq.max_sg);
	qp->npages_recv = (qp->rq.wqe_cnt * qp->rq.wqe_size + PAGE_SIZE - 1) /
			  PAGE_SIZE;

	return 0;
}

static int pvrdma_set_sq_size(struct pvrdma_dev *dev, struct ib_qp_cap *req_cap,
			      struct pvrdma_qp *qp)
{
	if (req_cap->max_send_wr > dev->dsr->caps.max_qp_wr ||
	    req_cap->max_send_sge > dev->dsr->caps.max_sge) {
		dev_warn(&dev->pdev->dev, "send queue size invalid\n");
		return -EINVAL;
	}

	qp->sq.wqe_cnt = roundup_pow_of_two(max(1U, req_cap->max_send_wr));
	qp->sq.max_sg = roundup_pow_of_two(max(1U, req_cap->max_send_sge));

	/* Write back */
	req_cap->max_send_wr = qp->sq.wqe_cnt;
	req_cap->max_send_sge = qp->sq.max_sg;

	qp->sq.wqe_size = roundup_pow_of_two(sizeof(struct pvrdma_sq_wqe_hdr) +
					     sizeof(struct pvrdma_sge) *
					     qp->sq.max_sg);
	/* Note: one extra page for the header. */
	qp->npages_send = PVRDMA_QP_NUM_HEADER_PAGES +
			  (qp->sq.wqe_cnt * qp->sq.wqe_size + PAGE_SIZE - 1) /
								PAGE_SIZE;

	return 0;
}

/**
 * pvrdma_create_qp - create queue pair
 * @pd: protection domain
 * @init_attr: queue pair attributes
 * @udata: user data
 *
 * @return: the ib_qp pointer on success, otherwise returns an errno.
 */
struct ib_qp *pvrdma_create_qp(struct ib_pd *pd,
			       struct ib_qp_init_attr *init_attr,
			       struct ib_udata *udata)
{
	struct pvrdma_qp *qp = NULL;
	struct pvrdma_dev *dev = to_vdev(pd->device);
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_qp *cmd = &req.create_qp;
	struct pvrdma_cmd_create_qp_resp *resp = &rsp.create_qp_resp;
	struct pvrdma_create_qp ucmd;
	unsigned long flags;
	int ret;
	bool is_srq = !!init_attr->srq;

	if (init_attr->create_flags) {
		dev_warn(&dev->pdev->dev,
			 "invalid create queuepair flags %#x\n",
			 init_attr->create_flags);
		return ERR_PTR(-EINVAL);
	}

	if (init_attr->qp_type != IB_QPT_RC &&
	    init_attr->qp_type != IB_QPT_UD &&
	    init_attr->qp_type != IB_QPT_GSI) {
		dev_warn(&dev->pdev->dev, "queuepair type %d not supported\n",
			 init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}

	if (is_srq && !dev->dsr->caps.max_srq) {
		dev_warn(&dev->pdev->dev,
			 "SRQs not supported by device\n");
		return ERR_PTR(-EINVAL);
	}

	if (!atomic_add_unless(&dev->num_qps, 1, dev->dsr->caps.max_qp))
		return ERR_PTR(-ENOMEM);

	switch (init_attr->qp_type) {
	case IB_QPT_GSI:
		if (init_attr->port_num == 0 ||
		    init_attr->port_num > pd->device->phys_port_cnt ||
		    udata) {
			dev_warn(&dev->pdev->dev, "invalid queuepair attrs\n");
			ret = -EINVAL;
			goto err_qp;
		}
		/* fall through */
	case IB_QPT_RC:
	case IB_QPT_UD:
		qp = kzalloc(sizeof(*qp), GFP_KERNEL);
		if (!qp) {
			ret = -ENOMEM;
			goto err_qp;
		}

		spin_lock_init(&qp->sq.lock);
		spin_lock_init(&qp->rq.lock);
		mutex_init(&qp->mutex);
		refcount_set(&qp->refcnt, 1);
		init_completion(&qp->free);

		qp->state = IB_QPS_RESET;
		qp->is_kernel = !(pd->uobject && udata);

		if (!qp->is_kernel) {
			dev_dbg(&dev->pdev->dev,
				"create queuepair from user space\n");

			if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd))) {
				ret = -EFAULT;
				goto err_qp;
			}

			if (!is_srq) {
				/* set qp->sq.wqe_cnt, shift, buf_size.. */
				qp->rumem = ib_umem_get(pd->uobject->context,
							ucmd.rbuf_addr,
							ucmd.rbuf_size, 0, 0);
				if (IS_ERR(qp->rumem)) {
					ret = PTR_ERR(qp->rumem);
					goto err_qp;
				}
				qp->srq = NULL;
			} else {
				qp->rumem = NULL;
				qp->srq = to_vsrq(init_attr->srq);
			}

			qp->sumem = ib_umem_get(pd->uobject->context,
						ucmd.sbuf_addr,
						ucmd.sbuf_size, 0, 0);
			if (IS_ERR(qp->sumem)) {
				if (!is_srq)
					ib_umem_release(qp->rumem);
				ret = PTR_ERR(qp->sumem);
				goto err_qp;
			}

			qp->npages_send = ib_umem_page_count(qp->sumem);
			if (!is_srq)
				qp->npages_recv = ib_umem_page_count(qp->rumem);
			else
				qp->npages_recv = 0;
			qp->npages = qp->npages_send + qp->npages_recv;
		} else {
			ret = pvrdma_set_sq_size(to_vdev(pd->device),
						 &init_attr->cap, qp);
			if (ret)
				goto err_qp;

			ret = pvrdma_set_rq_size(to_vdev(pd->device),
						 &init_attr->cap, qp);
			if (ret)
				goto err_qp;

			qp->npages = qp->npages_send + qp->npages_recv;

			/* Skip header page. */
			qp->sq.offset = PVRDMA_QP_NUM_HEADER_PAGES * PAGE_SIZE;

			/* Recv queue pages are after send pages. */
			qp->rq.offset = qp->npages_send * PAGE_SIZE;
		}

		if (qp->npages < 0 || qp->npages > PVRDMA_PAGE_DIR_MAX_PAGES) {
			dev_warn(&dev->pdev->dev,
				 "overflow pages in queuepair\n");
			ret = -EINVAL;
			goto err_umem;
		}

		ret = pvrdma_page_dir_init(dev, &qp->pdir, qp->npages,
					   qp->is_kernel);
		if (ret) {
			dev_warn(&dev->pdev->dev,
				 "could not allocate page directory\n");
			goto err_umem;
		}

		if (!qp->is_kernel) {
			pvrdma_page_dir_insert_umem(&qp->pdir, qp->sumem, 0);
			if (!is_srq)
				pvrdma_page_dir_insert_umem(&qp->pdir,
							    qp->rumem,
							    qp->npages_send);
		} else {
			/* Ring state is always the first page. */
			qp->sq.ring = qp->pdir.pages[0];
			qp->rq.ring = is_srq ? NULL : &qp->sq.ring[1];
		}
		break;
	default:
		ret = -EINVAL;
		goto err_qp;
	}

	/* Not supported */
	init_attr->cap.max_inline_data = 0;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_QP;
	cmd->pd_handle = to_vpd(pd)->pd_handle;
	cmd->send_cq_handle = to_vcq(init_attr->send_cq)->cq_handle;
	cmd->recv_cq_handle = to_vcq(init_attr->recv_cq)->cq_handle;
	if (is_srq)
		cmd->srq_handle = to_vsrq(init_attr->srq)->srq_handle;
	else
		cmd->srq_handle = 0;
	cmd->max_send_wr = init_attr->cap.max_send_wr;
	cmd->max_recv_wr = init_attr->cap.max_recv_wr;
	cmd->max_send_sge = init_attr->cap.max_send_sge;
	cmd->max_recv_sge = init_attr->cap.max_recv_sge;
	cmd->max_inline_data = init_attr->cap.max_inline_data;
	cmd->sq_sig_all = (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR) ? 1 : 0;
	cmd->qp_type = ib_qp_type_to_pvrdma(init_attr->qp_type);
	cmd->is_srq = is_srq;
	cmd->lkey = 0;
	cmd->access_flags = IB_ACCESS_LOCAL_WRITE;
	cmd->total_chunks = qp->npages;
	cmd->send_chunks = qp->npages_send - PVRDMA_QP_NUM_HEADER_PAGES;
	cmd->pdir_dma = qp->pdir.dir_dma;

	dev_dbg(&dev->pdev->dev, "create queuepair with %d, %d, %d, %d\n",
		cmd->max_send_wr, cmd->max_recv_wr, cmd->max_send_sge,
		cmd->max_recv_sge);

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_CREATE_QP_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not create queuepair, error: %d\n", ret);
		goto err_pdir;
	}

	/* max_send_wr/_recv_wr/_send_sge/_recv_sge/_inline_data */
	qp->qp_handle = resp->qpn;
	qp->port = init_attr->port_num;
	qp->ibqp.qp_num = resp->qpn;
	spin_lock_irqsave(&dev->qp_tbl_lock, flags);
	dev->qp_tbl[qp->qp_handle % dev->dsr->caps.max_qp] = qp;
	spin_unlock_irqrestore(&dev->qp_tbl_lock, flags);

	return &qp->ibqp;

err_pdir:
	pvrdma_page_dir_cleanup(dev, &qp->pdir);
err_umem:
	if (!qp->is_kernel) {
		if (qp->rumem)
			ib_umem_release(qp->rumem);
		if (qp->sumem)
			ib_umem_release(qp->sumem);
	}
err_qp:
	kfree(qp);
	atomic_dec(&dev->num_qps);

	return ERR_PTR(ret);
}

static void pvrdma_free_qp(struct pvrdma_qp *qp)
{
	struct pvrdma_dev *dev = to_vdev(qp->ibqp.device);
	struct pvrdma_cq *scq;
	struct pvrdma_cq *rcq;
	unsigned long flags, scq_flags, rcq_flags;

	/* In case cq is polling */
	get_cqs(qp, &scq, &rcq);
	pvrdma_lock_cqs(scq, rcq, &scq_flags, &rcq_flags);

	_pvrdma_flush_cqe(qp, scq);
	if (scq != rcq)
		_pvrdma_flush_cqe(qp, rcq);

	spin_lock_irqsave(&dev->qp_tbl_lock, flags);
	dev->qp_tbl[qp->qp_handle] = NULL;
	spin_unlock_irqrestore(&dev->qp_tbl_lock, flags);

	pvrdma_unlock_cqs(scq, rcq, &scq_flags, &rcq_flags);

	if (refcount_dec_and_test(&qp->refcnt))
		complete(&qp->free);
	wait_for_completion(&qp->free);

	if (!qp->is_kernel) {
		if (qp->rumem)
			ib_umem_release(qp->rumem);
		if (qp->sumem)
			ib_umem_release(qp->sumem);
	}

	pvrdma_page_dir_cleanup(dev, &qp->pdir);

	kfree(qp);

	atomic_dec(&dev->num_qps);
}

/**
 * pvrdma_destroy_qp - destroy a queue pair
 * @qp: the queue pair to destroy
 *
 * @return: 0 on success.
 */
int pvrdma_destroy_qp(struct ib_qp *qp)
{
	struct pvrdma_qp *vqp = to_vqp(qp);
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_destroy_qp *cmd = &req.destroy_qp;
	int ret;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_DESTROY_QP;
	cmd->qp_handle = vqp->qp_handle;

	ret = pvrdma_cmd_post(to_vdev(qp->device), &req, NULL, 0);
	if (ret < 0)
		dev_warn(&to_vdev(qp->device)->pdev->dev,
			 "destroy queuepair failed, error: %d\n", ret);

	pvrdma_free_qp(vqp);

	return 0;
}

/**
 * pvrdma_modify_qp - modify queue pair attributes
 * @ibqp: the queue pair
 * @attr: the new queue pair's attributes
 * @attr_mask: attributes mask
 * @udata: user data
 *
 * @returns 0 on success, otherwise returns an errno.
 */
int pvrdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		     int attr_mask, struct ib_udata *udata)
{
	struct pvrdma_dev *dev = to_vdev(ibqp->device);
	struct pvrdma_qp *qp = to_vqp(ibqp);
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_modify_qp *cmd = &req.modify_qp;
	enum ib_qp_state cur_state, next_state;
	int ret;

	/* Sanity checking. Should need lock here */
	mutex_lock(&qp->mutex);
	cur_state = (attr_mask & IB_QP_CUR_STATE) ? attr->cur_qp_state :
		qp->state;
	next_state = (attr_mask & IB_QP_STATE) ? attr->qp_state : cur_state;

	if (!ib_modify_qp_is_ok(cur_state, next_state, ibqp->qp_type,
				attr_mask)) {
		ret = -EINVAL;
		goto out;
	}

	if (attr_mask & IB_QP_PORT) {
		if (attr->port_num == 0 ||
		    attr->port_num > ibqp->device->phys_port_cnt) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		if (attr->min_rnr_timer > 31) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		if (attr->pkey_index >= dev->dsr->caps.max_pkeys) {
			ret = -EINVAL;
			goto out;
		}
	}

	if (attr_mask & IB_QP_QKEY)
		qp->qkey = attr->qkey;

	if (cur_state == next_state && cur_state == IB_QPS_RESET) {
		ret = 0;
		goto out;
	}

	qp->state = next_state;
	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_MODIFY_QP;
	cmd->qp_handle = qp->qp_handle;
	cmd->attr_mask = ib_qp_attr_mask_to_pvrdma(attr_mask);
	cmd->attrs.qp_state = ib_qp_state_to_pvrdma(attr->qp_state);
	cmd->attrs.cur_qp_state =
		ib_qp_state_to_pvrdma(attr->cur_qp_state);
	cmd->attrs.path_mtu = ib_mtu_to_pvrdma(attr->path_mtu);
	cmd->attrs.path_mig_state =
		ib_mig_state_to_pvrdma(attr->path_mig_state);
	cmd->attrs.qkey = attr->qkey;
	cmd->attrs.rq_psn = attr->rq_psn;
	cmd->attrs.sq_psn = attr->sq_psn;
	cmd->attrs.dest_qp_num = attr->dest_qp_num;
	cmd->attrs.qp_access_flags =
		ib_access_flags_to_pvrdma(attr->qp_access_flags);
	cmd->attrs.pkey_index = attr->pkey_index;
	cmd->attrs.alt_pkey_index = attr->alt_pkey_index;
	cmd->attrs.en_sqd_async_notify = attr->en_sqd_async_notify;
	cmd->attrs.sq_draining = attr->sq_draining;
	cmd->attrs.max_rd_atomic = attr->max_rd_atomic;
	cmd->attrs.max_dest_rd_atomic = attr->max_dest_rd_atomic;
	cmd->attrs.min_rnr_timer = attr->min_rnr_timer;
	cmd->attrs.port_num = attr->port_num;
	cmd->attrs.timeout = attr->timeout;
	cmd->attrs.retry_cnt = attr->retry_cnt;
	cmd->attrs.rnr_retry = attr->rnr_retry;
	cmd->attrs.alt_port_num = attr->alt_port_num;
	cmd->attrs.alt_timeout = attr->alt_timeout;
	ib_qp_cap_to_pvrdma(&cmd->attrs.cap, &attr->cap);
	rdma_ah_attr_to_pvrdma(&cmd->attrs.ah_attr, &attr->ah_attr);
	rdma_ah_attr_to_pvrdma(&cmd->attrs.alt_ah_attr, &attr->alt_ah_attr);

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_MODIFY_QP_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not modify queuepair, error: %d\n", ret);
	} else if (rsp.hdr.err > 0) {
		dev_warn(&dev->pdev->dev,
			 "cannot modify queuepair, error: %d\n", rsp.hdr.err);
		ret = -EINVAL;
	}

	if (ret == 0 && next_state == IB_QPS_RESET)
		pvrdma_reset_qp(qp);

out:
	mutex_unlock(&qp->mutex);

	return ret;
}

static inline void *get_sq_wqe(struct pvrdma_qp *qp, unsigned int n)
{
	return pvrdma_page_dir_get_ptr(&qp->pdir,
				       qp->sq.offset + n * qp->sq.wqe_size);
}

static inline void *get_rq_wqe(struct pvrdma_qp *qp, unsigned int n)
{
	return pvrdma_page_dir_get_ptr(&qp->pdir,
				       qp->rq.offset + n * qp->rq.wqe_size);
}

static int set_reg_seg(struct pvrdma_sq_wqe_hdr *wqe_hdr,
		       const struct ib_reg_wr *wr)
{
	struct pvrdma_user_mr *mr = to_vmr(wr->mr);

	wqe_hdr->wr.fast_reg.iova_start = mr->ibmr.iova;
	wqe_hdr->wr.fast_reg.pl_pdir_dma = mr->pdir.dir_dma;
	wqe_hdr->wr.fast_reg.page_shift = mr->page_shift;
	wqe_hdr->wr.fast_reg.page_list_len = mr->npages;
	wqe_hdr->wr.fast_reg.length = mr->ibmr.length;
	wqe_hdr->wr.fast_reg.access_flags = wr->access;
	wqe_hdr->wr.fast_reg.rkey = wr->key;

	return pvrdma_page_dir_insert_page_list(&mr->pdir, mr->pages,
						mr->npages);
}

/**
 * pvrdma_post_send - post send work request entries on a QP
 * @ibqp: the QP
 * @wr: work request list to post
 * @bad_wr: the first bad WR returned
 *
 * @return: 0 on success, otherwise errno returned.
 */
int pvrdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		     const struct ib_send_wr **bad_wr)
{
	struct pvrdma_qp *qp = to_vqp(ibqp);
	struct pvrdma_dev *dev = to_vdev(ibqp->device);
	unsigned long flags;
	struct pvrdma_sq_wqe_hdr *wqe_hdr;
	struct pvrdma_sge *sge;
	int i, ret;

	/*
	 * In states lower than RTS, we can fail immediately. In other states,
	 * just post and let the device figure it out.
	 */
	if (qp->state < IB_QPS_RTS) {
		*bad_wr = wr;
		return -EINVAL;
	}

	spin_lock_irqsave(&qp->sq.lock, flags);

	while (wr) {
		unsigned int tail = 0;

		if (unlikely(!pvrdma_idx_ring_has_space(
				qp->sq.ring, qp->sq.wqe_cnt, &tail))) {
			dev_warn_ratelimited(&dev->pdev->dev,
					     "send queue is full\n");
			*bad_wr = wr;
			ret = -ENOMEM;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->sq.max_sg || wr->num_sge < 0)) {
			dev_warn_ratelimited(&dev->pdev->dev,
					     "send SGE overflow\n");
			*bad_wr = wr;
			ret = -EINVAL;
			goto out;
		}

		if (unlikely(wr->opcode < 0)) {
			dev_warn_ratelimited(&dev->pdev->dev,
					     "invalid send opcode\n");
			*bad_wr = wr;
			ret = -EINVAL;
			goto out;
		}

		/*
		 * Only support UD, RC.
		 * Need to check opcode table for thorough checking.
		 * opcode		_UD	_UC	_RC
		 * _SEND		x	x	x
		 * _SEND_WITH_IMM	x	x	x
		 * _RDMA_WRITE			x	x
		 * _RDMA_WRITE_WITH_IMM		x	x
		 * _LOCAL_INV			x	x
		 * _SEND_WITH_INV		x	x
		 * _RDMA_READ				x
		 * _ATOMIC_CMP_AND_SWP			x
		 * _ATOMIC_FETCH_AND_ADD		x
		 * _MASK_ATOMIC_CMP_AND_SWP		x
		 * _MASK_ATOMIC_FETCH_AND_ADD		x
		 * _REG_MR				x
		 *
		 */
		if (qp->ibqp.qp_type != IB_QPT_UD &&
		    qp->ibqp.qp_type != IB_QPT_RC &&
			wr->opcode != IB_WR_SEND) {
			dev_warn_ratelimited(&dev->pdev->dev,
					     "unsupported queuepair type\n");
			*bad_wr = wr;
			ret = -EINVAL;
			goto out;
		} else if (qp->ibqp.qp_type == IB_QPT_UD ||
			   qp->ibqp.qp_type == IB_QPT_GSI) {
			if (wr->opcode != IB_WR_SEND &&
			    wr->opcode != IB_WR_SEND_WITH_IMM) {
				dev_warn_ratelimited(&dev->pdev->dev,
						     "invalid send opcode\n");
				*bad_wr = wr;
				ret = -EINVAL;
				goto out;
			}
		}

		wqe_hdr = (struct pvrdma_sq_wqe_hdr *)get_sq_wqe(qp, tail);
		memset(wqe_hdr, 0, sizeof(*wqe_hdr));
		wqe_hdr->wr_id = wr->wr_id;
		wqe_hdr->num_sge = wr->num_sge;
		wqe_hdr->opcode = ib_wr_opcode_to_pvrdma(wr->opcode);
		wqe_hdr->send_flags = ib_send_flags_to_pvrdma(wr->send_flags);
		if (wr->opcode == IB_WR_SEND_WITH_IMM ||
		    wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM)
			wqe_hdr->ex.imm_data = wr->ex.imm_data;

		switch (qp->ibqp.qp_type) {
		case IB_QPT_GSI:
		case IB_QPT_UD:
			if (unlikely(!ud_wr(wr)->ah)) {
				dev_warn_ratelimited(&dev->pdev->dev,
						     "invalid address handle\n");
				*bad_wr = wr;
				ret = -EINVAL;
				goto out;
			}

			/*
			 * Use qkey from qp context if high order bit set,
			 * otherwise from work request.
			 */
			wqe_hdr->wr.ud.remote_qpn = ud_wr(wr)->remote_qpn;
			wqe_hdr->wr.ud.remote_qkey =
				ud_wr(wr)->remote_qkey & 0x80000000 ?
				qp->qkey : ud_wr(wr)->remote_qkey;
			wqe_hdr->wr.ud.av = to_vah(ud_wr(wr)->ah)->av;

			break;
		case IB_QPT_RC:
			switch (wr->opcode) {
			case IB_WR_RDMA_READ:
			case IB_WR_RDMA_WRITE:
			case IB_WR_RDMA_WRITE_WITH_IMM:
				wqe_hdr->wr.rdma.remote_addr =
					rdma_wr(wr)->remote_addr;
				wqe_hdr->wr.rdma.rkey = rdma_wr(wr)->rkey;
				break;
			case IB_WR_LOCAL_INV:
			case IB_WR_SEND_WITH_INV:
				wqe_hdr->ex.invalidate_rkey =
					wr->ex.invalidate_rkey;
				break;
			case IB_WR_ATOMIC_CMP_AND_SWP:
			case IB_WR_ATOMIC_FETCH_AND_ADD:
				wqe_hdr->wr.atomic.remote_addr =
					atomic_wr(wr)->remote_addr;
				wqe_hdr->wr.atomic.rkey = atomic_wr(wr)->rkey;
				wqe_hdr->wr.atomic.compare_add =
					atomic_wr(wr)->compare_add;
				if (wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP)
					wqe_hdr->wr.atomic.swap =
						atomic_wr(wr)->swap;
				break;
			case IB_WR_REG_MR:
				ret = set_reg_seg(wqe_hdr, reg_wr(wr));
				if (ret < 0) {
					dev_warn_ratelimited(&dev->pdev->dev,
							     "Failed to set fast register work request\n");
					*bad_wr = wr;
					goto out;
				}
				break;
			default:
				break;
			}

			break;
		default:
			dev_warn_ratelimited(&dev->pdev->dev,
					     "invalid queuepair type\n");
			ret = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		sge = (struct pvrdma_sge *)(wqe_hdr + 1);
		for (i = 0; i < wr->num_sge; i++) {
			/* Need to check wqe_size 0 or max size */
			sge->addr = wr->sg_list[i].addr;
			sge->length = wr->sg_list[i].length;
			sge->lkey = wr->sg_list[i].lkey;
			sge++;
		}

		/* Make sure wqe is written before index update */
		smp_wmb();

		/* Update shared sq ring */
		pvrdma_idx_ring_inc(&qp->sq.ring->prod_tail,
				    qp->sq.wqe_cnt);

		wr = wr->next;
	}

	ret = 0;

out:
	spin_unlock_irqrestore(&qp->sq.lock, flags);

	if (!ret)
		pvrdma_write_uar_qp(dev, PVRDMA_UAR_QP_SEND | qp->qp_handle);

	return ret;
}

/**
 * pvrdma_post_receive - post receive work request entries on a QP
 * @ibqp: the QP
 * @wr: the work request list to post
 * @bad_wr: the first bad WR returned
 *
 * @return: 0 on success, otherwise errno returned.
 */
int pvrdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		     const struct ib_recv_wr **bad_wr)
{
	struct pvrdma_dev *dev = to_vdev(ibqp->device);
	unsigned long flags;
	struct pvrdma_qp *qp = to_vqp(ibqp);
	struct pvrdma_rq_wqe_hdr *wqe_hdr;
	struct pvrdma_sge *sge;
	int ret = 0;
	int i;

	/*
	 * In the RESET state, we can fail immediately. For other states,
	 * just post and let the device figure it out.
	 */
	if (qp->state == IB_QPS_RESET) {
		*bad_wr = wr;
		return -EINVAL;
	}

	if (qp->srq) {
		dev_warn(&dev->pdev->dev, "QP associated with SRQ\n");
		*bad_wr = wr;
		return -EINVAL;
	}

	spin_lock_irqsave(&qp->rq.lock, flags);

	while (wr) {
		unsigned int tail = 0;

		if (unlikely(wr->num_sge > qp->rq.max_sg ||
			     wr->num_sge < 0)) {
			ret = -EINVAL;
			*bad_wr = wr;
			dev_warn_ratelimited(&dev->pdev->dev,
					     "recv SGE overflow\n");
			goto out;
		}

		if (unlikely(!pvrdma_idx_ring_has_space(
				qp->rq.ring, qp->rq.wqe_cnt, &tail))) {
			ret = -ENOMEM;
			*bad_wr = wr;
			dev_warn_ratelimited(&dev->pdev->dev,
					     "recv queue full\n");
			goto out;
		}

		wqe_hdr = (struct pvrdma_rq_wqe_hdr *)get_rq_wqe(qp, tail);
		wqe_hdr->wr_id = wr->wr_id;
		wqe_hdr->num_sge = wr->num_sge;
		wqe_hdr->total_len = 0;

		sge = (struct pvrdma_sge *)(wqe_hdr + 1);
		for (i = 0; i < wr->num_sge; i++) {
			sge->addr = wr->sg_list[i].addr;
			sge->length = wr->sg_list[i].length;
			sge->lkey = wr->sg_list[i].lkey;
			sge++;
		}

		/* Make sure wqe is written before index update */
		smp_wmb();

		/* Update shared rq ring */
		pvrdma_idx_ring_inc(&qp->rq.ring->prod_tail,
				    qp->rq.wqe_cnt);

		wr = wr->next;
	}

	spin_unlock_irqrestore(&qp->rq.lock, flags);

	pvrdma_write_uar_qp(dev, PVRDMA_UAR_QP_RECV | qp->qp_handle);

	return ret;

out:
	spin_unlock_irqrestore(&qp->rq.lock, flags);

	return ret;
}

/**
 * pvrdma_query_qp - query a queue pair's attributes
 * @ibqp: the queue pair to query
 * @attr: the queue pair's attributes
 * @attr_mask: attributes mask
 * @init_attr: initial queue pair attributes
 *
 * @returns 0 on success, otherwise returns an errno.
 */
int pvrdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		    int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct pvrdma_dev *dev = to_vdev(ibqp->device);
	struct pvrdma_qp *qp = to_vqp(ibqp);
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_query_qp *cmd = &req.query_qp;
	struct pvrdma_cmd_query_qp_resp *resp = &rsp.query_qp_resp;
	int ret = 0;

	mutex_lock(&qp->mutex);

	if (qp->state == IB_QPS_RESET) {
		attr->qp_state = IB_QPS_RESET;
		goto out;
	}

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_QUERY_QP;
	cmd->qp_handle = qp->qp_handle;
	cmd->attr_mask = ib_qp_attr_mask_to_pvrdma(attr_mask);

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_QUERY_QP_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not query queuepair, error: %d\n", ret);
		goto out;
	}

	attr->qp_state = pvrdma_qp_state_to_ib(resp->attrs.qp_state);
	attr->cur_qp_state =
		pvrdma_qp_state_to_ib(resp->attrs.cur_qp_state);
	attr->path_mtu = pvrdma_mtu_to_ib(resp->attrs.path_mtu);
	attr->path_mig_state =
		pvrdma_mig_state_to_ib(resp->attrs.path_mig_state);
	attr->qkey = resp->attrs.qkey;
	attr->rq_psn = resp->attrs.rq_psn;
	attr->sq_psn = resp->attrs.sq_psn;
	attr->dest_qp_num = resp->attrs.dest_qp_num;
	attr->qp_access_flags =
		pvrdma_access_flags_to_ib(resp->attrs.qp_access_flags);
	attr->pkey_index = resp->attrs.pkey_index;
	attr->alt_pkey_index = resp->attrs.alt_pkey_index;
	attr->en_sqd_async_notify = resp->attrs.en_sqd_async_notify;
	attr->sq_draining = resp->attrs.sq_draining;
	attr->max_rd_atomic = resp->attrs.max_rd_atomic;
	attr->max_dest_rd_atomic = resp->attrs.max_dest_rd_atomic;
	attr->min_rnr_timer = resp->attrs.min_rnr_timer;
	attr->port_num = resp->attrs.port_num;
	attr->timeout = resp->attrs.timeout;
	attr->retry_cnt = resp->attrs.retry_cnt;
	attr->rnr_retry = resp->attrs.rnr_retry;
	attr->alt_port_num = resp->attrs.alt_port_num;
	attr->alt_timeout = resp->attrs.alt_timeout;
	pvrdma_qp_cap_to_ib(&attr->cap, &resp->attrs.cap);
	pvrdma_ah_attr_to_rdma(&attr->ah_attr, &resp->attrs.ah_attr);
	pvrdma_ah_attr_to_rdma(&attr->alt_ah_attr, &resp->attrs.alt_ah_attr);

	qp->state = attr->qp_state;

	ret = 0;

out:
	attr->cur_qp_state = attr->qp_state;

	init_attr->event_handler = qp->ibqp.event_handler;
	init_attr->qp_context = qp->ibqp.qp_context;
	init_attr->send_cq = qp->ibqp.send_cq;
	init_attr->recv_cq = qp->ibqp.recv_cq;
	init_attr->srq = qp->ibqp.srq;
	init_attr->xrcd = NULL;
	init_attr->cap = attr->cap;
	init_attr->sq_sig_type = 0;
	init_attr->qp_type = qp->ibqp.qp_type;
	init_attr->create_flags = 0;
	init_attr->port_num = qp->port;

	mutex_unlock(&qp->mutex);
	return ret;
}
