/*
 * Functions related to barrier IO handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/gfp.h>

#include "blk.h"

/**
 * blk_queue_ordered - does this queue support ordered writes
 * @q:        the request queue
 * @ordered:  one of QUEUE_ORDERED_*
 *
 * Description:
 *   For journalled file systems, doing ordered writes on a commit
 *   block instead of explicitly doing wait_on_buffer (which is bad
 *   for performance) can be a big win. Block drivers supporting this
 *   feature should call this function and indicate so.
 *
 **/
int blk_queue_ordered(struct request_queue *q, unsigned ordered)
{
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

	return 0;
}
EXPORT_SYMBOL(blk_queue_ordered);

/*
 * Cache flushing for ordered writes handling
 */
unsigned blk_ordered_cur_seq(struct request_queue *q)
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
	if (rq->cmd_type != REQ_TYPE_FS)
		return QUEUE_ORDSEQ_DRAIN;

	if ((rq->cmd_flags & REQ_ORDERED_COLOR) ==
	    (q->orig_bar_rq->cmd_flags & REQ_ORDERED_COLOR))
		return QUEUE_ORDSEQ_DRAIN;
	else
		return QUEUE_ORDSEQ_DONE;
}

bool blk_ordered_complete_seq(struct request_queue *q, unsigned seq, int error)
{
	struct request *rq;

	if (error && !q->orderr)
		q->orderr = error;

	BUG_ON(q->ordseq & seq);
	q->ordseq |= seq;

	if (blk_ordered_cur_seq(q) != QUEUE_ORDSEQ_DONE)
		return false;

	/*
	 * Okay, sequence complete.
	 */
	q->ordseq = 0;
	rq = q->orig_bar_rq;
	__blk_end_request_all(rq, q->orderr);
	return true;
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

	if (which == QUEUE_ORDERED_DO_PREFLUSH) {
		rq = &q->pre_flush_rq;
		end_io = pre_flush_end_io;
	} else {
		rq = &q->post_flush_rq;
		end_io = post_flush_end_io;
	}

	blk_rq_init(q, rq);
	rq->cmd_type = REQ_TYPE_FS;
	rq->cmd_flags = REQ_HARDBARRIER | REQ_FLUSH;
	rq->rq_disk = q->orig_bar_rq->rq_disk;
	rq->end_io = end_io;

	elv_insert(q, rq, ELEVATOR_INSERT_FRONT);
}

static inline bool start_ordered(struct request_queue *q, struct request **rqp)
{
	struct request *rq = *rqp;
	unsigned skip = 0;

	q->orderr = 0;
	q->ordered = q->next_ordered;
	q->ordseq |= QUEUE_ORDSEQ_STARTED;

	/*
	 * For an empty barrier, there's no actual BAR request, which
	 * in turn makes POSTFLUSH unnecessary.  Mask them off.
	 */
	if (!blk_rq_sectors(rq)) {
		q->ordered &= ~(QUEUE_ORDERED_DO_BAR |
				QUEUE_ORDERED_DO_POSTFLUSH);
		/*
		 * Empty barrier on a write-through device w/ ordered
		 * tag has no command to issue and without any command
		 * to issue, ordering by tag can't be used.  Drain
		 * instead.
		 */
		if ((q->ordered & QUEUE_ORDERED_BY_TAG) &&
		    !(q->ordered & QUEUE_ORDERED_DO_PREFLUSH)) {
			q->ordered &= ~QUEUE_ORDERED_BY_TAG;
			q->ordered |= QUEUE_ORDERED_BY_DRAIN;
		}
	}

	/* stash away the original request */
	blk_dequeue_request(rq);
	q->orig_bar_rq = rq;
	rq = NULL;

	/*
	 * Queue ordered sequence.  As we stack them at the head, we
	 * need to queue in reverse order.  Note that we rely on that
	 * no fs request uses ELEVATOR_INSERT_FRONT and thus no fs
	 * request gets inbetween ordered sequence.
	 */
	if (q->ordered & QUEUE_ORDERED_DO_POSTFLUSH) {
		queue_flush(q, QUEUE_ORDERED_DO_POSTFLUSH);
		rq = &q->post_flush_rq;
	} else
		skip |= QUEUE_ORDSEQ_POSTFLUSH;

	if (q->ordered & QUEUE_ORDERED_DO_BAR) {
		rq = &q->bar_rq;

		/* initialize proxy request and queue it */
		blk_rq_init(q, rq);
		if (bio_data_dir(q->orig_bar_rq->bio) == WRITE)
			rq->cmd_flags |= REQ_WRITE;
		if (q->ordered & QUEUE_ORDERED_DO_FUA)
			rq->cmd_flags |= REQ_FUA;
		init_request_from_bio(rq, q->orig_bar_rq->bio);
		rq->end_io = bar_end_io;

		elv_insert(q, rq, ELEVATOR_INSERT_FRONT);
	} else
		skip |= QUEUE_ORDSEQ_BAR;

	if (q->ordered & QUEUE_ORDERED_DO_PREFLUSH) {
		queue_flush(q, QUEUE_ORDERED_DO_PREFLUSH);
		rq = &q->pre_flush_rq;
	} else
		skip |= QUEUE_ORDSEQ_PREFLUSH;

	if ((q->ordered & QUEUE_ORDERED_BY_DRAIN) && queue_in_flight(q))
		rq = NULL;
	else
		skip |= QUEUE_ORDSEQ_DRAIN;

	*rqp = rq;

	/*
	 * Complete skipped sequences.  If whole sequence is complete,
	 * return false to tell elevator that this request is gone.
	 */
	return !blk_ordered_complete_seq(q, skip, 0);
}

bool blk_do_ordered(struct request_queue *q, struct request **rqp)
{
	struct request *rq = *rqp;
	const int is_barrier = rq->cmd_type == REQ_TYPE_FS &&
				(rq->cmd_flags & REQ_HARDBARRIER);

	if (!q->ordseq) {
		if (!is_barrier)
			return true;

		if (q->next_ordered != QUEUE_ORDERED_NONE)
			return start_ordered(q, rqp);
		else {
			/*
			 * Queue ordering not supported.  Terminate
			 * with prejudice.
			 */
			blk_dequeue_request(rq);
			__blk_end_request_all(rq, -EOPNOTSUPP);
			*rqp = NULL;
			return false;
		}
	}

	/*
	 * Ordered sequence in progress
	 */

	/* Special requests are not subject to ordering rules. */
	if (rq->cmd_type != REQ_TYPE_FS &&
	    rq != &q->pre_flush_rq && rq != &q->post_flush_rq)
		return true;

	if (q->ordered & QUEUE_ORDERED_BY_TAG) {
		/* Ordered by tag.  Blocking the next barrier is enough. */
		if (is_barrier && rq != &q->bar_rq)
			*rqp = NULL;
	} else {
		/* Ordered by draining.  Wait for turn. */
		WARN_ON(blk_ordered_req_seq(rq) < blk_ordered_cur_seq(q));
		if (blk_ordered_req_seq(rq) > blk_ordered_cur_seq(q))
			*rqp = NULL;
	}

	return true;
}

static void bio_end_empty_barrier(struct bio *bio, int err)
{
	if (err) {
		if (err == -EOPNOTSUPP)
			set_bit(BIO_EOPNOTSUPP, &bio->bi_flags);
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	}
	if (bio->bi_private)
		complete(bio->bi_private);
	bio_put(bio);
}

/**
 * blkdev_issue_flush - queue a flush
 * @bdev:	blockdev to issue flush for
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @error_sector:	error sector
 * @flags:	BLKDEV_IFL_* flags to control behaviour
 *
 * Description:
 *    Issue a flush for the block device in question. Caller can supply
 *    room for storing the error offset in case of a flush error, if they
 *    wish to. If WAIT flag is not passed then caller may check only what
 *    request was pushed in some internal queue for later handling.
 */
int blkdev_issue_flush(struct block_device *bdev, gfp_t gfp_mask,
		sector_t *error_sector, unsigned long flags)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request_queue *q;
	struct bio *bio;
	int ret = 0;

	if (bdev->bd_disk == NULL)
		return -ENXIO;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	/*
	 * some block devices may not have their queue correctly set up here
	 * (e.g. loop device without a backing file) and so issuing a flush
	 * here will panic. Ensure there is a request function before issuing
	 * the barrier.
	 */
	if (!q->make_request_fn)
		return -ENXIO;

	bio = bio_alloc(gfp_mask, 0);
	bio->bi_end_io = bio_end_empty_barrier;
	bio->bi_bdev = bdev;
	if (test_bit(BLKDEV_WAIT, &flags))
		bio->bi_private = &wait;

	bio_get(bio);
	submit_bio(WRITE_BARRIER, bio);
	if (test_bit(BLKDEV_WAIT, &flags)) {
		wait_for_completion(&wait);
		/*
		 * The driver must store the error location in ->bi_sector, if
		 * it supports it. For non-stacked drivers, this should be
		 * copied from blk_rq_pos(rq).
		 */
		if (error_sector)
			*error_sector = bio->bi_sector;
	}

	if (bio_flagged(bio, BIO_EOPNOTSUPP))
		ret = -EOPNOTSUPP;
	else if (!bio_flagged(bio, BIO_UPTODATE))
		ret = -EIO;

	bio_put(bio);
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_flush);
