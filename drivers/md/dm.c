/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-uevent.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/moduleparam.h>
#include <linux/blkpg.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/hdreg.h>

#include <trace/events/block.h>

#define DM_MSG_PREFIX "core"

static const char *_name = DM_NAME;

static unsigned int major = 0;
static unsigned int _major = 0;

static DEFINE_SPINLOCK(_minor_lock);
/*
 * For bio-based dm.
 * One of these is allocated per bio.
 */
struct dm_io {
	struct mapped_device *md;
	int error;
	atomic_t io_count;
	struct bio *bio;
	unsigned long start_time;
};

/*
 * For bio-based dm.
 * One of these is allocated per target within a bio.  Hopefully
 * this will be simplified out one day.
 */
struct dm_target_io {
	struct dm_io *io;
	struct dm_target *ti;
	union map_info info;
};

/*
 * For request-based dm.
 * One of these is allocated per request.
 */
struct dm_rq_target_io {
	struct mapped_device *md;
	struct dm_target *ti;
	struct request *orig, clone;
	int error;
	union map_info info;
};

/*
 * For request-based dm.
 * One of these is allocated per bio.
 */
struct dm_rq_clone_bio_info {
	struct bio *orig;
	struct request *rq;
};

union map_info *dm_get_mapinfo(struct bio *bio)
{
	if (bio && bio->bi_private)
		return &((struct dm_target_io *)bio->bi_private)->info;
	return NULL;
}

#define MINOR_ALLOCED ((void *)-1)

/*
 * Bits for the md->flags field.
 */
#define DMF_BLOCK_IO_FOR_SUSPEND 0
#define DMF_SUSPENDED 1
#define DMF_FROZEN 2
#define DMF_FREEING 3
#define DMF_DELETING 4
#define DMF_NOFLUSH_SUSPENDING 5
#define DMF_QUEUE_IO_TO_THREAD 6

/*
 * Work processed by per-device workqueue.
 */
struct mapped_device {
	struct rw_semaphore io_lock;
	struct mutex suspend_lock;
	rwlock_t map_lock;
	atomic_t holders;
	atomic_t open_count;

	unsigned long flags;

	struct request_queue *queue;
	struct gendisk *disk;
	char name[16];

	void *interface_ptr;

	/*
	 * A list of ios that arrived while we were suspended.
	 */
	atomic_t pending;
	wait_queue_head_t wait;
	struct work_struct work;
	struct bio_list deferred;
	spinlock_t deferred_lock;

	/*
	 * An error from the barrier request currently being processed.
	 */
	int barrier_error;

	/*
	 * Processing queue (flush/barriers)
	 */
	struct workqueue_struct *wq;

	/*
	 * The current mapping.
	 */
	struct dm_table *map;

	/*
	 * io objects are allocated from here.
	 */
	mempool_t *io_pool;
	mempool_t *tio_pool;

	struct bio_set *bs;

	/*
	 * Event handling.
	 */
	atomic_t event_nr;
	wait_queue_head_t eventq;
	atomic_t uevent_seq;
	struct list_head uevent_list;
	spinlock_t uevent_lock; /* Protect access to uevent_list */

	/*
	 * freeze/thaw support require holding onto a super block
	 */
	struct super_block *frozen_sb;
	struct block_device *suspended_bdev;

	/* forced geometry settings */
	struct hd_geometry geometry;

	/* sysfs handle */
	struct kobject kobj;
};

#define MIN_IOS 256
static struct kmem_cache *_io_cache;
static struct kmem_cache *_tio_cache;
static struct kmem_cache *_rq_tio_cache;
static struct kmem_cache *_rq_bio_info_cache;

static int __init local_init(void)
{
	int r = -ENOMEM;

	/* allocate a slab for the dm_ios */
	_io_cache = KMEM_CACHE(dm_io, 0);
	if (!_io_cache)
		return r;

	/* allocate a slab for the target ios */
	_tio_cache = KMEM_CACHE(dm_target_io, 0);
	if (!_tio_cache)
		goto out_free_io_cache;

	_rq_tio_cache = KMEM_CACHE(dm_rq_target_io, 0);
	if (!_rq_tio_cache)
		goto out_free_tio_cache;

	_rq_bio_info_cache = KMEM_CACHE(dm_rq_clone_bio_info, 0);
	if (!_rq_bio_info_cache)
		goto out_free_rq_tio_cache;

	r = dm_uevent_init();
	if (r)
		goto out_free_rq_bio_info_cache;

	_major = major;
	r = register_blkdev(_major, _name);
	if (r < 0)
		goto out_uevent_exit;

	if (!_major)
		_major = r;

	return 0;

out_uevent_exit:
	dm_uevent_exit();
out_free_rq_bio_info_cache:
	kmem_cache_destroy(_rq_bio_info_cache);
out_free_rq_tio_cache:
	kmem_cache_destroy(_rq_tio_cache);
out_free_tio_cache:
	kmem_cache_destroy(_tio_cache);
out_free_io_cache:
	kmem_cache_destroy(_io_cache);

	return r;
}

static void local_exit(void)
{
	kmem_cache_destroy(_rq_bio_info_cache);
	kmem_cache_destroy(_rq_tio_cache);
	kmem_cache_destroy(_tio_cache);
	kmem_cache_destroy(_io_cache);
	unregister_blkdev(_major, _name);
	dm_uevent_exit();

	_major = 0;

	DMINFO("cleaned up");
}

static int (*_inits[])(void) __initdata = {
	local_init,
	dm_target_init,
	dm_linear_init,
	dm_stripe_init,
	dm_kcopyd_init,
	dm_interface_init,
};

static void (*_exits[])(void) = {
	local_exit,
	dm_target_exit,
	dm_linear_exit,
	dm_stripe_exit,
	dm_kcopyd_exit,
	dm_interface_exit,
};

static int __init dm_init(void)
{
	const int count = ARRAY_SIZE(_inits);

	int r, i;

	for (i = 0; i < count; i++) {
		r = _inits[i]();
		if (r)
			goto bad;
	}

	return 0;

      bad:
	while (i--)
		_exits[i]();

	return r;
}

static void __exit dm_exit(void)
{
	int i = ARRAY_SIZE(_exits);

	while (i--)
		_exits[i]();
}

/*
 * Block device functions
 */
static int dm_blk_open(struct block_device *bdev, fmode_t mode)
{
	struct mapped_device *md;

	spin_lock(&_minor_lock);

	md = bdev->bd_disk->private_data;
	if (!md)
		goto out;

	if (test_bit(DMF_FREEING, &md->flags) ||
	    test_bit(DMF_DELETING, &md->flags)) {
		md = NULL;
		goto out;
	}

	dm_get(md);
	atomic_inc(&md->open_count);

out:
	spin_unlock(&_minor_lock);

	return md ? 0 : -ENXIO;
}

static int dm_blk_close(struct gendisk *disk, fmode_t mode)
{
	struct mapped_device *md = disk->private_data;
	atomic_dec(&md->open_count);
	dm_put(md);
	return 0;
}

int dm_open_count(struct mapped_device *md)
{
	return atomic_read(&md->open_count);
}

/*
 * Guarantees nothing is using the device before it's deleted.
 */
int dm_lock_for_deletion(struct mapped_device *md)
{
	int r = 0;

	spin_lock(&_minor_lock);

	if (dm_open_count(md))
		r = -EBUSY;
	else
		set_bit(DMF_DELETING, &md->flags);

	spin_unlock(&_minor_lock);

	return r;
}

static int dm_blk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mapped_device *md = bdev->bd_disk->private_data;

	return dm_get_geometry(md, geo);
}

static int dm_blk_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	struct dm_table *map = dm_get_table(md);
	struct dm_target *tgt;
	int r = -ENOTTY;

	if (!map || !dm_table_get_size(map))
		goto out;

	/* We only support devices that have a single target */
	if (dm_table_get_num_targets(map) != 1)
		goto out;

	tgt = dm_table_get_target(map, 0);

	if (dm_suspended(md)) {
		r = -EAGAIN;
		goto out;
	}

	if (tgt->type->ioctl)
		r = tgt->type->ioctl(tgt, cmd, arg);

out:
	dm_table_put(map);

	return r;
}

static struct dm_io *alloc_io(struct mapped_device *md)
{
	return mempool_alloc(md->io_pool, GFP_NOIO);
}

static void free_io(struct mapped_device *md, struct dm_io *io)
{
	mempool_free(io, md->io_pool);
}

static struct dm_target_io *alloc_tio(struct mapped_device *md)
{
	return mempool_alloc(md->tio_pool, GFP_NOIO);
}

static void free_tio(struct mapped_device *md, struct dm_target_io *tio)
{
	mempool_free(tio, md->tio_pool);
}

static void start_io_acct(struct dm_io *io)
{
	struct mapped_device *md = io->md;
	int cpu;

	io->start_time = jiffies;

	cpu = part_stat_lock();
	part_round_stats(cpu, &dm_disk(md)->part0);
	part_stat_unlock();
	dm_disk(md)->part0.in_flight = atomic_inc_return(&md->pending);
}

static void end_io_acct(struct dm_io *io)
{
	struct mapped_device *md = io->md;
	struct bio *bio = io->bio;
	unsigned long duration = jiffies - io->start_time;
	int pending, cpu;
	int rw = bio_data_dir(bio);

	cpu = part_stat_lock();
	part_round_stats(cpu, &dm_disk(md)->part0);
	part_stat_add(cpu, &dm_disk(md)->part0, ticks[rw], duration);
	part_stat_unlock();

	/*
	 * After this is decremented the bio must not be touched if it is
	 * a barrier.
	 */
	dm_disk(md)->part0.in_flight = pending =
		atomic_dec_return(&md->pending);

	/* nudge anyone waiting on suspend queue */
	if (!pending)
		wake_up(&md->wait);
}

/*
 * Add the bio to the list of deferred io.
 */
static void queue_io(struct mapped_device *md, struct bio *bio)
{
	down_write(&md->io_lock);

	spin_lock_irq(&md->deferred_lock);
	bio_list_add(&md->deferred, bio);
	spin_unlock_irq(&md->deferred_lock);

	if (!test_and_set_bit(DMF_QUEUE_IO_TO_THREAD, &md->flags))
		queue_work(md->wq, &md->work);

	up_write(&md->io_lock);
}

/*
 * Everyone (including functions in this file), should use this
 * function to access the md->map field, and make sure they call
 * dm_table_put() when finished.
 */
struct dm_table *dm_get_table(struct mapped_device *md)
{
	struct dm_table *t;

	read_lock(&md->map_lock);
	t = md->map;
	if (t)
		dm_table_get(t);
	read_unlock(&md->map_lock);

	return t;
}

/*
 * Get the geometry associated with a dm device
 */
int dm_get_geometry(struct mapped_device *md, struct hd_geometry *geo)
{
	*geo = md->geometry;

	return 0;
}

/*
 * Set the geometry of a device.
 */
int dm_set_geometry(struct mapped_device *md, struct hd_geometry *geo)
{
	sector_t sz = (sector_t)geo->cylinders * geo->heads * geo->sectors;

	if (geo->start > sz) {
		DMWARN("Start sector is beyond the geometry limits.");
		return -EINVAL;
	}

	md->geometry = *geo;

	return 0;
}

/*-----------------------------------------------------------------
 * CRUD START:
 *   A more elegant soln is in the works that uses the queue
 *   merge fn, unfortunately there are a couple of changes to
 *   the block layer that I want to make for this.  So in the
 *   interests of getting something for people to use I give
 *   you this clearly demarcated crap.
 *---------------------------------------------------------------*/

static int __noflush_suspending(struct mapped_device *md)
{
	return test_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);
}

/*
 * Decrements the number of outstanding ios that a bio has been
 * cloned into, completing the original io if necc.
 */
static void dec_pending(struct dm_io *io, int error)
{
	unsigned long flags;
	int io_error;
	struct bio *bio;
	struct mapped_device *md = io->md;

	/* Push-back supersedes any I/O errors */
	if (error && !(io->error > 0 && __noflush_suspending(md)))
		io->error = error;

	if (atomic_dec_and_test(&io->io_count)) {
		if (io->error == DM_ENDIO_REQUEUE) {
			/*
			 * Target requested pushing back the I/O.
			 */
			spin_lock_irqsave(&md->deferred_lock, flags);
			if (__noflush_suspending(md))
				bio_list_add_head(&md->deferred, io->bio);
			else
				/* noflush suspend was interrupted. */
				io->error = -EIO;
			spin_unlock_irqrestore(&md->deferred_lock, flags);
		}

		io_error = io->error;
		bio = io->bio;

		if (bio_barrier(bio)) {
			/*
			 * There can be just one barrier request so we use
			 * a per-device variable for error reporting.
			 * Note that you can't touch the bio after end_io_acct
			 */
			md->barrier_error = io_error;
			end_io_acct(io);
		} else {
			end_io_acct(io);

			if (io_error != DM_ENDIO_REQUEUE) {
				trace_block_bio_complete(md->queue, bio);

				bio_endio(bio, io_error);
			}
		}

		free_io(md, io);
	}
}

static void clone_endio(struct bio *bio, int error)
{
	int r = 0;
	struct dm_target_io *tio = bio->bi_private;
	struct dm_io *io = tio->io;
	struct mapped_device *md = tio->io->md;
	dm_endio_fn endio = tio->ti->type->end_io;

	if (!bio_flagged(bio, BIO_UPTODATE) && !error)
		error = -EIO;

	if (endio) {
		r = endio(tio->ti, bio, error, &tio->info);
		if (r < 0 || r == DM_ENDIO_REQUEUE)
			/*
			 * error and requeue request are handled
			 * in dec_pending().
			 */
			error = r;
		else if (r == DM_ENDIO_INCOMPLETE)
			/* The target will handle the io */
			return;
		else if (r) {
			DMWARN("unimplemented target endio return value: %d", r);
			BUG();
		}
	}

	/*
	 * Store md for cleanup instead of tio which is about to get freed.
	 */
	bio->bi_private = md->bs;

	free_tio(md, tio);
	bio_put(bio);
	dec_pending(io, error);
}

static sector_t max_io_len(struct mapped_device *md,
			   sector_t sector, struct dm_target *ti)
{
	sector_t offset = sector - ti->begin;
	sector_t len = ti->len - offset;

	/*
	 * Does the target need to split even further ?
	 */
	if (ti->split_io) {
		sector_t boundary;
		boundary = ((offset + ti->split_io) & ~(ti->split_io - 1))
			   - offset;
		if (len > boundary)
			len = boundary;
	}

	return len;
}

static void __map_bio(struct dm_target *ti, struct bio *clone,
		      struct dm_target_io *tio)
{
	int r;
	sector_t sector;
	struct mapped_device *md;

	/*
	 * Sanity checks.
	 */
	BUG_ON(!clone->bi_size);

	clone->bi_end_io = clone_endio;
	clone->bi_private = tio;

	/*
	 * Map the clone.  If r == 0 we don't need to do
	 * anything, the target has assumed ownership of
	 * this io.
	 */
	atomic_inc(&tio->io->io_count);
	sector = clone->bi_sector;
	r = ti->type->map(ti, clone, &tio->info);
	if (r == DM_MAPIO_REMAPPED) {
		/* the bio has been remapped so dispatch it */

		trace_block_remap(bdev_get_queue(clone->bi_bdev), clone,
				    tio->io->bio->bi_bdev->bd_dev, sector);

		generic_make_request(clone);
	} else if (r < 0 || r == DM_MAPIO_REQUEUE) {
		/* error the io and bail out, or requeue it if needed */
		md = tio->io->md;
		dec_pending(tio->io, r);
		/*
		 * Store bio_set for cleanup.
		 */
		clone->bi_private = md->bs;
		bio_put(clone);
		free_tio(md, tio);
	} else if (r) {
		DMWARN("unimplemented target map return value: %d", r);
		BUG();
	}
}

struct clone_info {
	struct mapped_device *md;
	struct dm_table *map;
	struct bio *bio;
	struct dm_io *io;
	sector_t sector;
	sector_t sector_count;
	unsigned short idx;
};

static void dm_bio_destructor(struct bio *bio)
{
	struct bio_set *bs = bio->bi_private;

	bio_free(bio, bs);
}

/*
 * Creates a little bio that is just does part of a bvec.
 */
static struct bio *split_bvec(struct bio *bio, sector_t sector,
			      unsigned short idx, unsigned int offset,
			      unsigned int len, struct bio_set *bs)
{
	struct bio *clone;
	struct bio_vec *bv = bio->bi_io_vec + idx;

	clone = bio_alloc_bioset(GFP_NOIO, 1, bs);
	clone->bi_destructor = dm_bio_destructor;
	*clone->bi_io_vec = *bv;

	clone->bi_sector = sector;
	clone->bi_bdev = bio->bi_bdev;
	clone->bi_rw = bio->bi_rw & ~(1 << BIO_RW_BARRIER);
	clone->bi_vcnt = 1;
	clone->bi_size = to_bytes(len);
	clone->bi_io_vec->bv_offset = offset;
	clone->bi_io_vec->bv_len = clone->bi_size;
	clone->bi_flags |= 1 << BIO_CLONED;

	if (bio_integrity(bio)) {
		bio_integrity_clone(clone, bio, GFP_NOIO);
		bio_integrity_trim(clone,
				   bio_sector_offset(bio, idx, offset), len);
	}

	return clone;
}

/*
 * Creates a bio that consists of range of complete bvecs.
 */
static struct bio *clone_bio(struct bio *bio, sector_t sector,
			     unsigned short idx, unsigned short bv_count,
			     unsigned int len, struct bio_set *bs)
{
	struct bio *clone;

	clone = bio_alloc_bioset(GFP_NOIO, bio->bi_max_vecs, bs);
	__bio_clone(clone, bio);
	clone->bi_rw &= ~(1 << BIO_RW_BARRIER);
	clone->bi_destructor = dm_bio_destructor;
	clone->bi_sector = sector;
	clone->bi_idx = idx;
	clone->bi_vcnt = idx + bv_count;
	clone->bi_size = to_bytes(len);
	clone->bi_flags &= ~(1 << BIO_SEG_VALID);

	if (bio_integrity(bio)) {
		bio_integrity_clone(clone, bio, GFP_NOIO);

		if (idx != bio->bi_idx || clone->bi_size < bio->bi_size)
			bio_integrity_trim(clone,
					   bio_sector_offset(bio, idx, 0), len);
	}

	return clone;
}

static int __clone_and_map(struct clone_info *ci)
{
	struct bio *clone, *bio = ci->bio;
	struct dm_target *ti;
	sector_t len = 0, max;
	struct dm_target_io *tio;

	ti = dm_table_find_target(ci->map, ci->sector);
	if (!dm_target_is_valid(ti))
		return -EIO;

	max = max_io_len(ci->md, ci->sector, ti);

	/*
	 * Allocate a target io object.
	 */
	tio = alloc_tio(ci->md);
	tio->io = ci->io;
	tio->ti = ti;
	memset(&tio->info, 0, sizeof(tio->info));

	if (ci->sector_count <= max) {
		/*
		 * Optimise for the simple case where we can do all of
		 * the remaining io with a single clone.
		 */
		clone = clone_bio(bio, ci->sector, ci->idx,
				  bio->bi_vcnt - ci->idx, ci->sector_count,
				  ci->md->bs);
		__map_bio(ti, clone, tio);
		ci->sector_count = 0;

	} else if (to_sector(bio->bi_io_vec[ci->idx].bv_len) <= max) {
		/*
		 * There are some bvecs that don't span targets.
		 * Do as many of these as possible.
		 */
		int i;
		sector_t remaining = max;
		sector_t bv_len;

		for (i = ci->idx; remaining && (i < bio->bi_vcnt); i++) {
			bv_len = to_sector(bio->bi_io_vec[i].bv_len);

			if (bv_len > remaining)
				break;

			remaining -= bv_len;
			len += bv_len;
		}

		clone = clone_bio(bio, ci->sector, ci->idx, i - ci->idx, len,
				  ci->md->bs);
		__map_bio(ti, clone, tio);

		ci->sector += len;
		ci->sector_count -= len;
		ci->idx = i;

	} else {
		/*
		 * Handle a bvec that must be split between two or more targets.
		 */
		struct bio_vec *bv = bio->bi_io_vec + ci->idx;
		sector_t remaining = to_sector(bv->bv_len);
		unsigned int offset = 0;

		do {
			if (offset) {
				ti = dm_table_find_target(ci->map, ci->sector);
				if (!dm_target_is_valid(ti))
					return -EIO;

				max = max_io_len(ci->md, ci->sector, ti);

				tio = alloc_tio(ci->md);
				tio->io = ci->io;
				tio->ti = ti;
				memset(&tio->info, 0, sizeof(tio->info));
			}

			len = min(remaining, max);

			clone = split_bvec(bio, ci->sector, ci->idx,
					   bv->bv_offset + offset, len,
					   ci->md->bs);

			__map_bio(ti, clone, tio);

			ci->sector += len;
			ci->sector_count -= len;
			offset += to_bytes(len);
		} while (remaining -= len);

		ci->idx++;
	}

	return 0;
}

/*
 * Split the bio into several clones and submit it to targets.
 */
static void __split_and_process_bio(struct mapped_device *md, struct bio *bio)
{
	struct clone_info ci;
	int error = 0;

	ci.map = dm_get_table(md);
	if (unlikely(!ci.map)) {
		if (!bio_barrier(bio))
			bio_io_error(bio);
		else
			md->barrier_error = -EIO;
		return;
	}

	ci.md = md;
	ci.bio = bio;
	ci.io = alloc_io(md);
	ci.io->error = 0;
	atomic_set(&ci.io->io_count, 1);
	ci.io->bio = bio;
	ci.io->md = md;
	ci.sector = bio->bi_sector;
	ci.sector_count = bio_sectors(bio);
	ci.idx = bio->bi_idx;

	start_io_acct(ci.io);
	while (ci.sector_count && !error)
		error = __clone_and_map(&ci);

	/* drop the extra reference count */
	dec_pending(ci.io, error);
	dm_table_put(ci.map);
}
/*-----------------------------------------------------------------
 * CRUD END
 *---------------------------------------------------------------*/

static int dm_merge_bvec(struct request_queue *q,
			 struct bvec_merge_data *bvm,
			 struct bio_vec *biovec)
{
	struct mapped_device *md = q->queuedata;
	struct dm_table *map = dm_get_table(md);
	struct dm_target *ti;
	sector_t max_sectors;
	int max_size = 0;

	if (unlikely(!map))
		goto out;

	ti = dm_table_find_target(map, bvm->bi_sector);
	if (!dm_target_is_valid(ti))
		goto out_table;

	/*
	 * Find maximum amount of I/O that won't need splitting
	 */
	max_sectors = min(max_io_len(md, bvm->bi_sector, ti),
			  (sector_t) BIO_MAX_SECTORS);
	max_size = (max_sectors << SECTOR_SHIFT) - bvm->bi_size;
	if (max_size < 0)
		max_size = 0;

	/*
	 * merge_bvec_fn() returns number of bytes
	 * it can accept at this offset
	 * max is precomputed maximal io size
	 */
	if (max_size && ti->type->merge)
		max_size = ti->type->merge(ti, bvm, biovec, max_size);

out_table:
	dm_table_put(map);

out:
	/*
	 * Always allow an entire first page
	 */
	if (max_size <= biovec->bv_len && !(bvm->bi_size >> SECTOR_SHIFT))
		max_size = biovec->bv_len;

	return max_size;
}

/*
 * The request function that just remaps the bio built up by
 * dm_merge_bvec.
 */
static int dm_request(struct request_queue *q, struct bio *bio)
{
	int rw = bio_data_dir(bio);
	struct mapped_device *md = q->queuedata;
	int cpu;

	down_read(&md->io_lock);

	cpu = part_stat_lock();
	part_stat_inc(cpu, &dm_disk(md)->part0, ios[rw]);
	part_stat_add(cpu, &dm_disk(md)->part0, sectors[rw], bio_sectors(bio));
	part_stat_unlock();

	/*
	 * If we're suspended or the thread is processing barriers
	 * we have to queue this io for later.
	 */
	if (unlikely(test_bit(DMF_QUEUE_IO_TO_THREAD, &md->flags)) ||
	    unlikely(bio_barrier(bio))) {
		up_read(&md->io_lock);

		if (unlikely(test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) &&
		    bio_rw(bio) == READA) {
			bio_io_error(bio);
			return 0;
		}

		queue_io(md, bio);

		return 0;
	}

	__split_and_process_bio(md, bio);
	up_read(&md->io_lock);
	return 0;
}

static void dm_unplug_all(struct request_queue *q)
{
	struct mapped_device *md = q->queuedata;
	struct dm_table *map = dm_get_table(md);

	if (map) {
		dm_table_unplug_all(map);
		dm_table_put(map);
	}
}

static int dm_any_congested(void *congested_data, int bdi_bits)
{
	int r = bdi_bits;
	struct mapped_device *md = congested_data;
	struct dm_table *map;

	if (!test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) {
		map = dm_get_table(md);
		if (map) {
			r = dm_table_any_congested(map, bdi_bits);
			dm_table_put(map);
		}
	}

	return r;
}

/*-----------------------------------------------------------------
 * An IDR is used to keep track of allocated minor numbers.
 *---------------------------------------------------------------*/
static DEFINE_IDR(_minor_idr);

static void free_minor(int minor)
{
	spin_lock(&_minor_lock);
	idr_remove(&_minor_idr, minor);
	spin_unlock(&_minor_lock);
}

/*
 * See if the device with a specific minor # is free.
 */
static int specific_minor(int minor)
{
	int r, m;

	if (minor >= (1 << MINORBITS))
		return -EINVAL;

	r = idr_pre_get(&_minor_idr, GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	spin_lock(&_minor_lock);

	if (idr_find(&_minor_idr, minor)) {
		r = -EBUSY;
		goto out;
	}

	r = idr_get_new_above(&_minor_idr, MINOR_ALLOCED, minor, &m);
	if (r)
		goto out;

	if (m != minor) {
		idr_remove(&_minor_idr, m);
		r = -EBUSY;
		goto out;
	}

out:
	spin_unlock(&_minor_lock);
	return r;
}

static int next_free_minor(int *minor)
{
	int r, m;

	r = idr_pre_get(&_minor_idr, GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	spin_lock(&_minor_lock);

	r = idr_get_new(&_minor_idr, MINOR_ALLOCED, &m);
	if (r)
		goto out;

	if (m >= (1 << MINORBITS)) {
		idr_remove(&_minor_idr, m);
		r = -ENOSPC;
		goto out;
	}

	*minor = m;

out:
	spin_unlock(&_minor_lock);
	return r;
}

static struct block_device_operations dm_blk_dops;

static void dm_wq_work(struct work_struct *work);

/*
 * Allocate and initialise a blank device with a given minor.
 */
static struct mapped_device *alloc_dev(int minor)
{
	int r;
	struct mapped_device *md = kzalloc(sizeof(*md), GFP_KERNEL);
	void *old_md;

	if (!md) {
		DMWARN("unable to allocate device, out of memory.");
		return NULL;
	}

	if (!try_module_get(THIS_MODULE))
		goto bad_module_get;

	/* get a minor number for the dev */
	if (minor == DM_ANY_MINOR)
		r = next_free_minor(&minor);
	else
		r = specific_minor(minor);
	if (r < 0)
		goto bad_minor;

	init_rwsem(&md->io_lock);
	mutex_init(&md->suspend_lock);
	spin_lock_init(&md->deferred_lock);
	rwlock_init(&md->map_lock);
	atomic_set(&md->holders, 1);
	atomic_set(&md->open_count, 0);
	atomic_set(&md->event_nr, 0);
	atomic_set(&md->uevent_seq, 0);
	INIT_LIST_HEAD(&md->uevent_list);
	spin_lock_init(&md->uevent_lock);

	md->queue = blk_alloc_queue(GFP_KERNEL);
	if (!md->queue)
		goto bad_queue;

	md->queue->queuedata = md;
	md->queue->backing_dev_info.congested_fn = dm_any_congested;
	md->queue->backing_dev_info.congested_data = md;
	blk_queue_make_request(md->queue, dm_request);
	blk_queue_ordered(md->queue, QUEUE_ORDERED_DRAIN, NULL);
	blk_queue_bounce_limit(md->queue, BLK_BOUNCE_ANY);
	md->queue->unplug_fn = dm_unplug_all;
	blk_queue_merge_bvec(md->queue, dm_merge_bvec);

	md->io_pool = mempool_create_slab_pool(MIN_IOS, _io_cache);
	if (!md->io_pool)
		goto bad_io_pool;

	md->tio_pool = mempool_create_slab_pool(MIN_IOS, _tio_cache);
	if (!md->tio_pool)
		goto bad_tio_pool;

	md->bs = bioset_create(16, 0);
	if (!md->bs)
		goto bad_no_bioset;

	md->disk = alloc_disk(1);
	if (!md->disk)
		goto bad_disk;

	atomic_set(&md->pending, 0);
	init_waitqueue_head(&md->wait);
	INIT_WORK(&md->work, dm_wq_work);
	init_waitqueue_head(&md->eventq);

	md->disk->major = _major;
	md->disk->first_minor = minor;
	md->disk->fops = &dm_blk_dops;
	md->disk->queue = md->queue;
	md->disk->private_data = md;
	sprintf(md->disk->disk_name, "dm-%d", minor);
	add_disk(md->disk);
	format_dev_t(md->name, MKDEV(_major, minor));

	md->wq = create_singlethread_workqueue("kdmflush");
	if (!md->wq)
		goto bad_thread;

	/* Populate the mapping, nobody knows we exist yet */
	spin_lock(&_minor_lock);
	old_md = idr_replace(&_minor_idr, md, minor);
	spin_unlock(&_minor_lock);

	BUG_ON(old_md != MINOR_ALLOCED);

	return md;

bad_thread:
	put_disk(md->disk);
bad_disk:
	bioset_free(md->bs);
bad_no_bioset:
	mempool_destroy(md->tio_pool);
bad_tio_pool:
	mempool_destroy(md->io_pool);
bad_io_pool:
	blk_cleanup_queue(md->queue);
bad_queue:
	free_minor(minor);
bad_minor:
	module_put(THIS_MODULE);
bad_module_get:
	kfree(md);
	return NULL;
}

static void unlock_fs(struct mapped_device *md);

static void free_dev(struct mapped_device *md)
{
	int minor = MINOR(disk_devt(md->disk));

	if (md->suspended_bdev) {
		unlock_fs(md);
		bdput(md->suspended_bdev);
	}
	destroy_workqueue(md->wq);
	mempool_destroy(md->tio_pool);
	mempool_destroy(md->io_pool);
	bioset_free(md->bs);
	blk_integrity_unregister(md->disk);
	del_gendisk(md->disk);
	free_minor(minor);

	spin_lock(&_minor_lock);
	md->disk->private_data = NULL;
	spin_unlock(&_minor_lock);

	put_disk(md->disk);
	blk_cleanup_queue(md->queue);
	module_put(THIS_MODULE);
	kfree(md);
}

/*
 * Bind a table to the device.
 */
static void event_callback(void *context)
{
	unsigned long flags;
	LIST_HEAD(uevents);
	struct mapped_device *md = (struct mapped_device *) context;

	spin_lock_irqsave(&md->uevent_lock, flags);
	list_splice_init(&md->uevent_list, &uevents);
	spin_unlock_irqrestore(&md->uevent_lock, flags);

	dm_send_uevents(&uevents, &disk_to_dev(md->disk)->kobj);

	atomic_inc(&md->event_nr);
	wake_up(&md->eventq);
}

static void __set_size(struct mapped_device *md, sector_t size)
{
	set_capacity(md->disk, size);

	mutex_lock(&md->suspended_bdev->bd_inode->i_mutex);
	i_size_write(md->suspended_bdev->bd_inode, (loff_t)size << SECTOR_SHIFT);
	mutex_unlock(&md->suspended_bdev->bd_inode->i_mutex);
}

static int __bind(struct mapped_device *md, struct dm_table *t)
{
	struct request_queue *q = md->queue;
	sector_t size;

	size = dm_table_get_size(t);

	/*
	 * Wipe any geometry if the size of the table changed.
	 */
	if (size != get_capacity(md->disk))
		memset(&md->geometry, 0, sizeof(md->geometry));

	if (md->suspended_bdev)
		__set_size(md, size);

	if (!size) {
		dm_table_destroy(t);
		return 0;
	}

	dm_table_event_callback(t, event_callback, md);

	write_lock(&md->map_lock);
	md->map = t;
	dm_table_set_restrictions(t, q);
	write_unlock(&md->map_lock);

	return 0;
}

static void __unbind(struct mapped_device *md)
{
	struct dm_table *map = md->map;

	if (!map)
		return;

	dm_table_event_callback(map, NULL, NULL);
	write_lock(&md->map_lock);
	md->map = NULL;
	write_unlock(&md->map_lock);
	dm_table_destroy(map);
}

/*
 * Constructor for a new device.
 */
int dm_create(int minor, struct mapped_device **result)
{
	struct mapped_device *md;

	md = alloc_dev(minor);
	if (!md)
		return -ENXIO;

	dm_sysfs_init(md);

	*result = md;
	return 0;
}

static struct mapped_device *dm_find_md(dev_t dev)
{
	struct mapped_device *md;
	unsigned minor = MINOR(dev);

	if (MAJOR(dev) != _major || minor >= (1 << MINORBITS))
		return NULL;

	spin_lock(&_minor_lock);

	md = idr_find(&_minor_idr, minor);
	if (md && (md == MINOR_ALLOCED ||
		   (MINOR(disk_devt(dm_disk(md))) != minor) ||
		   test_bit(DMF_FREEING, &md->flags))) {
		md = NULL;
		goto out;
	}

out:
	spin_unlock(&_minor_lock);

	return md;
}

struct mapped_device *dm_get_md(dev_t dev)
{
	struct mapped_device *md = dm_find_md(dev);

	if (md)
		dm_get(md);

	return md;
}

void *dm_get_mdptr(struct mapped_device *md)
{
	return md->interface_ptr;
}

void dm_set_mdptr(struct mapped_device *md, void *ptr)
{
	md->interface_ptr = ptr;
}

void dm_get(struct mapped_device *md)
{
	atomic_inc(&md->holders);
}

const char *dm_device_name(struct mapped_device *md)
{
	return md->name;
}
EXPORT_SYMBOL_GPL(dm_device_name);

void dm_put(struct mapped_device *md)
{
	struct dm_table *map;

	BUG_ON(test_bit(DMF_FREEING, &md->flags));

	if (atomic_dec_and_lock(&md->holders, &_minor_lock)) {
		map = dm_get_table(md);
		idr_replace(&_minor_idr, MINOR_ALLOCED,
			    MINOR(disk_devt(dm_disk(md))));
		set_bit(DMF_FREEING, &md->flags);
		spin_unlock(&_minor_lock);
		if (!dm_suspended(md)) {
			dm_table_presuspend_targets(map);
			dm_table_postsuspend_targets(map);
		}
		dm_sysfs_exit(md);
		dm_table_put(map);
		__unbind(md);
		free_dev(md);
	}
}
EXPORT_SYMBOL_GPL(dm_put);

static int dm_wait_for_completion(struct mapped_device *md, int interruptible)
{
	int r = 0;
	DECLARE_WAITQUEUE(wait, current);

	dm_unplug_all(md->queue);

	add_wait_queue(&md->wait, &wait);

	while (1) {
		set_current_state(interruptible);

		smp_mb();
		if (!atomic_read(&md->pending))
			break;

		if (interruptible == TASK_INTERRUPTIBLE &&
		    signal_pending(current)) {
			r = -EINTR;
			break;
		}

		io_schedule();
	}
	set_current_state(TASK_RUNNING);

	remove_wait_queue(&md->wait, &wait);

	return r;
}

static int dm_flush(struct mapped_device *md)
{
	dm_wait_for_completion(md, TASK_UNINTERRUPTIBLE);
	return 0;
}

static void process_barrier(struct mapped_device *md, struct bio *bio)
{
	int error = dm_flush(md);

	if (unlikely(error)) {
		bio_endio(bio, error);
		return;
	}
	if (bio_empty_barrier(bio)) {
		bio_endio(bio, 0);
		return;
	}

	__split_and_process_bio(md, bio);

	error = dm_flush(md);

	if (!error && md->barrier_error)
		error = md->barrier_error;

	if (md->barrier_error != DM_ENDIO_REQUEUE)
		bio_endio(bio, error);
}

/*
 * Process the deferred bios
 */
static void dm_wq_work(struct work_struct *work)
{
	struct mapped_device *md = container_of(work, struct mapped_device,
						work);
	struct bio *c;

	down_write(&md->io_lock);

	while (!test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) {
		spin_lock_irq(&md->deferred_lock);
		c = bio_list_pop(&md->deferred);
		spin_unlock_irq(&md->deferred_lock);

		if (!c) {
			clear_bit(DMF_QUEUE_IO_TO_THREAD, &md->flags);
			break;
		}

		up_write(&md->io_lock);

		if (bio_barrier(c))
			process_barrier(md, c);
		else
			__split_and_process_bio(md, c);

		down_write(&md->io_lock);
	}

	up_write(&md->io_lock);
}

static void dm_queue_flush(struct mapped_device *md)
{
	clear_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	smp_mb__after_clear_bit();
	queue_work(md->wq, &md->work);
}

/*
 * Swap in a new table (destroying old one).
 */
int dm_swap_table(struct mapped_device *md, struct dm_table *table)
{
	int r = -EINVAL;

	mutex_lock(&md->suspend_lock);

	/* device must be suspended */
	if (!dm_suspended(md))
		goto out;

	/* without bdev, the device size cannot be changed */
	if (!md->suspended_bdev)
		if (get_capacity(md->disk) != dm_table_get_size(table))
			goto out;

	__unbind(md);
	r = __bind(md, table);

out:
	mutex_unlock(&md->suspend_lock);
	return r;
}

/*
 * Functions to lock and unlock any filesystem running on the
 * device.
 */
static int lock_fs(struct mapped_device *md)
{
	int r;

	WARN_ON(md->frozen_sb);

	md->frozen_sb = freeze_bdev(md->suspended_bdev);
	if (IS_ERR(md->frozen_sb)) {
		r = PTR_ERR(md->frozen_sb);
		md->frozen_sb = NULL;
		return r;
	}

	set_bit(DMF_FROZEN, &md->flags);

	/* don't bdput right now, we don't want the bdev
	 * to go away while it is locked.
	 */
	return 0;
}

static void unlock_fs(struct mapped_device *md)
{
	if (!test_bit(DMF_FROZEN, &md->flags))
		return;

	thaw_bdev(md->suspended_bdev, md->frozen_sb);
	md->frozen_sb = NULL;
	clear_bit(DMF_FROZEN, &md->flags);
}

/*
 * We need to be able to change a mapping table under a mounted
 * filesystem.  For example we might want to move some data in
 * the background.  Before the table can be swapped with
 * dm_bind_table, dm_suspend must be called to flush any in
 * flight bios and ensure that any further io gets deferred.
 */
int dm_suspend(struct mapped_device *md, unsigned suspend_flags)
{
	struct dm_table *map = NULL;
	int r = 0;
	int do_lockfs = suspend_flags & DM_SUSPEND_LOCKFS_FLAG ? 1 : 0;
	int noflush = suspend_flags & DM_SUSPEND_NOFLUSH_FLAG ? 1 : 0;

	mutex_lock(&md->suspend_lock);

	if (dm_suspended(md)) {
		r = -EINVAL;
		goto out_unlock;
	}

	map = dm_get_table(md);

	/*
	 * DMF_NOFLUSH_SUSPENDING must be set before presuspend.
	 * This flag is cleared before dm_suspend returns.
	 */
	if (noflush)
		set_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);

	/* This does not get reverted if there's an error later. */
	dm_table_presuspend_targets(map);

	/* bdget() can stall if the pending I/Os are not flushed */
	if (!noflush) {
		md->suspended_bdev = bdget_disk(md->disk, 0);
		if (!md->suspended_bdev) {
			DMWARN("bdget failed in dm_suspend");
			r = -ENOMEM;
			goto out;
		}

		/*
		 * Flush I/O to the device. noflush supersedes do_lockfs,
		 * because lock_fs() needs to flush I/Os.
		 */
		if (do_lockfs) {
			r = lock_fs(md);
			if (r)
				goto out;
		}
	}

	/*
	 * Here we must make sure that no processes are submitting requests
	 * to target drivers i.e. no one may be executing
	 * __split_and_process_bio. This is called from dm_request and
	 * dm_wq_work.
	 *
	 * To get all processes out of __split_and_process_bio in dm_request,
	 * we take the write lock. To prevent any process from reentering
	 * __split_and_process_bio from dm_request, we set
	 * DMF_QUEUE_IO_TO_THREAD.
	 *
	 * To quiesce the thread (dm_wq_work), we set DMF_BLOCK_IO_FOR_SUSPEND
	 * and call flush_workqueue(md->wq). flush_workqueue will wait until
	 * dm_wq_work exits and DMF_BLOCK_IO_FOR_SUSPEND will prevent any
	 * further calls to __split_and_process_bio from dm_wq_work.
	 */
	down_write(&md->io_lock);
	set_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	set_bit(DMF_QUEUE_IO_TO_THREAD, &md->flags);
	up_write(&md->io_lock);

	flush_workqueue(md->wq);

	/*
	 * At this point no more requests are entering target request routines.
	 * We call dm_wait_for_completion to wait for all existing requests
	 * to finish.
	 */
	r = dm_wait_for_completion(md, TASK_INTERRUPTIBLE);

	down_write(&md->io_lock);
	if (noflush)
		clear_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);
	up_write(&md->io_lock);

	/* were we interrupted ? */
	if (r < 0) {
		dm_queue_flush(md);

		unlock_fs(md);
		goto out; /* pushback list is already flushed, so skip flush */
	}

	/*
	 * If dm_wait_for_completion returned 0, the device is completely
	 * quiescent now. There is no request-processing activity. All new
	 * requests are being added to md->deferred list.
	 */

	dm_table_postsuspend_targets(map);

	set_bit(DMF_SUSPENDED, &md->flags);

out:
	if (r && md->suspended_bdev) {
		bdput(md->suspended_bdev);
		md->suspended_bdev = NULL;
	}

	dm_table_put(map);

out_unlock:
	mutex_unlock(&md->suspend_lock);
	return r;
}

int dm_resume(struct mapped_device *md)
{
	int r = -EINVAL;
	struct dm_table *map = NULL;

	mutex_lock(&md->suspend_lock);
	if (!dm_suspended(md))
		goto out;

	map = dm_get_table(md);
	if (!map || !dm_table_get_size(map))
		goto out;

	r = dm_table_resume_targets(map);
	if (r)
		goto out;

	dm_queue_flush(md);

	unlock_fs(md);

	if (md->suspended_bdev) {
		bdput(md->suspended_bdev);
		md->suspended_bdev = NULL;
	}

	clear_bit(DMF_SUSPENDED, &md->flags);

	dm_table_unplug_all(map);

	dm_kobject_uevent(md);

	r = 0;

out:
	dm_table_put(map);
	mutex_unlock(&md->suspend_lock);

	return r;
}

/*-----------------------------------------------------------------
 * Event notification.
 *---------------------------------------------------------------*/
void dm_kobject_uevent(struct mapped_device *md)
{
	kobject_uevent(&disk_to_dev(md->disk)->kobj, KOBJ_CHANGE);
}

uint32_t dm_next_uevent_seq(struct mapped_device *md)
{
	return atomic_add_return(1, &md->uevent_seq);
}

uint32_t dm_get_event_nr(struct mapped_device *md)
{
	return atomic_read(&md->event_nr);
}

int dm_wait_event(struct mapped_device *md, int event_nr)
{
	return wait_event_interruptible(md->eventq,
			(event_nr != atomic_read(&md->event_nr)));
}

void dm_uevent_add(struct mapped_device *md, struct list_head *elist)
{
	unsigned long flags;

	spin_lock_irqsave(&md->uevent_lock, flags);
	list_add(elist, &md->uevent_list);
	spin_unlock_irqrestore(&md->uevent_lock, flags);
}

/*
 * The gendisk is only valid as long as you have a reference
 * count on 'md'.
 */
struct gendisk *dm_disk(struct mapped_device *md)
{
	return md->disk;
}

struct kobject *dm_kobject(struct mapped_device *md)
{
	return &md->kobj;
}

/*
 * struct mapped_device should not be exported outside of dm.c
 * so use this check to verify that kobj is part of md structure
 */
struct mapped_device *dm_get_from_kobject(struct kobject *kobj)
{
	struct mapped_device *md;

	md = container_of(kobj, struct mapped_device, kobj);
	if (&md->kobj != kobj)
		return NULL;

	dm_get(md);
	return md;
}

int dm_suspended(struct mapped_device *md)
{
	return test_bit(DMF_SUSPENDED, &md->flags);
}

int dm_noflush_suspending(struct dm_target *ti)
{
	struct mapped_device *md = dm_table_get_md(ti->table);
	int r = __noflush_suspending(md);

	dm_put(md);

	return r;
}
EXPORT_SYMBOL_GPL(dm_noflush_suspending);

static struct block_device_operations dm_blk_dops = {
	.open = dm_blk_open,
	.release = dm_blk_close,
	.ioctl = dm_blk_ioctl,
	.getgeo = dm_blk_getgeo,
	.owner = THIS_MODULE
};

EXPORT_SYMBOL(dm_get_mapinfo);

/*
 * module hooks
 */
module_init(dm_init);
module_exit(dm_exit);

module_param(major, uint, 0);
MODULE_PARM_DESC(major, "The major number of the device mapper");
MODULE_DESCRIPTION(DM_NAME " driver");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
