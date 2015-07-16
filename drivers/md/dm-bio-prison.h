/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */

#ifndef DM_BIO_PRISON_H
#define DM_BIO_PRISON_H

#include "persistent-data/dm-block-manager.h" /* FIXME: for dm_block_t */
#include "dm-thin-metadata.h" /* FIXME: for dm_thin_id */

#include <linux/bio.h>
#include <linux/rbtree.h>

/*----------------------------------------------------------------*/

/*
 * Sometimes we can't deal with a bio straight away.  We put them in prison
 * where they can't cause any mischief.  Bios are put in a cell identified
 * by a key, multiple bios can be in the same cell.  When the cell is
 * subsequently unlocked the bios become available.
 */
struct dm_bio_prison;

/*
 * Keys define a range of blocks within either a virtual or physical
 * device.
 */
struct dm_cell_key {
	int virtual;
	dm_thin_id dev;
	dm_block_t block_begin, block_end;
};

/*
 * Treat this as opaque, only in header so callers can manage allocation
 * themselves.
 */
struct dm_bio_prison_cell {
	struct list_head user_list;	/* for client use */
	struct rb_node node;

	struct dm_cell_key key;
	struct bio *holder;
	struct bio_list bios;
};

struct dm_bio_prison *dm_bio_prison_create(void);
void dm_bio_prison_destroy(struct dm_bio_prison *prison);

/*
 * These two functions just wrap a mempool.  This is a transitory step:
 * Eventually all bio prison clients should manage their own cell memory.
 *
 * Like mempool_alloc(), dm_bio_prison_alloc_cell() can only fail if called
 * in interrupt context or passed GFP_NOWAIT.
 */
struct dm_bio_prison_cell *dm_bio_prison_alloc_cell(struct dm_bio_prison *prison,
						    gfp_t gfp);
void dm_bio_prison_free_cell(struct dm_bio_prison *prison,
			     struct dm_bio_prison_cell *cell);

/*
 * Creates, or retrieves a cell that overlaps the given key.
 *
 * Returns 1 if pre-existing cell returned, zero if new cell created using
 * @cell_prealloc.
 */
int dm_get_cell(struct dm_bio_prison *prison,
		struct dm_cell_key *key,
		struct dm_bio_prison_cell *cell_prealloc,
		struct dm_bio_prison_cell **cell_result);

/*
 * An atomic op that combines retrieving or creating a cell, and adding a
 * bio to it.
 *
 * Returns 1 if the cell was already held, 0 if @inmate is the new holder.
 */
int dm_bio_detain(struct dm_bio_prison *prison,
		  struct dm_cell_key *key,
		  struct bio *inmate,
		  struct dm_bio_prison_cell *cell_prealloc,
		  struct dm_bio_prison_cell **cell_result);

void dm_cell_release(struct dm_bio_prison *prison,
		     struct dm_bio_prison_cell *cell,
		     struct bio_list *bios);
void dm_cell_release_no_holder(struct dm_bio_prison *prison,
			       struct dm_bio_prison_cell *cell,
			       struct bio_list *inmates);
void dm_cell_error(struct dm_bio_prison *prison,
		   struct dm_bio_prison_cell *cell, int error);

/*
 * Visits the cell and then releases.  Guarantees no new inmates are
 * inserted between the visit and release.
 */
void dm_cell_visit_release(struct dm_bio_prison *prison,
			   void (*visit_fn)(void *, struct dm_bio_prison_cell *),
			   void *context, struct dm_bio_prison_cell *cell);

/*
 * Rather than always releasing the prisoners in a cell, the client may
 * want to promote one of them to be the new holder.  There is a race here
 * though between releasing an empty cell, and other threads adding new
 * inmates.  So this function makes the decision with its lock held.
 *
 * This function can have two outcomes:
 * i) An inmate is promoted to be the holder of the cell (return value of 0).
 * ii) The cell has no inmate for promotion and is released (return value of 1).
 */
int dm_cell_promote_or_release(struct dm_bio_prison *prison,
			       struct dm_bio_prison_cell *cell);

/*----------------------------------------------------------------*/

/*
 * We use the deferred set to keep track of pending reads to shared blocks.
 * We do this to ensure the new mapping caused by a write isn't performed
 * until these prior reads have completed.  Otherwise the insertion of the
 * new mapping could free the old block that the read bios are mapped to.
 */

struct dm_deferred_set;
struct dm_deferred_entry;

struct dm_deferred_set *dm_deferred_set_create(void);
void dm_deferred_set_destroy(struct dm_deferred_set *ds);

struct dm_deferred_entry *dm_deferred_entry_inc(struct dm_deferred_set *ds);
void dm_deferred_entry_dec(struct dm_deferred_entry *entry, struct list_head *head);
int dm_deferred_set_add_work(struct dm_deferred_set *ds, struct list_head *work);

/*----------------------------------------------------------------*/

#endif
