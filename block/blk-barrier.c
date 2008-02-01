/*
 * Functions related to barrier IO handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#include "blk.h"

/**
 * blk_queue_ordered - does this queue support ordered writes
 * @q:        the request queue
 * @ordered:  one of QUEUE_ORDERED_*
 * @prepare_flush_fn: rq setup helper for cache flush ordered writes
 *
 * Description:
 *   For journalled file systems, doing ordered writes on a commit
 *   block instead of explicitly doing wait_on_buffer (which is bad
 *   for performance) can be a big win. Block drivers supporting this
 *   feature should call this function and indicate so.
 *
 **/
int blk_queue_ordered(struct request_queue *q, unsigned ordered,
		      prepare_flush_fn *prepare_flush_fn)
{
	if (ordered & (QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_POSTFLUSH) &&
	    prepare_flush_fn == NULL) {
		printk(KERN_ERR "%s: prepare_flush_fn required\n",
								__FUNCTION__);
		return -EINVAL;
	}

	if (ordered != QUEUE_ORDERED_NONE &&
	    ordered != QUEUE_ORDERED_DRAIN &&
	    ordered != QUEUE_ORDERED_DRAIN_FLUSH &&
	    ordered != QUEUE_ORDERED_DRAIN_FUA &&
	    ordered != QUEUE_ORDERED_TAG &&
	    ordered != QUEUE_ORDERED_TAG_FLUSH &&
	    ordered != QUEUE_ORDERED_TAG_FUA) {
		printk(KERN_ERR "blk_queue_ordered: bad value %d\n", ordered);
		return -EINVAL;
	}

	q->ordered = ordered;
	q->next_ordered = ordered;
	q->prepare_flush_fn = prepare_flush_fn;

	return 0;
}
EXPORT_SYMBOL(blk_queue_ordered);

/*
 * Cache flushing for ordered writes handling
 */
inline unsigned blk_ordered_cur_seq(struct request_queue *q)
{
	if (!q->ordseq)
		return 0;
	return 1 << ffz(q->ordseq);
}

unsigned blk_ordered_req_seq(struct request *rq)
{
	struct request_queue *q = rq->q;

	BUG_ON(q->ordseq == 0);

	if (rq == &q->pre_flush_rq)
		return QUEUE_ORDSEQ_PREFLUSH;
	if (rq == &q->bar_rq)
		return QUEUE_ORDSEQ_BAR;
	if (rq == &q->post_flush_rq)
		return QUEUE_ORDSEQ_POSTFLUSH;

	/*
	 * !fs requests don't need to follow barrier ordering.  Always
	 * put them at the front.  This fixes the following deadlock.
	 *
	 * http://thread.gmane.org/gmane.linux.kernel/537473
	 */
	if (!blk_fs_request(rq))
		return QUEUE_ORDSEQ_DRAIN;

	if ((rq->cmd_flags & REQ_ORDERED_COLOR) ==
	    (q->orig_bar_rq->cmd_flags & REQ_ORDERED_COLOR))
		return QUEUE_ORDSEQ_DRAIN;
	else
		return QUEUE_ORDSEQ_DONE;
}

void blk_ordered_complete_seq(struct request_queue *q, unsigned seq, int error)
{
	struct request *rq;

	if (error && !q->orderr)
		q->orderr = error;

	BUG_ON(q->ordseq & seq);
	q->ordseq |= seq;

	if (blk_ordered_cur_seq(q) != QUEUE_ORDSEQ_DONE)
		return;

	/*
	 * Okay, sequence complete.
	 */
	q->ordseq = 0;
	rq = q->orig_bar_rq;

	if (__blk_end_request(rq, q->orderr, blk_rq_bytes(rq)))
		BUG();
}

static void pre_flush_end_io(struct request *rq, int error)
{
	elv_completed_request(rq->q, rq);
	blk_ordered_complete_seq(rq->q, QUEUE_ORDSEQ_PREFLUSH, error);
}

static void bar_end_io(struct request *rq, int error)
{
	elv_completed_request(rq->q, rq);
	blk_ordered_complete_seq(rq->q, QUEUE_ORDSEQ_BAR, error);
}

static void post_flush_end_io(struct request *rq, int error)
{
	elv_completed_request(rq->q, rq);
	blk_ordered_complete_seq(rq->q, QUEUE_ORDSEQ_POSTFLUSH, error);
}

static void queue_flush(struct request_queue *q, unsigned which)
{
	struct request *rq;
	rq_end_io_fn *end_io;

	if (which == QUEUE_ORDERED_PREFLUSH) {
		rq = &q->pre_flush_rq;
		end_io = pre_flush_end_io;
	} else {
		rq = &q->post_flush_rq;
		end_io = post_flush_end_io;
	}

	rq->cmd_flags = REQ_HARDBARRIER;
	rq_init(q, rq);
	rq->elevator_private = NULL;
	rq->elevator_private2 = NULL;
	rq->rq_disk = q->bar_rq.rq_disk;
	rq->end_io = end_io;
	q->prepare_flush_fn(q, rq);

	elv_insert(q, rq, ELEVATOR_INSERT_FRONT);
}

static inline struct request *start_ordered(struct request_queue *q,
					    struct request *rq)
{
	q->orderr = 0;
	q->ordered = q->next_ordered;
	q->ordseq |= QUEUE_ORDSEQ_STARTED;

	/*
	 * Prep proxy barrier request.
	 */
	blkdev_dequeue_request(rq);
	q->orig_bar_rq = rq;
	rq = &q->bar_rq;
	rq->cmd_flags = 0;
	rq_init(q, rq);
	if (bio_data_dir(q->orig_bar_rq->bio) == WRITE)
		rq->cmd_flags |= REQ_RW;
	if (q->ordered & QUEUE_ORDERED_FUA)
		rq->cmd_flags |= REQ_FUA;
	rq->elevator_private = NULL;
	rq->elevator_private2 = NULL;
	init_request_from_bio(rq, q->orig_bar_rq->bio);
	rq->end_io = bar_end_io;

	/*
	 * Queue ordered sequence.  As we stack them at the head, we
	 * need to queue in reverse order.  Note that we rely on that
	 * no fs request uses ELEVATOR_INSERT_FRONT and thus no fs
	 * request gets inbetween ordered sequence. If this request is
	 * an empty barrier, we don't need to do a postflush ever since
	 * there will be no data written between the pre and post flush.
	 * Hence a single flush will suffice.
	 */
	if ((q->ordered & QUEUE_ORDERED_POSTFLUSH) && !blk_empty_barrier(rq))
		queue_flush(q, QUEUE_ORDERED_POSTFLUSH);
	else
		q->ordseq |= QUEUE_ORDSEQ_POSTFLUSH;

	elv_insert(q, rq, ELEVATOR_INSERT_FRONT);

	if (q->ordered & QUEUE_ORDERED_PREFLUSH) {
		queue_flush(q, QUEUE_ORDERED_PREFLUSH);
		rq = &q->pre_flush_rq;
	} else
		q->ordseq |= QUEUE_ORDSEQ_PREFLUSH;

	if ((q->ordered & QUEUE_ORDERED_TAG) || q->in_flight == 0)
		q->ordseq |= QUEUE_ORDSEQ_DRAIN;
	else
		rq = NULL;

	return rq;
}

int blk_do_ordered(struct request_queue *q, struct request **rqp)
{
	struct request *rq = *rqp;
	const int is_barrier = blk_fs_request(rq) && blk_barrier_rq(rq);

	if (!q->ordseq) {
		if (!is_barrier)
			return 1;

		if (q->next_ordered != QUEUE_ORDERED_NONE) {
			*rqp = start_ordered(q, rq);
			return 1;
		} else {
			/*
			 * This can happen when the queue switches to
			 * ORDERED_NONE while this request is on it.
			 */
			blkdev_dequeue_request(rq);
			if (__blk_end_request(rq, -EOPNOTSUPP,
					      blk_rq_bytes(rq)))
				BUG();
			*rqp = NULL;
			return 0;
		}
	}

	/*
	 * Ordered sequence in progress
	 */

	/* Special requests are not subject to ordering rules. */
	if (!blk_fs_request(rq) &&
	    rq != &q->pre_flush_rq && rq != &q->post_flush_rq)
		return 1;

	if (q->ordered & QUEUE_ORDERED_TAG) {
		/* Ordered by tag.  Blocking the next barrier is enough. */
		if (is_barrier && rq != &q->bar_rq)
			*rqp = NULL;
	} else {
		/* Ordered by draining.  Wait for turn. */
		WARN_ON(blk_ordered_req_seq(rq) < blk_ordered_cur_seq(q));
		if (blk_ordered_req_seq(rq) > blk_ordered_cur_seq(q))
			*rqp = NULL;
	}

	return 1;
}

static void bio_end_empty_barrier(struct bio *bio, int err)
{
	if (err)
		clear_bit(BIO_UPTODATE, &bio->bi_flags);

	complete(bio->bi_private);
}

/**
 * blkdev_issue_flush - queue a flush
 * @bdev:	blockdev to issue flush for
 * @error_sector:	error sector
 *
 * Description:
 *    Issue a flush for the block device in question. Caller can supply
 *    room for storing the error offset in case of a flush error, if they
 *    wish to.  Caller must run wait_for_completion() on its own.
 */
int blkdev_issue_flush(struct block_device *bdev, sector_t *error_sector)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request_queue *q;
	struct bio *bio;
	int ret;

	if (bdev->bd_disk == NULL)
		return -ENXIO;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	bio = bio_alloc(GFP_KERNEL, 0);
	if (!bio)
		return -ENOMEM;

	bio->bi_end_io = bio_end_empty_barrier;
	bio->bi_private = &wait;
	bio->bi_bdev = bdev;
	submit_bio(1 << BIO_RW_BARRIER, bio);

	wait_for_completion(&wait);

	/*
	 * The driver must store the error location in ->bi_sector, if
	 * it supports it. For non-stacked drivers, this should be copied
	 * from rq->sector.
	 */
	if (error_sector)
		*error_sector = bio->bi_sector;

	ret = 0;
	if (!bio_flagged(bio, BIO_UPTODATE))
		ret = -EIO;

	bio_put(bio);
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_flush);
