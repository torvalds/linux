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

#include <linux/list.h>
#include <linux/slab.h>

#include "pvrdma.h"

/**
 * pvrdma_get_dma_mr - get a DMA memory region
 * @pd: protection domain
 * @acc: access flags
 *
 * @return: ib_mr pointer on success, otherwise returns an errno.
 */
struct ib_mr *pvrdma_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct pvrdma_dev *dev = to_vdev(pd->device);
	struct pvrdma_user_mr *mr;
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_mr *cmd = &req.create_mr;
	struct pvrdma_cmd_create_mr_resp *resp = &rsp.create_mr_resp;
	int ret;

	/* Support only LOCAL_WRITE flag for DMA MRs */
	if (acc & ~IB_ACCESS_LOCAL_WRITE) {
		dev_warn(&dev->pdev->dev,
			 "unsupported dma mr access flags %#x\n", acc);
		return ERR_PTR(-EOPNOTSUPP);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_MR;
	cmd->pd_handle = to_vpd(pd)->pd_handle;
	cmd->access_flags = acc;
	cmd->flags = PVRDMA_MR_FLAG_DMA;

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_CREATE_MR_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not get DMA mem region, error: %d\n", ret);
		kfree(mr);
		return ERR_PTR(ret);
	}

	mr->mmr.mr_handle = resp->mr_handle;
	mr->ibmr.lkey = resp->lkey;
	mr->ibmr.rkey = resp->rkey;

	return &mr->ibmr;
}

/**
 * pvrdma_reg_user_mr - register a userspace memory region
 * @pd: protection domain
 * @start: starting address
 * @length: length of region
 * @virt_addr: I/O virtual address
 * @access_flags: access flags for memory region
 * @udata: user data
 *
 * @return: ib_mr pointer on success, otherwise returns an errno.
 */
struct ib_mr *pvrdma_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				 u64 virt_addr, int access_flags,
				 struct ib_udata *udata)
{
	struct pvrdma_dev *dev = to_vdev(pd->device);
	struct pvrdma_user_mr *mr = NULL;
	struct ib_umem *umem;
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_mr *cmd = &req.create_mr;
	struct pvrdma_cmd_create_mr_resp *resp = &rsp.create_mr_resp;
	int ret, npages;

	if (length == 0 || length > dev->dsr->caps.max_mr_size) {
		dev_warn(&dev->pdev->dev, "invalid mem region length\n");
		return ERR_PTR(-EINVAL);
	}

	umem = ib_umem_get(pd->device, start, length, access_flags);
	if (IS_ERR(umem)) {
		dev_warn(&dev->pdev->dev,
			 "could not get umem for mem region\n");
		return ERR_CAST(umem);
	}

	npages = ib_umem_num_dma_blocks(umem, PAGE_SIZE);
	if (npages < 0 || npages > PVRDMA_PAGE_DIR_MAX_PAGES) {
		dev_warn(&dev->pdev->dev, "overflow %d pages in mem region\n",
			 npages);
		ret = -EINVAL;
		goto err_umem;
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		ret = -ENOMEM;
		goto err_umem;
	}

	mr->mmr.iova = virt_addr;
	mr->mmr.size = length;
	mr->umem = umem;

	ret = pvrdma_page_dir_init(dev, &mr->pdir, npages, false);
	if (ret) {
		dev_warn(&dev->pdev->dev,
			 "could not allocate page directory\n");
		goto err_umem;
	}

	ret = pvrdma_page_dir_insert_umem(&mr->pdir, mr->umem, 0);
	if (ret)
		goto err_pdir;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_MR;
	cmd->start = start;
	cmd->length = length;
	cmd->pd_handle = to_vpd(pd)->pd_handle;
	cmd->access_flags = access_flags;
	cmd->nchunks = npages;
	cmd->pdir_dma = mr->pdir.dir_dma;

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_CREATE_MR_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not register mem region, error: %d\n", ret);
		goto err_pdir;
	}

	mr->mmr.mr_handle = resp->mr_handle;
	mr->ibmr.lkey = resp->lkey;
	mr->ibmr.rkey = resp->rkey;

	return &mr->ibmr;

err_pdir:
	pvrdma_page_dir_cleanup(dev, &mr->pdir);
err_umem:
	ib_umem_release(umem);
	kfree(mr);

	return ERR_PTR(ret);
}

/**
 * pvrdma_alloc_mr - allocate a memory region
 * @pd: protection domain
 * @mr_type: type of memory region
 * @max_num_sg: maximum number of pages
 *
 * @return: ib_mr pointer on success, otherwise returns an errno.
 */
struct ib_mr *pvrdma_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			      u32 max_num_sg)
{
	struct pvrdma_dev *dev = to_vdev(pd->device);
	struct pvrdma_user_mr *mr;
	union pvrdma_cmd_req req;
	union pvrdma_cmd_resp rsp;
	struct pvrdma_cmd_create_mr *cmd = &req.create_mr;
	struct pvrdma_cmd_create_mr_resp *resp = &rsp.create_mr_resp;
	int size = max_num_sg * sizeof(u64);
	int ret;

	if (mr_type != IB_MR_TYPE_MEM_REG ||
	    max_num_sg > PVRDMA_MAX_FAST_REG_PAGES)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->pages = kzalloc(size, GFP_KERNEL);
	if (!mr->pages) {
		ret = -ENOMEM;
		goto freemr;
	}

	ret = pvrdma_page_dir_init(dev, &mr->pdir, max_num_sg, false);
	if (ret) {
		dev_warn(&dev->pdev->dev,
			 "failed to allocate page dir for mr\n");
		ret = -ENOMEM;
		goto freepages;
	}

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_CREATE_MR;
	cmd->pd_handle = to_vpd(pd)->pd_handle;
	cmd->access_flags = 0;
	cmd->flags = PVRDMA_MR_FLAG_FRMR;
	cmd->nchunks = max_num_sg;

	ret = pvrdma_cmd_post(dev, &req, &rsp, PVRDMA_CMD_CREATE_MR_RESP);
	if (ret < 0) {
		dev_warn(&dev->pdev->dev,
			 "could not create FR mem region, error: %d\n", ret);
		goto freepdir;
	}

	mr->max_pages = max_num_sg;
	mr->mmr.mr_handle = resp->mr_handle;
	mr->ibmr.lkey = resp->lkey;
	mr->ibmr.rkey = resp->rkey;
	mr->page_shift = PAGE_SHIFT;
	mr->umem = NULL;

	return &mr->ibmr;

freepdir:
	pvrdma_page_dir_cleanup(dev, &mr->pdir);
freepages:
	kfree(mr->pages);
freemr:
	kfree(mr);
	return ERR_PTR(ret);
}

/**
 * pvrdma_dereg_mr - deregister a memory region
 * @ibmr: memory region
 * @udata: pointer to user data
 *
 * @return: 0 on success.
 */
int pvrdma_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct pvrdma_user_mr *mr = to_vmr(ibmr);
	struct pvrdma_dev *dev = to_vdev(ibmr->device);
	union pvrdma_cmd_req req;
	struct pvrdma_cmd_destroy_mr *cmd = &req.destroy_mr;
	int ret;

	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.cmd = PVRDMA_CMD_DESTROY_MR;
	cmd->mr_handle = mr->mmr.mr_handle;
	ret = pvrdma_cmd_post(dev, &req, NULL, 0);
	if (ret < 0)
		dev_warn(&dev->pdev->dev,
			 "could not deregister mem region, error: %d\n", ret);

	pvrdma_page_dir_cleanup(dev, &mr->pdir);
	ib_umem_release(mr->umem);

	kfree(mr->pages);
	kfree(mr);

	return 0;
}

static int pvrdma_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct pvrdma_user_mr *mr = to_vmr(ibmr);

	if (mr->npages == mr->max_pages)
		return -ENOMEM;

	mr->pages[mr->npages++] = addr;
	return 0;
}

int pvrdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		     unsigned int *sg_offset)
{
	struct pvrdma_user_mr *mr = to_vmr(ibmr);
	struct pvrdma_dev *dev = to_vdev(ibmr->device);
	int ret;

	mr->npages = 0;

	ret = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, pvrdma_set_page);
	if (ret < 0)
		dev_warn(&dev->pdev->dev, "could not map sg to pages\n");

	return ret;
}
