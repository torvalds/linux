// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>

#include "dm-core.h"

#define DM_MSG_PREFIX "zone"

#define DM_ZONE_INVALID_WP_OFST		UINT_MAX

/*
 * For internal zone reports bypassing the top BIO submission path.
 */
static int dm_blk_do_report_zones(struct mapped_device *md, struct dm_table *t,
				  sector_t sector, unsigned int nr_zones,
				  report_zones_cb cb, void *data)
{
	struct gendisk *disk = md->disk;
	int ret;
	struct dm_report_zones_args args = {
		.next_sector = sector,
		.orig_data = data,
		.orig_cb = cb,
	};

	do {
		struct dm_target *tgt;

		tgt = dm_table_find_target(t, args.next_sector);
		if (WARN_ON_ONCE(!tgt->type->report_zones))
			return -EIO;

		args.tgt = tgt;
		ret = tgt->type->report_zones(tgt, &args,
					      nr_zones - args.zone_idx);
		if (ret < 0)
			return ret;
	} while (args.zone_idx < nr_zones &&
		 args.next_sector < get_capacity(disk));

	return args.zone_idx;
}

/*
 * User facing dm device block device report zone operation. This calls the
 * report_zones operation for each target of a device table. This operation is
 * generally implemented by targets using dm_report_zones().
 */
int dm_blk_report_zones(struct gendisk *disk, sector_t sector,
			unsigned int nr_zones, report_zones_cb cb, void *data)
{
	struct mapped_device *md = disk->private_data;
	struct dm_table *map;
	int srcu_idx, ret;

	if (dm_suspended_md(md))
		return -EAGAIN;

	map = dm_get_live_table(md, &srcu_idx);
	if (!map)
		return -EIO;

	ret = dm_blk_do_report_zones(md, map, sector, nr_zones, cb, data);

	dm_put_live_table(md, srcu_idx);

	return ret;
}

static int dm_report_zones_cb(struct blk_zone *zone, unsigned int idx,
			      void *data)
{
	struct dm_report_zones_args *args = data;
	sector_t sector_diff = args->tgt->begin - args->start;

	/*
	 * Ignore zones beyond the target range.
	 */
	if (zone->start >= args->start + args->tgt->len)
		return 0;

	/*
	 * Remap the start sector and write pointer position of the zone
	 * to match its position in the target range.
	 */
	zone->start += sector_diff;
	if (zone->type != BLK_ZONE_TYPE_CONVENTIONAL) {
		if (zone->cond == BLK_ZONE_COND_FULL)
			zone->wp = zone->start + zone->len;
		else if (zone->cond == BLK_ZONE_COND_EMPTY)
			zone->wp = zone->start;
		else
			zone->wp += sector_diff;
	}

	args->next_sector = zone->start + zone->len;
	return args->orig_cb(zone, args->zone_idx++, args->orig_data);
}

/*
 * Helper for drivers of zoned targets to implement struct target_type
 * report_zones operation.
 */
int dm_report_zones(struct block_device *bdev, sector_t start, sector_t sector,
		    struct dm_report_zones_args *args, unsigned int nr_zones)
{
	/*
	 * Set the target mapping start sector first so that
	 * dm_report_zones_cb() can correctly remap zone information.
	 */
	args->start = start;

	return blkdev_report_zones(bdev, sector, nr_zones,
				   dm_report_zones_cb, args);
}
EXPORT_SYMBOL_GPL(dm_report_zones);

bool dm_is_zone_write(struct mapped_device *md, struct bio *bio)
{
	struct request_queue *q = md->queue;

	if (!blk_queue_is_zoned(q))
		return false;

	switch (bio_op(bio)) {
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE:
		return !op_is_flush(bio->bi_opf) && bio_sectors(bio);
	default:
		return false;
	}
}

void dm_cleanup_zoned_dev(struct mapped_device *md)
{
	if (md->disk) {
		kfree(md->disk->conv_zones_bitmap);
		md->disk->conv_zones_bitmap = NULL;
		kfree(md->disk->seq_zones_wlock);
		md->disk->seq_zones_wlock = NULL;
	}

	kvfree(md->zwp_offset);
	md->zwp_offset = NULL;
	md->nr_zones = 0;
}

static unsigned int dm_get_zone_wp_offset(struct blk_zone *zone)
{
	switch (zone->cond) {
	case BLK_ZONE_COND_IMP_OPEN:
	case BLK_ZONE_COND_EXP_OPEN:
	case BLK_ZONE_COND_CLOSED:
		return zone->wp - zone->start;
	case BLK_ZONE_COND_FULL:
		return zone->len;
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_NOT_WP:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
	default:
		/*
		 * Conventional, offline and read-only zones do not have a valid
		 * write pointer. Use 0 as for an empty zone.
		 */
		return 0;
	}
}

static int dm_zone_revalidate_cb(struct blk_zone *zone, unsigned int idx,
				 void *data)
{
	struct mapped_device *md = data;
	struct gendisk *disk = md->disk;

	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		if (!disk->conv_zones_bitmap) {
			disk->conv_zones_bitmap =
				kcalloc(BITS_TO_LONGS(disk->nr_zones),
					sizeof(unsigned long), GFP_NOIO);
			if (!disk->conv_zones_bitmap)
				return -ENOMEM;
		}
		set_bit(idx, disk->conv_zones_bitmap);
		break;
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		if (!disk->seq_zones_wlock) {
			disk->seq_zones_wlock =
				kcalloc(BITS_TO_LONGS(disk->nr_zones),
					sizeof(unsigned long), GFP_NOIO);
			if (!disk->seq_zones_wlock)
				return -ENOMEM;
		}
		if (!md->zwp_offset) {
			md->zwp_offset =
				kvcalloc(disk->nr_zones, sizeof(unsigned int),
					 GFP_KERNEL);
			if (!md->zwp_offset)
				return -ENOMEM;
		}
		md->zwp_offset[idx] = dm_get_zone_wp_offset(zone);

		break;
	default:
		DMERR("Invalid zone type 0x%x at sectors %llu",
		      (int)zone->type, zone->start);
		return -ENODEV;
	}

	return 0;
}

/*
 * Revalidate the zones of a mapped device to initialize resource necessary
 * for zone append emulation. Note that we cannot simply use the block layer
 * blk_revalidate_disk_zones() function here as the mapped device is suspended
 * (this is called from __bind() context).
 */
static int dm_revalidate_zones(struct mapped_device *md, struct dm_table *t)
{
	struct gendisk *disk = md->disk;
	unsigned int noio_flag;
	int ret;

	/*
	 * Check if something changed. If yes, cleanup the current resources
	 * and reallocate everything.
	 */
	if (!disk->nr_zones || disk->nr_zones != md->nr_zones)
		dm_cleanup_zoned_dev(md);
	if (md->nr_zones)
		return 0;

	/*
	 * Scan all zones to initialize everything. Ensure that all vmalloc
	 * operations in this context are done as if GFP_NOIO was specified.
	 */
	noio_flag = memalloc_noio_save();
	ret = dm_blk_do_report_zones(md, t, 0, disk->nr_zones,
				     dm_zone_revalidate_cb, md);
	memalloc_noio_restore(noio_flag);
	if (ret < 0)
		goto err;
	if (ret != disk->nr_zones) {
		ret = -EIO;
		goto err;
	}

	md->nr_zones = disk->nr_zones;

	return 0;

err:
	DMERR("Revalidate zones failed %d", ret);
	dm_cleanup_zoned_dev(md);
	return ret;
}

static int device_not_zone_append_capable(struct dm_target *ti,
					  struct dm_dev *dev, sector_t start,
					  sector_t len, void *data)
{
	return !bdev_is_zoned(dev->bdev);
}

static bool dm_table_supports_zone_append(struct dm_table *t)
{
	for (unsigned int i = 0; i < t->num_targets; i++) {
		struct dm_target *ti = dm_table_get_target(t, i);

		if (ti->emulate_zone_append)
			return false;

		if (!ti->type->iterate_devices ||
		    ti->type->iterate_devices(ti, device_not_zone_append_capable, NULL))
			return false;
	}

	return true;
}

int dm_set_zones_restrictions(struct dm_table *t, struct request_queue *q)
{
	struct mapped_device *md = t->md;

	/*
	 * For a zoned target, the number of zones should be updated for the
	 * correct value to be exposed in sysfs queue/nr_zones.
	 */
	WARN_ON_ONCE(queue_is_mq(q));
	md->disk->nr_zones = bdev_nr_zones(md->disk->part0);

	/* Check if zone append is natively supported */
	if (dm_table_supports_zone_append(t)) {
		clear_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
		dm_cleanup_zoned_dev(md);
		return 0;
	}

	/*
	 * Mark the mapped device as needing zone append emulation and
	 * initialize the emulation resources once the capacity is set.
	 */
	set_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
	if (!get_capacity(md->disk))
		return 0;

	return dm_revalidate_zones(md, t);
}

static int dm_update_zone_wp_offset_cb(struct blk_zone *zone, unsigned int idx,
				       void *data)
{
	unsigned int *wp_offset = data;

	*wp_offset = dm_get_zone_wp_offset(zone);

	return 0;
}

static int dm_update_zone_wp_offset(struct mapped_device *md, unsigned int zno,
				    unsigned int *wp_ofst)
{
	sector_t sector = zno * bdev_zone_sectors(md->disk->part0);
	unsigned int noio_flag;
	struct dm_table *t;
	int srcu_idx, ret;

	t = dm_get_live_table(md, &srcu_idx);
	if (!t)
		return -EIO;

	/*
	 * Ensure that all memory allocations in this context are done as if
	 * GFP_NOIO was specified.
	 */
	noio_flag = memalloc_noio_save();
	ret = dm_blk_do_report_zones(md, t, sector, 1,
				     dm_update_zone_wp_offset_cb, wp_ofst);
	memalloc_noio_restore(noio_flag);

	dm_put_live_table(md, srcu_idx);

	if (ret != 1)
		return -EIO;

	return 0;
}

struct orig_bio_details {
	enum req_op op;
	unsigned int nr_sectors;
};

/*
 * First phase of BIO mapping for targets with zone append emulation:
 * check all BIO that change a zone writer pointer and change zone
 * append operations into regular write operations.
 */
static bool dm_zone_map_bio_begin(struct mapped_device *md,
				  unsigned int zno, struct bio *clone)
{
	sector_t zsectors = bdev_zone_sectors(md->disk->part0);
	unsigned int zwp_offset = READ_ONCE(md->zwp_offset[zno]);

	/*
	 * If the target zone is in an error state, recover by inspecting the
	 * zone to get its current write pointer position. Note that since the
	 * target zone is already locked, a BIO issuing context should never
	 * see the zone write in the DM_ZONE_UPDATING_WP_OFST state.
	 */
	if (zwp_offset == DM_ZONE_INVALID_WP_OFST) {
		if (dm_update_zone_wp_offset(md, zno, &zwp_offset))
			return false;
		WRITE_ONCE(md->zwp_offset[zno], zwp_offset);
	}

	switch (bio_op(clone)) {
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_FINISH:
		return true;
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE:
		/* Writes must be aligned to the zone write pointer */
		if ((clone->bi_iter.bi_sector & (zsectors - 1)) != zwp_offset)
			return false;
		break;
	case REQ_OP_ZONE_APPEND:
		/*
		 * Change zone append operations into a non-mergeable regular
		 * writes directed at the current write pointer position of the
		 * target zone.
		 */
		clone->bi_opf = REQ_OP_WRITE | REQ_NOMERGE |
			(clone->bi_opf & (~REQ_OP_MASK));
		clone->bi_iter.bi_sector += zwp_offset;
		break;
	default:
		DMWARN_LIMIT("Invalid BIO operation");
		return false;
	}

	/* Cannot write to a full zone */
	if (zwp_offset >= zsectors)
		return false;

	return true;
}

/*
 * Second phase of BIO mapping for targets with zone append emulation:
 * update the zone write pointer offset array to account for the additional
 * data written to a zone. Note that at this point, the remapped clone BIO
 * may already have completed, so we do not touch it.
 */
static blk_status_t dm_zone_map_bio_end(struct mapped_device *md, unsigned int zno,
					struct orig_bio_details *orig_bio_details,
					unsigned int nr_sectors)
{
	unsigned int zwp_offset = READ_ONCE(md->zwp_offset[zno]);

	/* The clone BIO may already have been completed and failed */
	if (zwp_offset == DM_ZONE_INVALID_WP_OFST)
		return BLK_STS_IOERR;

	/* Update the zone wp offset */
	switch (orig_bio_details->op) {
	case REQ_OP_ZONE_RESET:
		WRITE_ONCE(md->zwp_offset[zno], 0);
		return BLK_STS_OK;
	case REQ_OP_ZONE_FINISH:
		WRITE_ONCE(md->zwp_offset[zno],
			   bdev_zone_sectors(md->disk->part0));
		return BLK_STS_OK;
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE:
		WRITE_ONCE(md->zwp_offset[zno], zwp_offset + nr_sectors);
		return BLK_STS_OK;
	case REQ_OP_ZONE_APPEND:
		/*
		 * Check that the target did not truncate the write operation
		 * emulating a zone append.
		 */
		if (nr_sectors != orig_bio_details->nr_sectors) {
			DMWARN_LIMIT("Truncated write for zone append");
			return BLK_STS_IOERR;
		}
		WRITE_ONCE(md->zwp_offset[zno], zwp_offset + nr_sectors);
		return BLK_STS_OK;
	default:
		DMWARN_LIMIT("Invalid BIO operation");
		return BLK_STS_IOERR;
	}
}

static inline void dm_zone_lock(struct gendisk *disk, unsigned int zno,
				struct bio *clone)
{
	if (WARN_ON_ONCE(bio_flagged(clone, BIO_ZONE_WRITE_LOCKED)))
		return;

	wait_on_bit_lock_io(disk->seq_zones_wlock, zno, TASK_UNINTERRUPTIBLE);
	bio_set_flag(clone, BIO_ZONE_WRITE_LOCKED);
}

static inline void dm_zone_unlock(struct gendisk *disk, unsigned int zno,
				  struct bio *clone)
{
	if (!bio_flagged(clone, BIO_ZONE_WRITE_LOCKED))
		return;

	WARN_ON_ONCE(!test_bit(zno, disk->seq_zones_wlock));
	clear_bit_unlock(zno, disk->seq_zones_wlock);
	smp_mb__after_atomic();
	wake_up_bit(disk->seq_zones_wlock, zno);

	bio_clear_flag(clone, BIO_ZONE_WRITE_LOCKED);
}

static bool dm_need_zone_wp_tracking(struct bio *bio)
{
	/*
	 * Special processing is not needed for operations that do not need the
	 * zone write lock, that is, all operations that target conventional
	 * zones and all operations that do not modify directly a sequential
	 * zone write pointer.
	 */
	if (op_is_flush(bio->bi_opf) && !bio_sectors(bio))
		return false;
	switch (bio_op(bio)) {
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE:
	case REQ_OP_ZONE_RESET:
	case REQ_OP_ZONE_FINISH:
	case REQ_OP_ZONE_APPEND:
		return bio_zone_is_seq(bio);
	default:
		return false;
	}
}

/*
 * Special IO mapping for targets needing zone append emulation.
 */
int dm_zone_map_bio(struct dm_target_io *tio)
{
	struct dm_io *io = tio->io;
	struct dm_target *ti = tio->ti;
	struct mapped_device *md = io->md;
	struct bio *clone = &tio->clone;
	struct orig_bio_details orig_bio_details;
	unsigned int zno;
	blk_status_t sts;
	int r;

	/*
	 * IOs that do not change a zone write pointer do not need
	 * any additional special processing.
	 */
	if (!dm_need_zone_wp_tracking(clone))
		return ti->type->map(ti, clone);

	/* Lock the target zone */
	zno = bio_zone_no(clone);
	dm_zone_lock(md->disk, zno, clone);

	orig_bio_details.nr_sectors = bio_sectors(clone);
	orig_bio_details.op = bio_op(clone);

	/*
	 * Check that the bio and the target zone write pointer offset are
	 * both valid, and if the bio is a zone append, remap it to a write.
	 */
	if (!dm_zone_map_bio_begin(md, zno, clone)) {
		dm_zone_unlock(md->disk, zno, clone);
		return DM_MAPIO_KILL;
	}

	/* Let the target do its work */
	r = ti->type->map(ti, clone);
	switch (r) {
	case DM_MAPIO_SUBMITTED:
		/*
		 * The target submitted the clone BIO. The target zone will
		 * be unlocked on completion of the clone.
		 */
		sts = dm_zone_map_bio_end(md, zno, &orig_bio_details,
					  *tio->len_ptr);
		break;
	case DM_MAPIO_REMAPPED:
		/*
		 * The target only remapped the clone BIO. In case of error,
		 * unlock the target zone here as the clone will not be
		 * submitted.
		 */
		sts = dm_zone_map_bio_end(md, zno, &orig_bio_details,
					  *tio->len_ptr);
		if (sts != BLK_STS_OK)
			dm_zone_unlock(md->disk, zno, clone);
		break;
	case DM_MAPIO_REQUEUE:
	case DM_MAPIO_KILL:
	default:
		dm_zone_unlock(md->disk, zno, clone);
		sts = BLK_STS_IOERR;
		break;
	}

	if (sts != BLK_STS_OK)
		return DM_MAPIO_KILL;

	return r;
}

/*
 * IO completion callback called from clone_endio().
 */
void dm_zone_endio(struct dm_io *io, struct bio *clone)
{
	struct mapped_device *md = io->md;
	struct gendisk *disk = md->disk;
	struct bio *orig_bio = io->orig_bio;
	unsigned int zwp_offset;
	unsigned int zno;

	/*
	 * For targets that do not emulate zone append, we only need to
	 * handle native zone-append bios.
	 */
	if (!dm_emulate_zone_append(md)) {
		/*
		 * Get the offset within the zone of the written sector
		 * and add that to the original bio sector position.
		 */
		if (clone->bi_status == BLK_STS_OK &&
		    bio_op(clone) == REQ_OP_ZONE_APPEND) {
			sector_t mask =
				(sector_t)bdev_zone_sectors(disk->part0) - 1;

			orig_bio->bi_iter.bi_sector +=
				clone->bi_iter.bi_sector & mask;
		}

		return;
	}

	/*
	 * For targets that do emulate zone append, if the clone BIO does not
	 * own the target zone write lock, we have nothing to do.
	 */
	if (!bio_flagged(clone, BIO_ZONE_WRITE_LOCKED))
		return;

	zno = bio_zone_no(orig_bio);

	if (clone->bi_status != BLK_STS_OK) {
		/*
		 * BIOs that modify a zone write pointer may leave the zone
		 * in an unknown state in case of failure (e.g. the write
		 * pointer was only partially advanced). In this case, set
		 * the target zone write pointer as invalid unless it is
		 * already being updated.
		 */
		WRITE_ONCE(md->zwp_offset[zno], DM_ZONE_INVALID_WP_OFST);
	} else if (bio_op(orig_bio) == REQ_OP_ZONE_APPEND) {
		/*
		 * Get the written sector for zone append operation that were
		 * emulated using regular write operations.
		 */
		zwp_offset = READ_ONCE(md->zwp_offset[zno]);
		if (WARN_ON_ONCE(zwp_offset < bio_sectors(orig_bio)))
			WRITE_ONCE(md->zwp_offset[zno],
				   DM_ZONE_INVALID_WP_OFST);
		else
			orig_bio->bi_iter.bi_sector +=
				zwp_offset - bio_sectors(orig_bio);
	}

	dm_zone_unlock(disk, zno, clone);
}
