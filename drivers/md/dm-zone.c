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
	struct dm_table *zone_revalidate_map = md->zone_revalidate_map;
	int srcu_idx, ret = -EIO;
	bool put_table = false;

	if (!zone_revalidate_map || md->revalidate_map_task != current) {
		/*
		 * Regular user context or
		 * Zone revalidation during __bind() is in progress, but this
		 * call is from a different process
		 */
		if (dm_suspended_md(md))
			return -EAGAIN;

		map = dm_get_live_table(md, &srcu_idx);
		put_table = true;
	} else {
		/* Zone revalidation during __bind() */
		map = zone_revalidate_map;
	}

	if (map)
		ret = dm_blk_do_report_zones(md, map, sector, nr_zones, cb,
					     data);

	if (put_table)
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

/*
 * Revalidate the zones of a mapped device to initialize resource necessary
 * for zone append emulation. Note that we cannot simply use the block layer
 * blk_revalidate_disk_zones() function here as the mapped device is suspended
 * (this is called from __bind() context).
 */
int dm_revalidate_zones(struct dm_table *t, struct request_queue *q)
{
	struct mapped_device *md = t->md;
	struct gendisk *disk = md->disk;
	unsigned int nr_zones = disk->nr_zones;
	int ret;

	if (!get_capacity(disk))
		return 0;

	/*
	 * Do not revalidate if zone write plug resources have already
	 * been allocated.
	 */
	if (dm_has_zone_plugs(md))
		return 0;

	DMINFO("%s using %s zone append", disk->disk_name,
	       queue_emulates_zone_append(q) ? "emulated" : "native");

	/*
	 * Our table is not live yet. So the call to dm_get_live_table()
	 * in dm_blk_report_zones() will fail. Set a temporary pointer to
	 * our table for dm_blk_report_zones() to use directly.
	 */
	md->zone_revalidate_map = t;
	md->revalidate_map_task = current;
	ret = blk_revalidate_disk_zones(disk);
	md->revalidate_map_task = NULL;
	md->zone_revalidate_map = NULL;

	if (ret) {
		DMERR("Revalidate zones failed %d", ret);
		disk->nr_zones = nr_zones;
		return ret;
	}

	md->nr_zones = disk->nr_zones;

	return 0;
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

struct dm_device_zone_count {
	sector_t start;
	sector_t len;
	unsigned int total_nr_seq_zones;
	unsigned int target_nr_seq_zones;
};

/*
 * Count the total number of and the number of mapped sequential zones of a
 * target zoned device.
 */
static int dm_device_count_zones_cb(struct blk_zone *zone,
				    unsigned int idx, void *data)
{
	struct dm_device_zone_count *zc = data;

	if (zone->type != BLK_ZONE_TYPE_CONVENTIONAL) {
		zc->total_nr_seq_zones++;
		if (zone->start >= zc->start &&
		    zone->start < zc->start + zc->len)
			zc->target_nr_seq_zones++;
	}

	return 0;
}

static int dm_device_count_zones(struct dm_dev *dev,
				 struct dm_device_zone_count *zc)
{
	int ret;

	ret = blkdev_report_zones(dev->bdev, 0, BLK_ALL_ZONES,
				  dm_device_count_zones_cb, zc);
	if (ret < 0)
		return ret;
	if (!ret)
		return -EIO;
	return 0;
}

struct dm_zone_resource_limits {
	unsigned int mapped_nr_seq_zones;
	struct queue_limits *lim;
	bool reliable_limits;
};

static int device_get_zone_resource_limits(struct dm_target *ti,
					   struct dm_dev *dev, sector_t start,
					   sector_t len, void *data)
{
	struct dm_zone_resource_limits *zlim = data;
	struct gendisk *disk = dev->bdev->bd_disk;
	unsigned int max_open_zones, max_active_zones;
	int ret;
	struct dm_device_zone_count zc = {
		.start = start,
		.len = len,
	};

	/*
	 * If the target is not the whole device, the device zone resources may
	 * be shared between different targets. Check this by counting the
	 * number of mapped sequential zones: if this number is smaller than the
	 * total number of sequential zones of the target device, then resource
	 * sharing may happen and the zone limits will not be reliable.
	 */
	ret = dm_device_count_zones(dev, &zc);
	if (ret) {
		DMERR("Count %s zones failed %d", disk->disk_name, ret);
		return ret;
	}

	/*
	 * If the target does not map any sequential zones, then we do not need
	 * any zone resource limits.
	 */
	if (!zc.target_nr_seq_zones)
		return 0;

	/*
	 * If the target does not map all sequential zones, the limits
	 * will not be reliable and we cannot use REQ_OP_ZONE_RESET_ALL.
	 */
	if (zc.target_nr_seq_zones < zc.total_nr_seq_zones) {
		zlim->reliable_limits = false;
		ti->zone_reset_all_supported = false;
	}

	/*
	 * If the target maps less sequential zones than the limit values, then
	 * we do not have limits for this target.
	 */
	max_active_zones = disk->queue->limits.max_active_zones;
	if (max_active_zones >= zc.target_nr_seq_zones)
		max_active_zones = 0;
	zlim->lim->max_active_zones =
		min_not_zero(max_active_zones, zlim->lim->max_active_zones);

	max_open_zones = disk->queue->limits.max_open_zones;
	if (max_open_zones >= zc.target_nr_seq_zones)
		max_open_zones = 0;
	zlim->lim->max_open_zones =
		min_not_zero(max_open_zones, zlim->lim->max_open_zones);

	/*
	 * Also count the total number of sequential zones for the mapped
	 * device so that when we are done inspecting all its targets, we are
	 * able to check if the mapped device actually has any sequential zones.
	 */
	zlim->mapped_nr_seq_zones += zc.target_nr_seq_zones;

	return 0;
}

int dm_set_zones_restrictions(struct dm_table *t, struct request_queue *q,
		struct queue_limits *lim)
{
	struct mapped_device *md = t->md;
	struct gendisk *disk = md->disk;
	struct dm_zone_resource_limits zlim = {
		.reliable_limits = true,
		.lim = lim,
	};

	/*
	 * Check if zone append is natively supported, and if not, set the
	 * mapped device queue as needing zone append emulation.
	 */
	WARN_ON_ONCE(queue_is_mq(q));
	if (!dm_table_supports_zone_append(t))
		lim->max_hw_zone_append_sectors = 0;

	/*
	 * Determine the max open and max active zone limits for the mapped
	 * device by inspecting the zone resource limits and the zones mapped
	 * by each target.
	 */
	for (unsigned int i = 0; i < t->num_targets; i++) {
		struct dm_target *ti = dm_table_get_target(t, i);

		/*
		 * Assume that the target can accept REQ_OP_ZONE_RESET_ALL.
		 * device_get_zone_resource_limits() may adjust this if one of
		 * the device used by the target does not have all its
		 * sequential write required zones mapped.
		 */
		ti->zone_reset_all_supported = true;

		if (!ti->type->iterate_devices ||
		    ti->type->iterate_devices(ti,
				device_get_zone_resource_limits, &zlim)) {
			DMERR("Could not determine %s zone resource limits",
			      disk->disk_name);
			return -ENODEV;
		}
	}

	/*
	 * If we only have conventional zones mapped, expose the mapped device
	 + as a regular device.
	 */
	if (!zlim.mapped_nr_seq_zones) {
		lim->max_open_zones = 0;
		lim->max_active_zones = 0;
		lim->max_hw_zone_append_sectors = 0;
		lim->max_zone_append_sectors = 0;
		lim->zone_write_granularity = 0;
		lim->chunk_sectors = 0;
		lim->features &= ~BLK_FEAT_ZONED;
		return 0;
	}

	if (get_capacity(disk) && dm_has_zone_plugs(t->md)) {
		if (q->limits.chunk_sectors != lim->chunk_sectors) {
			DMWARN("%s: device has zone write plug resources. "
			       "Cannot change zone size",
			       disk->disk_name);
			return -EINVAL;
		}
		if (lim->max_hw_zone_append_sectors != 0 &&
		    !dm_table_is_wildcard(t)) {
			DMWARN("%s: device has zone write plug resources. "
			       "New table must emulate zone append",
			       disk->disk_name);
			return -EINVAL;
		}
	}
	/*
	 * Warn once (when the capacity is not yet set) if the mapped device is
	 * partially using zone resources of the target devices as that leads to
	 * unreliable limits, i.e. if another mapped device uses the same
	 * underlying devices, we cannot enforce zone limits to guarantee that
	 * writing will not lead to errors. Note that we really should return
	 * an error for such case but there is no easy way to find out if
	 * another mapped device uses the same underlying zoned devices.
	 */
	if (!get_capacity(disk) && !zlim.reliable_limits)
		DMWARN("%s zone resource limits may be unreliable",
		       disk->disk_name);

	if (lim->features & BLK_FEAT_ZONED &&
	    !static_key_enabled(&zoned_enabled.key))
		static_branch_enable(&zoned_enabled);
	return 0;
}

void dm_finalize_zone_settings(struct dm_table *t, struct queue_limits *lim)
{
	struct mapped_device *md = t->md;

	if (lim->features & BLK_FEAT_ZONED) {
		if (dm_table_supports_zone_append(t))
			clear_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
		else
			set_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
	} else {
		clear_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
		md->nr_zones = 0;
		md->disk->nr_zones = 0;
	}
}


/*
 * IO completion callback called from clone_endio().
 */
void dm_zone_endio(struct dm_io *io, struct bio *clone)
{
	struct mapped_device *md = io->md;
	struct gendisk *disk = md->disk;
	struct bio *orig_bio = io->orig_bio;

	/*
	 * Get the offset within the zone of the written sector
	 * and add that to the original bio sector position.
	 */
	if (clone->bi_status == BLK_STS_OK &&
	    bio_op(clone) == REQ_OP_ZONE_APPEND) {
		sector_t mask = bdev_zone_sectors(disk->part0) - 1;

		orig_bio->bi_iter.bi_sector += clone->bi_iter.bi_sector & mask;
	}

	return;
}

static int dm_zone_need_reset_cb(struct blk_zone *zone, unsigned int idx,
				 void *data)
{
	/*
	 * For an all-zones reset, ignore conventional, empty, read-only
	 * and offline zones.
	 */
	switch (zone->cond) {
	case BLK_ZONE_COND_NOT_WP:
	case BLK_ZONE_COND_EMPTY:
	case BLK_ZONE_COND_READONLY:
	case BLK_ZONE_COND_OFFLINE:
		return 0;
	default:
		set_bit(idx, (unsigned long *)data);
		return 0;
	}
}

int dm_zone_get_reset_bitmap(struct mapped_device *md, struct dm_table *t,
			     sector_t sector, unsigned int nr_zones,
			     unsigned long *need_reset)
{
	int ret;

	ret = dm_blk_do_report_zones(md, t, sector, nr_zones,
				     dm_zone_need_reset_cb, need_reset);
	if (ret != nr_zones) {
		DMERR("Get %s zone reset bitmap failed\n",
		      md->disk->disk_name);
		return -EIO;
	}

	return 0;
}
