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

	if (!md->zone_revalidate_map) {
		/* Regular user context */
		if (dm_suspended_md(md))
			return -EAGAIN;

		map = dm_get_live_table(md, &srcu_idx);
		if (!map)
			return -EIO;
	} else {
		/* Zone revalidation during __bind() */
		map = md->zone_revalidate_map;
	}

	ret = dm_blk_do_report_zones(md, map, sector, nr_zones, cb, data);

	if (!md->zone_revalidate_map)
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
 * Count conventional zones of a mapped zoned device. If the device
 * only has conventional zones, do not expose it as zoned.
 */
static int dm_check_zoned_cb(struct blk_zone *zone, unsigned int idx,
			     void *data)
{
	unsigned int *nr_conv_zones = data;

	if (zone->type == BLK_ZONE_TYPE_CONVENTIONAL)
		(*nr_conv_zones)++;

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
	int ret;

	/* Revalidate only if something changed. */
	if (!disk->nr_zones || disk->nr_zones != md->nr_zones)
		md->nr_zones = 0;

	if (md->nr_zones)
		return 0;

	/*
	 * Our table is not live yet. So the call to dm_get_live_table()
	 * in dm_blk_report_zones() will fail. Set a temporary pointer to
	 * our table for dm_blk_report_zones() to use directly.
	 */
	md->zone_revalidate_map = t;
	ret = blk_revalidate_disk_zones(disk);
	md->zone_revalidate_map = NULL;

	if (ret) {
		DMERR("Revalidate zones failed %d", ret);
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

int dm_set_zones_restrictions(struct dm_table *t, struct request_queue *q,
		struct queue_limits *lim)
{
	struct mapped_device *md = t->md;
	struct gendisk *disk = md->disk;
	unsigned int nr_conv_zones = 0;
	int ret;

	/*
	 * Check if zone append is natively supported, and if not, set the
	 * mapped device queue as needing zone append emulation.
	 */
	WARN_ON_ONCE(queue_is_mq(q));
	if (dm_table_supports_zone_append(t)) {
		clear_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
	} else {
		set_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
		lim->max_zone_append_sectors = 0;
	}

	if (!get_capacity(md->disk))
		return 0;

	/*
	 * Count conventional zones to check that the mapped device will indeed 
	 * have sequential write required zones.
	 */
	md->zone_revalidate_map = t;
	ret = dm_blk_report_zones(disk, 0, UINT_MAX,
				  dm_check_zoned_cb, &nr_conv_zones);
	md->zone_revalidate_map = NULL;
	if (ret < 0) {
		DMERR("Check zoned failed %d", ret);
		return ret;
	}

	/*
	 * If we only have conventional zones, expose the mapped device as
	 * a regular device.
	 */
	if (nr_conv_zones >= ret) {
		lim->max_open_zones = 0;
		lim->max_active_zones = 0;
		lim->zoned = false;
		clear_bit(DMF_EMULATE_ZONE_APPEND, &md->flags);
		disk->nr_zones = 0;
		return 0;
	}

	if (!md->disk->nr_zones) {
		DMINFO("%s using %s zone append",
		       md->disk->disk_name,
		       queue_emulates_zone_append(q) ? "emulated" : "native");
	}

	ret = dm_revalidate_zones(md, t);
	if (ret < 0)
		return ret;

	if (!static_key_enabled(&zoned_enabled.key))
		static_branch_enable(&zoned_enabled);
	return 0;
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
