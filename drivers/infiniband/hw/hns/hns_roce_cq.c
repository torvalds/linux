/*
 * Copyright (c) 2016 Hisilicon Limited.
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
 *        disclaimer in the documentation and/or other materials
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

#include <linux/platform_device.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_ioctl.h>
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"
#include <rdma/hns-abi.h>
#include "hns_roce_common.h"

static void hns_roce_ib_cq_comp(struct hns_roce_cq *hr_cq)
{
	struct ib_cq *ibcq = &hr_cq->ib_cq;

	ibcq->comp_handler(ibcq, ibcq->cq_context);
}

static void hns_roce_ib_cq_event(struct hns_roce_cq *hr_cq,
				 enum hns_roce_event event_type)
{
	struct hns_roce_dev *hr_dev;
	struct ib_event event;
	struct ib_cq *ibcq;

	ibcq = &hr_cq->ib_cq;
	hr_dev = to_hr_dev(ibcq->device);

	if (event_type != HNS_ROCE_EVENT_TYPE_CQ_ID_INVALID &&
	    event_type != HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR &&
	    event_type != HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW) {
		dev_err(hr_dev->dev,
			"hns_roce_ib: Unexpected event type 0x%x on CQ %06lx\n",
			event_type, hr_cq->cqn);
		return;
	}

	if (ibcq->event_handler) {
		event.device = ibcq->device;
		event.event = IB_EVENT_CQ_ERR;
		event.element.cq = ibcq;
		ibcq->event_handler(&event, ibcq->cq_context);
	}
}

static int hns_roce_sw2hw_cq(struct hns_roce_dev *dev,
			     struct hns_roce_cmd_mailbox *mailbox,
			     unsigned long cq_num)
{
	return hns_roce_cmd_mbox(dev, mailbox->dma, 0, cq_num, 0,
			    HNS_ROCE_CMD_SW2HW_CQ, HNS_ROCE_CMD_TIMEOUT_MSECS);
}

static int hns_roce_cq_alloc(struct hns_roce_dev *hr_dev, int nent,
			     struct hns_roce_mtt *hr_mtt,
			     struct hns_roce_cq *hr_cq, int vector)
{
	struct hns_roce_cmd_mailbox *mailbox;
	struct hns_roce_hem_table *mtt_table;
	struct hns_roce_cq_table *cq_table;
	struct device *dev = hr_dev->dev;
	dma_addr_t dma_handle;
	u64 *mtts;
	int ret;

	cq_table = &hr_dev->cq_table;

	/* Get the physical address of cq buf */
	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE))
		mtt_table = &hr_dev->mr_table.mtt_cqe_table;
	else
		mtt_table = &hr_dev->mr_table.mtt_table;

	mtts = hns_roce_table_find(hr_dev, mtt_table,
				   hr_mtt->first_seg, &dma_handle);
	if (!mtts) {
		dev_err(dev, "CQ alloc.Failed to find cq buf addr.\n");
		return -EINVAL;
	}

	if (vector >= hr_dev->caps.num_comp_vectors) {
		dev_err(dev, "CQ alloc.Invalid vector.\n");
		return -EINVAL;
	}
	hr_cq->vector = vector;

	ret = hns_roce_bitmap_alloc(&cq_table->bitmap, &hr_cq->cqn);
	if (ret == -1) {
		dev_err(dev, "CQ alloc.Failed to alloc index.\n");
		return -ENOMEM;
	}

	/* Get CQC memory HEM(Hardware Entry Memory) table */
	ret = hns_roce_table_get(hr_dev, &cq_table->table, hr_cq->cqn);
	if (ret) {
		dev_err(dev, "CQ alloc.Failed to get context mem.\n");
		goto err_out;
	}

	ret = xa_err(xa_store(&cq_table->array, hr_cq->cqn, hr_cq, GFP_KERNEL));
	if (ret) {
		dev_err(dev, "CQ alloc failed xa_store.\n");
		goto err_put;
	}

	/* Allocate mailbox memory */
	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox)) {
		ret = PTR_ERR(mailbox);
		goto err_xa;
	}

	hr_dev->hw->write_cqc(hr_dev, hr_cq, mailbox->buf, mtts, dma_handle,
			      nent, vector);

	/* Send mailbox to hw */
	ret = hns_roce_sw2hw_cq(hr_dev, mailbox, hr_cq->cqn);
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	if (ret) {
		dev_err(dev, "CQ alloc.Failed to cmd mailbox.\n");
		goto err_xa;
	}

	hr_cq->cons_index = 0;
	hr_cq->arm_sn = 1;

	atomic_set(&hr_cq->refcount, 1);
	init_completion(&hr_cq->free);

	return 0;

err_xa:
	xa_erase(&cq_table->array, hr_cq->cqn);

err_put:
	hns_roce_table_put(hr_dev, &cq_table->table, hr_cq->cqn);

err_out:
	hns_roce_bitmap_free(&cq_table->bitmap, hr_cq->cqn, BITMAP_NO_RR);
	return ret;
}

static int hns_roce_hw2sw_cq(struct hns_roce_dev *dev,
			     struct hns_roce_cmd_mailbox *mailbox,
			     unsigned long cq_num)
{
	return hns_roce_cmd_mbox(dev, 0, mailbox ? mailbox->dma : 0, cq_num,
				 mailbox ? 0 : 1, HNS_ROCE_CMD_HW2SW_CQ,
				 HNS_ROCE_CMD_TIMEOUT_MSECS);
}

void hns_roce_free_cq(struct hns_roce_dev *hr_dev, struct hns_roce_cq *hr_cq)
{
	struct hns_roce_cq_table *cq_table = &hr_dev->cq_table;
	struct device *dev = hr_dev->dev;
	int ret;

	ret = hns_roce_hw2sw_cq(hr_dev, NULL, hr_cq->cqn);
	if (ret)
		dev_err(dev, "HW2SW_CQ failed (%d) for CQN %06lx\n", ret,
			hr_cq->cqn);

	xa_erase(&cq_table->array, hr_cq->cqn);

	/* Waiting interrupt process procedure carried out */
	synchronize_irq(hr_dev->eq_table.eq[hr_cq->vector].irq);

	/* wait for all interrupt processed */
	if (atomic_dec_and_test(&hr_cq->refcount))
		complete(&hr_cq->free);
	wait_for_completion(&hr_cq->free);

	hns_roce_table_put(hr_dev, &cq_table->table, hr_cq->cqn);
	hns_roce_bitmap_free(&cq_table->bitmap, hr_cq->cqn, BITMAP_NO_RR);
}

static int hns_roce_ib_get_cq_umem(struct hns_roce_dev *hr_dev,
				   struct ib_udata *udata,
				   struct hns_roce_cq_buf *buf,
				   struct ib_umem **umem, u64 buf_addr, int cqe)
{
	int ret;
	u32 page_shift;
	u32 npages;

	*umem = ib_umem_get(udata, buf_addr, cqe * hr_dev->caps.cq_entry_sz,
			    IB_ACCESS_LOCAL_WRITE, 1);
	if (IS_ERR(*umem))
		return PTR_ERR(*umem);

	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE))
		buf->hr_mtt.mtt_type = MTT_TYPE_CQE;
	else
		buf->hr_mtt.mtt_type = MTT_TYPE_WQE;

	if (hr_dev->caps.cqe_buf_pg_sz) {
		npages = (ib_umem_page_count(*umem) +
			(1 << hr_dev->caps.cqe_buf_pg_sz) - 1) /
			(1 << hr_dev->caps.cqe_buf_pg_sz);
		page_shift = PAGE_SHIFT + hr_dev->caps.cqe_buf_pg_sz;
		ret = hns_roce_mtt_init(hr_dev, npages, page_shift,
					&buf->hr_mtt);
	} else {
		ret = hns_roce_mtt_init(hr_dev, ib_umem_page_count(*umem),
					PAGE_SHIFT, &buf->hr_mtt);
	}
	if (ret)
		goto err_buf;

	ret = hns_roce_ib_umem_write_mtt(hr_dev, &buf->hr_mtt, *umem);
	if (ret)
		goto err_mtt;

	return 0;

err_mtt:
	hns_roce_mtt_cleanup(hr_dev, &buf->hr_mtt);

err_buf:
	ib_umem_release(*umem);
	return ret;
}

static int hns_roce_ib_alloc_cq_buf(struct hns_roce_dev *hr_dev,
				    struct hns_roce_cq_buf *buf, u32 nent)
{
	int ret;
	u32 page_shift = PAGE_SHIFT + hr_dev->caps.cqe_buf_pg_sz;

	ret = hns_roce_buf_alloc(hr_dev, nent * hr_dev->caps.cq_entry_sz,
				 (1 << page_shift) * 2, &buf->hr_buf,
				 page_shift);
	if (ret)
		goto out;

	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE))
		buf->hr_mtt.mtt_type = MTT_TYPE_CQE;
	else
		buf->hr_mtt.mtt_type = MTT_TYPE_WQE;

	ret = hns_roce_mtt_init(hr_dev, buf->hr_buf.npages,
				buf->hr_buf.page_shift, &buf->hr_mtt);
	if (ret)
		goto err_buf;

	ret = hns_roce_buf_write_mtt(hr_dev, &buf->hr_mtt, &buf->hr_buf);
	if (ret)
		goto err_mtt;

	return 0;

err_mtt:
	hns_roce_mtt_cleanup(hr_dev, &buf->hr_mtt);

err_buf:
	hns_roce_buf_free(hr_dev, nent * hr_dev->caps.cq_entry_sz,
			  &buf->hr_buf);
out:
	return ret;
}

static void hns_roce_ib_free_cq_buf(struct hns_roce_dev *hr_dev,
				    struct hns_roce_cq_buf *buf, int cqe)
{
	hns_roce_buf_free(hr_dev, (cqe + 1) * hr_dev->caps.cq_entry_sz,
			  &buf->hr_buf);
}

static int create_user_cq(struct hns_roce_dev *hr_dev,
			  struct hns_roce_cq *hr_cq,
			  struct ib_udata *udata,
			  struct hns_roce_ib_create_cq_resp *resp,
			  int cq_entries)
{
	struct hns_roce_ib_create_cq ucmd;
	struct device *dev = hr_dev->dev;
	int ret;
	struct hns_roce_ucontext *context = rdma_udata_to_drv_context(
				   udata, struct hns_roce_ucontext, ibucontext);

	if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd))) {
		dev_err(dev, "Failed to copy_from_udata.\n");
		return -EFAULT;
	}

	/* Get user space address, write it into mtt table */
	ret = hns_roce_ib_get_cq_umem(hr_dev, udata, &hr_cq->hr_buf,
				      &hr_cq->umem, ucmd.buf_addr,
				      cq_entries);
	if (ret) {
		dev_err(dev, "Failed to get_cq_umem.\n");
		return ret;
	}

	if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) &&
	    (udata->outlen >= sizeof(*resp))) {
		ret = hns_roce_db_map_user(context, udata, ucmd.db_addr,
					   &hr_cq->db);
		if (ret) {
			dev_err(dev, "cq record doorbell map failed!\n");
			goto err_mtt;
		}
		hr_cq->db_en = 1;
		resp->cap_flags |= HNS_ROCE_SUPPORT_CQ_RECORD_DB;
	}

	return 0;

err_mtt:
	hns_roce_mtt_cleanup(hr_dev, &hr_cq->hr_buf.hr_mtt);
	ib_umem_release(hr_cq->umem);

	return ret;
}

static int create_kernel_cq(struct hns_roce_dev *hr_dev,
			    struct hns_roce_cq *hr_cq, int cq_entries)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_uar *uar;
	int ret;

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) {
		ret = hns_roce_alloc_db(hr_dev, &hr_cq->db, 1);
		if (ret)
			return ret;

		hr_cq->set_ci_db = hr_cq->db.db_record;
		*hr_cq->set_ci_db = 0;
		hr_cq->db_en = 1;
	}

	/* Init mtt table and write buff address to mtt table */
	ret = hns_roce_ib_alloc_cq_buf(hr_dev, &hr_cq->hr_buf, cq_entries);
	if (ret) {
		dev_err(dev, "Failed to alloc_cq_buf.\n");
		goto err_db;
	}

	uar = &hr_dev->priv_uar;
	hr_cq->cq_db_l = hr_dev->reg_base + hr_dev->odb_offset +
			 DB_REG_OFFSET * uar->index;

	return 0;

err_db:
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB)
		hns_roce_free_db(hr_dev, &hr_cq->db);

	return ret;
}

static void destroy_user_cq(struct hns_roce_dev *hr_dev,
			    struct hns_roce_cq *hr_cq,
			    struct ib_udata *udata,
			    struct hns_roce_ib_create_cq_resp *resp)
{
	struct hns_roce_ucontext *context = rdma_udata_to_drv_context(
				   udata, struct hns_roce_ucontext, ibucontext);

	if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) &&
	    (udata->outlen >= sizeof(*resp)))
		hns_roce_db_unmap_user(context, &hr_cq->db);

	hns_roce_mtt_cleanup(hr_dev, &hr_cq->hr_buf.hr_mtt);
	ib_umem_release(hr_cq->umem);
}

static void destroy_kernel_cq(struct hns_roce_dev *hr_dev,
			      struct hns_roce_cq *hr_cq)
{
	hns_roce_mtt_cleanup(hr_dev, &hr_cq->hr_buf.hr_mtt);
	hns_roce_ib_free_cq_buf(hr_dev, &hr_cq->hr_buf, hr_cq->ib_cq.cqe);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB)
		hns_roce_free_db(hr_dev, &hr_cq->db);
}

int hns_roce_ib_create_cq(struct ib_cq *ib_cq,
			  const struct ib_cq_init_attr *attr,
			  struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_cq->device);
	struct device *dev = hr_dev->dev;
	struct hns_roce_ib_create_cq_resp resp = {};
	struct hns_roce_cq *hr_cq = to_hr_cq(ib_cq);
	int vector = attr->comp_vector;
	int cq_entries = attr->cqe;
	int ret;

	if (cq_entries < 1 || cq_entries > hr_dev->caps.max_cqes) {
		dev_err(dev, "Creat CQ failed. entries=%d, max=%d\n",
			cq_entries, hr_dev->caps.max_cqes);
		return -EINVAL;
	}

	if (hr_dev->caps.min_cqes)
		cq_entries = max(cq_entries, hr_dev->caps.min_cqes);

	cq_entries = roundup_pow_of_two((unsigned int)cq_entries);
	hr_cq->ib_cq.cqe = cq_entries - 1;
	spin_lock_init(&hr_cq->lock);

	if (udata) {
		ret = create_user_cq(hr_dev, hr_cq, udata, &resp, cq_entries);
		if (ret) {
			dev_err(dev, "Create cq failed in user mode!\n");
			goto err_cq;
		}
	} else {
		ret = create_kernel_cq(hr_dev, hr_cq, cq_entries);
		if (ret) {
			dev_err(dev, "Create cq failed in kernel mode!\n");
			goto err_cq;
		}
	}

	/* Allocate cq index, fill cq_context */
	ret = hns_roce_cq_alloc(hr_dev, cq_entries, &hr_cq->hr_buf.hr_mtt,
				hr_cq, vector);
	if (ret) {
		dev_err(dev, "Creat CQ .Failed to cq_alloc.\n");
		goto err_dbmap;
	}

	/*
	 * For the QP created by kernel space, tptr value should be initialized
	 * to zero; For the QP created by user space, it will cause synchronous
	 * problems if tptr is set to zero here, so we initialze it in user
	 * space.
	 */
	if (!udata && hr_cq->tptr_addr)
		*hr_cq->tptr_addr = 0;

	/* Get created cq handler and carry out event */
	hr_cq->comp = hns_roce_ib_cq_comp;
	hr_cq->event = hns_roce_ib_cq_event;
	hr_cq->cq_depth = cq_entries;

	if (udata) {
		resp.cqn = hr_cq->cqn;
		ret = ib_copy_to_udata(udata, &resp, sizeof(resp));
		if (ret)
			goto err_cqc;
	}

	return 0;

err_cqc:
	hns_roce_free_cq(hr_dev, hr_cq);

err_dbmap:
	if (udata)
		destroy_user_cq(hr_dev, hr_cq, udata, &resp);
	else
		destroy_kernel_cq(hr_dev, hr_cq);

err_cq:
	return ret;
}

void hns_roce_ib_destroy_cq(struct ib_cq *ib_cq, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_cq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ib_cq);

	if (hr_dev->hw->destroy_cq) {
		hr_dev->hw->destroy_cq(ib_cq, udata);
		return;
	}

	hns_roce_free_cq(hr_dev, hr_cq);
	hns_roce_mtt_cleanup(hr_dev, &hr_cq->hr_buf.hr_mtt);

	ib_umem_release(hr_cq->umem);
	if (udata) {
		if (hr_cq->db_en == 1)
			hns_roce_db_unmap_user(rdma_udata_to_drv_context(
						       udata,
						       struct hns_roce_ucontext,
						       ibucontext),
					       &hr_cq->db);
	} else {
		/* Free the buff of stored cq */
		hns_roce_ib_free_cq_buf(hr_dev, &hr_cq->hr_buf, ib_cq->cqe);
		if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB)
			hns_roce_free_db(hr_dev, &hr_cq->db);
	}
}

void hns_roce_cq_completion(struct hns_roce_dev *hr_dev, u32 cqn)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_cq *cq;

	cq = xa_load(&hr_dev->cq_table.array, cqn & (hr_dev->caps.num_cqs - 1));
	if (!cq) {
		dev_warn(dev, "Completion event for bogus CQ 0x%08x\n", cqn);
		return;
	}

	++cq->arm_sn;
	cq->comp(cq);
}

void hns_roce_cq_event(struct hns_roce_dev *hr_dev, u32 cqn, int event_type)
{
	struct hns_roce_cq_table *cq_table = &hr_dev->cq_table;
	struct device *dev = hr_dev->dev;
	struct hns_roce_cq *cq;

	cq = xa_load(&cq_table->array, cqn & (hr_dev->caps.num_cqs - 1));
	if (cq)
		atomic_inc(&cq->refcount);

	if (!cq) {
		dev_warn(dev, "Async event for bogus CQ %08x\n", cqn);
		return;
	}

	cq->event(cq, (enum hns_roce_event)event_type);

	if (atomic_dec_and_test(&cq->refcount))
		complete(&cq->free);
}

int hns_roce_init_cq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cq_table *cq_table = &hr_dev->cq_table;

	xa_init(&cq_table->array);

	return hns_roce_bitmap_init(&cq_table->bitmap, hr_dev->caps.num_cqs,
				    hr_dev->caps.num_cqs - 1,
				    hr_dev->caps.reserved_cqs, 0);
}

void hns_roce_cleanup_cq_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->cq_table.bitmap);
}
