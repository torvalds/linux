// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to setting various queue properties from drivers
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/backing-dev-defs.h>
#include <linux/gcd.h>
#include <linux/lcm.h>
#include <linux/jiffies.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>

#include "blk.h"
#include "blk-wbt.h"

void blk_queue_rq_timeout(struct request_queue *q, unsigned int timeout)
{
	q->rq_timeout = timeout;
}
EXPORT_SYMBOL_GPL(blk_queue_rq_timeout);

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
	lim->max_discard_segments = 1;
	lim->max_integrity_segments = 0;
	lim->seg_boundary_mask = BLK_SEG_BOUNDARY_MASK;
	lim->virt_boundary_mask = 0;
	lim->max_segment_size = BLK_MAX_SEGMENT_SIZE;
	lim->max_sectors = lim->max_hw_sectors = BLK_SAFE_MAX_SECTORS;
	lim->max_dev_sectors = 0;
	lim->chunk_sectors = 0;
	lim->max_write_zeroes_sectors = 0;
	lim->max_zone_append_sectors = 0;
	lim->max_discard_sectors = 0;
	lim->max_hw_discard_sectors = 0;
	lim->max_secure_erase_sectors = 0;
	lim->discard_granularity = 0;
	lim->discard_alignment = 0;
	lim->discard_misaligned = 0;
	lim->logical_block_size = lim->physical_block_size = lim->io_min = 512;
	lim->bounce = BLK_BOUNCE_NONE;
	lim->alignment_offset = 0;
	lim->io_opt = 0;
	lim->misaligned = 0;
	lim->zoned = BLK_ZONED_NONE;
	lim->zone_write_granularity = 0;
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
	lim->max_segments = USHRT_MAX;
	lim->max_discard_segments = USHRT_MAX;
	lim->max_hw_sectors = UINT_MAX;
	lim->max_segment_size = UINT_MAX;
	lim->max_sectors = UINT_MAX;
	lim->max_dev_sectors = UINT_MAX;
	lim->max_write_zeroes_sectors = UINT_MAX;
	lim->max_zone_append_sectors = UINT_MAX;
}
EXPORT_SYMBOL(blk_set_stacking_limits);

/**
 * blk_queue_bounce_limit - set bounce buffer limit for queue
 * @q: the request queue for the device
 * @bounce: bounce limit to enforce
 *
 * Description:
 *    Force bouncing for ISA DMA ranges or highmem.
 *
 *    DEPRECATED, don't use in new code.
 **/
void blk_queue_bounce_limit(struct request_queue *q, enum blk_bounce bounce)
{
	q->limits.bounce = bounce;
}
EXPORT_SYMBOL(blk_queue_bounce_limit);

/**
 * blk_queue_max_hw_sectors - set max sectors for a request for this queue
 * @q:  the request queue for the device
 * @max_hw_sectors:  max hardware sectors in the usual 512b unit
 *
 * Description:
 *    Enables a low level driver to set a hard upper limit,
 *    max_hw_sectors, on the size of requests.  max_hw_sectors is set by
 *    the device driver based upon the capabilities of the I/O
 *    controller.
 *
 *    max_dev_sectors is a hard limit imposed by the storage device for
 *    READ/WRITE requests. It is set by the disk driver.
 *
 *    max_sectors is a soft limit imposed by the block layer for
 *    filesystem type requests.  This value can be overridden on a
 *    per-device basis in /sys/block/<device>/queue/max_sectors_kb.
 *    The soft limit can not exceed max_hw_sectors.
 **/
void blk_queue_max_hw_sectors(struct request_queue *q, unsigned int max_hw_sectors)
{
	struct queue_limits *limits = &q->limits;
	unsigned int max_sectors;

	if ((max_hw_sectors << 9) < PAGE_SIZE) {
		max_hw_sectors = 1 << (PAGE_SHIFT - 9);
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_hw_sectors);
	}

	max_hw_sectors = round_down(max_hw_sectors,
				    limits->logical_block_size >> SECTOR_SHIFT);
	limits->max_hw_sectors = max_hw_sectors;

	max_sectors = min_not_zero(max_hw_sectors, limits->max_dev_sectors);
	max_sectors = min_t(unsigned int, max_sectors, BLK_DEF_MAX_SECTORS);
	max_sectors = round_down(max_sectors,
				 limits->logical_block_size >> SECTOR_SHIFT);
	limits->max_sectors = max_sectors;

	if (!q->disk)
		return;
	q->disk->bdi->io_pages = max_sectors >> (PAGE_SHIFT - 9);
}
EXPORT_SYMBOL(blk_queue_max_hw_sectors);

/**
 * blk_queue_chunk_sectors - set size of the chunk for this queue
 * @q:  the request queue for the device
 * @chunk_sectors:  chunk sectors in the usual 512b unit
 *
 * Description:
 *    If a driver doesn't want IOs to cross a given chunk size, it can set
 *    this limit and prevent merging across chunks. Note that the block layer
 *    must accept a page worth of data at any offset. So if the crossing of
 *    chunks is a hard limitation in the driver, it must still be prepared
 *    to split single page bios.
 **/
void blk_queue_chunk_sectors(struct request_queue *q, unsigned int chunk_sectors)
{
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
	q->limits.max_hw_discard_sectors = max_discard_sectors;
	q->limits.max_discard_sectors = max_discard_sectors;
}
EXPORT_SYMBOL(blk_queue_max_discard_sectors);

/**
 * blk_queue_max_secure_erase_sectors - set max sectors for a secure erase
 * @q:  the request queue for the device
 * @max_sectors: maximum number of sectors to secure_erase
 **/
void blk_queue_max_secure_erase_sectors(struct request_queue *q,
		unsigned int max_sectors)
{
	q->limits.max_secure_erase_sectors = max_sectors;
}
EXPORT_SYMBOL(blk_queue_max_secure_erase_sectors);

/**
 * blk_queue_max_write_zeroes_sectors - set max sectors for a single
 *                                      write zeroes
 * @q:  the request queue for the device
 * @max_write_zeroes_sectors: maximum number of sectors to write per command
 **/
void blk_queue_max_write_zeroes_sectors(struct request_queue *q,
		unsigned int max_write_zeroes_sectors)
{
	q->limits.max_write_zeroes_sectors = max_write_zeroes_sectors;
}
EXPORT_SYMBOL(blk_queue_max_write_zeroes_sectors);

/**
 * blk_queue_max_zone_append_sectors - set max sectors for a single zone append
 * @q:  the request queue for the device
 * @max_zone_append_sectors: maximum number of sectors to write per command
 **/
void blk_queue_max_zone_append_sectors(struct request_queue *q,
		unsigned int max_zone_append_sectors)
{
	unsigned int max_sectors;

	if (WARN_ON(!blk_queue_is_zoned(q)))
		return;

	max_sectors = min(q->limits.max_hw_sectors, max_zone_append_sectors);
	max_sectors = min(q->limits.chunk_sectors, max_sectors);

	/*
	 * Signal eventual driver bugs resulting in the max_zone_append sectors limit
	 * being 0 due to a 0 argument, the chunk_sectors limit (zone size) not set,
	 * or the max_hw_sectors limit not set.
	 */
	WARN_ON(!max_sectors);

	q->limits.max_zone_append_sectors = max_sectors;
}
EXPORT_SYMBOL_GPL(blk_queue_max_zone_append_sectors);

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
 * blk_queue_max_discard_segments - set max segments for discard requests
 * @q:  the request queue for the device
 * @max_segments:  max number of segments
 *
 * Description:
 *    Enables a low level driver to set an upper limit on the number of
 *    segments in a discard request.
 **/
void blk_queue_max_discard_segments(struct request_queue *q,
		unsigned short max_segments)
{
	q->limits.max_discard_segments = max_segments;
}
EXPORT_SYMBOL_GPL(blk_queue_max_discard_segments);

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
	if (max_size < PAGE_SIZE) {
		max_size = PAGE_SIZE;
		printk(KERN_INFO "%s: set to minimum %d\n",
		       __func__, max_size);
	}

	/* see blk_queue_virt_boundary() for the explanation */
	WARN_ON_ONCE(q->limits.virt_boundary_mask);

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
void blk_queue_logical_block_size(struct request_queue *q, unsigned int size)
{
	struct queue_limits *limits = &q->limits;

	limits->logical_block_size = size;

	if (limits->physical_block_size < size)
		limits->physical_block_size = size;

	if (limits->io_min < limits->physical_block_size)
		limits->io_min = limits->physical_block_size;

	limits->max_hw_sectors =
		round_down(limits->max_hw_sectors, size >> SECTOR_SHIFT);
	limits->max_sectors =
		round_down(limits->max_sectors, size >> SECTOR_SHIFT);
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
 * blk_queue_zone_write_granularity - set zone write granularity for the queue
 * @q:  the request queue for the zoned device
 * @size:  the zone write granularity size, in bytes
 *
 * Description:
 *   This should be set to the lowest possible size allowing to write in
 *   sequential zones of a zoned block device.
 */
void blk_queue_zone_write_granularity(struct request_queue *q,
				      unsigned int size)
{
	if (WARN_ON_ONCE(!blk_queue_is_zoned(q)))
		return;

	q->limits.zone_write_granularity = size;

	if (q->limits.zone_write_granularity < q->limits.logical_block_size)
		q->limits.zone_write_granularity = q->limits.logical_block_size;
}
EXPORT_SYMBOL_GPL(blk_queue_zone_write_granularity);

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

void disk_update_readahead(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;

	/*
	 * For read-ahead of large files to be effective, we need to read ahead
	 * at least twice the optimal I/O size.
	 */
	disk->bdi->ra_pages =
		max(queue_io_opt(q) * 2 / PAGE_SIZE, VM_READAHEAD_PAGES);
	disk->bdi->io_pages = queue_max_sectors(q) >> (PAGE_SHIFT - 9);
}
EXPORT_SYMBOL_GPL(disk_update_readahead);

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
	if (!q->disk)
		return;
	q->disk->bdi->ra_pages =
		max(queue_io_opt(q) * 2 / PAGE_SIZE, VM_READAHEAD_PAGES);
}
EXPORT_SYMBOL(blk_queue_io_opt);

static int queue_limit_alignment_offset(struct queue_limits *lim,
		sector_t sector)
{
	unsigned int granularity = max(lim->physical_block_size, lim->io_min);
	unsigned int alignment = sector_div(sector, granularity >> SECTOR_SHIFT)
		<< SECTOR_SHIFT;

	return (granularity + lim->alignment_offset - alignment) % granularity;
}

static unsigned int queue_limit_discard_alignment(struct queue_limits *lim,
		sector_t sector)
{
	unsigned int alignment, granularity, offset;

	if (!lim->max_discard_sectors)
		return 0;

	/* Why are these in bytes, not sectors? */
	alignment = lim->discard_alignment >> SECTOR_SHIFT;
	granularity = lim->discard_granularity >> SECTOR_SHIFT;
	if (!granularity)
		return 0;

	/* Offset of the partition start in 'granularity' sectors */
	offset = sector_div(sector, granularity);

	/* And why do we do this modulus *again* in blkdev_issue_discard()? */
	offset = (granularity + alignment - offset) % granularity;

	/* Turn it back into bytes, gaah */
	return offset << SECTOR_SHIFT;
}

static unsigned int blk_round_down_sectors(unsigned int sectors, unsigned int lbs)
{
	sectors = round_down(sectors, lbs >> SECTOR_SHIFT);
	if (sectors < PAGE_SIZE >> SECTOR_SHIFT)
		sectors = PAGE_SIZE >> SECTOR_SHIFT;
	return sectors;
}

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
	t->max_dev_sectors = min_not_zero(t->max_dev_sectors, b->max_dev_sectors);
	t->max_write_zeroes_sectors = min(t->max_write_zeroes_sectors,
					b->max_write_zeroes_sectors);
	t->max_zone_append_sectors = min(t->max_zone_append_sectors,
					b->max_zone_append_sectors);
	t->bounce = max(t->bounce, b->bounce);

	t->seg_boundary_mask = min_not_zero(t->seg_boundary_mask,
					    b->seg_boundary_mask);
	t->virt_boundary_mask = min_not_zero(t->virt_boundary_mask,
					    b->virt_boundary_mask);

	t->max_segments = min_not_zero(t->max_segments, b->max_segments);
	t->max_discard_segments = min_not_zero(t->max_discard_segments,
					       b->max_discard_segments);
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
		if (max(top, bottom) % min(top, bottom)) {
			t->misaligned = 1;
			ret = -1;
		}
	}

	t->logical_block_size = max(t->logical_block_size,
				    b->logical_block_size);

	t->physical_block_size = max(t->physical_block_size,
				     b->physical_block_size);

	t->io_min = max(t->io_min, b->io_min);
	t->io_opt = lcm_not_zero(t->io_opt, b->io_opt);

	/* Set non-power-of-2 compatible chunk_sectors boundary */
	if (b->chunk_sectors)
		t->chunk_sectors = gcd(t->chunk_sectors, b->chunk_sectors);

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

	/* chunk_sectors a multiple of the physical block size? */
	if ((t->chunk_sectors << 9) & (t->physical_block_size - 1)) {
		t->chunk_sectors = 0;
		t->misaligned = 1;
		ret = -1;
	}

	t->raid_partial_stripes_expensive =
		max(t->raid_partial_stripes_expensive,
		    b->raid_partial_stripes_expensive);

	/* Find lowest common alignment_offset */
	t->alignment_offset = lcm_not_zero(t->alignment_offset, alignment)
		% max(t->physical_block_size, t->io_min);

	/* Verify that new alignment_offset is on a logical block boundary */
	if (t->alignment_offset & (t->logical_block_size - 1)) {
		t->misaligned = 1;
		ret = -1;
	}

	t->max_sectors = blk_round_down_sectors(t->max_sectors, t->logical_block_size);
	t->max_hw_sectors = blk_round_down_sectors(t->max_hw_sectors, t->logical_block_size);
	t->max_dev_sectors = blk_round_down_sectors(t->max_dev_sectors, t->logical_block_size);

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
		t->max_hw_discard_sectors = min_not_zero(t->max_hw_discard_sectors,
							 b->max_hw_discard_sectors);
		t->discard_granularity = max(t->discard_granularity,
					     b->discard_granularity);
		t->discard_alignment = lcm_not_zero(t->discard_alignment, alignment) %
			t->discard_granularity;
	}
	t->max_secure_erase_sectors = min_not_zero(t->max_secure_erase_sectors,
						   b->max_secure_erase_sectors);
	t->zone_write_granularity = max(t->zone_write_granularity,
					b->zone_write_granularity);
	t->zoned = max(t->zoned, b->zoned);
	return ret;
}
EXPORT_SYMBOL(blk_stack_limits);

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

	if (blk_stack_limits(&t->limits, &bdev_get_queue(bdev)->limits,
			get_start_sect(bdev) + (offset >> 9)) < 0)
		pr_notice("%s: Warning: Device %pg is misaligned\n",
			disk->disk_name, bdev);

	disk_update_readahead(disk);
}
EXPORT_SYMBOL(disk_stack_limits);

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
 * blk_queue_segment_boundary - set boundary rules for segment merging
 * @q:  the request queue for the device
 * @mask:  the memory boundary mask
 **/
void blk_queue_segment_boundary(struct request_queue *q, unsigned long mask)
{
	if (mask < PAGE_SIZE - 1) {
		mask = PAGE_SIZE - 1;
		printk(KERN_INFO "%s: set to minimum %lx\n",
		       __func__, mask);
	}

	q->limits.seg_boundary_mask = mask;
}
EXPORT_SYMBOL(blk_queue_segment_boundary);

/**
 * blk_queue_virt_boundary - set boundary rules for bio merging
 * @q:  the request queue for the device
 * @mask:  the memory boundary mask
 **/
void blk_queue_virt_boundary(struct request_queue *q, unsigned long mask)
{
	q->limits.virt_boundary_mask = mask;

	/*
	 * Devices that require a virtual boundary do not support scatter/gather
	 * I/O natively, but instead require a descriptor list entry for each
	 * page (which might not be idential to the Linux PAGE_SIZE).  Because
	 * of that they are not limited by our notion of "segment size".
	 */
	if (mask)
		q->limits.max_segment_size = UINT_MAX;
}
EXPORT_SYMBOL(blk_queue_virt_boundary);

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
 * blk_set_queue_depth - tell the block layer about the device queue depth
 * @q:		the request queue for the device
 * @depth:		queue depth
 *
 */
void blk_set_queue_depth(struct request_queue *q, unsigned int depth)
{
	q->queue_depth = depth;
	rq_qos_queue_depth_changed(q);
}
EXPORT_SYMBOL(blk_set_queue_depth);

/**
 * blk_queue_write_cache - configure queue's write cache
 * @q:		the request queue for the device
 * @wc:		write back cache on or off
 * @fua:	device supports FUA writes, if true
 *
 * Tell the block layer about the write cache of @q.
 */
void blk_queue_write_cache(struct request_queue *q, bool wc, bool fua)
{
	if (wc)
		blk_queue_flag_set(QUEUE_FLAG_WC, q);
	else
		blk_queue_flag_clear(QUEUE_FLAG_WC, q);
	if (fua)
		blk_queue_flag_set(QUEUE_FLAG_FUA, q);
	else
		blk_queue_flag_clear(QUEUE_FLAG_FUA, q);

	wbt_set_write_cache(q, test_bit(QUEUE_FLAG_WC, &q->queue_flags));
}
EXPORT_SYMBOL_GPL(blk_queue_write_cache);

/**
 * blk_queue_required_elevator_features - Set a queue required elevator features
 * @q:		the request queue for the target device
 * @features:	Required elevator features OR'ed together
 *
 * Tell the block layer that for the device controlled through @q, only the
 * only elevators that can be used are those that implement at least the set of
 * features specified by @features.
 */
void blk_queue_required_elevator_features(struct request_queue *q,
					  unsigned int features)
{
	q->required_elevator_features = features;
}
EXPORT_SYMBOL_GPL(blk_queue_required_elevator_features);

/**
 * blk_queue_can_use_dma_map_merging - configure queue for merging segments.
 * @q:		the request queue for the device
 * @dev:	the device pointer for dma
 *
 * Tell the block layer about merging the segments by dma map of @q.
 */
bool blk_queue_can_use_dma_map_merging(struct request_queue *q,
				       struct device *dev)
{
	unsigned long boundary = dma_get_merge_boundary(dev);

	if (!boundary)
		return false;

	/* No need to update max_segment_size. see blk_queue_virt_boundary() */
	blk_queue_virt_boundary(q, boundary);

	return true;
}
EXPORT_SYMBOL_GPL(blk_queue_can_use_dma_map_merging);

static bool disk_has_partitions(struct gendisk *disk)
{
	unsigned long idx;
	struct block_device *part;
	bool ret = false;

	rcu_read_lock();
	xa_for_each(&disk->part_tbl, idx, part) {
		if (bdev_is_partition(part)) {
			ret = true;
			break;
		}
	}
	rcu_read_unlock();

	return ret;
}

/**
 * disk_set_zoned - configure the zoned model for a disk
 * @disk:	the gendisk of the queue to configure
 * @model:	the zoned model to set
 *
 * Set the zoned model of @disk to @model.
 *
 * When @model is BLK_ZONED_HM (host managed), this should be called only
 * if zoned block device support is enabled (CONFIG_BLK_DEV_ZONED option).
 * If @model specifies BLK_ZONED_HA (host aware), the effective model used
 * depends on CONFIG_BLK_DEV_ZONED settings and on the existence of partitions
 * on the disk.
 */
void disk_set_zoned(struct gendisk *disk, enum blk_zoned_model model)
{
	struct request_queue *q = disk->queue;

	switch (model) {
	case BLK_ZONED_HM:
		/*
		 * Host managed devices are supported only if
		 * CONFIG_BLK_DEV_ZONED is enabled.
		 */
		WARN_ON_ONCE(!IS_ENABLED(CONFIG_BLK_DEV_ZONED));
		break;
	case BLK_ZONED_HA:
		/*
		 * Host aware devices can be treated either as regular block
		 * devices (similar to drive managed devices) or as zoned block
		 * devices to take advantage of the zone command set, similarly
		 * to host managed devices. We try the latter if there are no
		 * partitions and zoned block device support is enabled, else
		 * we do nothing special as far as the block layer is concerned.
		 */
		if (!IS_ENABLED(CONFIG_BLK_DEV_ZONED) ||
		    disk_has_partitions(disk))
			model = BLK_ZONED_NONE;
		break;
	case BLK_ZONED_NONE:
	default:
		if (WARN_ON_ONCE(model != BLK_ZONED_NONE))
			model = BLK_ZONED_NONE;
		break;
	}

	q->limits.zoned = model;
	if (model != BLK_ZONED_NONE) {
		/*
		 * Set the zone write granularity to the device logical block
		 * size by default. The driver can change this value if needed.
		 */
		blk_queue_zone_write_granularity(q,
						queue_logical_block_size(q));
	} else {
		disk_clear_zone_settings(disk);
	}
}
EXPORT_SYMBOL_GPL(disk_set_zoned);

int bdev_alignment_offset(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (q->limits.misaligned)
		return -1;
	if (bdev_is_partition(bdev))
		return queue_limit_alignment_offset(&q->limits,
				bdev->bd_start_sect);
	return q->limits.alignment_offset;
}
EXPORT_SYMBOL_GPL(bdev_alignment_offset);

unsigned int bdev_discard_alignment(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (bdev_is_partition(bdev))
		return queue_limit_discard_alignment(&q->limits,
				bdev->bd_start_sect);
	return q->limits.discard_alignment;
}
EXPORT_SYMBOL_GPL(bdev_discard_alignment);
