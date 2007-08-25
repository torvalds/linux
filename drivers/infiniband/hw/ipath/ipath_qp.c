/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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

#include <linux/err.h>
#include <linux/vmalloc.h>

#include "ipath_verbs.h"
#include "ipath_kernel.h"

#define BITS_PER_PAGE		(PAGE_SIZE*BITS_PER_BYTE)
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)
#define mk_qpn(qpt, map, off)	(((map) - (qpt)->map) * BITS_PER_PAGE + \
				 (off))
#define find_next_offset(map, off) find_next_zero_bit((map)->page, \
						      BITS_PER_PAGE, off)

/*
 * Convert the AETH credit code into the number of credits.
 */
static u32 credit_table[31] = {
	0,			/* 0 */
	1,			/* 1 */
	2,			/* 2 */
	3,			/* 3 */
	4,			/* 4 */
	6,			/* 5 */
	8,			/* 6 */
	12,			/* 7 */
	16,			/* 8 */
	24,			/* 9 */
	32,			/* A */
	48,			/* B */
	64,			/* C */
	96,			/* D */
	128,			/* E */
	192,			/* F */
	256,			/* 10 */
	384,			/* 11 */
	512,			/* 12 */
	768,			/* 13 */
	1024,			/* 14 */
	1536,			/* 15 */
	2048,			/* 16 */
	3072,			/* 17 */
	4096,			/* 18 */
	6144,			/* 19 */
	8192,			/* 1A */
	12288,			/* 1B */
	16384,			/* 1C */
	24576,			/* 1D */
	32768			/* 1E */
};


static void get_map_page(struct ipath_qp_table *qpt, struct qpn_map *map)
{
	unsigned long page = get_zeroed_page(GFP_KERNEL);
	unsigned long flags;

	/*
	 * Free the page if someone raced with us installing it.
	 */

	spin_lock_irqsave(&qpt->lock, flags);
	if (map->page)
		free_page(page);
	else
		map->page = (void *)page;
	spin_unlock_irqrestore(&qpt->lock, flags);
}


static int alloc_qpn(struct ipath_qp_table *qpt, enum ib_qp_type type)
{
	u32 i, offset, max_scan, qpn;
	struct qpn_map *map;
	u32 ret = -1;

	if (type == IB_QPT_SMI)
		ret = 0;
	else if (type == IB_QPT_GSI)
		ret = 1;

	if (ret != -1) {
		map = &qpt->map[0];
		if (unlikely(!map->page)) {
			get_map_page(qpt, map);
			if (unlikely(!map->page)) {
				ret = -ENOMEM;
				goto bail;
			}
		}
		if (!test_and_set_bit(ret, map->page))
			atomic_dec(&map->n_free);
		else
			ret = -EBUSY;
		goto bail;
	}

	qpn = qpt->last + 1;
	if (qpn >= QPN_MAX)
		qpn = 2;
	offset = qpn & BITS_PER_PAGE_MASK;
	map = &qpt->map[qpn / BITS_PER_PAGE];
	max_scan = qpt->nmaps - !offset;
	for (i = 0;;) {
		if (unlikely(!map->page)) {
			get_map_page(qpt, map);
			if (unlikely(!map->page))
				break;
		}
		if (likely(atomic_read(&map->n_free))) {
			do {
				if (!test_and_set_bit(offset, map->page)) {
					atomic_dec(&map->n_free);
					qpt->last = qpn;
					ret = qpn;
					goto bail;
				}
				offset = find_next_offset(map, offset);
				qpn = mk_qpn(qpt, map, offset);
				/*
				 * This test differs from alloc_pidmap().
				 * If find_next_offset() does find a zero
				 * bit, we don't need to check for QPN
				 * wrapping around past our starting QPN.
				 * We just need to be sure we don't loop
				 * forever.
				 */
			} while (offset < BITS_PER_PAGE && qpn < QPN_MAX);
		}
		/*
		 * In order to keep the number of pages allocated to a
		 * minimum, we scan the all existing pages before increasing
		 * the size of the bitmap table.
		 */
		if (++i > max_scan) {
			if (qpt->nmaps == QPNMAP_ENTRIES)
				break;
			map = &qpt->map[qpt->nmaps++];
			offset = 0;
		} else if (map < &qpt->map[qpt->nmaps]) {
			++map;
			offset = 0;
		} else {
			map = &qpt->map[0];
			offset = 2;
		}
		qpn = mk_qpn(qpt, map, offset);
	}

	ret = -ENOMEM;

bail:
	return ret;
}

static void free_qpn(struct ipath_qp_table *qpt, u32 qpn)
{
	struct qpn_map *map;

	map = qpt->map + qpn / BITS_PER_PAGE;
	if (map->page)
		clear_bit(qpn & BITS_PER_PAGE_MASK, map->page);
	atomic_inc(&map->n_free);
}

/**
 * ipath_alloc_qpn - allocate a QP number
 * @qpt: the QP table
 * @qp: the QP
 * @type: the QP type (IB_QPT_SMI and IB_QPT_GSI are special)
 *
 * Allocate the next available QPN and put the QP into the hash table.
 * The hash table holds a reference to the QP.
 */
static int ipath_alloc_qpn(struct ipath_qp_table *qpt, struct ipath_qp *qp,
			   enum ib_qp_type type)
{
	unsigned long flags;
	int ret;

	ret = alloc_qpn(qpt, type);
	if (ret < 0)
		goto bail;
	qp->ibqp.qp_num = ret;

	/* Add the QP to the hash table. */
	spin_lock_irqsave(&qpt->lock, flags);

	ret %= qpt->max;
	qp->next = qpt->table[ret];
	qpt->table[ret] = qp;
	atomic_inc(&qp->refcount);

	spin_unlock_irqrestore(&qpt->lock, flags);
	ret = 0;

bail:
	return ret;
}

/**
 * ipath_free_qp - remove a QP from the QP table
 * @qpt: the QP table
 * @qp: the QP to remove
 *
 * Remove the QP from the table so it can't be found asynchronously by
 * the receive interrupt routine.
 */
static void ipath_free_qp(struct ipath_qp_table *qpt, struct ipath_qp *qp)
{
	struct ipath_qp *q, **qpp;
	unsigned long flags;
	int fnd = 0;

	spin_lock_irqsave(&qpt->lock, flags);

	/* Remove QP from the hash table. */
	qpp = &qpt->table[qp->ibqp.qp_num % qpt->max];
	for (; (q = *qpp) != NULL; qpp = &q->next) {
		if (q == qp) {
			*qpp = qp->next;
			qp->next = NULL;
			atomic_dec(&qp->refcount);
			fnd = 1;
			break;
		}
	}

	spin_unlock_irqrestore(&qpt->lock, flags);

	if (!fnd)
		return;

	free_qpn(qpt, qp->ibqp.qp_num);

	wait_event(qp->wait, !atomic_read(&qp->refcount));
}

/**
 * ipath_free_all_qps - remove all QPs from the table
 * @qpt: the QP table to empty
 */
void ipath_free_all_qps(struct ipath_qp_table *qpt)
{
	unsigned long flags;
	struct ipath_qp *qp, *nqp;
	u32 n;

	for (n = 0; n < qpt->max; n++) {
		spin_lock_irqsave(&qpt->lock, flags);
		qp = qpt->table[n];
		qpt->table[n] = NULL;
		spin_unlock_irqrestore(&qpt->lock, flags);

		while (qp) {
			nqp = qp->next;
			free_qpn(qpt, qp->ibqp.qp_num);
			if (!atomic_dec_and_test(&qp->refcount) ||
			    !ipath_destroy_qp(&qp->ibqp))
				ipath_dbg("QP memory leak!\n");
			qp = nqp;
		}
	}

	for (n = 0; n < ARRAY_SIZE(qpt->map); n++) {
		if (qpt->map[n].page)
			free_page((unsigned long)qpt->map[n].page);
	}
}

/**
 * ipath_lookup_qpn - return the QP with the given QPN
 * @qpt: the QP table
 * @qpn: the QP number to look up
 *
 * The caller is responsible for decrementing the QP reference count
 * when done.
 */
struct ipath_qp *ipath_lookup_qpn(struct ipath_qp_table *qpt, u32 qpn)
{
	unsigned long flags;
	struct ipath_qp *qp;

	spin_lock_irqsave(&qpt->lock, flags);

	for (qp = qpt->table[qpn % qpt->max]; qp; qp = qp->next) {
		if (qp->ibqp.qp_num == qpn) {
			atomic_inc(&qp->refcount);
			break;
		}
	}

	spin_unlock_irqrestore(&qpt->lock, flags);
	return qp;
}

/**
 * ipath_reset_qp - initialize the QP state to the reset state
 * @qp: the QP to reset
 */
static void ipath_reset_qp(struct ipath_qp *qp)
{
	qp->remote_qpn = 0;
	qp->qkey = 0;
	qp->qp_access_flags = 0;
	qp->s_busy = 0;
	qp->s_flags &= IPATH_S_SIGNAL_REQ_WR;
	qp->s_hdrwords = 0;
	qp->s_wqe = NULL;
	qp->s_psn = 0;
	qp->r_psn = 0;
	qp->r_msn = 0;
	if (qp->ibqp.qp_type == IB_QPT_RC) {
		qp->s_state = IB_OPCODE_RC_SEND_LAST;
		qp->r_state = IB_OPCODE_RC_SEND_LAST;
	} else {
		qp->s_state = IB_OPCODE_UC_SEND_LAST;
		qp->r_state = IB_OPCODE_UC_SEND_LAST;
	}
	qp->s_ack_state = IB_OPCODE_RC_ACKNOWLEDGE;
	qp->r_nak_state = 0;
	qp->r_wrid_valid = 0;
	qp->s_rnr_timeout = 0;
	qp->s_head = 0;
	qp->s_tail = 0;
	qp->s_cur = 0;
	qp->s_last = 0;
	qp->s_ssn = 1;
	qp->s_lsn = 0;
	qp->s_wait_credit = 0;
	memset(qp->s_ack_queue, 0, sizeof(qp->s_ack_queue));
	qp->r_head_ack_queue = 0;
	qp->s_tail_ack_queue = 0;
	qp->s_num_rd_atomic = 0;
	if (qp->r_rq.wq) {
		qp->r_rq.wq->head = 0;
		qp->r_rq.wq->tail = 0;
	}
	qp->r_reuse_sge = 0;
}

/**
 * ipath_error_qp - put a QP into an error state
 * @qp: the QP to put into an error state
 * @err: the receive completion error to signal if a RWQE is active
 *
 * Flushes both send and receive work queues.
 * Returns true if last WQE event should be generated.
 * The QP s_lock should be held and interrupts disabled.
 */

int ipath_error_qp(struct ipath_qp *qp, enum ib_wc_status err)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	struct ib_wc wc;
	int ret = 0;

	ipath_dbg("QP%d/%d in error state\n",
		  qp->ibqp.qp_num, qp->remote_qpn);

	spin_lock(&dev->pending_lock);
	/* XXX What if its already removed by the timeout code? */
	if (!list_empty(&qp->timerwait))
		list_del_init(&qp->timerwait);
	if (!list_empty(&qp->piowait))
		list_del_init(&qp->piowait);
	spin_unlock(&dev->pending_lock);

	wc.vendor_err = 0;
	wc.byte_len = 0;
	wc.imm_data = 0;
	wc.qp = &qp->ibqp;
	wc.src_qp = 0;
	wc.wc_flags = 0;
	wc.pkey_index = 0;
	wc.slid = 0;
	wc.sl = 0;
	wc.dlid_path_bits = 0;
	wc.port_num = 0;
	if (qp->r_wrid_valid) {
		qp->r_wrid_valid = 0;
		wc.wr_id = qp->r_wr_id;
		wc.opcode = IB_WC_RECV;
		wc.status = err;
		ipath_cq_enter(to_icq(qp->ibqp.send_cq), &wc, 1);
	}
	wc.status = IB_WC_WR_FLUSH_ERR;

	while (qp->s_last != qp->s_head) {
		struct ipath_swqe *wqe = get_swqe_ptr(qp, qp->s_last);

		wc.wr_id = wqe->wr.wr_id;
		wc.opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
		if (++qp->s_last >= qp->s_size)
			qp->s_last = 0;
		ipath_cq_enter(to_icq(qp->ibqp.send_cq), &wc, 1);
	}
	qp->s_cur = qp->s_tail = qp->s_head;
	qp->s_hdrwords = 0;
	qp->s_ack_state = IB_OPCODE_RC_ACKNOWLEDGE;

	if (qp->r_rq.wq) {
		struct ipath_rwq *wq;
		u32 head;
		u32 tail;

		spin_lock(&qp->r_rq.lock);

		/* sanity check pointers before trusting them */
		wq = qp->r_rq.wq;
		head = wq->head;
		if (head >= qp->r_rq.size)
			head = 0;
		tail = wq->tail;
		if (tail >= qp->r_rq.size)
			tail = 0;
		wc.opcode = IB_WC_RECV;
		while (tail != head) {
			wc.wr_id = get_rwqe_ptr(&qp->r_rq, tail)->wr_id;
			if (++tail >= qp->r_rq.size)
				tail = 0;
			ipath_cq_enter(to_icq(qp->ibqp.recv_cq), &wc, 1);
		}
		wq->tail = tail;

		spin_unlock(&qp->r_rq.lock);
	} else if (qp->ibqp.event_handler)
		ret = 1;

	return ret;
}

/**
 * ipath_modify_qp - modify the attributes of a queue pair
 * @ibqp: the queue pair who's attributes we're modifying
 * @attr: the new attributes
 * @attr_mask: the mask of attributes to modify
 * @udata: user data for ipathverbs.so
 *
 * Returns 0 on success, otherwise returns an errno.
 */
int ipath_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		    int attr_mask, struct ib_udata *udata)
{
	struct ipath_ibdev *dev = to_idev(ibqp->device);
	struct ipath_qp *qp = to_iqp(ibqp);
	enum ib_qp_state cur_state, new_state;
	unsigned long flags;
	int lastwqe = 0;
	int ret;

	spin_lock_irqsave(&qp->s_lock, flags);

	cur_state = attr_mask & IB_QP_CUR_STATE ?
		attr->cur_qp_state : qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
				attr_mask))
		goto inval;

	if (attr_mask & IB_QP_AV) {
		if (attr->ah_attr.dlid == 0 ||
		    attr->ah_attr.dlid >= IPATH_MULTICAST_LID_BASE)
			goto inval;

		if ((attr->ah_attr.ah_flags & IB_AH_GRH) &&
		    (attr->ah_attr.grh.sgid_index > 1))
			goto inval;
	}

	if (attr_mask & IB_QP_PKEY_INDEX)
		if (attr->pkey_index >= ipath_get_npkeys(dev->dd))
			goto inval;

	if (attr_mask & IB_QP_MIN_RNR_TIMER)
		if (attr->min_rnr_timer > 31)
			goto inval;

	if (attr_mask & IB_QP_PORT)
		if (attr->port_num == 0 ||
		    attr->port_num > ibqp->device->phys_port_cnt)
			goto inval;

	/*
	 * Note: the chips support a maximum MTU of 4096, but the driver
	 * hasn't implemented this feature yet, so don't allow Path MTU
	 * values greater than 2048.
	 */
	if (attr_mask & IB_QP_PATH_MTU)
		if (attr->path_mtu > IB_MTU_2048)
			goto inval;

	if (attr_mask & IB_QP_PATH_MIG_STATE)
		if (attr->path_mig_state != IB_MIG_MIGRATED &&
		    attr->path_mig_state != IB_MIG_REARM)
			goto inval;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		if (attr->max_dest_rd_atomic > IPATH_MAX_RDMA_ATOMIC)
			goto inval;

	switch (new_state) {
	case IB_QPS_RESET:
		ipath_reset_qp(qp);
		break;

	case IB_QPS_ERR:
		lastwqe = ipath_error_qp(qp, IB_WC_WR_FLUSH_ERR);
		break;

	default:
		break;

	}

	if (attr_mask & IB_QP_PKEY_INDEX)
		qp->s_pkey_index = attr->pkey_index;

	if (attr_mask & IB_QP_DEST_QPN)
		qp->remote_qpn = attr->dest_qp_num;

	if (attr_mask & IB_QP_SQ_PSN) {
		qp->s_psn = qp->s_next_psn = attr->sq_psn;
		qp->s_last_psn = qp->s_next_psn - 1;
	}

	if (attr_mask & IB_QP_RQ_PSN)
		qp->r_psn = attr->rq_psn;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->qp_access_flags = attr->qp_access_flags;

	if (attr_mask & IB_QP_AV)
		qp->remote_ah_attr = attr->ah_attr;

	if (attr_mask & IB_QP_PATH_MTU)
		qp->path_mtu = attr->path_mtu;

	if (attr_mask & IB_QP_RETRY_CNT)
		qp->s_retry = qp->s_retry_cnt = attr->retry_cnt;

	if (attr_mask & IB_QP_RNR_RETRY) {
		qp->s_rnr_retry = attr->rnr_retry;
		if (qp->s_rnr_retry > 7)
			qp->s_rnr_retry = 7;
		qp->s_rnr_retry_cnt = qp->s_rnr_retry;
	}

	if (attr_mask & IB_QP_MIN_RNR_TIMER)
		qp->r_min_rnr_timer = attr->min_rnr_timer;

	if (attr_mask & IB_QP_TIMEOUT)
		qp->timeout = attr->timeout;

	if (attr_mask & IB_QP_QKEY)
		qp->qkey = attr->qkey;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		qp->r_max_rd_atomic = attr->max_dest_rd_atomic;

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC)
		qp->s_max_rd_atomic = attr->max_rd_atomic;

	qp->state = new_state;
	spin_unlock_irqrestore(&qp->s_lock, flags);

	if (lastwqe) {
		struct ib_event ev;

		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_QP_LAST_WQE_REACHED;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
	ret = 0;
	goto bail;

inval:
	spin_unlock_irqrestore(&qp->s_lock, flags);
	ret = -EINVAL;

bail:
	return ret;
}

int ipath_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		   int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct ipath_qp *qp = to_iqp(ibqp);

	attr->qp_state = qp->state;
	attr->cur_qp_state = attr->qp_state;
	attr->path_mtu = qp->path_mtu;
	attr->path_mig_state = 0;
	attr->qkey = qp->qkey;
	attr->rq_psn = qp->r_psn;
	attr->sq_psn = qp->s_next_psn;
	attr->dest_qp_num = qp->remote_qpn;
	attr->qp_access_flags = qp->qp_access_flags;
	attr->cap.max_send_wr = qp->s_size - 1;
	attr->cap.max_recv_wr = qp->ibqp.srq ? 0 : qp->r_rq.size - 1;
	attr->cap.max_send_sge = qp->s_max_sge;
	attr->cap.max_recv_sge = qp->r_rq.max_sge;
	attr->cap.max_inline_data = 0;
	attr->ah_attr = qp->remote_ah_attr;
	memset(&attr->alt_ah_attr, 0, sizeof(attr->alt_ah_attr));
	attr->pkey_index = qp->s_pkey_index;
	attr->alt_pkey_index = 0;
	attr->en_sqd_async_notify = 0;
	attr->sq_draining = 0;
	attr->max_rd_atomic = qp->s_max_rd_atomic;
	attr->max_dest_rd_atomic = qp->r_max_rd_atomic;
	attr->min_rnr_timer = qp->r_min_rnr_timer;
	attr->port_num = 1;
	attr->timeout = qp->timeout;
	attr->retry_cnt = qp->s_retry_cnt;
	attr->rnr_retry = qp->s_rnr_retry;
	attr->alt_port_num = 0;
	attr->alt_timeout = 0;

	init_attr->event_handler = qp->ibqp.event_handler;
	init_attr->qp_context = qp->ibqp.qp_context;
	init_attr->send_cq = qp->ibqp.send_cq;
	init_attr->recv_cq = qp->ibqp.recv_cq;
	init_attr->srq = qp->ibqp.srq;
	init_attr->cap = attr->cap;
	if (qp->s_flags & IPATH_S_SIGNAL_REQ_WR)
		init_attr->sq_sig_type = IB_SIGNAL_REQ_WR;
	else
		init_attr->sq_sig_type = IB_SIGNAL_ALL_WR;
	init_attr->qp_type = qp->ibqp.qp_type;
	init_attr->port_num = 1;
	return 0;
}

/**
 * ipath_compute_aeth - compute the AETH (syndrome + MSN)
 * @qp: the queue pair to compute the AETH for
 *
 * Returns the AETH.
 */
__be32 ipath_compute_aeth(struct ipath_qp *qp)
{
	u32 aeth = qp->r_msn & IPATH_MSN_MASK;

	if (qp->ibqp.srq) {
		/*
		 * Shared receive queues don't generate credits.
		 * Set the credit field to the invalid value.
		 */
		aeth |= IPATH_AETH_CREDIT_INVAL << IPATH_AETH_CREDIT_SHIFT;
	} else {
		u32 min, max, x;
		u32 credits;
		struct ipath_rwq *wq = qp->r_rq.wq;
		u32 head;
		u32 tail;

		/* sanity check pointers before trusting them */
		head = wq->head;
		if (head >= qp->r_rq.size)
			head = 0;
		tail = wq->tail;
		if (tail >= qp->r_rq.size)
			tail = 0;
		/*
		 * Compute the number of credits available (RWQEs).
		 * XXX Not holding the r_rq.lock here so there is a small
		 * chance that the pair of reads are not atomic.
		 */
		credits = head - tail;
		if ((int)credits < 0)
			credits += qp->r_rq.size;
		/*
		 * Binary search the credit table to find the code to
		 * use.
		 */
		min = 0;
		max = 31;
		for (;;) {
			x = (min + max) / 2;
			if (credit_table[x] == credits)
				break;
			if (credit_table[x] > credits)
				max = x;
			else if (min == x)
				break;
			else
				min = x;
		}
		aeth |= x << IPATH_AETH_CREDIT_SHIFT;
	}
	return cpu_to_be32(aeth);
}

/**
 * ipath_create_qp - create a queue pair for a device
 * @ibpd: the protection domain who's device we create the queue pair for
 * @init_attr: the attributes of the queue pair
 * @udata: unused by InfiniPath
 *
 * Returns the queue pair on success, otherwise returns an errno.
 *
 * Called by the ib_create_qp() core verbs function.
 */
struct ib_qp *ipath_create_qp(struct ib_pd *ibpd,
			      struct ib_qp_init_attr *init_attr,
			      struct ib_udata *udata)
{
	struct ipath_qp *qp;
	int err;
	struct ipath_swqe *swq = NULL;
	struct ipath_ibdev *dev;
	size_t sz;
	struct ib_qp *ret;

	if (init_attr->cap.max_send_sge > ib_ipath_max_sges ||
	    init_attr->cap.max_recv_sge > ib_ipath_max_sges ||
	    init_attr->cap.max_send_wr > ib_ipath_max_qp_wrs ||
	    init_attr->cap.max_recv_wr > ib_ipath_max_qp_wrs) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	if (init_attr->cap.max_send_sge +
	    init_attr->cap.max_recv_sge +
	    init_attr->cap.max_send_wr +
	    init_attr->cap.max_recv_wr == 0) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	switch (init_attr->qp_type) {
	case IB_QPT_UC:
	case IB_QPT_RC:
	case IB_QPT_UD:
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		sz = sizeof(struct ipath_sge) *
			init_attr->cap.max_send_sge +
			sizeof(struct ipath_swqe);
		swq = vmalloc((init_attr->cap.max_send_wr + 1) * sz);
		if (swq == NULL) {
			ret = ERR_PTR(-ENOMEM);
			goto bail;
		}
		sz = sizeof(*qp);
		if (init_attr->srq) {
			struct ipath_srq *srq = to_isrq(init_attr->srq);

			sz += sizeof(*qp->r_sg_list) *
				srq->rq.max_sge;
		} else
			sz += sizeof(*qp->r_sg_list) *
				init_attr->cap.max_recv_sge;
		qp = kmalloc(sz, GFP_KERNEL);
		if (!qp) {
			ret = ERR_PTR(-ENOMEM);
			goto bail_swq;
		}
		if (init_attr->srq) {
			sz = 0;
			qp->r_rq.size = 0;
			qp->r_rq.max_sge = 0;
			qp->r_rq.wq = NULL;
			init_attr->cap.max_recv_wr = 0;
			init_attr->cap.max_recv_sge = 0;
		} else {
			qp->r_rq.size = init_attr->cap.max_recv_wr + 1;
			qp->r_rq.max_sge = init_attr->cap.max_recv_sge;
			sz = (sizeof(struct ib_sge) * qp->r_rq.max_sge) +
				sizeof(struct ipath_rwqe);
			qp->r_rq.wq = vmalloc_user(sizeof(struct ipath_rwq) +
					      qp->r_rq.size * sz);
			if (!qp->r_rq.wq) {
				ret = ERR_PTR(-ENOMEM);
				goto bail_qp;
			}
		}

		/*
		 * ib_create_qp() will initialize qp->ibqp
		 * except for qp->ibqp.qp_num.
		 */
		spin_lock_init(&qp->s_lock);
		spin_lock_init(&qp->r_rq.lock);
		atomic_set(&qp->refcount, 0);
		init_waitqueue_head(&qp->wait);
		tasklet_init(&qp->s_task, ipath_do_send, (unsigned long)qp);
		INIT_LIST_HEAD(&qp->piowait);
		INIT_LIST_HEAD(&qp->timerwait);
		qp->state = IB_QPS_RESET;
		qp->s_wq = swq;
		qp->s_size = init_attr->cap.max_send_wr + 1;
		qp->s_max_sge = init_attr->cap.max_send_sge;
		if (init_attr->sq_sig_type == IB_SIGNAL_REQ_WR)
			qp->s_flags = IPATH_S_SIGNAL_REQ_WR;
		else
			qp->s_flags = 0;
		dev = to_idev(ibpd->device);
		err = ipath_alloc_qpn(&dev->qp_table, qp,
				      init_attr->qp_type);
		if (err) {
			ret = ERR_PTR(err);
			goto bail_rwq;
		}
		qp->ip = NULL;
		ipath_reset_qp(qp);
		break;

	default:
		/* Don't support raw QPs */
		ret = ERR_PTR(-ENOSYS);
		goto bail;
	}

	init_attr->cap.max_inline_data = 0;

	/*
	 * Return the address of the RWQ as the offset to mmap.
	 * See ipath_mmap() for details.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		int err;

		if (!qp->r_rq.wq) {
			__u64 offset = 0;

			err = ib_copy_to_udata(udata, &offset,
					       sizeof(offset));
			if (err) {
				ret = ERR_PTR(err);
				goto bail_rwq;
			}
		} else {
			u32 s = sizeof(struct ipath_rwq) +
				qp->r_rq.size * sz;

			qp->ip =
			    ipath_create_mmap_info(dev, s,
						   ibpd->uobject->context,
						   qp->r_rq.wq);
			if (!qp->ip) {
				ret = ERR_PTR(-ENOMEM);
				goto bail_rwq;
			}

			err = ib_copy_to_udata(udata, &(qp->ip->offset),
					       sizeof(qp->ip->offset));
			if (err) {
				ret = ERR_PTR(err);
				goto bail_ip;
			}
		}
	}

	spin_lock(&dev->n_qps_lock);
	if (dev->n_qps_allocated == ib_ipath_max_qps) {
		spin_unlock(&dev->n_qps_lock);
		ret = ERR_PTR(-ENOMEM);
		goto bail_ip;
	}

	dev->n_qps_allocated++;
	spin_unlock(&dev->n_qps_lock);

	if (qp->ip) {
		spin_lock_irq(&dev->pending_lock);
		list_add(&qp->ip->pending_mmaps, &dev->pending_mmaps);
		spin_unlock_irq(&dev->pending_lock);
	}

	ret = &qp->ibqp;
	goto bail;

bail_ip:
	kfree(qp->ip);
bail_rwq:
	vfree(qp->r_rq.wq);
bail_qp:
	kfree(qp);
bail_swq:
	vfree(swq);
bail:
	return ret;
}

/**
 * ipath_destroy_qp - destroy a queue pair
 * @ibqp: the queue pair to destroy
 *
 * Returns 0 on success.
 *
 * Note that this can be called while the QP is actively sending or
 * receiving!
 */
int ipath_destroy_qp(struct ib_qp *ibqp)
{
	struct ipath_qp *qp = to_iqp(ibqp);
	struct ipath_ibdev *dev = to_idev(ibqp->device);
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	qp->state = IB_QPS_ERR;
	spin_unlock_irqrestore(&qp->s_lock, flags);
	spin_lock(&dev->n_qps_lock);
	dev->n_qps_allocated--;
	spin_unlock(&dev->n_qps_lock);

	/* Stop the sending tasklet. */
	tasklet_kill(&qp->s_task);

	/* Make sure the QP isn't on the timeout list. */
	spin_lock_irqsave(&dev->pending_lock, flags);
	if (!list_empty(&qp->timerwait))
		list_del_init(&qp->timerwait);
	if (!list_empty(&qp->piowait))
		list_del_init(&qp->piowait);
	spin_unlock_irqrestore(&dev->pending_lock, flags);

	/*
	 * Make sure that the QP is not in the QPN table so receive
	 * interrupts will discard packets for this QP.  XXX Also remove QP
	 * from multicast table.
	 */
	if (atomic_read(&qp->refcount) != 0)
		ipath_free_qp(&dev->qp_table, qp);

	if (qp->ip)
		kref_put(&qp->ip->ref, ipath_release_mmap_info);
	else
		vfree(qp->r_rq.wq);
	vfree(qp->s_wq);
	kfree(qp);
	return 0;
}

/**
 * ipath_init_qp_table - initialize the QP table for a device
 * @idev: the device who's QP table we're initializing
 * @size: the size of the QP table
 *
 * Returns 0 on success, otherwise returns an errno.
 */
int ipath_init_qp_table(struct ipath_ibdev *idev, int size)
{
	int i;
	int ret;

	idev->qp_table.last = 1;	/* QPN 0 and 1 are special. */
	idev->qp_table.max = size;
	idev->qp_table.nmaps = 1;
	idev->qp_table.table = kzalloc(size * sizeof(*idev->qp_table.table),
				       GFP_KERNEL);
	if (idev->qp_table.table == NULL) {
		ret = -ENOMEM;
		goto bail;
	}

	for (i = 0; i < ARRAY_SIZE(idev->qp_table.map); i++) {
		atomic_set(&idev->qp_table.map[i].n_free, BITS_PER_PAGE);
		idev->qp_table.map[i].page = NULL;
	}

	ret = 0;

bail:
	return ret;
}

/**
 * ipath_sqerror_qp - put a QP's send queue into an error state
 * @qp: QP who's send queue will be put into an error state
 * @wc: the WC responsible for putting the QP in this state
 *
 * Flushes the send work queue.
 * The QP s_lock should be held and interrupts disabled.
 */

void ipath_sqerror_qp(struct ipath_qp *qp, struct ib_wc *wc)
{
	struct ipath_ibdev *dev = to_idev(qp->ibqp.device);
	struct ipath_swqe *wqe = get_swqe_ptr(qp, qp->s_last);

	ipath_dbg("Send queue error on QP%d/%d: err: %d\n",
		  qp->ibqp.qp_num, qp->remote_qpn, wc->status);

	spin_lock(&dev->pending_lock);
	/* XXX What if its already removed by the timeout code? */
	if (!list_empty(&qp->timerwait))
		list_del_init(&qp->timerwait);
	if (!list_empty(&qp->piowait))
		list_del_init(&qp->piowait);
	spin_unlock(&dev->pending_lock);

	ipath_cq_enter(to_icq(qp->ibqp.send_cq), wc, 1);
	if (++qp->s_last >= qp->s_size)
		qp->s_last = 0;

	wc->status = IB_WC_WR_FLUSH_ERR;

	while (qp->s_last != qp->s_head) {
		wqe = get_swqe_ptr(qp, qp->s_last);
		wc->wr_id = wqe->wr.wr_id;
		wc->opcode = ib_ipath_wc_opcode[wqe->wr.opcode];
		ipath_cq_enter(to_icq(qp->ibqp.send_cq), wc, 1);
		if (++qp->s_last >= qp->s_size)
			qp->s_last = 0;
	}
	qp->s_cur = qp->s_tail = qp->s_head;
	qp->state = IB_QPS_SQE;
}

/**
 * ipath_get_credit - flush the send work queue of a QP
 * @qp: the qp who's send work queue to flush
 * @aeth: the Acknowledge Extended Transport Header
 *
 * The QP s_lock should be held.
 */
void ipath_get_credit(struct ipath_qp *qp, u32 aeth)
{
	u32 credit = (aeth >> IPATH_AETH_CREDIT_SHIFT) & IPATH_AETH_CREDIT_MASK;

	/*
	 * If the credit is invalid, we can send
	 * as many packets as we like.  Otherwise, we have to
	 * honor the credit field.
	 */
	if (credit == IPATH_AETH_CREDIT_INVAL)
		qp->s_lsn = (u32) -1;
	else if (qp->s_lsn != (u32) -1) {
		/* Compute new LSN (i.e., MSN + credit) */
		credit = (aeth + credit_table[credit]) & IPATH_MSN_MASK;
		if (ipath_cmp24(credit, qp->s_lsn) > 0)
			qp->s_lsn = credit;
	}

	/* Restart sending if it was blocked due to lack of credits. */
	if (qp->s_cur != qp->s_head &&
	    (qp->s_lsn == (u32) -1 ||
	     ipath_cmp24(get_swqe_ptr(qp, qp->s_cur)->ssn,
			 qp->s_lsn + 1) <= 0))
		tasklet_hi_schedule(&qp->s_task);
}
