/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011-2017 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef DM_BIO_PRISON_V2_H
#define DM_BIO_PRISON_V2_H

#include "persistent-data/dm-block-manager.h" /* FIXME: for dm_block_t */
#include "dm-thin-metadata.h" /* FIXME: for dm_thin_id */

#include <linux/bio.h>
#include <linux/rbtree.h>
#include <linux/workqueue.h>

/*----------------------------------------------------------------*/

int dm_bio_prison_init_v2(void);
void dm_bio_prison_exit_v2(void);

/*
 * Sometimes we can't deal with a bio straight away.  We put them in prison
 * where they can't cause any mischief.  Bios are put in a cell identified
 * by a key, multiple bios can be in the same cell.  When the cell is
 * subsequently unlocked the bios become available.
 */
struct dm_bio_prison_v2;

/*
 * Keys define a range of blocks within either a virtual or physical
 * device.
 */
struct dm_cell_key_v2 {
	int virtual;
	dm_thin_id dev;
	dm_block_t block_begin, block_end;
};

/*
 * Treat this as opaque, only in header so callers can manage allocation
 * themselves.
 */
struct dm_bio_prison_cell_v2 {
	// FIXME: pack these
	bool exclusive_lock;
	unsigned int exclusive_level;
	unsigned int shared_count;
	struct work_struct *quiesce_continuation;

	struct rb_node node;
	struct dm_cell_key_v2 key;
	struct bio_list bios;
};

struct dm_bio_prison_v2 *dm_bio_prison_create_v2(struct workqueue_struct *wq);
void dm_bio_prison_destroy_v2(struct dm_bio_prison_v2 *prison);

/*
 * These two functions just wrap a mempool.  This is a transitory step:
 * Eventually all bio prison clients should manage their own cell memory.
 *
 * Like mempool_alloc(), dm_bio_prison_alloc_cell_v2() can only fail if called
 * in interrupt context or passed GFP_NOWAIT.
 */
struct dm_bio_prison_cell_v2 *dm_bio_prison_alloc_cell_v2(struct dm_bio_prison_v2 *prison,
						    gfp_t gfp);
void dm_bio_prison_free_cell_v2(struct dm_bio_prison_v2 *prison,
				struct dm_bio_prison_cell_v2 *cell);

/*
 * Shared locks have a bio associated with them.
 *
 * If the lock is granted the caller can continue to use the bio, and must
 * call dm_cell_put_v2() to drop the reference count when finished using it.
 *
 * If the lock cannot be granted then the bio will be tracked within the
 * cell, and later given to the holder of the exclusive lock.
 *
 * See dm_cell_lock_v2() for discussion of the lock_level parameter.
 *
 * Compare *cell_result with cell_prealloc to see if the prealloc was used.
 * If cell_prealloc was used then inmate wasn't added to it.
 *
 * Returns true if the lock is granted.
 */
bool dm_cell_get_v2(struct dm_bio_prison_v2 *prison,
		    struct dm_cell_key_v2 *key,
		    unsigned int lock_level,
		    struct bio *inmate,
		    struct dm_bio_prison_cell_v2 *cell_prealloc,
		    struct dm_bio_prison_cell_v2 **cell_result);

/*
 * Decrement the shared reference count for the lock.  Returns true if
 * returning ownership of the cell (ie. you should free it).
 */
bool dm_cell_put_v2(struct dm_bio_prison_v2 *prison,
		    struct dm_bio_prison_cell_v2 *cell);

/*
 * Locks a cell.  No associated bio.  Exclusive locks get priority.  These
 * locks constrain whether the io locks are granted according to level.
 *
 * Shared locks will still be granted if the lock_level is > (not = to) the
 * exclusive lock level.
 *
 * If an _exclusive_ lock is already held then -EBUSY is returned.
 *
 * Return values:
 *  < 0 - error
 *  0   - locked; no quiescing needed
 *  1   - locked; quiescing needed
 */
int dm_cell_lock_v2(struct dm_bio_prison_v2 *prison,
		    struct dm_cell_key_v2 *key,
		    unsigned int lock_level,
		    struct dm_bio_prison_cell_v2 *cell_prealloc,
		    struct dm_bio_prison_cell_v2 **cell_result);

void dm_cell_quiesce_v2(struct dm_bio_prison_v2 *prison,
			struct dm_bio_prison_cell_v2 *cell,
			struct work_struct *continuation);

/*
 * Promotes an _exclusive_ lock to a higher lock level.
 *
 * Return values:
 *  < 0 - error
 *  0   - promoted; no quiescing needed
 *  1   - promoted; quiescing needed
 */
int dm_cell_lock_promote_v2(struct dm_bio_prison_v2 *prison,
			    struct dm_bio_prison_cell_v2 *cell,
			    unsigned int new_lock_level);

/*
 * Adds any held bios to the bio list.
 *
 * There may be shared locks still held at this point even if you quiesced
 * (ie. different lock levels).
 *
 * Returns true if returning ownership of the cell (ie. you should free
 * it).
 */
bool dm_cell_unlock_v2(struct dm_bio_prison_v2 *prison,
		       struct dm_bio_prison_cell_v2 *cell,
		       struct bio_list *bios);

/*----------------------------------------------------------------*/

#endif
