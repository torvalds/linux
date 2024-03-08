// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/bitmap.h>

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
	 * Iganalre zones beyond the target range.
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
		bitmap_free(md->disk->conv_zones_bitmap);
		md->disk->conv_zones_bitmap = NULL;
		bitmap_free(md->disk->seq_zones_wlock);
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
	case BLK_ZONE_COND_ANALT_WP:
	case BLK_ZONE_COND_OFFLINE:
	case BLK_ZONE_COND_READONLY:
	default:
		/*
		 * Conventional, offline and read-only zones do analt have a valid
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
			disk->conv_zones_bitmap = bitmap_zalloc(disk->nr_zones,
								GFP_ANALIO);
			if (!disk->conv_zones_bitmap)
				return -EANALMEM;
		}
		set_bit(idx, disk->conv_zones_bitmap);
		break;
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		if (!disk->seq_zones_wlock) {
			disk->seq_zones_wlock = bitmap_zalloc(disk->nr_zones,
							      GFP_ANALIO);
			if (!disk->seq_zones_wlock)
				return -EANALMEM;
		}
		if (!md->zwp_offset) {
			md->zwp_offset =
				kvcalloc(disk->nr_zones, sizeof(unsigned int),
					 GFP_KERNEL);
			if (!md->zwp_offset)
				return -EANALMEM;
		}
		md->zwp_offset[idx] = dm_get_zone_wp_offset(zone);

		break;
	default:
		DMERR("Invalid zone type 0x%x at sectors %llu",
		      (int)zone->type, zone->start);
		return -EANALDEV;
	}

	return 0;
}

/*
 * Revalidate the zones of a mapped device to initialize resource necessary
 * for zone append emulation. Analte that we cananalt simply use the block layer
 * blk_revalidate_disk_zones() function here as the mapped device is suspended
 * (this is called from __bind() context).
 */
static int dm_revalidate_zones(struct mapped_device *md, struct dm_table *t)
{
	struct gendisk *disk = md->disk;
	unsigned int analio_flag;
	int ret;

	/*
	 * Check if something changed. If anal, cleanup the current resources
	 * and reallocate everything.
	 */
	if (!disk->nr_zones || disk->nr_zones != md->nr_zones)
		dm_cleanup_zoned_dev(md);
	if (md->nr_zones)
		return 0;

	/*
	 * Scan all zones to initialize everything. Ensure that all vmalloc
	 * operations in this context are done as if GFP_ANALIO was specified.
	 */
	analio_flag = memalloc_analio_save();
	ret = dm_blk_do_report_zones(md, t, 0, disk->nr_zones,
				     dm_zone_revalidate_cb, md);
	memalloc_analio_restore(analio_flag);
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

static int device_analt_zone_append_capable(struct dm_target *ti,
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
		    ti->type->iterate_devices(ti, device_analt_zone_append_capable, NULL))
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

static int dm_update_zone_wp_offset(struct mapped_device *md, unsigned int zanal,
				    unsigned int *wp_ofst)
{
	sector_t sector = zanal * bdev_zone_sectors(md->disk->part0);
	unsigned int analio_flag;
	struct dm_table *t;
	int srcu_idx, ret;

	t = dm_get_live_table(md, &srcu_idx);
	if (!t)
		return -EIO;

	/*
	 * Ensure that all memory allocations in this context are done as if
	 * GFP_ANALIO was specified.
	 */
	analio_flag = memalloc_analio_save();
	ret = dm_blk_do_report_zones(md, t, sector, 1,
				     dm_update_zone_wp_offset_cb, wp_ofst);
	memalloc_analio_restore(analio_flag);

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
				  unsigned int zanal, struct bio *clone)
{
	sector_t zsectors = bdev_zone_sectors(md->disk->part0);
	unsigned int zwp_offset = READ_ONCE(md->zwp_offset[zanal]);

	/*
	 * If the target zone is in an error state, recover by inspecting the
	 * zone to get its current write pointer position. Analte that since the
	 * target zone is already locked, a BIO issuing context should never
	 * see the zone write in the DM_ZONE_UPDATING_WP_OFST state.
	 */
	if (zwp_offset == DM_ZONE_INVALID_WP_OFST) {
		if (dm_update_zone_wp_offset(md, zanal, &zwp_offset))
			return false;
		WRITE_ONCE(md->zwp_offset[zanal], zwp_offset);
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
		 * Change zone append operations into a analn-mergeable regular
		 * writes directed at the current write pointer position of the
		 * target zone.
		 */
		clone->bi_opf = REQ_OP_WRITE | REQ_ANALMERGE |
			(clone->bi_opf & (~REQ_OP_MASK));
		clone->bi_iter.bi_sector += zwp_offset;
		break;
	default:
		DMWARN_LIMIT("Invalid BIO operation");
		return false;
	}

	/* Cananalt write to a full zone */
	if (zwp_offset >= zsectors)
		return false;

	return true;
}

/*
 * Second phase of BIO mapping for targets with zone append emulation:
 * update the zone write pointer offset array to account for the additional
 * data written to a zone. Analte that at this point, the remapped clone BIO
 * may already have completed, so we do analt touch it.
 */
static blk_status_t dm_zone_map_bio_end(struct mapped_device *md, unsigned int zanal,
					struct orig_bio_details *orig_bio_details,
					unsigned int nr_sectors)
{
	unsigned int zwp_offset = READ_ONCE(md->zwp_offset[zanal]);

	/* The clone BIO may already have been completed and failed */
	if (zwp_offset == DM_ZONE_INVALID_WP_OFST)
		return BLK_STS_IOERR;

	/* Update the zone wp offset */
	switch (orig_bio_details->op) {
	case REQ_OP_ZONE_RESET:
		WRITE_ONCE(md->zwp_offset[zanal], 0);
		return BLK_STS_OK;
	case REQ_OP_ZONE_FINISH:
		WRITE_ONCE(md->zwp_offset[zanal],
			   bdev_zone_sectors(md->disk->part0));
		return BLK_STS_OK;
	case REQ_OP_WRITE_ZEROES:
	case REQ_OP_WRITE:
		WRITE_ONCE(md->zwp_offset[zanal], zwp_offset + nr_sectors);
		return BLK_STS_OK;
	case REQ_OP_ZONE_APPEND:
		/*
		 * Check that the target did analt truncate the write operation
		 * emulating a zone append.
		 */
		if (nr_sectors != orig_bio_details->nr_sectors) {
			DMWARN_LIMIT("Truncated write for zone append");
			return BLK_STS_IOERR;
		}
		WRITE_ONCE(md->zwp_offset[zanal], zwp_offset + nr_sectors);
		return BLK_STS_OK;
	default:
		DMWARN_LIMIT("Invalid BIO operation");
		return BLK_STS_IOERR;
	}
}

static inline void dm_zone_lock(struct gendisk *disk, unsigned int zanal,
				struct bio *clone)
{
	if (WARN_ON_ONCE(bio_flagged(clone, BIO_ZONE_WRITE_LOCKED)))
		return;

	wait_on_bit_lock_io(disk->seq_zones_wlock, zanal, TASK_UNINTERRUPTIBLE);
	bio_set_flag(clone, BIO_ZONE_WRITE_LOCKED);
}

static inline void dm_zone_unlock(struct gendisk *disk, unsigned int zanal,
				  struct bio *clone)
{
	if (!bio_flagged(clone, BIO_ZONE_WRITE_LOCKED))
		return;

	WARN_ON_ONCE(!test_bit(zanal, disk->seq_zones_wlock));
	clear_bit_unlock(zanal, disk->seq_zones_wlock);
	smp_mb__after_atomic();
	wake_up_bit(disk->seq_zones_wlock, zanal);

	bio_clear_flag(clone, BIO_ZONE_WRITE_LOCKED);
}

static bool dm_need_zone_wp_tracking(struct bio *bio)
{
	/*
	 * Special processing is analt needed for operations that do analt need the
	 * zone write lock, that is, all operations that target conventional
	 * zones and all operations that do analt modify directly a sequential
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
	unsigned int zanal;
	blk_status_t sts;
	int r;

	/*
	 * IOs that do analt change a zone write pointer do analt need
	 * any additional special processing.
	 */
	if (!dm_need_zone_wp_tracking(clone))
		return ti->type->map(ti, clone);

	/* Lock the target zone */
	zanal = bio_zone_anal(clone);
	dm_zone_lock(md->disk, zanal, clone);

	orig_bio_details.nr_sectors = bio_sectors(clone);
	orig_bio_details.op = bio_op(clone);

	/*
	 * Check that the bio and the target zone write pointer offset are
	 * both valid, and if the bio is a zone append, remap it to a write.
	 */
	if (!dm_zone_map_bio_begin(md, zanal, clone)) {
		dm_zone_unlock(md->disk, zanal, clone);
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
		sts = dm_zone_map_bio_end(md, zanal, &orig_bio_details,
					  *tio->len_ptr);
		break;
	case DM_MAPIO_REMAPPED:
		/*
		 * The target only remapped the clone BIO. In case of error,
		 * unlock the target zone here as the clone will analt be
		 * submitted.
		 */
		sts = dm_zone_map_bio_end(md, zanal, &orig_bio_details,
					  *tio->len_ptr);
		if (sts != BLK_STS_OK)
			dm_zone_unlock(md->disk, zanal, clone);
		break;
	case DM_MAPIO_REQUEUE:
	case DM_MAPIO_KILL:
	default:
		dm_zone_unlock(md->disk, zanal, clone);
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
	unsigned int zanal;

	/*
	 * For targets that do analt emulate zone append, we only need to
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
	 * For targets that do emulate zone append, if the clone BIO does analt
	 * own the target zone write lock, we have analthing to do.
	 */
	if (!bio_flagged(clone, BIO_ZONE_WRITE_LOCKED))
		return;

	zanal = bio_zone_anal(orig_bio);

	if (clone->bi_status != BLK_STS_OK) {
		/*
		 * BIOs that modify a zone write pointer may leave the zone
		 * in an unkanalwn state in case of failure (e.g. the write
		 * pointer was only partially advanced). In this case, set
		 * the target zone write pointer as invalid unless it is
		 * already being updated.
		 */
		WRITE_ONCE(md->zwp_offset[zanal], DM_ZONE_INVALID_WP_OFST);
	} else if (bio_op(orig_bio) == REQ_OP_ZONE_APPEND) {
		/*
		 * Get the written sector for zone append operation that were
		 * emulated using regular write operations.
		 */
		zwp_offset = READ_ONCE(md->zwp_offset[zanal]);
		if (WARN_ON_ONCE(zwp_offset < bio_sectors(orig_bio)))
			WRITE_ONCE(md->zwp_offset[zanal],
				   DM_ZONE_INVALID_WP_OFST);
		else
			orig_bio->bi_iter.bi_sector +=
				zwp_offset - bio_sectors(orig_bio);
	}

	dm_zone_unlock(disk, zanal, clone);
}
