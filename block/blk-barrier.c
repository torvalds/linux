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
	if (!prepare_flush_fn && (ordered & (QUEUE_ORDERED_DO_PREFLUSH |
					     QUEUE_ORDERED_DO_POSTFLUSH))) {
		printk(KERN_ERR "%s: prepare_flush_fn required\n", __func__);
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
	if (!blk_fs_request(rq))
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
	rq->cmd_flags = REQ_HARDBARRIER;
	rq->rq_disk = q->bar_rq.rq_disk;
	rq->end_io = end_io;
	q->prepare_flush_fn(q, rq);

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
			rq->cmd_flags |= REQ_RW;
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
	const int is_barrier = blk_fs_request(rq) && blk_barrier_rq(rq);

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
	if (!blk_fs_request(rq) &&
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
 *    wish to.
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
	bio->bi_end_io = bio_end_empty_barrier;
	bio->bi_private = &wait;
	bio->bi_bdev = bdev;
	submit_bio(WRITE_BARRIER, bio);

	wait_for_completion(&wait);

	/*
	 * The driver must store the error location in ->bi_sector, if
	 * it supports it. For non-stacked drivers, this should be copied
	 * from blk_rq_pos(rq).
	 */
	if (error_sector)
		*error_sector = bio->bi_sector;

	ret = 0;
	if (bio_flagged(bio, BIO_EOPNOTSUPP))
		ret = -EOPNOTSUPP;
	else if (!bio_flagged(bio, BIO_UPTODATE))
		ret = -EIO;

	bio_put(bio);
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_flush);

static void blkdev_discard_end_io(struct bio *bio, int err)
{
	if (err) {
		if (err == -EOPNOTSUPP)
			set_bit(BIO_EOPNOTSUPP, &bio->bi_flags);
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	}

	if (bio->bi_private)
		complete(bio->bi_private);
	__free_page(bio_page(bio));

	bio_put(bio);
}

/**
 * blkdev_issue_discard - queue a discard
 * @bdev:	blockdev to issue discard for
 * @sector:	start sector
 * @nr_sects:	number of sectors to discard
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @flags:	DISCARD_FL_* flags to control behaviour
 *
 * Description:
 *    Issue a discard request for the sectors in question.
 */
int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, int flags)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request_queue *q = bdev_get_queue(bdev);
	int type = flags & DISCARD_FL_BARRIER ?
		DISCARD_BARRIER : DISCARD_NOBARRIER;
	struct bio *bio;
	struct page *page;
	int ret = 0;

	if (!q)
		return -ENXIO;

	if (!blk_queue_discard(q))
		return -EOPNOTSUPP;

	while (nr_sects && !ret) {
		unsigned int sector_size = q->limits.logical_block_size;
		unsigned int max_discard_sectors =
			min(q->limits.max_discard_sectors, UINT_MAX >> 9);

		bio = bio_alloc(gfp_mask, 1);
		if (!bio)
			goto out;
		bio->bi_sector = sector;
		bio->bi_end_io = blkdev_discard_end_io;
		bio->bi_bdev = bdev;
		if (flags & DISCARD_FL_WAIT)
			bio->bi_private = &wait;

		/*
		 * Add a zeroed one-sector payload as that's what
		 * our current implementations need.  If we'll ever need
		 * more the interface will need revisiting.
		 */
		page = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!page)
			goto out_free_bio;
		if (bio_add_pc_page(q, bio, page, sector_size, 0) < sector_size)
			goto out_free_page;

		/*
		 * And override the bio size - the way discard works we
		 * touch many more blocks on disk than the actual payload
		 * length.
		 */
		if (nr_sects > max_discard_sectors) {
			bio->bi_size = max_discard_sectors << 9;
			nr_sects -= max_discard_sectors;
			sector += max_discard_sectors;
		} else {
			bio->bi_size = nr_sects << 9;
			nr_sects = 0;
		}

		bio_get(bio);
		submit_bio(type, bio);

		if (flags & DISCARD_FL_WAIT)
			wait_for_completion(&wait);

		if (bio_flagged(bio, BIO_EOPNOTSUPP))
			ret = -EOPNOTSUPP;
		else if (!bio_flagged(bio, BIO_UPTODATE))
			ret = -EIO;
		bio_put(bio);
	}
	return ret;
out_free_page:
	__free_page(page);
out_free_bio:
	bio_put(bio);
out:
	return -ENOMEM;
}
EXPORT_SYMBOL(blkdev_issue_discard);
