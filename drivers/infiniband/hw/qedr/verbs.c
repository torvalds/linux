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
#include "qedr_cm.h"

#define DB_ADDR_SHIFT(addr)		((addr) << DB_PWM_ADDR_OFFSET_SHIFT)

int qedr_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	if (index > QEDR_ROCE_PKEY_TABLE_LEN)
		return -EINVAL;

	*pkey = QEDR_ROCE_PKEY_DEFAULT;
	return 0;
}

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

	/* *attr being zeroed by the caller, avoid zeroing it here */
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

struct ib_pd *qedr_alloc_pd(struct ib_device *ibdev,
			    struct ib_ucontext *context, struct ib_udata *udata)
{
	struct qedr_dev *dev = get_qedr_dev(ibdev);
	struct qedr_pd *pd;
	u16 pd_id;
	int rc;

	DP_DEBUG(dev, QEDR_MSG_INIT, "Function called from: %s\n",
		 (udata && context) ? "User Lib" : "Kernel");

	if (!dev->rdma_ctx) {
		DP_ERR(dev, "invlaid RDMA context\n");
		return ERR_PTR(-EINVAL);
	}

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	rc = dev->ops->rdma_alloc_pd(dev->rdma_ctx, &pd_id);
	if (rc)
		goto err;

	pd->pd_id = pd_id;

	if (udata && context) {
		struct qedr_alloc_pd_uresp uresp;

		uresp.pd_id = pd_id;

		rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (rc) {
			DP_ERR(dev, "copy error pd_id=0x%x.\n", pd_id);
			dev->ops->rdma_dealloc_pd(dev->rdma_ctx, pd_id);
			goto err;
		}

		pd->uctx = get_qedr_ucontext(context);
		pd->uctx->pd = pd;
	}

	return &pd->ibpd;

err:
	kfree(pd);
	return ERR_PTR(rc);
}

int qedr_dealloc_pd(struct ib_pd *ibpd)
{
	struct qedr_dev *dev = get_qedr_dev(ibpd->device);
	struct qedr_pd *pd = get_qedr_pd(ibpd);

	if (!pd) {
		pr_err("Invalid PD received in dealloc_pd\n");
		return -EINVAL;
	}

	DP_DEBUG(dev, QEDR_MSG_INIT, "Deallocating PD %d\n", pd->pd_id);
	dev->ops->rdma_dealloc_pd(dev->rdma_ctx, pd->pd_id);

	kfree(pd);

	return 0;
}

static void qedr_free_pbl(struct qedr_dev *dev,
			  struct qedr_pbl_info *pbl_info, struct qedr_pbl *pbl)
{
	struct pci_dev *pdev = dev->pdev;
	int i;

	for (i = 0; i < pbl_info->num_pbls; i++) {
		if (!pbl[i].va)
			continue;
		dma_free_coherent(&pdev->dev, pbl_info->pbl_size,
				  pbl[i].va, pbl[i].pa);
	}

	kfree(pbl);
}

#define MIN_FW_PBL_PAGE_SIZE (4 * 1024)
#define MAX_FW_PBL_PAGE_SIZE (64 * 1024)

#define NUM_PBES_ON_PAGE(_page_size) (_page_size / sizeof(u64))
#define MAX_PBES_ON_PAGE NUM_PBES_ON_PAGE(MAX_FW_PBL_PAGE_SIZE)
#define MAX_PBES_TWO_LAYER (MAX_PBES_ON_PAGE * MAX_PBES_ON_PAGE)

static struct qedr_pbl *qedr_alloc_pbl_tbl(struct qedr_dev *dev,
					   struct qedr_pbl_info *pbl_info,
					   gfp_t flags)
{
	struct pci_dev *pdev = dev->pdev;
	struct qedr_pbl *pbl_table;
	dma_addr_t *pbl_main_tbl;
	dma_addr_t pa;
	void *va;
	int i;

	pbl_table = kcalloc(pbl_info->num_pbls, sizeof(*pbl_table), flags);
	if (!pbl_table)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < pbl_info->num_pbls; i++) {
		va = dma_alloc_coherent(&pdev->dev, pbl_info->pbl_size,
					&pa, flags);
		if (!va)
			goto err;

		memset(va, 0, pbl_info->pbl_size);
		pbl_table[i].va = va;
		pbl_table[i].pa = pa;
	}

	/* Two-Layer PBLs, if we have more than one pbl we need to initialize
	 * the first one with physical pointers to all of the rest
	 */
	pbl_main_tbl = (dma_addr_t *)pbl_table[0].va;
	for (i = 0; i < pbl_info->num_pbls - 1; i++)
		pbl_main_tbl[i] = pbl_table[i + 1].pa;

	return pbl_table;

err:
	for (i--; i >= 0; i--)
		dma_free_coherent(&pdev->dev, pbl_info->pbl_size,
				  pbl_table[i].va, pbl_table[i].pa);

	qedr_free_pbl(dev, pbl_info, pbl_table);

	return ERR_PTR(-ENOMEM);
}

static int qedr_prepare_pbl_tbl(struct qedr_dev *dev,
				struct qedr_pbl_info *pbl_info,
				u32 num_pbes, int two_layer_capable)
{
	u32 pbl_capacity;
	u32 pbl_size;
	u32 num_pbls;

	if ((num_pbes > MAX_PBES_ON_PAGE) && two_layer_capable) {
		if (num_pbes > MAX_PBES_TWO_LAYER) {
			DP_ERR(dev, "prepare pbl table: too many pages %d\n",
			       num_pbes);
			return -EINVAL;
		}

		/* calculate required pbl page size */
		pbl_size = MIN_FW_PBL_PAGE_SIZE;
		pbl_capacity = NUM_PBES_ON_PAGE(pbl_size) *
			       NUM_PBES_ON_PAGE(pbl_size);

		while (pbl_capacity < num_pbes) {
			pbl_size *= 2;
			pbl_capacity = pbl_size / sizeof(u64);
			pbl_capacity = pbl_capacity * pbl_capacity;
		}

		num_pbls = DIV_ROUND_UP(num_pbes, NUM_PBES_ON_PAGE(pbl_size));
		num_pbls++;	/* One for the layer0 ( points to the pbls) */
		pbl_info->two_layered = true;
	} else {
		/* One layered PBL */
		num_pbls = 1;
		pbl_size = max_t(u32, MIN_FW_PBL_PAGE_SIZE,
				 roundup_pow_of_two((num_pbes * sizeof(u64))));
		pbl_info->two_layered = false;
	}

	pbl_info->num_pbls = num_pbls;
	pbl_info->pbl_size = pbl_size;
	pbl_info->num_pbes = num_pbes;

	DP_DEBUG(dev, QEDR_MSG_MR,
		 "prepare pbl table: num_pbes=%d, num_pbls=%d, pbl_size=%d\n",
		 pbl_info->num_pbes, pbl_info->num_pbls, pbl_info->pbl_size);

	return 0;
}

static void qedr_populate_pbls(struct qedr_dev *dev, struct ib_umem *umem,
			       struct qedr_pbl *pbl,
			       struct qedr_pbl_info *pbl_info)
{
	int shift, pg_cnt, pages, pbe_cnt, total_num_pbes = 0;
	struct qedr_pbl *pbl_tbl;
	struct scatterlist *sg;
	struct regpair *pbe;
	int entry;
	u32 addr;

	if (!pbl_info->num_pbes)
		return;

	/* If we have a two layered pbl, the first pbl points to the rest
	 * of the pbls and the first entry lays on the second pbl in the table
	 */
	if (pbl_info->two_layered)
		pbl_tbl = &pbl[1];
	else
		pbl_tbl = pbl;

	pbe = (struct regpair *)pbl_tbl->va;
	if (!pbe) {
		DP_ERR(dev, "cannot populate PBL due to a NULL PBE\n");
		return;
	}

	pbe_cnt = 0;

	shift = ilog2(umem->page_size);

	for_each_sg(umem->sg_head.sgl, sg, umem->nmap, entry) {
		pages = sg_dma_len(sg) >> shift;
		for (pg_cnt = 0; pg_cnt < pages; pg_cnt++) {
			/* store the page address in pbe */
			pbe->lo = cpu_to_le32(sg_dma_address(sg) +
					      umem->page_size * pg_cnt);
			addr = upper_32_bits(sg_dma_address(sg) +
					     umem->page_size * pg_cnt);
			pbe->hi = cpu_to_le32(addr);
			pbe_cnt++;
			total_num_pbes++;
			pbe++;

			if (total_num_pbes == pbl_info->num_pbes)
				return;

			/* If the given pbl is full storing the pbes,
			 * move to next pbl.
			 */
			if (pbe_cnt == (pbl_info->pbl_size / sizeof(u64))) {
				pbl_tbl++;
				pbe = (struct regpair *)pbl_tbl->va;
				pbe_cnt = 0;
			}
		}
	}
}

static int qedr_copy_cq_uresp(struct qedr_dev *dev,
			      struct qedr_cq *cq, struct ib_udata *udata)
{
	struct qedr_create_cq_uresp uresp;
	int rc;

	memset(&uresp, 0, sizeof(uresp));

	uresp.db_offset = DB_ADDR_SHIFT(DQ_PWM_OFFSET_UCM_RDMA_CQ_CONS_32BIT);
	uresp.icid = cq->icid;

	rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (rc)
		DP_ERR(dev, "copy error cqid=0x%x.\n", cq->icid);

	return rc;
}

static void consume_cqe(struct qedr_cq *cq)
{
	if (cq->latest_cqe == cq->toggle_cqe)
		cq->pbl_toggle ^= RDMA_CQE_REQUESTER_TOGGLE_BIT_MASK;

	cq->latest_cqe = qed_chain_consume(&cq->pbl);
}

static inline int qedr_align_cq_entries(int entries)
{
	u64 size, aligned_size;

	/* We allocate an extra entry that we don't report to the FW. */
	size = (entries + 1) * QEDR_CQE_SIZE;
	aligned_size = ALIGN(size, PAGE_SIZE);

	return aligned_size / QEDR_CQE_SIZE;
}

static inline int qedr_init_user_queue(struct ib_ucontext *ib_ctx,
				       struct qedr_dev *dev,
				       struct qedr_userq *q,
				       u64 buf_addr, size_t buf_len,
				       int access, int dmasync)
{
	int page_cnt;
	int rc;

	q->buf_addr = buf_addr;
	q->buf_len = buf_len;
	q->umem = ib_umem_get(ib_ctx, q->buf_addr, q->buf_len, access, dmasync);
	if (IS_ERR(q->umem)) {
		DP_ERR(dev, "create user queue: failed ib_umem_get, got %ld\n",
		       PTR_ERR(q->umem));
		return PTR_ERR(q->umem);
	}

	page_cnt = ib_umem_page_count(q->umem);
	rc = qedr_prepare_pbl_tbl(dev, &q->pbl_info, page_cnt, 0);
	if (rc)
		goto err0;

	q->pbl_tbl = qedr_alloc_pbl_tbl(dev, &q->pbl_info, GFP_KERNEL);
	if (IS_ERR(q->pbl_tbl)) {
		rc = PTR_ERR(q->pbl_tbl);
		goto err0;
	}

	qedr_populate_pbls(dev, q->umem, q->pbl_tbl, &q->pbl_info);

	return 0;

err0:
	ib_umem_release(q->umem);

	return rc;
}

static inline void qedr_init_cq_params(struct qedr_cq *cq,
				       struct qedr_ucontext *ctx,
				       struct qedr_dev *dev, int vector,
				       int chain_entries, int page_cnt,
				       u64 pbl_ptr,
				       struct qed_rdma_create_cq_in_params
				       *params)
{
	memset(params, 0, sizeof(*params));
	params->cq_handle_hi = upper_32_bits((uintptr_t)cq);
	params->cq_handle_lo = lower_32_bits((uintptr_t)cq);
	params->cnq_id = vector;
	params->cq_size = chain_entries - 1;
	params->dpi = (ctx) ? ctx->dpi : dev->dpi;
	params->pbl_num_pages = page_cnt;
	params->pbl_ptr = pbl_ptr;
	params->pbl_two_level = 0;
}

static void doorbell_cq(struct qedr_cq *cq, u32 cons, u8 flags)
{
	/* Flush data before signalling doorbell */
	wmb();
	cq->db.data.agg_flags = flags;
	cq->db.data.value = cpu_to_le32(cons);
	writeq(cq->db.raw, cq->db_addr);

	/* Make sure write would stick */
	mmiowb();
}

int qedr_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct qedr_cq *cq = get_qedr_cq(ibcq);
	unsigned long sflags;

	if (cq->cq_type == QEDR_CQ_TYPE_GSI)
		return 0;

	spin_lock_irqsave(&cq->cq_lock, sflags);

	cq->arm_flags = 0;

	if (flags & IB_CQ_SOLICITED)
		cq->arm_flags |= DQ_UCM_ROCE_CQ_ARM_SE_CF_CMD;

	if (flags & IB_CQ_NEXT_COMP)
		cq->arm_flags |= DQ_UCM_ROCE_CQ_ARM_CF_CMD;

	doorbell_cq(cq, cq->cq_cons - 1, cq->arm_flags);

	spin_unlock_irqrestore(&cq->cq_lock, sflags);

	return 0;
}

struct ib_cq *qedr_create_cq(struct ib_device *ibdev,
			     const struct ib_cq_init_attr *attr,
			     struct ib_ucontext *ib_ctx, struct ib_udata *udata)
{
	struct qedr_ucontext *ctx = get_qedr_ucontext(ib_ctx);
	struct qed_rdma_destroy_cq_out_params destroy_oparams;
	struct qed_rdma_destroy_cq_in_params destroy_iparams;
	struct qedr_dev *dev = get_qedr_dev(ibdev);
	struct qed_rdma_create_cq_in_params params;
	struct qedr_create_cq_ureq ureq;
	int vector = attr->comp_vector;
	int entries = attr->cqe;
	struct qedr_cq *cq;
	int chain_entries;
	int page_cnt;
	u64 pbl_ptr;
	u16 icid;
	int rc;

	DP_DEBUG(dev, QEDR_MSG_INIT,
		 "create_cq: called from %s. entries=%d, vector=%d\n",
		 udata ? "User Lib" : "Kernel", entries, vector);

	if (entries > QEDR_MAX_CQES) {
		DP_ERR(dev,
		       "create cq: the number of entries %d is too high. Must be equal or below %d.\n",
		       entries, QEDR_MAX_CQES);
		return ERR_PTR(-EINVAL);
	}

	chain_entries = qedr_align_cq_entries(entries);
	chain_entries = min_t(int, chain_entries, QEDR_MAX_CQES);

	cq = kzalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq)
		return ERR_PTR(-ENOMEM);

	if (udata) {
		memset(&ureq, 0, sizeof(ureq));
		if (ib_copy_from_udata(&ureq, udata, sizeof(ureq))) {
			DP_ERR(dev,
			       "create cq: problem copying data from user space\n");
			goto err0;
		}

		if (!ureq.len) {
			DP_ERR(dev,
			       "create cq: cannot create a cq with 0 entries\n");
			goto err0;
		}

		cq->cq_type = QEDR_CQ_TYPE_USER;

		rc = qedr_init_user_queue(ib_ctx, dev, &cq->q, ureq.addr,
					  ureq.len, IB_ACCESS_LOCAL_WRITE, 1);
		if (rc)
			goto err0;

		pbl_ptr = cq->q.pbl_tbl->pa;
		page_cnt = cq->q.pbl_info.num_pbes;

		cq->ibcq.cqe = chain_entries;
	} else {
		cq->cq_type = QEDR_CQ_TYPE_KERNEL;

		rc = dev->ops->common->chain_alloc(dev->cdev,
						   QED_CHAIN_USE_TO_CONSUME,
						   QED_CHAIN_MODE_PBL,
						   QED_CHAIN_CNT_TYPE_U32,
						   chain_entries,
						   sizeof(union rdma_cqe),
						   &cq->pbl);
		if (rc)
			goto err1;

		page_cnt = qed_chain_get_page_cnt(&cq->pbl);
		pbl_ptr = qed_chain_get_pbl_phys(&cq->pbl);
		cq->ibcq.cqe = cq->pbl.capacity;
	}

	qedr_init_cq_params(cq, ctx, dev, vector, chain_entries, page_cnt,
			    pbl_ptr, &params);

	rc = dev->ops->rdma_create_cq(dev->rdma_ctx, &params, &icid);
	if (rc)
		goto err2;

	cq->icid = icid;
	cq->sig = QEDR_CQ_MAGIC_NUMBER;
	spin_lock_init(&cq->cq_lock);

	if (ib_ctx) {
		rc = qedr_copy_cq_uresp(dev, cq, udata);
		if (rc)
			goto err3;
	} else {
		/* Generate doorbell address. */
		cq->db_addr = dev->db_addr +
		    DB_ADDR_SHIFT(DQ_PWM_OFFSET_UCM_RDMA_CQ_CONS_32BIT);
		cq->db.data.icid = cq->icid;
		cq->db.data.params = DB_AGG_CMD_SET <<
		    RDMA_PWM_VAL32_DATA_AGG_CMD_SHIFT;

		/* point to the very last element, passing it we will toggle */
		cq->toggle_cqe = qed_chain_get_last_elem(&cq->pbl);
		cq->pbl_toggle = RDMA_CQE_REQUESTER_TOGGLE_BIT_MASK;
		cq->latest_cqe = NULL;
		consume_cqe(cq);
		cq->cq_cons = qed_chain_get_cons_idx_u32(&cq->pbl);
	}

	DP_DEBUG(dev, QEDR_MSG_CQ,
		 "create cq: icid=0x%0x, addr=%p, size(entries)=0x%0x\n",
		 cq->icid, cq, params.cq_size);

	return &cq->ibcq;

err3:
	destroy_iparams.icid = cq->icid;
	dev->ops->rdma_destroy_cq(dev->rdma_ctx, &destroy_iparams,
				  &destroy_oparams);
err2:
	if (udata)
		qedr_free_pbl(dev, &cq->q.pbl_info, cq->q.pbl_tbl);
	else
		dev->ops->common->chain_free(dev->cdev, &cq->pbl);
err1:
	if (udata)
		ib_umem_release(cq->q.umem);
err0:
	kfree(cq);
	return ERR_PTR(-EINVAL);
}

int qedr_resize_cq(struct ib_cq *ibcq, int new_cnt, struct ib_udata *udata)
{
	struct qedr_dev *dev = get_qedr_dev(ibcq->device);
	struct qedr_cq *cq = get_qedr_cq(ibcq);

	DP_ERR(dev, "cq %p RESIZE NOT SUPPORTED\n", cq);

	return 0;
}

int qedr_destroy_cq(struct ib_cq *ibcq)
{
	struct qedr_dev *dev = get_qedr_dev(ibcq->device);
	struct qed_rdma_destroy_cq_out_params oparams;
	struct qed_rdma_destroy_cq_in_params iparams;
	struct qedr_cq *cq = get_qedr_cq(ibcq);

	DP_DEBUG(dev, QEDR_MSG_CQ, "destroy cq: cq_id %d", cq->icid);

	/* GSIs CQs are handled by driver, so they don't exist in the FW */
	if (cq->cq_type != QEDR_CQ_TYPE_GSI) {
		int rc;

		iparams.icid = cq->icid;
		rc = dev->ops->rdma_destroy_cq(dev->rdma_ctx, &iparams,
					       &oparams);
		if (rc)
			return rc;
		dev->ops->common->chain_free(dev->cdev, &cq->pbl);
	}

	if (ibcq->uobject && ibcq->uobject->context) {
		qedr_free_pbl(dev, &cq->q.pbl_info, cq->q.pbl_tbl);
		ib_umem_release(cq->q.umem);
	}

	kfree(cq);

	return 0;
}

static inline int get_gid_info_from_table(struct ib_qp *ibqp,
					  struct ib_qp_attr *attr,
					  int attr_mask,
					  struct qed_rdma_modify_qp_in_params
					  *qp_params)
{
	enum rdma_network_type nw_type;
	struct ib_gid_attr gid_attr;
	union ib_gid gid;
	u32 ipv4_addr;
	int rc = 0;
	int i;

	rc = ib_get_cached_gid(ibqp->device, attr->ah_attr.port_num,
			       attr->ah_attr.grh.sgid_index, &gid, &gid_attr);
	if (rc)
		return rc;

	if (!memcmp(&gid, &zgid, sizeof(gid)))
		return -ENOENT;

	if (gid_attr.ndev) {
		qp_params->vlan_id = rdma_vlan_dev_vlan_id(gid_attr.ndev);

		dev_put(gid_attr.ndev);
		nw_type = ib_gid_to_network_type(gid_attr.gid_type, &gid);
		switch (nw_type) {
		case RDMA_NETWORK_IPV6:
			memcpy(&qp_params->sgid.bytes[0], &gid.raw[0],
			       sizeof(qp_params->sgid));
			memcpy(&qp_params->dgid.bytes[0],
			       &attr->ah_attr.grh.dgid,
			       sizeof(qp_params->dgid));
			qp_params->roce_mode = ROCE_V2_IPV6;
			SET_FIELD(qp_params->modify_flags,
				  QED_ROCE_MODIFY_QP_VALID_ROCE_MODE, 1);
			break;
		case RDMA_NETWORK_IB:
			memcpy(&qp_params->sgid.bytes[0], &gid.raw[0],
			       sizeof(qp_params->sgid));
			memcpy(&qp_params->dgid.bytes[0],
			       &attr->ah_attr.grh.dgid,
			       sizeof(qp_params->dgid));
			qp_params->roce_mode = ROCE_V1;
			break;
		case RDMA_NETWORK_IPV4:
			memset(&qp_params->sgid, 0, sizeof(qp_params->sgid));
			memset(&qp_params->dgid, 0, sizeof(qp_params->dgid));
			ipv4_addr = qedr_get_ipv4_from_gid(gid.raw);
			qp_params->sgid.ipv4_addr = ipv4_addr;
			ipv4_addr =
			    qedr_get_ipv4_from_gid(attr->ah_attr.grh.dgid.raw);
			qp_params->dgid.ipv4_addr = ipv4_addr;
			SET_FIELD(qp_params->modify_flags,
				  QED_ROCE_MODIFY_QP_VALID_ROCE_MODE, 1);
			qp_params->roce_mode = ROCE_V2_IPV4;
			break;
		}
	}

	for (i = 0; i < 4; i++) {
		qp_params->sgid.dwords[i] = ntohl(qp_params->sgid.dwords[i]);
		qp_params->dgid.dwords[i] = ntohl(qp_params->dgid.dwords[i]);
	}

	if (qp_params->vlan_id >= VLAN_CFI_MASK)
		qp_params->vlan_id = 0;

	return 0;
}

static int qedr_check_qp_attrs(struct ib_pd *ibpd, struct qedr_dev *dev,
			       struct ib_qp_init_attr *attrs)
{
	struct qedr_device_attr *qattr = &dev->attr;

	/* QP0... attrs->qp_type == IB_QPT_GSI */
	if (attrs->qp_type != IB_QPT_RC && attrs->qp_type != IB_QPT_GSI) {
		DP_DEBUG(dev, QEDR_MSG_QP,
			 "create qp: unsupported qp type=0x%x requested\n",
			 attrs->qp_type);
		return -EINVAL;
	}

	if (attrs->cap.max_send_wr > qattr->max_sqe) {
		DP_ERR(dev,
		       "create qp: cannot create a SQ with %d elements (max_send_wr=0x%x)\n",
		       attrs->cap.max_send_wr, qattr->max_sqe);
		return -EINVAL;
	}

	if (attrs->cap.max_inline_data > qattr->max_inline) {
		DP_ERR(dev,
		       "create qp: unsupported inline data size=0x%x requested (max_inline=0x%x)\n",
		       attrs->cap.max_inline_data, qattr->max_inline);
		return -EINVAL;
	}

	if (attrs->cap.max_send_sge > qattr->max_sge) {
		DP_ERR(dev,
		       "create qp: unsupported send_sge=0x%x requested (max_send_sge=0x%x)\n",
		       attrs->cap.max_send_sge, qattr->max_sge);
		return -EINVAL;
	}

	if (attrs->cap.max_recv_sge > qattr->max_sge) {
		DP_ERR(dev,
		       "create qp: unsupported recv_sge=0x%x requested (max_recv_sge=0x%x)\n",
		       attrs->cap.max_recv_sge, qattr->max_sge);
		return -EINVAL;
	}

	/* Unprivileged user space cannot create special QP */
	if (ibpd->uobject && attrs->qp_type == IB_QPT_GSI) {
		DP_ERR(dev,
		       "create qp: userspace can't create special QPs of type=0x%x\n",
		       attrs->qp_type);
		return -EINVAL;
	}

	return 0;
}

static void qedr_copy_rq_uresp(struct qedr_create_qp_uresp *uresp,
			       struct qedr_qp *qp)
{
	uresp->rq_db_offset = DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_ROCE_RQ_PROD);
	uresp->rq_icid = qp->icid;
}

static void qedr_copy_sq_uresp(struct qedr_create_qp_uresp *uresp,
			       struct qedr_qp *qp)
{
	uresp->sq_db_offset = DB_ADDR_SHIFT(DQ_PWM_OFFSET_XCM_RDMA_SQ_PROD);
	uresp->sq_icid = qp->icid + 1;
}

static int qedr_copy_qp_uresp(struct qedr_dev *dev,
			      struct qedr_qp *qp, struct ib_udata *udata)
{
	struct qedr_create_qp_uresp uresp;
	int rc;

	memset(&uresp, 0, sizeof(uresp));
	qedr_copy_sq_uresp(&uresp, qp);
	qedr_copy_rq_uresp(&uresp, qp);

	uresp.atomic_supported = dev->atomic_cap != IB_ATOMIC_NONE;
	uresp.qp_id = qp->qp_id;

	rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (rc)
		DP_ERR(dev,
		       "create qp: failed a copy to user space with qp icid=0x%x.\n",
		       qp->icid);

	return rc;
}

static void qedr_set_common_qp_params(struct qedr_dev *dev,
				      struct qedr_qp *qp,
				      struct qedr_pd *pd,
				      struct ib_qp_init_attr *attrs)
{
	spin_lock_init(&qp->q_lock);
	qp->pd = pd;
	qp->qp_type = attrs->qp_type;
	qp->max_inline_data = attrs->cap.max_inline_data;
	qp->sq.max_sges = attrs->cap.max_send_sge;
	qp->state = QED_ROCE_QP_STATE_RESET;
	qp->signaled = (attrs->sq_sig_type == IB_SIGNAL_ALL_WR) ? true : false;
	qp->sq_cq = get_qedr_cq(attrs->send_cq);
	qp->rq_cq = get_qedr_cq(attrs->recv_cq);
	qp->dev = dev;
	qp->rq.max_sges = attrs->cap.max_recv_sge;

	DP_DEBUG(dev, QEDR_MSG_QP,
		 "RQ params:\trq_max_sges = %d, rq_cq_id = %d\n",
		 qp->rq.max_sges, qp->rq_cq->icid);
	DP_DEBUG(dev, QEDR_MSG_QP,
		 "QP params:\tpd = %d, qp_type = %d, max_inline_data = %d, state = %d, signaled = %d, use_srq=%d\n",
		 pd->pd_id, qp->qp_type, qp->max_inline_data,
		 qp->state, qp->signaled, (attrs->srq) ? 1 : 0);
	DP_DEBUG(dev, QEDR_MSG_QP,
		 "SQ params:\tsq_max_sges = %d, sq_cq_id = %d\n",
		 qp->sq.max_sges, qp->sq_cq->icid);
}

static void qedr_set_roce_db_info(struct qedr_dev *dev, struct qedr_qp *qp)
{
	qp->sq.db = dev->db_addr +
		    DB_ADDR_SHIFT(DQ_PWM_OFFSET_XCM_RDMA_SQ_PROD);
	qp->sq.db_data.data.icid = qp->icid + 1;
	qp->rq.db = dev->db_addr +
		    DB_ADDR_SHIFT(DQ_PWM_OFFSET_TCM_ROCE_RQ_PROD);
	qp->rq.db_data.data.icid = qp->icid;
}

static inline void
qedr_init_common_qp_in_params(struct qedr_dev *dev,
			      struct qedr_pd *pd,
			      struct qedr_qp *qp,
			      struct ib_qp_init_attr *attrs,
			      bool fmr_and_reserved_lkey,
			      struct qed_rdma_create_qp_in_params *params)
{
	/* QP handle to be written in an async event */
	params->qp_handle_async_lo = lower_32_bits((uintptr_t) qp);
	params->qp_handle_async_hi = upper_32_bits((uintptr_t) qp);

	params->signal_all = (attrs->sq_sig_type == IB_SIGNAL_ALL_WR);
	params->fmr_and_reserved_lkey = fmr_and_reserved_lkey;
	params->pd = pd->pd_id;
	params->dpi = pd->uctx ? pd->uctx->dpi : dev->dpi;
	params->sq_cq_id = get_qedr_cq(attrs->send_cq)->icid;
	params->stats_queue = 0;
	params->rq_cq_id = get_qedr_cq(attrs->recv_cq)->icid;
	params->srq_id = 0;
	params->use_srq = false;
}

static inline void qedr_qp_user_print(struct qedr_dev *dev, struct qedr_qp *qp)
{
	DP_DEBUG(dev, QEDR_MSG_QP, "create qp: successfully created user QP. "
		 "qp=%p. "
		 "sq_addr=0x%llx, "
		 "sq_len=%zd, "
		 "rq_addr=0x%llx, "
		 "rq_len=%zd"
		 "\n",
		 qp,
		 qp->usq.buf_addr,
		 qp->usq.buf_len, qp->urq.buf_addr, qp->urq.buf_len);
}

static void qedr_cleanup_user(struct qedr_dev *dev, struct qedr_qp *qp)
{
	if (qp->usq.umem)
		ib_umem_release(qp->usq.umem);
	qp->usq.umem = NULL;

	if (qp->urq.umem)
		ib_umem_release(qp->urq.umem);
	qp->urq.umem = NULL;
}

static int qedr_create_user_qp(struct qedr_dev *dev,
			       struct qedr_qp *qp,
			       struct ib_pd *ibpd,
			       struct ib_udata *udata,
			       struct ib_qp_init_attr *attrs)
{
	struct qed_rdma_create_qp_in_params in_params;
	struct qed_rdma_create_qp_out_params out_params;
	struct qedr_pd *pd = get_qedr_pd(ibpd);
	struct ib_ucontext *ib_ctx = NULL;
	struct qedr_ucontext *ctx = NULL;
	struct qedr_create_qp_ureq ureq;
	int rc = -EINVAL;

	ib_ctx = ibpd->uobject->context;
	ctx = get_qedr_ucontext(ib_ctx);

	memset(&ureq, 0, sizeof(ureq));
	rc = ib_copy_from_udata(&ureq, udata, sizeof(ureq));
	if (rc) {
		DP_ERR(dev, "Problem copying data from user space\n");
		return rc;
	}

	/* SQ - read access only (0), dma sync not required (0) */
	rc = qedr_init_user_queue(ib_ctx, dev, &qp->usq, ureq.sq_addr,
				  ureq.sq_len, 0, 0);
	if (rc)
		return rc;

	/* RQ - read access only (0), dma sync not required (0) */
	rc = qedr_init_user_queue(ib_ctx, dev, &qp->urq, ureq.rq_addr,
				  ureq.rq_len, 0, 0);

	if (rc)
		return rc;

	memset(&in_params, 0, sizeof(in_params));
	qedr_init_common_qp_in_params(dev, pd, qp, attrs, false, &in_params);
	in_params.qp_handle_lo = ureq.qp_handle_lo;
	in_params.qp_handle_hi = ureq.qp_handle_hi;
	in_params.sq_num_pages = qp->usq.pbl_info.num_pbes;
	in_params.sq_pbl_ptr = qp->usq.pbl_tbl->pa;
	in_params.rq_num_pages = qp->urq.pbl_info.num_pbes;
	in_params.rq_pbl_ptr = qp->urq.pbl_tbl->pa;

	qp->qed_qp = dev->ops->rdma_create_qp(dev->rdma_ctx,
					      &in_params, &out_params);

	if (!qp->qed_qp) {
		rc = -ENOMEM;
		goto err1;
	}

	qp->qp_id = out_params.qp_id;
	qp->icid = out_params.icid;

	rc = qedr_copy_qp_uresp(dev, qp, udata);
	if (rc)
		goto err;

	qedr_qp_user_print(dev, qp);

	return 0;
err:
	rc = dev->ops->rdma_destroy_qp(dev->rdma_ctx, qp->qed_qp);
	if (rc)
		DP_ERR(dev, "create qp: fatal fault. rc=%d", rc);

err1:
	qedr_cleanup_user(dev, qp);
	return rc;
}

static int
qedr_roce_create_kernel_qp(struct qedr_dev *dev,
			   struct qedr_qp *qp,
			   struct qed_rdma_create_qp_in_params *in_params,
			   u32 n_sq_elems, u32 n_rq_elems)
{
	struct qed_rdma_create_qp_out_params out_params;
	int rc;

	rc = dev->ops->common->chain_alloc(dev->cdev,
					   QED_CHAIN_USE_TO_PRODUCE,
					   QED_CHAIN_MODE_PBL,
					   QED_CHAIN_CNT_TYPE_U32,
					   n_sq_elems,
					   QEDR_SQE_ELEMENT_SIZE,
					   &qp->sq.pbl);

	if (rc)
		return rc;

	in_params->sq_num_pages = qed_chain_get_page_cnt(&qp->sq.pbl);
	in_params->sq_pbl_ptr = qed_chain_get_pbl_phys(&qp->sq.pbl);

	rc = dev->ops->common->chain_alloc(dev->cdev,
					   QED_CHAIN_USE_TO_CONSUME_PRODUCE,
					   QED_CHAIN_MODE_PBL,
					   QED_CHAIN_CNT_TYPE_U32,
					   n_rq_elems,
					   QEDR_RQE_ELEMENT_SIZE,
					   &qp->rq.pbl);
	if (rc)
		return rc;

	in_params->rq_num_pages = qed_chain_get_page_cnt(&qp->rq.pbl);
	in_params->rq_pbl_ptr = qed_chain_get_pbl_phys(&qp->rq.pbl);

	qp->qed_qp = dev->ops->rdma_create_qp(dev->rdma_ctx,
					      in_params, &out_params);

	if (!qp->qed_qp)
		return -EINVAL;

	qp->qp_id = out_params.qp_id;
	qp->icid = out_params.icid;

	qedr_set_roce_db_info(dev, qp);

	return 0;
}

static void qedr_cleanup_kernel(struct qedr_dev *dev, struct qedr_qp *qp)
{
	dev->ops->common->chain_free(dev->cdev, &qp->sq.pbl);
	kfree(qp->wqe_wr_id);

	dev->ops->common->chain_free(dev->cdev, &qp->rq.pbl);
	kfree(qp->rqe_wr_id);
}

static int qedr_create_kernel_qp(struct qedr_dev *dev,
				 struct qedr_qp *qp,
				 struct ib_pd *ibpd,
				 struct ib_qp_init_attr *attrs)
{
	struct qed_rdma_create_qp_in_params in_params;
	struct qedr_pd *pd = get_qedr_pd(ibpd);
	int rc = -EINVAL;
	u32 n_rq_elems;
	u32 n_sq_elems;
	u32 n_sq_entries;

	memset(&in_params, 0, sizeof(in_params));

	/* A single work request may take up to QEDR_MAX_SQ_WQE_SIZE elements in
	 * the ring. The ring should allow at least a single WR, even if the
	 * user requested none, due to allocation issues.
	 * We should add an extra WR since the prod and cons indices of
	 * wqe_wr_id are managed in such a way that the WQ is considered full
	 * when (prod+1)%max_wr==cons. We currently don't do that because we
	 * double the number of entries due an iSER issue that pushes far more
	 * WRs than indicated. If we decline its ib_post_send() then we get
	 * error prints in the dmesg we'd like to avoid.
	 */
	qp->sq.max_wr = min_t(u32, attrs->cap.max_send_wr * dev->wq_multiplier,
			      dev->attr.max_sqe);

	qp->wqe_wr_id = kzalloc(qp->sq.max_wr * sizeof(*qp->wqe_wr_id),
				GFP_KERNEL);
	if (!qp->wqe_wr_id) {
		DP_ERR(dev, "create qp: failed SQ shadow memory allocation\n");
		return -ENOMEM;
	}

	/* QP handle to be written in CQE */
	in_params.qp_handle_lo = lower_32_bits((uintptr_t) qp);
	in_params.qp_handle_hi = upper_32_bits((uintptr_t) qp);

	/* A single work request may take up to QEDR_MAX_RQ_WQE_SIZE elements in
	 * the ring. There ring should allow at least a single WR, even if the
	 * user requested none, due to allocation issues.
	 */
	qp->rq.max_wr = (u16) max_t(u32, attrs->cap.max_recv_wr, 1);

	/* Allocate driver internal RQ array */
	qp->rqe_wr_id = kzalloc(qp->rq.max_wr * sizeof(*qp->rqe_wr_id),
				GFP_KERNEL);
	if (!qp->rqe_wr_id) {
		DP_ERR(dev,
		       "create qp: failed RQ shadow memory allocation\n");
		kfree(qp->wqe_wr_id);
		return -ENOMEM;
	}

	qedr_init_common_qp_in_params(dev, pd, qp, attrs, true, &in_params);

	n_sq_entries = attrs->cap.max_send_wr;
	n_sq_entries = min_t(u32, n_sq_entries, dev->attr.max_sqe);
	n_sq_entries = max_t(u32, n_sq_entries, 1);
	n_sq_elems = n_sq_entries * QEDR_MAX_SQE_ELEMENTS_PER_SQE;

	n_rq_elems = qp->rq.max_wr * QEDR_MAX_RQE_ELEMENTS_PER_RQE;

	rc = qedr_roce_create_kernel_qp(dev, qp, &in_params,
					n_sq_elems, n_rq_elems);
	if (rc)
		qedr_cleanup_kernel(dev, qp);

	return rc;
}

struct ib_qp *qedr_create_qp(struct ib_pd *ibpd,
			     struct ib_qp_init_attr *attrs,
			     struct ib_udata *udata)
{
	struct qedr_dev *dev = get_qedr_dev(ibpd->device);
	struct qedr_pd *pd = get_qedr_pd(ibpd);
	struct qedr_qp *qp;
	struct ib_qp *ibqp;
	int rc = 0;

	DP_DEBUG(dev, QEDR_MSG_QP, "create qp: called from %s, pd=%p\n",
		 udata ? "user library" : "kernel", pd);

	rc = qedr_check_qp_attrs(ibpd, dev, attrs);
	if (rc)
		return ERR_PTR(rc);

	if (attrs->srq)
		return ERR_PTR(-EINVAL);

	DP_DEBUG(dev, QEDR_MSG_QP,
		 "create qp: called from %s, event_handler=%p, eepd=%p sq_cq=%p, sq_icid=%d, rq_cq=%p, rq_icid=%d\n",
		 udata ? "user library" : "kernel", attrs->event_handler, pd,
		 get_qedr_cq(attrs->send_cq),
		 get_qedr_cq(attrs->send_cq)->icid,
		 get_qedr_cq(attrs->recv_cq),
		 get_qedr_cq(attrs->recv_cq)->icid);

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp) {
		DP_ERR(dev, "create qp: failed allocating memory\n");
		return ERR_PTR(-ENOMEM);
	}

	qedr_set_common_qp_params(dev, qp, pd, attrs);

	if (attrs->qp_type == IB_QPT_GSI) {
		ibqp = qedr_create_gsi_qp(dev, attrs, qp);
		if (IS_ERR(ibqp))
			kfree(qp);
		return ibqp;
	}

	if (udata)
		rc = qedr_create_user_qp(dev, qp, ibpd, udata, attrs);
	else
		rc = qedr_create_kernel_qp(dev, qp, ibpd, attrs);

	if (rc)
		goto err;

	qp->ibqp.qp_num = qp->qp_id;

	return &qp->ibqp;

err:
	kfree(qp);

	return ERR_PTR(-EFAULT);
}

static enum ib_qp_state qedr_get_ibqp_state(enum qed_roce_qp_state qp_state)
{
	switch (qp_state) {
	case QED_ROCE_QP_STATE_RESET:
		return IB_QPS_RESET;
	case QED_ROCE_QP_STATE_INIT:
		return IB_QPS_INIT;
	case QED_ROCE_QP_STATE_RTR:
		return IB_QPS_RTR;
	case QED_ROCE_QP_STATE_RTS:
		return IB_QPS_RTS;
	case QED_ROCE_QP_STATE_SQD:
		return IB_QPS_SQD;
	case QED_ROCE_QP_STATE_ERR:
		return IB_QPS_ERR;
	case QED_ROCE_QP_STATE_SQE:
		return IB_QPS_SQE;
	}
	return IB_QPS_ERR;
}

static enum qed_roce_qp_state qedr_get_state_from_ibqp(
					enum ib_qp_state qp_state)
{
	switch (qp_state) {
	case IB_QPS_RESET:
		return QED_ROCE_QP_STATE_RESET;
	case IB_QPS_INIT:
		return QED_ROCE_QP_STATE_INIT;
	case IB_QPS_RTR:
		return QED_ROCE_QP_STATE_RTR;
	case IB_QPS_RTS:
		return QED_ROCE_QP_STATE_RTS;
	case IB_QPS_SQD:
		return QED_ROCE_QP_STATE_SQD;
	case IB_QPS_ERR:
		return QED_ROCE_QP_STATE_ERR;
	default:
		return QED_ROCE_QP_STATE_ERR;
	}
}

static void qedr_reset_qp_hwq_info(struct qedr_qp_hwq_info *qph)
{
	qed_chain_reset(&qph->pbl);
	qph->prod = 0;
	qph->cons = 0;
	qph->wqe_cons = 0;
	qph->db_data.data.value = cpu_to_le16(0);
}

static int qedr_update_qp_state(struct qedr_dev *dev,
				struct qedr_qp *qp,
				enum qed_roce_qp_state new_state)
{
	int status = 0;

	if (new_state == qp->state)
		return 0;

	switch (qp->state) {
	case QED_ROCE_QP_STATE_RESET:
		switch (new_state) {
		case QED_ROCE_QP_STATE_INIT:
			qp->prev_wqe_size = 0;
			qedr_reset_qp_hwq_info(&qp->sq);
			qedr_reset_qp_hwq_info(&qp->rq);
			break;
		default:
			status = -EINVAL;
			break;
		};
		break;
	case QED_ROCE_QP_STATE_INIT:
		switch (new_state) {
		case QED_ROCE_QP_STATE_RTR:
			/* Update doorbell (in case post_recv was
			 * done before move to RTR)
			 */
			wmb();
			writel(qp->rq.db_data.raw, qp->rq.db);
			/* Make sure write takes effect */
			mmiowb();
			break;
		case QED_ROCE_QP_STATE_ERR:
			break;
		default:
			/* Invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case QED_ROCE_QP_STATE_RTR:
		/* RTR->XXX */
		switch (new_state) {
		case QED_ROCE_QP_STATE_RTS:
			break;
		case QED_ROCE_QP_STATE_ERR:
			break;
		default:
			/* Invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case QED_ROCE_QP_STATE_RTS:
		/* RTS->XXX */
		switch (new_state) {
		case QED_ROCE_QP_STATE_SQD:
			break;
		case QED_ROCE_QP_STATE_ERR:
			break;
		default:
			/* Invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case QED_ROCE_QP_STATE_SQD:
		/* SQD->XXX */
		switch (new_state) {
		case QED_ROCE_QP_STATE_RTS:
		case QED_ROCE_QP_STATE_ERR:
			break;
		default:
			/* Invalid state change. */
			status = -EINVAL;
			break;
		};
		break;
	case QED_ROCE_QP_STATE_ERR:
		/* ERR->XXX */
		switch (new_state) {
		case QED_ROCE_QP_STATE_RESET:
			if ((qp->rq.prod != qp->rq.cons) ||
			    (qp->sq.prod != qp->sq.cons)) {
				DP_NOTICE(dev,
					  "Error->Reset with rq/sq not empty rq.prod=%x rq.cons=%x sq.prod=%x sq.cons=%x\n",
					  qp->rq.prod, qp->rq.cons, qp->sq.prod,
					  qp->sq.cons);
				status = -EINVAL;
			}
			break;
		default:
			status = -EINVAL;
			break;
		};
		break;
	default:
		status = -EINVAL;
		break;
	};

	return status;
}

int qedr_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		   int attr_mask, struct ib_udata *udata)
{
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	struct qed_rdma_modify_qp_in_params qp_params = { 0 };
	struct qedr_dev *dev = get_qedr_dev(&qp->dev->ibdev);
	enum ib_qp_state old_qp_state, new_qp_state;
	int rc = 0;

	DP_DEBUG(dev, QEDR_MSG_QP,
		 "modify qp: qp %p attr_mask=0x%x, state=%d", qp, attr_mask,
		 attr->qp_state);

	old_qp_state = qedr_get_ibqp_state(qp->state);
	if (attr_mask & IB_QP_STATE)
		new_qp_state = attr->qp_state;
	else
		new_qp_state = old_qp_state;

	if (!ib_modify_qp_is_ok
	    (old_qp_state, new_qp_state, ibqp->qp_type, attr_mask,
	     IB_LINK_LAYER_ETHERNET)) {
		DP_ERR(dev,
		       "modify qp: invalid attribute mask=0x%x specified for\n"
		       "qpn=0x%x of type=0x%x old_qp_state=0x%x, new_qp_state=0x%x\n",
		       attr_mask, qp->qp_id, ibqp->qp_type, old_qp_state,
		       new_qp_state);
		rc = -EINVAL;
		goto err;
	}

	/* Translate the masks... */
	if (attr_mask & IB_QP_STATE) {
		SET_FIELD(qp_params.modify_flags,
			  QED_RDMA_MODIFY_QP_VALID_NEW_STATE, 1);
		qp_params.new_state = qedr_get_state_from_ibqp(attr->qp_state);
	}

	if (attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY)
		qp_params.sqd_async = true;

	if (attr_mask & IB_QP_PKEY_INDEX) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_PKEY, 1);
		if (attr->pkey_index >= QEDR_ROCE_PKEY_TABLE_LEN) {
			rc = -EINVAL;
			goto err;
		}

		qp_params.pkey = QEDR_ROCE_PKEY_DEFAULT;
	}

	if (attr_mask & IB_QP_QKEY)
		qp->qkey = attr->qkey;

	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		SET_FIELD(qp_params.modify_flags,
			  QED_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN, 1);
		qp_params.incoming_rdma_read_en = attr->qp_access_flags &
						  IB_ACCESS_REMOTE_READ;
		qp_params.incoming_rdma_write_en = attr->qp_access_flags &
						   IB_ACCESS_REMOTE_WRITE;
		qp_params.incoming_atomic_en = attr->qp_access_flags &
					       IB_ACCESS_REMOTE_ATOMIC;
	}

	if (attr_mask & (IB_QP_AV | IB_QP_PATH_MTU)) {
		if (attr_mask & IB_QP_PATH_MTU) {
			if (attr->path_mtu < IB_MTU_256 ||
			    attr->path_mtu > IB_MTU_4096) {
				pr_err("error: Only MTU sizes of 256, 512, 1024, 2048 and 4096 are supported by RoCE\n");
				rc = -EINVAL;
				goto err;
			}
			qp->mtu = min(ib_mtu_enum_to_int(attr->path_mtu),
				      ib_mtu_enum_to_int(iboe_get_mtu
							 (dev->ndev->mtu)));
		}

		if (!qp->mtu) {
			qp->mtu =
			ib_mtu_enum_to_int(iboe_get_mtu(dev->ndev->mtu));
			pr_err("Fixing zeroed MTU to qp->mtu = %d\n", qp->mtu);
		}

		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR, 1);

		qp_params.traffic_class_tos = attr->ah_attr.grh.traffic_class;
		qp_params.flow_label = attr->ah_attr.grh.flow_label;
		qp_params.hop_limit_ttl = attr->ah_attr.grh.hop_limit;

		qp->sgid_idx = attr->ah_attr.grh.sgid_index;

		rc = get_gid_info_from_table(ibqp, attr, attr_mask, &qp_params);
		if (rc) {
			DP_ERR(dev,
			       "modify qp: problems with GID index %d (rc=%d)\n",
			       attr->ah_attr.grh.sgid_index, rc);
			return rc;
		}

		rc = qedr_get_dmac(dev, &attr->ah_attr,
				   qp_params.remote_mac_addr);
		if (rc)
			return rc;

		qp_params.use_local_mac = true;
		ether_addr_copy(qp_params.local_mac_addr, dev->ndev->dev_addr);

		DP_DEBUG(dev, QEDR_MSG_QP, "dgid=%x:%x:%x:%x\n",
			 qp_params.dgid.dwords[0], qp_params.dgid.dwords[1],
			 qp_params.dgid.dwords[2], qp_params.dgid.dwords[3]);
		DP_DEBUG(dev, QEDR_MSG_QP, "sgid=%x:%x:%x:%x\n",
			 qp_params.sgid.dwords[0], qp_params.sgid.dwords[1],
			 qp_params.sgid.dwords[2], qp_params.sgid.dwords[3]);
		DP_DEBUG(dev, QEDR_MSG_QP, "remote_mac=[%pM]\n",
			 qp_params.remote_mac_addr);

		qp_params.mtu = qp->mtu;
		qp_params.lb_indication = false;
	}

	if (!qp_params.mtu) {
		/* Stay with current MTU */
		if (qp->mtu)
			qp_params.mtu = qp->mtu;
		else
			qp_params.mtu =
			    ib_mtu_enum_to_int(iboe_get_mtu(dev->ndev->mtu));
	}

	if (attr_mask & IB_QP_TIMEOUT) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT, 1);

		qp_params.ack_timeout = attr->timeout;
		if (attr->timeout) {
			u32 temp;

			temp = 4096 * (1UL << attr->timeout) / 1000 / 1000;
			/* FW requires [msec] */
			qp_params.ack_timeout = temp;
		} else {
			/* Infinite */
			qp_params.ack_timeout = 0;
		}
	}
	if (attr_mask & IB_QP_RETRY_CNT) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_RETRY_CNT, 1);
		qp_params.retry_cnt = attr->retry_cnt;
	}

	if (attr_mask & IB_QP_RNR_RETRY) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT, 1);
		qp_params.rnr_retry_cnt = attr->rnr_retry;
	}

	if (attr_mask & IB_QP_RQ_PSN) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_RQ_PSN, 1);
		qp_params.rq_psn = attr->rq_psn;
		qp->rq_psn = attr->rq_psn;
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		if (attr->max_rd_atomic > dev->attr.max_qp_req_rd_atomic_resc) {
			rc = -EINVAL;
			DP_ERR(dev,
			       "unsupported max_rd_atomic=%d, supported=%d\n",
			       attr->max_rd_atomic,
			       dev->attr.max_qp_req_rd_atomic_resc);
			goto err;
		}

		SET_FIELD(qp_params.modify_flags,
			  QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ, 1);
		qp_params.max_rd_atomic_req = attr->max_rd_atomic;
	}

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER, 1);
		qp_params.min_rnr_nak_timer = attr->min_rnr_timer;
	}

	if (attr_mask & IB_QP_SQ_PSN) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_SQ_PSN, 1);
		qp_params.sq_psn = attr->sq_psn;
		qp->sq_psn = attr->sq_psn;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		if (attr->max_dest_rd_atomic >
		    dev->attr.max_qp_resp_rd_atomic_resc) {
			DP_ERR(dev,
			       "unsupported max_dest_rd_atomic=%d, supported=%d\n",
			       attr->max_dest_rd_atomic,
			       dev->attr.max_qp_resp_rd_atomic_resc);

			rc = -EINVAL;
			goto err;
		}

		SET_FIELD(qp_params.modify_flags,
			  QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP, 1);
		qp_params.max_rd_atomic_resp = attr->max_dest_rd_atomic;
	}

	if (attr_mask & IB_QP_DEST_QPN) {
		SET_FIELD(qp_params.modify_flags,
			  QED_ROCE_MODIFY_QP_VALID_DEST_QP, 1);

		qp_params.dest_qp = attr->dest_qp_num;
		qp->dest_qp_num = attr->dest_qp_num;
	}

	if (qp->qp_type != IB_QPT_GSI)
		rc = dev->ops->rdma_modify_qp(dev->rdma_ctx,
					      qp->qed_qp, &qp_params);

	if (attr_mask & IB_QP_STATE) {
		if ((qp->qp_type != IB_QPT_GSI) && (!udata))
			rc = qedr_update_qp_state(dev, qp, qp_params.new_state);
		qp->state = qp_params.new_state;
	}

err:
	return rc;
}

static int qedr_to_ib_qp_acc_flags(struct qed_rdma_query_qp_out_params *params)
{
	int ib_qp_acc_flags = 0;

	if (params->incoming_rdma_write_en)
		ib_qp_acc_flags |= IB_ACCESS_REMOTE_WRITE;
	if (params->incoming_rdma_read_en)
		ib_qp_acc_flags |= IB_ACCESS_REMOTE_READ;
	if (params->incoming_atomic_en)
		ib_qp_acc_flags |= IB_ACCESS_REMOTE_ATOMIC;
	ib_qp_acc_flags |= IB_ACCESS_LOCAL_WRITE;
	return ib_qp_acc_flags;
}

int qedr_query_qp(struct ib_qp *ibqp,
		  struct ib_qp_attr *qp_attr,
		  int attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct qed_rdma_query_qp_out_params params;
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	struct qedr_dev *dev = qp->dev;
	int rc = 0;

	memset(&params, 0, sizeof(params));

	rc = dev->ops->rdma_query_qp(dev->rdma_ctx, qp->qed_qp, &params);
	if (rc)
		goto err;

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	qp_attr->qp_state = qedr_get_ibqp_state(params.state);
	qp_attr->cur_qp_state = qedr_get_ibqp_state(params.state);
	qp_attr->path_mtu = ib_mtu_int_to_enum(params.mtu);
	qp_attr->path_mig_state = IB_MIG_MIGRATED;
	qp_attr->rq_psn = params.rq_psn;
	qp_attr->sq_psn = params.sq_psn;
	qp_attr->dest_qp_num = params.dest_qp;

	qp_attr->qp_access_flags = qedr_to_ib_qp_acc_flags(&params);

	qp_attr->cap.max_send_wr = qp->sq.max_wr;
	qp_attr->cap.max_recv_wr = qp->rq.max_wr;
	qp_attr->cap.max_send_sge = qp->sq.max_sges;
	qp_attr->cap.max_recv_sge = qp->rq.max_sges;
	qp_attr->cap.max_inline_data = ROCE_REQ_MAX_INLINE_DATA_SIZE;
	qp_init_attr->cap = qp_attr->cap;

	memcpy(&qp_attr->ah_attr.grh.dgid.raw[0], &params.dgid.bytes[0],
	       sizeof(qp_attr->ah_attr.grh.dgid.raw));

	qp_attr->ah_attr.grh.flow_label = params.flow_label;
	qp_attr->ah_attr.grh.sgid_index = qp->sgid_idx;
	qp_attr->ah_attr.grh.hop_limit = params.hop_limit_ttl;
	qp_attr->ah_attr.grh.traffic_class = params.traffic_class_tos;

	qp_attr->ah_attr.ah_flags = IB_AH_GRH;
	qp_attr->ah_attr.port_num = 1;
	qp_attr->ah_attr.sl = 0;
	qp_attr->timeout = params.timeout;
	qp_attr->rnr_retry = params.rnr_retry;
	qp_attr->retry_cnt = params.retry_cnt;
	qp_attr->min_rnr_timer = params.min_rnr_nak_timer;
	qp_attr->pkey_index = params.pkey_index;
	qp_attr->port_num = 1;
	qp_attr->ah_attr.src_path_bits = 0;
	qp_attr->ah_attr.static_rate = 0;
	qp_attr->alt_pkey_index = 0;
	qp_attr->alt_port_num = 0;
	qp_attr->alt_timeout = 0;
	memset(&qp_attr->alt_ah_attr, 0, sizeof(qp_attr->alt_ah_attr));

	qp_attr->sq_draining = (params.state == QED_ROCE_QP_STATE_SQD) ? 1 : 0;
	qp_attr->max_dest_rd_atomic = params.max_dest_rd_atomic;
	qp_attr->max_rd_atomic = params.max_rd_atomic;
	qp_attr->en_sqd_async_notify = (params.sqd_async) ? 1 : 0;

	DP_DEBUG(dev, QEDR_MSG_QP, "QEDR_QUERY_QP: max_inline_data=%d\n",
		 qp_attr->cap.max_inline_data);

err:
	return rc;
}

int qedr_free_qp_resources(struct qedr_dev *dev, struct qedr_qp *qp)
{
	int rc = 0;

	if (qp->qp_type != IB_QPT_GSI) {
		rc = dev->ops->rdma_destroy_qp(dev->rdma_ctx, qp->qed_qp);
		if (rc)
			return rc;
	}

	if (qp->ibqp.uobject && qp->ibqp.uobject->context)
		qedr_cleanup_user(dev, qp);
	else
		qedr_cleanup_kernel(dev, qp);

	return 0;
}

int qedr_destroy_qp(struct ib_qp *ibqp)
{
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	struct qedr_dev *dev = qp->dev;
	struct ib_qp_attr attr;
	int attr_mask = 0;
	int rc = 0;

	DP_DEBUG(dev, QEDR_MSG_QP, "destroy qp: destroying %p, qp type=%d\n",
		 qp, qp->qp_type);

	if ((qp->state != QED_ROCE_QP_STATE_RESET) &&
	    (qp->state != QED_ROCE_QP_STATE_ERR) &&
	    (qp->state != QED_ROCE_QP_STATE_INIT)) {

		attr.qp_state = IB_QPS_ERR;
		attr_mask |= IB_QP_STATE;

		/* Change the QP state to ERROR */
		qedr_modify_qp(ibqp, &attr, attr_mask, NULL);
	}

	if (qp->qp_type == IB_QPT_GSI)
		qedr_destroy_gsi_qp(dev);

	qedr_free_qp_resources(dev, qp);

	kfree(qp);

	return rc;
}

struct ib_ah *qedr_create_ah(struct ib_pd *ibpd, struct ib_ah_attr *attr,
			     struct ib_udata *udata)
{
	struct qedr_ah *ah;

	ah = kzalloc(sizeof(*ah), GFP_ATOMIC);
	if (!ah)
		return ERR_PTR(-ENOMEM);

	ah->attr = *attr;

	return &ah->ibah;
}

int qedr_destroy_ah(struct ib_ah *ibah)
{
	struct qedr_ah *ah = get_qedr_ah(ibah);

	kfree(ah);
	return 0;
}

static void free_mr_info(struct qedr_dev *dev, struct mr_info *info)
{
	struct qedr_pbl *pbl, *tmp;

	if (info->pbl_table)
		list_add_tail(&info->pbl_table->list_entry,
			      &info->free_pbl_list);

	if (!list_empty(&info->inuse_pbl_list))
		list_splice(&info->inuse_pbl_list, &info->free_pbl_list);

	list_for_each_entry_safe(pbl, tmp, &info->free_pbl_list, list_entry) {
		list_del(&pbl->list_entry);
		qedr_free_pbl(dev, &info->pbl_info, pbl);
	}
}

static int init_mr_info(struct qedr_dev *dev, struct mr_info *info,
			size_t page_list_len, bool two_layered)
{
	struct qedr_pbl *tmp;
	int rc;

	INIT_LIST_HEAD(&info->free_pbl_list);
	INIT_LIST_HEAD(&info->inuse_pbl_list);

	rc = qedr_prepare_pbl_tbl(dev, &info->pbl_info,
				  page_list_len, two_layered);
	if (rc)
		goto done;

	info->pbl_table = qedr_alloc_pbl_tbl(dev, &info->pbl_info, GFP_KERNEL);
	if (IS_ERR(info->pbl_table)) {
		rc = PTR_ERR(info->pbl_table);
		goto done;
	}

	DP_DEBUG(dev, QEDR_MSG_MR, "pbl_table_pa = %pa\n",
		 &info->pbl_table->pa);

	/* in usual case we use 2 PBLs, so we add one to free
	 * list and allocating another one
	 */
	tmp = qedr_alloc_pbl_tbl(dev, &info->pbl_info, GFP_KERNEL);
	if (IS_ERR(tmp)) {
		DP_DEBUG(dev, QEDR_MSG_MR, "Extra PBL is not allocated\n");
		goto done;
	}

	list_add_tail(&tmp->list_entry, &info->free_pbl_list);

	DP_DEBUG(dev, QEDR_MSG_MR, "extra pbl_table_pa = %pa\n", &tmp->pa);

done:
	if (rc)
		free_mr_info(dev, info);

	return rc;
}

struct ib_mr *qedr_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
			       u64 usr_addr, int acc, struct ib_udata *udata)
{
	struct qedr_dev *dev = get_qedr_dev(ibpd->device);
	struct qedr_mr *mr;
	struct qedr_pd *pd;
	int rc = -ENOMEM;

	pd = get_qedr_pd(ibpd);
	DP_DEBUG(dev, QEDR_MSG_MR,
		 "qedr_register user mr pd = %d start = %lld, len = %lld, usr_addr = %lld, acc = %d\n",
		 pd->pd_id, start, len, usr_addr, acc);

	if (acc & IB_ACCESS_REMOTE_WRITE && !(acc & IB_ACCESS_LOCAL_WRITE))
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(rc);

	mr->type = QEDR_MR_USER;

	mr->umem = ib_umem_get(ibpd->uobject->context, start, len, acc, 0);
	if (IS_ERR(mr->umem)) {
		rc = -EFAULT;
		goto err0;
	}

	rc = init_mr_info(dev, &mr->info, ib_umem_page_count(mr->umem), 1);
	if (rc)
		goto err1;

	qedr_populate_pbls(dev, mr->umem, mr->info.pbl_table,
			   &mr->info.pbl_info);

	rc = dev->ops->rdma_alloc_tid(dev->rdma_ctx, &mr->hw_mr.itid);
	if (rc) {
		DP_ERR(dev, "roce alloc tid returned an error %d\n", rc);
		goto err1;
	}

	/* Index only, 18 bit long, lkey = itid << 8 | key */
	mr->hw_mr.tid_type = QED_RDMA_TID_REGISTERED_MR;
	mr->hw_mr.key = 0;
	mr->hw_mr.pd = pd->pd_id;
	mr->hw_mr.local_read = 1;
	mr->hw_mr.local_write = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hw_mr.remote_read = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hw_mr.remote_write = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hw_mr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	mr->hw_mr.mw_bind = false;
	mr->hw_mr.pbl_ptr = mr->info.pbl_table[0].pa;
	mr->hw_mr.pbl_two_level = mr->info.pbl_info.two_layered;
	mr->hw_mr.pbl_page_size_log = ilog2(mr->info.pbl_info.pbl_size);
	mr->hw_mr.page_size_log = ilog2(mr->umem->page_size);
	mr->hw_mr.fbo = ib_umem_offset(mr->umem);
	mr->hw_mr.length = len;
	mr->hw_mr.vaddr = usr_addr;
	mr->hw_mr.zbva = false;
	mr->hw_mr.phy_mr = false;
	mr->hw_mr.dma_mr = false;

	rc = dev->ops->rdma_register_tid(dev->rdma_ctx, &mr->hw_mr);
	if (rc) {
		DP_ERR(dev, "roce register tid returned an error %d\n", rc);
		goto err2;
	}

	mr->ibmr.lkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;
	if (mr->hw_mr.remote_write || mr->hw_mr.remote_read ||
	    mr->hw_mr.remote_atomic)
		mr->ibmr.rkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;

	DP_DEBUG(dev, QEDR_MSG_MR, "register user mr lkey: %x\n",
		 mr->ibmr.lkey);
	return &mr->ibmr;

err2:
	dev->ops->rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);
err1:
	qedr_free_pbl(dev, &mr->info.pbl_info, mr->info.pbl_table);
err0:
	kfree(mr);
	return ERR_PTR(rc);
}

int qedr_dereg_mr(struct ib_mr *ib_mr)
{
	struct qedr_mr *mr = get_qedr_mr(ib_mr);
	struct qedr_dev *dev = get_qedr_dev(ib_mr->device);
	int rc = 0;

	rc = dev->ops->rdma_deregister_tid(dev->rdma_ctx, mr->hw_mr.itid);
	if (rc)
		return rc;

	dev->ops->rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);

	if ((mr->type != QEDR_MR_DMA) && (mr->type != QEDR_MR_FRMR))
		qedr_free_pbl(dev, &mr->info.pbl_info, mr->info.pbl_table);

	/* it could be user registered memory. */
	if (mr->umem)
		ib_umem_release(mr->umem);

	kfree(mr);

	return rc;
}

static struct qedr_mr *__qedr_alloc_mr(struct ib_pd *ibpd,
				       int max_page_list_len)
{
	struct qedr_pd *pd = get_qedr_pd(ibpd);
	struct qedr_dev *dev = get_qedr_dev(ibpd->device);
	struct qedr_mr *mr;
	int rc = -ENOMEM;

	DP_DEBUG(dev, QEDR_MSG_MR,
		 "qedr_alloc_frmr pd = %d max_page_list_len= %d\n", pd->pd_id,
		 max_page_list_len);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(rc);

	mr->dev = dev;
	mr->type = QEDR_MR_FRMR;

	rc = init_mr_info(dev, &mr->info, max_page_list_len, 1);
	if (rc)
		goto err0;

	rc = dev->ops->rdma_alloc_tid(dev->rdma_ctx, &mr->hw_mr.itid);
	if (rc) {
		DP_ERR(dev, "roce alloc tid returned an error %d\n", rc);
		goto err0;
	}

	/* Index only, 18 bit long, lkey = itid << 8 | key */
	mr->hw_mr.tid_type = QED_RDMA_TID_FMR;
	mr->hw_mr.key = 0;
	mr->hw_mr.pd = pd->pd_id;
	mr->hw_mr.local_read = 1;
	mr->hw_mr.local_write = 0;
	mr->hw_mr.remote_read = 0;
	mr->hw_mr.remote_write = 0;
	mr->hw_mr.remote_atomic = 0;
	mr->hw_mr.mw_bind = false;
	mr->hw_mr.pbl_ptr = 0;
	mr->hw_mr.pbl_two_level = mr->info.pbl_info.two_layered;
	mr->hw_mr.pbl_page_size_log = ilog2(mr->info.pbl_info.pbl_size);
	mr->hw_mr.fbo = 0;
	mr->hw_mr.length = 0;
	mr->hw_mr.vaddr = 0;
	mr->hw_mr.zbva = false;
	mr->hw_mr.phy_mr = true;
	mr->hw_mr.dma_mr = false;

	rc = dev->ops->rdma_register_tid(dev->rdma_ctx, &mr->hw_mr);
	if (rc) {
		DP_ERR(dev, "roce register tid returned an error %d\n", rc);
		goto err1;
	}

	mr->ibmr.lkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;
	mr->ibmr.rkey = mr->ibmr.lkey;

	DP_DEBUG(dev, QEDR_MSG_MR, "alloc frmr: %x\n", mr->ibmr.lkey);
	return mr;

err1:
	dev->ops->rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);
err0:
	kfree(mr);
	return ERR_PTR(rc);
}

struct ib_mr *qedr_alloc_mr(struct ib_pd *ibpd,
			    enum ib_mr_type mr_type, u32 max_num_sg)
{
	struct qedr_dev *dev;
	struct qedr_mr *mr;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EINVAL);

	mr = __qedr_alloc_mr(ibpd, max_num_sg);

	if (IS_ERR(mr))
		return ERR_PTR(-EINVAL);

	dev = mr->dev;

	return &mr->ibmr;
}

static int qedr_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct qedr_mr *mr = get_qedr_mr(ibmr);
	struct qedr_pbl *pbl_table;
	struct regpair *pbe;
	u32 pbes_in_page;

	if (unlikely(mr->npages == mr->info.pbl_info.num_pbes)) {
		DP_ERR(mr->dev, "qedr_set_page failes when %d\n", mr->npages);
		return -ENOMEM;
	}

	DP_DEBUG(mr->dev, QEDR_MSG_MR, "qedr_set_page pages[%d] = 0x%llx\n",
		 mr->npages, addr);

	pbes_in_page = mr->info.pbl_info.pbl_size / sizeof(u64);
	pbl_table = mr->info.pbl_table + (mr->npages / pbes_in_page);
	pbe = (struct regpair *)pbl_table->va;
	pbe +=  mr->npages % pbes_in_page;
	pbe->lo = cpu_to_le32((u32)addr);
	pbe->hi = cpu_to_le32((u32)upper_32_bits(addr));

	mr->npages++;

	return 0;
}

static void handle_completed_mrs(struct qedr_dev *dev, struct mr_info *info)
{
	int work = info->completed - info->completed_handled - 1;

	DP_DEBUG(dev, QEDR_MSG_MR, "Special FMR work = %d\n", work);
	while (work-- > 0 && !list_empty(&info->inuse_pbl_list)) {
		struct qedr_pbl *pbl;

		/* Free all the page list that are possible to be freed
		 * (all the ones that were invalidated), under the assumption
		 * that if an FMR was completed successfully that means that
		 * if there was an invalidate operation before it also ended
		 */
		pbl = list_first_entry(&info->inuse_pbl_list,
				       struct qedr_pbl, list_entry);
		list_move_tail(&pbl->list_entry, &info->free_pbl_list);
		info->completed_handled++;
	}
}

int qedr_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		   int sg_nents, unsigned int *sg_offset)
{
	struct qedr_mr *mr = get_qedr_mr(ibmr);

	mr->npages = 0;

	handle_completed_mrs(mr->dev, &mr->info);
	return ib_sg_to_pages(ibmr, sg, sg_nents, NULL, qedr_set_page);
}

struct ib_mr *qedr_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	struct qedr_dev *dev = get_qedr_dev(ibpd->device);
	struct qedr_pd *pd = get_qedr_pd(ibpd);
	struct qedr_mr *mr;
	int rc;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->type = QEDR_MR_DMA;

	rc = dev->ops->rdma_alloc_tid(dev->rdma_ctx, &mr->hw_mr.itid);
	if (rc) {
		DP_ERR(dev, "roce alloc tid returned an error %d\n", rc);
		goto err1;
	}

	/* index only, 18 bit long, lkey = itid << 8 | key */
	mr->hw_mr.tid_type = QED_RDMA_TID_REGISTERED_MR;
	mr->hw_mr.pd = pd->pd_id;
	mr->hw_mr.local_read = 1;
	mr->hw_mr.local_write = (acc & IB_ACCESS_LOCAL_WRITE) ? 1 : 0;
	mr->hw_mr.remote_read = (acc & IB_ACCESS_REMOTE_READ) ? 1 : 0;
	mr->hw_mr.remote_write = (acc & IB_ACCESS_REMOTE_WRITE) ? 1 : 0;
	mr->hw_mr.remote_atomic = (acc & IB_ACCESS_REMOTE_ATOMIC) ? 1 : 0;
	mr->hw_mr.dma_mr = true;

	rc = dev->ops->rdma_register_tid(dev->rdma_ctx, &mr->hw_mr);
	if (rc) {
		DP_ERR(dev, "roce register tid returned an error %d\n", rc);
		goto err2;
	}

	mr->ibmr.lkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;
	if (mr->hw_mr.remote_write || mr->hw_mr.remote_read ||
	    mr->hw_mr.remote_atomic)
		mr->ibmr.rkey = mr->hw_mr.itid << 8 | mr->hw_mr.key;

	DP_DEBUG(dev, QEDR_MSG_MR, "get dma mr: lkey = %x\n", mr->ibmr.lkey);
	return &mr->ibmr;

err2:
	dev->ops->rdma_free_tid(dev->rdma_ctx, mr->hw_mr.itid);
err1:
	kfree(mr);
	return ERR_PTR(rc);
}

static inline int qedr_wq_is_full(struct qedr_qp_hwq_info *wq)
{
	return (((wq->prod + 1) % wq->max_wr) == wq->cons);
}

static int sge_data_len(struct ib_sge *sg_list, int num_sge)
{
	int i, len = 0;

	for (i = 0; i < num_sge; i++)
		len += sg_list[i].length;

	return len;
}

static void swap_wqe_data64(u64 *p)
{
	int i;

	for (i = 0; i < QEDR_SQE_ELEMENT_SIZE / sizeof(u64); i++, p++)
		*p = cpu_to_be64(cpu_to_le64(*p));
}

static u32 qedr_prepare_sq_inline_data(struct qedr_dev *dev,
				       struct qedr_qp *qp, u8 *wqe_size,
				       struct ib_send_wr *wr,
				       struct ib_send_wr **bad_wr, u8 *bits,
				       u8 bit)
{
	u32 data_size = sge_data_len(wr->sg_list, wr->num_sge);
	char *seg_prt, *wqe;
	int i, seg_siz;

	if (data_size > ROCE_REQ_MAX_INLINE_DATA_SIZE) {
		DP_ERR(dev, "Too much inline data in WR: %d\n", data_size);
		*bad_wr = wr;
		return 0;
	}

	if (!data_size)
		return data_size;

	*bits |= bit;

	seg_prt = NULL;
	wqe = NULL;
	seg_siz = 0;

	/* Copy data inline */
	for (i = 0; i < wr->num_sge; i++) {
		u32 len = wr->sg_list[i].length;
		void *src = (void *)(uintptr_t)wr->sg_list[i].addr;

		while (len > 0) {
			u32 cur;

			/* New segment required */
			if (!seg_siz) {
				wqe = (char *)qed_chain_produce(&qp->sq.pbl);
				seg_prt = wqe;
				seg_siz = sizeof(struct rdma_sq_common_wqe);
				(*wqe_size)++;
			}

			/* Calculate currently allowed length */
			cur = min_t(u32, len, seg_siz);
			memcpy(seg_prt, src, cur);

			/* Update segment variables */
			seg_prt += cur;
			seg_siz -= cur;

			/* Update sge variables */
			src += cur;
			len -= cur;

			/* Swap fully-completed segments */
			if (!seg_siz)
				swap_wqe_data64((u64 *)wqe);
		}
	}

	/* swap last not completed segment */
	if (seg_siz)
		swap_wqe_data64((u64 *)wqe);

	return data_size;
}

#define RQ_SGE_SET(sge, vaddr, vlength, vflags)			\
	do {							\
		DMA_REGPAIR_LE(sge->addr, vaddr);		\
		(sge)->length = cpu_to_le32(vlength);		\
		(sge)->flags = cpu_to_le32(vflags);		\
	} while (0)

#define SRQ_HDR_SET(hdr, vwr_id, num_sge)			\
	do {							\
		DMA_REGPAIR_LE(hdr->wr_id, vwr_id);		\
		(hdr)->num_sges = num_sge;			\
	} while (0)

#define SRQ_SGE_SET(sge, vaddr, vlength, vlkey)			\
	do {							\
		DMA_REGPAIR_LE(sge->addr, vaddr);		\
		(sge)->length = cpu_to_le32(vlength);		\
		(sge)->l_key = cpu_to_le32(vlkey);		\
	} while (0)

static u32 qedr_prepare_sq_sges(struct qedr_qp *qp, u8 *wqe_size,
				struct ib_send_wr *wr)
{
	u32 data_size = 0;
	int i;

	for (i = 0; i < wr->num_sge; i++) {
		struct rdma_sq_sge *sge = qed_chain_produce(&qp->sq.pbl);

		DMA_REGPAIR_LE(sge->addr, wr->sg_list[i].addr);
		sge->l_key = cpu_to_le32(wr->sg_list[i].lkey);
		sge->length = cpu_to_le32(wr->sg_list[i].length);
		data_size += wr->sg_list[i].length;
	}

	if (wqe_size)
		*wqe_size += wr->num_sge;

	return data_size;
}

static u32 qedr_prepare_sq_rdma_data(struct qedr_dev *dev,
				     struct qedr_qp *qp,
				     struct rdma_sq_rdma_wqe_1st *rwqe,
				     struct rdma_sq_rdma_wqe_2nd *rwqe2,
				     struct ib_send_wr *wr,
				     struct ib_send_wr **bad_wr)
{
	rwqe2->r_key = cpu_to_le32(rdma_wr(wr)->rkey);
	DMA_REGPAIR_LE(rwqe2->remote_va, rdma_wr(wr)->remote_addr);

	if (wr->send_flags & IB_SEND_INLINE &&
	    (wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM ||
	     wr->opcode == IB_WR_RDMA_WRITE)) {
		u8 flags = 0;

		SET_FIELD2(flags, RDMA_SQ_RDMA_WQE_1ST_INLINE_FLG, 1);
		return qedr_prepare_sq_inline_data(dev, qp, &rwqe->wqe_size, wr,
						   bad_wr, &rwqe->flags, flags);
	}

	return qedr_prepare_sq_sges(qp, &rwqe->wqe_size, wr);
}

static u32 qedr_prepare_sq_send_data(struct qedr_dev *dev,
				     struct qedr_qp *qp,
				     struct rdma_sq_send_wqe_1st *swqe,
				     struct rdma_sq_send_wqe_2st *swqe2,
				     struct ib_send_wr *wr,
				     struct ib_send_wr **bad_wr)
{
	memset(swqe2, 0, sizeof(*swqe2));
	if (wr->send_flags & IB_SEND_INLINE) {
		u8 flags = 0;

		SET_FIELD2(flags, RDMA_SQ_SEND_WQE_INLINE_FLG, 1);
		return qedr_prepare_sq_inline_data(dev, qp, &swqe->wqe_size, wr,
						   bad_wr, &swqe->flags, flags);
	}

	return qedr_prepare_sq_sges(qp, &swqe->wqe_size, wr);
}

static int qedr_prepare_reg(struct qedr_qp *qp,
			    struct rdma_sq_fmr_wqe_1st *fwqe1,
			    struct ib_reg_wr *wr)
{
	struct qedr_mr *mr = get_qedr_mr(wr->mr);
	struct rdma_sq_fmr_wqe_2nd *fwqe2;

	fwqe2 = (struct rdma_sq_fmr_wqe_2nd *)qed_chain_produce(&qp->sq.pbl);
	fwqe1->addr.hi = upper_32_bits(mr->ibmr.iova);
	fwqe1->addr.lo = lower_32_bits(mr->ibmr.iova);
	fwqe1->l_key = wr->key;

	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_REMOTE_READ,
		   !!(wr->access & IB_ACCESS_REMOTE_READ));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_REMOTE_WRITE,
		   !!(wr->access & IB_ACCESS_REMOTE_WRITE));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_ENABLE_ATOMIC,
		   !!(wr->access & IB_ACCESS_REMOTE_ATOMIC));
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_LOCAL_READ, 1);
	SET_FIELD2(fwqe2->access_ctrl, RDMA_SQ_FMR_WQE_2ND_LOCAL_WRITE,
		   !!(wr->access & IB_ACCESS_LOCAL_WRITE));
	fwqe2->fmr_ctrl = 0;

	SET_FIELD2(fwqe2->fmr_ctrl, RDMA_SQ_FMR_WQE_2ND_PAGE_SIZE_LOG,
		   ilog2(mr->ibmr.page_size) - 12);

	fwqe2->length_hi = 0;
	fwqe2->length_lo = mr->ibmr.length;
	fwqe2->pbl_addr.hi = upper_32_bits(mr->info.pbl_table->pa);
	fwqe2->pbl_addr.lo = lower_32_bits(mr->info.pbl_table->pa);

	qp->wqe_wr_id[qp->sq.prod].mr = mr;

	return 0;
}

static enum ib_wc_opcode qedr_ib_to_wc_opcode(enum ib_wr_opcode opcode)
{
	switch (opcode) {
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		return IB_WC_RDMA_WRITE;
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_INV:
		return IB_WC_SEND;
	case IB_WR_RDMA_READ:
		return IB_WC_RDMA_READ;
	case IB_WR_ATOMIC_CMP_AND_SWP:
		return IB_WC_COMP_SWAP;
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		return IB_WC_FETCH_ADD;
	case IB_WR_REG_MR:
		return IB_WC_REG_MR;
	case IB_WR_LOCAL_INV:
		return IB_WC_LOCAL_INV;
	default:
		return IB_WC_SEND;
	}
}

static inline bool qedr_can_post_send(struct qedr_qp *qp, struct ib_send_wr *wr)
{
	int wq_is_full, err_wr, pbl_is_full;
	struct qedr_dev *dev = qp->dev;

	/* prevent SQ overflow and/or processing of a bad WR */
	err_wr = wr->num_sge > qp->sq.max_sges;
	wq_is_full = qedr_wq_is_full(&qp->sq);
	pbl_is_full = qed_chain_get_elem_left_u32(&qp->sq.pbl) <
		      QEDR_MAX_SQE_ELEMENTS_PER_SQE;
	if (wq_is_full || err_wr || pbl_is_full) {
		if (wq_is_full && !(qp->err_bitmap & QEDR_QP_ERR_SQ_FULL)) {
			DP_ERR(dev,
			       "error: WQ is full. Post send on QP %p failed (this error appears only once)\n",
			       qp);
			qp->err_bitmap |= QEDR_QP_ERR_SQ_FULL;
		}

		if (err_wr && !(qp->err_bitmap & QEDR_QP_ERR_BAD_SR)) {
			DP_ERR(dev,
			       "error: WR is bad. Post send on QP %p failed (this error appears only once)\n",
			       qp);
			qp->err_bitmap |= QEDR_QP_ERR_BAD_SR;
		}

		if (pbl_is_full &&
		    !(qp->err_bitmap & QEDR_QP_ERR_SQ_PBL_FULL)) {
			DP_ERR(dev,
			       "error: WQ PBL is full. Post send on QP %p failed (this error appears only once)\n",
			       qp);
			qp->err_bitmap |= QEDR_QP_ERR_SQ_PBL_FULL;
		}
		return false;
	}
	return true;
}

static int __qedr_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		     struct ib_send_wr **bad_wr)
{
	struct qedr_dev *dev = get_qedr_dev(ibqp->device);
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	struct rdma_sq_atomic_wqe_1st *awqe1;
	struct rdma_sq_atomic_wqe_2nd *awqe2;
	struct rdma_sq_atomic_wqe_3rd *awqe3;
	struct rdma_sq_send_wqe_2st *swqe2;
	struct rdma_sq_local_inv_wqe *iwqe;
	struct rdma_sq_rdma_wqe_2nd *rwqe2;
	struct rdma_sq_send_wqe_1st *swqe;
	struct rdma_sq_rdma_wqe_1st *rwqe;
	struct rdma_sq_fmr_wqe_1st *fwqe1;
	struct rdma_sq_common_wqe *wqe;
	u32 length;
	int rc = 0;
	bool comp;

	if (!qedr_can_post_send(qp, wr)) {
		*bad_wr = wr;
		return -ENOMEM;
	}

	wqe = qed_chain_produce(&qp->sq.pbl);
	qp->wqe_wr_id[qp->sq.prod].signaled =
		!!(wr->send_flags & IB_SEND_SIGNALED) || qp->signaled;

	wqe->flags = 0;
	SET_FIELD2(wqe->flags, RDMA_SQ_SEND_WQE_SE_FLG,
		   !!(wr->send_flags & IB_SEND_SOLICITED));
	comp = (!!(wr->send_flags & IB_SEND_SIGNALED)) || qp->signaled;
	SET_FIELD2(wqe->flags, RDMA_SQ_SEND_WQE_COMP_FLG, comp);
	SET_FIELD2(wqe->flags, RDMA_SQ_SEND_WQE_RD_FENCE_FLG,
		   !!(wr->send_flags & IB_SEND_FENCE));
	wqe->prev_wqe_size = qp->prev_wqe_size;

	qp->wqe_wr_id[qp->sq.prod].opcode = qedr_ib_to_wc_opcode(wr->opcode);

	switch (wr->opcode) {
	case IB_WR_SEND_WITH_IMM:
		wqe->req_type = RDMA_SQ_REQ_TYPE_SEND_WITH_IMM;
		swqe = (struct rdma_sq_send_wqe_1st *)wqe;
		swqe->wqe_size = 2;
		swqe2 = qed_chain_produce(&qp->sq.pbl);

		swqe->inv_key_or_imm_data = cpu_to_le32(wr->ex.imm_data);
		length = qedr_prepare_sq_send_data(dev, qp, swqe, swqe2,
						   wr, bad_wr);
		swqe->length = cpu_to_le32(length);
		qp->wqe_wr_id[qp->sq.prod].wqe_size = swqe->wqe_size;
		qp->prev_wqe_size = swqe->wqe_size;
		qp->wqe_wr_id[qp->sq.prod].bytes_len = swqe->length;
		break;
	case IB_WR_SEND:
		wqe->req_type = RDMA_SQ_REQ_TYPE_SEND;
		swqe = (struct rdma_sq_send_wqe_1st *)wqe;

		swqe->wqe_size = 2;
		swqe2 = qed_chain_produce(&qp->sq.pbl);
		length = qedr_prepare_sq_send_data(dev, qp, swqe, swqe2,
						   wr, bad_wr);
		swqe->length = cpu_to_le32(length);
		qp->wqe_wr_id[qp->sq.prod].wqe_size = swqe->wqe_size;
		qp->prev_wqe_size = swqe->wqe_size;
		qp->wqe_wr_id[qp->sq.prod].bytes_len = swqe->length;
		break;
	case IB_WR_SEND_WITH_INV:
		wqe->req_type = RDMA_SQ_REQ_TYPE_SEND_WITH_INVALIDATE;
		swqe = (struct rdma_sq_send_wqe_1st *)wqe;
		swqe2 = qed_chain_produce(&qp->sq.pbl);
		swqe->wqe_size = 2;
		swqe->inv_key_or_imm_data = cpu_to_le32(wr->ex.invalidate_rkey);
		length = qedr_prepare_sq_send_data(dev, qp, swqe, swqe2,
						   wr, bad_wr);
		swqe->length = cpu_to_le32(length);
		qp->wqe_wr_id[qp->sq.prod].wqe_size = swqe->wqe_size;
		qp->prev_wqe_size = swqe->wqe_size;
		qp->wqe_wr_id[qp->sq.prod].bytes_len = swqe->length;
		break;

	case IB_WR_RDMA_WRITE_WITH_IMM:
		wqe->req_type = RDMA_SQ_REQ_TYPE_RDMA_WR_WITH_IMM;
		rwqe = (struct rdma_sq_rdma_wqe_1st *)wqe;

		rwqe->wqe_size = 2;
		rwqe->imm_data = htonl(cpu_to_le32(wr->ex.imm_data));
		rwqe2 = qed_chain_produce(&qp->sq.pbl);
		length = qedr_prepare_sq_rdma_data(dev, qp, rwqe, rwqe2,
						   wr, bad_wr);
		rwqe->length = cpu_to_le32(length);
		qp->wqe_wr_id[qp->sq.prod].wqe_size = rwqe->wqe_size;
		qp->prev_wqe_size = rwqe->wqe_size;
		qp->wqe_wr_id[qp->sq.prod].bytes_len = rwqe->length;
		break;
	case IB_WR_RDMA_WRITE:
		wqe->req_type = RDMA_SQ_REQ_TYPE_RDMA_WR;
		rwqe = (struct rdma_sq_rdma_wqe_1st *)wqe;

		rwqe->wqe_size = 2;
		rwqe2 = qed_chain_produce(&qp->sq.pbl);
		length = qedr_prepare_sq_rdma_data(dev, qp, rwqe, rwqe2,
						   wr, bad_wr);
		rwqe->length = cpu_to_le32(length);
		qp->wqe_wr_id[qp->sq.prod].wqe_size = rwqe->wqe_size;
		qp->prev_wqe_size = rwqe->wqe_size;
		qp->wqe_wr_id[qp->sq.prod].bytes_len = rwqe->length;
		break;
	case IB_WR_RDMA_READ_WITH_INV:
		DP_ERR(dev,
		       "RDMA READ WITH INVALIDATE not supported\n");
		*bad_wr = wr;
		rc = -EINVAL;
		break;

	case IB_WR_RDMA_READ:
		wqe->req_type = RDMA_SQ_REQ_TYPE_RDMA_RD;
		rwqe = (struct rdma_sq_rdma_wqe_1st *)wqe;

		rwqe->wqe_size = 2;
		rwqe2 = qed_chain_produce(&qp->sq.pbl);
		length = qedr_prepare_sq_rdma_data(dev, qp, rwqe, rwqe2,
						   wr, bad_wr);
		rwqe->length = cpu_to_le32(length);
		qp->wqe_wr_id[qp->sq.prod].wqe_size = rwqe->wqe_size;
		qp->prev_wqe_size = rwqe->wqe_size;
		qp->wqe_wr_id[qp->sq.prod].bytes_len = rwqe->length;
		break;

	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		awqe1 = (struct rdma_sq_atomic_wqe_1st *)wqe;
		awqe1->wqe_size = 4;

		awqe2 = qed_chain_produce(&qp->sq.pbl);
		DMA_REGPAIR_LE(awqe2->remote_va, atomic_wr(wr)->remote_addr);
		awqe2->r_key = cpu_to_le32(atomic_wr(wr)->rkey);

		awqe3 = qed_chain_produce(&qp->sq.pbl);

		if (wr->opcode == IB_WR_ATOMIC_FETCH_AND_ADD) {
			wqe->req_type = RDMA_SQ_REQ_TYPE_ATOMIC_ADD;
			DMA_REGPAIR_LE(awqe3->swap_data,
				       atomic_wr(wr)->compare_add);
		} else {
			wqe->req_type = RDMA_SQ_REQ_TYPE_ATOMIC_CMP_AND_SWAP;
			DMA_REGPAIR_LE(awqe3->swap_data,
				       atomic_wr(wr)->swap);
			DMA_REGPAIR_LE(awqe3->cmp_data,
				       atomic_wr(wr)->compare_add);
		}

		qedr_prepare_sq_sges(qp, NULL, wr);

		qp->wqe_wr_id[qp->sq.prod].wqe_size = awqe1->wqe_size;
		qp->prev_wqe_size = awqe1->wqe_size;
		break;

	case IB_WR_LOCAL_INV:
		iwqe = (struct rdma_sq_local_inv_wqe *)wqe;
		iwqe->wqe_size = 1;

		iwqe->req_type = RDMA_SQ_REQ_TYPE_LOCAL_INVALIDATE;
		iwqe->inv_l_key = wr->ex.invalidate_rkey;
		qp->wqe_wr_id[qp->sq.prod].wqe_size = iwqe->wqe_size;
		qp->prev_wqe_size = iwqe->wqe_size;
		break;
	case IB_WR_REG_MR:
		DP_DEBUG(dev, QEDR_MSG_CQ, "REG_MR\n");
		wqe->req_type = RDMA_SQ_REQ_TYPE_FAST_MR;
		fwqe1 = (struct rdma_sq_fmr_wqe_1st *)wqe;
		fwqe1->wqe_size = 2;

		rc = qedr_prepare_reg(qp, fwqe1, reg_wr(wr));
		if (rc) {
			DP_ERR(dev, "IB_REG_MR failed rc=%d\n", rc);
			*bad_wr = wr;
			break;
		}

		qp->wqe_wr_id[qp->sq.prod].wqe_size = fwqe1->wqe_size;
		qp->prev_wqe_size = fwqe1->wqe_size;
		break;
	default:
		DP_ERR(dev, "invalid opcode 0x%x!\n", wr->opcode);
		rc = -EINVAL;
		*bad_wr = wr;
		break;
	}

	if (*bad_wr) {
		u16 value;

		/* Restore prod to its position before
		 * this WR was processed
		 */
		value = le16_to_cpu(qp->sq.db_data.data.value);
		qed_chain_set_prod(&qp->sq.pbl, value, wqe);

		/* Restore prev_wqe_size */
		qp->prev_wqe_size = wqe->prev_wqe_size;
		rc = -EINVAL;
		DP_ERR(dev, "POST SEND FAILED\n");
	}

	return rc;
}

int qedr_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		   struct ib_send_wr **bad_wr)
{
	struct qedr_dev *dev = get_qedr_dev(ibqp->device);
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	unsigned long flags;
	int rc = 0;

	*bad_wr = NULL;

	if (qp->qp_type == IB_QPT_GSI)
		return qedr_gsi_post_send(ibqp, wr, bad_wr);

	spin_lock_irqsave(&qp->q_lock, flags);

	if ((qp->state != QED_ROCE_QP_STATE_RTS) &&
	    (qp->state != QED_ROCE_QP_STATE_ERR) &&
	    (qp->state != QED_ROCE_QP_STATE_SQD)) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		*bad_wr = wr;
		DP_DEBUG(dev, QEDR_MSG_CQ,
			 "QP in wrong state! QP icid=0x%x state %d\n",
			 qp->icid, qp->state);
		return -EINVAL;
	}

	while (wr) {
		rc = __qedr_post_send(ibqp, wr, bad_wr);
		if (rc)
			break;

		qp->wqe_wr_id[qp->sq.prod].wr_id = wr->wr_id;

		qedr_inc_sw_prod(&qp->sq);

		qp->sq.db_data.data.value++;

		wr = wr->next;
	}

	/* Trigger doorbell
	 * If there was a failure in the first WR then it will be triggered in
	 * vane. However this is not harmful (as long as the producer value is
	 * unchanged). For performance reasons we avoid checking for this
	 * redundant doorbell.
	 */
	wmb();
	writel(qp->sq.db_data.raw, qp->sq.db);

	/* Make sure write sticks */
	mmiowb();

	spin_unlock_irqrestore(&qp->q_lock, flags);

	return rc;
}

int qedr_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		   struct ib_recv_wr **bad_wr)
{
	struct qedr_qp *qp = get_qedr_qp(ibqp);
	struct qedr_dev *dev = qp->dev;
	unsigned long flags;
	int status = 0;

	if (qp->qp_type == IB_QPT_GSI)
		return qedr_gsi_post_recv(ibqp, wr, bad_wr);

	spin_lock_irqsave(&qp->q_lock, flags);

	if (qp->state == QED_ROCE_QP_STATE_RESET) {
		spin_unlock_irqrestore(&qp->q_lock, flags);
		*bad_wr = wr;
		return -EINVAL;
	}

	while (wr) {
		int i;

		if (qed_chain_get_elem_left_u32(&qp->rq.pbl) <
		    QEDR_MAX_RQE_ELEMENTS_PER_RQE ||
		    wr->num_sge > qp->rq.max_sges) {
			DP_ERR(dev, "Can't post WR  (%d < %d) || (%d > %d)\n",
			       qed_chain_get_elem_left_u32(&qp->rq.pbl),
			       QEDR_MAX_RQE_ELEMENTS_PER_RQE, wr->num_sge,
			       qp->rq.max_sges);
			status = -ENOMEM;
			*bad_wr = wr;
			break;
		}
		for (i = 0; i < wr->num_sge; i++) {
			u32 flags = 0;
			struct rdma_rq_sge *rqe =
			    qed_chain_produce(&qp->rq.pbl);

			/* First one must include the number
			 * of SGE in the list
			 */
			if (!i)
				SET_FIELD(flags, RDMA_RQ_SGE_NUM_SGES,
					  wr->num_sge);

			SET_FIELD(flags, RDMA_RQ_SGE_L_KEY,
				  wr->sg_list[i].lkey);

			RQ_SGE_SET(rqe, wr->sg_list[i].addr,
				   wr->sg_list[i].length, flags);
		}

		/* Special case of no sges. FW requires between 1-4 sges...
		 * in this case we need to post 1 sge with length zero. this is
		 * because rdma write with immediate consumes an RQ.
		 */
		if (!wr->num_sge) {
			u32 flags = 0;
			struct rdma_rq_sge *rqe =
			    qed_chain_produce(&qp->rq.pbl);

			/* First one must include the number
			 * of SGE in the list
			 */
			SET_FIELD(flags, RDMA_RQ_SGE_L_KEY, 0);
			SET_FIELD(flags, RDMA_RQ_SGE_NUM_SGES, 1);

			RQ_SGE_SET(rqe, 0, 0, flags);
			i = 1;
		}

		qp->rqe_wr_id[qp->rq.prod].wr_id = wr->wr_id;
		qp->rqe_wr_id[qp->rq.prod].wqe_size = i;

		qedr_inc_sw_prod(&qp->rq);

		/* Flush all the writes before signalling doorbell */
		wmb();

		qp->rq.db_data.data.value++;

		writel(qp->rq.db_data.raw, qp->rq.db);

		/* Make sure write sticks */
		mmiowb();

		wr = wr->next;
	}

	spin_unlock_irqrestore(&qp->q_lock, flags);

	return status;
}

static int is_valid_cqe(struct qedr_cq *cq, union rdma_cqe *cqe)
{
	struct rdma_cqe_requester *resp_cqe = &cqe->req;

	return (resp_cqe->flags & RDMA_CQE_REQUESTER_TOGGLE_BIT_MASK) ==
		cq->pbl_toggle;
}

static struct qedr_qp *cqe_get_qp(union rdma_cqe *cqe)
{
	struct rdma_cqe_requester *resp_cqe = &cqe->req;
	struct qedr_qp *qp;

	qp = (struct qedr_qp *)(uintptr_t)HILO_GEN(resp_cqe->qp_handle.hi,
						   resp_cqe->qp_handle.lo,
						   u64);
	return qp;
}

static enum rdma_cqe_type cqe_get_type(union rdma_cqe *cqe)
{
	struct rdma_cqe_requester *resp_cqe = &cqe->req;

	return GET_FIELD(resp_cqe->flags, RDMA_CQE_REQUESTER_TYPE);
}

/* Return latest CQE (needs processing) */
static union rdma_cqe *get_cqe(struct qedr_cq *cq)
{
	return cq->latest_cqe;
}

/* In fmr we need to increase the number of fmr completed counter for the fmr
 * algorithm determining whether we can free a pbl or not.
 * we need to perform this whether the work request was signaled or not. for
 * this purpose we call this function from the condition that checks if a wr
 * should be skipped, to make sure we don't miss it ( possibly this fmr
 * operation was not signalted)
 */
static inline void qedr_chk_if_fmr(struct qedr_qp *qp)
{
	if (qp->wqe_wr_id[qp->sq.cons].opcode == IB_WC_REG_MR)
		qp->wqe_wr_id[qp->sq.cons].mr->info.completed++;
}

static int process_req(struct qedr_dev *dev, struct qedr_qp *qp,
		       struct qedr_cq *cq, int num_entries,
		       struct ib_wc *wc, u16 hw_cons, enum ib_wc_status status,
		       int force)
{
	u16 cnt = 0;

	while (num_entries && qp->sq.wqe_cons != hw_cons) {
		if (!qp->wqe_wr_id[qp->sq.cons].signaled && !force) {
			qedr_chk_if_fmr(qp);
			/* skip WC */
			goto next_cqe;
		}

		/* fill WC */
		wc->status = status;
		wc->vendor_err = 0;
		wc->wc_flags = 0;
		wc->src_qp = qp->id;
		wc->qp = &qp->ibqp;

		wc->wr_id = qp->wqe_wr_id[qp->sq.cons].wr_id;
		wc->opcode = qp->wqe_wr_id[qp->sq.cons].opcode;

		switch (wc->opcode) {
		case IB_WC_RDMA_WRITE:
			wc->byte_len = qp->wqe_wr_id[qp->sq.cons].bytes_len;
			break;
		case IB_WC_COMP_SWAP:
		case IB_WC_FETCH_ADD:
			wc->byte_len = 8;
			break;
		case IB_WC_REG_MR:
			qp->wqe_wr_id[qp->sq.cons].mr->info.completed++;
			break;
		default:
			break;
		}

		num_entries--;
		wc++;
		cnt++;
next_cqe:
		while (qp->wqe_wr_id[qp->sq.cons].wqe_size--)
			qed_chain_consume(&qp->sq.pbl);
		qedr_inc_sw_cons(&qp->sq);
	}

	return cnt;
}

static int qedr_poll_cq_req(struct qedr_dev *dev,
			    struct qedr_qp *qp, struct qedr_cq *cq,
			    int num_entries, struct ib_wc *wc,
			    struct rdma_cqe_requester *req)
{
	int cnt = 0;

	switch (req->status) {
	case RDMA_CQE_REQ_STS_OK:
		cnt = process_req(dev, qp, cq, num_entries, wc, req->sq_cons,
				  IB_WC_SUCCESS, 0);
		break;
	case RDMA_CQE_REQ_STS_WORK_REQUEST_FLUSHED_ERR:
		if (qp->state != QED_ROCE_QP_STATE_ERR)
			DP_ERR(dev,
			       "Error: POLL CQ with RDMA_CQE_REQ_STS_WORK_REQUEST_FLUSHED_ERR. CQ icid=0x%x, QP icid=0x%x\n",
			       cq->icid, qp->icid);
		cnt = process_req(dev, qp, cq, num_entries, wc, req->sq_cons,
				  IB_WC_WR_FLUSH_ERR, 1);
		break;
	default:
		/* process all WQE before the cosumer */
		qp->state = QED_ROCE_QP_STATE_ERR;
		cnt = process_req(dev, qp, cq, num_entries, wc,
				  req->sq_cons - 1, IB_WC_SUCCESS, 0);
		wc += cnt;
		/* if we have extra WC fill it with actual error info */
		if (cnt < num_entries) {
			enum ib_wc_status wc_status;

			switch (req->status) {
			case RDMA_CQE_REQ_STS_BAD_RESPONSE_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_BAD_RESPONSE_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_BAD_RESP_ERR;
				break;
			case RDMA_CQE_REQ_STS_LOCAL_LENGTH_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_LOCAL_LENGTH_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_LOC_LEN_ERR;
				break;
			case RDMA_CQE_REQ_STS_LOCAL_QP_OPERATION_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_LOCAL_QP_OPERATION_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_LOC_QP_OP_ERR;
				break;
			case RDMA_CQE_REQ_STS_LOCAL_PROTECTION_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_LOCAL_PROTECTION_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_LOC_PROT_ERR;
				break;
			case RDMA_CQE_REQ_STS_MEMORY_MGT_OPERATION_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_MEMORY_MGT_OPERATION_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_MW_BIND_ERR;
				break;
			case RDMA_CQE_REQ_STS_REMOTE_INVALID_REQUEST_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_REMOTE_INVALID_REQUEST_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_REM_INV_REQ_ERR;
				break;
			case RDMA_CQE_REQ_STS_REMOTE_ACCESS_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_REMOTE_ACCESS_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_REM_ACCESS_ERR;
				break;
			case RDMA_CQE_REQ_STS_REMOTE_OPERATION_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_REMOTE_OPERATION_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_REM_OP_ERR;
				break;
			case RDMA_CQE_REQ_STS_RNR_NAK_RETRY_CNT_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with RDMA_CQE_REQ_STS_RNR_NAK_RETRY_CNT_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_RNR_RETRY_EXC_ERR;
				break;
			case RDMA_CQE_REQ_STS_TRANSPORT_RETRY_CNT_ERR:
				DP_ERR(dev,
				       "Error: POLL CQ with ROCE_CQE_REQ_STS_TRANSPORT_RETRY_CNT_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_RETRY_EXC_ERR;
				break;
			default:
				DP_ERR(dev,
				       "Error: POLL CQ with IB_WC_GENERAL_ERR. CQ icid=0x%x, QP icid=0x%x\n",
				       cq->icid, qp->icid);
				wc_status = IB_WC_GENERAL_ERR;
			}
			cnt += process_req(dev, qp, cq, 1, wc, req->sq_cons,
					   wc_status, 1);
		}
	}

	return cnt;
}

static void __process_resp_one(struct qedr_dev *dev, struct qedr_qp *qp,
			       struct qedr_cq *cq, struct ib_wc *wc,
			       struct rdma_cqe_responder *resp, u64 wr_id)
{
	enum ib_wc_status wc_status = IB_WC_SUCCESS;
	u8 flags;

	wc->opcode = IB_WC_RECV;
	wc->wc_flags = 0;

	switch (resp->status) {
	case RDMA_CQE_RESP_STS_LOCAL_ACCESS_ERR:
		wc_status = IB_WC_LOC_ACCESS_ERR;
		break;
	case RDMA_CQE_RESP_STS_LOCAL_LENGTH_ERR:
		wc_status = IB_WC_LOC_LEN_ERR;
		break;
	case RDMA_CQE_RESP_STS_LOCAL_QP_OPERATION_ERR:
		wc_status = IB_WC_LOC_QP_OP_ERR;
		break;
	case RDMA_CQE_RESP_STS_LOCAL_PROTECTION_ERR:
		wc_status = IB_WC_LOC_PROT_ERR;
		break;
	case RDMA_CQE_RESP_STS_MEMORY_MGT_OPERATION_ERR:
		wc_status = IB_WC_MW_BIND_ERR;
		break;
	case RDMA_CQE_RESP_STS_REMOTE_INVALID_REQUEST_ERR:
		wc_status = IB_WC_REM_INV_RD_REQ_ERR;
		break;
	case RDMA_CQE_RESP_STS_OK:
		wc_status = IB_WC_SUCCESS;
		wc->byte_len = le32_to_cpu(resp->length);

		flags = resp->flags & QEDR_RESP_RDMA_IMM;

		if (flags == QEDR_RESP_RDMA_IMM)
			wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;

		if (flags == QEDR_RESP_RDMA_IMM || flags == QEDR_RESP_IMM) {
			wc->ex.imm_data =
				le32_to_cpu(resp->imm_data_or_inv_r_Key);
			wc->wc_flags |= IB_WC_WITH_IMM;
		}
		break;
	default:
		wc->status = IB_WC_GENERAL_ERR;
		DP_ERR(dev, "Invalid CQE status detected\n");
	}

	/* fill WC */
	wc->status = wc_status;
	wc->vendor_err = 0;
	wc->src_qp = qp->id;
	wc->qp = &qp->ibqp;
	wc->wr_id = wr_id;
}

static int process_resp_one(struct qedr_dev *dev, struct qedr_qp *qp,
			    struct qedr_cq *cq, struct ib_wc *wc,
			    struct rdma_cqe_responder *resp)
{
	u64 wr_id = qp->rqe_wr_id[qp->rq.cons].wr_id;

	__process_resp_one(dev, qp, cq, wc, resp, wr_id);

	while (qp->rqe_wr_id[qp->rq.cons].wqe_size--)
		qed_chain_consume(&qp->rq.pbl);
	qedr_inc_sw_cons(&qp->rq);

	return 1;
}

static int process_resp_flush(struct qedr_qp *qp, struct qedr_cq *cq,
			      int num_entries, struct ib_wc *wc, u16 hw_cons)
{
	u16 cnt = 0;

	while (num_entries && qp->rq.wqe_cons != hw_cons) {
		/* fill WC */
		wc->status = IB_WC_WR_FLUSH_ERR;
		wc->vendor_err = 0;
		wc->wc_flags = 0;
		wc->src_qp = qp->id;
		wc->byte_len = 0;
		wc->wr_id = qp->rqe_wr_id[qp->rq.cons].wr_id;
		wc->qp = &qp->ibqp;
		num_entries--;
		wc++;
		cnt++;
		while (qp->rqe_wr_id[qp->rq.cons].wqe_size--)
			qed_chain_consume(&qp->rq.pbl);
		qedr_inc_sw_cons(&qp->rq);
	}

	return cnt;
}

static void try_consume_resp_cqe(struct qedr_cq *cq, struct qedr_qp *qp,
				 struct rdma_cqe_responder *resp, int *update)
{
	if (le16_to_cpu(resp->rq_cons) == qp->rq.wqe_cons) {
		consume_cqe(cq);
		*update |= 1;
	}
}

static int qedr_poll_cq_resp(struct qedr_dev *dev, struct qedr_qp *qp,
			     struct qedr_cq *cq, int num_entries,
			     struct ib_wc *wc, struct rdma_cqe_responder *resp,
			     int *update)
{
	int cnt;

	if (resp->status == RDMA_CQE_RESP_STS_WORK_REQUEST_FLUSHED_ERR) {
		cnt = process_resp_flush(qp, cq, num_entries, wc,
					 resp->rq_cons);
		try_consume_resp_cqe(cq, qp, resp, update);
	} else {
		cnt = process_resp_one(dev, qp, cq, wc, resp);
		consume_cqe(cq);
		*update |= 1;
	}

	return cnt;
}

static void try_consume_req_cqe(struct qedr_cq *cq, struct qedr_qp *qp,
				struct rdma_cqe_requester *req, int *update)
{
	if (le16_to_cpu(req->sq_cons) == qp->sq.wqe_cons) {
		consume_cqe(cq);
		*update |= 1;
	}
}

int qedr_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct qedr_dev *dev = get_qedr_dev(ibcq->device);
	struct qedr_cq *cq = get_qedr_cq(ibcq);
	union rdma_cqe *cqe = cq->latest_cqe;
	u32 old_cons, new_cons;
	unsigned long flags;
	int update = 0;
	int done = 0;

	if (cq->cq_type == QEDR_CQ_TYPE_GSI)
		return qedr_gsi_poll_cq(ibcq, num_entries, wc);

	spin_lock_irqsave(&cq->cq_lock, flags);
	old_cons = qed_chain_get_cons_idx_u32(&cq->pbl);
	while (num_entries && is_valid_cqe(cq, cqe)) {
		struct qedr_qp *qp;
		int cnt = 0;

		/* prevent speculative reads of any field of CQE */
		rmb();

		qp = cqe_get_qp(cqe);
		if (!qp) {
			WARN(1, "Error: CQE QP pointer is NULL. CQE=%p\n", cqe);
			break;
		}

		wc->qp = &qp->ibqp;

		switch (cqe_get_type(cqe)) {
		case RDMA_CQE_TYPE_REQUESTER:
			cnt = qedr_poll_cq_req(dev, qp, cq, num_entries, wc,
					       &cqe->req);
			try_consume_req_cqe(cq, qp, &cqe->req, &update);
			break;
		case RDMA_CQE_TYPE_RESPONDER_RQ:
			cnt = qedr_poll_cq_resp(dev, qp, cq, num_entries, wc,
						&cqe->resp, &update);
			break;
		case RDMA_CQE_TYPE_INVALID:
		default:
			DP_ERR(dev, "Error: invalid CQE type = %d\n",
			       cqe_get_type(cqe));
		}
		num_entries -= cnt;
		wc += cnt;
		done += cnt;

		cqe = get_cqe(cq);
	}
	new_cons = qed_chain_get_cons_idx_u32(&cq->pbl);

	cq->cq_cons += new_cons - old_cons;

	if (update)
		/* doorbell notifies abount latest VALID entry,
		 * but chain already point to the next INVALID one
		 */
		doorbell_cq(cq, cq->cq_cons - 1, cq->arm_flags);

	spin_unlock_irqrestore(&cq->cq_lock, flags);
	return done;
}

int qedr_process_mad(struct ib_device *ibdev, int process_mad_flags,
		     u8 port_num,
		     const struct ib_wc *in_wc,
		     const struct ib_grh *in_grh,
		     const struct ib_mad_hdr *mad_hdr,
		     size_t in_mad_size, struct ib_mad_hdr *out_mad,
		     size_t *out_mad_size, u16 *out_mad_pkey_index)
{
	struct qedr_dev *dev = get_qedr_dev(ibdev);

	DP_DEBUG(dev, QEDR_MSG_GSI,
		 "QEDR_PROCESS_MAD in_mad %x %x %x %x %x %x %x %x\n",
		 mad_hdr->attr_id, mad_hdr->base_version, mad_hdr->attr_mod,
		 mad_hdr->class_specific, mad_hdr->class_version,
		 mad_hdr->method, mad_hdr->mgmt_class, mad_hdr->status);
	return IB_MAD_RESULT_SUCCESS;
}

int qedr_port_immutable(struct ib_device *ibdev, u8 port_num,
			struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE |
				    RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;

	return 0;
}
