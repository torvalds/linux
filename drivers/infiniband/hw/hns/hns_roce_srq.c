// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018 Hisilicon Limited.
 */

#include <rdma/ib_umem.h>
#include <rdma/hns-abi.h>
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"

void hns_roce_srq_event(struct hns_roce_dev *hr_dev, u32 srqn, int event_type)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;
	struct hns_roce_srq *srq;

	xa_lock(&srq_table->xa);
	srq = xa_load(&srq_table->xa, srqn & (hr_dev->caps.num_srqs - 1));
	if (srq)
		atomic_inc(&srq->refcount);
	xa_unlock(&srq_table->xa);

	if (!srq) {
		dev_warn(hr_dev->dev, "Async event for bogus SRQ %08x\n", srqn);
		return;
	}

	srq->event(srq, event_type);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
}

static void hns_roce_ib_srq_event(struct hns_roce_srq *srq,
				  enum hns_roce_event event_type)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(srq->ibsrq.device);
	struct ib_srq *ibsrq = &srq->ibsrq;
	struct ib_event event;

	if (ibsrq->event_handler) {
		event.device      = ibsrq->device;
		event.element.srq = ibsrq;
		switch (event_type) {
		case HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH:
			event.event = IB_EVENT_SRQ_LIMIT_REACHED;
			break;
		case HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR:
			event.event = IB_EVENT_SRQ_ERR;
			break;
		default:
			dev_err(hr_dev->dev,
			   "hns_roce:Unexpected event type 0x%x on SRQ %06lx\n",
			   event_type, srq->srqn);
			return;
		}

		ibsrq->event_handler(&event, ibsrq->srq_context);
	}
}

static int hns_roce_sw2hw_srq(struct hns_roce_dev *dev,
			      struct hns_roce_cmd_mailbox *mailbox,
			      unsigned long srq_num)
{
	return hns_roce_cmd_mbox(dev, mailbox->dma, 0, srq_num, 0,
				 HNS_ROCE_CMD_SW2HW_SRQ,
				 HNS_ROCE_CMD_TIMEOUT_MSECS);
}

static int hns_roce_hw2sw_srq(struct hns_roce_dev *dev,
			     struct hns_roce_cmd_mailbox *mailbox,
			     unsigned long srq_num)
{
	return hns_roce_cmd_mbox(dev, 0, mailbox ? mailbox->dma : 0, srq_num,
				 mailbox ? 0 : 1, HNS_ROCE_CMD_HW2SW_SRQ,
				 HNS_ROCE_CMD_TIMEOUT_MSECS);
}

static int hns_roce_srq_alloc(struct hns_roce_dev *hr_dev, u32 pdn, u32 cqn,
			      u16 xrcd, struct hns_roce_mtt *hr_mtt,
			      u64 db_rec_addr, struct hns_roce_srq *srq)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;
	struct hns_roce_cmd_mailbox *mailbox;
	dma_addr_t dma_handle_wqe;
	dma_addr_t dma_handle_idx;
	u64 *mtts_wqe;
	u64 *mtts_idx;
	int ret;

	/* Get the physical address of srq buf */
	mtts_wqe = hns_roce_table_find(hr_dev,
				       &hr_dev->mr_table.mtt_srqwqe_table,
				       srq->mtt.first_seg,
				       &dma_handle_wqe);
	if (!mtts_wqe) {
		dev_err(hr_dev->dev,
			"SRQ alloc.Failed to find srq buf addr.\n");
		return -EINVAL;
	}

	/* Get physical address of idx que buf */
	mtts_idx = hns_roce_table_find(hr_dev, &hr_dev->mr_table.mtt_idx_table,
				       srq->idx_que.mtt.first_seg,
				       &dma_handle_idx);
	if (!mtts_idx) {
		dev_err(hr_dev->dev,
			"SRQ alloc.Failed to find idx que buf addr.\n");
		return -EINVAL;
	}

	ret = hns_roce_bitmap_alloc(&srq_table->bitmap, &srq->srqn);
	if (ret == -1) {
		dev_err(hr_dev->dev, "SRQ alloc.Failed to alloc index.\n");
		return -ENOMEM;
	}

	ret = hns_roce_table_get(hr_dev, &srq_table->table, srq->srqn);
	if (ret)
		goto err_out;

	ret = xa_err(xa_store(&srq_table->xa, srq->srqn, srq, GFP_KERNEL));
	if (ret)
		goto err_put;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox)) {
		ret = PTR_ERR(mailbox);
		goto err_xa;
	}

	hr_dev->hw->write_srqc(hr_dev, srq, pdn, xrcd, cqn, mailbox->buf,
			       mtts_wqe, mtts_idx, dma_handle_wqe,
			       dma_handle_idx);

	ret = hns_roce_sw2hw_srq(hr_dev, mailbox, srq->srqn);
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	if (ret)
		goto err_xa;

	atomic_set(&srq->refcount, 1);
	init_completion(&srq->free);
	return ret;

err_xa:
	xa_erase(&srq_table->xa, srq->srqn);

err_put:
	hns_roce_table_put(hr_dev, &srq_table->table, srq->srqn);

err_out:
	hns_roce_bitmap_free(&srq_table->bitmap, srq->srqn, BITMAP_NO_RR);
	return ret;
}

static void hns_roce_srq_free(struct hns_roce_dev *hr_dev,
			      struct hns_roce_srq *srq)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;
	int ret;

	ret = hns_roce_hw2sw_srq(hr_dev, NULL, srq->srqn);
	if (ret)
		dev_err(hr_dev->dev, "HW2SW_SRQ failed (%d) for CQN %06lx\n",
			ret, srq->srqn);

	xa_erase(&srq_table->xa, srq->srqn);

	if (atomic_dec_and_test(&srq->refcount))
		complete(&srq->free);
	wait_for_completion(&srq->free);

	hns_roce_table_put(hr_dev, &srq_table->table, srq->srqn);
	hns_roce_bitmap_free(&srq_table->bitmap, srq->srqn, BITMAP_NO_RR);
}

static int create_user_srq(struct hns_roce_srq *srq, struct ib_udata *udata,
			   int srq_buf_size)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(srq->ibsrq.device);
	struct hns_roce_ib_create_srq  ucmd;
	struct hns_roce_buf *buf;
	int ret;

	if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd)))
		return -EFAULT;

	srq->umem = ib_umem_get(udata, ucmd.buf_addr, srq_buf_size, 0, 0);
	if (IS_ERR(srq->umem))
		return PTR_ERR(srq->umem);

	buf = &srq->buf;
	buf->npages = (ib_umem_page_count(srq->umem) +
		       (1 << hr_dev->caps.srqwqe_buf_pg_sz) - 1) /
		      (1 << hr_dev->caps.srqwqe_buf_pg_sz);
	buf->page_shift = PAGE_SHIFT + hr_dev->caps.srqwqe_buf_pg_sz;
	ret = hns_roce_mtt_init(hr_dev, buf->npages, buf->page_shift,
				&srq->mtt);
	if (ret)
		goto err_user_buf;

	ret = hns_roce_ib_umem_write_mtt(hr_dev, &srq->mtt, srq->umem);
	if (ret)
		goto err_user_srq_mtt;

	/* config index queue BA */
	srq->idx_que.umem = ib_umem_get(udata, ucmd.que_addr,
					srq->idx_que.buf_size, 0, 0);
	if (IS_ERR(srq->idx_que.umem)) {
		dev_err(hr_dev->dev, "ib_umem_get error for index queue\n");
		ret = PTR_ERR(srq->idx_que.umem);
		goto err_user_srq_mtt;
	}

	buf = &srq->idx_que.idx_buf;
	buf->npages = DIV_ROUND_UP(ib_umem_page_count(srq->idx_que.umem),
				   1 << hr_dev->caps.idx_buf_pg_sz);
	buf->page_shift = PAGE_SHIFT + hr_dev->caps.idx_buf_pg_sz;
	ret = hns_roce_mtt_init(hr_dev, buf->npages, buf->page_shift,
				&srq->idx_que.mtt);
	if (ret) {
		dev_err(hr_dev->dev, "hns_roce_mtt_init error for idx que\n");
		goto err_user_idx_mtt;
	}

	ret = hns_roce_ib_umem_write_mtt(hr_dev, &srq->idx_que.mtt,
					 srq->idx_que.umem);
	if (ret) {
		dev_err(hr_dev->dev,
			"hns_roce_ib_umem_write_mtt error for idx que\n");
		goto err_user_idx_buf;
	}

	return 0;

err_user_idx_buf:
	hns_roce_mtt_cleanup(hr_dev, &srq->idx_que.mtt);

err_user_idx_mtt:
	ib_umem_release(srq->idx_que.umem);

err_user_srq_mtt:
	hns_roce_mtt_cleanup(hr_dev, &srq->mtt);

err_user_buf:
	ib_umem_release(srq->umem);

	return ret;
}

static int hns_roce_create_idx_que(struct ib_pd *pd, struct hns_roce_srq *srq,
				   u32 page_shift)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct hns_roce_idx_que *idx_que = &srq->idx_que;

	idx_que->bitmap = bitmap_zalloc(srq->max, GFP_KERNEL);
	if (!idx_que->bitmap)
		return -ENOMEM;

	idx_que->buf_size = srq->idx_que.buf_size;

	if (hns_roce_buf_alloc(hr_dev, idx_que->buf_size, (1 << page_shift) * 2,
			       &idx_que->idx_buf, page_shift)) {
		bitmap_free(idx_que->bitmap);
		return -ENOMEM;
	}

	return 0;
}

static int create_kernel_srq(struct hns_roce_srq *srq, int srq_buf_size)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(srq->ibsrq.device);
	u32 page_shift = PAGE_SHIFT + hr_dev->caps.srqwqe_buf_pg_sz;
	int ret;

	if (hns_roce_buf_alloc(hr_dev, srq_buf_size, (1 << page_shift) * 2,
			       &srq->buf, page_shift))
		return -ENOMEM;

	srq->head = 0;
	srq->tail = srq->max - 1;

	ret = hns_roce_mtt_init(hr_dev, srq->buf.npages, srq->buf.page_shift,
				&srq->mtt);
	if (ret)
		goto err_kernel_buf;

	ret = hns_roce_buf_write_mtt(hr_dev, &srq->mtt, &srq->buf);
	if (ret)
		goto err_kernel_srq_mtt;

	page_shift = PAGE_SHIFT + hr_dev->caps.idx_buf_pg_sz;
	ret = hns_roce_create_idx_que(srq->ibsrq.pd, srq, page_shift);
	if (ret) {
		dev_err(hr_dev->dev, "Create idx queue fail(%d)!\n", ret);
		goto err_kernel_srq_mtt;
	}

	/* Init mtt table for idx_que */
	ret = hns_roce_mtt_init(hr_dev, srq->idx_que.idx_buf.npages,
				srq->idx_que.idx_buf.page_shift,
				&srq->idx_que.mtt);
	if (ret)
		goto err_kernel_create_idx;

	/* Write buffer address into the mtt table */
	ret = hns_roce_buf_write_mtt(hr_dev, &srq->idx_que.mtt,
				     &srq->idx_que.idx_buf);
	if (ret)
		goto err_kernel_idx_buf;

	srq->wrid = kvmalloc_array(srq->max, sizeof(u64), GFP_KERNEL);
	if (!srq->wrid) {
		ret = -ENOMEM;
		goto err_kernel_idx_buf;
	}

	return 0;

err_kernel_idx_buf:
	hns_roce_mtt_cleanup(hr_dev, &srq->idx_que.mtt);

err_kernel_create_idx:
	hns_roce_buf_free(hr_dev, srq->idx_que.buf_size,
			  &srq->idx_que.idx_buf);
	kfree(srq->idx_que.bitmap);

err_kernel_srq_mtt:
	hns_roce_mtt_cleanup(hr_dev, &srq->mtt);

err_kernel_buf:
	hns_roce_buf_free(hr_dev, srq_buf_size, &srq->buf);

	return ret;
}

static void destroy_user_srq(struct hns_roce_dev *hr_dev,
			     struct hns_roce_srq *srq)
{
	hns_roce_mtt_cleanup(hr_dev, &srq->idx_que.mtt);
	ib_umem_release(srq->idx_que.umem);
	hns_roce_mtt_cleanup(hr_dev, &srq->mtt);
	ib_umem_release(srq->umem);
}

static void destroy_kernel_srq(struct hns_roce_dev *hr_dev,
			       struct hns_roce_srq *srq, int srq_buf_size)
{
	kvfree(srq->wrid);
	hns_roce_mtt_cleanup(hr_dev, &srq->idx_que.mtt);
	hns_roce_buf_free(hr_dev, srq->idx_que.buf_size, &srq->idx_que.idx_buf);
	kfree(srq->idx_que.bitmap);
	hns_roce_mtt_cleanup(hr_dev, &srq->mtt);
	hns_roce_buf_free(hr_dev, srq_buf_size, &srq->buf);
}

int hns_roce_create_srq(struct ib_srq *ib_srq,
			struct ib_srq_init_attr *srq_init_attr,
			struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_srq->device);
	struct hns_roce_ib_create_srq_resp resp = {};
	struct hns_roce_srq *srq = to_hr_srq(ib_srq);
	int srq_desc_size;
	int srq_buf_size;
	int ret = 0;
	u32 cqn;

	/* Check the actual SRQ wqe and SRQ sge num */
	if (srq_init_attr->attr.max_wr >= hr_dev->caps.max_srq_wrs ||
	    srq_init_attr->attr.max_sge > hr_dev->caps.max_srq_sges)
		return -EINVAL;

	mutex_init(&srq->mutex);
	spin_lock_init(&srq->lock);

	srq->max = roundup_pow_of_two(srq_init_attr->attr.max_wr + 1);
	srq->max_gs = srq_init_attr->attr.max_sge;

	srq_desc_size = max(16, 16 * srq->max_gs);

	srq->wqe_shift = ilog2(srq_desc_size);

	srq_buf_size = srq->max * srq_desc_size;

	srq->idx_que.entry_sz = HNS_ROCE_IDX_QUE_ENTRY_SZ;
	srq->idx_que.buf_size = srq->max * srq->idx_que.entry_sz;
	srq->mtt.mtt_type = MTT_TYPE_SRQWQE;
	srq->idx_que.mtt.mtt_type = MTT_TYPE_IDX;

	if (udata) {
		ret = create_user_srq(srq, udata, srq_buf_size);
		if (ret) {
			dev_err(hr_dev->dev, "Create user srq failed\n");
			goto err_srq;
		}
	} else {
		ret = create_kernel_srq(srq, srq_buf_size);
		if (ret) {
			dev_err(hr_dev->dev, "Create kernel srq failed\n");
			goto err_srq;
		}
	}

	cqn = ib_srq_has_cq(srq_init_attr->srq_type) ?
	      to_hr_cq(srq_init_attr->ext.cq)->cqn : 0;

	srq->db_reg_l = hr_dev->reg_base + SRQ_DB_REG;

	ret = hns_roce_srq_alloc(hr_dev, to_hr_pd(ib_srq->pd)->pdn, cqn, 0,
				 &srq->mtt, 0, srq);
	if (ret)
		goto err_wrid;

	srq->event = hns_roce_ib_srq_event;
	resp.srqn = srq->srqn;

	if (udata) {
		if (ib_copy_to_udata(udata, &resp,
				     min(udata->outlen, sizeof(resp)))) {
			ret = -EFAULT;
			goto err_srqc_alloc;
		}
	}

	return 0;

err_srqc_alloc:
	hns_roce_srq_free(hr_dev, srq);

err_wrid:
	if (udata)
		destroy_user_srq(hr_dev, srq);
	else
		destroy_kernel_srq(hr_dev, srq, srq_buf_size);

err_srq:
	return ret;
}

void hns_roce_destroy_srq(struct ib_srq *ibsrq, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibsrq->device);
	struct hns_roce_srq *srq = to_hr_srq(ibsrq);

	hns_roce_srq_free(hr_dev, srq);
	hns_roce_mtt_cleanup(hr_dev, &srq->mtt);

	if (udata) {
		hns_roce_mtt_cleanup(hr_dev, &srq->idx_que.mtt);
	} else {
		kvfree(srq->wrid);
		hns_roce_buf_free(hr_dev, srq->max << srq->wqe_shift,
				  &srq->buf);
	}
	ib_umem_release(srq->idx_que.umem);
	ib_umem_release(srq->umem);
}

int hns_roce_init_srq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;

	xa_init(&srq_table->xa);

	return hns_roce_bitmap_init(&srq_table->bitmap, hr_dev->caps.num_srqs,
				    hr_dev->caps.num_srqs - 1,
				    hr_dev->caps.reserved_srqs, 0);
}

void hns_roce_cleanup_srq_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->srq_table.bitmap);
}
