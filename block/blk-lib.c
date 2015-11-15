/*
 * Functions related to generic helpers functions
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>

#include "blk.h"

struct bio_batch {
	atomic_t		done;
	int			error;
	struct completion	*wait;
};

static void bio_batch_end_io(struct bio *bio)
{
	struct bio_batch *bb = bio->bi_private;

	if (bio->bi_error && bio->bi_error != -EOPNOTSUPP)
		bb->error = bio->bi_error;
	if (atomic_dec_and_test(&bb->done))
		complete(bb->wait);
	bio_put(bio);
}

/**
 * blkdev_issue_discard - queue a discard
 * @bdev:	blockdev to issue discard for
 * @sector:	start sector
 * @nr_sects:	number of sectors to discard
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @flags:	BLKDEV_IFL_* flags to control behaviour
 *
 * Description:
 *    Issue a discard request for the sectors in question.
 */
int blkdev_issue_discard(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned long flags)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request_queue *q = bdev_get_queue(bdev);
	int type = REQ_WRITE | REQ_DISCARD;
	unsigned int granularity;
	int alignment;
	struct bio_batch bb;
	struct bio *bio;
	int ret = 0;
	struct blk_plug plug;

	if (!q)
		return -ENXIO;

	if (!blk_queue_discard(q))
		return -EOPNOTSUPP;

	/* Zero-sector (unknown) and one-sector granularities are the same.  */
	granularity = max(q->limits.discard_granularity >> 9, 1U);
	alignment = (bdev_discard_alignment(bdev) >> 9) % granularity;

	if (flags & BLKDEV_DISCARD_SECURE) {
		if (!blk_queue_secdiscard(q))
			return -EOPNOTSUPP;
		type |= REQ_SECURE;
	}

	atomic_set(&bb.done, 1);
	bb.error = 0;
	bb.wait = &wait;

	blk_start_plug(&plug);
	while (nr_sects) {
		unsigned int req_sects;
		sector_t end_sect, tmp;

		bio = bio_alloc(gfp_mask, 1);
		if (!bio) {
			ret = -ENOMEM;
			break;
		}

		/* Make sure bi_size doesn't overflow */
		req_sects = min_t(sector_t, nr_sects, UINT_MAX >> 9);

		/*
		 * If splitting a request, and the next starting sector would be
		 * misaligned, stop the discard at the previous aligned sector.
		 */
		end_sect = sector + req_sects;
		tmp = end_sect;
		if (req_sects < nr_sects &&
		    sector_div(tmp, granularity) != alignment) {
			end_sect = end_sect - alignment;
			sector_div(end_sect, granularity);
			end_sect = end_sect * granularity + alignment;
			req_sects = end_sect - sector;
		}

		bio->bi_iter.bi_sector = sector;
		bio->bi_end_io = bio_batch_end_io;
		bio->bi_bdev = bdev;
		bio->bi_private = &bb;

		bio->bi_iter.bi_size = req_sects << 9;
		nr_sects -= req_sects;
		sector = end_sect;

		atomic_inc(&bb.done);
		submit_bio(type, bio);

		/*
		 * We can loop for a long time in here, if someone does
		 * full device discards (like mkfs). Be nice and allow
		 * us to schedule out to avoid softlocking if preempt
		 * is disabled.
		 */
		cond_resched();
	}
	blk_finish_plug(&plug);

	/* Wait for bios in-flight */
	if (!atomic_dec_and_test(&bb.done))
		wait_for_completion_io(&wait);

	if (bb.error)
		return bb.error;
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_discard);

/**
 * blkdev_issue_write_same - queue a write same operation
 * @bdev:	target blockdev
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @page:	page containing data to write
 *
 * Description:
 *    Issue a write same request for the sectors in question.
 */
int blkdev_issue_write_same(struct block_device *bdev, sector_t sector,
			    sector_t nr_sects, gfp_t gfp_mask,
			    struct page *page)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	struct request_queue *q = bdev_get_queue(bdev);
	unsigned int max_write_same_sectors;
	struct bio_batch bb;
	struct bio *bio;
	int ret = 0;

	if (!q)
		return -ENXIO;

	/* Ensure that max_write_same_sectors doesn't overflow bi_size */
	max_write_same_sectors = UINT_MAX >> 9;

	atomic_set(&bb.done, 1);
	bb.error = 0;
	bb.wait = &wait;

	while (nr_sects) {
		bio = bio_alloc(gfp_mask, 1);
		if (!bio) {
			ret = -ENOMEM;
			break;
		}

		bio->bi_iter.bi_sector = sector;
		bio->bi_end_io = bio_batch_end_io;
		bio->bi_bdev = bdev;
		bio->bi_private = &bb;
		bio->bi_vcnt = 1;
		bio->bi_io_vec->bv_page = page;
		bio->bi_io_vec->bv_offset = 0;
		bio->bi_io_vec->bv_len = bdev_logical_block_size(bdev);

		if (nr_sects > max_write_same_sectors) {
			bio->bi_iter.bi_size = max_write_same_sectors << 9;
			nr_sects -= max_write_same_sectors;
			sector += max_write_same_sectors;
		} else {
			bio->bi_iter.bi_size = nr_sects << 9;
			nr_sects = 0;
		}

		atomic_inc(&bb.done);
		submit_bio(REQ_WRITE | REQ_WRITE_SAME, bio);
	}

	/* Wait for bios in-flight */
	if (!atomic_dec_and_test(&bb.done))
		wait_for_completion_io(&wait);

	if (bb.error)
		return bb.error;
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_write_same);

/**
 * blkdev_issue_zeroout - generate number of zero filed write bios
 * @bdev:	blockdev to issue
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 *
 * Description:
 *  Generate and issue number of bios with zerofiled pages.
 */

static int __blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
				  sector_t nr_sects, gfp_t gfp_mask)
{
	int ret;
	struct bio *bio;
	struct bio_batch bb;
	unsigned int sz;
	DECLARE_COMPLETION_ONSTACK(wait);

	atomic_set(&bb.done, 1);
	bb.error = 0;
	bb.wait = &wait;

	ret = 0;
	while (nr_sects != 0) {
		bio = bio_alloc(gfp_mask,
				min(nr_sects, (sector_t)BIO_MAX_PAGES));
		if (!bio) {
			ret = -ENOMEM;
			break;
		}

		bio->bi_iter.bi_sector = sector;
		bio->bi_bdev   = bdev;
		bio->bi_end_io = bio_batch_end_io;
		bio->bi_private = &bb;

		while (nr_sects != 0) {
			sz = min((sector_t) PAGE_SIZE >> 9 , nr_sects);
			ret = bio_add_page(bio, ZERO_PAGE(0), sz << 9, 0);
			nr_sects -= ret >> 9;
			sector += ret >> 9;
			if (ret < (sz << 9))
				break;
		}
		ret = 0;
		atomic_inc(&bb.done);
		submit_bio(WRITE, bio);
	}

	/* Wait for bios in-flight */
	if (!atomic_dec_and_test(&bb.done))
		wait_for_completion_io(&wait);

	if (bb.error)
		return bb.error;
	return ret;
}

/**
 * blkdev_issue_zeroout - zero-fill a block range
 * @bdev:	blockdev to write
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @discard:	whether to discard the block range
 *
 * Description:
 *  Zero-fill a block range.  If the discard flag is set and the block
 *  device guarantees that subsequent READ operations to the block range
 *  in question will return zeroes, the blocks will be discarded. Should
 *  the discard request fail, if the discard flag is not set, or if
 *  discard_zeroes_data is not supported, this function will resort to
 *  zeroing the blocks manually, thus provisioning (allocating,
 *  anchoring) them. If the block device supports the WRITE SAME command
 *  blkdev_issue_zeroout() will use it to optimize the process of
 *  clearing the block range. Otherwise the zeroing will be performed
 *  using regular WRITE calls.
 */

int blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
			 sector_t nr_sects, gfp_t gfp_mask, bool discard)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (discard && blk_queue_discard(q) && q->limits.discard_zeroes_data &&
	    blkdev_issue_discard(bdev, sector, nr_sects, gfp_mask, 0) == 0)
		return 0;

	if (bdev_write_same(bdev) &&
	    blkdev_issue_write_same(bdev, sector, nr_sects, gfp_mask,
				    ZERO_PAGE(0)) == 0)
		return 0;

	return __blkdev_issue_zeroout(bdev, sector, nr_sects, gfp_mask);
}
EXPORT_SYMBOL(blkdev_issue_zeroout);
