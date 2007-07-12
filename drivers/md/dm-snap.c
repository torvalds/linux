/*
 * dm-snapshot.c
 *
 * Copyright (C) 2001-2002 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 */

#include <linux/blkdev.h>
#include <linux/ctype.h>
#include <linux/device-mapper.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/list.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "dm-snap.h"
#include "dm-bio-list.h"
#include "kcopyd.h"

#define DM_MSG_PREFIX "snapshots"

/*
 * The percentage increment we will wake up users at
 */
#define WAKE_UP_PERCENT 5

/*
 * kcopyd priority of snapshot operations
 */
#define SNAPSHOT_COPY_PRIORITY 2

/*
 * Each snapshot reserves this many pages for io
 */
#define SNAPSHOT_PAGES 256

static struct workqueue_struct *ksnapd;
static void flush_queued_bios(struct work_struct *work);

struct dm_snap_pending_exception {
	struct dm_snap_exception e;

	/*
	 * Origin buffers waiting for this to complete are held
	 * in a bio list
	 */
	struct bio_list origin_bios;
	struct bio_list snapshot_bios;

	/*
	 * Short-term queue of pending exceptions prior to submission.
	 */
	struct list_head list;

	/*
	 * The primary pending_exception is the one that holds
	 * the ref_count and the list of origin_bios for a
	 * group of pending_exceptions.  It is always last to get freed.
	 * These fields get set up when writing to the origin.
	 */
	struct dm_snap_pending_exception *primary_pe;

	/*
	 * Number of pending_exceptions processing this chunk.
	 * When this drops to zero we must complete the origin bios.
	 * If incrementing or decrementing this, hold pe->snap->lock for
	 * the sibling concerned and not pe->primary_pe->snap->lock unless
	 * they are the same.
	 */
	atomic_t ref_count;

	/* Pointer back to snapshot context */
	struct dm_snapshot *snap;

	/*
	 * 1 indicates the exception has already been sent to
	 * kcopyd.
	 */
	int started;
};

/*
 * Hash table mapping origin volumes to lists of snapshots and
 * a lock to protect it
 */
static struct kmem_cache *exception_cache;
static struct kmem_cache *pending_cache;
static mempool_t *pending_pool;

/*
 * One of these per registered origin, held in the snapshot_origins hash
 */
struct origin {
	/* The origin device */
	struct block_device *bdev;

	struct list_head hash_list;

	/* List of snapshots for this origin */
	struct list_head snapshots;
};

/*
 * Size of the hash table for origin volumes. If we make this
 * the size of the minors list then it should be nearly perfect
 */
#define ORIGIN_HASH_SIZE 256
#define ORIGIN_MASK      0xFF
static struct list_head *_origins;
static struct rw_semaphore _origins_lock;

static int init_origin_hash(void)
{
	int i;

	_origins = kmalloc(ORIGIN_HASH_SIZE * sizeof(struct list_head),
			   GFP_KERNEL);
	if (!_origins) {
		DMERR("unable to allocate memory");
		return -ENOMEM;
	}

	for (i = 0; i < ORIGIN_HASH_SIZE; i++)
		INIT_LIST_HEAD(_origins + i);
	init_rwsem(&_origins_lock);

	return 0;
}

static void exit_origin_hash(void)
{
	kfree(_origins);
}

static unsigned origin_hash(struct block_device *bdev)
{
	return bdev->bd_dev & ORIGIN_MASK;
}

static struct origin *__lookup_origin(struct block_device *origin)
{
	struct list_head *ol;
	struct origin *o;

	ol = &_origins[origin_hash(origin)];
	list_for_each_entry (o, ol, hash_list)
		if (bdev_equal(o->bdev, origin))
			return o;

	return NULL;
}

static void __insert_origin(struct origin *o)
{
	struct list_head *sl = &_origins[origin_hash(o->bdev)];
	list_add_tail(&o->hash_list, sl);
}

/*
 * Make a note of the snapshot and its origin so we can look it
 * up when the origin has a write on it.
 */
static int register_snapshot(struct dm_snapshot *snap)
{
	struct origin *o;
	struct block_device *bdev = snap->origin->bdev;

	down_write(&_origins_lock);
	o = __lookup_origin(bdev);

	if (!o) {
		/* New origin */
		o = kmalloc(sizeof(*o), GFP_KERNEL);
		if (!o) {
			up_write(&_origins_lock);
			return -ENOMEM;
		}

		/* Initialise the struct */
		INIT_LIST_HEAD(&o->snapshots);
		o->bdev = bdev;

		__insert_origin(o);
	}

	list_add_tail(&snap->list, &o->snapshots);

	up_write(&_origins_lock);
	return 0;
}

static void unregister_snapshot(struct dm_snapshot *s)
{
	struct origin *o;

	down_write(&_origins_lock);
	o = __lookup_origin(s->origin->bdev);

	list_del(&s->list);
	if (list_empty(&o->snapshots)) {
		list_del(&o->hash_list);
		kfree(o);
	}

	up_write(&_origins_lock);
}

/*
 * Implementation of the exception hash tables.
 */
static int init_exception_table(struct exception_table *et, uint32_t size)
{
	unsigned int i;

	et->hash_mask = size - 1;
	et->table = dm_vcalloc(size, sizeof(struct list_head));
	if (!et->table)
		return -ENOMEM;

	for (i = 0; i < size; i++)
		INIT_LIST_HEAD(et->table + i);

	return 0;
}

static void exit_exception_table(struct exception_table *et, struct kmem_cache *mem)
{
	struct list_head *slot;
	struct dm_snap_exception *ex, *next;
	int i, size;

	size = et->hash_mask + 1;
	for (i = 0; i < size; i++) {
		slot = et->table + i;

		list_for_each_entry_safe (ex, next, slot, hash_list)
			kmem_cache_free(mem, ex);
	}

	vfree(et->table);
}

static uint32_t exception_hash(struct exception_table *et, chunk_t chunk)
{
	return chunk & et->hash_mask;
}

static void insert_exception(struct exception_table *eh,
			     struct dm_snap_exception *e)
{
	struct list_head *l = &eh->table[exception_hash(eh, e->old_chunk)];
	list_add(&e->hash_list, l);
}

static void remove_exception(struct dm_snap_exception *e)
{
	list_del(&e->hash_list);
}

/*
 * Return the exception data for a sector, or NULL if not
 * remapped.
 */
static struct dm_snap_exception *lookup_exception(struct exception_table *et,
						  chunk_t chunk)
{
	struct list_head *slot;
	struct dm_snap_exception *e;

	slot = &et->table[exception_hash(et, chunk)];
	list_for_each_entry (e, slot, hash_list)
		if (e->old_chunk == chunk)
			return e;

	return NULL;
}

static struct dm_snap_exception *alloc_exception(void)
{
	struct dm_snap_exception *e;

	e = kmem_cache_alloc(exception_cache, GFP_NOIO);
	if (!e)
		e = kmem_cache_alloc(exception_cache, GFP_ATOMIC);

	return e;
}

static void free_exception(struct dm_snap_exception *e)
{
	kmem_cache_free(exception_cache, e);
}

static struct dm_snap_pending_exception *alloc_pending_exception(void)
{
	return mempool_alloc(pending_pool, GFP_NOIO);
}

static void free_pending_exception(struct dm_snap_pending_exception *pe)
{
	mempool_free(pe, pending_pool);
}

int dm_add_exception(struct dm_snapshot *s, chunk_t old, chunk_t new)
{
	struct dm_snap_exception *e;

	e = alloc_exception();
	if (!e)
		return -ENOMEM;

	e->old_chunk = old;
	e->new_chunk = new;
	insert_exception(&s->complete, e);
	return 0;
}

/*
 * Hard coded magic.
 */
static int calc_max_buckets(void)
{
	/* use a fixed size of 2MB */
	unsigned long mem = 2 * 1024 * 1024;
	mem /= sizeof(struct list_head);

	return mem;
}

/*
 * Rounds a number down to a power of 2.
 */
static uint32_t round_down(uint32_t n)
{
	while (n & (n - 1))
		n &= (n - 1);
	return n;
}

/*
 * Allocate room for a suitable hash table.
 */
static int init_hash_tables(struct dm_snapshot *s)
{
	sector_t hash_size, cow_dev_size, origin_dev_size, max_buckets;

	/*
	 * Calculate based on the size of the original volume or
	 * the COW volume...
	 */
	cow_dev_size = get_dev_size(s->cow->bdev);
	origin_dev_size = get_dev_size(s->origin->bdev);
	max_buckets = calc_max_buckets();

	hash_size = min(origin_dev_size, cow_dev_size) >> s->chunk_shift;
	hash_size = min(hash_size, max_buckets);

	/* Round it down to a power of 2 */
	hash_size = round_down(hash_size);
	if (init_exception_table(&s->complete, hash_size))
		return -ENOMEM;

	/*
	 * Allocate hash table for in-flight exceptions
	 * Make this smaller than the real hash table
	 */
	hash_size >>= 3;
	if (hash_size < 64)
		hash_size = 64;

	if (init_exception_table(&s->pending, hash_size)) {
		exit_exception_table(&s->complete, exception_cache);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Round a number up to the nearest 'size' boundary.  size must
 * be a power of 2.
 */
static ulong round_up(ulong n, ulong size)
{
	size--;
	return (n + size) & ~size;
}

static int set_chunk_size(struct dm_snapshot *s, const char *chunk_size_arg,
			  char **error)
{
	unsigned long chunk_size;
	char *value;

	chunk_size = simple_strtoul(chunk_size_arg, &value, 10);
	if (*chunk_size_arg == '\0' || *value != '\0') {
		*error = "Invalid chunk size";
		return -EINVAL;
	}

	if (!chunk_size) {
		s->chunk_size = s->chunk_mask = s->chunk_shift = 0;
		return 0;
	}

	/*
	 * Chunk size must be multiple of page size.  Silently
	 * round up if it's not.
	 */
	chunk_size = round_up(chunk_size, PAGE_SIZE >> 9);

	/* Check chunk_size is a power of 2 */
	if (chunk_size & (chunk_size - 1)) {
		*error = "Chunk size is not a power of 2";
		return -EINVAL;
	}

	/* Validate the chunk size against the device block size */
	if (chunk_size % (bdev_hardsect_size(s->cow->bdev) >> 9)) {
		*error = "Chunk size is not a multiple of device blocksize";
		return -EINVAL;
	}

	s->chunk_size = chunk_size;
	s->chunk_mask = chunk_size - 1;
	s->chunk_shift = ffs(chunk_size) - 1;

	return 0;
}

/*
 * Construct a snapshot mapping: <origin_dev> <COW-dev> <p/n> <chunk-size>
 */
static int snapshot_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct dm_snapshot *s;
	int r = -EINVAL;
	char persistent;
	char *origin_path;
	char *cow_path;

	if (argc != 4) {
		ti->error = "requires exactly 4 arguments";
		r = -EINVAL;
		goto bad1;
	}

	origin_path = argv[0];
	cow_path = argv[1];
	persistent = toupper(*argv[2]);

	if (persistent != 'P' && persistent != 'N') {
		ti->error = "Persistent flag is not P or N";
		r = -EINVAL;
		goto bad1;
	}

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL) {
		ti->error = "Cannot allocate snapshot context private "
		    "structure";
		r = -ENOMEM;
		goto bad1;
	}

	r = dm_get_device(ti, origin_path, 0, ti->len, FMODE_READ, &s->origin);
	if (r) {
		ti->error = "Cannot get origin device";
		goto bad2;
	}

	r = dm_get_device(ti, cow_path, 0, 0,
			  FMODE_READ | FMODE_WRITE, &s->cow);
	if (r) {
		dm_put_device(ti, s->origin);
		ti->error = "Cannot get COW device";
		goto bad2;
	}

	r = set_chunk_size(s, argv[3], &ti->error);
	if (r)
		goto bad3;

	s->type = persistent;

	s->valid = 1;
	s->active = 0;
	s->last_percent = 0;
	init_rwsem(&s->lock);
	spin_lock_init(&s->pe_lock);
	s->table = ti->table;

	/* Allocate hash table for COW data */
	if (init_hash_tables(s)) {
		ti->error = "Unable to allocate hash table space";
		r = -ENOMEM;
		goto bad3;
	}

	s->store.snap = s;

	if (persistent == 'P')
		r = dm_create_persistent(&s->store);
	else
		r = dm_create_transient(&s->store);

	if (r) {
		ti->error = "Couldn't create exception store";
		r = -EINVAL;
		goto bad4;
	}

	r = kcopyd_client_create(SNAPSHOT_PAGES, &s->kcopyd_client);
	if (r) {
		ti->error = "Could not create kcopyd client";
		goto bad5;
	}

	/* Metadata must only be loaded into one table at once */
	r = s->store.read_metadata(&s->store);
	if (r < 0) {
		ti->error = "Failed to read snapshot metadata";
		goto bad6;
	} else if (r > 0) {
		s->valid = 0;
		DMWARN("Snapshot is marked invalid.");
	}

	bio_list_init(&s->queued_bios);
	INIT_WORK(&s->queued_bios_work, flush_queued_bios);

	/* Add snapshot to the list of snapshots for this origin */
	/* Exceptions aren't triggered till snapshot_resume() is called */
	if (register_snapshot(s)) {
		r = -EINVAL;
		ti->error = "Cannot register snapshot origin";
		goto bad6;
	}

	ti->private = s;
	ti->split_io = s->chunk_size;

	return 0;

 bad6:
	kcopyd_client_destroy(s->kcopyd_client);

 bad5:
	s->store.destroy(&s->store);

 bad4:
	exit_exception_table(&s->pending, pending_cache);
	exit_exception_table(&s->complete, exception_cache);

 bad3:
	dm_put_device(ti, s->cow);
	dm_put_device(ti, s->origin);

 bad2:
	kfree(s);

 bad1:
	return r;
}

static void __free_exceptions(struct dm_snapshot *s)
{
	kcopyd_client_destroy(s->kcopyd_client);
	s->kcopyd_client = NULL;

	exit_exception_table(&s->pending, pending_cache);
	exit_exception_table(&s->complete, exception_cache);

	s->store.destroy(&s->store);
}

static void snapshot_dtr(struct dm_target *ti)
{
	struct dm_snapshot *s = ti->private;

	flush_workqueue(ksnapd);

	/* Prevent further origin writes from using this snapshot. */
	/* After this returns there can be no new kcopyd jobs. */
	unregister_snapshot(s);

	__free_exceptions(s);

	dm_put_device(ti, s->origin);
	dm_put_device(ti, s->cow);

	kfree(s);
}

/*
 * Flush a list of buffers.
 */
static void flush_bios(struct bio *bio)
{
	struct bio *n;

	while (bio) {
		n = bio->bi_next;
		bio->bi_next = NULL;
		generic_make_request(bio);
		bio = n;
	}
}

static void flush_queued_bios(struct work_struct *work)
{
	struct dm_snapshot *s =
		container_of(work, struct dm_snapshot, queued_bios_work);
	struct bio *queued_bios;
	unsigned long flags;

	spin_lock_irqsave(&s->pe_lock, flags);
	queued_bios = bio_list_get(&s->queued_bios);
	spin_unlock_irqrestore(&s->pe_lock, flags);

	flush_bios(queued_bios);
}

/*
 * Error a list of buffers.
 */
static void error_bios(struct bio *bio)
{
	struct bio *n;

	while (bio) {
		n = bio->bi_next;
		bio->bi_next = NULL;
		bio_io_error(bio, bio->bi_size);
		bio = n;
	}
}

static void __invalidate_snapshot(struct dm_snapshot *s, int err)
{
	if (!s->valid)
		return;

	if (err == -EIO)
		DMERR("Invalidating snapshot: Error reading/writing.");
	else if (err == -ENOMEM)
		DMERR("Invalidating snapshot: Unable to allocate exception.");

	if (s->store.drop_snapshot)
		s->store.drop_snapshot(&s->store);

	s->valid = 0;

	dm_table_event(s->table);
}

static void get_pending_exception(struct dm_snap_pending_exception *pe)
{
	atomic_inc(&pe->ref_count);
}

static struct bio *put_pending_exception(struct dm_snap_pending_exception *pe)
{
	struct dm_snap_pending_exception *primary_pe;
	struct bio *origin_bios = NULL;

	primary_pe = pe->primary_pe;

	/*
	 * If this pe is involved in a write to the origin and
	 * it is the last sibling to complete then release
	 * the bios for the original write to the origin.
	 */
	if (primary_pe &&
	    atomic_dec_and_test(&primary_pe->ref_count))
		origin_bios = bio_list_get(&primary_pe->origin_bios);

	/*
	 * Free the pe if it's not linked to an origin write or if
	 * it's not itself a primary pe.
	 */
	if (!primary_pe || primary_pe != pe)
		free_pending_exception(pe);

	/*
	 * Free the primary pe if nothing references it.
	 */
	if (primary_pe && !atomic_read(&primary_pe->ref_count))
		free_pending_exception(primary_pe);

	return origin_bios;
}

static void pending_complete(struct dm_snap_pending_exception *pe, int success)
{
	struct dm_snap_exception *e;
	struct dm_snapshot *s = pe->snap;
	struct bio *origin_bios = NULL;
	struct bio *snapshot_bios = NULL;
	int error = 0;

	if (!success) {
		/* Read/write error - snapshot is unusable */
		down_write(&s->lock);
		__invalidate_snapshot(s, -EIO);
		error = 1;
		goto out;
	}

	e = alloc_exception();
	if (!e) {
		down_write(&s->lock);
		__invalidate_snapshot(s, -ENOMEM);
		error = 1;
		goto out;
	}
	*e = pe->e;

	down_write(&s->lock);
	if (!s->valid) {
		free_exception(e);
		error = 1;
		goto out;
	}

	/*
	 * Add a proper exception, and remove the
	 * in-flight exception from the list.
	 */
	insert_exception(&s->complete, e);

 out:
	remove_exception(&pe->e);
	snapshot_bios = bio_list_get(&pe->snapshot_bios);
	origin_bios = put_pending_exception(pe);

	up_write(&s->lock);

	/* Submit any pending write bios */
	if (error)
		error_bios(snapshot_bios);
	else
		flush_bios(snapshot_bios);

	flush_bios(origin_bios);
}

static void commit_callback(void *context, int success)
{
	struct dm_snap_pending_exception *pe = context;

	pending_complete(pe, success);
}

/*
 * Called when the copy I/O has finished.  kcopyd actually runs
 * this code so don't block.
 */
static void copy_callback(int read_err, unsigned int write_err, void *context)
{
	struct dm_snap_pending_exception *pe = context;
	struct dm_snapshot *s = pe->snap;

	if (read_err || write_err)
		pending_complete(pe, 0);

	else
		/* Update the metadata if we are persistent */
		s->store.commit_exception(&s->store, &pe->e, commit_callback,
					  pe);
}

/*
 * Dispatches the copy operation to kcopyd.
 */
static void start_copy(struct dm_snap_pending_exception *pe)
{
	struct dm_snapshot *s = pe->snap;
	struct io_region src, dest;
	struct block_device *bdev = s->origin->bdev;
	sector_t dev_size;

	dev_size = get_dev_size(bdev);

	src.bdev = bdev;
	src.sector = chunk_to_sector(s, pe->e.old_chunk);
	src.count = min(s->chunk_size, dev_size - src.sector);

	dest.bdev = s->cow->bdev;
	dest.sector = chunk_to_sector(s, pe->e.new_chunk);
	dest.count = src.count;

	/* Hand over to kcopyd */
	kcopyd_copy(s->kcopyd_client,
		    &src, 1, &dest, 0, copy_callback, pe);
}

/*
 * Looks to see if this snapshot already has a pending exception
 * for this chunk, otherwise it allocates a new one and inserts
 * it into the pending table.
 *
 * NOTE: a write lock must be held on snap->lock before calling
 * this.
 */
static struct dm_snap_pending_exception *
__find_pending_exception(struct dm_snapshot *s, struct bio *bio)
{
	struct dm_snap_exception *e;
	struct dm_snap_pending_exception *pe;
	chunk_t chunk = sector_to_chunk(s, bio->bi_sector);

	/*
	 * Is there a pending exception for this already ?
	 */
	e = lookup_exception(&s->pending, chunk);
	if (e) {
		/* cast the exception to a pending exception */
		pe = container_of(e, struct dm_snap_pending_exception, e);
		goto out;
	}

	/*
	 * Create a new pending exception, we don't want
	 * to hold the lock while we do this.
	 */
	up_write(&s->lock);
	pe = alloc_pending_exception();
	down_write(&s->lock);

	if (!s->valid) {
		free_pending_exception(pe);
		return NULL;
	}

	e = lookup_exception(&s->pending, chunk);
	if (e) {
		free_pending_exception(pe);
		pe = container_of(e, struct dm_snap_pending_exception, e);
		goto out;
	}

	pe->e.old_chunk = chunk;
	bio_list_init(&pe->origin_bios);
	bio_list_init(&pe->snapshot_bios);
	pe->primary_pe = NULL;
	atomic_set(&pe->ref_count, 0);
	pe->snap = s;
	pe->started = 0;

	if (s->store.prepare_exception(&s->store, &pe->e)) {
		free_pending_exception(pe);
		return NULL;
	}

	get_pending_exception(pe);
	insert_exception(&s->pending, &pe->e);

 out:
	return pe;
}

static void remap_exception(struct dm_snapshot *s, struct dm_snap_exception *e,
			    struct bio *bio)
{
	bio->bi_bdev = s->cow->bdev;
	bio->bi_sector = chunk_to_sector(s, e->new_chunk) +
		(bio->bi_sector & s->chunk_mask);
}

static int snapshot_map(struct dm_target *ti, struct bio *bio,
			union map_info *map_context)
{
	struct dm_snap_exception *e;
	struct dm_snapshot *s = ti->private;
	int r = DM_MAPIO_REMAPPED;
	chunk_t chunk;
	struct dm_snap_pending_exception *pe = NULL;

	chunk = sector_to_chunk(s, bio->bi_sector);

	/* Full snapshots are not usable */
	/* To get here the table must be live so s->active is always set. */
	if (!s->valid)
		return -EIO;

	if (unlikely(bio_barrier(bio)))
		return -EOPNOTSUPP;

	/* FIXME: should only take write lock if we need
	 * to copy an exception */
	down_write(&s->lock);

	if (!s->valid) {
		r = -EIO;
		goto out_unlock;
	}

	/* If the block is already remapped - use that, else remap it */
	e = lookup_exception(&s->complete, chunk);
	if (e) {
		remap_exception(s, e, bio);
		goto out_unlock;
	}

	/*
	 * Write to snapshot - higher level takes care of RW/RO
	 * flags so we should only get this if we are
	 * writeable.
	 */
	if (bio_rw(bio) == WRITE) {
		pe = __find_pending_exception(s, bio);
		if (!pe) {
			__invalidate_snapshot(s, -ENOMEM);
			r = -EIO;
			goto out_unlock;
		}

		remap_exception(s, &pe->e, bio);
		bio_list_add(&pe->snapshot_bios, bio);

		r = DM_MAPIO_SUBMITTED;

		if (!pe->started) {
			/* this is protected by snap->lock */
			pe->started = 1;
			up_write(&s->lock);
			start_copy(pe);
			goto out;
		}
	} else
		/*
		 * FIXME: this read path scares me because we
		 * always use the origin when we have a pending
		 * exception.  However I can't think of a
		 * situation where this is wrong - ejt.
		 */
		bio->bi_bdev = s->origin->bdev;

 out_unlock:
	up_write(&s->lock);
 out:
	return r;
}

static void snapshot_resume(struct dm_target *ti)
{
	struct dm_snapshot *s = ti->private;

	down_write(&s->lock);
	s->active = 1;
	up_write(&s->lock);
}

static int snapshot_status(struct dm_target *ti, status_type_t type,
			   char *result, unsigned int maxlen)
{
	struct dm_snapshot *snap = ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		if (!snap->valid)
			snprintf(result, maxlen, "Invalid");
		else {
			if (snap->store.fraction_full) {
				sector_t numerator, denominator;
				snap->store.fraction_full(&snap->store,
							  &numerator,
							  &denominator);
				snprintf(result, maxlen, "%llu/%llu",
					(unsigned long long)numerator,
					(unsigned long long)denominator);
			}
			else
				snprintf(result, maxlen, "Unknown");
		}
		break;

	case STATUSTYPE_TABLE:
		/*
		 * kdevname returns a static pointer so we need
		 * to make private copies if the output is to
		 * make sense.
		 */
		snprintf(result, maxlen, "%s %s %c %llu",
			 snap->origin->name, snap->cow->name,
			 snap->type,
			 (unsigned long long)snap->chunk_size);
		break;
	}

	return 0;
}

/*-----------------------------------------------------------------
 * Origin methods
 *---------------------------------------------------------------*/
static int __origin_write(struct list_head *snapshots, struct bio *bio)
{
	int r = DM_MAPIO_REMAPPED, first = 0;
	struct dm_snapshot *snap;
	struct dm_snap_exception *e;
	struct dm_snap_pending_exception *pe, *next_pe, *primary_pe = NULL;
	chunk_t chunk;
	LIST_HEAD(pe_queue);

	/* Do all the snapshots on this origin */
	list_for_each_entry (snap, snapshots, list) {

		down_write(&snap->lock);

		/* Only deal with valid and active snapshots */
		if (!snap->valid || !snap->active)
			goto next_snapshot;

		/* Nothing to do if writing beyond end of snapshot */
		if (bio->bi_sector >= dm_table_get_size(snap->table))
			goto next_snapshot;

		/*
		 * Remember, different snapshots can have
		 * different chunk sizes.
		 */
		chunk = sector_to_chunk(snap, bio->bi_sector);

		/*
		 * Check exception table to see if block
		 * is already remapped in this snapshot
		 * and trigger an exception if not.
		 *
		 * ref_count is initialised to 1 so pending_complete()
		 * won't destroy the primary_pe while we're inside this loop.
		 */
		e = lookup_exception(&snap->complete, chunk);
		if (e)
			goto next_snapshot;

		pe = __find_pending_exception(snap, bio);
		if (!pe) {
			__invalidate_snapshot(snap, -ENOMEM);
			goto next_snapshot;
		}

		if (!primary_pe) {
			/*
			 * Either every pe here has same
			 * primary_pe or none has one yet.
			 */
			if (pe->primary_pe)
				primary_pe = pe->primary_pe;
			else {
				primary_pe = pe;
				first = 1;
			}

			bio_list_add(&primary_pe->origin_bios, bio);

			r = DM_MAPIO_SUBMITTED;
		}

		if (!pe->primary_pe) {
			pe->primary_pe = primary_pe;
			get_pending_exception(primary_pe);
		}

		if (!pe->started) {
			pe->started = 1;
			list_add_tail(&pe->list, &pe_queue);
		}

 next_snapshot:
		up_write(&snap->lock);
	}

	if (!primary_pe)
		return r;

	/*
	 * If this is the first time we're processing this chunk and
	 * ref_count is now 1 it means all the pending exceptions
	 * got completed while we were in the loop above, so it falls to
	 * us here to remove the primary_pe and submit any origin_bios.
	 */

	if (first && atomic_dec_and_test(&primary_pe->ref_count)) {
		flush_bios(bio_list_get(&primary_pe->origin_bios));
		free_pending_exception(primary_pe);
		/* If we got here, pe_queue is necessarily empty. */
		return r;
	}

	/*
	 * Now that we have a complete pe list we can start the copying.
	 */
	list_for_each_entry_safe(pe, next_pe, &pe_queue, list)
		start_copy(pe);

	return r;
}

/*
 * Called on a write from the origin driver.
 */
static int do_origin(struct dm_dev *origin, struct bio *bio)
{
	struct origin *o;
	int r = DM_MAPIO_REMAPPED;

	down_read(&_origins_lock);
	o = __lookup_origin(origin->bdev);
	if (o)
		r = __origin_write(&o->snapshots, bio);
	up_read(&_origins_lock);

	return r;
}

/*
 * Origin: maps a linear range of a device, with hooks for snapshotting.
 */

/*
 * Construct an origin mapping: <dev_path>
 * The context for an origin is merely a 'struct dm_dev *'
 * pointing to the real device.
 */
static int origin_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r;
	struct dm_dev *dev;

	if (argc != 1) {
		ti->error = "origin: incorrect number of arguments";
		return -EINVAL;
	}

	r = dm_get_device(ti, argv[0], 0, ti->len,
			  dm_table_get_mode(ti->table), &dev);
	if (r) {
		ti->error = "Cannot get target device";
		return r;
	}

	ti->private = dev;
	return 0;
}

static void origin_dtr(struct dm_target *ti)
{
	struct dm_dev *dev = ti->private;
	dm_put_device(ti, dev);
}

static int origin_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	struct dm_dev *dev = ti->private;
	bio->bi_bdev = dev->bdev;

	if (unlikely(bio_barrier(bio)))
		return -EOPNOTSUPP;

	/* Only tell snapshots if this is a write */
	return (bio_rw(bio) == WRITE) ? do_origin(dev, bio) : DM_MAPIO_REMAPPED;
}

#define min_not_zero(l, r) (l == 0) ? r : ((r == 0) ? l : min(l, r))

/*
 * Set the target "split_io" field to the minimum of all the snapshots'
 * chunk sizes.
 */
static void origin_resume(struct dm_target *ti)
{
	struct dm_dev *dev = ti->private;
	struct dm_snapshot *snap;
	struct origin *o;
	chunk_t chunk_size = 0;

	down_read(&_origins_lock);
	o = __lookup_origin(dev->bdev);
	if (o)
		list_for_each_entry (snap, &o->snapshots, list)
			chunk_size = min_not_zero(chunk_size, snap->chunk_size);
	up_read(&_origins_lock);

	ti->split_io = chunk_size;
}

static int origin_status(struct dm_target *ti, status_type_t type, char *result,
			 unsigned int maxlen)
{
	struct dm_dev *dev = ti->private;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		snprintf(result, maxlen, "%s", dev->name);
		break;
	}

	return 0;
}

static struct target_type origin_target = {
	.name    = "snapshot-origin",
	.version = {1, 5, 0},
	.module  = THIS_MODULE,
	.ctr     = origin_ctr,
	.dtr     = origin_dtr,
	.map     = origin_map,
	.resume  = origin_resume,
	.status  = origin_status,
};

static struct target_type snapshot_target = {
	.name    = "snapshot",
	.version = {1, 5, 0},
	.module  = THIS_MODULE,
	.ctr     = snapshot_ctr,
	.dtr     = snapshot_dtr,
	.map     = snapshot_map,
	.resume  = snapshot_resume,
	.status  = snapshot_status,
};

static int __init dm_snapshot_init(void)
{
	int r;

	r = dm_register_target(&snapshot_target);
	if (r) {
		DMERR("snapshot target register failed %d", r);
		return r;
	}

	r = dm_register_target(&origin_target);
	if (r < 0) {
		DMERR("Origin target register failed %d", r);
		goto bad1;
	}

	r = init_origin_hash();
	if (r) {
		DMERR("init_origin_hash failed.");
		goto bad2;
	}

	exception_cache = KMEM_CACHE(dm_snap_exception, 0);
	if (!exception_cache) {
		DMERR("Couldn't create exception cache.");
		r = -ENOMEM;
		goto bad3;
	}

	pending_cache = KMEM_CACHE(dm_snap_pending_exception, 0);
	if (!pending_cache) {
		DMERR("Couldn't create pending cache.");
		r = -ENOMEM;
		goto bad4;
	}

	pending_pool = mempool_create_slab_pool(128, pending_cache);
	if (!pending_pool) {
		DMERR("Couldn't create pending pool.");
		r = -ENOMEM;
		goto bad5;
	}

	ksnapd = create_singlethread_workqueue("ksnapd");
	if (!ksnapd) {
		DMERR("Failed to create ksnapd workqueue.");
		r = -ENOMEM;
		goto bad6;
	}

	return 0;

      bad6:
	mempool_destroy(pending_pool);
      bad5:
	kmem_cache_destroy(pending_cache);
      bad4:
	kmem_cache_destroy(exception_cache);
      bad3:
	exit_origin_hash();
      bad2:
	dm_unregister_target(&origin_target);
      bad1:
	dm_unregister_target(&snapshot_target);
	return r;
}

static void __exit dm_snapshot_exit(void)
{
	int r;

	destroy_workqueue(ksnapd);

	r = dm_unregister_target(&snapshot_target);
	if (r)
		DMERR("snapshot unregister failed %d", r);

	r = dm_unregister_target(&origin_target);
	if (r)
		DMERR("origin unregister failed %d", r);

	exit_origin_hash();
	mempool_destroy(pending_pool);
	kmem_cache_destroy(pending_cache);
	kmem_cache_destroy(exception_cache);
}

/* Module hooks */
module_init(dm_snapshot_init);
module_exit(dm_snapshot_exit);

MODULE_DESCRIPTION(DM_NAME " snapshot target");
MODULE_AUTHOR("Joe Thornber");
MODULE_LICENSE("GPL");
