/*******************************************************************
 * This file is part of the Emulex RoCE Device Driver for          *
 * RoCE (RDMA over Converged Ethernet) adapters.                   *
 * Copyright (C) 2008-2012 Emulex. All rights reserved.            *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 *******************************************************************/

#include <linux/dma-mapping.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_addr.h>

#include "ocrdma.h"
#include "ocrdma_hw.h"
#include "ocrdma_verbs.h"
#include "ocrdma_abi.h"

int ocrdma_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	if (index > 1)
		return -EINVAL;

	*pkey = 0xffff;
	return 0;
}

int ocrdma_query_gid(struct ib_device *ibdev, u8 port,
		     int index, union ib_gid *sgid)
{
	struct ocrdma_dev *dev;

	dev = get_ocrdma_dev(ibdev);
	memset(sgid, 0, sizeof(*sgid));
	if (index > OCRDMA_MAX_SGID)
		return -EINVAL;

	memcpy(sgid, &dev->sgid_tbl[index], sizeof(*sgid));

	return 0;
}

int ocrdma_query_device(struct ib_device *ibdev, struct ib_device_attr *attr)
{
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);

	memset(attr, 0, sizeof *attr);
	memcpy(&attr->fw_ver, &dev->attr.fw_ver[0],
	       min(sizeof(dev->attr.fw_ver), sizeof(attr->fw_ver)));
	ocrdma_get_guid(dev, (u8 *)&attr->sys_image_guid);
	attr->max_mr_size = ~0ull;
	attr->page_size_cap = 0xffff000;
	attr->vendor_id = dev->nic_info.pdev->vendor;
	attr->vendor_part_id = dev->nic_info.pdev->device;
	attr->hw_ver = 0;
	attr->max_qp = dev->attr.max_qp;
	attr->max_ah = dev->attr.max_qp;
	attr->max_qp_wr = dev->attr.max_wqe;

	attr->device_cap_flags = IB_DEVICE_CURR_QP_STATE_MOD |
					IB_DEVICE_RC_RNR_NAK_GEN |
					IB_DEVICE_SHUTDOWN_PORT |
					IB_DEVICE_SYS_IMAGE_GUID |
					IB_DEVICE_LOCAL_DMA_LKEY;
	attr->max_sge = dev->attr.max_send_sge;
	attr->max_sge_rd = dev->attr.max_send_sge;
	attr->max_cq = dev->attr.max_cq;
	attr->max_cqe = dev->attr.max_cqe;
	attr->max_mr = dev->attr.max_mr;
	attr->max_mw = 0;
	attr->max_pd = dev->attr.max_pd;
	attr->atomic_cap = 0;
	attr->max_fmr = 0;
	attr->max_map_per_fmr = 0;
	attr->max_qp_rd_atom =
	    min(dev->attr.max_ord_per_qp, dev->attr.max_ird_per_qp);
	attr->max_qp_init_rd_atom = dev->attr.max_ord_per_qp;
	attr->max_srq = (dev->attr.max_qp - 1);
	attr->max_srq_sge = attr->max_sge;
	attr->max_srq_wr = dev->attr.max_rqe;
	attr->local_ca_ack_delay = dev->attr.local_ca_ack_delay;
	attr->max_fast_reg_page_list_len = 0;
	attr->max_pkeys = 1;
	return 0;
}

int ocrdma_query_port(struct ib_device *ibdev,
		      u8 port, struct ib_port_attr *props)
{
	enum ib_port_state port_state;
	struct ocrdma_dev *dev;
	struct net_device *netdev;

	dev = get_ocrdma_dev(ibdev);
	if (port > 1) {
		ocrdma_err("%s(%d) invalid_port=0x%x\n", __func__,
			   dev->id, port);
		return -EINVAL;
	}
	netdev = dev->nic_info.netdev;
	if (netif_running(netdev) && netif_oper_up(netdev)) {
		port_state = IB_PORT_ACTIVE;
		props->phys_state = 5;
	} else {
		port_state = IB_PORT_DOWN;
		props->phys_state = 3;
	}
	props->max_mtu = IB_MTU_4096;
	props->active_mtu = iboe_get_mtu(netdev->mtu);
	props->lid = 0;
	props->lmc = 0;
	props->sm_lid = 0;
	props->sm_sl = 0;
	props->state = port_state;
	props->port_cap_flags =
	    IB_PORT_CM_SUP |
	    IB_PORT_REINIT_SUP |
	    IB_PORT_DEVICE_MGMT_SUP | IB_PORT_VENDOR_CLASS_SUP;
	props->gid_tbl_len = OCRDMA_MAX_SGID;
	props->pkey_tbl_len = 1;
	props->bad_pkey_cntr = 0;
	props->qkey_viol_cntr = 0;
	props->active_width = IB_WIDTH_1X;
	props->active_speed = 4;
	props->max_msg_sz = 0x80000000;
	props->max_vl_num = 4;
	return 0;
}

int ocrdma_modify_port(struct ib_device *ibdev, u8 port, int mask,
		       struct ib_port_modify *props)
{
	struct ocrdma_dev *dev;

	dev = get_ocrdma_dev(ibdev);
	if (port > 1) {
		ocrdma_err("%s(%d) invalid_port=0x%x\n", __func__,
			   dev->id, port);
		return -EINVAL;
	}
	return 0;
}

static int ocrdma_add_mmap(struct ocrdma_ucontext *uctx, u64 phy_addr,
			   unsigned long len)
{
	struct ocrdma_mm *mm;

	mm = kzalloc(sizeof(*mm), GFP_KERNEL);
	if (mm == NULL)
		return -ENOMEM;
	mm->key.phy_addr = phy_addr;
	mm->key.len = len;
	INIT_LIST_HEAD(&mm->entry);

	mutex_lock(&uctx->mm_list_lock);
	list_add_tail(&mm->entry, &uctx->mm_head);
	mutex_unlock(&uctx->mm_list_lock);
	return 0;
}

static void ocrdma_del_mmap(struct ocrdma_ucontext *uctx, u64 phy_addr,
			    unsigned long len)
{
	struct ocrdma_mm *mm, *tmp;

	mutex_lock(&uctx->mm_list_lock);
	list_for_each_entry_safe(mm, tmp, &uctx->mm_head, entry) {
		if (len != mm->key.len || phy_addr != mm->key.phy_addr)
			continue;

		list_del(&mm->entry);
		kfree(mm);
		break;
	}
	mutex_unlock(&uctx->mm_list_lock);
}

static bool ocrdma_search_mmap(struct ocrdma_ucontext *uctx, u64 phy_addr,
			      unsigned long len)
{
	bool found = false;
	struct ocrdma_mm *mm;

	mutex_lock(&uctx->mm_list_lock);
	list_for_each_entry(mm, &uctx->mm_head, entry) {
		if (len != mm->key.len || phy_addr != mm->key.phy_addr)
			continue;

		found = true;
		break;
	}
	mutex_unlock(&uctx->mm_list_lock);
	return found;
}

struct ib_ucontext *ocrdma_alloc_ucontext(struct ib_device *ibdev,
					  struct ib_udata *udata)
{
	int status;
	struct ocrdma_ucontext *ctx;
	struct ocrdma_alloc_ucontext_resp resp;
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);
	struct pci_dev *pdev = dev->nic_info.pdev;
	u32 map_len = roundup(sizeof(u32) * 2048, PAGE_SIZE);

	if (!udata)
		return ERR_PTR(-EFAULT);
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);
	ctx->dev = dev;
	INIT_LIST_HEAD(&ctx->mm_head);
	mutex_init(&ctx->mm_list_lock);

	ctx->ah_tbl.va = dma_alloc_coherent(&pdev->dev, map_len,
					    &ctx->ah_tbl.pa, GFP_KERNEL);
	if (!ctx->ah_tbl.va) {
		kfree(ctx);
		return ERR_PTR(-ENOMEM);
	}
	memset(ctx->ah_tbl.va, 0, map_len);
	ctx->ah_tbl.len = map_len;

	resp.ah_tbl_len = ctx->ah_tbl.len;
	resp.ah_tbl_page = ctx->ah_tbl.pa;

	status = ocrdma_add_mmap(ctx, resp.ah_tbl_page, resp.ah_tbl_len);
	if (status)
		goto map_err;
	resp.dev_id = dev->id;
	resp.max_inline_data = dev->attr.max_inline_data;
	resp.wqe_size = dev->attr.wqe_size;
	resp.rqe_size = dev->attr.rqe_size;
	resp.dpp_wqe_size = dev->attr.wqe_size;
	resp.rsvd = 0;

	memcpy(resp.fw_ver, dev->attr.fw_ver, sizeof(resp.fw_ver));
	status = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (status)
		goto cpy_err;
	return &ctx->ibucontext;

cpy_err:
	ocrdma_del_mmap(ctx, ctx->ah_tbl.pa, ctx->ah_tbl.len);
map_err:
	dma_free_coherent(&pdev->dev, ctx->ah_tbl.len, ctx->ah_tbl.va,
			  ctx->ah_tbl.pa);
	kfree(ctx);
	return ERR_PTR(status);
}

int ocrdma_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct ocrdma_mm *mm, *tmp;
	struct ocrdma_ucontext *uctx = get_ocrdma_ucontext(ibctx);
	struct pci_dev *pdev = uctx->dev->nic_info.pdev;

	ocrdma_del_mmap(uctx, uctx->ah_tbl.pa, uctx->ah_tbl.len);
	dma_free_coherent(&pdev->dev, uctx->ah_tbl.len, uctx->ah_tbl.va,
			  uctx->ah_tbl.pa);

	list_for_each_entry_safe(mm, tmp, &uctx->mm_head, entry) {
		list_del(&mm->entry);
		kfree(mm);
	}
	kfree(uctx);
	return 0;
}

int ocrdma_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct ocrdma_ucontext *ucontext = get_ocrdma_ucontext(context);
	struct ocrdma_dev *dev = ucontext->dev;
	unsigned long vm_page = vma->vm_pgoff << PAGE_SHIFT;
	u64 unmapped_db = (u64) dev->nic_info.unmapped_db;
	unsigned long len = (vma->vm_end - vma->vm_start);
	int status = 0;
	bool found;

	if (vma->vm_start & (PAGE_SIZE - 1))
		return -EINVAL;
	found = ocrdma_search_mmap(ucontext, vma->vm_pgoff << PAGE_SHIFT, len);
	if (!found)
		return -EINVAL;

	if ((vm_page >= unmapped_db) && (vm_page <= (unmapped_db +
		dev->nic_info.db_total_size)) &&
		(len <=	dev->nic_info.db_page_size)) {
		/* doorbell mapping */
		status = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					    len, vma->vm_page_prot);
	} else if (dev->nic_info.dpp_unmapped_len &&
		(vm_page >= (u64) dev->nic_info.dpp_unmapped_addr) &&
		(vm_page <= (u64) (dev->nic_info.dpp_unmapped_addr +
			dev->nic_info.dpp_unmapped_len)) &&
		(len <= dev->nic_info.dpp_unmapped_len)) {
		/* dpp area mapping */
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		status = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					    len, vma->vm_page_prot);
	} else {
		/* queue memory mapping */
		status = remap_pfn_range(vma, vma->vm_start,
					 vma->vm_pgoff, len, vma->vm_page_prot);
	}
	return status;
}

static int ocrdma_copy_pd_uresp(struct ocrdma_pd *pd,
				struct ib_ucontext *ib_ctx,
				struct ib_udata *udata)
{
	int status;
	u64 db_page_addr;
	u64 dpp_page_addr = 0;
	u32 db_page_size;
	struct ocrdma_alloc_pd_uresp rsp;
	struct ocrdma_ucontext *uctx = get_ocrdma_ucontext(ib_ctx);

	rsp.id = pd->id;
	rsp.dpp_enabled = pd->dpp_enabled;
	db_page_addr = pd->dev->nic_info.unmapped_db +
			(pd->id * pd->dev->nic_info.db_page_size);
	db_page_size = pd->dev->nic_info.db_page_size;

	status = ocrdma_add_mmap(uctx, db_page_addr, db_page_size);
	if (status)
		return status;

	if (pd->dpp_enabled) {
		dpp_page_addr = pd->dev->nic_info.dpp_unmapped_addr +
				(pd->id * OCRDMA_DPP_PAGE_SIZE);
		status = ocrdma_add_mmap(uctx, dpp_page_addr,
				 OCRDMA_DPP_PAGE_SIZE);
		if (status)
			goto dpp_map_err;
		rsp.dpp_page_addr_hi = upper_32_bits(dpp_page_addr);
		rsp.dpp_page_addr_lo = dpp_page_addr;
	}

	status = ib_copy_to_udata(udata, &rsp, sizeof(rsp));
	if (status)
		goto ucopy_err;

	pd->uctx = uctx;
	return 0;

ucopy_err:
	if (pd->dpp_enabled)
		ocrdma_del_mmap(pd->uctx, dpp_page_addr, OCRDMA_DPP_PAGE_SIZE);
dpp_map_err:
	ocrdma_del_mmap(pd->uctx, db_page_addr, db_page_size);
	return status;
}

struct ib_pd *ocrdma_alloc_pd(struct ib_device *ibdev,
			      struct ib_ucontext *context,
			      struct ib_udata *udata)
{
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);
	struct ocrdma_pd *pd;
	int status;

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);
	pd->dev = dev;
	if (udata && context) {
		pd->dpp_enabled = (dev->nic_info.dev_family ==
					OCRDMA_GEN2_FAMILY) ? true : false;
		pd->num_dpp_qp =
			pd->dpp_enabled ? OCRDMA_PD_MAX_DPP_ENABLED_QP : 0;
	}
	status = ocrdma_mbx_alloc_pd(dev, pd);
	if (status) {
		kfree(pd);
		return ERR_PTR(status);
	}
	atomic_set(&pd->use_cnt, 0);

	if (udata && context) {
		status = ocrdma_copy_pd_uresp(pd, context, udata);
		if (status)
			goto err;
	}
	return &pd->ibpd;

err:
	ocrdma_dealloc_pd(&pd->ibpd);
	return ERR_PTR(status);
}

int ocrdma_dealloc_pd(struct ib_pd *ibpd)
{
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = pd->dev;
	int status;
	u64 usr_db;

	if (atomic_read(&pd->use_cnt)) {
		ocrdma_err("%s(%d) pd=0x%x is in use.\n",
			   __func__, dev->id, pd->id);
		status = -EFAULT;
		goto dealloc_err;
	}
	status = ocrdma_mbx_dealloc_pd(dev, pd);
	if (pd->uctx) {
		u64 dpp_db = dev->nic_info.dpp_unmapped_addr +
		    (pd->id * OCRDMA_DPP_PAGE_SIZE);
		if (pd->dpp_enabled)
			ocrdma_del_mmap(pd->uctx, dpp_db, OCRDMA_DPP_PAGE_SIZE);
		usr_db = dev->nic_info.unmapped_db +
		    (pd->id * dev->nic_info.db_page_size);
		ocrdma_del_mmap(pd->uctx, usr_db, dev->nic_info.db_page_size);
	}
	kfree(pd);
dealloc_err:
	return status;
}

static struct ocrdma_mr *ocrdma_alloc_lkey(struct ib_pd *ibpd,
					   int acc, u32 num_pbls,
					   u32 addr_check)
{
	int status;
	struct ocrdma_mr *mr;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = pd->dev;

	if (acc & IB_ACCESS_REMOTE_WRITE && !(acc & IB_ACCESS_LOCAL_WRITE)) {
		ocrdma_err("%s(%d) leaving err, invalid access rights\n",
			   __func__, dev->id);
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);
	mr->hwmr.dev = dev;
	mr->hwmr.fr_mr = 0;
	mr->hwmr.local_rd = 1;
	mr->hwmr.remote_rd = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hwmr.remote_wr = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hwmr.local_wr = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hwmr.mw_bind = (acc & IB_ACCESS_MW_BIND) ? 1 : 0;
	mr->hwmr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	mr->hwmr.num_pbls = num_pbls;

	status = ocrdma_mbx_alloc_lkey(dev, &mr->hwmr, pd->id, addr_check);
	if (status) {
		kfree(mr);
		return ERR_PTR(-ENOMEM);
	}
	mr->pd = pd;
	atomic_inc(&pd->use_cnt);
	mr->ibmr.lkey = mr->hwmr.lkey;
	if (mr->hwmr.remote_wr || mr->hwmr.remote_rd)
		mr->ibmr.rkey = mr->hwmr.lkey;
	return mr;
}

struct ib_mr *ocrdma_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	struct ocrdma_mr *mr;

	mr = ocrdma_alloc_lkey(ibpd, acc, 0, OCRDMA_ADDR_CHECK_DISABLE);
	if (IS_ERR(mr))
		return ERR_CAST(mr);

	return &mr->ibmr;
}

static void ocrdma_free_mr_pbl_tbl(struct ocrdma_dev *dev,
				   struct ocrdma_hw_mr *mr)
{
	struct pci_dev *pdev = dev->nic_info.pdev;
	int i = 0;

	if (mr->pbl_table) {
		for (i = 0; i < mr->num_pbls; i++) {
			if (!mr->pbl_table[i].va)
				continue;
			dma_free_coherent(&pdev->dev, mr->pbl_size,
					  mr->pbl_table[i].va,
					  mr->pbl_table[i].pa);
		}
		kfree(mr->pbl_table);
		mr->pbl_table = NULL;
	}
}

static int ocrdma_get_pbl_info(struct ocrdma_mr *mr, u32 num_pbes)
{
	u32 num_pbls = 0;
	u32 idx = 0;
	int status = 0;
	u32 pbl_size;

	do {
		pbl_size = OCRDMA_MIN_HPAGE_SIZE * (1 << idx);
		if (pbl_size > MAX_OCRDMA_PBL_SIZE) {
			status = -EFAULT;
			break;
		}
		num_pbls = roundup(num_pbes, (pbl_size / sizeof(u64)));
		num_pbls = num_pbls / (pbl_size / sizeof(u64));
		idx++;
	} while (num_pbls >= mr->hwmr.dev->attr.max_num_mr_pbl);

	mr->hwmr.num_pbes = num_pbes;
	mr->hwmr.num_pbls = num_pbls;
	mr->hwmr.pbl_size = pbl_size;
	return status;
}

static int ocrdma_build_pbl_tbl(struct ocrdma_dev *dev, struct ocrdma_hw_mr *mr)
{
	int status = 0;
	int i;
	u32 dma_len = mr->pbl_size;
	struct pci_dev *pdev = dev->nic_info.pdev;
	void *va;
	dma_addr_t pa;

	mr->pbl_table = kzalloc(sizeof(struct ocrdma_pbl) *
				mr->num_pbls, GFP_KERNEL);

	if (!mr->pbl_table)
		return -ENOMEM;

	for (i = 0; i < mr->num_pbls; i++) {
		va = dma_alloc_coherent(&pdev->dev, dma_len, &pa, GFP_KERNEL);
		if (!va) {
			ocrdma_free_mr_pbl_tbl(dev, mr);
			status = -ENOMEM;
			break;
		}
		memset(va, 0, dma_len);
		mr->pbl_table[i].va = va;
		mr->pbl_table[i].pa = pa;
	}
	return status;
}

static void build_user_pbes(struct ocrdma_dev *dev, struct ocrdma_mr *mr,
			    u32 num_pbes)
{
	struct ocrdma_pbe *pbe;
	struct ib_umem_chunk *chunk;
	struct ocrdma_pbl *pbl_tbl = mr->hwmr.pbl_table;
	struct ib_umem *umem = mr->umem;
	int i, shift, pg_cnt, pages, pbe_cnt, total_num_pbes = 0;

	if (!mr->hwmr.num_pbes)
		return;

	pbe = (struct ocrdma_pbe *)pbl_tbl->va;
	pbe_cnt = 0;

	shift = ilog2(umem->page_size);

	list_for_each_entry(chunk, &umem->chunk_list, list) {
		/* get all the dma regions from the chunk. */
		for (i = 0; i < chunk->nmap; i++) {
			pages = sg_dma_len(&chunk->page_list[i]) >> shift;
			for (pg_cnt = 0; pg_cnt < pages; pg_cnt++) {
				/* store the page address in pbe */
				pbe->pa_lo =
				    cpu_to_le32(sg_dma_address
						(&chunk->page_list[i]) +
						(umem->page_size * pg_cnt));
				pbe->pa_hi =
				    cpu_to_le32(upper_32_bits
						((sg_dma_address
						  (&chunk->page_list[i]) +
						  umem->page_size * pg_cnt)));
				pbe_cnt += 1;
				total_num_pbes += 1;
				pbe++;

				/* if done building pbes, issue the mbx cmd. */
				if (total_num_pbes == num_pbes)
					return;

				/* if the given pbl is full storing the pbes,
				 * move to next pbl.
				 */
				if (pbe_cnt ==
					(mr->hwmr.pbl_size / sizeof(u64))) {
					pbl_tbl++;
					pbe = (struct ocrdma_pbe *)pbl_tbl->va;
					pbe_cnt = 0;
				}
			}
		}
	}
}

struct ib_mr *ocrdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				 u64 usr_addr, int acc, struct ib_udata *udata)
{
	int status = -ENOMEM;
	struct ocrdma_dev *dev;
	struct ocrdma_mr *mr;
	struct ocrdma_pd *pd;
	u32 num_pbes;

	pd = get_ocrdma_pd(ibpd);
	dev = pd->dev;

	if (acc & IB_ACCESS_REMOTE_WRITE && !(acc & IB_ACCESS_LOCAL_WRITE))
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(status);
	mr->hwmr.dev = dev;
	mr->umem = ib_umem_get(ibpd->uobject->context, start, len, acc, 0);
	if (IS_ERR(mr->umem)) {
		status = -EFAULT;
		goto umem_err;
	}
	num_pbes = ib_umem_page_count(mr->umem);
	status = ocrdma_get_pbl_info(mr, num_pbes);
	if (status)
		goto umem_err;

	mr->hwmr.pbe_size = mr->umem->page_size;
	mr->hwmr.fbo = mr->umem->offset;
	mr->hwmr.va = usr_addr;
	mr->hwmr.len = len;
	mr->hwmr.remote_wr = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hwmr.remote_rd = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hwmr.local_wr = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hwmr.local_rd = 1;
	mr->hwmr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	status = ocrdma_build_pbl_tbl(dev, &mr->hwmr);
	if (status)
		goto umem_err;
	build_user_pbes(dev, mr, num_pbes);
	status = ocrdma_reg_mr(dev, &mr->hwmr, pd->id, acc);
	if (status)
		goto mbx_err;
	mr->pd = pd;
	atomic_inc(&pd->use_cnt);
	mr->ibmr.lkey = mr->hwmr.lkey;
	if (mr->hwmr.remote_wr || mr->hwmr.remote_rd)
		mr->ibmr.rkey = mr->hwmr.lkey;

	return &mr->ibmr;

mbx_err:
	ocrdma_free_mr_pbl_tbl(dev, &mr->hwmr);
umem_err:
	kfree(mr);
	return ERR_PTR(status);
}

int ocrdma_dereg_mr(struct ib_mr *ib_mr)
{
	struct ocrdma_mr *mr = get_ocrdma_mr(ib_mr);
	struct ocrdma_dev *dev = mr->hwmr.dev;
	int status;

	status = ocrdma_mbx_dealloc_lkey(dev, mr->hwmr.fr_mr, mr->hwmr.lkey);

	if (mr->hwmr.fr_mr == 0)
		ocrdma_free_mr_pbl_tbl(dev, &mr->hwmr);

	atomic_dec(&mr->pd->use_cnt);
	/* it could be user registered memory. */
	if (mr->umem)
		ib_umem_release(mr->umem);
	kfree(mr);
	return status;
}

static int ocrdma_copy_cq_uresp(struct ocrdma_cq *cq, struct ib_udata *udata,
				struct ib_ucontext *ib_ctx)
{
	int status;
	struct ocrdma_ucontext *uctx;
	struct ocrdma_create_cq_uresp uresp;

	uresp.cq_id = cq->id;
	uresp.page_size = cq->len;
	uresp.num_pages = 1;
	uresp.max_hw_cqe = cq->max_hw_cqe;
	uresp.page_addr[0] = cq->pa;
	uresp.db_page_addr = cq->dev->nic_info.unmapped_db;
	uresp.db_page_size = cq->dev->nic_info.db_page_size;
	uresp.phase_change = cq->phase_change ? 1 : 0;
	status = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (status) {
		ocrdma_err("%s(%d) copy error cqid=0x%x.\n",
			   __func__, cq->dev->id, cq->id);
		goto err;
	}
	uctx = get_ocrdma_ucontext(ib_ctx);
	status = ocrdma_add_mmap(uctx, uresp.db_page_addr, uresp.db_page_size);
	if (status)
		goto err;
	status = ocrdma_add_mmap(uctx, uresp.page_addr[0], uresp.page_size);
	if (status) {
		ocrdma_del_mmap(uctx, uresp.db_page_addr, uresp.db_page_size);
		goto err;
	}
	cq->ucontext = uctx;
err:
	return status;
}

struct ib_cq *ocrdma_create_cq(struct ib_device *ibdev, int entries, int vector,
			       struct ib_ucontext *ib_ctx,
			       struct ib_udata *udata)
{
	struct ocrdma_cq *cq;
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);
	int status;
	struct ocrdma_create_cq_ureq ureq;

	if (udata) {
		if (ib_copy_from_udata(&ureq, udata, sizeof(ureq)))
			return ERR_PTR(-EFAULT);
	} else
		ureq.dpp_cq = 0;
	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&cq->cq_lock);
	spin_lock_init(&cq->comp_handler_lock);
	atomic_set(&cq->use_cnt, 0);
	INIT_LIST_HEAD(&cq->sq_head);
	INIT_LIST_HEAD(&cq->rq_head);
	cq->dev = dev;

	status = ocrdma_mbx_create_cq(dev, cq, entries, ureq.dpp_cq);
	if (status) {
		kfree(cq);
		return ERR_PTR(status);
	}
	if (ib_ctx) {
		status = ocrdma_copy_cq_uresp(cq, udata, ib_ctx);
		if (status)
			goto ctx_err;
	}
	cq->phase = OCRDMA_CQE_VALID;
	cq->arm_needed = true;
	dev->cq_tbl[cq->id] = cq;

	return &cq->ibcq;

ctx_err:
	ocrdma_mbx_destroy_cq(dev, cq);
	kfree(cq);
	return ERR_PTR(status);
}

int ocrdma_resize_cq(struct ib_cq *ibcq, int new_cnt,
		     struct ib_udata *udata)
{
	int status = 0;
	struct ocrdma_cq *cq = get_ocrdma_cq(ibcq);

	if (new_cnt < 1 || new_cnt > cq->max_hw_cqe) {
		status = -EINVAL;
		return status;
	}
	ibcq->cqe = new_cnt;
	return status;
}

int ocrdma_destroy_cq(struct ib_cq *ibcq)
{
	int status;
	struct ocrdma_cq *cq = get_ocrdma_cq(ibcq);
	struct ocrdma_dev *dev = cq->dev;

	if (atomic_read(&cq->use_cnt))
		return -EINVAL;

	status = ocrdma_mbx_destroy_cq(dev, cq);

	if (cq->ucontext) {
		ocrdma_del_mmap(cq->ucontext, (u64) cq->pa, cq->len);
		ocrdma_del_mmap(cq->ucontext, dev->nic_info.unmapped_db,
				dev->nic_info.db_page_size);
	}
	dev->cq_tbl[cq->id] = NULL;

	kfree(cq);
	return status;
}

static int ocrdma_add_qpn_map(struct ocrdma_dev *dev, struct ocrdma_qp *qp)
{
	int status = -EINVAL;

	if (qp->id < OCRDMA_MAX_QP && dev->qp_tbl[qp->id] == NULL) {
		dev->qp_tbl[qp->id] = qp;
		status = 0;
	}
	return status;
}

static void ocrdma_del_qpn_map(struct ocrdma_dev *dev, struct ocrdma_qp *qp)
{
	dev->qp_tbl[qp->id] = NULL;
}

static int ocrdma_check_qp_params(struct ib_pd *ibpd, struct ocrdma_dev *dev,
				  struct ib_qp_init_attr *attrs)
{
	if (attrs->qp_type != IB_QPT_GSI &&
	    attrs->qp_type != IB_QPT_RC &&
	    attrs->qp_type != IB_QPT_UD) {
		ocrdma_err("%s(%d) unsupported qp type=0x%x requested\n",
			   __func__, dev->id, attrs->qp_type);
		return -EINVAL;
	}
	if (attrs->cap.max_send_wr > dev->attr.max_wqe) {
		ocrdma_err("%s(%d) unsupported send_wr=0x%x requested\n",
			   __func__, dev->id, attrs->cap.max_send_wr);
		ocrdma_err("%s(%d) supported send_wr=0x%x\n",
			   __func__, dev->id, dev->attr.max_wqe);
		return -EINVAL;
	}
	if (!attrs->srq && (attrs->cap.max_recv_wr > dev->attr.max_rqe)) {
		ocrdma_err("%s(%d) unsupported recv_wr=0x%x requested\n",
			   __func__, dev->id, attrs->cap.max_recv_wr);
		ocrdma_err("%s(%d) supported recv_wr=0x%x\n",
			   __func__, dev->id, dev->attr.max_rqe);
		return -EINVAL;
	}
	if (attrs->cap.max_inline_data > dev->attr.max_inline_data) {
		ocrdma_err("%s(%d) unsupported inline data size=0x%x"
			   " requested\n", __func__, dev->id,
			   attrs->cap.max_inline_data);
		ocrdma_err("%s(%d) supported inline data size=0x%x\n",
			   __func__, dev->id, dev->attr.max_inline_data);
		return -EINVAL;
	}
	if (attrs->cap.max_send_sge > dev->attr.max_send_sge) {
		ocrdma_err("%s(%d) unsupported send_sge=0x%x requested\n",
			   __func__, dev->id, attrs->cap.max_send_sge);
		ocrdma_err("%s(%d) supported send_sge=0x%x\n",
			   __func__, dev->id, dev->attr.max_send_sge);
		return -EINVAL;
	}
	if (attrs->cap.max_recv_sge > dev->attr.max_recv_sge) {
		ocrdma_err("%s(%d) unsupported recv_sge=0x%x requested\n",
			   __func__, dev->id, attrs->cap.max_recv_sge);
		ocrdma_err("%s(%d) supported recv_sge=0x%x\n",
			   __func__, dev->id, dev->attr.max_recv_sge);
		return -EINVAL;
	}
	/* unprivileged user space cannot create special QP */
	if (ibpd->uobject && attrs->qp_type == IB_QPT_GSI) {
		ocrdma_err
		    ("%s(%d) Userspace can't create special QPs of type=0x%x\n",
		     __func__, dev->id, attrs->qp_type);
		return -EINVAL;
	}
	/* allow creating only one GSI type of QP */
	if (attrs->qp_type == IB_QPT_GSI && dev->gsi_qp_created) {
		ocrdma_err("%s(%d) GSI special QPs already created.\n",
			   __func__, dev->id);
		return -EINVAL;
	}
	/* verify consumer QPs are not trying to use GSI QP's CQ */
	if ((attrs->qp_type != IB_QPT_GSI) && (dev->gsi_qp_created)) {
		if ((dev->gsi_sqcq == get_ocrdma_cq(attrs->send_cq)) ||
		    (dev->gsi_sqcq == get_ocrdma_cq(attrs->send_cq))) {
			ocrdma_err("%s(%d) Consumer QP cannot use GSI CQs.\n",
				   __func__, dev->id);
			return -EINVAL;
		}
	}
	return 0;
}

static int ocrdma_copy_qp_uresp(struct ocrdma_qp *qp,
				struct ib_udata *udata, int dpp_offset,
				int dpp_credit_lmt, int srq)
{
	int status = 0;
	u64 usr_db;
	struct ocrdma_create_qp_uresp uresp;
	struct ocrdma_dev *dev = qp->dev;
	struct ocrdma_pd *pd = qp->pd;

	memset(&uresp, 0, sizeof(uresp));
	usr_db = dev->nic_info.unmapped_db +
			(pd->id * dev->nic_info.db_page_size);
	uresp.qp_id = qp->id;
	uresp.sq_dbid = qp->sq.dbid;
	uresp.num_sq_pages = 1;
	uresp.sq_page_size = qp->sq.len;
	uresp.sq_page_addr[0] = qp->sq.pa;
	uresp.num_wqe_allocated = qp->sq.max_cnt;
	if (!srq) {
		uresp.rq_dbid = qp->rq.dbid;
		uresp.num_rq_pages = 1;
		uresp.rq_page_size = qp->rq.len;
		uresp.rq_page_addr[0] = qp->rq.pa;
		uresp.num_rqe_allocated = qp->rq.max_cnt;
	}
	uresp.db_page_addr = usr_db;
	uresp.db_page_size = dev->nic_info.db_page_size;
	if (dev->nic_info.dev_family == OCRDMA_GEN2_FAMILY) {
		uresp.db_sq_offset = OCRDMA_DB_GEN2_SQ_OFFSET;
		uresp.db_rq_offset = ((qp->id & 0xFFFF) < 128) ?
			OCRDMA_DB_GEN2_RQ1_OFFSET : OCRDMA_DB_GEN2_RQ2_OFFSET;
		uresp.db_shift = (qp->id < 128) ? 24 : 16;
	} else {
		uresp.db_sq_offset = OCRDMA_DB_SQ_OFFSET;
		uresp.db_rq_offset = OCRDMA_DB_RQ_OFFSET;
		uresp.db_shift = 16;
	}

	if (qp->dpp_enabled) {
		uresp.dpp_credit = dpp_credit_lmt;
		uresp.dpp_offset = dpp_offset;
	}
	status = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (status) {
		ocrdma_err("%s(%d) user copy error.\n", __func__, dev->id);
		goto err;
	}
	status = ocrdma_add_mmap(pd->uctx, uresp.sq_page_addr[0],
				 uresp.sq_page_size);
	if (status)
		goto err;

	if (!srq) {
		status = ocrdma_add_mmap(pd->uctx, uresp.rq_page_addr[0],
					 uresp.rq_page_size);
		if (status)
			goto rq_map_err;
	}
	return status;
rq_map_err:
	ocrdma_del_mmap(pd->uctx, uresp.sq_page_addr[0], uresp.sq_page_size);
err:
	return status;
}

static void ocrdma_set_qp_db(struct ocrdma_dev *dev, struct ocrdma_qp *qp,
			     struct ocrdma_pd *pd)
{
	if (dev->nic_info.dev_family == OCRDMA_GEN2_FAMILY) {
		qp->sq_db = dev->nic_info.db +
			(pd->id * dev->nic_info.db_page_size) +
			OCRDMA_DB_GEN2_SQ_OFFSET;
		qp->rq_db = dev->nic_info.db +
			(pd->id * dev->nic_info.db_page_size) +
			((qp->id < 128) ?
			OCRDMA_DB_GEN2_RQ1_OFFSET : OCRDMA_DB_GEN2_RQ2_OFFSET);
	} else {
		qp->sq_db = dev->nic_info.db +
			(pd->id * dev->nic_info.db_page_size) +
			OCRDMA_DB_SQ_OFFSET;
		qp->rq_db = dev->nic_info.db +
			(pd->id * dev->nic_info.db_page_size) +
			OCRDMA_DB_RQ_OFFSET;
	}
}

static int ocrdma_alloc_wr_id_tbl(struct ocrdma_qp *qp)
{
	qp->wqe_wr_id_tbl =
	    kzalloc(sizeof(*(qp->wqe_wr_id_tbl)) * qp->sq.max_cnt,
		    GFP_KERNEL);
	if (qp->wqe_wr_id_tbl == NULL)
		return -ENOMEM;
	qp->rqe_wr_id_tbl =
	    kzalloc(sizeof(u64) * qp->rq.max_cnt, GFP_KERNEL);
	if (qp->rqe_wr_id_tbl == NULL)
		return -ENOMEM;

	return 0;
}

static void ocrdma_set_qp_init_params(struct ocrdma_qp *qp,
				      struct ocrdma_pd *pd,
				      struct ib_qp_init_attr *attrs)
{
	qp->pd = pd;
	spin_lock_init(&qp->q_lock);
	INIT_LIST_HEAD(&qp->sq_entry);
	INIT_LIST_HEAD(&qp->rq_entry);

	qp->qp_type = attrs->qp_type;
	qp->cap_flags = OCRDMA_QP_INB_RD | OCRDMA_QP_INB_WR;
	qp->max_inline_data = attrs->cap.max_inline_data;
	qp->sq.max_sges = attrs->cap.max_send_sge;
	qp->rq.max_sges = attrs->cap.max_recv_sge;
	qp->state = OCRDMA_QPS_RST;
}

static void ocrdma_set_qp_use_cnt(struct ocrdma_qp *qp, struct ocrdma_pd *pd)
{
	atomic_inc(&pd->use_cnt);
	atomic_inc(&qp->sq_cq->use_cnt);
	atomic_inc(&qp->rq_cq->use_cnt);
	if (qp->srq)
		atomic_inc(&qp->srq->use_cnt);
	qp->ibqp.qp_num = qp->id;
}

static void ocrdma_store_gsi_qp_cq(struct ocrdma_dev *dev,
				   struct ib_qp_init_attr *attrs)
{
	if (attrs->qp_type == IB_QPT_GSI) {
		dev->gsi_qp_created = 1;
		dev->gsi_sqcq = get_ocrdma_cq(attrs->send_cq);
		dev->gsi_rqcq = get_ocrdma_cq(attrs->recv_cq);
	}
}

struct ib_qp *ocrdma_create_qp(struct ib_pd *ibpd,
			       struct ib_qp_init_attr *attrs,
			       struct ib_udata *udata)
{
	int status;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_qp *qp;
	struct ocrdma_dev *dev = pd->dev;
	struct ocrdma_create_qp_ureq ureq;
	u16 dpp_credit_lmt, dpp_offset;

	status = ocrdma_check_qp_params(ibpd, dev, attrs);
	if (status)
		goto gen_err;

	memset(&ureq, 0, sizeof(ureq));
	if (udata) {
		if (ib_copy_from_udata(&ureq, udata, sizeof(ureq)))
			return ERR_PTR(-EFAULT);
	}
	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		status = -ENOMEM;
		goto gen_err;
	}
	qp->dev = dev;
	ocrdma_set_qp_init_params(qp, pd, attrs);

	mutex_lock(&dev->dev_lock);
	status = ocrdma_mbx_create_qp(qp, attrs, ureq.enable_dpp_cq,
					ureq.dpp_cq_id,
					&dpp_offset, &dpp_credit_lmt);
	if (status)
		goto mbx_err;

	/* user space QP's wr_id table are managed in library */
	if (udata == NULL) {
		qp->cap_flags |= (OCRDMA_QP_MW_BIND | OCRDMA_QP_LKEY0 |
				  OCRDMA_QP_FAST_REG);
		status = ocrdma_alloc_wr_id_tbl(qp);
		if (status)
			goto map_err;
	}

	status = ocrdma_add_qpn_map(dev, qp);
	if (status)
		goto map_err;
	ocrdma_set_qp_db(dev, qp, pd);
	if (udata) {
		status = ocrdma_copy_qp_uresp(qp, udata, dpp_offset,
					      dpp_credit_lmt,
					      (attrs->srq != NULL));
		if (status)
			goto cpy_err;
	}
	ocrdma_store_gsi_qp_cq(dev, attrs);
	ocrdma_set_qp_use_cnt(qp, pd);
	mutex_unlock(&dev->dev_lock);
	return &qp->ibqp;

cpy_err:
	ocrdma_del_qpn_map(dev, qp);
map_err:
	ocrdma_mbx_destroy_qp(dev, qp);
mbx_err:
	mutex_unlock(&dev->dev_lock);
	kfree(qp->wqe_wr_id_tbl);
	kfree(qp->rqe_wr_id_tbl);
	kfree(qp);
	ocrdma_err("%s(%d) error=%d\n", __func__, dev->id, status);
gen_err:
	return ERR_PTR(status);
}

int _ocrdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask)
{
	int status = 0;
	struct ocrdma_qp *qp;
	struct ocrdma_dev *dev;
	enum ib_qp_state old_qps;

	qp = get_ocrdma_qp(ibqp);
	dev = qp->dev;
	if (attr_mask & IB_QP_STATE)
		status = ocrdma_qp_state_machine(qp, attr->qp_state, &old_qps);
	/* if new and previous states are same hw doesn't need to
	 * know about it.
	 */
	if (status < 0)
		return status;
	status = ocrdma_mbx_modify_qp(dev, qp, attr, attr_mask, old_qps);
	return status;
}

int ocrdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		     int attr_mask, struct ib_udata *udata)
{
	unsigned long flags;
	int status = -EINVAL;
	struct ocrdma_qp *qp;
	struct ocrdma_dev *dev;
	enum ib_qp_state old_qps, new_qps;

	qp = get_ocrdma_qp(ibqp);
	dev = qp->dev;

	/* syncronize with multiple context trying to change, retrive qps */
	mutex_lock(&dev->dev_lock);
	/* syncronize with wqe, rqe posting and cqe processing contexts */
	spin_lock_irqsave(&qp->q_lock, flags);
	old_qps = get_ibqp_state(qp->state);
	if (attr_mask & IB_QP_STATE)
		new_qps = attr->qp_state;
	else
		new_qps = old_qps;
	spin_unlock_irqrestore(&qp->q_lock, flags);

	if (!ib_modify_qp_is_ok(old_qps, new_qps, ibqp->qp_type, attr_mask)) {
		ocrdma_err("%s(%d) invalid attribute mask=0x%x specified for "
			   "qpn=0x%x of type=0x%x old_qps=0x%x, new_qps=0x%x\n",
			   __func__, dev->id, attr_mask, qp->id, ibqp->qp_type,
			   old_qps, new_qps);
		goto param_err;
	}

	status = _ocrdma_modify_qp(ibqp, attr, attr_mask);
	if (status > 0)
		status = 0;
param_err:
	mutex_unlock(&dev->dev_lock);
	return status;
}

static enum ib_mtu ocrdma_mtu_int_to_enum(u16 mtu)
{
	switch (mtu) {
	case 256:
		return IB_MTU_256;
	case 512:
		return IB_MTU_512;
	case 1024:
		return IB_MTU_1024;
	case 2048:
		return IB_MTU_2048;
	case 4096:
		return IB_MTU_4096;
	default:
		return IB_MTU_1024;
	}
}

static int ocrdma_to_ib_qp_acc_flags(int qp_cap_flags)
{
	int ib_qp_acc_flags = 0;

	if (qp_cap_flags & OCRDMA_QP_INB_WR)
		ib_qp_acc_flags |= IB_ACCESS_REMOTE_WRITE;
	if (qp_cap_flags & OCRDMA_QP_INB_RD)
		ib_qp_acc_flags |= IB_ACCESS_LOCAL_WRITE;
	return ib_qp_acc_flags;
}

int ocrdma_query_qp(struct ib_qp *ibqp,
		    struct ib_qp_attr *qp_attr,
		    int attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	int status;
	u32 qp_state;
	struct ocrdma_qp_params params;
	struct ocrdma_qp *qp = get_ocrdma_qp(ibqp);
	struct ocrdma_dev *dev = qp->dev;

	memset(&params, 0, sizeof(params));
	mutex_lock(&dev->dev_lock);
	status = ocrdma_mbx_query_qp(dev, qp, &params);
	mutex_unlock(&dev->dev_lock);
	if (status)
		goto mbx_err;
	qp_attr->qp_state = get_ibqp_state(IB_QPS_INIT);
	qp_attr->cur_qp_state = get_ibqp_state(IB_QPS_INIT);
	qp_attr->path_mtu =
		ocrdma_mtu_int_to_enum(params.path_mtu_pkey_indx &
				OCRDMA_QP_PARAMS_PATH_MTU_MASK) >>
				OCRDMA_QP_PARAMS_PATH_MTU_SHIFT;
	qp_attr->path_mig_state = IB_MIG_MIGRATED;
	qp_attr->rq_psn = params.hop_lmt_rq_psn & OCRDMA_QP_PARAMS_RQ_PSN_MASK;
	qp_attr->sq_psn = params.tclass_sq_psn & OCRDMA_QP_PARAMS_SQ_PSN_MASK;
	qp_attr->dest_qp_num =
	    params.ack_to_rnr_rtc_dest_qpn & OCRDMA_QP_PARAMS_DEST_QPN_MASK;

	qp_attr->qp_access_flags = ocrdma_to_ib_qp_acc_flags(qp->cap_flags);
	qp_attr->cap.max_send_wr = qp->sq.max_cnt - 1;
	qp_attr->cap.max_recv_wr = qp->rq.max_cnt - 1;
	qp_attr->cap.max_send_sge = qp->sq.max_sges;
	qp_attr->cap.max_recv_sge = qp->rq.max_sges;
	qp_attr->cap.max_inline_data = dev->attr.max_inline_data;
	qp_init_attr->cap = qp_attr->cap;
	memcpy(&qp_attr->ah_attr.grh.dgid, &params.dgid[0],
	       sizeof(params.dgid));
	qp_attr->ah_attr.grh.flow_label = params.rnt_rc_sl_fl &
	    OCRDMA_QP_PARAMS_FLOW_LABEL_MASK;
	qp_attr->ah_attr.grh.sgid_index = qp->sgid_idx;
	qp_attr->ah_attr.grh.hop_limit = (params.hop_lmt_rq_psn &
					  OCRDMA_QP_PARAMS_HOP_LMT_MASK) >>
						OCRDMA_QP_PARAMS_HOP_LMT_SHIFT;
	qp_attr->ah_attr.grh.traffic_class = (params.tclass_sq_psn &
					      OCRDMA_QP_PARAMS_SQ_PSN_MASK) >>
						OCRDMA_QP_PARAMS_TCLASS_SHIFT;

	qp_attr->ah_attr.ah_flags = IB_AH_GRH;
	qp_attr->ah_attr.port_num = 1;
	qp_attr->ah_attr.sl = (params.rnt_rc_sl_fl &
			       OCRDMA_QP_PARAMS_SL_MASK) >>
				OCRDMA_QP_PARAMS_SL_SHIFT;
	qp_attr->timeout = (params.ack_to_rnr_rtc_dest_qpn &
			    OCRDMA_QP_PARAMS_ACK_TIMEOUT_MASK) >>
				OCRDMA_QP_PARAMS_ACK_TIMEOUT_SHIFT;
	qp_attr->rnr_retry = (params.ack_to_rnr_rtc_dest_qpn &
			      OCRDMA_QP_PARAMS_RNR_RETRY_CNT_MASK) >>
				OCRDMA_QP_PARAMS_RNR_RETRY_CNT_SHIFT;
	qp_attr->retry_cnt =
	    (params.rnt_rc_sl_fl & OCRDMA_QP_PARAMS_RETRY_CNT_MASK) >>
		OCRDMA_QP_PARAMS_RETRY_CNT_SHIFT;
	qp_attr->min_rnr_timer = 0;
	qp_attr->pkey_index = 0;
	qp_attr->port_num = 1;
	qp_attr->ah_attr.src_path_bits = 0;
	qp_attr->ah_attr.static_rate = 0;
	qp_attr->alt_pkey_index = 0;
	qp_attr->alt_port_num = 0;
	qp_attr->alt_timeout = 0;
	memset(&qp_attr->alt_ah_attr, 0, sizeof(qp_attr->alt_ah_attr));
	qp_state = (params.max_sge_recv_flags & OCRDMA_QP_PARAMS_STATE_MASK) >>
		    OCRDMA_QP_PARAMS_STATE_SHIFT;
	qp_attr->sq_draining = (qp_state == OCRDMA_QPS_SQ_DRAINING) ? 1 : 0;
	qp_attr->max_dest_rd_atomic =
	    params.max_ord_ird >> OCRDMA_QP_PARAMS_MAX_ORD_SHIFT;
	qp_attr->max_rd_atomic =
	    params.max_ord_ird & OCRDMA_QP_PARAMS_MAX_IRD_MASK;
	qp_attr->en_sqd_async_notify = (params.max_sge_recv_flags &
				OCRDMA_QP_PARAMS_FLAGS_SQD_ASYNC) ? 1 : 0;
mbx_err:
	return status;
}

static void ocrdma_srq_toggle_bit(struct ocrdma_srq *srq, int idx)
{
	int i = idx / 32;
	unsigned int mask = (1 << (idx % 32));

	if (srq->idx_bit_fields[i] & mask)
		srq->idx_bit_fields[i] &= ~mask;
	else
		srq->idx_bit_fields[i] |= mask;
}

static int ocrdma_hwq_free_cnt(struct ocrdma_qp_hwq_info *q)
{
	int free_cnt;
	if (q->head >= q->tail)
		free_cnt = (q->max_cnt - q->head) + q->tail;
	else
		free_cnt = q->tail - q->head;
	return free_cnt;
}

static int is_hw_sq_empty(struct ocrdma_qp *qp)
{
	return (qp->sq.tail == qp->sq.head &&
		ocrdma_hwq_free_cnt(&qp->sq) ? 1 : 0);
}

static int is_hw_rq_empty(struct ocrdma_qp *qp)
{
	return (qp->rq.tail == qp->rq.head) ? 1 : 0;
}

static void *ocrdma_hwq_head(struct ocrdma_qp_hwq_info *q)
{
	return q->va + (q->head * q->entry_size);
}

static void *ocrdma_hwq_head_from_idx(struct ocrdma_qp_hwq_info *q,
				      u32 idx)
{
	return q->va + (idx * q->entry_size);
}

static void ocrdma_hwq_inc_head(struct ocrdma_qp_hwq_info *q)
{
	q->head = (q->head + 1) & q->max_wqe_idx;
}

static void ocrdma_hwq_inc_tail(struct ocrdma_qp_hwq_info *q)
{
	q->tail = (q->tail + 1) & q->max_wqe_idx;
}

/* discard the cqe for a given QP */
static void ocrdma_discard_cqes(struct ocrdma_qp *qp, struct ocrdma_cq *cq)
{
	unsigned long cq_flags;
	unsigned long flags;
	int discard_cnt = 0;
	u32 cur_getp, stop_getp;
	struct ocrdma_cqe *cqe;
	u32 qpn = 0;

	spin_lock_irqsave(&cq->cq_lock, cq_flags);

	/* traverse through the CQEs in the hw CQ,
	 * find the matching CQE for a given qp,
	 * mark the matching one discarded by clearing qpn.
	 * ring the doorbell in the poll_cq() as
	 * we don't complete out of order cqe.
	 */

	cur_getp = cq->getp;
	/* find upto when do we reap the cq. */
	stop_getp = cur_getp;
	do {
		if (is_hw_sq_empty(qp) && (!qp->srq && is_hw_rq_empty(qp)))
			break;

		cqe = cq->va + cur_getp;
		/* if (a) done reaping whole hw cq, or
		 *    (b) qp_xq becomes empty.
		 * then exit
		 */
		qpn = cqe->cmn.qpn & OCRDMA_CQE_QPN_MASK;
		/* if previously discarded cqe found, skip that too. */
		/* check for matching qp */
		if (qpn == 0 || qpn != qp->id)
			goto skip_cqe;

		/* mark cqe discarded so that it is not picked up later
		 * in the poll_cq().
		 */
		discard_cnt += 1;
		cqe->cmn.qpn = 0;
		if (is_cqe_for_sq(cqe))
			ocrdma_hwq_inc_tail(&qp->sq);
		else {
			if (qp->srq) {
				spin_lock_irqsave(&qp->srq->q_lock, flags);
				ocrdma_hwq_inc_tail(&qp->srq->rq);
				ocrdma_srq_toggle_bit(qp->srq, cur_getp);
				spin_unlock_irqrestore(&qp->srq->q_lock, flags);

			} else
				ocrdma_hwq_inc_tail(&qp->rq);
		}
skip_cqe:
		cur_getp = (cur_getp + 1) % cq->max_hw_cqe;
	} while (cur_getp != stop_getp);
	spin_unlock_irqrestore(&cq->cq_lock, cq_flags);
}

static void ocrdma_del_flush_qp(struct ocrdma_qp *qp)
{
	int found = false;
	unsigned long flags;
	struct ocrdma_dev *dev = qp->dev;
	/* sync with any active CQ poll */

	spin_lock_irqsave(&dev->flush_q_lock, flags);
	found = ocrdma_is_qp_in_sq_flushlist(qp->sq_cq, qp);
	if (found)
		list_del(&qp->sq_entry);
	if (!qp->srq) {
		found = ocrdma_is_qp_in_rq_flushlist(qp->rq_cq, qp);
		if (found)
			list_del(&qp->rq_entry);
	}
	spin_unlock_irqrestore(&dev->flush_q_lock, flags);
}

int ocrdma_destroy_qp(struct ib_qp *ibqp)
{
	int status;
	struct ocrdma_pd *pd;
	struct ocrdma_qp *qp;
	struct ocrdma_dev *dev;
	struct ib_qp_attr attrs;
	int attr_mask = IB_QP_STATE;
	unsigned long flags;

	qp = get_ocrdma_qp(ibqp);
	dev = qp->dev;

	attrs.qp_state = IB_QPS_ERR;
	pd = qp->pd;

	/* change the QP state to ERROR */
	_ocrdma_modify_qp(ibqp, &attrs, attr_mask);

	/* ensure that CQEs for newly created QP (whose id may be same with
	 * one which just getting destroyed are same), dont get
	 * discarded until the old CQEs are discarded.
	 */
	mutex_lock(&dev->dev_lock);
	status = ocrdma_mbx_destroy_qp(dev, qp);

	/*
	 * acquire CQ lock while destroy is in progress, in order to
	 * protect against proessing in-flight CQEs for this QP.
	 */
	spin_lock_irqsave(&qp->sq_cq->cq_lock, flags);
	if (qp->rq_cq && (qp->rq_cq != qp->sq_cq))
		spin_lock(&qp->rq_cq->cq_lock);

	ocrdma_del_qpn_map(dev, qp);

	if (qp->rq_cq && (qp->rq_cq != qp->sq_cq))
		spin_unlock(&qp->rq_cq->cq_lock);
	spin_unlock_irqrestore(&qp->sq_cq->cq_lock, flags);

	if (!pd->uctx) {
		ocrdma_discard_cqes(qp, qp->sq_cq);
		ocrdma_discard_cqes(qp, qp->rq_cq);
	}
	mutex_unlock(&dev->dev_lock);

	if (pd->uctx) {
		ocrdma_del_mmap(pd->uctx, (u64) qp->sq.pa, qp->sq.len);
		if (!qp->srq)
			ocrdma_del_mmap(pd->uctx, (u64) qp->rq.pa, qp->rq.len);
	}

	ocrdma_del_flush_qp(qp);

	atomic_dec(&qp->pd->use_cnt);
	atomic_dec(&qp->sq_cq->use_cnt);
	atomic_dec(&qp->rq_cq->use_cnt);
	if (qp->srq)
		atomic_dec(&qp->srq->use_cnt);
	kfree(qp->wqe_wr_id_tbl);
	kfree(qp->rqe_wr_id_tbl);
	kfree(qp);
	return status;
}

static int ocrdma_copy_srq_uresp(struct ocrdma_srq *srq, struct ib_udata *udata)
{
	int status;
	struct ocrdma_create_srq_uresp uresp;

	uresp.rq_dbid = srq->rq.dbid;
	uresp.num_rq_pages = 1;
	uresp.rq_page_addr[0] = srq->rq.pa;
	uresp.rq_page_size = srq->rq.len;
	uresp.db_page_addr = srq->dev->nic_info.unmapped_db +
	    (srq->pd->id * srq->dev->nic_info.db_page_size);
	uresp.db_page_size = srq->dev->nic_info.db_page_size;
	uresp.num_rqe_allocated = srq->rq.max_cnt;
	if (srq->dev->nic_info.dev_family == OCRDMA_GEN2_FAMILY) {
		uresp.db_rq_offset = OCRDMA_DB_GEN2_RQ1_OFFSET;
		uresp.db_shift = 24;
	} else {
		uresp.db_rq_offset = OCRDMA_DB_RQ_OFFSET;
		uresp.db_shift = 16;
	}

	status = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (status)
		return status;
	status = ocrdma_add_mmap(srq->pd->uctx, uresp.rq_page_addr[0],
				 uresp.rq_page_size);
	if (status)
		return status;
	return status;
}

struct ib_srq *ocrdma_create_srq(struct ib_pd *ibpd,
				 struct ib_srq_init_attr *init_attr,
				 struct ib_udata *udata)
{
	int status = -ENOMEM;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = pd->dev;
	struct ocrdma_srq *srq;

	if (init_attr->attr.max_sge > dev->attr.max_recv_sge)
		return ERR_PTR(-EINVAL);
	if (init_attr->attr.max_wr > dev->attr.max_rqe)
		return ERR_PTR(-EINVAL);

	srq = kzalloc(sizeof(*srq), GFP_KERNEL);
	if (!srq)
		return ERR_PTR(status);

	spin_lock_init(&srq->q_lock);
	srq->dev = dev;
	srq->pd = pd;
	srq->db = dev->nic_info.db + (pd->id * dev->nic_info.db_page_size);
	status = ocrdma_mbx_create_srq(srq, init_attr, pd);
	if (status)
		goto err;

	if (udata == NULL) {
		srq->rqe_wr_id_tbl = kzalloc(sizeof(u64) * srq->rq.max_cnt,
			    GFP_KERNEL);
		if (srq->rqe_wr_id_tbl == NULL)
			goto arm_err;

		srq->bit_fields_len = (srq->rq.max_cnt / 32) +
		    (srq->rq.max_cnt % 32 ? 1 : 0);
		srq->idx_bit_fields =
		    kmalloc(srq->bit_fields_len * sizeof(u32), GFP_KERNEL);
		if (srq->idx_bit_fields == NULL)
			goto arm_err;
		memset(srq->idx_bit_fields, 0xff,
		       srq->bit_fields_len * sizeof(u32));
	}

	if (init_attr->attr.srq_limit) {
		status = ocrdma_mbx_modify_srq(srq, &init_attr->attr);
		if (status)
			goto arm_err;
	}

	atomic_set(&srq->use_cnt, 0);
	if (udata) {
		status = ocrdma_copy_srq_uresp(srq, udata);
		if (status)
			goto arm_err;
	}

	atomic_inc(&pd->use_cnt);
	return &srq->ibsrq;

arm_err:
	ocrdma_mbx_destroy_srq(dev, srq);
err:
	kfree(srq->rqe_wr_id_tbl);
	kfree(srq->idx_bit_fields);
	kfree(srq);
	return ERR_PTR(status);
}

int ocrdma_modify_srq(struct ib_srq *ibsrq,
		      struct ib_srq_attr *srq_attr,
		      enum ib_srq_attr_mask srq_attr_mask,
		      struct ib_udata *udata)
{
	int status = 0;
	struct ocrdma_srq *srq;

	srq = get_ocrdma_srq(ibsrq);
	if (srq_attr_mask & IB_SRQ_MAX_WR)
		status = -EINVAL;
	else
		status = ocrdma_mbx_modify_srq(srq, srq_attr);
	return status;
}

int ocrdma_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr)
{
	int status;
	struct ocrdma_srq *srq;

	srq = get_ocrdma_srq(ibsrq);
	status = ocrdma_mbx_query_srq(srq, srq_attr);
	return status;
}

int ocrdma_destroy_srq(struct ib_srq *ibsrq)
{
	int status;
	struct ocrdma_srq *srq;
	struct ocrdma_dev *dev;

	srq = get_ocrdma_srq(ibsrq);
	dev = srq->dev;
	if (atomic_read(&srq->use_cnt)) {
		ocrdma_err("%s(%d) err, srq=0x%x in use\n",
			   __func__, dev->id, srq->id);
		return -EAGAIN;
	}

	status = ocrdma_mbx_destroy_srq(dev, srq);

	if (srq->pd->uctx)
		ocrdma_del_mmap(srq->pd->uctx, (u64) srq->rq.pa, srq->rq.len);

	atomic_dec(&srq->pd->use_cnt);
	kfree(srq->idx_bit_fields);
	kfree(srq->rqe_wr_id_tbl);
	kfree(srq);
	return status;
}

/* unprivileged verbs and their support functions. */
static void ocrdma_build_ud_hdr(struct ocrdma_qp *qp,
				struct ocrdma_hdr_wqe *hdr,
				struct ib_send_wr *wr)
{
	struct ocrdma_ewqe_ud_hdr *ud_hdr =
		(struct ocrdma_ewqe_ud_hdr *)(hdr + 1);
	struct ocrdma_ah *ah = get_ocrdma_ah(wr->wr.ud.ah);

	ud_hdr->rsvd_dest_qpn = wr->wr.ud.remote_qpn;
	if (qp->qp_type == IB_QPT_GSI)
		ud_hdr->qkey = qp->qkey;
	else
		ud_hdr->qkey = wr->wr.ud.remote_qkey;
	ud_hdr->rsvd_ahid = ah->id;
}

static void ocrdma_build_sges(struct ocrdma_hdr_wqe *hdr,
			      struct ocrdma_sge *sge, int num_sge,
			      struct ib_sge *sg_list)
{
	int i;

	for (i = 0; i < num_sge; i++) {
		sge[i].lrkey = sg_list[i].lkey;
		sge[i].addr_lo = sg_list[i].addr;
		sge[i].addr_hi = upper_32_bits(sg_list[i].addr);
		sge[i].len = sg_list[i].length;
		hdr->total_len += sg_list[i].length;
	}
	if (num_sge == 0)
		memset(sge, 0, sizeof(*sge));
}

static int ocrdma_build_inline_sges(struct ocrdma_qp *qp,
				    struct ocrdma_hdr_wqe *hdr,
				    struct ocrdma_sge *sge,
				    struct ib_send_wr *wr, u32 wqe_size)
{
	if (wr->send_flags & IB_SEND_INLINE) {
		if (wr->sg_list[0].length > qp->max_inline_data) {
			ocrdma_err("%s() supported_len=0x%x,"
				" unspported len req=0x%x\n", __func__,
				qp->max_inline_data, wr->sg_list[0].length);
			return -EINVAL;
		}
		memcpy(sge,
		       (void *)(unsigned long)wr->sg_list[0].addr,
		       wr->sg_list[0].length);
		hdr->total_len = wr->sg_list[0].length;
		wqe_size += roundup(hdr->total_len, OCRDMA_WQE_ALIGN_BYTES);
		hdr->cw |= (OCRDMA_TYPE_INLINE << OCRDMA_WQE_TYPE_SHIFT);
	} else {
		ocrdma_build_sges(hdr, sge, wr->num_sge, wr->sg_list);
		if (wr->num_sge)
			wqe_size += (wr->num_sge * sizeof(struct ocrdma_sge));
		else
			wqe_size += sizeof(struct ocrdma_sge);
		hdr->cw |= (OCRDMA_TYPE_LKEY << OCRDMA_WQE_TYPE_SHIFT);
	}
	hdr->cw |= ((wqe_size / OCRDMA_WQE_STRIDE) << OCRDMA_WQE_SIZE_SHIFT);
	return 0;
}

static int ocrdma_build_send(struct ocrdma_qp *qp, struct ocrdma_hdr_wqe *hdr,
			     struct ib_send_wr *wr)
{
	int status;
	struct ocrdma_sge *sge;
	u32 wqe_size = sizeof(*hdr);

	if (qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_GSI) {
		ocrdma_build_ud_hdr(qp, hdr, wr);
		sge = (struct ocrdma_sge *)(hdr + 2);
		wqe_size += sizeof(struct ocrdma_ewqe_ud_hdr);
	} else
		sge = (struct ocrdma_sge *)(hdr + 1);

	status = ocrdma_build_inline_sges(qp, hdr, sge, wr, wqe_size);
	return status;
}

static int ocrdma_build_write(struct ocrdma_qp *qp, struct ocrdma_hdr_wqe *hdr,
			      struct ib_send_wr *wr)
{
	int status;
	struct ocrdma_sge *ext_rw = (struct ocrdma_sge *)(hdr + 1);
	struct ocrdma_sge *sge = ext_rw + 1;
	u32 wqe_size = sizeof(*hdr) + sizeof(*ext_rw);

	status = ocrdma_build_inline_sges(qp, hdr, sge, wr, wqe_size);
	if (status)
		return status;
	ext_rw->addr_lo = wr->wr.rdma.remote_addr;
	ext_rw->addr_hi = upper_32_bits(wr->wr.rdma.remote_addr);
	ext_rw->lrkey = wr->wr.rdma.rkey;
	ext_rw->len = hdr->total_len;
	return 0;
}

static void ocrdma_build_read(struct ocrdma_qp *qp, struct ocrdma_hdr_wqe *hdr,
			      struct ib_send_wr *wr)
{
	struct ocrdma_sge *ext_rw = (struct ocrdma_sge *)(hdr + 1);
	struct ocrdma_sge *sge = ext_rw + 1;
	u32 wqe_size = ((wr->num_sge + 1) * sizeof(struct ocrdma_sge)) +
	    sizeof(struct ocrdma_hdr_wqe);

	ocrdma_build_sges(hdr, sge, wr->num_sge, wr->sg_list);
	hdr->cw |= ((wqe_size / OCRDMA_WQE_STRIDE) << OCRDMA_WQE_SIZE_SHIFT);
	hdr->cw |= (OCRDMA_READ << OCRDMA_WQE_OPCODE_SHIFT);
	hdr->cw |= (OCRDMA_TYPE_LKEY << OCRDMA_WQE_TYPE_SHIFT);

	ext_rw->addr_lo = wr->wr.rdma.remote_addr;
	ext_rw->addr_hi = upper_32_bits(wr->wr.rdma.remote_addr);
	ext_rw->lrkey = wr->wr.rdma.rkey;
	ext_rw->len = hdr->total_len;
}

static void ocrdma_ring_sq_db(struct ocrdma_qp *qp)
{
	u32 val = qp->sq.dbid | (1 << 16);

	iowrite32(val, qp->sq_db);
}

int ocrdma_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		     struct ib_send_wr **bad_wr)
{
	int status = 0;
	struct ocrdma_qp *qp = get_ocrdma_qp(ibqp);
	struct ocrdma_hdr_wqe *hdr;
	unsigned long flags;

	spin_lock_irqsave(&qp->q_lock, flags);
	if (qp->state != OCRDMA_QPS_RTS && qp->state != OCRDMA_QPS_SQD) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		return -EINVAL;
	}

	while (wr) {
		if (ocrdma_hwq_free_cnt(&qp->sq) == 0 ||
		    wr->num_sge > qp->sq.max_sges) {
			status = -ENOMEM;
			break;
		}
		hdr = ocrdma_hwq_head(&qp->sq);
		hdr->cw = 0;
		if (wr->send_flags & IB_SEND_SIGNALED)
			hdr->cw |= (OCRDMA_FLAG_SIG << OCRDMA_WQE_FLAGS_SHIFT);
		if (wr->send_flags & IB_SEND_FENCE)
			hdr->cw |=
			    (OCRDMA_FLAG_FENCE_L << OCRDMA_WQE_FLAGS_SHIFT);
		if (wr->send_flags & IB_SEND_SOLICITED)
			hdr->cw |=
			    (OCRDMA_FLAG_SOLICIT << OCRDMA_WQE_FLAGS_SHIFT);
		hdr->total_len = 0;
		switch (wr->opcode) {
		case IB_WR_SEND_WITH_IMM:
			hdr->cw |= (OCRDMA_FLAG_IMM << OCRDMA_WQE_FLAGS_SHIFT);
			hdr->immdt = ntohl(wr->ex.imm_data);
		case IB_WR_SEND:
			hdr->cw |= (OCRDMA_SEND << OCRDMA_WQE_OPCODE_SHIFT);
			ocrdma_build_send(qp, hdr, wr);
			break;
		case IB_WR_SEND_WITH_INV:
			hdr->cw |= (OCRDMA_FLAG_INV << OCRDMA_WQE_FLAGS_SHIFT);
			hdr->cw |= (OCRDMA_SEND << OCRDMA_WQE_OPCODE_SHIFT);
			hdr->lkey = wr->ex.invalidate_rkey;
			status = ocrdma_build_send(qp, hdr, wr);
			break;
		case IB_WR_RDMA_WRITE_WITH_IMM:
			hdr->cw |= (OCRDMA_FLAG_IMM << OCRDMA_WQE_FLAGS_SHIFT);
			hdr->immdt = ntohl(wr->ex.imm_data);
		case IB_WR_RDMA_WRITE:
			hdr->cw |= (OCRDMA_WRITE << OCRDMA_WQE_OPCODE_SHIFT);
			status = ocrdma_build_write(qp, hdr, wr);
			break;
		case IB_WR_RDMA_READ_WITH_INV:
			hdr->cw |= (OCRDMA_FLAG_INV << OCRDMA_WQE_FLAGS_SHIFT);
		case IB_WR_RDMA_READ:
			ocrdma_build_read(qp, hdr, wr);
			break;
		case IB_WR_LOCAL_INV:
			hdr->cw |=
			    (OCRDMA_LKEY_INV << OCRDMA_WQE_OPCODE_SHIFT);
			hdr->cw |= (sizeof(struct ocrdma_hdr_wqe) /
				OCRDMA_WQE_STRIDE) << OCRDMA_WQE_SIZE_SHIFT;
			hdr->lkey = wr->ex.invalidate_rkey;
			break;
		default:
			status = -EINVAL;
			break;
		}
		if (status) {
			*bad_wr = wr;
			break;
		}
		if (wr->send_flags & IB_SEND_SIGNALED)
			qp->wqe_wr_id_tbl[qp->sq.head].signaled = 1;
		else
			qp->wqe_wr_id_tbl[qp->sq.head].signaled = 0;
		qp->wqe_wr_id_tbl[qp->sq.head].wrid = wr->wr_id;
		ocrdma_cpu_to_le32(hdr, ((hdr->cw >> OCRDMA_WQE_SIZE_SHIFT) &
				   OCRDMA_WQE_SIZE_MASK) * OCRDMA_WQE_STRIDE);
		/* make sure wqe is written before adapter can access it */
		wmb();
		/* inform hw to start processing it */
		ocrdma_ring_sq_db(qp);

		/* update pointer, counter for next wr */
		ocrdma_hwq_inc_head(&qp->sq);
		wr = wr->next;
	}
	spin_unlock_irqrestore(&qp->q_lock, flags);
	return status;
}

static void ocrdma_ring_rq_db(struct ocrdma_qp *qp)
{
	u32 val = qp->rq.dbid | (1 << OCRDMA_GET_NUM_POSTED_SHIFT_VAL(qp));

	iowrite32(val, qp->rq_db);
}

static void ocrdma_build_rqe(struct ocrdma_hdr_wqe *rqe, struct ib_recv_wr *wr,
			     u16 tag)
{
	u32 wqe_size = 0;
	struct ocrdma_sge *sge;
	if (wr->num_sge)
		wqe_size = (wr->num_sge * sizeof(*sge)) + sizeof(*rqe);
	else
		wqe_size = sizeof(*sge) + sizeof(*rqe);

	rqe->cw = ((wqe_size / OCRDMA_WQE_STRIDE) <<
				OCRDMA_WQE_SIZE_SHIFT);
	rqe->cw |= (OCRDMA_FLAG_SIG << OCRDMA_WQE_FLAGS_SHIFT);
	rqe->cw |= (OCRDMA_TYPE_LKEY << OCRDMA_WQE_TYPE_SHIFT);
	rqe->total_len = 0;
	rqe->rsvd_tag = tag;
	sge = (struct ocrdma_sge *)(rqe + 1);
	ocrdma_build_sges(rqe, sge, wr->num_sge, wr->sg_list);
	ocrdma_cpu_to_le32(rqe, wqe_size);
}

int ocrdma_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		     struct ib_recv_wr **bad_wr)
{
	int status = 0;
	unsigned long flags;
	struct ocrdma_qp *qp = get_ocrdma_qp(ibqp);
	struct ocrdma_hdr_wqe *rqe;

	spin_lock_irqsave(&qp->q_lock, flags);
	if (qp->state == OCRDMA_QPS_RST || qp->state == OCRDMA_QPS_ERR) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		*bad_wr = wr;
		return -EINVAL;
	}
	while (wr) {
		if (ocrdma_hwq_free_cnt(&qp->rq) == 0 ||
		    wr->num_sge > qp->rq.max_sges) {
			*bad_wr = wr;
			status = -ENOMEM;
			break;
		}
		rqe = ocrdma_hwq_head(&qp->rq);
		ocrdma_build_rqe(rqe, wr, 0);

		qp->rqe_wr_id_tbl[qp->rq.head] = wr->wr_id;
		/* make sure rqe is written before adapter can access it */
		wmb();

		/* inform hw to start processing it */
		ocrdma_ring_rq_db(qp);

		/* update pointer, counter for next wr */
		ocrdma_hwq_inc_head(&qp->rq);
		wr = wr->next;
	}
	spin_unlock_irqrestore(&qp->q_lock, flags);
	return status;
}

/* cqe for srq's rqe can potentially arrive out of order.
 * index gives the entry in the shadow table where to store
 * the wr_id. tag/index is returned in cqe to reference back
 * for a given rqe.
 */
static int ocrdma_srq_get_idx(struct ocrdma_srq *srq)
{
	int row = 0;
	int indx = 0;

	for (row = 0; row < srq->bit_fields_len; row++) {
		if (srq->idx_bit_fields[row]) {
			indx = ffs(srq->idx_bit_fields[row]);
			indx = (row * 32) + (indx - 1);
			if (indx >= srq->rq.max_cnt)
				BUG();
			ocrdma_srq_toggle_bit(srq, indx);
			break;
		}
	}

	if (row == srq->bit_fields_len)
		BUG();
	return indx;
}

static void ocrdma_ring_srq_db(struct ocrdma_srq *srq)
{
	u32 val = srq->rq.dbid | (1 << 16);

	iowrite32(val, srq->db + OCRDMA_DB_GEN2_SRQ_OFFSET);
}

int ocrdma_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			 struct ib_recv_wr **bad_wr)
{
	int status = 0;
	unsigned long flags;
	struct ocrdma_srq *srq;
	struct ocrdma_hdr_wqe *rqe;
	u16 tag;

	srq = get_ocrdma_srq(ibsrq);

	spin_lock_irqsave(&srq->q_lock, flags);
	while (wr) {
		if (ocrdma_hwq_free_cnt(&srq->rq) == 0 ||
		    wr->num_sge > srq->rq.max_sges) {
			status = -ENOMEM;
			*bad_wr = wr;
			break;
		}
		tag = ocrdma_srq_get_idx(srq);
		rqe = ocrdma_hwq_head(&srq->rq);
		ocrdma_build_rqe(rqe, wr, tag);

		srq->rqe_wr_id_tbl[tag] = wr->wr_id;
		/* make sure rqe is written before adapter can perform DMA */
		wmb();
		/* inform hw to start processing it */
		ocrdma_ring_srq_db(srq);
		/* update pointer, counter for next wr */
		ocrdma_hwq_inc_head(&srq->rq);
		wr = wr->next;
	}
	spin_unlock_irqrestore(&srq->q_lock, flags);
	return status;
}

static enum ib_wc_status ocrdma_to_ibwc_err(u16 status)
{
	enum ib_wc_status ibwc_status = IB_WC_GENERAL_ERR;

	switch (status) {
	case OCRDMA_CQE_GENERAL_ERR:
		ibwc_status = IB_WC_GENERAL_ERR;
		break;
	case OCRDMA_CQE_LOC_LEN_ERR:
		ibwc_status = IB_WC_LOC_LEN_ERR;
		break;
	case OCRDMA_CQE_LOC_QP_OP_ERR:
		ibwc_status = IB_WC_LOC_QP_OP_ERR;
		break;
	case OCRDMA_CQE_LOC_EEC_OP_ERR:
		ibwc_status = IB_WC_LOC_EEC_OP_ERR;
		break;
	case OCRDMA_CQE_LOC_PROT_ERR:
		ibwc_status = IB_WC_LOC_PROT_ERR;
		break;
	case OCRDMA_CQE_WR_FLUSH_ERR:
		ibwc_status = IB_WC_WR_FLUSH_ERR;
		break;
	case OCRDMA_CQE_MW_BIND_ERR:
		ibwc_status = IB_WC_MW_BIND_ERR;
		break;
	case OCRDMA_CQE_BAD_RESP_ERR:
		ibwc_status = IB_WC_BAD_RESP_ERR;
		break;
	case OCRDMA_CQE_LOC_ACCESS_ERR:
		ibwc_status = IB_WC_LOC_ACCESS_ERR;
		break;
	case OCRDMA_CQE_REM_INV_REQ_ERR:
		ibwc_status = IB_WC_REM_INV_REQ_ERR;
		break;
	case OCRDMA_CQE_REM_ACCESS_ERR:
		ibwc_status = IB_WC_REM_ACCESS_ERR;
		break;
	case OCRDMA_CQE_REM_OP_ERR:
		ibwc_status = IB_WC_REM_OP_ERR;
		break;
	case OCRDMA_CQE_RETRY_EXC_ERR:
		ibwc_status = IB_WC_RETRY_EXC_ERR;
		break;
	case OCRDMA_CQE_RNR_RETRY_EXC_ERR:
		ibwc_status = IB_WC_RNR_RETRY_EXC_ERR;
		break;
	case OCRDMA_CQE_LOC_RDD_VIOL_ERR:
		ibwc_status = IB_WC_LOC_RDD_VIOL_ERR;
		break;
	case OCRDMA_CQE_REM_INV_RD_REQ_ERR:
		ibwc_status = IB_WC_REM_INV_RD_REQ_ERR;
		break;
	case OCRDMA_CQE_REM_ABORT_ERR:
		ibwc_status = IB_WC_REM_ABORT_ERR;
		break;
	case OCRDMA_CQE_INV_EECN_ERR:
		ibwc_status = IB_WC_INV_EECN_ERR;
		break;
	case OCRDMA_CQE_INV_EEC_STATE_ERR:
		ibwc_status = IB_WC_INV_EEC_STATE_ERR;
		break;
	case OCRDMA_CQE_FATAL_ERR:
		ibwc_status = IB_WC_FATAL_ERR;
		break;
	case OCRDMA_CQE_RESP_TIMEOUT_ERR:
		ibwc_status = IB_WC_RESP_TIMEOUT_ERR;
		break;
	default:
		ibwc_status = IB_WC_GENERAL_ERR;
		break;
	};
	return ibwc_status;
}

static void ocrdma_update_wc(struct ocrdma_qp *qp, struct ib_wc *ibwc,
		      u32 wqe_idx)
{
	struct ocrdma_hdr_wqe *hdr;
	struct ocrdma_sge *rw;
	int opcode;

	hdr = ocrdma_hwq_head_from_idx(&qp->sq, wqe_idx);

	ibwc->wr_id = qp->wqe_wr_id_tbl[wqe_idx].wrid;
	/* Undo the hdr->cw swap */
	opcode = le32_to_cpu(hdr->cw) & OCRDMA_WQE_OPCODE_MASK;
	switch (opcode) {
	case OCRDMA_WRITE:
		ibwc->opcode = IB_WC_RDMA_WRITE;
		break;
	case OCRDMA_READ:
		rw = (struct ocrdma_sge *)(hdr + 1);
		ibwc->opcode = IB_WC_RDMA_READ;
		ibwc->byte_len = rw->len;
		break;
	case OCRDMA_SEND:
		ibwc->opcode = IB_WC_SEND;
		break;
	case OCRDMA_LKEY_INV:
		ibwc->opcode = IB_WC_LOCAL_INV;
		break;
	default:
		ibwc->status = IB_WC_GENERAL_ERR;
		ocrdma_err("%s() invalid opcode received = 0x%x\n",
			   __func__, hdr->cw & OCRDMA_WQE_OPCODE_MASK);
		break;
	};
}

static void ocrdma_set_cqe_status_flushed(struct ocrdma_qp *qp,
						struct ocrdma_cqe *cqe)
{
	if (is_cqe_for_sq(cqe)) {
		cqe->flags_status_srcqpn = cpu_to_le32(le32_to_cpu(
				cqe->flags_status_srcqpn) &
					~OCRDMA_CQE_STATUS_MASK);
		cqe->flags_status_srcqpn = cpu_to_le32(le32_to_cpu(
				cqe->flags_status_srcqpn) |
				(OCRDMA_CQE_WR_FLUSH_ERR <<
					OCRDMA_CQE_STATUS_SHIFT));
	} else {
		if (qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_GSI) {
			cqe->flags_status_srcqpn = cpu_to_le32(le32_to_cpu(
					cqe->flags_status_srcqpn) &
						~OCRDMA_CQE_UD_STATUS_MASK);
			cqe->flags_status_srcqpn = cpu_to_le32(le32_to_cpu(
					cqe->flags_status_srcqpn) |
					(OCRDMA_CQE_WR_FLUSH_ERR <<
						OCRDMA_CQE_UD_STATUS_SHIFT));
		} else {
			cqe->flags_status_srcqpn = cpu_to_le32(le32_to_cpu(
					cqe->flags_status_srcqpn) &
						~OCRDMA_CQE_STATUS_MASK);
			cqe->flags_status_srcqpn = cpu_to_le32(le32_to_cpu(
					cqe->flags_status_srcqpn) |
					(OCRDMA_CQE_WR_FLUSH_ERR <<
						OCRDMA_CQE_STATUS_SHIFT));
		}
	}
}

static bool ocrdma_update_err_cqe(struct ib_wc *ibwc, struct ocrdma_cqe *cqe,
				  struct ocrdma_qp *qp, int status)
{
	bool expand = false;

	ibwc->byte_len = 0;
	ibwc->qp = &qp->ibqp;
	ibwc->status = ocrdma_to_ibwc_err(status);

	ocrdma_flush_qp(qp);
	ocrdma_qp_state_machine(qp, IB_QPS_ERR, NULL);

	/* if wqe/rqe pending for which cqe needs to be returned,
	 * trigger inflating it.
	 */
	if (!is_hw_rq_empty(qp) || !is_hw_sq_empty(qp)) {
		expand = true;
		ocrdma_set_cqe_status_flushed(qp, cqe);
	}
	return expand;
}

static int ocrdma_update_err_rcqe(struct ib_wc *ibwc, struct ocrdma_cqe *cqe,
				  struct ocrdma_qp *qp, int status)
{
	ibwc->opcode = IB_WC_RECV;
	ibwc->wr_id = qp->rqe_wr_id_tbl[qp->rq.tail];
	ocrdma_hwq_inc_tail(&qp->rq);

	return ocrdma_update_err_cqe(ibwc, cqe, qp, status);
}

static int ocrdma_update_err_scqe(struct ib_wc *ibwc, struct ocrdma_cqe *cqe,
				  struct ocrdma_qp *qp, int status)
{
	ocrdma_update_wc(qp, ibwc, qp->sq.tail);
	ocrdma_hwq_inc_tail(&qp->sq);

	return ocrdma_update_err_cqe(ibwc, cqe, qp, status);
}


static bool ocrdma_poll_err_scqe(struct ocrdma_qp *qp,
				 struct ocrdma_cqe *cqe, struct ib_wc *ibwc,
				 bool *polled, bool *stop)
{
	bool expand;
	int status = (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_STATUS_MASK) >> OCRDMA_CQE_STATUS_SHIFT;

	/* when hw sq is empty, but rq is not empty, so we continue
	 * to keep the cqe in order to get the cq event again.
	 */
	if (is_hw_sq_empty(qp) && !is_hw_rq_empty(qp)) {
		/* when cq for rq and sq is same, it is safe to return
		 * flush cqe for RQEs.
		 */
		if (!qp->srq && (qp->sq_cq == qp->rq_cq)) {
			*polled = true;
			status = OCRDMA_CQE_WR_FLUSH_ERR;
			expand = ocrdma_update_err_rcqe(ibwc, cqe, qp, status);
		} else {
			/* stop processing further cqe as this cqe is used for
			 * triggering cq event on buddy cq of RQ.
			 * When QP is destroyed, this cqe will be removed
			 * from the cq's hardware q.
			 */
			*polled = false;
			*stop = true;
			expand = false;
		}
	} else {
		*polled = true;
		expand = ocrdma_update_err_scqe(ibwc, cqe, qp, status);
	}
	return expand;
}

static bool ocrdma_poll_success_scqe(struct ocrdma_qp *qp,
				     struct ocrdma_cqe *cqe,
				     struct ib_wc *ibwc, bool *polled)
{
	bool expand = false;
	int tail = qp->sq.tail;
	u32 wqe_idx;

	if (!qp->wqe_wr_id_tbl[tail].signaled) {
		expand = true;	/* CQE cannot be consumed yet */
		*polled = false;    /* WC cannot be consumed yet */
	} else {
		ibwc->status = IB_WC_SUCCESS;
		ibwc->wc_flags = 0;
		ibwc->qp = &qp->ibqp;
		ocrdma_update_wc(qp, ibwc, tail);
		*polled = true;
		wqe_idx = le32_to_cpu(cqe->wq.wqeidx) &	OCRDMA_CQE_WQEIDX_MASK;
		if (tail != wqe_idx)
			expand = true; /* Coalesced CQE can't be consumed yet */
	}
	ocrdma_hwq_inc_tail(&qp->sq);
	return expand;
}

static bool ocrdma_poll_scqe(struct ocrdma_qp *qp, struct ocrdma_cqe *cqe,
			     struct ib_wc *ibwc, bool *polled, bool *stop)
{
	int status;
	bool expand;

	status = (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_STATUS_MASK) >> OCRDMA_CQE_STATUS_SHIFT;

	if (status == OCRDMA_CQE_SUCCESS)
		expand = ocrdma_poll_success_scqe(qp, cqe, ibwc, polled);
	else
		expand = ocrdma_poll_err_scqe(qp, cqe, ibwc, polled, stop);
	return expand;
}

static int ocrdma_update_ud_rcqe(struct ib_wc *ibwc, struct ocrdma_cqe *cqe)
{
	int status;

	status = (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_UD_STATUS_MASK) >> OCRDMA_CQE_UD_STATUS_SHIFT;
	ibwc->src_qp = le32_to_cpu(cqe->flags_status_srcqpn) &
						OCRDMA_CQE_SRCQP_MASK;
	ibwc->pkey_index = le32_to_cpu(cqe->ud.rxlen_pkey) &
						OCRDMA_CQE_PKEY_MASK;
	ibwc->wc_flags = IB_WC_GRH;
	ibwc->byte_len = (le32_to_cpu(cqe->ud.rxlen_pkey) >>
					OCRDMA_CQE_UD_XFER_LEN_SHIFT);
	return status;
}

static void ocrdma_update_free_srq_cqe(struct ib_wc *ibwc,
				       struct ocrdma_cqe *cqe,
				       struct ocrdma_qp *qp)
{
	unsigned long flags;
	struct ocrdma_srq *srq;
	u32 wqe_idx;

	srq = get_ocrdma_srq(qp->ibqp.srq);
	wqe_idx = le32_to_cpu(cqe->rq.buftag_qpn) >> OCRDMA_CQE_BUFTAG_SHIFT;
	ibwc->wr_id = srq->rqe_wr_id_tbl[wqe_idx];
	spin_lock_irqsave(&srq->q_lock, flags);
	ocrdma_srq_toggle_bit(srq, wqe_idx);
	spin_unlock_irqrestore(&srq->q_lock, flags);
	ocrdma_hwq_inc_tail(&srq->rq);
}

static bool ocrdma_poll_err_rcqe(struct ocrdma_qp *qp, struct ocrdma_cqe *cqe,
				struct ib_wc *ibwc, bool *polled, bool *stop,
				int status)
{
	bool expand;

	/* when hw_rq is empty, but wq is not empty, so continue
	 * to keep the cqe to get the cq event again.
	 */
	if (is_hw_rq_empty(qp) && !is_hw_sq_empty(qp)) {
		if (!qp->srq && (qp->sq_cq == qp->rq_cq)) {
			*polled = true;
			status = OCRDMA_CQE_WR_FLUSH_ERR;
			expand = ocrdma_update_err_scqe(ibwc, cqe, qp, status);
		} else {
			*polled = false;
			*stop = true;
			expand = false;
		}
	} else
		expand = ocrdma_update_err_rcqe(ibwc, cqe, qp, status);
	return expand;
}

static void ocrdma_poll_success_rcqe(struct ocrdma_qp *qp,
				     struct ocrdma_cqe *cqe, struct ib_wc *ibwc)
{
	ibwc->opcode = IB_WC_RECV;
	ibwc->qp = &qp->ibqp;
	ibwc->status = IB_WC_SUCCESS;

	if (qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_GSI)
		ocrdma_update_ud_rcqe(ibwc, cqe);
	else
		ibwc->byte_len = le32_to_cpu(cqe->rq.rxlen);

	if (is_cqe_imm(cqe)) {
		ibwc->ex.imm_data = htonl(le32_to_cpu(cqe->rq.lkey_immdt));
		ibwc->wc_flags |= IB_WC_WITH_IMM;
	} else if (is_cqe_wr_imm(cqe)) {
		ibwc->opcode = IB_WC_RECV_RDMA_WITH_IMM;
		ibwc->ex.imm_data = htonl(le32_to_cpu(cqe->rq.lkey_immdt));
		ibwc->wc_flags |= IB_WC_WITH_IMM;
	} else if (is_cqe_invalidated(cqe)) {
		ibwc->ex.invalidate_rkey = le32_to_cpu(cqe->rq.lkey_immdt);
		ibwc->wc_flags |= IB_WC_WITH_INVALIDATE;
	}
	if (qp->ibqp.srq)
		ocrdma_update_free_srq_cqe(ibwc, cqe, qp);
	else {
		ibwc->wr_id = qp->rqe_wr_id_tbl[qp->rq.tail];
		ocrdma_hwq_inc_tail(&qp->rq);
	}
}

static bool ocrdma_poll_rcqe(struct ocrdma_qp *qp, struct ocrdma_cqe *cqe,
			     struct ib_wc *ibwc, bool *polled, bool *stop)
{
	int status;
	bool expand = false;

	ibwc->wc_flags = 0;
	if (qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_GSI)
		status = (le32_to_cpu(cqe->flags_status_srcqpn) &
					OCRDMA_CQE_UD_STATUS_MASK) >>
					OCRDMA_CQE_UD_STATUS_SHIFT;
	else
		status = (le32_to_cpu(cqe->flags_status_srcqpn) &
			     OCRDMA_CQE_STATUS_MASK) >> OCRDMA_CQE_STATUS_SHIFT;

	if (status == OCRDMA_CQE_SUCCESS) {
		*polled = true;
		ocrdma_poll_success_rcqe(qp, cqe, ibwc);
	} else {
		expand = ocrdma_poll_err_rcqe(qp, cqe, ibwc, polled, stop,
					      status);
	}
	return expand;
}

static void ocrdma_change_cq_phase(struct ocrdma_cq *cq, struct ocrdma_cqe *cqe,
				   u16 cur_getp)
{
	if (cq->phase_change) {
		if (cur_getp == 0)
			cq->phase = (~cq->phase & OCRDMA_CQE_VALID);
	} else
		/* clear valid bit */
		cqe->flags_status_srcqpn = 0;
}

static int ocrdma_poll_hwcq(struct ocrdma_cq *cq, int num_entries,
			    struct ib_wc *ibwc)
{
	u16 qpn = 0;
	int i = 0;
	bool expand = false;
	int polled_hw_cqes = 0;
	struct ocrdma_qp *qp = NULL;
	struct ocrdma_dev *dev = cq->dev;
	struct ocrdma_cqe *cqe;
	u16 cur_getp; bool polled = false; bool stop = false;

	cur_getp = cq->getp;
	while (num_entries) {
		cqe = cq->va + cur_getp;
		/* check whether valid cqe or not */
		if (!is_cqe_valid(cq, cqe))
			break;
		qpn = (le32_to_cpu(cqe->cmn.qpn) & OCRDMA_CQE_QPN_MASK);
		/* ignore discarded cqe */
		if (qpn == 0)
			goto skip_cqe;
		qp = dev->qp_tbl[qpn];
		BUG_ON(qp == NULL);

		if (is_cqe_for_sq(cqe)) {
			expand = ocrdma_poll_scqe(qp, cqe, ibwc, &polled,
						  &stop);
		} else {
			expand = ocrdma_poll_rcqe(qp, cqe, ibwc, &polled,
						  &stop);
		}
		if (expand)
			goto expand_cqe;
		if (stop)
			goto stop_cqe;
		/* clear qpn to avoid duplicate processing by discard_cqe() */
		cqe->cmn.qpn = 0;
skip_cqe:
		polled_hw_cqes += 1;
		cur_getp = (cur_getp + 1) % cq->max_hw_cqe;
		ocrdma_change_cq_phase(cq, cqe, cur_getp);
expand_cqe:
		if (polled) {
			num_entries -= 1;
			i += 1;
			ibwc = ibwc + 1;
			polled = false;
		}
	}
stop_cqe:
	cq->getp = cur_getp;
	if (polled_hw_cqes || expand || stop) {
		ocrdma_ring_cq_db(dev, cq->id, cq->armed, cq->solicited,
				  polled_hw_cqes);
	}
	return i;
}

/* insert error cqe if the QP's SQ or RQ's CQ matches the CQ under poll. */
static int ocrdma_add_err_cqe(struct ocrdma_cq *cq, int num_entries,
			      struct ocrdma_qp *qp, struct ib_wc *ibwc)
{
	int err_cqes = 0;

	while (num_entries) {
		if (is_hw_sq_empty(qp) && is_hw_rq_empty(qp))
			break;
		if (!is_hw_sq_empty(qp) && qp->sq_cq == cq) {
			ocrdma_update_wc(qp, ibwc, qp->sq.tail);
			ocrdma_hwq_inc_tail(&qp->sq);
		} else if (!is_hw_rq_empty(qp) && qp->rq_cq == cq) {
			ibwc->wr_id = qp->rqe_wr_id_tbl[qp->rq.tail];
			ocrdma_hwq_inc_tail(&qp->rq);
		} else
			return err_cqes;
		ibwc->byte_len = 0;
		ibwc->status = IB_WC_WR_FLUSH_ERR;
		ibwc = ibwc + 1;
		err_cqes += 1;
		num_entries -= 1;
	}
	return err_cqes;
}

int ocrdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	int cqes_to_poll = num_entries;
	struct ocrdma_cq *cq = NULL;
	unsigned long flags;
	struct ocrdma_dev *dev;
	int num_os_cqe = 0, err_cqes = 0;
	struct ocrdma_qp *qp;

	cq = get_ocrdma_cq(ibcq);
	dev = cq->dev;

	/* poll cqes from adapter CQ */
	spin_lock_irqsave(&cq->cq_lock, flags);
	num_os_cqe = ocrdma_poll_hwcq(cq, cqes_to_poll, wc);
	spin_unlock_irqrestore(&cq->cq_lock, flags);
	cqes_to_poll -= num_os_cqe;

	if (cqes_to_poll) {
		wc = wc + num_os_cqe;
		/* adapter returns single error cqe when qp moves to
		 * error state. So insert error cqes with wc_status as
		 * FLUSHED for pending WQEs and RQEs of QP's SQ and RQ
		 * respectively which uses this CQ.
		 */
		spin_lock_irqsave(&dev->flush_q_lock, flags);
		list_for_each_entry(qp, &cq->sq_head, sq_entry) {
			if (cqes_to_poll == 0)
				break;
			err_cqes = ocrdma_add_err_cqe(cq, cqes_to_poll, qp, wc);
			cqes_to_poll -= err_cqes;
			num_os_cqe += err_cqes;
			wc = wc + err_cqes;
		}
		spin_unlock_irqrestore(&dev->flush_q_lock, flags);
	}
	return num_os_cqe;
}

int ocrdma_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags cq_flags)
{
	struct ocrdma_cq *cq;
	unsigned long flags;
	struct ocrdma_dev *dev;
	u16 cq_id;
	u16 cur_getp;
	struct ocrdma_cqe *cqe;

	cq = get_ocrdma_cq(ibcq);
	cq_id = cq->id;
	dev = cq->dev;

	spin_lock_irqsave(&cq->cq_lock, flags);
	if (cq_flags & IB_CQ_NEXT_COMP || cq_flags & IB_CQ_SOLICITED)
		cq->armed = true;
	if (cq_flags & IB_CQ_SOLICITED)
		cq->solicited = true;

	cur_getp = cq->getp;
	cqe = cq->va + cur_getp;

	/* check whether any valid cqe exist or not, if not then safe to
	 * arm. If cqe is not yet consumed, then let it get consumed and then
	 * we arm it to avoid false interrupts.
	 */
	if (!is_cqe_valid(cq, cqe) || cq->arm_needed) {
		cq->arm_needed = false;
		ocrdma_ring_cq_db(dev, cq_id, cq->armed, cq->solicited, 0);
	}
	spin_unlock_irqrestore(&cq->cq_lock, flags);
	return 0;
}
