// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Western Digital Corporation or its affiliates.
 *
 * This file is released under the GPL.
 */

#include "dm-zoned.h"

#include <linux/module.h>

#define	DM_MSG_PREFIX		"zoned"

#define DMZ_MIN_BIOS		8192

/*
 * Zone BIO context.
 */
struct dmz_bioctx {
	struct dmz_target	*target;
	struct dm_zone		*zone;
	struct bio		*bio;
	refcount_t		ref;
};

/*
 * Chunk work descriptor.
 */
struct dm_chunk_work {
	struct work_struct	work;
	refcount_t		refcount;
	struct dmz_target	*target;
	unsigned int		chunk;
	struct bio_list		bio_list;
};

/*
 * Target descriptor.
 */
struct dmz_target {
	struct dm_dev		*ddev;

	unsigned long		flags;

	/* Zoned block device information */
	struct dmz_dev		*dev;

	/* For metadata handling */
	struct dmz_metadata     *metadata;

	/* For reclaim */
	struct dmz_reclaim	*reclaim;

	/* For chunk work */
	struct radix_tree_root	chunk_rxtree;
	struct workqueue_struct *chunk_wq;
	struct mutex		chunk_lock;

	/* For cloned BIOs to zones */
	struct bio_set		bio_set;

	/* For flush */
	spinlock_t		flush_lock;
	struct bio_list		flush_list;
	struct delayed_work	flush_work;
	struct workqueue_struct *flush_wq;
};

/*
 * Flush intervals (seconds).
 */
#define DMZ_FLUSH_PERIOD	(10 * HZ)

/*
 * Target BIO completion.
 */
static inline void dmz_bio_endio(struct bio *bio, blk_status_t status)
{
	struct dmz_bioctx *bioctx = dm_per_bio_data(bio, sizeof(struct dmz_bioctx));

	if (status != BLK_STS_OK && bio->bi_status == BLK_STS_OK)
		bio->bi_status = status;
	if (bio->bi_status != BLK_STS_OK)
		bioctx->target->dev->flags |= DMZ_CHECK_BDEV;

	if (refcount_dec_and_test(&bioctx->ref)) {
		struct dm_zone *zone = bioctx->zone;

		if (zone) {
			if (bio->bi_status != BLK_STS_OK &&
			    bio_op(bio) == REQ_OP_WRITE &&
			    dmz_is_seq(zone))
				set_bit(DMZ_SEQ_WRITE_ERR, &zone->flags);
			dmz_deactivate_zone(zone);
		}
		bio_endio(bio);
	}
}

/*
 * Completion callback for an internally cloned target BIO. This terminates the
 * target BIO when there are no more references to its context.
 */
static void dmz_clone_endio(struct bio *clone)
{
	struct dmz_bioctx *bioctx = clone->bi_private;
	blk_status_t status = clone->bi_status;

	bio_put(clone);
	dmz_bio_endio(bioctx->bio, status);
}

/*
 * Issue a clone of a target BIO. The clone may only partially process the
 * original target BIO.
 */
static int dmz_submit_bio(struct dmz_target *dmz, struct dm_zone *zone,
			  struct bio *bio, sector_t chunk_block,
			  unsigned int nr_blocks)
{
	struct dmz_bioctx *bioctx = dm_per_bio_data(bio, sizeof(struct dmz_bioctx));
	struct bio *clone;

	clone = bio_clone_fast(bio, GFP_NOIO, &dmz->bio_set);
	if (!clone)
		return -ENOMEM;

	bio_set_dev(clone, dmz->dev->bdev);
	clone->bi_iter.bi_sector =
		dmz_start_sect(dmz->metadata, zone) + dmz_blk2sect(chunk_block);
	clone->bi_iter.bi_size = dmz_blk2sect(nr_blocks) << SECTOR_SHIFT;
	clone->bi_end_io = dmz_clone_endio;
	clone->bi_private = bioctx;

	bio_advance(bio, clone->bi_iter.bi_size);

	refcount_inc(&bioctx->ref);
	generic_make_request(clone);

	if (bio_op(bio) == REQ_OP_WRITE && dmz_is_seq(zone))
		zone->wp_block += nr_blocks;

	return 0;
}

/*
 * Zero out pages of discarded blocks accessed by a read BIO.
 */
static void dmz_handle_read_zero(struct dmz_target *dmz, struct bio *bio,
				 sector_t chunk_block, unsigned int nr_blocks)
{
	unsigned int size = nr_blocks << DMZ_BLOCK_SHIFT;

	/* Clear nr_blocks */
	swap(bio->bi_iter.bi_size, size);
	zero_fill_bio(bio);
	swap(bio->bi_iter.bi_size, size);

	bio_advance(bio, size);
}

/*
 * Process a read BIO.
 */
static int dmz_handle_read(struct dmz_target *dmz, struct dm_zone *zone,
			   struct bio *bio)
{
	sector_t chunk_block = dmz_chunk_block(dmz->dev, dmz_bio_block(bio));
	unsigned int nr_blocks = dmz_bio_blocks(bio);
	sector_t end_block = chunk_block + nr_blocks;
	struct dm_zone *rzone, *bzone;
	int ret;

	/* Read into unmapped chunks need only zeroing the BIO buffer */
	if (!zone) {
		zero_fill_bio(bio);
		return 0;
	}

	dmz_dev_debug(dmz->dev, "READ chunk %llu -> %s zone %u, block %llu, %u blocks",
		      (unsigned long long)dmz_bio_chunk(dmz->dev, bio),
		      (dmz_is_rnd(zone) ? "RND" : "SEQ"),
		      dmz_id(dmz->metadata, zone),
		      (unsigned long long)chunk_block, nr_blocks);

	/* Check block validity to determine the read location */
	bzone = zone->bzone;
	while (chunk_block < end_block) {
		nr_blocks = 0;
		if (dmz_is_rnd(zone) || chunk_block < zone->wp_block) {
			/* Test block validity in the data zone */
			ret = dmz_block_valid(dmz->metadata, zone, chunk_block);
			if (ret < 0)
				return ret;
			if (ret > 0) {
				/* Read data zone blocks */
				nr_blocks = ret;
				rzone = zone;
			}
		}

		/*
		 * No valid blocks found in the data zone.
		 * Check the buffer zone, if there is one.
		 */
		if (!nr_blocks && bzone) {
			ret = dmz_block_valid(dmz->metadata, bzone, chunk_block);
			if (ret < 0)
				return ret;
			if (ret > 0) {
				/* Read buffer zone blocks */
				nr_blocks = ret;
				rzone = bzone;
			}
		}

		if (nr_blocks) {
			/* Valid blocks found: read them */
			nr_blocks = min_t(unsigned int, nr_blocks, end_block - chunk_block);
			ret = dmz_submit_bio(dmz, rzone, bio, chunk_block, nr_blocks);
			if (ret)
				return ret;
			chunk_block += nr_blocks;
		} else {
			/* No valid block: zeroout the current BIO block */
			dmz_handle_read_zero(dmz, bio, chunk_block, 1);
			chunk_block++;
		}
	}

	return 0;
}

/*
 * Write blocks directly in a data zone, at the write pointer.
 * If a buffer zone is assigned, invalidate the blocks written
 * in place.
 */
static int dmz_handle_direct_write(struct dmz_target *dmz,
				   struct dm_zone *zone, struct bio *bio,
				   sector_t chunk_block,
				   unsigned int nr_blocks)
{
	struct dmz_metadata *zmd = dmz->metadata;
	struct dm_zone *bzone = zone->bzone;
	int ret;

	if (dmz_is_readonly(zone))
		return -EROFS;

	/* Submit write */
	ret = dmz_submit_bio(dmz, zone, bio, chunk_block, nr_blocks);
	if (ret)
		return ret;

	/*
	 * Validate the blocks in the data zone and invalidate
	 * in the buffer zone, if there is one.
	 */
	ret = dmz_validate_blocks(zmd, zone, chunk_block, nr_blocks);
	if (ret == 0 && bzone)
		ret = dmz_invalidate_blocks(zmd, bzone, chunk_block, nr_blocks);

	return ret;
}

/*
 * Write blocks in the buffer zone of @zone.
 * If no buffer zone is assigned yet, get one.
 * Called with @zone write locked.
 */
static int dmz_handle_buffered_write(struct dmz_target *dmz,
				     struct dm_zone *zone, struct bio *bio,
				     sector_t chunk_block,
				     unsigned int nr_blocks)
{
	struct dmz_metadata *zmd = dmz->metadata;
	struct dm_zone *bzone;
	int ret;

	/* Get the buffer zone. One will be allocated if needed */
	bzone = dmz_get_chunk_buffer(zmd, zone);
	if (IS_ERR(bzone))
		return PTR_ERR(bzone);

	if (dmz_is_readonly(bzone))
		return -EROFS;

	/* Submit write */
	ret = dmz_submit_bio(dmz, bzone, bio, chunk_block, nr_blocks);
	if (ret)
		return ret;

	/*
	 * Validate the blocks in the buffer zone
	 * and invalidate in the data zone.
	 */
	ret = dmz_validate_blocks(zmd, bzone, chunk_block, nr_blocks);
	if (ret == 0 && chunk_block < zone->wp_block)
		ret = dmz_invalidate_blocks(zmd, zone, chunk_block, nr_blocks);

	return ret;
}

/*
 * Process a write BIO.
 */
static int dmz_handle_write(struct dmz_target *dmz, struct dm_zone *zone,
			    struct bio *bio)
{
	sector_t chunk_block = dmz_chunk_block(dmz->dev, dmz_bio_block(bio));
	unsigned int nr_blocks = dmz_bio_blocks(bio);

	if (!zone)
		return -ENOSPC;

	dmz_dev_debug(dmz->dev, "WRITE chunk %llu -> %s zone %u, block %llu, %u blocks",
		      (unsigned long long)dmz_bio_chunk(dmz->dev, bio),
		      (dmz_is_rnd(zone) ? "RND" : "SEQ"),
		      dmz_id(dmz->metadata, zone),
		      (unsigned long long)chunk_block, nr_blocks);

	if (dmz_is_rnd(zone) || chunk_block == zone->wp_block) {
		/*
		 * zone is a random zone or it is a sequential zone
		 * and the BIO is aligned to the zone write pointer:
		 * direct write the zone.
		 */
		return dmz_handle_direct_write(dmz, zone, bio, chunk_block, nr_blocks);
	}

	/*
	 * This is an unaligned write in a sequential zone:
	 * use buffered write.
	 */
	return dmz_handle_buffered_write(dmz, zone, bio, chunk_block, nr_blocks);
}

/*
 * Process a discard BIO.
 */
static int dmz_handle_discard(struct dmz_target *dmz, struct dm_zone *zone,
			      struct bio *bio)
{
	struct dmz_metadata *zmd = dmz->metadata;
	sector_t block = dmz_bio_block(bio);
	unsigned int nr_blocks = dmz_bio_blocks(bio);
	sector_t chunk_block = dmz_chunk_block(dmz->dev, block);
	int ret = 0;

	/* For unmapped chunks, there is nothing to do */
	if (!zone)
		return 0;

	if (dmz_is_readonly(zone))
		return -EROFS;

	dmz_dev_debug(dmz->dev, "DISCARD chunk %llu -> zone %u, block %llu, %u blocks",
		      (unsigned long long)dmz_bio_chunk(dmz->dev, bio),
		      dmz_id(zmd, zone),
		      (unsigned long long)chunk_block, nr_blocks);

	/*
	 * Invalidate blocks in the data zone and its
	 * buffer zone if one is mapped.
	 */
	if (dmz_is_rnd(zone) || chunk_block < zone->wp_block)
		ret = dmz_invalidate_blocks(zmd, zone, chunk_block, nr_blocks);
	if (ret == 0 && zone->bzone)
		ret = dmz_invalidate_blocks(zmd, zone->bzone,
					    chunk_block, nr_blocks);
	return ret;
}

/*
 * Process a BIO.
 */
static void dmz_handle_bio(struct dmz_target *dmz, struct dm_chunk_work *cw,
			   struct bio *bio)
{
	struct dmz_bioctx *bioctx = dm_per_bio_data(bio, sizeof(struct dmz_bioctx));
	struct dmz_metadata *zmd = dmz->metadata;
	struct dm_zone *zone;
	int ret;

	/*
	 * Write may trigger a zone allocation. So make sure the
	 * allocation can succeed.
	 */
	if (bio_op(bio) == REQ_OP_WRITE)
		dmz_schedule_reclaim(dmz->reclaim);

	dmz_lock_metadata(zmd);

	if (dmz->dev->flags & DMZ_BDEV_DYING) {
		ret = -EIO;
		goto out;
	}

	/*
	 * Get the data zone mapping the chunk. There may be no
	 * mapping for read and discard. If a mapping is obtained,
	 + the zone returned will be set to active state.
	 */
	zone = dmz_get_chunk_mapping(zmd, dmz_bio_chunk(dmz->dev, bio),
				     bio_op(bio));
	if (IS_ERR(zone)) {
		ret = PTR_ERR(zone);
		goto out;
	}

	/* Process the BIO */
	if (zone) {
		dmz_activate_zone(zone);
		bioctx->zone = zone;
	}

	switch (bio_op(bio)) {
	case REQ_OP_READ:
		ret = dmz_handle_read(dmz, zone, bio);
		break;
	case REQ_OP_WRITE:
		ret = dmz_handle_write(dmz, zone, bio);
		break;
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
		ret = dmz_handle_discard(dmz, zone, bio);
		break;
	default:
		dmz_dev_err(dmz->dev, "Unsupported BIO operation 0x%x",
			    bio_op(bio));
		ret = -EIO;
	}

	/*
	 * Release the chunk mapping. This will check that the mapping
	 * is still valid, that is, that the zone used still has valid blocks.
	 */
	if (zone)
		dmz_put_chunk_mapping(zmd, zone);
out:
	dmz_bio_endio(bio, errno_to_blk_status(ret));

	dmz_unlock_metadata(zmd);
}

/*
 * Increment a chunk reference counter.
 */
static inline void dmz_get_chunk_work(struct dm_chunk_work *cw)
{
	refcount_inc(&cw->refcount);
}

/*
 * Decrement a chunk work reference count and
 * free it if it becomes 0.
 */
static void dmz_put_chunk_work(struct dm_chunk_work *cw)
{
	if (refcount_dec_and_test(&cw->refcount)) {
		WARN_ON(!bio_list_empty(&cw->bio_list));
		radix_tree_delete(&cw->target->chunk_rxtree, cw->chunk);
		kfree(cw);
	}
}

/*
 * Chunk BIO work function.
 */
static void dmz_chunk_work(struct work_struct *work)
{
	struct dm_chunk_work *cw = container_of(work, struct dm_chunk_work, work);
	struct dmz_target *dmz = cw->target;
	struct bio *bio;

	mutex_lock(&dmz->chunk_lock);

	/* Process the chunk BIOs */
	while ((bio = bio_list_pop(&cw->bio_list))) {
		mutex_unlock(&dmz->chunk_lock);
		dmz_handle_bio(dmz, cw, bio);
		mutex_lock(&dmz->chunk_lock);
		dmz_put_chunk_work(cw);
	}

	/* Queueing the work incremented the work refcount */
	dmz_put_chunk_work(cw);

	mutex_unlock(&dmz->chunk_lock);
}

/*
 * Flush work.
 */
static void dmz_flush_work(struct work_struct *work)
{
	struct dmz_target *dmz = container_of(work, struct dmz_target, flush_work.work);
	struct bio *bio;
	int ret;

	/* Flush dirty metadata blocks */
	ret = dmz_flush_metadata(dmz->metadata);
	if (ret)
		dmz_dev_debug(dmz->dev, "Metadata flush failed, rc=%d\n", ret);

	/* Process queued flush requests */
	while (1) {
		spin_lock(&dmz->flush_lock);
		bio = bio_list_pop(&dmz->flush_list);
		spin_unlock(&dmz->flush_lock);

		if (!bio)
			break;

		dmz_bio_endio(bio, errno_to_blk_status(ret));
	}

	queue_delayed_work(dmz->flush_wq, &dmz->flush_work, DMZ_FLUSH_PERIOD);
}

/*
 * Get a chunk work and start it to process a new BIO.
 * If the BIO chunk has no work yet, create one.
 */
static int dmz_queue_chunk_work(struct dmz_target *dmz, struct bio *bio)
{
	unsigned int chunk = dmz_bio_chunk(dmz->dev, bio);
	struct dm_chunk_work *cw;
	int ret = 0;

	mutex_lock(&dmz->chunk_lock);

	/* Get the BIO chunk work. If one is not active yet, create one */
	cw = radix_tree_lookup(&dmz->chunk_rxtree, chunk);
	if (cw) {
		dmz_get_chunk_work(cw);
	} else {
		/* Create a new chunk work */
		cw = kmalloc(sizeof(struct dm_chunk_work), GFP_NOIO);
		if (unlikely(!cw)) {
			ret = -ENOMEM;
			goto out;
		}

		INIT_WORK(&cw->work, dmz_chunk_work);
		refcount_set(&cw->refcount, 1);
		cw->target = dmz;
		cw->chunk = chunk;
		bio_list_init(&cw->bio_list);

		ret = radix_tree_insert(&dmz->chunk_rxtree, chunk, cw);
		if (unlikely(ret)) {
			kfree(cw);
			goto out;
		}
	}

	bio_list_add(&cw->bio_list, bio);

	dmz_reclaim_bio_acc(dmz->reclaim);
	if (queue_work(dmz->chunk_wq, &cw->work))
		dmz_get_chunk_work(cw);
out:
	mutex_unlock(&dmz->chunk_lock);
	return ret;
}

/*
 * Check if the backing device is being removed. If it's on the way out,
 * start failing I/O. Reclaim and metadata components also call this
 * function to cleanly abort operation in the event of such failure.
 */
bool dmz_bdev_is_dying(struct dmz_dev *dmz_dev)
{
	if (dmz_dev->flags & DMZ_BDEV_DYING)
		return true;

	if (dmz_dev->flags & DMZ_CHECK_BDEV)
		return !dmz_check_bdev(dmz_dev);

	if (blk_queue_dying(bdev_get_queue(dmz_dev->bdev))) {
		dmz_dev_warn(dmz_dev, "Backing device queue dying");
		dmz_dev->flags |= DMZ_BDEV_DYING;
	}

	return dmz_dev->flags & DMZ_BDEV_DYING;
}

/*
 * Check the backing device availability. This detects such events as
 * backing device going offline due to errors, media removals, etc.
 * This check is less efficient than dmz_bdev_is_dying() and should
 * only be performed as a part of error handling.
 */
bool dmz_check_bdev(struct dmz_dev *dmz_dev)
{
	struct gendisk *disk;

	dmz_dev->flags &= ~DMZ_CHECK_BDEV;

	if (dmz_bdev_is_dying(dmz_dev))
		return false;

	disk = dmz_dev->bdev->bd_disk;
	if (disk->fops->check_events &&
	    disk->fops->check_events(disk, 0) & DISK_EVENT_MEDIA_CHANGE) {
		dmz_dev_warn(dmz_dev, "Backing device offline");
		dmz_dev->flags |= DMZ_BDEV_DYING;
	}

	return !(dmz_dev->flags & DMZ_BDEV_DYING);
}

/*
 * Process a new BIO.
 */
static int dmz_map(struct dm_target *ti, struct bio *bio)
{
	struct dmz_target *dmz = ti->private;
	struct dmz_dev *dev = dmz->dev;
	struct dmz_bioctx *bioctx = dm_per_bio_data(bio, sizeof(struct dmz_bioctx));
	sector_t sector = bio->bi_iter.bi_sector;
	unsigned int nr_sectors = bio_sectors(bio);
	sector_t chunk_sector;
	int ret;

	if (dmz_bdev_is_dying(dmz->dev))
		return DM_MAPIO_KILL;

	dmz_dev_debug(dev, "BIO op %d sector %llu + %u => chunk %llu, block %llu, %u blocks",
		      bio_op(bio), (unsigned long long)sector, nr_sectors,
		      (unsigned long long)dmz_bio_chunk(dmz->dev, bio),
		      (unsigned long long)dmz_chunk_block(dmz->dev, dmz_bio_block(bio)),
		      (unsigned int)dmz_bio_blocks(bio));

	bio_set_dev(bio, dev->bdev);

	if (!nr_sectors && bio_op(bio) != REQ_OP_WRITE)
		return DM_MAPIO_REMAPPED;

	/* The BIO should be block aligned */
	if ((nr_sectors & DMZ_BLOCK_SECTORS_MASK) || (sector & DMZ_BLOCK_SECTORS_MASK))
		return DM_MAPIO_KILL;

	/* Initialize the BIO context */
	bioctx->target = dmz;
	bioctx->zone = NULL;
	bioctx->bio = bio;
	refcount_set(&bioctx->ref, 1);

	/* Set the BIO pending in the flush list */
	if (!nr_sectors && bio_op(bio) == REQ_OP_WRITE) {
		spin_lock(&dmz->flush_lock);
		bio_list_add(&dmz->flush_list, bio);
		spin_unlock(&dmz->flush_lock);
		mod_delayed_work(dmz->flush_wq, &dmz->flush_work, 0);
		return DM_MAPIO_SUBMITTED;
	}

	/* Split zone BIOs to fit entirely into a zone */
	chunk_sector = sector & (dev->zone_nr_sectors - 1);
	if (chunk_sector + nr_sectors > dev->zone_nr_sectors)
		dm_accept_partial_bio(bio, dev->zone_nr_sectors - chunk_sector);

	/* Now ready to handle this BIO */
	ret = dmz_queue_chunk_work(dmz, bio);
	if (ret) {
		dmz_dev_debug(dmz->dev,
			      "BIO op %d, can't process chunk %llu, err %i\n",
			      bio_op(bio), (u64)dmz_bio_chunk(dmz->dev, bio),
			      ret);
		return DM_MAPIO_REQUEUE;
	}

	return DM_MAPIO_SUBMITTED;
}

/*
 * Get zoned device information.
 */
static int dmz_get_zoned_device(struct dm_target *ti, char *path)
{
	struct dmz_target *dmz = ti->private;
	struct request_queue *q;
	struct dmz_dev *dev;
	sector_t aligned_capacity;
	int ret;

	/* Get the target device */
	ret = dm_get_device(ti, path, dm_table_get_mode(ti->table), &dmz->ddev);
	if (ret) {
		ti->error = "Get target device failed";
		dmz->ddev = NULL;
		return ret;
	}

	dev = kzalloc(sizeof(struct dmz_dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->bdev = dmz->ddev->bdev;
	(void)bdevname(dev->bdev, dev->name);

	if (bdev_zoned_model(dev->bdev) == BLK_ZONED_NONE) {
		ti->error = "Not a zoned block device";
		ret = -EINVAL;
		goto err;
	}

	q = bdev_get_queue(dev->bdev);
	dev->capacity = i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT;
	aligned_capacity = dev->capacity &
				~((sector_t)blk_queue_zone_sectors(q) - 1);
	if (ti->begin ||
	    ((ti->len != dev->capacity) && (ti->len != aligned_capacity))) {
		ti->error = "Partial mapping not supported";
		ret = -EINVAL;
		goto err;
	}

	dev->zone_nr_sectors = blk_queue_zone_sectors(q);
	dev->zone_nr_sectors_shift = ilog2(dev->zone_nr_sectors);

	dev->zone_nr_blocks = dmz_sect2blk(dev->zone_nr_sectors);
	dev->zone_nr_blocks_shift = ilog2(dev->zone_nr_blocks);

	dev->nr_zones = blkdev_nr_zones(dev->bdev->bd_disk);

	dmz->dev = dev;

	return 0;
err:
	dm_put_device(ti, dmz->ddev);
	kfree(dev);

	return ret;
}

/*
 * Cleanup zoned device information.
 */
static void dmz_put_zoned_device(struct dm_target *ti)
{
	struct dmz_target *dmz = ti->private;

	dm_put_device(ti, dmz->ddev);
	kfree(dmz->dev);
	dmz->dev = NULL;
}

/*
 * Setup target.
 */
static int dmz_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct dmz_target *dmz;
	struct dmz_dev *dev;
	int ret;

	/* Check arguments */
	if (argc != 1) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	/* Allocate and initialize the target descriptor */
	dmz = kzalloc(sizeof(struct dmz_target), GFP_KERNEL);
	if (!dmz) {
		ti->error = "Unable to allocate the zoned target descriptor";
		return -ENOMEM;
	}
	ti->private = dmz;

	/* Get the target zoned block device */
	ret = dmz_get_zoned_device(ti, argv[0]);
	if (ret) {
		dmz->ddev = NULL;
		goto err;
	}

	/* Initialize metadata */
	dev = dmz->dev;
	ret = dmz_ctr_metadata(dev, &dmz->metadata);
	if (ret) {
		ti->error = "Metadata initialization failed";
		goto err_dev;
	}

	/* Set target (no write same support) */
	ti->max_io_len = dev->zone_nr_sectors << 9;
	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_write_zeroes_bios = 1;
	ti->per_io_data_size = sizeof(struct dmz_bioctx);
	ti->flush_supported = true;
	ti->discards_supported = true;

	/* The exposed capacity is the number of chunks that can be mapped */
	ti->len = (sector_t)dmz_nr_chunks(dmz->metadata) << dev->zone_nr_sectors_shift;

	/* Zone BIO */
	ret = bioset_init(&dmz->bio_set, DMZ_MIN_BIOS, 0, 0);
	if (ret) {
		ti->error = "Create BIO set failed";
		goto err_meta;
	}

	/* Chunk BIO work */
	mutex_init(&dmz->chunk_lock);
	INIT_RADIX_TREE(&dmz->chunk_rxtree, GFP_NOIO);
	dmz->chunk_wq = alloc_workqueue("dmz_cwq_%s", WQ_MEM_RECLAIM | WQ_UNBOUND,
					0, dev->name);
	if (!dmz->chunk_wq) {
		ti->error = "Create chunk workqueue failed";
		ret = -ENOMEM;
		goto err_bio;
	}

	/* Flush work */
	spin_lock_init(&dmz->flush_lock);
	bio_list_init(&dmz->flush_list);
	INIT_DELAYED_WORK(&dmz->flush_work, dmz_flush_work);
	dmz->flush_wq = alloc_ordered_workqueue("dmz_fwq_%s", WQ_MEM_RECLAIM,
						dev->name);
	if (!dmz->flush_wq) {
		ti->error = "Create flush workqueue failed";
		ret = -ENOMEM;
		goto err_cwq;
	}
	mod_delayed_work(dmz->flush_wq, &dmz->flush_work, DMZ_FLUSH_PERIOD);

	/* Initialize reclaim */
	ret = dmz_ctr_reclaim(dev, dmz->metadata, &dmz->reclaim);
	if (ret) {
		ti->error = "Zone reclaim initialization failed";
		goto err_fwq;
	}

	dmz_dev_info(dev, "Target device: %llu 512-byte logical sectors (%llu blocks)",
		     (unsigned long long)ti->len,
		     (unsigned long long)dmz_sect2blk(ti->len));

	return 0;
err_fwq:
	destroy_workqueue(dmz->flush_wq);
err_cwq:
	destroy_workqueue(dmz->chunk_wq);
err_bio:
	mutex_destroy(&dmz->chunk_lock);
	bioset_exit(&dmz->bio_set);
err_meta:
	dmz_dtr_metadata(dmz->metadata);
err_dev:
	dmz_put_zoned_device(ti);
err:
	kfree(dmz);

	return ret;
}

/*
 * Cleanup target.
 */
static void dmz_dtr(struct dm_target *ti)
{
	struct dmz_target *dmz = ti->private;

	flush_workqueue(dmz->chunk_wq);
	destroy_workqueue(dmz->chunk_wq);

	dmz_dtr_reclaim(dmz->reclaim);

	cancel_delayed_work_sync(&dmz->flush_work);
	destroy_workqueue(dmz->flush_wq);

	(void) dmz_flush_metadata(dmz->metadata);

	dmz_dtr_metadata(dmz->metadata);

	bioset_exit(&dmz->bio_set);

	dmz_put_zoned_device(ti);

	mutex_destroy(&dmz->chunk_lock);

	kfree(dmz);
}

/*
 * Setup target request queue limits.
 */
static void dmz_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct dmz_target *dmz = ti->private;
	unsigned int chunk_sectors = dmz->dev->zone_nr_sectors;

	limits->logical_block_size = DMZ_BLOCK_SIZE;
	limits->physical_block_size = DMZ_BLOCK_SIZE;

	blk_limits_io_min(limits, DMZ_BLOCK_SIZE);
	blk_limits_io_opt(limits, DMZ_BLOCK_SIZE);

	limits->discard_alignment = DMZ_BLOCK_SIZE;
	limits->discard_granularity = DMZ_BLOCK_SIZE;
	limits->max_discard_sectors = chunk_sectors;
	limits->max_hw_discard_sectors = chunk_sectors;
	limits->max_write_zeroes_sectors = chunk_sectors;

	/* FS hint to try to align to the device zone size */
	limits->chunk_sectors = chunk_sectors;
	limits->max_sectors = chunk_sectors;

	/* We are exposing a drive-managed zoned block device */
	limits->zoned = BLK_ZONED_NONE;
}

/*
 * Pass on ioctl to the backend device.
 */
static int dmz_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct dmz_target *dmz = ti->private;

	if (!dmz_check_bdev(dmz->dev))
		return -EIO;

	*bdev = dmz->dev->bdev;

	return 0;
}

/*
 * Stop works on suspend.
 */
static void dmz_suspend(struct dm_target *ti)
{
	struct dmz_target *dmz = ti->private;

	flush_workqueue(dmz->chunk_wq);
	dmz_suspend_reclaim(dmz->reclaim);
	cancel_delayed_work_sync(&dmz->flush_work);
}

/*
 * Restart works on resume or if suspend failed.
 */
static void dmz_resume(struct dm_target *ti)
{
	struct dmz_target *dmz = ti->private;

	queue_delayed_work(dmz->flush_wq, &dmz->flush_work, DMZ_FLUSH_PERIOD);
	dmz_resume_reclaim(dmz->reclaim);
}

static int dmz_iterate_devices(struct dm_target *ti,
			       iterate_devices_callout_fn fn, void *data)
{
	struct dmz_target *dmz = ti->private;
	struct dmz_dev *dev = dmz->dev;
	sector_t capacity = dev->capacity & ~(dev->zone_nr_sectors - 1);

	return fn(ti, dmz->ddev, 0, capacity, data);
}

static struct target_type dmz_type = {
	.name		 = "zoned",
	.version	 = {1, 0, 0},
	.features	 = DM_TARGET_SINGLETON | DM_TARGET_ZONED_HM,
	.module		 = THIS_MODULE,
	.ctr		 = dmz_ctr,
	.dtr		 = dmz_dtr,
	.map		 = dmz_map,
	.io_hints	 = dmz_io_hints,
	.prepare_ioctl	 = dmz_prepare_ioctl,
	.postsuspend	 = dmz_suspend,
	.resume		 = dmz_resume,
	.iterate_devices = dmz_iterate_devices,
};

static int __init dmz_init(void)
{
	return dm_register_target(&dmz_type);
}

static void __exit dmz_exit(void)
{
	dm_unregister_target(&dmz_type);
}

module_init(dmz_init);
module_exit(dmz_exit);

MODULE_DESCRIPTION(DM_NAME " target for zoned block devices");
MODULE_AUTHOR("Damien Le Moal <damien.lemoal@wdc.com>");
MODULE_LICENSE("GPL");
