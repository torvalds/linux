/*
 * dm-snapshot.c
 *
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-snap.h"
#include "dm-io.h"
#include "kcopyd.h"

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

/*-----------------------------------------------------------------
 * Persistent snapshots, by persistent we mean that the snapshot
 * will survive a reboot.
 *---------------------------------------------------------------*/

/*
 * We need to store a record of which parts of the origin have
 * been copied to the snapshot device.  The snapshot code
 * requires that we copy exception chunks to chunk aligned areas
 * of the COW store.  It makes sense therefore, to store the
 * metadata in chunk size blocks.
 *
 * There is no backward or forward compatibility implemented,
 * snapshots with different disk versions than the kernel will
 * not be usable.  It is expected that "lvcreate" will blank out
 * the start of a fresh COW device before calling the snapshot
 * constructor.
 *
 * The first chunk of the COW device just contains the header.
 * After this there is a chunk filled with exception metadata,
 * followed by as many exception chunks as can fit in the
 * metadata areas.
 *
 * All on disk structures are in little-endian format.  The end
 * of the exceptions info is indicated by an exception with a
 * new_chunk of 0, which is invalid since it would point to the
 * header chunk.
 */

/*
 * Magic for persistent snapshots: "SnAp" - Feeble isn't it.
 */
#define SNAP_MAGIC 0x70416e53

/*
 * The on-disk version of the metadata.
 */
#define SNAPSHOT_DISK_VERSION 1

struct disk_header {
	uint32_t magic;

	/*
	 * Is this snapshot valid.  There is no way of recovering
	 * an invalid snapshot.
	 */
	uint32_t valid;

	/*
	 * Simple, incrementing version. no backward
	 * compatibility.
	 */
	uint32_t version;

	/* In sectors */
	uint32_t chunk_size;
};

struct disk_exception {
	uint64_t old_chunk;
	uint64_t new_chunk;
};

struct commit_callback {
	void (*callback)(void *, int success);
	void *context;
};

/*
 * The top level structure for a persistent exception store.
 */
struct pstore {
	struct dm_snapshot *snap;	/* up pointer to my snapshot */
	int version;
	int valid;
	uint32_t chunk_size;
	uint32_t exceptions_per_area;

	/*
	 * Now that we have an asynchronous kcopyd there is no
	 * need for large chunk sizes, so it wont hurt to have a
	 * whole chunks worth of metadata in memory at once.
	 */
	void *area;

	/*
	 * Used to keep track of which metadata area the data in
	 * 'chunk' refers to.
	 */
	uint32_t current_area;

	/*
	 * The next free chunk for an exception.
	 */
	uint32_t next_free;

	/*
	 * The index of next free exception in the current
	 * metadata area.
	 */
	uint32_t current_committed;

	atomic_t pending_count;
	uint32_t callback_count;
	struct commit_callback *callbacks;
};

static inline unsigned int sectors_to_pages(unsigned int sectors)
{
	return sectors / (PAGE_SIZE >> 9);
}

static int alloc_area(struct pstore *ps)
{
	int r = -ENOMEM;
	size_t len;

	len = ps->chunk_size << SECTOR_SHIFT;

	/*
	 * Allocate the chunk_size block of memory that will hold
	 * a single metadata area.
	 */
	ps->area = vmalloc(len);
	if (!ps->area)
		return r;

	return 0;
}

static void free_area(struct pstore *ps)
{
	vfree(ps->area);
}

/*
 * Read or write a chunk aligned and sized block of data from a device.
 */
static int chunk_io(struct pstore *ps, uint32_t chunk, int rw)
{
	struct io_region where;
	unsigned long bits;

	where.bdev = ps->snap->cow->bdev;
	where.sector = ps->chunk_size * chunk;
	where.count = ps->chunk_size;

	return dm_io_sync_vm(1, &where, rw, ps->area, &bits);
}

/*
 * Read or write a metadata area.  Remembering to skip the first
 * chunk which holds the header.
 */
static int area_io(struct pstore *ps, uint32_t area, int rw)
{
	int r;
	uint32_t chunk;

	/* convert a metadata area index to a chunk index */
	chunk = 1 + ((ps->exceptions_per_area + 1) * area);

	r = chunk_io(ps, chunk, rw);
	if (r)
		return r;

	ps->current_area = area;
	return 0;
}

static int zero_area(struct pstore *ps, uint32_t area)
{
	memset(ps->area, 0, ps->chunk_size << SECTOR_SHIFT);
	return area_io(ps, area, WRITE);
}

static int read_header(struct pstore *ps, int *new_snapshot)
{
	int r;
	struct disk_header *dh;

	r = chunk_io(ps, 0, READ);
	if (r)
		return r;

	dh = (struct disk_header *) ps->area;

	if (le32_to_cpu(dh->magic) == 0) {
		*new_snapshot = 1;

	} else if (le32_to_cpu(dh->magic) == SNAP_MAGIC) {
		*new_snapshot = 0;
		ps->valid = le32_to_cpu(dh->valid);
		ps->version = le32_to_cpu(dh->version);
		ps->chunk_size = le32_to_cpu(dh->chunk_size);

	} else {
		DMWARN("Invalid/corrupt snapshot");
		r = -ENXIO;
	}

	return r;
}

static int write_header(struct pstore *ps)
{
	struct disk_header *dh;

	memset(ps->area, 0, ps->chunk_size << SECTOR_SHIFT);

	dh = (struct disk_header *) ps->area;
	dh->magic = cpu_to_le32(SNAP_MAGIC);
	dh->valid = cpu_to_le32(ps->valid);
	dh->version = cpu_to_le32(ps->version);
	dh->chunk_size = cpu_to_le32(ps->chunk_size);

	return chunk_io(ps, 0, WRITE);
}

/*
 * Access functions for the disk exceptions, these do the endian conversions.
 */
static struct disk_exception *get_exception(struct pstore *ps, uint32_t index)
{
	if (index >= ps->exceptions_per_area)
		return NULL;

	return ((struct disk_exception *) ps->area) + index;
}

static int read_exception(struct pstore *ps,
			  uint32_t index, struct disk_exception *result)
{
	struct disk_exception *e;

	e = get_exception(ps, index);
	if (!e)
		return -EINVAL;

	/* copy it */
	result->old_chunk = le64_to_cpu(e->old_chunk);
	result->new_chunk = le64_to_cpu(e->new_chunk);

	return 0;
}

static int write_exception(struct pstore *ps,
			   uint32_t index, struct disk_exception *de)
{
	struct disk_exception *e;

	e = get_exception(ps, index);
	if (!e)
		return -EINVAL;

	/* copy it */
	e->old_chunk = cpu_to_le64(de->old_chunk);
	e->new_chunk = cpu_to_le64(de->new_chunk);

	return 0;
}

/*
 * Registers the exceptions that are present in the current area.
 * 'full' is filled in to indicate if the area has been
 * filled.
 */
static int insert_exceptions(struct pstore *ps, int *full)
{
	int r;
	unsigned int i;
	struct disk_exception de;

	/* presume the area is full */
	*full = 1;

	for (i = 0; i < ps->exceptions_per_area; i++) {
		r = read_exception(ps, i, &de);

		if (r)
			return r;

		/*
		 * If the new_chunk is pointing at the start of
		 * the COW device, where the first metadata area
		 * is we know that we've hit the end of the
		 * exceptions.  Therefore the area is not full.
		 */
		if (de.new_chunk == 0LL) {
			ps->current_committed = i;
			*full = 0;
			break;
		}

		/*
		 * Keep track of the start of the free chunks.
		 */
		if (ps->next_free <= de.new_chunk)
			ps->next_free = de.new_chunk + 1;

		/*
		 * Otherwise we add the exception to the snapshot.
		 */
		r = dm_add_exception(ps->snap, de.old_chunk, de.new_chunk);
		if (r)
			return r;
	}

	return 0;
}

static int read_exceptions(struct pstore *ps)
{
	uint32_t area;
	int r, full = 1;

	/*
	 * Keeping reading chunks and inserting exceptions until
	 * we find a partially full area.
	 */
	for (area = 0; full; area++) {
		r = area_io(ps, area, READ);
		if (r)
			return r;

		r = insert_exceptions(ps, &full);
		if (r)
			return r;
	}

	return 0;
}

static inline struct pstore *get_info(struct exception_store *store)
{
	return (struct pstore *) store->context;
}

static void persistent_fraction_full(struct exception_store *store,
				     sector_t *numerator, sector_t *denominator)
{
	*numerator = get_info(store)->next_free * store->snap->chunk_size;
	*denominator = get_dev_size(store->snap->cow->bdev);
}

static void persistent_destroy(struct exception_store *store)
{
	struct pstore *ps = get_info(store);

	dm_io_put(sectors_to_pages(ps->chunk_size));
	vfree(ps->callbacks);
	free_area(ps);
	kfree(ps);
}

static int persistent_read_metadata(struct exception_store *store)
{
	int r, new_snapshot;
	struct pstore *ps = get_info(store);

	/*
	 * Read the snapshot header.
	 */
	r = read_header(ps, &new_snapshot);
	if (r)
		return r;

	/*
	 * Do we need to setup a new snapshot ?
	 */
	if (new_snapshot) {
		r = write_header(ps);
		if (r) {
			DMWARN("write_header failed");
			return r;
		}

		r = zero_area(ps, 0);
		if (r) {
			DMWARN("zero_area(0) failed");
			return r;
		}

	} else {
		/*
		 * Sanity checks.
		 */
		if (!ps->valid) {
			DMWARN("snapshot is marked invalid");
			return -EINVAL;
		}

		if (ps->version != SNAPSHOT_DISK_VERSION) {
			DMWARN("unable to handle snapshot disk version %d",
			       ps->version);
			return -EINVAL;
		}

		/*
		 * Read the metadata.
		 */
		r = read_exceptions(ps);
		if (r)
			return r;
	}

	return 0;
}

static int persistent_prepare(struct exception_store *store,
			      struct exception *e)
{
	struct pstore *ps = get_info(store);
	uint32_t stride;
	sector_t size = get_dev_size(store->snap->cow->bdev);

	/* Is there enough room ? */
	if (size < ((ps->next_free + 1) * store->snap->chunk_size))
		return -ENOSPC;

	e->new_chunk = ps->next_free;

	/*
	 * Move onto the next free pending, making sure to take
	 * into account the location of the metadata chunks.
	 */
	stride = (ps->exceptions_per_area + 1);
	if ((++ps->next_free % stride) == 1)
		ps->next_free++;

	atomic_inc(&ps->pending_count);
	return 0;
}

static void persistent_commit(struct exception_store *store,
			      struct exception *e,
			      void (*callback) (void *, int success),
			      void *callback_context)
{
	int r;
	unsigned int i;
	struct pstore *ps = get_info(store);
	struct disk_exception de;
	struct commit_callback *cb;

	de.old_chunk = e->old_chunk;
	de.new_chunk = e->new_chunk;
	write_exception(ps, ps->current_committed++, &de);

	/*
	 * Add the callback to the back of the array.  This code
	 * is the only place where the callback array is
	 * manipulated, and we know that it will never be called
	 * multiple times concurrently.
	 */
	cb = ps->callbacks + ps->callback_count++;
	cb->callback = callback;
	cb->context = callback_context;

	/*
	 * If there are no more exceptions in flight, or we have
	 * filled this metadata area we commit the exceptions to
	 * disk.
	 */
	if (atomic_dec_and_test(&ps->pending_count) ||
	    (ps->current_committed == ps->exceptions_per_area)) {
		r = area_io(ps, ps->current_area, WRITE);
		if (r)
			ps->valid = 0;

		for (i = 0; i < ps->callback_count; i++) {
			cb = ps->callbacks + i;
			cb->callback(cb->context, r == 0 ? 1 : 0);
		}

		ps->callback_count = 0;
	}

	/*
	 * Have we completely filled the current area ?
	 */
	if (ps->current_committed == ps->exceptions_per_area) {
		ps->current_committed = 0;
		r = zero_area(ps, ps->current_area + 1);
		if (r)
			ps->valid = 0;
	}
}

static void persistent_drop(struct exception_store *store)
{
	struct pstore *ps = get_info(store);

	ps->valid = 0;
	if (write_header(ps))
		DMWARN("write header failed");
}

int dm_create_persistent(struct exception_store *store, uint32_t chunk_size)
{
	int r;
	struct pstore *ps;

	r = dm_io_get(sectors_to_pages(chunk_size));
	if (r)
		return r;

	/* allocate the pstore */
	ps = kmalloc(sizeof(*ps), GFP_KERNEL);
	if (!ps) {
		r = -ENOMEM;
		goto bad;
	}

	ps->snap = store->snap;
	ps->valid = 1;
	ps->version = SNAPSHOT_DISK_VERSION;
	ps->chunk_size = chunk_size;
	ps->exceptions_per_area = (chunk_size << SECTOR_SHIFT) /
	    sizeof(struct disk_exception);
	ps->next_free = 2;	/* skipping the header and first area */
	ps->current_committed = 0;

	r = alloc_area(ps);
	if (r)
		goto bad;

	/*
	 * Allocate space for all the callbacks.
	 */
	ps->callback_count = 0;
	atomic_set(&ps->pending_count, 0);
	ps->callbacks = dm_vcalloc(ps->exceptions_per_area,
				   sizeof(*ps->callbacks));

	if (!ps->callbacks) {
		r = -ENOMEM;
		goto bad;
	}

	store->destroy = persistent_destroy;
	store->read_metadata = persistent_read_metadata;
	store->prepare_exception = persistent_prepare;
	store->commit_exception = persistent_commit;
	store->drop_snapshot = persistent_drop;
	store->fraction_full = persistent_fraction_full;
	store->context = ps;

	return 0;

      bad:
	dm_io_put(sectors_to_pages(chunk_size));
	if (ps && ps->area)
		free_area(ps);
	kfree(ps);
	return r;
}

/*-----------------------------------------------------------------
 * Implementation of the store for non-persistent snapshots.
 *---------------------------------------------------------------*/
struct transient_c {
	sector_t next_free;
};

static void transient_destroy(struct exception_store *store)
{
	kfree(store->context);
}

static int transient_read_metadata(struct exception_store *store)
{
	return 0;
}

static int transient_prepare(struct exception_store *store, struct exception *e)
{
	struct transient_c *tc = (struct transient_c *) store->context;
	sector_t size = get_dev_size(store->snap->cow->bdev);

	if (size < (tc->next_free + store->snap->chunk_size))
		return -1;

	e->new_chunk = sector_to_chunk(store->snap, tc->next_free);
	tc->next_free += store->snap->chunk_size;

	return 0;
}

static void transient_commit(struct exception_store *store,
		      struct exception *e,
		      void (*callback) (void *, int success),
		      void *callback_context)
{
	/* Just succeed */
	callback(callback_context, 1);
}

static void transient_fraction_full(struct exception_store *store,
				    sector_t *numerator, sector_t *denominator)
{
	*numerator = ((struct transient_c *) store->context)->next_free;
	*denominator = get_dev_size(store->snap->cow->bdev);
}

int dm_create_transient(struct exception_store *store,
			struct dm_snapshot *s, int blocksize)
{
	struct transient_c *tc;

	memset(store, 0, sizeof(*store));
	store->destroy = transient_destroy;
	store->read_metadata = transient_read_metadata;
	store->prepare_exception = transient_prepare;
	store->commit_exception = transient_commit;
	store->fraction_full = transient_fraction_full;
	store->snap = s;

	tc = kmalloc(sizeof(struct transient_c), GFP_KERNEL);
	if (!tc)
		return -ENOMEM;

	tc->next_free = 0;
	store->context = tc;

	return 0;
}
