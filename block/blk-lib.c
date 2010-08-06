/*
 * Functions related to generic helpers functions
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>

#include "blk.h"

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
	int type = flags & BLKDEV_IFL_BARRIER ?
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
		if (flags & BLKDEV_IFL_WAIT)
			bio->bi_private = &wait;

		/*
		 * Add a zeroed one-sector payload as that's what
		 * our current implementations need.  If we'll ever need
		 * more the interface will need revisiting.
		 */
		page = alloc_page(gfp_mask | __GFP_ZERO);
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

		if (flags & BLKDEV_IFL_WAIT)
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

struct bio_batch
{
	atomic_t 		done;
	unsigned long 		flags;
	struct completion 	*wait;
	bio_end_io_t		*end_io;
};

static void bio_batch_end_io(struct bio *bio, int err)
{
	struct bio_batch *bb = bio->bi_private;

	if (err) {
		if (err == -EOPNOTSUPP)
			set_bit(BIO_EOPNOTSUPP, &bb->flags);
		else
			clear_bit(BIO_UPTODATE, &bb->flags);
	}
	if (bb) {
		if (bb->end_io)
			bb->end_io(bio, err);
		atomic_inc(&bb->done);
		complete(bb->wait);
	}
	bio_put(bio);
}

/**
 * blkdev_issue_zeroout generate number of zero filed write bios
 * @bdev:	blockdev to issue
 * @sector:	start sector
 * @nr_sects:	number of sectors to write
 * @gfp_mask:	memory allocation flags (for bio_alloc)
 * @flags:	BLKDEV_IFL_* flags to control behaviour
 *
 * Description:
 *  Generate and issue number of bios with zerofiled pages.
 *  Send barrier at the beginning and at the end if requested. This guarantie
 *  correct request ordering. Empty barrier allow us to avoid post queue flush.
 */

int blkdev_issue_zeroout(struct block_device *bdev, sector_t sector,
			sector_t nr_sects, gfp_t gfp_mask, unsigned long flags)
{
	int ret = 0;
	struct bio *bio;
	struct bio_batch bb;
	unsigned int sz, issued = 0;
	DECLARE_COMPLETION_ONSTACK(wait);

	atomic_set(&bb.done, 0);
	bb.flags = 1 << BIO_UPTODATE;
	bb.wait = &wait;
	bb.end_io = NULL;

	if (flags & BLKDEV_IFL_BARRIER) {
		/* issue async barrier before the data */
		ret = blkdev_issue_flush(bdev, gfp_mask, NULL, 0);
		if (ret)
			return ret;
	}
submit:
	while (nr_sects != 0) {
		bio = bio_alloc(gfp_mask,
				min(nr_sects, (sector_t)BIO_MAX_PAGES));
		if (!bio)
			break;

		bio->bi_sector = sector;
		bio->bi_bdev   = bdev;
		bio->bi_end_io = bio_batch_end_io;
		if (flags & BLKDEV_IFL_WAIT)
			bio->bi_private = &bb;

		while (nr_sects != 0) {
			sz = min((sector_t) PAGE_SIZE >> 9 , nr_sects);
			if (sz == 0)
				/* bio has maximum size possible */
				break;
			ret = bio_add_page(bio, ZERO_PAGE(0), sz << 9, 0);
			nr_sects -= ret >> 9;
			sector += ret >> 9;
			if (ret < (sz << 9))
				break;
		}
		issued++;
		submit_bio(WRITE, bio);
	}
	/*
	 * When all data bios are in flight. Send final barrier if requeted.
	 */
	if (nr_sects == 0 && flags & BLKDEV_IFL_BARRIER)
		ret = blkdev_issue_flush(bdev, gfp_mask, NULL,
					flags & BLKDEV_IFL_WAIT);


	if (flags & BLKDEV_IFL_WAIT)
		/* Wait for bios in-flight */
		while ( issued != atomic_read(&bb.done))
			wait_for_completion(&wait);

	if (!test_bit(BIO_UPTODATE, &bb.flags))
		/* One of bios in the batch was completed with error.*/
		ret = -EIO;

	if (ret)
		goto out;

	if (test_bit(BIO_EOPNOTSUPP, &bb.flags)) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (nr_sects != 0)
		goto submit;
out:
	return ret;
}
EXPORT_SYMBOL(blkdev_issue_zeroout);
