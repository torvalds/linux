/*
 * Copyright(c) 2015 Intel Corporation.
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

#include <linux/bitops.h>
#include <linux/lockdep.h>
#include "vt.h"
#include "qp.h"

static void get_map_page(struct rvt_qpn_table *qpt, struct rvt_qpn_map *map)
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

	if (!(rdi->dparms.qpn_res_end > rdi->dparms.qpn_res_start))
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
	for (i = rdi->dparms.qpn_res_start; i < rdi->dparms.qpn_res_end; i++) {
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

int rvt_driver_qp_init(struct rvt_dev_info *rdi)
{
	int i;
	int ret = -ENOMEM;

	if (rdi->flags & RVT_FLAG_QP_INIT_DRIVER) {
		rvt_pr_info(rdi, "Driver is doing QP init.\n");
		return 0;
	}

	if (!rdi->dparms.qp_table_size)
		return -EINVAL;

	/*
	 * If driver is not doing any QP allocation then make sure it is
	 * providing the necessary QP functions.
	 */
	if (!rdi->driver_f.free_all_qps)
		return -EINVAL;

	/* allocate parent object */
	rdi->qp_dev = kzalloc(sizeof(*rdi->qp_dev), GFP_KERNEL);
	if (!rdi->qp_dev)
		return -ENOMEM;

	/* allocate hash table */
	rdi->qp_dev->qp_table_size = rdi->dparms.qp_table_size;
	rdi->qp_dev->qp_table_bits = ilog2(rdi->dparms.qp_table_size);
	rdi->qp_dev->qp_table =
		kmalloc(rdi->qp_dev->qp_table_size *
			sizeof(*rdi->qp_dev->qp_table),
			GFP_KERNEL);
	if (!rdi->qp_dev->qp_table)
		goto no_qp_table;

	for (i = 0; i < rdi->qp_dev->qp_table_size; i++)
		RCU_INIT_POINTER(rdi->qp_dev->qp_table[i], NULL);

	spin_lock_init(&rdi->qp_dev->qpt_lock);

	/* initialize qpn map */
	if (init_qpn_table(rdi, &rdi->qp_dev->qpn_table))
		goto fail_table;

	return ret;

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
static unsigned free_all_qps(struct rvt_dev_info *rdi)
{
	unsigned long flags;
	struct rvt_qp *qp;
	unsigned n, qp_inuse = 0;
	spinlock_t *ql; /* work around too long line below */

	rdi->driver_f.free_all_qps(rdi);

	if (!rdi->qp_dev)
		return 0;

	ql = &rdi->qp_dev->qpt_lock;
	spin_lock_irqsave(&rdi->qp_dev->qpt_lock, flags);
	for (n = 0; n < rdi->qp_dev->qp_table_size; n++) {
		qp = rcu_dereference_protected(rdi->qp_dev->qp_table[n],
					       lockdep_is_held(ql));
		RCU_INIT_POINTER(rdi->qp_dev->qp_table[n], NULL);
		qp =  rcu_dereference_protected(qp->next,
						lockdep_is_held(ql));
		while (qp) {
			qp_inuse++;
			qp =  rcu_dereference_protected(qp->next,
							lockdep_is_held(ql));
		}
	}
	spin_unlock_irqrestore(ql, flags);
	synchronize_rcu();
	return qp_inuse;
}

void rvt_qp_exit(struct rvt_dev_info *rdi)
{
	u32 qps_inuse = free_all_qps(rdi);

	qps_inuse = free_all_qps(rdi);
	if (qps_inuse)
		rvt_pr_err(rdi, "QP memory leak! %u still in use\n",
			   qps_inuse);
	if (!rdi->qp_dev)
		return;

	kfree(rdi->qp_dev->qp_table);
	free_qpn_table(&rdi->qp_dev->qpn_table);
	kfree(rdi->qp_dev);
}

/**
 * rvt_create_qp - create a queue pair for a device
 * @ibpd: the protection domain who's device we create the queue pair for
 * @init_attr: the attributes of the queue pair
 * @udata: user data for libibverbs.so
 *
 * Returns the queue pair on success, otherwise returns an errno.
 *
 * Called by the ib_create_qp() core verbs function.
 */
struct ib_qp *rvt_create_qp(struct ib_pd *ibpd,
			    struct ib_qp_init_attr *init_attr,
			    struct ib_udata *udata)
{
	/*
	 * Queue pair creation is mostly an rvt issue. However, drivers have
	 * their own unique idea of what queue pare numbers mean. For instance
	 * there is a reserved range for PSM.
	 *
	 * VI-DRIVER-API: make_qpn()
	 * Returns a valid QPN for verbs to use
	 */
	return ERR_PTR(-EOPNOTSUPP);
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
int rvt_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		  int attr_mask, struct ib_udata *udata)
{
	/*
	 * VT-DRIVER-API: qp_mtu()
	 * OPA devices have a per VL MTU the driver has a mapping of IB SL to SC
	 * to VL and the mapping table of MTUs per VL. This is not something
	 * that IB has and should not live in the rvt.
	 */
	return -EOPNOTSUPP;
}

/**
 * rvt_destroy_qp - destroy a queue pair
 * @ibqp: the queue pair to destroy
 *
 * Returns 0 on success.
 *
 * Note that this can be called while the QP is actively sending or
 * receiving!
 */
int rvt_destroy_qp(struct ib_qp *ibqp)
{
	/*
	 * VT-DRIVER-API: qp_flush()
	 * Driver provies a mechanism to flush and wait for that flush to
	 * finish.
	 */

	return -EOPNOTSUPP;
}

int rvt_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		 int attr_mask, struct ib_qp_init_attr *init_attr)
{
	return -EOPNOTSUPP;
}

/**
 * rvt_post_receive - post a receive on a QP
 * @ibqp: the QP to post the receive on
 * @wr: the WR to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 */
int rvt_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		  struct ib_recv_wr **bad_wr)
{
	/*
	 * When a packet arrives the driver needs to call up to rvt to process
	 * the packet. The UD, RC, UC processing will be done in rvt, however
	 * the driver should be able to override this if it so choses. Perhaps a
	 * set of function pointers set up at registration time.
	 */

	return -EOPNOTSUPP;
}

/**
 * rvt_post_send - post a send on a QP
 * @ibqp: the QP to post the send on
 * @wr: the list of work requests to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 */
int rvt_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		  struct ib_send_wr **bad_wr)
{
	/*
	 * VT-DRIVER-API: do_send()
	 * Driver needs to have a do_send() call which is a single entry point
	 * to take an already formed packet and throw it out on the wire. Once
	 * the packet is sent the driver needs to make an upcall to rvt so the
	 * completion queue can be notified and/or any other outstanding
	 * work/book keeping can be finished.
	 *
	 * Note that there should also be a way for rvt to protect itself
	 * against hangs in the driver layer. If a send doesn't actually
	 * complete in a timely manor rvt needs to return an error event.
	 */

	return -EOPNOTSUPP;
}

/**
 * rvt_post_srq_receive - post a receive on a shared receive queue
 * @ibsrq: the SRQ to post the receive on
 * @wr: the list of work requests to post
 * @bad_wr: A pointer to the first WR to cause a problem is put here
 *
 * This may be called from interrupt context.
 */
int rvt_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr)
{
	return -EOPNOTSUPP;
}
