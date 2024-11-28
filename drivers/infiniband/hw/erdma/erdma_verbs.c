// SPDX-License-Identifier: GPL-2.0

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

/* Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved. */

#include <linux/vmalloc.h>
#include <net/addrconf.h>
#include <rdma/erdma-abi.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_ioctl.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_verbs.h"

static int create_qp_cmd(struct erdma_dev *dev, struct erdma_qp *qp)
{
	struct erdma_cmdq_create_qp_req req;
	struct erdma_pd *pd = to_epd(qp->ibqp.pd);
	struct erdma_uqp *user_qp;
	u64 resp0, resp1;
	int err;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_CREATE_QP);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_QP_SQ_DEPTH_MASK,
			      ilog2(qp->attrs.sq_size)) |
		   FIELD_PREP(ERDMA_CMD_CREATE_QP_QPN_MASK, QP_ID(qp));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_QP_RQ_DEPTH_MASK,
			      ilog2(qp->attrs.rq_size)) |
		   FIELD_PREP(ERDMA_CMD_CREATE_QP_PD_MASK, pd->pdn);

	if (rdma_is_kernel_res(&qp->ibqp.res)) {
		u32 pgsz_range = ilog2(SZ_1M) - ERDMA_HW_PAGE_SHIFT;

		req.sq_cqn_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
				   pgsz_range) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);
		req.rq_cqn_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
				   pgsz_range) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg =
			FIELD_PREP(ERDMA_CMD_CREATE_QP_PAGE_OFFSET_MASK, 0) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK, 1) |
			FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK,
				   ERDMA_MR_INLINE_MTT);
		req.rq_mtt_cfg = req.sq_mtt_cfg;

		req.rq_buf_addr = qp->kern_qp.rq_buf_dma_addr;
		req.sq_buf_addr = qp->kern_qp.sq_buf_dma_addr;
		req.sq_db_info_dma_addr = qp->kern_qp.sq_buf_dma_addr +
					  (qp->attrs.sq_size << SQEBB_SHIFT);
		req.rq_db_info_dma_addr = qp->kern_qp.rq_buf_dma_addr +
					  (qp->attrs.rq_size << RQE_SHIFT);
	} else {
		user_qp = &qp->user_qp;
		req.sq_cqn_mtt_cfg = FIELD_PREP(
			ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->sq_mtt.page_size) - ERDMA_HW_PAGE_SHIFT);
		req.sq_cqn_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->scq->cqn);

		req.rq_cqn_mtt_cfg = FIELD_PREP(
			ERDMA_CMD_CREATE_QP_PAGE_SIZE_MASK,
			ilog2(user_qp->rq_mtt.page_size) - ERDMA_HW_PAGE_SHIFT);
		req.rq_cqn_mtt_cfg |=
			FIELD_PREP(ERDMA_CMD_CREATE_QP_CQN_MASK, qp->rcq->cqn);

		req.sq_mtt_cfg = user_qp->sq_mtt.page_offset;
		req.sq_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK,
					     user_qp->sq_mtt.mtt_nents) |
				  FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK,
					     user_qp->sq_mtt.mtt_type);

		req.rq_mtt_cfg = user_qp->rq_mtt.page_offset;
		req.rq_mtt_cfg |= FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_CNT_MASK,
					     user_qp->rq_mtt.mtt_nents) |
				  FIELD_PREP(ERDMA_CMD_CREATE_QP_MTT_TYPE_MASK,
					     user_qp->rq_mtt.mtt_type);

		req.sq_buf_addr = user_qp->sq_mtt.mtt_entry[0];
		req.rq_buf_addr = user_qp->rq_mtt.mtt_entry[0];

		req.sq_db_info_dma_addr = user_qp->sq_db_info_dma_addr;
		req.rq_db_info_dma_addr = user_qp->rq_db_info_dma_addr;
	}

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), &resp0,
				  &resp1);
	if (!err)
		qp->attrs.cookie =
			FIELD_GET(ERDMA_CMDQ_CREATE_QP_RESP_COOKIE_MASK, resp0);

	return err;
}

static int regmr_cmd(struct erdma_dev *dev, struct erdma_mr *mr)
{
	struct erdma_cmdq_reg_mr_req req;
	struct erdma_pd *pd = to_epd(mr->ibmr.pd);
	u64 *phy_addr;
	int i;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA, CMDQ_OPCODE_REG_MR);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_MR_VALID_MASK, mr->valid) |
		   FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, mr->ibmr.lkey & 0xFF) |
		   FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, mr->ibmr.lkey >> 8);
	req.cfg1 = FIELD_PREP(ERDMA_CMD_REGMR_PD_MASK, pd->pdn) |
		   FIELD_PREP(ERDMA_CMD_REGMR_TYPE_MASK, mr->type) |
		   FIELD_PREP(ERDMA_CMD_REGMR_RIGHT_MASK, mr->access) |
		   FIELD_PREP(ERDMA_CMD_REGMR_ACC_MODE_MASK, 0);
	req.cfg2 = FIELD_PREP(ERDMA_CMD_REGMR_PAGESIZE_MASK,
			      ilog2(mr->mem.page_size)) |
		   FIELD_PREP(ERDMA_CMD_REGMR_MTT_TYPE_MASK, mr->mem.mtt_type) |
		   FIELD_PREP(ERDMA_CMD_REGMR_MTT_CNT_MASK, mr->mem.page_cnt);

	if (mr->type == ERDMA_MR_TYPE_DMA)
		goto post_cmd;

	if (mr->type == ERDMA_MR_TYPE_NORMAL) {
		req.start_va = mr->mem.va;
		req.size = mr->mem.len;
	}

	if (mr->type == ERDMA_MR_TYPE_FRMR ||
	    mr->mem.mtt_type == ERDMA_MR_INDIRECT_MTT) {
		phy_addr = req.phy_addr;
		*phy_addr = mr->mem.mtt_entry[0];
	} else {
		phy_addr = req.phy_addr;
		for (i = 0; i < mr->mem.mtt_nents; i++)
			*phy_addr++ = mr->mem.mtt_entry[i];
	}

post_cmd:
	return erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
}

static int create_cq_cmd(struct erdma_dev *dev, struct erdma_cq *cq)
{
	struct erdma_cmdq_create_cq_req req;
	u32 page_size;
	struct erdma_mem *mtt;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_CREATE_CQ);

	req.cfg0 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_CQN_MASK, cq->cqn) |
		   FIELD_PREP(ERDMA_CMD_CREATE_CQ_DEPTH_MASK, ilog2(cq->depth));
	req.cfg1 = FIELD_PREP(ERDMA_CMD_CREATE_CQ_EQN_MASK, cq->assoc_eqn);

	if (rdma_is_kernel_res(&cq->ibcq.res)) {
		page_size = SZ_32M;
		req.cfg0 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK,
				       ilog2(page_size) - ERDMA_HW_PAGE_SHIFT);
		req.qbuf_addr_l = lower_32_bits(cq->kern_cq.qbuf_dma_addr);
		req.qbuf_addr_h = upper_32_bits(cq->kern_cq.qbuf_dma_addr);

		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK, 1) |
			    FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_TYPE_MASK,
				       ERDMA_MR_INLINE_MTT);

		req.first_page_offset = 0;
		req.cq_db_info_addr =
			cq->kern_cq.qbuf_dma_addr + (cq->depth << CQE_SHIFT);
	} else {
		mtt = &cq->user_cq.qbuf_mtt;
		req.cfg0 |=
			FIELD_PREP(ERDMA_CMD_CREATE_CQ_PAGESIZE_MASK,
				   ilog2(mtt->page_size) - ERDMA_HW_PAGE_SHIFT);
		if (mtt->mtt_nents == 1) {
			req.qbuf_addr_l = lower_32_bits(*(u64 *)mtt->mtt_buf);
			req.qbuf_addr_h = upper_32_bits(*(u64 *)mtt->mtt_buf);
		} else {
			req.qbuf_addr_l = lower_32_bits(mtt->mtt_entry[0]);
			req.qbuf_addr_h = upper_32_bits(mtt->mtt_entry[0]);
		}
		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_CNT_MASK,
				       mtt->mtt_nents);
		req.cfg1 |= FIELD_PREP(ERDMA_CMD_CREATE_CQ_MTT_TYPE_MASK,
				       mtt->mtt_type);

		req.first_page_offset = mtt->page_offset;
		req.cq_db_info_addr = cq->user_cq.db_info_dma_addr;
	}

	return erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
}

static int erdma_alloc_idx(struct erdma_resource_cb *res_cb)
{
	int idx;
	unsigned long flags;

	spin_lock_irqsave(&res_cb->lock, flags);
	idx = find_next_zero_bit(res_cb->bitmap, res_cb->max_cap,
				 res_cb->next_alloc_idx);
	if (idx == res_cb->max_cap) {
		idx = find_first_zero_bit(res_cb->bitmap, res_cb->max_cap);
		if (idx == res_cb->max_cap) {
			res_cb->next_alloc_idx = 1;
			spin_unlock_irqrestore(&res_cb->lock, flags);
			return -ENOSPC;
		}
	}

	set_bit(idx, res_cb->bitmap);
	res_cb->next_alloc_idx = idx + 1;
	spin_unlock_irqrestore(&res_cb->lock, flags);

	return idx;
}

static inline void erdma_free_idx(struct erdma_resource_cb *res_cb, u32 idx)
{
	unsigned long flags;
	u32 used;

	spin_lock_irqsave(&res_cb->lock, flags);
	used = __test_and_clear_bit(idx, res_cb->bitmap);
	spin_unlock_irqrestore(&res_cb->lock, flags);
	WARN_ON(!used);
}

static struct rdma_user_mmap_entry *
erdma_user_mmap_entry_insert(struct erdma_ucontext *uctx, void *address,
			     u32 size, u8 mmap_flag, u64 *mmap_offset)
{
	struct erdma_user_mmap_entry *entry =
		kzalloc(sizeof(*entry), GFP_KERNEL);
	int ret;

	if (!entry)
		return NULL;

	entry->address = (u64)address;
	entry->mmap_flag = mmap_flag;

	size = PAGE_ALIGN(size);

	ret = rdma_user_mmap_entry_insert(&uctx->ibucontext, &entry->rdma_entry,
					  size);
	if (ret) {
		kfree(entry);
		return NULL;
	}

	*mmap_offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return &entry->rdma_entry;
}

int erdma_query_device(struct ib_device *ibdev, struct ib_device_attr *attr,
		       struct ib_udata *unused)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(attr, 0, sizeof(*attr));

	attr->max_mr_size = dev->attrs.max_mr_size;
	attr->vendor_id = PCI_VENDOR_ID_ALIBABA;
	attr->vendor_part_id = dev->pdev->device;
	attr->hw_ver = dev->pdev->revision;
	attr->max_qp = dev->attrs.max_qp - 1;
	attr->max_qp_wr = min(dev->attrs.max_send_wr, dev->attrs.max_recv_wr);
	attr->max_qp_rd_atom = dev->attrs.max_ord;
	attr->max_qp_init_rd_atom = dev->attrs.max_ird;
	attr->max_res_rd_atom = dev->attrs.max_qp * dev->attrs.max_ird;
	attr->device_cap_flags = IB_DEVICE_MEM_MGT_EXTENSIONS;
	attr->kernel_cap_flags = IBK_LOCAL_DMA_LKEY;
	ibdev->local_dma_lkey = dev->attrs.local_dma_key;
	attr->max_send_sge = dev->attrs.max_send_sge;
	attr->max_recv_sge = dev->attrs.max_recv_sge;
	attr->max_sge_rd = dev->attrs.max_sge_rd;
	attr->max_cq = dev->attrs.max_cq - 1;
	attr->max_cqe = dev->attrs.max_cqe;
	attr->max_mr = dev->attrs.max_mr;
	attr->max_pd = dev->attrs.max_pd;
	attr->max_mw = dev->attrs.max_mw;
	attr->max_fast_reg_page_list_len = ERDMA_MAX_FRMR_PA;
	attr->page_size_cap = ERDMA_PAGE_SIZE_SUPPORT;
	attr->fw_ver = dev->attrs.fw_version;

	if (dev->netdev)
		addrconf_addr_eui48((u8 *)&attr->sys_image_guid,
				    dev->netdev->dev_addr);

	return 0;
}

int erdma_query_gid(struct ib_device *ibdev, u32 port, int idx,
		    union ib_gid *gid)
{
	struct erdma_dev *dev = to_edev(ibdev);

	memset(gid, 0, sizeof(*gid));
	ether_addr_copy(gid->raw, dev->attrs.peer_addr);

	return 0;
}

int erdma_query_port(struct ib_device *ibdev, u32 port,
		     struct ib_port_attr *attr)
{
	struct erdma_dev *dev = to_edev(ibdev);
	struct net_device *ndev = dev->netdev;

	memset(attr, 0, sizeof(*attr));

	attr->gid_tbl_len = 1;
	attr->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_DEVICE_MGMT_SUP;
	attr->max_msg_sz = -1;

	if (!ndev)
		goto out;

	ib_get_eth_speed(ibdev, port, &attr->active_speed, &attr->active_width);
	attr->max_mtu = ib_mtu_int_to_enum(ndev->mtu);
	attr->active_mtu = ib_mtu_int_to_enum(ndev->mtu);
	if (netif_running(ndev) && netif_carrier_ok(ndev))
		dev->state = IB_PORT_ACTIVE;
	else
		dev->state = IB_PORT_DOWN;
	attr->state = dev->state;

out:
	if (dev->state == IB_PORT_ACTIVE)
		attr->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	else
		attr->phys_state = IB_PORT_PHYS_STATE_DISABLED;

	return 0;
}

int erdma_get_port_immutable(struct ib_device *ibdev, u32 port,
			     struct ib_port_immutable *port_immutable)
{
	port_immutable->gid_tbl_len = 1;
	port_immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;

	return 0;
}

int erdma_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_pd *pd = to_epd(ibpd);
	struct erdma_dev *dev = to_edev(ibpd->device);
	int pdn;

	pdn = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_PD]);
	if (pdn < 0)
		return pdn;

	pd->pdn = pdn;

	return 0;
}

int erdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct erdma_pd *pd = to_epd(ibpd);
	struct erdma_dev *dev = to_edev(ibpd->device);

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_PD], pd->pdn);

	return 0;
}

static int erdma_qp_validate_cap(struct erdma_dev *dev,
				 struct ib_qp_init_attr *attrs)
{
	if ((attrs->cap.max_send_wr > dev->attrs.max_send_wr) ||
	    (attrs->cap.max_recv_wr > dev->attrs.max_recv_wr) ||
	    (attrs->cap.max_send_sge > dev->attrs.max_send_sge) ||
	    (attrs->cap.max_recv_sge > dev->attrs.max_recv_sge) ||
	    (attrs->cap.max_inline_data > ERDMA_MAX_INLINE) ||
	    !attrs->cap.max_send_wr || !attrs->cap.max_recv_wr) {
		return -EINVAL;
	}

	return 0;
}

static int erdma_qp_validate_attr(struct erdma_dev *dev,
				  struct ib_qp_init_attr *attrs)
{
	if (attrs->qp_type != IB_QPT_RC)
		return -EOPNOTSUPP;

	if (attrs->srq)
		return -EOPNOTSUPP;

	if (!attrs->send_cq || !attrs->recv_cq)
		return -EOPNOTSUPP;

	return 0;
}

static void free_kernel_qp(struct erdma_qp *qp)
{
	struct erdma_dev *dev = qp->dev;

	vfree(qp->kern_qp.swr_tbl);
	vfree(qp->kern_qp.rwr_tbl);

	if (qp->kern_qp.sq_buf)
		dma_free_coherent(
			&dev->pdev->dev,
			WARPPED_BUFSIZE(qp->attrs.sq_size << SQEBB_SHIFT),
			qp->kern_qp.sq_buf, qp->kern_qp.sq_buf_dma_addr);

	if (qp->kern_qp.rq_buf)
		dma_free_coherent(
			&dev->pdev->dev,
			WARPPED_BUFSIZE(qp->attrs.rq_size << RQE_SHIFT),
			qp->kern_qp.rq_buf, qp->kern_qp.rq_buf_dma_addr);
}

static int init_kernel_qp(struct erdma_dev *dev, struct erdma_qp *qp,
			  struct ib_qp_init_attr *attrs)
{
	struct erdma_kqp *kqp = &qp->kern_qp;
	int size;

	if (attrs->sq_sig_type == IB_SIGNAL_ALL_WR)
		kqp->sig_all = 1;

	kqp->sq_pi = 0;
	kqp->sq_ci = 0;
	kqp->rq_pi = 0;
	kqp->rq_ci = 0;
	kqp->hw_sq_db =
		dev->func_bar + (ERDMA_SDB_SHARED_PAGE_INDEX << PAGE_SHIFT);
	kqp->hw_rq_db = dev->func_bar + ERDMA_BAR_RQDB_SPACE_OFFSET;

	kqp->swr_tbl = vmalloc(qp->attrs.sq_size * sizeof(u64));
	kqp->rwr_tbl = vmalloc(qp->attrs.rq_size * sizeof(u64));
	if (!kqp->swr_tbl || !kqp->rwr_tbl)
		goto err_out;

	size = (qp->attrs.sq_size << SQEBB_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE;
	kqp->sq_buf = dma_alloc_coherent(&dev->pdev->dev, size,
					 &kqp->sq_buf_dma_addr, GFP_KERNEL);
	if (!kqp->sq_buf)
		goto err_out;

	size = (qp->attrs.rq_size << RQE_SHIFT) + ERDMA_EXTRA_BUFFER_SIZE;
	kqp->rq_buf = dma_alloc_coherent(&dev->pdev->dev, size,
					 &kqp->rq_buf_dma_addr, GFP_KERNEL);
	if (!kqp->rq_buf)
		goto err_out;

	kqp->sq_db_info = kqp->sq_buf + (qp->attrs.sq_size << SQEBB_SHIFT);
	kqp->rq_db_info = kqp->rq_buf + (qp->attrs.rq_size << RQE_SHIFT);

	return 0;

err_out:
	free_kernel_qp(qp);
	return -ENOMEM;
}

static int get_mtt_entries(struct erdma_dev *dev, struct erdma_mem *mem,
			   u64 start, u64 len, int access, u64 virt,
			   unsigned long req_page_size, u8 force_indirect_mtt)
{
	struct ib_block_iter biter;
	uint64_t *phy_addr = NULL;
	int ret = 0;

	mem->umem = ib_umem_get(&dev->ibdev, start, len, access);
	if (IS_ERR(mem->umem)) {
		ret = PTR_ERR(mem->umem);
		mem->umem = NULL;
		return ret;
	}

	mem->va = virt;
	mem->len = len;
	mem->page_size = ib_umem_find_best_pgsz(mem->umem, req_page_size, virt);
	mem->page_offset = start & (mem->page_size - 1);
	mem->mtt_nents = ib_umem_num_dma_blocks(mem->umem, mem->page_size);
	mem->page_cnt = mem->mtt_nents;

	if (mem->page_cnt > ERDMA_MAX_INLINE_MTT_ENTRIES ||
	    force_indirect_mtt) {
		mem->mtt_type = ERDMA_MR_INDIRECT_MTT;
		mem->mtt_buf =
			alloc_pages_exact(MTT_SIZE(mem->page_cnt), GFP_KERNEL);
		if (!mem->mtt_buf) {
			ret = -ENOMEM;
			goto error_ret;
		}
		phy_addr = mem->mtt_buf;
	} else {
		mem->mtt_type = ERDMA_MR_INLINE_MTT;
		phy_addr = mem->mtt_entry;
	}

	rdma_umem_for_each_dma_block(mem->umem, &biter, mem->page_size) {
		*phy_addr = rdma_block_iter_dma_address(&biter);
		phy_addr++;
	}

	if (mem->mtt_type == ERDMA_MR_INDIRECT_MTT) {
		mem->mtt_entry[0] =
			dma_map_single(&dev->pdev->dev, mem->mtt_buf,
				       MTT_SIZE(mem->page_cnt), DMA_TO_DEVICE);
		if (dma_mapping_error(&dev->pdev->dev, mem->mtt_entry[0])) {
			free_pages_exact(mem->mtt_buf, MTT_SIZE(mem->page_cnt));
			mem->mtt_buf = NULL;
			ret = -ENOMEM;
			goto error_ret;
		}
	}

	return 0;

error_ret:
	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}

	return ret;
}

static void put_mtt_entries(struct erdma_dev *dev, struct erdma_mem *mem)
{
	if (mem->mtt_buf) {
		dma_unmap_single(&dev->pdev->dev, mem->mtt_entry[0],
				 MTT_SIZE(mem->page_cnt), DMA_TO_DEVICE);
		free_pages_exact(mem->mtt_buf, MTT_SIZE(mem->page_cnt));
	}

	if (mem->umem) {
		ib_umem_release(mem->umem);
		mem->umem = NULL;
	}
}

static int erdma_map_user_dbrecords(struct erdma_ucontext *ctx,
				    u64 dbrecords_va,
				    struct erdma_user_dbrecords_page **dbr_page,
				    dma_addr_t *dma_addr)
{
	struct erdma_user_dbrecords_page *page = NULL;
	int rv = 0;

	mutex_lock(&ctx->dbrecords_page_mutex);

	list_for_each_entry(page, &ctx->dbrecords_page_list, list)
		if (page->va == (dbrecords_va & PAGE_MASK))
			goto found;

	page = kmalloc(sizeof(*page), GFP_KERNEL);
	if (!page) {
		rv = -ENOMEM;
		goto out;
	}

	page->va = (dbrecords_va & PAGE_MASK);
	page->refcnt = 0;

	page->umem = ib_umem_get(ctx->ibucontext.device,
				 dbrecords_va & PAGE_MASK, PAGE_SIZE, 0);
	if (IS_ERR(page->umem)) {
		rv = PTR_ERR(page->umem);
		kfree(page);
		goto out;
	}

	list_add(&page->list, &ctx->dbrecords_page_list);

found:
	*dma_addr = sg_dma_address(page->umem->sgt_append.sgt.sgl) +
		    (dbrecords_va & ~PAGE_MASK);
	*dbr_page = page;
	page->refcnt++;

out:
	mutex_unlock(&ctx->dbrecords_page_mutex);
	return rv;
}

static void
erdma_unmap_user_dbrecords(struct erdma_ucontext *ctx,
			   struct erdma_user_dbrecords_page **dbr_page)
{
	if (!ctx || !(*dbr_page))
		return;

	mutex_lock(&ctx->dbrecords_page_mutex);
	if (--(*dbr_page)->refcnt == 0) {
		list_del(&(*dbr_page)->list);
		ib_umem_release((*dbr_page)->umem);
		kfree(*dbr_page);
	}

	*dbr_page = NULL;
	mutex_unlock(&ctx->dbrecords_page_mutex);
}

static int init_user_qp(struct erdma_qp *qp, struct erdma_ucontext *uctx,
			u64 va, u32 len, u64 db_info_va)
{
	dma_addr_t db_info_dma_addr;
	u32 rq_offset;
	int ret;

	if (len < (ALIGN(qp->attrs.sq_size * SQEBB_SIZE, ERDMA_HW_PAGE_SIZE) +
		   qp->attrs.rq_size * RQE_SIZE))
		return -EINVAL;

	ret = get_mtt_entries(qp->dev, &qp->user_qp.sq_mtt, va,
			      qp->attrs.sq_size << SQEBB_SHIFT, 0, va,
			      (SZ_1M - SZ_4K), 1);
	if (ret)
		return ret;

	rq_offset = ALIGN(qp->attrs.sq_size << SQEBB_SHIFT, ERDMA_HW_PAGE_SIZE);
	qp->user_qp.rq_offset = rq_offset;

	ret = get_mtt_entries(qp->dev, &qp->user_qp.rq_mtt, va + rq_offset,
			      qp->attrs.rq_size << RQE_SHIFT, 0, va + rq_offset,
			      (SZ_1M - SZ_4K), 1);
	if (ret)
		goto put_sq_mtt;

	ret = erdma_map_user_dbrecords(uctx, db_info_va,
				       &qp->user_qp.user_dbr_page,
				       &db_info_dma_addr);
	if (ret)
		goto put_rq_mtt;

	qp->user_qp.sq_db_info_dma_addr = db_info_dma_addr;
	qp->user_qp.rq_db_info_dma_addr = db_info_dma_addr + ERDMA_DB_SIZE;

	return 0;

put_rq_mtt:
	put_mtt_entries(qp->dev, &qp->user_qp.rq_mtt);

put_sq_mtt:
	put_mtt_entries(qp->dev, &qp->user_qp.sq_mtt);

	return ret;
}

static void free_user_qp(struct erdma_qp *qp, struct erdma_ucontext *uctx)
{
	put_mtt_entries(qp->dev, &qp->user_qp.sq_mtt);
	put_mtt_entries(qp->dev, &qp->user_qp.rq_mtt);
	erdma_unmap_user_dbrecords(uctx, &qp->user_qp.user_dbr_page);
}

int erdma_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attrs,
		    struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	struct erdma_ureq_create_qp ureq;
	struct erdma_uresp_create_qp uresp;
	int ret;

	ret = erdma_qp_validate_cap(dev, attrs);
	if (ret)
		goto err_out;

	ret = erdma_qp_validate_attr(dev, attrs);
	if (ret)
		goto err_out;

	qp->scq = to_ecq(attrs->send_cq);
	qp->rcq = to_ecq(attrs->recv_cq);
	qp->dev = dev;
	qp->attrs.cc = dev->attrs.cc;

	init_rwsem(&qp->state_lock);
	kref_init(&qp->ref);
	init_completion(&qp->safe_free);

	ret = xa_alloc_cyclic(&dev->qp_xa, &qp->ibqp.qp_num, qp,
			      XA_LIMIT(1, dev->attrs.max_qp - 1),
			      &dev->next_alloc_qpn, GFP_KERNEL);
	if (ret < 0) {
		ret = -ENOMEM;
		goto err_out;
	}

	qp->attrs.sq_size = roundup_pow_of_two(attrs->cap.max_send_wr *
					       ERDMA_MAX_WQEBB_PER_SQE);
	qp->attrs.rq_size = roundup_pow_of_two(attrs->cap.max_recv_wr);

	if (uctx) {
		ret = ib_copy_from_udata(&ureq, udata,
					 min(sizeof(ureq), udata->inlen));
		if (ret)
			goto err_out_xa;

		ret = init_user_qp(qp, uctx, ureq.qbuf_va, ureq.qbuf_len,
				   ureq.db_record_va);
		if (ret)
			goto err_out_xa;

		memset(&uresp, 0, sizeof(uresp));

		uresp.num_sqe = qp->attrs.sq_size;
		uresp.num_rqe = qp->attrs.rq_size;
		uresp.qp_id = QP_ID(qp);
		uresp.rq_offset = qp->user_qp.rq_offset;

		ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
		if (ret)
			goto err_out_cmd;
	} else {
		init_kernel_qp(dev, qp, attrs);
	}

	qp->attrs.max_send_sge = attrs->cap.max_send_sge;
	qp->attrs.max_recv_sge = attrs->cap.max_recv_sge;
	qp->attrs.state = ERDMA_QP_STATE_IDLE;

	ret = create_qp_cmd(dev, qp);
	if (ret)
		goto err_out_cmd;

	spin_lock_init(&qp->lock);

	return 0;

err_out_cmd:
	if (uctx)
		free_user_qp(qp, uctx);
	else
		free_kernel_qp(qp);
err_out_xa:
	xa_erase(&dev->qp_xa, QP_ID(qp));
err_out:
	return ret;
}

static int erdma_create_stag(struct erdma_dev *dev, u32 *stag)
{
	int stag_idx;

	stag_idx = erdma_alloc_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX]);
	if (stag_idx < 0)
		return stag_idx;

	/* For now, we always let key field be zero. */
	*stag = (stag_idx << 8);

	return 0;
}

struct ib_mr *erdma_get_dma_mr(struct ib_pd *ibpd, int acc)
{
	struct erdma_dev *dev = to_edev(ibpd->device);
	struct erdma_mr *mr;
	u32 stag;
	int ret;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_DMA;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->access = ERDMA_MR_ACC_LR | to_erdma_access_flags(acc);
	ret = regmr_cmd(dev, mr);
	if (ret)
		goto out_remove_stag;

	return &mr->ibmr;

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

struct ib_mr *erdma_ib_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type,
				u32 max_num_sg)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibpd->device);
	int ret;
	u32 stag;

	if (mr_type != IB_MR_TYPE_MEM_REG)
		return ERR_PTR(-EOPNOTSUPP);

	if (max_num_sg > ERDMA_MR_MAX_MTT_CNT)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto out_free;

	mr->type = ERDMA_MR_TYPE_FRMR;

	mr->ibmr.lkey = stag;
	mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	/* update it in FRMR. */
	mr->access = ERDMA_MR_ACC_LR | ERDMA_MR_ACC_LW | ERDMA_MR_ACC_RR |
		     ERDMA_MR_ACC_RW;

	mr->mem.page_size = PAGE_SIZE; /* update it later. */
	mr->mem.page_cnt = max_num_sg;
	mr->mem.mtt_type = ERDMA_MR_INDIRECT_MTT;
	mr->mem.mtt_buf =
		alloc_pages_exact(MTT_SIZE(mr->mem.page_cnt), GFP_KERNEL);
	if (!mr->mem.mtt_buf) {
		ret = -ENOMEM;
		goto out_remove_stag;
	}

	mr->mem.mtt_entry[0] =
		dma_map_single(&dev->pdev->dev, mr->mem.mtt_buf,
			       MTT_SIZE(mr->mem.page_cnt), DMA_TO_DEVICE);
	if (dma_mapping_error(&dev->pdev->dev, mr->mem.mtt_entry[0])) {
		ret = -ENOMEM;
		goto out_free_mtt;
	}

	ret = regmr_cmd(dev, mr);
	if (ret)
		goto out_dma_unmap;

	return &mr->ibmr;

out_dma_unmap:
	dma_unmap_single(&dev->pdev->dev, mr->mem.mtt_entry[0],
			 MTT_SIZE(mr->mem.page_cnt), DMA_TO_DEVICE);
out_free_mtt:
	free_pages_exact(mr->mem.mtt_buf, MTT_SIZE(mr->mem.page_cnt));

out_remove_stag:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

static int erdma_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct erdma_mr *mr = to_emr(ibmr);

	if (mr->mem.mtt_nents >= mr->mem.page_cnt)
		return -1;

	*((u64 *)mr->mem.mtt_buf + mr->mem.mtt_nents) = addr;
	mr->mem.mtt_nents++;

	return 0;
}

int erdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset)
{
	struct erdma_mr *mr = to_emr(ibmr);
	int num;

	mr->mem.mtt_nents = 0;

	num = ib_sg_to_pages(&mr->ibmr, sg, sg_nents, sg_offset,
			     erdma_set_page);

	return num;
}

struct ib_mr *erdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				u64 virt, int access, struct ib_udata *udata)
{
	struct erdma_mr *mr = NULL;
	struct erdma_dev *dev = to_edev(ibpd->device);
	u32 stag;
	int ret;

	if (!len || len > dev->attrs.max_mr_size)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	ret = get_mtt_entries(dev, &mr->mem, start, len, access, virt,
			      SZ_2G - SZ_4K, 0);
	if (ret)
		goto err_out_free;

	ret = erdma_create_stag(dev, &stag);
	if (ret)
		goto err_out_put_mtt;

	mr->ibmr.lkey = mr->ibmr.rkey = stag;
	mr->ibmr.pd = ibpd;
	mr->mem.va = virt;
	mr->mem.len = len;
	mr->access = ERDMA_MR_ACC_LR | to_erdma_access_flags(access);
	mr->valid = 1;
	mr->type = ERDMA_MR_TYPE_NORMAL;

	ret = regmr_cmd(dev, mr);
	if (ret)
		goto err_out_mr;

	return &mr->ibmr;

err_out_mr:
	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX],
		       mr->ibmr.lkey >> 8);

err_out_put_mtt:
	put_mtt_entries(dev, &mr->mem);

err_out_free:
	kfree(mr);

	return ERR_PTR(ret);
}

int erdma_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct erdma_mr *mr;
	struct erdma_dev *dev = to_edev(ibmr->device);
	struct erdma_cmdq_dereg_mr_req req;
	int ret;

	mr = to_emr(ibmr);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DEREG_MR);

	req.cfg = FIELD_PREP(ERDMA_CMD_MR_MPT_IDX_MASK, ibmr->lkey >> 8) |
		  FIELD_PREP(ERDMA_CMD_MR_KEY_MASK, ibmr->lkey & 0xFF);

	ret = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (ret)
		return ret;

	erdma_free_idx(&dev->res_cb[ERDMA_RES_TYPE_STAG_IDX], ibmr->lkey >> 8);

	put_mtt_entries(dev, &mr->mem);

	kfree(mr);
	return 0;
}

int erdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_dev *dev = to_edev(ibcq->device);
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	int err;
	struct erdma_cmdq_destroy_cq_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DESTROY_CQ);
	req.cqn = cq->cqn;

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (err)
		return err;

	if (rdma_is_kernel_res(&cq->ibcq.res)) {
		dma_free_coherent(&dev->pdev->dev,
				  WARPPED_BUFSIZE(cq->depth << CQE_SHIFT),
				  cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
	} else {
		erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
	}

	xa_erase(&dev->cq_xa, cq->cqn);

	return 0;
}

int erdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct erdma_qp *qp = to_eqp(ibqp);
	struct erdma_dev *dev = to_edev(ibqp->device);
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);
	struct erdma_qp_attrs qp_attrs;
	int err;
	struct erdma_cmdq_destroy_qp_req req;

	down_write(&qp->state_lock);
	qp_attrs.state = ERDMA_QP_STATE_ERROR;
	erdma_modify_qp_internal(qp, &qp_attrs, ERDMA_QP_ATTR_STATE);
	up_write(&qp->state_lock);

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_RDMA,
				CMDQ_OPCODE_DESTROY_QP);
	req.qpn = QP_ID(qp);

	err = erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
	if (err)
		return err;

	erdma_qp_put(qp);
	wait_for_completion(&qp->safe_free);

	if (rdma_is_kernel_res(&qp->ibqp.res)) {
		vfree(qp->kern_qp.swr_tbl);
		vfree(qp->kern_qp.rwr_tbl);
		dma_free_coherent(
			&dev->pdev->dev,
			WARPPED_BUFSIZE(qp->attrs.rq_size << RQE_SHIFT),
			qp->kern_qp.rq_buf, qp->kern_qp.rq_buf_dma_addr);
		dma_free_coherent(
			&dev->pdev->dev,
			WARPPED_BUFSIZE(qp->attrs.sq_size << SQEBB_SHIFT),
			qp->kern_qp.sq_buf, qp->kern_qp.sq_buf_dma_addr);
	} else {
		put_mtt_entries(dev, &qp->user_qp.sq_mtt);
		put_mtt_entries(dev, &qp->user_qp.rq_mtt);
		erdma_unmap_user_dbrecords(ctx, &qp->user_qp.user_dbr_page);
	}

	if (qp->cep)
		erdma_cep_put(qp->cep);
	xa_erase(&dev->qp_xa, QP_ID(qp));

	return 0;
}

void erdma_qp_get_ref(struct ib_qp *ibqp)
{
	erdma_qp_get(to_eqp(ibqp));
}

void erdma_qp_put_ref(struct ib_qp *ibqp)
{
	erdma_qp_put(to_eqp(ibqp));
}

int erdma_mmap(struct ib_ucontext *ctx, struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct erdma_user_mmap_entry *entry;
	pgprot_t prot;
	int err;

	rdma_entry = rdma_user_mmap_entry_get(ctx, vma);
	if (!rdma_entry)
		return -EINVAL;

	entry = to_emmap(rdma_entry);

	switch (entry->mmap_flag) {
	case ERDMA_MMAP_IO_NC:
		/* map doorbell. */
		prot = pgprot_device(vma->vm_page_prot);
		break;
	default:
		err = -EINVAL;
		goto put_entry;
	}

	err = rdma_user_mmap_io(ctx, vma, PFN_DOWN(entry->address), PAGE_SIZE,
				prot, rdma_entry);

put_entry:
	rdma_user_mmap_entry_put(rdma_entry);
	return err;
}

void erdma_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct erdma_user_mmap_entry *entry = to_emmap(rdma_entry);

	kfree(entry);
}

#define ERDMA_SDB_PAGE 0
#define ERDMA_SDB_ENTRY 1
#define ERDMA_SDB_SHARED 2

static void alloc_db_resources(struct erdma_dev *dev,
			       struct erdma_ucontext *ctx)
{
	u32 bitmap_idx;
	struct erdma_devattr *attrs = &dev->attrs;

	if (attrs->disable_dwqe)
		goto alloc_normal_db;

	/* Try to alloc independent SDB page. */
	spin_lock(&dev->db_bitmap_lock);
	bitmap_idx = find_first_zero_bit(dev->sdb_page, attrs->dwqe_pages);
	if (bitmap_idx != attrs->dwqe_pages) {
		set_bit(bitmap_idx, dev->sdb_page);
		spin_unlock(&dev->db_bitmap_lock);

		ctx->sdb_type = ERDMA_SDB_PAGE;
		ctx->sdb_idx = bitmap_idx;
		ctx->sdb_page_idx = bitmap_idx;
		ctx->sdb = dev->func_bar_addr + ERDMA_BAR_SQDB_SPACE_OFFSET +
			   (bitmap_idx << PAGE_SHIFT);
		ctx->sdb_page_off = 0;

		return;
	}

	bitmap_idx = find_first_zero_bit(dev->sdb_entry, attrs->dwqe_entries);
	if (bitmap_idx != attrs->dwqe_entries) {
		set_bit(bitmap_idx, dev->sdb_entry);
		spin_unlock(&dev->db_bitmap_lock);

		ctx->sdb_type = ERDMA_SDB_ENTRY;
		ctx->sdb_idx = bitmap_idx;
		ctx->sdb_page_idx = attrs->dwqe_pages +
				    bitmap_idx / ERDMA_DWQE_TYPE1_CNT_PER_PAGE;
		ctx->sdb_page_off = bitmap_idx % ERDMA_DWQE_TYPE1_CNT_PER_PAGE;

		ctx->sdb = dev->func_bar_addr + ERDMA_BAR_SQDB_SPACE_OFFSET +
			   (ctx->sdb_page_idx << PAGE_SHIFT);

		return;
	}

	spin_unlock(&dev->db_bitmap_lock);

alloc_normal_db:
	ctx->sdb_type = ERDMA_SDB_SHARED;
	ctx->sdb_idx = 0;
	ctx->sdb_page_idx = ERDMA_SDB_SHARED_PAGE_INDEX;
	ctx->sdb_page_off = 0;

	ctx->sdb = dev->func_bar_addr + (ctx->sdb_page_idx << PAGE_SHIFT);
}

static void erdma_uctx_user_mmap_entries_remove(struct erdma_ucontext *uctx)
{
	rdma_user_mmap_entry_remove(uctx->sq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->rq_db_mmap_entry);
	rdma_user_mmap_entry_remove(uctx->cq_db_mmap_entry);
}

int erdma_alloc_ucontext(struct ib_ucontext *ibctx, struct ib_udata *udata)
{
	struct erdma_ucontext *ctx = to_ectx(ibctx);
	struct erdma_dev *dev = to_edev(ibctx->device);
	int ret;
	struct erdma_uresp_alloc_ctx uresp = {};

	if (atomic_inc_return(&dev->num_ctx) > ERDMA_MAX_CONTEXT) {
		ret = -ENOMEM;
		goto err_out;
	}

	INIT_LIST_HEAD(&ctx->dbrecords_page_list);
	mutex_init(&ctx->dbrecords_page_mutex);

	alloc_db_resources(dev, ctx);

	ctx->rdb = dev->func_bar_addr + ERDMA_BAR_RQDB_SPACE_OFFSET;
	ctx->cdb = dev->func_bar_addr + ERDMA_BAR_CQDB_SPACE_OFFSET;

	if (udata->outlen < sizeof(uresp)) {
		ret = -EINVAL;
		goto err_out;
	}

	ctx->sq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ctx, (void *)ctx->sdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.sdb);
	if (!ctx->sq_db_mmap_entry) {
		ret = -ENOMEM;
		goto err_out;
	}

	ctx->rq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ctx, (void *)ctx->rdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.rdb);
	if (!ctx->rq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_out;
	}

	ctx->cq_db_mmap_entry = erdma_user_mmap_entry_insert(
		ctx, (void *)ctx->cdb, PAGE_SIZE, ERDMA_MMAP_IO_NC, &uresp.cdb);
	if (!ctx->cq_db_mmap_entry) {
		ret = -EINVAL;
		goto err_out;
	}

	uresp.dev_id = dev->pdev->device;
	uresp.sdb_type = ctx->sdb_type;
	uresp.sdb_offset = ctx->sdb_page_off;

	ret = ib_copy_to_udata(udata, &uresp, sizeof(uresp));
	if (ret)
		goto err_out;

	return 0;

err_out:
	erdma_uctx_user_mmap_entries_remove(ctx);
	atomic_dec(&dev->num_ctx);
	return ret;
}

void erdma_dealloc_ucontext(struct ib_ucontext *ibctx)
{
	struct erdma_ucontext *ctx = to_ectx(ibctx);
	struct erdma_dev *dev = to_edev(ibctx->device);

	spin_lock(&dev->db_bitmap_lock);
	if (ctx->sdb_type == ERDMA_SDB_PAGE)
		clear_bit(ctx->sdb_idx, dev->sdb_page);
	else if (ctx->sdb_type == ERDMA_SDB_ENTRY)
		clear_bit(ctx->sdb_idx, dev->sdb_entry);

	erdma_uctx_user_mmap_entries_remove(ctx);

	spin_unlock(&dev->db_bitmap_lock);

	atomic_dec(&dev->num_ctx);
}

static int ib_qp_state_to_erdma_qp_state[IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = ERDMA_QP_STATE_IDLE,
	[IB_QPS_INIT] = ERDMA_QP_STATE_IDLE,
	[IB_QPS_RTR] = ERDMA_QP_STATE_RTR,
	[IB_QPS_RTS] = ERDMA_QP_STATE_RTS,
	[IB_QPS_SQD] = ERDMA_QP_STATE_CLOSING,
	[IB_QPS_SQE] = ERDMA_QP_STATE_TERMINATE,
	[IB_QPS_ERR] = ERDMA_QP_STATE_ERROR
};

int erdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		    struct ib_udata *udata)
{
	struct erdma_qp_attrs new_attrs;
	enum erdma_qp_attr_mask erdma_attr_mask = 0;
	struct erdma_qp *qp = to_eqp(ibqp);
	int ret = 0;

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	memset(&new_attrs, 0, sizeof(new_attrs));

	if (attr_mask & IB_QP_STATE) {
		new_attrs.state = ib_qp_state_to_erdma_qp_state[attr->qp_state];

		erdma_attr_mask |= ERDMA_QP_ATTR_STATE;
	}

	down_write(&qp->state_lock);

	ret = erdma_modify_qp_internal(qp, &new_attrs, erdma_attr_mask);

	up_write(&qp->state_lock);

	return ret;
}

static enum ib_qp_state query_qp_state(struct erdma_qp *qp)
{
	switch (qp->attrs.state) {
	case ERDMA_QP_STATE_IDLE:
		return IB_QPS_INIT;
	case ERDMA_QP_STATE_RTR:
		return IB_QPS_RTR;
	case ERDMA_QP_STATE_RTS:
		return IB_QPS_RTS;
	case ERDMA_QP_STATE_CLOSING:
		return IB_QPS_ERR;
	case ERDMA_QP_STATE_TERMINATE:
		return IB_QPS_ERR;
	case ERDMA_QP_STATE_ERROR:
		return IB_QPS_ERR;
	default:
		return IB_QPS_ERR;
	}
}

int erdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
		   int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct erdma_dev *dev;
	struct erdma_qp *qp;

	if (ibqp && qp_attr && qp_init_attr) {
		qp = to_eqp(ibqp);
		dev = to_edev(ibqp->device);
	} else {
		return -EINVAL;
	}

	qp_attr->cap.max_inline_data = ERDMA_MAX_INLINE;
	qp_init_attr->cap.max_inline_data = ERDMA_MAX_INLINE;

	qp_attr->cap.max_send_wr = qp->attrs.sq_size;
	qp_attr->cap.max_recv_wr = qp->attrs.rq_size;
	qp_attr->cap.max_send_sge = qp->attrs.max_send_sge;
	qp_attr->cap.max_recv_sge = qp->attrs.max_recv_sge;

	qp_attr->path_mtu = ib_mtu_int_to_enum(dev->netdev->mtu);
	qp_attr->max_rd_atomic = qp->attrs.irq_size;
	qp_attr->max_dest_rd_atomic = qp->attrs.orq_size;

	qp_attr->qp_access_flags = IB_ACCESS_LOCAL_WRITE |
				   IB_ACCESS_REMOTE_WRITE |
				   IB_ACCESS_REMOTE_READ;

	qp_init_attr->cap = qp_attr->cap;

	qp_attr->qp_state = query_qp_state(qp);
	qp_attr->cur_qp_state = query_qp_state(qp);

	return 0;
}

static int erdma_init_user_cq(struct erdma_ucontext *ctx, struct erdma_cq *cq,
			      struct erdma_ureq_create_cq *ureq)
{
	int ret;
	struct erdma_dev *dev = to_edev(cq->ibcq.device);

	ret = get_mtt_entries(dev, &cq->user_cq.qbuf_mtt, ureq->qbuf_va,
			      ureq->qbuf_len, 0, ureq->qbuf_va, SZ_64M - SZ_4K,
			      1);
	if (ret)
		return ret;

	ret = erdma_map_user_dbrecords(ctx, ureq->db_record_va,
				       &cq->user_cq.user_dbr_page,
				       &cq->user_cq.db_info_dma_addr);
	if (ret)
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);

	return ret;
}

static int erdma_init_kernel_cq(struct erdma_cq *cq)
{
	struct erdma_dev *dev = to_edev(cq->ibcq.device);

	cq->kern_cq.qbuf =
		dma_alloc_coherent(&dev->pdev->dev,
				   WARPPED_BUFSIZE(cq->depth << CQE_SHIFT),
				   &cq->kern_cq.qbuf_dma_addr, GFP_KERNEL);
	if (!cq->kern_cq.qbuf)
		return -ENOMEM;

	cq->kern_cq.db_record =
		(u64 *)(cq->kern_cq.qbuf + (cq->depth << CQE_SHIFT));
	spin_lock_init(&cq->kern_cq.lock);
	/* use default cqdb addr */
	cq->kern_cq.db = dev->func_bar + ERDMA_BAR_CQDB_SPACE_OFFSET;

	return 0;
}

int erdma_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		    struct ib_udata *udata)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	struct erdma_dev *dev = to_edev(ibcq->device);
	unsigned int depth = attr->cqe;
	int ret;
	struct erdma_ucontext *ctx = rdma_udata_to_drv_context(
		udata, struct erdma_ucontext, ibucontext);

	if (depth > dev->attrs.max_cqe)
		return -EINVAL;

	depth = roundup_pow_of_two(depth);
	cq->ibcq.cqe = depth;
	cq->depth = depth;
	cq->assoc_eqn = attr->comp_vector + 1;

	ret = xa_alloc_cyclic(&dev->cq_xa, &cq->cqn, cq,
			      XA_LIMIT(1, dev->attrs.max_cq - 1),
			      &dev->next_alloc_cqn, GFP_KERNEL);
	if (ret < 0)
		return ret;

	if (!rdma_is_kernel_res(&ibcq->res)) {
		struct erdma_ureq_create_cq ureq;
		struct erdma_uresp_create_cq uresp;

		ret = ib_copy_from_udata(&ureq, udata,
					 min(udata->inlen, sizeof(ureq)));
		if (ret)
			goto err_out_xa;

		ret = erdma_init_user_cq(ctx, cq, &ureq);
		if (ret)
			goto err_out_xa;

		uresp.cq_id = cq->cqn;
		uresp.num_cqe = depth;

		ret = ib_copy_to_udata(udata, &uresp,
				       min(sizeof(uresp), udata->outlen));
		if (ret)
			goto err_free_res;
	} else {
		ret = erdma_init_kernel_cq(cq);
		if (ret)
			goto err_out_xa;
	}

	ret = create_cq_cmd(dev, cq);
	if (ret)
		goto err_free_res;

	return 0;

err_free_res:
	if (!rdma_is_kernel_res(&ibcq->res)) {
		erdma_unmap_user_dbrecords(ctx, &cq->user_cq.user_dbr_page);
		put_mtt_entries(dev, &cq->user_cq.qbuf_mtt);
	} else {
		dma_free_coherent(&dev->pdev->dev,
				  WARPPED_BUFSIZE(depth << CQE_SHIFT),
				  cq->kern_cq.qbuf, cq->kern_cq.qbuf_dma_addr);
	}

err_out_xa:
	xa_erase(&dev->cq_xa, cq->cqn);

	return ret;
}

void erdma_set_mtu(struct erdma_dev *dev, u32 mtu)
{
	struct erdma_cmdq_config_mtu_req req;

	erdma_cmdq_build_reqhdr(&req.hdr, CMDQ_SUBMOD_COMMON,
				CMDQ_OPCODE_CONF_MTU);
	req.mtu = mtu;

	erdma_post_cmd_wait(&dev->cmdq, &req, sizeof(req), NULL, NULL);
}

void erdma_port_event(struct erdma_dev *dev, enum ib_event_type reason)
{
	struct ib_event event;

	event.device = &dev->ibdev;
	event.element.port_num = 1;
	event.event = reason;

	ib_dispatch_event(&event);
}
