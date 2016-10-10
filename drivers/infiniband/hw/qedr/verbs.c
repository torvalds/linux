/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/dma-mapping.h>
#include <linux/crc32.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <linux/iommu.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/iw_cm.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>

#include "qedr_hsi.h"
#include <linux/qed/qed_if.h>
#include "qedr.h"
#include "verbs.h"
#include <rdma/qedr-abi.h>

int qedr_query_gid(struct ib_device *ibdev, u8 port, int index,
		   union ib_gid *sgid)
{
	struct qedr_dev *dev = get_qedr_dev(ibdev);
	int rc = 0;

	if (!rdma_cap_roce_gid_table(ibdev, port))
		return -ENODEV;

	rc = ib_get_cached_gid(ibdev, port, index, sgid, NULL);
	if (rc == -EAGAIN) {
		memcpy(sgid, &zgid, sizeof(*sgid));
		return 0;
	}

	DP_DEBUG(dev, QEDR_MSG_INIT, "query gid: index=%d %llx:%llx\n", index,
		 sgid->global.interface_id, sgid->global.subnet_prefix);

	return rc;
}

int qedr_add_gid(struct ib_device *device, u8 port_num,
		 unsigned int index, const union ib_gid *gid,
		 const struct ib_gid_attr *attr, void **context)
{
	if (!rdma_cap_roce_gid_table(device, port_num))
		return -EINVAL;

	if (port_num > QEDR_MAX_PORT)
		return -EINVAL;

	if (!context)
		return -EINVAL;

	return 0;
}

int qedr_del_gid(struct ib_device *device, u8 port_num,
		 unsigned int index, void **context)
{
	if (!rdma_cap_roce_gid_table(device, port_num))
		return -EINVAL;

	if (port_num > QEDR_MAX_PORT)
		return -EINVAL;

	if (!context)
		return -EINVAL;

	return 0;
}

int qedr_query_device(struct ib_device *ibdev,
		      struct ib_device_attr *attr, struct ib_udata *udata)
{
	struct qedr_dev *dev = get_qedr_dev(ibdev);
	struct qedr_device_attr *qattr = &dev->attr;

	if (!dev->rdma_ctx) {
		DP_ERR(dev,
		       "qedr_query_device called with invalid params rdma_ctx=%p\n",
		       dev->rdma_ctx);
		return -EINVAL;
	}

	memset(attr, 0, sizeof(*attr));

	attr->fw_ver = qattr->fw_ver;
	attr->sys_image_guid = qattr->sys_image_guid;
	attr->max_mr_size = qattr->max_mr_size;
	attr->page_size_cap = qattr->page_size_caps;
	attr->vendor_id = qattr->vendor_id;
	attr->vendor_part_id = qattr->vendor_part_id;
	attr->hw_ver = qattr->hw_ver;
	attr->max_qp = qattr->max_qp;
	attr->max_qp_wr = max_t(u32, qattr->max_sqe, qattr->max_rqe);
	attr->device_cap_flags = IB_DEVICE_CURR_QP_STATE_MOD |
	    IB_DEVICE_RC_RNR_NAK_GEN |
	    IB_DEVICE_LOCAL_DMA_LKEY | IB_DEVICE_MEM_MGT_EXTENSIONS;

	attr->max_sge = qattr->max_sge;
	attr->max_sge_rd = qattr->max_sge;
	attr->max_cq = qattr->max_cq;
	attr->max_cqe = qattr->max_cqe;
	attr->max_mr = qattr->max_mr;
	attr->max_mw = qattr->max_mw;
	attr->max_pd = qattr->max_pd;
	attr->atomic_cap = dev->atomic_cap;
	attr->max_fmr = qattr->max_fmr;
	attr->max_map_per_fmr = 16;
	attr->max_qp_init_rd_atom =
	    1 << (fls(qattr->max_qp_req_rd_atomic_resc) - 1);
	attr->max_qp_rd_atom =
	    min(1 << (fls(qattr->max_qp_resp_rd_atomic_resc) - 1),
		attr->max_qp_init_rd_atom);

	attr->max_srq = qattr->max_srq;
	attr->max_srq_sge = qattr->max_srq_sge;
	attr->max_srq_wr = qattr->max_srq_wr;

	attr->local_ca_ack_delay = qattr->dev_ack_delay;
	attr->max_fast_reg_page_list_len = qattr->max_mr / 8;
	attr->max_pkeys = QEDR_ROCE_PKEY_MAX;
	attr->max_ah = qattr->max_ah;

	return 0;
}

#define QEDR_SPEED_SDR		(1)
#define QEDR_SPEED_DDR		(2)
#define QEDR_SPEED_QDR		(4)
#define QEDR_SPEED_FDR10	(8)
#define QEDR_SPEED_FDR		(16)
#define QEDR_SPEED_EDR		(32)

static inline void get_link_speed_and_width(int speed, u8 *ib_speed,
					    u8 *ib_width)
{
	switch (speed) {
	case 1000:
		*ib_speed = QEDR_SPEED_SDR;
		*ib_width = IB_WIDTH_1X;
		break;
	case 10000:
		*ib_speed = QEDR_SPEED_QDR;
		*ib_width = IB_WIDTH_1X;
		break;

	case 20000:
		*ib_speed = QEDR_SPEED_DDR;
		*ib_width = IB_WIDTH_4X;
		break;

	case 25000:
		*ib_speed = QEDR_SPEED_EDR;
		*ib_width = IB_WIDTH_1X;
		break;

	case 40000:
		*ib_speed = QEDR_SPEED_QDR;
		*ib_width = IB_WIDTH_4X;
		break;

	case 50000:
		*ib_speed = QEDR_SPEED_QDR;
		*ib_width = IB_WIDTH_4X;
		break;

	case 100000:
		*ib_speed = QEDR_SPEED_EDR;
		*ib_width = IB_WIDTH_4X;
		break;

	default:
		/* Unsupported */
		*ib_speed = QEDR_SPEED_SDR;
		*ib_width = IB_WIDTH_1X;
	}
}

int qedr_query_port(struct ib_device *ibdev, u8 port, struct ib_port_attr *attr)
{
	struct qedr_dev *dev;
	struct qed_rdma_port *rdma_port;

	dev = get_qedr_dev(ibdev);
	if (port > 1) {
		DP_ERR(dev, "invalid_port=0x%x\n", port);
		return -EINVAL;
	}

	if (!dev->rdma_ctx) {
		DP_ERR(dev, "rdma_ctx is NULL\n");
		return -EINVAL;
	}

	rdma_port = dev->ops->rdma_query_port(dev->rdma_ctx);
	memset(attr, 0, sizeof(*attr));

	if (rdma_port->port_state == QED_RDMA_PORT_UP) {
		attr->state = IB_PORT_ACTIVE;
		attr->phys_state = 5;
	} else {
		attr->state = IB_PORT_DOWN;
		attr->phys_state = 3;
	}
	attr->max_mtu = IB_MTU_4096;
	attr->active_mtu = iboe_get_mtu(dev->ndev->mtu);
	attr->lid = 0;
	attr->lmc = 0;
	attr->sm_lid = 0;
	attr->sm_sl = 0;
	attr->port_cap_flags = IB_PORT_IP_BASED_GIDS;
	attr->gid_tbl_len = QEDR_MAX_SGID;
	attr->pkey_tbl_len = QEDR_ROCE_PKEY_TABLE_LEN;
	attr->bad_pkey_cntr = rdma_port->pkey_bad_counter;
	attr->qkey_viol_cntr = 0;
	get_link_speed_and_width(rdma_port->link_speed,
				 &attr->active_speed, &attr->active_width);
	attr->max_msg_sz = rdma_port->max_msg_size;
	attr->max_vl_num = 4;

	return 0;
}

int qedr_modify_port(struct ib_device *ibdev, u8 port, int mask,
		     struct ib_port_modify *props)
{
	struct qedr_dev *dev;

	dev = get_qedr_dev(ibdev);
	if (port > 1) {
		DP_ERR(dev, "invalid_port=0x%x\n", port);
		return -EINVAL;
	}

	return 0;
}

static int qedr_add_mmap(struct qedr_ucontext *uctx, u64 phy_addr,
			 unsigned long len)
{
	struct qedr_mm *mm;

	mm = kzalloc(sizeof(*mm), GFP_KERNEL);
	if (!mm)
		return -ENOMEM;

	mm->key.phy_addr = phy_addr;
	/* This function might be called with a length which is not a multiple
	 * of PAGE_SIZE, while the mapping is PAGE_SIZE grained and the kernel
	 * forces this granularity by increasing the requested size if needed.
	 * When qedr_mmap is called, it will search the list with the updated
	 * length as a key. To prevent search failures, the length is rounded up
	 * in advance to PAGE_SIZE.
	 */
	mm->key.len = roundup(len, PAGE_SIZE);
	INIT_LIST_HEAD(&mm->entry);

	mutex_lock(&uctx->mm_list_lock);
	list_add(&mm->entry, &uctx->mm_head);
	mutex_unlock(&uctx->mm_list_lock);

	DP_DEBUG(uctx->dev, QEDR_MSG_MISC,
		 "added (addr=0x%llx,len=0x%lx) for ctx=%p\n",
		 (unsigned long long)mm->key.phy_addr,
		 (unsigned long)mm->key.len, uctx);

	return 0;
}

static bool qedr_search_mmap(struct qedr_ucontext *uctx, u64 phy_addr,
			     unsigned long len)
{
	bool found = false;
	struct qedr_mm *mm;

	mutex_lock(&uctx->mm_list_lock);
	list_for_each_entry(mm, &uctx->mm_head, entry) {
		if (len != mm->key.len || phy_addr != mm->key.phy_addr)
			continue;

		found = true;
		break;
	}
	mutex_unlock(&uctx->mm_list_lock);
	DP_DEBUG(uctx->dev, QEDR_MSG_MISC,
		 "searched for (addr=0x%llx,len=0x%lx) for ctx=%p, result=%d\n",
		 mm->key.phy_addr, mm->key.len, uctx, found);

	return found;
}

struct ib_ucontext *qedr_alloc_ucontext(struct ib_device *ibdev,
					struct ib_udata *udata)
{
	int rc;
	struct qedr_ucontext *ctx;
	struct qedr_alloc_ucontext_resp uresp;
	struct qedr_dev *dev = get_qedr_dev(ibdev);
	struct qed_rdma_add_user_out_params oparams;

	if (!udata)
		return ERR_PTR(-EFAULT);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	rc = dev->ops->rdma_add_user(dev->rdma_ctx, &oparams);
	if (rc) {
		DP_ERR(dev,
		       "failed to allocate a DPI for a new RoCE application, rc=%d. To overcome this consider to increase the number of DPIs, increase the doorbell BAR size or just close unnecessary RoCE applications. In order to increase the number of DPIs consult the qedr readme\n",
		       rc);
		goto err;
	}

	ctx->dpi = oparams.dpi;
	ctx->dpi_addr = oparams.dpi_addr;
	ctx->dpi_phys_addr = oparams.dpi_phys_addr;
	ctx->dpi_size = oparams.dpi_size;
	INIT_LIST_HEAD(&ctx->mm_head);
	mutex_init(&ctx->mm_list_lock);

	memset(&uresp, 0, sizeof(uresp));

	uresp.db_pa = ctx->dpi_phys_addr;
	uresp.db_size = ctx->dpi_size;
	uresp.max_send_wr = dev->attr.max_sqe;
	uresp.max_recv_wr = dev->attr.max_rqe;
	uresp.max_srq_wr = dev->attr.max_srq_wr;
	uresp.sges_per_send_wr = QEDR_MAX_SQE_ELEMENTS_PER_SQE;
	uresp.sges_per_recv_wr = QEDR_MAX_RQE_ELEMENTS_PER_RQE;
	uresp.sges_per_srq_wr = dev->attr.max_srq_sge;
	uresp.max_cqes = QEDR_MAX_CQES;

	rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (rc)
		goto err;

	ctx->dev = dev;

	rc = qedr_add_mmap(ctx, ctx->dpi_phys_addr, ctx->dpi_size);
	if (rc)
		goto err;

	DP_DEBUG(dev, QEDR_MSG_INIT, "Allocating user context %p\n",
		 &ctx->ibucontext);
	return &ctx->ibucontext;

err:
	kfree(ctx);
	return ERR_PTR(rc);
}

int qedr_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct qedr_ucontext *uctx = get_qedr_ucontext(ibctx);
	struct qedr_mm *mm, *tmp;
	int status = 0;

	DP_DEBUG(uctx->dev, QEDR_MSG_INIT, "Deallocating user context %p\n",
		 uctx);
	uctx->dev->ops->rdma_remove_user(uctx->dev->rdma_ctx, uctx->dpi);

	list_for_each_entry_safe(mm, tmp, &uctx->mm_head, entry) {
		DP_DEBUG(uctx->dev, QEDR_MSG_MISC,
			 "deleted (addr=0x%llx,len=0x%lx) for ctx=%p\n",
			 mm->key.phy_addr, mm->key.len, uctx);
		list_del(&mm->entry);
		kfree(mm);
	}

	kfree(uctx);
	return status;
}

int qedr_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct qedr_ucontext *ucontext = get_qedr_ucontext(context);
	struct qedr_dev *dev = get_qedr_dev(context->device);
	unsigned long vm_page = vma->vm_pgoff << PAGE_SHIFT;
	u64 unmapped_db = dev->db_phys_addr;
	unsigned long len = (vma->vm_end - vma->vm_start);
	int rc = 0;
	bool found;

	DP_DEBUG(dev, QEDR_MSG_INIT,
		 "qedr_mmap called vm_page=0x%lx vm_pgoff=0x%lx unmapped_db=0x%llx db_size=%x, len=%lx\n",
		 vm_page, vma->vm_pgoff, unmapped_db, dev->db_size, len);
	if (vma->vm_start & (PAGE_SIZE - 1)) {
		DP_ERR(dev, "Vma_start not page aligned = %ld\n",
		       vma->vm_start);
		return -EINVAL;
	}

	found = qedr_search_mmap(ucontext, vm_page, len);
	if (!found) {
		DP_ERR(dev, "Vma_pgoff not found in mapped array = %ld\n",
		       vma->vm_pgoff);
		return -EINVAL;
	}

	DP_DEBUG(dev, QEDR_MSG_INIT, "Mapping doorbell bar\n");

	if ((vm_page >= unmapped_db) && (vm_page <= (unmapped_db +
						     dev->db_size))) {
		DP_DEBUG(dev, QEDR_MSG_INIT, "Mapping doorbell bar\n");
		if (vma->vm_flags & VM_READ) {
			DP_ERR(dev, "Trying to map doorbell bar for read\n");
			return -EPERM;
		}

		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

		rc = io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
					PAGE_SIZE, vma->vm_page_prot);
	} else {
		DP_DEBUG(dev, QEDR_MSG_INIT, "Mapping chains\n");
		rc = remap_pfn_range(vma, vma->vm_start,
				     vma->vm_pgoff, len, vma->vm_page_prot);
	}
	DP_DEBUG(dev, QEDR_MSG_INIT, "qedr_mmap return code: %d\n", rc);
	return rc;
}
