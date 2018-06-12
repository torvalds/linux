/*
 * Zoned block device handling
 *
 * Copyright (c) 2015, Hannes Reinecke
 * Copyright (c) 2015, SUSE Linux GmbH
 *
 * Copyright (c) 2016, Damien Le Moal
 * Copyright (c) 2016, Western Digital
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/blkdev.h>

static inline sector_t blk_zone_start(struct request_queue *q,
				      sector_t sector)
{
	sector_t zone_mask = blk_queue_zone_sectors(q) - 1;

	return sector & ~zone_mask;
}

/*
 * Return true if a request is a write requests that needs zone write locking.
 */
bool blk_req_needs_zone_write_lock(struct request *rq)
{
	if (!rq->q->seq_zones_wlock)
		return false;

	if (blk_rq_is_passthrough(rq))
		return false;

	switch (req_op(rq)) {
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE_SAME:
	case REQ_OP_WRITE:
		return blk_rq_zone_is_seq(rq);
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(blk_req_needs_zone_write_lock);

void __blk_req_zone_write_lock(struct request *rq)
{
	if (WARN_ON_ONCE(test_and_set_bit(blk_rq_zone_no(rq),
					  rq->q->seq_zones_wlock)))
		return;

	WARN_ON_ONCE(rq->rq_flags & RQF_ZONE_WRITE_LOCKED);
	rq->rq_flags |= RQF_ZONE_WRITE_LOCKED;
}
EXPORT_SYMBOL_GPL(__blk_req_zone_write_lock);

void __blk_req_zone_write_unlock(struct request *rq)
{
	rq->rq_flags &= ~RQF_ZONE_WRITE_LOCKED;
	if (rq->q->seq_zones_wlock)
		WARN_ON_ONCE(!test_and_clear_bit(blk_rq_zone_no(rq),
						 rq->q->seq_zones_wlock));
}
EXPORT_SYMBOL_GPL(__blk_req_zone_write_unlock);

/*
 * Check that a zone report belongs to the partition.
 * If yes, fix its start sector and write pointer, copy it in the
 * zone information array and return true. Return false otherwise.
 */
static bool blkdev_report_zone(struct block_device *bdev,
			       struct blk_zone *rep,
			       struct blk_zone *zone)
{
	sector_t offset = get_start_sect(bdev);

	if (rep->start < offset)
		return false;

	rep->start -= offset;
	if (rep->start + rep->len > bdev->bd_part->nr_sects)
		return false;

	if (rep->type == BLK_ZONE_TYPE_CONVENTIONAL)
		rep->wp = rep->start + rep->len;
	else
		rep->wp -= offset;
	memcpy(zone, rep, sizeof(struct blk_zone));

	return true;
}

/**
 * blkdev_report_zones - Get zones information
 * @bdev:	Target block device
 * @sector:	Sector from which to report zones
 * @zones:	Array of zone structures where to return the zones information
 * @nr_zones:	Number of zone structures in the zone array
 * @gfp_mask:	Memory allocation flags (for bio_alloc)
 *
 * Description:
 *    Get zone information starting from the zone containing @sector.
 *    The number of zone information reported may be less than the number
 *    requested by @nr_zones. The number of zones actually reported is
 *    returned in @nr_zones.
 */
int blkdev_report_zones(struct block_device *bdev,
			sector_t sector,
			struct blk_zone *zones,
			unsigned int *nr_zones,
			gfp_t gfp_mask)
{
	struct request_queue *q = bdev_get_queue(bdev);
	struct blk_zone_report_hdr *hdr;
	unsigned int nrz = *nr_zones;
	struct page *page;
	unsigned int nr_rep;
	size_t rep_bytes;
	unsigned int nr_pages;
	struct bio *bio;
	struct bio_vec *bv;
	unsigned int i, n, nz;
	unsigned int ofst;
	void *addr;
	int ret;

	if (!q)
		return -ENXIO;

	if (!blk_queue_is_zoned(q))
		return -EOPNOTSUPP;

	if (!nrz)
		return 0;

	if (sector > bdev->bd_part->nr_sects) {
		*nr_zones = 0;
		return 0;
	}

	/*
	 * The zone report has a header. So make room for it in the
	 * payload. Also make sure that the report fits in a single BIO
	 * that will not be split down the stack.
	 */
	rep_bytes = sizeof(struct blk_zone_report_hdr) +
		sizeof(struct blk_zone) * nrz;
	rep_bytes = (rep_bytes + PAGE_SIZE - 1) & PAGE_MASK;
	if (rep_bytes > (queue_max_sectors(q) << 9))
		rep_bytes = queue_max_sectors(q) << 9;

	nr_pages = min_t(unsigned int, BIO_MAX_PAGES,
			 rep_bytes >> PAGE_SHIFT);
	nr_pages = min_t(unsigned int, nr_pages,
			 queue_max_segments(q));

	bio = bio_alloc(gfp_mask, nr_pages);
	if (!bio)
		return -ENOMEM;

	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = blk_zone_start(q, sector);
	bio_set_op_attrs(bio, REQ_OP_ZONE_REPORT, 0);

	for (i = 0; i < nr_pages; i++) {
		page = alloc_page(gfp_mask);
		if (!page) {
			ret = -ENOMEM;
			goto out;
		}
		if (!bio_add_page(bio, page, PAGE_SIZE, 0)) {
			__free_page(page);
			break;
		}
	}

	if (i == 0)
		ret = -ENOMEM;
	else
		ret = submit_bio_wait(bio);
	if (ret)
		goto out;

	/*
	 * Process the report result: skip the header and go through the
	 * reported zones to fixup and fixup the zone information for
	 * partitions. At the same time, return the zone information into
	 * the zone array.
	 */
	n = 0;
	nz = 0;
	nr_rep = 0;
	bio_for_each_segment_all(bv, bio, i) {

		if (!bv->bv_page)
			break;

		addr = kmap_atomic(bv->bv_page);

		/* Get header in the first page */
		ofst = 0;
		if (!nr_rep) {
			hdr = (struct blk_zone_report_hdr *) addr;
			nr_rep = hdr->nr_zones;
			ofst = sizeof(struct blk_zone_report_hdr);
		}

		/* Fixup and report zones */
		while (ofst < bv->bv_len &&
		       n < nr_rep && nz < nrz) {
			if (blkdev_report_zone(bdev, addr + ofst, &zones[nz]))
				nz++;
			ofst += sizeof(struct blk_zone);
			n++;
		}

		kunmap_atomic(addr);

		if (n >= nr_rep || nz >= nrz)
			break;

	}

	*nr_zones = nz;
out:
	bio_for_each_segment_all(bv, bio, i)
		__free_page(bv->bv_page);
	bio_put(bio);

	return ret;
}
EXPORT_SYMBOL_GPL(blkdev_report_zones);

/**
 * blkdev_reset_zones - Reset zones write pointer
 * @bdev:	Target block device
 * @sector:	Start sector of the first zone to reset
 * @nr_sectors:	Number of sectors, at least the length of one zone
 * @gfp_mask:	Memory allocation flags (for bio_alloc)
 *
 * Description:
 *    Reset the write pointer of the zones contained in the range
 *    @sector..@sector+@nr_sectors. Specifying the entire disk sector range
 *    is valid, but the specified range should not contain conventional zones.
 */
int blkdev_reset_zones(struct block_device *bdev,
		       sector_t sector, sector_t nr_sectors,
		       gfp_t gfp_mask)
{
	struct request_queue *q = bdev_get_queue(bdev);
	sector_t zone_sectors;
	sector_t end_sector = sector + nr_sectors;
	struct bio *bio;
	int ret;

	if (!q)
		return -ENXIO;

	if (!blk_queue_is_zoned(q))
		return -EOPNOTSUPP;

	if (end_sector > bdev->bd_part->nr_sects)
		/* Out of range */
		return -EINVAL;

	/* Check alignment (handle eventual smaller last zone) */
	zone_sectors = blk_queue_zone_sectors(q);
	if (sector & (zone_sectors - 1))
		return -EINVAL;

	if ((nr_sectors & (zone_sectors - 1)) &&
	    end_sector != bdev->bd_part->nr_sects)
		return -EINVAL;

	while (sector < end_sector) {

		bio = bio_alloc(gfp_mask, 0);
		bio->bi_iter.bi_sector = sector;
		bio_set_dev(bio, bdev);
		bio_set_op_attrs(bio, REQ_OP_ZONE_RESET, 0);

		ret = submit_bio_wait(bio);
		bio_put(bio);

		if (ret)
			return ret;

		sector += zone_sectors;

		/* This may take a while, so be nice to others */
		cond_resched();

	}

	return 0;
}
EXPORT_SYMBOL_GPL(blkdev_reset_zones);

/*
 * BLKREPORTZONE ioctl processing.
 * Called from blkdev_ioctl.
 */
int blkdev_report_zones_ioctl(struct block_device *bdev, fmode_t mode,
			      unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct request_queue *q;
	struct blk_zone_report rep;
	struct blk_zone *zones;
	int ret;

	if (!argp)
		return -EINVAL;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	if (!blk_queue_is_zoned(q))
		return -ENOTTY;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (copy_from_user(&rep, argp, sizeof(struct blk_zone_report)))
		return -EFAULT;

	if (!rep.nr_zones)
		return -EINVAL;

	if (rep.nr_zones > INT_MAX / sizeof(struct blk_zone))
		return -ERANGE;

	zones = kvmalloc_array(rep.nr_zones, sizeof(struct blk_zone),
			       GFP_KERNEL | __GFP_ZERO);
	if (!zones)
		return -ENOMEM;

	ret = blkdev_report_zones(bdev, rep.sector,
				  zones, &rep.nr_zones,
				  GFP_KERNEL);
	if (ret)
		goto out;

	if (copy_to_user(argp, &rep, sizeof(struct blk_zone_report))) {
		ret = -EFAULT;
		goto out;
	}

	if (rep.nr_zones) {
		if (copy_to_user(argp + sizeof(struct blk_zone_report), zones,
				 sizeof(struct blk_zone) * rep.nr_zones))
			ret = -EFAULT;
	}

 out:
	kvfree(zones);

	return ret;
}

/*
 * BLKRESETZONE ioctl processing.
 * Called from blkdev_ioctl.
 */
int blkdev_reset_zones_ioctl(struct block_device *bdev, fmode_t mode,
			     unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct request_queue *q;
	struct blk_zone_range zrange;

	if (!argp)
		return -EINVAL;

	q = bdev_get_queue(bdev);
	if (!q)
		return -ENXIO;

	if (!blk_queue_is_zoned(q))
		return -ENOTTY;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!(mode & FMODE_WRITE))
		return -EBADF;

	if (copy_from_user(&zrange, argp, sizeof(struct blk_zone_range)))
		return -EFAULT;

	return blkdev_reset_zones(bdev, zrange.sector, zrange.nr_sectors,
				  GFP_KERNEL);
}
