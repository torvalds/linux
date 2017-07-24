/*
 * Copyright(c) 2016, 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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

#include <linux/hash.h>
#include <linux/bitops.h>
#include <linux/lockdep.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_hdrs.h>
#include "qp.h"
#include "vt.h"
#include "trace.h"

static void rvt_rc_timeout(unsigned long arg);

/*
 * Convert the AETH RNR timeout code into the number of microseconds.
 */
static const u32 ib_rvt_rnr_table[32] = {
	655360, /* 00: 655.36 */
	10,     /* 01:    .01 */
	20,     /* 02     .02 */
	30,     /* 03:    .03 */
	40,     /* 04:    .04 */
	60,     /* 05:    .06 */
	80,     /* 06:    .08 */
	120,    /* 07:    .12 */
	160,    /* 08:    .16 */
	240,    /* 09:    .24 */
	320,    /* 0A:    .32 */
	480,    /* 0B:    .48 */
	640,    /* 0C:    .64 */
	960,    /* 0D:    .96 */
	1280,   /* 0E:   1.28 */
	1920,   /* 0F:   1.92 */
	2560,   /* 10:   2.56 */
	3840,   /* 11:   3.84 */
	5120,   /* 12:   5.12 */
	7680,   /* 13:   7.68 */
	10240,  /* 14:  10.24 */
	15360,  /* 15:  15.36 */
	20480,  /* 16:  20.48 */
	30720,  /* 17:  30.72 */
	40960,  /* 18:  40.96 */
	61440,  /* 19:  61.44 */
	81920,  /* 1A:  81.92 */
	122880, /* 1B: 122.88 */
	163840, /* 1C: 163.84 */
	245760, /* 1D: 245.76 */
	327680, /* 1E: 327.68 */
	491520  /* 1F: 491.52 */
};

/*
 * Note that it is OK to post send work requests in the SQE and ERR
 * states; rvt_do_send() will process them and generate error
 * completions as per IB 1.2 C10-96.
 */
const int ib_rvt_state_ops[IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = 0,
	[IB_QPS_INIT] = RVT_POST_RECV_OK,
	[IB_QPS_RTR] = RVT_POST_RECV_OK | RVT_PROCESS_RECV_OK,
	[IB_QPS_RTS] = RVT_POST_RECV_OK | RVT_PROCESS_RECV_OK |
	    RVT_POST_SEND_OK | RVT_PROCESS_SEND_OK |
	    RVT_PROCESS_NEXT_SEND_OK,
	[IB_QPS_SQD] = RVT_POST_RECV_OK | RVT_PROCESS_RECV_OK |
	    RVT_POST_SEND_OK | RVT_PROCESS_SEND_OK,
	[IB_QPS_SQE] = RVT_POST_RECV_OK | RVT_PROCESS_RECV_OK |
	    RVT_POST_SEND_OK | RVT_FLUSH_SEND,
	[IB_QPS_ERR] = RVT_POST_RECV_OK | RVT_FLUSH_RECV |
	    RVT_POST_SEND_OK | RVT_FLUSH_SEND,
};
EXPORT_SYMBOL(ib_rvt_state_ops);

static void get_map_page(struct rvt_qpn_table *qpt,
			 struct rvt_qpn_map *map)
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

/**
 * init_qpn_table - initialize the QP number table for a device
 * @qpt: the QPN table
 */
static int init_qpn_table(struct rvt_dev_info *rdi, struct rvt_qpn_table *qpt)
{
	u32 offset, i;
	struct rvt_qpn_map *map;
	int ret = 0;

	if (!(rdi->dparms.qpn_res_end >= rdi->dparms.qpn_res_start))
		return -EINVAL;

	spin_lock_init(&qpt->lock);

	qpt->last = rdi->dparms.qpn_start;
	qpt->incr = rdi->dparms.qpn_inc << rdi->dparms.qos_shift;

	/*
	 * Drivers may want some QPs beyond what we need for verbs let them use
	 * our qpn table. No need for two. Lets go ahead and mark the bitmaps
	 * for those. The reserved range must be *after* the range which verbs
	 * will pick from.
	 */

	/* Figure out number of bit maps needed before reserved range */
	qpt->nmaps = rdi->dparms.qpn_res_start / RVT_BITS_PER_PAGE;

	/* This should always be zero */
	offset = rdi->dparms.qpn_res_start & RVT_BITS_PER_PAGE_MASK;

	/* Starting with the first reserved bit map */
	map = &qpt->map[qpt->nmaps];

	rvt_pr_info(rdi, "Reserving QPNs from 0x%x to 0x%x for non-verbs use\n",
		    rdi->dparms.qpn_res_start, rdi->dparms.qpn_res_end);
	for (i = rdi->dparms.qpn_res_start; i <= rdi->dparms.qpn_res_end; i++) {
		if (!map->page) {
			get_map_page(qpt, map);
			if (!map->page) {
				ret = -ENOMEM;
				break;
			}
		}
		set_bit(offset, map->page);
		offset++;
		if (offset == RVT_BITS_PER_PAGE) {
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
static void free_qpn_table(struct rvt_qpn_table *qpt)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qpt->map); i++)
		free_page((unsigned long)qpt->map[i].page);
}

/**
 * rvt_driver_qp_init - Init driver qp resources
 * @rdi: rvt dev strucutre
 *
 * Return: 0 on success
 */
int rvt_driver_qp_init(struct rvt_dev_info *rdi)
{
	int i;
	int ret = -ENOMEM;

	if (!rdi->dparms.qp_table_size)
		return -EINVAL;

	/*
	 * If driver is not doing any QP allocation then make sure it is
	 * providing the necessary QP functions.
	 */
	if (!rdi->driver_f.free_all_qps ||
	    !rdi->driver_f.qp_priv_alloc ||
	    !rdi->driver_f.qp_priv_free ||
	    !rdi->driver_f.notify_qp_reset ||
	    !rdi->driver_f.notify_restart_rc)
		return -EINVAL;

	/* allocate parent object */
	rdi->qp_dev = kzalloc_node(sizeof(*rdi->qp_dev), GFP_KERNEL,
				   rdi->dparms.node);
	if (!rdi->qp_dev)
		return -ENOMEM;

	/* allocate hash table */
	rdi->qp_dev->qp_table_size = rdi->dparms.qp_table_size;
	rdi->qp_dev->qp_table_bits = ilog2(rdi->dparms.qp_table_size);
	rdi->qp_dev->qp_table =
		kmalloc_node(rdi->qp_dev->qp_table_size *
			     sizeof(*rdi->qp_dev->qp_table),
			     GFP_KERNEL, rdi->dparms.node);
	if (!rdi->qp_dev->qp_table)
		goto no_qp_table;

	for (i = 0; i < rdi->qp_dev->qp_table_size; i++)
		RCU_INIT_POINTER(rdi->qp_dev->qp_table[i], NULL);

	spin_lock_init(&rdi->qp_dev->qpt_lock);

	/* initialize qpn map */
	if (init_qpn_table(rdi, &rdi->qp_dev->qpn_table))
		goto fail_table;

	spin_lock_init(&rdi->n_qps_lock);

	return 0;

fail_table:
	kfree(rdi->qp_dev->qp_table);
	free_qpn_table(&rdi->qp_dev->qpn_table);

no_qp_table:
	kfree(rdi->qp_dev);

	return ret;
}

/**
 * free_all_qps - check for QPs still in use
 * @qpt: the QP table to empty
 *
 * There should not be any QPs still in use.
 * Free memory for table.
 */
static unsigned rvt_free_all_qps(struct rvt_dev_info *rdi)
{
	unsigned long flags;
	struct rvt_qp *qp;
	unsigned n, qp_inuse = 0;
	spinlock_t *ql; /* work around too long line below */

	if (rdi->driver_f.free_all_qps)
		qp_inuse = rdi->driver_f.free_all_qps(rdi);

	qp_inuse += rvt_mcast_tree_empty(rdi);

	if (!rdi->qp_dev)
		return qp_inuse;

	ql = &rdi->qp_dev->qpt_lock;
	spin_lock_irqsave(ql, flags);
	for (n = 0; n < rdi->qp_dev->qp_table_size; n++) {
		qp = rcu_dereference_protected(rdi->qp_dev->qp_table[n],
					       lockdep_is_held(ql));
		RCU_INIT_POINTER(rdi->qp_dev->qp_table[n], NULL);

		for (; qp; qp = rcu_dereference_protected(qp->next,
							  lockdep_is_held(ql)))
			qp_inuse++;
	}
	spin_unlock_irqrestore(ql, flags);
	synchronize_rcu();
	return qp_inuse;
}

/**
 * rvt_qp_exit - clean up qps on device exit
 * @rdi: rvt dev structure
 *
 * Check for qp leaks and free resources.
 */
void rvt_qp_exit(struct rvt_dev_info *rdi)
{
	u32 qps_inuse = rvt_free_all_qps(rdi);

	if (qps_inuse)
		rvt_pr_err(rdi, "QP memory leak! %u still in use\n",
			   qps_inuse);
	if (!rdi->qp_dev)
		return;

	kfree(rdi->qp_dev->qp_table);
	free_qpn_table(&rdi->qp_dev->qpn_table);
	kfree(rdi->qp_dev);
}

static inline unsigned mk_qpn(struct rvt_qpn_table *qpt,
			      struct rvt_qpn_map *map, unsigned off)
{
	return (map - qpt->map) * RVT_BITS_PER_PAGE + off;
}

/**
 * alloc_qpn - Allocate the next available qpn or zero/one for QP type
 *	       IB_QPT_SMI/IB_QPT_GSI
 *@rdi:	rvt device info structure
 *@qpt: queue pair number table pointer
 *@port_num: IB port number, 1 based, comes from core
 *
 * Return: The queue pair number
 */
static int alloc_qpn(struct rvt_dev_info *rdi, struct rvt_qpn_table *qpt,
		     enum ib_qp_type type, u8 port_num)
{
	u32 i, offset, max_scan, qpn;
	struct rvt_qpn_map *map;
	u32 ret;

	if (rdi->driver_f.alloc_qpn)
		return rdi->driver_f.alloc_qpn(rdi, qpt, type, port_num);

	if (type == IB_QPT_SMI || type == IB_QPT_GSI) {
		unsigned n;

		ret = type == IB_QPT_GSI;
		n = 1 << (ret + 2 * (port_num - 1));
		spin_lock(&qpt->lock);
		if (qpt->flags & n)
			ret = -EINVAL;
		else
			qpt->flags |= n;
		spin_unlock(&qpt->lock);
		goto bail;
	}

	qpn = qpt->last + qpt->incr;
	if (qpn >= RVT_QPN_MAX)
		qpn = qpt->incr | ((qpt->last & 1) ^ 1);
	/* offset carries bit 0 */
	offset = qpn & RVT_BITS_PER_PAGE_MASK;
	map = &qpt->map[qpn / RVT_BITS_PER_PAGE];
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
		/* there can be no set bits in low-order QoS bits */
		WARN_ON(offset & (BIT(rdi->dparms.qos_shift) - 1));
		qpn = mk_qpn(qpt, map, offset);
	}

	ret = -ENOMEM;

bail:
	return ret;
}

/**
 * rvt_clear_mr_refs - Drop help mr refs
 * @qp: rvt qp data structure
 * @clr_sends: If shoudl clear send side or not
 */
static void rvt_clear_mr_refs(struct rvt_qp *qp, int clr_sends)
{
	unsigned n;
	struct rvt_dev_info *rdi = ib_to_rvt(qp->ibqp.device);

	if (test_and_clear_bit(RVT_R_REWIND_SGE, &qp->r_aflags))
		rvt_put_ss(&qp->s_rdma_read_sge);

	rvt_put_ss(&qp->r_sge);

	if (clr_sends) {
		while (qp->s_last != qp->s_head) {
			struct rvt_swqe *wqe = rvt_get_swqe_ptr(qp, qp->s_last);
			unsigned i;

			for (i = 0; i < wqe->wr.num_sge; i++) {
				struct rvt_sge *sge = &wqe->sg_list[i];

				rvt_put_mr(sge->mr);
			}
			if (qp->ibqp.qp_type == IB_QPT_UD ||
			    qp->ibqp.qp_type == IB_QPT_SMI ||
			    qp->ibqp.qp_type == IB_QPT_GSI)
				atomic_dec(&ibah_to_rvtah(
						wqe->ud_wr.ah)->refcount);
			if (++qp->s_last >= qp->s_size)
				qp->s_last = 0;
			smp_wmb(); /* see qp_set_savail */
		}
		if (qp->s_rdma_mr) {
			rvt_put_mr(qp->s_rdma_mr);
			qp->s_rdma_mr = NULL;
		}
	}

	if (qp->ibqp.qp_type != IB_QPT_RC)
		return;

	for (n = 0; n < rvt_max_atomic(rdi); n++) {
		struct rvt_ack_entry *e = &qp->s_ack_queue[n];

		if (e->rdma_sge.mr) {
			rvt_put_mr(e->rdma_sge.mr);
			e->rdma_sge.mr = NULL;
		}
	}
}

/**
 * rvt_remove_qp - remove qp form table
 * @rdi: rvt dev struct
 * @qp: qp to remove
 *
 * Remove the QP from the table so it can't be found asynchronously by
 * the receive routine.
 */
static void rvt_remove_qp(struct rvt_dev_info *rdi, struct rvt_qp *qp)
{
	struct rvt_ibport *rvp = rdi->ports[qp->port_num - 1];
	u32 n = hash_32(qp->ibqp.qp_num, rdi->qp_dev->qp_table_bits);
	unsigned long flags;
	int removed = 1;

	spin_lock_irqsave(&rdi->qp_dev->qpt_lock, flags);

	if (rcu_dereference_protected(rvp->qp[0],
			lockdep_is_held(&rdi->qp_dev->qpt_lock)) == qp) {
		RCU_INIT_POINTER(rvp->qp[0], NULL);
	} else if (rcu_dereference_protected(rvp->qp[1],
			lockdep_is_held(&rdi->qp_dev->qpt_lock)) == qp) {
		RCU_INIT_POINTER(rvp->qp[1], NULL);
	} else {
		struct rvt_qp *q;
		struct rvt_qp __rcu **qpp;

		removed = 0;
		qpp = &rdi->qp_dev->qp_table[n];
		for (; (q = rcu_dereference_protected(*qpp,
			lockdep_is_held(&rdi->qp_dev->qpt_lock))) != NULL;
			qpp = &q->next) {
			if (q == qp) {
				RCU_INIT_POINTER(*qpp,
				     rcu_dereference_protected(qp->next,
				     lockdep_is_held(&rdi->qp_dev->qpt_lock)));
				removed = 1;
				trace_rvt_qpremove(qp, n);
				break;
			}
		}
	}

	spin_unlock_irqrestore(&rdi->qp_dev->qpt_lock, flags);
	if (removed) {
		synchronize_rcu();
		rvt_put_qp(qp);
	}
}

/**
 * rvt_init_qp - initialize the QP state to the reset state
 * @qp: the QP to init or reinit
 * @type: the QP type
 *
 * This function is called from both rvt_create_qp() and
 * rvt_reset_qp().   The difference is that the reset
 * patch the necessary locks to protect against concurent
 * access.
 */
static void rvt_init_qp(struct rvt_dev_info *rdi, struct rvt_qp *qp,
			enum ib_qp_type type)
{
	qp->remote_qpn = 0;
	qp->qkey = 0;
	qp->qp_access_flags = 0;
	qp->s_flags &= RVT_S_SIGNAL_REQ_WR;
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
	qp->s_mig_state = IB_MIG_MIGRATED;
	qp->r_head_ack_queue = 0;
	qp->s_tail_ack_queue = 0;
	qp->s_num_rd_atomic = 0;
	if (qp->r_rq.wq) {
		qp->r_rq.wq->head = 0;
		qp->r_rq.wq->tail = 0;
	}
	qp->r_sge.num_sge = 0;
	atomic_set(&qp->s_reserved_used, 0);
}

/**
 * rvt_reset_qp - initialize the QP state to the reset state
 * @qp: the QP to reset
 * @type: the QP type
 *
 * r_lock, s_hlock, and s_lock are required to be held by the caller
 */
static void rvt_reset_qp(struct rvt_dev_info *rdi, struct rvt_qp *qp,
			 enum ib_qp_type type)
	__must_hold(&qp->s_lock)
	__must_hold(&qp->s_hlock)
	__must_hold(&qp->r_lock)
{
	lockdep_assert_held(&qp->r_lock);
	lockdep_assert_held(&qp->s_hlock);
	lockdep_assert_held(&qp->s_lock);
	if (qp->state != IB_QPS_RESET) {
		qp->state = IB_QPS_RESET;

		/* Let drivers flush their waitlist */
		rdi->driver_f.flush_qp_waiters(qp);
		rvt_stop_rc_timers(qp);
		qp->s_flags &= ~(RVT_S_TIMER | RVT_S_ANY_WAIT);
		spin_unlock(&qp->s_lock);
		spin_unlock(&qp->s_hlock);
		spin_unlock_irq(&qp->r_lock);

		/* Stop the send queue and the retry timer */
		rdi->driver_f.stop_send_queue(qp);
		rvt_del_timers_sync(qp);
		/* Wait for things to stop */
		rdi->driver_f.quiesce_qp(qp);

		/* take qp out the hash and wait for it to be unused */
		rvt_remove_qp(rdi, qp);
		wait_event(qp->wait, !atomic_read(&qp->refcount));

		/* grab the lock b/c it was locked at call time */
		spin_lock_irq(&qp->r_lock);
		spin_lock(&qp->s_hlock);
		spin_lock(&qp->s_lock);

		rvt_clear_mr_refs(qp, 1);
		/*
		 * Let the driver do any tear down or re-init it needs to for
		 * a qp that has been reset
		 */
		rdi->driver_f.notify_qp_reset(qp);
	}
	rvt_init_qp(rdi, qp, type);
	lockdep_assert_held(&qp->r_lock);
	lockdep_assert_held(&qp->s_hlock);
	lockdep_assert_held(&qp->s_lock);
}

/** rvt_free_qpn - Free a qpn from the bit map
 * @qpt: QP table
 * @qpn: queue pair number to free
 */
static void rvt_free_qpn(struct rvt_qpn_table *qpt, u32 qpn)
{
	struct rvt_qpn_map *map;

	map = qpt->map + (qpn & RVT_QPN_MASK) / RVT_BITS_PER_PAGE;
	if (map->page)
		clear_bit(qpn & RVT_BITS_PER_PAGE_MASK, map->page);
}

/**
 * rvt_create_qp - create a queue pair for a device
 * @ibpd: the protection domain who's device we create the queue pair for
 * @init_attr: the attributes of the queue pair
 * @udata: user data for libibverbs.so
 *
 * Queue pair creation is mostly an rvt issue. However, drivers have their own
 * unique idea of what queue pair numbers mean. For instance there is a reserved
 * range for PSM.
 *
 * Return: the queue pair on success, otherwise returns an errno.
 *
 * Called by the ib_create_qp() core verbs function.
 */
struct ib_qp *rvt_create_qp(struct ib_pd *ibpd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata)
{
	struct rvt_qp *qp;
	int err;
	struct rvt_swqe *swq = NULL;
	size_t sz;
	size_t sg_list_sz;
	struct ib_qp *ret = ERR_PTR(-ENOMEM);
	struct rvt_dev_info *rdi = ib_to_rvt(ibpd->device);
	void *priv = NULL;
	size_t sqsize;

	if (!rdi)
		return ERR_PTR(-EINVAL);

	if (init_attr->cap.max_send_sge > rdi->dparms.props.max_sge ||
	    init_attr->cap.max_send_wr > rdi->dparms.props.max_qp_wr ||
	    init_attr->create_flags)
		return ERR_PTR(-EINVAL);

	/* Check receive queue parameters if no SRQ is specified. */
	if (!init_attr->srq) {
		if (init_attr->cap.max_recv_sge > rdi->dparms.props.max_sge ||
		    init_attr->cap.max_recv_wr > rdi->dparms.props.max_qp_wr)
			return ERR_PTR(-EINVAL);

		if (init_attr->cap.max_send_sge +
		    init_attr->cap.max_send_wr +
		    init_attr->cap.max_recv_sge +
		    init_attr->cap.max_recv_wr == 0)
			return ERR_PTR(-EINVAL);
	}
	sqsize =
		init_attr->cap.max_send_wr + 1 +
		rdi->dparms.reserved_operations;
	switch (init_attr->qp_type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		if (init_attr->port_num == 0 ||
		    init_attr->port_num > ibpd->device->phys_port_cnt)
			return ERR_PTR(-EINVAL);
	case IB_QPT_UC:
	case IB_QPT_RC:
	case IB_QPT_UD:
		sz = sizeof(struct rvt_sge) *
			init_attr->cap.max_send_sge +
			sizeof(struct rvt_swqe);
		swq = vzalloc_node(sqsize * sz, rdi->dparms.node);
		if (!swq)
			return ERR_PTR(-ENOMEM);

		sz = sizeof(*qp);
		sg_list_sz = 0;
		if (init_attr->srq) {
			struct rvt_srq *srq = ibsrq_to_rvtsrq(init_attr->srq);

			if (srq->rq.max_sge > 1)
				sg_list_sz = sizeof(*qp->r_sg_list) *
					(srq->rq.max_sge - 1);
		} else if (init_attr->cap.max_recv_sge > 1)
			sg_list_sz = sizeof(*qp->r_sg_list) *
				(init_attr->cap.max_recv_sge - 1);
		qp = kzalloc_node(sz + sg_list_sz, GFP_KERNEL,
				  rdi->dparms.node);
		if (!qp)
			goto bail_swq;

		RCU_INIT_POINTER(qp->next, NULL);
		if (init_attr->qp_type == IB_QPT_RC) {
			qp->s_ack_queue =
				kzalloc_node(
					sizeof(*qp->s_ack_queue) *
					 rvt_max_atomic(rdi),
					GFP_KERNEL,
					rdi->dparms.node);
			if (!qp->s_ack_queue)
				goto bail_qp;
		}
		/* initialize timers needed for rc qp */
		setup_timer(&qp->s_timer, rvt_rc_timeout, (unsigned long)qp);
		hrtimer_init(&qp->s_rnr_timer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL);
		qp->s_rnr_timer.function = rvt_rc_rnr_retry;

		/*
		 * Driver needs to set up it's private QP structure and do any
		 * initialization that is needed.
		 */
		priv = rdi->driver_f.qp_priv_alloc(rdi, qp);
		if (IS_ERR(priv)) {
			ret = priv;
			goto bail_qp;
		}
		qp->priv = priv;
		qp->timeout_jiffies =
			usecs_to_jiffies((4096UL * (1UL << qp->timeout)) /
				1000UL);
		if (init_attr->srq) {
			sz = 0;
		} else {
			qp->r_rq.size = init_attr->cap.max_recv_wr + 1;
			qp->r_rq.max_sge = init_attr->cap.max_recv_sge;
			sz = (sizeof(struct ib_sge) * qp->r_rq.max_sge) +
				sizeof(struct rvt_rwqe);
			if (udata)
				qp->r_rq.wq = vmalloc_user(
						sizeof(struct rvt_rwq) +
						qp->r_rq.size * sz);
			else
				qp->r_rq.wq = vzalloc_node(
						sizeof(struct rvt_rwq) +
						qp->r_rq.size * sz,
						rdi->dparms.node);
			if (!qp->r_rq.wq)
				goto bail_driver_priv;
		}

		/*
		 * ib_create_qp() will initialize qp->ibqp
		 * except for qp->ibqp.qp_num.
		 */
		spin_lock_init(&qp->r_lock);
		spin_lock_init(&qp->s_hlock);
		spin_lock_init(&qp->s_lock);
		spin_lock_init(&qp->r_rq.lock);
		atomic_set(&qp->refcount, 0);
		atomic_set(&qp->local_ops_pending, 0);
		init_waitqueue_head(&qp->wait);
		init_timer(&qp->s_timer);
		qp->s_timer.data = (unsigned long)qp;
		INIT_LIST_HEAD(&qp->rspwait);
		qp->state = IB_QPS_RESET;
		qp->s_wq = swq;
		qp->s_size = sqsize;
		qp->s_avail = init_attr->cap.max_send_wr;
		qp->s_max_sge = init_attr->cap.max_send_sge;
		if (init_attr->sq_sig_type == IB_SIGNAL_REQ_WR)
			qp->s_flags = RVT_S_SIGNAL_REQ_WR;

		err = alloc_qpn(rdi, &rdi->qp_dev->qpn_table,
				init_attr->qp_type,
				init_attr->port_num);
		if (err < 0) {
			ret = ERR_PTR(err);
			goto bail_rq_wq;
		}
		qp->ibqp.qp_num = err;
		qp->port_num = init_attr->port_num;
		rvt_init_qp(rdi, qp, init_attr->qp_type);
		break;

	default:
		/* Don't support raw QPs */
		return ERR_PTR(-EINVAL);
	}

	init_attr->cap.max_inline_data = 0;

	/*
	 * Return the address of the RWQ as the offset to mmap.
	 * See rvt_mmap() for details.
	 */
	if (udata && udata->outlen >= sizeof(__u64)) {
		if (!qp->r_rq.wq) {
			__u64 offset = 0;

			err = ib_copy_to_udata(udata, &offset,
					       sizeof(offset));
			if (err) {
				ret = ERR_PTR(err);
				goto bail_qpn;
			}
		} else {
			u32 s = sizeof(struct rvt_rwq) + qp->r_rq.size * sz;

			qp->ip = rvt_create_mmap_info(rdi, s,
						      ibpd->uobject->context,
						      qp->r_rq.wq);
			if (!qp->ip) {
				ret = ERR_PTR(-ENOMEM);
				goto bail_qpn;
			}

			err = ib_copy_to_udata(udata, &qp->ip->offset,
					       sizeof(qp->ip->offset));
			if (err) {
				ret = ERR_PTR(err);
				goto bail_ip;
			}
		}
		qp->pid = current->pid;
	}

	spin_lock(&rdi->n_qps_lock);
	if (rdi->n_qps_allocated == rdi->dparms.props.max_qp) {
		spin_unlock(&rdi->n_qps_lock);
		ret = ERR_PTR(-ENOMEM);
		goto bail_ip;
	}

	rdi->n_qps_allocated++;
	/*
	 * Maintain a busy_jiffies variable that will be added to the timeout
	 * period in mod_retry_timer and add_retry_timer. This busy jiffies
	 * is scaled by the number of rc qps created for the device to reduce
	 * the number of timeouts occurring when there is a large number of
	 * qps. busy_jiffies is incremented every rc qp scaling interval.
	 * The scaling interval is selected based on extensive performance
	 * evaluation of targeted workloads.
	 */
	if (init_attr->qp_type == IB_QPT_RC) {
		rdi->n_rc_qps++;
		rdi->busy_jiffies = rdi->n_rc_qps / RC_QP_SCALING_INTERVAL;
	}
	spin_unlock(&rdi->n_qps_lock);

	if (qp->ip) {
		spin_lock_irq(&rdi->pending_lock);
		list_add(&qp->ip->pending_mmaps, &rdi->pending_mmaps);
		spin_unlock_irq(&rdi->pending_lock);
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
		qp->allowed_ops = IB_OPCODE_UD;
		break;
	case IB_QPT_RC:
		qp->allowed_ops = IB_OPCODE_RC;
		break;
	case IB_QPT_UC:
		qp->allowed_ops = IB_OPCODE_UC;
		break;
	default:
		ret = ERR_PTR(-EINVAL);
		goto bail_ip;
	}

	return ret;

bail_ip:
	if (qp->ip)
		kref_put(&qp->ip->ref, rvt_release_mmap_info);

bail_qpn:
	rvt_free_qpn(&rdi->qp_dev->qpn_table, qp->ibqp.qp_num);

bail_rq_wq:
	if (!qp->ip)
		vfree(qp->r_rq.wq);

bail_driver_priv:
	rdi->driver_f.qp_priv_free(rdi, qp);

bail_qp:
	kfree(qp->s_ack_queue);
	kfree(qp);

bail_swq:
	vfree(swq);

	return ret;
}

/**
 * rvt_error_qp - put a QP into the error state
 * @qp: the QP to put into the error state
 * @err: the receive completion error to signal if a RWQE is active
 *
 * Flushes both send and receive work queues.
 *
 * Return: true if last WQE event should be generated.
 * The QP r_lock and s_lock should be held and interrupts disabled.
 * If we are already in error state, just return.
 */
int rvt_error_qp(struct rvt_qp *qp, enum ib_wc_status err)
{
	struct ib_wc wc;
	int ret = 0;
	struct rvt_dev_info *rdi = ib_to_rvt(qp->ibqp.device);

	lockdep_assert_held(&qp->r_lock);
	lockdep_assert_held(&qp->s_lock);
	if (qp->state == IB_QPS_ERR || qp->state == IB_QPS_RESET)
		goto bail;

	qp->state = IB_QPS_ERR;

	if (qp->s_flags & (RVT_S_TIMER | RVT_S_WAIT_RNR)) {
		qp->s_flags &= ~(RVT_S_TIMER | RVT_S_WAIT_RNR);
		del_timer(&qp->s_timer);
	}

	if (qp->s_flags & RVT_S_ANY_WAIT_SEND)
		qp->s_flags &= ~RVT_S_ANY_WAIT_SEND;

	rdi->driver_f.notify_error_qp(qp);

	/* Schedule the sending tasklet to drain the send work queue. */
	if (ACCESS_ONCE(qp->s_last) != qp->s_head)
		rdi->driver_f.schedule_send(qp);

	rvt_clear_mr_refs(qp, 0);

	memset(&wc, 0, sizeof(wc));
	wc.qp = &qp->ibqp;
	wc.opcode = IB_WC_RECV;

	if (test_and_clear_bit(RVT_R_WRID_VALID, &qp->r_aflags)) {
		wc.wr_id = qp->r_wr_id;
		wc.status = err;
		rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc, 1);
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
			wc.wr_id = rvt_get_rwqe_ptr(&qp->r_rq, tail)->wr_id;
			if (++tail >= qp->r_rq.size)
				tail = 0;
			rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc, 1);
		}
		wq->tail = tail;

		spin_unlock(&qp->r_rq.lock);
	} else if (qp->ibqp.event_handler) {
		ret = 1;
	}

bail:
	return ret;
}
EXPORT_SYMBOL(rvt_error_qp);

/*
 * Put the QP into the hash table.
 * The hash table holds a reference to the QP.
 */
static void rvt_insert_qp(struct rvt_dev_info *rdi, struct rvt_qp *qp)
{
	struct rvt_ibport *rvp = rdi->ports[qp->port_num - 1];
	unsigned long flags;

	rvt_get_qp(qp);
	spin_lock_irqsave(&rdi->qp_dev->qpt_lock, flags);

	if (qp->ibqp.qp_num <= 1) {
		rcu_assign_pointer(rvp->qp[qp->ibqp.qp_num], qp);
	} else {
		u32 n = hash_32(qp->ibqp.qp_num, rdi->qp_dev->qp_table_bits);

		qp->next = rdi->qp_dev->qp_table[n];
		rcu_assign_pointer(rdi->qp_dev->qp_table[n], qp);
		trace_rvt_qpinsert(qp, n);
	}

	spin_unlock_irqrestore(&rdi->qp_dev->qpt_lock, flags);
}

/**
 * rvt_modify_qp - modify the attributes of a queue pair
 * @ibqp: the queue pair who's attributes we're modifying
 * @attr: the new attributes
 * @attr_mask: the mask of attributes to modify
 * @udata: user data for libibverbs.so
 *
 * Return: 0 on success, otherwise returns an errno.
 */
int rvt_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		  int attr_mask, struct ib_udata *udata)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);
	struct rvt_qp *qp = ibqp_to_rvtqp(ibqp);
	enum ib_qp_state cur_state, new_state;
	struct ib_event ev;
	int lastwqe = 0;
	int mig = 0;
	int pmtu = 0; /* for gcc warning only */
	enum rdma_link_layer link;

	link = rdma_port_get_link_layer(ibqp->device, qp->port_num);

	spin_lock_irq(&qp->r_lock);
	spin_lock(&qp->s_hlock);
	spin_lock(&qp->s_lock);

	cur_state = attr_mask & IB_QP_CUR_STATE ?
		attr->cur_qp_state : qp->state;
	new_state = attr_mask & IB_QP_STATE ? attr->qp_state : cur_state;

	if (!ib_modify_qp_is_ok(cur_state, new_state, ibqp->qp_type,
				attr_mask, link))
		goto inval;

	if (rdi->driver_f.check_modify_qp &&
	    rdi->driver_f.check_modify_qp(qp, attr, attr_mask, udata))
		goto inval;

	if (attr_mask & IB_QP_AV) {
		if (rdma_ah_get_dlid(&attr->ah_attr) >=
		    be16_to_cpu(IB_MULTICAST_LID_BASE))
			goto inval;
		if (rvt_check_ah(qp->ibqp.device, &attr->ah_attr))
			goto inval;
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		if (rdma_ah_get_dlid(&attr->alt_ah_attr) >=
		    be16_to_cpu(IB_MULTICAST_LID_BASE))
			goto inval;
		if (rvt_check_ah(qp->ibqp.device, &attr->alt_ah_attr))
			goto inval;
		if (attr->alt_pkey_index >= rvt_get_npkeys(rdi))
			goto inval;
	}

	if (attr_mask & IB_QP_PKEY_INDEX)
		if (attr->pkey_index >= rvt_get_npkeys(rdi))
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
		if (attr->dest_qp_num > RVT_QPN_MASK)
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
		pmtu = rdi->driver_f.get_pmtu_from_attr(rdi, qp, attr);
		if (pmtu < 0)
			goto inval;
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
		} else {
			goto inval;
		}
	}

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		if (attr->max_dest_rd_atomic > rdi->dparms.max_rdma_atomic)
			goto inval;

	switch (new_state) {
	case IB_QPS_RESET:
		if (qp->state != IB_QPS_RESET)
			rvt_reset_qp(rdi, qp, ibqp->qp_type);
		break;

	case IB_QPS_RTR:
		/* Allow event to re-trigger if QP set to RTR more than once */
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
		lastwqe = rvt_error_qp(qp, IB_WC_WR_FLUSH_ERR);
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
		qp->s_next_psn = attr->sq_psn & rdi->dparms.psn_modify_mask;
		qp->s_psn = qp->s_next_psn;
		qp->s_sending_psn = qp->s_next_psn;
		qp->s_last_psn = qp->s_next_psn - 1;
		qp->s_sending_hpsn = qp->s_last_psn;
	}

	if (attr_mask & IB_QP_RQ_PSN)
		qp->r_psn = attr->rq_psn & rdi->dparms.psn_modify_mask;

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		qp->qp_access_flags = attr->qp_access_flags;

	if (attr_mask & IB_QP_AV) {
		qp->remote_ah_attr = attr->ah_attr;
		qp->s_srate = rdma_ah_get_static_rate(&attr->ah_attr);
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
			qp->port_num = rdma_ah_get_port_num(&qp->alt_ah_attr);
			qp->s_pkey_index = qp->s_alt_pkey_index;
		}
	}

	if (attr_mask & IB_QP_PATH_MTU) {
		qp->pmtu = rdi->driver_f.mtu_from_qp(rdi, qp, pmtu);
		qp->path_mtu = rdi->driver_f.mtu_to_path_mtu(qp->pmtu);
		qp->log_pmtu = ilog2(qp->pmtu);
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
		qp->timeout_jiffies = rvt_timeout_to_jiffies(qp->timeout);
	}

	if (attr_mask & IB_QP_QKEY)
		qp->qkey = attr->qkey;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		qp->r_max_rd_atomic = attr->max_dest_rd_atomic;

	if (attr_mask & IB_QP_MAX_QP_RD_ATOMIC)
		qp->s_max_rd_atomic = attr->max_rd_atomic;

	if (rdi->driver_f.modify_qp)
		rdi->driver_f.modify_qp(qp, attr, attr_mask, udata);

	spin_unlock(&qp->s_lock);
	spin_unlock(&qp->s_hlock);
	spin_unlock_irq(&qp->r_lock);

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT)
		rvt_insert_qp(rdi, qp);

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
	return 0;

inval:
	spin_unlock(&qp->s_lock);
	spin_unlock(&qp->s_hlock);
	spin_unlock_irq(&qp->r_lock);
	return -EINVAL;
}

/**
 * rvt_destroy_qp - destroy a queue pair
 * @ibqp: the queue pair to destroy
 *
 * Note that this can be called while the QP is actively sending or
 * receiving!
 *
 * Return: 0 on success.
 */
int rvt_destroy_qp(struct ib_qp *ibqp)
{
	struct rvt_qp *qp = ibqp_to_rvtqp(ibqp);
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);

	spin_lock_irq(&qp->r_lock);
	spin_lock(&qp->s_hlock);
	spin_lock(&qp->s_lock);
	rvt_reset_qp(rdi, qp, ibqp->qp_type);
	spin_unlock(&qp->s_lock);
	spin_unlock(&qp->s_hlock);
	spin_unlock_irq(&qp->r_lock);

	/* qpn is now available for use again */
	rvt_free_qpn(&rdi->qp_dev->qpn_table, qp->ibqp.qp_num);

	spin_lock(&rdi->n_qps_lock);
	rdi->n_qps_allocated--;
	if (qp->ibqp.qp_type == IB_QPT_RC) {
		rdi->n_rc_qps--;
		rdi->busy_jiffies = rdi->n_rc_qps / RC_QP_SCALING_INTERVAL;
	}
	spin_unlock(&rdi->n_qps_lock);

	if (qp->ip)
		kref_put(&qp->ip->ref, rvt_release_mmap_info);
	else
		vfree(qp->r_rq.wq);
	vfree(qp->s_wq);
	rdi->driver_f.qp_priv_free(rdi, qp);
	kfree(qp->s_ack_queue);
	kfree(qp);
	return 0;
}

/**
 * rvt_query_qp - query an ipbq
 * @ibqp: IB qp to query
 * @attr: attr struct to fill in
 * @attr_mask: attr mask ignored
 * @init_attr: struct to fill in
 *
 * Return: always 0
 */
int rvt_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		 int attr_mask, struct ib_qp_init_attr *init_attr)
{
	struct rvt_qp *qp = ibqp_to_rvtqp(ibqp);
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);

	attr->qp_state = qp->state;
	attr->cur_qp_state = attr->qp_state;
	attr->path_mtu = qp->path_mtu;
	attr->path_mig_state = qp->s_mig_state;
	attr->qkey = qp->qkey;
	attr->rq_psn = qp->r_psn & rdi->dparms.psn_mask;
	attr->sq_psn = qp->s_next_psn & rdi->dparms.psn_mask;
	attr->dest_qp_num = qp->remote_qpn;
	attr->qp_access_flags = qp->qp_access_flags;
	attr->cap.max_send_wr = qp->s_size - 1 -
		rdi->dparms.reserved_operations;
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
	attr->alt_port_num =
		rdma_ah_get_port_num(&qp->alt_ah_attr);
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
 * rvt_post_receive - post a receive on a QP
 * @ibqp: the QP to post the receive on
 * @wr: the WR to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 *
 * Return: 0 on success otherwise errno
 */
int rvt_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		  struct ib_recv_wr **bad_wr)
{
	struct rvt_qp *qp = ibqp_to_rvtqp(ibqp);
	struct rvt_rwq *wq = qp->r_rq.wq;
	unsigned long flags;
	int qp_err_flush = (ib_rvt_state_ops[qp->state] & RVT_FLUSH_RECV) &&
				!qp->ibqp.srq;

	/* Check that state is OK to post receive. */
	if (!(ib_rvt_state_ops[qp->state] & RVT_POST_RECV_OK) || !wq) {
		*bad_wr = wr;
		return -EINVAL;
	}

	for (; wr; wr = wr->next) {
		struct rvt_rwqe *wqe;
		u32 next;
		int i;

		if ((unsigned)wr->num_sge > qp->r_rq.max_sge) {
			*bad_wr = wr;
			return -EINVAL;
		}

		spin_lock_irqsave(&qp->r_rq.lock, flags);
		next = wq->head + 1;
		if (next >= qp->r_rq.size)
			next = 0;
		if (next == wq->tail) {
			spin_unlock_irqrestore(&qp->r_rq.lock, flags);
			*bad_wr = wr;
			return -ENOMEM;
		}
		if (unlikely(qp_err_flush)) {
			struct ib_wc wc;

			memset(&wc, 0, sizeof(wc));
			wc.qp = &qp->ibqp;
			wc.opcode = IB_WC_RECV;
			wc.wr_id = wr->wr_id;
			wc.status = IB_WC_WR_FLUSH_ERR;
			rvt_cq_enter(ibcq_to_rvtcq(qp->ibqp.recv_cq), &wc, 1);
		} else {
			wqe = rvt_get_rwqe_ptr(&qp->r_rq, wq->head);
			wqe->wr_id = wr->wr_id;
			wqe->num_sge = wr->num_sge;
			for (i = 0; i < wr->num_sge; i++)
				wqe->sg_list[i] = wr->sg_list[i];
			/*
			 * Make sure queue entry is written
			 * before the head index.
			 */
			smp_wmb();
			wq->head = next;
		}
		spin_unlock_irqrestore(&qp->r_rq.lock, flags);
	}
	return 0;
}

/**
 * rvt_qp_valid_operation - validate post send wr request
 * @qp - the qp
 * @post-parms - the post send table for the driver
 * @wr - the work request
 *
 * The routine validates the operation based on the
 * validation table an returns the length of the operation
 * which can extend beyond the ib_send_bw.  Operation
 * dependent flags key atomic operation validation.
 *
 * There is an exception for UD qps that validates the pd and
 * overrides the length to include the additional UD specific
 * length.
 *
 * Returns a negative error or the length of the work request
 * for building the swqe.
 */
static inline int rvt_qp_valid_operation(
	struct rvt_qp *qp,
	const struct rvt_operation_params *post_parms,
	struct ib_send_wr *wr)
{
	int len;

	if (wr->opcode >= RVT_OPERATION_MAX || !post_parms[wr->opcode].length)
		return -EINVAL;
	if (!(post_parms[wr->opcode].qpt_support & BIT(qp->ibqp.qp_type)))
		return -EINVAL;
	if ((post_parms[wr->opcode].flags & RVT_OPERATION_PRIV) &&
	    ibpd_to_rvtpd(qp->ibqp.pd)->user)
		return -EINVAL;
	if (post_parms[wr->opcode].flags & RVT_OPERATION_ATOMIC_SGE &&
	    (wr->num_sge == 0 ||
	     wr->sg_list[0].length < sizeof(u64) ||
	     wr->sg_list[0].addr & (sizeof(u64) - 1)))
		return -EINVAL;
	if (post_parms[wr->opcode].flags & RVT_OPERATION_ATOMIC &&
	    !qp->s_max_rd_atomic)
		return -EINVAL;
	len = post_parms[wr->opcode].length;
	/* UD specific */
	if (qp->ibqp.qp_type != IB_QPT_UC &&
	    qp->ibqp.qp_type != IB_QPT_RC) {
		if (qp->ibqp.pd != ud_wr(wr)->ah->pd)
			return -EINVAL;
		len = sizeof(struct ib_ud_wr);
	}
	return len;
}

/**
 * rvt_qp_is_avail - determine queue capacity
 * @qp - the qp
 * @rdi - the rdmavt device
 * @reserved_op - is reserved operation
 *
 * This assumes the s_hlock is held but the s_last
 * qp variable is uncontrolled.
 *
 * For non reserved operations, the qp->s_avail
 * may be changed.
 *
 * The return value is zero or a -ENOMEM.
 */
static inline int rvt_qp_is_avail(
	struct rvt_qp *qp,
	struct rvt_dev_info *rdi,
	bool reserved_op)
{
	u32 slast;
	u32 avail;
	u32 reserved_used;

	/* see rvt_qp_wqe_unreserve() */
	smp_mb__before_atomic();
	reserved_used = atomic_read(&qp->s_reserved_used);
	if (unlikely(reserved_op)) {
		/* see rvt_qp_wqe_unreserve() */
		smp_mb__before_atomic();
		if (reserved_used >= rdi->dparms.reserved_operations)
			return -ENOMEM;
		return 0;
	}
	/* non-reserved operations */
	if (likely(qp->s_avail))
		return 0;
	smp_read_barrier_depends(); /* see rc.c */
	slast = ACCESS_ONCE(qp->s_last);
	if (qp->s_head >= slast)
		avail = qp->s_size - (qp->s_head - slast);
	else
		avail = slast - qp->s_head;

	/* see rvt_qp_wqe_unreserve() */
	smp_mb__before_atomic();
	reserved_used = atomic_read(&qp->s_reserved_used);
	avail =  avail - 1 -
		(rdi->dparms.reserved_operations - reserved_used);
	/* insure we don't assign a negative s_avail */
	if ((s32)avail <= 0)
		return -ENOMEM;
	qp->s_avail = avail;
	if (WARN_ON(qp->s_avail >
		    (qp->s_size - 1 - rdi->dparms.reserved_operations)))
		rvt_pr_err(rdi,
			   "More avail entries than QP RB size.\nQP: %u, size: %u, avail: %u\nhead: %u, tail: %u, cur: %u, acked: %u, last: %u",
			   qp->ibqp.qp_num, qp->s_size, qp->s_avail,
			   qp->s_head, qp->s_tail, qp->s_cur,
			   qp->s_acked, qp->s_last);
	return 0;
}

/**
 * rvt_post_one_wr - post one RC, UC, or UD send work request
 * @qp: the QP to post on
 * @wr: the work request to send
 */
static int rvt_post_one_wr(struct rvt_qp *qp,
			   struct ib_send_wr *wr,
			   int *call_send)
{
	struct rvt_swqe *wqe;
	u32 next;
	int i;
	int j;
	int acc;
	struct rvt_lkey_table *rkt;
	struct rvt_pd *pd;
	struct rvt_dev_info *rdi = ib_to_rvt(qp->ibqp.device);
	u8 log_pmtu;
	int ret, incr;
	size_t cplen;
	bool reserved_op;
	int local_ops_delayed = 0;

	BUILD_BUG_ON(IB_QPT_MAX >= (sizeof(u32) * BITS_PER_BYTE));

	/* IB spec says that num_sge == 0 is OK. */
	if (unlikely(wr->num_sge > qp->s_max_sge))
		return -EINVAL;

	ret = rvt_qp_valid_operation(qp, rdi->post_parms, wr);
	if (ret < 0)
		return ret;
	cplen = ret;

	/*
	 * Local operations include fast register and local invalidate.
	 * Fast register needs to be processed immediately because the
	 * registered lkey may be used by following work requests and the
	 * lkey needs to be valid at the time those requests are posted.
	 * Local invalidate can be processed immediately if fencing is
	 * not required and no previous local invalidate ops are pending.
	 * Signaled local operations that have been processed immediately
	 * need to have requests with "completion only" flags set posted
	 * to the send queue in order to generate completions.
	 */
	if ((rdi->post_parms[wr->opcode].flags & RVT_OPERATION_LOCAL)) {
		switch (wr->opcode) {
		case IB_WR_REG_MR:
			ret = rvt_fast_reg_mr(qp,
					      reg_wr(wr)->mr,
					      reg_wr(wr)->key,
					      reg_wr(wr)->access);
			if (ret || !(wr->send_flags & IB_SEND_SIGNALED))
				return ret;
			break;
		case IB_WR_LOCAL_INV:
			if ((wr->send_flags & IB_SEND_FENCE) ||
			    atomic_read(&qp->local_ops_pending)) {
				local_ops_delayed = 1;
			} else {
				ret = rvt_invalidate_rkey(
					qp, wr->ex.invalidate_rkey);
				if (ret || !(wr->send_flags & IB_SEND_SIGNALED))
					return ret;
			}
			break;
		default:
			return -EINVAL;
		}
	}

	reserved_op = rdi->post_parms[wr->opcode].flags &
			RVT_OPERATION_USE_RESERVE;
	/* check for avail */
	ret = rvt_qp_is_avail(qp, rdi, reserved_op);
	if (ret)
		return ret;
	next = qp->s_head + 1;
	if (next >= qp->s_size)
		next = 0;

	rkt = &rdi->lkey_table;
	pd = ibpd_to_rvtpd(qp->ibqp.pd);
	wqe = rvt_get_swqe_ptr(qp, qp->s_head);

	/* cplen has length from above */
	memcpy(&wqe->wr, wr, cplen);

	wqe->length = 0;
	j = 0;
	if (wr->num_sge) {
		struct rvt_sge *last_sge = NULL;

		acc = wr->opcode >= IB_WR_RDMA_READ ?
			IB_ACCESS_LOCAL_WRITE : 0;
		for (i = 0; i < wr->num_sge; i++) {
			u32 length = wr->sg_list[i].length;

			if (length == 0)
				continue;
			incr = rvt_lkey_ok(rkt, pd, &wqe->sg_list[j], last_sge,
					   &wr->sg_list[i], acc);
			if (unlikely(incr < 0))
				goto bail_lkey_error;
			wqe->length += length;
			if (incr)
				last_sge = &wqe->sg_list[j];
			j += incr;
		}
		wqe->wr.num_sge = j;
	}

	/* general part of wqe valid - allow for driver checks */
	if (rdi->driver_f.check_send_wqe) {
		ret = rdi->driver_f.check_send_wqe(qp, wqe);
		if (ret < 0)
			goto bail_inval_free;
		if (ret)
			*call_send = ret;
	}

	log_pmtu = qp->log_pmtu;
	if (qp->ibqp.qp_type != IB_QPT_UC &&
	    qp->ibqp.qp_type != IB_QPT_RC) {
		struct rvt_ah *ah = ibah_to_rvtah(wqe->ud_wr.ah);

		log_pmtu = ah->log_pmtu;
		atomic_inc(&ibah_to_rvtah(ud_wr(wr)->ah)->refcount);
	}

	if (rdi->post_parms[wr->opcode].flags & RVT_OPERATION_LOCAL) {
		if (local_ops_delayed)
			atomic_inc(&qp->local_ops_pending);
		else
			wqe->wr.send_flags |= RVT_SEND_COMPLETION_ONLY;
		wqe->ssn = 0;
		wqe->psn = 0;
		wqe->lpsn = 0;
	} else {
		wqe->ssn = qp->s_ssn++;
		wqe->psn = qp->s_next_psn;
		wqe->lpsn = wqe->psn +
				(wqe->length ?
					((wqe->length - 1) >> log_pmtu) :
					0);
		qp->s_next_psn = wqe->lpsn + 1;
	}
	if (unlikely(reserved_op)) {
		wqe->wr.send_flags |= RVT_SEND_RESERVE_USED;
		rvt_qp_wqe_reserve(qp, wqe);
	} else {
		wqe->wr.send_flags &= ~RVT_SEND_RESERVE_USED;
		qp->s_avail--;
	}
	trace_rvt_post_one_wr(qp, wqe, wr->num_sge);
	smp_wmb(); /* see request builders */
	qp->s_head = next;

	return 0;

bail_lkey_error:
	ret = incr;
bail_inval_free:
	/* release mr holds */
	while (j) {
		struct rvt_sge *sge = &wqe->sg_list[--j];

		rvt_put_mr(sge->mr);
	}
	return ret;
}

/**
 * rvt_post_send - post a send on a QP
 * @ibqp: the QP to post the send on
 * @wr: the list of work requests to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 *
 * Return: 0 on success else errno
 */
int rvt_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		  struct ib_send_wr **bad_wr)
{
	struct rvt_qp *qp = ibqp_to_rvtqp(ibqp);
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);
	unsigned long flags = 0;
	int call_send;
	unsigned nreq = 0;
	int err = 0;

	spin_lock_irqsave(&qp->s_hlock, flags);

	/*
	 * Ensure QP state is such that we can send. If not bail out early,
	 * there is no need to do this every time we post a send.
	 */
	if (unlikely(!(ib_rvt_state_ops[qp->state] & RVT_POST_SEND_OK))) {
		spin_unlock_irqrestore(&qp->s_hlock, flags);
		return -EINVAL;
	}

	/*
	 * If the send queue is empty, and we only have a single WR then just go
	 * ahead and kick the send engine into gear. Otherwise we will always
	 * just schedule the send to happen later.
	 */
	call_send = qp->s_head == ACCESS_ONCE(qp->s_last) && !wr->next;

	for (; wr; wr = wr->next) {
		err = rvt_post_one_wr(qp, wr, &call_send);
		if (unlikely(err)) {
			*bad_wr = wr;
			goto bail;
		}
		nreq++;
	}
bail:
	spin_unlock_irqrestore(&qp->s_hlock, flags);
	if (nreq) {
		if (call_send)
			rdi->driver_f.do_send(qp);
		else
			rdi->driver_f.schedule_send_no_lock(qp);
	}
	return err;
}

/**
 * rvt_post_srq_receive - post a receive on a shared receive queue
 * @ibsrq: the SRQ to post the receive on
 * @wr: the list of work requests to post
 * @bad_wr: A pointer to the first WR to cause a problem is put here
 *
 * This may be called from interrupt context.
 *
 * Return: 0 on success else errno
 */
int rvt_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr)
{
	struct rvt_srq *srq = ibsrq_to_rvtsrq(ibsrq);
	struct rvt_rwq *wq;
	unsigned long flags;

	for (; wr; wr = wr->next) {
		struct rvt_rwqe *wqe;
		u32 next;
		int i;

		if ((unsigned)wr->num_sge > srq->rq.max_sge) {
			*bad_wr = wr;
			return -EINVAL;
		}

		spin_lock_irqsave(&srq->rq.lock, flags);
		wq = srq->rq.wq;
		next = wq->head + 1;
		if (next >= srq->rq.size)
			next = 0;
		if (next == wq->tail) {
			spin_unlock_irqrestore(&srq->rq.lock, flags);
			*bad_wr = wr;
			return -ENOMEM;
		}

		wqe = rvt_get_rwqe_ptr(&srq->rq, wq->head);
		wqe->wr_id = wr->wr_id;
		wqe->num_sge = wr->num_sge;
		for (i = 0; i < wr->num_sge; i++)
			wqe->sg_list[i] = wr->sg_list[i];
		/* Make sure queue entry is written before the head index. */
		smp_wmb();
		wq->head = next;
		spin_unlock_irqrestore(&srq->rq.lock, flags);
	}
	return 0;
}

/**
 * qp_comm_est - handle trap with QP established
 * @qp: the QP
 */
void rvt_comm_est(struct rvt_qp *qp)
{
	qp->r_flags |= RVT_R_COMM_EST;
	if (qp->ibqp.event_handler) {
		struct ib_event ev;

		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_COMM_EST;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
}
EXPORT_SYMBOL(rvt_comm_est);

void rvt_rc_error(struct rvt_qp *qp, enum ib_wc_status err)
{
	unsigned long flags;
	int lastwqe;

	spin_lock_irqsave(&qp->s_lock, flags);
	lastwqe = rvt_error_qp(qp, err);
	spin_unlock_irqrestore(&qp->s_lock, flags);

	if (lastwqe) {
		struct ib_event ev;

		ev.device = qp->ibqp.device;
		ev.element.qp = &qp->ibqp;
		ev.event = IB_EVENT_QP_LAST_WQE_REACHED;
		qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
	}
}
EXPORT_SYMBOL(rvt_rc_error);

/*
 *  rvt_rnr_tbl_to_usec - return index into ib_rvt_rnr_table
 *  @index - the index
 *  return usec from an index into ib_rvt_rnr_table
 */
unsigned long rvt_rnr_tbl_to_usec(u32 index)
{
	return ib_rvt_rnr_table[(index & IB_AETH_CREDIT_MASK)];
}
EXPORT_SYMBOL(rvt_rnr_tbl_to_usec);

static inline unsigned long rvt_aeth_to_usec(u32 aeth)
{
	return ib_rvt_rnr_table[(aeth >> IB_AETH_CREDIT_SHIFT) &
				  IB_AETH_CREDIT_MASK];
}

/*
 *  rvt_add_retry_timer - add/start a retry timer
 *  @qp - the QP
 *  add a retry timer on the QP
 */
void rvt_add_retry_timer(struct rvt_qp *qp)
{
	struct ib_qp *ibqp = &qp->ibqp;
	struct rvt_dev_info *rdi = ib_to_rvt(ibqp->device);

	lockdep_assert_held(&qp->s_lock);
	qp->s_flags |= RVT_S_TIMER;
       /* 4.096 usec. * (1 << qp->timeout) */
	qp->s_timer.expires = jiffies + qp->timeout_jiffies +
			     rdi->busy_jiffies;
	add_timer(&qp->s_timer);
}
EXPORT_SYMBOL(rvt_add_retry_timer);

/**
 * rvt_add_rnr_timer - add/start an rnr timer
 * @qp - the QP
 * @aeth - aeth of RNR timeout, simulated aeth for loopback
 * add an rnr timer on the QP
 */
void rvt_add_rnr_timer(struct rvt_qp *qp, u32 aeth)
{
	u32 to;

	lockdep_assert_held(&qp->s_lock);
	qp->s_flags |= RVT_S_WAIT_RNR;
	to = rvt_aeth_to_usec(aeth);
	hrtimer_start(&qp->s_rnr_timer,
		      ns_to_ktime(1000 * to), HRTIMER_MODE_REL);
}
EXPORT_SYMBOL(rvt_add_rnr_timer);

/**
 * rvt_stop_rc_timers - stop all timers
 * @qp - the QP
 * stop any pending timers
 */
void rvt_stop_rc_timers(struct rvt_qp *qp)
{
	lockdep_assert_held(&qp->s_lock);
	/* Remove QP from all timers */
	if (qp->s_flags & (RVT_S_TIMER | RVT_S_WAIT_RNR)) {
		qp->s_flags &= ~(RVT_S_TIMER | RVT_S_WAIT_RNR);
		del_timer(&qp->s_timer);
		hrtimer_try_to_cancel(&qp->s_rnr_timer);
	}
}
EXPORT_SYMBOL(rvt_stop_rc_timers);

/**
 * rvt_stop_rnr_timer - stop an rnr timer
 * @qp - the QP
 *
 * stop an rnr timer and return if the timer
 * had been pending.
 */
static int rvt_stop_rnr_timer(struct rvt_qp *qp)
{
	int rval = 0;

	lockdep_assert_held(&qp->s_lock);
	/* Remove QP from rnr timer */
	if (qp->s_flags & RVT_S_WAIT_RNR) {
		qp->s_flags &= ~RVT_S_WAIT_RNR;
		rval = hrtimer_try_to_cancel(&qp->s_rnr_timer);
	}
	return rval;
}

/**
 * rvt_del_timers_sync - wait for any timeout routines to exit
 * @qp - the QP
 */
void rvt_del_timers_sync(struct rvt_qp *qp)
{
	del_timer_sync(&qp->s_timer);
	hrtimer_cancel(&qp->s_rnr_timer);
}
EXPORT_SYMBOL(rvt_del_timers_sync);

/**
 * This is called from s_timer for missing responses.
 */
static void rvt_rc_timeout(unsigned long arg)
{
	struct rvt_qp *qp = (struct rvt_qp *)arg;
	struct rvt_dev_info *rdi = ib_to_rvt(qp->ibqp.device);
	unsigned long flags;

	spin_lock_irqsave(&qp->r_lock, flags);
	spin_lock(&qp->s_lock);
	if (qp->s_flags & RVT_S_TIMER) {
		struct rvt_ibport *rvp = rdi->ports[qp->port_num - 1];

		qp->s_flags &= ~RVT_S_TIMER;
		rvp->n_rc_timeouts++;
		del_timer(&qp->s_timer);
		trace_rvt_rc_timeout(qp, qp->s_last_psn + 1);
		if (rdi->driver_f.notify_restart_rc)
			rdi->driver_f.notify_restart_rc(qp,
							qp->s_last_psn + 1,
							1);
		rdi->driver_f.schedule_send(qp);
	}
	spin_unlock(&qp->s_lock);
	spin_unlock_irqrestore(&qp->r_lock, flags);
}

/*
 * This is called from s_timer for RNR timeouts.
 */
enum hrtimer_restart rvt_rc_rnr_retry(struct hrtimer *t)
{
	struct rvt_qp *qp = container_of(t, struct rvt_qp, s_rnr_timer);
	struct rvt_dev_info *rdi = ib_to_rvt(qp->ibqp.device);
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	rvt_stop_rnr_timer(qp);
	rdi->driver_f.schedule_send(qp);
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return HRTIMER_NORESTART;
}
EXPORT_SYMBOL(rvt_rc_rnr_retry);
