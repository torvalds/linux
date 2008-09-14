/*
 * Functions related to setting various queue properties from drivers
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/bootmem.h>	/* for max_pfn/max_low_pfn */

#include "blk.h"

unsigned long blk_max_low_pfn;
EXPORT_SYMBOL(blk_max_low_pfn);

unsigned long blk_max_pfn;

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
void blk_queue_prep_rq(struct request_queue *q, prep_rq_fn *pfn)
{
	q->prep_rq_fn = pfn;
}
EXPORT_SYMBOL(blk_queue_prep_rq);

/**
 * blk_queue_set_discard - set a discard_sectors function for queue
 * @q:		queue
 * @dfn:	prepare_discard function
 *
 * It's possible for a queue to register a discard callback which is used
 * to transform a discard request into the appropriate type for the
 * hardware. If none is registered, then discard requests are failed
 * with %EOPNOTSUPP.
 *
 */
void blk_queue_set_discard(struct request_queue *q, prepare_discard_fn *dfn)
{
	q->prepare_discard_fn = dfn;
}
EXPORT_SYMBOL(blk_queue_set_discard);

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
void blk_queue_merge_bvec(struct request_queue *q, merge_bvec_fn *mbfn)
{
	q->merge_bvec_fn = mbfn;
}
EXPORT_SYMBOL(blk_queue_merge_bvec);

void blk_queue_softirq_done(struct request_queue *q, softirq_done_fn *fn)
{
	q->softirq_done_fn = fn;
}
EXPORT_SYMBOL(blk_queue_softirq_done);

void blk_queue_rq_timeout(struct request_queue *q, unsigned int timeout)
{
	q->rq_timeout = timeout;
}
EXPORT_SYMBOL_GPL(blk_queue_rq_timeout);

void blk_queue_rq_timed_out(struct request_queue *q, rq_timed_out_fn *fn)
{
	q->rq_timed_out_fn = fn;
}
EXPORT_SYMBOL_GPL(blk_queue_rq_timed_out);

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
void blk_queue_make_request(struct request_queue *q, make_request_fn *mfn)
{
	/*
	 * set defaults
	 */
	q->nr_requests = BLKDEV_MAX_RQ;
	blk_queue_max_phys_segments(q, MAX_PHYS_SEGMENTS);
	blk_queue_max_hw_segments(q, MAX_HW_SEGMENTS);
	q->make_request_fn = mfn;
	q->backing_dev_info.ra_pages =
			(VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE;
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

/**
 * blk_queue_bounce_limit - set bounce buffer limit for queue
 * @q:  the request queue for the device
 * @dma_addr:   bus address limit
 *
 * Description:
 *    Different hardware can have different requirements as to what pages
 *    it can do I/O directly to. A low level driver can call
 *    blk_queue_bounce_limit to have lower memory pages allocated as bounce
 *    buffers for doing I/O to pages residing above @dma_addr.
 **/
void blk_queue_bounce_limit(struct request_queue *q, u64 dma_addr)
{
	unsigned long b_pfn = dma_addr >> PAGE_SHIFT;
	int dma = 0;

	q->bounce_gfp = GFP_NOIO;
#if BITS_PER_LONG == 64
	/* Assume anything <= 4GB can be handled by IOMMU.
	   Actually some IOMMUs can handle everything, but I don't
	   know of a way to test this here. */
	if (b_pfn < (min_t(u64, 0x100000000UL, BLK_BOUNCE_HIGH) >> PAGE_SHIFT))
		dma = 1;
	q->bounce_pfn = max_low_pfn;
#else
	if (b_pfn < blk_max_low_pfn)
		dma = 1;
	q->bounce_pfn = b_pfn;
#endif
	if (dma) {
		init_emergency_isa_pool();
		q->bounce_gfp = GFP_NOIO | GFP_DMA;
		q->bounce_pfn = b_pfn;
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
void blk_queue_max_sectors(struct request_queue *q, unsigned int max_sectors)
{
	if ((max_sectors << 9) < PAGE_CACHE_SIZE) {
		max_sectors = 1 << (PAGE_CACHE_SHIFT - 9);
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_sectors);
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
void blk_queue_max_phys_segments(struct request_queue *q,
				 unsigned short max_segments)
{
	if (!max_segments) {
		max_segments = 1;
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_segments);
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
 *    address/length pairs the host adapter can actually give at once
 *    to the device.
 **/
void blk_queue_max_hw_segments(struct request_queue *q,
			       unsigned short max_segments)
{
	if (!max_segments) {
		max_segments = 1;
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_segments);
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
void blk_queue_max_segment_size(struct request_queue *q, unsigned int max_size)
{
	if (max_size < PAGE_CACHE_SIZE) {
		max_size = PAGE_CACHE_SIZE;
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_size);
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
void blk_queue_hardsect_size(struct request_queue *q, unsigned short size)
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
void blk_queue_stack_limits(struct request_queue *t, struct request_queue *b)
{
	/* zero is "infinity" */
	t->max_sectors = min_not_zero(t->max_sectors, b->max_sectors);
	t->max_hw_sectors = min_not_zero(t->max_hw_sectors, b->max_hw_sectors);

	t->max_phys_segments = min(t->max_phys_segments, b->max_phys_segments);
	t->max_hw_segments = min(t->max_hw_segments, b->max_hw_segments);
	t->max_segment_size = min(t->max_segment_size, b->max_segment_size);
	t->hardsect_size = max(t->hardsect_size, b->hardsect_size);
	if (!t->queue_lock)
		WARN_ON_ONCE(1);
	else if (!test_bit(QUEUE_FLAG_CLUSTER, &b->queue_flags)) {
		unsigned long flags;
		spin_lock_irqsave(t->queue_lock, flags);
		queue_flag_clear(QUEUE_FLAG_CLUSTER, t);
		spin_unlock_irqrestore(t->queue_lock, flags);
	}
}
EXPORT_SYMBOL(blk_queue_stack_limits);

/**
 * blk_queue_dma_pad - set pad mask
 * @q:     the request queue for the device
 * @mask:  pad mask
 *
 * Set dma pad mask.
 *
 * Appending pad buffer to a request modifies the last entry of a
 * scatter list such that it includes the pad buffer.
 **/
void blk_queue_dma_pad(struct request_queue *q, unsigned int mask)
{
	q->dma_pad_mask = mask;
}
EXPORT_SYMBOL(blk_queue_dma_pad);

/**
 * blk_queue_update_dma_pad - update pad mask
 * @q:     the request queue for the device
 * @mask:  pad mask
 *
 * Update dma pad mask.
 *
 * Appending pad buffer to a request modifies the last entry of a
 * scatter list such that it includes the pad buffer.
 **/
void blk_queue_update_dma_pad(struct request_queue *q, unsigned int mask)
{
	if (mask > q->dma_pad_mask)
		q->dma_pad_mask = mask;
}
EXPORT_SYMBOL(blk_queue_update_dma_pad);

/**
 * blk_queue_dma_drain - Set up a drain buffer for excess dma.
 * @q:  the request queue for the device
 * @dma_drain_needed: fn which returns non-zero if drain is necessary
 * @buf:	physically contiguous buffer
 * @size:	size of the buffer in bytes
 *
 * Some devices have excess DMA problems and can't simply discard (or
 * zero fill) the unwanted piece of the transfer.  They have to have a
 * real area of memory to transfer it into.  The use case for this is
 * ATAPI devices in DMA mode.  If the packet command causes a transfer
 * bigger than the transfer size some HBAs will lock up if there
 * aren't DMA elements to contain the excess transfer.  What this API
 * does is adjust the queue so that the buf is always appended
 * silently to the scatterlist.
 *
 * Note: This routine adjusts max_hw_segments to make room for
 * appending the drain buffer.  If you call
 * blk_queue_max_hw_segments() or blk_queue_max_phys_segments() after
 * calling this routine, you must set the limit to one fewer than your
 * device can support otherwise there won't be room for the drain
 * buffer.
 */
int blk_queue_dma_drain(struct request_queue *q,
			       dma_drain_needed_fn *dma_drain_needed,
			       void *buf, unsigned int size)
{
	if (q->max_hw_segments < 2 || q->max_phys_segments < 2)
		return -EINVAL;
	/* make room for appending the drain */
	--q->max_hw_segments;
	--q->max_phys_segments;
	q->dma_drain_needed = dma_drain_needed;
	q->dma_drain_buffer = buf;
	q->dma_drain_size = size;

	return 0;
}
EXPORT_SYMBOL_GPL(blk_queue_dma_drain);

/**
 * blk_queue_segment_boundary - set boundary rules for segment merging
 * @q:  the request queue for the device
 * @mask:  the memory boundary mask
 **/
void blk_queue_segment_boundary(struct request_queue *q, unsigned long mask)
{
	if (mask < PAGE_CACHE_SIZE - 1) {
		mask = PAGE_CACHE_SIZE - 1;
		printk(KERN_INFO "%s: set to minimum %lx\n",
		       __func__, mask);
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
 *    set required memory and length alignment for direct dma transactions.
 *    this is used when buiding direct io requests for the queue.
 *
 **/
void blk_queue_dma_alignment(struct request_queue *q, int mask)
{
	q->dma_alignment = mask;
}
EXPORT_SYMBOL(blk_queue_dma_alignment);

/**
 * blk_queue_update_dma_alignment - update dma length and memory alignment
 * @q:     the request queue for the device
 * @mask:  alignment mask
 *
 * description:
 *    update required memory and length alignment for direct dma transactions.
 *    If the requested alignment is larger than the current alignment, then
 *    the current queue alignment is updated to the new value, otherwise it
 *    is left alone.  The design of this is to allow multiple objects
 *    (driver, device, transport etc) to set their respective
 *    alignments without having them interfere.
 *
 **/
void blk_queue_update_dma_alignment(struct request_queue *q, int mask)
{
	BUG_ON(mask > PAGE_SIZE);

	if (mask > q->dma_alignment)
		q->dma_alignment = mask;
}
EXPORT_SYMBOL(blk_queue_update_dma_alignment);

static int __init blk_settings_init(void)
{
	blk_max_low_pfn = max_low_pfn - 1;
	blk_max_pfn = max_pfn - 1;
	return 0;
}
subsys_initcall(blk_settings_init);
