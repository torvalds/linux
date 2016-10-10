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

struct ib_pd *qedr_alloc_pd(struct ib_device *ibdev,
			    struct ib_ucontext *context, struct ib_udata *udata)
{
	struct qedr_dev *dev = get_qedr_dev(ibdev);
	struct qedr_ucontext *uctx = NULL;
	struct qedr_alloc_pd_uresp uresp;
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

	dev->ops->rdma_alloc_pd(dev->rdma_ctx, &pd_id);

	uresp.pd_id = pd_id;
	pd->pd_id = pd_id;

	if (udata && context) {
		rc = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (rc)
			DP_ERR(dev, "copy error pd_id=0x%x.\n", pd_id);
		uctx = get_qedr_ucontext(context);
		uctx->pd = pd;
		pd->uctx = uctx;
	}

	return &pd->ibpd;
}

int qedr_dealloc_pd(struct ib_pd *ibpd)
{
	struct qedr_dev *dev = get_qedr_dev(ibpd->device);
	struct qedr_pd *pd = get_qedr_pd(ibpd);

	if (!pd)
		pr_err("Invalid PD received in dealloc_pd\n");

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
	if (IS_ERR_OR_NULL(q->pbl_tbl))
		goto err0;

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
		iparams.icid = cq->icid;
		dev->ops->rdma_destroy_cq(dev->rdma_ctx, &iparams, &oparams);
		dev->ops->common->chain_free(dev->cdev, &cq->pbl);
	}

	if (ibcq->uobject && ibcq->uobject->context) {
		qedr_free_pbl(dev, &cq->q.pbl_info, cq->q.pbl_tbl);
		ib_umem_release(cq->q.umem);
	}

	kfree(cq);

	return 0;
}
