// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018 Hisilicon Limited.
 */

#include <linux/pci.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_ioctl.h>
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
		refcount_inc(&srq->refcount);
	xa_unlock(&srq_table->xa);

	if (!srq) {
		dev_warn(hr_dev->dev, "Async event for bogus SRQ %08x\n", srqn);
		return;
	}

	srq->event(srq, event_type);

	if (refcount_dec_and_test(&srq->refcount))
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

static int alloc_srqn(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq)
{
	struct hns_roce_ida *srq_ida = &hr_dev->srq_table.srq_ida;
	int id;

	id = ida_alloc_range(&srq_ida->ida, srq_ida->min, srq_ida->max,
			     GFP_KERNEL);
	if (id < 0) {
		ibdev_err(&hr_dev->ib_dev, "failed to alloc srq(%d).\n", id);
		return -ENOMEM;
	}

	srq->srqn = id;

	return 0;
}

static void free_srqn(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq)
{
	ida_free(&hr_dev->srq_table.srq_ida.ida, (int)srq->srqn);
}

static int hns_roce_create_srqc(struct hns_roce_dev *hr_dev,
				struct hns_roce_srq *srq)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox)) {
		ibdev_err(ibdev, "failed to alloc mailbox for SRQC.\n");
		return PTR_ERR(mailbox);
	}

	ret = hr_dev->hw->write_srqc(srq, mailbox->buf);
	if (ret) {
		ibdev_err(ibdev, "failed to write SRQC.\n");
		goto err_mbox;
	}

	ret = hns_roce_create_hw_ctx(hr_dev, mailbox, HNS_ROCE_CMD_CREATE_SRQ,
				     srq->srqn);
	if (ret)
		ibdev_err(ibdev, "failed to config SRQC, ret = %d.\n", ret);

err_mbox:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int alloc_srqc(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int ret;

	ret = hns_roce_table_get(hr_dev, &srq_table->table, srq->srqn);
	if (ret) {
		ibdev_err(ibdev, "failed to get SRQC table, ret = %d.\n", ret);
		return ret;
	}

	ret = xa_err(xa_store_irq(&srq_table->xa, srq->srqn, srq, GFP_KERNEL));
	if (ret) {
		ibdev_err(ibdev, "failed to store SRQC, ret = %d.\n", ret);
		goto err_put;
	}

	ret = hns_roce_create_srqc(hr_dev, srq);
	if (ret)
		goto err_xa;

	return 0;

err_xa:
	xa_erase_irq(&srq_table->xa, srq->srqn);
err_put:
	hns_roce_table_put(hr_dev, &srq_table->table, srq->srqn);

	return ret;
}

static void free_srqc(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;
	int ret;

	ret = hns_roce_destroy_hw_ctx(hr_dev, HNS_ROCE_CMD_DESTROY_SRQ,
				      srq->srqn);
	if (ret)
		dev_err_ratelimited(hr_dev->dev, "DESTROY_SRQ failed (%d) for SRQN %06lx\n",
				    ret, srq->srqn);

	xa_erase_irq(&srq_table->xa, srq->srqn);

	if (refcount_dec_and_test(&srq->refcount))
		complete(&srq->free);
	wait_for_completion(&srq->free);

	hns_roce_table_put(hr_dev, &srq_table->table, srq->srqn);
}

static int alloc_srq_idx(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq,
			 struct ib_udata *udata, unsigned long addr)
{
	struct hns_roce_idx_que *idx_que = &srq->idx_que;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_buf_attr buf_attr = {};
	int ret;

	srq->idx_que.entry_shift = ilog2(HNS_ROCE_IDX_QUE_ENTRY_SZ);

	buf_attr.page_shift = hr_dev->caps.idx_buf_pg_sz + PAGE_SHIFT;
	buf_attr.region[0].size = to_hr_hem_entries_size(srq->wqe_cnt,
					srq->idx_que.entry_shift);
	buf_attr.region[0].hopnum = hr_dev->caps.idx_hop_num;
	buf_attr.region_count = 1;

	ret = hns_roce_mtr_create(hr_dev, &idx_que->mtr, &buf_attr,
				  hr_dev->caps.idx_ba_pg_sz + PAGE_SHIFT,
				  udata, addr);
	if (ret) {
		ibdev_err(ibdev,
			  "failed to alloc SRQ idx mtr, ret = %d.\n", ret);
		return ret;
	}

	if (!udata) {
		idx_que->bitmap = bitmap_zalloc(srq->wqe_cnt, GFP_KERNEL);
		if (!idx_que->bitmap) {
			ibdev_err(ibdev, "failed to alloc SRQ idx bitmap.\n");
			ret = -ENOMEM;
			goto err_idx_mtr;
		}
	}

	idx_que->head = 0;
	idx_que->tail = 0;

	return 0;
err_idx_mtr:
	hns_roce_mtr_destroy(hr_dev, &idx_que->mtr);

	return ret;
}

static void free_srq_idx(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq)
{
	struct hns_roce_idx_que *idx_que = &srq->idx_que;

	bitmap_free(idx_que->bitmap);
	idx_que->bitmap = NULL;
	hns_roce_mtr_destroy(hr_dev, &idx_que->mtr);
}

static int alloc_srq_wqe_buf(struct hns_roce_dev *hr_dev,
			     struct hns_roce_srq *srq,
			     struct ib_udata *udata, unsigned long addr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_buf_attr buf_attr = {};
	int ret;

	srq->wqe_shift = ilog2(roundup_pow_of_two(max(HNS_ROCE_SGE_SIZE,
						      HNS_ROCE_SGE_SIZE *
						      srq->max_gs)));

	buf_attr.page_shift = hr_dev->caps.srqwqe_buf_pg_sz + PAGE_SHIFT;
	buf_attr.region[0].size = to_hr_hem_entries_size(srq->wqe_cnt,
							 srq->wqe_shift);
	buf_attr.region[0].hopnum = hr_dev->caps.srqwqe_hop_num;
	buf_attr.region_count = 1;

	ret = hns_roce_mtr_create(hr_dev, &srq->buf_mtr, &buf_attr,
				  hr_dev->caps.srqwqe_ba_pg_sz + PAGE_SHIFT,
				  udata, addr);
	if (ret)
		ibdev_err(ibdev,
			  "failed to alloc SRQ buf mtr, ret = %d.\n", ret);

	return ret;
}

static void free_srq_wqe_buf(struct hns_roce_dev *hr_dev,
			     struct hns_roce_srq *srq)
{
	hns_roce_mtr_destroy(hr_dev, &srq->buf_mtr);
}

static int alloc_srq_wrid(struct hns_roce_srq *srq)
{
	srq->wrid = kvmalloc_array(srq->wqe_cnt, sizeof(u64), GFP_KERNEL);
	if (!srq->wrid)
		return -ENOMEM;

	return 0;
}

static void free_srq_wrid(struct hns_roce_srq *srq)
{
	kvfree(srq->wrid);
	srq->wrid = NULL;
}

static u32 proc_srq_sge(struct hns_roce_dev *dev, struct hns_roce_srq *hr_srq,
			bool user)
{
	u32 max_sge = dev->caps.max_srq_sges;

	if (dev->pci_dev->revision >= PCI_REVISION_ID_HIP09)
		return max_sge;

	/* Reserve SGEs only for HIP08 in kernel; The userspace driver will
	 * calculate number of max_sge with reserved SGEs when allocating wqe
	 * buf, so there is no need to do this again in kernel. But the number
	 * may exceed the capacity of SGEs recorded in the firmware, so the
	 * kernel driver should just adapt the value accordingly.
	 */
	if (user)
		max_sge = roundup_pow_of_two(max_sge + 1);
	else
		hr_srq->rsv_sge = 1;

	return max_sge;
}

static int set_srq_basic_param(struct hns_roce_srq *srq,
			       struct ib_srq_init_attr *init_attr,
			       struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(srq->ibsrq.device);
	struct ib_srq_attr *attr = &init_attr->attr;
	u32 max_sge;

	max_sge = proc_srq_sge(hr_dev, srq, !!udata);
	if (attr->max_wr > hr_dev->caps.max_srq_wrs ||
	    attr->max_sge > max_sge || !attr->max_sge) {
		ibdev_err(&hr_dev->ib_dev,
			  "invalid SRQ attr, depth = %u, sge = %u.\n",
			  attr->max_wr, attr->max_sge);
		return -EINVAL;
	}

	attr->max_wr = max_t(u32, attr->max_wr, HNS_ROCE_MIN_SRQ_WQE_NUM);
	srq->wqe_cnt = roundup_pow_of_two(attr->max_wr);
	srq->max_gs = roundup_pow_of_two(attr->max_sge + srq->rsv_sge);

	attr->max_wr = srq->wqe_cnt;
	attr->max_sge = srq->max_gs - srq->rsv_sge;
	attr->srq_limit = 0;

	return 0;
}

static void set_srq_ext_param(struct hns_roce_srq *srq,
			      struct ib_srq_init_attr *init_attr)
{
	srq->cqn = ib_srq_has_cq(init_attr->srq_type) ?
		   to_hr_cq(init_attr->ext.cq)->cqn : 0;

	srq->xrcdn = (init_attr->srq_type == IB_SRQT_XRC) ?
		     to_hr_xrcd(init_attr->ext.xrc.xrcd)->xrcdn : 0;
}

static int set_srq_param(struct hns_roce_srq *srq,
			 struct ib_srq_init_attr *init_attr,
			 struct ib_udata *udata)
{
	int ret;

	ret = set_srq_basic_param(srq, init_attr, udata);
	if (ret)
		return ret;

	set_srq_ext_param(srq, init_attr);

	return 0;
}

static int alloc_srq_buf(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq,
			 struct ib_udata *udata)
{
	struct hns_roce_ib_create_srq ucmd = {};
	int ret;

	if (udata) {
		ret = ib_copy_from_udata(&ucmd, udata,
					 min(udata->inlen, sizeof(ucmd)));
		if (ret) {
			ibdev_err(&hr_dev->ib_dev,
				  "failed to copy SRQ udata, ret = %d.\n",
				  ret);
			return ret;
		}
	}

	ret = alloc_srq_idx(hr_dev, srq, udata, ucmd.que_addr);
	if (ret)
		return ret;

	ret = alloc_srq_wqe_buf(hr_dev, srq, udata, ucmd.buf_addr);
	if (ret)
		goto err_idx;

	if (!udata) {
		ret = alloc_srq_wrid(srq);
		if (ret)
			goto err_wqe_buf;
	}

	return 0;

err_wqe_buf:
	free_srq_wqe_buf(hr_dev, srq);
err_idx:
	free_srq_idx(hr_dev, srq);

	return ret;
}

static void free_srq_buf(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq)
{
	free_srq_wrid(srq);
	free_srq_wqe_buf(hr_dev, srq);
	free_srq_idx(hr_dev, srq);
}

static int get_srq_ucmd(struct hns_roce_srq *srq, struct ib_udata *udata,
			struct hns_roce_ib_create_srq *ucmd)
{
	struct ib_device *ibdev = srq->ibsrq.device;
	int ret;

	ret = ib_copy_from_udata(ucmd, udata, min(udata->inlen, sizeof(*ucmd)));
	if (ret) {
		ibdev_err(ibdev, "failed to copy SRQ udata, ret = %d.\n", ret);
		return ret;
	}

	return 0;
}

static void free_srq_db(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq,
			struct ib_udata *udata)
{
	struct hns_roce_ucontext *uctx;

	if (!(srq->cap_flags & HNS_ROCE_SRQ_CAP_RECORD_DB))
		return;

	srq->cap_flags &= ~HNS_ROCE_SRQ_CAP_RECORD_DB;
	if (udata) {
		uctx = rdma_udata_to_drv_context(udata,
						 struct hns_roce_ucontext,
						 ibucontext);
		hns_roce_db_unmap_user(uctx, &srq->rdb);
	} else {
		hns_roce_free_db(hr_dev, &srq->rdb);
	}
}

static int alloc_srq_db(struct hns_roce_dev *hr_dev, struct hns_roce_srq *srq,
			struct ib_udata *udata,
			struct hns_roce_ib_create_srq_resp *resp)
{
	struct hns_roce_ib_create_srq ucmd = {};
	struct hns_roce_ucontext *uctx;
	int ret;

	if (udata) {
		ret = get_srq_ucmd(srq, udata, &ucmd);
		if (ret)
			return ret;

		if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ_RECORD_DB) &&
		    (ucmd.req_cap_flags & HNS_ROCE_SRQ_CAP_RECORD_DB)) {
			uctx = rdma_udata_to_drv_context(udata,
					struct hns_roce_ucontext, ibucontext);
			ret = hns_roce_db_map_user(uctx, ucmd.db_addr,
						   &srq->rdb);
			if (ret)
				return ret;

			srq->cap_flags |= HNS_ROCE_RSP_SRQ_CAP_RECORD_DB;
		}
	} else {
		if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ_RECORD_DB) {
			ret = hns_roce_alloc_db(hr_dev, &srq->rdb, 1);
			if (ret)
				return ret;

			*srq->rdb.db_record = 0;
			srq->cap_flags |= HNS_ROCE_RSP_SRQ_CAP_RECORD_DB;
		}
		srq->db_reg = hr_dev->reg_base + SRQ_DB_REG;
	}

	return 0;
}

int hns_roce_create_srq(struct ib_srq *ib_srq,
			struct ib_srq_init_attr *init_attr,
			struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_srq->device);
	struct hns_roce_ib_create_srq_resp resp = {};
	struct hns_roce_srq *srq = to_hr_srq(ib_srq);
	int ret;

	mutex_init(&srq->mutex);
	spin_lock_init(&srq->lock);

	ret = set_srq_param(srq, init_attr, udata);
	if (ret)
		goto err_out;

	ret = alloc_srq_buf(hr_dev, srq, udata);
	if (ret)
		goto err_out;

	ret = alloc_srq_db(hr_dev, srq, udata, &resp);
	if (ret)
		goto err_srq_buf;

	ret = alloc_srqn(hr_dev, srq);
	if (ret)
		goto err_srq_db;

	ret = alloc_srqc(hr_dev, srq);
	if (ret)
		goto err_srqn;

	if (udata) {
		resp.cap_flags = srq->cap_flags;
		resp.srqn = srq->srqn;
		if (ib_copy_to_udata(udata, &resp,
				     min(udata->outlen, sizeof(resp)))) {
			ret = -EFAULT;
			goto err_srqc;
		}
	}

	srq->event = hns_roce_ib_srq_event;
	refcount_set(&srq->refcount, 1);
	init_completion(&srq->free);

	return 0;

err_srqc:
	free_srqc(hr_dev, srq);
err_srqn:
	free_srqn(hr_dev, srq);
err_srq_db:
	free_srq_db(hr_dev, srq, udata);
err_srq_buf:
	free_srq_buf(hr_dev, srq);
err_out:
	mutex_destroy(&srq->mutex);
	atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_SRQ_CREATE_ERR_CNT]);

	return ret;
}

int hns_roce_destroy_srq(struct ib_srq *ibsrq, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibsrq->device);
	struct hns_roce_srq *srq = to_hr_srq(ibsrq);

	free_srqc(hr_dev, srq);
	free_srqn(hr_dev, srq);
	free_srq_db(hr_dev, srq, udata);
	free_srq_buf(hr_dev, srq);
	mutex_destroy(&srq->mutex);
	return 0;
}

void hns_roce_init_srq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_srq_table *srq_table = &hr_dev->srq_table;
	struct hns_roce_ida *srq_ida = &srq_table->srq_ida;

	xa_init(&srq_table->xa);

	ida_init(&srq_ida->ida);
	srq_ida->max = hr_dev->caps.num_srqs - 1;
	srq_ida->min = hr_dev->caps.reserved_srqs;
}
