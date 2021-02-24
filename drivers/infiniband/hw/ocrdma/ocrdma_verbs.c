/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#include <linux/dma-mapping.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>
#include <rdma/uverbs_ioctl.h>

#include "ocrdma.h"
#include "ocrdma_hw.h"
#include "ocrdma_verbs.h"
#include <rdma/ocrdma-abi.h>

int ocrdma_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	if (index > 0)
		return -EINVAL;

	*pkey = 0xffff;
	return 0;
}

int ocrdma_query_device(struct ib_device *ibdev, struct ib_device_attr *attr,
			struct ib_udata *uhw)
{
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;

	memset(attr, 0, sizeof *attr);
	memcpy(&attr->fw_ver, &dev->attr.fw_ver[0],
	       min(sizeof(dev->attr.fw_ver), sizeof(attr->fw_ver)));
	ocrdma_get_guid(dev, (u8 *)&attr->sys_image_guid);
	attr->max_mr_size = dev->attr.max_mr_size;
	attr->page_size_cap = 0xffff000;
	attr->vendor_id = dev->nic_info.pdev->vendor;
	attr->vendor_part_id = dev->nic_info.pdev->device;
	attr->hw_ver = dev->asic_id;
	attr->max_qp = dev->attr.max_qp;
	attr->max_ah = OCRDMA_MAX_AH;
	attr->max_qp_wr = dev->attr.max_wqe;

	attr->device_cap_flags = IB_DEVICE_CURR_QP_STATE_MOD |
					IB_DEVICE_RC_RNR_NAK_GEN |
					IB_DEVICE_SHUTDOWN_PORT |
					IB_DEVICE_SYS_IMAGE_GUID |
					IB_DEVICE_LOCAL_DMA_LKEY |
					IB_DEVICE_MEM_MGT_EXTENSIONS;
	attr->max_send_sge = dev->attr.max_send_sge;
	attr->max_recv_sge = dev->attr.max_recv_sge;
	attr->max_sge_rd = dev->attr.max_rdma_sge;
	attr->max_cq = dev->attr.max_cq;
	attr->max_cqe = dev->attr.max_cqe;
	attr->max_mr = dev->attr.max_mr;
	attr->max_mw = dev->attr.max_mw;
	attr->max_pd = dev->attr.max_pd;
	attr->atomic_cap = 0;
	attr->max_qp_rd_atom =
	    min(dev->attr.max_ord_per_qp, dev->attr.max_ird_per_qp);
	attr->max_qp_init_rd_atom = dev->attr.max_ord_per_qp;
	attr->max_srq = dev->attr.max_srq;
	attr->max_srq_sge = dev->attr.max_srq_sge;
	attr->max_srq_wr = dev->attr.max_rqe;
	attr->local_ca_ack_delay = dev->attr.local_ca_ack_delay;
	attr->max_fast_reg_page_list_len = dev->attr.max_pages_per_frmr;
	attr->max_pkeys = 1;
	return 0;
}

static inline void get_link_speed_and_width(struct ocrdma_dev *dev,
					    u16 *ib_speed, u8 *ib_width)
{
	int status;
	u8 speed;

	status = ocrdma_mbx_get_link_speed(dev, &speed, NULL);
	if (status)
		speed = OCRDMA_PHYS_LINK_SPEED_ZERO;

	switch (speed) {
	case OCRDMA_PHYS_LINK_SPEED_1GBPS:
		*ib_speed = IB_SPEED_SDR;
		*ib_width = IB_WIDTH_1X;
		break;

	case OCRDMA_PHYS_LINK_SPEED_10GBPS:
		*ib_speed = IB_SPEED_QDR;
		*ib_width = IB_WIDTH_1X;
		break;

	case OCRDMA_PHYS_LINK_SPEED_20GBPS:
		*ib_speed = IB_SPEED_DDR;
		*ib_width = IB_WIDTH_4X;
		break;

	case OCRDMA_PHYS_LINK_SPEED_40GBPS:
		*ib_speed = IB_SPEED_QDR;
		*ib_width = IB_WIDTH_4X;
		break;

	default:
		/* Unsupported */
		*ib_speed = IB_SPEED_SDR;
		*ib_width = IB_WIDTH_1X;
	}
}

int ocrdma_query_port(struct ib_device *ibdev,
		      u8 port, struct ib_port_attr *props)
{
	enum ib_port_state port_state;
	struct ocrdma_dev *dev;
	struct net_device *netdev;

	/* props being zeroed by the caller, avoid zeroing it here */
	dev = get_ocrdma_dev(ibdev);
	netdev = dev->nic_info.netdev;
	if (netif_running(netdev) && netif_oper_up(netdev)) {
		port_state = IB_PORT_ACTIVE;
		props->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	} else {
		port_state = IB_PORT_DOWN;
		props->phys_state = IB_PORT_PHYS_STATE_DISABLED;
	}
	props->max_mtu = IB_MTU_4096;
	props->active_mtu = iboe_get_mtu(netdev->mtu);
	props->lid = 0;
	props->lmc = 0;
	props->sm_lid = 0;
	props->sm_sl = 0;
	props->state = port_state;
	props->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_REINIT_SUP |
				IB_PORT_DEVICE_MGMT_SUP |
				IB_PORT_VENDOR_CLASS_SUP;
	props->ip_gids = true;
	props->gid_tbl_len = OCRDMA_MAX_SGID;
	props->pkey_tbl_len = 1;
	props->bad_pkey_cntr = 0;
	props->qkey_viol_cntr = 0;
	get_link_speed_and_width(dev, &props->active_speed,
				 &props->active_width);
	props->max_msg_sz = 0x80000000;
	props->max_vl_num = 4;
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
		if (len != mm->key.len && phy_addr != mm->key.phy_addr)
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
		if (len != mm->key.len && phy_addr != mm->key.phy_addr)
			continue;

		found = true;
		break;
	}
	mutex_unlock(&uctx->mm_list_lock);
	return found;
}


static u16 _ocrdma_pd_mgr_get_bitmap(struct ocrdma_dev *dev, bool dpp_pool)
{
	u16 pd_bitmap_idx = 0;
	const unsigned long *pd_bitmap;

	if (dpp_pool) {
		pd_bitmap = dev->pd_mgr->pd_dpp_bitmap;
		pd_bitmap_idx = find_first_zero_bit(pd_bitmap,
						    dev->pd_mgr->max_dpp_pd);
		__set_bit(pd_bitmap_idx, dev->pd_mgr->pd_dpp_bitmap);
		dev->pd_mgr->pd_dpp_count++;
		if (dev->pd_mgr->pd_dpp_count > dev->pd_mgr->pd_dpp_thrsh)
			dev->pd_mgr->pd_dpp_thrsh = dev->pd_mgr->pd_dpp_count;
	} else {
		pd_bitmap = dev->pd_mgr->pd_norm_bitmap;
		pd_bitmap_idx = find_first_zero_bit(pd_bitmap,
						    dev->pd_mgr->max_normal_pd);
		__set_bit(pd_bitmap_idx, dev->pd_mgr->pd_norm_bitmap);
		dev->pd_mgr->pd_norm_count++;
		if (dev->pd_mgr->pd_norm_count > dev->pd_mgr->pd_norm_thrsh)
			dev->pd_mgr->pd_norm_thrsh = dev->pd_mgr->pd_norm_count;
	}
	return pd_bitmap_idx;
}

static int _ocrdma_pd_mgr_put_bitmap(struct ocrdma_dev *dev, u16 pd_id,
					bool dpp_pool)
{
	u16 pd_count;
	u16 pd_bit_index;

	pd_count = dpp_pool ? dev->pd_mgr->pd_dpp_count :
			      dev->pd_mgr->pd_norm_count;
	if (pd_count == 0)
		return -EINVAL;

	if (dpp_pool) {
		pd_bit_index = pd_id - dev->pd_mgr->pd_dpp_start;
		if (pd_bit_index >= dev->pd_mgr->max_dpp_pd) {
			return -EINVAL;
		} else {
			__clear_bit(pd_bit_index, dev->pd_mgr->pd_dpp_bitmap);
			dev->pd_mgr->pd_dpp_count--;
		}
	} else {
		pd_bit_index = pd_id - dev->pd_mgr->pd_norm_start;
		if (pd_bit_index >= dev->pd_mgr->max_normal_pd) {
			return -EINVAL;
		} else {
			__clear_bit(pd_bit_index, dev->pd_mgr->pd_norm_bitmap);
			dev->pd_mgr->pd_norm_count--;
		}
	}

	return 0;
}

static int ocrdma_put_pd_num(struct ocrdma_dev *dev, u16 pd_id,
				   bool dpp_pool)
{
	int status;

	mutex_lock(&dev->dev_lock);
	status = _ocrdma_pd_mgr_put_bitmap(dev, pd_id, dpp_pool);
	mutex_unlock(&dev->dev_lock);
	return status;
}

static int ocrdma_get_pd_num(struct ocrdma_dev *dev, struct ocrdma_pd *pd)
{
	u16 pd_idx = 0;
	int status = 0;

	mutex_lock(&dev->dev_lock);
	if (pd->dpp_enabled) {
		/* try allocating DPP PD, if not available then normal PD */
		if (dev->pd_mgr->pd_dpp_count < dev->pd_mgr->max_dpp_pd) {
			pd_idx = _ocrdma_pd_mgr_get_bitmap(dev, true);
			pd->id = dev->pd_mgr->pd_dpp_start + pd_idx;
			pd->dpp_page = dev->pd_mgr->dpp_page_index + pd_idx;
		} else if (dev->pd_mgr->pd_norm_count <
			   dev->pd_mgr->max_normal_pd) {
			pd_idx = _ocrdma_pd_mgr_get_bitmap(dev, false);
			pd->id = dev->pd_mgr->pd_norm_start + pd_idx;
			pd->dpp_enabled = false;
		} else {
			status = -EINVAL;
		}
	} else {
		if (dev->pd_mgr->pd_norm_count < dev->pd_mgr->max_normal_pd) {
			pd_idx = _ocrdma_pd_mgr_get_bitmap(dev, false);
			pd->id = dev->pd_mgr->pd_norm_start + pd_idx;
		} else {
			status = -EINVAL;
		}
	}
	mutex_unlock(&dev->dev_lock);
	return status;
}

/*
 * NOTE:
 *
 * ocrdma_ucontext must be used here because this function is also
 * called from ocrdma_alloc_ucontext where ib_udata does not have
 * valid ib_ucontext pointer. ib_uverbs_get_context does not call
 * uobj_{alloc|get_xxx} helpers which are used to store the
 * ib_ucontext in uverbs_attr_bundle wrapping the ib_udata. so
 * ib_udata does NOT imply valid ib_ucontext here!
 */
static int _ocrdma_alloc_pd(struct ocrdma_dev *dev, struct ocrdma_pd *pd,
			    struct ocrdma_ucontext *uctx,
			    struct ib_udata *udata)
{
	int status;

	if (udata && uctx && dev->attr.max_dpp_pds) {
		pd->dpp_enabled =
			ocrdma_get_asic_type(dev) == OCRDMA_ASIC_GEN_SKH_R;
		pd->num_dpp_qp =
			pd->dpp_enabled ? (dev->nic_info.db_page_size /
					   dev->attr.wqe_size) : 0;
	}

	if (dev->pd_mgr->pd_prealloc_valid)
		return ocrdma_get_pd_num(dev, pd);

retry:
	status = ocrdma_mbx_alloc_pd(dev, pd);
	if (status) {
		if (pd->dpp_enabled) {
			pd->dpp_enabled = false;
			pd->num_dpp_qp = 0;
			goto retry;
		}
		return status;
	}

	return 0;
}

static inline int is_ucontext_pd(struct ocrdma_ucontext *uctx,
				 struct ocrdma_pd *pd)
{
	return (uctx->cntxt_pd == pd);
}

static void _ocrdma_dealloc_pd(struct ocrdma_dev *dev,
			      struct ocrdma_pd *pd)
{
	if (dev->pd_mgr->pd_prealloc_valid)
		ocrdma_put_pd_num(dev, pd->id, pd->dpp_enabled);
	else
		ocrdma_mbx_dealloc_pd(dev, pd);
}

static int ocrdma_alloc_ucontext_pd(struct ocrdma_dev *dev,
				    struct ocrdma_ucontext *uctx,
				    struct ib_udata *udata)
{
	struct ib_device *ibdev = &dev->ibdev;
	struct ib_pd *pd;
	int status;

	pd = rdma_zalloc_drv_obj(ibdev, ib_pd);
	if (!pd)
		return -ENOMEM;

	pd->device  = ibdev;
	uctx->cntxt_pd = get_ocrdma_pd(pd);

	status = _ocrdma_alloc_pd(dev, uctx->cntxt_pd, uctx, udata);
	if (status) {
		kfree(uctx->cntxt_pd);
		goto err;
	}

	uctx->cntxt_pd->uctx = uctx;
	uctx->cntxt_pd->ibpd.device = &dev->ibdev;
err:
	return status;
}

static void ocrdma_dealloc_ucontext_pd(struct ocrdma_ucontext *uctx)
{
	struct ocrdma_pd *pd = uctx->cntxt_pd;
	struct ocrdma_dev *dev = get_ocrdma_dev(pd->ibpd.device);

	if (uctx->pd_in_use) {
		pr_err("%s(%d) Freeing in use pdid=0x%x.\n",
		       __func__, dev->id, pd->id);
	}
	uctx->cntxt_pd = NULL;
	_ocrdma_dealloc_pd(dev, pd);
	kfree(pd);
}

static struct ocrdma_pd *ocrdma_get_ucontext_pd(struct ocrdma_ucontext *uctx)
{
	struct ocrdma_pd *pd = NULL;

	mutex_lock(&uctx->mm_list_lock);
	if (!uctx->pd_in_use) {
		uctx->pd_in_use = true;
		pd = uctx->cntxt_pd;
	}
	mutex_unlock(&uctx->mm_list_lock);

	return pd;
}

static void ocrdma_release_ucontext_pd(struct ocrdma_ucontext *uctx)
{
	mutex_lock(&uctx->mm_list_lock);
	uctx->pd_in_use = false;
	mutex_unlock(&uctx->mm_list_lock);
}

int ocrdma_alloc_ucontext(struct ib_ucontext *uctx, struct ib_udata *udata)
{
	struct ib_device *ibdev = uctx->device;
	int status;
	struct ocrdma_ucontext *ctx = get_ocrdma_ucontext(uctx);
	struct ocrdma_alloc_ucontext_resp resp = {};
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);
	struct pci_dev *pdev = dev->nic_info.pdev;
	u32 map_len = roundup(sizeof(u32) * 2048, PAGE_SIZE);

	if (!udata)
		return -EFAULT;
	INIT_LIST_HEAD(&ctx->mm_head);
	mutex_init(&ctx->mm_list_lock);

	ctx->ah_tbl.va = dma_alloc_coherent(&pdev->dev, map_len,
					    &ctx->ah_tbl.pa, GFP_KERNEL);
	if (!ctx->ah_tbl.va)
		return -ENOMEM;

	ctx->ah_tbl.len = map_len;

	resp.ah_tbl_len = ctx->ah_tbl.len;
	resp.ah_tbl_page = virt_to_phys(ctx->ah_tbl.va);

	status = ocrdma_add_mmap(ctx, resp.ah_tbl_page, resp.ah_tbl_len);
	if (status)
		goto map_err;

	status = ocrdma_alloc_ucontext_pd(dev, ctx, udata);
	if (status)
		goto pd_err;

	resp.dev_id = dev->id;
	resp.max_inline_data = dev->attr.max_inline_data;
	resp.wqe_size = dev->attr.wqe_size;
	resp.rqe_size = dev->attr.rqe_size;
	resp.dpp_wqe_size = dev->attr.wqe_size;

	memcpy(resp.fw_ver, dev->attr.fw_ver, sizeof(resp.fw_ver));
	status = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (status)
		goto cpy_err;
	return 0;

cpy_err:
	ocrdma_dealloc_ucontext_pd(ctx);
pd_err:
	ocrdma_del_mmap(ctx, ctx->ah_tbl.pa, ctx->ah_tbl.len);
map_err:
	dma_free_coherent(&pdev->dev, ctx->ah_tbl.len, ctx->ah_tbl.va,
			  ctx->ah_tbl.pa);
	return status;
}

void ocrdma_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct ocrdma_mm *mm, *tmp;
	struct ocrdma_ucontext *uctx = get_ocrdma_ucontext(ibctx);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibctx->device);
	struct pci_dev *pdev = dev->nic_info.pdev;

	ocrdma_dealloc_ucontext_pd(uctx);

	ocrdma_del_mmap(uctx, uctx->ah_tbl.pa, uctx->ah_tbl.len);
	dma_free_coherent(&pdev->dev, uctx->ah_tbl.len, uctx->ah_tbl.va,
			  uctx->ah_tbl.pa);

	list_for_each_entry_safe(mm, tmp, &uctx->mm_head, entry) {
		list_del(&mm->entry);
		kfree(mm);
	}
}

int ocrdma_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct ocrdma_ucontext *ucontext = get_ocrdma_ucontext(context);
	struct ocrdma_dev *dev = get_ocrdma_dev(context->device);
	unsigned long vm_page = vma->vm_pgoff << PAGE_SHIFT;
	u64 unmapped_db = (u64) dev->nic_info.unmapped_db;
	unsigned long len = (vma->vm_end - vma->vm_start);
	int status;
	bool found;

	if (vma->vm_start & (PAGE_SIZE - 1))
		return -EINVAL;
	found = ocrdma_search_mmap(ucontext, vma->vm_pgoff << PAGE_SHIFT, len);
	if (!found)
		return -EINVAL;

	if ((vm_page >= unmapped_db) && (vm_page <= (unmapped_db +
		dev->nic_info.db_total_size)) &&
		(len <=	dev->nic_info.db_page_size)) {
		if (vma->vm_flags & VM_READ)
			return -EPERM;

		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		status = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					    len, vma->vm_page_prot);
	} else if (dev->nic_info.dpp_unmapped_len &&
		(vm_page >= (u64) dev->nic_info.dpp_unmapped_addr) &&
		(vm_page <= (u64) (dev->nic_info.dpp_unmapped_addr +
			dev->nic_info.dpp_unmapped_len)) &&
		(len <= dev->nic_info.dpp_unmapped_len)) {
		if (vma->vm_flags & VM_READ)
			return -EPERM;

		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		status = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					    len, vma->vm_page_prot);
	} else {
		status = remap_pfn_range(vma, vma->vm_start,
					 vma->vm_pgoff, len, vma->vm_page_prot);
	}
	return status;
}

static int ocrdma_copy_pd_uresp(struct ocrdma_dev *dev, struct ocrdma_pd *pd,
				struct ib_udata *udata)
{
	int status;
	u64 db_page_addr;
	u64 dpp_page_addr = 0;
	u32 db_page_size;
	struct ocrdma_alloc_pd_uresp rsp;
	struct ocrdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct ocrdma_ucontext, ibucontext);

	memset(&rsp, 0, sizeof(rsp));
	rsp.id = pd->id;
	rsp.dpp_enabled = pd->dpp_enabled;
	db_page_addr = ocrdma_get_db_addr(dev, pd->id);
	db_page_size = dev->nic_info.db_page_size;

	status = ocrdma_add_mmap(uctx, db_page_addr, db_page_size);
	if (status)
		return status;

	if (pd->dpp_enabled) {
		dpp_page_addr = dev->nic_info.dpp_unmapped_addr +
				(pd->id * PAGE_SIZE);
		status = ocrdma_add_mmap(uctx, dpp_page_addr,
				 PAGE_SIZE);
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
		ocrdma_del_mmap(pd->uctx, dpp_page_addr, PAGE_SIZE);
dpp_map_err:
	ocrdma_del_mmap(pd->uctx, db_page_addr, db_page_size);
	return status;
}

int ocrdma_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ib_device *ibdev = ibpd->device;
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);
	struct ocrdma_pd *pd;
	int status;
	u8 is_uctx_pd = false;
	struct ocrdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct ocrdma_ucontext, ibucontext);

	if (udata) {
		pd = ocrdma_get_ucontext_pd(uctx);
		if (pd) {
			is_uctx_pd = true;
			goto pd_mapping;
		}
	}

	pd = get_ocrdma_pd(ibpd);
	status = _ocrdma_alloc_pd(dev, pd, uctx, udata);
	if (status)
		goto exit;

pd_mapping:
	if (udata) {
		status = ocrdma_copy_pd_uresp(dev, pd, udata);
		if (status)
			goto err;
	}
	return 0;

err:
	if (is_uctx_pd)
		ocrdma_release_ucontext_pd(uctx);
	else
		_ocrdma_dealloc_pd(dev, pd);
exit:
	return status;
}

int ocrdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibpd->device);
	struct ocrdma_ucontext *uctx = NULL;
	u64 usr_db;

	uctx = pd->uctx;
	if (uctx) {
		u64 dpp_db = dev->nic_info.dpp_unmapped_addr +
			(pd->id * PAGE_SIZE);
		if (pd->dpp_enabled)
			ocrdma_del_mmap(pd->uctx, dpp_db, PAGE_SIZE);
		usr_db = ocrdma_get_db_addr(dev, pd->id);
		ocrdma_del_mmap(pd->uctx, usr_db, dev->nic_info.db_page_size);

		if (is_ucontext_pd(uctx, pd)) {
			ocrdma_release_ucontext_pd(uctx);
			return 0;
		}
	}
	_ocrdma_dealloc_pd(dev, pd);
	return 0;
}

static int ocrdma_alloc_lkey(struct ocrdma_dev *dev, struct ocrdma_mr *mr,
			    u32 pdid, int acc, u32 num_pbls, u32 addr_check)
{
	int status;

	mr->hwmr.fr_mr = 0;
	mr->hwmr.local_rd = 1;
	mr->hwmr.remote_rd = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hwmr.remote_wr = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hwmr.local_wr = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hwmr.mw_bind = (acc & IB_ACCESS_MW_BIND) ? 1 : 0;
	mr->hwmr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	mr->hwmr.num_pbls = num_pbls;

	status = ocrdma_mbx_alloc_lkey(dev, &mr->hwmr, pdid, addr_check);
	if (status)
		return status;

	mr->ibmr.lkey = mr->hwmr.lkey;
	if (mr->hwmr.remote_wr || mr->hwmr.remote_rd)
		mr->ibmr.rkey = mr->hwmr.lkey;
	return 0;
}

struct ib_mr *ocrdma_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	int status;
	struct ocrdma_mr *mr;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibpd->device);

	if (acc & IB_ACCESS_REMOTE_WRITE && !(acc & IB_ACCESS_LOCAL_WRITE)) {
		pr_err("%s err, invalid access rights\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	status = ocrdma_alloc_lkey(dev, mr, pd->id, acc, 0,
				   OCRDMA_ADDR_CHECK_DISABLE);
	if (status) {
		kfree(mr);
		return ERR_PTR(status);
	}

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

static int ocrdma_get_pbl_info(struct ocrdma_dev *dev, struct ocrdma_mr *mr,
			      u32 num_pbes)
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
	} while (num_pbls >= dev->attr.max_num_mr_pbl);

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

	mr->pbl_table = kcalloc(mr->num_pbls, sizeof(struct ocrdma_pbl),
				GFP_KERNEL);

	if (!mr->pbl_table)
		return -ENOMEM;

	for (i = 0; i < mr->num_pbls; i++) {
		va = dma_alloc_coherent(&pdev->dev, dma_len, &pa, GFP_KERNEL);
		if (!va) {
			ocrdma_free_mr_pbl_tbl(dev, mr);
			status = -ENOMEM;
			break;
		}
		mr->pbl_table[i].va = va;
		mr->pbl_table[i].pa = pa;
	}
	return status;
}

static void build_user_pbes(struct ocrdma_dev *dev, struct ocrdma_mr *mr)
{
	struct ocrdma_pbe *pbe;
	struct ib_block_iter biter;
	struct ocrdma_pbl *pbl_tbl = mr->hwmr.pbl_table;
	int pbe_cnt;
	u64 pg_addr;

	if (!mr->hwmr.num_pbes)
		return;

	pbe = (struct ocrdma_pbe *)pbl_tbl->va;
	pbe_cnt = 0;

	rdma_umem_for_each_dma_block (mr->umem, &biter, PAGE_SIZE) {
		/* store the page address in pbe */
		pg_addr = rdma_block_iter_dma_address(&biter);
		pbe->pa_lo = cpu_to_le32(pg_addr);
		pbe->pa_hi = cpu_to_le32(upper_32_bits(pg_addr));
		pbe_cnt += 1;
		pbe++;

		/* if the given pbl is full storing the pbes,
		 * move to next pbl.
		 */
		if (pbe_cnt == (mr->hwmr.pbl_size / sizeof(u64))) {
			pbl_tbl++;
			pbe = (struct ocrdma_pbe *)pbl_tbl->va;
			pbe_cnt = 0;
		}
	}
}

struct ib_mr *ocrdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				 u64 usr_addr, int acc, struct ib_udata *udata)
{
	int status = -ENOMEM;
	struct ocrdma_dev *dev = get_ocrdma_dev(ibpd->device);
	struct ocrdma_mr *mr;
	struct ocrdma_pd *pd;

	pd = get_ocrdma_pd(ibpd);

	if (acc & IB_ACCESS_REMOTE_WRITE && !(acc & IB_ACCESS_LOCAL_WRITE))
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(status);
	mr->umem = ib_umem_get(ibpd->device, start, len, acc);
	if (IS_ERR(mr->umem)) {
		status = -EFAULT;
		goto umem_err;
	}
	status = ocrdma_get_pbl_info(
		dev, mr, ib_umem_num_dma_blocks(mr->umem, PAGE_SIZE));
	if (status)
		goto umem_err;

	mr->hwmr.pbe_size = PAGE_SIZE;
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
	build_user_pbes(dev, mr);
	status = ocrdma_reg_mr(dev, &mr->hwmr, pd->id, acc);
	if (status)
		goto mbx_err;
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

int ocrdma_dereg_mr(struct ib_mr *ib_mr, struct ib_udata *udata)
{
	struct ocrdma_mr *mr = get_ocrdma_mr(ib_mr);
	struct ocrdma_dev *dev = get_ocrdma_dev(ib_mr->device);

	(void) ocrdma_mbx_dealloc_lkey(dev, mr->hwmr.fr_mr, mr->hwmr.lkey);

	kfree(mr->pages);
	ocrdma_free_mr_pbl_tbl(dev, &mr->hwmr);

	/* it could be user registered memory. */
	ib_umem_release(mr->umem);
	kfree(mr);

	/* Don't stop cleanup, in case FW is unresponsive */
	if (dev->mqe_ctx.fw_error_state) {
		pr_err("%s(%d) fw not responding.\n",
		       __func__, dev->id);
	}
	return 0;
}

static int ocrdma_copy_cq_uresp(struct ocrdma_dev *dev, struct ocrdma_cq *cq,
				struct ib_udata *udata)
{
	int status;
	struct ocrdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct ocrdma_ucontext, ibucontext);
	struct ocrdma_create_cq_uresp uresp;

	/* this must be user flow! */
	if (!udata)
		return -EINVAL;

	memset(&uresp, 0, sizeof(uresp));
	uresp.cq_id = cq->id;
	uresp.page_size = PAGE_ALIGN(cq->len);
	uresp.num_pages = 1;
	uresp.max_hw_cqe = cq->max_hw_cqe;
	uresp.page_addr[0] = virt_to_phys(cq->va);
	uresp.db_page_addr =  ocrdma_get_db_addr(dev, uctx->cntxt_pd->id);
	uresp.db_page_size = dev->nic_info.db_page_size;
	uresp.phase_change = cq->phase_change ? 1 : 0;
	status = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (status) {
		pr_err("%s(%d) copy error cqid=0x%x.\n",
		       __func__, dev->id, cq->id);
		goto err;
	}
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

int ocrdma_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		     struct ib_udata *udata)
{
	struct ib_device *ibdev = ibcq->device;
	int entries = attr->cqe;
	struct ocrdma_cq *cq = get_ocrdma_cq(ibcq);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibdev);
	struct ocrdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct ocrdma_ucontext, ibucontext);
	u16 pd_id = 0;
	int status;
	struct ocrdma_create_cq_ureq ureq;

	if (attr->flags)
		return -EOPNOTSUPP;

	if (udata) {
		if (ib_copy_from_udata(&ureq, udata, sizeof(ureq)))
			return -EFAULT;
	} else
		ureq.dpp_cq = 0;

	spin_lock_init(&cq->cq_lock);
	spin_lock_init(&cq->comp_handler_lock);
	INIT_LIST_HEAD(&cq->sq_head);
	INIT_LIST_HEAD(&cq->rq_head);

	if (udata)
		pd_id = uctx->cntxt_pd->id;

	status = ocrdma_mbx_create_cq(dev, cq, entries, ureq.dpp_cq, pd_id);
	if (status)
		return status;

	if (udata) {
		status = ocrdma_copy_cq_uresp(dev, cq, udata);
		if (status)
			goto ctx_err;
	}
	cq->phase = OCRDMA_CQE_VALID;
	dev->cq_tbl[cq->id] = cq;
	return 0;

ctx_err:
	ocrdma_mbx_destroy_cq(dev, cq);
	return status;
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

static void ocrdma_flush_cq(struct ocrdma_cq *cq)
{
	int cqe_cnt;
	int valid_count = 0;
	unsigned long flags;

	struct ocrdma_dev *dev = get_ocrdma_dev(cq->ibcq.device);
	struct ocrdma_cqe *cqe = NULL;

	cqe = cq->va;
	cqe_cnt = cq->cqe_cnt;

	/* Last irq might have scheduled a polling thread
	 * sync-up with it before hard flushing.
	 */
	spin_lock_irqsave(&cq->cq_lock, flags);
	while (cqe_cnt) {
		if (is_cqe_valid(cq, cqe))
			valid_count++;
		cqe++;
		cqe_cnt--;
	}
	ocrdma_ring_cq_db(dev, cq->id, false, false, valid_count);
	spin_unlock_irqrestore(&cq->cq_lock, flags);
}

int ocrdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct ocrdma_cq *cq = get_ocrdma_cq(ibcq);
	struct ocrdma_eq *eq = NULL;
	struct ocrdma_dev *dev = get_ocrdma_dev(ibcq->device);
	int pdid = 0;
	u32 irq, indx;

	dev->cq_tbl[cq->id] = NULL;
	indx = ocrdma_get_eq_table_index(dev, cq->eqn);

	eq = &dev->eq_tbl[indx];
	irq = ocrdma_get_irq(dev, eq);
	synchronize_irq(irq);
	ocrdma_flush_cq(cq);

	ocrdma_mbx_destroy_cq(dev, cq);
	if (cq->ucontext) {
		pdid = cq->ucontext->cntxt_pd->id;
		ocrdma_del_mmap(cq->ucontext, (u64) cq->pa,
				PAGE_ALIGN(cq->len));
		ocrdma_del_mmap(cq->ucontext,
				ocrdma_get_db_addr(dev, pdid),
				dev->nic_info.db_page_size);
	}
	return 0;
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
				  struct ib_qp_init_attr *attrs,
				  struct ib_udata *udata)
{
	if ((attrs->qp_type != IB_QPT_GSI) &&
	    (attrs->qp_type != IB_QPT_RC) &&
	    (attrs->qp_type != IB_QPT_UC) &&
	    (attrs->qp_type != IB_QPT_UD)) {
		pr_err("%s(%d) unsupported qp type=0x%x requested\n",
		       __func__, dev->id, attrs->qp_type);
		return -EOPNOTSUPP;
	}
	/* Skip the check for QP1 to support CM size of 128 */
	if ((attrs->qp_type != IB_QPT_GSI) &&
	    (attrs->cap.max_send_wr > dev->attr.max_wqe)) {
		pr_err("%s(%d) unsupported send_wr=0x%x requested\n",
		       __func__, dev->id, attrs->cap.max_send_wr);
		pr_err("%s(%d) supported send_wr=0x%x\n",
		       __func__, dev->id, dev->attr.max_wqe);
		return -EINVAL;
	}
	if (!attrs->srq && (attrs->cap.max_recv_wr > dev->attr.max_rqe)) {
		pr_err("%s(%d) unsupported recv_wr=0x%x requested\n",
		       __func__, dev->id, attrs->cap.max_recv_wr);
		pr_err("%s(%d) supported recv_wr=0x%x\n",
		       __func__, dev->id, dev->attr.max_rqe);
		return -EINVAL;
	}
	if (attrs->cap.max_inline_data > dev->attr.max_inline_data) {
		pr_err("%s(%d) unsupported inline data size=0x%x requested\n",
		       __func__, dev->id, attrs->cap.max_inline_data);
		pr_err("%s(%d) supported inline data size=0x%x\n",
		       __func__, dev->id, dev->attr.max_inline_data);
		return -EINVAL;
	}
	if (attrs->cap.max_send_sge > dev->attr.max_send_sge) {
		pr_err("%s(%d) unsupported send_sge=0x%x requested\n",
		       __func__, dev->id, attrs->cap.max_send_sge);
		pr_err("%s(%d) supported send_sge=0x%x\n",
		       __func__, dev->id, dev->attr.max_send_sge);
		return -EINVAL;
	}
	if (attrs->cap.max_recv_sge > dev->attr.max_recv_sge) {
		pr_err("%s(%d) unsupported recv_sge=0x%x requested\n",
		       __func__, dev->id, attrs->cap.max_recv_sge);
		pr_err("%s(%d) supported recv_sge=0x%x\n",
		       __func__, dev->id, dev->attr.max_recv_sge);
		return -EINVAL;
	}
	/* unprivileged user space cannot create special QP */
	if (udata && attrs->qp_type == IB_QPT_GSI) {
		pr_err
		    ("%s(%d) Userspace can't create special QPs of type=0x%x\n",
		     __func__, dev->id, attrs->qp_type);
		return -EINVAL;
	}
	/* allow creating only one GSI type of QP */
	if (attrs->qp_type == IB_QPT_GSI && dev->gsi_qp_created) {
		pr_err("%s(%d) GSI special QPs already created.\n",
		       __func__, dev->id);
		return -EINVAL;
	}
	/* verify consumer QPs are not trying to use GSI QP's CQ */
	if ((attrs->qp_type != IB_QPT_GSI) && (dev->gsi_qp_created)) {
		if ((dev->gsi_sqcq == get_ocrdma_cq(attrs->send_cq)) ||
			(dev->gsi_rqcq == get_ocrdma_cq(attrs->recv_cq))) {
			pr_err("%s(%d) Consumer QP cannot use GSI CQs.\n",
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
	int status;
	u64 usr_db;
	struct ocrdma_create_qp_uresp uresp;
	struct ocrdma_pd *pd = qp->pd;
	struct ocrdma_dev *dev = get_ocrdma_dev(pd->ibpd.device);

	memset(&uresp, 0, sizeof(uresp));
	usr_db = dev->nic_info.unmapped_db +
			(pd->id * dev->nic_info.db_page_size);
	uresp.qp_id = qp->id;
	uresp.sq_dbid = qp->sq.dbid;
	uresp.num_sq_pages = 1;
	uresp.sq_page_size = PAGE_ALIGN(qp->sq.len);
	uresp.sq_page_addr[0] = virt_to_phys(qp->sq.va);
	uresp.num_wqe_allocated = qp->sq.max_cnt;
	if (!srq) {
		uresp.rq_dbid = qp->rq.dbid;
		uresp.num_rq_pages = 1;
		uresp.rq_page_size = PAGE_ALIGN(qp->rq.len);
		uresp.rq_page_addr[0] = virt_to_phys(qp->rq.va);
		uresp.num_rqe_allocated = qp->rq.max_cnt;
	}
	uresp.db_page_addr = usr_db;
	uresp.db_page_size = dev->nic_info.db_page_size;
	uresp.db_sq_offset = OCRDMA_DB_GEN2_SQ_OFFSET;
	uresp.db_rq_offset = OCRDMA_DB_GEN2_RQ_OFFSET;
	uresp.db_shift = OCRDMA_DB_RQ_SHIFT;

	if (qp->dpp_enabled) {
		uresp.dpp_credit = dpp_credit_lmt;
		uresp.dpp_offset = dpp_offset;
	}
	status = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (status) {
		pr_err("%s(%d) user copy error.\n", __func__, dev->id);
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
	if (ocrdma_get_asic_type(dev) == OCRDMA_ASIC_GEN_SKH_R) {
		qp->sq_db = dev->nic_info.db +
			(pd->id * dev->nic_info.db_page_size) +
			OCRDMA_DB_GEN2_SQ_OFFSET;
		qp->rq_db = dev->nic_info.db +
			(pd->id * dev->nic_info.db_page_size) +
			OCRDMA_DB_GEN2_RQ_OFFSET;
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
	    kcalloc(qp->sq.max_cnt, sizeof(*(qp->wqe_wr_id_tbl)),
		    GFP_KERNEL);
	if (qp->wqe_wr_id_tbl == NULL)
		return -ENOMEM;
	qp->rqe_wr_id_tbl =
	    kcalloc(qp->rq.max_cnt, sizeof(u64), GFP_KERNEL);
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
	qp->signaled = (attrs->sq_sig_type == IB_SIGNAL_ALL_WR) ? true : false;
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
	struct ocrdma_dev *dev = get_ocrdma_dev(ibpd->device);
	struct ocrdma_create_qp_ureq ureq;
	u16 dpp_credit_lmt, dpp_offset;

	if (attrs->create_flags)
		return ERR_PTR(-EOPNOTSUPP);

	status = ocrdma_check_qp_params(ibpd, dev, attrs, udata);
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
	ocrdma_set_qp_init_params(qp, pd, attrs);
	if (udata == NULL)
		qp->cap_flags |= (OCRDMA_QP_MW_BIND | OCRDMA_QP_LKEY0 |
					OCRDMA_QP_FAST_REG);

	mutex_lock(&dev->dev_lock);
	status = ocrdma_mbx_create_qp(qp, attrs, ureq.enable_dpp_cq,
					ureq.dpp_cq_id,
					&dpp_offset, &dpp_credit_lmt);
	if (status)
		goto mbx_err;

	/* user space QP's wr_id table are managed in library */
	if (udata == NULL) {
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
	qp->ibqp.qp_num = qp->id;
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
	pr_err("%s(%d) error=%d\n", __func__, dev->id, status);
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
	dev = get_ocrdma_dev(ibqp->device);
	if (attr_mask & IB_QP_STATE)
		status = ocrdma_qp_state_change(qp, attr->qp_state, &old_qps);
	/* if new and previous states are same hw doesn't need to
	 * know about it.
	 */
	if (status < 0)
		return status;
	return ocrdma_mbx_modify_qp(dev, qp, attr, attr_mask);
}

int ocrdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		     int attr_mask, struct ib_udata *udata)
{
	unsigned long flags;
	int status = -EINVAL;
	struct ocrdma_qp *qp;
	struct ocrdma_dev *dev;
	enum ib_qp_state old_qps, new_qps;

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	qp = get_ocrdma_qp(ibqp);
	dev = get_ocrdma_dev(ibqp->device);

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
		pr_err("%s(%d) invalid attribute mask=0x%x specified for\n"
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
	struct ocrdma_dev *dev = get_ocrdma_dev(ibqp->device);

	memset(&params, 0, sizeof(params));
	mutex_lock(&dev->dev_lock);
	status = ocrdma_mbx_query_qp(dev, qp, &params);
	mutex_unlock(&dev->dev_lock);
	if (status)
		goto mbx_err;
	if (qp->qp_type == IB_QPT_UD)
		qp_attr->qkey = params.qkey;
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
	qp_attr->cap.max_inline_data = qp->max_inline_data;
	qp_init_attr->cap = qp_attr->cap;
	qp_attr->ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;

	rdma_ah_set_grh(&qp_attr->ah_attr, NULL,
			params.rnt_rc_sl_fl &
			  OCRDMA_QP_PARAMS_FLOW_LABEL_MASK,
			qp->sgid_idx,
			(params.hop_lmt_rq_psn &
			 OCRDMA_QP_PARAMS_HOP_LMT_MASK) >>
			 OCRDMA_QP_PARAMS_HOP_LMT_SHIFT,
			(params.tclass_sq_psn &
			 OCRDMA_QP_PARAMS_TCLASS_MASK) >>
			 OCRDMA_QP_PARAMS_TCLASS_SHIFT);
	rdma_ah_set_dgid_raw(&qp_attr->ah_attr, &params.dgid[0]);

	rdma_ah_set_port_num(&qp_attr->ah_attr, 1);
	rdma_ah_set_sl(&qp_attr->ah_attr, (params.rnt_rc_sl_fl &
					   OCRDMA_QP_PARAMS_SL_MASK) >>
					   OCRDMA_QP_PARAMS_SL_SHIFT);
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
	rdma_ah_set_path_bits(&qp_attr->ah_attr, 0);
	rdma_ah_set_static_rate(&qp_attr->ah_attr, 0);
	qp_attr->alt_pkey_index = 0;
	qp_attr->alt_port_num = 0;
	qp_attr->alt_timeout = 0;
	memset(&qp_attr->alt_ah_attr, 0, sizeof(qp_attr->alt_ah_attr));
	qp_state = (params.max_sge_recv_flags & OCRDMA_QP_PARAMS_STATE_MASK) >>
		    OCRDMA_QP_PARAMS_STATE_SHIFT;
	qp_attr->qp_state = get_ibqp_state(qp_state);
	qp_attr->cur_qp_state = qp_attr->qp_state;
	qp_attr->sq_draining = (qp_state == OCRDMA_QPS_SQ_DRAINING) ? 1 : 0;
	qp_attr->max_dest_rd_atomic =
	    params.max_ord_ird >> OCRDMA_QP_PARAMS_MAX_ORD_SHIFT;
	qp_attr->max_rd_atomic =
	    params.max_ord_ird & OCRDMA_QP_PARAMS_MAX_IRD_MASK;
	qp_attr->en_sqd_async_notify = (params.max_sge_recv_flags &
				OCRDMA_QP_PARAMS_FLAGS_SQD_ASYNC) ? 1 : 0;
	/* Sync driver QP state with FW */
	ocrdma_qp_state_change(qp, qp_attr->qp_state, NULL);
mbx_err:
	return status;
}

static void ocrdma_srq_toggle_bit(struct ocrdma_srq *srq, unsigned int idx)
{
	unsigned int i = idx / 32;
	u32 mask = (1U << (idx % 32));

	srq->idx_bit_fields[i] ^= mask;
}

static int ocrdma_hwq_free_cnt(struct ocrdma_qp_hwq_info *q)
{
	return ((q->max_wqe_idx - q->head) + q->tail) % q->max_cnt;
}

static int is_hw_sq_empty(struct ocrdma_qp *qp)
{
	return (qp->sq.tail == qp->sq.head);
}

static int is_hw_rq_empty(struct ocrdma_qp *qp)
{
	return (qp->rq.tail == qp->rq.head);
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
	u32 qpn = 0, wqe_idx = 0;

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

		if (is_cqe_for_sq(cqe)) {
			ocrdma_hwq_inc_tail(&qp->sq);
		} else {
			if (qp->srq) {
				wqe_idx = (le32_to_cpu(cqe->rq.buftag_qpn) >>
					OCRDMA_CQE_BUFTAG_SHIFT) &
					qp->srq->rq.max_wqe_idx;
				BUG_ON(wqe_idx < 1);
				spin_lock_irqsave(&qp->srq->q_lock, flags);
				ocrdma_hwq_inc_tail(&qp->srq->rq);
				ocrdma_srq_toggle_bit(qp->srq, wqe_idx - 1);
				spin_unlock_irqrestore(&qp->srq->q_lock, flags);

			} else {
				ocrdma_hwq_inc_tail(&qp->rq);
			}
		}
		/* mark cqe discarded so that it is not picked up later
		 * in the poll_cq().
		 */
		discard_cnt += 1;
		cqe->cmn.qpn = 0;
skip_cqe:
		cur_getp = (cur_getp + 1) % cq->max_hw_cqe;
	} while (cur_getp != stop_getp);
	spin_unlock_irqrestore(&cq->cq_lock, cq_flags);
}

void ocrdma_del_flush_qp(struct ocrdma_qp *qp)
{
	int found = false;
	unsigned long flags;
	struct ocrdma_dev *dev = get_ocrdma_dev(qp->ibqp.device);
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

int ocrdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct ocrdma_pd *pd;
	struct ocrdma_qp *qp;
	struct ocrdma_dev *dev;
	struct ib_qp_attr attrs;
	int attr_mask;
	unsigned long flags;

	qp = get_ocrdma_qp(ibqp);
	dev = get_ocrdma_dev(ibqp->device);

	pd = qp->pd;

	/* change the QP state to ERROR */
	if (qp->state != OCRDMA_QPS_RST) {
		attrs.qp_state = IB_QPS_ERR;
		attr_mask = IB_QP_STATE;
		_ocrdma_modify_qp(ibqp, &attrs, attr_mask);
	}
	/* ensure that CQEs for newly created QP (whose id may be same with
	 * one which just getting destroyed are same), dont get
	 * discarded until the old CQEs are discarded.
	 */
	mutex_lock(&dev->dev_lock);
	(void) ocrdma_mbx_destroy_qp(dev, qp);

	/*
	 * acquire CQ lock while destroy is in progress, in order to
	 * protect against proessing in-flight CQEs for this QP.
	 */
	spin_lock_irqsave(&qp->sq_cq->cq_lock, flags);
	if (qp->rq_cq && (qp->rq_cq != qp->sq_cq)) {
		spin_lock(&qp->rq_cq->cq_lock);
		ocrdma_del_qpn_map(dev, qp);
		spin_unlock(&qp->rq_cq->cq_lock);
	} else {
		ocrdma_del_qpn_map(dev, qp);
	}
	spin_unlock_irqrestore(&qp->sq_cq->cq_lock, flags);

	if (!pd->uctx) {
		ocrdma_discard_cqes(qp, qp->sq_cq);
		ocrdma_discard_cqes(qp, qp->rq_cq);
	}
	mutex_unlock(&dev->dev_lock);

	if (pd->uctx) {
		ocrdma_del_mmap(pd->uctx, (u64) qp->sq.pa,
				PAGE_ALIGN(qp->sq.len));
		if (!qp->srq)
			ocrdma_del_mmap(pd->uctx, (u64) qp->rq.pa,
					PAGE_ALIGN(qp->rq.len));
	}

	ocrdma_del_flush_qp(qp);

	kfree(qp->wqe_wr_id_tbl);
	kfree(qp->rqe_wr_id_tbl);
	kfree(qp);
	return 0;
}

static int ocrdma_copy_srq_uresp(struct ocrdma_dev *dev, struct ocrdma_srq *srq,
				struct ib_udata *udata)
{
	int status;
	struct ocrdma_create_srq_uresp uresp;

	memset(&uresp, 0, sizeof(uresp));
	uresp.rq_dbid = srq->rq.dbid;
	uresp.num_rq_pages = 1;
	uresp.rq_page_addr[0] = virt_to_phys(srq->rq.va);
	uresp.rq_page_size = srq->rq.len;
	uresp.db_page_addr = dev->nic_info.unmapped_db +
	    (srq->pd->id * dev->nic_info.db_page_size);
	uresp.db_page_size = dev->nic_info.db_page_size;
	uresp.num_rqe_allocated = srq->rq.max_cnt;
	if (ocrdma_get_asic_type(dev) == OCRDMA_ASIC_GEN_SKH_R) {
		uresp.db_rq_offset = OCRDMA_DB_GEN2_RQ_OFFSET;
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

int ocrdma_create_srq(struct ib_srq *ibsrq, struct ib_srq_init_attr *init_attr,
		      struct ib_udata *udata)
{
	int status;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibsrq->pd);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibsrq->device);
	struct ocrdma_srq *srq = get_ocrdma_srq(ibsrq);

	if (init_attr->srq_type != IB_SRQT_BASIC)
		return -EOPNOTSUPP;

	if (init_attr->attr.max_sge > dev->attr.max_recv_sge)
		return -EINVAL;
	if (init_attr->attr.max_wr > dev->attr.max_rqe)
		return -EINVAL;

	spin_lock_init(&srq->q_lock);
	srq->pd = pd;
	srq->db = dev->nic_info.db + (pd->id * dev->nic_info.db_page_size);
	status = ocrdma_mbx_create_srq(dev, srq, init_attr, pd);
	if (status)
		return status;

	if (!udata) {
		srq->rqe_wr_id_tbl = kcalloc(srq->rq.max_cnt, sizeof(u64),
					     GFP_KERNEL);
		if (!srq->rqe_wr_id_tbl) {
			status = -ENOMEM;
			goto arm_err;
		}

		srq->bit_fields_len = (srq->rq.max_cnt / 32) +
		    (srq->rq.max_cnt % 32 ? 1 : 0);
		srq->idx_bit_fields =
		    kmalloc_array(srq->bit_fields_len, sizeof(u32),
				  GFP_KERNEL);
		if (!srq->idx_bit_fields) {
			status = -ENOMEM;
			goto arm_err;
		}
		memset(srq->idx_bit_fields, 0xff,
		       srq->bit_fields_len * sizeof(u32));
	}

	if (init_attr->attr.srq_limit) {
		status = ocrdma_mbx_modify_srq(srq, &init_attr->attr);
		if (status)
			goto arm_err;
	}

	if (udata) {
		status = ocrdma_copy_srq_uresp(dev, srq, udata);
		if (status)
			goto arm_err;
	}

	return 0;

arm_err:
	ocrdma_mbx_destroy_srq(dev, srq);
	kfree(srq->rqe_wr_id_tbl);
	kfree(srq->idx_bit_fields);
	return status;
}

int ocrdma_modify_srq(struct ib_srq *ibsrq,
		      struct ib_srq_attr *srq_attr,
		      enum ib_srq_attr_mask srq_attr_mask,
		      struct ib_udata *udata)
{
	int status;
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

int ocrdma_destroy_srq(struct ib_srq *ibsrq, struct ib_udata *udata)
{
	struct ocrdma_srq *srq;
	struct ocrdma_dev *dev = get_ocrdma_dev(ibsrq->device);

	srq = get_ocrdma_srq(ibsrq);

	ocrdma_mbx_destroy_srq(dev, srq);

	if (srq->pd->uctx)
		ocrdma_del_mmap(srq->pd->uctx, (u64) srq->rq.pa,
				PAGE_ALIGN(srq->rq.len));

	kfree(srq->idx_bit_fields);
	kfree(srq->rqe_wr_id_tbl);
	return 0;
}

/* unprivileged verbs and their support functions. */
static void ocrdma_build_ud_hdr(struct ocrdma_qp *qp,
				struct ocrdma_hdr_wqe *hdr,
				const struct ib_send_wr *wr)
{
	struct ocrdma_ewqe_ud_hdr *ud_hdr =
		(struct ocrdma_ewqe_ud_hdr *)(hdr + 1);
	struct ocrdma_ah *ah = get_ocrdma_ah(ud_wr(wr)->ah);

	ud_hdr->rsvd_dest_qpn = ud_wr(wr)->remote_qpn;
	if (qp->qp_type == IB_QPT_GSI)
		ud_hdr->qkey = qp->qkey;
	else
		ud_hdr->qkey = ud_wr(wr)->remote_qkey;
	ud_hdr->rsvd_ahid = ah->id;
	ud_hdr->hdr_type = ah->hdr_type;
	if (ah->av->valid & OCRDMA_AV_VLAN_VALID)
		hdr->cw |= (OCRDMA_FLAG_AH_VLAN_PR << OCRDMA_WQE_FLAGS_SHIFT);
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

static inline uint32_t ocrdma_sglist_len(struct ib_sge *sg_list, int num_sge)
{
	uint32_t total_len = 0, i;

	for (i = 0; i < num_sge; i++)
		total_len += sg_list[i].length;
	return total_len;
}


static int ocrdma_build_inline_sges(struct ocrdma_qp *qp,
				    struct ocrdma_hdr_wqe *hdr,
				    struct ocrdma_sge *sge,
				    const struct ib_send_wr *wr, u32 wqe_size)
{
	int i;
	char *dpp_addr;

	if (wr->send_flags & IB_SEND_INLINE && qp->qp_type != IB_QPT_UD) {
		hdr->total_len = ocrdma_sglist_len(wr->sg_list, wr->num_sge);
		if (unlikely(hdr->total_len > qp->max_inline_data)) {
			pr_err("%s() supported_len=0x%x,\n"
			       " unsupported len req=0x%x\n", __func__,
				qp->max_inline_data, hdr->total_len);
			return -EINVAL;
		}
		dpp_addr = (char *)sge;
		for (i = 0; i < wr->num_sge; i++) {
			memcpy(dpp_addr,
			       (void *)(unsigned long)wr->sg_list[i].addr,
			       wr->sg_list[i].length);
			dpp_addr += wr->sg_list[i].length;
		}

		wqe_size += roundup(hdr->total_len, OCRDMA_WQE_ALIGN_BYTES);
		if (0 == hdr->total_len)
			wqe_size += sizeof(struct ocrdma_sge);
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
			     const struct ib_send_wr *wr)
{
	int status;
	struct ocrdma_sge *sge;
	u32 wqe_size = sizeof(*hdr);

	if (qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_GSI) {
		ocrdma_build_ud_hdr(qp, hdr, wr);
		sge = (struct ocrdma_sge *)(hdr + 2);
		wqe_size += sizeof(struct ocrdma_ewqe_ud_hdr);
	} else {
		sge = (struct ocrdma_sge *)(hdr + 1);
	}

	status = ocrdma_build_inline_sges(qp, hdr, sge, wr, wqe_size);
	return status;
}

static int ocrdma_build_write(struct ocrdma_qp *qp, struct ocrdma_hdr_wqe *hdr,
			      const struct ib_send_wr *wr)
{
	int status;
	struct ocrdma_sge *ext_rw = (struct ocrdma_sge *)(hdr + 1);
	struct ocrdma_sge *sge = ext_rw + 1;
	u32 wqe_size = sizeof(*hdr) + sizeof(*ext_rw);

	status = ocrdma_build_inline_sges(qp, hdr, sge, wr, wqe_size);
	if (status)
		return status;
	ext_rw->addr_lo = rdma_wr(wr)->remote_addr;
	ext_rw->addr_hi = upper_32_bits(rdma_wr(wr)->remote_addr);
	ext_rw->lrkey = rdma_wr(wr)->rkey;
	ext_rw->len = hdr->total_len;
	return 0;
}

static void ocrdma_build_read(struct ocrdma_qp *qp, struct ocrdma_hdr_wqe *hdr,
			      const struct ib_send_wr *wr)
{
	struct ocrdma_sge *ext_rw = (struct ocrdma_sge *)(hdr + 1);
	struct ocrdma_sge *sge = ext_rw + 1;
	u32 wqe_size = ((wr->num_sge + 1) * sizeof(struct ocrdma_sge)) +
	    sizeof(struct ocrdma_hdr_wqe);

	ocrdma_build_sges(hdr, sge, wr->num_sge, wr->sg_list);
	hdr->cw |= ((wqe_size / OCRDMA_WQE_STRIDE) << OCRDMA_WQE_SIZE_SHIFT);
	hdr->cw |= (OCRDMA_READ << OCRDMA_WQE_OPCODE_SHIFT);
	hdr->cw |= (OCRDMA_TYPE_LKEY << OCRDMA_WQE_TYPE_SHIFT);

	ext_rw->addr_lo = rdma_wr(wr)->remote_addr;
	ext_rw->addr_hi = upper_32_bits(rdma_wr(wr)->remote_addr);
	ext_rw->lrkey = rdma_wr(wr)->rkey;
	ext_rw->len = hdr->total_len;
}

static int get_encoded_page_size(int pg_sz)
{
	/* Max size is 256M 4096 << 16 */
	int i = 0;
	for (; i < 17; i++)
		if (pg_sz == (4096 << i))
			break;
	return i;
}

static int ocrdma_build_reg(struct ocrdma_qp *qp,
			    struct ocrdma_hdr_wqe *hdr,
			    const struct ib_reg_wr *wr)
{
	u64 fbo;
	struct ocrdma_ewqe_fr *fast_reg = (struct ocrdma_ewqe_fr *)(hdr + 1);
	struct ocrdma_mr *mr = get_ocrdma_mr(wr->mr);
	struct ocrdma_pbl *pbl_tbl = mr->hwmr.pbl_table;
	struct ocrdma_pbe *pbe;
	u32 wqe_size = sizeof(*fast_reg) + sizeof(*hdr);
	int num_pbes = 0, i;

	wqe_size = roundup(wqe_size, OCRDMA_WQE_ALIGN_BYTES);

	hdr->cw |= (OCRDMA_FR_MR << OCRDMA_WQE_OPCODE_SHIFT);
	hdr->cw |= ((wqe_size / OCRDMA_WQE_STRIDE) << OCRDMA_WQE_SIZE_SHIFT);

	if (wr->access & IB_ACCESS_LOCAL_WRITE)
		hdr->rsvd_lkey_flags |= OCRDMA_LKEY_FLAG_LOCAL_WR;
	if (wr->access & IB_ACCESS_REMOTE_WRITE)
		hdr->rsvd_lkey_flags |= OCRDMA_LKEY_FLAG_REMOTE_WR;
	if (wr->access & IB_ACCESS_REMOTE_READ)
		hdr->rsvd_lkey_flags |= OCRDMA_LKEY_FLAG_REMOTE_RD;
	hdr->lkey = wr->key;
	hdr->total_len = mr->ibmr.length;

	fbo = mr->ibmr.iova - mr->pages[0];

	fast_reg->va_hi = upper_32_bits(mr->ibmr.iova);
	fast_reg->va_lo = (u32) (mr->ibmr.iova & 0xffffffff);
	fast_reg->fbo_hi = upper_32_bits(fbo);
	fast_reg->fbo_lo = (u32) fbo & 0xffffffff;
	fast_reg->num_sges = mr->npages;
	fast_reg->size_sge = get_encoded_page_size(mr->ibmr.page_size);

	pbe = pbl_tbl->va;
	for (i = 0; i < mr->npages; i++) {
		u64 buf_addr = mr->pages[i];

		pbe->pa_lo = cpu_to_le32((u32) (buf_addr & PAGE_MASK));
		pbe->pa_hi = cpu_to_le32((u32) upper_32_bits(buf_addr));
		num_pbes += 1;
		pbe++;

		/* if the pbl is full storing the pbes,
		 * move to next pbl.
		*/
		if (num_pbes == (mr->hwmr.pbl_size/sizeof(u64))) {
			pbl_tbl++;
			pbe = (struct ocrdma_pbe *)pbl_tbl->va;
		}
	}

	return 0;
}

static void ocrdma_ring_sq_db(struct ocrdma_qp *qp)
{
	u32 val = qp->sq.dbid | (1 << OCRDMA_DB_SQ_SHIFT);

	iowrite32(val, qp->sq_db);
}

int ocrdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		     const struct ib_send_wr **bad_wr)
{
	int status = 0;
	struct ocrdma_qp *qp = get_ocrdma_qp(ibqp);
	struct ocrdma_hdr_wqe *hdr;
	unsigned long flags;

	spin_lock_irqsave(&qp->q_lock, flags);
	if (qp->state != OCRDMA_QPS_RTS && qp->state != OCRDMA_QPS_SQD) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		*bad_wr = wr;
		return -EINVAL;
	}

	while (wr) {
		if (qp->qp_type == IB_QPT_UD &&
		    (wr->opcode != IB_WR_SEND &&
		     wr->opcode != IB_WR_SEND_WITH_IMM)) {
			*bad_wr = wr;
			status = -EINVAL;
			break;
		}
		if (ocrdma_hwq_free_cnt(&qp->sq) == 0 ||
		    wr->num_sge > qp->sq.max_sges) {
			*bad_wr = wr;
			status = -ENOMEM;
			break;
		}
		hdr = ocrdma_hwq_head(&qp->sq);
		hdr->cw = 0;
		if (wr->send_flags & IB_SEND_SIGNALED || qp->signaled)
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
			fallthrough;
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
			fallthrough;
		case IB_WR_RDMA_WRITE:
			hdr->cw |= (OCRDMA_WRITE << OCRDMA_WQE_OPCODE_SHIFT);
			status = ocrdma_build_write(qp, hdr, wr);
			break;
		case IB_WR_RDMA_READ:
			ocrdma_build_read(qp, hdr, wr);
			break;
		case IB_WR_LOCAL_INV:
			hdr->cw |=
			    (OCRDMA_LKEY_INV << OCRDMA_WQE_OPCODE_SHIFT);
			hdr->cw |= ((sizeof(struct ocrdma_hdr_wqe) +
					sizeof(struct ocrdma_sge)) /
				OCRDMA_WQE_STRIDE) << OCRDMA_WQE_SIZE_SHIFT;
			hdr->lkey = wr->ex.invalidate_rkey;
			break;
		case IB_WR_REG_MR:
			status = ocrdma_build_reg(qp, hdr, reg_wr(wr));
			break;
		default:
			status = -EINVAL;
			break;
		}
		if (status) {
			*bad_wr = wr;
			break;
		}
		if (wr->send_flags & IB_SEND_SIGNALED || qp->signaled)
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
	u32 val = qp->rq.dbid | (1 << OCRDMA_DB_RQ_SHIFT);

	iowrite32(val, qp->rq_db);
}

static void ocrdma_build_rqe(struct ocrdma_hdr_wqe *rqe,
			     const struct ib_recv_wr *wr, u16 tag)
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

int ocrdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		     const struct ib_recv_wr **bad_wr)
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
			BUG_ON(indx >= srq->rq.max_cnt);
			ocrdma_srq_toggle_bit(srq, indx);
			break;
		}
	}

	BUG_ON(row == srq->bit_fields_len);
	return indx + 1; /* Use from index 1 */
}

static void ocrdma_ring_srq_db(struct ocrdma_srq *srq)
{
	u32 val = srq->rq.dbid | (1 << 16);

	iowrite32(val, srq->db + OCRDMA_DB_GEN2_SRQ_OFFSET);
}

int ocrdma_post_srq_recv(struct ib_srq *ibsrq, const struct ib_recv_wr *wr,
			 const struct ib_recv_wr **bad_wr)
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
	enum ib_wc_status ibwc_status;

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
	}
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
	case OCRDMA_FR_MR:
		ibwc->opcode = IB_WC_REG_MR;
		break;
	case OCRDMA_LKEY_INV:
		ibwc->opcode = IB_WC_LOCAL_INV;
		break;
	default:
		ibwc->status = IB_WC_GENERAL_ERR;
		pr_err("%s() invalid opcode received = 0x%x\n",
		       __func__, hdr->cw & OCRDMA_WQE_OPCODE_MASK);
		break;
	}
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
	ocrdma_qp_state_change(qp, IB_QPS_ERR, NULL);

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
	struct ocrdma_dev *dev = get_ocrdma_dev(qp->ibqp.device);
	int status = (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_STATUS_MASK) >> OCRDMA_CQE_STATUS_SHIFT;
	if (status < OCRDMA_MAX_CQE_ERR)
		atomic_inc(&dev->cqe_err_stats[status]);

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
	} else if (is_hw_sq_empty(qp)) {
		/* Do nothing */
		expand = false;
		*polled = false;
		*stop = false;
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
		*polled = false;    /* WC cannot be consumed yet */
	} else {
		ibwc->status = IB_WC_SUCCESS;
		ibwc->wc_flags = 0;
		ibwc->qp = &qp->ibqp;
		ocrdma_update_wc(qp, ibwc, tail);
		*polled = true;
	}
	wqe_idx = (le32_to_cpu(cqe->wq.wqeidx) &
			OCRDMA_CQE_WQEIDX_MASK) & qp->sq.max_wqe_idx;
	if (tail != wqe_idx)
		expand = true; /* Coalesced CQE can't be consumed yet */

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

static int ocrdma_update_ud_rcqe(struct ocrdma_dev *dev, struct ib_wc *ibwc,
				 struct ocrdma_cqe *cqe)
{
	int status;
	u16 hdr_type = 0;

	status = (le32_to_cpu(cqe->flags_status_srcqpn) &
		OCRDMA_CQE_UD_STATUS_MASK) >> OCRDMA_CQE_UD_STATUS_SHIFT;
	ibwc->src_qp = le32_to_cpu(cqe->flags_status_srcqpn) &
						OCRDMA_CQE_SRCQP_MASK;
	ibwc->pkey_index = 0;
	ibwc->wc_flags = IB_WC_GRH;
	ibwc->byte_len = (le32_to_cpu(cqe->ud.rxlen_pkey) >>
			  OCRDMA_CQE_UD_XFER_LEN_SHIFT) &
			  OCRDMA_CQE_UD_XFER_LEN_MASK;

	if (ocrdma_is_udp_encap_supported(dev)) {
		hdr_type = (le32_to_cpu(cqe->ud.rxlen_pkey) >>
			    OCRDMA_CQE_UD_L3TYPE_SHIFT) &
			    OCRDMA_CQE_UD_L3TYPE_MASK;
		ibwc->wc_flags |= IB_WC_WITH_NETWORK_HDR_TYPE;
		ibwc->network_hdr_type = hdr_type;
	}

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
	wqe_idx = (le32_to_cpu(cqe->rq.buftag_qpn) >>
		OCRDMA_CQE_BUFTAG_SHIFT) & srq->rq.max_wqe_idx;
	BUG_ON(wqe_idx < 1);

	ibwc->wr_id = srq->rqe_wr_id_tbl[wqe_idx];
	spin_lock_irqsave(&srq->q_lock, flags);
	ocrdma_srq_toggle_bit(srq, wqe_idx - 1);
	spin_unlock_irqrestore(&srq->q_lock, flags);
	ocrdma_hwq_inc_tail(&srq->rq);
}

static bool ocrdma_poll_err_rcqe(struct ocrdma_qp *qp, struct ocrdma_cqe *cqe,
				struct ib_wc *ibwc, bool *polled, bool *stop,
				int status)
{
	bool expand;
	struct ocrdma_dev *dev = get_ocrdma_dev(qp->ibqp.device);

	if (status < OCRDMA_MAX_CQE_ERR)
		atomic_inc(&dev->cqe_err_stats[status]);

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
	} else if (is_hw_rq_empty(qp)) {
		/* Do nothing */
		expand = false;
		*polled = false;
		*stop = false;
	} else {
		*polled = true;
		expand = ocrdma_update_err_rcqe(ibwc, cqe, qp, status);
	}
	return expand;
}

static void ocrdma_poll_success_rcqe(struct ocrdma_qp *qp,
				     struct ocrdma_cqe *cqe, struct ib_wc *ibwc)
{
	struct ocrdma_dev *dev;

	dev = get_ocrdma_dev(qp->ibqp.device);
	ibwc->opcode = IB_WC_RECV;
	ibwc->qp = &qp->ibqp;
	ibwc->status = IB_WC_SUCCESS;

	if (qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_GSI)
		ocrdma_update_ud_rcqe(dev, ibwc, cqe);
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
	if (qp->ibqp.srq) {
		ocrdma_update_free_srq_cqe(ibwc, cqe, qp);
	} else {
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
	if (qp->qp_type == IB_QPT_UD || qp->qp_type == IB_QPT_GSI) {
		status = (le32_to_cpu(cqe->flags_status_srcqpn) &
					OCRDMA_CQE_UD_STATUS_MASK) >>
					OCRDMA_CQE_UD_STATUS_SHIFT;
	} else {
		status = (le32_to_cpu(cqe->flags_status_srcqpn) &
			     OCRDMA_CQE_STATUS_MASK) >> OCRDMA_CQE_STATUS_SHIFT;
	}

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
	} else {
		/* clear valid bit */
		cqe->flags_status_srcqpn = 0;
	}
}

static int ocrdma_poll_hwcq(struct ocrdma_cq *cq, int num_entries,
			    struct ib_wc *ibwc)
{
	u16 qpn = 0;
	int i = 0;
	bool expand = false;
	int polled_hw_cqes = 0;
	struct ocrdma_qp *qp = NULL;
	struct ocrdma_dev *dev = get_ocrdma_dev(cq->ibcq.device);
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

	if (polled_hw_cqes)
		ocrdma_ring_cq_db(dev, cq->id, false, false, polled_hw_cqes);

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
		} else {
			return err_cqes;
		}
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
	struct ocrdma_cq *cq = get_ocrdma_cq(ibcq);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibcq->device);
	int num_os_cqe = 0, err_cqes = 0;
	struct ocrdma_qp *qp;
	unsigned long flags;

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
	struct ocrdma_cq *cq = get_ocrdma_cq(ibcq);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibcq->device);
	u16 cq_id;
	unsigned long flags;
	bool arm_needed = false, sol_needed = false;

	cq_id = cq->id;

	spin_lock_irqsave(&cq->cq_lock, flags);
	if (cq_flags & IB_CQ_NEXT_COMP || cq_flags & IB_CQ_SOLICITED)
		arm_needed = true;
	if (cq_flags & IB_CQ_SOLICITED)
		sol_needed = true;

	ocrdma_ring_cq_db(dev, cq_id, arm_needed, sol_needed, 0);
	spin_unlock_irqrestore(&cq->cq_lock, flags);

	return 0;
}

struct ib_mr *ocrdma_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type,
			      u32 max_num_sg)
{
	int status;
	struct ocrdma_mr *mr;
	struct ocrdma_pd *pd = get_ocrdma_pd(ibpd);
	struct ocrdma_dev *dev = get_ocrdma_dev(ibpd->device);

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	if (max_num_sg > dev->attr.max_pages_per_frmr)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->pages = kcalloc(max_num_sg, sizeof(u64), GFP_KERNEL);
	if (!mr->pages) {
		status = -ENOMEM;
		goto pl_err;
	}

	status = ocrdma_get_pbl_info(dev, mr, max_num_sg);
	if (status)
		goto pbl_err;
	mr->hwmr.fr_mr = 1;
	mr->hwmr.remote_rd = 0;
	mr->hwmr.remote_wr = 0;
	mr->hwmr.local_rd = 0;
	mr->hwmr.local_wr = 0;
	mr->hwmr.mw_bind = 0;
	status = ocrdma_build_pbl_tbl(dev, &mr->hwmr);
	if (status)
		goto pbl_err;
	status = ocrdma_reg_mr(dev, &mr->hwmr, pd->id, 0);
	if (status)
		goto mbx_err;
	mr->ibmr.rkey = mr->hwmr.lkey;
	mr->ibmr.lkey = mr->hwmr.lkey;
	dev->stag_arr[(mr->hwmr.lkey >> 8) & (OCRDMA_MAX_STAG - 1)] =
		(unsigned long) mr;
	return &mr->ibmr;
mbx_err:
	ocrdma_free_mr_pbl_tbl(dev, &mr->hwmr);
pbl_err:
	kfree(mr->pages);
pl_err:
	kfree(mr);
	return ERR_PTR(-ENOMEM);
}

static int ocrdma_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct ocrdma_mr *mr = get_ocrdma_mr(ibmr);

	if (unlikely(mr->npages == mr->hwmr.num_pbes))
		return -ENOMEM;

	mr->pages[mr->npages++] = addr;

	return 0;
}

int ocrdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		     unsigned int *sg_offset)
{
	struct ocrdma_mr *mr = get_ocrdma_mr(ibmr);

	mr->npages = 0;

	return ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset, ocrdma_set_page);
}
