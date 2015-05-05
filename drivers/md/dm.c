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
#include <linux/mempool.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/hdreg.h>
#include <linux/delay.h>

#include <trace/events/block.h>

#define DM_MSG_PREFIX "core"

#ifdef CONFIG_PRINTK
/*
 * ratelimit state to be used in DMXXX_LIMIT().
 */
DEFINE_RATELIMIT_STATE(dm_ratelimit_state,
		       DEFAULT_RATELIMIT_INTERVAL,
		       DEFAULT_RATELIMIT_BURST);
EXPORT_SYMBOL(dm_ratelimit_state);
#endif

/*
 * Cookies are numeric values sent with CHANGE and REMOVE
 * uevents while resuming, removing or renaming the device.
 */
#define DM_COOKIE_ENV_VAR_NAME "DM_COOKIE"
#define DM_COOKIE_LENGTH 24

static const char *_name = DM_NAME;

static unsigned int major = 0;
static unsigned int _major = 0;

static DEFINE_IDR(_minor_idr);

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
	spinlock_t endio_lock;
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
 * For request-based dm - the bio clones we allocate are embedded in these
 * structs.
 *
 * We allocate these with bio_alloc_bioset, using the front_pad parameter when
 * the bioset is created - this means the bio has to come at the end of the
 * struct.
 */
struct dm_rq_clone_bio_info {
	struct bio *orig;
	struct dm_rq_target_io *tio;
	struct bio clone;
};

union map_info *dm_get_mapinfo(struct bio *bio)
{
	if (bio && bio->bi_private)
		return &((struct dm_target_io *)bio->bi_private)->info;
	return NULL;
}

union map_info *dm_get_rq_mapinfo(struct request *rq)
{
	if (rq && rq->end_io_data)
		return &((struct dm_rq_target_io *)rq->end_io_data)->info;
	return NULL;
}
EXPORT_SYMBOL_GPL(dm_get_rq_mapinfo);

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
#define DMF_MERGE_IS_OPTIONAL 6

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
	unsigned type;
	/* Protect queue and type against concurrent access. */
	struct mutex type_lock;

	struct target_type *immutable_target_type;

	struct gendisk *disk;
	char name[16];

	void *interface_ptr;

	/*
	 * A list of ios that arrived while we were suspended.
	 */
	atomic_t pending[2];
	wait_queue_head_t wait;
	struct work_struct work;
	struct bio_list deferred;
	spinlock_t deferred_lock;

	/*
	 * Processing queue (flush)
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
	struct block_device *bdev;

	/* forced geometry settings */
	struct hd_geometry geometry;

	/* kobject and completion */
	struct dm_kobject_holder kobj_holder;

	/* zero-length flush that will be cloned and submitted to targets */
	struct bio flush_bio;
};

/*
 * For mempools pre-allocation at the table loading time.
 */
struct dm_md_mempools {
	mempool_t *io_pool;
	struct bio_set *bs;
};

#define MIN_IOS 256
static struct kmem_cache *_io_cache;
static struct kmem_cache *_rq_tio_cache;

static int __init local_init(void)
{
	int r = -ENOMEM;

	/* allocate a slab for the dm_ios */
	_io_cache = KMEM_CACHE(dm_io, 0);
	if (!_io_cache)
		return r;

	_rq_tio_cache = KMEM_CACHE(dm_rq_target_io, 0);
	if (!_rq_tio_cache)
		goto out_free_io_cache;

	r = dm_uevent_init();
	if (r)
		goto out_free_rq_tio_cache;

	_major = major;
	r = register_blkdev(_major, _name);
	if (r < 0)
		goto out_uevent_exit;

	if (!_major)
		_major = r;

	return 0;

out_uevent_exit:
	dm_uevent_exit();
out_free_rq_tio_cache:
	kmem_cache_destroy(_rq_tio_cache);
out_free_io_cache:
	kmem_cache_destroy(_io_cache);

	return r;
}

static void local_exit(void)
{
	kmem_cache_destroy(_rq_tio_cache);
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
	dm_io_init,
	dm_kcopyd_init,
	dm_interface_init,
};

static void (*_exits[])(void) = {
	local_exit,
	dm_target_exit,
	dm_linear_exit,
	dm_stripe_exit,
	dm_io_exit,
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

	/*
	 * Should be empty by this point.
	 */
	idr_destroy(&_minor_idr);
}

/*
 * Block device functions
 */
int dm_deleting_md(struct mapped_device *md)
{
	return test_bit(DMF_DELETING, &md->flags);
}

static int dm_blk_open(struct block_device *bdev, fmode_t mode)
{
	struct mapped_device *md;

	spin_lock(&_minor_lock);

	md = bdev->bd_disk->private_data;
	if (!md)
		goto out;

	if (test_bit(DMF_FREEING, &md->flags) ||
	    dm_deleting_md(md)) {
		md = NULL;
		goto out;
	}

	dm_get(md);
	atomic_inc(&md->open_count);

out:
	spin_unlock(&_minor_lock);

	return md ? 0 : -ENXIO;
}

static void dm_blk_close(struct gendisk *disk, fmode_t mode)
{
	struct mapped_device *md = disk->private_data;

	spin_lock(&_minor_lock);

	atomic_dec(&md->open_count);
	dm_put(md);

	spin_unlock(&_minor_lock);
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
	struct dm_table *map;
	struct dm_target *tgt;
	int r = -ENOTTY;

retry:
	map = dm_get_live_table(md);
	if (!map || !dm_table_get_size(map))
		goto out;

	/* We only support devices that have a single target */
	if (dm_table_get_num_targets(map) != 1)
		goto out;

	tgt = dm_table_get_target(map, 0);

	if (dm_suspended_md(md)) {
		r = -EAGAIN;
		goto out;
	}

	if (tgt->type->ioctl)
		r = tgt->type->ioctl(tgt, cmd, arg);

out:
	dm_table_put(map);

	if (r == -ENOTCONN) {
		msleep(10);
		goto retry;
	}

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

static void free_tio(struct mapped_device *md, struct dm_target_io *tio)
{
	bio_put(&tio->clone);
}

static struct dm_rq_target_io *alloc_rq_tio(struct mapped_device *md,
					    gfp_t gfp_mask)
{
	return mempool_alloc(md->io_pool, gfp_mask);
}

static void free_rq_tio(struct dm_rq_target_io *tio)
{
	mempool_free(tio, tio->md->io_pool);
}

static int md_in_flight(struct mapped_device *md)
{
	return atomic_read(&md->pending[READ]) +
	       atomic_read(&md->pending[WRITE]);
}

static void start_io_acct(struct dm_io *io)
{
	struct mapped_device *md = io->md;
	int cpu;
	int rw = bio_data_dir(io->bio);

	io->start_time = jiffies;

	cpu = part_stat_lock();
	part_round_stats(cpu, &dm_disk(md)->part0);
	part_stat_unlock();
	atomic_set(&dm_disk(md)->part0.in_flight[rw],
		atomic_inc_return(&md->pending[rw]));
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
	 * a flush.
	 */
	pending = atomic_dec_return(&md->pending[rw]);
	atomic_set(&dm_disk(md)->part0.in_flight[rw], pending);
	pending += atomic_read(&md->pending[rw^0x1]);

	/* nudge anyone waiting on suspend queue */
	if (!pending)
		wake_up(&md->wait);
}

/*
 * Add the bio to the list of deferred io.
 */
static void queue_io(struct mapped_device *md, struct bio *bio)
{
	unsigned long flags;

	spin_lock_irqsave(&md->deferred_lock, flags);
	bio_list_add(&md->deferred, bio);
	spin_unlock_irqrestore(&md->deferred_lock, flags);
	queue_work(md->wq, &md->work);
}

/*
 * Everyone (including functions in this file), should use this
 * function to access the md->map field, and make sure they call
 * dm_table_put() when finished.
 */
struct dm_table *dm_get_live_table(struct mapped_device *md)
{
	struct dm_table *t;
	unsigned long flags;

	read_lock_irqsave(&md->map_lock, flags);
	t = md->map;
	if (t)
		dm_table_get(t);
	read_unlock_irqrestore(&md->map_lock, flags);

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
	if (unlikely(error)) {
		spin_lock_irqsave(&io->endio_lock, flags);
		if (!(io->error > 0 && __noflush_suspending(md)))
			io->error = error;
		spin_unlock_irqrestore(&io->endio_lock, flags);
	}

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
		end_io_acct(io);
		free_io(md, io);

		if (io_error == DM_ENDIO_REQUEUE)
			return;

		if ((bio->bi_rw & REQ_FLUSH) && bio->bi_size) {
			/*
			 * Preflush done for flush with data, reissue
			 * without REQ_FLUSH.
			 */
			bio->bi_rw &= ~REQ_FLUSH;
			queue_io(md, bio);
		} else {
			/* done with normal IO or empty flush */
			trace_block_bio_complete(md->queue, bio, io_error);
			bio_endio(bio, io_error);
		}
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
		r = endio(tio->ti, bio, error);
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

	free_tio(md, tio);
	dec_pending(io, error);
}

/*
 * Partial completion handling for request-based dm
 */
static void end_clone_bio(struct bio *clone, int error)
{
	struct dm_rq_clone_bio_info *info = clone->bi_private;
	struct dm_rq_target_io *tio = info->tio;
	struct bio *bio = info->orig;
	unsigned int nr_bytes = info->orig->bi_size;

	bio_put(clone);

	if (tio->error)
		/*
		 * An error has already been detected on the request.
		 * Once error occurred, just let clone->end_io() handle
		 * the remainder.
		 */
		return;
	else if (error) {
		/*
		 * Don't notice the error to the upper layer yet.
		 * The error handling decision is made by the target driver,
		 * when the request is completed.
		 */
		tio->error = error;
		return;
	}

	/*
	 * I/O for the bio successfully completed.
	 * Notice the data completion to the upper layer.
	 */

	/*
	 * bios are processed from the head of the list.
	 * So the completing bio should always be rq->bio.
	 * If it's not, something wrong is happening.
	 */
	if (tio->orig->bio != bio)
		DMERR("bio completion is going in the middle of the request");

	/*
	 * Update the original request.
	 * Do not use blk_end_request() here, because it may complete
	 * the original request before the clone, and break the ordering.
	 */
	blk_update_request(tio->orig, 0, nr_bytes);
}

/*
 * Don't touch any member of the md after calling this function because
 * the md may be freed in dm_put() at the end of this function.
 * Or do dm_get() before calling this function and dm_put() later.
 */
static void rq_completed(struct mapped_device *md, int rw, int run_queue)
{
	atomic_dec(&md->pending[rw]);

	/* nudge anyone waiting on suspend queue */
	if (!md_in_flight(md))
		wake_up(&md->wait);

	/*
	 * Run this off this callpath, as drivers could invoke end_io while
	 * inside their request_fn (and holding the queue lock). Calling
	 * back into ->request_fn() could deadlock attempting to grab the
	 * queue lock again.
	 */
	if (run_queue)
		blk_run_queue_async(md->queue);

	/*
	 * dm_put() must be at the end of this function. See the comment above
	 */
	dm_put(md);
}

static void free_rq_clone(struct request *clone)
{
	struct dm_rq_target_io *tio = clone->end_io_data;

	blk_rq_unprep_clone(clone);
	free_rq_tio(tio);
}

/*
 * Complete the clone and the original request.
 * Must be called without queue lock.
 */
static void dm_end_request(struct request *clone, int error)
{
	int rw = rq_data_dir(clone);
	struct dm_rq_target_io *tio = clone->end_io_data;
	struct mapped_device *md = tio->md;
	struct request *rq = tio->orig;

	if (rq->cmd_type == REQ_TYPE_BLOCK_PC) {
		rq->errors = clone->errors;
		rq->resid_len = clone->resid_len;

		if (rq->sense)
			/*
			 * We are using the sense buffer of the original
			 * request.
			 * So setting the length of the sense data is enough.
			 */
			rq->sense_len = clone->sense_len;
	}

	free_rq_clone(clone);
	blk_end_request_all(rq, error);
	rq_completed(md, rw, true);
}

static void dm_unprep_request(struct request *rq)
{
	struct request *clone = rq->special;

	rq->special = NULL;
	rq->cmd_flags &= ~REQ_DONTPREP;

	free_rq_clone(clone);
}

/*
 * Requeue the original request of a clone.
 */
void dm_requeue_unmapped_request(struct request *clone)
{
	int rw = rq_data_dir(clone);
	struct dm_rq_target_io *tio = clone->end_io_data;
	struct mapped_device *md = tio->md;
	struct request *rq = tio->orig;
	struct request_queue *q = rq->q;
	unsigned long flags;

	dm_unprep_request(rq);

	spin_lock_irqsave(q->queue_lock, flags);
	blk_requeue_request(q, rq);
	spin_unlock_irqrestore(q->queue_lock, flags);

	rq_completed(md, rw, 0);
}
EXPORT_SYMBOL_GPL(dm_requeue_unmapped_request);

static void __stop_queue(struct request_queue *q)
{
	blk_stop_queue(q);
}

static void stop_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__stop_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void __start_queue(struct request_queue *q)
{
	if (blk_queue_stopped(q))
		blk_start_queue(q);
}

static void start_queue(struct request_queue *q)
{
	unsigned long flags;

	spin_lock_irqsave(q->queue_lock, flags);
	__start_queue(q);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void dm_done(struct request *clone, int error, bool mapped)
{
	int r = error;
	struct dm_rq_target_io *tio = clone->end_io_data;
	dm_request_endio_fn rq_end_io = NULL;

	if (tio->ti) {
		rq_end_io = tio->ti->type->rq_end_io;

		if (mapped && rq_end_io)
			r = rq_end_io(tio->ti, clone, error, &tio->info);
	}

	if (r <= 0)
		/* The target wants to complete the I/O */
		dm_end_request(clone, r);
	else if (r == DM_ENDIO_INCOMPLETE)
		/* The target will handle the I/O */
		return;
	else if (r == DM_ENDIO_REQUEUE)
		/* The target wants to requeue the I/O */
		dm_requeue_unmapped_request(clone);
	else {
		DMWARN("unimplemented target endio return value: %d", r);
		BUG();
	}
}

/*
 * Request completion handler for request-based dm
 */
static void dm_softirq_done(struct request *rq)
{
	bool mapped = true;
	struct request *clone = rq->completion_data;
	struct dm_rq_target_io *tio = clone->end_io_data;

	if (rq->cmd_flags & REQ_FAILED)
		mapped = false;

	dm_done(clone, tio->error, mapped);
}

/*
 * Complete the clone and the original request with the error status
 * through softirq context.
 */
static void dm_complete_request(struct request *clone, int error)
{
	struct dm_rq_target_io *tio = clone->end_io_data;
	struct request *rq = tio->orig;

	tio->error = error;
	rq->completion_data = clone;
	blk_complete_request(rq);
}

/*
 * Complete the not-mapped clone and the original request with the error status
 * through softirq context.
 * Target's rq_end_io() function isn't called.
 * This may be used when the target's map_rq() function fails.
 */
void dm_kill_unmapped_request(struct request *clone, int error)
{
	struct dm_rq_target_io *tio = clone->end_io_data;
	struct request *rq = tio->orig;

	rq->cmd_flags |= REQ_FAILED;
	dm_complete_request(clone, error);
}
EXPORT_SYMBOL_GPL(dm_kill_unmapped_request);

/*
 * Called with the queue lock held
 */
static void end_clone_request(struct request *clone, int error)
{
	/*
	 * For just cleaning up the information of the queue in which
	 * the clone was dispatched.
	 * The clone is *NOT* freed actually here because it is alloced from
	 * dm own mempool and REQ_ALLOCED isn't set in clone->cmd_flags.
	 */
	__blk_put_request(clone->q, clone);

	/*
	 * Actual request completion is done in a softirq context which doesn't
	 * hold the queue lock.  Otherwise, deadlock could occur because:
	 *     - another request may be submitted by the upper level driver
	 *       of the stacking during the completion
	 *     - the submission which requires queue lock may be done
	 *       against this queue
	 */
	dm_complete_request(clone, error);
}

/*
 * Return maximum size of I/O possible at the supplied sector up to the current
 * target boundary.
 */
static sector_t max_io_len_target_boundary(sector_t sector, struct dm_target *ti)
{
	sector_t target_offset = dm_target_offset(ti, sector);

	return ti->len - target_offset;
}

static sector_t max_io_len(sector_t sector, struct dm_target *ti)
{
	sector_t len = max_io_len_target_boundary(sector, ti);
	sector_t offset, max_len;

	/*
	 * Does the target need to split even further?
	 */
	if (ti->max_io_len) {
		offset = dm_target_offset(ti, sector);
		if (unlikely(ti->max_io_len & (ti->max_io_len - 1)))
			max_len = sector_div(offset, ti->max_io_len);
		else
			max_len = offset & (ti->max_io_len - 1);
		max_len = ti->max_io_len - max_len;

		if (len > max_len)
			len = max_len;
	}

	return len;
}

int dm_set_target_max_io_len(struct dm_target *ti, sector_t len)
{
	if (len > UINT_MAX) {
		DMERR("Specified maximum size of target IO (%llu) exceeds limit (%u)",
		      (unsigned long long)len, UINT_MAX);
		ti->error = "Maximum size of target IO is too large";
		return -EINVAL;
	}

	ti->max_io_len = (uint32_t) len;

	return 0;
}
EXPORT_SYMBOL_GPL(dm_set_target_max_io_len);

static void __map_bio(struct dm_target_io *tio)
{
	int r;
	sector_t sector;
	struct mapped_device *md;
	struct bio *clone = &tio->clone;
	struct dm_target *ti = tio->ti;

	clone->bi_end_io = clone_endio;
	clone->bi_private = tio;

	/*
	 * Map the clone.  If r == 0 we don't need to do
	 * anything, the target has assumed ownership of
	 * this io.
	 */
	atomic_inc(&tio->io->io_count);
	sector = clone->bi_sector;
	r = ti->type->map(ti, clone);
	if (r == DM_MAPIO_REMAPPED) {
		/* the bio has been remapped so dispatch it */

		trace_block_bio_remap(bdev_get_queue(clone->bi_bdev), clone,
				      tio->io->bio->bi_bdev->bd_dev, sector);

		generic_make_request(clone);
	} else if (r < 0 || r == DM_MAPIO_REQUEUE) {
		/* error the io and bail out, or requeue it if needed */
		md = tio->io->md;
		dec_pending(tio->io, r);
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

static void bio_setup_sector(struct bio *bio, sector_t sector, sector_t len)
{
	bio->bi_sector = sector;
	bio->bi_size = to_bytes(len);
}

static void bio_setup_bv(struct bio *bio, unsigned short idx, unsigned short bv_count)
{
	bio->bi_idx = idx;
	bio->bi_vcnt = idx + bv_count;
	bio->bi_flags &= ~(1 << BIO_SEG_VALID);
}

static void clone_bio_integrity(struct bio *bio, struct bio *clone,
				unsigned short idx, unsigned len, unsigned offset,
				unsigned trim)
{
	if (!bio_integrity(bio))
		return;

	bio_integrity_clone(clone, bio, GFP_NOIO);

	if (trim)
		bio_integrity_trim(clone, bio_sector_offset(bio, idx, offset), len);
}

/*
 * Creates a little bio that just does part of a bvec.
 */
static void clone_split_bio(struct dm_target_io *tio, struct bio *bio,
			    sector_t sector, unsigned short idx,
			    unsigned offset, unsigned len)
{
	struct bio *clone = &tio->clone;
	struct bio_vec *bv = bio->bi_io_vec + idx;

	*clone->bi_io_vec = *bv;

	bio_setup_sector(clone, sector, len);

	clone->bi_bdev = bio->bi_bdev;
	clone->bi_rw = bio->bi_rw;
	clone->bi_vcnt = 1;
	clone->bi_io_vec->bv_offset = offset;
	clone->bi_io_vec->bv_len = clone->bi_size;
	clone->bi_flags |= 1 << BIO_CLONED;

	clone_bio_integrity(bio, clone, idx, len, offset, 1);
}

/*
 * Creates a bio that consists of range of complete bvecs.
 */
static void clone_bio(struct dm_target_io *tio, struct bio *bio,
		      sector_t sector, unsigned short idx,
		      unsigned short bv_count, unsigned len)
{
	struct bio *clone = &tio->clone;
	unsigned trim = 0;

	__bio_clone(clone, bio);
	bio_setup_sector(clone, sector, len);
	bio_setup_bv(clone, idx, bv_count);

	if (idx != bio->bi_idx || clone->bi_size < bio->bi_size)
		trim = 1;
	clone_bio_integrity(bio, clone, idx, len, 0, trim);
}

static struct dm_target_io *alloc_tio(struct clone_info *ci,
				      struct dm_target *ti, int nr_iovecs,
				      unsigned target_bio_nr)
{
	struct dm_target_io *tio;
	struct bio *clone;

	clone = bio_alloc_bioset(GFP_NOIO, nr_iovecs, ci->md->bs);
	tio = container_of(clone, struct dm_target_io, clone);

	tio->io = ci->io;
	tio->ti = ti;
	memset(&tio->info, 0, sizeof(tio->info));
	tio->target_bio_nr = target_bio_nr;

	return tio;
}

static void __clone_and_map_simple_bio(struct clone_info *ci,
				       struct dm_target *ti,
				       unsigned target_bio_nr, sector_t len)
{
	struct dm_target_io *tio = alloc_tio(ci, ti, ci->bio->bi_max_vecs, target_bio_nr);
	struct bio *clone = &tio->clone;

	/*
	 * Discard requests require the bio's inline iovecs be initialized.
	 * ci->bio->bi_max_vecs is BIO_INLINE_VECS anyway, for both flush
	 * and discard, so no need for concern about wasted bvec allocations.
	 */
	 __bio_clone(clone, ci->bio);
	if (len)
		bio_setup_sector(clone, ci->sector, len);

	__map_bio(tio);
}

static void __send_duplicate_bios(struct clone_info *ci, struct dm_target *ti,
				  unsigned num_bios, sector_t len)
{
	unsigned target_bio_nr;

	for (target_bio_nr = 0; target_bio_nr < num_bios; target_bio_nr++)
		__clone_and_map_simple_bio(ci, ti, target_bio_nr, len);
}

static int __send_empty_flush(struct clone_info *ci)
{
	unsigned target_nr = 0;
	struct dm_target *ti;

	BUG_ON(bio_has_data(ci->bio));
	while ((ti = dm_table_get_target(ci->map, target_nr++)))
		__send_duplicate_bios(ci, ti, ti->num_flush_bios, 0);

	return 0;
}

static void __clone_and_map_data_bio(struct clone_info *ci, struct dm_target *ti,
				     sector_t sector, int nr_iovecs,
				     unsigned short idx, unsigned short bv_count,
				     unsigned offset, unsigned len,
				     unsigned split_bvec)
{
	struct bio *bio = ci->bio;
	struct dm_target_io *tio;
	unsigned target_bio_nr;
	unsigned num_target_bios = 1;

	/*
	 * Does the target want to receive duplicate copies of the bio?
	 */
	if (bio_data_dir(bio) == WRITE && ti->num_write_bios)
		num_target_bios = ti->num_write_bios(ti, bio);

	for (target_bio_nr = 0; target_bio_nr < num_target_bios; target_bio_nr++) {
		tio = alloc_tio(ci, ti, nr_iovecs, target_bio_nr);
		if (split_bvec)
			clone_split_bio(tio, bio, sector, idx, offset, len);
		else
			clone_bio(tio, bio, sector, idx, bv_count, len);
		__map_bio(tio);
	}
}

typedef unsigned (*get_num_bios_fn)(struct dm_target *ti);

static unsigned get_num_discard_bios(struct dm_target *ti)
{
	return ti->num_discard_bios;
}

static unsigned get_num_write_same_bios(struct dm_target *ti)
{
	return ti->num_write_same_bios;
}

typedef bool (*is_split_required_fn)(struct dm_target *ti);

static bool is_split_required_for_discard(struct dm_target *ti)
{
	return ti->split_discard_bios;
}

static int __send_changing_extent_only(struct clone_info *ci,
				       get_num_bios_fn get_num_bios,
				       is_split_required_fn is_split_required)
{
	struct dm_target *ti;
	sector_t len;
	unsigned num_bios;

	do {
		ti = dm_table_find_target(ci->map, ci->sector);
		if (!dm_target_is_valid(ti))
			return -EIO;

		/*
		 * Even though the device advertised support for this type of
		 * request, that does not mean every target supports it, and
		 * reconfiguration might also have changed that since the
		 * check was performed.
		 */
		num_bios = get_num_bios ? get_num_bios(ti) : 0;
		if (!num_bios)
			return -EOPNOTSUPP;

		if (is_split_required && !is_split_required(ti))
			len = min(ci->sector_count, max_io_len_target_boundary(ci->sector, ti));
		else
			len = min(ci->sector_count, max_io_len(ci->sector, ti));

		__send_duplicate_bios(ci, ti, num_bios, len);

		ci->sector += len;
	} while (ci->sector_count -= len);

	return 0;
}

static int __send_discard(struct clone_info *ci)
{
	return __send_changing_extent_only(ci, get_num_discard_bios,
					   is_split_required_for_discard);
}

static int __send_write_same(struct clone_info *ci)
{
	return __send_changing_extent_only(ci, get_num_write_same_bios, NULL);
}

/*
 * Find maximum number of sectors / bvecs we can process with a single bio.
 */
static sector_t __len_within_target(struct clone_info *ci, sector_t max, int *idx)
{
	struct bio *bio = ci->bio;
	sector_t bv_len, total_len = 0;

	for (*idx = ci->idx; max && (*idx < bio->bi_vcnt); (*idx)++) {
		bv_len = to_sector(bio->bi_io_vec[*idx].bv_len);

		if (bv_len > max)
			break;

		max -= bv_len;
		total_len += bv_len;
	}

	return total_len;
}

static int __split_bvec_across_targets(struct clone_info *ci,
				       struct dm_target *ti, sector_t max)
{
	struct bio *bio = ci->bio;
	struct bio_vec *bv = bio->bi_io_vec + ci->idx;
	sector_t remaining = to_sector(bv->bv_len);
	unsigned offset = 0;
	sector_t len;

	do {
		if (offset) {
			ti = dm_table_find_target(ci->map, ci->sector);
			if (!dm_target_is_valid(ti))
				return -EIO;

			max = max_io_len(ci->sector, ti);
		}

		len = min(remaining, max);

		__clone_and_map_data_bio(ci, ti, ci->sector, 1, ci->idx, 0,
					 bv->bv_offset + offset, len, 1);

		ci->sector += len;
		ci->sector_count -= len;
		offset += to_bytes(len);
	} while (remaining -= len);

	ci->idx++;

	return 0;
}

/*
 * Select the correct strategy for processing a non-flush bio.
 */
static int __split_and_process_non_flush(struct clone_info *ci)
{
	struct bio *bio = ci->bio;
	struct dm_target *ti;
	sector_t len, max;
	int idx;

	if (unlikely(bio->bi_rw & REQ_DISCARD))
		return __send_discard(ci);
	else if (unlikely(bio->bi_rw & REQ_WRITE_SAME))
		return __send_write_same(ci);

	ti = dm_table_find_target(ci->map, ci->sector);
	if (!dm_target_is_valid(ti))
		return -EIO;

	max = max_io_len(ci->sector, ti);

	/*
	 * Optimise for the simple case where we can do all of
	 * the remaining io with a single clone.
	 */
	if (ci->sector_count <= max) {
		__clone_and_map_data_bio(ci, ti, ci->sector, bio->bi_max_vecs,
					 ci->idx, bio->bi_vcnt - ci->idx, 0,
					 ci->sector_count, 0);
		ci->sector_count = 0;
		return 0;
	}

	/*
	 * There are some bvecs that don't span targets.
	 * Do as many of these as possible.
	 */
	if (to_sector(bio->bi_io_vec[ci->idx].bv_len) <= max) {
		len = __len_within_target(ci, max, &idx);

		__clone_and_map_data_bio(ci, ti, ci->sector, bio->bi_max_vecs,
					 ci->idx, idx - ci->idx, 0, len, 0);

		ci->sector += len;
		ci->sector_count -= len;
		ci->idx = idx;

		return 0;
	}

	/*
	 * Handle a bvec that must be split between two or more targets.
	 */
	return __split_bvec_across_targets(ci, ti, max);
}

/*
 * Entry point to split a bio into clones and submit them to the targets.
 */
static void __split_and_process_bio(struct mapped_device *md, struct bio *bio)
{
	struct clone_info ci;
	int error = 0;

	ci.map = dm_get_live_table(md);
	if (unlikely(!ci.map)) {
		bio_io_error(bio);
		return;
	}

	ci.md = md;
	ci.io = alloc_io(md);
	ci.io->error = 0;
	atomic_set(&ci.io->io_count, 1);
	ci.io->bio = bio;
	ci.io->md = md;
	spin_lock_init(&ci.io->endio_lock);
	ci.sector = bio->bi_sector;
	ci.idx = bio->bi_idx;

	start_io_acct(ci.io);

	if (bio->bi_rw & REQ_FLUSH) {
		ci.bio = &ci.md->flush_bio;
		ci.sector_count = 0;
		error = __send_empty_flush(&ci);
		/* dec_pending submits any data associated with flush */
	} else {
		ci.bio = bio;
		ci.sector_count = bio_sectors(bio);
		while (ci.sector_count && !error)
			error = __split_and_process_non_flush(&ci);
	}

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
	struct dm_table *map = dm_get_live_table(md);
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
	max_sectors = min(max_io_len(bvm->bi_sector, ti),
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
	/*
	 * If the target doesn't support merge method and some of the devices
	 * provided their merge_bvec method (we know this by looking at
	 * queue_max_hw_sectors), then we can't allow bios with multiple vector
	 * entries.  So always set max_size to 0, and the code below allows
	 * just one page.
	 */
	else if (queue_max_hw_sectors(q) <= PAGE_SIZE >> 9)

		max_size = 0;

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
static void _dm_request(struct request_queue *q, struct bio *bio)
{
	int rw = bio_data_dir(bio);
	struct mapped_device *md = q->queuedata;
	int cpu;

	down_read(&md->io_lock);

	cpu = part_stat_lock();
	part_stat_inc(cpu, &dm_disk(md)->part0, ios[rw]);
	part_stat_add(cpu, &dm_disk(md)->part0, sectors[rw], bio_sectors(bio));
	part_stat_unlock();

	/* if we're suspended, we have to queue this io for later */
	if (unlikely(test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags))) {
		up_read(&md->io_lock);

		if (bio_rw(bio) != READA)
			queue_io(md, bio);
		else
			bio_io_error(bio);
		return;
	}

	__split_and_process_bio(md, bio);
	up_read(&md->io_lock);
	return;
}

static int dm_request_based(struct mapped_device *md)
{
	return blk_queue_stackable(md->queue);
}

static void dm_request(struct request_queue *q, struct bio *bio)
{
	struct mapped_device *md = q->queuedata;

	if (dm_request_based(md))
		blk_queue_bio(q, bio);
	else
		_dm_request(q, bio);
}

void dm_dispatch_request(struct request *rq)
{
	int r;

	if (blk_queue_io_stat(rq->q))
		rq->cmd_flags |= REQ_IO_STAT;

	rq->start_time = jiffies;
	r = blk_insert_cloned_request(rq->q, rq);
	if (r)
		dm_complete_request(rq, r);
}
EXPORT_SYMBOL_GPL(dm_dispatch_request);

static int dm_rq_bio_constructor(struct bio *bio, struct bio *bio_orig,
				 void *data)
{
	struct dm_rq_target_io *tio = data;
	struct dm_rq_clone_bio_info *info =
		container_of(bio, struct dm_rq_clone_bio_info, clone);

	info->orig = bio_orig;
	info->tio = tio;
	bio->bi_end_io = end_clone_bio;
	bio->bi_private = info;

	return 0;
}

static int setup_clone(struct request *clone, struct request *rq,
		       struct dm_rq_target_io *tio)
{
	int r;

	r = blk_rq_prep_clone(clone, rq, tio->md->bs, GFP_ATOMIC,
			      dm_rq_bio_constructor, tio);
	if (r)
		return r;

	clone->cmd = rq->cmd;
	clone->cmd_len = rq->cmd_len;
	clone->sense = rq->sense;
	clone->buffer = rq->buffer;
	clone->end_io = end_clone_request;
	clone->end_io_data = tio;

	return 0;
}

static struct request *clone_rq(struct request *rq, struct mapped_device *md,
				gfp_t gfp_mask)
{
	struct request *clone;
	struct dm_rq_target_io *tio;

	tio = alloc_rq_tio(md, gfp_mask);
	if (!tio)
		return NULL;

	tio->md = md;
	tio->ti = NULL;
	tio->orig = rq;
	tio->error = 0;
	memset(&tio->info, 0, sizeof(tio->info));

	clone = &tio->clone;
	if (setup_clone(clone, rq, tio)) {
		/* -ENOMEM */
		free_rq_tio(tio);
		return NULL;
	}

	return clone;
}

/*
 * Called with the queue lock held.
 */
static int dm_prep_fn(struct request_queue *q, struct request *rq)
{
	struct mapped_device *md = q->queuedata;
	struct request *clone;

	if (unlikely(rq->special)) {
		DMWARN("Already has something in rq->special.");
		return BLKPREP_KILL;
	}

	clone = clone_rq(rq, md, GFP_ATOMIC);
	if (!clone)
		return BLKPREP_DEFER;

	rq->special = clone;
	rq->cmd_flags |= REQ_DONTPREP;

	return BLKPREP_OK;
}

/*
 * Returns:
 * 0  : the request has been processed (not requeued)
 * !0 : the request has been requeued
 */
static int map_request(struct dm_target *ti, struct request *clone,
		       struct mapped_device *md)
{
	int r, requeued = 0;
	struct dm_rq_target_io *tio = clone->end_io_data;

	tio->ti = ti;
	r = ti->type->map_rq(ti, clone, &tio->info);
	switch (r) {
	case DM_MAPIO_SUBMITTED:
		/* The target has taken the I/O to submit by itself later */
		break;
	case DM_MAPIO_REMAPPED:
		/* The target has remapped the I/O so dispatch it */
		trace_block_rq_remap(clone->q, clone, disk_devt(dm_disk(md)),
				     blk_rq_pos(tio->orig));
		dm_dispatch_request(clone);
		break;
	case DM_MAPIO_REQUEUE:
		/* The target wants to requeue the I/O */
		dm_requeue_unmapped_request(clone);
		requeued = 1;
		break;
	default:
		if (r > 0) {
			DMWARN("unimplemented target map return value: %d", r);
			BUG();
		}

		/* The target wants to complete the I/O */
		dm_kill_unmapped_request(clone, r);
		break;
	}

	return requeued;
}

static struct request *dm_start_request(struct mapped_device *md, struct request *orig)
{
	struct request *clone;

	blk_start_request(orig);
	clone = orig->special;
	atomic_inc(&md->pending[rq_data_dir(clone)]);

	/*
	 * Hold the md reference here for the in-flight I/O.
	 * We can't rely on the reference count by device opener,
	 * because the device may be closed during the request completion
	 * when all bios are completed.
	 * See the comment in rq_completed() too.
	 */
	dm_get(md);

	return clone;
}

/*
 * q->request_fn for request-based dm.
 * Called with the queue lock held.
 */
static void dm_request_fn(struct request_queue *q)
{
	struct mapped_device *md = q->queuedata;
	struct dm_table *map = dm_get_live_table(md);
	struct dm_target *ti;
	struct request *rq, *clone;
	sector_t pos;

	/*
	 * For suspend, check blk_queue_stopped() and increment
	 * ->pending within a single queue_lock not to increment the
	 * number of in-flight I/Os after the queue is stopped in
	 * dm_suspend().
	 */
	while (!blk_queue_stopped(q)) {
		rq = blk_peek_request(q);
		if (!rq)
			goto delay_and_out;

		/* always use block 0 to find the target for flushes for now */
		pos = 0;
		if (!(rq->cmd_flags & REQ_FLUSH))
			pos = blk_rq_pos(rq);

		ti = dm_table_find_target(map, pos);
		if (!dm_target_is_valid(ti)) {
			/*
			 * Must perform setup, that dm_done() requires,
			 * before calling dm_kill_unmapped_request
			 */
			DMERR_LIMIT("request attempted access beyond the end of device");
			clone = dm_start_request(md, rq);
			dm_kill_unmapped_request(clone, -EIO);
			continue;
		}

		if (ti->type->busy && ti->type->busy(ti))
			goto delay_and_out;

		clone = dm_start_request(md, rq);

		spin_unlock(q->queue_lock);
		if (map_request(ti, clone, md))
			goto requeued;

		BUG_ON(!irqs_disabled());
		spin_lock(q->queue_lock);
	}

	goto out;

requeued:
	BUG_ON(!irqs_disabled());
	spin_lock(q->queue_lock);

delay_and_out:
	blk_delay_queue(q, HZ / 10);
out:
	dm_table_put(map);
}

int dm_underlying_device_busy(struct request_queue *q)
{
	return blk_lld_busy(q);
}
EXPORT_SYMBOL_GPL(dm_underlying_device_busy);

static int dm_lld_busy(struct request_queue *q)
{
	int r;
	struct mapped_device *md = q->queuedata;
	struct dm_table *map = dm_get_live_table(md);

	if (!map || test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags))
		r = 1;
	else
		r = dm_table_any_busy_target(map);

	dm_table_put(map);

	return r;
}

static int dm_any_congested(void *congested_data, int bdi_bits)
{
	int r = bdi_bits;
	struct mapped_device *md = congested_data;
	struct dm_table *map;

	if (!test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) {
		map = dm_get_live_table(md);
		if (map) {
			/*
			 * Request-based dm cares about only own queue for
			 * the query about congestion status of request_queue
			 */
			if (dm_request_based(md))
				r = md->queue->backing_dev_info.state &
				    bdi_bits;
			else
				r = dm_table_any_congested(map, bdi_bits);

			dm_table_put(map);
		}
	}

	return r;
}

/*-----------------------------------------------------------------
 * An IDR is used to keep track of allocated minor numbers.
 *---------------------------------------------------------------*/
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
	int r;

	if (minor >= (1 << MINORBITS))
		return -EINVAL;

	idr_preload(GFP_KERNEL);
	spin_lock(&_minor_lock);

	r = idr_alloc(&_minor_idr, MINOR_ALLOCED, minor, minor + 1, GFP_NOWAIT);

	spin_unlock(&_minor_lock);
	idr_preload_end();
	if (r < 0)
		return r == -ENOSPC ? -EBUSY : r;
	return 0;
}

static int next_free_minor(int *minor)
{
	int r;

	idr_preload(GFP_KERNEL);
	spin_lock(&_minor_lock);

	r = idr_alloc(&_minor_idr, MINOR_ALLOCED, 0, 1 << MINORBITS, GFP_NOWAIT);

	spin_unlock(&_minor_lock);
	idr_preload_end();
	if (r < 0)
		return r;
	*minor = r;
	return 0;
}

static const struct block_device_operations dm_blk_dops;

static void dm_wq_work(struct work_struct *work);

static void dm_init_md_queue(struct mapped_device *md)
{
	/*
	 * Request-based dm devices cannot be stacked on top of bio-based dm
	 * devices.  The type of this dm device has not been decided yet.
	 * The type is decided at the first table loading time.
	 * To prevent problematic device stacking, clear the queue flag
	 * for request stacking support until then.
	 *
	 * This queue is new, so no concurrency on the queue_flags.
	 */
	queue_flag_clear_unlocked(QUEUE_FLAG_STACKABLE, md->queue);

	md->queue->queuedata = md;
	md->queue->backing_dev_info.congested_fn = dm_any_congested;
	md->queue->backing_dev_info.congested_data = md;
	blk_queue_make_request(md->queue, dm_request);
	blk_queue_bounce_limit(md->queue, BLK_BOUNCE_ANY);
	blk_queue_merge_bvec(md->queue, dm_merge_bvec);
}

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

	md->type = DM_TYPE_NONE;
	init_rwsem(&md->io_lock);
	mutex_init(&md->suspend_lock);
	mutex_init(&md->type_lock);
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

	dm_init_md_queue(md);

	md->disk = alloc_disk(1);
	if (!md->disk)
		goto bad_disk;

	atomic_set(&md->pending[0], 0);
	atomic_set(&md->pending[1], 0);
	init_waitqueue_head(&md->wait);
	INIT_WORK(&md->work, dm_wq_work);
	init_waitqueue_head(&md->eventq);
	init_completion(&md->kobj_holder.completion);

	md->disk->major = _major;
	md->disk->first_minor = minor;
	md->disk->fops = &dm_blk_dops;
	md->disk->queue = md->queue;
	md->disk->private_data = md;
	sprintf(md->disk->disk_name, "dm-%d", minor);
	add_disk(md->disk);
	format_dev_t(md->name, MKDEV(_major, minor));

	md->wq = alloc_workqueue("kdmflush",
				 WQ_NON_REENTRANT | WQ_MEM_RECLAIM, 0);
	if (!md->wq)
		goto bad_thread;

	md->bdev = bdget_disk(md->disk, 0);
	if (!md->bdev)
		goto bad_bdev;

	bio_init(&md->flush_bio);
	md->flush_bio.bi_bdev = md->bdev;
	md->flush_bio.bi_rw = WRITE_FLUSH;

	/* Populate the mapping, nobody knows we exist yet */
	spin_lock(&_minor_lock);
	old_md = idr_replace(&_minor_idr, md, minor);
	spin_unlock(&_minor_lock);

	BUG_ON(old_md != MINOR_ALLOCED);

	return md;

bad_bdev:
	destroy_workqueue(md->wq);
bad_thread:
	del_gendisk(md->disk);
	put_disk(md->disk);
bad_disk:
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

	unlock_fs(md);
	bdput(md->bdev);
	destroy_workqueue(md->wq);
	if (md->io_pool)
		mempool_destroy(md->io_pool);
	if (md->bs)
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

static void __bind_mempools(struct mapped_device *md, struct dm_table *t)
{
	struct dm_md_mempools *p = dm_table_get_md_mempools(t);

	if (md->io_pool && md->bs) {
		/* The md already has necessary mempools. */
		if (dm_table_get_type(t) == DM_TYPE_BIO_BASED) {
			/*
			 * Reload bioset because front_pad may have changed
			 * because a different table was loaded.
			 */
			bioset_free(md->bs);
			md->bs = p->bs;
			p->bs = NULL;
		} else if (dm_table_get_type(t) == DM_TYPE_REQUEST_BASED) {
			/*
			 * There's no need to reload with request-based dm
			 * because the size of front_pad doesn't change.
			 * Note for future: If you are to reload bioset,
			 * prep-ed requests in the queue may refer
			 * to bio from the old bioset, so you must walk
			 * through the queue to unprep.
			 */
		}
		goto out;
	}

	BUG_ON(!p || md->io_pool || md->bs);

	md->io_pool = p->io_pool;
	p->io_pool = NULL;
	md->bs = p->bs;
	p->bs = NULL;

out:
	/* mempool bind completed, now no need any mempools in the table */
	dm_table_free_md_mempools(t);
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

/*
 * Protected by md->suspend_lock obtained by dm_swap_table().
 */
static void __set_size(struct mapped_device *md, sector_t size)
{
	set_capacity(md->disk, size);

	i_size_write(md->bdev->bd_inode, (loff_t)size << SECTOR_SHIFT);
}

/*
 * Return 1 if the queue has a compulsory merge_bvec_fn function.
 *
 * If this function returns 0, then the device is either a non-dm
 * device without a merge_bvec_fn, or it is a dm device that is
 * able to split any bios it receives that are too big.
 */
int dm_queue_merge_is_compulsory(struct request_queue *q)
{
	struct mapped_device *dev_md;

	if (!q->merge_bvec_fn)
		return 0;

	if (q->make_request_fn == dm_request) {
		dev_md = q->queuedata;
		if (test_bit(DMF_MERGE_IS_OPTIONAL, &dev_md->flags))
			return 0;
	}

	return 1;
}

static int dm_device_merge_is_compulsory(struct dm_target *ti,
					 struct dm_dev *dev, sector_t start,
					 sector_t len, void *data)
{
	struct block_device *bdev = dev->bdev;
	struct request_queue *q = bdev_get_queue(bdev);

	return dm_queue_merge_is_compulsory(q);
}

/*
 * Return 1 if it is acceptable to ignore merge_bvec_fn based
 * on the properties of the underlying devices.
 */
static int dm_table_merge_is_optional(struct dm_table *table)
{
	unsigned i = 0;
	struct dm_target *ti;

	while (i < dm_table_get_num_targets(table)) {
		ti = dm_table_get_target(table, i++);

		if (ti->type->iterate_devices &&
		    ti->type->iterate_devices(ti, dm_device_merge_is_compulsory, NULL))
			return 0;
	}

	return 1;
}

/*
 * Returns old map, which caller must destroy.
 */
static struct dm_table *__bind(struct mapped_device *md, struct dm_table *t,
			       struct queue_limits *limits)
{
	struct dm_table *old_map;
	struct request_queue *q = md->queue;
	sector_t size;
	unsigned long flags;
	int merge_is_optional;

	size = dm_table_get_size(t);

	/*
	 * Wipe any geometry if the size of the table changed.
	 */
	if (size != get_capacity(md->disk))
		memset(&md->geometry, 0, sizeof(md->geometry));

	__set_size(md, size);

	dm_table_event_callback(t, event_callback, md);

	/*
	 * The queue hasn't been stopped yet, if the old table type wasn't
	 * for request-based during suspension.  So stop it to prevent
	 * I/O mapping before resume.
	 * This must be done before setting the queue restrictions,
	 * because request-based dm may be run just after the setting.
	 */
	if (dm_table_request_based(t) && !blk_queue_stopped(q))
		stop_queue(q);

	__bind_mempools(md, t);

	merge_is_optional = dm_table_merge_is_optional(t);

	write_lock_irqsave(&md->map_lock, flags);
	old_map = md->map;
	md->map = t;
	md->immutable_target_type = dm_table_get_immutable_target_type(t);

	dm_table_set_restrictions(t, q, limits);
	if (merge_is_optional)
		set_bit(DMF_MERGE_IS_OPTIONAL, &md->flags);
	else
		clear_bit(DMF_MERGE_IS_OPTIONAL, &md->flags);
	write_unlock_irqrestore(&md->map_lock, flags);

	return old_map;
}

/*
 * Returns unbound table for the caller to free.
 */
static struct dm_table *__unbind(struct mapped_device *md)
{
	struct dm_table *map = md->map;
	unsigned long flags;

	if (!map)
		return NULL;

	dm_table_event_callback(map, NULL, NULL);
	write_lock_irqsave(&md->map_lock, flags);
	md->map = NULL;
	write_unlock_irqrestore(&md->map_lock, flags);

	return map;
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

/*
 * Functions to manage md->type.
 * All are required to hold md->type_lock.
 */
void dm_lock_md_type(struct mapped_device *md)
{
	mutex_lock(&md->type_lock);
}

void dm_unlock_md_type(struct mapped_device *md)
{
	mutex_unlock(&md->type_lock);
}

void dm_set_md_type(struct mapped_device *md, unsigned type)
{
	md->type = type;
}

unsigned dm_get_md_type(struct mapped_device *md)
{
	return md->type;
}

struct target_type *dm_get_immutable_target_type(struct mapped_device *md)
{
	return md->immutable_target_type;
}

/*
 * The queue_limits are only valid as long as you have a reference
 * count on 'md'.
 */
struct queue_limits *dm_get_queue_limits(struct mapped_device *md)
{
	BUG_ON(!atomic_read(&md->holders));
	return &md->queue->limits;
}
EXPORT_SYMBOL_GPL(dm_get_queue_limits);

/*
 * Fully initialize a request-based queue (->elevator, ->request_fn, etc).
 */
static int dm_init_request_based_queue(struct mapped_device *md)
{
	struct request_queue *q = NULL;

	if (md->queue->elevator)
		return 1;

	/* Fully initialize the queue */
	q = blk_init_allocated_queue(md->queue, dm_request_fn, NULL);
	if (!q)
		return 0;

	md->queue = q;
	dm_init_md_queue(md);
	blk_queue_softirq_done(md->queue, dm_softirq_done);
	blk_queue_prep_rq(md->queue, dm_prep_fn);
	blk_queue_lld_busy(md->queue, dm_lld_busy);

	elv_register_queue(md->queue);

	return 1;
}

/*
 * Setup the DM device's queue based on md's type
 */
int dm_setup_md_queue(struct mapped_device *md)
{
	if ((dm_get_md_type(md) == DM_TYPE_REQUEST_BASED) &&
	    !dm_init_request_based_queue(md)) {
		DMWARN("Cannot initialize queue for request-based mapped device");
		return -EINVAL;
	}

	return 0;
}

struct mapped_device *dm_get_md(dev_t dev)
{
	struct mapped_device *md;
	unsigned minor = MINOR(dev);

	if (MAJOR(dev) != _major || minor >= (1 << MINORBITS))
		return NULL;

	spin_lock(&_minor_lock);

	md = idr_find(&_minor_idr, minor);
	if (md) {
		if ((md == MINOR_ALLOCED ||
		     (MINOR(disk_devt(dm_disk(md))) != minor) ||
		     dm_deleting_md(md) ||
		     test_bit(DMF_FREEING, &md->flags))) {
			md = NULL;
			goto out;
		}
		dm_get(md);
	}

out:
	spin_unlock(&_minor_lock);

	return md;
}
EXPORT_SYMBOL_GPL(dm_get_md);

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
	BUG_ON(test_bit(DMF_FREEING, &md->flags));
}

const char *dm_device_name(struct mapped_device *md)
{
	return md->name;
}
EXPORT_SYMBOL_GPL(dm_device_name);

static void __dm_destroy(struct mapped_device *md, bool wait)
{
	struct dm_table *map;

	might_sleep();

	spin_lock(&_minor_lock);
	map = dm_get_live_table(md);
	idr_replace(&_minor_idr, MINOR_ALLOCED, MINOR(disk_devt(dm_disk(md))));
	set_bit(DMF_FREEING, &md->flags);
	spin_unlock(&_minor_lock);

	/*
	 * Take suspend_lock so that presuspend and postsuspend methods
	 * do not race with internal suspend.
	 */
	mutex_lock(&md->suspend_lock);
	if (!dm_suspended_md(md)) {
		dm_table_presuspend_targets(map);
		dm_table_postsuspend_targets(map);
	}
	mutex_unlock(&md->suspend_lock);

	/*
	 * Rare, but there may be I/O requests still going to complete,
	 * for example.  Wait for all references to disappear.
	 * No one should increment the reference count of the mapped_device,
	 * after the mapped_device state becomes DMF_FREEING.
	 */
	if (wait)
		while (atomic_read(&md->holders))
			msleep(1);
	else if (atomic_read(&md->holders))
		DMWARN("%s: Forcibly removing mapped_device still in use! (%d users)",
		       dm_device_name(md), atomic_read(&md->holders));

	dm_sysfs_exit(md);
	dm_table_put(map);
	dm_table_destroy(__unbind(md));
	free_dev(md);
}

void dm_destroy(struct mapped_device *md)
{
	__dm_destroy(md, true);
}

void dm_destroy_immediate(struct mapped_device *md)
{
	__dm_destroy(md, false);
}

void dm_put(struct mapped_device *md)
{
	atomic_dec(&md->holders);
}
EXPORT_SYMBOL_GPL(dm_put);

static int dm_wait_for_completion(struct mapped_device *md, int interruptible)
{
	int r = 0;
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&md->wait, &wait);

	while (1) {
		set_current_state(interruptible);

		if (!md_in_flight(md))
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

/*
 * Process the deferred bios
 */
static void dm_wq_work(struct work_struct *work)
{
	struct mapped_device *md = container_of(work, struct mapped_device,
						work);
	struct bio *c;

	down_read(&md->io_lock);

	while (!test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) {
		spin_lock_irq(&md->deferred_lock);
		c = bio_list_pop(&md->deferred);
		spin_unlock_irq(&md->deferred_lock);

		if (!c)
			break;

		up_read(&md->io_lock);

		if (dm_request_based(md))
			generic_make_request(c);
		else
			__split_and_process_bio(md, c);

		down_read(&md->io_lock);
	}

	up_read(&md->io_lock);
}

static void dm_queue_flush(struct mapped_device *md)
{
	clear_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	smp_mb__after_clear_bit();
	queue_work(md->wq, &md->work);
}

/*
 * Swap in a new table, returning the old one for the caller to destroy.
 */
struct dm_table *dm_swap_table(struct mapped_device *md, struct dm_table *table)
{
	struct dm_table *live_map = NULL, *map = ERR_PTR(-EINVAL);
	struct queue_limits limits;
	int r;

	mutex_lock(&md->suspend_lock);

	/* device must be suspended */
	if (!dm_suspended_md(md))
		goto out;

	/*
	 * If the new table has no data devices, retain the existing limits.
	 * This helps multipath with queue_if_no_path if all paths disappear,
	 * then new I/O is queued based on these limits, and then some paths
	 * reappear.
	 */
	if (dm_table_has_no_data_devices(table)) {
		live_map = dm_get_live_table(md);
		if (live_map)
			limits = md->queue->limits;
		dm_table_put(live_map);
	}

	if (!live_map) {
		r = dm_calculate_queue_limits(table, &limits);
		if (r) {
			map = ERR_PTR(r);
			goto out;
		}
	}

	map = __bind(md, table, &limits);

out:
	mutex_unlock(&md->suspend_lock);
	return map;
}

/*
 * Functions to lock and unlock any filesystem running on the
 * device.
 */
static int lock_fs(struct mapped_device *md)
{
	int r;

	WARN_ON(md->frozen_sb);

	md->frozen_sb = freeze_bdev(md->bdev);
	if (IS_ERR(md->frozen_sb)) {
		r = PTR_ERR(md->frozen_sb);
		md->frozen_sb = NULL;
		return r;
	}

	set_bit(DMF_FROZEN, &md->flags);

	return 0;
}

static void unlock_fs(struct mapped_device *md)
{
	if (!test_bit(DMF_FROZEN, &md->flags))
		return;

	thaw_bdev(md->bdev, md->frozen_sb);
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
/*
 * Suspend mechanism in request-based dm.
 *
 * 1. Flush all I/Os by lock_fs() if needed.
 * 2. Stop dispatching any I/O by stopping the request_queue.
 * 3. Wait for all in-flight I/Os to be completed or requeued.
 *
 * To abort suspend, start the request_queue.
 */
int dm_suspend(struct mapped_device *md, unsigned suspend_flags)
{
	struct dm_table *map = NULL;
	int r = 0;
	int do_lockfs = suspend_flags & DM_SUSPEND_LOCKFS_FLAG ? 1 : 0;
	int noflush = suspend_flags & DM_SUSPEND_NOFLUSH_FLAG ? 1 : 0;

	mutex_lock(&md->suspend_lock);

	if (dm_suspended_md(md)) {
		r = -EINVAL;
		goto out_unlock;
	}

	map = dm_get_live_table(md);

	/*
	 * DMF_NOFLUSH_SUSPENDING must be set before presuspend.
	 * This flag is cleared before dm_suspend returns.
	 */
	if (noflush)
		set_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);

	/* This does not get reverted if there's an error later. */
	dm_table_presuspend_targets(map);

	/*
	 * Flush I/O to the device.
	 * Any I/O submitted after lock_fs() may not be flushed.
	 * noflush takes precedence over do_lockfs.
	 * (lock_fs() flushes I/Os and waits for them to complete.)
	 */
	if (!noflush && do_lockfs) {
		r = lock_fs(md);
		if (r)
			goto out;
	}

	/*
	 * Here we must make sure that no processes are submitting requests
	 * to target drivers i.e. no one may be executing
	 * __split_and_process_bio. This is called from dm_request and
	 * dm_wq_work.
	 *
	 * To get all processes out of __split_and_process_bio in dm_request,
	 * we take the write lock. To prevent any process from reentering
	 * __split_and_process_bio from dm_request and quiesce the thread
	 * (dm_wq_work), we set BMF_BLOCK_IO_FOR_SUSPEND and call
	 * flush_workqueue(md->wq).
	 */
	down_write(&md->io_lock);
	set_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	up_write(&md->io_lock);

	/*
	 * Stop md->queue before flushing md->wq in case request-based
	 * dm defers requests to md->wq from md->queue.
	 */
	if (dm_request_based(md))
		stop_queue(md->queue);

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

		if (dm_request_based(md))
			start_queue(md->queue);

		unlock_fs(md);
		goto out; /* pushback list is already flushed, so skip flush */
	}

	/*
	 * If dm_wait_for_completion returned 0, the device is completely
	 * quiescent now. There is no request-processing activity. All new
	 * requests are being added to md->deferred list.
	 */

	set_bit(DMF_SUSPENDED, &md->flags);

	dm_table_postsuspend_targets(map);

out:
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
	if (!dm_suspended_md(md))
		goto out;

	map = dm_get_live_table(md);
	if (!map || !dm_table_get_size(map))
		goto out;

	r = dm_table_resume_targets(map);
	if (r)
		goto out;

	dm_queue_flush(md);

	/*
	 * Flushing deferred I/Os must be done after targets are resumed
	 * so that mapping of targets can work correctly.
	 * Request-based dm is queueing the deferred I/Os in its request_queue.
	 */
	if (dm_request_based(md))
		start_queue(md->queue);

	unlock_fs(md);

	clear_bit(DMF_SUSPENDED, &md->flags);

	r = 0;
out:
	dm_table_put(map);
	mutex_unlock(&md->suspend_lock);

	return r;
}

/*-----------------------------------------------------------------
 * Event notification.
 *---------------------------------------------------------------*/
int dm_kobject_uevent(struct mapped_device *md, enum kobject_action action,
		       unsigned cookie)
{
	char udev_cookie[DM_COOKIE_LENGTH];
	char *envp[] = { udev_cookie, NULL };

	if (!cookie)
		return kobject_uevent(&disk_to_dev(md->disk)->kobj, action);
	else {
		snprintf(udev_cookie, DM_COOKIE_LENGTH, "%s=%u",
			 DM_COOKIE_ENV_VAR_NAME, cookie);
		return kobject_uevent_env(&disk_to_dev(md->disk)->kobj,
					  action, envp);
	}
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
	return &md->kobj_holder.kobj;
}

struct mapped_device *dm_get_from_kobject(struct kobject *kobj)
{
	struct mapped_device *md;

	md = container_of(kobj, struct mapped_device, kobj_holder.kobj);

	if (test_bit(DMF_FREEING, &md->flags) ||
	    dm_deleting_md(md))
		return NULL;

	dm_get(md);
	return md;
}

int dm_suspended_md(struct mapped_device *md)
{
	return test_bit(DMF_SUSPENDED, &md->flags);
}

int dm_suspended(struct dm_target *ti)
{
	return dm_suspended_md(dm_table_get_md(ti->table));
}
EXPORT_SYMBOL_GPL(dm_suspended);

int dm_noflush_suspending(struct dm_target *ti)
{
	return __noflush_suspending(dm_table_get_md(ti->table));
}
EXPORT_SYMBOL_GPL(dm_noflush_suspending);

struct dm_md_mempools *dm_alloc_md_mempools(unsigned type, unsigned integrity, unsigned per_bio_data_size)
{
	struct dm_md_mempools *pools = kzalloc(sizeof(*pools), GFP_KERNEL);
	struct kmem_cache *cachep;
	unsigned int pool_size;
	unsigned int front_pad;

	if (!pools)
		return NULL;

	if (type == DM_TYPE_BIO_BASED) {
		cachep = _io_cache;
		pool_size = 16;
		front_pad = roundup(per_bio_data_size, __alignof__(struct dm_target_io)) + offsetof(struct dm_target_io, clone);
	} else if (type == DM_TYPE_REQUEST_BASED) {
		cachep = _rq_tio_cache;
		pool_size = MIN_IOS;
		front_pad = offsetof(struct dm_rq_clone_bio_info, clone);
		/* per_bio_data_size is not used. See __bind_mempools(). */
		WARN_ON(per_bio_data_size != 0);
	} else
		goto out;

	pools->io_pool = mempool_create_slab_pool(MIN_IOS, cachep);
	if (!pools->io_pool)
		goto out;

	pools->bs = bioset_create(pool_size, front_pad);
	if (!pools->bs)
		goto out;

	if (integrity && bioset_integrity_create(pools->bs, pool_size))
		goto out;

	return pools;

out:
	dm_free_md_mempools(pools);

	return NULL;
}

void dm_free_md_mempools(struct dm_md_mempools *pools)
{
	if (!pools)
		return;

	if (pools->io_pool)
		mempool_destroy(pools->io_pool);

	if (pools->bs)
		bioset_free(pools->bs);

	kfree(pools);
}

static const struct block_device_operations dm_blk_dops = {
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
