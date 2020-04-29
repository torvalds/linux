/*
 * Copyright (c) 2016-2017 VMware, Inc.  All rights reserved.
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

/**
 * pvrdma_query_srq - query shared receive queue
 * @ibsrq: the shared receive queue to query
 * @srq_attr: attributes to query and return to client
 *
 * @return: 0 for success, otherwise returns an errno.
 */
int pvrdma_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr)
{
	struct pvrdma_dev *dev = to_vdev(ibsrq->device);
	struct pvrdma_srq *srq = to_vsrq(ibsrq);
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_query_srq *cmd = &req.query_srq;
	struct pvrdma_cmd_query_srq_resp *resp = &rsp.query_srq_resp;
	int ret;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_QUERY_SRQ;
	cmd->srq_handle = srq->srq_handle;

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_QUERY_SRQ_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not query shared receive queue, error: %d\n",
			 ret);
		return -EINVAL;
	}

	srq_attr->srq_limit = resp->attrs.srq_limit;
	srq_attr->max_wr = resp->attrs.max_wr;
	srq_attr->max_sge = resp->attrs.max_sge;

	return 0;
}

/**
 * pvrdma_create_srq - create shared receive queue
 * @pd: protection domain
 * @init_attr: shared receive queue attributes
 * @udata: user data
 *
 * @return: 0 on success, otherwise returns an errno.
 */
int pvrdma_create_srq(struct ib_srq *ibsrq, struct ib_srq_init_attr *init_attr,
		      struct ib_udata *udata)
{
	struct pvrdma_srq *srq = to_vsrq(ibsrq);
	struct pvrdma_dev *dev = to_vdev(ibsrq->device);
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_srq *cmd = &req.create_srq;
	struct pvrdma_cmd_create_srq_resp *resp = &rsp.create_srq_resp;
	struct pvrdma_create_srq_resp srq_resp = {};
	struct pvrdma_create_srq ucmd;
	unsigned long flags;
	int ret;

	if (!udata) {
		/* No support for kernel clients. */
		dev_warn(&dev->pdev->dev,
			 "no shared receive queue support for kernel client\n");
		return -EOPNOTSUPP;
	}

	if (init_attr->srq_type != IB_SRQT_BASIC) {
		dev_warn(&dev->pdev->dev,
			 "shared receive queue type %d not supported\n",
			 init_attr->srq_type);
		return -EINVAL;
	}

	if (init_attr->attr.max_wr  > dev->dsr->caps.max_srq_wr ||
	    init_attr->attr.max_sge > dev->dsr->caps.max_srq_sge) {
		dev_warn(&dev->pdev->dev,
			 "shared receive queue size invalid\n");
		return -EINVAL;
	}

	if (!atomic_add_unless(&dev->num_srqs, 1, dev->dsr->caps.max_srq))
		return -ENOMEM;

	spin_lock_init(&srq->lock);
	refcount_set(&srq->refcnt, 1);
	init_completion(&srq->free);

	dev_dbg(&dev->pdev->dev,
		"create shared receive queue from user space\n");

	if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd))) {
		ret = -EFAULT;
		goto err_srq;
	}

	srq->umem = ib_umem_get(ibsrq->device, ucmd.buf_addr, ucmd.buf_size, 0);
	if (IS_ERR(srq->umem)) {
		ret = PTR_ERR(srq->umem);
		goto err_srq;
	}

	srq->npages = ib_umem_page_count(srq->umem);

	if (srq->npages < 0 || srq->npages > PVRDMA_PAGE_DIR_MAX_PAGES) {
		dev_warn(&dev->pdev->dev,
			 "overflow pages in shared receive queue\n");
		ret = -EINVAL;
		goto err_umem;
	}

	ret = pvrdma_page_dir_init(dev, &srq->pdir, srq->npages, false);
	if (ret) {
		dev_warn(&dev->pdev->dev,
			 "could not allocate page directory\n");
		goto err_umem;
	}

	pvrdma_page_dir_insert_umem(&srq->pdir, srq->umem, 0);

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_SRQ;
	cmd->srq_type = init_attr->srq_type;
	cmd->nchunks = srq->npages;
	cmd->pd_handle = to_vpd(ibsrq->pd)->pd_handle;
	cmd->attrs.max_wr = init_attr->attr.max_wr;
	cmd->attrs.max_sge = init_attr->attr.max_sge;
	cmd->attrs.srq_limit = init_attr->attr.srq_limit;
	cmd->pdir_dma = srq->pdir.dir_dma;

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_CREATE_SRQ_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not create shared receive queue, error: %d\n",
			 ret);
		goto err_page_dir;
	}

	srq->srq_handle = resp->srqn;
	srq_resp.srqn = resp->srqn;
	spin_lock_irqsave(&dev->srq_tbl_lock, flags);
	dev->srq_tbl[srq->srq_handle % dev->dsr->caps.max_srq] = srq;
	spin_unlock_irqrestore(&dev->srq_tbl_lock, flags);

	/* Copy udata back. */
	if (ib_copy_to_udata(udata, &srq_resp, sizeof(srq_resp))) {
		dev_warn(&dev->pdev->dev, "failed to copy back udata\n");
		pvrdma_destroy_srq(&srq->ibsrq, udata);
		return -EINVAL;
	}

	return 0;

err_page_dir:
	pvrdma_page_dir_cleanup(dev, &srq->pdir);
err_umem:
	ib_umem_release(srq->umem);
err_srq:
	atomic_dec(&dev->num_srqs);

	return ret;
}

static void pvrdma_free_srq(struct pvrdma_dev *dev, struct pvrdma_srq *srq)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->srq_tbl_lock, flags);
	dev->srq_tbl[srq->srq_handle] = NULL;
	spin_unlock_irqrestore(&dev->srq_tbl_lock, flags);

	if (refcount_dec_and_test(&srq->refcnt))
		complete(&srq->free);
	wait_for_completion(&srq->free);

	/* There is no support for kernel clients, so this is safe. */
	ib_umem_release(srq->umem);

	pvrdma_page_dir_cleanup(dev, &srq->pdir);

	atomic_dec(&dev->num_srqs);
}

/**
 * pvrdma_destroy_srq - destroy shared receive queue
 * @srq: the shared receive queue to destroy
 * @udata: user data or null for kernel object
 *
 * @return: 0 for success.
 */
void pvrdma_destroy_srq(struct ib_srq *srq, struct ib_udata *udata)
{
	struct pvrdma_srq *vsrq = to_vsrq(srq);
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_destroy_srq *cmd = &req.destroy_srq;
	struct pvrdma_dev *dev = to_vdev(srq->device);
	int ret;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_DESTROY_SRQ;
	cmd->srq_handle = vsrq->srq_handle;

	ret = pvrdma_cmd_post(dev, &req, NULL, 0);
	if (ret < 0)
		dev_warn(&dev->pdev->dev,
			 "destroy shared receive queue failed, error: %d\n",
			 ret);

	pvrdma_free_srq(dev, vsrq);
}

/**
 * pvrdma_modify_srq - modify shared receive queue attributes
 * @ibsrq: the shared receive queue to modify
 * @attr: the shared receive queue's new attributes
 * @attr_mask: attributes mask
 * @udata: user data
 *
 * @returns 0 on success, otherwise returns an errno.
 */
int pvrdma_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		      enum ib_srq_attr_mask attr_mask, struct ib_udata *udata)
{
	struct pvrdma_srq *vsrq = to_vsrq(ibsrq);
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_modify_srq *cmd = &req.modify_srq;
	struct pvrdma_dev *dev = to_vdev(ibsrq->device);
	int ret;

	/* Only support SRQ limit. */
	if (!(attr_mask & IB_SRQ_LIMIT))
		return -EINVAL;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_MODIFY_SRQ;
	cmd->srq_handle = vsrq->srq_handle;
	cmd->attrs.srq_limit = attr->srq_limit;
	cmd->attr_mask = attr_mask;

	ret = pvrdma_cmd_post(dev, &req, NULL, 0);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not modify shared receive queue, error: %d\n",
			 ret);

		return -EINVAL;
	}

	return ret;
}
