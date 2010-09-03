/*
 * Functions related to barrier IO handling
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/gfp.h>

#include "blk.h"

static struct request *queue_next_ordseq(struct request_queue *q);

/*
 * Cache flushing for ordered writes handling
 */
unsigned blk_ordered_cur_seq(struct request_queue *q)
{
	if (!q->ordseq)
		return 0;
	return 1 << ffz(q->ordseq);
}

static struct request *blk_ordered_complete_seq(struct request_queue *q,
						unsigned seq, int error)
{
	struct request *next_rq = NULL;

	if (error && !q->orderr)
		q->orderr = error;

	BUG_ON(q->ordseq & seq);
	q->ordseq |= seq;

	if (blk_ordered_cur_seq(q) != QUEUE_ORDSEQ_DONE) {
		/* not complete yet, queue the next ordered sequence */
		next_rq = queue_next_ordseq(q);
	} else {
		/* complete this barrier request */
		__blk_end_request_all(q->orig_bar_rq, q->orderr);
		q->orig_bar_rq = NULL;
		q->ordseq = 0;

		/* dispatch the next barrier if there's one */
		if (!list_empty(&q->pending_barriers)) {
			next_rq = list_entry_rq(q->pending_barriers.next);
			list_move(&next_rq->queuelist, &q->queue_head);
		}
	}
	return next_rq;
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

static void queue_flush(struct request_queue *q, struct request *rq,
			rq_end_io_fn *end_io)
{
	blk_rq_init(q, rq);
	rq->cmd_type = REQ_TYPE_FS;
	rq->cmd_flags = REQ_FLUSH;
	rq->rq_disk = q->orig_bar_rq->rq_disk;
	rq->end_io = end_io;

	elv_insert(q, rq, ELEVATOR_INSERT_FRONT);
}

static struct request *queue_next_ordseq(struct request_queue *q)
{
	struct request *rq = &q->bar_rq;

	switch (blk_ordered_cur_seq(q)) {
	case QUEUE_ORDSEQ_PREFLUSH:
		queue_flush(q, rq, pre_flush_end_io);
		break;

	case QUEUE_ORDSEQ_BAR:
		/* initialize proxy request and queue it */
		blk_rq_init(q, rq);
		init_request_from_bio(rq, q->orig_bar_rq->bio);
		rq->cmd_flags &= ~REQ_HARDBARRIER;
		if (q->ordered & QUEUE_ORDERED_DO_FUA)
			rq->cmd_flags |= REQ_FUA;
		rq->end_io = bar_end_io;

		elv_insert(q, rq, ELEVATOR_INSERT_FRONT);
		break;

	case QUEUE_ORDSEQ_POSTFLUSH:
		queue_flush(q, rq, post_flush_end_io);
		break;

	default:
		BUG();
	}
	return rq;
}

struct request *blk_do_ordered(struct request_queue *q, struct request *rq)
{
	unsigned skip = 0;

	if (!(rq->cmd_flags & REQ_HARDBARRIER))
		return rq;

	if (q->ordseq) {
		/*
		 * Barrier is already in progress and they can't be
		 * processed in parallel.  Queue for later processing.
		 */
		list_move_tail(&rq->queuelist, &q->pending_barriers);
		return NULL;
	}

	if (unlikely(q->next_ordered == QUEUE_ORDERED_NONE)) {
		/*
		 * Queue ordering not supported.  Terminate
		 * with prejudice.
		 */
		blk_dequeue_request(rq);
		__blk_end_request_all(rq, -EOPNOTSUPP);
		return NULL;
	}

	/*
	 * Start a new ordered sequence
	 */
	q->orderr = 0;
	q->ordered = q->next_ordered;
	q->ordseq |= QUEUE_ORDSEQ_STARTED;

	/*
	 * For an empty barrier, there's no actual BAR request, which
	 * in turn makes POSTFLUSH unnecessary.  Mask them off.
	 */
	if (!blk_rq_sectors(rq))
		q->ordered &= ~(QUEUE_ORDERED_DO_BAR |
				QUEUE_ORDERED_DO_POSTFLUSH);

	/* stash away the original request */
	blk_dequeue_request(rq);
	q->orig_bar_rq = rq;

	if (!(q->ordered & QUEUE_ORDERED_DO_PREFLUSH))
		skip |= QUEUE_ORDSEQ_PREFLUSH;

	if (!(q->ordered & QUEUE_ORDERED_DO_BAR))
		skip |= QUEUE_ORDSEQ_BAR;

	if (!(q->ordered & QUEUE_ORDERED_DO_POSTFLUSH))
		skip |= QUEUE_ORDSEQ_POSTFLUSH;

	/* complete skipped sequences and return the first sequence */
	return blk_ordered_complete_seq(q, skip, 0);
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
