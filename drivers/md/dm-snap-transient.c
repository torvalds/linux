/*
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 * Copyright (C) 2006-2008 Red Hat GmbH
 *
 * This file is released under the GPL.
 */

#include "dm-exception-store.h"
#include "dm-snap.h"

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/dm-io.h>

#define DM_MSG_PREFIX "transient snapshot"

/*-----------------------------------------------------------------
 * Implementation of the store for non-persistent snapshots.
 *---------------------------------------------------------------*/
struct transient_c {
	sector_t next_free;
};

static void transient_destroy(struct dm_exception_store *store)
{
	kfree(store->context);
}

static int transient_read_metadata(struct dm_exception_store *store,
				   int (*callback)(void *callback_context,
						   chunk_t old, chunk_t new),
				   void *callback_context)
{
	return 0;
}

static int transient_prepare_exception(struct dm_exception_store *store,
				       struct dm_snap_exception *e)
{
	struct transient_c *tc = (struct transient_c *) store->context;
	sector_t size = get_dev_size(store->snap->cow->bdev);

	if (size < (tc->next_free + store->snap->chunk_size))
		return -1;

	e->new_chunk = sector_to_chunk(store->snap, tc->next_free);
	tc->next_free += store->snap->chunk_size;

	return 0;
}

static void transient_commit_exception(struct dm_exception_store *store,
				       struct dm_snap_exception *e,
				       void (*callback) (void *, int success),
				       void *callback_context)
{
	/* Just succeed */
	callback(callback_context, 1);
}

static void transient_fraction_full(struct dm_exception_store *store,
				    sector_t *numerator, sector_t *denominator)
{
	*numerator = ((struct transient_c *) store->context)->next_free;
	*denominator = get_dev_size(store->snap->cow->bdev);
}

int dm_create_transient(struct dm_exception_store *store)
{
	struct transient_c *tc;

	store->destroy = transient_destroy;
	store->read_metadata = transient_read_metadata;
	store->prepare_exception = transient_prepare_exception;
	store->commit_exception = transient_commit_exception;
	store->drop_snapshot = NULL;
	store->fraction_full = transient_fraction_full;

	tc = kmalloc(sizeof(struct transient_c), GFP_KERNEL);
	if (!tc)
		return -ENOMEM;

	tc->next_free = 0;
	store->context = tc;

	return 0;
}

int dm_transient_snapshot_init(void)
{
	return 0;
}

void dm_transient_snapshot_exit(void)
{
}
