/*
 * Copyright (c) 2012, 2013 Intel Corporation.  All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation.  * All rights reserved.
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
#include <linux/jhash.h>
#include <rdma/rdma_vt.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>
#endif

#include "qib.h"

/*
 * mask field which was present in now deleted qib_qpn_table
 * is not present in rvt_qpn_table. Defining the same field
 * as qpt_mask here instead of adding the mask field to
 * rvt_qpn_table.
 */
u16 qpt_mask;

static inline unsigned mk_qpn(struct rvt_qpn_table *qpt,
			      struct rvt_qpn_map *map, unsigned off)
{
	return (map - qpt->map) * RVT_BITS_PER_PAGE + off;
}

static inline unsigned find_next_offset(struct rvt_qpn_table *qpt,
					struct rvt_qpn_map *map, unsigned off,
					unsigned n)
{
	if (qpt_mask) {
		off++;
		if (((off & qpt_mask) >> 1) >= n)
			off = (off | qpt_mask) + 2;
	} else {
		off = find_next_zero_bit(map->page, RVT_BITS_PER_PAGE, off);
	}
	return off;
}

/*
 * Convert the AETH credit code into the number of credits.
 */
static u32 credit_table[31] = {
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

static void get_map_page(struct rvt_qpn_table *qpt, struct rvt_qpn_map *map,
			 gfp_t gfp)
{
	unsigned long page = get_zeroed_page(gfp);

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
int alloc_qpn(struct rvt_dev_info *rdi, struct rvt_qpn_table *qpt,
	      enum ib_qp_type type, u8 port, gfp_t gfp)
{
	u32 i, offset, max_scan, qpn;
	struct rvt_qpn_map *map;
	u32 ret;
	struct qib_ibdev *verbs_dev = container_of(rdi, struct qib_ibdev, rdi);
	struct qib_devdata *dd = container_of(verbs_dev, struct qib_devdata,
					      verbs_dev);

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

	qpn = qpt->last + 2;
	if (qpn >= RVT_QPN_MAX)
		qpn = 2;
	if (qpt_mask && ((qpn & qpt_mask) >> 1) >= dd->n_krcv_queues)
		qpn = (qpn | qpt_mask) + 2;
	offset = qpn & RVT_BITS_PER_PAGE_MASK;
	map = &qpt->map[qpn / RVT_BITS_PER_PAGE];
	max_scan = qpt->nmaps - !offset;
	for (i = 0;;) {
		if (unlikely(!map->page)) {
			get_map_page(qpt, map, gfp);
			if (unlikely(!map->page))
				break;
		}
		do {
			if (!test_and_set_bit(offset, map->page)) {
				qpt->last = qpn;
				ret = qpn;
				goto bail;
			}
			offset = find_next_offset(qpt, map, offset,
				dd->n_krcv_queues);
			qpn = mk_qpn(qpt, map, offset);
			/*
			 * This test differs from alloc_pidmap().
			 * If find_next_offset() does find a zero
			 * bit, we don't need to check for QPN
			 * wrapping around past our starting QPN.
			 * We just need to be sure we don't loop
			 * forever.
			 */
		} while (offset < RVT_BITS_PER_PAGE && qpn < RVT_QPN_MAX);
		/*
		 * In order to keep the number of pages allocated to a
		 * minimum, we scan the all existing pages before increasing
		 * the size of the bitmap table.
		 */
		if (++i > max_scan) {
			if (qpt->nmaps == RVT_QPNMAP_ENTRIES)
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

static void free_qpn(struct rvt_qpn_table *qpt, u32 qpn)
{
	struct rvt_qpn_map *map;

	map = qpt->map + qpn / RVT_BITS_PER_PAGE;
	if (map->page)
		clear_bit(qpn & RVT_BITS_PER_PAGE_MASK, map->page);
}

static inline unsigned qpn_hash(struct qib_ibdev *dev, u32 qpn)
{
	return jhash_1word(qpn, dev->qp_rnd) &
		(dev->rdi.qp_dev->qp_table_size - 1);
}


/*
 * Put the QP into the hash table.
 * The hash table holds a reference to the QP.
 */
static void insert_qp(struct qib_ibdev *dev, struct rvt_qp *qp)
{
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	unsigned long flags;
	unsigned n = qpn_hash(dev, qp->ibqp.qp_num);

	atomic_inc(&qp->refcount);
	spin_lock_irqsave(&dev->rdi.qp_dev->qpt_lock, flags);

	if (qp->ibqp.qp_num == 0)
		rcu_assign_pointer(ibp->rvp.qp[0], qp);
	else if (qp->ibqp.qp_num == 1)
		rcu_assign_pointer(ibp->rvp.qp[1], qp);
	else {
		qp->next = dev->rdi.qp_dev->qp_table[n];
		rcu_assign_pointer(dev->rdi.qp_dev->qp_table[n], qp);
	}

	spin_unlock_irqrestore(&dev->rdi.qp_dev->qpt_lock, flags);
}

/*
 * Remove the QP from the table so it can't be found asynchronously by
 * the receive interrupt routine.
 */
static void remove_qp(struct qib_ibdev *dev, struct rvt_qp *qp)
{
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	unsigned n = qpn_hash(dev, qp->ibqp.qp_num);
	unsigned long flags;
	int removed = 1;
	spinlock_t *qpt_lock_ptr; /* Pointer to make checkpatch happy */

	spin_lock_irqsave(&dev->rdi.qp_dev->qpt_lock, flags);

	qpt_lock_ptr = &dev->rdi.qp_dev->qpt_lock;
	if (rcu_dereference_protected(ibp->rvp.qp[0],
				      lockdep_is_held(qpt_lock_ptr)) == qp) {
		RCU_INIT_POINTER(ibp->rvp.qp[0], NULL);
	} else if (rcu_dereference_protected(ibp->rvp.qp[1],
			lockdep_is_held(&dev->rdi.qp_dev->qpt_lock)) == qp) {
		RCU_INIT_POINTER(ibp->rvp.qp[1], NULL);
	} else {
		struct rvt_qp *q;
		struct rvt_qp __rcu **qpp;

		removed = 0;
		qpp = &dev->rdi.qp_dev->qp_table[n];
		for (; (q = rcu_dereference_protected(*qpp,
				lockdep_is_held(qpt_lock_ptr))) != NULL;
				qpp = &q->next)
			if (q == qp) {
				RCU_INIT_POINTER(*qpp,
					rcu_dereference_protected(qp->next,
					 lockdep_is_held(qpt_lock_ptr)));
				removed = 1;
				break;
			}
	}

	spin_unlock_irqrestore(&dev->rdi.qp_dev->qpt_lock, flags);
	if (removed) {
		synchronize_rcu();
		atomic_dec(&qp->refcount);
	}
}

/**
 * qib_free_all_qps - check for QPs still in use
 */
unsigned qib_free_all_qps(struct rvt_dev_info *rdi)
{
	struct qib_ibdev *verbs_dev = container_of(rdi, struct qib_ibdev, rdi);
	struct qib_devdata *dd = container_of(verbs_dev, struct qib_devdata,
					      verbs_dev);
	unsigned n, qp_inuse = 0;

	for (n = 0; n < dd->num_pports; n++) {
		struct qib_ibport *ibp = &dd->pport[n].ibport_data;

		if (!qib_mcast_tree_empty(ibp))
			qp_inuse++;
		rcu_read_lock();
		if (rcu_dereference(ibp->rvp.qp[0]))
			qp_inuse++;
		if (rcu_dereference(ibp->rvp.qp[1]))
			qp_inuse++;
		rcu_read_unlock();
	}
	return qp_inuse;
}

/**
 * qib_lookup_qpn - return the QP with the given QPN
 * @qpt: the QP table
 * @qpn: the QP number to look up
 *
 * The caller is responsible for decrementing the QP reference count
 * when done.
 */
struct rvt_qp *qib_lookup_qpn(struct qib_ibport *ibp, u32 qpn)
{
	struct rvt_qp *qp = NULL;

	rcu_read_lock();
	if (unlikely(qpn <= 1)) {
		if (qpn == 0)
			qp = rcu_dereference(ibp->rvp.qp[0]);
		else
			qp = rcu_dereference(ibp->rvp.qp[1]);
		if (qp)
			atomic_inc(&qp->refcount);
	} else {
		struct qib_ibdev *dev = &ppd_from_ibp(ibp)->dd->verbs_dev;
		unsigned n = qpn_hash(dev, qpn);

		for (qp = rcu_dereference(dev->rdi.qp_dev->qp_table[n]); qp;
			qp = rcu_dereference(qp->next))
			if (qp->ibqp.qp_num == qpn) {
				atomic_inc(&qp->refcount);
				break;
			}
	}
	rcu_read_unlock();
	return qp;
}

void notify_qp_reset(struct rvt_qp *qp)
{
	struct qib_qp_priv *priv = qp->priv;

	atomic_set(&priv->s_dma_busy, 0);
}

static void clear_mr_refs(struct rvt_qp *qp, int clr_sends)
{
	unsigned n;

	if (test_and_clear_bit(RVT_R_REWIND_SGE, &qp->r_aflags))
		qib_put_ss(&qp->s_rdma_read_sge);

	qib_put_ss(&qp->r_sge);

	if (clr_sends) {
		while (qp->s_last != qp->s_head) {
			struct rvt_swqe *wqe = get_swqe_ptr(qp, qp->s_last);
			unsigned i;

			for (i = 0; i < wqe->wr.num_sge; i++) {
				struct rvt_sge *sge = &wqe->sg_list[i];

				rvt_put_mr(sge->mr);
			}
			if (qp->ibqp.qp_type == IB_QPT_UD ||
			    qp->ibqp.qp_type == IB_QPT_SMI ||
			    qp->ibqp.qp_type == IB_QPT_GSI)
				atomic_dec(
				 &ibah_to_rvtah(wqe->ud_wr.ah)->refcount);
			if (++qp->s_last >= qp->s_size)
				qp->s_last = 0;
		}
		if (qp->s_rdma_mr) {
			rvt_put_mr(qp->s_rdma_mr);
			qp->s_rdma_mr = NULL;
		}
	}

	if (qp->ibqp.qp_type != IB_QPT_RC)
		return;

	for (n = 0; n < ARRAY_SIZE(qp->s_ack_queue); n++) {
		struct rvt_ack_entry *e = &qp->s_ack_queue[n];

		if (e->opcode == IB_OPCODE_RC_RDMA_READ_REQUEST &&
		    e->rdma_sge.mr) {
			rvt_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}
	}
}

/**
 * qib_error_qp - put a QP into the error state
 * @qp: the QP to put into the error state
 * @err: the receive completion error to signal if a RWQE is active
 *
 * Flushes both send and receive work queues.
 * Returns true if last WQE event should be generated.
 * The QP r_lock and s_lock should be held and interrupts disabled.
 * If we are already in error state, just return.
 */
int qib_error_qp(struct rvt_qp *qp, enum ib_wc_status err)
{
	struct qib_qp_priv *priv = qp->priv;
	struct qib_ibdev *dev = to_idev(qp->ibqp.device);
	struct ib_wc wc;
	int ret = 0;

	if (qp->state == IB_QPS_ERR || qp->state == IB_QPS_RESET)
		goto bail;

	qp->state = IB_QPS_ERR;

	if (qp->s_flags & (RVT_S_TIMER | RVT_S_WAIT_RNR)) {
		qp->s_flags &= ~(RVT_S_TIMER | RVT_S_WAIT_RNR);
		del_timer(&qp->s_timer);
	}

	if (qp->s_flags & RVT_S_ANY_WAIT_SEND)
		qp->s_flags &= ~RVT_S_ANY_WAIT_SEND;

	spin_lock(&dev->rdi.pending_lock);
	if (!list_empty(&priv->iowait) && !(qp->s_flags & RVT_S_BUSY)) {
		qp->s_flags &= ~RVT_S_ANY_WAIT_IO;
		list_del_init(&priv->iowait);
	}
	spin_unlock(&dev->rdi.pending_lock);

	if (!(qp->s_flags & RVT_S_BUSY)) {
		qp->s_hdrwords = 0;
		if (qp->s_rdma_mr) {
			rvt_put_mr(qp->s_rdma_mr);
			qp->s_rdma_mr = NULL;
		}
		if (priv->s_tx) {
			qib_put_txreq(priv->s_tx);
			priv->s_tx = NULL;
		}
	}

	/* Schedule the sending tasklet to drain the send work queue. */
	if (qp->s_last != qp->s_head)
		qib_schedule_send(qp);

	clear_mr_refs(qp, 0);

	memset(&wc, 0, sizeof(wc));
	wc.qp = &qp->ibqp;
	wc.opcode = IB_WC_RECV;

	if (test_and_clear_bit(RVT_R_WRID_VALID, &qp->r_aflags)) {
		wc.wr_id = qp->r_wr_id;
		wc.status = err;
		qib_cq_enter(to_icq(qp->ibqp.recv_cq), &wc, 1);
	}
	wc.status = IB_WC_WR_FLUSH_ERR;

	if (qp->r_rq.wq) {
		struct rvt_rwq *wq;
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
			qib_cq_enter(to_icq(qp->ibqp.recv_cq), &wc, 1);
		}
		wq->tail = tail;

		spin_unlock(&qp->r_rq.lock);
	} else if (qp->ibqp.event_handler)
		ret = 1;

bail:
	return ret;
}

/**
 * qib_modify_qp - modify the attributes of a queue pair
 * @ibqp: the queue pair who's attributes we're modifying
 * @attr: the new attributes
 * @attr_mask: the mask of attributes to modify
 * @udata: user data for libibverbs.so
 *
 * Returns 0 on success, otherwise returns an errno.
 */
int qib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		  int attr_mask, struct ib_udata *udata)
{
	struct qib_ibdev *dev = to_idev(ibqp->device);
	struct rvt_qp *qp = to_iqp(ibqp);
	struct qib_qp_priv *priv = qp->priv;
	enum ib_qp_state cur_state, new_state;
	struct ib_event ev;
	int lastwqe = 0;
	int mig = 0;
	int ret;
	u32 pmtu = 0; /* for gcc warning only */

	spin_lock_irq(&qp->r_lock);
	spin_lock(&qp->s_lock);

	cur_state = attr_mask & IB_QP_CUR_STATE ?
		attr->cur_qp_state : qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
				attr_mask, IB_LINK_LAYER_UNSPECIFIED))
		goto inval;

	if (attr_mask & IB_QP_AV) {
		if (attr->ah_attr.dlid >= be16_to_cpu(IB_MULTICAST_LID_BASE))
			goto inval;
		if (rvt_check_ah(qp->ibqp.device, &attr->ah_attr))
			goto inval;
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		if (attr->alt_ah_attr.dlid >=
		    be16_to_cpu(IB_MULTICAST_LID_BASE))
			goto inval;
		if (rvt_check_ah(qp->ibqp.device, &attr->alt_ah_attr))
			goto inval;
		if (attr->alt_pkey_index >= qib_get_npkeys(dd_from_dev(dev)))
			goto inval;
	}

	if (attr_mask & IB_QP_PKEY_INDEX)
		if (attr->pkey_index >= qib_get_npkeys(dd_from_dev(dev)))
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
		if (attr->dest_qp_num > QIB_QPN_MASK)
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
		struct qib_devdata *dd = dd_from_dev(dev);
		int mtu, pidx = qp->port_num - 1;

		mtu = ib_mtu_enum_to_int(attr->path_mtu);
		if (mtu == -1)
			goto inval;
		if (mtu > dd->pport[pidx].ibmtu) {
			switch (dd->pport[pidx].ibmtu) {
			case 4096:
				pmtu = IB_MTU_4096;
				break;
			case 2048:
				pmtu = IB_MTU_2048;
				break;
			case 1024:
				pmtu = IB_MTU_1024;
				break;
			case 512:
				pmtu = IB_MTU_512;
				break;
			case 256:
				pmtu = IB_MTU_256;
				break;
			default:
				pmtu = IB_MTU_2048;
			}
		} else
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
		if (attr->max_dest_rd_atomic > QIB_MAX_RDMA_ATOMIC)
			goto inval;

	switch (new_state) {
	case IB_QPS_RESET:
		if (qp->state != IB_QPS_RESET) {
			qp->state = IB_QPS_RESET;
			spin_lock(&dev->rdi.pending_lock);
			if (!list_empty(&priv->iowait))
				list_del_init(&priv->iowait);
			spin_unlock(&dev->rdi.pending_lock);
			qp->s_flags &= ~(RVT_S_TIMER | RVT_S_ANY_WAIT);
			spin_unlock(&qp->s_lock);
			spin_unlock_irq(&qp->r_lock);
			/* Stop the sending work queue and retry timer */
			cancel_work_sync(&priv->s_work);
			del_timer_sync(&qp->s_timer);
			wait_event(priv->wait_dma,
				   !atomic_read(&priv->s_dma_busy));
			if (priv->s_tx) {
				qib_put_txreq(priv->s_tx);
				priv->s_tx = NULL;
			}
			remove_qp(dev, qp);
			wait_event(qp->wait, !atomic_read(&qp->refcount));
			spin_lock_irq(&qp->r_lock);
			spin_lock(&qp->s_lock);
			clear_mr_refs(qp, 1);
			rvt_reset_qp(&dev->rdi, qp, ibqp->qp_type);
		}
		break;

	case IB_QPS_RTR:
		/* Allow event to retrigger if QP set to RTR more than once */
		qp->r_flags &= ~RVT_R_COMM_EST;
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
		lastwqe = qib_error_qp(qp, IB_WC_WR_FLUSH_ERR);
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
		qp->s_next_psn = attr->sq_psn & QIB_PSN_MASK;
		qp->s_psn = qp->s_next_psn;
		qp->s_sending_psn = qp->s_next_psn;
		qp->s_last_psn = qp->s_next_psn - 1;
		qp->s_sending_hpsn = qp->s_last_psn;
	}

	if (attr_mask & IB_QP_RQ_PSN)
		qp->r_psn = attr->rq_psn & QIB_PSN_MASK;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->qp_access_flags = attr->qp_access_flags;

	if (attr_mask & IB_QP_AV) {
		qp->remote_ah_attr = attr->ah_attr;
		qp->s_srate = attr->ah_attr.static_rate;
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
		}
	}

	if (attr_mask & IB_QP_PATH_MTU) {
		qp->path_mtu = pmtu;
		qp->pmtu = ib_mtu_enum_to_int(pmtu);
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

int qib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		 int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct rvt_qp *qp = to_iqp(ibqp);

	attr->qp_state = qp->state;
	attr->cur_qp_state = attr->qp_state;
	attr->path_mtu = qp->path_mtu;
	attr->path_mig_state = qp->s_mig_state;
	attr->qkey = qp->qkey;
	attr->rq_psn = qp->r_psn & QIB_PSN_MASK;
	attr->sq_psn = qp->s_next_psn & QIB_PSN_MASK;
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
	if (qp->s_flags & RVT_S_SIGNAL_REQ_WR)
		init_attr->sq_sig_type = IB_SIGNAL_REQ_WR;
	else
		init_attr->sq_sig_type = IB_SIGNAL_ALL_WR;
	init_attr->qp_type = qp->ibqp.qp_type;
	init_attr->port_num = qp->port_num;
	return 0;
}

/**
 * qib_compute_aeth - compute the AETH (syndrome + MSN)
 * @qp: the queue pair to compute the AETH for
 *
 * Returns the AETH.
 */
__be32 qib_compute_aeth(struct rvt_qp *qp)
{
	u32 aeth = qp->r_msn & QIB_MSN_MASK;

	if (qp->ibqp.srq) {
		/*
		 * Shared receive queues don't generate credits.
		 * Set the credit field to the invalid value.
		 */
		aeth |= QIB_AETH_CREDIT_INVAL << QIB_AETH_CREDIT_SHIFT;
	} else {
		u32 min, max, x;
		u32 credits;
		struct rvt_rwq *wq = qp->r_rq.wq;
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
		aeth |= x << QIB_AETH_CREDIT_SHIFT;
	}
	return cpu_to_be32(aeth);
}

void *qp_priv_alloc(struct rvt_dev_info *rdi, struct rvt_qp *qp, gfp_t gfp)
{
	struct qib_qp_priv *priv;

	priv = kzalloc(sizeof(*priv), gfp);
	if (!priv)
		return ERR_PTR(-ENOMEM);
	priv->owner = qp;

	priv->s_hdr = kzalloc(sizeof(*priv->s_hdr), gfp);
	if (!priv->s_hdr) {
		kfree(priv);
		return ERR_PTR(-ENOMEM);
	}
	init_waitqueue_head(&priv->wait_dma);
	INIT_WORK(&priv->s_work, qib_do_send);
	INIT_LIST_HEAD(&priv->iowait);

	return priv;
}

void qp_priv_free(struct rvt_dev_info *rdi, struct rvt_qp *qp)
{
	struct qib_qp_priv *priv = qp->priv;

	kfree(priv->s_hdr);
	kfree(priv);
}

/**
 * qib_destroy_qp - destroy a queue pair
 * @ibqp: the queue pair to destroy
 *
 * Returns 0 on success.
 *
 * Note that this can be called while the QP is actively sending or
 * receiving!
 */
int qib_destroy_qp(struct ib_qp *ibqp)
{
	struct rvt_qp *qp = to_iqp(ibqp);
	struct qib_ibdev *dev = to_idev(ibqp->device);
	struct qib_qp_priv *priv = qp->priv;

	/* Make sure HW and driver activity is stopped. */
	spin_lock_irq(&qp->s_lock);
	if (qp->state != IB_QPS_RESET) {
		qp->state = IB_QPS_RESET;
		spin_lock(&dev->rdi.pending_lock);
		if (!list_empty(&priv->iowait))
			list_del_init(&priv->iowait);
		spin_unlock(&dev->rdi.pending_lock);
		qp->s_flags &= ~(RVT_S_TIMER | RVT_S_ANY_WAIT);
		spin_unlock_irq(&qp->s_lock);
		cancel_work_sync(&priv->s_work);
		del_timer_sync(&qp->s_timer);
		wait_event(priv->wait_dma, !atomic_read(&priv->s_dma_busy));
		if (priv->s_tx) {
			qib_put_txreq(priv->s_tx);
			priv->s_tx = NULL;
		}
		remove_qp(dev, qp);
		wait_event(qp->wait, !atomic_read(&qp->refcount));
		clear_mr_refs(qp, 1);
	} else
		spin_unlock_irq(&qp->s_lock);

	/* all user's cleaned up, mark it available */
	free_qpn(&dev->rdi.qp_dev->qpn_table, qp->ibqp.qp_num);
	spin_lock(&dev->n_qps_lock);
	dev->n_qps_allocated--;
	spin_unlock(&dev->n_qps_lock);

	if (qp->ip)
		kref_put(&qp->ip->ref, rvt_release_mmap_info);
	else
		vfree(qp->r_rq.wq);
	vfree(qp->s_wq);
	kfree(priv->s_hdr);
	kfree(priv);
	kfree(qp);
	return 0;
}

/**
 * qib_get_credit - flush the send work queue of a QP
 * @qp: the qp who's send work queue to flush
 * @aeth: the Acknowledge Extended Transport Header
 *
 * The QP s_lock should be held.
 */
void qib_get_credit(struct rvt_qp *qp, u32 aeth)
{
	u32 credit = (aeth >> QIB_AETH_CREDIT_SHIFT) & QIB_AETH_CREDIT_MASK;

	/*
	 * If the credit is invalid, we can send
	 * as many packets as we like.  Otherwise, we have to
	 * honor the credit field.
	 */
	if (credit == QIB_AETH_CREDIT_INVAL) {
		if (!(qp->s_flags & RVT_S_UNLIMITED_CREDIT)) {
			qp->s_flags |= RVT_S_UNLIMITED_CREDIT;
			if (qp->s_flags & RVT_S_WAIT_SSN_CREDIT) {
				qp->s_flags &= ~RVT_S_WAIT_SSN_CREDIT;
				qib_schedule_send(qp);
			}
		}
	} else if (!(qp->s_flags & RVT_S_UNLIMITED_CREDIT)) {
		/* Compute new LSN (i.e., MSN + credit) */
		credit = (aeth + credit_table[credit]) & QIB_MSN_MASK;
		if (qib_cmp24(credit, qp->s_lsn) > 0) {
			qp->s_lsn = credit;
			if (qp->s_flags & RVT_S_WAIT_SSN_CREDIT) {
				qp->s_flags &= ~RVT_S_WAIT_SSN_CREDIT;
				qib_schedule_send(qp);
			}
		}
	}
}

#ifdef CONFIG_DEBUG_FS

struct qib_qp_iter {
	struct qib_ibdev *dev;
	struct rvt_qp *qp;
	int n;
};

struct qib_qp_iter *qib_qp_iter_init(struct qib_ibdev *dev)
{
	struct qib_qp_iter *iter;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return NULL;

	iter->dev = dev;
	if (qib_qp_iter_next(iter)) {
		kfree(iter);
		return NULL;
	}

	return iter;
}

int qib_qp_iter_next(struct qib_qp_iter *iter)
{
	struct qib_ibdev *dev = iter->dev;
	int n = iter->n;
	int ret = 1;
	struct rvt_qp *pqp = iter->qp;
	struct rvt_qp *qp;

	for (; n < dev->rdi.qp_dev->qp_table_size; n++) {
		if (pqp)
			qp = rcu_dereference(pqp->next);
		else
			qp = rcu_dereference(dev->rdi.qp_dev->qp_table[n]);
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

void qib_qp_iter_print(struct seq_file *s, struct qib_qp_iter *iter)
{
	struct rvt_swqe *wqe;
	struct rvt_qp *qp = iter->qp;
	struct qib_qp_priv *priv = qp->priv;

	wqe = get_swqe_ptr(qp, qp->s_last);
	seq_printf(s,
		   "N %d QP%u %s %u %u %u f=%x %u %u %u %u %u PSN %x %x %x %x %x (%u %u %u %u %u %u) QP%u LID %x\n",
		   iter->n,
		   qp->ibqp.qp_num,
		   qp_type_str[qp->ibqp.qp_type],
		   qp->state,
		   wqe->wr.opcode,
		   qp->s_hdrwords,
		   qp->s_flags,
		   atomic_read(&priv->s_dma_busy),
		   !list_empty(&priv->iowait),
		   qp->timeout,
		   wqe->ssn,
		   qp->s_lsn,
		   qp->s_last_psn,
		   qp->s_psn, qp->s_next_psn,
		   qp->s_sending_psn, qp->s_sending_hpsn,
		   qp->s_last, qp->s_acked, qp->s_cur,
		   qp->s_tail, qp->s_head, qp->s_size,
		   qp->remote_qpn,
		   qp->remote_ah_attr.dlid);
}

#endif
