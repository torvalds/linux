/*
 * dm-snapshot.c
 *
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#ifndef DM_SNAPSHOT_H
#define DM_SNAPSHOT_H

#include "dm.h"
#include <linux/blkdev.h>

struct exception_table {
	uint32_t hash_mask;
	struct list_head *table;
};

/*
 * The snapshot code deals with largish chunks of the disk at a
 * time. Typically 64k - 256k.
 */
/* FIXME: can we get away with limiting these to a uint32_t ? */
typedef sector_t chunk_t;

/*
 * An exception is used where an old chunk of data has been
 * replaced by a new one.
 */
struct exception {
	struct list_head hash_list;

	chunk_t old_chunk;
	chunk_t new_chunk;
};

/*
 * Abstraction to handle the meta/layout of exception stores (the
 * COW device).
 */
struct exception_store {

	/*
	 * Destroys this object when you've finished with it.
	 */
	void (*destroy) (struct exception_store *store);

	/*
	 * The target shouldn't read the COW device until this is
	 * called.
	 */
	int (*read_metadata) (struct exception_store *store);

	/*
	 * Find somewhere to store the next exception.
	 */
	int (*prepare_exception) (struct exception_store *store,
				  struct exception *e);

	/*
	 * Update the metadata with this exception.
	 */
	void (*commit_exception) (struct exception_store *store,
				  struct exception *e,
				  void (*callback) (void *, int success),
				  void *callback_context);

	/*
	 * The snapshot is invalid, note this in the metadata.
	 */
	void (*drop_snapshot) (struct exception_store *store);

	/*
	 * Return how full the snapshot is.
	 */
	void (*fraction_full) (struct exception_store *store,
			       sector_t *numerator,
			       sector_t *denominator);

	struct dm_snapshot *snap;
	void *context;
};

struct dm_snapshot {
	struct rw_semaphore lock;
	struct dm_table *table;

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
	int have_metadata;

	/* Used for display of table */
	char type;

	/* The last percentage we notified */
	int last_percent;

	struct exception_table pending;
	struct exception_table complete;

	/* The on disk metadata handler */
	struct exception_store store;

	struct kcopyd_client *kcopyd_client;
};

/*
 * Used by the exception stores to load exceptions hen
 * initialising.
 */
int dm_add_exception(struct dm_snapshot *s, chunk_t old, chunk_t new);

/*
 * Constructor and destructor for the default persistent
 * store.
 */
int dm_create_persistent(struct exception_store *store, uint32_t chunk_size);

int dm_create_transient(struct exception_store *store,
			struct dm_snapshot *s, int blocksize);

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
