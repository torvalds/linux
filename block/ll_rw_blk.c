/*
 * Copyright (C) 1991, 1992 Linus Torvalds
 * Copyright (C) 1994,      Karl Keyte: Added support for disk statistics
 * Elevator latency, (C) 2000  Andrea Arcangeli <andrea@suse.de> SuSE
 * Queue request tables / lock, selectable elevator, Jens Axboe <axboe@suse.de>
 * kernel-doc documentation started by NeilBrown <neilb@cse.unsw.edu.au> -  July2000
 * bio rewrite, highmem i/o, etc, Jens Axboe <axboe@suse.de> - may 2001
 */

/*
 * This handles all read/write requests to block devices
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/backing-dev.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>	/* for max_pfn/max_low_pfn */
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/blktrace_api.h>
#include <linux/fault-inject.h>

/*
 * for max sense size
 */
#include <scsi/scsi_cmnd.h>

static void blk_unplug_work(struct work_struct *work);
static void blk_unplug_timeout(unsigned long data);
static void drive_stat_acct(struct request *rq, int nr_sectors, int new_io);
static void init_request_from_bio(struct request *req, struct bio *bio);
static int __make_request(request_queue_t *q, struct bio *bio);
static struct io_context *current_io_context(gfp_t gfp_flags, int node);

/*
 * For the allocated request tables
 */
static struct kmem_cache *request_cachep;

/*
 * For queue allocation
 */
static struct kmem_cache *requestq_cachep;

/*
 * For io context allocations
 */
static struct kmem_cache *iocontext_cachep;

/*
 * Controlling structure to kblockd
 */
static struct workqueue_struct *kblockd_workqueue;

unsigned long blk_max_low_pfn, blk_max_pfn;

EXPORT_SYMBOL(blk_max_low_pfn);
EXPORT_SYMBOL(blk_max_pfn);

static DEFINE_PER_CPU(struct list_head, blk_cpu_done);

/* Amount of time in which a process may batch requests */
#define BLK_BATCH_TIME	(HZ/50UL)

/* Number of requests a "batching" process may submit */
#define BLK_BATCH_REQ	32

/*
 * Return the threshold (number of used requests) at which the queue is
 * considered to be congested.  It include a little hysteresis to keep the
 * context switch rate down.
 */
static inline int queue_congestion_on_threshold(struct request_queue *q)
{
	return q->nr_congestion_on;
}

/*
 * The threshold at which a queue is considered to be uncongested
 */
static inline int queue_congestion_off_threshold(struct request_queue *q)
{
	return q->nr_congestion_off;
}

static void blk_queue_congestion_threshold(struct request_queue *q)
{
	int nr;

	nr = q->nr_requests - (q->nr_requests / 8) + 1;
	if (nr > q->nr_requests)
		nr = q->nr_requests;
	q->nr_congestion_on = nr;

	nr = q->nr_requests - (q->nr_requests / 8) - (q->nr_requests / 16) - 1;
	if (nr < 1)
		nr = 1;
	q->nr_congestion_off = nr;
}

/**
 * blk_get_backing_dev_info - get the address of a queue's backing_dev_info
 * @bdev:	device
 *
 * Locates the passed device's request queue and returns the address of its
 * backing_dev_info
 *
 * Will return NULL if the request queue cannot be located.
 */
struct backing_dev_info *blk_get_backing_dev_info(struct block_device *bdev)
{
	struct backing_dev_info *ret = NULL;
	request_queue_t *q = bdev_get_queue(bdev);

	if (q)
		ret = &q->backing_dev_info;
	return ret;
}
EXPORT_SYMBOL(blk_get_backing_dev_info);

/**
 * blk_queue_prep_rq - set a prepare_request function for queue
 * @q:		queue
 * @pfn:	prepare_request function
 *
 * It's possible for a queue to register a prepare_request callback which
 * is invoked before the request is handed to the request_fn. The goal of
 * the function is to prepare a request for I/O, it can be used to build a
 * cdb from the request data for instance.
 *
 */
void blk_queue_prep_rq(request_queue_t *q, prep_rq_fn *pfn)
{
	q->prep_rq_fn = pfn;
}

EXPORT_SYMBOL(blk_queue_prep_rq);

/**
 * blk_queue_merge_bvec - set a merge_bvec function for queue
 * @q:		queue
 * @mbfn:	merge_bvec_fn
 *
 * Usually queues have static limitations on the max sectors or segments that
 * we can put in a request. Stacking drivers may have some settings that
 * are dynamic, and thus we have to query the queue whether it is ok to
 * add a new bio_vec to a bio at a given offset or not. If the block device
 * has such limitations, it needs to register a merge_bvec_fn to control
 * the size of bio's sent to it. Note that a block device *must* allow a
 * single page to be added to an empty bio. The block device driver may want
 * to use the bio_split() function to deal with these bio's. By default
 * no merge_bvec_fn is defined for a queue, and only the fixed limits are
 * honored.
 */
void blk_queue_merge_bvec(request_queue_t *q, merge_bvec_fn *mbfn)
{
	q->merge_bvec_fn = mbfn;
}

EXPORT_SYMBOL(blk_queue_merge_bvec);

void blk_queue_softirq_done(request_queue_t *q, softirq_done_fn *fn)
{
	q->softirq_done_fn = fn;
}

EXPORT_SYMBOL(blk_queue_softirq_done);

/**
 * blk_queue_make_request - define an alternate make_request function for a device
 * @q:  the request queue for the device to be affected
 * @mfn: the alternate make_request function
 *
 * Description:
 *    The normal way for &struct bios to be passed to a device
 *    driver is for them to be collected into requests on a request
 *    queue, and then to allow the device driver to select requests
 *    off that queue when it is ready.  This works well for many block
 *    devices. However some block devices (typically virtual devices
 *    such as md or lvm) do not benefit from the processing on the
 *    request queue, and are served best by having the requests passed
 *    directly to them.  This can be achieved by providing a function
 *    to blk_queue_make_request().
 *
 * Caveat:
 *    The driver that does this *must* be able to deal appropriately
 *    with buffers in "highmemory". This can be accomplished by either calling
 *    __bio_kmap_atomic() to get a temporary kernel mapping, or by calling
 *    blk_queue_bounce() to create a buffer in normal memory.
 **/
void blk_queue_make_request(request_queue_t * q, make_request_fn * mfn)
{
	/*
	 * set defaults
	 */
	q->nr_requests = BLKDEV_MAX_RQ;
	blk_queue_max_phys_segments(q, MAX_PHYS_SEGMENTS);
	blk_queue_max_hw_segments(q, MAX_HW_SEGMENTS);
	q->make_request_fn = mfn;
	q->backing_dev_info.ra_pages = (VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE;
	q->backing_dev_info.state = 0;
	q->backing_dev_info.capabilities = BDI_CAP_MAP_COPY;
	blk_queue_max_sectors(q, SAFE_MAX_SECTORS);
	blk_queue_hardsect_size(q, 512);
	blk_queue_dma_alignment(q, 511);
	blk_queue_congestion_threshold(q);
	q->nr_batching = BLK_BATCH_REQ;

	q->unplug_thresh = 4;		/* hmm */
	q->unplug_delay = (3 * HZ) / 1000;	/* 3 milliseconds */
	if (q->unplug_delay == 0)
		q->unplug_delay = 1;

	INIT_WORK(&q->unplug_work, blk_unplug_work);

	q->unplug_timer.function = blk_unplug_timeout;
	q->unplug_timer.data = (unsigned long)q;

	/*
	 * by default assume old behaviour and bounce for any highmem page
	 */
	blk_queue_bounce_limit(q, BLK_BOUNCE_HIGH);
}

EXPORT_SYMBOL(blk_queue_make_request);

static void rq_init(request_queue_t *q, struct request *rq)
{
	INIT_LIST_HEAD(&rq->queuelist);
	INIT_LIST_HEAD(&rq->donelist);

	rq->errors = 0;
	rq->bio = rq->biotail = NULL;
	INIT_HLIST_NODE(&rq->hash);
	RB_CLEAR_NODE(&rq->rb_node);
	rq->ioprio = 0;
	rq->buffer = NULL;
	rq->ref_count = 1;
	rq->q = q;
	rq->special = NULL;
	rq->data_len = 0;
	rq->data = NULL;
	rq->nr_phys_segments = 0;
	rq->sense = NULL;
	rq->end_io = NULL;
	rq->end_io_data = NULL;
	rq->completion_data = NULL;
}

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
int blk_queue_ordered(request_queue_t *q, unsigned ordered,
		      prepare_flush_fn *prepare_flush_fn)
{
	if (ordered & (QUEUE_ORDERED_PREFLUSH | QUEUE_ORDERED_POSTFLUSH) &&
	    prepare_flush_fn == NULL) {
		printk(KERN_ERR "blk_queue_ordered: prepare_flush_fn required\n");
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

/**
 * blk_queue_issue_flush_fn - set function for issuing a flush
 * @q:     the request queue
 * @iff:   the function to be called issuing the flush
 *
 * Description:
 *   If a driver supports issuing a flush command, the support is notified
 *   to the block layer by defining it through this call.
 *
 **/
void blk_queue_issue_flush_fn(request_queue_t *q, issue_flush_fn *iff)
{
	q->issue_flush_fn = iff;
}

EXPORT_SYMBOL(blk_queue_issue_flush_fn);

/*
 * Cache flushing for ordered writes handling
 */
inline unsigned blk_ordered_cur_seq(request_queue_t *q)
{
	if (!q->ordseq)
		return 0;
	return 1 << ffz(q->ordseq);
}

unsigned blk_ordered_req_seq(struct request *rq)
{
	request_queue_t *q = rq->q;

	BUG_ON(q->ordseq == 0);

	if (rq == &q->pre_flush_rq)
		return QUEUE_ORDSEQ_PREFLUSH;
	if (rq == &q->bar_rq)
		return QUEUE_ORDSEQ_BAR;
	if (rq == &q->post_flush_rq)
		return QUEUE_ORDSEQ_POSTFLUSH;

	if ((rq->cmd_flags & REQ_ORDERED_COLOR) ==
	    (q->orig_bar_rq->cmd_flags & REQ_ORDERED_COLOR))
		return QUEUE_ORDSEQ_DRAIN;
	else
		return QUEUE_ORDSEQ_DONE;
}

void blk_ordered_complete_seq(request_queue_t *q, unsigned seq, int error)
{
	struct request *rq;
	int uptodate;

	if (error && !q->orderr)
		q->orderr = error;

	BUG_ON(q->ordseq & seq);
	q->ordseq |= seq;

	if (blk_ordered_cur_seq(q) != QUEUE_ORDSEQ_DONE)
		return;

	/*
	 * Okay, sequence complete.
	 */
	rq = q->orig_bar_rq;
	uptodate = q->orderr ? q->orderr : 1;

	q->ordseq = 0;

	end_that_request_first(rq, uptodate, rq->hard_nr_sectors);
	end_that_request_last(rq, uptodate);
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

static void queue_flush(request_queue_t *q, unsigned which)
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

static inline struct request *start_ordered(request_queue_t *q,
					    struct request *rq)
{
	q->bi_size = 0;
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
	rq->cmd_flags |= q->ordered & QUEUE_ORDERED_FUA ? REQ_FUA : 0;
	rq->elevator_private = NULL;
	rq->elevator_private2 = NULL;
	init_request_from_bio(rq, q->orig_bar_rq->bio);
	rq->end_io = bar_end_io;

	/*
	 * Queue ordered sequence.  As we stack them at the head, we
	 * need to queue in reverse order.  Note that we rely on that
	 * no fs request uses ELEVATOR_INSERT_FRONT and thus no fs
	 * request gets inbetween ordered sequence.
	 */
	if (q->ordered & QUEUE_ORDERED_POSTFLUSH)
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

int blk_do_ordered(request_queue_t *q, struct request **rqp)
{
	struct request *rq = *rqp;
	int is_barrier = blk_fs_request(rq) && blk_barrier_rq(rq);

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
			end_that_request_first(rq, -EOPNOTSUPP,
					       rq->hard_nr_sectors);
			end_that_request_last(rq, -EOPNOTSUPP);
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

static int flush_dry_bio_endio(struct bio *bio, unsigned int bytes, int error)
{
	request_queue_t *q = bio->bi_private;
	struct bio_vec *bvec;
	int i;

	/*
	 * This is dry run, restore bio_sector and size.  We'll finish
	 * this request again with the original bi_end_io after an
	 * error occurs or post flush is complete.
	 */
	q->bi_size += bytes;

	if (bio->bi_size)
		return 1;

	/* Rewind bvec's */
	bio->bi_idx = 0;
	bio_for_each_segment(bvec, bio, i) {
		bvec->bv_len += bvec->bv_offset;
		bvec->bv_offset = 0;
	}

	/* Reset bio */
	set_bit(BIO_UPTODATE, &bio->bi_flags);
	bio->bi_size = q->bi_size;
	bio->bi_sector -= (q->bi_size >> 9);
	q->bi_size = 0;

	return 0;
}

static int ordered_bio_endio(struct request *rq, struct bio *bio,
			     unsigned int nbytes, int error)
{
	request_queue_t *q = rq->q;
	bio_end_io_t *endio;
	void *private;

	if (&q->bar_rq != rq)
		return 0;

	/*
	 * Okay, this is the barrier request in progress, dry finish it.
	 */
	if (error && !q->orderr)
		q->orderr = error;

	endio = bio->bi_end_io;
	private = bio->bi_private;
	bio->bi_end_io = flush_dry_bio_endio;
	bio->bi_private = q;

	bio_endio(bio, nbytes, error);

	bio->bi_end_io = endio;
	bio->bi_private = private;

	return 1;
}

/**
 * blk_queue_bounce_limit - set bounce buffer limit for queue
 * @q:  the request queue for the device
 * @dma_addr:   bus address limit
 *
 * Description:
 *    Different hardware can have different requirements as to what pages
 *    it can do I/O directly to. A low level driver can call
 *    blk_queue_bounce_limit to have lower memory pages allocated as bounce
 *    buffers for doing I/O to pages residing above @page.
 **/
void blk_queue_bounce_limit(request_queue_t *q, u64 dma_addr)
{
	unsigned long bounce_pfn = dma_addr >> PAGE_SHIFT;
	int dma = 0;

	q->bounce_gfp = GFP_NOIO;
#if BITS_PER_LONG == 64
	/* Assume anything <= 4GB can be handled by IOMMU.
	   Actually some IOMMUs can handle everything, but I don't
	   know of a way to test this here. */
	if (bounce_pfn < (min_t(u64,0xffffffff,BLK_BOUNCE_HIGH) >> PAGE_SHIFT))
		dma = 1;
	q->bounce_pfn = max_low_pfn;
#else
	if (bounce_pfn < blk_max_low_pfn)
		dma = 1;
	q->bounce_pfn = bounce_pfn;
#endif
	if (dma) {
		init_emergency_isa_pool();
		q->bounce_gfp = GFP_NOIO | GFP_DMA;
		q->bounce_pfn = bounce_pfn;
	}
}

EXPORT_SYMBOL(blk_queue_bounce_limit);

/**
 * blk_queue_max_sectors - set max sectors for a request for this queue
 * @q:  the request queue for the device
 * @max_sectors:  max sectors in the usual 512b unit
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the size of
 *    received requests.
 **/
void blk_queue_max_sectors(request_queue_t *q, unsigned int max_sectors)
{
	if ((max_sectors << 9) < PAGE_CACHE_SIZE) {
		max_sectors = 1 << (PAGE_CACHE_SHIFT - 9);
		printk("%s: set to minimum %d\n", __FUNCTION__, max_sectors);
	}

	if (BLK_DEF_MAX_SECTORS > max_sectors)
		q->max_hw_sectors = q->max_sectors = max_sectors;
 	else {
		q->max_sectors = BLK_DEF_MAX_SECTORS;
		q->max_hw_sectors = max_sectors;
	}
}

EXPORT_SYMBOL(blk_queue_max_sectors);

/**
 * blk_queue_max_phys_segments - set max phys segments for a request for this queue
 * @q:  the request queue for the device
 * @max_segments:  max number of segments
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the number of
 *    physical data segments in a request.  This would be the largest sized
 *    scatter list the driver could handle.
 **/
void blk_queue_max_phys_segments(request_queue_t *q, unsigned short max_segments)
{
	if (!max_segments) {
		max_segments = 1;
		printk("%s: set to minimum %d\n", __FUNCTION__, max_segments);
	}

	q->max_phys_segments = max_segments;
}

EXPORT_SYMBOL(blk_queue_max_phys_segments);

/**
 * blk_queue_max_hw_segments - set max hw segments for a request for this queue
 * @q:  the request queue for the device
 * @max_segments:  max number of segments
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the number of
 *    hw data segments in a request.  This would be the largest number of
 *    address/length pairs the host adapter can actually give as once
 *    to the device.
 **/
void blk_queue_max_hw_segments(request_queue_t *q, unsigned short max_segments)
{
	if (!max_segments) {
		max_segments = 1;
		printk("%s: set to minimum %d\n", __FUNCTION__, max_segments);
	}

	q->max_hw_segments = max_segments;
}

EXPORT_SYMBOL(blk_queue_max_hw_segments);

/**
 * blk_queue_max_segment_size - set max segment size for blk_rq_map_sg
 * @q:  the request queue for the device
 * @max_size:  max size of segment in bytes
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the size of a
 *    coalesced segment
 **/
void blk_queue_max_segment_size(request_queue_t *q, unsigned int max_size)
{
	if (max_size < PAGE_CACHE_SIZE) {
		max_size = PAGE_CACHE_SIZE;
		printk("%s: set to minimum %d\n", __FUNCTION__, max_size);
	}

	q->max_segment_size = max_size;
}

EXPORT_SYMBOL(blk_queue_max_segment_size);

/**
 * blk_queue_hardsect_size - set hardware sector size for the queue
 * @q:  the request queue for the device
 * @size:  the hardware sector size, in bytes
 *
 * Description:
 *   This should typically be set to the lowest possible sector size
 *   that the hardware can operate on (possible without reverting to
 *   even internal read-modify-write operations). Usually the default
 *   of 512 covers most hardware.
 **/
void blk_queue_hardsect_size(request_queue_t *q, unsigned short size)
{
	q->hardsect_size = size;
}

EXPORT_SYMBOL(blk_queue_hardsect_size);

/*
 * Returns the minimum that is _not_ zero, unless both are zero.
 */
#define min_not_zero(l, r) (l == 0) ? r : ((r == 0) ? l : min(l, r))

/**
 * blk_queue_stack_limits - inherit underlying queue limits for stacked drivers
 * @t:	the stacking driver (top)
 * @b:  the underlying device (bottom)
 **/
void blk_queue_stack_limits(request_queue_t *t, request_queue_t *b)
{
	/* zero is "infinity" */
	t->max_sectors = min_not_zero(t->max_sectors,b->max_sectors);
	t->max_hw_sectors = min_not_zero(t->max_hw_sectors,b->max_hw_sectors);

	t->max_phys_segments = min(t->max_phys_segments,b->max_phys_segments);
	t->max_hw_segments = min(t->max_hw_segments,b->max_hw_segments);
	t->max_segment_size = min(t->max_segment_size,b->max_segment_size);
	t->hardsect_size = max(t->hardsect_size,b->hardsect_size);
	if (!test_bit(QUEUE_FLAG_CLUSTER, &b->queue_flags))
		clear_bit(QUEUE_FLAG_CLUSTER, &t->queue_flags);
}

EXPORT_SYMBOL(blk_queue_stack_limits);

/**
 * blk_queue_segment_boundary - set boundary rules for segment merging
 * @q:  the request queue for the device
 * @mask:  the memory boundary mask
 **/
void blk_queue_segment_boundary(request_queue_t *q, unsigned long mask)
{
	if (mask < PAGE_CACHE_SIZE - 1) {
		mask = PAGE_CACHE_SIZE - 1;
		printk("%s: set to minimum %lx\n", __FUNCTION__, mask);
	}

	q->seg_boundary_mask = mask;
}

EXPORT_SYMBOL(blk_queue_segment_boundary);

/**
 * blk_queue_dma_alignment - set dma length and memory alignment
 * @q:     the request queue for the device
 * @mask:  alignment mask
 *
 * description:
 *    set required memory and length aligment for direct dma transactions.
 *    this is used when buiding direct io requests for the queue.
 *
 **/
void blk_queue_dma_alignment(request_queue_t *q, int mask)
{
	q->dma_alignment = mask;
}

EXPORT_SYMBOL(blk_queue_dma_alignment);

/**
 * blk_queue_find_tag - find a request by its tag and queue
 * @q:	 The request queue for the device
 * @tag: The tag of the request
 *
 * Notes:
 *    Should be used when a device returns a tag and you want to match
 *    it with a request.
 *
 *    no locks need be held.
 **/
struct request *blk_queue_find_tag(request_queue_t *q, int tag)
{
	return blk_map_queue_find_tag(q->queue_tags, tag);
}

EXPORT_SYMBOL(blk_queue_find_tag);

/**
 * __blk_free_tags - release a given set of tag maintenance info
 * @bqt:	the tag map to free
 *
 * Tries to free the specified @bqt@.  Returns true if it was
 * actually freed and false if there are still references using it
 */
static int __blk_free_tags(struct blk_queue_tag *bqt)
{
	int retval;

	retval = atomic_dec_and_test(&bqt->refcnt);
	if (retval) {
		BUG_ON(bqt->busy);
		BUG_ON(!list_empty(&bqt->busy_list));

		kfree(bqt->tag_index);
		bqt->tag_index = NULL;

		kfree(bqt->tag_map);
		bqt->tag_map = NULL;

		kfree(bqt);

	}

	return retval;
}

/**
 * __blk_queue_free_tags - release tag maintenance info
 * @q:  the request queue for the device
 *
 *  Notes:
 *    blk_cleanup_queue() will take care of calling this function, if tagging
 *    has been used. So there's no need to call this directly.
 **/
static void __blk_queue_free_tags(request_queue_t *q)
{
	struct blk_queue_tag *bqt = q->queue_tags;

	if (!bqt)
		return;

	__blk_free_tags(bqt);

	q->queue_tags = NULL;
	q->queue_flags &= ~(1 << QUEUE_FLAG_QUEUED);
}


/**
 * blk_free_tags - release a given set of tag maintenance info
 * @bqt:	the tag map to free
 *
 * For externally managed @bqt@ frees the map.  Callers of this
 * function must guarantee to have released all the queues that
 * might have been using this tag map.
 */
void blk_free_tags(struct blk_queue_tag *bqt)
{
	if (unlikely(!__blk_free_tags(bqt)))
		BUG();
}
EXPORT_SYMBOL(blk_free_tags);

/**
 * blk_queue_free_tags - release tag maintenance info
 * @q:  the request queue for the device
 *
 *  Notes:
 *	This is used to disabled tagged queuing to a device, yet leave
 *	queue in function.
 **/
void blk_queue_free_tags(request_queue_t *q)
{
	clear_bit(QUEUE_FLAG_QUEUED, &q->queue_flags);
}

EXPORT_SYMBOL(blk_queue_free_tags);

static int
init_tag_map(request_queue_t *q, struct blk_queue_tag *tags, int depth)
{
	struct request **tag_index;
	unsigned long *tag_map;
	int nr_ulongs;

	if (q && depth > q->nr_requests * 2) {
		depth = q->nr_requests * 2;
		printk(KERN_ERR "%s: adjusted depth to %d\n",
				__FUNCTION__, depth);
	}

	tag_index = kzalloc(depth * sizeof(struct request *), GFP_ATOMIC);
	if (!tag_index)
		goto fail;

	nr_ulongs = ALIGN(depth, BITS_PER_LONG) / BITS_PER_LONG;
	tag_map = kzalloc(nr_ulongs * sizeof(unsigned long), GFP_ATOMIC);
	if (!tag_map)
		goto fail;

	tags->real_max_depth = depth;
	tags->max_depth = depth;
	tags->tag_index = tag_index;
	tags->tag_map = tag_map;

	return 0;
fail:
	kfree(tag_index);
	return -ENOMEM;
}

static struct blk_queue_tag *__blk_queue_init_tags(struct request_queue *q,
						   int depth)
{
	struct blk_queue_tag *tags;

	tags = kmalloc(sizeof(struct blk_queue_tag), GFP_ATOMIC);
	if (!tags)
		goto fail;

	if (init_tag_map(q, tags, depth))
		goto fail;

	INIT_LIST_HEAD(&tags->busy_list);
	tags->busy = 0;
	atomic_set(&tags->refcnt, 1);
	return tags;
fail:
	kfree(tags);
	return NULL;
}

/**
 * blk_init_tags - initialize the tag info for an external tag map
 * @depth:	the maximum queue depth supported
 * @tags: the tag to use
 **/
struct blk_queue_tag *blk_init_tags(int depth)
{
	return __blk_queue_init_tags(NULL, depth);
}
EXPORT_SYMBOL(blk_init_tags);

/**
 * blk_queue_init_tags - initialize the queue tag info
 * @q:  the request queue for the device
 * @depth:  the maximum queue depth supported
 * @tags: the tag to use
 **/
int blk_queue_init_tags(request_queue_t *q, int depth,
			struct blk_queue_tag *tags)
{
	int rc;

	BUG_ON(tags && q->queue_tags && tags != q->queue_tags);

	if (!tags && !q->queue_tags) {
		tags = __blk_queue_init_tags(q, depth);

		if (!tags)
			goto fail;
	} else if (q->queue_tags) {
		if ((rc = blk_queue_resize_tags(q, depth)))
			return rc;
		set_bit(QUEUE_FLAG_QUEUED, &q->queue_flags);
		return 0;
	} else
		atomic_inc(&tags->refcnt);

	/*
	 * assign it, all done
	 */
	q->queue_tags = tags;
	q->queue_flags |= (1 << QUEUE_FLAG_QUEUED);
	return 0;
fail:
	kfree(tags);
	return -ENOMEM;
}

EXPORT_SYMBOL(blk_queue_init_tags);

/**
 * blk_queue_resize_tags - change the queueing depth
 * @q:  the request queue for the device
 * @new_depth: the new max command queueing depth
 *
 *  Notes:
 *    Must be called with the queue lock held.
 **/
int blk_queue_resize_tags(request_queue_t *q, int new_depth)
{
	struct blk_queue_tag *bqt = q->queue_tags;
	struct request **tag_index;
	unsigned long *tag_map;
	int max_depth, nr_ulongs;

	if (!bqt)
		return -ENXIO;

	/*
	 * if we already have large enough real_max_depth.  just
	 * adjust max_depth.  *NOTE* as requests with tag value
	 * between new_depth and real_max_depth can be in-flight, tag
	 * map can not be shrunk blindly here.
	 */
	if (new_depth <= bqt->real_max_depth) {
		bqt->max_depth = new_depth;
		return 0;
	}

	/*
	 * Currently cannot replace a shared tag map with a new
	 * one, so error out if this is the case
	 */
	if (atomic_read(&bqt->refcnt) != 1)
		return -EBUSY;

	/*
	 * save the old state info, so we can copy it back
	 */
	tag_index = bqt->tag_index;
	tag_map = bqt->tag_map;
	max_depth = bqt->real_max_depth;

	if (init_tag_map(q, bqt, new_depth))
		return -ENOMEM;

	memcpy(bqt->tag_index, tag_index, max_depth * sizeof(struct request *));
	nr_ulongs = ALIGN(max_depth, BITS_PER_LONG) / BITS_PER_LONG;
	memcpy(bqt->tag_map, tag_map, nr_ulongs * sizeof(unsigned long));

	kfree(tag_index);
	kfree(tag_map);
	return 0;
}

EXPORT_SYMBOL(blk_queue_resize_tags);

/**
 * blk_queue_end_tag - end tag operations for a request
 * @q:  the request queue for the device
 * @rq: the request that has completed
 *
 *  Description:
 *    Typically called when end_that_request_first() returns 0, meaning
 *    all transfers have been done for a request. It's important to call
 *    this function before end_that_request_last(), as that will put the
 *    request back on the free list thus corrupting the internal tag list.
 *
 *  Notes:
 *   queue lock must be held.
 **/
void blk_queue_end_tag(request_queue_t *q, struct request *rq)
{
	struct blk_queue_tag *bqt = q->queue_tags;
	int tag = rq->tag;

	BUG_ON(tag == -1);

	if (unlikely(tag >= bqt->real_max_depth))
		/*
		 * This can happen after tag depth has been reduced.
		 * FIXME: how about a warning or info message here?
		 */
		return;

	if (unlikely(!__test_and_clear_bit(tag, bqt->tag_map))) {
		printk(KERN_ERR "%s: attempt to clear non-busy tag (%d)\n",
		       __FUNCTION__, tag);
		return;
	}

	list_del_init(&rq->queuelist);
	rq->cmd_flags &= ~REQ_QUEUED;
	rq->tag = -1;

	if (unlikely(bqt->tag_index[tag] == NULL))
		printk(KERN_ERR "%s: tag %d is missing\n",
		       __FUNCTION__, tag);

	bqt->tag_index[tag] = NULL;
	bqt->busy--;
}

EXPORT_SYMBOL(blk_queue_end_tag);

/**
 * blk_queue_start_tag - find a free tag and assign it
 * @q:  the request queue for the device
 * @rq:  the block request that needs tagging
 *
 *  Description:
 *    This can either be used as a stand-alone helper, or possibly be
 *    assigned as the queue &prep_rq_fn (in which case &struct request
 *    automagically gets a tag assigned). Note that this function
 *    assumes that any type of request can be queued! if this is not
 *    true for your device, you must check the request type before
 *    calling this function.  The request will also be removed from
 *    the request queue, so it's the drivers responsibility to readd
 *    it if it should need to be restarted for some reason.
 *
 *  Notes:
 *   queue lock must be held.
 **/
int blk_queue_start_tag(request_queue_t *q, struct request *rq)
{
	struct blk_queue_tag *bqt = q->queue_tags;
	int tag;

	if (unlikely((rq->cmd_flags & REQ_QUEUED))) {
		printk(KERN_ERR 
		       "%s: request %p for device [%s] already tagged %d",
		       __FUNCTION__, rq,
		       rq->rq_disk ? rq->rq_disk->disk_name : "?", rq->tag);
		BUG();
	}

	/*
	 * Protect against shared tag maps, as we may not have exclusive
	 * access to the tag map.
	 */
	do {
		tag = find_first_zero_bit(bqt->tag_map, bqt->max_depth);
		if (tag >= bqt->max_depth)
			return 1;

	} while (test_and_set_bit(tag, bqt->tag_map));

	rq->cmd_flags |= REQ_QUEUED;
	rq->tag = tag;
	bqt->tag_index[tag] = rq;
	blkdev_dequeue_request(rq);
	list_add(&rq->queuelist, &bqt->busy_list);
	bqt->busy++;
	return 0;
}

EXPORT_SYMBOL(blk_queue_start_tag);

/**
 * blk_queue_invalidate_tags - invalidate all pending tags
 * @q:  the request queue for the device
 *
 *  Description:
 *   Hardware conditions may dictate a need to stop all pending requests.
 *   In this case, we will safely clear the block side of the tag queue and
 *   readd all requests to the request queue in the right order.
 *
 *  Notes:
 *   queue lock must be held.
 **/
void blk_queue_invalidate_tags(request_queue_t *q)
{
	struct blk_queue_tag *bqt = q->queue_tags;
	struct list_head *tmp, *n;
	struct request *rq;

	list_for_each_safe(tmp, n, &bqt->busy_list) {
		rq = list_entry_rq(tmp);

		if (rq->tag == -1) {
			printk(KERN_ERR
			       "%s: bad tag found on list\n", __FUNCTION__);
			list_del_init(&rq->queuelist);
			rq->cmd_flags &= ~REQ_QUEUED;
		} else
			blk_queue_end_tag(q, rq);

		rq->cmd_flags &= ~REQ_STARTED;
		__elv_add_request(q, rq, ELEVATOR_INSERT_BACK, 0);
	}
}

EXPORT_SYMBOL(blk_queue_invalidate_tags);

void blk_dump_rq_flags(struct request *rq, char *msg)
{
	int bit;

	printk("%s: dev %s: type=%x, flags=%x\n", msg,
		rq->rq_disk ? rq->rq_disk->disk_name : "?", rq->cmd_type,
		rq->cmd_flags);

	printk("\nsector %llu, nr/cnr %lu/%u\n", (unsigned long long)rq->sector,
						       rq->nr_sectors,
						       rq->current_nr_sectors);
	printk("bio %p, biotail %p, buffer %p, data %p, len %u\n", rq->bio, rq->biotail, rq->buffer, rq->data, rq->data_len);

	if (blk_pc_request(rq)) {
		printk("cdb: ");
		for (bit = 0; bit < sizeof(rq->cmd); bit++)
			printk("%02x ", rq->cmd[bit]);
		printk("\n");
	}
}

EXPORT_SYMBOL(blk_dump_rq_flags);

void blk_recount_segments(request_queue_t *q, struct bio *bio)
{
	struct bio_vec *bv, *bvprv = NULL;
	int i, nr_phys_segs, nr_hw_segs, seg_size, hw_seg_size, cluster;
	int high, highprv = 1;

	if (unlikely(!bio->bi_io_vec))
		return;

	cluster = q->queue_flags & (1 << QUEUE_FLAG_CLUSTER);
	hw_seg_size = seg_size = nr_phys_segs = nr_hw_segs = 0;
	bio_for_each_segment(bv, bio, i) {
		/*
		 * the trick here is making sure that a high page is never
		 * considered part of another segment, since that might
		 * change with the bounce page.
		 */
		high = page_to_pfn(bv->bv_page) >= q->bounce_pfn;
		if (high || highprv)
			goto new_hw_segment;
		if (cluster) {
			if (seg_size + bv->bv_len > q->max_segment_size)
				goto new_segment;
			if (!BIOVEC_PHYS_MERGEABLE(bvprv, bv))
				goto new_segment;
			if (!BIOVEC_SEG_BOUNDARY(q, bvprv, bv))
				goto new_segment;
			if (BIOVEC_VIRT_OVERSIZE(hw_seg_size + bv->bv_len))
				goto new_hw_segment;

			seg_size += bv->bv_len;
			hw_seg_size += bv->bv_len;
			bvprv = bv;
			continue;
		}
new_segment:
		if (BIOVEC_VIRT_MERGEABLE(bvprv, bv) &&
		    !BIOVEC_VIRT_OVERSIZE(hw_seg_size + bv->bv_len)) {
			hw_seg_size += bv->bv_len;
		} else {
new_hw_segment:
			if (hw_seg_size > bio->bi_hw_front_size)
				bio->bi_hw_front_size = hw_seg_size;
			hw_seg_size = BIOVEC_VIRT_START_SIZE(bv) + bv->bv_len;
			nr_hw_segs++;
		}

		nr_phys_segs++;
		bvprv = bv;
		seg_size = bv->bv_len;
		highprv = high;
	}
	if (hw_seg_size > bio->bi_hw_back_size)
		bio->bi_hw_back_size = hw_seg_size;
	if (nr_hw_segs == 1 && hw_seg_size > bio->bi_hw_front_size)
		bio->bi_hw_front_size = hw_seg_size;
	bio->bi_phys_segments = nr_phys_segs;
	bio->bi_hw_segments = nr_hw_segs;
	bio->bi_flags |= (1 << BIO_SEG_VALID);
}


static int blk_phys_contig_segment(request_queue_t *q, struct bio *bio,
				   struct bio *nxt)
{
	if (!(q->queue_flags & (1 << QUEUE_FLAG_CLUSTER)))
		return 0;

	if (!BIOVEC_PHYS_MERGEABLE(__BVEC_END(bio), __BVEC_START(nxt)))
		return 0;
	if (bio->bi_size + nxt->bi_size > q->max_segment_size)
		return 0;

	/*
	 * bio and nxt are contigous in memory, check if the queue allows
	 * these two to be merged into one
	 */
	if (BIO_SEG_BOUNDARY(q, bio, nxt))
		return 1;

	return 0;
}

static int blk_hw_contig_segment(request_queue_t *q, struct bio *bio,
				 struct bio *nxt)
{
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);
	if (unlikely(!bio_flagged(nxt, BIO_SEG_VALID)))
		blk_recount_segments(q, nxt);
	if (!BIOVEC_VIRT_MERGEABLE(__BVEC_END(bio), __BVEC_START(nxt)) ||
	    BIOVEC_VIRT_OVERSIZE(bio->bi_hw_front_size + bio->bi_hw_back_size))
		return 0;
	if (bio->bi_size + nxt->bi_size > q->max_segment_size)
		return 0;

	return 1;
}

/*
 * map a request to scatterlist, return number of sg entries setup. Caller
 * must make sure sg can hold rq->nr_phys_segments entries
 */
int blk_rq_map_sg(request_queue_t *q, struct request *rq, struct scatterlist *sg)
{
	struct bio_vec *bvec, *bvprv;
	struct bio *bio;
	int nsegs, i, cluster;

	nsegs = 0;
	cluster = q->queue_flags & (1 << QUEUE_FLAG_CLUSTER);

	/*
	 * for each bio in rq
	 */
	bvprv = NULL;
	rq_for_each_bio(bio, rq) {
		/*
		 * for each segment in bio
		 */
		bio_for_each_segment(bvec, bio, i) {
			int nbytes = bvec->bv_len;

			if (bvprv && cluster) {
				if (sg[nsegs - 1].length + nbytes > q->max_segment_size)
					goto new_segment;

				if (!BIOVEC_PHYS_MERGEABLE(bvprv, bvec))
					goto new_segment;
				if (!BIOVEC_SEG_BOUNDARY(q, bvprv, bvec))
					goto new_segment;

				sg[nsegs - 1].length += nbytes;
			} else {
new_segment:
				memset(&sg[nsegs],0,sizeof(struct scatterlist));
				sg[nsegs].page = bvec->bv_page;
				sg[nsegs].length = nbytes;
				sg[nsegs].offset = bvec->bv_offset;

				nsegs++;
			}
			bvprv = bvec;
		} /* segments in bio */
	} /* bios in rq */

	return nsegs;
}

EXPORT_SYMBOL(blk_rq_map_sg);

/*
 * the standard queue merge functions, can be overridden with device
 * specific ones if so desired
 */

static inline int ll_new_mergeable(request_queue_t *q,
				   struct request *req,
				   struct bio *bio)
{
	int nr_phys_segs = bio_phys_segments(q, bio);

	if (req->nr_phys_segments + nr_phys_segs > q->max_phys_segments) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}

	/*
	 * A hw segment is just getting larger, bump just the phys
	 * counter.
	 */
	req->nr_phys_segments += nr_phys_segs;
	return 1;
}

static inline int ll_new_hw_segment(request_queue_t *q,
				    struct request *req,
				    struct bio *bio)
{
	int nr_hw_segs = bio_hw_segments(q, bio);
	int nr_phys_segs = bio_phys_segments(q, bio);

	if (req->nr_hw_segments + nr_hw_segs > q->max_hw_segments
	    || req->nr_phys_segments + nr_phys_segs > q->max_phys_segments) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}

	/*
	 * This will form the start of a new hw segment.  Bump both
	 * counters.
	 */
	req->nr_hw_segments += nr_hw_segs;
	req->nr_phys_segments += nr_phys_segs;
	return 1;
}

static int ll_back_merge_fn(request_queue_t *q, struct request *req, 
			    struct bio *bio)
{
	unsigned short max_sectors;
	int len;

	if (unlikely(blk_pc_request(req)))
		max_sectors = q->max_hw_sectors;
	else
		max_sectors = q->max_sectors;

	if (req->nr_sectors + bio_sectors(bio) > max_sectors) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}
	if (unlikely(!bio_flagged(req->biotail, BIO_SEG_VALID)))
		blk_recount_segments(q, req->biotail);
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);
	len = req->biotail->bi_hw_back_size + bio->bi_hw_front_size;
	if (BIOVEC_VIRT_MERGEABLE(__BVEC_END(req->biotail), __BVEC_START(bio)) &&
	    !BIOVEC_VIRT_OVERSIZE(len)) {
		int mergeable =  ll_new_mergeable(q, req, bio);

		if (mergeable) {
			if (req->nr_hw_segments == 1)
				req->bio->bi_hw_front_size = len;
			if (bio->bi_hw_segments == 1)
				bio->bi_hw_back_size = len;
		}
		return mergeable;
	}

	return ll_new_hw_segment(q, req, bio);
}

static int ll_front_merge_fn(request_queue_t *q, struct request *req, 
			     struct bio *bio)
{
	unsigned short max_sectors;
	int len;

	if (unlikely(blk_pc_request(req)))
		max_sectors = q->max_hw_sectors;
	else
		max_sectors = q->max_sectors;


	if (req->nr_sectors + bio_sectors(bio) > max_sectors) {
		req->cmd_flags |= REQ_NOMERGE;
		if (req == q->last_merge)
			q->last_merge = NULL;
		return 0;
	}
	len = bio->bi_hw_back_size + req->bio->bi_hw_front_size;
	if (unlikely(!bio_flagged(bio, BIO_SEG_VALID)))
		blk_recount_segments(q, bio);
	if (unlikely(!bio_flagged(req->bio, BIO_SEG_VALID)))
		blk_recount_segments(q, req->bio);
	if (BIOVEC_VIRT_MERGEABLE(__BVEC_END(bio), __BVEC_START(req->bio)) &&
	    !BIOVEC_VIRT_OVERSIZE(len)) {
		int mergeable =  ll_new_mergeable(q, req, bio);

		if (mergeable) {
			if (bio->bi_hw_segments == 1)
				bio->bi_hw_front_size = len;
			if (req->nr_hw_segments == 1)
				req->biotail->bi_hw_back_size = len;
		}
		return mergeable;
	}

	return ll_new_hw_segment(q, req, bio);
}

static int ll_merge_requests_fn(request_queue_t *q, struct request *req,
				struct request *next)
{
	int total_phys_segments;
	int total_hw_segments;

	/*
	 * First check if the either of the requests are re-queued
	 * requests.  Can't merge them if they are.
	 */
	if (req->special || next->special)
		return 0;

	/*
	 * Will it become too large?
	 */
	if ((req->nr_sectors + next->nr_sectors) > q->max_sectors)
		return 0;

	total_phys_segments = req->nr_phys_segments + next->nr_phys_segments;
	if (blk_phys_contig_segment(q, req->biotail, next->bio))
		total_phys_segments--;

	if (total_phys_segments > q->max_phys_segments)
		return 0;

	total_hw_segments = req->nr_hw_segments + next->nr_hw_segments;
	if (blk_hw_contig_segment(q, req->biotail, next->bio)) {
		int len = req->biotail->bi_hw_back_size + next->bio->bi_hw_front_size;
		/*
		 * propagate the combined length to the end of the requests
		 */
		if (req->nr_hw_segments == 1)
			req->bio->bi_hw_front_size = len;
		if (next->nr_hw_segments == 1)
			next->biotail->bi_hw_back_size = len;
		total_hw_segments--;
	}

	if (total_hw_segments > q->max_hw_segments)
		return 0;

	/* Merge is OK... */
	req->nr_phys_segments = total_phys_segments;
	req->nr_hw_segments = total_hw_segments;
	return 1;
}

/*
 * "plug" the device if there are no outstanding requests: this will
 * force the transfer to start only after we have put all the requests
 * on the list.
 *
 * This is called with interrupts off and no requests on the queue and
 * with the queue lock held.
 */
void blk_plug_device(request_queue_t *q)
{
	WARN_ON(!irqs_disabled());

	/*
	 * don't plug a stopped queue, it must be paired with blk_start_queue()
	 * which will restart the queueing
	 */
	if (blk_queue_stopped(q))
		return;

	if (!test_and_set_bit(QUEUE_FLAG_PLUGGED, &q->queue_flags)) {
		mod_timer(&q->unplug_timer, jiffies + q->unplug_delay);
		blk_add_trace_generic(q, NULL, 0, BLK_TA_PLUG);
	}
}

EXPORT_SYMBOL(blk_plug_device);

/*
 * remove the queue from the plugged list, if present. called with
 * queue lock held and interrupts disabled.
 */
int blk_remove_plug(request_queue_t *q)
{
	WARN_ON(!irqs_disabled());

	if (!test_and_clear_bit(QUEUE_FLAG_PLUGGED, &q->queue_flags))
		return 0;

	del_timer(&q->unplug_timer);
	return 1;
}

EXPORT_SYMBOL(blk_remove_plug);

/*
 * remove the plug and let it rip..
 */
void __generic_unplug_device(request_queue_t *q)
{
	if (unlikely(blk_queue_stopped(q)))
		return;

	if (!blk_remove_plug(q))
		return;

	q->request_fn(q);
}
EXPORT_SYMBOL(__generic_unplug_device);

/**
 * generic_unplug_device - fire a request queue
 * @q:    The &request_queue_t in question
 *
 * Description:
 *   Linux uses plugging to build bigger requests queues before letting
 *   the device have at them. If a queue is plugged, the I/O scheduler
 *   is still adding and merging requests on the queue. Once the queue
 *   gets unplugged, the request_fn defined for the queue is invoked and
 *   transfers started.
 **/
void generic_unplug_device(request_queue_t *q)
{
	spin_lock_irq(q->queue_lock);
	__generic_unplug_device(q);
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL(generic_unplug_device);

static void blk_backing_dev_unplug(struct backing_dev_info *bdi,
				   struct page *page)
{
	request_queue_t *q = bdi->unplug_io_data;

	/*
	 * devices don't necessarily have an ->unplug_fn defined
	 */
	if (q->unplug_fn) {
		blk_add_trace_pdu_int(q, BLK_TA_UNPLUG_IO, NULL,
					q->rq.count[READ] + q->rq.count[WRITE]);

		q->unplug_fn(q);
	}
}

static void blk_unplug_work(struct work_struct *work)
{
	request_queue_t *q = container_of(work, request_queue_t, unplug_work);

	blk_add_trace_pdu_int(q, BLK_TA_UNPLUG_IO, NULL,
				q->rq.count[READ] + q->rq.count[WRITE]);

	q->unplug_fn(q);
}

static void blk_unplug_timeout(unsigned long data)
{
	request_queue_t *q = (request_queue_t *)data;

	blk_add_trace_pdu_int(q, BLK_TA_UNPLUG_TIMER, NULL,
				q->rq.count[READ] + q->rq.count[WRITE]);

	kblockd_schedule_work(&q->unplug_work);
}

/**
 * blk_start_queue - restart a previously stopped queue
 * @q:    The &request_queue_t in question
 *
 * Description:
 *   blk_start_queue() will clear the stop flag on the queue, and call
 *   the request_fn for the queue if it was in a stopped state when
 *   entered. Also see blk_stop_queue(). Queue lock must be held.
 **/
void blk_start_queue(request_queue_t *q)
{
	WARN_ON(!irqs_disabled());

	clear_bit(QUEUE_FLAG_STOPPED, &q->queue_flags);

	/*
	 * one level of recursion is ok and is much faster than kicking
	 * the unplug handling
	 */
	if (!test_and_set_bit(QUEUE_FLAG_REENTER, &q->queue_flags)) {
		q->request_fn(q);
		clear_bit(QUEUE_FLAG_REENTER, &q->queue_flags);
	} else {
		blk_plug_device(q);
		kblockd_schedule_work(&q->unplug_work);
	}
}

EXPORT_SYMBOL(blk_start_queue);

/**
 * blk_stop_queue - stop a queue
 * @q:    The &request_queue_t in question
 *
 * Description:
 *   The Linux block layer assumes that a block driver will consume all
 *   entries on the request queue when the request_fn strategy is called.
 *   Often this will not happen, because of hardware limitations (queue
 *   depth settings). If a device driver gets a 'queue full' response,
 *   or if it simply chooses not to queue more I/O at one point, it can
 *   call this function to prevent the request_fn from being called until
 *   the driver has signalled it's ready to go again. This happens by calling
 *   blk_start_queue() to restart queue operations. Queue lock must be held.
 **/
void blk_stop_queue(request_queue_t *q)
{
	blk_remove_plug(q);
	set_bit(QUEUE_FLAG_STOPPED, &q->queue_flags);
}
EXPORT_SYMBOL(blk_stop_queue);

/**
 * blk_sync_queue - cancel any pending callbacks on a queue
 * @q: the queue
 *
 * Description:
 *     The block layer may perform asynchronous callback activity
 *     on a queue, such as calling the unplug function after a timeout.
 *     A block device may call blk_sync_queue to ensure that any
 *     such activity is cancelled, thus allowing it to release resources
 *     the the callbacks might use. The caller must already have made sure
 *     that its ->make_request_fn will not re-add plugging prior to calling
 *     this function.
 *
 */
void blk_sync_queue(struct request_queue *q)
{
	del_timer_sync(&q->unplug_timer);
	kblockd_flush();
}
EXPORT_SYMBOL(blk_sync_queue);

/**
 * blk_run_queue - run a single device queue
 * @q:	The queue to run
 */
void blk_run_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	blk_remove_plug(q);

	/*
	 * Only recurse once to avoid overrunning the stack, let the unplug
	 * handling reinvoke the handler shortly if we already got there.
	 */
	if (!elv_queue_empty(q)) {
		if (!test_and_set_bit(QUEUE_FLAG_REENTER, &q->queue_flags)) {
			q->request_fn(q);
			clear_bit(QUEUE_FLAG_REENTER, &q->queue_flags);
		} else {
			blk_plug_device(q);
			kblockd_schedule_work(&q->unplug_work);
		}
	}

	spin_unlock_irqrestore(q->queue_lock, flags);
}
EXPORT_SYMBOL(blk_run_queue);

/**
 * blk_cleanup_queue: - release a &request_queue_t when it is no longer needed
 * @kobj:    the kobj belonging of the request queue to be released
 *
 * Description:
 *     blk_cleanup_queue is the pair to blk_init_queue() or
 *     blk_queue_make_request().  It should be called when a request queue is
 *     being released; typically when a block device is being de-registered.
 *     Currently, its primary task it to free all the &struct request
 *     structures that were allocated to the queue and the queue itself.
 *
 * Caveat:
 *     Hopefully the low level driver will have finished any
 *     outstanding requests first...
 **/
static void blk_release_queue(struct kobject *kobj)
{
	request_queue_t *q = container_of(kobj, struct request_queue, kobj);
	struct request_list *rl = &q->rq;

	blk_sync_queue(q);

	if (rl->rq_pool)
		mempool_destroy(rl->rq_pool);

	if (q->queue_tags)
		__blk_queue_free_tags(q);

	blk_trace_shutdown(q);

	kmem_cache_free(requestq_cachep, q);
}

void blk_put_queue(request_queue_t *q)
{
	kobject_put(&q->kobj);
}
EXPORT_SYMBOL(blk_put_queue);

void blk_cleanup_queue(request_queue_t * q)
{
	mutex_lock(&q->sysfs_lock);
	set_bit(QUEUE_FLAG_DEAD, &q->queue_flags);
	mutex_unlock(&q->sysfs_lock);

	if (q->elevator)
		elevator_exit(q->elevator);

	blk_put_queue(q);
}

EXPORT_SYMBOL(blk_cleanup_queue);

static int blk_init_free_list(request_queue_t *q)
{
	struct request_list *rl = &q->rq;

	rl->count[READ] = rl->count[WRITE] = 0;
	rl->starved[READ] = rl->starved[WRITE] = 0;
	rl->elvpriv = 0;
	init_waitqueue_head(&rl->wait[READ]);
	init_waitqueue_head(&rl->wait[WRITE]);

	rl->rq_pool = mempool_create_node(BLKDEV_MIN_RQ, mempool_alloc_slab,
				mempool_free_slab, request_cachep, q->node);

	if (!rl->rq_pool)
		return -ENOMEM;

	return 0;
}

request_queue_t *blk_alloc_queue(gfp_t gfp_mask)
{
	return blk_alloc_queue_node(gfp_mask, -1);
}
EXPORT_SYMBOL(blk_alloc_queue);

static struct kobj_type queue_ktype;

request_queue_t *blk_alloc_queue_node(gfp_t gfp_mask, int node_id)
{
	request_queue_t *q;

	q = kmem_cache_alloc_node(requestq_cachep, gfp_mask, node_id);
	if (!q)
		return NULL;

	memset(q, 0, sizeof(*q));
	init_timer(&q->unplug_timer);

	snprintf(q->kobj.name, KOBJ_NAME_LEN, "%s", "queue");
	q->kobj.ktype = &queue_ktype;
	kobject_init(&q->kobj);

	q->backing_dev_info.unplug_io_fn = blk_backing_dev_unplug;
	q->backing_dev_info.unplug_io_data = q;

	mutex_init(&q->sysfs_lock);

	return q;
}
EXPORT_SYMBOL(blk_alloc_queue_node);

/**
 * blk_init_queue  - prepare a request queue for use with a block device
 * @rfn:  The function to be called to process requests that have been
 *        placed on the queue.
 * @lock: Request queue spin lock
 *
 * Description:
 *    If a block device wishes to use the standard request handling procedures,
 *    which sorts requests and coalesces adjacent requests, then it must
 *    call blk_init_queue().  The function @rfn will be called when there
 *    are requests on the queue that need to be processed.  If the device
 *    supports plugging, then @rfn may not be called immediately when requests
 *    are available on the queue, but may be called at some time later instead.
 *    Plugged queues are generally unplugged when a buffer belonging to one
 *    of the requests on the queue is needed, or due to memory pressure.
 *
 *    @rfn is not required, or even expected, to remove all requests off the
 *    queue, but only as many as it can handle at a time.  If it does leave
 *    requests on the queue, it is responsible for arranging that the requests
 *    get dealt with eventually.
 *
 *    The queue spin lock must be held while manipulating the requests on the
 *    request queue; this lock will be taken also from interrupt context, so irq
 *    disabling is needed for it.
 *
 *    Function returns a pointer to the initialized request queue, or NULL if
 *    it didn't succeed.
 *
 * Note:
 *    blk_init_queue() must be paired with a blk_cleanup_queue() call
 *    when the block device is deactivated (such as at module unload).
 **/

request_queue_t *blk_init_queue(request_fn_proc *rfn, spinlock_t *lock)
{
	return blk_init_queue_node(rfn, lock, -1);
}
EXPORT_SYMBOL(blk_init_queue);

request_queue_t *
blk_init_queue_node(request_fn_proc *rfn, spinlock_t *lock, int node_id)
{
	request_queue_t *q = blk_alloc_queue_node(GFP_KERNEL, node_id);

	if (!q)
		return NULL;

	q->node = node_id;
	if (blk_init_free_list(q)) {
		kmem_cache_free(requestq_cachep, q);
		return NULL;
	}

	/*
	 * if caller didn't supply a lock, they get per-queue locking with
	 * our embedded lock
	 */
	if (!lock) {
		spin_lock_init(&q->__queue_lock);
		lock = &q->__queue_lock;
	}

	q->request_fn		= rfn;
	q->back_merge_fn       	= ll_back_merge_fn;
	q->front_merge_fn      	= ll_front_merge_fn;
	q->merge_requests_fn	= ll_merge_requests_fn;
	q->prep_rq_fn		= NULL;
	q->unplug_fn		= generic_unplug_device;
	q->queue_flags		= (1 << QUEUE_FLAG_CLUSTER);
	q->queue_lock		= lock;

	blk_queue_segment_boundary(q, 0xffffffff);

	blk_queue_make_request(q, __make_request);
	blk_queue_max_segment_size(q, MAX_SEGMENT_SIZE);

	blk_queue_max_hw_segments(q, MAX_HW_SEGMENTS);
	blk_queue_max_phys_segments(q, MAX_PHYS_SEGMENTS);

	/*
	 * all done
	 */
	if (!elevator_init(q, NULL)) {
		blk_queue_congestion_threshold(q);
		return q;
	}

	blk_put_queue(q);
	return NULL;
}
EXPORT_SYMBOL(blk_init_queue_node);

int blk_get_queue(request_queue_t *q)
{
	if (likely(!test_bit(QUEUE_FLAG_DEAD, &q->queue_flags))) {
		kobject_get(&q->kobj);
		return 0;
	}

	return 1;
}

EXPORT_SYMBOL(blk_get_queue);

static inline void blk_free_request(request_queue_t *q, struct request *rq)
{
	if (rq->cmd_flags & REQ_ELVPRIV)
		elv_put_request(q, rq);
	mempool_free(rq, q->rq.rq_pool);
}

static struct request *
blk_alloc_request(request_queue_t *q, int rw, int priv, gfp_t gfp_mask)
{
	struct request *rq = mempool_alloc(q->rq.rq_pool, gfp_mask);

	if (!rq)
		return NULL;

	/*
	 * first three bits are identical in rq->cmd_flags and bio->bi_rw,
	 * see bio.h and blkdev.h
	 */
	rq->cmd_flags = rw | REQ_ALLOCED;

	if (priv) {
		if (unlikely(elv_set_request(q, rq, gfp_mask))) {
			mempool_free(rq, q->rq.rq_pool);
			return NULL;
		}
		rq->cmd_flags |= REQ_ELVPRIV;
	}

	return rq;
}

/*
 * ioc_batching returns true if the ioc is a valid batching request and
 * should be given priority access to a request.
 */
static inline int ioc_batching(request_queue_t *q, struct io_context *ioc)
{
	if (!ioc)
		return 0;

	/*
	 * Make sure the process is able to allocate at least 1 request
	 * even if the batch times out, otherwise we could theoretically
	 * lose wakeups.
	 */
	return ioc->nr_batch_requests == q->nr_batching ||
		(ioc->nr_batch_requests > 0
		&& time_before(jiffies, ioc->last_waited + BLK_BATCH_TIME));
}

/*
 * ioc_set_batching sets ioc to be a new "batcher" if it is not one. This
 * will cause the process to be a "batcher" on all queues in the system. This
 * is the behaviour we want though - once it gets a wakeup it should be given
 * a nice run.
 */
static void ioc_set_batching(request_queue_t *q, struct io_context *ioc)
{
	if (!ioc || ioc_batching(q, ioc))
		return;

	ioc->nr_batch_requests = q->nr_batching;
	ioc->last_waited = jiffies;
}

static void __freed_request(request_queue_t *q, int rw)
{
	struct request_list *rl = &q->rq;

	if (rl->count[rw] < queue_congestion_off_threshold(q))
		blk_clear_queue_congested(q, rw);

	if (rl->count[rw] + 1 <= q->nr_requests) {
		if (waitqueue_active(&rl->wait[rw]))
			wake_up(&rl->wait[rw]);

		blk_clear_queue_full(q, rw);
	}
}

/*
 * A request has just been released.  Account for it, update the full and
 * congestion status, wake up any waiters.   Called under q->queue_lock.
 */
static void freed_request(request_queue_t *q, int rw, int priv)
{
	struct request_list *rl = &q->rq;

	rl->count[rw]--;
	if (priv)
		rl->elvpriv--;

	__freed_request(q, rw);

	if (unlikely(rl->starved[rw ^ 1]))
		__freed_request(q, rw ^ 1);
}

#define blkdev_free_rq(list) list_entry((list)->next, struct request, queuelist)
/*
 * Get a free request, queue_lock must be held.
 * Returns NULL on failure, with queue_lock held.
 * Returns !NULL on success, with queue_lock *not held*.
 */
static struct request *get_request(request_queue_t *q, int rw_flags,
				   struct bio *bio, gfp_t gfp_mask)
{
	struct request *rq = NULL;
	struct request_list *rl = &q->rq;
	struct io_context *ioc = NULL;
	const int rw = rw_flags & 0x01;
	int may_queue, priv;

	may_queue = elv_may_queue(q, rw_flags);
	if (may_queue == ELV_MQUEUE_NO)
		goto rq_starved;

	if (rl->count[rw]+1 >= queue_congestion_on_threshold(q)) {
		if (rl->count[rw]+1 >= q->nr_requests) {
			ioc = current_io_context(GFP_ATOMIC, q->node);
			/*
			 * The queue will fill after this allocation, so set
			 * it as full, and mark this process as "batching".
			 * This process will be allowed to complete a batch of
			 * requests, others will be blocked.
			 */
			if (!blk_queue_full(q, rw)) {
				ioc_set_batching(q, ioc);
				blk_set_queue_full(q, rw);
			} else {
				if (may_queue != ELV_MQUEUE_MUST
						&& !ioc_batching(q, ioc)) {
					/*
					 * The queue is full and the allocating
					 * process is not a "batcher", and not
					 * exempted by the IO scheduler
					 */
					goto out;
				}
			}
		}
		blk_set_queue_congested(q, rw);
	}

	/*
	 * Only allow batching queuers to allocate up to 50% over the defined
	 * limit of requests, otherwise we could have thousands of requests
	 * allocated with any setting of ->nr_requests
	 */
	if (rl->count[rw] >= (3 * q->nr_requests / 2))
		goto out;

	rl->count[rw]++;
	rl->starved[rw] = 0;

	priv = !test_bit(QUEUE_FLAG_ELVSWITCH, &q->queue_flags);
	if (priv)
		rl->elvpriv++;

	spin_unlock_irq(q->queue_lock);

	rq = blk_alloc_request(q, rw_flags, priv, gfp_mask);
	if (unlikely(!rq)) {
		/*
		 * Allocation failed presumably due to memory. Undo anything
		 * we might have messed up.
		 *
		 * Allocating task should really be put onto the front of the
		 * wait queue, but this is pretty rare.
		 */
		spin_lock_irq(q->queue_lock);
		freed_request(q, rw, priv);

		/*
		 * in the very unlikely event that allocation failed and no
		 * requests for this direction was pending, mark us starved
		 * so that freeing of a request in the other direction will
		 * notice us. another possible fix would be to split the
		 * rq mempool into READ and WRITE
		 */
rq_starved:
		if (unlikely(rl->count[rw] == 0))
			rl->starved[rw] = 1;

		goto out;
	}

	/*
	 * ioc may be NULL here, and ioc_batching will be false. That's
	 * OK, if the queue is under the request limit then requests need
	 * not count toward the nr_batch_requests limit. There will always
	 * be some limit enforced by BLK_BATCH_TIME.
	 */
	if (ioc_batching(q, ioc))
		ioc->nr_batch_requests--;
	
	rq_init(q, rq);

	blk_add_trace_generic(q, bio, rw, BLK_TA_GETRQ);
out:
	return rq;
}

/*
 * No available requests for this queue, unplug the device and wait for some
 * requests to become available.
 *
 * Called with q->queue_lock held, and returns with it unlocked.
 */
static struct request *get_request_wait(request_queue_t *q, int rw_flags,
					struct bio *bio)
{
	const int rw = rw_flags & 0x01;
	struct request *rq;

	rq = get_request(q, rw_flags, bio, GFP_NOIO);
	while (!rq) {
		DEFINE_WAIT(wait);
		struct request_list *rl = &q->rq;

		prepare_to_wait_exclusive(&rl->wait[rw], &wait,
				TASK_UNINTERRUPTIBLE);

		rq = get_request(q, rw_flags, bio, GFP_NOIO);

		if (!rq) {
			struct io_context *ioc;

			blk_add_trace_generic(q, bio, rw, BLK_TA_SLEEPRQ);

			__generic_unplug_device(q);
			spin_unlock_irq(q->queue_lock);
			io_schedule();

			/*
			 * After sleeping, we become a "batching" process and
			 * will be able to allocate at least one request, and
			 * up to a big batch of them for a small period time.
			 * See ioc_batching, ioc_set_batching
			 */
			ioc = current_io_context(GFP_NOIO, q->node);
			ioc_set_batching(q, ioc);

			spin_lock_irq(q->queue_lock);
		}
		finish_wait(&rl->wait[rw], &wait);
	}

	return rq;
}

struct request *blk_get_request(request_queue_t *q, int rw, gfp_t gfp_mask)
{
	struct request *rq;

	BUG_ON(rw != READ && rw != WRITE);

	spin_lock_irq(q->queue_lock);
	if (gfp_mask & __GFP_WAIT) {
		rq = get_request_wait(q, rw, NULL);
	} else {
		rq = get_request(q, rw, NULL, gfp_mask);
		if (!rq)
			spin_unlock_irq(q->queue_lock);
	}
	/* q->queue_lock is unlocked at this point */

	return rq;
}
EXPORT_SYMBOL(blk_get_request);

/**
 * blk_start_queueing - initiate dispatch of requests to device
 * @q:		request queue to kick into gear
 *
 * This is basically a helper to remove the need to know whether a queue
 * is plugged or not if someone just wants to initiate dispatch of requests
 * for this queue.
 *
 * The queue lock must be held with interrupts disabled.
 */
void blk_start_queueing(request_queue_t *q)
{
	if (!blk_queue_plugged(q))
		q->request_fn(q);
	else
		__generic_unplug_device(q);
}
EXPORT_SYMBOL(blk_start_queueing);

/**
 * blk_requeue_request - put a request back on queue
 * @q:		request queue where request should be inserted
 * @rq:		request to be inserted
 *
 * Description:
 *    Drivers often keep queueing requests until the hardware cannot accept
 *    more, when that condition happens we need to put the request back
 *    on the queue. Must be called with queue lock held.
 */
void blk_requeue_request(request_queue_t *q, struct request *rq)
{
	blk_add_trace_rq(q, rq, BLK_TA_REQUEUE);

	if (blk_rq_tagged(rq))
		blk_queue_end_tag(q, rq);

	elv_requeue_request(q, rq);
}

EXPORT_SYMBOL(blk_requeue_request);

/**
 * blk_insert_request - insert a special request in to a request queue
 * @q:		request queue where request should be inserted
 * @rq:		request to be inserted
 * @at_head:	insert request at head or tail of queue
 * @data:	private data
 *
 * Description:
 *    Many block devices need to execute commands asynchronously, so they don't
 *    block the whole kernel from preemption during request execution.  This is
 *    accomplished normally by inserting aritficial requests tagged as
 *    REQ_SPECIAL in to the corresponding request queue, and letting them be
 *    scheduled for actual execution by the request queue.
 *
 *    We have the option of inserting the head or the tail of the queue.
 *    Typically we use the tail for new ioctls and so forth.  We use the head
 *    of the queue for things like a QUEUE_FULL message from a device, or a
 *    host that is unable to accept a particular command.
 */
void blk_insert_request(request_queue_t *q, struct request *rq,
			int at_head, void *data)
{
	int where = at_head ? ELEVATOR_INSERT_FRONT : ELEVATOR_INSERT_BACK;
	unsigned long flags;

	/*
	 * tell I/O scheduler that this isn't a regular read/write (ie it
	 * must not attempt merges on this) and that it acts as a soft
	 * barrier
	 */
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd_flags |= REQ_SOFTBARRIER;

	rq->special = data;

	spin_lock_irqsave(q->queue_lock, flags);

	/*
	 * If command is tagged, release the tag
	 */
	if (blk_rq_tagged(rq))
		blk_queue_end_tag(q, rq);

	drive_stat_acct(rq, rq->nr_sectors, 1);
	__elv_add_request(q, rq, where, 0);
	blk_start_queueing(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

EXPORT_SYMBOL(blk_insert_request);

static int __blk_rq_unmap_user(struct bio *bio)
{
	int ret = 0;

	if (bio) {
		if (bio_flagged(bio, BIO_USER_MAPPED))
			bio_unmap_user(bio);
		else
			ret = bio_uncopy_user(bio);
	}

	return ret;
}

static int __blk_rq_map_user(request_queue_t *q, struct request *rq,
			     void __user *ubuf, unsigned int len)
{
	unsigned long uaddr;
	struct bio *bio, *orig_bio;
	int reading, ret;

	reading = rq_data_dir(rq) == READ;

	/*
	 * if alignment requirement is satisfied, map in user pages for
	 * direct dma. else, set up kernel bounce buffers
	 */
	uaddr = (unsigned long) ubuf;
	if (!(uaddr & queue_dma_alignment(q)) && !(len & queue_dma_alignment(q)))
		bio = bio_map_user(q, NULL, uaddr, len, reading);
	else
		bio = bio_copy_user(q, uaddr, len, reading);

	if (IS_ERR(bio)) {
		return PTR_ERR(bio);
	}

	orig_bio = bio;
	blk_queue_bounce(q, &bio);
	/*
	 * We link the bounce buffer in and could have to traverse it
	 * later so we have to get a ref to prevent it from being freed
	 */
	bio_get(bio);

	/*
	 * for most (all? don't know of any) queues we could
	 * skip grabbing the queue lock here. only drivers with
	 * funky private ->back_merge_fn() function could be
	 * problematic.
	 */
	spin_lock_irq(q->queue_lock);
	if (!rq->bio)
		blk_rq_bio_prep(q, rq, bio);
	else if (!q->back_merge_fn(q, rq, bio)) {
		ret = -EINVAL;
		spin_unlock_irq(q->queue_lock);
		goto unmap_bio;
	} else {
		rq->biotail->bi_next = bio;
		rq->biotail = bio;

		rq->nr_sectors += bio_sectors(bio);
		rq->hard_nr_sectors = rq->nr_sectors;
		rq->data_len += bio->bi_size;
	}
	spin_unlock_irq(q->queue_lock);

	return bio->bi_size;

unmap_bio:
	/* if it was boucned we must call the end io function */
	bio_endio(bio, bio->bi_size, 0);
	__blk_rq_unmap_user(orig_bio);
	bio_put(bio);
	return ret;
}

/**
 * blk_rq_map_user - map user data to a request, for REQ_BLOCK_PC usage
 * @q:		request queue where request should be inserted
 * @rq:		request structure to fill
 * @ubuf:	the user buffer
 * @len:	length of user data
 *
 * Description:
 *    Data will be mapped directly for zero copy io, if possible. Otherwise
 *    a kernel bounce buffer is used.
 *
 *    A matching blk_rq_unmap_user() must be issued at the end of io, while
 *    still in process context.
 *
 *    Note: The mapped bio may need to be bounced through blk_queue_bounce()
 *    before being submitted to the device, as pages mapped may be out of
 *    reach. It's the callers responsibility to make sure this happens. The
 *    original bio must be passed back in to blk_rq_unmap_user() for proper
 *    unmapping.
 */
int blk_rq_map_user(request_queue_t *q, struct request *rq, void __user *ubuf,
		    unsigned long len)
{
	unsigned long bytes_read = 0;
	int ret;

	if (len > (q->max_hw_sectors << 9))
		return -EINVAL;
	if (!len || !ubuf)
		return -EINVAL;

	while (bytes_read != len) {
		unsigned long map_len, end, start;

		map_len = min_t(unsigned long, len - bytes_read, BIO_MAX_SIZE);
		end = ((unsigned long)ubuf + map_len + PAGE_SIZE - 1)
								>> PAGE_SHIFT;
		start = (unsigned long)ubuf >> PAGE_SHIFT;

		/*
		 * A bad offset could cause us to require BIO_MAX_PAGES + 1
		 * pages. If this happens we just lower the requested
		 * mapping len by a page so that we can fit
		 */
		if (end - start > BIO_MAX_PAGES)
			map_len -= PAGE_SIZE;

		ret = __blk_rq_map_user(q, rq, ubuf, map_len);
		if (ret < 0)
			goto unmap_rq;
		bytes_read += ret;
		ubuf += ret;
	}

	rq->buffer = rq->data = NULL;
	return 0;
unmap_rq:
	blk_rq_unmap_user(rq);
	return ret;
}

EXPORT_SYMBOL(blk_rq_map_user);

/**
 * blk_rq_map_user_iov - map user data to a request, for REQ_BLOCK_PC usage
 * @q:		request queue where request should be inserted
 * @rq:		request to map data to
 * @iov:	pointer to the iovec
 * @iov_count:	number of elements in the iovec
 *
 * Description:
 *    Data will be mapped directly for zero copy io, if possible. Otherwise
 *    a kernel bounce buffer is used.
 *
 *    A matching blk_rq_unmap_user() must be issued at the end of io, while
 *    still in process context.
 *
 *    Note: The mapped bio may need to be bounced through blk_queue_bounce()
 *    before being submitted to the device, as pages mapped may be out of
 *    reach. It's the callers responsibility to make sure this happens. The
 *    original bio must be passed back in to blk_rq_unmap_user() for proper
 *    unmapping.
 */
int blk_rq_map_user_iov(request_queue_t *q, struct request *rq,
			struct sg_iovec *iov, int iov_count, unsigned int len)
{
	struct bio *bio;

	if (!iov || iov_count <= 0)
		return -EINVAL;

	/* we don't allow misaligned data like bio_map_user() does.  If the
	 * user is using sg, they're expected to know the alignment constraints
	 * and respect them accordingly */
	bio = bio_map_user_iov(q, NULL, iov, iov_count, rq_data_dir(rq)== READ);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	if (bio->bi_size != len) {
		bio_endio(bio, bio->bi_size, 0);
		bio_unmap_user(bio);
		return -EINVAL;
	}

	bio_get(bio);
	blk_rq_bio_prep(q, rq, bio);
	rq->buffer = rq->data = NULL;
	return 0;
}

EXPORT_SYMBOL(blk_rq_map_user_iov);

/**
 * blk_rq_unmap_user - unmap a request with user data
 * @rq:		rq to be unmapped
 *
 * Description:
 *    Unmap a rq previously mapped by blk_rq_map_user().
 *    rq->bio must be set to the original head of the request.
 */
int blk_rq_unmap_user(struct request *rq)
{
	struct bio *bio, *mapped_bio;

	while ((bio = rq->bio)) {
		if (bio_flagged(bio, BIO_BOUNCED))
			mapped_bio = bio->bi_private;
		else
			mapped_bio = bio;

		__blk_rq_unmap_user(mapped_bio);
		rq->bio = bio->bi_next;
		bio_put(bio);
	}
	return 0;
}

EXPORT_SYMBOL(blk_rq_unmap_user);

/**
 * blk_rq_map_kern - map kernel data to a request, for REQ_BLOCK_PC usage
 * @q:		request queue where request should be inserted
 * @rq:		request to fill
 * @kbuf:	the kernel buffer
 * @len:	length of user data
 * @gfp_mask:	memory allocation flags
 */
int blk_rq_map_kern(request_queue_t *q, struct request *rq, void *kbuf,
		    unsigned int len, gfp_t gfp_mask)
{
	struct bio *bio;

	if (len > (q->max_hw_sectors << 9))
		return -EINVAL;
	if (!len || !kbuf)
		return -EINVAL;

	bio = bio_map_kern(q, kbuf, len, gfp_mask);
	if (IS_ERR(bio))
		return PTR_ERR(bio);

	if (rq_data_dir(rq) == WRITE)
		bio->bi_rw |= (1 << BIO_RW);

	blk_rq_bio_prep(q, rq, bio);
	rq->buffer = rq->data = NULL;
	return 0;
}

EXPORT_SYMBOL(blk_rq_map_kern);

/**
 * blk_execute_rq_nowait - insert a request into queue for execution
 * @q:		queue to insert the request in
 * @bd_disk:	matching gendisk
 * @rq:		request to insert
 * @at_head:    insert request at head or tail of queue
 * @done:	I/O completion handler
 *
 * Description:
 *    Insert a fully prepared request at the back of the io scheduler queue
 *    for execution.  Don't wait for completion.
 */
void blk_execute_rq_nowait(request_queue_t *q, struct gendisk *bd_disk,
			   struct request *rq, int at_head,
			   rq_end_io_fn *done)
{
	int where = at_head ? ELEVATOR_INSERT_FRONT : ELEVATOR_INSERT_BACK;

	rq->rq_disk = bd_disk;
	rq->cmd_flags |= REQ_NOMERGE;
	rq->end_io = done;
	WARN_ON(irqs_disabled());
	spin_lock_irq(q->queue_lock);
	__elv_add_request(q, rq, where, 1);
	__generic_unplug_device(q);
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL_GPL(blk_execute_rq_nowait);

/**
 * blk_execute_rq - insert a request into queue for execution
 * @q:		queue to insert the request in
 * @bd_disk:	matching gendisk
 * @rq:		request to insert
 * @at_head:    insert request at head or tail of queue
 *
 * Description:
 *    Insert a fully prepared request at the back of the io scheduler queue
 *    for execution and wait for completion.
 */
int blk_execute_rq(request_queue_t *q, struct gendisk *bd_disk,
		   struct request *rq, int at_head)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	char sense[SCSI_SENSE_BUFFERSIZE];
	int err = 0;

	/*
	 * we need an extra reference to the request, so we can look at
	 * it after io completion
	 */
	rq->ref_count++;

	if (!rq->sense) {
		memset(sense, 0, sizeof(sense));
		rq->sense = sense;
		rq->sense_len = 0;
	}

	rq->end_io_data = &wait;
	blk_execute_rq_nowait(q, bd_disk, rq, at_head, blk_end_sync_rq);
	wait_for_completion(&wait);

	if (rq->errors)
		err = -EIO;

	return err;
}

EXPORT_SYMBOL(blk_execute_rq);

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
	request_queue_t *q;

	if (bdev->bd_disk == NULL)
		return -ENXIO;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;
	if (!q->issue_flush_fn)
		return -EOPNOTSUPP;

	return q->issue_flush_fn(q, bdev->bd_disk, error_sector);
}

EXPORT_SYMBOL(blkdev_issue_flush);

static void drive_stat_acct(struct request *rq, int nr_sectors, int new_io)
{
	int rw = rq_data_dir(rq);

	if (!blk_fs_request(rq) || !rq->rq_disk)
		return;

	if (!new_io) {
		__disk_stat_inc(rq->rq_disk, merges[rw]);
	} else {
		disk_round_stats(rq->rq_disk);
		rq->rq_disk->in_flight++;
	}
}

/*
 * add-request adds a request to the linked list.
 * queue lock is held and interrupts disabled, as we muck with the
 * request queue list.
 */
static inline void add_request(request_queue_t * q, struct request * req)
{
	drive_stat_acct(req, req->nr_sectors, 1);

	/*
	 * elevator indicated where it wants this request to be
	 * inserted at elevator_merge time
	 */
	__elv_add_request(q, req, ELEVATOR_INSERT_SORT, 0);
}
 
/*
 * disk_round_stats()	- Round off the performance stats on a struct
 * disk_stats.
 *
 * The average IO queue length and utilisation statistics are maintained
 * by observing the current state of the queue length and the amount of
 * time it has been in this state for.
 *
 * Normally, that accounting is done on IO completion, but that can result
 * in more than a second's worth of IO being accounted for within any one
 * second, leading to >100% utilisation.  To deal with that, we call this
 * function to do a round-off before returning the results when reading
 * /proc/diskstats.  This accounts immediately for all queue usage up to
 * the current jiffies and restarts the counters again.
 */
void disk_round_stats(struct gendisk *disk)
{
	unsigned long now = jiffies;

	if (now == disk->stamp)
		return;

	if (disk->in_flight) {
		__disk_stat_add(disk, time_in_queue,
				disk->in_flight * (now - disk->stamp));
		__disk_stat_add(disk, io_ticks, (now - disk->stamp));
	}
	disk->stamp = now;
}

EXPORT_SYMBOL_GPL(disk_round_stats);

/*
 * queue lock must be held
 */
void __blk_put_request(request_queue_t *q, struct request *req)
{
	if (unlikely(!q))
		return;
	if (unlikely(--req->ref_count))
		return;

	elv_completed_request(q, req);

	/*
	 * Request may not have originated from ll_rw_blk. if not,
	 * it didn't come out of our reserved rq pools
	 */
	if (req->cmd_flags & REQ_ALLOCED) {
		int rw = rq_data_dir(req);
		int priv = req->cmd_flags & REQ_ELVPRIV;

		BUG_ON(!list_empty(&req->queuelist));
		BUG_ON(!hlist_unhashed(&req->hash));

		blk_free_request(q, req);
		freed_request(q, rw, priv);
	}
}

EXPORT_SYMBOL_GPL(__blk_put_request);

void blk_put_request(struct request *req)
{
	unsigned long flags;
	request_queue_t *q = req->q;

	/*
	 * Gee, IDE calls in w/ NULL q.  Fix IDE and remove the
	 * following if (q) test.
	 */
	if (q) {
		spin_lock_irqsave(q->queue_lock, flags);
		__blk_put_request(q, req);
		spin_unlock_irqrestore(q->queue_lock, flags);
	}
}

EXPORT_SYMBOL(blk_put_request);

/**
 * blk_end_sync_rq - executes a completion event on a request
 * @rq: request to complete
 * @error: end io status of the request
 */
void blk_end_sync_rq(struct request *rq, int error)
{
	struct completion *waiting = rq->end_io_data;

	rq->end_io_data = NULL;
	__blk_put_request(rq->q, rq);

	/*
	 * complete last, if this is a stack request the process (and thus
	 * the rq pointer) could be invalid right after this complete()
	 */
	complete(waiting);
}
EXPORT_SYMBOL(blk_end_sync_rq);

/*
 * Has to be called with the request spinlock acquired
 */
static int attempt_merge(request_queue_t *q, struct request *req,
			  struct request *next)
{
	if (!rq_mergeable(req) || !rq_mergeable(next))
		return 0;

	/*
	 * not contiguous
	 */
	if (req->sector + req->nr_sectors != next->sector)
		return 0;

	if (rq_data_dir(req) != rq_data_dir(next)
	    || req->rq_disk != next->rq_disk
	    || next->special)
		return 0;

	/*
	 * If we are allowed to merge, then append bio list
	 * from next to rq and release next. merge_requests_fn
	 * will have updated segment counts, update sector
	 * counts here.
	 */
	if (!q->merge_requests_fn(q, req, next))
		return 0;

	/*
	 * At this point we have either done a back merge
	 * or front merge. We need the smaller start_time of
	 * the merged requests to be the current request
	 * for accounting purposes.
	 */
	if (time_after(req->start_time, next->start_time))
		req->start_time = next->start_time;

	req->biotail->bi_next = next->bio;
	req->biotail = next->biotail;

	req->nr_sectors = req->hard_nr_sectors += next->hard_nr_sectors;

	elv_merge_requests(q, req, next);

	if (req->rq_disk) {
		disk_round_stats(req->rq_disk);
		req->rq_disk->in_flight--;
	}

	req->ioprio = ioprio_best(req->ioprio, next->ioprio);

	__blk_put_request(q, next);
	return 1;
}

static inline int attempt_back_merge(request_queue_t *q, struct request *rq)
{
	struct request *next = elv_latter_request(q, rq);

	if (next)
		return attempt_merge(q, rq, next);

	return 0;
}

static inline int attempt_front_merge(request_queue_t *q, struct request *rq)
{
	struct request *prev = elv_former_request(q, rq);

	if (prev)
		return attempt_merge(q, prev, rq);

	return 0;
}

static void init_request_from_bio(struct request *req, struct bio *bio)
{
	req->cmd_type = REQ_TYPE_FS;

	/*
	 * inherit FAILFAST from bio (for read-ahead, and explicit FAILFAST)
	 */
	if (bio_rw_ahead(bio) || bio_failfast(bio))
		req->cmd_flags |= REQ_FAILFAST;

	/*
	 * REQ_BARRIER implies no merging, but lets make it explicit
	 */
	if (unlikely(bio_barrier(bio)))
		req->cmd_flags |= (REQ_HARDBARRIER | REQ_NOMERGE);

	if (bio_sync(bio))
		req->cmd_flags |= REQ_RW_SYNC;
	if (bio_rw_meta(bio))
		req->cmd_flags |= REQ_RW_META;

	req->errors = 0;
	req->hard_sector = req->sector = bio->bi_sector;
	req->hard_nr_sectors = req->nr_sectors = bio_sectors(bio);
	req->current_nr_sectors = req->hard_cur_sectors = bio_cur_sectors(bio);
	req->nr_phys_segments = bio_phys_segments(req->q, bio);
	req->nr_hw_segments = bio_hw_segments(req->q, bio);
	req->buffer = bio_data(bio);	/* see ->buffer comment above */
	req->bio = req->biotail = bio;
	req->ioprio = bio_prio(bio);
	req->rq_disk = bio->bi_bdev->bd_disk;
	req->start_time = jiffies;
}

static int __make_request(request_queue_t *q, struct bio *bio)
{
	struct request *req;
	int el_ret, nr_sectors, barrier, err;
	const unsigned short prio = bio_prio(bio);
	const int sync = bio_sync(bio);
	int rw_flags;

	nr_sectors = bio_sectors(bio);

	/*
	 * low level driver can indicate that it wants pages above a
	 * certain limit bounced to low memory (ie for highmem, or even
	 * ISA dma in theory)
	 */
	blk_queue_bounce(q, &bio);

	barrier = bio_barrier(bio);
	if (unlikely(barrier) && (q->next_ordered == QUEUE_ORDERED_NONE)) {
		err = -EOPNOTSUPP;
		goto end_io;
	}

	spin_lock_irq(q->queue_lock);

	if (unlikely(barrier) || elv_queue_empty(q))
		goto get_rq;

	el_ret = elv_merge(q, &req, bio);
	switch (el_ret) {
		case ELEVATOR_BACK_MERGE:
			BUG_ON(!rq_mergeable(req));

			if (!q->back_merge_fn(q, req, bio))
				break;

			blk_add_trace_bio(q, bio, BLK_TA_BACKMERGE);

			req->biotail->bi_next = bio;
			req->biotail = bio;
			req->nr_sectors = req->hard_nr_sectors += nr_sectors;
			req->ioprio = ioprio_best(req->ioprio, prio);
			drive_stat_acct(req, nr_sectors, 0);
			if (!attempt_back_merge(q, req))
				elv_merged_request(q, req, el_ret);
			goto out;

		case ELEVATOR_FRONT_MERGE:
			BUG_ON(!rq_mergeable(req));

			if (!q->front_merge_fn(q, req, bio))
				break;

			blk_add_trace_bio(q, bio, BLK_TA_FRONTMERGE);

			bio->bi_next = req->bio;
			req->bio = bio;

			/*
			 * may not be valid. if the low level driver said
			 * it didn't need a bounce buffer then it better
			 * not touch req->buffer either...
			 */
			req->buffer = bio_data(bio);
			req->current_nr_sectors = bio_cur_sectors(bio);
			req->hard_cur_sectors = req->current_nr_sectors;
			req->sector = req->hard_sector = bio->bi_sector;
			req->nr_sectors = req->hard_nr_sectors += nr_sectors;
			req->ioprio = ioprio_best(req->ioprio, prio);
			drive_stat_acct(req, nr_sectors, 0);
			if (!attempt_front_merge(q, req))
				elv_merged_request(q, req, el_ret);
			goto out;

		/* ELV_NO_MERGE: elevator says don't/can't merge. */
		default:
			;
	}

get_rq:
	/*
	 * This sync check and mask will be re-done in init_request_from_bio(),
	 * but we need to set it earlier to expose the sync flag to the
	 * rq allocator and io schedulers.
	 */
	rw_flags = bio_data_dir(bio);
	if (sync)
		rw_flags |= REQ_RW_SYNC;

	/*
	 * Grab a free request. This is might sleep but can not fail.
	 * Returns with the queue unlocked.
	 */
	req = get_request_wait(q, rw_flags, bio);

	/*
	 * After dropping the lock and possibly sleeping here, our request
	 * may now be mergeable after it had proven unmergeable (above).
	 * We don't worry about that case for efficiency. It won't happen
	 * often, and the elevators are able to handle it.
	 */
	init_request_from_bio(req, bio);

	spin_lock_irq(q->queue_lock);
	if (elv_queue_empty(q))
		blk_plug_device(q);
	add_request(q, req);
out:
	if (sync)
		__generic_unplug_device(q);

	spin_unlock_irq(q->queue_lock);
	return 0;

end_io:
	bio_endio(bio, nr_sectors << 9, err);
	return 0;
}

/*
 * If bio->bi_dev is a partition, remap the location
 */
static inline void blk_partition_remap(struct bio *bio)
{
	struct block_device *bdev = bio->bi_bdev;

	if (bdev != bdev->bd_contains) {
		struct hd_struct *p = bdev->bd_part;
		const int rw = bio_data_dir(bio);

		p->sectors[rw] += bio_sectors(bio);
		p->ios[rw]++;

		bio->bi_sector += p->start_sect;
		bio->bi_bdev = bdev->bd_contains;
	}
}

static void handle_bad_sector(struct bio *bio)
{
	char b[BDEVNAME_SIZE];

	printk(KERN_INFO "attempt to access beyond end of device\n");
	printk(KERN_INFO "%s: rw=%ld, want=%Lu, limit=%Lu\n",
			bdevname(bio->bi_bdev, b),
			bio->bi_rw,
			(unsigned long long)bio->bi_sector + bio_sectors(bio),
			(long long)(bio->bi_bdev->bd_inode->i_size >> 9));

	set_bit(BIO_EOF, &bio->bi_flags);
}

#ifdef CONFIG_FAIL_MAKE_REQUEST

static DECLARE_FAULT_ATTR(fail_make_request);

static int __init setup_fail_make_request(char *str)
{
	return setup_fault_attr(&fail_make_request, str);
}
__setup("fail_make_request=", setup_fail_make_request);

static int should_fail_request(struct bio *bio)
{
	if ((bio->bi_bdev->bd_disk->flags & GENHD_FL_FAIL) ||
	    (bio->bi_bdev->bd_part && bio->bi_bdev->bd_part->make_it_fail))
		return should_fail(&fail_make_request, bio->bi_size);

	return 0;
}

static int __init fail_make_request_debugfs(void)
{
	return init_fault_attr_dentries(&fail_make_request,
					"fail_make_request");
}

late_initcall(fail_make_request_debugfs);

#else /* CONFIG_FAIL_MAKE_REQUEST */

static inline int should_fail_request(struct bio *bio)
{
	return 0;
}

#endif /* CONFIG_FAIL_MAKE_REQUEST */

/**
 * generic_make_request: hand a buffer to its device driver for I/O
 * @bio:  The bio describing the location in memory and on the device.
 *
 * generic_make_request() is used to make I/O requests of block
 * devices. It is passed a &struct bio, which describes the I/O that needs
 * to be done.
 *
 * generic_make_request() does not return any status.  The
 * success/failure status of the request, along with notification of
 * completion, is delivered asynchronously through the bio->bi_end_io
 * function described (one day) else where.
 *
 * The caller of generic_make_request must make sure that bi_io_vec
 * are set to describe the memory buffer, and that bi_dev and bi_sector are
 * set to describe the device address, and the
 * bi_end_io and optionally bi_private are set to describe how
 * completion notification should be signaled.
 *
 * generic_make_request and the drivers it calls may use bi_next if this
 * bio happens to be merged with someone else, and may change bi_dev and
 * bi_sector for remaps as it sees fit.  So the values of these fields
 * should NOT be depended on after the call to generic_make_request.
 */
void generic_make_request(struct bio *bio)
{
	request_queue_t *q;
	sector_t maxsector;
	sector_t old_sector;
	int ret, nr_sectors = bio_sectors(bio);
	dev_t old_dev;

	might_sleep();
	/* Test device or partition size, when known. */
	maxsector = bio->bi_bdev->bd_inode->i_size >> 9;
	if (maxsector) {
		sector_t sector = bio->bi_sector;

		if (maxsector < nr_sectors || maxsector - nr_sectors < sector) {
			/*
			 * This may well happen - the kernel calls bread()
			 * without checking the size of the device, e.g., when
			 * mounting a device.
			 */
			handle_bad_sector(bio);
			goto end_io;
		}
	}

	/*
	 * Resolve the mapping until finished. (drivers are
	 * still free to implement/resolve their own stacking
	 * by explicitly returning 0)
	 *
	 * NOTE: we don't repeat the blk_size check for each new device.
	 * Stacking drivers are expected to know what they are doing.
	 */
	old_sector = -1;
	old_dev = 0;
	do {
		char b[BDEVNAME_SIZE];

		q = bdev_get_queue(bio->bi_bdev);
		if (!q) {
			printk(KERN_ERR
			       "generic_make_request: Trying to access "
				"nonexistent block-device %s (%Lu)\n",
				bdevname(bio->bi_bdev, b),
				(long long) bio->bi_sector);
end_io:
			bio_endio(bio, bio->bi_size, -EIO);
			break;
		}

		if (unlikely(bio_sectors(bio) > q->max_hw_sectors)) {
			printk("bio too big device %s (%u > %u)\n", 
				bdevname(bio->bi_bdev, b),
				bio_sectors(bio),
				q->max_hw_sectors);
			goto end_io;
		}

		if (unlikely(test_bit(QUEUE_FLAG_DEAD, &q->queue_flags)))
			goto end_io;

		if (should_fail_request(bio))
			goto end_io;

		/*
		 * If this device has partitions, remap block n
		 * of partition p to block n+start(p) of the disk.
		 */
		blk_partition_remap(bio);

		if (old_sector != -1)
			blk_add_trace_remap(q, bio, old_dev, bio->bi_sector, 
					    old_sector);

		blk_add_trace_bio(q, bio, BLK_TA_QUEUE);

		old_sector = bio->bi_sector;
		old_dev = bio->bi_bdev->bd_dev;

		maxsector = bio->bi_bdev->bd_inode->i_size >> 9;
		if (maxsector) {
			sector_t sector = bio->bi_sector;

			if (maxsector < nr_sectors ||
					maxsector - nr_sectors < sector) {
				/*
				 * This may well happen - partitions are not
				 * checked to make sure they are within the size
				 * of the whole device.
				 */
				handle_bad_sector(bio);
				goto end_io;
			}
		}

		ret = q->make_request_fn(q, bio);
	} while (ret);
}

EXPORT_SYMBOL(generic_make_request);

/**
 * submit_bio: submit a bio to the block device layer for I/O
 * @rw: whether to %READ or %WRITE, or maybe to %READA (read ahead)
 * @bio: The &struct bio which describes the I/O
 *
 * submit_bio() is very similar in purpose to generic_make_request(), and
 * uses that function to do most of the work. Both are fairly rough
 * interfaces, @bio must be presetup and ready for I/O.
 *
 */
void submit_bio(int rw, struct bio *bio)
{
	int count = bio_sectors(bio);

	BIO_BUG_ON(!bio->bi_size);
	BIO_BUG_ON(!bio->bi_io_vec);
	bio->bi_rw |= rw;
	if (rw & WRITE) {
		count_vm_events(PGPGOUT, count);
	} else {
		task_io_account_read(bio->bi_size);
		count_vm_events(PGPGIN, count);
	}

	if (unlikely(block_dump)) {
		char b[BDEVNAME_SIZE];
		printk(KERN_DEBUG "%s(%d): %s block %Lu on %s\n",
			current->comm, current->pid,
			(rw & WRITE) ? "WRITE" : "READ",
			(unsigned long long)bio->bi_sector,
			bdevname(bio->bi_bdev,b));
	}

	generic_make_request(bio);
}

EXPORT_SYMBOL(submit_bio);

static void blk_recalc_rq_segments(struct request *rq)
{
	struct bio *bio, *prevbio = NULL;
	int nr_phys_segs, nr_hw_segs;
	unsigned int phys_size, hw_size;
	request_queue_t *q = rq->q;

	if (!rq->bio)
		return;

	phys_size = hw_size = nr_phys_segs = nr_hw_segs = 0;
	rq_for_each_bio(bio, rq) {
		/* Force bio hw/phys segs to be recalculated. */
		bio->bi_flags &= ~(1 << BIO_SEG_VALID);

		nr_phys_segs += bio_phys_segments(q, bio);
		nr_hw_segs += bio_hw_segments(q, bio);
		if (prevbio) {
			int pseg = phys_size + prevbio->bi_size + bio->bi_size;
			int hseg = hw_size + prevbio->bi_size + bio->bi_size;

			if (blk_phys_contig_segment(q, prevbio, bio) &&
			    pseg <= q->max_segment_size) {
				nr_phys_segs--;
				phys_size += prevbio->bi_size + bio->bi_size;
			} else
				phys_size = 0;

			if (blk_hw_contig_segment(q, prevbio, bio) &&
			    hseg <= q->max_segment_size) {
				nr_hw_segs--;
				hw_size += prevbio->bi_size + bio->bi_size;
			} else
				hw_size = 0;
		}
		prevbio = bio;
	}

	rq->nr_phys_segments = nr_phys_segs;
	rq->nr_hw_segments = nr_hw_segs;
}

static void blk_recalc_rq_sectors(struct request *rq, int nsect)
{
	if (blk_fs_request(rq)) {
		rq->hard_sector += nsect;
		rq->hard_nr_sectors -= nsect;

		/*
		 * Move the I/O submission pointers ahead if required.
		 */
		if ((rq->nr_sectors >= rq->hard_nr_sectors) &&
		    (rq->sector <= rq->hard_sector)) {
			rq->sector = rq->hard_sector;
			rq->nr_sectors = rq->hard_nr_sectors;
			rq->hard_cur_sectors = bio_cur_sectors(rq->bio);
			rq->current_nr_sectors = rq->hard_cur_sectors;
			rq->buffer = bio_data(rq->bio);
		}

		/*
		 * if total number of sectors is less than the first segment
		 * size, something has gone terribly wrong
		 */
		if (rq->nr_sectors < rq->current_nr_sectors) {
			printk("blk: request botched\n");
			rq->nr_sectors = rq->current_nr_sectors;
		}
	}
}

static int __end_that_request_first(struct request *req, int uptodate,
				    int nr_bytes)
{
	int total_bytes, bio_nbytes, error, next_idx = 0;
	struct bio *bio;

	blk_add_trace_rq(req->q, req, BLK_TA_COMPLETE);

	/*
	 * extend uptodate bool to allow < 0 value to be direct io error
	 */
	error = 0;
	if (end_io_error(uptodate))
		error = !uptodate ? -EIO : uptodate;

	/*
	 * for a REQ_BLOCK_PC request, we want to carry any eventual
	 * sense key with us all the way through
	 */
	if (!blk_pc_request(req))
		req->errors = 0;

	if (!uptodate) {
		if (blk_fs_request(req) && !(req->cmd_flags & REQ_QUIET))
			printk("end_request: I/O error, dev %s, sector %llu\n",
				req->rq_disk ? req->rq_disk->disk_name : "?",
				(unsigned long long)req->sector);
	}

	if (blk_fs_request(req) && req->rq_disk) {
		const int rw = rq_data_dir(req);

		disk_stat_add(req->rq_disk, sectors[rw], nr_bytes >> 9);
	}

	total_bytes = bio_nbytes = 0;
	while ((bio = req->bio) != NULL) {
		int nbytes;

		if (nr_bytes >= bio->bi_size) {
			req->bio = bio->bi_next;
			nbytes = bio->bi_size;
			if (!ordered_bio_endio(req, bio, nbytes, error))
				bio_endio(bio, nbytes, error);
			next_idx = 0;
			bio_nbytes = 0;
		} else {
			int idx = bio->bi_idx + next_idx;

			if (unlikely(bio->bi_idx >= bio->bi_vcnt)) {
				blk_dump_rq_flags(req, "__end_that");
				printk("%s: bio idx %d >= vcnt %d\n",
						__FUNCTION__,
						bio->bi_idx, bio->bi_vcnt);
				break;
			}

			nbytes = bio_iovec_idx(bio, idx)->bv_len;
			BIO_BUG_ON(nbytes > bio->bi_size);

			/*
			 * not a complete bvec done
			 */
			if (unlikely(nbytes > nr_bytes)) {
				bio_nbytes += nr_bytes;
				total_bytes += nr_bytes;
				break;
			}

			/*
			 * advance to the next vector
			 */
			next_idx++;
			bio_nbytes += nbytes;
		}

		total_bytes += nbytes;
		nr_bytes -= nbytes;

		if ((bio = req->bio)) {
			/*
			 * end more in this run, or just return 'not-done'
			 */
			if (unlikely(nr_bytes <= 0))
				break;
		}
	}

	/*
	 * completely done
	 */
	if (!req->bio)
		return 0;

	/*
	 * if the request wasn't completed, update state
	 */
	if (bio_nbytes) {
		if (!ordered_bio_endio(req, bio, bio_nbytes, error))
			bio_endio(bio, bio_nbytes, error);
		bio->bi_idx += next_idx;
		bio_iovec(bio)->bv_offset += nr_bytes;
		bio_iovec(bio)->bv_len -= nr_bytes;
	}

	blk_recalc_rq_sectors(req, total_bytes >> 9);
	blk_recalc_rq_segments(req);
	return 1;
}

/**
 * end_that_request_first - end I/O on a request
 * @req:      the request being processed
 * @uptodate: 1 for success, 0 for I/O error, < 0 for specific error
 * @nr_sectors: number of sectors to end I/O on
 *
 * Description:
 *     Ends I/O on a number of sectors attached to @req, and sets it up
 *     for the next range of segments (if any) in the cluster.
 *
 * Return:
 *     0 - we are done with this request, call end_that_request_last()
 *     1 - still buffers pending for this request
 **/
int end_that_request_first(struct request *req, int uptodate, int nr_sectors)
{
	return __end_that_request_first(req, uptodate, nr_sectors << 9);
}

EXPORT_SYMBOL(end_that_request_first);

/**
 * end_that_request_chunk - end I/O on a request
 * @req:      the request being processed
 * @uptodate: 1 for success, 0 for I/O error, < 0 for specific error
 * @nr_bytes: number of bytes to complete
 *
 * Description:
 *     Ends I/O on a number of bytes attached to @req, and sets it up
 *     for the next range of segments (if any). Like end_that_request_first(),
 *     but deals with bytes instead of sectors.
 *
 * Return:
 *     0 - we are done with this request, call end_that_request_last()
 *     1 - still buffers pending for this request
 **/
int end_that_request_chunk(struct request *req, int uptodate, int nr_bytes)
{
	return __end_that_request_first(req, uptodate, nr_bytes);
}

EXPORT_SYMBOL(end_that_request_chunk);

/*
 * splice the completion data to a local structure and hand off to
 * process_completion_queue() to complete the requests
 */
static void blk_done_softirq(struct softirq_action *h)
{
	struct list_head *cpu_list, local_list;

	local_irq_disable();
	cpu_list = &__get_cpu_var(blk_cpu_done);
	list_replace_init(cpu_list, &local_list);
	local_irq_enable();

	while (!list_empty(&local_list)) {
		struct request *rq = list_entry(local_list.next, struct request, donelist);

		list_del_init(&rq->donelist);
		rq->q->softirq_done_fn(rq);
	}
}

static int blk_cpu_notify(struct notifier_block *self, unsigned long action,
			  void *hcpu)
{
	/*
	 * If a CPU goes away, splice its entries to the current CPU
	 * and trigger a run of the softirq
	 */
	if (action == CPU_DEAD) {
		int cpu = (unsigned long) hcpu;

		local_irq_disable();
		list_splice_init(&per_cpu(blk_cpu_done, cpu),
				 &__get_cpu_var(blk_cpu_done));
		raise_softirq_irqoff(BLOCK_SOFTIRQ);
		local_irq_enable();
	}

	return NOTIFY_OK;
}


static struct notifier_block __devinitdata blk_cpu_notifier = {
	.notifier_call	= blk_cpu_notify,
};

/**
 * blk_complete_request - end I/O on a request
 * @req:      the request being processed
 *
 * Description:
 *     Ends all I/O on a request. It does not handle partial completions,
 *     unless the driver actually implements this in its completion callback
 *     through requeueing. Theh actual completion happens out-of-order,
 *     through a softirq handler. The user must have registered a completion
 *     callback through blk_queue_softirq_done().
 **/

void blk_complete_request(struct request *req)
{
	struct list_head *cpu_list;
	unsigned long flags;

	BUG_ON(!req->q->softirq_done_fn);
		
	local_irq_save(flags);

	cpu_list = &__get_cpu_var(blk_cpu_done);
	list_add_tail(&req->donelist, cpu_list);
	raise_softirq_irqoff(BLOCK_SOFTIRQ);

	local_irq_restore(flags);
}

EXPORT_SYMBOL(blk_complete_request);
	
/*
 * queue lock must be held
 */
void end_that_request_last(struct request *req, int uptodate)
{
	struct gendisk *disk = req->rq_disk;
	int error;

	/*
	 * extend uptodate bool to allow < 0 value to be direct io error
	 */
	error = 0;
	if (end_io_error(uptodate))
		error = !uptodate ? -EIO : uptodate;

	if (unlikely(laptop_mode) && blk_fs_request(req))
		laptop_io_completion();

	/*
	 * Account IO completion.  bar_rq isn't accounted as a normal
	 * IO on queueing nor completion.  Accounting the containing
	 * request is enough.
	 */
	if (disk && blk_fs_request(req) && req != &req->q->bar_rq) {
		unsigned long duration = jiffies - req->start_time;
		const int rw = rq_data_dir(req);

		__disk_stat_inc(disk, ios[rw]);
		__disk_stat_add(disk, ticks[rw], duration);
		disk_round_stats(disk);
		disk->in_flight--;
	}
	if (req->end_io)
		req->end_io(req, error);
	else
		__blk_put_request(req->q, req);
}

EXPORT_SYMBOL(end_that_request_last);

void end_request(struct request *req, int uptodate)
{
	if (!end_that_request_first(req, uptodate, req->hard_cur_sectors)) {
		add_disk_randomness(req->rq_disk);
		blkdev_dequeue_request(req);
		end_that_request_last(req, uptodate);
	}
}

EXPORT_SYMBOL(end_request);

void blk_rq_bio_prep(request_queue_t *q, struct request *rq, struct bio *bio)
{
	/* first two bits are identical in rq->cmd_flags and bio->bi_rw */
	rq->cmd_flags |= (bio->bi_rw & 3);

	rq->nr_phys_segments = bio_phys_segments(q, bio);
	rq->nr_hw_segments = bio_hw_segments(q, bio);
	rq->current_nr_sectors = bio_cur_sectors(bio);
	rq->hard_cur_sectors = rq->current_nr_sectors;
	rq->hard_nr_sectors = rq->nr_sectors = bio_sectors(bio);
	rq->buffer = bio_data(bio);
	rq->data_len = bio->bi_size;

	rq->bio = rq->biotail = bio;
}

EXPORT_SYMBOL(blk_rq_bio_prep);

int kblockd_schedule_work(struct work_struct *work)
{
	return queue_work(kblockd_workqueue, work);
}

EXPORT_SYMBOL(kblockd_schedule_work);

void kblockd_flush(void)
{
	flush_workqueue(kblockd_workqueue);
}
EXPORT_SYMBOL(kblockd_flush);

int __init blk_dev_init(void)
{
	int i;

	kblockd_workqueue = create_workqueue("kblockd");
	if (!kblockd_workqueue)
		panic("Failed to create kblockd\n");

	request_cachep = kmem_cache_create("blkdev_requests",
			sizeof(struct request), 0, SLAB_PANIC, NULL, NULL);

	requestq_cachep = kmem_cache_create("blkdev_queue",
			sizeof(request_queue_t), 0, SLAB_PANIC, NULL, NULL);

	iocontext_cachep = kmem_cache_create("blkdev_ioc",
			sizeof(struct io_context), 0, SLAB_PANIC, NULL, NULL);

	for_each_possible_cpu(i)
		INIT_LIST_HEAD(&per_cpu(blk_cpu_done, i));

	open_softirq(BLOCK_SOFTIRQ, blk_done_softirq, NULL);
	register_hotcpu_notifier(&blk_cpu_notifier);

	blk_max_low_pfn = max_low_pfn;
	blk_max_pfn = max_pfn;

	return 0;
}

/*
 * IO Context helper functions
 */
void put_io_context(struct io_context *ioc)
{
	if (ioc == NULL)
		return;

	BUG_ON(atomic_read(&ioc->refcount) == 0);

	if (atomic_dec_and_test(&ioc->refcount)) {
		struct cfq_io_context *cic;

		rcu_read_lock();
		if (ioc->aic && ioc->aic->dtor)
			ioc->aic->dtor(ioc->aic);
		if (ioc->cic_root.rb_node != NULL) {
			struct rb_node *n = rb_first(&ioc->cic_root);

			cic = rb_entry(n, struct cfq_io_context, rb_node);
			cic->dtor(ioc);
		}
		rcu_read_unlock();

		kmem_cache_free(iocontext_cachep, ioc);
	}
}
EXPORT_SYMBOL(put_io_context);

/* Called by the exitting task */
void exit_io_context(void)
{
	struct io_context *ioc;
	struct cfq_io_context *cic;

	task_lock(current);
	ioc = current->io_context;
	current->io_context = NULL;
	task_unlock(current);

	ioc->task = NULL;
	if (ioc->aic && ioc->aic->exit)
		ioc->aic->exit(ioc->aic);
	if (ioc->cic_root.rb_node != NULL) {
		cic = rb_entry(rb_first(&ioc->cic_root), struct cfq_io_context, rb_node);
		cic->exit(ioc);
	}

	put_io_context(ioc);
}

/*
 * If the current task has no IO context then create one and initialise it.
 * Otherwise, return its existing IO context.
 *
 * This returned IO context doesn't have a specifically elevated refcount,
 * but since the current task itself holds a reference, the context can be
 * used in general code, so long as it stays within `current` context.
 */
static struct io_context *current_io_context(gfp_t gfp_flags, int node)
{
	struct task_struct *tsk = current;
	struct io_context *ret;

	ret = tsk->io_context;
	if (likely(ret))
		return ret;

	ret = kmem_cache_alloc_node(iocontext_cachep, gfp_flags, node);
	if (ret) {
		atomic_set(&ret->refcount, 1);
		ret->task = current;
		ret->ioprio_changed = 0;
		ret->last_waited = jiffies; /* doesn't matter... */
		ret->nr_batch_requests = 0; /* because this is 0 */
		ret->aic = NULL;
		ret->cic_root.rb_node = NULL;
		/* make sure set_task_ioprio() sees the settings above */
		smp_wmb();
		tsk->io_context = ret;
	}

	return ret;
}
EXPORT_SYMBOL(current_io_context);

/*
 * If the current task has no IO context then create one and initialise it.
 * If it does have a context, take a ref on it.
 *
 * This is always called in the context of the task which submitted the I/O.
 */
struct io_context *get_io_context(gfp_t gfp_flags, int node)
{
	struct io_context *ret;
	ret = current_io_context(gfp_flags, node);
	if (likely(ret))
		atomic_inc(&ret->refcount);
	return ret;
}
EXPORT_SYMBOL(get_io_context);

void copy_io_context(struct io_context **pdst, struct io_context **psrc)
{
	struct io_context *src = *psrc;
	struct io_context *dst = *pdst;

	if (src) {
		BUG_ON(atomic_read(&src->refcount) == 0);
		atomic_inc(&src->refcount);
		put_io_context(dst);
		*pdst = src;
	}
}
EXPORT_SYMBOL(copy_io_context);

void swap_io_context(struct io_context **ioc1, struct io_context **ioc2)
{
	struct io_context *temp;
	temp = *ioc1;
	*ioc1 = *ioc2;
	*ioc2 = temp;
}
EXPORT_SYMBOL(swap_io_context);

/*
 * sysfs parts below
 */
struct queue_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct request_queue *, char *);
	ssize_t (*store)(struct request_queue *, const char *, size_t);
};

static ssize_t
queue_var_show(unsigned int var, char *page)
{
	return sprintf(page, "%d\n", var);
}

static ssize_t
queue_var_store(unsigned long *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtoul(p, &p, 10);
	return count;
}

static ssize_t queue_requests_show(struct request_queue *q, char *page)
{
	return queue_var_show(q->nr_requests, (page));
}

static ssize_t
queue_requests_store(struct request_queue *q, const char *page, size_t count)
{
	struct request_list *rl = &q->rq;
	unsigned long nr;
	int ret = queue_var_store(&nr, page, count);
	if (nr < BLKDEV_MIN_RQ)
		nr = BLKDEV_MIN_RQ;

	spin_lock_irq(q->queue_lock);
	q->nr_requests = nr;
	blk_queue_congestion_threshold(q);

	if (rl->count[READ] >= queue_congestion_on_threshold(q))
		blk_set_queue_congested(q, READ);
	else if (rl->count[READ] < queue_congestion_off_threshold(q))
		blk_clear_queue_congested(q, READ);

	if (rl->count[WRITE] >= queue_congestion_on_threshold(q))
		blk_set_queue_congested(q, WRITE);
	else if (rl->count[WRITE] < queue_congestion_off_threshold(q))
		blk_clear_queue_congested(q, WRITE);

	if (rl->count[READ] >= q->nr_requests) {
		blk_set_queue_full(q, READ);
	} else if (rl->count[READ]+1 <= q->nr_requests) {
		blk_clear_queue_full(q, READ);
		wake_up(&rl->wait[READ]);
	}

	if (rl->count[WRITE] >= q->nr_requests) {
		blk_set_queue_full(q, WRITE);
	} else if (rl->count[WRITE]+1 <= q->nr_requests) {
		blk_clear_queue_full(q, WRITE);
		wake_up(&rl->wait[WRITE]);
	}
	spin_unlock_irq(q->queue_lock);
	return ret;
}

static ssize_t queue_ra_show(struct request_queue *q, char *page)
{
	int ra_kb = q->backing_dev_info.ra_pages << (PAGE_CACHE_SHIFT - 10);

	return queue_var_show(ra_kb, (page));
}

static ssize_t
queue_ra_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long ra_kb;
	ssize_t ret = queue_var_store(&ra_kb, page, count);

	spin_lock_irq(q->queue_lock);
	q->backing_dev_info.ra_pages = ra_kb >> (PAGE_CACHE_SHIFT - 10);
	spin_unlock_irq(q->queue_lock);

	return ret;
}

static ssize_t queue_max_sectors_show(struct request_queue *q, char *page)
{
	int max_sectors_kb = q->max_sectors >> 1;

	return queue_var_show(max_sectors_kb, (page));
}

static ssize_t
queue_max_sectors_store(struct request_queue *q, const char *page, size_t count)
{
	unsigned long max_sectors_kb,
			max_hw_sectors_kb = q->max_hw_sectors >> 1,
			page_kb = 1 << (PAGE_CACHE_SHIFT - 10);
	ssize_t ret = queue_var_store(&max_sectors_kb, page, count);
	int ra_kb;

	if (max_sectors_kb > max_hw_sectors_kb || max_sectors_kb < page_kb)
		return -EINVAL;
	/*
	 * Take the queue lock to update the readahead and max_sectors
	 * values synchronously:
	 */
	spin_lock_irq(q->queue_lock);
	/*
	 * Trim readahead window as well, if necessary:
	 */
	ra_kb = q->backing_dev_info.ra_pages << (PAGE_CACHE_SHIFT - 10);
	if (ra_kb > max_sectors_kb)
		q->backing_dev_info.ra_pages =
				max_sectors_kb >> (PAGE_CACHE_SHIFT - 10);

	q->max_sectors = max_sectors_kb << 1;
	spin_unlock_irq(q->queue_lock);

	return ret;
}

static ssize_t queue_max_hw_sectors_show(struct request_queue *q, char *page)
{
	int max_hw_sectors_kb = q->max_hw_sectors >> 1;

	return queue_var_show(max_hw_sectors_kb, (page));
}


static struct queue_sysfs_entry queue_requests_entry = {
	.attr = {.name = "nr_requests", .mode = S_IRUGO | S_IWUSR },
	.show = queue_requests_show,
	.store = queue_requests_store,
};

static struct queue_sysfs_entry queue_ra_entry = {
	.attr = {.name = "read_ahead_kb", .mode = S_IRUGO | S_IWUSR },
	.show = queue_ra_show,
	.store = queue_ra_store,
};

static struct queue_sysfs_entry queue_max_sectors_entry = {
	.attr = {.name = "max_sectors_kb", .mode = S_IRUGO | S_IWUSR },
	.show = queue_max_sectors_show,
	.store = queue_max_sectors_store,
};

static struct queue_sysfs_entry queue_max_hw_sectors_entry = {
	.attr = {.name = "max_hw_sectors_kb", .mode = S_IRUGO },
	.show = queue_max_hw_sectors_show,
};

static struct queue_sysfs_entry queue_iosched_entry = {
	.attr = {.name = "scheduler", .mode = S_IRUGO | S_IWUSR },
	.show = elv_iosched_show,
	.store = elv_iosched_store,
};

static struct attribute *default_attrs[] = {
	&queue_requests_entry.attr,
	&queue_ra_entry.attr,
	&queue_max_hw_sectors_entry.attr,
	&queue_max_sectors_entry.attr,
	&queue_iosched_entry.attr,
	NULL,
};

#define to_queue(atr) container_of((atr), struct queue_sysfs_entry, attr)

static ssize_t
queue_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	request_queue_t *q = container_of(kobj, struct request_queue, kobj);
	ssize_t res;

	if (!entry->show)
		return -EIO;
	mutex_lock(&q->sysfs_lock);
	if (test_bit(QUEUE_FLAG_DEAD, &q->queue_flags)) {
		mutex_unlock(&q->sysfs_lock);
		return -ENOENT;
	}
	res = entry->show(q, page);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static ssize_t
queue_attr_store(struct kobject *kobj, struct attribute *attr,
		    const char *page, size_t length)
{
	struct queue_sysfs_entry *entry = to_queue(attr);
	request_queue_t *q = container_of(kobj, struct request_queue, kobj);

	ssize_t res;

	if (!entry->store)
		return -EIO;
	mutex_lock(&q->sysfs_lock);
	if (test_bit(QUEUE_FLAG_DEAD, &q->queue_flags)) {
		mutex_unlock(&q->sysfs_lock);
		return -ENOENT;
	}
	res = entry->store(q, page, length);
	mutex_unlock(&q->sysfs_lock);
	return res;
}

static struct sysfs_ops queue_sysfs_ops = {
	.show	= queue_attr_show,
	.store	= queue_attr_store,
};

static struct kobj_type queue_ktype = {
	.sysfs_ops	= &queue_sysfs_ops,
	.default_attrs	= default_attrs,
	.release	= blk_release_queue,
};

int blk_register_queue(struct gendisk *disk)
{
	int ret;

	request_queue_t *q = disk->queue;

	if (!q || !q->request_fn)
		return -ENXIO;

	q->kobj.parent = kobject_get(&disk->kobj);

	ret = kobject_add(&q->kobj);
	if (ret < 0)
		return ret;

	kobject_uevent(&q->kobj, KOBJ_ADD);

	ret = elv_register_queue(q);
	if (ret) {
		kobject_uevent(&q->kobj, KOBJ_REMOVE);
		kobject_del(&q->kobj);
		return ret;
	}

	return 0;
}

void blk_unregister_queue(struct gendisk *disk)
{
	request_queue_t *q = disk->queue;

	if (q && q->request_fn) {
		elv_unregister_queue(q);

		kobject_uevent(&q->kobj, KOBJ_REMOVE);
		kobject_del(&q->kobj);
		kobject_put(&disk->kobj);
	}
}
