// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to setting various queue properties from drivers
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/blk-integrity.h>
#include <linux/pagemap.h>
#include <linux/backing-dev-defs.h>
#include <linux/gcd.h>
#include <linux/lcm.h>
#include <linux/jiffies.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>

#include "blk.h"
#include "blk-rq-qos.h"
#include "blk-wbt.h"

void blk_queue_rq_timeout(struct request_queue *q, unsigned int timeout)
{
	q->rq_timeout = timeout;
}
EXPORT_SYMBOL_GPL(blk_queue_rq_timeout);

/**
 * blk_set_stacking_limits - set default limits for stacking devices
 * @lim:  the queue_limits structure to reset
 *
 * Prepare queue limits for applying limits from underlying devices using
 * blk_stack_limits().
 */
void blk_set_stacking_limits(struct queue_limits *lim)
{
	memset(lim, 0, sizeof(*lim));
	lim->logical_block_size = SECTOR_SIZE;
	lim->physical_block_size = SECTOR_SIZE;
	lim->io_min = SECTOR_SIZE;
	lim->discard_granularity = SECTOR_SIZE;
	lim->dma_alignment = SECTOR_SIZE - 1;
	lim->seg_boundary_mask = BLK_SEG_BOUNDARY_MASK;

	/* Inherit limits from component devices */
	lim->max_segments = USHRT_MAX;
	lim->max_discard_segments = USHRT_MAX;
	lim->max_hw_sectors = UINT_MAX;
	lim->max_segment_size = UINT_MAX;
	lim->max_sectors = UINT_MAX;
	lim->max_dev_sectors = UINT_MAX;
	lim->max_write_zeroes_sectors = UINT_MAX;
	lim->max_hw_zone_append_sectors = UINT_MAX;
	lim->max_user_discard_sectors = UINT_MAX;
}
EXPORT_SYMBOL(blk_set_stacking_limits);

void blk_apply_bdi_limits(struct backing_dev_info *bdi,
		struct queue_limits *lim)
{
	/*
	 * For read-ahead of large files to be effective, we need to read ahead
	 * at least twice the optimal I/O size.
	 */
	bdi->ra_pages = max(lim->io_opt * 2 / PAGE_SIZE, VM_READAHEAD_PAGES);
	bdi->io_pages = lim->max_sectors >> PAGE_SECTORS_SHIFT;
}

static int blk_validate_zoned_limits(struct queue_limits *lim)
{
	if (!(lim->features & BLK_FEAT_ZONED)) {
		if (WARN_ON_ONCE(lim->max_open_zones) ||
		    WARN_ON_ONCE(lim->max_active_zones) ||
		    WARN_ON_ONCE(lim->zone_write_granularity) ||
		    WARN_ON_ONCE(lim->max_zone_append_sectors))
			return -EINVAL;
		return 0;
	}

	if (WARN_ON_ONCE(!IS_ENABLED(CONFIG_BLK_DEV_ZONED)))
		return -EINVAL;

	/*
	 * Given that active zones include open zones, the maximum number of
	 * open zones cannot be larger than the maximum number of active zones.
	 */
	if (lim->max_active_zones &&
	    lim->max_open_zones > lim->max_active_zones)
		return -EINVAL;

	if (lim->zone_write_granularity < lim->logical_block_size)
		lim->zone_write_granularity = lim->logical_block_size;

	/*
	 * The Zone Append size is limited by the maximum I/O size and the zone
	 * size given that it can't span zones.
	 *
	 * If no max_hw_zone_append_sectors limit is provided, the block layer
	 * will emulated it, else we're also bound by the hardware limit.
	 */
	lim->max_zone_append_sectors =
		min_not_zero(lim->max_hw_zone_append_sectors,
			min(lim->chunk_sectors, lim->max_hw_sectors));
	return 0;
}

static int blk_validate_integrity_limits(struct queue_limits *lim)
{
	struct blk_integrity *bi = &lim->integrity;

	if (!bi->tuple_size) {
		if (bi->csum_type != BLK_INTEGRITY_CSUM_NONE ||
		    bi->tag_size || ((bi->flags & BLK_INTEGRITY_REF_TAG))) {
			pr_warn("invalid PI settings.\n");
			return -EINVAL;
		}
		return 0;
	}

	if (!IS_ENABLED(CONFIG_BLK_DEV_INTEGRITY)) {
		pr_warn("integrity support disabled.\n");
		return -EINVAL;
	}

	if (bi->csum_type == BLK_INTEGRITY_CSUM_NONE &&
	    (bi->flags & BLK_INTEGRITY_REF_TAG)) {
		pr_warn("ref tag not support without checksum.\n");
		return -EINVAL;
	}

	if (!bi->interval_exp)
		bi->interval_exp = ilog2(lim->logical_block_size);

	return 0;
}

/*
 * Returns max guaranteed bytes which we can fit in a bio.
 *
 * We request that an atomic_write is ITER_UBUF iov_iter (so a single vector),
 * so we assume that we can fit in at least PAGE_SIZE in a segment, apart from
 * the first and last segments.
 */
static unsigned int blk_queue_max_guaranteed_bio(struct queue_limits *lim)
{
	unsigned int max_segments = min(BIO_MAX_VECS, lim->max_segments);
	unsigned int length;

	length = min(max_segments, 2) * lim->logical_block_size;
	if (max_segments > 2)
		length += (max_segments - 2) * PAGE_SIZE;

	return length;
}

static void blk_atomic_writes_update_limits(struct queue_limits *lim)
{
	unsigned int unit_limit = min(lim->max_hw_sectors << SECTOR_SHIFT,
					blk_queue_max_guaranteed_bio(lim));

	unit_limit = rounddown_pow_of_two(unit_limit);

	lim->atomic_write_max_sectors =
		min(lim->atomic_write_hw_max >> SECTOR_SHIFT,
			lim->max_hw_sectors);
	lim->atomic_write_unit_min =
		min(lim->atomic_write_hw_unit_min, unit_limit);
	lim->atomic_write_unit_max =
		min(lim->atomic_write_hw_unit_max, unit_limit);
	lim->atomic_write_boundary_sectors =
		lim->atomic_write_hw_boundary >> SECTOR_SHIFT;
}

static void blk_validate_atomic_write_limits(struct queue_limits *lim)
{
	unsigned int boundary_sectors;

	if (!lim->atomic_write_hw_max)
		goto unsupported;

	if (WARN_ON_ONCE(!is_power_of_2(lim->atomic_write_hw_unit_min)))
		goto unsupported;

	if (WARN_ON_ONCE(!is_power_of_2(lim->atomic_write_hw_unit_max)))
		goto unsupported;

	if (WARN_ON_ONCE(lim->atomic_write_hw_unit_min >
			 lim->atomic_write_hw_unit_max))
		goto unsupported;

	if (WARN_ON_ONCE(lim->atomic_write_hw_unit_max >
			 lim->atomic_write_hw_max))
		goto unsupported;

	boundary_sectors = lim->atomic_write_hw_boundary >> SECTOR_SHIFT;

	if (boundary_sectors) {
		if (WARN_ON_ONCE(lim->atomic_write_hw_max >
				 lim->atomic_write_hw_boundary))
			goto unsupported;
		/*
		 * A feature of boundary support is that it disallows bios to
		 * be merged which would result in a merged request which
		 * crosses either a chunk sector or atomic write HW boundary,
		 * even though chunk sectors may be just set for performance.
		 * For simplicity, disallow atomic writes for a chunk sector
		 * which is non-zero and smaller than atomic write HW boundary.
		 * Furthermore, chunk sectors must be a multiple of atomic
		 * write HW boundary. Otherwise boundary support becomes
		 * complicated.
		 * Devices which do not conform to these rules can be dealt
		 * with if and when they show up.
		 */
		if (WARN_ON_ONCE(lim->chunk_sectors % boundary_sectors))
			goto unsupported;

		/*
		 * The boundary size just needs to be a multiple of unit_max
		 * (and not necessarily a power-of-2), so this following check
		 * could be relaxed in future.
		 * Furthermore, if needed, unit_max could even be reduced so
		 * that it is compliant with a !power-of-2 boundary.
		 */
		if (!is_power_of_2(boundary_sectors))
			goto unsupported;
	}

	blk_atomic_writes_update_limits(lim);
	return;

unsupported:
	lim->atomic_write_max_sectors = 0;
	lim->atomic_write_boundary_sectors = 0;
	lim->atomic_write_unit_min = 0;
	lim->atomic_write_unit_max = 0;
}

/*
 * Check that the limits in lim are valid, initialize defaults for unset
 * values, and cap values based on others where needed.
 */
int blk_validate_limits(struct queue_limits *lim)
{
	unsigned int max_hw_sectors;
	unsigned int logical_block_sectors;
	int err;

	/*
	 * Unless otherwise specified, default to 512 byte logical blocks and a
	 * physical block size equal to the logical block size.
	 */
	if (!lim->logical_block_size)
		lim->logical_block_size = SECTOR_SIZE;
	else if (blk_validate_block_size(lim->logical_block_size)) {
		pr_warn("Invalid logical block size (%d)\n", lim->logical_block_size);
		return -EINVAL;
	}
	if (lim->physical_block_size < lim->logical_block_size)
		lim->physical_block_size = lim->logical_block_size;

	/*
	 * The minimum I/O size defaults to the physical block size unless
	 * explicitly overridden.
	 */
	if (lim->io_min < lim->physical_block_size)
		lim->io_min = lim->physical_block_size;

	/*
	 * The optimal I/O size may not be aligned to physical block size
	 * (because it may be limited by dma engines which have no clue about
	 * block size of the disks attached to them), so we round it down here.
	 */
	lim->io_opt = round_down(lim->io_opt, lim->physical_block_size);

	/*
	 * max_hw_sectors has a somewhat weird default for historical reason,
	 * but driver really should set their own instead of relying on this
	 * value.
	 *
	 * The block layer relies on the fact that every driver can
	 * handle at lest a page worth of data per I/O, and needs the value
	 * aligned to the logical block size.
	 */
	if (!lim->max_hw_sectors)
		lim->max_hw_sectors = BLK_SAFE_MAX_SECTORS;
	if (WARN_ON_ONCE(lim->max_hw_sectors < PAGE_SECTORS))
		return -EINVAL;
	logical_block_sectors = lim->logical_block_size >> SECTOR_SHIFT;
	if (WARN_ON_ONCE(logical_block_sectors > lim->max_hw_sectors))
		return -EINVAL;
	lim->max_hw_sectors = round_down(lim->max_hw_sectors,
			logical_block_sectors);

	/*
	 * The actual max_sectors value is a complex beast and also takes the
	 * max_dev_sectors value (set by SCSI ULPs) and a user configurable
	 * value into account.  The ->max_sectors value is always calculated
	 * from these, so directly setting it won't have any effect.
	 */
	max_hw_sectors = min_not_zero(lim->max_hw_sectors,
				lim->max_dev_sectors);
	if (lim->max_user_sectors) {
		if (lim->max_user_sectors < PAGE_SIZE / SECTOR_SIZE)
			return -EINVAL;
		lim->max_sectors = min(max_hw_sectors, lim->max_user_sectors);
	} else if (lim->io_opt > (BLK_DEF_MAX_SECTORS_CAP << SECTOR_SHIFT)) {
		lim->max_sectors =
			min(max_hw_sectors, lim->io_opt >> SECTOR_SHIFT);
	} else if (lim->io_min > (BLK_DEF_MAX_SECTORS_CAP << SECTOR_SHIFT)) {
		lim->max_sectors =
			min(max_hw_sectors, lim->io_min >> SECTOR_SHIFT);
	} else {
		lim->max_sectors = min(max_hw_sectors, BLK_DEF_MAX_SECTORS_CAP);
	}
	lim->max_sectors = round_down(lim->max_sectors,
			logical_block_sectors);

	/*
	 * Random default for the maximum number of segments.  Driver should not
	 * rely on this and set their own.
	 */
	if (!lim->max_segments)
		lim->max_segments = BLK_MAX_SEGMENTS;

	lim->max_discard_sectors =
		min(lim->max_hw_discard_sectors, lim->max_user_discard_sectors);

	if (!lim->max_discard_segments)
		lim->max_discard_segments = 1;

	if (lim->discard_granularity < lim->physical_block_size)
		lim->discard_granularity = lim->physical_block_size;

	/*
	 * By default there is no limit on the segment boundary alignment,
	 * but if there is one it can't be smaller than the page size as
	 * that would break all the normal I/O patterns.
	 */
	if (!lim->seg_boundary_mask)
		lim->seg_boundary_mask = BLK_SEG_BOUNDARY_MASK;
	if (WARN_ON_ONCE(lim->seg_boundary_mask < PAGE_SIZE - 1))
		return -EINVAL;

	/*
	 * Stacking device may have both virtual boundary and max segment
	 * size limit, so allow this setting now, and long-term the two
	 * might need to move out of stacking limits since we have immutable
	 * bvec and lower layer bio splitting is supposed to handle the two
	 * correctly.
	 */
	if (lim->virt_boundary_mask) {
		if (!lim->max_segment_size)
			lim->max_segment_size = UINT_MAX;
	} else {
		/*
		 * The maximum segment size has an odd historic 64k default that
		 * drivers probably should override.  Just like the I/O size we
		 * require drivers to at least handle a full page per segment.
		 */
		if (!lim->max_segment_size)
			lim->max_segment_size = BLK_MAX_SEGMENT_SIZE;
		if (WARN_ON_ONCE(lim->max_segment_size < PAGE_SIZE))
			return -EINVAL;
	}

	/*
	 * We require drivers to at least do logical block aligned I/O, but
	 * historically could not check for that due to the separate calls
	 * to set the limits.  Once the transition is finished the check
	 * below should be narrowed down to check the logical block size.
	 */
	if (!lim->dma_alignment)
		lim->dma_alignment = SECTOR_SIZE - 1;
	if (WARN_ON_ONCE(lim->dma_alignment > PAGE_SIZE))
		return -EINVAL;

	if (lim->alignment_offset) {
		lim->alignment_offset &= (lim->physical_block_size - 1);
		lim->flags &= ~BLK_FLAG_MISALIGNED;
	}

	if (!(lim->features & BLK_FEAT_WRITE_CACHE))
		lim->features &= ~BLK_FEAT_FUA;

	blk_validate_atomic_write_limits(lim);

	err = blk_validate_integrity_limits(lim);
	if (err)
		return err;
	return blk_validate_zoned_limits(lim);
}
EXPORT_SYMBOL_GPL(blk_validate_limits);

/*
 * Set the default limits for a newly allocated queue.  @lim contains the
 * initial limits set by the driver, which could be no limit in which case
 * all fields are cleared to zero.
 */
int blk_set_default_limits(struct queue_limits *lim)
{
	/*
	 * Most defaults are set by capping the bounds in blk_validate_limits,
	 * but max_user_discard_sectors is special and needs an explicit
	 * initialization to the max value here.
	 */
	lim->max_user_discard_sectors = UINT_MAX;
	return blk_validate_limits(lim);
}

/**
 * queue_limits_commit_update - commit an atomic update of queue limits
 * @q:		queue to update
 * @lim:	limits to apply
 *
 * Apply the limits in @lim that were obtained from queue_limits_start_update()
 * and updated by the caller to @q.  The caller must have frozen the queue or
 * ensure that there are no outstanding I/Os by other means.
 *
 * Returns 0 if successful, else a negative error code.
 */
int queue_limits_commit_update(struct request_queue *q,
		struct queue_limits *lim)
{
	int error;

	error = blk_validate_limits(lim);
	if (error)
		goto out_unlock;

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
	if (q->crypto_profile && lim->integrity.tag_size) {
		pr_warn("blk-integrity: Integrity and hardware inline encryption are not supported together.\n");
		error = -EINVAL;
		goto out_unlock;
	}
#endif

	q->limits = *lim;
	if (q->disk)
		blk_apply_bdi_limits(q->disk->bdi, lim);
out_unlock:
	mutex_unlock(&q->limits_lock);
	return error;
}
EXPORT_SYMBOL_GPL(queue_limits_commit_update);

/**
 * queue_limits_commit_update_frozen - commit an atomic update of queue limits
 * @q:		queue to update
 * @lim:	limits to apply
 *
 * Apply the limits in @lim that were obtained from queue_limits_start_update()
 * and updated with the new values by the caller to @q.  Freezes the queue
 * before the update and unfreezes it after.
 *
 * Returns 0 if successful, else a negative error code.
 */
int queue_limits_commit_update_frozen(struct request_queue *q,
		struct queue_limits *lim)
{
	int ret;

	blk_mq_freeze_queue(q);
	ret = queue_limits_commit_update(q, lim);
	blk_mq_unfreeze_queue(q);

	return ret;
}
EXPORT_SYMBOL_GPL(queue_limits_commit_update_frozen);

/**
 * queue_limits_set - apply queue limits to queue
 * @q:		queue to update
 * @lim:	limits to apply
 *
 * Apply the limits in @lim that were freshly initialized to @q.
 * To update existing limits use queue_limits_start_update() and
 * queue_limits_commit_update() instead.
 *
 * Returns 0 if successful, else a negative error code.
 */
int queue_limits_set(struct request_queue *q, struct queue_limits *lim)
{
	mutex_lock(&q->limits_lock);
	return queue_limits_commit_update(q, lim);
}
EXPORT_SYMBOL_GPL(queue_limits_set);

static int queue_limit_alignment_offset(const struct queue_limits *lim,
		sector_t sector)
{
	unsigned int granularity = max(lim->physical_block_size, lim->io_min);
	unsigned int alignment = sector_div(sector, granularity >> SECTOR_SHIFT)
		<< SECTOR_SHIFT;

	return (granularity + lim->alignment_offset - alignment) % granularity;
}

static unsigned int queue_limit_discard_alignment(
		const struct queue_limits *lim, sector_t sector)
{
	unsigned int alignment, granularity, offset;

	if (!lim->max_discard_sectors)
		return 0;

	/* Why are these in bytes, not sectors? */
	alignment = lim->discard_alignment >> SECTOR_SHIFT;
	granularity = lim->discard_granularity >> SECTOR_SHIFT;

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

/* Check if second and later bottom devices are compliant */
static bool blk_stack_atomic_writes_tail(struct queue_limits *t,
				struct queue_limits *b)
{
	/* We're not going to support different boundary sizes.. yet */
	if (t->atomic_write_hw_boundary != b->atomic_write_hw_boundary)
		return false;

	/* Can't support this */
	if (t->atomic_write_hw_unit_min > b->atomic_write_hw_unit_max)
		return false;

	/* Or this */
	if (t->atomic_write_hw_unit_max < b->atomic_write_hw_unit_min)
		return false;

	t->atomic_write_hw_max = min(t->atomic_write_hw_max,
				b->atomic_write_hw_max);
	t->atomic_write_hw_unit_min = max(t->atomic_write_hw_unit_min,
				b->atomic_write_hw_unit_min);
	t->atomic_write_hw_unit_max = min(t->atomic_write_hw_unit_max,
				b->atomic_write_hw_unit_max);
	return true;
}

/* Check for valid boundary of first bottom device */
static bool blk_stack_atomic_writes_boundary_head(struct queue_limits *t,
				struct queue_limits *b)
{
	/*
	 * Ensure atomic write boundary is aligned with chunk sectors. Stacked
	 * devices store chunk sectors in t->io_min.
	 */
	if (b->atomic_write_hw_boundary > t->io_min &&
	    b->atomic_write_hw_boundary % t->io_min)
		return false;
	if (t->io_min > b->atomic_write_hw_boundary &&
	    t->io_min % b->atomic_write_hw_boundary)
		return false;

	t->atomic_write_hw_boundary = b->atomic_write_hw_boundary;
	return true;
}


/* Check stacking of first bottom device */
static bool blk_stack_atomic_writes_head(struct queue_limits *t,
				struct queue_limits *b)
{
	if (b->atomic_write_hw_boundary &&
	    !blk_stack_atomic_writes_boundary_head(t, b))
		return false;

	if (t->io_min <= SECTOR_SIZE) {
		/* No chunk sectors, so use bottom device values directly */
		t->atomic_write_hw_unit_max = b->atomic_write_hw_unit_max;
		t->atomic_write_hw_unit_min = b->atomic_write_hw_unit_min;
		t->atomic_write_hw_max = b->atomic_write_hw_max;
		return true;
	}

	/*
	 * Find values for limits which work for chunk size.
	 * b->atomic_write_hw_unit_{min, max} may not be aligned with chunk
	 * size (t->io_min), as chunk size is not restricted to a power-of-2.
	 * So we need to find highest power-of-2 which works for the chunk
	 * size.
	 * As an example scenario, we could have b->unit_max = 16K and
	 * t->io_min = 24K. For this case, reduce t->unit_max to a value
	 * aligned with both limits, i.e. 8K in this example.
	 */
	t->atomic_write_hw_unit_max = b->atomic_write_hw_unit_max;
	while (t->io_min % t->atomic_write_hw_unit_max)
		t->atomic_write_hw_unit_max /= 2;

	t->atomic_write_hw_unit_min = min(b->atomic_write_hw_unit_min,
					  t->atomic_write_hw_unit_max);
	t->atomic_write_hw_max = min(b->atomic_write_hw_max, t->io_min);

	return true;
}

static void blk_stack_atomic_writes_limits(struct queue_limits *t,
				struct queue_limits *b, sector_t start)
{
	if (!(t->features & BLK_FEAT_ATOMIC_WRITES_STACKED))
		goto unsupported;

	if (!b->atomic_write_hw_unit_min)
		goto unsupported;

	if (!blk_atomic_write_start_sect_aligned(start, b))
		goto unsupported;

	/*
	 * If atomic_write_hw_max is set, we have already stacked 1x bottom
	 * device, so check for compliance.
	 */
	if (t->atomic_write_hw_max) {
		if (!blk_stack_atomic_writes_tail(t, b))
			goto unsupported;
		return;
	}

	if (!blk_stack_atomic_writes_head(t, b))
		goto unsupported;
	return;

unsupported:
	t->atomic_write_hw_max = 0;
	t->atomic_write_hw_unit_max = 0;
	t->atomic_write_hw_unit_min = 0;
	t->atomic_write_hw_boundary = 0;
	t->features &= ~BLK_FEAT_ATOMIC_WRITES_STACKED;
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

	t->features |= (b->features & BLK_FEAT_INHERIT_MASK);

	/*
	 * Some feaures need to be supported both by the stacking driver and all
	 * underlying devices.  The stacking driver sets these flags before
	 * stacking the limits, and this will clear the flags if any of the
	 * underlying devices does not support it.
	 */
	if (!(b->features & BLK_FEAT_NOWAIT))
		t->features &= ~BLK_FEAT_NOWAIT;
	if (!(b->features & BLK_FEAT_POLL))
		t->features &= ~BLK_FEAT_POLL;

	t->flags |= (b->flags & BLK_FLAG_MISALIGNED);

	t->max_sectors = min_not_zero(t->max_sectors, b->max_sectors);
	t->max_user_sectors = min_not_zero(t->max_user_sectors,
			b->max_user_sectors);
	t->max_hw_sectors = min_not_zero(t->max_hw_sectors, b->max_hw_sectors);
	t->max_dev_sectors = min_not_zero(t->max_dev_sectors, b->max_dev_sectors);
	t->max_write_zeroes_sectors = min(t->max_write_zeroes_sectors,
					b->max_write_zeroes_sectors);
	t->max_hw_zone_append_sectors = min(t->max_hw_zone_append_sectors,
					b->max_hw_zone_append_sectors);

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
			t->flags |= BLK_FLAG_MISALIGNED;
			ret = -1;
		}
	}

	t->logical_block_size = max(t->logical_block_size,
				    b->logical_block_size);

	t->physical_block_size = max(t->physical_block_size,
				     b->physical_block_size);

	t->io_min = max(t->io_min, b->io_min);
	t->io_opt = lcm_not_zero(t->io_opt, b->io_opt);
	t->dma_alignment = max(t->dma_alignment, b->dma_alignment);

	/* Set non-power-of-2 compatible chunk_sectors boundary */
	if (b->chunk_sectors)
		t->chunk_sectors = gcd(t->chunk_sectors, b->chunk_sectors);

	/* Physical block size a multiple of the logical block size? */
	if (t->physical_block_size & (t->logical_block_size - 1)) {
		t->physical_block_size = t->logical_block_size;
		t->flags |= BLK_FLAG_MISALIGNED;
		ret = -1;
	}

	/* Minimum I/O a multiple of the physical block size? */
	if (t->io_min & (t->physical_block_size - 1)) {
		t->io_min = t->physical_block_size;
		t->flags |= BLK_FLAG_MISALIGNED;
		ret = -1;
	}

	/* Optimal I/O a multiple of the physical block size? */
	if (t->io_opt & (t->physical_block_size - 1)) {
		t->io_opt = 0;
		t->flags |= BLK_FLAG_MISALIGNED;
		ret = -1;
	}

	/* chunk_sectors a multiple of the physical block size? */
	if ((t->chunk_sectors << 9) & (t->physical_block_size - 1)) {
		t->chunk_sectors = 0;
		t->flags |= BLK_FLAG_MISALIGNED;
		ret = -1;
	}

	/* Find lowest common alignment_offset */
	t->alignment_offset = lcm_not_zero(t->alignment_offset, alignment)
		% max(t->physical_block_size, t->io_min);

	/* Verify that new alignment_offset is on a logical block boundary */
	if (t->alignment_offset & (t->logical_block_size - 1)) {
		t->flags |= BLK_FLAG_MISALIGNED;
		ret = -1;
	}

	t->max_sectors = blk_round_down_sectors(t->max_sectors, t->logical_block_size);
	t->max_hw_sectors = blk_round_down_sectors(t->max_hw_sectors, t->logical_block_size);
	t->max_dev_sectors = blk_round_down_sectors(t->max_dev_sectors, t->logical_block_size);

	/* Discard alignment and granularity */
	if (b->discard_granularity) {
		alignment = queue_limit_discard_alignment(b, start);

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
	if (!(t->features & BLK_FEAT_ZONED)) {
		t->zone_write_granularity = 0;
		t->max_zone_append_sectors = 0;
	}
	blk_stack_atomic_writes_limits(t, b, start);

	return ret;
}
EXPORT_SYMBOL(blk_stack_limits);

/**
 * queue_limits_stack_bdev - adjust queue_limits for stacked devices
 * @t:	the stacking driver limits (top device)
 * @bdev:  the underlying block device (bottom)
 * @offset:  offset to beginning of data within component device
 * @pfx: prefix to use for warnings logged
 *
 * Description:
 *    This function is used by stacking drivers like MD and DM to ensure
 *    that all component devices have compatible block sizes and
 *    alignments.  The stacking driver must provide a queue_limits
 *    struct (top) and then iteratively call the stacking function for
 *    all component (bottom) devices.  The stacking function will
 *    attempt to combine the values and ensure proper alignment.
 */
void queue_limits_stack_bdev(struct queue_limits *t, struct block_device *bdev,
		sector_t offset, const char *pfx)
{
	if (blk_stack_limits(t, bdev_limits(bdev),
			get_start_sect(bdev) + offset))
		pr_notice("%s: Warning: Device %pg is misaligned\n",
			pfx, bdev);
}
EXPORT_SYMBOL_GPL(queue_limits_stack_bdev);

/**
 * queue_limits_stack_integrity - stack integrity profile
 * @t: target queue limits
 * @b: base queue limits
 *
 * Check if the integrity profile in the @b can be stacked into the
 * target @t.  Stacking is possible if either:
 *
 *   a) does not have any integrity information stacked into it yet
 *   b) the integrity profile in @b is identical to the one in @t
 *
 * If @b can be stacked into @t, return %true.  Else return %false and clear the
 * integrity information in @t.
 */
bool queue_limits_stack_integrity(struct queue_limits *t,
		struct queue_limits *b)
{
	struct blk_integrity *ti = &t->integrity;
	struct blk_integrity *bi = &b->integrity;

	if (!IS_ENABLED(CONFIG_BLK_DEV_INTEGRITY))
		return true;

	if (!ti->tuple_size) {
		/* inherit the settings from the first underlying device */
		if (!(ti->flags & BLK_INTEGRITY_STACKED)) {
			ti->flags = BLK_INTEGRITY_DEVICE_CAPABLE |
				(bi->flags & BLK_INTEGRITY_REF_TAG);
			ti->csum_type = bi->csum_type;
			ti->tuple_size = bi->tuple_size;
			ti->pi_offset = bi->pi_offset;
			ti->interval_exp = bi->interval_exp;
			ti->tag_size = bi->tag_size;
			goto done;
		}
		if (!bi->tuple_size)
			goto done;
	}

	if (ti->tuple_size != bi->tuple_size)
		goto incompatible;
	if (ti->interval_exp != bi->interval_exp)
		goto incompatible;
	if (ti->tag_size != bi->tag_size)
		goto incompatible;
	if (ti->csum_type != bi->csum_type)
		goto incompatible;
	if ((ti->flags & BLK_INTEGRITY_REF_TAG) !=
	    (bi->flags & BLK_INTEGRITY_REF_TAG))
		goto incompatible;

done:
	ti->flags |= BLK_INTEGRITY_STACKED;
	return true;

incompatible:
	memset(ti, 0, sizeof(*ti));
	return false;
}
EXPORT_SYMBOL_GPL(queue_limits_stack_integrity);

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

int bdev_alignment_offset(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (q->limits.flags & BLK_FLAG_MISALIGNED)
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
