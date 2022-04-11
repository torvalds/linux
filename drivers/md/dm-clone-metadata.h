/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019 Arrikto, Inc. All Rights Reserved.
 */

#ifndef DM_CLONE_METADATA_H
#define DM_CLONE_METADATA_H

#include "persistent-data/dm-block-manager.h"
#include "persistent-data/dm-space-map-metadata.h"

#define DM_CLONE_METADATA_BLOCK_SIZE DM_SM_METADATA_BLOCK_SIZE

/*
 * The metadata device is currently limited in size.
 */
#define DM_CLONE_METADATA_MAX_SECTORS DM_SM_METADATA_MAX_SECTORS

/*
 * A metadata device larger than 16GB triggers a warning.
 */
#define DM_CLONE_METADATA_MAX_SECTORS_WARNING (16 * (1024 * 1024 * 1024 >> SECTOR_SHIFT))

#define SPACE_MAP_ROOT_SIZE 128

/* dm-clone metadata */
struct dm_clone_metadata;

/*
 * Set region status to hydrated.
 *
 * @cmd: The dm-clone metadata
 * @region_nr: The region number
 *
 * This function doesn't block, so it's safe to call it from interrupt context.
 */
int dm_clone_set_region_hydrated(struct dm_clone_metadata *cmd, unsigned long region_nr);

/*
 * Set status of all regions in the provided range to hydrated, if not already
 * hydrated.
 *
 * @cmd: The dm-clone metadata
 * @start: Starting region number
 * @nr_regions: Number of regions in the range
 *
 * This function doesn't block, so it's safe to call it from interrupt context.
 */
int dm_clone_cond_set_range(struct dm_clone_metadata *cmd, unsigned long start,
			    unsigned long nr_regions);

/*
 * Read existing or create fresh metadata.
 *
 * @bdev: The device storing the metadata
 * @target_size: The target size
 * @region_size: The region size
 *
 * @returns: The dm-clone metadata
 *
 * This function reads the superblock of @bdev and checks if it's all zeroes.
 * If it is, it formats @bdev and creates fresh metadata. If it isn't, it
 * validates the metadata stored in @bdev.
 */
struct dm_clone_metadata *dm_clone_metadata_open(struct block_device *bdev,
						 sector_t target_size,
						 sector_t region_size);

/*
 * Free the resources related to metadata management.
 */
void dm_clone_metadata_close(struct dm_clone_metadata *cmd);

/*
 * Commit dm-clone metadata to disk.
 */
int dm_clone_metadata_commit(struct dm_clone_metadata *cmd);

/*
 * Reload the in core copy of the on-disk bitmap.
 *
 * This should be used after aborting a metadata transaction and setting the
 * metadata to read-only, to invalidate the in-core cache and make it match the
 * on-disk metadata.
 *
 * WARNING: It must not be called concurrently with either
 * dm_clone_set_region_hydrated() or dm_clone_cond_set_range(), as it updates
 * the region bitmap without taking the relevant spinlock. We don't take the
 * spinlock because dm_clone_reload_in_core_bitset() does I/O, so it may block.
 *
 * But, it's safe to use it after calling dm_clone_metadata_set_read_only(),
 * because the latter sets the metadata to read-only mode. Both
 * dm_clone_set_region_hydrated() and dm_clone_cond_set_range() refuse to touch
 * the region bitmap, after calling dm_clone_metadata_set_read_only().
 */
int dm_clone_reload_in_core_bitset(struct dm_clone_metadata *cmd);

/*
 * Check whether dm-clone's metadata changed this transaction.
 */
bool dm_clone_changed_this_transaction(struct dm_clone_metadata *cmd);

/*
 * Abort current metadata transaction and rollback metadata to the last
 * committed transaction.
 */
int dm_clone_metadata_abort(struct dm_clone_metadata *cmd);

/*
 * Switches metadata to a read only mode. Once read-only mode has been entered
 * the following functions will return -EPERM:
 *
 *   dm_clone_metadata_commit()
 *   dm_clone_set_region_hydrated()
 *   dm_clone_cond_set_range()
 *   dm_clone_metadata_abort()
 */
void dm_clone_metadata_set_read_only(struct dm_clone_metadata *cmd);
void dm_clone_metadata_set_read_write(struct dm_clone_metadata *cmd);

/*
 * Returns true if the hydration of the destination device is finished.
 */
bool dm_clone_is_hydration_done(struct dm_clone_metadata *cmd);

/*
 * Returns true if region @region_nr is hydrated.
 */
bool dm_clone_is_region_hydrated(struct dm_clone_metadata *cmd, unsigned long region_nr);

/*
 * Returns true if all the regions in the range are hydrated.
 */
bool dm_clone_is_range_hydrated(struct dm_clone_metadata *cmd,
				unsigned long start, unsigned long nr_regions);

/*
 * Returns the number of hydrated regions.
 */
unsigned long dm_clone_nr_of_hydrated_regions(struct dm_clone_metadata *cmd);

/*
 * Returns the first unhydrated region with region_nr >= @start
 */
unsigned long dm_clone_find_next_unhydrated_region(struct dm_clone_metadata *cmd,
						   unsigned long start);

/*
 * Get the number of free metadata blocks.
 */
int dm_clone_get_free_metadata_block_count(struct dm_clone_metadata *cmd, dm_block_t *result);

/*
 * Get the total number of metadata blocks.
 */
int dm_clone_get_metadata_dev_size(struct dm_clone_metadata *cmd, dm_block_t *result);

#endif /* DM_CLONE_METADATA_H */
