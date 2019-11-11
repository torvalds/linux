// SPDX-License-Identifier: GPL-2.0
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
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/sched/mm.h>

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
	sector_t zone_sectors = blk_queue_zone_sectors(q);

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

/**
 * blkdev_report_zones - Get zones information
 * @bdev:	Target block device
 * @sector:	Sector from which to report zones
 * @zones:	Array of zone structures where to return the zones information
 * @nr_zones:	Number of zone structures in the zone array
 *
 * Description:
 *    Get zone information starting from the zone containing @sector.
 *    The number of zone information reported may be less than the number
 *    requested by @nr_zones. The number of zones actually reported is
 *    returned in @nr_zones.
 *    The caller must use memalloc_noXX_save/restore() calls to control
 *    memory allocations done within this function (zone array and command
 *    buffer allocation by the device driver).
 */
int blkdev_report_zones(struct block_device *bdev, sector_t sector,
			struct blk_zone *zones, unsigned int *nr_zones)
{
	struct request_queue *q = bdev_get_queue(bdev);
	struct gendisk *disk = bdev->bd_disk;
	unsigned int i, nrz;
	int ret;

	if (!blk_queue_is_zoned(q))
		return -EOPNOTSUPP;

	/*
	 * A block device that advertized itself as zoned must have a
	 * report_zones method. If it does not have one defined, the device
	 * driver has a bug. So warn about that.
	 */
	if (WARN_ON_ONCE(!disk->fops->report_zones))
		return -EOPNOTSUPP;

	if (!*nr_zones || sector >= bdev->bd_part->nr_sects) {
		*nr_zones = 0;
		return 0;
	}

	nrz = min(*nr_zones,
		  __blkdev_nr_zones(q, bdev->bd_part->nr_sects - sector));
	ret = disk->fops->report_zones(disk, get_start_sect(bdev) + sector,
				       zones, &nrz);
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

static inline bool blkdev_allow_reset_all_zones(struct block_device *bdev,
						sector_t sector,
						sector_t nr_sectors)
{
	if (!blk_queue_zone_resetall(bdev_get_queue(bdev)))
		return false;

	if (sector || nr_sectors != part_nr_sects_read(bdev->bd_part))
		return false;
	/*
	 * REQ_OP_ZONE_RESET_ALL can be executed only if the block device is
	 * the entire disk, that is, if the blocks device start offset is 0 and
	 * its capacity is the same as the entire disk.
	 */
	return get_start_sect(bdev) == 0 &&
	       part_nr_sects_read(bdev->bd_part) == get_capacity(bdev->bd_disk);
}

/**
 * blkdev_zone_mgmt - Execute a zone management operation on a range of zones
 * @bdev:	Target block device
 * @op:		Operation to be performed on the zones
 * @sector:	Start sector of the first zone to operate on
 * @nr_sectors:	Number of sectors, should be at least the length of one zone and
 *		must be zone size aligned.
 * @gfp_mask:	Memory allocation flags (for bio_alloc)
 *
 * Description:
 *    Perform the specified operation on the range of zones specified by
 *    @sector..@sector+@nr_sectors. Specifying the entire disk sector range
 *    is valid, but the specified range should not contain conventional zones.
 *    The operation to execute on each zone can be a zone reset, open, close
 *    or finish request.
 */
int blkdev_zone_mgmt(struct block_device *bdev, enum req_opf op,
		     sector_t sector, sector_t nr_sectors,
		     gfp_t gfp_mask)
{
	struct request_queue *q = bdev_get_queue(bdev);
	sector_t zone_sectors = blk_queue_zone_sectors(q);
	sector_t end_sector = sector + nr_sectors;
	struct bio *bio = NULL;
	int ret;

	if (!blk_queue_is_zoned(q))
		return -EOPNOTSUPP;

	if (bdev_read_only(bdev))
		return -EPERM;

	if (!op_is_zone_mgmt(op))
		return -EOPNOTSUPP;

	if (!nr_sectors || end_sector > bdev->bd_part->nr_sects)
		/* Out of range */
		return -EINVAL;

	/* Check alignment (handle eventual smaller last zone) */
	if (sector & (zone_sectors - 1))
		return -EINVAL;

	if ((nr_sectors & (zone_sectors - 1)) &&
	    end_sector != bdev->bd_part->nr_sects)
		return -EINVAL;

	while (sector < end_sector) {
		bio = blk_next_bio(bio, 0, gfp_mask);
		bio_set_dev(bio, bdev);

		/*
		 * Special case for the zone reset operation that reset all
		 * zones, this is useful for applications like mkfs.
		 */
		if (op == REQ_OP_ZONE_RESET &&
		    blkdev_allow_reset_all_zones(bdev, sector, nr_sectors)) {
			bio->bi_opf = REQ_OP_ZONE_RESET_ALL;
			break;
		}

		bio->bi_opf = op;
		bio->bi_iter.bi_sector = sector;
		sector += zone_sectors;

		/* This may take a while, so be nice to others */
		cond_resched();
	}

	ret = submit_bio_wait(bio);
	bio_put(bio);

	return ret;
}
EXPORT_SYMBOL_GPL(blkdev_zone_mgmt);

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

	ret = blkdev_report_zones(bdev, rep.sector, zones, &rep.nr_zones);
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
 * BLKRESETZONE, BLKOPENZONE, BLKCLOSEZONE and BLKFINISHZONE ioctl processing.
 * Called from blkdev_ioctl.
 */
int blkdev_zone_mgmt_ioctl(struct block_device *bdev, fmode_t mode,
			   unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct request_queue *q;
	struct blk_zone_range zrange;
	enum req_opf op;

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

	switch (cmd) {
	case BLKRESETZONE:
		op = REQ_OP_ZONE_RESET;
		break;
	case BLKOPENZONE:
		op = REQ_OP_ZONE_OPEN;
		break;
	case BLKCLOSEZONE:
		op = REQ_OP_ZONE_CLOSE;
		break;
	case BLKFINISHZONE:
		op = REQ_OP_ZONE_FINISH;
		break;
	default:
		return -ENOTTY;
	}

	return blkdev_zone_mgmt(bdev, op, zrange.sector, zrange.nr_sectors,
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
static struct blk_zone *blk_alloc_zones(unsigned int *nr_zones)
{
	struct blk_zone *zones;
	size_t nrz = min(*nr_zones, BLK_ZONED_REPORT_MAX_ZONES);

	/*
	 * GFP_KERNEL here is meaningless as the caller task context has
	 * the PF_MEMALLOC_NOIO flag set in blk_revalidate_disk_zones()
	 * with memalloc_noio_save().
	 */
	zones = kvcalloc(nrz, sizeof(struct blk_zone), GFP_KERNEL);
	if (!zones) {
		*nr_zones = 0;
		return NULL;
	}

	*nr_zones = nrz;

	return zones;
}

void blk_queue_free_zone_bitmaps(struct request_queue *q)
{
	kfree(q->seq_zones_bitmap);
	q->seq_zones_bitmap = NULL;
	kfree(q->seq_zones_wlock);
	q->seq_zones_wlock = NULL;
}

/*
 * Helper function to check the validity of zones of a zoned block device.
 */
static bool blk_zone_valid(struct gendisk *disk, struct blk_zone *zone,
			   sector_t *sector)
{
	struct request_queue *q = disk->queue;
	sector_t zone_sectors = blk_queue_zone_sectors(q);
	sector_t capacity = get_capacity(disk);

	/*
	 * All zones must have the same size, with the exception on an eventual
	 * smaller last zone.
	 */
	if (zone->start + zone_sectors < capacity &&
	    zone->len != zone_sectors) {
		pr_warn("%s: Invalid zoned device with non constant zone size\n",
			disk->disk_name);
		return false;
	}

	if (zone->start + zone->len >= capacity &&
	    zone->len > zone_sectors) {
		pr_warn("%s: Invalid zoned device with larger last zone size\n",
			disk->disk_name);
		return false;
	}

	/* Check for holes in the zone report */
	if (zone->start != *sector) {
		pr_warn("%s: Zone gap at sectors %llu..%llu\n",
			disk->disk_name, *sector, zone->start);
		return false;
	}

	/* Check zone type */
	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		break;
	default:
		pr_warn("%s: Invalid zone type 0x%x at sectors %llu\n",
			disk->disk_name, (int)zone->type, zone->start);
		return false;
	}

	*sector += zone->len;

	return true;
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
	unsigned int noio_flag;
	sector_t sector = 0;
	int ret = 0;

	if (WARN_ON_ONCE(!blk_queue_is_zoned(q)))
		return -EIO;

	/*
	 * BIO based queues do not use a scheduler so only q->nr_zones
	 * needs to be updated so that the sysfs exposed value is correct.
	 */
	if (!queue_is_mq(q)) {
		q->nr_zones = nr_zones;
		return 0;
	}

	/*
	 * Ensure that all memory allocations in this context are done as
	 * if GFP_NOIO was specified.
	 */
	noio_flag = memalloc_noio_save();

	if (!nr_zones)
		goto update;

	/* Allocate bitmaps */
	ret = -ENOMEM;
	seq_zones_wlock = blk_alloc_zone_bitmap(q->node, nr_zones);
	if (!seq_zones_wlock)
		goto out;
	seq_zones_bitmap = blk_alloc_zone_bitmap(q->node, nr_zones);
	if (!seq_zones_bitmap)
		goto out;

	/*
	 * Get zone information to check the zones and initialize
	 * seq_zones_bitmap.
	 */
	rep_nr_zones = nr_zones;
	zones = blk_alloc_zones(&rep_nr_zones);
	if (!zones)
		goto out;

	while (z < nr_zones) {
		nrz = min(nr_zones - z, rep_nr_zones);
		ret = disk->fops->report_zones(disk, sector, zones, &nrz);
		if (ret)
			goto out;
		if (!nrz)
			break;
		for (i = 0; i < nrz; i++) {
			if (!blk_zone_valid(disk, &zones[i], &sector)) {
				ret = -ENODEV;
				goto out;
			}
			if (zones[i].type != BLK_ZONE_TYPE_CONVENTIONAL)
				set_bit(z, seq_zones_bitmap);
			z++;
		}
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
	memalloc_noio_restore(noio_flag);

	kvfree(zones);
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

