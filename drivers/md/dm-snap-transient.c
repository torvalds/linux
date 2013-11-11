/*
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 * Copyright (C) 2006-2008 Red Hat GmbH
 *
 * This file is released under the GPL.
 */

#include "dm-exception-store.h"

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/dm-io.h>

#define DM_MSG_PREFIX "transient snapshot"

/*-----------------------------------------------------------------
 * Implementation of the store for non-persistent snapshots.
 *---------------------------------------------------------------*/
struct transient_c {
	sector_t next_free;
};

static void transient_dtr(struct dm_exception_store *store)
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
				       struct dm_exception *e)
{
	struct transient_c *tc = store->context;
	sector_t size = get_dev_size(dm_snap_cow(store->snap)->bdev);

	if (size < (tc->next_free + store->chunk_size))
		return -1;

	e->new_chunk = sector_to_chunk(store, tc->next_free);
	tc->next_free += store->chunk_size;

	return 0;
}

static void transient_commit_exception(struct dm_exception_store *store,
				       struct dm_exception *e,
				       void (*callback) (void *, int success),
				       void *callback_context)
{
	/* Just succeed */
	callback(callback_context, 1);
}

static void transient_usage(struct dm_exception_store *store,
			    sector_t *total_sectors,
			    sector_t *sectors_allocated,
			    sector_t *metadata_sectors)
{
	*sectors_allocated = ((struct transient_c *) store->context)->next_free;
	*total_sectors = get_dev_size(dm_snap_cow(store->snap)->bdev);
	*metadata_sectors = 0;
}

static int transient_ctr(struct dm_exception_store *store,
			 unsigned argc, char **argv)
{
	struct transient_c *tc;

	tc = kmalloc(sizeof(struct transient_c), GFP_KERNEL);
	if (!tc)
		return -ENOMEM;

	tc->next_free = 0;
	store->context = tc;

	return 0;
}

static unsigned transient_status(struct dm_exception_store *store,
				 status_type_t status, char *result,
				 unsigned maxlen)
{
	unsigned sz = 0;

	switch (status) {
	case STATUSTYPE_INFO:
		break;
	case STATUSTYPE_TABLE:
		DMEMIT(" N %llu", (unsigned long long)store->chunk_size);
	}

	return sz;
}

static struct dm_exception_store_type _transient_type = {
	.name = "transient",
	.module = THIS_MODULE,
	.ctr = transient_ctr,
	.dtr = transient_dtr,
	.read_metadata = transient_read_metadata,
	.prepare_exception = transient_prepare_exception,
	.commit_exception = transient_commit_exception,
	.usage = transient_usage,
	.status = transient_status,
};

static struct dm_exception_store_type _transient_compat_type = {
	.name = "N",
	.module = THIS_MODULE,
	.ctr = transient_ctr,
	.dtr = transient_dtr,
	.read_metadata = transient_read_metadata,
	.prepare_exception = transient_prepare_exception,
	.commit_exception = transient_commit_exception,
	.usage = transient_usage,
	.status = transient_status,
};

int dm_transient_snapshot_init(void)
{
	int r;

	r = dm_exception_store_type_register(&_transient_type);
	if (r) {
		DMWARN("Unable to register transient exception store type");
		return r;
	}

	r = dm_exception_store_type_register(&_transient_compat_type);
	if (r) {
		DMWARN("Unable to register old-style transient "
		       "exception store type");
		dm_exception_store_type_unregister(&_transient_type);
		return r;
	}

	return r;
}

void dm_transient_snapshot_exit(void)
{
	dm_exception_store_type_unregister(&_transient_type);
	dm_exception_store_type_unregister(&_transient_compat_type);
}
