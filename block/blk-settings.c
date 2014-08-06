/*
 * Functions related to setting various queue properties from drivers
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/bootmem.h>	/* for max_pfn/max_low_pfn */
#include <linux/gcd.h>
#include <linux/lcm.h>
#include <linux/jiffies.h>
#include <linux/gfp.h>

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
 * blk_queue_unprep_rq - set an unprepare_request function for queue
 * @q:		queue
 * @ufn:	unprepare_request function
 *
 * It's possible for a queue to register an unprepare_request callback
 * which is invoked before the request is finally completed. The goal
 * of the function is to deallocate any data that was allocated in the
 * prepare_request callback.
 *
 */
void blk_queue_unprep_rq(struct request_queue *q, unprep_rq_fn *ufn)
{
	q->unprep_rq_fn = ufn;
}
EXPORT_SYMBOL(blk_queue_unprep_rq);

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

void blk_queue_lld_busy(struct request_queue *q, lld_busy_fn *fn)
{
	q->lld_busy_fn = fn;
}
EXPORT_SYMBOL_GPL(blk_queue_lld_busy);

/**
 * blk_set_default_limits - reset limits to default values
 * @lim:  the queue_limits structure to reset
 *
 * Description:
 *   Returns a queue_limit struct to its default state.
 */
void blk_set_default_limits(struct queue_limits *lim)
{
	lim->max_segments = BLK_MAX_SEGMENTS;
	lim->max_integrity_segments = 0;
	lim->seg_boundary_mask = BLK_SEG_BOUNDARY_MASK;
	lim->max_segment_size = BLK_MAX_SEGMENT_SIZE;
	lim->max_sectors = lim->max_hw_sectors = BLK_SAFE_MAX_SECTORS;
	lim->chunk_sectors = 0;
	lim->max_write_same_sectors = 0;
	lim->max_discard_sectors = 0;
	lim->discard_granularity = 0;
	lim->discard_alignment = 0;
	lim->discard_misaligned = 0;
	lim->discard_zeroes_data = 0;
	lim->logical_block_size = lim->physical_block_size = lim->io_min = 512;
	lim->bounce_pfn = (unsigned long)(BLK_BOUNCE_ANY >> PAGE_SHIFT);
	lim->alignment_offset = 0;
	lim->io_opt = 0;
	lim->misaligned = 0;
	lim->cluster = 1;
}
EXPORT_SYMBOL(blk_set_default_limits);

/**
 * blk_set_stacking_limits - set default limits for stacking devices
 * @lim:  the queue_limits structure to reset
 *
 * Description:
 *   Returns a queue_limit struct to its default state. Should be used
 *   by stacking drivers like DM that have no internal limits.
 */
void blk_set_stacking_limits(struct queue_limits *lim)
{
	blk_set_default_limits(lim);

	/* Inherit limits from component devices */
	lim->discard_zeroes_data = 1;
	lim->max_segments = USHRT_MAX;
	lim->max_hw_sectors = UINT_MAX;
	lim->max_segment_size = UINT_MAX;
	lim->max_sectors = UINT_MAX;
	lim->max_write_same_sectors = UINT_MAX;
}
EXPORT_SYMBOL(blk_set_stacking_limits);

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

	q->make_request_fn = mfn;
	blk_queue_dma_alignment(q, 511);
	blk_queue_congestion_threshold(q);
	q->nr_batching = BLK_BATCH_REQ;

	blk_set_default_limits(&q->limits);

	/*
	 * by default assume old behaviour and bounce for any highmem page
	 */
	blk_queue_bounce_limit(q, BLK_BOUNCE_HIGH);
}
EXPORT_SYMBOL(blk_queue_make_request);

/**
 * blk_queue_bounce_limit - set bounce buffer limit for queue
 * @q: the request queue for the device
 * @max_addr: the maximum address the device can handle
 *
 * Description:
 *    Different hardware can have different requirements as to what pages
 *    it can do I/O directly to. A low level driver can call
 *    blk_queue_bounce_limit to have lower memory pages allocated as bounce
 *    buffers for doing I/O to pages residing above @max_addr.
 **/
void blk_queue_bounce_limit(struct request_queue *q, u64 max_addr)
{
	unsigned long b_pfn = max_addr >> PAGE_SHIFT;
	int dma = 0;

	q->bounce_gfp = GFP_NOIO;
#if BITS_PER_LONG == 64
	/*
	 * Assume anything <= 4GB can be handled by IOMMU.  Actually
	 * some IOMMUs can handle everything, but I don't know of a
	 * way to test this here.
	 */
	if (b_pfn < (min_t(u64, 0xffffffffUL, BLK_BOUNCE_HIGH) >> PAGE_SHIFT))
		dma = 1;
	q->limits.bounce_pfn = max(max_low_pfn, b_pfn);
#else
	if (b_pfn < blk_max_low_pfn)
		dma = 1;
	q->limits.bounce_pfn = b_pfn;
#endif
	if (dma) {
		init_emergency_isa_pool();
		q->bounce_gfp = GFP_NOIO | GFP_DMA;
		q->limits.bounce_pfn = b_pfn;
	}
}
EXPORT_SYMBOL(blk_queue_bounce_limit);

/**
 * blk_limits_max_hw_sectors - set hard and soft limit of max sectors for request
 * @limits: the queue limits
 * @max_hw_sectors:  max hardware sectors in the usual 512b unit
 *
 * Description:
 *    Enables a low level driver to set a hard upper limit,
 *    max_hw_sectors, on the size of requests.  max_hw_sectors is set by
 *    the device driver based upon the combined capabilities of I/O
 *    controller and storage device.
 *
 *    max_sectors is a soft limit imposed by the block layer for
 *    filesystem type requests.  This value can be overridden on a
 *    per-device basis in /sys/block/<device>/queue/max_sectors_kb.
 *    The soft limit can not exceed max_hw_sectors.
 **/
void blk_limits_max_hw_sectors(struct queue_limits *limits, unsigned int max_hw_sectors)
{
	if ((max_hw_sectors << 9) < PAGE_CACHE_SIZE) {
		max_hw_sectors = 1 << (PAGE_CACHE_SHIFT - 9);
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_hw_sectors);
	}

	limits->max_hw_sectors = max_hw_sectors;
	limits->max_sectors = min_t(unsigned int, max_hw_sectors,
				    BLK_DEF_MAX_SECTORS);
}
EXPORT_SYMBOL(blk_limits_max_hw_sectors);

/**
 * blk_queue_max_hw_sectors - set max sectors for a request for this queue
 * @q:  the request queue for the device
 * @max_hw_sectors:  max hardware sectors in the usual 512b unit
 *
 * Description:
 *    See description for blk_limits_max_hw_sectors().
 **/
void blk_queue_max_hw_sectors(struct request_queue *q, unsigned int max_hw_sectors)
{
	blk_limits_max_hw_sectors(&q->limits, max_hw_sectors);
}
EXPORT_SYMBOL(blk_queue_max_hw_sectors);

/**
 * blk_queue_chunk_sectors - set size of the chunk for this queue
 * @q:  the request queue for the device
 * @chunk_sectors:  chunk sectors in the usual 512b unit
 *
 * Description:
 *    If a driver doesn't want IOs to cross a given chunk size, it can set
 *    this limit and prevent merging across chunks. Note that the chunk size
 *    must currently be a power-of-2 in sectors. Also note that the block
 *    layer must accept a page worth of data at any offset. So if the
 *    crossing of chunks is a hard limitation in the driver, it must still be
 *    prepared to split single page bios.
 **/
void blk_queue_chunk_sectors(struct request_queue *q, unsigned int chunk_sectors)
{
	BUG_ON(!is_power_of_2(chunk_sectors));
	q->limits.chunk_sectors = chunk_sectors;
}
EXPORT_SYMBOL(blk_queue_chunk_sectors);

/**
 * blk_queue_max_discard_sectors - set max sectors for a single discard
 * @q:  the request queue for the device
 * @max_discard_sectors: maximum number of sectors to discard
 **/
void blk_queue_max_discard_sectors(struct request_queue *q,
		unsigned int max_discard_sectors)
{
	q->limits.max_discard_sectors = max_discard_sectors;
}
EXPORT_SYMBOL(blk_queue_max_discard_sectors);

/**
 * blk_queue_max_write_same_sectors - set max sectors for a single write same
 * @q:  the request queue for the device
 * @max_write_same_sectors: maximum number of sectors to write per command
 **/
void blk_queue_max_write_same_sectors(struct request_queue *q,
				      unsigned int max_write_same_sectors)
{
	q->limits.max_write_same_sectors = max_write_same_sectors;
}
EXPORT_SYMBOL(blk_queue_max_write_same_sectors);

/**
 * blk_queue_max_segments - set max hw segments for a request for this queue
 * @q:  the request queue for the device
 * @max_segments:  max number of segments
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the number of
 *    hw data segments in a request.
 **/
void blk_queue_max_segments(struct request_queue *q, unsigned short max_segments)
{
	if (!max_segments) {
		max_segments = 1;
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_segments);
	}

	q->limits.max_segments = max_segments;
}
EXPORT_SYMBOL(blk_queue_max_segments);

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

	q->limits.max_segment_size = max_size;
}
EXPORT_SYMBOL(blk_queue_max_segment_size);

/**
 * blk_queue_logical_block_size - set logical block size for the queue
 * @q:  the request queue for the device
 * @size:  the logical block size, in bytes
 *
 * Description:
 *   This should be set to the lowest possible block size that the
 *   storage device can address.  The default of 512 covers most
 *   hardware.
 **/
void blk_queue_logical_block_size(struct request_queue *q, unsigned short size)
{
	q->limits.logical_block_size = size;

	if (q->limits.physical_block_size < size)
		q->limits.physical_block_size = size;

	if (q->limits.io_min < q->limits.physical_block_size)
		q->limits.io_min = q->limits.physical_block_size;
}
EXPORT_SYMBOL(blk_queue_logical_block_size);

/**
 * blk_queue_physical_block_size - set physical block size for the queue
 * @q:  the request queue for the device
 * @size:  the physical block size, in bytes
 *
 * Description:
 *   This should be set to the lowest possible sector size that the
 *   hardware can operate on without reverting to read-modify-write
 *   operations.
 */
void blk_queue_physical_block_size(struct request_queue *q, unsigned int size)
{
	q->limits.physical_block_size = size;

	if (q->limits.physical_block_size < q->limits.logical_block_size)
		q->limits.physical_block_size = q->limits.logical_block_size;

	if (q->limits.io_min < q->limits.physical_block_size)
		q->limits.io_min = q->limits.physical_block_size;
}
EXPORT_SYMBOL(blk_queue_physical_block_size);

/**
 * blk_queue_alignment_offset - set physical block alignment offset
 * @q:	the request queue for the device
 * @offset: alignment offset in bytes
 *
 * Description:
 *   Some devices are naturally misaligned to compensate for things like
 *   the legacy DOS partition table 63-sector offset.  Low-level drivers
 *   should call this function for devices whose first sector is not
 *   naturally aligned.
 */
void blk_queue_alignment_offset(struct request_queue *q, unsigned int offset)
{
	q->limits.alignment_offset =
		offset & (q->limits.physical_block_size - 1);
	q->limits.misaligned = 0;
}
EXPORT_SYMBOL(blk_queue_alignment_offset);

/**
 * blk_limits_io_min - set minimum request size for a device
 * @limits: the queue limits
 * @min:  smallest I/O size in bytes
 *
 * Description:
 *   Some devices have an internal block size bigger than the reported
 *   hardware sector size.  This function can be used to signal the
 *   smallest I/O the device can perform without incurring a performance
 *   penalty.
 */
void blk_limits_io_min(struct queue_limits *limits, unsigned int min)
{
	limits->io_min = min;

	if (limits->io_min < limits->logical_block_size)
		limits->io_min = limits->logical_block_size;

	if (limits->io_min < limits->physical_block_size)
		limits->io_min = limits->physical_block_size;
}
EXPORT_SYMBOL(blk_limits_io_min);

/**
 * blk_queue_io_min - set minimum request size for the queue
 * @q:	the request queue for the device
 * @min:  smallest I/O size in bytes
 *
 * Description:
 *   Storage devices may report a granularity or preferred minimum I/O
 *   size which is the smallest request the device can perform without
 *   incurring a performance penalty.  For disk drives this is often the
 *   physical block size.  For RAID arrays it is often the stripe chunk
 *   size.  A properly aligned multiple of minimum_io_size is the
 *   preferred request size for workloads where a high number of I/O
 *   operations is desired.
 */
void blk_queue_io_min(struct request_queue *q, unsigned int min)
{
	blk_limits_io_min(&q->limits, min);
}
EXPORT_SYMBOL(blk_queue_io_min);

/**
 * blk_limits_io_opt - set optimal request size for a device
 * @limits: the queue limits
 * @opt:  smallest I/O size in bytes
 *
 * Description:
 *   Storage devices may report an optimal I/O size, which is the
 *   device's preferred unit for sustained I/O.  This is rarely reported
 *   for disk drives.  For RAID arrays it is usually the stripe width or
 *   the internal track size.  A properly aligned multiple of
 *   optimal_io_size is the preferred request size for workloads where
 *   sustained throughput is desired.
 */
void blk_limits_io_opt(struct queue_limits *limits, unsigned int opt)
{
	limits->io_opt = opt;
}
EXPORT_SYMBOL(blk_limits_io_opt);

/**
 * blk_queue_io_opt - set optimal request size for the queue
 * @q:	the request queue for the device
 * @opt:  optimal request size in bytes
 *
 * Description:
 *   Storage devices may report an optimal I/O size, which is the
 *   device's preferred unit for sustained I/O.  This is rarely reported
 *   for disk drives.  For RAID arrays it is usually the stripe width or
 *   the internal track size.  A properly aligned multiple of
 *   optimal_io_size is the preferred request size for workloads where
 *   sustained throughput is desired.
 */
void blk_queue_io_opt(struct request_queue *q, unsigned int opt)
{
	blk_limits_io_opt(&q->limits, opt);
}
EXPORT_SYMBOL(blk_queue_io_opt);

/**
 * blk_queue_stack_limits - inherit underlying queue limits for stacked drivers
 * @t:	the stacking driver (top)
 * @b:  the underlying device (bottom)
 **/
void blk_queue_stack_limits(struct request_queue *t, struct request_queue *b)
{
	blk_stack_limits(&t->limits, &b->limits, 0);
}
EXPORT_SYMBOL(blk_queue_stack_limits);

/**
 * blk_stack_limits - adjust queue_limits for stacked devices
 * @t:	the stacking driver limits (top device)
 * @b:  the underlying queue limits (bottom, component device)
 * @start:  first data sector within component device
 *
 * Description:
 *    This function is used by stacking drivers like MD and DM to ensure
 *    that all component devices have compatible block sizes and
 *    alignments.  The stacking driver must provide a queue_limits
 *    struct (top) and then iteratively call the stacking function for
 *    all component (bottom) devices.  The stacking function will
 *    attempt to combine the values and ensure proper alignment.
 *
 *    Returns 0 if the top and bottom queue_limits are compatible.  The
 *    top device's block sizes and alignment offsets may be adjusted to
 *    ensure alignment with the bottom device. If no compatible sizes
 *    and alignments exist, -1 is returned and the resulting top
 *    queue_limits will have the misaligned flag set to indicate that
 *    the alignment_offset is undefined.
 */
int blk_stack_limits(struct queue_limits *t, struct queue_limits *b,
		     sector_t start)
{
	unsigned int top, bottom, alignment, ret = 0;

	t->max_sectors = min_not_zero(t->max_sectors, b->max_sectors);
	t->max_hw_sectors = min_not_zero(t->max_hw_sectors, b->max_hw_sectors);
	t->max_write_same_sectors = min(t->max_write_same_sectors,
					b->max_write_same_sectors);
	t->bounce_pfn = min_not_zero(t->bounce_pfn, b->bounce_pfn);

	t->seg_boundary_mask = min_not_zero(t->seg_boundary_mask,
					    b->seg_boundary_mask);

	t->max_segments = min_not_zero(t->max_segments, b->max_segments);
	t->max_integrity_segments = min_not_zero(t->max_integrity_segments,
						 b->max_integrity_segments);

	t->max_segment_size = min_not_zero(t->max_segment_size,
					   b->max_segment_size);

	t->misaligned |= b->misaligned;

	alignment = queue_limit_alignment_offset(b, start);

	/* Bottom device has different alignment.  Check that it is
	 * compatible with the current top alignment.
	 */
	if (t->alignment_offset != alignment) {

		top = max(t->physical_block_size, t->io_min)
			+ t->alignment_offset;
		bottom = max(b->physical_block_size, b->io_min) + alignment;

		/* Verify that top and bottom intervals line up */
		if (max(top, bottom) & (min(top, bottom) - 1)) {
			t->misaligned = 1;
			ret = -1;
		}
	}

	t->logical_block_size = max(t->logical_block_size,
				    b->logical_block_size);

	t->physical_block_size = max(t->physical_block_size,
				     b->physical_block_size);

	t->io_min = max(t->io_min, b->io_min);
	t->io_opt = lcm(t->io_opt, b->io_opt);

	t->cluster &= b->cluster;
	t->discard_zeroes_data &= b->discard_zeroes_data;

	/* Physical block size a multiple of the logical block size? */
	if (t->physical_block_size & (t->logical_block_size - 1)) {
		t->physical_block_size = t->logical_block_size;
		t->misaligned = 1;
		ret = -1;
	}

	/* Minimum I/O a multiple of the physical block size? */
	if (t->io_min & (t->physical_block_size - 1)) {
		t->io_min = t->physical_block_size;
		t->misaligned = 1;
		ret = -1;
	}

	/* Optimal I/O a multiple of the physical block size? */
	if (t->io_opt & (t->physical_block_size - 1)) {
		t->io_opt = 0;
		t->misaligned = 1;
		ret = -1;
	}

	t->raid_partial_stripes_expensive =
		max(t->raid_partial_stripes_expensive,
		    b->raid_partial_stripes_expensive);

	/* Find lowest common alignment_offset */
	t->alignment_offset = lcm(t->alignment_offset, alignment)
		& (max(t->physical_block_size, t->io_min) - 1);

	/* Verify that new alignment_offset is on a logical block boundary */
	if (t->alignment_offset & (t->logical_block_size - 1)) {
		t->misaligned = 1;
		ret = -1;
	}

	/* Discard alignment and granularity */
	if (b->discard_granularity) {
		alignment = queue_limit_discard_alignment(b, start);

		if (t->discard_granularity != 0 &&
		    t->discard_alignment != alignment) {
			top = t->discard_granularity + t->discard_alignment;
			bottom = b->discard_granularity + alignment;

			/* Verify that top and bottom intervals line up */
			if ((max(top, bottom) % min(top, bottom)) != 0)
				t->discard_misaligned = 1;
		}

		t->max_discard_sectors = min_not_zero(t->max_discard_sectors,
						      b->max_discard_sectors);
		t->discard_granularity = max(t->discard_granularity,
					     b->discard_granularity);
		t->discard_alignment = lcm(t->discard_alignment, alignment) %
			t->discard_granularity;
	}

	return ret;
}
EXPORT_SYMBOL(blk_stack_limits);

/**
 * bdev_stack_limits - adjust queue limits for stacked drivers
 * @t:	the stacking driver limits (top device)
 * @bdev:  the component block_device (bottom)
 * @start:  first data sector within component device
 *
 * Description:
 *    Merges queue limits for a top device and a block_device.  Returns
 *    0 if alignment didn't change.  Returns -1 if adding the bottom
 *    device caused misalignment.
 */
int bdev_stack_limits(struct queue_limits *t, struct block_device *bdev,
		      sector_t start)
{
	struct request_queue *bq = bdev_get_queue(bdev);

	start += get_start_sect(bdev);

	return blk_stack_limits(t, &bq->limits, start);
}
EXPORT_SYMBOL(bdev_stack_limits);

/**
 * disk_stack_limits - adjust queue limits for stacked drivers
 * @disk:  MD/DM gendisk (top)
 * @bdev:  the underlying block device (bottom)
 * @offset:  offset to beginning of data within component device
 *
 * Description:
 *    Merges the limits for a top level gendisk and a bottom level
 *    block_device.
 */
void disk_stack_limits(struct gendisk *disk, struct block_device *bdev,
		       sector_t offset)
{
	struct request_queue *t = disk->queue;

	if (bdev_stack_limits(&t->limits, bdev, offset >> 9) < 0) {
		char top[BDEVNAME_SIZE], bottom[BDEVNAME_SIZE];

		disk_name(disk, 0, top);
		bdevname(bdev, bottom);

		printk(KERN_NOTICE "%s: Warning: Device %s is misaligned\n",
		       top, bottom);
	}
}
EXPORT_SYMBOL(disk_stack_limits);

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
 * Note: This routine adjusts max_hw_segments to make room for appending
 * the drain buffer.  If you call blk_queue_max_segments() after calling
 * this routine, you must set the limit to one fewer than your device
 * can support otherwise there won't be room for the drain buffer.
 */
int blk_queue_dma_drain(struct request_queue *q,
			       dma_drain_needed_fn *dma_drain_needed,
			       void *buf, unsigned int size)
{
	if (queue_max_segments(q) < 2)
		return -EINVAL;
	/* make room for appending the drain */
	blk_queue_max_segments(q, queue_max_segments(q) - 1);
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

	q->limits.seg_boundary_mask = mask;
}
EXPORT_SYMBOL(blk_queue_segment_boundary);

/**
 * blk_queue_dma_alignment - set dma length and memory alignment
 * @q:     the request queue for the device
 * @mask:  alignment mask
 *
 * description:
 *    set required memory and length alignment for direct dma transactions.
 *    this is used when building direct io requests for the queue.
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

/**
 * blk_queue_flush - configure queue's cache flush capability
 * @q:		the request queue for the device
 * @flush:	0, REQ_FLUSH or REQ_FLUSH | REQ_FUA
 *
 * Tell block layer cache flush capability of @q.  If it supports
 * flushing, REQ_FLUSH should be set.  If it supports bypassing
 * write cache for individual writes, REQ_FUA should be set.
 */
void blk_queue_flush(struct request_queue *q, unsigned int flush)
{
	WARN_ON_ONCE(flush & ~(REQ_FLUSH | REQ_FUA));

	if (WARN_ON_ONCE(!(flush & REQ_FLUSH) && (flush & REQ_FUA)))
		flush &= ~REQ_FUA;

	q->flush_flags = flush & (REQ_FLUSH | REQ_FUA);
}
EXPORT_SYMBOL_GPL(blk_queue_flush);

void blk_queue_flush_queueable(struct request_queue *q, bool queueable)
{
	q->flush_not_queueable = !queueable;
}
EXPORT_SYMBOL_GPL(blk_queue_flush_queueable);

static int __init blk_settings_init(void)
{
	blk_max_low_pfn = max_low_pfn - 1;
	blk_max_pfn = max_pfn - 1;
	return 0;
}
subsys_initcall(blk_settings_init);
