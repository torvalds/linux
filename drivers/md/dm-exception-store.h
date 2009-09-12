/*
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 * Copyright (C) 2008 Red Hat, Inc. All rights reserved.
 *
 * Device-mapper snapshot exception store.
 *
 * This file is released under the GPL.
 */

#ifndef _LINUX_DM_EXCEPTION_STORE
#define _LINUX_DM_EXCEPTION_STORE

#include <linux/blkdev.h>
#include <linux/device-mapper.h>

/*
 * The snapshot code deals with largish chunks of the disk at a
 * time. Typically 32k - 512k.
 */
typedef sector_t chunk_t;

/*
 * An exception is used where an old chunk of data has been
 * replaced by a new one.
 * If chunk_t is 64 bits in size, the top 8 bits of new_chunk hold the number
 * of chunks that follow contiguously.  Remaining bits hold the number of the
 * chunk within the device.
 */
struct dm_snap_exception {
	struct list_head hash_list;

	chunk_t old_chunk;
	chunk_t new_chunk;
};

/*
 * Abstraction to handle the meta/layout of exception stores (the
 * COW device).
 */
struct dm_exception_store;
struct dm_exception_store_type {
	const char *name;
	struct module *module;

	int (*ctr) (struct dm_exception_store *store,
		    unsigned argc, char **argv);

	/*
	 * Destroys this object when you've finished with it.
	 */
	void (*dtr) (struct dm_exception_store *store);

	/*
	 * The target shouldn't read the COW device until this is
	 * called.  As exceptions are read from the COW, they are
	 * reported back via the callback.
	 */
	int (*read_metadata) (struct dm_exception_store *store,
			      int (*callback)(void *callback_context,
					      chunk_t old, chunk_t new),
			      void *callback_context);

	/*
	 * Find somewhere to store the next exception.
	 */
	int (*prepare_exception) (struct dm_exception_store *store,
				  struct dm_snap_exception *e);

	/*
	 * Update the metadata with this exception.
	 */
	void (*commit_exception) (struct dm_exception_store *store,
				  struct dm_snap_exception *e,
				  void (*callback) (void *, int success),
				  void *callback_context);

	/*
	 * The snapshot is invalid, note this in the metadata.
	 */
	void (*drop_snapshot) (struct dm_exception_store *store);

	unsigned (*status) (struct dm_exception_store *store,
			    status_type_t status, char *result,
			    unsigned maxlen);

	/*
	 * Return how full the snapshot is.
	 */
	void (*fraction_full) (struct dm_exception_store *store,
			       sector_t *numerator,
			       sector_t *denominator);

	/* For internal device-mapper use only. */
	struct list_head list;
};

struct dm_exception_store {
	struct dm_exception_store_type *type;
	struct dm_target *ti;

	struct dm_dev *cow;

	/* Size of data blocks saved - must be a power of 2 */
	chunk_t chunk_size;
	chunk_t chunk_mask;
	chunk_t chunk_shift;

	void *context;
};

/*
 * Funtions to manipulate consecutive chunks
 */
#  if defined(CONFIG_LBDAF) || (BITS_PER_LONG == 64)
#    define DM_CHUNK_CONSECUTIVE_BITS 8
#    define DM_CHUNK_NUMBER_BITS 56

static inline chunk_t dm_chunk_number(chunk_t chunk)
{
	return chunk & (chunk_t)((1ULL << DM_CHUNK_NUMBER_BITS) - 1ULL);
}

static inline unsigned dm_consecutive_chunk_count(struct dm_snap_exception *e)
{
	return e->new_chunk >> DM_CHUNK_NUMBER_BITS;
}

static inline void dm_consecutive_chunk_count_inc(struct dm_snap_exception *e)
{
	e->new_chunk += (1ULL << DM_CHUNK_NUMBER_BITS);

	BUG_ON(!dm_consecutive_chunk_count(e));
}

#  else
#    define DM_CHUNK_CONSECUTIVE_BITS 0

static inline chunk_t dm_chunk_number(chunk_t chunk)
{
	return chunk;
}

static inline unsigned dm_consecutive_chunk_count(struct dm_snap_exception *e)
{
	return 0;
}

static inline void dm_consecutive_chunk_count_inc(struct dm_snap_exception *e)
{
}

#  endif

/*
 * Return the number of sectors in the device.
 */
static inline sector_t get_dev_size(struct block_device *bdev)
{
	return i_size_read(bdev->bd_inode) >> SECTOR_SHIFT;
}

static inline chunk_t sector_to_chunk(struct dm_exception_store *store,
				      sector_t sector)
{
	return (sector & ~store->chunk_mask) >> store->chunk_shift;
}

int dm_exception_store_type_register(struct dm_exception_store_type *type);
int dm_exception_store_type_unregister(struct dm_exception_store_type *type);

int dm_exception_store_set_chunk_size(struct dm_exception_store *store,
				      unsigned long chunk_size_ulong,
				      char **error);

int dm_exception_store_create(struct dm_target *ti, int argc, char **argv,
			      unsigned *args_used,
			      struct dm_exception_store **store);
void dm_exception_store_destroy(struct dm_exception_store *store);

int dm_exception_store_init(void);
void dm_exception_store_exit(void);

/*
 * Two exception store implementations.
 */
int dm_persistent_snapshot_init(void);
void dm_persistent_snapshot_exit(void);

int dm_transient_snapshot_init(void);
void dm_transient_snapshot_exit(void);

#endif /* _LINUX_DM_EXCEPTION_STORE */
