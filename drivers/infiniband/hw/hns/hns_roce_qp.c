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
	     event_type == HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR ||
	     event_type == HNS_ROCE_EVENT_TYPE_XRCD_VIOLATION ||
	     event_type == HNS_ROCE_EVENT_TYPE_INVALID_XRCETH)) {
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
	struct ib_qp *ibqp = &hr_qp->ibqp;
	struct ib_event event;

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
		case HNS_ROCE_EVENT_TYPE_XRCD_VIOLATION:
		case HNS_ROCE_EVENT_TYPE_INVALID_XRCETH:
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

static u8 get_least_load_bankid_for_qp(struct hns_roce_bank *bank)
{
	u32 least_load = bank[0].inuse;
	u8 bankid = 0;
	u32 bankcnt;
	u8 i;

	for (i = 1; i < HNS_ROCE_QP_BANK_NUM; i++) {
		bankcnt = bank[i].inuse;
		if (bankcnt < least_load) {
			least_load = bankcnt;
			bankid = i;
		}
	}

	return bankid;
}

static int alloc_qpn_with_bankid(struct hns_roce_bank *bank, u8 bankid,
				 unsigned long *qpn)
{
	int id;

	id = ida_alloc_range(&bank->ida, bank->next, bank->max, GFP_KERNEL);
	if (id < 0) {
		id = ida_alloc_range(&bank->ida, bank->min, bank->max,
				     GFP_KERNEL);
		if (id < 0)
			return id;
	}

	/* the QPN should keep increasing until the max value is reached. */
	bank->next = (id + 1) > bank->max ? bank->min : id + 1;

	/* the lower 3 bits is bankid */
	*qpn = (id << 3) | bankid;

	return 0;
}
static int alloc_qpn(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	struct hns_roce_qp_table *qp_table = &hr_dev->qp_table;
	unsigned long num = 0;
	u8 bankid;
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
		mutex_lock(&qp_table->bank_mutex);
		bankid = get_least_load_bankid_for_qp(qp_table->bank);

		ret = alloc_qpn_with_bankid(&qp_table->bank[bankid], bankid,
					    &num);
		if (ret) {
			ibdev_err(&hr_dev->ib_dev,
				  "failed to alloc QPN, ret = %d\n", ret);
			mutex_unlock(&qp_table->bank_mutex);
			return ret;
		}

		qp_table->bank[bankid].inuse++;
		mutex_unlock(&qp_table->bank_mutex);

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

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL) {
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

	if (hr_qp->ibqp.qp_type != IB_QPT_XRC_TGT)
		list_del(&hr_qp->sq_node);

	if (hr_qp->ibqp.qp_type != IB_QPT_XRC_INI &&
	    hr_qp->ibqp.qp_type != IB_QPT_XRC_TGT)
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

static inline u8 get_qp_bankid(unsigned long qpn)
{
	/* The lower 3 bits of QPN are used to hash to different banks */
	return (u8)(qpn & GENMASK(2, 0));
}

static void free_qpn(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	u8 bankid;

	if (hr_qp->ibqp.qp_type == IB_QPT_GSI)
		return;

	if (hr_qp->qpn < hr_dev->caps.reserved_qps)
		return;

	bankid = get_qp_bankid(hr_qp->qpn);

	ida_free(&hr_dev->qp_table.bank[bankid].ida, hr_qp->qpn >> 3);

	mutex_lock(&hr_dev->qp_table.bank_mutex);
	hr_dev->qp_table.bank[bankid].inuse--;
	mutex_unlock(&hr_dev->qp_table.bank_mutex);
}

static u32 proc_rq_sge(struct hns_roce_dev *dev, struct hns_roce_qp *hr_qp,
		       bool user)
{
	u32 max_sge = dev->caps.max_rq_sg;

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
		hr_qp->rq.rsv_sge = 1;

	return max_sge;
}

static int set_rq_size(struct hns_roce_dev *hr_dev, struct ib_qp_cap *cap,
		       struct hns_roce_qp *hr_qp, int has_rq, bool user)
{
	u32 max_sge = proc_rq_sge(hr_dev, hr_qp, user);
	u32 cnt;

	/* If srq exist, set zero for relative number of rq */
	if (!has_rq) {
		hr_qp->rq.wqe_cnt = 0;
		hr_qp->rq.max_gs = 0;
		hr_qp->rq_inl_buf.wqe_cnt = 0;
		cap->max_recv_wr = 0;
		cap->max_recv_sge = 0;

		return 0;
	}

	/* Check the validity of QP support capacity */
	if (!cap->max_recv_wr || cap->max_recv_wr > hr_dev->caps.max_wqes ||
	    cap->max_recv_sge > max_sge) {
		ibdev_err(&hr_dev->ib_dev,
			  "RQ config error, depth = %u, sge = %u\n",
			  cap->max_recv_wr, cap->max_recv_sge);
		return -EINVAL;
	}

	cnt = roundup_pow_of_two(max(cap->max_recv_wr, hr_dev->caps.min_wqes));
	if (cnt > hr_dev->caps.max_wqes) {
		ibdev_err(&hr_dev->ib_dev, "rq depth %u too large\n",
			  cap->max_recv_wr);
		return -EINVAL;
	}

	hr_qp->rq.max_gs = roundup_pow_of_two(max(1U, cap->max_recv_sge) +
					      hr_qp->rq.rsv_sge);

	if (hr_dev->caps.max_rq_sg <= HNS_ROCE_SGE_IN_WQE)
		hr_qp->rq.wqe_shift = ilog2(hr_dev->caps.max_rq_desc_sz);
	else
		hr_qp->rq.wqe_shift = ilog2(hr_dev->caps.max_rq_desc_sz *
					    hr_qp->rq.max_gs);

	hr_qp->rq.wqe_cnt = cnt;
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE &&
	    hr_qp->ibqp.qp_type != IB_QPT_UD &&
	    hr_qp->ibqp.qp_type != IB_QPT_GSI)
		hr_qp->rq_inl_buf.wqe_cnt = cnt;
	else
		hr_qp->rq_inl_buf.wqe_cnt = 0;

	cap->max_recv_wr = cnt;
	cap->max_recv_sge = hr_qp->rq.max_gs - hr_qp->rq.rsv_sge;

	return 0;
}

static u32 get_wqe_ext_sge_cnt(struct hns_roce_qp *qp)
{
	/* GSI/UD QP only has extended sge */
	if (qp->ibqp.qp_type == IB_QPT_GSI || qp->ibqp.qp_type == IB_QPT_UD)
		return qp->sq.max_gs;

	if (qp->sq.max_gs > HNS_ROCE_SGE_IN_WQE)
		return qp->sq.max_gs - HNS_ROCE_SGE_IN_WQE;

	return 0;
}

static void set_ext_sge_param(struct hns_roce_dev *hr_dev, u32 sq_wqe_cnt,
			      struct hns_roce_qp *hr_qp, struct ib_qp_cap *cap)
{
	u32 total_sge_cnt;
	u32 wqe_sge_cnt;

	hr_qp->sge.sge_shift = HNS_ROCE_SGE_SHIFT;

	if (hr_dev->hw_rev == HNS_ROCE_HW_VER1) {
		hr_qp->sq.max_gs = HNS_ROCE_SGE_IN_WQE;
		return;
	}

	hr_qp->sq.max_gs = max(1U, cap->max_send_sge);

	wqe_sge_cnt = get_wqe_ext_sge_cnt(hr_qp);

	/* If the number of extended sge is not zero, they MUST use the
	 * space of HNS_HW_PAGE_SIZE at least.
	 */
	if (wqe_sge_cnt) {
		total_sge_cnt = roundup_pow_of_two(sq_wqe_cnt * wqe_sge_cnt);
		hr_qp->sge.sge_cnt = max(total_sge_cnt,
				(u32)HNS_HW_PAGE_SIZE / HNS_ROCE_SGE_SIZE);
	}
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
		ibdev_err(&hr_dev->ib_dev, "failed to check SQ stride size.\n");
		return -EINVAL;
	}

	if (cap->max_send_sge > hr_dev->caps.max_sq_sg) {
		ibdev_err(&hr_dev->ib_dev, "failed to check SQ SGE size %u.\n",
			  cap->max_send_sge);
		return -EINVAL;
	}

	return 0;
}

static int set_user_sq_size(struct hns_roce_dev *hr_dev,
			    struct ib_qp_cap *cap, struct hns_roce_qp *hr_qp,
			    struct hns_roce_ib_create_qp *ucmd)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u32 cnt = 0;
	int ret;

	if (check_shl_overflow(1, ucmd->log_sq_bb_count, &cnt) ||
	    cnt > hr_dev->caps.max_wqes)
		return -EINVAL;

	ret = check_sq_size_with_integrity(hr_dev, cap, ucmd);
	if (ret) {
		ibdev_err(ibdev, "failed to check user SQ size, ret = %d.\n",
			  ret);
		return ret;
	}

	set_ext_sge_param(hr_dev, cnt, hr_qp, cap);

	hr_qp->sq.wqe_shift = ucmd->log_sq_stride;
	hr_qp->sq.wqe_cnt = cnt;

	return 0;
}

static int set_wqe_buf_attr(struct hns_roce_dev *hr_dev,
			    struct hns_roce_qp *hr_qp,
			    struct hns_roce_buf_attr *buf_attr)
{
	int buf_size;
	int idx = 0;

	hr_qp->buff_size = 0;

	/* SQ WQE */
	hr_qp->sq.offset = 0;
	buf_size = to_hr_hem_entries_size(hr_qp->sq.wqe_cnt,
					  hr_qp->sq.wqe_shift);
	if (buf_size > 0 && idx < ARRAY_SIZE(buf_attr->region)) {
		buf_attr->region[idx].size = buf_size;
		buf_attr->region[idx].hopnum = hr_dev->caps.wqe_sq_hop_num;
		idx++;
		hr_qp->buff_size += buf_size;
	}

	/* extend SGE WQE in SQ */
	hr_qp->sge.offset = hr_qp->buff_size;
	buf_size = to_hr_hem_entries_size(hr_qp->sge.sge_cnt,
					  hr_qp->sge.sge_shift);
	if (buf_size > 0 && idx < ARRAY_SIZE(buf_attr->region)) {
		buf_attr->region[idx].size = buf_size;
		buf_attr->region[idx].hopnum = hr_dev->caps.wqe_sge_hop_num;
		idx++;
		hr_qp->buff_size += buf_size;
	}

	/* RQ WQE */
	hr_qp->rq.offset = hr_qp->buff_size;
	buf_size = to_hr_hem_entries_size(hr_qp->rq.wqe_cnt,
					  hr_qp->rq.wqe_shift);
	if (buf_size > 0 && idx < ARRAY_SIZE(buf_attr->region)) {
		buf_attr->region[idx].size = buf_size;
		buf_attr->region[idx].hopnum = hr_dev->caps.wqe_rq_hop_num;
		idx++;
		hr_qp->buff_size += buf_size;
	}

	if (hr_qp->buff_size < 1)
		return -EINVAL;

	buf_attr->page_shift = HNS_HW_PAGE_SHIFT + hr_dev->caps.mtt_buf_pg_sz;
	buf_attr->region_count = idx;

	return 0;
}

static int set_kernel_sq_size(struct hns_roce_dev *hr_dev,
			      struct ib_qp_cap *cap, struct hns_roce_qp *hr_qp)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u32 cnt;

	if (!cap->max_send_wr || cap->max_send_wr > hr_dev->caps.max_wqes ||
	    cap->max_send_sge > hr_dev->caps.max_sq_sg) {
		ibdev_err(ibdev,
			  "failed to check SQ WR or SGE num, ret = %d.\n",
			  -EINVAL);
		return -EINVAL;
	}

	cnt = roundup_pow_of_two(max(cap->max_send_wr, hr_dev->caps.min_wqes));
	if (cnt > hr_dev->caps.max_wqes) {
		ibdev_err(ibdev, "failed to check WQE num, WQE num = %u.\n",
			  cnt);
		return -EINVAL;
	}

	hr_qp->sq.wqe_shift = ilog2(hr_dev->caps.max_sq_desc_sz);
	hr_qp->sq.wqe_cnt = cnt;

	set_ext_sge_param(hr_dev, cnt, hr_qp, cap);

	/* sync the parameters of kernel QP to user's configuration */
	cap->max_send_wr = cnt;
	cap->max_send_sge = hr_qp->sq.max_gs;

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
	u32 wqe_cnt = hr_qp->rq_inl_buf.wqe_cnt;
	struct hns_roce_rinl_wqe *wqe_list;
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

	return 0;

err_wqe_list:
	kfree(wqe_list);

err:
	return -ENOMEM;
}

static void free_rq_inline_buf(struct hns_roce_qp *hr_qp)
{
	if (hr_qp->rq_inl_buf.wqe_list)
		kfree(hr_qp->rq_inl_buf.wqe_list[0].sg_list);
	kfree(hr_qp->rq_inl_buf.wqe_list);
}

static int alloc_qp_buf(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp,
			struct ib_qp_init_attr *init_attr,
			struct ib_udata *udata, unsigned long addr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_buf_attr buf_attr = {};
	int ret;

	if (!udata && hr_qp->rq_inl_buf.wqe_cnt) {
		ret = alloc_rq_inline_buf(hr_qp, init_attr);
		if (ret) {
			ibdev_err(ibdev,
				  "failed to alloc inline buf, ret = %d.\n",
				  ret);
			return ret;
		}
	} else {
		hr_qp->rq_inl_buf.wqe_list = NULL;
	}

	ret = set_wqe_buf_attr(hr_dev, hr_qp, &buf_attr);
	if (ret) {
		ibdev_err(ibdev, "failed to split WQE buf, ret = %d.\n", ret);
		goto err_inline;
	}
	ret = hns_roce_mtr_create(hr_dev, &hr_qp->mtr, &buf_attr,
				  HNS_HW_PAGE_SHIFT + hr_dev->caps.mtt_ba_pg_sz,
				  udata, addr);
	if (ret) {
		ibdev_err(ibdev, "failed to create WQE mtr, ret = %d.\n", ret);
		goto err_inline;
	}

	return 0;
err_inline:
	free_rq_inline_buf(hr_qp);

	return ret;
}

static void free_qp_buf(struct hns_roce_dev *hr_dev, struct hns_roce_qp *hr_qp)
{
	hns_roce_mtr_destroy(hr_dev, &hr_qp->mtr);
	free_rq_inline_buf(hr_qp);
}

static inline bool user_qp_has_sdb(struct hns_roce_dev *hr_dev,
				   struct ib_qp_init_attr *init_attr,
				   struct ib_udata *udata,
				   struct hns_roce_ib_create_qp_resp *resp,
				   struct hns_roce_ib_create_qp *ucmd)
{
	return ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_RECORD_DB) &&
		udata->outlen >= offsetofend(typeof(*resp), cap_flags) &&
		hns_roce_qp_has_sq(init_attr) &&
		udata->inlen >= offsetofend(typeof(*ucmd), sdb_addr));
}

static inline bool user_qp_has_rdb(struct hns_roce_dev *hr_dev,
				   struct ib_qp_init_attr *init_attr,
				   struct ib_udata *udata,
				   struct hns_roce_ib_create_qp_resp *resp)
{
	return ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_RECORD_DB) &&
		udata->outlen >= offsetofend(typeof(*resp), cap_flags) &&
		hns_roce_qp_has_rq(init_attr));
}

static inline bool kernel_qp_has_rdb(struct hns_roce_dev *hr_dev,
				     struct ib_qp_init_attr *init_attr)
{
	return ((hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_RECORD_DB) &&
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

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SDI_MODE)
		hr_qp->en_flags |= HNS_ROCE_QP_CAP_OWNER_DB;

	if (udata) {
		if (user_qp_has_sdb(hr_dev, init_attr, udata, resp, ucmd)) {
			ret = hns_roce_db_map_user(uctx, ucmd->sdb_addr,
						   &hr_qp->sdb);
			if (ret) {
				ibdev_err(ibdev,
					  "failed to map user SQ doorbell, ret = %d.\n",
					  ret);
				goto err_out;
			}
			hr_qp->en_flags |= HNS_ROCE_QP_CAP_SQ_RECORD_DB;
			resp->cap_flags |= HNS_ROCE_QP_CAP_SQ_RECORD_DB;
		}

		if (user_qp_has_rdb(hr_dev, init_attr, udata, resp)) {
			ret = hns_roce_db_map_user(uctx, ucmd->db_addr,
						   &hr_qp->rdb);
			if (ret) {
				ibdev_err(ibdev,
					  "failed to map user RQ doorbell, ret = %d.\n",
					  ret);
				goto err_sdb;
			}
			hr_qp->en_flags |= HNS_ROCE_QP_CAP_RQ_RECORD_DB;
			resp->cap_flags |= HNS_ROCE_QP_CAP_RQ_RECORD_DB;
		}
	} else {
		if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09)
			hr_qp->sq.db_reg = hr_dev->mem_base +
					   HNS_ROCE_DWQE_SIZE * hr_qp->qpn;
		else
			hr_qp->sq.db_reg =
				hr_dev->reg_base + hr_dev->sdb_offset +
				DB_REG_OFFSET * hr_dev->priv_uar.index;

		hr_qp->rq.db_reg = hr_dev->reg_base + hr_dev->odb_offset +
				   DB_REG_OFFSET * hr_dev->priv_uar.index;

		if (kernel_qp_has_rdb(hr_dev, init_attr)) {
			ret = hns_roce_alloc_db(hr_dev, &hr_qp->rdb, 0);
			if (ret) {
				ibdev_err(ibdev,
					  "failed to alloc kernel RQ doorbell, ret = %d.\n",
					  ret);
				goto err_out;
			}
			*hr_qp->rdb.db_record = 0;
			hr_qp->en_flags |= HNS_ROCE_QP_CAP_RQ_RECORD_DB;
		}
	}

	return 0;
err_sdb:
	if (udata && hr_qp->en_flags & HNS_ROCE_QP_CAP_SQ_RECORD_DB)
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
		if (hr_qp->en_flags & HNS_ROCE_QP_CAP_RQ_RECORD_DB)
			hns_roce_db_unmap_user(uctx, &hr_qp->rdb);
		if (hr_qp->en_flags & HNS_ROCE_QP_CAP_SQ_RECORD_DB)
			hns_roce_db_unmap_user(uctx, &hr_qp->sdb);
	} else {
		if (hr_qp->en_flags & HNS_ROCE_QP_CAP_RQ_RECORD_DB)
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
		ibdev_err(ibdev, "failed to alloc SQ wrid.\n");
		return -ENOMEM;
	}

	if (hr_qp->rq.wqe_cnt) {
		rq_wrid = kcalloc(hr_qp->rq.wqe_cnt, sizeof(u64), GFP_KERNEL);
		if (ZERO_OR_NULL_PTR(rq_wrid)) {
			ibdev_err(ibdev, "failed to alloc RQ wrid.\n");
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

static void free_kernel_wrid(struct hns_roce_qp *hr_qp)
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

	if (init_attr->cap.max_inline_data > hr_dev->caps.max_sq_inline)
		init_attr->cap.max_inline_data = hr_dev->caps.max_sq_inline;

	hr_qp->max_inline_data = init_attr->cap.max_inline_data;

	if (init_attr->sq_sig_type == IB_SIGNAL_ALL_WR)
		hr_qp->sq_signal_bits = IB_SIGNAL_ALL_WR;
	else
		hr_qp->sq_signal_bits = IB_SIGNAL_REQ_WR;

	ret = set_rq_size(hr_dev, &init_attr->cap, hr_qp,
			  hns_roce_qp_has_rq(init_attr), !!udata);
	if (ret) {
		ibdev_err(ibdev, "failed to set user RQ size, ret = %d.\n",
			  ret);
		return ret;
	}

	if (udata) {
		ret = ib_copy_from_udata(ucmd, udata,
					 min(udata->inlen, sizeof(*ucmd)));
		if (ret) {
			ibdev_err(ibdev,
				  "failed to copy QP ucmd, ret = %d\n", ret);
			return ret;
		}

		ret = set_user_sq_size(hr_dev, &init_attr->cap, hr_qp, ucmd);
		if (ret)
			ibdev_err(ibdev,
				  "failed to set user SQ size, ret = %d.\n",
				  ret);
	} else {
		ret = set_kernel_sq_size(hr_dev, &init_attr->cap, hr_qp);
		if (ret)
			ibdev_err(ibdev,
				  "failed to set kernel SQ size, ret = %d.\n",
				  ret);
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

	if (init_attr->create_flags)
		return -EOPNOTSUPP;

	ret = set_qp_param(hr_dev, hr_qp, init_attr, udata, &ucmd);
	if (ret) {
		ibdev_err(ibdev, "failed to set QP param, ret = %d.\n", ret);
		return ret;
	}

	if (!udata) {
		ret = alloc_kernel_wrid(hr_dev, hr_qp);
		if (ret) {
			ibdev_err(ibdev, "failed to alloc wrid, ret = %d.\n",
				  ret);
			return ret;
		}
	}

	ret = alloc_qp_buf(hr_dev, hr_qp, init_attr, udata, ucmd.buf_addr);
	if (ret) {
		ibdev_err(ibdev, "failed to alloc QP buffer, ret = %d.\n", ret);
		goto err_buf;
	}

	ret = alloc_qpn(hr_dev, hr_qp);
	if (ret) {
		ibdev_err(ibdev, "failed to alloc QPN, ret = %d.\n", ret);
		goto err_qpn;
	}

	ret = alloc_qp_db(hr_dev, hr_qp, init_attr, udata, &ucmd, &resp);
	if (ret) {
		ibdev_err(ibdev, "failed to alloc QP doorbell, ret = %d.\n",
			  ret);
		goto err_db;
	}

	ret = alloc_qpc(hr_dev, hr_qp);
	if (ret) {
		ibdev_err(ibdev, "failed to alloc QP context, ret = %d.\n",
			  ret);
		goto err_qpc;
	}

	ret = hns_roce_qp_store(hr_dev, hr_qp, init_attr);
	if (ret) {
		ibdev_err(ibdev, "failed to store QP, ret = %d.\n", ret);
		goto err_store;
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
			goto err_flow_ctrl;
	}

	hr_qp->ibqp.qp_num = hr_qp->qpn;
	hr_qp->event = hns_roce_ib_qp_event;
	atomic_set(&hr_qp->refcount, 1);
	init_completion(&hr_qp->free);

	return 0;

err_flow_ctrl:
	hns_roce_qp_remove(hr_dev, hr_qp);
err_store:
	free_qpc(hr_dev, hr_qp);
err_qpc:
	free_qp_db(hr_dev, hr_qp, udata);
err_db:
	free_qpn(hr_dev, hr_qp);
err_qpn:
	free_qp_buf(hr_dev, hr_qp);
err_buf:
	free_kernel_wrid(hr_qp);
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
	free_kernel_wrid(hr_qp);
	free_qp_db(hr_dev, hr_qp, udata);

	kfree(hr_qp);
}

static int check_qp_type(struct hns_roce_dev *hr_dev, enum ib_qp_type type,
			 bool is_user)
{
	switch (type) {
	case IB_QPT_XRC_INI:
	case IB_QPT_XRC_TGT:
		if (!(hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_XRC))
			goto out;
		break;
	case IB_QPT_UD:
		if (hr_dev->pci_dev->revision <= PCI_REVISION_ID_HIP08 &&
		    is_user)
			goto out;
		break;
	case IB_QPT_RC:
	case IB_QPT_GSI:
		break;
	default:
		goto out;
	}

	return 0;

out:
	ibdev_err(&hr_dev->ib_dev, "not support QP type %d\n", type);

	return -EOPNOTSUPP;
}

struct ib_qp *hns_roce_create_qp(struct ib_pd *pd,
				 struct ib_qp_init_attr *init_attr,
				 struct ib_udata *udata)
{
	struct ib_device *ibdev = pd ? pd->device : init_attr->xrcd->device;
	struct hns_roce_dev *hr_dev = to_hr_dev(ibdev);
	struct hns_roce_qp *hr_qp;
	int ret;

	ret = check_qp_type(hr_dev, init_attr->qp_type, !!udata);
	if (ret)
		return ERR_PTR(ret);

	hr_qp = kzalloc(sizeof(*hr_qp), GFP_KERNEL);
	if (!hr_qp)
		return ERR_PTR(-ENOMEM);

	if (init_attr->qp_type == IB_QPT_XRC_INI)
		init_attr->recv_cq = NULL;

	if (init_attr->qp_type == IB_QPT_XRC_TGT) {
		hr_qp->xrcdn = to_hr_xrcd(init_attr->xrcd)->xrcdn;
		init_attr->recv_cq = NULL;
		init_attr->send_cq = NULL;
	}

	if (init_attr->qp_type == IB_QPT_GSI) {
		hr_qp->port = init_attr->port_num - 1;
		hr_qp->phy_port = hr_dev->iboe.phy_port[hr_qp->port];
	}

	ret = hns_roce_create_qp_common(hr_dev, pd, init_attr, udata, hr_qp);
	if (ret) {
		ibdev_err(ibdev, "Create QP type 0x%x failed(%d)\n",
			  init_attr->qp_type, ret);

		kfree(hr_qp);
		return ERR_PTR(ret);
	}

	return &hr_qp->ibqp;
}

int to_hr_qp_type(int qp_type)
{
	switch (qp_type) {
	case IB_QPT_RC:
		return SERV_TYPE_RC;
	case IB_QPT_UD:
	case IB_QPT_GSI:
		return SERV_TYPE_UD;
	case IB_QPT_XRC_INI:
	case IB_QPT_XRC_TGT:
		return SERV_TYPE_XRC;
	default:
		return -1;
	}
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
		ibdev_err(&hr_dev->ib_dev, "invalid attr, port_num = %u.\n",
			  attr->port_num);
		return -EINVAL;
	}

	if (attr_mask & IB_QP_PKEY_INDEX) {
		p = attr_mask & IB_QP_PORT ? (attr->port_num - 1) : hr_qp->port;
		if (attr->pkey_index >= hr_dev->caps.pkey_table_len[p]) {
			ibdev_err(&hr_dev->ib_dev,
				  "invalid attr, pkey_index = %u.\n",
				  attr->pkey_index);
			return -EINVAL;
		}
	}

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC &&
	    attr->max_rd_atomic > hr_dev->caps.max_qp_init_rdma) {
		ibdev_err(&hr_dev->ib_dev,
			  "invalid attr, max_rd_atomic = %u.\n",
			  attr->max_rd_atomic);
		return -EINVAL;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC &&
	    attr->max_dest_rd_atomic > hr_dev->caps.max_qp_dest_rdma) {
		ibdev_err(&hr_dev->ib_dev,
			  "invalid attr, max_dest_rd_atomic = %u.\n",
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

	if (attr_mask & IB_QP_CUR_STATE && attr->cur_qp_state != hr_qp->state)
		goto out;

	cur_state = hr_qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (ibqp->uobject &&
	    (attr_mask & IB_QP_STATE) && new_state == IB_QPS_ERR) {
		if (hr_qp->en_flags & HNS_ROCE_QP_CAP_SQ_RECORD_DB) {
			hr_qp->sq.head = *(int *)(hr_qp->sdb.virt_addr);

			if (hr_qp->en_flags & HNS_ROCE_QP_CAP_RQ_RECORD_DB)
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
		if (hr_dev->hw_rev == HNS_ROCE_HW_VER1) {
			ret = -EPERM;
			ibdev_err(&hr_dev->ib_dev,
				  "RST2RST state is not supported\n");
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

static inline void *get_wqe(struct hns_roce_qp *hr_qp, int offset)
{
	return hns_roce_buf_offset(hr_qp->mtr.kmem, offset);
}

void *hns_roce_get_recv_wqe(struct hns_roce_qp *hr_qp, unsigned int n)
{
	return get_wqe(hr_qp, hr_qp->rq.offset + (n << hr_qp->rq.wqe_shift));
}

void *hns_roce_get_send_wqe(struct hns_roce_qp *hr_qp, unsigned int n)
{
	return get_wqe(hr_qp, hr_qp->sq.offset + (n << hr_qp->sq.wqe_shift));
}

void *hns_roce_get_extend_sge(struct hns_roce_qp *hr_qp, unsigned int n)
{
	return get_wqe(hr_qp, hr_qp->sge.offset + (n << hr_qp->sge.sge_shift));
}

bool hns_roce_wq_overflow(struct hns_roce_wq *hr_wq, u32 nreq,
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
	unsigned int reserved_from_bot;
	unsigned int i;

	mutex_init(&qp_table->scc_mutex);
	mutex_init(&qp_table->bank_mutex);
	xa_init(&hr_dev->qp_table_xa);

	reserved_from_bot = hr_dev->caps.reserved_qps;

	for (i = 0; i < reserved_from_bot; i++) {
		hr_dev->qp_table.bank[get_qp_bankid(i)].inuse++;
		hr_dev->qp_table.bank[get_qp_bankid(i)].min++;
	}

	for (i = 0; i < HNS_ROCE_QP_BANK_NUM; i++) {
		ida_init(&hr_dev->qp_table.bank[i].ida);
		hr_dev->qp_table.bank[i].max = hr_dev->caps.num_qps /
					       HNS_ROCE_QP_BANK_NUM - 1;
		hr_dev->qp_table.bank[i].next = hr_dev->qp_table.bank[i].min;
	}

	return 0;
}

void hns_roce_cleanup_qp_table(struct hns_roce_dev *hr_dev)
{
	int i;

	for (i = 0; i < HNS_ROCE_QP_BANK_NUM; i++)
		ida_destroy(&hr_dev->qp_table.bank[i].ida);
}
