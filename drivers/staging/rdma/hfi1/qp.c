/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/hash.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/seq_file.h>

#include "hfi.h"
#include "qp.h"
#include "trace.h"
#include "sdma.h"

#define BITS_PER_PAGE           (PAGE_SIZE*BITS_PER_BYTE)
#define BITS_PER_PAGE_MASK      (BITS_PER_PAGE-1)

static unsigned int hfi1_qp_table_size = 256;
module_param_named(qp_table_size, hfi1_qp_table_size, uint, S_IRUGO);
MODULE_PARM_DESC(qp_table_size, "QP table size");

static void flush_tx_list(struct hfi1_qp *qp);
static int iowait_sleep(
	struct sdma_engine *sde,
	struct iowait *wait,
	struct sdma_txreq *stx,
	unsigned seq);
static void iowait_wakeup(struct iowait *wait, int reason);

static inline unsigned mk_qpn(struct hfi1_qpn_table *qpt,
			      struct qpn_map *map, unsigned off)
{
	return (map - qpt->map) * BITS_PER_PAGE + off;
}

/*
 * Convert the AETH credit code into the number of credits.
 */
static const u16 credit_table[31] = {
	0,                      /* 0 */
	1,                      /* 1 */
	2,                      /* 2 */
	3,                      /* 3 */
	4,                      /* 4 */
	6,                      /* 5 */
	8,                      /* 6 */
	12,                     /* 7 */
	16,                     /* 8 */
	24,                     /* 9 */
	32,                     /* A */
	48,                     /* B */
	64,                     /* C */
	96,                     /* D */
	128,                    /* E */
	192,                    /* F */
	256,                    /* 10 */
	384,                    /* 11 */
	512,                    /* 12 */
	768,                    /* 13 */
	1024,                   /* 14 */
	1536,                   /* 15 */
	2048,                   /* 16 */
	3072,                   /* 17 */
	4096,                   /* 18 */
	6144,                   /* 19 */
	8192,                   /* 1A */
	12288,                  /* 1B */
	16384,                  /* 1C */
	24576,                  /* 1D */
	32768                   /* 1E */
};

static void get_map_page(struct hfi1_qpn_table *qpt, struct qpn_map *map)
{
	unsigned long page = get_zeroed_page(GFP_KERNEL);

	/*
	 * Free the page if someone raced with us installing it.
	 */

	spin_lock(&qpt->lock);
	if (map->page)
		free_page(page);
	else
		map->page = (void *)page;
	spin_unlock(&qpt->lock);
}

/*
 * Allocate the next available QPN or
 * zero/one for QP type IB_QPT_SMI/IB_QPT_GSI.
 */
static int alloc_qpn(struct hfi1_devdata *dd, struct hfi1_qpn_table *qpt,
		     enum ib_qp_type type, u8 port)
{
	u32 i, offset, max_scan, qpn;
	struct qpn_map *map;
	u32 ret;

	if (type == IB_QPT_SMI || type == IB_QPT_GSI) {
		unsigned n;

		ret = type == IB_QPT_GSI;
		n = 1 << (ret + 2 * (port - 1));
		spin_lock(&qpt->lock);
		if (qpt->flags & n)
			ret = -EINVAL;
		else
			qpt->flags |= n;
		spin_unlock(&qpt->lock);
		goto bail;
	}

	qpn = qpt->last + qpt->incr;
	if (qpn >= QPN_MAX)
		qpn = qpt->incr | ((qpt->last & 1) ^ 1);
	/* offset carries bit 0 */
	offset = qpn & BITS_PER_PAGE_MASK;
	map = &qpt->map[qpn / BITS_PER_PAGE];
	max_scan = qpt->nmaps - !offset;
	for (i = 0;;) {
		if (unlikely(!map->page)) {
			get_map_page(qpt, map);
			if (unlikely(!map->page))
				break;
		}
		do {
			if (!test_and_set_bit(offset, map->page)) {
				qpt->last = qpn;
				ret = qpn;
				goto bail;
			}
			offset += qpt->incr;
			/*
			 * This qpn might be bogus if offset >= BITS_PER_PAGE.
			 * That is OK.   It gets re-assigned below
			 */
			qpn = mk_qpn(qpt, map, offset);
		} while (offset < BITS_PER_PAGE && qpn < QPN_MAX);
		/*
		 * In order to keep the number of pages allocated to a
		 * minimum, we scan the all existing pages before increasing
		 * the size of the bitmap table.
		 */
		if (++i > max_scan) {
			if (qpt->nmaps == QPNMAP_ENTRIES)
				break;
			map = &qpt->map[qpt->nmaps++];
			/* start at incr with current bit 0 */
			offset = qpt->incr | (offset & 1);
		} else if (map < &qpt->map[qpt->nmaps]) {
			++map;
			/* start at incr with current bit 0 */
			offset = qpt->incr | (offset & 1);
		} else {
			map = &qpt->map[0];
			/* wrap to first map page, invert bit 0 */
			offset = qpt->incr | ((offset & 1) ^ 1);
		}
		/* there can be no bits at shift and below */
		WARN_ON(offset & (dd->qos_shift - 1));
		qpn = mk_qpn(qpt, map, offset);
	}

	ret = -ENOMEM;

bail:
	return ret;
}

static void free_qpn(struct hfi1_qpn_table *qpt, u32 qpn)
{
	struct qpn_map *map;

	map = qpt->map + qpn / BITS_PER_PAGE;
	if (map->page)
		clear_bit(qpn & BITS_PER_PAGE_MASK, map->page);
}

/*
 * Put the QP into the hash table.
 * The hash table holds a reference to the QP.
 */
static void insert_qp(struct hfi1_ibdev *dev, struct hfi1_qp *qp)
{
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	unsigned long flags;

	atomic_inc(&qp->refcount);
	spin_lock_irqsave(&dev->qp_dev->qpt_lock, flags);

	if (qp->ibqp.qp_num <= 1) {
		rcu_assign_pointer(ibp->qp[qp->ibqp.qp_num], qp);
	} else {
		u32 n = qpn_hash(dev->qp_dev, qp->ibqp.qp_num);

		qp->next = dev->qp_dev->qp_table[n];
		rcu_assign_pointer(dev->qp_dev->qp_table[n], qp);
		trace_hfi1_qpinsert(qp, n);
	}

	spin_unlock_irqrestore(&dev->qp_dev->qpt_lock, flags);
}

/*
 * Remove the QP from the table so it can't be found asynchronously by
 * the receive interrupt routine.
 */
static void remove_qp(struct hfi1_ibdev *dev, struct hfi1_qp *qp)
{
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	u32 n = qpn_hash(dev->qp_dev, qp->ibqp.qp_num);
	unsigned long flags;
	int removed = 1;

	spin_lock_irqsave(&dev->qp_dev->qpt_lock, flags);

	if (rcu_dereference_protected(ibp->qp[0],
			lockdep_is_held(&dev->qp_dev->qpt_lock)) == qp) {
		RCU_INIT_POINTER(ibp->qp[0], NULL);
	} else if (rcu_dereference_protected(ibp->qp[1],
			lockdep_is_held(&dev->qp_dev->qpt_lock)) == qp) {
		RCU_INIT_POINTER(ibp->qp[1], NULL);
	} else {
		struct hfi1_qp *q;
		struct hfi1_qp __rcu **qpp;

		removed = 0;
		qpp = &dev->qp_dev->qp_table[n];
		for (; (q = rcu_dereference_protected(*qpp,
				lockdep_is_held(&dev->qp_dev->qpt_lock)))
					!= NULL;
				qpp = &q->next)
			if (q == qp) {
				RCU_INIT_POINTER(*qpp,
				 rcu_dereference_protected(qp->next,
				 lockdep_is_held(&dev->qp_dev->qpt_lock)));
				removed = 1;
				trace_hfi1_qpremove(qp, n);
				break;
			}
	}

	spin_unlock_irqrestore(&dev->qp_dev->qpt_lock, flags);
	if (removed) {
		synchronize_rcu();
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	}
}

/**
 * free_all_qps - check for QPs still in use
 * @qpt: the QP table to empty
 *
 * There should not be any QPs still in use.
 * Free memory for table.
 */
static unsigned free_all_qps(struct hfi1_devdata *dd)
{
	struct hfi1_ibdev *dev = &dd->verbs_dev;
	unsigned long flags;
	struct hfi1_qp *qp;
	unsigned n, qp_inuse = 0;

	for (n = 0; n < dd->num_pports; n++) {
		struct hfi1_ibport *ibp = &dd->pport[n].ibport_data;

		if (!hfi1_mcast_tree_empty(ibp))
			qp_inuse++;
		rcu_read_lock();
		if (rcu_dereference(ibp->qp[0]))
			qp_inuse++;
		if (rcu_dereference(ibp->qp[1]))
			qp_inuse++;
		rcu_read_unlock();
	}

	if (!dev->qp_dev)
		goto bail;
	spin_lock_irqsave(&dev->qp_dev->qpt_lock, flags);
	for (n = 0; n < dev->qp_dev->qp_table_size; n++) {
		qp = rcu_dereference_protected(dev->qp_dev->qp_table[n],
			lockdep_is_held(&dev->qp_dev->qpt_lock));
		RCU_INIT_POINTER(dev->qp_dev->qp_table[n], NULL);

		for (; qp; qp = rcu_dereference_protected(qp->next,
				lockdep_is_held(&dev->qp_dev->qpt_lock)))
			qp_inuse++;
	}
	spin_unlock_irqrestore(&dev->qp_dev->qpt_lock, flags);
	synchronize_rcu();
bail:
	return qp_inuse;
}

/**
 * reset_qp - initialize the QP state to the reset state
 * @qp: the QP to reset
 * @type: the QP type
 */
static void reset_qp(struct hfi1_qp *qp, enum ib_qp_type type)
{
	qp->remote_qpn = 0;
	qp->qkey = 0;
	qp->qp_access_flags = 0;
	iowait_init(
		&qp->s_iowait,
		1,
		hfi1_do_send,
		iowait_sleep,
		iowait_wakeup);
	qp->s_flags &= HFI1_S_SIGNAL_REQ_WR;
	qp->s_hdrwords = 0;
	qp->s_wqe = NULL;
	qp->s_draining = 0;
	qp->s_next_psn = 0;
	qp->s_last_psn = 0;
	qp->s_sending_psn = 0;
	qp->s_sending_hpsn = 0;
	qp->s_psn = 0;
	qp->r_psn = 0;
	qp->r_msn = 0;
	if (type == IB_QPT_RC) {
		qp->s_state = IB_OPCODE_RC_SEND_LAST;
		qp->r_state = IB_OPCODE_RC_SEND_LAST;
	} else {
		qp->s_state = IB_OPCODE_UC_SEND_LAST;
		qp->r_state = IB_OPCODE_UC_SEND_LAST;
	}
	qp->s_ack_state = IB_OPCODE_RC_ACKNOWLEDGE;
	qp->r_nak_state = 0;
	qp->r_aflags = 0;
	qp->r_flags = 0;
	qp->s_head = 0;
	qp->s_tail = 0;
	qp->s_cur = 0;
	qp->s_acked = 0;
	qp->s_last = 0;
	qp->s_ssn = 1;
	qp->s_lsn = 0;
	clear_ahg(qp);
	qp->s_mig_state = IB_MIG_MIGRATED;
	memset(qp->s_ack_queue, 0, sizeof(qp->s_ack_queue));
	qp->r_head_ack_queue = 0;
	qp->s_tail_ack_queue = 0;
	qp->s_num_rd_atomic = 0;
	if (qp->r_rq.wq) {
		qp->r_rq.wq->head = 0;
		qp->r_rq.wq->tail = 0;
	}
	qp->r_sge.num_sge = 0;
}

static void clear_mr_refs(struct hfi1_qp *qp, int clr_sends)
{
	unsigned n;

	if (test_and_clear_bit(HFI1_R_REWIND_SGE, &qp->r_aflags))
		hfi1_put_ss(&qp->s_rdma_read_sge);

	hfi1_put_ss(&qp->r_sge);

	if (clr_sends) {
		while (qp->s_last != qp->s_head) {
			struct hfi1_swqe *wqe = get_swqe_ptr(qp, qp->s_last);
			unsigned i;

			for (i = 0; i < wqe->wr.num_sge; i++) {
				struct hfi1_sge *sge = &wqe->sg_list[i];

				hfi1_put_mr(sge->mr);
			}
			if (qp->ibqp.qp_type == IB_QPT_UD ||
			    qp->ibqp.qp_type == IB_QPT_SMI ||
			    qp->ibqp.qp_type == IB_QPT_GSI)
				atomic_dec(&to_iah(wqe->ud_wr.ah)->refcount);
			if (++qp->s_last >= qp->s_size)
				qp->s_last = 0;
		}
		if (qp->s_rdma_mr) {
			hfi1_put_mr(qp->s_rdma_mr);
			qp->s_rdma_mr = NULL;
		}
	}

	if (qp->ibqp.qp_type != IB_QPT_RC)
		return;

	for (n = 0; n < ARRAY_SIZE(qp->s_ack_queue); n++) {
		struct hfi1_ack_entry *e = &qp->s_ack_queue[n];

		if (e->opcode == IB_OPCODE_RC_RDMA_READ_REQUEST &&
		    e->rdma_sge.mr) {
			hfi1_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}
	}
}

/**
 * hfi1_error_qp - put a QP into the error state
 * @qp: the QP to put into the error state
 * @err: the receive completion error to signal if a RWQE is active
 *
 * Flushes both send and receive work queues.
 * Returns true if last WQE event should be generated.
 * The QP r_lock and s_lock should be held and interrupts disabled.
 * If we are already in error state, just return.
 */
int hfi1_error_qp(struct hfi1_qp *qp, enum ib_wc_status err)
{
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	struct ib_wc wc;
	int ret = 0;

	if (qp->state == IB_QPS_ERR || qp->state == IB_QPS_RESET)
		goto bail;

	qp->state = IB_QPS_ERR;

	if (qp->s_flags & (HFI1_S_TIMER | HFI1_S_WAIT_RNR)) {
		qp->s_flags &= ~(HFI1_S_TIMER | HFI1_S_WAIT_RNR);
		del_timer(&qp->s_timer);
	}

	if (qp->s_flags & HFI1_S_ANY_WAIT_SEND)
		qp->s_flags &= ~HFI1_S_ANY_WAIT_SEND;

	write_seqlock(&dev->iowait_lock);
	if (!list_empty(&qp->s_iowait.list) && !(qp->s_flags & HFI1_S_BUSY)) {
		qp->s_flags &= ~HFI1_S_ANY_WAIT_IO;
		list_del_init(&qp->s_iowait.list);
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	}
	write_sequnlock(&dev->iowait_lock);

	if (!(qp->s_flags & HFI1_S_BUSY)) {
		qp->s_hdrwords = 0;
		if (qp->s_rdma_mr) {
			hfi1_put_mr(qp->s_rdma_mr);
			qp->s_rdma_mr = NULL;
		}
		flush_tx_list(qp);
	}

	/* Schedule the sending tasklet to drain the send work queue. */
	if (qp->s_last != qp->s_head)
		hfi1_schedule_send(qp);

	clear_mr_refs(qp, 0);

	memset(&wc, 0, sizeof(wc));
	wc.qp = &qp->ibqp;
	wc.opcode = IB_WC_RECV;

	if (test_and_clear_bit(HFI1_R_WRID_VALID, &qp->r_aflags)) {
		wc.wr_id = qp->r_wr_id;
		wc.status = err;
		hfi1_cq_enter(to_icq(qp->ibqp.recv_cq), &wc, 1);
	}
	wc.status = IB_WC_WR_FLUSH_ERR;

	if (qp->r_rq.wq) {
		struct hfi1_rwq *wq;
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
		while (tail != head) {
			wc.wr_id = get_rwqe_ptr(&qp->r_rq, tail)->wr_id;
			if (++tail >= qp->r_rq.size)
				tail = 0;
			hfi1_cq_enter(to_icq(qp->ibqp.recv_cq), &wc, 1);
		}
		wq->tail = tail;

		spin_unlock(&qp->r_rq.lock);
	} else if (qp->ibqp.event_handler)
		ret = 1;

bail:
	return ret;
}

static void flush_tx_list(struct hfi1_qp *qp)
{
	while (!list_empty(&qp->s_iowait.tx_head)) {
		struct sdma_txreq *tx;

		tx = list_first_entry(
			&qp->s_iowait.tx_head,
			struct sdma_txreq,
			list);
		list_del_init(&tx->list);
		hfi1_put_txreq(
			container_of(tx, struct verbs_txreq, txreq));
	}
}

static void flush_iowait(struct hfi1_qp *qp)
{
	struct hfi1_ibdev *dev = to_idev(qp->ibqp.device);
	unsigned long flags;

	write_seqlock_irqsave(&dev->iowait_lock, flags);
	if (!list_empty(&qp->s_iowait.list)) {
		list_del_init(&qp->s_iowait.list);
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	}
	write_sequnlock_irqrestore(&dev->iowait_lock, flags);
}

static inline int opa_mtu_enum_to_int(int mtu)
{
	switch (mtu) {
	case OPA_MTU_8192:  return 8192;
	case OPA_MTU_10240: return 10240;
	default:            return -1;
	}
}

/**
 * This function is what we would push to the core layer if we wanted to be a
 * "first class citizen".  Instead we hide this here and rely on Verbs ULPs
 * to blindly pass the MTU enum value from the PathRecord to us.
 *
 * The actual flag used to determine "8k MTU" will change and is currently
 * unknown.
 */
static inline int verbs_mtu_enum_to_int(struct ib_device *dev, enum ib_mtu mtu)
{
	int val = opa_mtu_enum_to_int((int)mtu);

	if (val > 0)
		return val;
	return ib_mtu_enum_to_int(mtu);
}


/**
 * hfi1_modify_qp - modify the attributes of a queue pair
 * @ibqp: the queue pair who's attributes we're modifying
 * @attr: the new attributes
 * @attr_mask: the mask of attributes to modify
 * @udata: user data for libibverbs.so
 *
 * Returns 0 on success, otherwise returns an errno.
 */
int hfi1_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		   int attr_mask, struct ib_udata *udata)
{
	struct hfi1_ibdev *dev = to_idev(ibqp->device);
	struct hfi1_qp *qp = to_iqp(ibqp);
	enum ib_qp_state cur_state, new_state;
	struct ib_event ev;
	int lastwqe = 0;
	int mig = 0;
	int ret;
	u32 pmtu = 0; /* for gcc warning only */
	struct hfi1_devdata *dd;

	spin_lock_irq(&qp->r_lock);
	spin_lock(&qp->s_lock);

	cur_state = attr_mask & IB_QP_CUR_STATE ?
		attr->cur_qp_state : qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
				attr_mask, IB_LINK_LAYER_UNSPECIFIED))
		goto inval;

	if (attr_mask & IB_QP_AV) {
		if (attr->ah_attr.dlid >= HFI1_MULTICAST_LID_BASE)
			goto inval;
		if (hfi1_check_ah(qp->ibqp.device, &attr->ah_attr))
			goto inval;
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		if (attr->alt_ah_attr.dlid >= HFI1_MULTICAST_LID_BASE)
			goto inval;
		if (hfi1_check_ah(qp->ibqp.device, &attr->alt_ah_attr))
			goto inval;
		if (attr->alt_pkey_index >= hfi1_get_npkeys(dd_from_dev(dev)))
			goto inval;
	}

	if (attr_mask & IB_QP_PKEY_INDEX)
		if (attr->pkey_index >= hfi1_get_npkeys(dd_from_dev(dev)))
			goto inval;

	if (attr_mask & IB_QP_MIN_RNR_TIMER)
		if (attr->min_rnr_timer > 31)
			goto inval;

	if (attr_mask & IB_QP_PORT)
		if (qp->ibqp.qp_type == IB_QPT_SMI ||
		    qp->ibqp.qp_type == IB_QPT_GSI ||
		    attr->port_num == 0 ||
		    attr->port_num > ibqp->device->phys_port_cnt)
			goto inval;

	if (attr_mask & IB_QP_DEST_QPN)
		if (attr->dest_qp_num > HFI1_QPN_MASK)
			goto inval;

	if (attr_mask & IB_QP_RETRY_CNT)
		if (attr->retry_cnt > 7)
			goto inval;

	if (attr_mask & IB_QP_RNR_RETRY)
		if (attr->rnr_retry > 7)
			goto inval;

	/*
	 * Don't allow invalid path_mtu values.  OK to set greater
	 * than the active mtu (or even the max_cap, if we have tuned
	 * that to a small mtu.  We'll set qp->path_mtu
	 * to the lesser of requested attribute mtu and active,
	 * for packetizing messages.
	 * Note that the QP port has to be set in INIT and MTU in RTR.
	 */
	if (attr_mask & IB_QP_PATH_MTU) {
		int mtu, pidx = qp->port_num - 1;

		dd = dd_from_dev(dev);
		mtu = verbs_mtu_enum_to_int(ibqp->device, attr->path_mtu);
		if (mtu == -1)
			goto inval;

		if (mtu > dd->pport[pidx].ibmtu)
			pmtu = mtu_to_enum(dd->pport[pidx].ibmtu, IB_MTU_2048);
		else
			pmtu = attr->path_mtu;
	}

	if (attr_mask & IB_QP_PATH_MIG_STATE) {
		if (attr->path_mig_state == IB_MIG_REARM) {
			if (qp->s_mig_state == IB_MIG_ARMED)
				goto inval;
			if (new_state != IB_QPS_RTS)
				goto inval;
		} else if (attr->path_mig_state == IB_MIG_MIGRATED) {
			if (qp->s_mig_state == IB_MIG_REARM)
				goto inval;
			if (new_state != IB_QPS_RTS && new_state != IB_QPS_SQD)
				goto inval;
			if (qp->s_mig_state == IB_MIG_ARMED)
				mig = 1;
		} else
			goto inval;
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		if (attr->max_dest_rd_atomic > HFI1_MAX_RDMA_ATOMIC)
			goto inval;

	switch (new_state) {
	case IB_QPS_RESET:
		if (qp->state != IB_QPS_RESET) {
			qp->state = IB_QPS_RESET;
			flush_iowait(qp);
			qp->s_flags &= ~(HFI1_S_TIMER | HFI1_S_ANY_WAIT);
			spin_unlock(&qp->s_lock);
			spin_unlock_irq(&qp->r_lock);
			/* Stop the sending work queue and retry timer */
			cancel_work_sync(&qp->s_iowait.iowork);
			del_timer_sync(&qp->s_timer);
			iowait_sdma_drain(&qp->s_iowait);
			flush_tx_list(qp);
			remove_qp(dev, qp);
			wait_event(qp->wait, !atomic_read(&qp->refcount));
			spin_lock_irq(&qp->r_lock);
			spin_lock(&qp->s_lock);
			clear_mr_refs(qp, 1);
			clear_ahg(qp);
			reset_qp(qp, ibqp->qp_type);
		}
		break;

	case IB_QPS_RTR:
		/* Allow event to re-trigger if QP set to RTR more than once */
		qp->r_flags &= ~HFI1_R_COMM_EST;
		qp->state = new_state;
		break;

	case IB_QPS_SQD:
		qp->s_draining = qp->s_last != qp->s_cur;
		qp->state = new_state;
		break;

	case IB_QPS_SQE:
		if (qp->ibqp.qp_type == IB_QPT_RC)
			goto inval;
		qp->state = new_state;
		break;

	case IB_QPS_ERR:
		lastwqe = hfi1_error_qp(qp, IB_WC_WR_FLUSH_ERR);
		break;

	default:
		qp->state = new_state;
		break;
	}

	if (attr_mask & IB_QP_PKEY_INDEX)
		qp->s_pkey_index = attr->pkey_index;

	if (attr_mask & IB_QP_PORT)
		qp->port_num = attr->port_num;

	if (attr_mask & IB_QP_DEST_QPN)
		qp->remote_qpn = attr->dest_qp_num;

	if (attr_mask & IB_QP_SQ_PSN) {
		qp->s_next_psn = attr->sq_psn & PSN_MODIFY_MASK;
		qp->s_psn = qp->s_next_psn;
		qp->s_sending_psn = qp->s_next_psn;
		qp->s_last_psn = qp->s_next_psn - 1;
		qp->s_sending_hpsn = qp->s_last_psn;
	}

	if (attr_mask & IB_QP_RQ_PSN)
		qp->r_psn = attr->rq_psn & PSN_MODIFY_MASK;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->qp_access_flags = attr->qp_access_flags;

	if (attr_mask & IB_QP_AV) {
		qp->remote_ah_attr = attr->ah_attr;
		qp->s_srate = attr->ah_attr.static_rate;
		qp->srate_mbps = ib_rate_to_mbps(qp->s_srate);
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		qp->alt_ah_attr = attr->alt_ah_attr;
		qp->s_alt_pkey_index = attr->alt_pkey_index;
	}

	if (attr_mask & IB_QP_PATH_MIG_STATE) {
		qp->s_mig_state = attr->path_mig_state;
		if (mig) {
			qp->remote_ah_attr = qp->alt_ah_attr;
			qp->port_num = qp->alt_ah_attr.port_num;
			qp->s_pkey_index = qp->s_alt_pkey_index;
			qp->s_flags |= HFI1_S_AHG_CLEAR;
		}
	}

	if (attr_mask & IB_QP_PATH_MTU) {
		struct hfi1_ibport *ibp;
		u8 sc, vl;
		u32 mtu;

		dd = dd_from_dev(dev);
		ibp = &dd->pport[qp->port_num - 1].ibport_data;

		sc = ibp->sl_to_sc[qp->remote_ah_attr.sl];
		vl = sc_to_vlt(dd, sc);

		mtu = verbs_mtu_enum_to_int(ibqp->device, pmtu);
		if (vl < PER_VL_SEND_CONTEXTS)
			mtu = min_t(u32, mtu, dd->vld[vl].mtu);
		pmtu = mtu_to_enum(mtu, OPA_MTU_8192);

		qp->path_mtu = pmtu;
		qp->pmtu = mtu;
	}

	if (attr_mask & IB_QP_RETRY_CNT) {
		qp->s_retry_cnt = attr->retry_cnt;
		qp->s_retry = attr->retry_cnt;
	}

	if (attr_mask & IB_QP_RNR_RETRY) {
		qp->s_rnr_retry_cnt = attr->rnr_retry;
		qp->s_rnr_retry = attr->rnr_retry;
	}

	if (attr_mask & IB_QP_MIN_RNR_TIMER)
		qp->r_min_rnr_timer = attr->min_rnr_timer;

	if (attr_mask & IB_QP_TIMEOUT) {
		qp->timeout = attr->timeout;
		qp->timeout_jiffies =
			usecs_to_jiffies((4096UL * (1UL << qp->timeout)) /
				1000UL);
	}

	if (attr_mask & IB_QP_QKEY)
		qp->qkey = attr->qkey;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		qp->r_max_rd_atomic = attr->max_dest_rd_atomic;

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC)
		qp->s_max_rd_atomic = attr->max_rd_atomic;

	spin_unlock(&qp->s_lock);
	spin_unlock_irq(&qp->r_lock);

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT)
		insert_qp(dev, qp);

	if (lastwqe) {
		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_QP_LAST_WQE_REACHED;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
	if (mig) {
		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_PATH_MIG;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
	ret = 0;
	goto bail;

inval:
	spin_unlock(&qp->s_lock);
	spin_unlock_irq(&qp->r_lock);
	ret = -EINVAL;

bail:
	return ret;
}

int hfi1_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		  int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct hfi1_qp *qp = to_iqp(ibqp);

	attr->qp_state = qp->state;
	attr->cur_qp_state = attr->qp_state;
	attr->path_mtu = qp->path_mtu;
	attr->path_mig_state = qp->s_mig_state;
	attr->qkey = qp->qkey;
	attr->rq_psn = mask_psn(qp->r_psn);
	attr->sq_psn = mask_psn(qp->s_next_psn);
	attr->dest_qp_num = qp->remote_qpn;
	attr->qp_access_flags = qp->qp_access_flags;
	attr->cap.max_send_wr = qp->s_size - 1;
	attr->cap.max_recv_wr = qp->ibqp.srq ? 0 : qp->r_rq.size - 1;
	attr->cap.max_send_sge = qp->s_max_sge;
	attr->cap.max_recv_sge = qp->r_rq.max_sge;
	attr->cap.max_inline_data = 0;
	attr->ah_attr = qp->remote_ah_attr;
	attr->alt_ah_attr = qp->alt_ah_attr;
	attr->pkey_index = qp->s_pkey_index;
	attr->alt_pkey_index = qp->s_alt_pkey_index;
	attr->en_sqd_async_notify = 0;
	attr->sq_draining = qp->s_draining;
	attr->max_rd_atomic = qp->s_max_rd_atomic;
	attr->max_dest_rd_atomic = qp->r_max_rd_atomic;
	attr->min_rnr_timer = qp->r_min_rnr_timer;
	attr->port_num = qp->port_num;
	attr->timeout = qp->timeout;
	attr->retry_cnt = qp->s_retry_cnt;
	attr->rnr_retry = qp->s_rnr_retry_cnt;
	attr->alt_port_num = qp->alt_ah_attr.port_num;
	attr->alt_timeout = qp->alt_timeout;

	init_attr->event_handler = qp->ibqp.event_handler;
	init_attr->qp_context = qp->ibqp.qp_context;
	init_attr->send_cq = qp->ibqp.send_cq;
	init_attr->recv_cq = qp->ibqp.recv_cq;
	init_attr->srq = qp->ibqp.srq;
	init_attr->cap = attr->cap;
	if (qp->s_flags & HFI1_S_SIGNAL_REQ_WR)
		init_attr->sq_sig_type = IB_SIGNAL_REQ_WR;
	else
		init_attr->sq_sig_type = IB_SIGNAL_ALL_WR;
	init_attr->qp_type = qp->ibqp.qp_type;
	init_attr->port_num = qp->port_num;
	return 0;
}

/**
 * hfi1_compute_aeth - compute the AETH (syndrome + MSN)
 * @qp: the queue pair to compute the AETH for
 *
 * Returns the AETH.
 */
__be32 hfi1_compute_aeth(struct hfi1_qp *qp)
{
	u32 aeth = qp->r_msn & HFI1_MSN_MASK;

	if (qp->ibqp.srq) {
		/*
		 * Shared receive queues don't generate credits.
		 * Set the credit field to the invalid value.
		 */
		aeth |= HFI1_AETH_CREDIT_INVAL << HFI1_AETH_CREDIT_SHIFT;
	} else {
		u32 min, max, x;
		u32 credits;
		struct hfi1_rwq *wq = qp->r_rq.wq;
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
		 * There is a small chance that the pair of reads are
		 * not atomic, which is OK, since the fuzziness is
		 * resolved as further ACKs go out.
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
		aeth |= x << HFI1_AETH_CREDIT_SHIFT;
	}
	return cpu_to_be32(aeth);
}

/**
 * hfi1_create_qp - create a queue pair for a device
 * @ibpd: the protection domain who's device we create the queue pair for
 * @init_attr: the attributes of the queue pair
 * @udata: user data for libibverbs.so
 *
 * Returns the queue pair on success, otherwise returns an errno.
 *
 * Called by the ib_create_qp() core verbs function.
 */
struct ib_qp *hfi1_create_qp(struct ib_pd *ibpd,
			     struct ib_qp_init_attr *init_attr,
			     struct ib_udata *udata)
{
	struct hfi1_qp *qp;
	int err;
	struct hfi1_swqe *swq = NULL;
	struct hfi1_ibdev *dev;
	struct hfi1_devdata *dd;
	size_t sz;
	size_t sg_list_sz;
	struct ib_qp *ret;

	if (init_attr->cap.max_send_sge > hfi1_max_sges ||
	    init_attr->cap.max_send_wr > hfi1_max_qp_wrs ||
	    init_attr->create_flags) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	/* Check receive queue parameters if no SRQ is specified. */
	if (!init_attr->srq) {
		if (init_attr->cap.max_recv_sge > hfi1_max_sges ||
		    init_attr->cap.max_recv_wr > hfi1_max_qp_wrs) {
			ret = ERR_PTR(-EINVAL);
			goto bail;
		}
		if (init_attr->cap.max_send_sge +
		    init_attr->cap.max_send_wr +
		    init_attr->cap.max_recv_sge +
		    init_attr->cap.max_recv_wr == 0) {
			ret = ERR_PTR(-EINVAL);
			goto bail;
		}
	}

	switch (init_attr->qp_type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		if (init_attr->port_num == 0 ||
		    init_attr->port_num > ibpd->device->phys_port_cnt) {
			ret = ERR_PTR(-EINVAL);
			goto bail;
		}
	case IB_QPT_UC:
	case IB_QPT_RC:
	case IB_QPT_UD:
		sz = sizeof(struct hfi1_sge) *
			init_attr->cap.max_send_sge +
			sizeof(struct hfi1_swqe);
		swq = vmalloc((init_attr->cap.max_send_wr + 1) * sz);
		if (swq == NULL) {
			ret = ERR_PTR(-ENOMEM);
			goto bail;
		}
		sz = sizeof(*qp);
		sg_list_sz = 0;
		if (init_attr->srq) {
			struct hfi1_srq *srq = to_isrq(init_attr->srq);

			if (srq->rq.max_sge > 1)
				sg_list_sz = sizeof(*qp->r_sg_list) *
					(srq->rq.max_sge - 1);
		} else if (init_attr->cap.max_recv_sge > 1)
			sg_list_sz = sizeof(*qp->r_sg_list) *
				(init_attr->cap.max_recv_sge - 1);
		qp = kzalloc(sz + sg_list_sz, GFP_KERNEL);
		if (!qp) {
			ret = ERR_PTR(-ENOMEM);
			goto bail_swq;
		}
		RCU_INIT_POINTER(qp->next, NULL);
		qp->s_hdr = kzalloc(sizeof(*qp->s_hdr), GFP_KERNEL);
		if (!qp->s_hdr) {
			ret = ERR_PTR(-ENOMEM);
			goto bail_qp;
		}
		qp->timeout_jiffies =
			usecs_to_jiffies((4096UL * (1UL << qp->timeout)) /
				1000UL);
		if (init_attr->srq)
			sz = 0;
		else {
			qp->r_rq.size = init_attr->cap.max_recv_wr + 1;
			qp->r_rq.max_sge = init_attr->cap.max_recv_sge;
			sz = (sizeof(struct ib_sge) * qp->r_rq.max_sge) +
				sizeof(struct hfi1_rwqe);
			qp->r_rq.wq = vmalloc_user(sizeof(struct hfi1_rwq) +
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
		spin_lock_init(&qp->r_lock);
		spin_lock_init(&qp->s_lock);
		spin_lock_init(&qp->r_rq.lock);
		atomic_set(&qp->refcount, 0);
		init_waitqueue_head(&qp->wait);
		init_timer(&qp->s_timer);
		qp->s_timer.data = (unsigned long)qp;
		INIT_LIST_HEAD(&qp->rspwait);
		qp->state = IB_QPS_RESET;
		qp->s_wq = swq;
		qp->s_size = init_attr->cap.max_send_wr + 1;
		qp->s_max_sge = init_attr->cap.max_send_sge;
		if (init_attr->sq_sig_type == IB_SIGNAL_REQ_WR)
			qp->s_flags = HFI1_S_SIGNAL_REQ_WR;
		dev = to_idev(ibpd->device);
		dd = dd_from_dev(dev);
		err = alloc_qpn(dd, &dev->qp_dev->qpn_table, init_attr->qp_type,
				init_attr->port_num);
		if (err < 0) {
			ret = ERR_PTR(err);
			vfree(qp->r_rq.wq);
			goto bail_qp;
		}
		qp->ibqp.qp_num = err;
		qp->port_num = init_attr->port_num;
		reset_qp(qp, init_attr->qp_type);

		break;

	default:
		/* Don't support raw QPs */
		ret = ERR_PTR(-ENOSYS);
		goto bail;
	}

	init_attr->cap.max_inline_data = 0;

	/*
	 * Return the address of the RWQ as the offset to mmap.
	 * See hfi1_mmap() for details.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		if (!qp->r_rq.wq) {
			__u64 offset = 0;

			err = ib_copy_to_udata(udata, &offset,
					       sizeof(offset));
			if (err) {
				ret = ERR_PTR(err);
				goto bail_ip;
			}
		} else {
			u32 s = sizeof(struct hfi1_rwq) + qp->r_rq.size * sz;

			qp->ip = hfi1_create_mmap_info(dev, s,
						      ibpd->uobject->context,
						      qp->r_rq.wq);
			if (!qp->ip) {
				ret = ERR_PTR(-ENOMEM);
				goto bail_ip;
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
	if (dev->n_qps_allocated == hfi1_max_qps) {
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

	/*
	 * We have our QP and its good, now keep track of what types of opcodes
	 * can be processed on this QP. We do this by keeping track of what the
	 * 3 high order bits of the opcode are.
	 */
	switch (init_attr->qp_type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
	case IB_QPT_UD:
		qp->allowed_ops = IB_OPCODE_UD_SEND_ONLY & OPCODE_QP_MASK;
		break;
	case IB_QPT_RC:
		qp->allowed_ops = IB_OPCODE_RC_SEND_ONLY & OPCODE_QP_MASK;
		break;
	case IB_QPT_UC:
		qp->allowed_ops = IB_OPCODE_UC_SEND_ONLY & OPCODE_QP_MASK;
		break;
	default:
		ret = ERR_PTR(-EINVAL);
		goto bail_ip;
	}

	goto bail;

bail_ip:
	if (qp->ip)
		kref_put(&qp->ip->ref, hfi1_release_mmap_info);
	else
		vfree(qp->r_rq.wq);
	free_qpn(&dev->qp_dev->qpn_table, qp->ibqp.qp_num);
bail_qp:
	kfree(qp->s_hdr);
	kfree(qp);
bail_swq:
	vfree(swq);
bail:
	return ret;
}

/**
 * hfi1_destroy_qp - destroy a queue pair
 * @ibqp: the queue pair to destroy
 *
 * Returns 0 on success.
 *
 * Note that this can be called while the QP is actively sending or
 * receiving!
 */
int hfi1_destroy_qp(struct ib_qp *ibqp)
{
	struct hfi1_qp *qp = to_iqp(ibqp);
	struct hfi1_ibdev *dev = to_idev(ibqp->device);

	/* Make sure HW and driver activity is stopped. */
	spin_lock_irq(&qp->r_lock);
	spin_lock(&qp->s_lock);
	if (qp->state != IB_QPS_RESET) {
		qp->state = IB_QPS_RESET;
		flush_iowait(qp);
		qp->s_flags &= ~(HFI1_S_TIMER | HFI1_S_ANY_WAIT);
		spin_unlock(&qp->s_lock);
		spin_unlock_irq(&qp->r_lock);
		cancel_work_sync(&qp->s_iowait.iowork);
		del_timer_sync(&qp->s_timer);
		iowait_sdma_drain(&qp->s_iowait);
		flush_tx_list(qp);
		remove_qp(dev, qp);
		wait_event(qp->wait, !atomic_read(&qp->refcount));
		spin_lock_irq(&qp->r_lock);
		spin_lock(&qp->s_lock);
		clear_mr_refs(qp, 1);
		clear_ahg(qp);
	}
	spin_unlock(&qp->s_lock);
	spin_unlock_irq(&qp->r_lock);

	/* all user's cleaned up, mark it available */
	free_qpn(&dev->qp_dev->qpn_table, qp->ibqp.qp_num);
	spin_lock(&dev->n_qps_lock);
	dev->n_qps_allocated--;
	spin_unlock(&dev->n_qps_lock);

	if (qp->ip)
		kref_put(&qp->ip->ref, hfi1_release_mmap_info);
	else
		vfree(qp->r_rq.wq);
	vfree(qp->s_wq);
	kfree(qp->s_hdr);
	kfree(qp);
	return 0;
}

/**
 * init_qpn_table - initialize the QP number table for a device
 * @qpt: the QPN table
 */
static int init_qpn_table(struct hfi1_devdata *dd, struct hfi1_qpn_table *qpt)
{
	u32 offset, qpn, i;
	struct qpn_map *map;
	int ret = 0;

	spin_lock_init(&qpt->lock);

	qpt->last = 0;
	qpt->incr = 1 << dd->qos_shift;

	/* insure we don't assign QPs from KDETH 64K window */
	qpn = kdeth_qp << 16;
	qpt->nmaps = qpn / BITS_PER_PAGE;
	/* This should always be zero */
	offset = qpn & BITS_PER_PAGE_MASK;
	map = &qpt->map[qpt->nmaps];
	dd_dev_info(dd, "Reserving QPNs for KDETH window from 0x%x to 0x%x\n",
		qpn, qpn + 65535);
	for (i = 0; i < 65536; i++) {
		if (!map->page) {
			get_map_page(qpt, map);
			if (!map->page) {
				ret = -ENOMEM;
				break;
			}
		}
		set_bit(offset, map->page);
		offset++;
		if (offset == BITS_PER_PAGE) {
			/* next page */
			qpt->nmaps++;
			map++;
			offset = 0;
		}
	}
	return ret;
}

/**
 * free_qpn_table - free the QP number table for a device
 * @qpt: the QPN table
 */
static void free_qpn_table(struct hfi1_qpn_table *qpt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qpt->map); i++)
		free_page((unsigned long) qpt->map[i].page);
}

/**
 * hfi1_get_credit - flush the send work queue of a QP
 * @qp: the qp who's send work queue to flush
 * @aeth: the Acknowledge Extended Transport Header
 *
 * The QP s_lock should be held.
 */
void hfi1_get_credit(struct hfi1_qp *qp, u32 aeth)
{
	u32 credit = (aeth >> HFI1_AETH_CREDIT_SHIFT) & HFI1_AETH_CREDIT_MASK;

	/*
	 * If the credit is invalid, we can send
	 * as many packets as we like.  Otherwise, we have to
	 * honor the credit field.
	 */
	if (credit == HFI1_AETH_CREDIT_INVAL) {
		if (!(qp->s_flags & HFI1_S_UNLIMITED_CREDIT)) {
			qp->s_flags |= HFI1_S_UNLIMITED_CREDIT;
			if (qp->s_flags & HFI1_S_WAIT_SSN_CREDIT) {
				qp->s_flags &= ~HFI1_S_WAIT_SSN_CREDIT;
				hfi1_schedule_send(qp);
			}
		}
	} else if (!(qp->s_flags & HFI1_S_UNLIMITED_CREDIT)) {
		/* Compute new LSN (i.e., MSN + credit) */
		credit = (aeth + credit_table[credit]) & HFI1_MSN_MASK;
		if (cmp_msn(credit, qp->s_lsn) > 0) {
			qp->s_lsn = credit;
			if (qp->s_flags & HFI1_S_WAIT_SSN_CREDIT) {
				qp->s_flags &= ~HFI1_S_WAIT_SSN_CREDIT;
				hfi1_schedule_send(qp);
			}
		}
	}
}

void hfi1_qp_wakeup(struct hfi1_qp *qp, u32 flag)
{
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (qp->s_flags & flag) {
		qp->s_flags &= ~flag;
		trace_hfi1_qpwakeup(qp, flag);
		hfi1_schedule_send(qp);
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
	/* Notify hfi1_destroy_qp() if it is waiting. */
	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);
}

static int iowait_sleep(
	struct sdma_engine *sde,
	struct iowait *wait,
	struct sdma_txreq *stx,
	unsigned seq)
{
	struct verbs_txreq *tx = container_of(stx, struct verbs_txreq, txreq);
	struct hfi1_qp *qp;
	unsigned long flags;
	int ret = 0;
	struct hfi1_ibdev *dev;

	qp = tx->qp;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_hfi1_state_ops[qp->state] & HFI1_PROCESS_RECV_OK) {

		/*
		 * If we couldn't queue the DMA request, save the info
		 * and try again later rather than destroying the
		 * buffer and undoing the side effects of the copy.
		 */
		/* Make a common routine? */
		dev = &sde->dd->verbs_dev;
		list_add_tail(&stx->list, &wait->tx_head);
		write_seqlock(&dev->iowait_lock);
		if (sdma_progress(sde, seq, stx))
			goto eagain;
		if (list_empty(&qp->s_iowait.list)) {
			struct hfi1_ibport *ibp =
				to_iport(qp->ibqp.device, qp->port_num);

			ibp->n_dmawait++;
			qp->s_flags |= HFI1_S_WAIT_DMA_DESC;
			list_add_tail(&qp->s_iowait.list, &sde->dmawait);
			trace_hfi1_qpsleep(qp, HFI1_S_WAIT_DMA_DESC);
			atomic_inc(&qp->refcount);
		}
		write_sequnlock(&dev->iowait_lock);
		qp->s_flags &= ~HFI1_S_BUSY;
		spin_unlock_irqrestore(&qp->s_lock, flags);
		ret = -EBUSY;
	} else {
		spin_unlock_irqrestore(&qp->s_lock, flags);
		hfi1_put_txreq(tx);
	}
	return ret;
eagain:
	write_sequnlock(&dev->iowait_lock);
	spin_unlock_irqrestore(&qp->s_lock, flags);
	list_del_init(&stx->list);
	return -EAGAIN;
}

static void iowait_wakeup(struct iowait *wait, int reason)
{
	struct hfi1_qp *qp = container_of(wait, struct hfi1_qp, s_iowait);

	WARN_ON(reason != SDMA_AVAIL_REASON);
	hfi1_qp_wakeup(qp, HFI1_S_WAIT_DMA_DESC);
}

int hfi1_qp_init(struct hfi1_ibdev *dev)
{
	struct hfi1_devdata *dd = dd_from_dev(dev);
	int i;
	int ret = -ENOMEM;

	/* allocate parent object */
	dev->qp_dev = kzalloc(sizeof(*dev->qp_dev), GFP_KERNEL);
	if (!dev->qp_dev)
		goto nomem;
	/* allocate hash table */
	dev->qp_dev->qp_table_size = hfi1_qp_table_size;
	dev->qp_dev->qp_table_bits = ilog2(hfi1_qp_table_size);
	dev->qp_dev->qp_table =
		kmalloc(dev->qp_dev->qp_table_size *
				sizeof(*dev->qp_dev->qp_table),
			GFP_KERNEL);
	if (!dev->qp_dev->qp_table)
		goto nomem;
	for (i = 0; i < dev->qp_dev->qp_table_size; i++)
		RCU_INIT_POINTER(dev->qp_dev->qp_table[i], NULL);
	spin_lock_init(&dev->qp_dev->qpt_lock);
	/* initialize qpn map */
	ret = init_qpn_table(dd, &dev->qp_dev->qpn_table);
	if (ret)
		goto nomem;
	return ret;
nomem:
	if (dev->qp_dev) {
		kfree(dev->qp_dev->qp_table);
		free_qpn_table(&dev->qp_dev->qpn_table);
		kfree(dev->qp_dev);
	}
	return ret;
}

void hfi1_qp_exit(struct hfi1_ibdev *dev)
{
	struct hfi1_devdata *dd = dd_from_dev(dev);
	u32 qps_inuse;

	qps_inuse = free_all_qps(dd);
	if (qps_inuse)
		dd_dev_err(dd, "QP memory leak! %u still in use\n",
			   qps_inuse);
	if (dev->qp_dev) {
		kfree(dev->qp_dev->qp_table);
		free_qpn_table(&dev->qp_dev->qpn_table);
		kfree(dev->qp_dev);
	}
}

/**
 *
 * qp_to_sdma_engine - map a qp to a send engine
 * @qp: the QP
 * @sc5: the 5 bit sc
 *
 * Return:
 * A send engine for the qp or NULL for SMI type qp.
 */
struct sdma_engine *qp_to_sdma_engine(struct hfi1_qp *qp, u8 sc5)
{
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct sdma_engine *sde;

	if (!(dd->flags & HFI1_HAS_SEND_DMA))
		return NULL;
	switch (qp->ibqp.qp_type) {
	case IB_QPT_UC:
	case IB_QPT_RC:
		break;
	case IB_QPT_SMI:
		return NULL;
	default:
		break;
	}
	sde = sdma_select_engine_sc(dd, qp->ibqp.qp_num >> dd->qos_shift, sc5);
	return sde;
}

struct qp_iter {
	struct hfi1_ibdev *dev;
	struct hfi1_qp *qp;
	int specials;
	int n;
};

struct qp_iter *qp_iter_init(struct hfi1_ibdev *dev)
{
	struct qp_iter *iter;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return NULL;

	iter->dev = dev;
	iter->specials = dev->ibdev.phys_port_cnt * 2;
	if (qp_iter_next(iter)) {
		kfree(iter);
		return NULL;
	}

	return iter;
}

int qp_iter_next(struct qp_iter *iter)
{
	struct hfi1_ibdev *dev = iter->dev;
	int n = iter->n;
	int ret = 1;
	struct hfi1_qp *pqp = iter->qp;
	struct hfi1_qp *qp;

	/*
	 * The approach is to consider the special qps
	 * as an additional table entries before the
	 * real hash table.  Since the qp code sets
	 * the qp->next hash link to NULL, this works just fine.
	 *
	 * iter->specials is 2 * # ports
	 *
	 * n = 0..iter->specials is the special qp indices
	 *
	 * n = iter->specials..dev->qp_dev->qp_table_size+iter->specials are
	 * the potential hash bucket entries
	 *
	 */
	for (; n <  dev->qp_dev->qp_table_size + iter->specials; n++) {
		if (pqp) {
			qp = rcu_dereference(pqp->next);
		} else {
			if (n < iter->specials) {
				struct hfi1_pportdata *ppd;
				struct hfi1_ibport *ibp;
				int pidx;

				pidx = n % dev->ibdev.phys_port_cnt;
				ppd = &dd_from_dev(dev)->pport[pidx];
				ibp = &ppd->ibport_data;

				if (!(n & 1))
					qp = rcu_dereference(ibp->qp[0]);
				else
					qp = rcu_dereference(ibp->qp[1]);
			} else {
				qp = rcu_dereference(
					dev->qp_dev->qp_table[
						(n - iter->specials)]);
			}
		}
		pqp = qp;
		if (qp) {
			iter->qp = qp;
			iter->n = n;
			return 0;
		}
	}
	return ret;
}

static const char * const qp_type_str[] = {
	"SMI", "GSI", "RC", "UC", "UD",
};

static int qp_idle(struct hfi1_qp *qp)
{
	return
		qp->s_last == qp->s_acked &&
		qp->s_acked == qp->s_cur &&
		qp->s_cur == qp->s_tail &&
		qp->s_tail == qp->s_head;
}

void qp_iter_print(struct seq_file *s, struct qp_iter *iter)
{
	struct hfi1_swqe *wqe;
	struct hfi1_qp *qp = iter->qp;
	struct sdma_engine *sde;

	sde = qp_to_sdma_engine(qp, qp->s_sc);
	wqe = get_swqe_ptr(qp, qp->s_last);
	seq_printf(s,
		   "N %d %s QP%u R %u %s %u %u %u f=%x %u %u %u %u %u PSN %x %x %x %x %x (%u %u %u %u %u %u) QP%u LID %x SL %u MTU %d %u %u %u SDE %p,%u\n",
		   iter->n,
		   qp_idle(qp) ? "I" : "B",
		   qp->ibqp.qp_num,
		   atomic_read(&qp->refcount),
		   qp_type_str[qp->ibqp.qp_type],
		   qp->state,
		   wqe ? wqe->wr.opcode : 0,
		   qp->s_hdrwords,
		   qp->s_flags,
		   atomic_read(&qp->s_iowait.sdma_busy),
		   !list_empty(&qp->s_iowait.list),
		   qp->timeout,
		   wqe ? wqe->ssn : 0,
		   qp->s_lsn,
		   qp->s_last_psn,
		   qp->s_psn, qp->s_next_psn,
		   qp->s_sending_psn, qp->s_sending_hpsn,
		   qp->s_last, qp->s_acked, qp->s_cur,
		   qp->s_tail, qp->s_head, qp->s_size,
		   qp->remote_qpn,
		   qp->remote_ah_attr.dlid,
		   qp->remote_ah_attr.sl,
		   qp->pmtu,
		   qp->s_retry_cnt,
		   qp->timeout,
		   qp->s_rnr_retry_cnt,
		   sde,
		   sde ? sde->this_idx : 0);
}

void qp_comm_est(struct hfi1_qp *qp)
{
	qp->r_flags |= HFI1_R_COMM_EST;
	if (qp->ibqp.event_handler) {
		struct ib_event ev;

		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_COMM_EST;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
}
