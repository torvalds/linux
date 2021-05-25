// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/blkdev.h>

#include "dm-core.h"

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
	struct dm_report_zones_args args = {
		.next_sector = sector,
		.orig_data = data,
		.orig_cb = cb,
	};

	if (dm_suspended_md(md))
		return -EAGAIN;

	map = dm_get_live_table(md, &srcu_idx);
	if (!map) {
		ret = -EIO;
		goto out;
	}

	do {
		struct dm_target *tgt;

		tgt = dm_table_find_target(map, args.next_sector);
		if (WARN_ON_ONCE(!tgt->type->report_zones)) {
			ret = -EIO;
			goto out;
		}

		args.tgt = tgt;
		ret = tgt->type->report_zones(tgt, &args,
					      nr_zones - args.zone_idx);
		if (ret < 0)
			goto out;
	} while (args.zone_idx < nr_zones &&
		 args.next_sector < get_capacity(disk));

	ret = args.zone_idx;
out:
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

void dm_set_zones_restrictions(struct dm_table *t, struct request_queue *q)
{
	if (!blk_queue_is_zoned(q))
		return;

	/*
	 * For a zoned target, the number of zones should be updated for the
	 * correct value to be exposed in sysfs queue/nr_zones. For a BIO based
	 * target, this is all that is needed.
	 */
	WARN_ON_ONCE(queue_is_mq(q));
	q->nr_zones = blkdev_nr_zones(t->md->disk);
}
