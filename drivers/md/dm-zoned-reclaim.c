/*
 * Copyright (C) 2017 Western Digital Corporation or its affiliates.
 *
 * This file is released under the GPL.
 */

#include "dm-zoned.h"

#include <linux/module.h>

#define	DM_MSG_PREFIX		"zoned reclaim"

struct dmz_reclaim {
	struct dmz_metadata     *metadata;
	struct dmz_dev		*dev;

	struct delayed_work	work;
	struct workqueue_struct *wq;

	struct dm_kcopyd_client	*kc;
	struct dm_kcopyd_throttle kc_throttle;
	int			kc_err;

	unsigned long		flags;

	/* Last target access time */
	unsigned long		atime;
};

/*
 * Reclaim state flags.
 */
enum {
	DMZ_RECLAIM_KCOPY,
};

/*
 * Number of seconds of target BIO inactivity to consider the target idle.
 */
#define DMZ_IDLE_PERIOD		(10UL * HZ)

/*
 * Percentage of unmapped (free) random zones below which reclaim starts
 * even if the target is busy.
 */
#define DMZ_RECLAIM_LOW_UNMAP_RND	30

/*
 * Percentage of unmapped (free) random zones above which reclaim will
 * stop if the target is busy.
 */
#define DMZ_RECLAIM_HIGH_UNMAP_RND	50

/*
 * Align a sequential zone write pointer to chunk_block.
 */
static int dmz_reclaim_align_wp(struct dmz_reclaim *zrc, struct dm_zone *zone,
				sector_t block)
{
	struct dmz_metadata *zmd = zrc->metadata;
	sector_t wp_block = zone->wp_block;
	unsigned int nr_blocks;
	int ret;

	if (wp_block == block)
		return 0;

	if (wp_block > block)
		return -EIO;

	/*
	 * Zeroout the space between the write
	 * pointer and the requested position.
	 */
	nr_blocks = block - wp_block;
	ret = blkdev_issue_zeroout(zrc->dev->bdev,
				   dmz_start_sect(zmd, zone) + dmz_blk2sect(wp_block),
				   dmz_blk2sect(nr_blocks), GFP_NOFS, false);
	if (ret) {
		dmz_dev_err(zrc->dev,
			    "Align zone %u wp %llu to %llu (wp+%u) blocks failed %d",
			    dmz_id(zmd, zone), (unsigned long long)wp_block,
			    (unsigned long long)block, nr_blocks, ret);
		return ret;
	}

	zone->wp_block = block;

	return 0;
}

/*
 * dm_kcopyd_copy end notification.
 */
static void dmz_reclaim_kcopy_end(int read_err, unsigned long write_err,
				  void *context)
{
	struct dmz_reclaim *zrc = context;

	if (read_err || write_err)
		zrc->kc_err = -EIO;
	else
		zrc->kc_err = 0;

	clear_bit_unlock(DMZ_RECLAIM_KCOPY, &zrc->flags);
	smp_mb__after_atomic();
	wake_up_bit(&zrc->flags, DMZ_RECLAIM_KCOPY);
}

/*
 * Copy valid blocks of src_zone into dst_zone.
 */
static int dmz_reclaim_copy(struct dmz_reclaim *zrc,
			    struct dm_zone *src_zone, struct dm_zone *dst_zone)
{
	struct dmz_metadata *zmd = zrc->metadata;
	struct dmz_dev *dev = zrc->dev;
	struct dm_io_region src, dst;
	sector_t block = 0, end_block;
	sector_t nr_blocks;
	sector_t src_zone_block;
	sector_t dst_zone_block;
	unsigned long flags = 0;
	int ret;

	if (dmz_is_seq(src_zone))
		end_block = src_zone->wp_block;
	else
		end_block = dev->zone_nr_blocks;
	src_zone_block = dmz_start_block(zmd, src_zone);
	dst_zone_block = dmz_start_block(zmd, dst_zone);

	if (dmz_is_seq(dst_zone))
		set_bit(DM_KCOPYD_WRITE_SEQ, &flags);

	while (block < end_block) {
		/* Get a valid region from the source zone */
		ret = dmz_first_valid_block(zmd, src_zone, &block);
		if (ret <= 0)
			return ret;
		nr_blocks = ret;

		/*
		 * If we are writing in a sequential zone, we must make sure
		 * that writes are sequential. So Zeroout any eventual hole
		 * between writes.
		 */
		if (dmz_is_seq(dst_zone)) {
			ret = dmz_reclaim_align_wp(zrc, dst_zone, block);
			if (ret)
				return ret;
		}

		src.bdev = dev->bdev;
		src.sector = dmz_blk2sect(src_zone_block + block);
		src.count = dmz_blk2sect(nr_blocks);

		dst.bdev = dev->bdev;
		dst.sector = dmz_blk2sect(dst_zone_block + block);
		dst.count = src.count;

		/* Copy the valid region */
		set_bit(DMZ_RECLAIM_KCOPY, &zrc->flags);
		ret = dm_kcopyd_copy(zrc->kc, &src, 1, &dst, flags,
				     dmz_reclaim_kcopy_end, zrc);
		if (ret)
			return ret;

		/* Wait for copy to complete */
		wait_on_bit_io(&zrc->flags, DMZ_RECLAIM_KCOPY,
			       TASK_UNINTERRUPTIBLE);
		if (zrc->kc_err)
			return zrc->kc_err;

		block += nr_blocks;
		if (dmz_is_seq(dst_zone))
			dst_zone->wp_block = block;
	}

	return 0;
}

/*
 * Move valid blocks of dzone buffer zone into dzone (after its write pointer)
 * and free the buffer zone.
 */
static int dmz_reclaim_buf(struct dmz_reclaim *zrc, struct dm_zone *dzone)
{
	struct dm_zone *bzone = dzone->bzone;
	sector_t chunk_block = dzone->wp_block;
	struct dmz_metadata *zmd = zrc->metadata;
	int ret;

	dmz_dev_debug(zrc->dev,
		      "Chunk %u, move buf zone %u (weight %u) to data zone %u (weight %u)",
		      dzone->chunk, dmz_id(zmd, bzone), dmz_weight(bzone),
		      dmz_id(zmd, dzone), dmz_weight(dzone));

	/* Flush data zone into the buffer zone */
	ret = dmz_reclaim_copy(zrc, bzone, dzone);
	if (ret < 0)
		return ret;

	dmz_lock_flush(zmd);

	/* Validate copied blocks */
	ret = dmz_merge_valid_blocks(zmd, bzone, dzone, chunk_block);
	if (ret == 0) {
		/* Free the buffer zone */
		dmz_invalidate_blocks(zmd, bzone, 0, zrc->dev->zone_nr_blocks);
		dmz_lock_map(zmd);
		dmz_unmap_zone(zmd, bzone);
		dmz_unlock_zone_reclaim(dzone);
		dmz_free_zone(zmd, bzone);
		dmz_unlock_map(zmd);
	}

	dmz_unlock_flush(zmd);

	return 0;
}

/*
 * Merge valid blocks of dzone into its buffer zone and free dzone.
 */
static int dmz_reclaim_seq_data(struct dmz_reclaim *zrc, struct dm_zone *dzone)
{
	unsigned int chunk = dzone->chunk;
	struct dm_zone *bzone = dzone->bzone;
	struct dmz_metadata *zmd = zrc->metadata;
	int ret = 0;

	dmz_dev_debug(zrc->dev,
		      "Chunk %u, move data zone %u (weight %u) to buf zone %u (weight %u)",
		      chunk, dmz_id(zmd, dzone), dmz_weight(dzone),
		      dmz_id(zmd, bzone), dmz_weight(bzone));

	/* Flush data zone into the buffer zone */
	ret = dmz_reclaim_copy(zrc, dzone, bzone);
	if (ret < 0)
		return ret;

	dmz_lock_flush(zmd);

	/* Validate copied blocks */
	ret = dmz_merge_valid_blocks(zmd, dzone, bzone, 0);
	if (ret == 0) {
		/*
		 * Free the data zone and remap the chunk to
		 * the buffer zone.
		 */
		dmz_invalidate_blocks(zmd, dzone, 0, zrc->dev->zone_nr_blocks);
		dmz_lock_map(zmd);
		dmz_unmap_zone(zmd, bzone);
		dmz_unmap_zone(zmd, dzone);
		dmz_unlock_zone_reclaim(dzone);
		dmz_free_zone(zmd, dzone);
		dmz_map_zone(zmd, bzone, chunk);
		dmz_unlock_map(zmd);
	}

	dmz_unlock_flush(zmd);

	return 0;
}

/*
 * Move valid blocks of the random data zone dzone into a free sequential zone.
 * Once blocks are moved, remap the zone chunk to the sequential zone.
 */
static int dmz_reclaim_rnd_data(struct dmz_reclaim *zrc, struct dm_zone *dzone)
{
	unsigned int chunk = dzone->chunk;
	struct dm_zone *szone = NULL;
	struct dmz_metadata *zmd = zrc->metadata;
	int ret;

	/* Get a free sequential zone */
	dmz_lock_map(zmd);
	szone = dmz_alloc_zone(zmd, DMZ_ALLOC_RECLAIM);
	dmz_unlock_map(zmd);
	if (!szone)
		return -ENOSPC;

	dmz_dev_debug(zrc->dev,
		      "Chunk %u, move rnd zone %u (weight %u) to seq zone %u",
		      chunk, dmz_id(zmd, dzone), dmz_weight(dzone),
		      dmz_id(zmd, szone));

	/* Flush the random data zone into the sequential zone */
	ret = dmz_reclaim_copy(zrc, dzone, szone);

	dmz_lock_flush(zmd);

	if (ret == 0) {
		/* Validate copied blocks */
		ret = dmz_copy_valid_blocks(zmd, dzone, szone);
	}
	if (ret) {
		/* Free the sequential zone */
		dmz_lock_map(zmd);
		dmz_free_zone(zmd, szone);
		dmz_unlock_map(zmd);
	} else {
		/* Free the data zone and remap the chunk */
		dmz_invalidate_blocks(zmd, dzone, 0, zrc->dev->zone_nr_blocks);
		dmz_lock_map(zmd);
		dmz_unmap_zone(zmd, dzone);
		dmz_unlock_zone_reclaim(dzone);
		dmz_free_zone(zmd, dzone);
		dmz_map_zone(zmd, szone, chunk);
		dmz_unlock_map(zmd);
	}

	dmz_unlock_flush(zmd);

	return 0;
}

/*
 * Reclaim an empty zone.
 */
static void dmz_reclaim_empty(struct dmz_reclaim *zrc, struct dm_zone *dzone)
{
	struct dmz_metadata *zmd = zrc->metadata;

	dmz_lock_flush(zmd);
	dmz_lock_map(zmd);
	dmz_unmap_zone(zmd, dzone);
	dmz_unlock_zone_reclaim(dzone);
	dmz_free_zone(zmd, dzone);
	dmz_unlock_map(zmd);
	dmz_unlock_flush(zmd);
}

/*
 * Find a candidate zone for reclaim and process it.
 */
static void dmz_reclaim(struct dmz_reclaim *zrc)
{
	struct dmz_metadata *zmd = zrc->metadata;
	struct dm_zone *dzone;
	struct dm_zone *rzone;
	unsigned long start;
	int ret;

	/* Get a data zone */
	dzone = dmz_get_zone_for_reclaim(zmd);
	if (!dzone)
		return;

	start = jiffies;

	if (dmz_is_rnd(dzone)) {
		if (!dmz_weight(dzone)) {
			/* Empty zone */
			dmz_reclaim_empty(zrc, dzone);
			ret = 0;
		} else {
			/*
			 * Reclaim the random data zone by moving its
			 * valid data blocks to a free sequential zone.
			 */
			ret = dmz_reclaim_rnd_data(zrc, dzone);
		}
		rzone = dzone;

	} else {
		struct dm_zone *bzone = dzone->bzone;
		sector_t chunk_block = 0;

		ret = dmz_first_valid_block(zmd, bzone, &chunk_block);
		if (ret < 0)
			goto out;

		if (ret == 0 || chunk_block >= dzone->wp_block) {
			/*
			 * The buffer zone is empty or its valid blocks are
			 * after the data zone write pointer.
			 */
			ret = dmz_reclaim_buf(zrc, dzone);
			rzone = bzone;
		} else {
			/*
			 * Reclaim the data zone by merging it into the
			 * buffer zone so that the buffer zone itself can
			 * be later reclaimed.
			 */
			ret = dmz_reclaim_seq_data(zrc, dzone);
			rzone = dzone;
		}
	}
out:
	if (ret) {
		dmz_unlock_zone_reclaim(dzone);
		return;
	}

	(void) dmz_flush_metadata(zrc->metadata);

	dmz_dev_debug(zrc->dev, "Reclaimed zone %u in %u ms",
		      dmz_id(zmd, rzone), jiffies_to_msecs(jiffies - start));
}

/*
 * Test if the target device is idle.
 */
static inline int dmz_target_idle(struct dmz_reclaim *zrc)
{
	return time_is_before_jiffies(zrc->atime + DMZ_IDLE_PERIOD);
}

/*
 * Test if reclaim is necessary.
 */
static bool dmz_should_reclaim(struct dmz_reclaim *zrc)
{
	struct dmz_metadata *zmd = zrc->metadata;
	unsigned int nr_rnd = dmz_nr_rnd_zones(zmd);
	unsigned int nr_unmap_rnd = dmz_nr_unmap_rnd_zones(zmd);
	unsigned int p_unmap_rnd = nr_unmap_rnd * 100 / nr_rnd;

	/* Reclaim when idle */
	if (dmz_target_idle(zrc) && nr_unmap_rnd < nr_rnd)
		return true;

	/* If there are still plenty of random zones, do not reclaim */
	if (p_unmap_rnd >= DMZ_RECLAIM_HIGH_UNMAP_RND)
		return false;

	/*
	 * If the percentage of unmappped random zones is low,
	 * reclaim even if the target is busy.
	 */
	return p_unmap_rnd <= DMZ_RECLAIM_LOW_UNMAP_RND;
}

/*
 * Reclaim work function.
 */
static void dmz_reclaim_work(struct work_struct *work)
{
	struct dmz_reclaim *zrc = container_of(work, struct dmz_reclaim, work.work);
	struct dmz_metadata *zmd = zrc->metadata;
	unsigned int nr_rnd, nr_unmap_rnd;
	unsigned int p_unmap_rnd;

	if (!dmz_should_reclaim(zrc)) {
		mod_delayed_work(zrc->wq, &zrc->work, DMZ_IDLE_PERIOD);
		return;
	}

	/*
	 * We need to start reclaiming random zones: set up zone copy
	 * throttling to either go fast if we are very low on random zones
	 * and slower if there are still some free random zones to avoid
	 * as much as possible to negatively impact the user workload.
	 */
	nr_rnd = dmz_nr_rnd_zones(zmd);
	nr_unmap_rnd = dmz_nr_unmap_rnd_zones(zmd);
	p_unmap_rnd = nr_unmap_rnd * 100 / nr_rnd;
	if (dmz_target_idle(zrc) || p_unmap_rnd < DMZ_RECLAIM_LOW_UNMAP_RND / 2) {
		/* Idle or very low percentage: go fast */
		zrc->kc_throttle.throttle = 100;
	} else {
		/* Busy but we still have some random zone: throttle */
		zrc->kc_throttle.throttle = min(75U, 100U - p_unmap_rnd / 2);
	}

	dmz_dev_debug(zrc->dev,
		      "Reclaim (%u): %s, %u%% free rnd zones (%u/%u)",
		      zrc->kc_throttle.throttle,
		      (dmz_target_idle(zrc) ? "Idle" : "Busy"),
		      p_unmap_rnd, nr_unmap_rnd, nr_rnd);

	dmz_reclaim(zrc);

	dmz_schedule_reclaim(zrc);
}

/*
 * Initialize reclaim.
 */
int dmz_ctr_reclaim(struct dmz_dev *dev, struct dmz_metadata *zmd,
		    struct dmz_reclaim **reclaim)
{
	struct dmz_reclaim *zrc;
	int ret;

	zrc = kzalloc(sizeof(struct dmz_reclaim), GFP_KERNEL);
	if (!zrc)
		return -ENOMEM;

	zrc->dev = dev;
	zrc->metadata = zmd;
	zrc->atime = jiffies;

	/* Reclaim kcopyd client */
	zrc->kc = dm_kcopyd_client_create(&zrc->kc_throttle);
	if (IS_ERR(zrc->kc)) {
		ret = PTR_ERR(zrc->kc);
		zrc->kc = NULL;
		goto err;
	}

	/* Reclaim work */
	INIT_DELAYED_WORK(&zrc->work, dmz_reclaim_work);
	zrc->wq = alloc_ordered_workqueue("dmz_rwq_%s", WQ_MEM_RECLAIM,
					  dev->name);
	if (!zrc->wq) {
		ret = -ENOMEM;
		goto err;
	}

	*reclaim = zrc;
	queue_delayed_work(zrc->wq, &zrc->work, 0);

	return 0;
err:
	if (zrc->kc)
		dm_kcopyd_client_destroy(zrc->kc);
	kfree(zrc);

	return ret;
}

/*
 * Terminate reclaim.
 */
void dmz_dtr_reclaim(struct dmz_reclaim *zrc)
{
	cancel_delayed_work_sync(&zrc->work);
	destroy_workqueue(zrc->wq);
	dm_kcopyd_client_destroy(zrc->kc);
	kfree(zrc);
}

/*
 * Suspend reclaim.
 */
void dmz_suspend_reclaim(struct dmz_reclaim *zrc)
{
	cancel_delayed_work_sync(&zrc->work);
}

/*
 * Resume reclaim.
 */
void dmz_resume_reclaim(struct dmz_reclaim *zrc)
{
	queue_delayed_work(zrc->wq, &zrc->work, DMZ_IDLE_PERIOD);
}

/*
 * BIO accounting.
 */
void dmz_reclaim_bio_acc(struct dmz_reclaim *zrc)
{
	zrc->atime = jiffies;
}

/*
 * Start reclaim if necessary.
 */
void dmz_schedule_reclaim(struct dmz_reclaim *zrc)
{
	if (dmz_should_reclaim(zrc))
		mod_delayed_work(zrc->wq, &zrc->work, 0);
}

