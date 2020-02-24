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

static void flush_work_handle(struct work_struct *work)
{
	struct hns_roce_work *flush_work = container_of(work,
					struct hns_roce_work, work);
	struct hns_roce_qp *hr_qp = container_of(flush_work,
					struct hns_roce_qp, flush_work);
	struct device *dev = flush_work->hr_dev->dev;
	struct ib_qp_attr attr;
	int attr_mask;
	int ret;

	attr_mask = IB_QP_STATE;
	attr.qp_state = IB_QPS_ERR;

	if (test_and_clear_bit(HNS_ROCE_FLUSH_FLAG, &hr_qp->flush_flag)) {
		ret = hns_roce_modify_qp(&hr_qp->ibqp, &attr, attr_mask, NULL);
		if (ret)
			dev_err(dev, "Modify QP to error state failed(%d) during CQE flush\n",
				ret);
	}

	/*
	 * make sure we signal QP destroy leg that flush QP was completed
	 * so that it can safely proceed ahead now and destroy QP
	 */
	if (atomic_dec_and_test(&hr_qp->refcount))
		complete(&hr_qp->free);
}

void init_flush_work(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_work *flush_work = &hr_qp->flush_work;

	flush_work->hr_dev = hr_dev;
	INIT_WORK(&flush_work->work, flush_work_handle);
	atomic_inc(&hr_qp->refcount);
	queue_work(hr_dev->irq_workq, &flush_work->work);
}

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

	if (hr_dev->hw_rev != HNS_ROCE_HW_VER1 &&
	    (event_type == HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR ||
	     event_type == HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR ||
	     event_type == HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR)) {
		qp->state = IB_QPS_ERR;
		if (!test_and_set_bit(HNS_ROCE_FLUSH_FLAG, &qp->flush_flag))
			init_flush_work(hr_dev, qp);
	}

	qp->event(qp, (enum hns_roce_event)event_type);

	if (atomic_dec_and_test(&qp->refcount))
		complete(&qp->free);
}

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

static int alloc_qpn(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	unsigned long num = 0;
	int ret;

	if (hr_qp->ibqp.qp_type == IB_QPT_GSI) {
		/* when hw version is v1, the sqpn is allocated */
		if (hr_dev->hw_rev == HNS_ROCE_HW_VER1)
			num = HNS_ROCE_MAX_PORTS +
			      hr_dev->iboe.phy_port[hr_qp->port];
		else
			num = 1;

		hr_qp->doorbell_qpn = 1;
	} else {
		ret = hns_roce_bitmap_alloc_range(&hr_dev->qp_table.bitmap,
						  1, 1, &num);
		if (ret) {
			ibdev_err(&hr_dev->ib_dev, "Failed to alloc bitmap\n");
			return -ENOMEM;
		}

		hr_qp->doorbell_qpn = (u32)num;
	}

	hr_qp->qpn = num;

	return 0;
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

static void add_qp_to_list(struct hns_roce_dev *hr_dev,
			   struct hns_roce_qp *hr_qp,
			   struct ib_cq *send_cq, struct ib_cq *recv_cq)
{
	struct hns_roce_cq *hr_send_cq, *hr_recv_cq;
	unsigned long flags;

	hr_send_cq = send_cq ? to_hr_cq(send_cq) : NULL;
	hr_recv_cq = recv_cq ? to_hr_cq(recv_cq) : NULL;

	spin_lock_irqsave(&hr_dev->qp_list_lock, flags);
	hns_roce_lock_cqs(hr_send_cq, hr_recv_cq);

	list_add_tail(&hr_qp->node, &hr_dev->qp_list);
	if (hr_send_cq)
		list_add_tail(&hr_qp->sq_node, &hr_send_cq->sq_list);
	if (hr_recv_cq)
		list_add_tail(&hr_qp->rq_node, &hr_recv_cq->rq_list);

	hns_roce_unlock_cqs(hr_send_cq, hr_recv_cq);
	spin_unlock_irqrestore(&hr_dev->qp_list_lock, flags);
}

static int hns_roce_qp_store(struct hns_roce_dev *hr_dev,
			     struct hns_roce_qp *hr_qp,
			     struct ib_qp_init_attr *init_attr)
{
	struct xarray *xa = &hr_dev->qp_table_xa;
	int ret;

	if (!hr_qp->qpn)
		return -EINVAL;

	ret = xa_err(xa_store_irq(xa, hr_qp->qpn, hr_qp, GFP_KERNEL));
	if (ret)
		dev_err(hr_dev->dev, "Failed to xa store for QPC\n");
	else
		/* add QP to device's QP list for softwc */
		add_qp_to_list(hr_dev, hr_qp, init_attr->send_cq,
			       init_attr->recv_cq);

	return ret;
}

static int alloc_qpc(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	struct device *dev = hr_dev->dev;
	int ret;

	if (!hr_qp->qpn)
		return -EINVAL;

	/* In v1 engine, GSI QP context is saved in the RoCE hw's register */
	if (hr_qp->ibqp.qp_type == IB_QPT_GSI &&
	    hr_dev->hw_rev == HNS_ROCE_HW_VER1)
		return 0;

	/* Alloc memory for QPC */
	ret = hns_roce_table_get(hr_dev, &qp_table->qp_table, hr_qp->qpn);
	if (ret) {
		dev_err(dev, "Failed to get QPC table\n");
		goto err_out;
	}

	/* Alloc memory for IRRL */
	ret = hns_roce_table_get(hr_dev, &qp_table->irrl_table, hr_qp->qpn);
	if (ret) {
		dev_err(dev, "Failed to get IRRL table\n");
		goto err_put_qp;
	}

	if (hr_dev->caps.trrl_entry_sz) {
		/* Alloc memory for TRRL */
		ret = hns_roce_table_get(hr_dev, &qp_table->trrl_table,
					 hr_qp->qpn);
		if (ret) {
			dev_err(dev, "Failed to get TRRL table\n");
			goto err_put_irrl;
		}
	}

	if (hr_dev->caps.sccc_entry_sz) {
		/* Alloc memory for SCC CTX */
		ret = hns_roce_table_get(hr_dev, &qp_table->sccc_table,
					 hr_qp->qpn);
		if (ret) {
			dev_err(dev, "Failed to get SCC CTX table\n");
			goto err_put_trrl;
		}
	}

	return 0;

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

	list_del(&hr_qp->node);
	list_del(&hr_qp->sq_node);
	list_del(&hr_qp->rq_node);

	xa_lock_irqsave(xa, flags);
	__xa_erase(xa, hr_qp->qpn & (hr_dev->caps.num_qps - 1));
	xa_unlock_irqrestore(xa, flags);
}

static void free_qpc(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	/* In v1 engine, GSI QP context is saved in the RoCE hw's register */
	if (hr_qp->ibqp.qp_type == IB_QPT_GSI &&
	    hr_dev->hw_rev == HNS_ROCE_HW_VER1)
		return;

	if (hr_dev->caps.trrl_entry_sz)
		hns_roce_table_put(hr_dev, &qp_table->trrl_table, hr_qp->qpn);
	hns_roce_table_put(hr_dev, &qp_table->irrl_table, hr_qp->qpn);
}

static void free_qpn(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;

	if (hr_qp->ibqp.qp_type == IB_QPT_GSI)
		return;

	if (hr_qp->qpn < hr_dev->caps.reserved_qps)
		return;

	hns_roce_bitmap_free_range(&qp_table->bitmap, hr_qp->qpn, 1, BITMAP_RR);
}

static int set_rq_size(struct hns_roce_dev *hr_dev,
				struct ib_qp_cap *cap, bool is_user, int has_rq,
				struct hns_roce_qp *hr_qp)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u32 max_cnt;

	/* Check the validity of QP support capacity */
	if (cap->max_recv_wr > hr_dev->caps.max_wqes ||
	    cap->max_recv_sge > hr_dev->caps.max_rq_sg) {
		ibdev_err(ibdev, "Failed to check max recv WR %d and SGE %d\n",
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
			ibdev_err(ibdev, "Failed to check user max recv WR and SGE\n");
			return -EINVAL;
		}

		if (hr_dev->caps.min_wqes)
			max_cnt = max(cap->max_recv_wr, hr_dev->caps.min_wqes);
		else
			max_cnt = cap->max_recv_wr;

		hr_qp->rq.wqe_cnt = roundup_pow_of_two(max_cnt);

		if ((u32)hr_qp->rq.wqe_cnt > hr_dev->caps.max_wqes) {
			ibdev_err(ibdev, "Failed to check RQ WQE count limit\n");
			return -EINVAL;
		}

		max_cnt = max(1U, cap->max_recv_sge);
		hr_qp->rq.max_gs = roundup_pow_of_two(max_cnt);
		if (hr_dev->caps.max_rq_sg <= HNS_ROCE_SGE_IN_WQE)
			hr_qp->rq.wqe_shift =
					ilog2(hr_dev->caps.max_rq_desc_sz);
		else
			hr_qp->rq.wqe_shift =
					ilog2(hr_dev->caps.max_rq_desc_sz
					      * hr_qp->rq.max_gs);
	}

	cap->max_recv_wr = hr_qp->rq.wqe_cnt;
	cap->max_recv_sge = hr_qp->rq.max_gs;

	return 0;
}

static int check_sq_size_with_integrity(struct hns_roce_dev *hr_dev,
					struct ib_qp_cap *cap,
					struct hns_roce_ib_create_qp *ucmd)
{
	u32 roundup_sq_stride = roundup_pow_of_two(hr_dev->caps.max_sq_desc_sz);
	u8 max_sq_stride = ilog2(roundup_sq_stride);

	/* Sanity check SQ size before proceeding */
	if (ucmd->log_sq_stride > max_sq_stride ||
	    ucmd->log_sq_stride < HNS_ROCE_IB_MIN_SQ_STRIDE) {
		ibdev_err(&hr_dev->ib_dev, "Failed to check SQ stride size\n");
		return -EINVAL;
	}

	if (cap->max_send_sge > hr_dev->caps.max_sq_sg) {
		ibdev_err(&hr_dev->ib_dev, "Failed to check SQ SGE size %d\n",
			  cap->max_send_sge);
		return -EINVAL;
	}

	return 0;
}

static int set_user_sq_size(struct hns_roce_dev *hr_dev,
			    struct ib_qp_cap *cap, struct hns_roce_qp *hr_qp,
			    struct hns_roce_ib_create_qp *ucmd)
{
	u32 ex_sge_num;
	u32 page_size;
	u32 max_cnt;
	int ret;

	if (check_shl_overflow(1, ucmd->log_sq_bb_count, &hr_qp->sq.wqe_cnt) ||
	    hr_qp->sq.wqe_cnt > hr_dev->caps.max_wqes)
		return -EINVAL;

	ret = check_sq_size_with_integrity(hr_dev, cap, ucmd);
	if (ret) {
		ibdev_err(&hr_dev->ib_dev, "Failed to check user SQ size limit\n");
		return ret;
	}

	hr_qp->sq.wqe_shift = ucmd->log_sq_stride;

	max_cnt = max(1U, cap->max_send_sge);
	if (hr_dev->hw_rev == HNS_ROCE_HW_VER1)
		hr_qp->sq.max_gs = roundup_pow_of_two(max_cnt);
	else
		hr_qp->sq.max_gs = max_cnt;

	if (hr_qp->sq.max_gs > HNS_ROCE_SGE_IN_WQE)
		hr_qp->sge.sge_cnt = roundup_pow_of_two(hr_qp->sq.wqe_cnt *
							(hr_qp->sq.max_gs - 2));

	if (hr_qp->sq.max_gs > HNS_ROCE_SGE_IN_WQE &&
	    hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08_A) {
		if (hr_qp->sge.sge_cnt > hr_dev->caps.max_extend_sg) {
			ibdev_err(&hr_dev->ib_dev,
				  "Failed to check extended SGE size limit %d\n",
				  hr_qp->sge.sge_cnt);
			return -EINVAL;
		}
	}

	hr_qp->sge.sge_shift = 4;
	ex_sge_num = hr_qp->sge.sge_cnt;

	/* Get buf size, SQ and RQ  are aligned to page_szie */
	if (hr_dev->hw_rev == HNS_ROCE_HW_VER1) {
		hr_qp->buff_size = round_up((hr_qp->rq.wqe_cnt <<
					     hr_qp->rq.wqe_shift), PAGE_SIZE) +
				   round_up((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), PAGE_SIZE);

		hr_qp->sq.offset = 0;
		hr_qp->rq.offset = round_up((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), PAGE_SIZE);
	} else {
		page_size = 1 << (hr_dev->caps.mtt_buf_pg_sz + PAGE_SHIFT);
		hr_qp->sge.sge_cnt = ex_sge_num ?
		   max(page_size / (1 << hr_qp->sge.sge_shift), ex_sge_num) : 0;
		hr_qp->buff_size = round_up((hr_qp->rq.wqe_cnt <<
					     hr_qp->rq.wqe_shift), page_size) +
				   round_up((hr_qp->sge.sge_cnt <<
					     hr_qp->sge.sge_shift), page_size) +
				   round_up((hr_qp->sq.wqe_cnt <<
					     hr_qp->sq.wqe_shift), page_size);

		hr_qp->sq.offset = 0;
		if (ex_sge_num) {
			hr_qp->sge.offset = round_up((hr_qp->sq.wqe_cnt <<
						      hr_qp->sq.wqe_shift),
						     page_size);
			hr_qp->rq.offset = hr_qp->sge.offset +
					   round_up((hr_qp->sge.sge_cnt <<
						     hr_qp->sge.sge_shift),
						    page_size);
		} else {
			hr_qp->rq.offset = round_up((hr_qp->sq.wqe_cnt <<
						     hr_qp->sq.wqe_shift),
						    page_size);
		}
	}

	return 0;
}

static int split_wqe_buf_region(struct hns_roce_dev *hr_dev,
				struct hns_roce_qp *hr_qp,
				struct hns_roce_buf_region *regions,
				int region_max, int page_shift)
{
	int page_size = 1 << page_shift;
	bool is_extend_sge;
	int region_cnt = 0;
	int buf_size;
	int buf_cnt;

	if (hr_qp->buff_size < 1 || region_max < 1)
		return region_cnt;

	if (hr_qp->sge.sge_cnt > 0)
		is_extend_sge = true;
	else
		is_extend_sge = false;

	/* sq region */
	if (is_extend_sge)
		buf_size = hr_qp->sge.offset - hr_qp->sq.offset;
	else
		buf_size = hr_qp->rq.offset - hr_qp->sq.offset;

	if (buf_size > 0 && region_cnt < region_max) {
		buf_cnt = DIV_ROUND_UP(buf_size, page_size);
		hns_roce_init_buf_region(&regions[region_cnt],
					 hr_dev->caps.wqe_sq_hop_num,
					 hr_qp->sq.offset / page_size,
					 buf_cnt);
		region_cnt++;
	}

	/* sge region */
	if (is_extend_sge) {
		buf_size = hr_qp->rq.offset - hr_qp->sge.offset;
		if (buf_size > 0 && region_cnt < region_max) {
			buf_cnt = DIV_ROUND_UP(buf_size, page_size);
			hns_roce_init_buf_region(&regions[region_cnt],
						 hr_dev->caps.wqe_sge_hop_num,
						 hr_qp->sge.offset / page_size,
						 buf_cnt);
			region_cnt++;
		}
	}

	/* rq region */
	buf_size = hr_qp->buff_size - hr_qp->rq.offset;
	if (buf_size > 0) {
		buf_cnt = DIV_ROUND_UP(buf_size, page_size);
		hns_roce_init_buf_region(&regions[region_cnt],
					 hr_dev->caps.wqe_rq_hop_num,
					 hr_qp->rq.offset / page_size,
					 buf_cnt);
		region_cnt++;
	}

	return region_cnt;
}

static int calc_wqe_bt_page_shift(struct hns_roce_dev *hr_dev,
				  struct hns_roce_buf_region *regions,
				  int region_cnt)
{
	int bt_pg_shift;
	int ba_num;
	int ret;

	bt_pg_shift = PAGE_SHIFT + hr_dev->caps.mtt_ba_pg_sz;

	/* all root ba entries must in one bt page */
	do {
		ba_num = (1 << bt_pg_shift) / BA_BYTE_LEN;
		ret = hns_roce_hem_list_calc_root_ba(regions, region_cnt,
						     ba_num);
		if (ret <= ba_num)
			break;

		bt_pg_shift++;
	} while (ret > ba_num);

	return bt_pg_shift - PAGE_SHIFT;
}

static int set_extend_sge_param(struct hns_roce_dev *hr_dev,
				struct hns_roce_qp *hr_qp)
{
	struct device *dev = hr_dev->dev;

	if (hr_qp->sq.max_gs > 2) {
		hr_qp->sge.sge_cnt = roundup_pow_of_two(hr_qp->sq.wqe_cnt *
				     (hr_qp->sq.max_gs - 2));
		hr_qp->sge.sge_shift = 4;
	}

	/* ud sqwqe's sge use extend sge */
	if (hr_dev->hw_rev != HNS_ROCE_HW_VER1 &&
	    hr_qp->ibqp.qp_type == IB_QPT_GSI) {
		hr_qp->sge.sge_cnt = roundup_pow_of_two(hr_qp->sq.wqe_cnt *
				     hr_qp->sq.max_gs);
		hr_qp->sge.sge_shift = 4;
	}

	if (hr_qp->sq.max_gs > 2 &&
	    hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08_A) {
		if (hr_qp->sge.sge_cnt > hr_dev->caps.max_extend_sg) {
			dev_err(dev, "The extended sge cnt error! sge_cnt=%d\n",
				hr_qp->sge.sge_cnt);
			return -EINVAL;
		}
	}

	return 0;
}

static int set_kernel_sq_size(struct hns_roce_dev *hr_dev,
			      struct ib_qp_cap *cap, struct hns_roce_qp *hr_qp)
{
	struct device *dev = hr_dev->dev;
	u32 page_size;
	u32 max_cnt;
	int size;
	int ret;

	if (cap->max_send_wr  > hr_dev->caps.max_wqes  ||
	    cap->max_send_sge > hr_dev->caps.max_sq_sg ||
	    cap->max_inline_data > hr_dev->caps.max_sq_inline) {
		dev_err(dev, "SQ WR or sge or inline data error!\n");
		return -EINVAL;
	}

	hr_qp->sq.wqe_shift = ilog2(hr_dev->caps.max_sq_desc_sz);

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
	if (hr_dev->hw_rev == HNS_ROCE_HW_VER1)
		hr_qp->sq.max_gs = roundup_pow_of_two(max_cnt);
	else
		hr_qp->sq.max_gs = max_cnt;

	ret = set_extend_sge_param(hr_dev, hr_qp);
	if (ret) {
		dev_err(dev, "set extend sge parameters fail\n");
		return ret;
	}

	/* Get buf size, SQ and RQ are aligned to PAGE_SIZE */
	page_size = 1 << (hr_dev->caps.mtt_buf_pg_sz + PAGE_SHIFT);
	hr_qp->sq.offset = 0;
	size = round_up(hr_qp->sq.wqe_cnt << hr_qp->sq.wqe_shift, page_size);

	if (hr_dev->hw_rev != HNS_ROCE_HW_VER1 && hr_qp->sge.sge_cnt) {
		hr_qp->sge.sge_cnt = max(page_size/(1 << hr_qp->sge.sge_shift),
					 (u32)hr_qp->sge.sge_cnt);
		hr_qp->sge.offset = size;
		size += round_up(hr_qp->sge.sge_cnt << hr_qp->sge.sge_shift,
				 page_size);
	}

	hr_qp->rq.offset = size;
	size += round_up((hr_qp->rq.wqe_cnt << hr_qp->rq.wqe_shift), page_size);
	hr_qp->buff_size = size;

	/* Get wr and sge number which send */
	cap->max_send_wr = hr_qp->sq.wqe_cnt;
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

static int alloc_rq_inline_buf(struct hns_roce_qp *hr_qp,
			       struct ib_qp_init_attr *init_attr)
{
	u32 max_recv_sge = init_attr->cap.max_recv_sge;
	struct hns_roce_rinl_wqe *wqe_list;
	u32 wqe_cnt = hr_qp->rq.wqe_cnt;
	int i;

	/* allocate recv inline buf */
	wqe_list = kcalloc(wqe_cnt, sizeof(struct hns_roce_rinl_wqe),
			   GFP_KERNEL);

	if (!wqe_list)
		goto err;

	/* Allocate a continuous buffer for all inline sge we need */
	wqe_list[0].sg_list = kcalloc(wqe_cnt, (max_recv_sge *
				      sizeof(struct hns_roce_rinl_sge)),
				      GFP_KERNEL);
	if (!wqe_list[0].sg_list)
		goto err_wqe_list;

	/* Assign buffers of sg_list to each inline wqe */
	for (i = 1; i < wqe_cnt; i++)
		wqe_list[i].sg_list = &wqe_list[0].sg_list[i * max_recv_sge];

	hr_qp->rq_inl_buf.wqe_list = wqe_list;
	hr_qp->rq_inl_buf.wqe_cnt = wqe_cnt;

	return 0;

err_wqe_list:
	kfree(wqe_list);

err:
	return -ENOMEM;
}

static void free_rq_inline_buf(struct hns_roce_qp *hr_qp)
{
	kfree(hr_qp->rq_inl_buf.wqe_list[0].sg_list);
	kfree(hr_qp->rq_inl_buf.wqe_list);
}

static int map_wqe_buf(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp,
		       u32 page_shift, bool is_user)
{
	dma_addr_t *buf_list[ARRAY_SIZE(hr_qp->regions)] = { NULL };
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_buf_region *r;
	int region_count;
	int buf_count;
	int ret;
	int i;

	region_count = split_wqe_buf_region(hr_dev, hr_qp, hr_qp->regions,
					ARRAY_SIZE(hr_qp->regions), page_shift);

	/* alloc a tmp list to store WQE buffers address */
	ret = hns_roce_alloc_buf_list(hr_qp->regions, buf_list, region_count);
	if (ret) {
		ibdev_err(ibdev, "Failed to alloc WQE buffer list\n");
		return ret;
	}

	for (i = 0; i < region_count; i++) {
		r = &hr_qp->regions[i];
		if (is_user)
			buf_count = hns_roce_get_umem_bufs(hr_dev, buf_list[i],
					r->count, r->offset, hr_qp->umem,
					page_shift);
		else
			buf_count = hns_roce_get_kmem_bufs(hr_dev, buf_list[i],
					r->count, r->offset, &hr_qp->hr_buf);

		if (buf_count != r->count) {
			ibdev_err(ibdev, "Failed to get %s WQE buf, expect %d = %d.\n",
				  is_user ? "user" : "kernel",
				  r->count, buf_count);
			ret = -ENOBUFS;
			goto done;
		}
	}

	hr_qp->wqe_bt_pg_shift = calc_wqe_bt_page_shift(hr_dev, hr_qp->regions,
							region_count);
	hns_roce_mtr_init(&hr_qp->mtr, PAGE_SHIFT + hr_qp->wqe_bt_pg_shift,
			  page_shift);
	ret = hns_roce_mtr_attach(hr_dev, &hr_qp->mtr, buf_list, hr_qp->regions,
				  region_count);
	if (ret)
		ibdev_err(ibdev, "Failed to attatch WQE's mtr\n");

	goto done;

	hns_roce_mtr_cleanup(hr_dev, &hr_qp->mtr);
done:
	hns_roce_free_buf_list(buf_list, region_count);

	return ret;
}

static int alloc_qp_buf(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp,
			struct ib_qp_init_attr *init_attr,
			struct ib_udata *udata, unsigned long addr)
{
	u32 page_shift = PAGE_SHIFT + hr_dev->caps.mtt_buf_pg_sz;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	bool is_rq_buf_inline;
	int ret;

	is_rq_buf_inline = (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE) &&
			   hns_roce_qp_has_rq(init_attr);
	if (is_rq_buf_inline) {
		ret = alloc_rq_inline_buf(hr_qp, init_attr);
		if (ret) {
			ibdev_err(ibdev, "Failed to alloc inline RQ buffer\n");
			return ret;
		}
	}

	if (udata) {
		hr_qp->umem = ib_umem_get(ibdev, addr, hr_qp->buff_size, 0);
		if (IS_ERR(hr_qp->umem)) {
			ret = PTR_ERR(hr_qp->umem);
			goto err_inline;
		}
	} else {
		ret = hns_roce_buf_alloc(hr_dev, hr_qp->buff_size,
					 (1 << page_shift) * 2,
					 &hr_qp->hr_buf, page_shift);
		if (ret)
			goto err_inline;
	}

	ret = map_wqe_buf(hr_dev, hr_qp, page_shift, udata);
	if (ret)
		goto err_alloc;

	return 0;

err_inline:
	if (is_rq_buf_inline)
		free_rq_inline_buf(hr_qp);

err_alloc:
	if (udata) {
		ib_umem_release(hr_qp->umem);
		hr_qp->umem = NULL;
	} else {
		hns_roce_buf_free(hr_dev, hr_qp->buff_size, &hr_qp->hr_buf);
	}

	ibdev_err(ibdev, "Failed to alloc WQE buffer, ret %d.\n", ret);

	return ret;
}

static void free_qp_buf(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	hns_roce_mtr_cleanup(hr_dev, &hr_qp->mtr);
	if (hr_qp->umem) {
		ib_umem_release(hr_qp->umem);
		hr_qp->umem = NULL;
	}

	if (hr_qp->hr_buf.nbufs > 0)
		hns_roce_buf_free(hr_dev, hr_qp->buff_size, &hr_qp->hr_buf);

	if ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE) &&
	     hr_qp->rq.wqe_cnt)
		free_rq_inline_buf(hr_qp);
}

static inline bool user_qp_has_sdb(struct hns_roce_dev *hr_dev,
				   struct ib_qp_init_attr *init_attr,
				   struct ib_udata *udata,
				   struct hns_roce_ib_create_qp_resp *resp,
				   struct hns_roce_ib_create_qp *ucmd)
{
	return ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SQ_RECORD_DB) &&
		udata->outlen >= offsetofend(typeof(*resp), cap_flags) &&
		hns_roce_qp_has_sq(init_attr) &&
		udata->inlen >= offsetofend(typeof(*ucmd), sdb_addr));
}

static inline bool user_qp_has_rdb(struct hns_roce_dev *hr_dev,
				   struct ib_qp_init_attr *init_attr,
				   struct ib_udata *udata,
				   struct hns_roce_ib_create_qp_resp *resp)
{
	return ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) &&
		udata->outlen >= offsetofend(typeof(*resp), cap_flags) &&
		hns_roce_qp_has_rq(init_attr));
}

static inline bool kernel_qp_has_rdb(struct hns_roce_dev *hr_dev,
				     struct ib_qp_init_attr *init_attr)
{
	return ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) &&
		hns_roce_qp_has_rq(init_attr));
}

static int alloc_qp_db(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp,
		       struct ib_qp_init_attr *init_attr,
		       struct ib_udata *udata,
		       struct hns_roce_ib_create_qp *ucmd,
		       struct hns_roce_ib_create_qp_resp *resp)
{
	struct hns_roce_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct hns_roce_ucontext, ibucontext);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int ret;

	if (udata) {
		if (user_qp_has_sdb(hr_dev, init_attr, udata, resp, ucmd)) {
			ret = hns_roce_db_map_user(uctx, udata, ucmd->sdb_addr,
						   &hr_qp->sdb);
			if (ret) {
				ibdev_err(ibdev,
					  "Failed to map user SQ doorbell\n");
				goto err_out;
			}
			hr_qp->sdb_en = 1;
			resp->cap_flags |= HNS_ROCE_SUPPORT_SQ_RECORD_DB;
		}

		if (user_qp_has_rdb(hr_dev, init_attr, udata, resp)) {
			ret = hns_roce_db_map_user(uctx, udata, ucmd->db_addr,
						   &hr_qp->rdb);
			if (ret) {
				ibdev_err(ibdev,
					  "Failed to map user RQ doorbell\n");
				goto err_sdb;
			}
			hr_qp->rdb_en = 1;
			resp->cap_flags |= HNS_ROCE_SUPPORT_RQ_RECORD_DB;
		}
	} else {
		/* QP doorbell register address */
		hr_qp->sq.db_reg_l = hr_dev->reg_base + hr_dev->sdb_offset +
				     DB_REG_OFFSET * hr_dev->priv_uar.index;
		hr_qp->rq.db_reg_l = hr_dev->reg_base + hr_dev->odb_offset +
				     DB_REG_OFFSET * hr_dev->priv_uar.index;

		if (kernel_qp_has_rdb(hr_dev, init_attr)) {
			ret = hns_roce_alloc_db(hr_dev, &hr_qp->rdb, 0);
			if (ret) {
				ibdev_err(ibdev,
					  "Failed to alloc kernel RQ doorbell\n");
				goto err_out;
			}
			*hr_qp->rdb.db_record = 0;
			hr_qp->rdb_en = 1;
		}
	}

	return 0;
err_sdb:
	if (udata && hr_qp->sdb_en)
		hns_roce_db_unmap_user(uctx, &hr_qp->sdb);
err_out:
	return ret;
}

static void free_qp_db(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp,
		       struct ib_udata *udata)
{
	struct hns_roce_ucontext *uctx = rdma_udata_to_drv_context(
		udata, struct hns_roce_ucontext, ibucontext);

	if (udata) {
		if (hr_qp->rdb_en)
			hns_roce_db_unmap_user(uctx, &hr_qp->rdb);
		if (hr_qp->sdb_en)
			hns_roce_db_unmap_user(uctx, &hr_qp->sdb);
	} else {
		if (hr_qp->rdb_en)
			hns_roce_free_db(hr_dev, &hr_qp->rdb);
	}
}

static int alloc_kernel_wrid(struct hns_roce_dev *hr_dev,
			     struct hns_roce_qp *hr_qp)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u64 *sq_wrid = NULL;
	u64 *rq_wrid = NULL;
	int ret;

	sq_wrid = kcalloc(hr_qp->sq.wqe_cnt, sizeof(u64), GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(sq_wrid)) {
		ibdev_err(ibdev, "Failed to alloc SQ wrid\n");
		return -ENOMEM;
	}

	if (hr_qp->rq.wqe_cnt) {
		rq_wrid = kcalloc(hr_qp->rq.wqe_cnt, sizeof(u64), GFP_KERNEL);
		if (ZERO_OR_NULL_PTR(rq_wrid)) {
			ibdev_err(ibdev, "Failed to alloc RQ wrid\n");
			ret = -ENOMEM;
			goto err_sq;
		}
	}

	hr_qp->sq.wrid = sq_wrid;
	hr_qp->rq.wrid = rq_wrid;
	return 0;
err_sq:
	kfree(sq_wrid);

	return ret;
}

static void free_kernel_wrid(struct hns_roce_dev *hr_dev,
			     struct hns_roce_qp *hr_qp)
{
	kfree(hr_qp->rq.wrid);
	kfree(hr_qp->sq.wrid);
}

static int set_qp_param(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp,
			struct ib_qp_init_attr *init_attr,
			struct ib_udata *udata,
			struct hns_roce_ib_create_qp *ucmd)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int ret;

	hr_qp->ibqp.qp_type = init_attr->qp_type;

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		hr_qp->sq_signal_bits = IB_SIGNAL_ALL_WR;
	else
		hr_qp->sq_signal_bits = IB_SIGNAL_REQ_WR;

	ret = set_rq_size(hr_dev, &init_attr->cap, udata,
			  hns_roce_qp_has_rq(init_attr), hr_qp);
	if (ret) {
		ibdev_err(ibdev, "Failed to set user RQ size\n");
		return ret;
	}

	if (udata) {
		if (ib_copy_from_udata(ucmd, udata, sizeof(*ucmd))) {
			ibdev_err(ibdev, "Failed to copy QP ucmd\n");
			return -EFAULT;
		}

		ret = set_user_sq_size(hr_dev, &init_attr->cap, hr_qp, ucmd);
		if (ret)
			ibdev_err(ibdev, "Failed to set user SQ size\n");
	} else {
		if (init_attr->create_flags &
		    IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK) {
			ibdev_err(ibdev, "Failed to check multicast loopback\n");
			return -EINVAL;
		}

		if (init_attr->create_flags & IB_QP_CREATE_IPOIB_UD_LSO) {
			ibdev_err(ibdev, "Failed to check ipoib ud lso\n");
			return -EINVAL;
		}

		ret = set_kernel_sq_size(hr_dev, &init_attr->cap, hr_qp);
		if (ret)
			ibdev_err(ibdev, "Failed to set kernel SQ size\n");
	}

	return ret;
}

static int hns_roce_create_qp_common(struct hns_roce_dev *hr_dev,
				     struct ib_pd *ib_pd,
				     struct ib_qp_init_attr *init_attr,
				     struct ib_udata *udata,
				     struct hns_roce_qp *hr_qp)
{
	struct hns_roce_ib_create_qp_resp resp = {};
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_ib_create_qp ucmd;
	int ret;

	mutex_init(&hr_qp->mutex);
	spin_lock_init(&hr_qp->sq.lock);
	spin_lock_init(&hr_qp->rq.lock);

	hr_qp->state = IB_QPS_RESET;
	hr_qp->flush_flag = 0;

	ret = set_qp_param(hr_dev, hr_qp, init_attr, udata, &ucmd);
	if (ret) {
		ibdev_err(ibdev, "Failed to set QP param\n");
		return ret;
	}

	if (!udata) {
		ret = alloc_kernel_wrid(hr_dev, hr_qp);
		if (ret) {
			ibdev_err(ibdev, "Failed to alloc wrid\n");
			return ret;
		}
	}

	ret = alloc_qp_db(hr_dev, hr_qp, init_attr, udata, &ucmd, &resp);
	if (ret) {
		ibdev_err(ibdev, "Failed to alloc QP doorbell\n");
		goto err_wrid;
	}

	ret = alloc_qp_buf(hr_dev, hr_qp, init_attr, udata, ucmd.buf_addr);
	if (ret) {
		ibdev_err(ibdev, "Failed to alloc QP buffer\n");
		goto err_db;
	}

	ret = alloc_qpn(hr_dev, hr_qp);
	if (ret) {
		ibdev_err(ibdev, "Failed to alloc QPN\n");
		goto err_buf;
	}

	ret = alloc_qpc(hr_dev, hr_qp);
	if (ret) {
		ibdev_err(ibdev, "Failed to alloc QP context\n");
		goto err_qpn;
	}

	ret = hns_roce_qp_store(hr_dev, hr_qp, init_attr);
	if (ret) {
		ibdev_err(ibdev, "Failed to store QP\n");
		goto err_qpc;
	}

	if (udata) {
		ret = ib_copy_to_udata(udata, &resp,
				       min(udata->outlen, sizeof(resp)));
		if (ret) {
			ibdev_err(ibdev, "copy qp resp failed!\n");
			goto err_store;
		}
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL) {
		ret = hr_dev->hw->qp_flow_control_init(hr_dev, hr_qp);
		if (ret)
			goto err_store;
	}

	hr_qp->ibqp.qp_num = hr_qp->qpn;
	hr_qp->event = hns_roce_ib_qp_event;
	atomic_set(&hr_qp->refcount, 1);
	init_completion(&hr_qp->free);

	return 0;

err_store:
	hns_roce_qp_remove(hr_dev, hr_qp);
err_qpc:
	free_qpc(hr_dev, hr_qp);
err_qpn:
	free_qpn(hr_dev, hr_qp);
err_buf:
	free_qp_buf(hr_dev, hr_qp);
err_db:
	free_qp_db(hr_dev, hr_qp, udata);
err_wrid:
	free_kernel_wrid(hr_dev, hr_qp);
	return ret;
}

void hns_roce_qp_destroy(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp,
			 struct ib_udata *udata)
{
	if (atomic_dec_and_test(&hr_qp->refcount))
		complete(&hr_qp->free);
	wait_for_completion(&hr_qp->free);

	free_qpc(hr_dev, hr_qp);
	free_qpn(hr_dev, hr_qp);
	free_qp_buf(hr_dev, hr_qp);
	free_kernel_wrid(hr_dev, hr_qp);
	free_qp_db(hr_dev, hr_qp, udata);

	kfree(hr_qp);
}

struct ib_qp *hns_roce_create_qp(struct ib_pd *pd,
				 struct ib_qp_init_attr *init_attr,
				 struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(pd->device);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_qp *hr_qp;
	int ret;

	switch (init_attr->qp_type) {
	case IB_QPT_RC: {
		hr_qp = kzalloc(sizeof(*hr_qp), GFP_KERNEL);
		if (!hr_qp)
			return ERR_PTR(-ENOMEM);

		ret = hns_roce_create_qp_common(hr_dev, pd, init_attr, udata,
						hr_qp);
		if (ret) {
			ibdev_err(ibdev, "Create QP 0x%06lx failed(%d)\n",
				  hr_qp->qpn, ret);
			kfree(hr_qp);
			return ERR_PTR(ret);
		}

		break;
	}
	case IB_QPT_GSI: {
		/* Userspace is not allowed to create special QPs: */
		if (udata) {
			ibdev_err(ibdev, "not support usr space GSI\n");
			return ERR_PTR(-EINVAL);
		}

		hr_qp = kzalloc(sizeof(*hr_qp), GFP_KERNEL);
		if (!hr_qp)
			return ERR_PTR(-ENOMEM);

		hr_qp->port = init_attr->port_num - 1;
		hr_qp->phy_port = hr_dev->iboe.phy_port[hr_qp->port];

		ret = hns_roce_create_qp_common(hr_dev, pd, init_attr, udata,
						hr_qp);
		if (ret) {
			ibdev_err(ibdev, "Create GSI QP failed!\n");
			kfree(hr_qp);
			return ERR_PTR(ret);
		}

		break;
	}
	default:{
		ibdev_err(ibdev, "not support QP type %d\n",
			  init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}
	}

	return &hr_qp->ibqp;
}

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

static int check_mtu_validate(struct hns_roce_dev *hr_dev,
			      struct hns_roce_qp *hr_qp,
			      struct ib_qp_attr *attr, int attr_mask)
{
	enum ib_mtu active_mtu;
	int p;

	p = attr_mask & IB_QP_PORT ? (attr->port_num - 1) : hr_qp->port;
	active_mtu = iboe_get_mtu(hr_dev->iboe.netdevs[p]->mtu);

	if ((hr_dev->caps.max_mtu >= IB_MTU_2048 &&
	    attr->path_mtu > hr_dev->caps.max_mtu) ||
	    attr->path_mtu < IB_MTU_256 || attr->path_mtu > active_mtu) {
		ibdev_err(&hr_dev->ib_dev,
			"attr path_mtu(%d)invalid while modify qp",
			attr->path_mtu);
		return -EINVAL;
	}

	return 0;
}

static int hns_roce_check_qp_attr(struct ib_qp *ibqp, struct ib_qp_attr *attr,
				  int attr_mask)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	int p;

	if ((attr_mask & IB_QP_PORT) &&
	    (attr->port_num == 0 || attr->port_num > hr_dev->caps.num_ports)) {
		ibdev_err(&hr_dev->ib_dev,
			"attr port_num invalid.attr->port_num=%d\n",
			attr->port_num);
		return -EINVAL;
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		p = attr_mask & IB_QP_PORT ? (attr->port_num - 1) : hr_qp->port;
		if (attr->pkey_index >= hr_dev->caps.pkey_table_len[p]) {
			ibdev_err(&hr_dev->ib_dev,
				"attr pkey_index invalid.attr->pkey_index=%d\n",
				attr->pkey_index);
			return -EINVAL;
		}
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
	    attr->max_rd_atomic > hr_dev->caps.max_qp_init_rdma) {
		ibdev_err(&hr_dev->ib_dev,
			"attr max_rd_atomic invalid.attr->max_rd_atomic=%d\n",
			attr->max_rd_atomic);
		return -EINVAL;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
	    attr->max_dest_rd_atomic > hr_dev->caps.max_qp_dest_rdma) {
		ibdev_err(&hr_dev->ib_dev,
			"attr max_dest_rd_atomic invalid.attr->max_dest_rd_atomic=%d\n",
			attr->max_dest_rd_atomic);
		return -EINVAL;
	}

	if (attr_mask & IB_QP_PATH_MTU)
		return check_mtu_validate(hr_dev, hr_qp, attr, attr_mask);

	return 0;
}

int hns_roce_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		       int attr_mask, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	enum ib_qp_state cur_state, new_state;
	int ret = -EINVAL;

	mutex_lock(&hr_qp->mutex);

	cur_state = attr_mask & IB_QP_CUR_STATE ?
		    attr->cur_qp_state : (enum ib_qp_state)hr_qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (ibqp->uobject &&
	    (attr_mask & IB_QP_STATE) && new_state == IB_QPS_ERR) {
		if (hr_qp->sdb_en == 1) {
			hr_qp->sq.head = *(int *)(hr_qp->sdb.virt_addr);

			if (hr_qp->rdb_en == 1)
				hr_qp->rq.head = *(int *)(hr_qp->rdb.virt_addr);
		} else {
			ibdev_warn(&hr_dev->ib_dev,
				  "flush cqe is not supported in userspace!\n");
			goto out;
		}
	}

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
				attr_mask)) {
		ibdev_err(&hr_dev->ib_dev, "ib_modify_qp_is_ok failed\n");
		goto out;
	}

	ret = hns_roce_check_qp_attr(ibqp, attr, attr_mask);
	if (ret)
		goto out;

	if (cur_state == new_state && cur_state == IB_QPS_RESET) {
		if (hr_dev->caps.min_wqes) {
			ret = -EPERM;
			ibdev_err(&hr_dev->ib_dev,
				"cur_state=%d new_state=%d\n", cur_state,
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
	if (unlikely(send_cq == NULL && recv_cq == NULL)) {
		__acquire(&send_cq->lock);
		__acquire(&recv_cq->lock);
	} else if (unlikely(send_cq != NULL && recv_cq == NULL)) {
		spin_lock_irq(&send_cq->lock);
		__acquire(&recv_cq->lock);
	} else if (unlikely(send_cq == NULL && recv_cq != NULL)) {
		spin_lock_irq(&recv_cq->lock);
		__acquire(&send_cq->lock);
	} else if (send_cq == recv_cq) {
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

void hns_roce_unlock_cqs(struct hns_roce_cq *send_cq,
			 struct hns_roce_cq *recv_cq) __releases(&send_cq->lock)
			 __releases(&recv_cq->lock)
{
	if (unlikely(send_cq == NULL && recv_cq == NULL)) {
		__release(&recv_cq->lock);
		__release(&send_cq->lock);
	} else if (unlikely(send_cq != NULL && recv_cq == NULL)) {
		__release(&recv_cq->lock);
		spin_unlock(&send_cq->lock);
	} else if (unlikely(send_cq == NULL && recv_cq != NULL)) {
		__release(&send_cq->lock);
		spin_unlock(&recv_cq->lock);
	} else if (send_cq == recv_cq) {
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

static void *get_wqe(struct hns_roce_qp *hr_qp, int offset)
{

	return hns_roce_buf_offset(&hr_qp->hr_buf, offset);
}

void *get_recv_wqe(struct hns_roce_qp *hr_qp, int n)
{
	return get_wqe(hr_qp, hr_qp->rq.offset + (n << hr_qp->rq.wqe_shift));
}

void *get_send_wqe(struct hns_roce_qp *hr_qp, int n)
{
	return get_wqe(hr_qp, hr_qp->sq.offset + (n << hr_qp->sq.wqe_shift));
}

void *get_send_extend_sge(struct hns_roce_qp *hr_qp, int n)
{
	return hns_roce_buf_offset(&hr_qp->hr_buf, hr_qp->sge.offset +
					(n << hr_qp->sge.sge_shift));
}

bool hns_roce_wq_overflow(struct hns_roce_wq *hr_wq, int nreq,
			  struct ib_cq *ib_cq)
{
	struct hns_roce_cq *hr_cq;
	u32 cur;

	cur = hr_wq->head - hr_wq->tail;
	if (likely(cur + nreq < hr_wq->wqe_cnt))
		return false;

	hr_cq = to_hr_cq(ib_cq);
	spin_lock(&hr_cq->lock);
	cur = hr_wq->head - hr_wq->tail;
	spin_unlock(&hr_cq->lock);

	return cur + nreq >= hr_wq->wqe_cnt;
}

int hns_roce_init_qp_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	int reserved_from_top = 0;
	int reserved_from_bot;
	int ret;

	mutex_init(&qp_table->scc_mutex);
	xa_init(&hr_dev->qp_table_xa);

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
