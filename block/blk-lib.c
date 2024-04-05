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

int __blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, struct bio **biop)
{
	struct bio *bio = *biop;
	sector_t bs_mask;

	if (bdev_read_only(bdev))
		return -EPERM;
	if (!bdev_max_discard_sectors(bdev))
		return -EOPNOTSUPP;

	/* In case the discard granularity isn't set by buggy device driver */
	if (WARN_ON_ONCE(!bdev_discard_granularity(bdev))) {
		pr_err_ratelimited("%pg: Error: discard_granularity is 0.\n",
				   bdev);
		return -EOPNOTSUPP;
	}

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

	if (!nr_sects)
		return -EINVAL;

	while (nr_sects) {
		sector_t req_sects =
			min(nr_sects, bio_discard_limit(bdev, sector));

		bio = blk_next_bio(bio, bdev, 0, REQ_OP_DISCARD, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		bio->bi_iter.bi_size = req_sects << 9;
		sector += req_sects;
		nr_sects -= req_sects;

		/*
		 * We can loop for a long time in here, if someone does
		 * full device discards (like mkfs). Be nice and allow
		 * us to schedule out to avoid softlocking if preempt
		 * is disabled.
		 */
		cond_resched();
	}

	*biop = bio;
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

static int __blkdev_issue_write_zeroes(struct block_device *bdev,
		sector_t sector, sector_t nr_sects, gfp_t gfp_mask,
		struct bio **biop, unsigned flags)
{
	struct bio *bio = *biop;
	unsigned int max_sectors;

	if (bdev_read_only(bdev))
		return -EPERM;

	/* Ensure that max_sectors doesn't overflow bi_size */
	max_sectors = bdev_write_zeroes_sectors(bdev);

	if (max_sectors == 0)
		return -EOPNOTSUPP;

	while (nr_sects) {
		unsigned int len = min_t(sector_t, nr_sects, max_sectors);

		bio = blk_next_bio(bio, bdev, 0, REQ_OP_WRITE_ZEROES, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		if (flags & BLKDEV_ZERO_NOUNMAP)
			bio->bi_opf |= REQ_NOUNMAP;

		bio->bi_iter.bi_size = len << SECTOR_SHIFT;
		nr_sects -= len;
		sector += len;
		cond_resched();
	}

	*biop = bio;
	return 0;
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

static int __blkdev_issue_zero_pages(struct block_device *bdev,
		sector_t sector, sector_t nr_sects, gfp_t gfp_mask,
		struct bio **biop)
{
	struct bio *bio = *biop;
	int bi_size = 0;
	unsigned int sz;

	if (bdev_read_only(bdev))
		return -EPERM;

	while (nr_sects != 0) {
		bio = blk_next_bio(bio, bdev, __blkdev_sectors_to_bio_pages(nr_sects),
				   REQ_OP_WRITE, gfp_mask);
		bio->bi_iter.bi_sector = sector;

		while (nr_sects != 0) {
			sz = min((sector_t) PAGE_SIZE, nr_sects << 9);
			bi_size = bio_add_page(bio, ZERO_PAGE(0), sz, 0);
			nr_sects -= bi_size >> 9;
			sector += bi_size >> 9;
			if (bi_size < sz)
				break;
		}
		cond_resched();
	}

	*biop = bio;
	return 0;
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
	int ret;
	sector_t bs_mask;

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

	ret = __blkdev_issue_write_zeroes(bdev, sector, nr_sects, gfp_mask,
			biop, flags);
	if (ret != -EOPNOTSUPP || (flags & BLKDEV_ZERO_NOFALLBACK))
		return ret;

	return __blkdev_issue_zero_pages(bdev, sector, nr_sects, gfp_mask,
					 biop);
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
	int ret = 0;
	sector_t bs_mask;
	struct bio *bio;
	struct blk_plug plug;
	bool try_write_zeroes = !!bdev_write_zeroes_sectors(bdev);

	bs_mask = (bdev_logical_block_size(bdev) >> 9) - 1;
	if ((sector | nr_sects) & bs_mask)
		return -EINVAL;

retry:
	bio = NULL;
	blk_start_plug(&plug);
	if (try_write_zeroes) {
		ret = __blkdev_issue_write_zeroes(bdev, sector, nr_sects,
						  gfp_mask, &bio, flags);
	} else if (!(flags & BLKDEV_ZERO_NOFALLBACK)) {
		ret = __blkdev_issue_zero_pages(bdev, sector, nr_sects,
						gfp_mask, &bio);
	} else {
		/* No zeroing offload support */
		ret = -EOPNOTSUPP;
	}
	if (ret == 0 && bio) {
		ret = submit_bio_wait(bio);
		bio_put(bio);
	}
	blk_finish_plug(&plug);
	if (ret && try_write_zeroes) {
		if (!(flags & BLKDEV_ZERO_NOFALLBACK)) {
			try_write_zeroes = false;
			goto retry;
		}
		if (!bdev_write_zeroes_sectors(bdev)) {
			/*
			 * Zeroing offload support was indicated, but the
			 * device reported ILLEGAL REQUEST (for some devices
			 * there is no non-destructive way to verify whether
			 * WRITE ZEROES is actually supported).
			 */
			ret = -EOPNOTSUPP;
		}
	}

	return ret;
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
