/*
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#ifndef DM_SNAPSHOT_H
#define DM_SNAPSHOT_H

#include <linux/device-mapper.h>
#include "dm-exception-store.h"
#include "dm-bio-list.h"
#include <linux/blkdev.h>
#include <linux/workqueue.h>

struct exception_table {
	uint32_t hash_mask;
	unsigned hash_shift;
	struct list_head *table;
};

#define DM_TRACKED_CHUNK_HASH_SIZE	16
#define DM_TRACKED_CHUNK_HASH(x)	((unsigned long)(x) & \
					 (DM_TRACKED_CHUNK_HASH_SIZE - 1))

struct dm_snapshot {
	struct rw_semaphore lock;
	struct dm_target *ti;

	struct dm_dev *origin;
	struct dm_dev *cow;

	/* List of snapshots per Origin */
	struct list_head list;

	/* Size of data blocks saved - must be a power of 2 */
	chunk_t chunk_size;
	chunk_t chunk_mask;
	chunk_t chunk_shift;

	/* You can't use a snapshot if this is 0 (e.g. if full) */
	int valid;

	/* Origin writes don't trigger exceptions until this is set */
	int active;

	/* Used for display of table */
	char type;

	mempool_t *pending_pool;

	atomic_t pending_exceptions_count;

	struct exception_table pending;
	struct exception_table complete;

	/*
	 * pe_lock protects all pending_exception operations and access
	 * as well as the snapshot_bios list.
	 */
	spinlock_t pe_lock;

	/* The on disk metadata handler */
	struct dm_exception_store store;

	struct dm_kcopyd_client *kcopyd_client;

	/* Queue of snapshot writes for ksnapd to flush */
	struct bio_list queued_bios;
	struct work_struct queued_bios_work;

	/* Chunks with outstanding reads */
	mempool_t *tracked_chunk_pool;
	spinlock_t tracked_chunk_lock;
	struct hlist_head tracked_chunk_hash[DM_TRACKED_CHUNK_HASH_SIZE];
};

/*
 * Return the number of sectors in the device.
 */
static inline sector_t get_dev_size(struct block_device *bdev)
{
	return bdev->bd_inode->i_size >> SECTOR_SHIFT;
}

static inline chunk_t sector_to_chunk(struct dm_snapshot *s, sector_t sector)
{
	return (sector & ~s->chunk_mask) >> s->chunk_shift;
}

static inline sector_t chunk_to_sector(struct dm_snapshot *s, chunk_t chunk)
{
	return chunk << s->chunk_shift;
}

static inline int bdev_equal(struct block_device *lhs, struct block_device *rhs)
{
	/*
	 * There is only ever one instance of a particular block
	 * device so we can compare pointers safely.
	 */
	return lhs == rhs;
}

#endif
