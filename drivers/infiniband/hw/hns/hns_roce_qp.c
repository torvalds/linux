/*
 * Copyright (c) 2016 Hisilicon Limited.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/pci.h>
#include <linux/platform_device.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_ioctl.h>
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_hem.h"
#include <rdma/hns-abi.h>

#define SQP_NUM				(2 * HNS_ROCE_MAX_PORTS)

void hns_roce_qp_event(struct hns_roce_dev *hr_dev, u32 qpn, int event_type)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_qp *qp;

	xa_lock(&hr_dev->qp_table_xa);
	qp = __hns_roce_qp_lookup(hr_dev, qpn);
	if (qp)
		atomic_inc(&qp->refcount);
	xa_unlock(&hr_dev->qp_table_xa);

	if (!qp) {
		dev_warn(dev, "Async event for bogus QP %08x\n", qpn);
		return;
	}

	qp->event(qp, (enum hns_roce_event)event_type);

	if (atomic_dec_and_test(&qp->refcount))
		complete(&qp->free);
}
EXPORT_SYMBOL_GPL(hns_roce_qp_event);

static void hns_roce_ib_qp_event(struct hns_roce_qp *hr_qp,
				 enum hns_roce_event type)
{
	struct ib_event event;
	struct ib_qp *ibqp = &hr_qp->ibqp;

	if (ibqp->event_handler) {
		event.device = ibqp->device;
		event.element.qp = ibqp;
		switch (type) {
		case HNS_ROCE_EVENT_TYPE_PATH_MIG:
			event.event = IB_EVENT_PATH_MIG;
			break;
		case HNS_ROCE_EVENT_TYPE_COMM_EST:
			event.event = IB_EVENT_COMM_EST;
			break;
		case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
			event.event = IB_EVENT_SQ_DRAINED;
			break;
		case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
			event.event = IB_EVENT_QP_LAST_WQE_REACHED;
			break;
		case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
			event.event = IB_EVENT_QP_FATAL;
			break;
		case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
			event.event = IB_EVENT_PATH_MIG_ERR;
			break;
		case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
			event.event = IB_EVENT_QP_REQ_ERR;
			break;
		case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
			event.event = IB_EVENT_QP_ACCESS_ERR;
			break;
		default:
			dev_dbg(ibqp->device->dev.parent, "roce_ib: Unexpected event type %d on QP %06lx\n",
				type, hr_qp->qpn);
			return;
		}
		ibqp->event_handler(&event, ibqp->qp_context);
	}
}

static int hns_roce_reserve_range_qp(struct hns_roce_dev *hr_dev, int cnt,
				     int align, unsigned long *base)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	return hns_roce_bitmap_alloc_range(&qp_table->bitmap, cnt, align,
					   base) ?
		       -ENOMEM :
		       0;
}

enum hns_roce_qp_state to_hns_roce_state(enum ib_qp_state state)
{
	switch (state) {
	case IB_QPS_RESET:
		return HNS_ROCE_QP_STATE_RST;
	case IB_QPS_INIT:
		return HNS_ROCE_QP_STATE_INIT;
	case IB_QPS_RTR:
		return HNS_ROCE_QP_STATE_RTR;
	case IB_QPS_RTS:
		return HNS_ROCE_QP_STATE_RTS;
	case IB_QPS_SQD:
		return HNS_ROCE_QP_STATE_SQD;
	case IB_QPS_ERR:
		return HNS_ROCE_QP_STATE_ERR;
	default:
		return HNS_ROCE_QP_NUM_STATE;
	}
}
EXPORT_SYMBOL_GPL(to_hns_roce_state);

static int hns_roce_gsi_qp_alloc(struct hns_roce_dev *hr_dev, unsigned long qpn,
				 struct hns_roce_qp *hr_qp)
{
	struct xarray *xa = &hr_dev->qp_table_xa;
	int ret;

	if (!qpn)
		return -EINVAL;

	hr_qp->qpn = qpn;
	atomic_set(&hr_qp->refcount, 1);
	init_completion(&hr_qp->free);

	ret = xa_err(xa_store_irq(xa, hr_qp->qpn & (hr_dev->caps.num_qps - 1),
				hr_qp, GFP_KERNEL));
	if (ret)
		dev_err(hr_dev->dev, "QPC xa_store failed\n");

	return ret;
}

static int hns_roce_qp_alloc(struct hns_roce_dev *hr_dev, unsigned long qpn,
			     struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	struct device *dev = hr_dev->dev;
	int ret;

	if (!qpn)
		return -EINVAL;

	hr_qp->qpn = qpn;

	/* Alloc memory for QPC */
	ret = hns_roce_table_get(hr_dev, &qp_table->qp_table, hr_qp->qpn);
	if (ret) {
		dev_err(dev, "QPC table get failed\n");
		goto err_out;
	}

	/* Alloc memory for IRRL */
	ret = hns_roce_table_get(hr_dev, &qp_table->irrl_table, hr_qp->qpn);
	if (ret) {
		dev_err(dev, "IRRL table get failed\n");
		goto err_put_qp;
	}

	if (hr_dev->caps.trrl_entry_sz) {
		/* Alloc memory for TRRL */
		ret = hns_roce_table_get(hr_dev, &qp_table->trrl_table,
					 hr_qp->qpn);
		if (ret) {
			dev_err(dev, "TRRL table get failed\n");
			goto err_put_irrl;
		}
	}

	if (hr_dev->caps.sccc_entry_sz) {
		/* Alloc memory for SCC CTX */
		ret = hns_roce_table_get(hr_dev, &qp_table->sccc_table,
					 hr_qp->qpn);
		if (ret) {
			dev_err(dev, "SCC CTX table get failed\n");
			goto err_put_trrl;
		}
	}

	ret = hns_roce_gsi_qp_alloc(hr_dev, qpn, hr_qp);
	if (ret)
		goto err_put_sccc;

	return 0;

err_put_sccc:
	if (hr_dev->caps.sccc_entry_sz)
		hns_roce_table_put(hr_dev, &qp_table->sccc_table,
				   hr_qp->qpn);

err_put_trrl:
	if (hr_dev->caps.trrl_entry_sz)
		hns_roce_table_put(hr_dev, &qp_table->trrl_table, hr_qp->qpn);

err_put_irrl:
	hns_roce_table_put(hr_dev, &qp_table->irrl_table, hr_qp->qpn);

err_put_qp:
	hns_roce_table_put(hr_dev, &qp_table->qp_table, hr_qp->qpn);

err_out:
	return ret;
}

void hns_roce_qp_remove(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct xarray *xa = &hr_dev->qp_table_xa;
	unsigned long flags;

	xa_lock_irqsave(xa, flags);
	__xa_erase(xa, hr_qp->qpn & (hr_dev->caps.num_qps - 1));
	xa_unlock_irqrestore(xa, flags);
}
EXPORT_SYMBOL_GPL(hns_roce_qp_remove);

void hns_roce_qp_free(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	if (atomic_dec_and_test(&hr_qp->refcount))
		complete(&hr_qp->free);
	wait_for_completion(&hr_qp->free);

	if ((hr_qp->ibqp.qp_type) != IB_QPT_GSI) {
		if (hr_dev->caps.trrl_entry_sz)
			hns_roce_table_put(hr_dev, &qp_table->trrl_table,
					   hr_qp->qpn);
		hns_roce_table_put(hr_dev, &qp_table->irrl_table, hr_qp->qpn);
		hns_roce_table_put(hr_dev, &qp_table->qp_table, hr_qp->qpn);
	}
}
EXPORT_SYMBOL_GPL(hns_roce_qp_free);

void hns_roce_release_range_qp(struct hns_roce_dev *hr_dev, int base_qpn,
			       int cnt)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	if (base_qpn < SQP_NUM)
		return;

	hns_roce_bitmap_free_range(&qp_table->bitmap, base_qpn, cnt, BITMAP_RR);
}
EXPORT_SYMBOL_GPL(hns_roce_release_range_qp);

static int hns_roce_set_rq_size(struct hns_roce_dev *hr_dev,
				struct ib_qp_cap *cap, bool is_user, int has_rq,
				struct hns_roce_qp *hr_qp)
{
	struct device *dev = hr_dev->dev;
	u32 max_cnt;

	/* Check the validity of QP support capacity */
	if (cap->max_recv_wr > hr_dev->caps.max_wqes ||
	    cap->max_recv_sge > hr_dev->caps.max_rq_sg) {
		dev_err(dev, "RQ WR or sge error!max_recv_wr=%d max_recv_sge=%d\n",
			cap->max_recv_wr, cap->max_recv_sge);
		return -EINVAL;
	}

	/* If srq exist, set zero for relative number of rq */
	if (!has_rq) {
		hr_qp->rq.wqe_cnt = 0;
		hr_qp->rq.max_gs = 0;
		cap->max_recv_wr = 0;
		cap->max_recv_sge = 0;
	} else {
		if (is_user && (!cap->max_recv_wr || !cap->max_recv_sge)) {
			dev_err(dev, "user space no need config max_recv_wr max_recv_sge\n");
			return -EINVAL;
		}

		if (hr_dev->caps.min_wqes)
			max_cnt = max(cap->max_recv_wr, hr_dev->caps.min_wqes);
		else
			max_cnt = cap->max_recv_wr;

		hr_qp->rq.wqe_cnt = roundup_pow_of_two(max_cnt);

		if ((u32)hr_qp->rq.wqe_cnt > hr_dev->caps.max_wqes) {
			dev_err(dev, "while setting rq size, rq.wqe_cnt too large\n");
			return -EINVAL;
		}

		max_cnt = max(1U, cap->max_recv_sge);
		hr_qp->rq.max_gs = roundup_pow_of_two(max_cnt);
		if (hr_dev->caps.max_rq_sg <= 2)
			hr_qp->rq.wqe_shift =
					ilog2(hr_dev->caps.max_rq_desc_sz);
		else
			hr_qp->rq.wqe_shift =
					ilog2(hr_dev->caps.max_rq_desc_sz
					      * hr_qp->rq.max_gs);
	}

	cap->max_recv_wr = hr_qp->rq.max_post = hr_qp->rq.wqe_cnt;
	cap->max_recv_sge = hr_qp->rq.max_gs;

	return 0;
}

static int hns_roce_set_user_sq_size(struct hns_roce_dev *hr_dev,
				     struct ib_qp_cap *cap,
				     struct hns_roce_qp *hr_qp,
				     struct hns_roce_ib_create_qp *ucmd)
{
	u32 roundup_sq_stride = roundup_pow_of_two(hr_dev->caps.max_sq_desc_sz);
	u8 max_sq_stride = ilog2(roundup_sq_stride);
	u32 ex_sge_num;
	u32 page_size;
	u32 max_cnt;

	/* Sanity check SQ size before proceeding */
	if ((u32)(1 << ucmd->log_sq_bb_count) > hr_dev->caps.max_wqes ||
	     ucmd->log_sq_stride > max_sq_stride ||
	     ucmd->log_sq_stride < HNS_ROCE_IB_MIN_SQ_STRIDE) {
		dev_err(hr_dev->dev, "check SQ size error!\n");
		return -EINVAL;
	}

	if (cap->max_send_sge > hr_dev->caps.max_sq_sg) {
		dev_err(hr_dev->dev, "SQ sge error! max_send_sge=%d\n",
			cap->max_send_sge);
		return -EINVAL;
	}

	hr_qp->sq.wqe_cnt = 1 << ucmd->log_sq_bb_count;
	hr_qp->sq.wqe_shift = ucmd->log_sq_stride;

	max_cnt = max(1U, cap->max_send_sge);
	if (hr_dev->caps.max_sq_sg <= 2)
		hr_qp->sq.max_gs = roundup_pow_of_two(max_cnt);
	else
		hr_qp->sq.max_gs = max_cnt;

	if (hr_qp->sq.max_gs > 2)
		hr_qp->sge.sge_cnt = roundup_pow_of_two(hr_qp->sq.wqe_cnt *
							(hr_qp->sq.max_gs - 2));

	if ((hr_qp->sq.max_gs > 2) && (hr_dev->pci_dev->revision == 0x20)) {
		if (hr_qp->sge.sge_cnt > hr_dev->caps.max_extend_sg) {
			dev_err(hr_dev->dev,
				"The extended sge cnt error! sge_cnt=%d\n",
				hr_qp->sge.sge_cnt);
			return -EINVAL;
		}
	}

	hr_qp->sge.sge_shift = 4;
	ex_sge_num = hr_qp->sge.sge_cnt;

	/* Get buf size, SQ and RQ  are aligned to page_szie */
	if (hr_dev->caps.max_sq_sg <= 2) {
		hr_qp->buff_size = HNS_ROCE_ALOGN_UP((hr_qp->rq.wqe_cnt <<
					     hr_qp->rq.wqe_shift), PAGE_SIZE) +
				   HNS_ROCE_ALOGN_UP((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), PAGE_SIZE);

		hr_qp->sq.offset = 0;
		hr_qp->rq.offset = HNS_ROCE_ALOGN_UP((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), PAGE_SIZE);
	} else {
		page_size = 1 << (hr_dev->caps.mtt_buf_pg_sz + PAGE_SHIFT);
		hr_qp->sge.sge_cnt =
		       max(page_size / (1 << hr_qp->sge.sge_shift), ex_sge_num);
		hr_qp->buff_size = HNS_ROCE_ALOGN_UP((hr_qp->rq.wqe_cnt <<
					     hr_qp->rq.wqe_shift), page_size) +
				   HNS_ROCE_ALOGN_UP((hr_qp->sge.sge_cnt <<
					     hr_qp->sge.sge_shift), page_size) +
				   HNS_ROCE_ALOGN_UP((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), page_size);

		hr_qp->sq.offset = 0;
		if (ex_sge_num) {
			hr_qp->sge.offset = HNS_ROCE_ALOGN_UP(
							(hr_qp->sq.wqe_cnt <<
							hr_qp->sq.wqe_shift),
							page_size);
			hr_qp->rq.offset = hr_qp->sge.offset +
					HNS_ROCE_ALOGN_UP((hr_qp->sge.sge_cnt <<
						hr_qp->sge.sge_shift),
						page_size);
		} else {
			hr_qp->rq.offset = HNS_ROCE_ALOGN_UP(
							(hr_qp->sq.wqe_cnt <<
							hr_qp->sq.wqe_shift),
							page_size);
		}
	}

	return 0;
}

static int hns_roce_set_kernel_sq_size(struct hns_roce_dev *hr_dev,
				       struct ib_qp_cap *cap,
				       struct hns_roce_qp *hr_qp)
{
	struct device *dev = hr_dev->dev;
	u32 page_size;
	u32 max_cnt;
	int size;

	if (cap->max_send_wr  > hr_dev->caps.max_wqes  ||
	    cap->max_send_sge > hr_dev->caps.max_sq_sg ||
	    cap->max_inline_data > hr_dev->caps.max_sq_inline) {
		dev_err(dev, "SQ WR or sge or inline data error!\n");
		return -EINVAL;
	}

	hr_qp->sq.wqe_shift = ilog2(hr_dev->caps.max_sq_desc_sz);
	hr_qp->sq_max_wqes_per_wr = 1;
	hr_qp->sq_spare_wqes = 0;

	if (hr_dev->caps.min_wqes)
		max_cnt = max(cap->max_send_wr, hr_dev->caps.min_wqes);
	else
		max_cnt = cap->max_send_wr;

	hr_qp->sq.wqe_cnt = roundup_pow_of_two(max_cnt);
	if ((u32)hr_qp->sq.wqe_cnt > hr_dev->caps.max_wqes) {
		dev_err(dev, "while setting kernel sq size, sq.wqe_cnt too large\n");
		return -EINVAL;
	}

	/* Get data_seg numbers */
	max_cnt = max(1U, cap->max_send_sge);
	if (hr_dev->caps.max_sq_sg <= 2)
		hr_qp->sq.max_gs = roundup_pow_of_two(max_cnt);
	else
		hr_qp->sq.max_gs = max_cnt;

	if (hr_qp->sq.max_gs > 2) {
		hr_qp->sge.sge_cnt = roundup_pow_of_two(hr_qp->sq.wqe_cnt *
				     (hr_qp->sq.max_gs - 2));
		hr_qp->sge.sge_shift = 4;
	}

	/* ud sqwqe's sge use extend sge */
	if (hr_dev->caps.max_sq_sg > 2 && hr_qp->ibqp.qp_type == IB_QPT_GSI) {
		hr_qp->sge.sge_cnt = roundup_pow_of_two(hr_qp->sq.wqe_cnt *
				     hr_qp->sq.max_gs);
		hr_qp->sge.sge_shift = 4;
	}

	if ((hr_qp->sq.max_gs > 2) && hr_dev->pci_dev->revision == 0x20) {
		if (hr_qp->sge.sge_cnt > hr_dev->caps.max_extend_sg) {
			dev_err(dev, "The extended sge cnt error! sge_cnt=%d\n",
				hr_qp->sge.sge_cnt);
			return -EINVAL;
		}
	}

	/* Get buf size, SQ and RQ are aligned to PAGE_SIZE */
	page_size = 1 << (hr_dev->caps.mtt_buf_pg_sz + PAGE_SHIFT);
	hr_qp->sq.offset = 0;
	size = HNS_ROCE_ALOGN_UP(hr_qp->sq.wqe_cnt << hr_qp->sq.wqe_shift,
				 page_size);

	if (hr_dev->caps.max_sq_sg > 2 && hr_qp->sge.sge_cnt) {
		hr_qp->sge.sge_cnt = max(page_size/(1 << hr_qp->sge.sge_shift),
					(u32)hr_qp->sge.sge_cnt);
		hr_qp->sge.offset = size;
		size += HNS_ROCE_ALOGN_UP(hr_qp->sge.sge_cnt <<
					  hr_qp->sge.sge_shift, page_size);
	}

	hr_qp->rq.offset = size;
	size += HNS_ROCE_ALOGN_UP((hr_qp->rq.wqe_cnt << hr_qp->rq.wqe_shift),
				  page_size);
	hr_qp->buff_size = size;

	/* Get wr and sge number which send */
	cap->max_send_wr = hr_qp->sq.max_post = hr_qp->sq.wqe_cnt;
	cap->max_send_sge = hr_qp->sq.max_gs;

	/* We don't support inline sends for kernel QPs (yet) */
	cap->max_inline_data = 0;

	return 0;
}

static int hns_roce_qp_has_sq(struct ib_qp_init_attr *attr)
{
	if (attr->qp_type == IB_QPT_XRC_TGT || !attr->cap.max_send_wr)
		return 0;

	return 1;
}

static int hns_roce_qp_has_rq(struct ib_qp_init_attr *attr)
{
	if (attr->qp_type == IB_QPT_XRC_INI ||
	    attr->qp_type == IB_QPT_XRC_TGT || attr->srq ||
	    !attr->cap.max_recv_wr)
		return 0;

	return 1;
}

static int hns_roce_create_qp_common(struct hns_roce_dev *hr_dev,
				     struct ib_pd *ib_pd,
				     struct ib_qp_init_attr *init_attr,
				     struct ib_udata *udata, unsigned long sqpn,
				     struct hns_roce_qp *hr_qp)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_ib_create_qp ucmd;
	struct hns_roce_ib_create_qp_resp resp = {};
	struct hns_roce_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct hns_roce_ucontext, ibucontext);
	unsigned long qpn = 0;
	int ret = 0;
	u32 page_shift;
	u32 npages;
	int i;

	mutex_init(&hr_qp->mutex);
	spin_lock_init(&hr_qp->sq.lock);
	spin_lock_init(&hr_qp->rq.lock);

	hr_qp->state = IB_QPS_RESET;

	hr_qp->ibqp.qp_type = init_attr->qp_type;

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		hr_qp->sq_signal_bits = cpu_to_le32(IB_SIGNAL_ALL_WR);
	else
		hr_qp->sq_signal_bits = cpu_to_le32(IB_SIGNAL_REQ_WR);

	ret = hns_roce_set_rq_size(hr_dev, &init_attr->cap, udata,
				   hns_roce_qp_has_rq(init_attr), hr_qp);
	if (ret) {
		dev_err(dev, "hns_roce_set_rq_size failed\n");
		goto err_out;
	}

	if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE) &&
	    hns_roce_qp_has_rq(init_attr)) {
		/* allocate recv inline buf */
		hr_qp->rq_inl_buf.wqe_list = kcalloc(hr_qp->rq.wqe_cnt,
					       sizeof(struct hns_roce_rinl_wqe),
					       GFP_KERNEL);
		if (!hr_qp->rq_inl_buf.wqe_list) {
			ret = -ENOMEM;
			goto err_out;
		}

		hr_qp->rq_inl_buf.wqe_cnt = hr_qp->rq.wqe_cnt;

		/* Firstly, allocate a list of sge space buffer */
		hr_qp->rq_inl_buf.wqe_list[0].sg_list =
					kcalloc(hr_qp->rq_inl_buf.wqe_cnt,
					       init_attr->cap.max_recv_sge *
					       sizeof(struct hns_roce_rinl_sge),
					       GFP_KERNEL);
		if (!hr_qp->rq_inl_buf.wqe_list[0].sg_list) {
			ret = -ENOMEM;
			goto err_wqe_list;
		}

		for (i = 1; i < hr_qp->rq_inl_buf.wqe_cnt; i++)
			/* Secondly, reallocate the buffer */
			hr_qp->rq_inl_buf.wqe_list[i].sg_list =
				&hr_qp->rq_inl_buf.wqe_list[0].sg_list[i *
				init_attr->cap.max_recv_sge];
	}

	if (udata) {
		if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd))) {
			dev_err(dev, "ib_copy_from_udata error for create qp\n");
			ret = -EFAULT;
			goto err_rq_sge_list;
		}

		ret = hns_roce_set_user_sq_size(hr_dev, &init_attr->cap, hr_qp,
						&ucmd);
		if (ret) {
			dev_err(dev, "hns_roce_set_user_sq_size error for create qp\n");
			goto err_rq_sge_list;
		}

		hr_qp->umem = ib_umem_get(udata, ucmd.buf_addr,
					  hr_qp->buff_size, 0, 0);
		if (IS_ERR(hr_qp->umem)) {
			dev_err(dev, "ib_umem_get error for create qp\n");
			ret = PTR_ERR(hr_qp->umem);
			goto err_rq_sge_list;
		}

		hr_qp->mtt.mtt_type = MTT_TYPE_WQE;
		page_shift = PAGE_SHIFT;
		if (hr_dev->caps.mtt_buf_pg_sz) {
			npages = (ib_umem_page_count(hr_qp->umem) +
				  (1 << hr_dev->caps.mtt_buf_pg_sz) - 1) /
				 (1 << hr_dev->caps.mtt_buf_pg_sz);
			page_shift += hr_dev->caps.mtt_buf_pg_sz;
			ret = hns_roce_mtt_init(hr_dev, npages,
				    page_shift,
				    &hr_qp->mtt);
		} else {
			ret = hns_roce_mtt_init(hr_dev,
						ib_umem_page_count(hr_qp->umem),
						page_shift, &hr_qp->mtt);
		}
		if (ret) {
			dev_err(dev, "hns_roce_mtt_init error for create qp\n");
			goto err_buf;
		}

		ret = hns_roce_ib_umem_write_mtt(hr_dev, &hr_qp->mtt,
						 hr_qp->umem);
		if (ret) {
			dev_err(dev, "hns_roce_ib_umem_write_mtt error for create qp\n");
			goto err_mtt;
		}

		if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SQ_RECORD_DB) &&
		    (udata->inlen >= sizeof(ucmd)) &&
		    (udata->outlen >= sizeof(resp)) &&
		    hns_roce_qp_has_sq(init_attr)) {
			ret = hns_roce_db_map_user(uctx, udata, ucmd.sdb_addr,
						   &hr_qp->sdb);
			if (ret) {
				dev_err(dev, "sq record doorbell map failed!\n");
				goto err_mtt;
			}

			/* indicate kernel supports sq record db */
			resp.cap_flags |= HNS_ROCE_SUPPORT_SQ_RECORD_DB;
			hr_qp->sdb_en = 1;
		}

		if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) &&
		    (udata->outlen >= sizeof(resp)) &&
		    hns_roce_qp_has_rq(init_attr)) {
			ret = hns_roce_db_map_user(uctx, udata, ucmd.db_addr,
						   &hr_qp->rdb);
			if (ret) {
				dev_err(dev, "rq record doorbell map failed!\n");
				goto err_sq_dbmap;
			}

			/* indicate kernel supports rq record db */
			resp.cap_flags |= HNS_ROCE_SUPPORT_RQ_RECORD_DB;
			hr_qp->rdb_en = 1;
		}
	} else {
		if (init_attr->create_flags &
		    IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK) {
			dev_err(dev, "init_attr->create_flags error!\n");
			ret = -EINVAL;
			goto err_rq_sge_list;
		}

		if (init_attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO) {
			dev_err(dev, "init_attr->create_flags error!\n");
			ret = -EINVAL;
			goto err_rq_sge_list;
		}

		/* Set SQ size */
		ret = hns_roce_set_kernel_sq_size(hr_dev, &init_attr->cap,
						  hr_qp);
		if (ret) {
			dev_err(dev, "hns_roce_set_kernel_sq_size error!\n");
			goto err_rq_sge_list;
		}

		/* QP doorbell register address */
		hr_qp->sq.db_reg_l = hr_dev->reg_base + hr_dev->sdb_offset +
				     DB_REG_OFFSET * hr_dev->priv_uar.index;
		hr_qp->rq.db_reg_l = hr_dev->reg_base + hr_dev->odb_offset +
				     DB_REG_OFFSET * hr_dev->priv_uar.index;

		if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) &&
		    hns_roce_qp_has_rq(init_attr)) {
			ret = hns_roce_alloc_db(hr_dev, &hr_qp->rdb, 0);
			if (ret) {
				dev_err(dev, "rq record doorbell alloc failed!\n");
				goto err_rq_sge_list;
			}
			*hr_qp->rdb.db_record = 0;
			hr_qp->rdb_en = 1;
		}

		/* Allocate QP buf */
		page_shift = PAGE_SHIFT + hr_dev->caps.mtt_buf_pg_sz;
		if (hns_roce_buf_alloc(hr_dev, hr_qp->buff_size,
				       (1 << page_shift) * 2,
				       &hr_qp->hr_buf, page_shift)) {
			dev_err(dev, "hns_roce_buf_alloc error!\n");
			ret = -ENOMEM;
			goto err_db;
		}

		hr_qp->mtt.mtt_type = MTT_TYPE_WQE;
		/* Write MTT */
		ret = hns_roce_mtt_init(hr_dev, hr_qp->hr_buf.npages,
					hr_qp->hr_buf.page_shift, &hr_qp->mtt);
		if (ret) {
			dev_err(dev, "hns_roce_mtt_init error for kernel create qp\n");
			goto err_buf;
		}

		ret = hns_roce_buf_write_mtt(hr_dev, &hr_qp->mtt,
					     &hr_qp->hr_buf);
		if (ret) {
			dev_err(dev, "hns_roce_buf_write_mtt error for kernel create qp\n");
			goto err_mtt;
		}

		hr_qp->sq.wrid = kcalloc(hr_qp->sq.wqe_cnt, sizeof(u64),
					 GFP_KERNEL);
		hr_qp->rq.wrid = kcalloc(hr_qp->rq.wqe_cnt, sizeof(u64),
					 GFP_KERNEL);
		if (!hr_qp->sq.wrid || !hr_qp->rq.wrid) {
			ret = -ENOMEM;
			goto err_wrid;
		}
	}

	if (sqpn) {
		qpn = sqpn;
	} else {
		/* Get QPN */
		ret = hns_roce_reserve_range_qp(hr_dev, 1, 1, &qpn);
		if (ret) {
			dev_err(dev, "hns_roce_reserve_range_qp alloc qpn error\n");
			goto err_wrid;
		}
	}

	if (init_attr->qp_type == IB_QPT_GSI &&
	    hr_dev->hw_rev == HNS_ROCE_HW_VER1) {
		/* In v1 engine, GSI QP context in RoCE engine's register */
		ret = hns_roce_gsi_qp_alloc(hr_dev, qpn, hr_qp);
		if (ret) {
			dev_err(dev, "hns_roce_qp_alloc failed!\n");
			goto err_qpn;
		}
	} else {
		ret = hns_roce_qp_alloc(hr_dev, qpn, hr_qp);
		if (ret) {
			dev_err(dev, "hns_roce_qp_alloc failed!\n");
			goto err_qpn;
		}
	}

	if (sqpn)
		hr_qp->doorbell_qpn = 1;
	else
		hr_qp->doorbell_qpn = cpu_to_le64(hr_qp->qpn);

	if (udata) {
		ret = ib_copy_to_udata(udata, &resp,
				       min(udata->outlen, sizeof(resp)));
		if (ret)
			goto err_qp;
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL) {
		ret = hr_dev->hw->qp_flow_control_init(hr_dev, hr_qp);
		if (ret)
			goto err_qp;
	}

	hr_qp->event = hns_roce_ib_qp_event;

	return 0;

err_qp:
	if (init_attr->qp_type == IB_QPT_GSI &&
		hr_dev->hw_rev == HNS_ROCE_HW_VER1)
		hns_roce_qp_remove(hr_dev, hr_qp);
	else
		hns_roce_qp_free(hr_dev, hr_qp);

err_qpn:
	if (!sqpn)
		hns_roce_release_range_qp(hr_dev, qpn, 1);

err_wrid:
	if (udata) {
		if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) &&
		    (udata->outlen >= sizeof(resp)) &&
		    hns_roce_qp_has_rq(init_attr))
			hns_roce_db_unmap_user(uctx, &hr_qp->rdb);
	} else {
		kfree(hr_qp->sq.wrid);
		kfree(hr_qp->rq.wrid);
	}

err_sq_dbmap:
	if (udata)
		if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SQ_RECORD_DB) &&
		    (udata->inlen >= sizeof(ucmd)) &&
		    (udata->outlen >= sizeof(resp)) &&
		    hns_roce_qp_has_sq(init_attr))
			hns_roce_db_unmap_user(uctx, &hr_qp->sdb);

err_mtt:
	hns_roce_mtt_cleanup(hr_dev, &hr_qp->mtt);

err_buf:
	if (hr_qp->umem)
		ib_umem_release(hr_qp->umem);
	else
		hns_roce_buf_free(hr_dev, hr_qp->buff_size, &hr_qp->hr_buf);

err_db:
	if (!udata && hns_roce_qp_has_rq(init_attr) &&
	    (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB))
		hns_roce_free_db(hr_dev, &hr_qp->rdb);

err_rq_sge_list:
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE)
		kfree(hr_qp->rq_inl_buf.wqe_list[0].sg_list);

err_wqe_list:
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE)
		kfree(hr_qp->rq_inl_buf.wqe_list);

err_out:
	return ret;
}

struct ib_qp *hns_roce_create_qp(struct ib_pd *pd,
				 struct ib_qp_init_attr *init_attr,
				 struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct device *dev = hr_dev->dev;
	struct hns_roce_sqp *hr_sqp;
	struct hns_roce_qp *hr_qp;
	int ret;

	switch (init_attr->qp_type) {
	case IB_QPT_RC: {
		hr_qp = kzalloc(sizeof(*hr_qp), GFP_KERNEL);
		if (!hr_qp)
			return ERR_PTR(-ENOMEM);

		ret = hns_roce_create_qp_common(hr_dev, pd, init_attr, udata, 0,
						hr_qp);
		if (ret) {
			dev_err(dev, "Create RC QP failed\n");
			kfree(hr_qp);
			return ERR_PTR(ret);
		}

		hr_qp->ibqp.qp_num = hr_qp->qpn;

		break;
	}
	case IB_QPT_GSI: {
		/* Userspace is not allowed to create special QPs: */
		if (udata) {
			dev_err(dev, "not support usr space GSI\n");
			return ERR_PTR(-EINVAL);
		}

		hr_sqp = kzalloc(sizeof(*hr_sqp), GFP_KERNEL);
		if (!hr_sqp)
			return ERR_PTR(-ENOMEM);

		hr_qp = &hr_sqp->hr_qp;
		hr_qp->port = init_attr->port_num - 1;
		hr_qp->phy_port = hr_dev->iboe.phy_port[hr_qp->port];

		/* when hw version is v1, the sqpn is allocated */
		if (hr_dev->caps.max_sq_sg <= 2)
			hr_qp->ibqp.qp_num = HNS_ROCE_MAX_PORTS +
					     hr_dev->iboe.phy_port[hr_qp->port];
		else
			hr_qp->ibqp.qp_num = 1;

		ret = hns_roce_create_qp_common(hr_dev, pd, init_attr, udata,
						hr_qp->ibqp.qp_num, hr_qp);
		if (ret) {
			dev_err(dev, "Create GSI QP failed!\n");
			kfree(hr_sqp);
			return ERR_PTR(ret);
		}

		break;
	}
	default:{
		dev_err(dev, "not support QP type %d\n", init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}
	}

	return &hr_qp->ibqp;
}
EXPORT_SYMBOL_GPL(hns_roce_create_qp);

int to_hr_qp_type(int qp_type)
{
	int transport_type;

	if (qp_type == IB_QPT_RC)
		transport_type = SERV_TYPE_RC;
	else if (qp_type == IB_QPT_UC)
		transport_type = SERV_TYPE_UC;
	else if (qp_type == IB_QPT_UD)
		transport_type = SERV_TYPE_UD;
	else if (qp_type == IB_QPT_GSI)
		transport_type = SERV_TYPE_UD;
	else
		transport_type = -1;

	return transport_type;
}
EXPORT_SYMBOL_GPL(to_hr_qp_type);

int hns_roce_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		       int attr_mask, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	enum ib_qp_state cur_state, new_state;
	struct device *dev = hr_dev->dev;
	int ret = -EINVAL;
	int p;
	enum ib_mtu active_mtu;

	mutex_lock(&hr_qp->mutex);

	cur_state = attr_mask & IB_QP_CUR_STATE ?
		    attr->cur_qp_state : (enum ib_qp_state)hr_qp->state;
	new_state = attr_mask & IB_QP_STATE ?
		    attr->qp_state : cur_state;

	if (ibqp->uobject &&
	    (attr_mask & IB_QP_STATE) && new_state == IB_QPS_ERR) {
		if (hr_qp->sdb_en == 1) {
			hr_qp->sq.head = *(int *)(hr_qp->sdb.virt_addr);

			if (hr_qp->rdb_en == 1)
				hr_qp->rq.head = *(int *)(hr_qp->rdb.virt_addr);
		} else {
			dev_warn(dev, "flush cqe is not supported in userspace!\n");
			goto out;
		}
	}

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
				attr_mask)) {
		dev_err(dev, "ib_modify_qp_is_ok failed\n");
		goto out;
	}

	if ((attr_mask & IB_QP_PORT) &&
	    (attr->port_num == 0 || attr->port_num > hr_dev->caps.num_ports)) {
		dev_err(dev, "attr port_num invalid.attr->port_num=%d\n",
			attr->port_num);
		goto out;
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		p = attr_mask & IB_QP_PORT ? (attr->port_num - 1) : hr_qp->port;
		if (attr->pkey_index >= hr_dev->caps.pkey_table_len[p]) {
			dev_err(dev, "attr pkey_index invalid.attr->pkey_index=%d\n",
				attr->pkey_index);
			goto out;
		}
	}

	if (attr_mask & IB_QP_PATH_MTU) {
		p = attr_mask & IB_QP_PORT ? (attr->port_num - 1) : hr_qp->port;
		active_mtu = iboe_get_mtu(hr_dev->iboe.netdevs[p]->mtu);

		if ((hr_dev->caps.max_mtu == IB_MTU_4096 &&
		    attr->path_mtu > IB_MTU_4096) ||
		    (hr_dev->caps.max_mtu == IB_MTU_2048 &&
		    attr->path_mtu > IB_MTU_2048) ||
		    attr->path_mtu < IB_MTU_256 ||
		    attr->path_mtu > active_mtu) {
			dev_err(dev, "attr path_mtu(%d)invalid while modify qp",
				attr->path_mtu);
			goto out;
		}
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
	    attr->max_rd_atomic > hr_dev->caps.max_qp_init_rdma) {
		dev_err(dev, "attr max_rd_atomic invalid.attr->max_rd_atomic=%d\n",
			attr->max_rd_atomic);
		goto out;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
	    attr->max_dest_rd_atomic > hr_dev->caps.max_qp_dest_rdma) {
		dev_err(dev, "attr max_dest_rd_atomic invalid.attr->max_dest_rd_atomic=%d\n",
			attr->max_dest_rd_atomic);
		goto out;
	}

	if (cur_state == new_state && cur_state == IB_QPS_RESET) {
		if (hr_dev->caps.min_wqes) {
			ret = -EPERM;
			dev_err(dev, "cur_state=%d new_state=%d\n", cur_state,
				new_state);
		} else {
			ret = 0;
		}

		goto out;
	}

	ret = hr_dev->hw->modify_qp(ibqp, attr, attr_mask, cur_state,
				    new_state);

out:
	mutex_unlock(&hr_qp->mutex);

	return ret;
}

void hns_roce_lock_cqs(struct hns_roce_cq *send_cq, struct hns_roce_cq *recv_cq)
		       __acquires(&send_cq->lock) __acquires(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		spin_lock_irq(&send_cq->lock);
		__acquire(&recv_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		spin_lock_irq(&send_cq->lock);
		spin_lock_nested(&recv_cq->lock, SINGLE_DEPTH_NESTING);
	} else {
		spin_lock_irq(&recv_cq->lock);
		spin_lock_nested(&send_cq->lock, SINGLE_DEPTH_NESTING);
	}
}
EXPORT_SYMBOL_GPL(hns_roce_lock_cqs);

void hns_roce_unlock_cqs(struct hns_roce_cq *send_cq,
			 struct hns_roce_cq *recv_cq) __releases(&send_cq->lock)
			 __releases(&recv_cq->lock)
{
	if (send_cq == recv_cq) {
		__release(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		spin_unlock(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else {
		spin_unlock(&send_cq->lock);
		spin_unlock_irq(&recv_cq->lock);
	}
}
EXPORT_SYMBOL_GPL(hns_roce_unlock_cqs);

static void *get_wqe(struct hns_roce_qp *hr_qp, int offset)
{

	return hns_roce_buf_offset(&hr_qp->hr_buf, offset);
}

void *get_recv_wqe(struct hns_roce_qp *hr_qp, int n)
{
	return get_wqe(hr_qp, hr_qp->rq.offset + (n << hr_qp->rq.wqe_shift));
}
EXPORT_SYMBOL_GPL(get_recv_wqe);

void *get_send_wqe(struct hns_roce_qp *hr_qp, int n)
{
	return get_wqe(hr_qp, hr_qp->sq.offset + (n << hr_qp->sq.wqe_shift));
}
EXPORT_SYMBOL_GPL(get_send_wqe);

void *get_send_extend_sge(struct hns_roce_qp *hr_qp, int n)
{
	return hns_roce_buf_offset(&hr_qp->hr_buf, hr_qp->sge.offset +
					(n << hr_qp->sge.sge_shift));
}
EXPORT_SYMBOL_GPL(get_send_extend_sge);

bool hns_roce_wq_overflow(struct hns_roce_wq *hr_wq, int nreq,
			  struct ib_cq *ib_cq)
{
	struct hns_roce_cq *hr_cq;
	u32 cur;

	cur = hr_wq->head - hr_wq->tail;
	if (likely(cur + nreq < hr_wq->max_post))
		return false;

	hr_cq = to_hr_cq(ib_cq);
	spin_lock(&hr_cq->lock);
	cur = hr_wq->head - hr_wq->tail;
	spin_unlock(&hr_cq->lock);

	return cur + nreq >= hr_wq->max_post;
}
EXPORT_SYMBOL_GPL(hns_roce_wq_overflow);

int hns_roce_init_qp_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	int reserved_from_top = 0;
	int reserved_from_bot;
	int ret;

	mutex_init(&qp_table->scc_mutex);
	xa_init(&hr_dev->qp_table_xa);

	/* In hw v1, a port include two SQP, six ports total 12 */
	if (hr_dev->caps.max_sq_sg <= 2)
		reserved_from_bot = SQP_NUM;
	else
		reserved_from_bot = hr_dev->caps.reserved_qps;

	ret = hns_roce_bitmap_init(&qp_table->bitmap, hr_dev->caps.num_qps,
				   hr_dev->caps.num_qps - 1, reserved_from_bot,
				   reserved_from_top);
	if (ret) {
		dev_err(hr_dev->dev, "qp bitmap init failed!error=%d\n",
			ret);
		return ret;
	}

	return 0;
}

void hns_roce_cleanup_qp_table(struct hns_roce_dev *hr_dev)
{
	hns_roce_bitmap_cleanup(&hr_dev->qp_table.bitmap);
}
