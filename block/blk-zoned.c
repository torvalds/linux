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
#include <linux/blk-mq.h>

#include "blk.h"

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

static inline unsigned int __blkdev_nr_zones(struct request_queue *q,
					     sector_t nr_sectors)
{
	unsigned long zone_sectors = blk_queue_zone_sectors(q);

	return (nr_sectors + zone_sectors - 1) >> ilog2(zone_sectors);
}

/**
 * blkdev_nr_zones - Get number of zones
 * @bdev:	Target block device
 *
 * Description:
 *    Return the total number of zones of a zoned block device.
 *    For a regular block device, the number of zones is always 0.
 */
unsigned int blkdev_nr_zones(struct block_device *bdev)
{
	struct request_queue *q = bdev_get_queue(bdev);

	if (!blk_queue_is_zoned(q))
		return 0;

	return __blkdev_nr_zones(q, bdev->bd_part->nr_sects);
}
EXPORT_SYMBOL_GPL(blkdev_nr_zones);

/*
 * Check that a zone report belongs to this partition, and if yes, fix its start
 * sector and write pointer and return true. Return false otherwise.
 */
static bool blkdev_report_zone(struct block_device *bdev, struct blk_zone *rep)
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
	return true;
}

static int blk_report_zones(struct gendisk *disk, sector_t sector,
			    struct blk_zone *zones, unsigned int *nr_zones,
			    gfp_t gfp_mask)
{
	struct request_queue *q = disk->queue;
	unsigned int z = 0, n, nrz = *nr_zones;
	sector_t capacity = get_capacity(disk);
	int ret;

	while (z < nrz && sector < capacity) {
		n = nrz - z;
		ret = disk->fops->report_zones(disk, sector, &zones[z], &n,
					       gfp_mask);
		if (ret)
			return ret;
		if (!n)
			break;
		sector += blk_queue_zone_sectors(q) * n;
		z += n;
	}

	WARN_ON(z > *nr_zones);
	*nr_zones = z;

	return 0;
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
int blkdev_report_zones(struct block_device *bdev, sector_t sector,
			struct blk_zone *zones, unsigned int *nr_zones,
			gfp_t gfp_mask)
{
	struct request_queue *q = bdev_get_queue(bdev);
	unsigned int i, nrz;
	int ret;

	if (!blk_queue_is_zoned(q))
		return -EOPNOTSUPP;

	/*
	 * A block device that advertized itself as zoned must have a
	 * report_zones method. If it does not have one defined, the device
	 * driver has a bug. So warn about that.
	 */
	if (WARN_ON_ONCE(!bdev->bd_disk->fops->report_zones))
		return -EOPNOTSUPP;

	if (!*nr_zones || sector >= bdev->bd_part->nr_sects) {
		*nr_zones = 0;
		return 0;
	}

	nrz = min(*nr_zones,
		  __blkdev_nr_zones(q, bdev->bd_part->nr_sects - sector));
	ret = blk_report_zones(bdev->bd_disk, get_start_sect(bdev) + sector,
			       zones, &nrz, gfp_mask);
	if (ret)
		return ret;

	for (i = 0; i < nrz; i++) {
		if (!blkdev_report_zone(bdev, zones))
			break;
		zones++;
	}

	*nr_zones = i;

	return 0;
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
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret;

	if (!blk_queue_is_zoned(q))
		return -EOPNOTSUPP;

	if (bdev_read_only(bdev))
		return -EPERM;

	if (!nr_sectors || end_sector > bdev->bd_part->nr_sects)
		/* Out of range */
		return -EINVAL;

	/* Check alignment (handle eventual smaller last zone) */
	zone_sectors = blk_queue_zone_sectors(q);
	if (sector & (zone_sectors - 1))
		return -EINVAL;

	if ((nr_sectors & (zone_sectors - 1)) &&
	    end_sector != bdev->bd_part->nr_sects)
		return -EINVAL;

	blk_start_plug(&plug);
	while (sector < end_sector) {

		bio = blk_next_bio(bio, 0, gfp_mask);
		bio->bi_iter.bi_sector = sector;
		bio_set_dev(bio, bdev);
		bio_set_op_attrs(bio, REQ_OP_ZONE_RESET, 0);

		sector += zone_sectors;

		/* This may take a while, so be nice to others */
		cond_resched();

	}

	ret = submit_bio_wait(bio);
	bio_put(bio);

	blk_finish_plug(&plug);

	return ret;
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

	rep.nr_zones = min(blkdev_nr_zones(bdev), rep.nr_zones);

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

static inline unsigned long *blk_alloc_zone_bitmap(int node,
						   unsigned int nr_zones)
{
	return kcalloc_node(BITS_TO_LONGS(nr_zones), sizeof(unsigned long),
			    GFP_NOIO, node);
}

/*
 * Allocate an array of struct blk_zone to get nr_zones zone information.
 * The allocated array may be smaller than nr_zones.
 */
static struct blk_zone *blk_alloc_zones(int node, unsigned int *nr_zones)
{
	size_t size = *nr_zones * sizeof(struct blk_zone);
	struct page *page;
	int order;

	for (order = get_order(size); order >= 0; order--) {
		page = alloc_pages_node(node, GFP_NOIO | __GFP_ZERO, order);
		if (page) {
			*nr_zones = min_t(unsigned int, *nr_zones,
				(PAGE_SIZE << order) / sizeof(struct blk_zone));
			return page_address(page);
		}
	}

	return NULL;
}

void blk_queue_free_zone_bitmaps(struct request_queue *q)
{
	kfree(q->seq_zones_bitmap);
	q->seq_zones_bitmap = NULL;
	kfree(q->seq_zones_wlock);
	q->seq_zones_wlock = NULL;
}

/**
 * blk_revalidate_disk_zones - (re)allocate and initialize zone bitmaps
 * @disk:	Target disk
 *
 * Helper function for low-level device drivers to (re) allocate and initialize
 * a disk request queue zone bitmaps. This functions should normally be called
 * within the disk ->revalidate method. For BIO based queues, no zone bitmap
 * is allocated.
 */
int blk_revalidate_disk_zones(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	unsigned int nr_zones = __blkdev_nr_zones(q, get_capacity(disk));
	unsigned long *seq_zones_wlock = NULL, *seq_zones_bitmap = NULL;
	unsigned int i, rep_nr_zones = 0, z = 0, nrz;
	struct blk_zone *zones = NULL;
	sector_t sector = 0;
	int ret = 0;

	/*
	 * BIO based queues do not use a scheduler so only q->nr_zones
	 * needs to be updated so that the sysfs exposed value is correct.
	 */
	if (!queue_is_rq_based(q)) {
		q->nr_zones = nr_zones;
		return 0;
	}

	if (!blk_queue_is_zoned(q) || !nr_zones) {
		nr_zones = 0;
		goto update;
	}

	/* Allocate bitmaps */
	ret = -ENOMEM;
	seq_zones_wlock = blk_alloc_zone_bitmap(q->node, nr_zones);
	if (!seq_zones_wlock)
		goto out;
	seq_zones_bitmap = blk_alloc_zone_bitmap(q->node, nr_zones);
	if (!seq_zones_bitmap)
		goto out;

	/* Get zone information and initialize seq_zones_bitmap */
	rep_nr_zones = nr_zones;
	zones = blk_alloc_zones(q->node, &rep_nr_zones);
	if (!zones)
		goto out;

	while (z < nr_zones) {
		nrz = min(nr_zones - z, rep_nr_zones);
		ret = blk_report_zones(disk, sector, zones, &nrz, GFP_NOIO);
		if (ret)
			goto out;
		if (!nrz)
			break;
		for (i = 0; i < nrz; i++) {
			if (zones[i].type != BLK_ZONE_TYPE_CONVENTIONAL)
				set_bit(z, seq_zones_bitmap);
			z++;
		}
		sector += nrz * blk_queue_zone_sectors(q);
	}

	if (WARN_ON(z != nr_zones)) {
		ret = -EIO;
		goto out;
	}

update:
	/*
	 * Install the new bitmaps, making sure the queue is stopped and
	 * all I/Os are completed (i.e. a scheduler is not referencing the
	 * bitmaps).
	 */
	blk_mq_freeze_queue(q);
	q->nr_zones = nr_zones;
	swap(q->seq_zones_wlock, seq_zones_wlock);
	swap(q->seq_zones_bitmap, seq_zones_bitmap);
	blk_mq_unfreeze_queue(q);

out:
	free_pages((unsigned long)zones,
		   get_order(rep_nr_zones * sizeof(struct blk_zone)));
	kfree(seq_zones_wlock);
	kfree(seq_zones_bitmap);

	if (ret) {
		pr_warn("%s: failed to revalidate zones\n", disk->disk_name);
		blk_mq_freeze_queue(q);
		blk_queue_free_zone_bitmaps(q);
		blk_mq_unfreeze_queue(q);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(blk_revalidate_disk_zones);

