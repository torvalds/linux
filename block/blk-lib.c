// SPDX-License-Identifier: GPL-2.0
/*
 * Functions related to generic helpers functions
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>

#include "blk.h"

static sector_t bio_discard_limit(struct block_device *bdev, sector_t sector)
{
	unsigned int discard_granularity = bdev_discard_granularity(bdev);
	sector_t granularity_aligned_sector;

	if (bdev_is_partition(bdev))
		sector += bdev->bd_start_sect;

	granularity_aligned_sector =
		round_up(sector, discard_granularity >> SECTOR_SHIFT);

	/*
	 * Make sure subsequent bios start aligned to the discard granularity if
	 * it needs to be split.
	 */
	if (granularity_aligned_sector != sector)
		return granularity_aligned_sector - sector;

	/*
	 * Align the bio size to the discard granularity to make splitting the bio
	 * at discard granularity boundaries easier in the driver if needed.
	 */
	return round_down(UINT_MAX, discard_granularity) >> SECTOR_SHIFT;
}

struct bio *blk_alloc_discard_bio(struct block_device *bdev,
		sector_t *sector, sector_t *nr_sects, gfp_t gfp_mask)
{
	sector_t bio_sects = min(*nr_sects, bio_discard_limit(bdev, *sector));
	struct bio *bio;

	if (!bio_sects)
		return NULL;

	bio = bio_alloc(bdev, 0, REQ_OP_DISCARD, gfp_mask);
	if (!bio)
		return NULL;
	bio->bi_iter.bi_sector = *sector;
	bio->bi_iter.bi_size = bio_sects << SECTOR_SHIFT;
	*sector += bio_sects;
	*nr_sects -= bio_sects;
	/*
	 * We can loop for a long time in here if someone does full device
	 * discards (like mkfs).  Be nice and allow us to schedule out to avoid
	 * softlocking if preempt is disabled.
	 */
	cond_resched();
	return bio;
}

int __blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, struct bio **biop)
{
	struct bio *bio;

	while ((bio = blk_alloc_discard_bio(bdev, &sector, &nr_sects,
			gfp_mask)))
		*biop = bio_chain_and_submit(*biop, bio);
	return 0;
}
EXPORT_SYMBOL(__blkdev_issue_discard);

/**
 * blkdev_issue_discard - queue a discard
 * @bdev:	blockdev to issue discard for
 * @sector:	start sector
 * @nr_sects:	number of sectors to discard
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 *
 * Description:
 *    Issue a discard request for the sectors in question.
 */
int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask)
{
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);
	ret = __blkdev_issue_discard(bdev, sector, nr_sects, gfp_mask, &bio);
	if (!ret && bio) {
		ret = submit_bio_wait(bio);
		if (ret == -EOPNOTSUPP)
			ret = 0;
		bio_put(bio);
	}
	blk_finish_plug(&plug);

	return ret;
}
EXPORT_SYMBOL(blkdev_issue_discard);

static sector_t bio_write_zeroes_limit(struct block_device *bdev)
{
	sector_t bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;

	return min(bdev_write_zeroes_sectors(bdev),
		(UINT_MAX >> SECTOR_SHIFT) & ~bs_mask);
}

/*
 * There is no reliable way for the SCSI subsystem to determine whether a
 * device supports a WRITE SAME operation without actually performing a write
 * to media. As a result, write_zeroes is enabled by default and will be
 * disabled if a zeroing operation subsequently fails. This means that this
 * queue limit is likely to change at runtime.
 */
static void __blkdev_issue_write_zeroes(struct block_device *bdev,
		sector_t sector, sector_t nr_sects, gfp_t gfp_mask,
		struct bio **biop, unsigned flags, sector_t limit)
{

	while (nr_sects) {
		unsigned int len = min(nr_sects, limit);
		struct bio *bio;

		if ((flags & BLKDEV_ZERO_KILLABLE) &&
		    fatal_signal_pending(current))
			break;

		bio = bio_alloc(bdev, 0, REQ_OP_WRITE_ZEROES, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		if (flags & BLKDEV_ZERO_NOUNMAP)
			bio->bi_opf |= REQ_NOUNMAP;

		bio->bi_iter.bi_size = len << SECTOR_SHIFT;
		*biop = bio_chain_and_submit(*biop, bio);

		nr_sects -= len;
		sector += len;
		cond_resched();
	}
}

static int blkdev_issue_write_zeroes(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp, unsigned flags)
{
	sector_t limit = bio_write_zeroes_limit(bdev);
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret = 0;

	blk_start_plug(&plug);
	__blkdev_issue_write_zeroes(bdev, sector, nr_sects, gfp, &bio,
			flags, limit);
	if (bio) {
		if ((flags & BLKDEV_ZERO_KILLABLE) &&
		    fatal_signal_pending(current)) {
			bio_await_chain(bio);
			blk_finish_plug(&plug);
			return -EINTR;
		}
		ret = submit_bio_wait(bio);
		bio_put(bio);
	}
	blk_finish_plug(&plug);

	/*
	 * For some devices there is no non-destructive way to verify whether
	 * WRITE ZEROES is actually supported.  These will clear the capability
	 * on an I/O error, in which case we'll turn any error into
	 * "not supported" here.
	 */
	if (ret && !limit)
		return -EOPNOTSUPP;
	return ret;
}

/*
 * Convert a number of 512B sectors to a number of pages.
 * The result is limited to a number of pages that can fit into a BIO.
 * Also make sure that the result is always at least 1 (page) for the cases
 * where nr_sects is lower than the number of sectors in a page.
 */
static unsigned int __blkdev_sectors_to_bio_pages(sector_t nr_sects)
{
	sector_t pages = DIV_ROUND_UP_SECTOR_T(nr_sects, PAGE_SIZE / 512);

	return min(pages, (sector_t)BIO_MAX_VECS);
}

static void __blkdev_issue_zero_pages(struct block_device *bdev,
		sector_t sector, sector_t nr_sects, gfp_t gfp_mask,
		struct bio **biop, unsigned int flags)
{
	while (nr_sects) {
		unsigned int nr_vecs = __blkdev_sectors_to_bio_pages(nr_sects);
		struct bio *bio;

		bio = bio_alloc(bdev, nr_vecs, REQ_OP_WRITE, gfp_mask);
		bio->bi_iter.bi_sector = sector;

		if ((flags & BLKDEV_ZERO_KILLABLE) &&
		    fatal_signal_pending(current))
			break;

		do {
			unsigned int len, added;

			len = min_t(sector_t,
				PAGE_SIZE, nr_sects << SECTOR_SHIFT);
			added = bio_add_page(bio, ZERO_PAGE(0), len, 0);
			if (added < len)
				break;
			nr_sects -= added >> SECTOR_SHIFT;
			sector += added >> SECTOR_SHIFT;
		} while (nr_sects);

		*biop = bio_chain_and_submit(*biop, bio);
		cond_resched();
	}
}

static int blkdev_issue_zero_pages(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp, unsigned flags)
{
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret = 0;

	if (flags & BLKDEV_ZERO_NOFALLBACK)
		return -EOPNOTSUPP;

	blk_start_plug(&plug);
	__blkdev_issue_zero_pages(bdev, sector, nr_sects, gfp, &bio, flags);
	if (bio) {
		if ((flags & BLKDEV_ZERO_KILLABLE) &&
		    fatal_signal_pending(current)) {
			bio_await_chain(bio);
			blk_finish_plug(&plug);
			return -EINTR;
		}
		ret = submit_bio_wait(bio);
		bio_put(bio);
	}
	blk_finish_plug(&plug);

	return ret;
}

/**
 * __blkdev_issue_zeroout - generate number of zero filed write bios
 * @bdev:	blockdev to issue
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @biop:	pointer to anchor bio
 * @flags:	controls detailed behavior
 *
 * Description:
 *  Zero-fill a block range, either using hardware offload or by explicitly
 *  writing zeroes to the device.
 *
 *  If a device is using logical block provisioning, the underlying space will
 *  not be released if %flags contains BLKDEV_ZERO_NOUNMAP.
 *
 *  If %flags contains BLKDEV_ZERO_NOFALLBACK, the function will return
 *  -EOPNOTSUPP if no explicit hardware offload for zeroing is provided.
 */
int __blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, struct bio **biop,
		unsigned flags)
{
	sector_t limit = bio_write_zeroes_limit(bdev);

	if (bdev_read_only(bdev))
		return -EPERM;

	if (limit) {
		__blkdev_issue_write_zeroes(bdev, sector, nr_sects,
				gfp_mask, biop, flags, limit);
	} else {
		if (flags & BLKDEV_ZERO_NOFALLBACK)
			return -EOPNOTSUPP;
		__blkdev_issue_zero_pages(bdev, sector, nr_sects, gfp_mask,
				biop, flags);
	}
	return 0;
}
EXPORT_SYMBOL(__blkdev_issue_zeroout);

/**
 * blkdev_issue_zeroout - zero-fill a block range
 * @bdev:	blockdev to write
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @flags:	controls detailed behavior
 *
 * Description:
 *  Zero-fill a block range, either using hardware offload or by explicitly
 *  writing zeroes to the device.  See __blkdev_issue_zeroout() for the
 *  valid values for %flags.
 */
int blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned flags)
{
	int ret;

	if ((sector | nr_sects) & ((bdev_logical_block_size(bdev) >> 9) - 1))
		return -EINVAL;
	if (bdev_read_only(bdev))
		return -EPERM;

	if (bdev_write_zeroes_sectors(bdev)) {
		ret = blkdev_issue_write_zeroes(bdev, sector, nr_sects,
				gfp_mask, flags);
		if (ret != -EOPNOTSUPP)
			return ret;
	}

	return blkdev_issue_zero_pages(bdev, sector, nr_sects, gfp_mask, flags);
}
EXPORT_SYMBOL(blkdev_issue_zeroout);

int blkdev_issue_secure_erase(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp)
{
	sector_t bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	unsigned int max_sectors = bdev_max_secure_erase_sectors(bdev);
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret = 0;

	/* make sure that "len << SECTOR_SHIFT" doesn't overflow */
	if (max_sectors > UINT_MAX >> SECTOR_SHIFT)
		max_sectors = UINT_MAX >> SECTOR_SHIFT;
	max_sectors &= ~bs_mask;

	if (max_sectors == 0)
		return -EOPNOTSUPP;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;
	if (bdev_read_only(bdev))
		return -EPERM;

	blk_start_plug(&plug);
	while (nr_sects) {
		unsigned int len = min_t(sector_t, nr_sects, max_sectors);

		bio = blk_next_bio(bio, bdev, 0, REQ_OP_SECURE_ERASE, gfp);
		bio->bi_iter.bi_sector = sector;
		bio->bi_iter.bi_size = len << SECTOR_SHIFT;

		sector += len;
		nr_sects -= len;
		cond_resched();
	}
	if (bio) {
		ret = submit_bio_wait(bio);
		bio_put(bio);
	}
	blk_finish_plug(&plug);

	return ret;
}
EXPORT_SYMBOL(blkdev_issue_secure_erase);
