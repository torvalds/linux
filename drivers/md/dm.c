/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-core.h"
#include "dm-rq.h"
#include "dm-uevent.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched/signal.h>
#include <linux/blkpg.h>
#include <linux/bio.h>
#include <linux/mempool.h>
#include <linux/dax.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/uio.h>
#include <linux/hdreg.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/pr.h>
#include <linux/refcount.h>

#define DM_MSG_PREFIX "core"

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

static void do_deferred_remove(struct work_struct *w);

static DECLARE_WORK(deferred_remove_work, do_deferred_remove);

static struct workqueue_struct *deferred_remove_workqueue;

atomic_t dm_global_event_nr = ATOMIC_INIT(0);
DECLARE_WAIT_QUEUE_HEAD(dm_global_eventq);

void dm_issue_global_event(void)
{
	atomic_inc(&dm_global_event_nr);
	wake_up(&dm_global_eventq);
}

/*
 * One of these is allocated per bio.
 */
struct dm_io {
	struct mapped_device *md;
	blk_status_t status;
	atomic_t io_count;
	struct bio *bio;
	unsigned long start_time;
	spinlock_t endio_lock;
	struct dm_stats_aux stats_aux;
};

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
#define DMF_DEFERRED_REMOVE 6
#define DMF_SUSPENDED_INTERNALLY 7

#define DM_NUMA_NODE NUMA_NO_NODE
static int dm_numa_node = DM_NUMA_NODE;

/*
 * For mempools pre-allocation at the table loading time.
 */
struct dm_md_mempools {
	mempool_t *io_pool;
	struct bio_set *bs;
};

struct table_device {
	struct list_head list;
	refcount_t count;
	struct dm_dev dm_dev;
};

static struct kmem_cache *_io_cache;
static struct kmem_cache *_rq_tio_cache;
static struct kmem_cache *_rq_cache;

/*
 * Bio-based DM's mempools' reserved IOs set by the user.
 */
#define RESERVED_BIO_BASED_IOS		16
static unsigned reserved_bio_based_ios = RESERVED_BIO_BASED_IOS;

static int __dm_get_module_param_int(int *module_param, int min, int max)
{
	int param = ACCESS_ONCE(*module_param);
	int modified_param = 0;
	bool modified = true;

	if (param < min)
		modified_param = min;
	else if (param > max)
		modified_param = max;
	else
		modified = false;

	if (modified) {
		(void)cmpxchg(module_param, param, modified_param);
		param = modified_param;
	}

	return param;
}

unsigned __dm_get_module_param(unsigned *module_param,
			       unsigned def, unsigned max)
{
	unsigned param = ACCESS_ONCE(*module_param);
	unsigned modified_param = 0;

	if (!param)
		modified_param = def;
	else if (param > max)
		modified_param = max;

	if (modified_param) {
		(void)cmpxchg(module_param, param, modified_param);
		param = modified_param;
	}

	return param;
}

unsigned dm_get_reserved_bio_based_ios(void)
{
	return __dm_get_module_param(&reserved_bio_based_ios,
				     RESERVED_BIO_BASED_IOS, DM_RESERVED_MAX_IOS);
}
EXPORT_SYMBOL_GPL(dm_get_reserved_bio_based_ios);

static unsigned dm_get_numa_node(void)
{
	return __dm_get_module_param_int(&dm_numa_node,
					 DM_NUMA_NODE, num_online_nodes() - 1);
}

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

	_rq_cache = kmem_cache_create("dm_old_clone_request", sizeof(struct request),
				      __alignof__(struct request), 0, NULL);
	if (!_rq_cache)
		goto out_free_rq_tio_cache;

	r = dm_uevent_init();
	if (r)
		goto out_free_rq_cache;

	deferred_remove_workqueue = alloc_workqueue("kdmremove", WQ_UNBOUND, 1);
	if (!deferred_remove_workqueue) {
		r = -ENOMEM;
		goto out_uevent_exit;
	}

	_major = major;
	r = register_blkdev(_major, _name);
	if (r < 0)
		goto out_free_workqueue;

	if (!_major)
		_major = r;

	return 0;

out_free_workqueue:
	destroy_workqueue(deferred_remove_workqueue);
out_uevent_exit:
	dm_uevent_exit();
out_free_rq_cache:
	kmem_cache_destroy(_rq_cache);
out_free_rq_tio_cache:
	kmem_cache_destroy(_rq_tio_cache);
out_free_io_cache:
	kmem_cache_destroy(_io_cache);

	return r;
}

static void local_exit(void)
{
	flush_scheduled_work();
	destroy_workqueue(deferred_remove_workqueue);

	kmem_cache_destroy(_rq_cache);
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
	dm_statistics_init,
};

static void (*_exits[])(void) = {
	local_exit,
	dm_target_exit,
	dm_linear_exit,
	dm_stripe_exit,
	dm_io_exit,
	dm_kcopyd_exit,
	dm_interface_exit,
	dm_statistics_exit,
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
	struct mapped_device *md;

	spin_lock(&_minor_lock);

	md = disk->private_data;
	if (WARN_ON(!md))
		goto out;

	if (atomic_dec_and_test(&md->open_count) &&
	    (test_bit(DMF_DEFERRED_REMOVE, &md->flags)))
		queue_work(deferred_remove_workqueue, &deferred_remove_work);

	dm_put(md);
out:
	spin_unlock(&_minor_lock);
}

int dm_open_count(struct mapped_device *md)
{
	return atomic_read(&md->open_count);
}

/*
 * Guarantees nothing is using the device before it's deleted.
 */
int dm_lock_for_deletion(struct mapped_device *md, bool mark_deferred, bool only_deferred)
{
	int r = 0;

	spin_lock(&_minor_lock);

	if (dm_open_count(md)) {
		r = -EBUSY;
		if (mark_deferred)
			set_bit(DMF_DEFERRED_REMOVE, &md->flags);
	} else if (only_deferred && !test_bit(DMF_DEFERRED_REMOVE, &md->flags))
		r = -EEXIST;
	else
		set_bit(DMF_DELETING, &md->flags);

	spin_unlock(&_minor_lock);

	return r;
}

int dm_cancel_deferred_remove(struct mapped_device *md)
{
	int r = 0;

	spin_lock(&_minor_lock);

	if (test_bit(DMF_DELETING, &md->flags))
		r = -EBUSY;
	else
		clear_bit(DMF_DEFERRED_REMOVE, &md->flags);

	spin_unlock(&_minor_lock);

	return r;
}

static void do_deferred_remove(struct work_struct *w)
{
	dm_deferred_remove();
}

sector_t dm_get_size(struct mapped_device *md)
{
	return get_capacity(md->disk);
}

struct request_queue *dm_get_md_queue(struct mapped_device *md)
{
	return md->queue;
}

struct dm_stats *dm_get_stats(struct mapped_device *md)
{
	return &md->stats;
}

static int dm_blk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mapped_device *md = bdev->bd_disk->private_data;

	return dm_get_geometry(md, geo);
}

static int dm_grab_bdev_for_ioctl(struct mapped_device *md,
				  struct block_device **bdev,
				  fmode_t *mode)
{
	struct dm_target *tgt;
	struct dm_table *map;
	int srcu_idx, r;

retry:
	r = -ENOTTY;
	map = dm_get_live_table(md, &srcu_idx);
	if (!map || !dm_table_get_size(map))
		goto out;

	/* We only support devices that have a single target */
	if (dm_table_get_num_targets(map) != 1)
		goto out;

	tgt = dm_table_get_target(map, 0);
	if (!tgt->type->prepare_ioctl)
		goto out;

	if (dm_suspended_md(md)) {
		r = -EAGAIN;
		goto out;
	}

	r = tgt->type->prepare_ioctl(tgt, bdev, mode);
	if (r < 0)
		goto out;

	bdgrab(*bdev);
	dm_put_live_table(md, srcu_idx);
	return r;

out:
	dm_put_live_table(md, srcu_idx);
	if (r == -ENOTCONN && !fatal_signal_pending(current)) {
		msleep(10);
		goto retry;
	}
	return r;
}

static int dm_blk_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	int r;

	r = dm_grab_bdev_for_ioctl(md, &bdev, &mode);
	if (r < 0)
		return r;

	if (r > 0) {
		/*
		 * Target determined this ioctl is being issued against a
		 * subset of the parent bdev; require extra privileges.
		 */
		if (!capable(CAP_SYS_RAWIO)) {
			DMWARN_LIMIT(
	"%s: sending ioctl %x to DM device without required privilege.",
				current->comm, cmd);
			r = -ENOIOCTLCMD;
			goto out;
		}
	}

	r =  __blkdev_driver_ioctl(bdev, mode, cmd, arg);
out:
	bdput(bdev);
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

static void free_tio(struct dm_target_io *tio)
{
	bio_put(&tio->clone);
}

int md_in_flight(struct mapped_device *md)
{
	return atomic_read(&md->pending[READ]) +
	       atomic_read(&md->pending[WRITE]);
}

static void start_io_acct(struct dm_io *io)
{
	struct mapped_device *md = io->md;
	struct bio *bio = io->bio;
	int cpu;
	int rw = bio_data_dir(bio);

	io->start_time = jiffies;

	cpu = part_stat_lock();
	part_round_stats(md->queue, cpu, &dm_disk(md)->part0);
	part_stat_unlock();
	atomic_set(&dm_disk(md)->part0.in_flight[rw],
		atomic_inc_return(&md->pending[rw]));

	if (unlikely(dm_stats_used(&md->stats)))
		dm_stats_account_io(&md->stats, bio_data_dir(bio),
				    bio->bi_iter.bi_sector, bio_sectors(bio),
				    false, 0, &io->stats_aux);
}

static void end_io_acct(struct dm_io *io)
{
	struct mapped_device *md = io->md;
	struct bio *bio = io->bio;
	unsigned long duration = jiffies - io->start_time;
	int pending;
	int rw = bio_data_dir(bio);

	generic_end_io_acct(md->queue, rw, &dm_disk(md)->part0, io->start_time);

	if (unlikely(dm_stats_used(&md->stats)))
		dm_stats_account_io(&md->stats, bio_data_dir(bio),
				    bio->bi_iter.bi_sector, bio_sectors(bio),
				    true, duration, &io->stats_aux);

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
 * dm_put_live_table() when finished.
 */
struct dm_table *dm_get_live_table(struct mapped_device *md, int *srcu_idx) __acquires(md->io_barrier)
{
	*srcu_idx = srcu_read_lock(&md->io_barrier);

	return srcu_dereference(md->map, &md->io_barrier);
}

void dm_put_live_table(struct mapped_device *md, int srcu_idx) __releases(md->io_barrier)
{
	srcu_read_unlock(&md->io_barrier, srcu_idx);
}

void dm_sync_table(struct mapped_device *md)
{
	synchronize_srcu(&md->io_barrier);
	synchronize_rcu_expedited();
}

/*
 * A fast alternative to dm_get_live_table/dm_put_live_table.
 * The caller must not block between these two functions.
 */
static struct dm_table *dm_get_live_table_fast(struct mapped_device *md) __acquires(RCU)
{
	rcu_read_lock();
	return rcu_dereference(md->map);
}

static void dm_put_live_table_fast(struct mapped_device *md) __releases(RCU)
{
	rcu_read_unlock();
}

/*
 * Open a table device so we can use it as a map destination.
 */
static int open_table_device(struct table_device *td, dev_t dev,
			     struct mapped_device *md)
{
	static char *_claim_ptr = "I belong to device-mapper";
	struct block_device *bdev;

	int r;

	BUG_ON(td->dm_dev.bdev);

	bdev = blkdev_get_by_dev(dev, td->dm_dev.mode | FMODE_EXCL, _claim_ptr);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);

	r = bd_link_disk_holder(bdev, dm_disk(md));
	if (r) {
		blkdev_put(bdev, td->dm_dev.mode | FMODE_EXCL);
		return r;
	}

	td->dm_dev.bdev = bdev;
	td->dm_dev.dax_dev = dax_get_by_host(bdev->bd_disk->disk_name);
	return 0;
}

/*
 * Close a table device that we've been using.
 */
static void close_table_device(struct table_device *td, struct mapped_device *md)
{
	if (!td->dm_dev.bdev)
		return;

	bd_unlink_disk_holder(td->dm_dev.bdev, dm_disk(md));
	blkdev_put(td->dm_dev.bdev, td->dm_dev.mode | FMODE_EXCL);
	put_dax(td->dm_dev.dax_dev);
	td->dm_dev.bdev = NULL;
	td->dm_dev.dax_dev = NULL;
}

static struct table_device *find_table_device(struct list_head *l, dev_t dev,
					      fmode_t mode) {
	struct table_device *td;

	list_for_each_entry(td, l, list)
		if (td->dm_dev.bdev->bd_dev == dev && td->dm_dev.mode == mode)
			return td;

	return NULL;
}

int dm_get_table_device(struct mapped_device *md, dev_t dev, fmode_t mode,
			struct dm_dev **result) {
	int r;
	struct table_device *td;

	mutex_lock(&md->table_devices_lock);
	td = find_table_device(&md->table_devices, dev, mode);
	if (!td) {
		td = kmalloc_node(sizeof(*td), GFP_KERNEL, md->numa_node_id);
		if (!td) {
			mutex_unlock(&md->table_devices_lock);
			return -ENOMEM;
		}

		td->dm_dev.mode = mode;
		td->dm_dev.bdev = NULL;

		if ((r = open_table_device(td, dev, md))) {
			mutex_unlock(&md->table_devices_lock);
			kfree(td);
			return r;
		}

		format_dev_t(td->dm_dev.name, dev);

		refcount_set(&td->count, 1);
		list_add(&td->list, &md->table_devices);
	} else {
		refcount_inc(&td->count);
	}
	mutex_unlock(&md->table_devices_lock);

	*result = &td->dm_dev;
	return 0;
}
EXPORT_SYMBOL_GPL(dm_get_table_device);

void dm_put_table_device(struct mapped_device *md, struct dm_dev *d)
{
	struct table_device *td = container_of(d, struct table_device, dm_dev);

	mutex_lock(&md->table_devices_lock);
	if (refcount_dec_and_test(&td->count)) {
		close_table_device(td, md);
		list_del(&td->list);
		kfree(td);
	}
	mutex_unlock(&md->table_devices_lock);
}
EXPORT_SYMBOL(dm_put_table_device);

static void free_table_devices(struct list_head *devices)
{
	struct list_head *tmp, *next;

	list_for_each_safe(tmp, next, devices) {
		struct table_device *td = list_entry(tmp, struct table_device, list);

		DMWARN("dm_destroy: %s still exists with %d references",
		       td->dm_dev.name, refcount_read(&td->count));
		kfree(td);
	}
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
static void dec_pending(struct dm_io *io, blk_status_t error)
{
	unsigned long flags;
	blk_status_t io_error;
	struct bio *bio;
	struct mapped_device *md = io->md;

	/* Push-back supersedes any I/O errors */
	if (unlikely(error)) {
		spin_lock_irqsave(&io->endio_lock, flags);
		if (!(io->status == BLK_STS_DM_REQUEUE &&
				__noflush_suspending(md)))
			io->status = error;
		spin_unlock_irqrestore(&io->endio_lock, flags);
	}

	if (atomic_dec_and_test(&io->io_count)) {
		if (io->status == BLK_STS_DM_REQUEUE) {
			/*
			 * Target requested pushing back the I/O.
			 */
			spin_lock_irqsave(&md->deferred_lock, flags);
			if (__noflush_suspending(md))
				bio_list_add_head(&md->deferred, io->bio);
			else
				/* noflush suspend was interrupted. */
				io->status = BLK_STS_IOERR;
			spin_unlock_irqrestore(&md->deferred_lock, flags);
		}

		io_error = io->status;
		bio = io->bio;
		end_io_acct(io);
		free_io(md, io);

		if (io_error == BLK_STS_DM_REQUEUE)
			return;

		if ((bio->bi_opf & REQ_PREFLUSH) && bio->bi_iter.bi_size) {
			/*
			 * Preflush done for flush with data, reissue
			 * without REQ_PREFLUSH.
			 */
			bio->bi_opf &= ~REQ_PREFLUSH;
			queue_io(md, bio);
		} else {
			/* done with normal IO or empty flush */
			bio->bi_status = io_error;
			bio_endio(bio);
		}
	}
}

void disable_write_same(struct mapped_device *md)
{
	struct queue_limits *limits = dm_get_queue_limits(md);

	/* device doesn't really support WRITE SAME, disable it */
	limits->max_write_same_sectors = 0;
}

void disable_write_zeroes(struct mapped_device *md)
{
	struct queue_limits *limits = dm_get_queue_limits(md);

	/* device doesn't really support WRITE ZEROES, disable it */
	limits->max_write_zeroes_sectors = 0;
}

static void clone_endio(struct bio *bio)
{
	blk_status_t error = bio->bi_status;
	struct dm_target_io *tio = container_of(bio, struct dm_target_io, clone);
	struct dm_io *io = tio->io;
	struct mapped_device *md = tio->io->md;
	dm_endio_fn endio = tio->ti->type->end_io;

	if (unlikely(error == BLK_STS_TARGET)) {
		if (bio_op(bio) == REQ_OP_WRITE_SAME &&
		    !bio->bi_disk->queue->limits.max_write_same_sectors)
			disable_write_same(md);
		if (bio_op(bio) == REQ_OP_WRITE_ZEROES &&
		    !bio->bi_disk->queue->limits.max_write_zeroes_sectors)
			disable_write_zeroes(md);
	}

	if (endio) {
		int r = endio(tio->ti, bio, &error);
		switch (r) {
		case DM_ENDIO_REQUEUE:
			error = BLK_STS_DM_REQUEUE;
			/*FALLTHRU*/
		case DM_ENDIO_DONE:
			break;
		case DM_ENDIO_INCOMPLETE:
			/* The target will handle the io */
			return;
		default:
			DMWARN("unimplemented target endio return value: %d", r);
			BUG();
		}
	}

	free_tio(tio);
	dec_pending(io, error);
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

static struct dm_target *dm_dax_get_live_target(struct mapped_device *md,
		sector_t sector, int *srcu_idx)
{
	struct dm_table *map;
	struct dm_target *ti;

	map = dm_get_live_table(md, srcu_idx);
	if (!map)
		return NULL;

	ti = dm_table_find_target(map, sector);
	if (!dm_target_is_valid(ti))
		return NULL;

	return ti;
}

static long dm_dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff,
		long nr_pages, void **kaddr, pfn_t *pfn)
{
	struct mapped_device *md = dax_get_private(dax_dev);
	sector_t sector = pgoff * PAGE_SECTORS;
	struct dm_target *ti;
	long len, ret = -EIO;
	int srcu_idx;

	ti = dm_dax_get_live_target(md, sector, &srcu_idx);

	if (!ti)
		goto out;
	if (!ti->type->direct_access)
		goto out;
	len = max_io_len(sector, ti) / PAGE_SECTORS;
	if (len < 1)
		goto out;
	nr_pages = min(len, nr_pages);
	if (ti->type->direct_access)
		ret = ti->type->direct_access(ti, pgoff, nr_pages, kaddr, pfn);

 out:
	dm_put_live_table(md, srcu_idx);

	return ret;
}

static size_t dm_dax_copy_from_iter(struct dax_device *dax_dev, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i)
{
	struct mapped_device *md = dax_get_private(dax_dev);
	sector_t sector = pgoff * PAGE_SECTORS;
	struct dm_target *ti;
	long ret = 0;
	int srcu_idx;

	ti = dm_dax_get_live_target(md, sector, &srcu_idx);

	if (!ti)
		goto out;
	if (!ti->type->dax_copy_from_iter) {
		ret = copy_from_iter(addr, bytes, i);
		goto out;
	}
	ret = ti->type->dax_copy_from_iter(ti, pgoff, addr, bytes, i);
 out:
	dm_put_live_table(md, srcu_idx);

	return ret;
}

/*
 * A target may call dm_accept_partial_bio only from the map routine.  It is
 * allowed for all bio types except REQ_PREFLUSH.
 *
 * dm_accept_partial_bio informs the dm that the target only wants to process
 * additional n_sectors sectors of the bio and the rest of the data should be
 * sent in a next bio.
 *
 * A diagram that explains the arithmetics:
 * +--------------------+---------------+-------+
 * |         1          |       2       |   3   |
 * +--------------------+---------------+-------+
 *
 * <-------------- *tio->len_ptr --------------->
 *                      <------- bi_size ------->
 *                      <-- n_sectors -->
 *
 * Region 1 was already iterated over with bio_advance or similar function.
 *	(it may be empty if the target doesn't use bio_advance)
 * Region 2 is the remaining bio size that the target wants to process.
 *	(it may be empty if region 1 is non-empty, although there is no reason
 *	 to make it empty)
 * The target requires that region 3 is to be sent in the next bio.
 *
 * If the target wants to receive multiple copies of the bio (via num_*bios, etc),
 * the partially processed part (the sum of regions 1+2) must be the same for all
 * copies of the bio.
 */
void dm_accept_partial_bio(struct bio *bio, unsigned n_sectors)
{
	struct dm_target_io *tio = container_of(bio, struct dm_target_io, clone);
	unsigned bi_size = bio->bi_iter.bi_size >> SECTOR_SHIFT;
	BUG_ON(bio->bi_opf & REQ_PREFLUSH);
	BUG_ON(bi_size > *tio->len_ptr);
	BUG_ON(n_sectors > bi_size);
	*tio->len_ptr -= bi_size - n_sectors;
	bio->bi_iter.bi_size = n_sectors << SECTOR_SHIFT;
}
EXPORT_SYMBOL_GPL(dm_accept_partial_bio);

/*
 * The zone descriptors obtained with a zone report indicate
 * zone positions within the target device. The zone descriptors
 * must be remapped to match their position within the dm device.
 * A target may call dm_remap_zone_report after completion of a
 * REQ_OP_ZONE_REPORT bio to remap the zone descriptors obtained
 * from the target device mapping to the dm device.
 */
void dm_remap_zone_report(struct dm_target *ti, struct bio *bio, sector_t start)
{
#ifdef CONFIG_BLK_DEV_ZONED
	struct dm_target_io *tio = container_of(bio, struct dm_target_io, clone);
	struct bio *report_bio = tio->io->bio;
	struct blk_zone_report_hdr *hdr = NULL;
	struct blk_zone *zone;
	unsigned int nr_rep = 0;
	unsigned int ofst;
	struct bio_vec bvec;
	struct bvec_iter iter;
	void *addr;

	if (bio->bi_status)
		return;

	/*
	 * Remap the start sector of the reported zones. For sequential zones,
	 * also remap the write pointer position.
	 */
	bio_for_each_segment(bvec, report_bio, iter) {
		addr = kmap_atomic(bvec.bv_page);

		/* Remember the report header in the first page */
		if (!hdr) {
			hdr = addr;
			ofst = sizeof(struct blk_zone_report_hdr);
		} else
			ofst = 0;

		/* Set zones start sector */
		while (hdr->nr_zones && ofst < bvec.bv_len) {
			zone = addr + ofst;
			if (zone->start >= start + ti->len) {
				hdr->nr_zones = 0;
				break;
			}
			zone->start = zone->start + ti->begin - start;
			if (zone->type != BLK_ZONE_TYPE_CONVENTIONAL) {
				if (zone->cond == BLK_ZONE_COND_FULL)
					zone->wp = zone->start + zone->len;
				else if (zone->cond == BLK_ZONE_COND_EMPTY)
					zone->wp = zone->start;
				else
					zone->wp = zone->wp + ti->begin - start;
			}
			ofst += sizeof(struct blk_zone);
			hdr->nr_zones--;
			nr_rep++;
		}

		if (addr != hdr)
			kunmap_atomic(addr);

		if (!hdr->nr_zones)
			break;
	}

	if (hdr) {
		hdr->nr_zones = nr_rep;
		kunmap_atomic(hdr);
	}

	bio_advance(report_bio, report_bio->bi_iter.bi_size);

#else /* !CONFIG_BLK_DEV_ZONED */
	bio->bi_status = BLK_STS_NOTSUPP;
#endif
}
EXPORT_SYMBOL_GPL(dm_remap_zone_report);

/*
 * Flush current->bio_list when the target map method blocks.
 * This fixes deadlocks in snapshot and possibly in other targets.
 */
struct dm_offload {
	struct blk_plug plug;
	struct blk_plug_cb cb;
};

static void flush_current_bio_list(struct blk_plug_cb *cb, bool from_schedule)
{
	struct dm_offload *o = container_of(cb, struct dm_offload, cb);
	struct bio_list list;
	struct bio *bio;
	int i;

	INIT_LIST_HEAD(&o->cb.list);

	if (unlikely(!current->bio_list))
		return;

	for (i = 0; i < 2; i++) {
		list = current->bio_list[i];
		bio_list_init(&current->bio_list[i]);

		while ((bio = bio_list_pop(&list))) {
			struct bio_set *bs = bio->bi_pool;
			if (unlikely(!bs) || bs == fs_bio_set ||
			    !bs->rescue_workqueue) {
				bio_list_add(&current->bio_list[i], bio);
				continue;
			}

			spin_lock(&bs->rescue_lock);
			bio_list_add(&bs->rescue_list, bio);
			queue_work(bs->rescue_workqueue, &bs->rescue_work);
			spin_unlock(&bs->rescue_lock);
		}
	}
}

static void dm_offload_start(struct dm_offload *o)
{
	blk_start_plug(&o->plug);
	o->cb.callback = flush_current_bio_list;
	list_add(&o->cb.list, &current->plug->cb_list);
}

static void dm_offload_end(struct dm_offload *o)
{
	list_del(&o->cb.list);
	blk_finish_plug(&o->plug);
}

static void __map_bio(struct dm_target_io *tio)
{
	int r;
	sector_t sector;
	struct dm_offload o;
	struct bio *clone = &tio->clone;
	struct dm_target *ti = tio->ti;

	clone->bi_end_io = clone_endio;

	/*
	 * Map the clone.  If r == 0 we don't need to do
	 * anything, the target has assumed ownership of
	 * this io.
	 */
	atomic_inc(&tio->io->io_count);
	sector = clone->bi_iter.bi_sector;

	dm_offload_start(&o);
	r = ti->type->map(ti, clone);
	dm_offload_end(&o);

	switch (r) {
	case DM_MAPIO_SUBMITTED:
		break;
	case DM_MAPIO_REMAPPED:
		/* the bio has been remapped so dispatch it */
		trace_block_bio_remap(clone->bi_disk->queue, clone,
				      bio_dev(tio->io->bio), sector);
		generic_make_request(clone);
		break;
	case DM_MAPIO_KILL:
		dec_pending(tio->io, BLK_STS_IOERR);
		free_tio(tio);
		break;
	case DM_MAPIO_REQUEUE:
		dec_pending(tio->io, BLK_STS_DM_REQUEUE);
		free_tio(tio);
		break;
	default:
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
	unsigned sector_count;
};

static void bio_setup_sector(struct bio *bio, sector_t sector, unsigned len)
{
	bio->bi_iter.bi_sector = sector;
	bio->bi_iter.bi_size = to_bytes(len);
}

/*
 * Creates a bio that consists of range of complete bvecs.
 */
static int clone_bio(struct dm_target_io *tio, struct bio *bio,
		     sector_t sector, unsigned len)
{
	struct bio *clone = &tio->clone;

	__bio_clone_fast(clone, bio);

	if (unlikely(bio_integrity(bio) != NULL)) {
		int r;

		if (unlikely(!dm_target_has_integrity(tio->ti->type) &&
			     !dm_target_passes_integrity(tio->ti->type))) {
			DMWARN("%s: the target %s doesn't support integrity data.",
				dm_device_name(tio->io->md),
				tio->ti->type->name);
			return -EIO;
		}

		r = bio_integrity_clone(clone, bio, GFP_NOIO);
		if (r < 0)
			return r;
	}

	if (bio_op(bio) != REQ_OP_ZONE_REPORT)
		bio_advance(clone, to_bytes(sector - clone->bi_iter.bi_sector));
	clone->bi_iter.bi_size = to_bytes(len);

	if (unlikely(bio_integrity(bio) != NULL))
		bio_integrity_trim(clone);

	return 0;
}

static struct dm_target_io *alloc_tio(struct clone_info *ci,
				      struct dm_target *ti,
				      unsigned target_bio_nr)
{
	struct dm_target_io *tio;
	struct bio *clone;

	clone = bio_alloc_bioset(GFP_NOIO, 0, ci->md->bs);
	tio = container_of(clone, struct dm_target_io, clone);

	tio->io = ci->io;
	tio->ti = ti;
	tio->target_bio_nr = target_bio_nr;

	return tio;
}

static void __clone_and_map_simple_bio(struct clone_info *ci,
				       struct dm_target *ti,
				       unsigned target_bio_nr, unsigned *len)
{
	struct dm_target_io *tio = alloc_tio(ci, ti, target_bio_nr);
	struct bio *clone = &tio->clone;

	tio->len_ptr = len;

	__bio_clone_fast(clone, ci->bio);
	if (len)
		bio_setup_sector(clone, ci->sector, *len);

	__map_bio(tio);
}

static void __send_duplicate_bios(struct clone_info *ci, struct dm_target *ti,
				  unsigned num_bios, unsigned *len)
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
		__send_duplicate_bios(ci, ti, ti->num_flush_bios, NULL);

	return 0;
}

static int __clone_and_map_data_bio(struct clone_info *ci, struct dm_target *ti,
				     sector_t sector, unsigned *len)
{
	struct bio *bio = ci->bio;
	struct dm_target_io *tio;
	unsigned target_bio_nr;
	unsigned num_target_bios = 1;
	int r = 0;

	/*
	 * Does the target want to receive duplicate copies of the bio?
	 */
	if (bio_data_dir(bio) == WRITE && ti->num_write_bios)
		num_target_bios = ti->num_write_bios(ti, bio);

	for (target_bio_nr = 0; target_bio_nr < num_target_bios; target_bio_nr++) {
		tio = alloc_tio(ci, ti, target_bio_nr);
		tio->len_ptr = len;
		r = clone_bio(tio, bio, sector, *len);
		if (r < 0) {
			free_tio(tio);
			break;
		}
		__map_bio(tio);
	}

	return r;
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

static unsigned get_num_write_zeroes_bios(struct dm_target *ti)
{
	return ti->num_write_zeroes_bios;
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
	unsigned len;
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
			len = min((sector_t)ci->sector_count, max_io_len_target_boundary(ci->sector, ti));
		else
			len = min((sector_t)ci->sector_count, max_io_len(ci->sector, ti));

		__send_duplicate_bios(ci, ti, num_bios, &len);

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

static int __send_write_zeroes(struct clone_info *ci)
{
	return __send_changing_extent_only(ci, get_num_write_zeroes_bios, NULL);
}

/*
 * Select the correct strategy for processing a non-flush bio.
 */
static int __split_and_process_non_flush(struct clone_info *ci)
{
	struct bio *bio = ci->bio;
	struct dm_target *ti;
	unsigned len;
	int r;

	if (unlikely(bio_op(bio) == REQ_OP_DISCARD))
		return __send_discard(ci);
	else if (unlikely(bio_op(bio) == REQ_OP_WRITE_SAME))
		return __send_write_same(ci);
	else if (unlikely(bio_op(bio) == REQ_OP_WRITE_ZEROES))
		return __send_write_zeroes(ci);

	ti = dm_table_find_target(ci->map, ci->sector);
	if (!dm_target_is_valid(ti))
		return -EIO;

	if (bio_op(bio) == REQ_OP_ZONE_REPORT)
		len = ci->sector_count;
	else
		len = min_t(sector_t, max_io_len(ci->sector, ti),
			    ci->sector_count);

	r = __clone_and_map_data_bio(ci, ti, ci->sector, &len);
	if (r < 0)
		return r;

	ci->sector += len;
	ci->sector_count -= len;

	return 0;
}

/*
 * Entry point to split a bio into clones and submit them to the targets.
 */
static void __split_and_process_bio(struct mapped_device *md,
				    struct dm_table *map, struct bio *bio)
{
	struct clone_info ci;
	int error = 0;

	if (unlikely(!map)) {
		bio_io_error(bio);
		return;
	}

	ci.map = map;
	ci.md = md;
	ci.io = alloc_io(md);
	ci.io->status = 0;
	atomic_set(&ci.io->io_count, 1);
	ci.io->bio = bio;
	ci.io->md = md;
	spin_lock_init(&ci.io->endio_lock);
	ci.sector = bio->bi_iter.bi_sector;

	start_io_acct(ci.io);

	if (bio->bi_opf & REQ_PREFLUSH) {
		ci.bio = &ci.md->flush_bio;
		ci.sector_count = 0;
		error = __send_empty_flush(&ci);
		/* dec_pending submits any data associated with flush */
	} else if (bio_op(bio) == REQ_OP_ZONE_RESET) {
		ci.bio = bio;
		ci.sector_count = 0;
		error = __split_and_process_non_flush(&ci);
	} else {
		ci.bio = bio;
		ci.sector_count = bio_sectors(bio);
		while (ci.sector_count && !error)
			error = __split_and_process_non_flush(&ci);
	}

	/* drop the extra reference count */
	dec_pending(ci.io, errno_to_blk_status(error));
}
/*-----------------------------------------------------------------
 * CRUD END
 *---------------------------------------------------------------*/

/*
 * The request function that just remaps the bio built up by
 * dm_merge_bvec.
 */
static blk_qc_t dm_make_request(struct request_queue *q, struct bio *bio)
{
	int rw = bio_data_dir(bio);
	struct mapped_device *md = q->queuedata;
	int srcu_idx;
	struct dm_table *map;

	map = dm_get_live_table(md, &srcu_idx);

	generic_start_io_acct(q, rw, bio_sectors(bio), &dm_disk(md)->part0);

	/* if we're suspended, we have to queue this io for later */
	if (unlikely(test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags))) {
		dm_put_live_table(md, srcu_idx);

		if (!(bio->bi_opf & REQ_RAHEAD))
			queue_io(md, bio);
		else
			bio_io_error(bio);
		return BLK_QC_T_NONE;
	}

	__split_and_process_bio(md, map, bio);
	dm_put_live_table(md, srcu_idx);
	return BLK_QC_T_NONE;
}

static int dm_any_congested(void *congested_data, int bdi_bits)
{
	int r = bdi_bits;
	struct mapped_device *md = congested_data;
	struct dm_table *map;

	if (!test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) {
		if (dm_request_based(md)) {
			/*
			 * With request-based DM we only need to check the
			 * top-level queue for congestion.
			 */
			r = md->queue->backing_dev_info->wb.state & bdi_bits;
		} else {
			map = dm_get_live_table_fast(md);
			if (map)
				r = dm_table_any_congested(map, bdi_bits);
			dm_put_live_table_fast(md);
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
static const struct dax_operations dm_dax_ops;

static void dm_wq_work(struct work_struct *work);

void dm_init_md_queue(struct mapped_device *md)
{
	/*
	 * Request-based dm devices cannot be stacked on top of bio-based dm
	 * devices.  The type of this dm device may not have been decided yet.
	 * The type is decided at the first table loading time.
	 * To prevent problematic device stacking, clear the queue flag
	 * for request stacking support until then.
	 *
	 * This queue is new, so no concurrency on the queue_flags.
	 */
	queue_flag_clear_unlocked(QUEUE_FLAG_STACKABLE, md->queue);

	/*
	 * Initialize data that will only be used by a non-blk-mq DM queue
	 * - must do so here (in alloc_dev callchain) before queue is used
	 */
	md->queue->queuedata = md;
	md->queue->backing_dev_info->congested_data = md;
}

void dm_init_normal_md_queue(struct mapped_device *md)
{
	md->use_blk_mq = false;
	dm_init_md_queue(md);

	/*
	 * Initialize aspects of queue that aren't relevant for blk-mq
	 */
	md->queue->backing_dev_info->congested_fn = dm_any_congested;
}

static void cleanup_mapped_device(struct mapped_device *md)
{
	if (md->wq)
		destroy_workqueue(md->wq);
	if (md->kworker_task)
		kthread_stop(md->kworker_task);
	mempool_destroy(md->io_pool);
	if (md->bs)
		bioset_free(md->bs);

	if (md->dax_dev) {
		kill_dax(md->dax_dev);
		put_dax(md->dax_dev);
		md->dax_dev = NULL;
	}

	if (md->disk) {
		spin_lock(&_minor_lock);
		md->disk->private_data = NULL;
		spin_unlock(&_minor_lock);
		del_gendisk(md->disk);
		put_disk(md->disk);
	}

	if (md->queue)
		blk_cleanup_queue(md->queue);

	cleanup_srcu_struct(&md->io_barrier);

	if (md->bdev) {
		bdput(md->bdev);
		md->bdev = NULL;
	}

	dm_mq_cleanup_mapped_device(md);
}

/*
 * Allocate and initialise a blank device with a given minor.
 */
static struct mapped_device *alloc_dev(int minor)
{
	int r, numa_node_id = dm_get_numa_node();
	struct dax_device *dax_dev;
	struct mapped_device *md;
	void *old_md;

	md = kvzalloc_node(sizeof(*md), GFP_KERNEL, numa_node_id);
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

	r = init_srcu_struct(&md->io_barrier);
	if (r < 0)
		goto bad_io_barrier;

	md->numa_node_id = numa_node_id;
	md->use_blk_mq = dm_use_blk_mq_default();
	md->init_tio_pdu = false;
	md->type = DM_TYPE_NONE;
	mutex_init(&md->suspend_lock);
	mutex_init(&md->type_lock);
	mutex_init(&md->table_devices_lock);
	spin_lock_init(&md->deferred_lock);
	atomic_set(&md->holders, 1);
	atomic_set(&md->open_count, 0);
	atomic_set(&md->event_nr, 0);
	atomic_set(&md->uevent_seq, 0);
	INIT_LIST_HEAD(&md->uevent_list);
	INIT_LIST_HEAD(&md->table_devices);
	spin_lock_init(&md->uevent_lock);

	md->queue = blk_alloc_queue_node(GFP_KERNEL, numa_node_id);
	if (!md->queue)
		goto bad;

	dm_init_md_queue(md);

	md->disk = alloc_disk_node(1, numa_node_id);
	if (!md->disk)
		goto bad;

	atomic_set(&md->pending[0], 0);
	atomic_set(&md->pending[1], 0);
	init_waitqueue_head(&md->wait);
	INIT_WORK(&md->work, dm_wq_work);
	init_waitqueue_head(&md->eventq);
	init_completion(&md->kobj_holder.completion);
	md->kworker_task = NULL;

	md->disk->major = _major;
	md->disk->first_minor = minor;
	md->disk->fops = &dm_blk_dops;
	md->disk->queue = md->queue;
	md->disk->private_data = md;
	sprintf(md->disk->disk_name, "dm-%d", minor);

	dax_dev = alloc_dax(md, md->disk->disk_name, &dm_dax_ops);
	if (!dax_dev)
		goto bad;
	md->dax_dev = dax_dev;

	add_disk(md->disk);
	format_dev_t(md->name, MKDEV(_major, minor));

	md->wq = alloc_workqueue("kdmflush", WQ_MEM_RECLAIM, 0);
	if (!md->wq)
		goto bad;

	md->bdev = bdget_disk(md->disk, 0);
	if (!md->bdev)
		goto bad;

	bio_init(&md->flush_bio, NULL, 0);
	bio_set_dev(&md->flush_bio, md->bdev);
	md->flush_bio.bi_opf = REQ_OP_WRITE | REQ_PREFLUSH | REQ_SYNC;

	dm_stats_init(&md->stats);

	/* Populate the mapping, nobody knows we exist yet */
	spin_lock(&_minor_lock);
	old_md = idr_replace(&_minor_idr, md, minor);
	spin_unlock(&_minor_lock);

	BUG_ON(old_md != MINOR_ALLOCED);

	return md;

bad:
	cleanup_mapped_device(md);
bad_io_barrier:
	free_minor(minor);
bad_minor:
	module_put(THIS_MODULE);
bad_module_get:
	kvfree(md);
	return NULL;
}

static void unlock_fs(struct mapped_device *md);

static void free_dev(struct mapped_device *md)
{
	int minor = MINOR(disk_devt(md->disk));

	unlock_fs(md);

	cleanup_mapped_device(md);

	free_table_devices(&md->table_devices);
	dm_stats_cleanup(&md->stats);
	free_minor(minor);

	module_put(THIS_MODULE);
	kvfree(md);
}

static void __bind_mempools(struct mapped_device *md, struct dm_table *t)
{
	struct dm_md_mempools *p = dm_table_get_md_mempools(t);

	if (md->bs) {
		/* The md already has necessary mempools. */
		if (dm_table_bio_based(t)) {
			/*
			 * Reload bioset because front_pad may have changed
			 * because a different table was loaded.
			 */
			bioset_free(md->bs);
			md->bs = p->bs;
			p->bs = NULL;
		}
		/*
		 * There's no need to reload with request-based dm
		 * because the size of front_pad doesn't change.
		 * Note for future: If you are to reload bioset,
		 * prep-ed requests in the queue may refer
		 * to bio from the old bioset, so you must walk
		 * through the queue to unprep.
		 */
		goto out;
	}

	BUG_ON(!p || md->io_pool || md->bs);

	md->io_pool = p->io_pool;
	p->io_pool = NULL;
	md->bs = p->bs;
	p->bs = NULL;

out:
	/* mempool bind completed, no longer need any mempools in the table */
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
	dm_issue_global_event();
}

/*
 * Protected by md->suspend_lock obtained by dm_swap_table().
 */
static void __set_size(struct mapped_device *md, sector_t size)
{
	lockdep_assert_held(&md->suspend_lock);

	set_capacity(md->disk, size);

	i_size_write(md->bdev->bd_inode, (loff_t)size << SECTOR_SHIFT);
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

	lockdep_assert_held(&md->suspend_lock);

	size = dm_table_get_size(t);

	/*
	 * Wipe any geometry if the size of the table changed.
	 */
	if (size != dm_get_size(md))
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
	if (dm_table_request_based(t)) {
		dm_stop_queue(q);
		/*
		 * Leverage the fact that request-based DM targets are
		 * immutable singletons and establish md->immutable_target
		 * - used to optimize both dm_request_fn and dm_mq_queue_rq
		 */
		md->immutable_target = dm_table_get_immutable_target(t);
	}

	__bind_mempools(md, t);

	old_map = rcu_dereference_protected(md->map, lockdep_is_held(&md->suspend_lock));
	rcu_assign_pointer(md->map, (void *)t);
	md->immutable_target_type = dm_table_get_immutable_target_type(t);

	dm_table_set_restrictions(t, q, limits);
	if (old_map)
		dm_sync_table(md);

	return old_map;
}

/*
 * Returns unbound table for the caller to free.
 */
static struct dm_table *__unbind(struct mapped_device *md)
{
	struct dm_table *map = rcu_dereference_protected(md->map, 1);

	if (!map)
		return NULL;

	dm_table_event_callback(map, NULL, NULL);
	RCU_INIT_POINTER(md->map, NULL);
	dm_sync_table(md);

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

void dm_set_md_type(struct mapped_device *md, enum dm_queue_mode type)
{
	BUG_ON(!mutex_is_locked(&md->type_lock));
	md->type = type;
}

enum dm_queue_mode dm_get_md_type(struct mapped_device *md)
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
 * Setup the DM device's queue based on md's type
 */
int dm_setup_md_queue(struct mapped_device *md, struct dm_table *t)
{
	int r;
	enum dm_queue_mode type = dm_get_md_type(md);

	switch (type) {
	case DM_TYPE_REQUEST_BASED:
		r = dm_old_init_request_queue(md, t);
		if (r) {
			DMERR("Cannot initialize queue for request-based mapped device");
			return r;
		}
		break;
	case DM_TYPE_MQ_REQUEST_BASED:
		r = dm_mq_init_request_queue(md, t);
		if (r) {
			DMERR("Cannot initialize queue for request-based dm-mq mapped device");
			return r;
		}
		break;
	case DM_TYPE_BIO_BASED:
	case DM_TYPE_DAX_BIO_BASED:
		dm_init_normal_md_queue(md);
		blk_queue_make_request(md->queue, dm_make_request);
		/*
		 * DM handles splitting bios as needed.  Free the bio_split bioset
		 * since it won't be used (saves 1 process per bio-based DM device).
		 */
		bioset_free(md->queue->bio_split);
		md->queue->bio_split = NULL;

		if (type == DM_TYPE_DAX_BIO_BASED)
			queue_flag_set_unlocked(QUEUE_FLAG_DAX, md->queue);
		break;
	case DM_TYPE_NONE:
		WARN_ON_ONCE(true);
		break;
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

int dm_hold(struct mapped_device *md)
{
	spin_lock(&_minor_lock);
	if (test_bit(DMF_FREEING, &md->flags)) {
		spin_unlock(&_minor_lock);
		return -EBUSY;
	}
	dm_get(md);
	spin_unlock(&_minor_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(dm_hold);

const char *dm_device_name(struct mapped_device *md)
{
	return md->name;
}
EXPORT_SYMBOL_GPL(dm_device_name);

static void __dm_destroy(struct mapped_device *md, bool wait)
{
	struct request_queue *q = dm_get_md_queue(md);
	struct dm_table *map;
	int srcu_idx;

	might_sleep();

	spin_lock(&_minor_lock);
	idr_replace(&_minor_idr, MINOR_ALLOCED, MINOR(disk_devt(dm_disk(md))));
	set_bit(DMF_FREEING, &md->flags);
	spin_unlock(&_minor_lock);

	blk_set_queue_dying(q);

	if (dm_request_based(md) && md->kworker_task)
		kthread_flush_worker(&md->kworker);

	/*
	 * Take suspend_lock so that presuspend and postsuspend methods
	 * do not race with internal suspend.
	 */
	mutex_lock(&md->suspend_lock);
	map = dm_get_live_table(md, &srcu_idx);
	if (!dm_suspended_md(md)) {
		dm_table_presuspend_targets(map);
		dm_table_postsuspend_targets(map);
	}
	/* dm_put_live_table must be before msleep, otherwise deadlock is possible */
	dm_put_live_table(md, srcu_idx);
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

static int dm_wait_for_completion(struct mapped_device *md, long task_state)
{
	int r = 0;
	DEFINE_WAIT(wait);

	while (1) {
		prepare_to_wait(&md->wait, &wait, task_state);

		if (!md_in_flight(md))
			break;

		if (signal_pending_state(task_state, current)) {
			r = -EINTR;
			break;
		}

		io_schedule();
	}
	finish_wait(&md->wait, &wait);

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
	int srcu_idx;
	struct dm_table *map;

	map = dm_get_live_table(md, &srcu_idx);

	while (!test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) {
		spin_lock_irq(&md->deferred_lock);
		c = bio_list_pop(&md->deferred);
		spin_unlock_irq(&md->deferred_lock);

		if (!c)
			break;

		if (dm_request_based(md))
			generic_make_request(c);
		else
			__split_and_process_bio(md, map, c);
	}

	dm_put_live_table(md, srcu_idx);
}

static void dm_queue_flush(struct mapped_device *md)
{
	clear_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	smp_mb__after_atomic();
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
		live_map = dm_get_live_table_fast(md);
		if (live_map)
			limits = md->queue->limits;
		dm_put_live_table_fast(md);
	}

	if (!live_map) {
		r = dm_calculate_queue_limits(table, &limits);
		if (r) {
			map = ERR_PTR(r);
			goto out;
		}
	}

	map = __bind(md, table, &limits);
	dm_issue_global_event();

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
 * @suspend_flags: DM_SUSPEND_LOCKFS_FLAG and/or DM_SUSPEND_NOFLUSH_FLAG
 * @task_state: e.g. TASK_INTERRUPTIBLE or TASK_UNINTERRUPTIBLE
 * @dmf_suspended_flag: DMF_SUSPENDED or DMF_SUSPENDED_INTERNALLY
 *
 * If __dm_suspend returns 0, the device is completely quiescent
 * now. There is no request-processing activity. All new requests
 * are being added to md->deferred list.
 */
static int __dm_suspend(struct mapped_device *md, struct dm_table *map,
			unsigned suspend_flags, long task_state,
			int dmf_suspended_flag)
{
	bool do_lockfs = suspend_flags & DM_SUSPEND_LOCKFS_FLAG;
	bool noflush = suspend_flags & DM_SUSPEND_NOFLUSH_FLAG;
	int r;

	lockdep_assert_held(&md->suspend_lock);

	/*
	 * DMF_NOFLUSH_SUSPENDING must be set before presuspend.
	 * This flag is cleared before dm_suspend returns.
	 */
	if (noflush)
		set_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);
	else
		pr_debug("%s: suspending with flush\n", dm_device_name(md));

	/*
	 * This gets reverted if there's an error later and the targets
	 * provide the .presuspend_undo hook.
	 */
	dm_table_presuspend_targets(map);

	/*
	 * Flush I/O to the device.
	 * Any I/O submitted after lock_fs() may not be flushed.
	 * noflush takes precedence over do_lockfs.
	 * (lock_fs() flushes I/Os and waits for them to complete.)
	 */
	if (!noflush && do_lockfs) {
		r = lock_fs(md);
		if (r) {
			dm_table_presuspend_undo_targets(map);
			return r;
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
	 * __split_and_process_bio from dm_request and quiesce the thread
	 * (dm_wq_work), we set BMF_BLOCK_IO_FOR_SUSPEND and call
	 * flush_workqueue(md->wq).
	 */
	set_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	if (map)
		synchronize_srcu(&md->io_barrier);

	/*
	 * Stop md->queue before flushing md->wq in case request-based
	 * dm defers requests to md->wq from md->queue.
	 */
	if (dm_request_based(md)) {
		dm_stop_queue(md->queue);
		if (md->kworker_task)
			kthread_flush_worker(&md->kworker);
	}

	flush_workqueue(md->wq);

	/*
	 * At this point no more requests are entering target request routines.
	 * We call dm_wait_for_completion to wait for all existing requests
	 * to finish.
	 */
	r = dm_wait_for_completion(md, task_state);
	if (!r)
		set_bit(dmf_suspended_flag, &md->flags);

	if (noflush)
		clear_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);
	if (map)
		synchronize_srcu(&md->io_barrier);

	/* were we interrupted ? */
	if (r < 0) {
		dm_queue_flush(md);

		if (dm_request_based(md))
			dm_start_queue(md->queue);

		unlock_fs(md);
		dm_table_presuspend_undo_targets(map);
		/* pushback list is already flushed, so skip flush */
	}

	return r;
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

retry:
	mutex_lock_nested(&md->suspend_lock, SINGLE_DEPTH_NESTING);

	if (dm_suspended_md(md)) {
		r = -EINVAL;
		goto out_unlock;
	}

	if (dm_suspended_internally_md(md)) {
		/* already internally suspended, wait for internal resume */
		mutex_unlock(&md->suspend_lock);
		r = wait_on_bit(&md->flags, DMF_SUSPENDED_INTERNALLY, TASK_INTERRUPTIBLE);
		if (r)
			return r;
		goto retry;
	}

	map = rcu_dereference_protected(md->map, lockdep_is_held(&md->suspend_lock));

	r = __dm_suspend(md, map, suspend_flags, TASK_INTERRUPTIBLE, DMF_SUSPENDED);
	if (r)
		goto out_unlock;

	dm_table_postsuspend_targets(map);

out_unlock:
	mutex_unlock(&md->suspend_lock);
	return r;
}

static int __dm_resume(struct mapped_device *md, struct dm_table *map)
{
	if (map) {
		int r = dm_table_resume_targets(map);
		if (r)
			return r;
	}

	dm_queue_flush(md);

	/*
	 * Flushing deferred I/Os must be done after targets are resumed
	 * so that mapping of targets can work correctly.
	 * Request-based dm is queueing the deferred I/Os in its request_queue.
	 */
	if (dm_request_based(md))
		dm_start_queue(md->queue);

	unlock_fs(md);

	return 0;
}

int dm_resume(struct mapped_device *md)
{
	int r;
	struct dm_table *map = NULL;

retry:
	r = -EINVAL;
	mutex_lock_nested(&md->suspend_lock, SINGLE_DEPTH_NESTING);

	if (!dm_suspended_md(md))
		goto out;

	if (dm_suspended_internally_md(md)) {
		/* already internally suspended, wait for internal resume */
		mutex_unlock(&md->suspend_lock);
		r = wait_on_bit(&md->flags, DMF_SUSPENDED_INTERNALLY, TASK_INTERRUPTIBLE);
		if (r)
			return r;
		goto retry;
	}

	map = rcu_dereference_protected(md->map, lockdep_is_held(&md->suspend_lock));
	if (!map || !dm_table_get_size(map))
		goto out;

	r = __dm_resume(md, map);
	if (r)
		goto out;

	clear_bit(DMF_SUSPENDED, &md->flags);
out:
	mutex_unlock(&md->suspend_lock);

	return r;
}

/*
 * Internal suspend/resume works like userspace-driven suspend. It waits
 * until all bios finish and prevents issuing new bios to the target drivers.
 * It may be used only from the kernel.
 */

static void __dm_internal_suspend(struct mapped_device *md, unsigned suspend_flags)
{
	struct dm_table *map = NULL;

	lockdep_assert_held(&md->suspend_lock);

	if (md->internal_suspend_count++)
		return; /* nested internal suspend */

	if (dm_suspended_md(md)) {
		set_bit(DMF_SUSPENDED_INTERNALLY, &md->flags);
		return; /* nest suspend */
	}

	map = rcu_dereference_protected(md->map, lockdep_is_held(&md->suspend_lock));

	/*
	 * Using TASK_UNINTERRUPTIBLE because only NOFLUSH internal suspend is
	 * supported.  Properly supporting a TASK_INTERRUPTIBLE internal suspend
	 * would require changing .presuspend to return an error -- avoid this
	 * until there is a need for more elaborate variants of internal suspend.
	 */
	(void) __dm_suspend(md, map, suspend_flags, TASK_UNINTERRUPTIBLE,
			    DMF_SUSPENDED_INTERNALLY);

	dm_table_postsuspend_targets(map);
}

static void __dm_internal_resume(struct mapped_device *md)
{
	BUG_ON(!md->internal_suspend_count);

	if (--md->internal_suspend_count)
		return; /* resume from nested internal suspend */

	if (dm_suspended_md(md))
		goto done; /* resume from nested suspend */

	/*
	 * NOTE: existing callers don't need to call dm_table_resume_targets
	 * (which may fail -- so best to avoid it for now by passing NULL map)
	 */
	(void) __dm_resume(md, NULL);

done:
	clear_bit(DMF_SUSPENDED_INTERNALLY, &md->flags);
	smp_mb__after_atomic();
	wake_up_bit(&md->flags, DMF_SUSPENDED_INTERNALLY);
}

void dm_internal_suspend_noflush(struct mapped_device *md)
{
	mutex_lock(&md->suspend_lock);
	__dm_internal_suspend(md, DM_SUSPEND_NOFLUSH_FLAG);
	mutex_unlock(&md->suspend_lock);
}
EXPORT_SYMBOL_GPL(dm_internal_suspend_noflush);

void dm_internal_resume(struct mapped_device *md)
{
	mutex_lock(&md->suspend_lock);
	__dm_internal_resume(md);
	mutex_unlock(&md->suspend_lock);
}
EXPORT_SYMBOL_GPL(dm_internal_resume);

/*
 * Fast variants of internal suspend/resume hold md->suspend_lock,
 * which prevents interaction with userspace-driven suspend.
 */

void dm_internal_suspend_fast(struct mapped_device *md)
{
	mutex_lock(&md->suspend_lock);
	if (dm_suspended_md(md) || dm_suspended_internally_md(md))
		return;

	set_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	synchronize_srcu(&md->io_barrier);
	flush_workqueue(md->wq);
	dm_wait_for_completion(md, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL_GPL(dm_internal_suspend_fast);

void dm_internal_resume_fast(struct mapped_device *md)
{
	if (dm_suspended_md(md) || dm_suspended_internally_md(md))
		goto done;

	dm_queue_flush(md);

done:
	mutex_unlock(&md->suspend_lock);
}
EXPORT_SYMBOL_GPL(dm_internal_resume_fast);

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
EXPORT_SYMBOL_GPL(dm_disk);

struct kobject *dm_kobject(struct mapped_device *md)
{
	return &md->kobj_holder.kobj;
}

struct mapped_device *dm_get_from_kobject(struct kobject *kobj)
{
	struct mapped_device *md;

	md = container_of(kobj, struct mapped_device, kobj_holder.kobj);

	spin_lock(&_minor_lock);
	if (test_bit(DMF_FREEING, &md->flags) || dm_deleting_md(md)) {
		md = NULL;
		goto out;
	}
	dm_get(md);
out:
	spin_unlock(&_minor_lock);

	return md;
}

int dm_suspended_md(struct mapped_device *md)
{
	return test_bit(DMF_SUSPENDED, &md->flags);
}

int dm_suspended_internally_md(struct mapped_device *md)
{
	return test_bit(DMF_SUSPENDED_INTERNALLY, &md->flags);
}

int dm_test_deferred_remove_flag(struct mapped_device *md)
{
	return test_bit(DMF_DEFERRED_REMOVE, &md->flags);
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

struct dm_md_mempools *dm_alloc_md_mempools(struct mapped_device *md, enum dm_queue_mode type,
					    unsigned integrity, unsigned per_io_data_size)
{
	struct dm_md_mempools *pools = kzalloc_node(sizeof(*pools), GFP_KERNEL, md->numa_node_id);
	unsigned int pool_size = 0;
	unsigned int front_pad;

	if (!pools)
		return NULL;

	switch (type) {
	case DM_TYPE_BIO_BASED:
	case DM_TYPE_DAX_BIO_BASED:
		pool_size = dm_get_reserved_bio_based_ios();
		front_pad = roundup(per_io_data_size, __alignof__(struct dm_target_io)) + offsetof(struct dm_target_io, clone);
	
		pools->io_pool = mempool_create_slab_pool(pool_size, _io_cache);
		if (!pools->io_pool)
			goto out;
		break;
	case DM_TYPE_REQUEST_BASED:
	case DM_TYPE_MQ_REQUEST_BASED:
		pool_size = dm_get_reserved_rq_based_ios();
		front_pad = offsetof(struct dm_rq_clone_bio_info, clone);
		/* per_io_data_size is used for blk-mq pdu at queue allocation */
		break;
	default:
		BUG();
	}

	pools->bs = bioset_create(pool_size, front_pad, BIOSET_NEED_RESCUER);
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

	mempool_destroy(pools->io_pool);

	if (pools->bs)
		bioset_free(pools->bs);

	kfree(pools);
}

struct dm_pr {
	u64	old_key;
	u64	new_key;
	u32	flags;
	bool	fail_early;
};

static int dm_call_pr(struct block_device *bdev, iterate_devices_callout_fn fn,
		      void *data)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	struct dm_table *table;
	struct dm_target *ti;
	int ret = -ENOTTY, srcu_idx;

	table = dm_get_live_table(md, &srcu_idx);
	if (!table || !dm_table_get_size(table))
		goto out;

	/* We only support devices that have a single target */
	if (dm_table_get_num_targets(table) != 1)
		goto out;
	ti = dm_table_get_target(table, 0);

	ret = -EINVAL;
	if (!ti->type->iterate_devices)
		goto out;

	ret = ti->type->iterate_devices(ti, fn, data);
out:
	dm_put_live_table(md, srcu_idx);
	return ret;
}

/*
 * For register / unregister we need to manually call out to every path.
 */
static int __dm_pr_register(struct dm_target *ti, struct dm_dev *dev,
			    sector_t start, sector_t len, void *data)
{
	struct dm_pr *pr = data;
	const struct pr_ops *ops = dev->bdev->bd_disk->fops->pr_ops;

	if (!ops || !ops->pr_register)
		return -EOPNOTSUPP;
	return ops->pr_register(dev->bdev, pr->old_key, pr->new_key, pr->flags);
}

static int dm_pr_register(struct block_device *bdev, u64 old_key, u64 new_key,
			  u32 flags)
{
	struct dm_pr pr = {
		.old_key	= old_key,
		.new_key	= new_key,
		.flags		= flags,
		.fail_early	= true,
	};
	int ret;

	ret = dm_call_pr(bdev, __dm_pr_register, &pr);
	if (ret && new_key) {
		/* unregister all paths if we failed to register any path */
		pr.old_key = new_key;
		pr.new_key = 0;
		pr.flags = 0;
		pr.fail_early = false;
		dm_call_pr(bdev, __dm_pr_register, &pr);
	}

	return ret;
}

static int dm_pr_reserve(struct block_device *bdev, u64 key, enum pr_type type,
			 u32 flags)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	const struct pr_ops *ops;
	fmode_t mode;
	int r;

	r = dm_grab_bdev_for_ioctl(md, &bdev, &mode);
	if (r < 0)
		return r;

	ops = bdev->bd_disk->fops->pr_ops;
	if (ops && ops->pr_reserve)
		r = ops->pr_reserve(bdev, key, type, flags);
	else
		r = -EOPNOTSUPP;

	bdput(bdev);
	return r;
}

static int dm_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	const struct pr_ops *ops;
	fmode_t mode;
	int r;

	r = dm_grab_bdev_for_ioctl(md, &bdev, &mode);
	if (r < 0)
		return r;

	ops = bdev->bd_disk->fops->pr_ops;
	if (ops && ops->pr_release)
		r = ops->pr_release(bdev, key, type);
	else
		r = -EOPNOTSUPP;

	bdput(bdev);
	return r;
}

static int dm_pr_preempt(struct block_device *bdev, u64 old_key, u64 new_key,
			 enum pr_type type, bool abort)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	const struct pr_ops *ops;
	fmode_t mode;
	int r;

	r = dm_grab_bdev_for_ioctl(md, &bdev, &mode);
	if (r < 0)
		return r;

	ops = bdev->bd_disk->fops->pr_ops;
	if (ops && ops->pr_preempt)
		r = ops->pr_preempt(bdev, old_key, new_key, type, abort);
	else
		r = -EOPNOTSUPP;

	bdput(bdev);
	return r;
}

static int dm_pr_clear(struct block_device *bdev, u64 key)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	const struct pr_ops *ops;
	fmode_t mode;
	int r;

	r = dm_grab_bdev_for_ioctl(md, &bdev, &mode);
	if (r < 0)
		return r;

	ops = bdev->bd_disk->fops->pr_ops;
	if (ops && ops->pr_clear)
		r = ops->pr_clear(bdev, key);
	else
		r = -EOPNOTSUPP;

	bdput(bdev);
	return r;
}

static const struct pr_ops dm_pr_ops = {
	.pr_register	= dm_pr_register,
	.pr_reserve	= dm_pr_reserve,
	.pr_release	= dm_pr_release,
	.pr_preempt	= dm_pr_preempt,
	.pr_clear	= dm_pr_clear,
};

static const struct block_device_operations dm_blk_dops = {
	.open = dm_blk_open,
	.release = dm_blk_close,
	.ioctl = dm_blk_ioctl,
	.getgeo = dm_blk_getgeo,
	.pr_ops = &dm_pr_ops,
	.owner = THIS_MODULE
};

static const struct dax_operations dm_dax_ops = {
	.direct_access = dm_dax_direct_access,
	.copy_from_iter = dm_dax_copy_from_iter,
};

/*
 * module hooks
 */
module_init(dm_init);
module_exit(dm_exit);

module_param(major, uint, 0);
MODULE_PARM_DESC(major, "The major number of the device mapper");

module_param(reserved_bio_based_ios, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(reserved_bio_based_ios, "Reserved IOs in bio-based mempools");

module_param(dm_numa_node, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dm_numa_node, "NUMA node for DM device memory allocations");

MODULE_DESCRIPTION(DM_NAME " driver");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
