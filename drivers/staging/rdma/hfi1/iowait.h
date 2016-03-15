#ifndef _HFI1_IOWAIT_H
#define _HFI1_IOWAIT_H
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

#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/sched.h>

/*
 * typedef (*restart_t)() - restart callback
 * @work: pointer to work structure
 */
typedef void (*restart_t)(struct work_struct *work);

struct sdma_txreq;
struct sdma_engine;
/**
 * struct iowait - linkage for delayed progress/waiting
 * @list: used to add/insert into QP/PQ wait lists
 * @tx_head: overflow list of sdma_txreq's
 * @sleep: no space callback
 * @wakeup: space callback
 * @iowork: workqueue overhead
 * @wait_dma: wait for sdma_busy == 0
 * @sdma_busy: # of packets in flight
 * @count: total number of descriptors in tx_head'ed list
 * @tx_limit: limit for overflow queuing
 * @tx_count: number of tx entry's in tx_head'ed list
 *
 * This is to be embedded in user's state structure
 * (QP or PQ).
 *
 * The sleep and wakeup members are a
 * bit misnamed.   They do not strictly
 * speaking sleep or wake up, but they
 * are callbacks for the ULP to implement
 * what ever queuing/dequeuing of
 * the embedded iowait and its containing struct
 * when a resource shortage like SDMA ring space is seen.
 *
 * Both potentially have locks help
 * so sleeping is not allowed.
 *
 * The wait_dma member along with the iow
 */

struct iowait {
	struct list_head list;
	struct list_head tx_head;
	int (*sleep)(
		struct sdma_engine *sde,
		struct iowait *wait,
		struct sdma_txreq *tx,
		unsigned seq);
	void (*wakeup)(struct iowait *wait, int reason);
	struct work_struct iowork;
	wait_queue_head_t wait_dma;
	atomic_t sdma_busy;
	u32 count;
	u32 tx_limit;
	u32 tx_count;
};

#define SDMA_AVAIL_REASON 0

/**
 * iowait_init() - initialize wait structure
 * @wait: wait struct to initialize
 * @tx_limit: limit for overflow queuing
 * @func: restart function for workqueue
 * @sleep: sleep function for no space
 * @wakeup: wakeup function for no space
 *
 * This function initializes the iowait
 * structure embedded in the QP or PQ.
 *
 */

static inline void iowait_init(
	struct iowait *wait,
	u32 tx_limit,
	void (*func)(struct work_struct *work),
	int (*sleep)(
		struct sdma_engine *sde,
		struct iowait *wait,
		struct sdma_txreq *tx,
		unsigned seq),
	void (*wakeup)(struct iowait *wait, int reason))
{
	wait->count = 0;
	INIT_LIST_HEAD(&wait->list);
	INIT_LIST_HEAD(&wait->tx_head);
	INIT_WORK(&wait->iowork, func);
	init_waitqueue_head(&wait->wait_dma);
	atomic_set(&wait->sdma_busy, 0);
	wait->tx_limit = tx_limit;
	wait->sleep = sleep;
	wait->wakeup = wakeup;
}

/**
 * iowait_schedule() - initialize wait structure
 * @wait: wait struct to schedule
 * @wq: workqueue for schedule
 * @cpu: cpu
 */
static inline void iowait_schedule(
	struct iowait *wait,
	struct workqueue_struct *wq,
	int cpu)
{
	queue_work_on(cpu, wq, &wait->iowork);
}

/**
 * iowait_sdma_drain() - wait for DMAs to drain
 *
 * @wait: iowait structure
 *
 * This will delay until the iowait sdmas have
 * completed.
 */
static inline void iowait_sdma_drain(struct iowait *wait)
{
	wait_event(wait->wait_dma, !atomic_read(&wait->sdma_busy));
}

/**
 * iowait_drain_wakeup() - trigger iowait_drain() waiter
 *
 * @wait: iowait structure
 *
 * This will trigger any waiters.
 */
static inline void iowait_drain_wakeup(struct iowait *wait)
{
	wake_up(&wait->wait_dma);
}

#endif
