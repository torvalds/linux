/*
 * Functions to sequence FLUSH and FUA writes.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/gfp.h>

#include "blk.h"

/* FLUSH/FUA sequences */
enum {
	QUEUE_FSEQ_STARTED	= (1 << 0), /* flushing in progress */
	QUEUE_FSEQ_PREFLUSH	= (1 << 1), /* pre-flushing in progress */
	QUEUE_FSEQ_DATA		= (1 << 2), /* data write in progress */
	QUEUE_FSEQ_POSTFLUSH	= (1 << 3), /* post-flushing in progress */
	QUEUE_FSEQ_DONE		= (1 << 4),
};

static struct request *queue_next_fseq(struct request_queue *q);

unsigned blk_flush_cur_seq(struct request_queue *q)
{
	if (!q->flush_seq)
		return 0;
	return 1 << ffz(q->flush_seq);
}

static struct request *blk_flush_complete_seq(struct request_queue *q,
					      unsigned seq, int error)
{
	struct request *next_rq = NULL;

	if (error && !q->flush_err)
		q->flush_err = error;

	BUG_ON(q->flush_seq & seq);
	q->flush_seq |= seq;

	if (blk_flush_cur_seq(q) != QUEUE_FSEQ_DONE) {
		/* not complete yet, queue the next flush sequence */
		next_rq = queue_next_fseq(q);
	} else {
		/* complete this flush request */
		__blk_end_request_all(q->orig_flush_rq, q->flush_err);
		q->orig_flush_rq = NULL;
		q->flush_seq = 0;

		/* dispatch the next flush if there's one */
		if (!list_empty(&q->pending_flushes)) {
			next_rq = list_entry_rq(q->pending_flushes.next);
			list_move(&next_rq->queuelist, &q->queue_head);
		}
	}
	return next_rq;
}

static void blk_flush_complete_seq_end_io(struct request_queue *q,
					  unsigned seq, int error)
{
	bool was_empty = elv_queue_empty(q);
	struct request *next_rq;

	next_rq = blk_flush_complete_seq(q, seq, error);

	/*
	 * Moving a request silently to empty queue_head may stall the
	 * queue.  Kick the queue in those cases.
	 */
	if (was_empty && next_rq)
		__blk_run_queue(q);
}

static void pre_flush_end_io(struct request *rq, int error)
{
	elv_completed_request(rq->q, rq);
	blk_flush_complete_seq_end_io(rq->q, QUEUE_FSEQ_PREFLUSH, error);
}

static void flush_data_end_io(struct request *rq, int error)
{
	elv_completed_request(rq->q, rq);
	blk_flush_complete_seq_end_io(rq->q, QUEUE_FSEQ_DATA, error);
}

static void post_flush_end_io(struct request *rq, int error)
{
	elv_completed_request(rq->q, rq);
	blk_flush_complete_seq_end_io(rq->q, QUEUE_FSEQ_POSTFLUSH, error);
}

static void init_flush_request(struct request *rq, struct gendisk *disk)
{
	rq->cmd_type = REQ_TYPE_FS;
	rq->cmd_flags = WRITE_FLUSH;
	rq->rq_disk = disk;
}

static struct request *queue_next_fseq(struct request_queue *q)
{
	struct request *orig_rq = q->orig_flush_rq;
	struct request *rq = &q->flush_rq;

	blk_rq_init(q, rq);

	switch (blk_flush_cur_seq(q)) {
	case QUEUE_FSEQ_PREFLUSH:
		init_flush_request(rq, orig_rq->rq_disk);
		rq->end_io = pre_flush_end_io;
		break;
	case QUEUE_FSEQ_DATA:
		init_request_from_bio(rq, orig_rq->bio);
		/*
		 * orig_rq->rq_disk may be different from
		 * bio->bi_bdev->bd_disk if orig_rq got here through
		 * remapping drivers.  Make sure rq->rq_disk points
		 * to the same one as orig_rq.
		 */
		rq->rq_disk = orig_rq->rq_disk;
		rq->cmd_flags &= ~(REQ_FLUSH | REQ_FUA);
		rq->cmd_flags |= orig_rq->cmd_flags & (REQ_FLUSH | REQ_FUA);
		rq->end_io = flush_data_end_io;
		break;
	case QUEUE_FSEQ_POSTFLUSH:
		init_flush_request(rq, orig_rq->rq_disk);
		rq->end_io = post_flush_end_io;
		break;
	default:
		BUG();
	}

	elv_insert(q, rq, ELEVATOR_INSERT_FRONT);
	return rq;
}

struct request *blk_do_flush(struct request_queue *q, struct request *rq)
{
	unsigned int fflags = q->flush_flags; /* may change, cache it */
	bool has_flush = fflags & REQ_FLUSH, has_fua = fflags & REQ_FUA;
	bool do_preflush = has_flush && (rq->cmd_flags & REQ_FLUSH);
	bool do_postflush = has_flush && !has_fua && (rq->cmd_flags & REQ_FUA);
	unsigned skip = 0;

	/*
	 * Special case.  If there's data but flush is not necessary,
	 * the request can be issued directly.
	 *
	 * Flush w/o data should be able to be issued directly too but
	 * currently some drivers assume that rq->bio contains
	 * non-zero data if it isn't NULL and empty FLUSH requests
	 * getting here usually have bio's without data.
	 */
	if (blk_rq_sectors(rq) && !do_preflush && !do_postflush) {
		rq->cmd_flags &= ~REQ_FLUSH;
		if (!has_fua)
			rq->cmd_flags &= ~REQ_FUA;
		return rq;
	}

	/*
	 * Sequenced flushes can't be processed in parallel.  If
	 * another one is already in progress, queue for later
	 * processing.
	 */
	if (q->flush_seq) {
		list_move_tail(&rq->queuelist, &q->pending_flushes);
		return NULL;
	}

	/*
	 * Start a new flush sequence
	 */
	q->flush_err = 0;
	q->flush_seq |= QUEUE_FSEQ_STARTED;

	/* adjust FLUSH/FUA of the original request and stash it away */
	rq->cmd_flags &= ~REQ_FLUSH;
	if (!has_fua)
		rq->cmd_flags &= ~REQ_FUA;
	blk_dequeue_request(rq);
	q->orig_flush_rq = rq;

	/* skip unneded sequences and return the first one */
	if (!do_preflush)
		skip |= QUEUE_FSEQ_PREFLUSH;
	if (!blk_rq_sectors(rq))
		skip |= QUEUE_FSEQ_DATA;
	if (!do_postflush)
		skip |= QUEUE_FSEQ_POSTFLUSH;
	return blk_flush_complete_seq(q, skip, 0);
}

static void bio_end_flush(struct bio *bio, int err)
{
	if (err)
		clear_bit(BIO_UPTODATE, &bio->bi_flags);
	if (bio->bi_private)
		complete(bio->bi_private);
	bio_put(bio);
}

/**
 * blkdev_issue_flush - queue a flush
 * @bdev:	blockdev to issue flush for
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @error_sector:	error sector
 *
 * Description:
 *    Issue a flush for the block device in question. Caller can supply
 *    room for storing the error offset in case of a flush error, if they
 *    wish to. If WAIT flag is not passed then caller may check only what
 *    request was pushed in some internal queue for later handling.
 */
int blkdev_issue_flush(struct block_device *bdev, gfp_t gfp_mask,
		sector_t *error_sector)
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
	 * the flush.
	 */
	if (!q->make_request_fn)
		return -ENXIO;

	bio = bio_alloc(gfp_mask, 0);
	bio->bi_end_io = bio_end_flush;
	bio->bi_bdev = bdev;
	bio->bi_private = &wait;

	bio_get(bio);
	submit_bio(WRITE_FLUSH, bio);
	wait_for_completion(&wait);

	/*
	 * The driver must store the error location in ->bi_sector, if
	 * it supports it. For non-stacked drivers, this should be
	 * copied from blk_rq_pos(rq).
	 */
	if (error_sector)
               *error_sector = bio->bi_sector;

	if (!bio_flagged(bio, BIO_UPTODATE))
		ret = -EIO;

	bio_put(bio);
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_flush);
