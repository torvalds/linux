/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 * Copyright (C) 2004-2006 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm.h"
#include "dm-bio-list.h"

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
#include <linux/blktrace_api.h>
#include <linux/smp_lock.h>

#define DM_MSG_PREFIX "core"

static const char *_name = DM_NAME;

static unsigned int major = 0;
static unsigned int _major = 0;

static DEFINE_SPINLOCK(_minor_lock);
/*
 * One of these is allocated per bio.
 */
struct dm_io {
	struct mapped_device *md;
	int error;
	struct bio *bio;
	atomic_t io_count;
	unsigned long start_time;
};

/*
 * One of these is allocated per target within a bio.  Hopefully
 * this will be simplified out one day.
 */
struct dm_target_io {
	struct dm_io *io;
	struct dm_target *ti;
	union map_info info;
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
#define DMF_BLOCK_IO 0
#define DMF_SUSPENDED 1
#define DMF_FROZEN 2
#define DMF_FREEING 3
#define DMF_DELETING 4
#define DMF_NOFLUSH_SUSPENDING 5

struct mapped_device {
	struct rw_semaphore io_lock;
	struct semaphore suspend_lock;
	spinlock_t pushback_lock;
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
	struct bio_list deferred;
	struct bio_list pushback;

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

	/*
	 * freeze/thaw support require holding onto a super block
	 */
	struct super_block *frozen_sb;
	struct block_device *suspended_bdev;

	/* forced geometry settings */
	struct hd_geometry geometry;
};

#define MIN_IOS 256
static struct kmem_cache *_io_cache;
static struct kmem_cache *_tio_cache;

static int __init local_init(void)
{
	int r;

	/* allocate a slab for the dm_ios */
	_io_cache = KMEM_CACHE(dm_io, 0);
	if (!_io_cache)
		return -ENOMEM;

	/* allocate a slab for the target ios */
	_tio_cache = KMEM_CACHE(dm_target_io, 0);
	if (!_tio_cache) {
		kmem_cache_destroy(_io_cache);
		return -ENOMEM;
	}

	_major = major;
	r = register_blkdev(_major, _name);
	if (r < 0) {
		kmem_cache_destroy(_tio_cache);
		kmem_cache_destroy(_io_cache);
		return r;
	}

	if (!_major)
		_major = r;

	return 0;
}

static void local_exit(void)
{
	kmem_cache_destroy(_tio_cache);
	kmem_cache_destroy(_io_cache);
	unregister_blkdev(_major, _name);

	_major = 0;

	DMINFO("cleaned up");
}

int (*_inits[])(void) __initdata = {
	local_init,
	dm_target_init,
	dm_linear_init,
	dm_stripe_init,
	dm_interface_init,
};

void (*_exits[])(void) = {
	local_exit,
	dm_target_exit,
	dm_linear_exit,
	dm_stripe_exit,
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
static int dm_blk_open(struct inode *inode, struct file *file)
{
	struct mapped_device *md;

	spin_lock(&_minor_lock);

	md = inode->i_bdev->bd_disk->private_data;
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

static int dm_blk_close(struct inode *inode, struct file *file)
{
	struct mapped_device *md;

	md = inode->i_bdev->bd_disk->private_data;
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

static int dm_blk_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct mapped_device *md;
	struct dm_table *map;
	struct dm_target *tgt;
	int r = -ENOTTY;

	/* We don't really need this lock, but we do need 'inode'. */
	unlock_kernel();

	md = inode->i_bdev->bd_disk->private_data;

	map = dm_get_table(md);

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
		r = tgt->type->ioctl(tgt, inode, file, cmd, arg);

out:
	dm_table_put(map);

	lock_kernel();
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

	io->start_time = jiffies;

	preempt_disable();
	disk_round_stats(dm_disk(md));
	preempt_enable();
	dm_disk(md)->in_flight = atomic_inc_return(&md->pending);
}

static int end_io_acct(struct dm_io *io)
{
	struct mapped_device *md = io->md;
	struct bio *bio = io->bio;
	unsigned long duration = jiffies - io->start_time;
	int pending;
	int rw = bio_data_dir(bio);

	preempt_disable();
	disk_round_stats(dm_disk(md));
	preempt_enable();
	dm_disk(md)->in_flight = pending = atomic_dec_return(&md->pending);

	disk_stat_add(dm_disk(md), ticks[rw], duration);

	return !pending;
}

/*
 * Add the bio to the list of deferred io.
 */
static int queue_io(struct mapped_device *md, struct bio *bio)
{
	down_write(&md->io_lock);

	if (!test_bit(DMF_BLOCK_IO, &md->flags)) {
		up_write(&md->io_lock);
		return 1;
	}

	bio_list_add(&md->deferred, bio);

	up_write(&md->io_lock);
	return 0;		/* deferred successfully */
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

	/* Push-back supersedes any I/O errors */
	if (error && !(io->error > 0 && __noflush_suspending(io->md)))
		io->error = error;

	if (atomic_dec_and_test(&io->io_count)) {
		if (io->error == DM_ENDIO_REQUEUE) {
			/*
			 * Target requested pushing back the I/O.
			 * This must be handled before the sleeper on
			 * suspend queue merges the pushback list.
			 */
			spin_lock_irqsave(&io->md->pushback_lock, flags);
			if (__noflush_suspending(io->md))
				bio_list_add(&io->md->pushback, io->bio);
			else
				/* noflush suspend was interrupted. */
				io->error = -EIO;
			spin_unlock_irqrestore(&io->md->pushback_lock, flags);
		}

		if (end_io_acct(io))
			/* nudge anyone waiting on suspend queue */
			wake_up(&io->md->wait);

		if (io->error != DM_ENDIO_REQUEUE) {
			blk_add_trace_bio(io->md->queue, io->bio,
					  BLK_TA_COMPLETE);

			bio_endio(io->bio, io->error);
		}

		free_io(io->md, io);
	}
}

static void clone_endio(struct bio *bio, int error)
{
	int r = 0;
	struct dm_target_io *tio = bio->bi_private;
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

	dec_pending(tio->io, error);

	/*
	 * Store md for cleanup instead of tio which is about to get freed.
	 */
	bio->bi_private = md->bs;

	bio_put(bio);
	free_tio(md, tio);
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

		blk_add_trace_remap(bdev_get_queue(clone->bi_bdev), clone,
				    tio->io->bio->bi_bdev->bd_dev,
				    clone->bi_sector, sector);

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
	clone->bi_rw = bio->bi_rw;
	clone->bi_vcnt = 1;
	clone->bi_size = to_bytes(len);
	clone->bi_io_vec->bv_offset = offset;
	clone->bi_io_vec->bv_len = clone->bi_size;

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
	clone->bi_destructor = dm_bio_destructor;
	clone->bi_sector = sector;
	clone->bi_idx = idx;
	clone->bi_vcnt = idx + bv_count;
	clone->bi_size = to_bytes(len);
	clone->bi_flags &= ~(1 << BIO_SEG_VALID);

	return clone;
}

static void __clone_and_map(struct clone_info *ci)
{
	struct bio *clone, *bio = ci->bio;
	struct dm_target *ti = dm_table_find_target(ci->map, ci->sector);
	sector_t len = 0, max = max_io_len(ci->md, ci->sector, ti);
	struct dm_target_io *tio;

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
}

/*
 * Split the bio into several clones.
 */
static void __split_bio(struct mapped_device *md, struct bio *bio)
{
	struct clone_info ci;

	ci.map = dm_get_table(md);
	if (!ci.map) {
		bio_io_error(bio);
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
	while (ci.sector_count)
		__clone_and_map(&ci);

	/* drop the extra reference count */
	dec_pending(ci.io, 0);
	dm_table_put(ci.map);
}
/*-----------------------------------------------------------------
 * CRUD END
 *---------------------------------------------------------------*/

/*
 * The request function that just remaps the bio built up by
 * dm_merge_bvec.
 */
static int dm_request(struct request_queue *q, struct bio *bio)
{
	int r;
	int rw = bio_data_dir(bio);
	struct mapped_device *md = q->queuedata;

	/*
	 * There is no use in forwarding any barrier request since we can't
	 * guarantee it is (or can be) handled by the targets correctly.
	 */
	if (unlikely(bio_barrier(bio))) {
		bio_endio(bio, -EOPNOTSUPP);
		return 0;
	}

	down_read(&md->io_lock);

	disk_stat_inc(dm_disk(md), ios[rw]);
	disk_stat_add(dm_disk(md), sectors[rw], bio_sectors(bio));

	/*
	 * If we're suspended we have to queue
	 * this io for later.
	 */
	while (test_bit(DMF_BLOCK_IO, &md->flags)) {
		up_read(&md->io_lock);

		if (bio_rw(bio) == READA) {
			bio_io_error(bio);
			return 0;
		}

		r = queue_io(md, bio);
		if (r < 0) {
			bio_io_error(bio);
			return 0;

		} else if (r == 0)
			return 0;	/* deferred successfully */

		/*
		 * We're in a while loop, because someone could suspend
		 * before we get to the following read lock.
		 */
		down_read(&md->io_lock);
	}

	__split_bio(md, bio);
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
	int r;
	struct mapped_device *md = (struct mapped_device *) congested_data;
	struct dm_table *map = dm_get_table(md);

	if (!map || test_bit(DMF_BLOCK_IO, &md->flags))
		r = bdi_bits;
	else
		r = dm_table_any_congested(map, bdi_bits);

	dm_table_put(map);
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
static int specific_minor(struct mapped_device *md, int minor)
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

static int next_free_minor(struct mapped_device *md, int *minor)
{
	int r, m;

	r = idr_pre_get(&_minor_idr, GFP_KERNEL);
	if (!r)
		return -ENOMEM;

	spin_lock(&_minor_lock);

	r = idr_get_new(&_minor_idr, MINOR_ALLOCED, &m);
	if (r) {
		goto out;
	}

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

/*
 * Allocate and initialise a blank device with a given minor.
 */
static struct mapped_device *alloc_dev(int minor)
{
	int r;
	struct mapped_device *md = kmalloc(sizeof(*md), GFP_KERNEL);
	void *old_md;

	if (!md) {
		DMWARN("unable to allocate device, out of memory.");
		return NULL;
	}

	if (!try_module_get(THIS_MODULE))
		goto bad0;

	/* get a minor number for the dev */
	if (minor == DM_ANY_MINOR)
		r = next_free_minor(md, &minor);
	else
		r = specific_minor(md, minor);
	if (r < 0)
		goto bad1;

	memset(md, 0, sizeof(*md));
	init_rwsem(&md->io_lock);
	init_MUTEX(&md->suspend_lock);
	spin_lock_init(&md->pushback_lock);
	rwlock_init(&md->map_lock);
	atomic_set(&md->holders, 1);
	atomic_set(&md->open_count, 0);
	atomic_set(&md->event_nr, 0);

	md->queue = blk_alloc_queue(GFP_KERNEL);
	if (!md->queue)
		goto bad1_free_minor;

	md->queue->queuedata = md;
	md->queue->backing_dev_info.congested_fn = dm_any_congested;
	md->queue->backing_dev_info.congested_data = md;
	blk_queue_make_request(md->queue, dm_request);
	blk_queue_bounce_limit(md->queue, BLK_BOUNCE_ANY);
	md->queue->unplug_fn = dm_unplug_all;

	md->io_pool = mempool_create_slab_pool(MIN_IOS, _io_cache);
	if (!md->io_pool)
		goto bad2;

	md->tio_pool = mempool_create_slab_pool(MIN_IOS, _tio_cache);
	if (!md->tio_pool)
		goto bad3;

	md->bs = bioset_create(16, 16);
	if (!md->bs)
		goto bad_no_bioset;

	md->disk = alloc_disk(1);
	if (!md->disk)
		goto bad4;

	atomic_set(&md->pending, 0);
	init_waitqueue_head(&md->wait);
	init_waitqueue_head(&md->eventq);

	md->disk->major = _major;
	md->disk->first_minor = minor;
	md->disk->fops = &dm_blk_dops;
	md->disk->queue = md->queue;
	md->disk->private_data = md;
	sprintf(md->disk->disk_name, "dm-%d", minor);
	add_disk(md->disk);
	format_dev_t(md->name, MKDEV(_major, minor));

	/* Populate the mapping, nobody knows we exist yet */
	spin_lock(&_minor_lock);
	old_md = idr_replace(&_minor_idr, md, minor);
	spin_unlock(&_minor_lock);

	BUG_ON(old_md != MINOR_ALLOCED);

	return md;

 bad4:
	bioset_free(md->bs);
 bad_no_bioset:
	mempool_destroy(md->tio_pool);
 bad3:
	mempool_destroy(md->io_pool);
 bad2:
	blk_cleanup_queue(md->queue);
 bad1_free_minor:
	free_minor(minor);
 bad1:
	module_put(THIS_MODULE);
 bad0:
	kfree(md);
	return NULL;
}

static void free_dev(struct mapped_device *md)
{
	int minor = md->disk->first_minor;

	if (md->suspended_bdev) {
		thaw_bdev(md->suspended_bdev, NULL);
		bdput(md->suspended_bdev);
	}
	mempool_destroy(md->tio_pool);
	mempool_destroy(md->io_pool);
	bioset_free(md->bs);
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
	struct mapped_device *md = (struct mapped_device *) context;

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
	if (size == 0)
		return 0;

	dm_table_get(t);
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
	dm_table_put(map);
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
		   (dm_disk(md)->first_minor != minor) ||
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
		idr_replace(&_minor_idr, MINOR_ALLOCED, dm_disk(md)->first_minor);
		set_bit(DMF_FREEING, &md->flags);
		spin_unlock(&_minor_lock);
		if (!dm_suspended(md)) {
			dm_table_presuspend_targets(map);
			dm_table_postsuspend_targets(map);
		}
		__unbind(md);
		dm_table_put(map);
		free_dev(md);
	}
}
EXPORT_SYMBOL_GPL(dm_put);

/*
 * Process the deferred bios
 */
static void __flush_deferred_io(struct mapped_device *md, struct bio *c)
{
	struct bio *n;

	while (c) {
		n = c->bi_next;
		c->bi_next = NULL;
		__split_bio(md, c);
		c = n;
	}
}

/*
 * Swap in a new table (destroying old one).
 */
int dm_swap_table(struct mapped_device *md, struct dm_table *table)
{
	int r = -EINVAL;

	down(&md->suspend_lock);

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
	up(&md->suspend_lock);
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
	unsigned long flags;
	DECLARE_WAITQUEUE(wait, current);
	struct bio *def;
	int r = -EINVAL;
	int do_lockfs = suspend_flags & DM_SUSPEND_LOCKFS_FLAG ? 1 : 0;
	int noflush = suspend_flags & DM_SUSPEND_NOFLUSH_FLAG ? 1 : 0;

	down(&md->suspend_lock);

	if (dm_suspended(md))
		goto out_unlock;

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
			goto flush_and_out;
		}
	}

	/*
	 * Flush I/O to the device.
	 * noflush supersedes do_lockfs, because lock_fs() needs to flush I/Os.
	 */
	if (do_lockfs && !noflush) {
		r = lock_fs(md);
		if (r)
			goto out;
	}

	/*
	 * First we set the BLOCK_IO flag so no more ios will be mapped.
	 */
	down_write(&md->io_lock);
	set_bit(DMF_BLOCK_IO, &md->flags);

	add_wait_queue(&md->wait, &wait);
	up_write(&md->io_lock);

	/* unplug */
	if (map)
		dm_table_unplug_all(map);

	/*
	 * Then we wait for the already mapped ios to
	 * complete.
	 */
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (!atomic_read(&md->pending) || signal_pending(current))
			break;

		io_schedule();
	}
	set_current_state(TASK_RUNNING);

	down_write(&md->io_lock);
	remove_wait_queue(&md->wait, &wait);

	if (noflush) {
		spin_lock_irqsave(&md->pushback_lock, flags);
		clear_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);
		bio_list_merge_head(&md->deferred, &md->pushback);
		bio_list_init(&md->pushback);
		spin_unlock_irqrestore(&md->pushback_lock, flags);
	}

	/* were we interrupted ? */
	r = -EINTR;
	if (atomic_read(&md->pending)) {
		clear_bit(DMF_BLOCK_IO, &md->flags);
		def = bio_list_get(&md->deferred);
		__flush_deferred_io(md, def);
		up_write(&md->io_lock);
		unlock_fs(md);
		goto out; /* pushback list is already flushed, so skip flush */
	}
	up_write(&md->io_lock);

	dm_table_postsuspend_targets(map);

	set_bit(DMF_SUSPENDED, &md->flags);

	r = 0;

flush_and_out:
	if (r && noflush) {
		/*
		 * Because there may be already I/Os in the pushback list,
		 * flush them before return.
		 */
		down_write(&md->io_lock);

		spin_lock_irqsave(&md->pushback_lock, flags);
		clear_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);
		bio_list_merge_head(&md->deferred, &md->pushback);
		bio_list_init(&md->pushback);
		spin_unlock_irqrestore(&md->pushback_lock, flags);

		def = bio_list_get(&md->deferred);
		__flush_deferred_io(md, def);
		up_write(&md->io_lock);
	}

out:
	if (r && md->suspended_bdev) {
		bdput(md->suspended_bdev);
		md->suspended_bdev = NULL;
	}

	dm_table_put(map);

out_unlock:
	up(&md->suspend_lock);
	return r;
}

int dm_resume(struct mapped_device *md)
{
	int r = -EINVAL;
	struct bio *def;
	struct dm_table *map = NULL;

	down(&md->suspend_lock);
	if (!dm_suspended(md))
		goto out;

	map = dm_get_table(md);
	if (!map || !dm_table_get_size(map))
		goto out;

	r = dm_table_resume_targets(map);
	if (r)
		goto out;

	down_write(&md->io_lock);
	clear_bit(DMF_BLOCK_IO, &md->flags);

	def = bio_list_get(&md->deferred);
	__flush_deferred_io(md, def);
	up_write(&md->io_lock);

	unlock_fs(md);

	if (md->suspended_bdev) {
		bdput(md->suspended_bdev);
		md->suspended_bdev = NULL;
	}

	clear_bit(DMF_SUSPENDED, &md->flags);

	dm_table_unplug_all(map);

	kobject_uevent(&md->disk->kobj, KOBJ_CHANGE);

	r = 0;

out:
	dm_table_put(map);
	up(&md->suspend_lock);

	return r;
}

/*-----------------------------------------------------------------
 * Event notification.
 *---------------------------------------------------------------*/
uint32_t dm_get_event_nr(struct mapped_device *md)
{
	return atomic_read(&md->event_nr);
}

int dm_wait_event(struct mapped_device *md, int event_nr)
{
	return wait_event_interruptible(md->eventq,
			(event_nr != atomic_read(&md->event_nr)));
}

/*
 * The gendisk is only valid as long as you have a reference
 * count on 'md'.
 */
struct gendisk *dm_disk(struct mapped_device *md)
{
	return md->disk;
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
