/* cyanblkdev_queue.h - Antioch Linux Block Driver queue source file
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor,
## Boston, MA  02110-1301, USA.
## ===========================
*/

/*
 * Request queue handling for Antioch block device driver.
 * Based on the mmc queue handling code by Russell King in the
 * linux 2.6.10 kernel.
 */

/*
 *  linux/drivers/mmc/mmc_queue.c
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/blkdev.h>

#include "cyasblkdev_queue.h"

#define CYASBLKDEV_QUEUE_EXIT		(1 << 0)
#define CYASBLKDEV_QUEUE_SUSPENDED	(1 << 1)
#define CY_AS_USE_ASYNC_API



/* print flags by name */
const char *rq_flag_bit_names[] = {
	"REQ_RW",			/* not set, read. set, write */
	"REQ_FAILFAST",		/* no low level driver retries */
	"REQ_SORTED",		/* elevator knows about this request */
	"REQ_SOFTBARRIER",	/* may not be passed by ioscheduler */
	"REQ_HARDBARRIER",	/* may not be passed by drive either */
	"REQ_FUA",			/* forced unit access */
	"REQ_NOMERGE",		/* don't touch this for merging */
	"REQ_STARTED",		/* drive already may have started this one */
	"REQ_DONTPREP",		/* don't call prep for this one */
	"REQ_QUEUED",		/* uses queueing */
	"REQ_ELVPRIV",		/* elevator private data attached */
	"REQ_FAILED",		/* set if the request failed */
	"REQ_QUIET",		/* don't worry about errors */
	"REQ_PREEMPT",		/* set for "ide_preempt" requests */
	"REQ_ORDERED_COLOR",/* is before or after barrier */
	"REQ_RW_SYNC",		/* request is sync (O_DIRECT) */
	"REQ_ALLOCED",		/* request came from our alloc pool */
	"REQ_RW_META",		/* metadata io request */
	"REQ_COPY_USER",	/* contains copies of user pages */
	"REQ_NR_BITS",		/* stops here */
};

void verbose_rq_flags(int flags)
{
	int i;
	uint32_t j;
	j = 1;
	for (i = 0; i < 32; i++) {
		if (flags & j)
			DBGPRN("<1>%s", rq_flag_bit_names[i]);
		j = j << 1;
	}
}


/*
 * Prepare a -BLK_DEV  request.  Essentially, this means passing the
 * preparation off to the media driver.  The media driver will
 * create request to CyAsDev.
 */
static int cyasblkdev_prep_request(
	struct request_queue *q, struct request *req)
{
	DBGPRN_FUNC_NAME;

	/* we only like normal block requests.*/
	if (req->cmd_type != REQ_TYPE_FS && !(req->cmd_flags & REQ_DISCARD)) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s:%x bad request received\n",
			__func__, current->pid);
		#endif

		blk_dump_rq_flags(req, "cyasblkdev bad request");
		return BLKPREP_KILL;
	}

	req->cmd_flags |= REQ_DONTPREP;

	return BLKPREP_OK;
}

/* queue worker thread */
static int cyasblkdev_queue_thread(void *d)
{
	DECLARE_WAITQUEUE(wait, current);
	struct cyasblkdev_queue *bq = d;
	struct request_queue *q = bq->queue;
	u32 qth_pid;

	DBGPRN_FUNC_NAME;

	/*
	 * set iothread to ensure that we aren't put to sleep by
	 * the process freezing.  we handle suspension ourselves.
	 */
	daemonize("cyasblkdev_queue_thread");

	/* signal to queue_init() so it could contnue */
	complete(&bq->thread_complete);

	down(&bq->thread_sem);
	add_wait_queue(&bq->thread_wq, &wait);

	qth_pid = current->pid;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message(
		"%s:%x started, bq:%p, q:%p\n", __func__, qth_pid, bq, q);
	#endif

	do {
		struct request *req = NULL;

		/* the thread wants to be woken up by signals as well */
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irq(q->queue_lock);

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: for bq->queue is null\n", __func__);
		#endif

		if (!bq->req) {
			/* chk if queue is plugged */
			if (!blk_queue_plugged(q)) {
				bq->req = req = blk_fetch_request(q);
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"%s: blk_fetch_request:%x\n",
					__func__, (uint32_t)req);
				#endif
			} else {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"%s: queue plugged, "
					"skip blk_fetch()\n", __func__);
				#endif
			}
		}
		spin_unlock_irq(q->queue_lock);

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: checking if request queue is null\n", __func__);
		#endif

		if (!req) {
			if (bq->flags & CYASBLKDEV_QUEUE_EXIT) {
				#ifndef WESTBRIDGE_NDEBUG
				cy_as_hal_print_message(
					"%s:got QUEUE_EXIT flag\n", __func__);
				#endif

				break;
			}

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"%s: request queue is null, goto sleep, "
				"thread_sem->count=%d\n",
				__func__, bq->thread_sem.count);
			if (spin_is_locked(q->queue_lock)) {
				cy_as_hal_print_message("%s: queue_lock "
				"is locked, need to release\n", __func__);
				spin_unlock(q->queue_lock);

				if (spin_is_locked(q->queue_lock))
					cy_as_hal_print_message(
						"%s: unlock did not work\n",
						__func__);
			} else {
				cy_as_hal_print_message(
					"%s: checked lock, is not locked\n",
					__func__);
			}
			#endif

			up(&bq->thread_sem);

			/* yields to the next rdytorun proc,
			 * then goes back to sleep*/
			schedule();
			down(&bq->thread_sem);

			#ifndef WESTBRIDGE_NDEBUG
			cy_as_hal_print_message(
				"%s: wake_up,continue\n",
				__func__);
			#endif
			continue;
		}

		/* new req recieved, issue it to the driver  */
		set_current_state(TASK_RUNNING);

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: issued a RQ:%x\n",
			__func__, (uint32_t)req);
		#endif

		bq->issue_fn(bq, req);

		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message(
			"%s: bq->issue_fn() returned\n",
			__func__);
		#endif


	} while (1);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&bq->thread_wq, &wait);
	up(&bq->thread_sem);

	complete_and_exit(&bq->thread_complete, 0);

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message("%s: is finished\n", __func__);
	#endif

	return 0;
}

/*
 * Generic request handler. it is called for any queue on a
 * particular host.  When the host is not busy, we look for a request
 * on any queue on this host, and attempt to issue it.  This may
 * not be the queue we were asked to process.
 */
static void cyasblkdev_request(struct request_queue *q)
{
	struct cyasblkdev_queue *bq = q->queuedata;
	DBGPRN_FUNC_NAME;

	#ifndef WESTBRIDGE_NDEBUG
	cy_as_hal_print_message(
		"%s new request on cyasblkdev_queue_t bq:=%x\n",
		__func__, (uint32_t)bq);
	#endif

	if (!bq->req) {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s wake_up(&bq->thread_wq)\n",
			__func__);
		#endif

		/* wake up cyasblkdev_queue worker thread*/
		wake_up(&bq->thread_wq);
	} else {
		#ifndef WESTBRIDGE_NDEBUG
		cy_as_hal_print_message("%s: don't wake Q_thr, bq->req:%x\n",
			__func__, (uint32_t)bq->req);
		#endif
	}
}

/*
 * cyasblkdev_init_queue - initialise a queue structure.
 * @bq: cyasblkdev queue
 * @dev:  CyAsDeviceHandle to attach this queue
 * @lock: queue lock
 *
 * Initialise a cyasblkdev_request queue.
 */

/* MAX NUMBER OF SECTORS PER REQUEST **/
#define Q_MAX_SECTORS 128

/* MAX NUMBER OF PHYS SEGMENTS (entries in the SG list)*/
#define Q_MAX_SGS   16

int cyasblkdev_init_queue(struct cyasblkdev_queue *bq, spinlock_t *lock)
{
	int ret;

	DBGPRN_FUNC_NAME;

	/* 1st param is a function that wakes up the queue thread */
	bq->queue = blk_init_queue(cyasblkdev_request, lock);
	if (!bq->queue)
		return -ENOMEM;

	blk_queue_prep_rq(bq->queue, cyasblkdev_prep_request);

	blk_queue_bounce_limit(bq->queue, BLK_BOUNCE_ANY);
	blk_queue_max_hw_sectors(bq->queue, Q_MAX_SECTORS);

	/* As of now, we have the HAL/driver support to
	 * merge scattered segments and handle them simultaneously.
	 * so, setting the max_phys_segments to 8. */
	/*blk_queue_max_phys_segments(bq->queue, Q_MAX_SGS);
	blk_queue_max_hw_segments(bq->queue, Q_MAX_SGS);*/
	blk_queue_max_segments(bq->queue, Q_MAX_SGS);

	/* should be < then HAL can handle */
	blk_queue_max_segment_size(bq->queue, 512*Q_MAX_SECTORS);

	bq->queue->queuedata = bq;
	bq->req = NULL;

	init_completion(&bq->thread_complete);
	init_waitqueue_head(&bq->thread_wq);
	sema_init(&bq->thread_sem, 1);

	ret = kernel_thread(cyasblkdev_queue_thread, bq, CLONE_KERNEL);
	if (ret >= 0) {
		/* wait until the thread is spawned */
		wait_for_completion(&bq->thread_complete);

		/* reinitialize the completion */
		init_completion(&bq->thread_complete);
		ret = 0;
		goto out;
	}

out:
	return ret;
}
EXPORT_SYMBOL(cyasblkdev_init_queue);

/*called from blk_put()  */
void cyasblkdev_cleanup_queue(struct cyasblkdev_queue *bq)
{
	DBGPRN_FUNC_NAME;

	bq->flags |= CYASBLKDEV_QUEUE_EXIT;
	wake_up(&bq->thread_wq);
	wait_for_completion(&bq->thread_complete);

	blk_cleanup_queue(bq->queue);
}
EXPORT_SYMBOL(cyasblkdev_cleanup_queue);


/**
 * cyasblkdev_queue_suspend - suspend a CyAsBlkDev request queue
 * @bq: CyAsBlkDev queue to suspend
 *
 * Stop the block request queue, and wait for our thread to
 * complete any outstanding requests.  This ensures that we
 * won't suspend while a request is being processed.
 */
void cyasblkdev_queue_suspend(struct cyasblkdev_queue *bq)
{
	struct request_queue *q = bq->queue;
	unsigned long flags;

	DBGPRN_FUNC_NAME;

	if (!(bq->flags & CYASBLKDEV_QUEUE_SUSPENDED)) {
		bq->flags |= CYASBLKDEV_QUEUE_SUSPENDED;

		spin_lock_irqsave(q->queue_lock, flags);
		blk_stop_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);

		down(&bq->thread_sem);
	}
}
EXPORT_SYMBOL(cyasblkdev_queue_suspend);

/*cyasblkdev_queue_resume - resume a previously suspended
 * CyAsBlkDev request queue @bq: CyAsBlkDev queue to resume */
void cyasblkdev_queue_resume(struct cyasblkdev_queue *bq)
{
	struct request_queue *q = bq->queue;
	unsigned long flags;

	DBGPRN_FUNC_NAME;

	if (bq->flags & CYASBLKDEV_QUEUE_SUSPENDED)  {
		bq->flags &= ~CYASBLKDEV_QUEUE_SUSPENDED;

		up(&bq->thread_sem);

		spin_lock_irqsave(q->queue_lock, flags);
		blk_start_queue(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}
EXPORT_SYMBOL(cyasblkdev_queue_resume);

/*[]*/
