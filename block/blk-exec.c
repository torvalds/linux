// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to setting various queue properties from drivers
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/sched/sysctl.h>

#include "blk.h"
#include "blk-mq-sched.h"

/**
 * blk_end_sync_rq - executes a completion event on a request
 * @rq: request to complete
 * @error: end I/O status of the request
 */
static void blk_end_sync_rq(struct request *rq, blk_status_t error)
{
	struct completion *waiting = rq->end_io_data;

	rq->end_io_data = (void *)(uintptr_t)error;

	/*
	 * complete last, if this is a stack request the process (and thus
	 * the rq pointer) could be invalid right after this complete()
	 */
	complete(waiting);
}

/**
 * blk_execute_rq_nowait - insert a request to I/O scheduler for execution
 * @bd_disk:	matching gendisk
 * @rq:		request to insert
 * @at_head:    insert request at head or tail of queue
 * @done:	I/O completion handler
 *
 * Description:
 *    Insert a fully prepared request at the back of the I/O scheduler queue
 *    for execution.  Don't wait for completion.
 *
 * Note:
 *    This function will invoke @done directly if the queue is dead.
 */
void blk_execute_rq_nowait(struct gendisk *bd_disk, struct request *rq,
			   int at_head, rq_end_io_fn *done)
{
	WARN_ON(irqs_disabled());
	WARN_ON(!blk_rq_is_passthrough(rq));

	rq->rq_disk = bd_disk;
	rq->end_io = done;

	blk_account_io_start(rq);

	/*
	 * don't check dying flag for MQ because the request won't
	 * be reused after dying flag is set
	 */
	blk_mq_sched_insert_request(rq, at_head, true, false);
}
EXPORT_SYMBOL_GPL(blk_execute_rq_nowait);

static bool blk_rq_is_poll(struct request *rq)
{
	return rq->mq_hctx && rq->mq_hctx->type == HCTX_TYPE_POLL;
}

static void blk_rq_poll_completion(struct request *rq, struct completion *wait)
{
	do {
		blk_poll(rq->q, request_to_qc_t(rq->mq_hctx, rq), true);
		cond_resched();
	} while (!completion_done(wait));
}

/**
 * blk_execute_rq - insert a request into queue for execution
 * @bd_disk:	matching gendisk
 * @rq:		request to insert
 * @at_head:    insert request at head or tail of queue
 *
 * Description:
 *    Insert a fully prepared request at the back of the I/O scheduler queue
 *    for execution and wait for completion.
 * Return: The blk_status_t result provided to blk_mq_end_request().
 */
blk_status_t blk_execute_rq(struct gendisk *bd_disk, struct request *rq, int at_head)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	unsigned long hang_check;

	rq->end_io_data = &wait;
	blk_execute_rq_nowait(bd_disk, rq, at_head, blk_end_sync_rq);

	/* Prevent hang_check timer from firing at us during very long I/O */
	hang_check = sysctl_hung_task_timeout_secs;

	if (blk_rq_is_poll(rq))
		blk_rq_poll_completion(rq, &wait);
	else if (hang_check)
		while (!wait_for_completion_io_timeout(&wait, hang_check * (HZ/2)));
	else
		wait_for_completion_io(&wait);

	return (blk_status_t)(uintptr_t)rq->end_io_data;
}
EXPORT_SYMBOL(blk_execute_rq);
