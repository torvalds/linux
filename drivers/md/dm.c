/*
 * Copyright (C) 2001, 2002 Sistina Software (UK) Limited.
 * Copyright (C) 2004-2008 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include "dm-core.h"
#include "dm-rq.h"
#include "dm-uevent.h"
#include "dm-ima.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sched/mm.h>
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
#include <linux/part_stat.h>
#include <linux/blk-crypto.h>
#include <linux/blk-crypto-profile.h>

#define DM_MSG_PREFIX "core"

/*
 * Cookies are numeric values sent with CHANGE and REMOVE
 * uevents while resuming, removing or renaming the device.
 */
#define DM_COOKIE_ENV_VAR_NAME "DM_COOKIE"
#define DM_COOKIE_LENGTH 24

/*
 * For REQ_POLLED fs bio, this flag is set if we link mapped underlying
 * dm_io into one list, and reuse bio->bi_private as the list head. Before
 * ending this fs bio, we will recover its ->bi_private.
 */
#define REQ_DM_POLL_LIST	REQ_DRV

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

DEFINE_STATIC_KEY_FALSE(stats_enabled);
DEFINE_STATIC_KEY_FALSE(swap_bios_enabled);
DEFINE_STATIC_KEY_FALSE(zoned_enabled);

/*
 * One of these is allocated (on-stack) per original bio.
 */
struct clone_info {
	struct dm_table *map;
	struct bio *bio;
	struct dm_io *io;
	sector_t sector;
	unsigned sector_count;
	bool is_abnormal_io:1;
	bool submit_as_polled:1;
};

static inline struct dm_target_io *clone_to_tio(struct bio *clone)
{
	return container_of(clone, struct dm_target_io, clone);
}

void *dm_per_bio_data(struct bio *bio, size_t data_size)
{
	if (!dm_tio_flagged(clone_to_tio(bio), DM_TIO_INSIDE_DM_IO))
		return (char *)bio - DM_TARGET_IO_BIO_OFFSET - data_size;
	return (char *)bio - DM_IO_BIO_OFFSET - data_size;
}
EXPORT_SYMBOL_GPL(dm_per_bio_data);

struct bio *dm_bio_from_per_bio_data(void *data, size_t data_size)
{
	struct dm_io *io = (struct dm_io *)((char *)data + data_size);
	if (io->magic == DM_IO_MAGIC)
		return (struct bio *)((char *)io + DM_IO_BIO_OFFSET);
	BUG_ON(io->magic != DM_TIO_MAGIC);
	return (struct bio *)((char *)io + DM_TARGET_IO_BIO_OFFSET);
}
EXPORT_SYMBOL_GPL(dm_bio_from_per_bio_data);

unsigned dm_bio_get_target_bio_nr(const struct bio *bio)
{
	return container_of(bio, struct dm_target_io, clone)->target_bio_nr;
}
EXPORT_SYMBOL_GPL(dm_bio_get_target_bio_nr);

#define MINOR_ALLOCED ((void *)-1)

#define DM_NUMA_NODE NUMA_NO_NODE
static int dm_numa_node = DM_NUMA_NODE;

#define DEFAULT_SWAP_BIOS	(8 * 1048576 / PAGE_SIZE)
static int swap_bios = DEFAULT_SWAP_BIOS;
static int get_swap_bios(void)
{
	int latch = READ_ONCE(swap_bios);
	if (unlikely(latch <= 0))
		latch = DEFAULT_SWAP_BIOS;
	return latch;
}

struct table_device {
	struct list_head list;
	refcount_t count;
	struct dm_dev dm_dev;
};

/*
 * Bio-based DM's mempools' reserved IOs set by the user.
 */
#define RESERVED_BIO_BASED_IOS		16
static unsigned reserved_bio_based_ios = RESERVED_BIO_BASED_IOS;

static int __dm_get_module_param_int(int *module_param, int min, int max)
{
	int param = READ_ONCE(*module_param);
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
	unsigned param = READ_ONCE(*module_param);
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
	int r;

	r = dm_uevent_init();
	if (r)
		return r;

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

	return r;
}

static void local_exit(void)
{
	flush_scheduled_work();
	destroy_workqueue(deferred_remove_workqueue);

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

#if (IS_ENABLED(CONFIG_IMA) && !IS_ENABLED(CONFIG_IMA_DISABLE_HTABLE))
	DMWARN("CONFIG_IMA_DISABLE_HTABLE is disabled."
	       " Duplicate IMA measurements will not be recorded in the IMA log.");
#endif

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

static int dm_blk_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mapped_device *md = bdev->bd_disk->private_data;

	return dm_get_geometry(md, geo);
}

static int dm_prepare_ioctl(struct mapped_device *md, int *srcu_idx,
			    struct block_device **bdev)
{
	struct dm_target *ti;
	struct dm_table *map;
	int r;

retry:
	r = -ENOTTY;
	map = dm_get_live_table(md, srcu_idx);
	if (!map || !dm_table_get_size(map))
		return r;

	/* We only support devices that have a single target */
	if (map->num_targets != 1)
		return r;

	ti = dm_table_get_target(map, 0);
	if (!ti->type->prepare_ioctl)
		return r;

	if (dm_suspended_md(md))
		return -EAGAIN;

	r = ti->type->prepare_ioctl(ti, bdev);
	if (r == -ENOTCONN && !fatal_signal_pending(current)) {
		dm_put_live_table(md, *srcu_idx);
		msleep(10);
		goto retry;
	}

	return r;
}

static void dm_unprepare_ioctl(struct mapped_device *md, int srcu_idx)
{
	dm_put_live_table(md, srcu_idx);
}

static int dm_blk_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	int r, srcu_idx;

	r = dm_prepare_ioctl(md, &srcu_idx, &bdev);
	if (r < 0)
		goto out;

	if (r > 0) {
		/*
		 * Target determined this ioctl is being issued against a
		 * subset of the parent bdev; require extra privileges.
		 */
		if (!capable(CAP_SYS_RAWIO)) {
			DMDEBUG_LIMIT(
	"%s: sending ioctl %x to DM device without required privilege.",
				current->comm, cmd);
			r = -ENOIOCTLCMD;
			goto out;
		}
	}

	if (!bdev->bd_disk->fops->ioctl)
		r = -ENOTTY;
	else
		r = bdev->bd_disk->fops->ioctl(bdev, mode, cmd, arg);
out:
	dm_unprepare_ioctl(md, srcu_idx);
	return r;
}

u64 dm_start_time_ns_from_clone(struct bio *bio)
{
	return jiffies_to_nsecs(clone_to_tio(bio)->io->start_time);
}
EXPORT_SYMBOL_GPL(dm_start_time_ns_from_clone);

static bool bio_is_flush_with_data(struct bio *bio)
{
	return ((bio->bi_opf & REQ_PREFLUSH) && bio->bi_iter.bi_size);
}

static void dm_io_acct(struct dm_io *io, bool end)
{
	struct dm_stats_aux *stats_aux = &io->stats_aux;
	unsigned long start_time = io->start_time;
	struct mapped_device *md = io->md;
	struct bio *bio = io->orig_bio;
	unsigned int sectors;

	/*
	 * If REQ_PREFLUSH set, don't account payload, it will be
	 * submitted (and accounted) after this flush completes.
	 */
	if (bio_is_flush_with_data(bio))
		sectors = 0;
	else if (likely(!(dm_io_flagged(io, DM_IO_WAS_SPLIT))))
		sectors = bio_sectors(bio);
	else
		sectors = io->sectors;

	if (!end)
		bdev_start_io_acct(bio->bi_bdev, sectors, bio_op(bio),
				   start_time);
	else
		bdev_end_io_acct(bio->bi_bdev, bio_op(bio), start_time);

	if (static_branch_unlikely(&stats_enabled) &&
	    unlikely(dm_stats_used(&md->stats))) {
		sector_t sector;

		if (likely(!dm_io_flagged(io, DM_IO_WAS_SPLIT)))
			sector = bio->bi_iter.bi_sector;
		else
			sector = bio_end_sector(bio) - io->sector_offset;

		dm_stats_account_io(&md->stats, bio_data_dir(bio),
				    sector, sectors,
				    end, start_time, stats_aux);
	}
}

static void __dm_start_io_acct(struct dm_io *io)
{
	dm_io_acct(io, false);
}

static void dm_start_io_acct(struct dm_io *io, struct bio *clone)
{
	/*
	 * Ensure IO accounting is only ever started once.
	 */
	if (dm_io_flagged(io, DM_IO_ACCOUNTED))
		return;

	/* Expect no possibility for race unless DM_TIO_IS_DUPLICATE_BIO. */
	if (!clone || likely(dm_tio_is_normal(clone_to_tio(clone)))) {
		dm_io_set_flag(io, DM_IO_ACCOUNTED);
	} else {
		unsigned long flags;
		/* Can afford locking given DM_TIO_IS_DUPLICATE_BIO */
		spin_lock_irqsave(&io->lock, flags);
		if (dm_io_flagged(io, DM_IO_ACCOUNTED)) {
			spin_unlock_irqrestore(&io->lock, flags);
			return;
		}
		dm_io_set_flag(io, DM_IO_ACCOUNTED);
		spin_unlock_irqrestore(&io->lock, flags);
	}

	__dm_start_io_acct(io);
}

static void dm_end_io_acct(struct dm_io *io)
{
	dm_io_acct(io, true);
}

static struct dm_io *alloc_io(struct mapped_device *md, struct bio *bio)
{
	struct dm_io *io;
	struct dm_target_io *tio;
	struct bio *clone;

	clone = bio_alloc_clone(NULL, bio, GFP_NOIO, &md->mempools->io_bs);
	tio = clone_to_tio(clone);
	tio->flags = 0;
	dm_tio_set_flag(tio, DM_TIO_INSIDE_DM_IO);
	tio->io = NULL;

	io = container_of(tio, struct dm_io, tio);
	io->magic = DM_IO_MAGIC;
	io->status = BLK_STS_OK;

	/* one ref is for submission, the other is for completion */
	atomic_set(&io->io_count, 2);
	this_cpu_inc(*md->pending_io);
	io->orig_bio = bio;
	io->md = md;
	spin_lock_init(&io->lock);
	io->start_time = jiffies;
	io->flags = 0;

	if (static_branch_unlikely(&stats_enabled))
		dm_stats_record_start(&md->stats, &io->stats_aux);

	return io;
}

static void free_io(struct dm_io *io)
{
	bio_put(&io->tio.clone);
}

static struct bio *alloc_tio(struct clone_info *ci, struct dm_target *ti,
			     unsigned target_bio_nr, unsigned *len, gfp_t gfp_mask)
{
	struct mapped_device *md = ci->io->md;
	struct dm_target_io *tio;
	struct bio *clone;

	if (!ci->io->tio.io) {
		/* the dm_target_io embedded in ci->io is available */
		tio = &ci->io->tio;
		/* alloc_io() already initialized embedded clone */
		clone = &tio->clone;
	} else {
		clone = bio_alloc_clone(NULL, ci->bio, gfp_mask,
					&md->mempools->bs);
		if (!clone)
			return NULL;

		/* REQ_DM_POLL_LIST shouldn't be inherited */
		clone->bi_opf &= ~REQ_DM_POLL_LIST;

		tio = clone_to_tio(clone);
		tio->flags = 0; /* also clears DM_TIO_INSIDE_DM_IO */
	}

	tio->magic = DM_TIO_MAGIC;
	tio->io = ci->io;
	tio->ti = ti;
	tio->target_bio_nr = target_bio_nr;
	tio->len_ptr = len;
	tio->old_sector = 0;

	/* Set default bdev, but target must bio_set_dev() before issuing IO */
	clone->bi_bdev = md->disk->part0;
	if (unlikely(ti->needs_bio_set_dev))
		bio_set_dev(clone, md->disk->part0);

	if (len) {
		clone->bi_iter.bi_size = to_bytes(*len);
		if (bio_integrity(clone))
			bio_integrity_trim(clone);
	}

	return clone;
}

static void free_tio(struct bio *clone)
{
	if (dm_tio_flagged(clone_to_tio(clone), DM_TIO_INSIDE_DM_IO))
		return;
	bio_put(clone);
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
struct dm_table *dm_get_live_table(struct mapped_device *md,
				   int *srcu_idx) __acquires(md->io_barrier)
{
	*srcu_idx = srcu_read_lock(&md->io_barrier);

	return srcu_dereference(md->map, &md->io_barrier);
}

void dm_put_live_table(struct mapped_device *md,
		       int srcu_idx) __releases(md->io_barrier)
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

static inline struct dm_table *dm_get_live_table_bio(struct mapped_device *md,
					int *srcu_idx, blk_opf_t bio_opf)
{
	if (bio_opf & REQ_NOWAIT)
		return dm_get_live_table_fast(md);
	else
		return dm_get_live_table(md, srcu_idx);
}

static inline void dm_put_live_table_bio(struct mapped_device *md, int srcu_idx,
					 blk_opf_t bio_opf)
{
	if (bio_opf & REQ_NOWAIT)
		dm_put_live_table_fast(md);
	else
		dm_put_live_table(md, srcu_idx);
}

static char *_dm_claim_ptr = "I belong to device-mapper";

/*
 * Open a table device so we can use it as a map destination.
 */
static struct table_device *open_table_device(struct mapped_device *md,
		dev_t dev, fmode_t mode)
{
	struct table_device *td;
	struct block_device *bdev;
	u64 part_off;
	int r;

	td = kmalloc_node(sizeof(*td), GFP_KERNEL, md->numa_node_id);
	if (!td)
		return ERR_PTR(-ENOMEM);
	refcount_set(&td->count, 1);

	bdev = blkdev_get_by_dev(dev, mode | FMODE_EXCL, _dm_claim_ptr);
	if (IS_ERR(bdev)) {
		r = PTR_ERR(bdev);
		goto out_free_td;
	}

	/*
	 * We can be called before the dm disk is added.  In that case we can't
	 * register the holder relation here.  It will be done once add_disk was
	 * called.
	 */
	if (md->disk->slave_dir) {
		r = bd_link_disk_holder(bdev, md->disk);
		if (r)
			goto out_blkdev_put;
	}

	td->dm_dev.mode = mode;
	td->dm_dev.bdev = bdev;
	td->dm_dev.dax_dev = fs_dax_get_by_bdev(bdev, &part_off, NULL, NULL);
	format_dev_t(td->dm_dev.name, dev);
	list_add(&td->list, &md->table_devices);
	return td;

out_blkdev_put:
	blkdev_put(bdev, mode | FMODE_EXCL);
out_free_td:
	kfree(td);
	return ERR_PTR(r);
}

/*
 * Close a table device that we've been using.
 */
static void close_table_device(struct table_device *td, struct mapped_device *md)
{
	if (md->disk->slave_dir)
		bd_unlink_disk_holder(td->dm_dev.bdev, md->disk);
	blkdev_put(td->dm_dev.bdev, td->dm_dev.mode | FMODE_EXCL);
	put_dax(td->dm_dev.dax_dev);
	list_del(&td->list);
	kfree(td);
}

static struct table_device *find_table_device(struct list_head *l, dev_t dev,
					      fmode_t mode)
{
	struct table_device *td;

	list_for_each_entry(td, l, list)
		if (td->dm_dev.bdev->bd_dev == dev && td->dm_dev.mode == mode)
			return td;

	return NULL;
}

int dm_get_table_device(struct mapped_device *md, dev_t dev, fmode_t mode,
			struct dm_dev **result)
{
	struct table_device *td;

	mutex_lock(&md->table_devices_lock);
	td = find_table_device(&md->table_devices, dev, mode);
	if (!td) {
		td = open_table_device(md, dev, mode);
		if (IS_ERR(td)) {
			mutex_unlock(&md->table_devices_lock);
			return PTR_ERR(td);
		}
	} else {
		refcount_inc(&td->count);
	}
	mutex_unlock(&md->table_devices_lock);

	*result = &td->dm_dev;
	return 0;
}

void dm_put_table_device(struct mapped_device *md, struct dm_dev *d)
{
	struct table_device *td = container_of(d, struct table_device, dm_dev);

	mutex_lock(&md->table_devices_lock);
	if (refcount_dec_and_test(&td->count))
		close_table_device(td, md);
	mutex_unlock(&md->table_devices_lock);
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
		DMERR("Start sector is beyond the geometry limits.");
		return -EINVAL;
	}

	md->geometry = *geo;

	return 0;
}

static int __noflush_suspending(struct mapped_device *md)
{
	return test_bit(DMF_NOFLUSH_SUSPENDING, &md->flags);
}

static void dm_requeue_add_io(struct dm_io *io, bool first_stage)
{
	struct mapped_device *md = io->md;

	if (first_stage) {
		struct dm_io *next = md->requeue_list;

		md->requeue_list = io;
		io->next = next;
	} else {
		bio_list_add_head(&md->deferred, io->orig_bio);
	}
}

static void dm_kick_requeue(struct mapped_device *md, bool first_stage)
{
	if (first_stage)
		queue_work(md->wq, &md->requeue_work);
	else
		queue_work(md->wq, &md->work);
}

/*
 * Return true if the dm_io's original bio is requeued.
 * io->status is updated with error if requeue disallowed.
 */
static bool dm_handle_requeue(struct dm_io *io, bool first_stage)
{
	struct bio *bio = io->orig_bio;
	bool handle_requeue = (io->status == BLK_STS_DM_REQUEUE);
	bool handle_polled_eagain = ((io->status == BLK_STS_AGAIN) &&
				     (bio->bi_opf & REQ_POLLED));
	struct mapped_device *md = io->md;
	bool requeued = false;

	if (handle_requeue || handle_polled_eagain) {
		unsigned long flags;

		if (bio->bi_opf & REQ_POLLED) {
			/*
			 * Upper layer won't help us poll split bio
			 * (io->orig_bio may only reflect a subset of the
			 * pre-split original) so clear REQ_POLLED.
			 */
			bio_clear_polled(bio);
		}

		/*
		 * Target requested pushing back the I/O or
		 * polled IO hit BLK_STS_AGAIN.
		 */
		spin_lock_irqsave(&md->deferred_lock, flags);
		if ((__noflush_suspending(md) &&
		     !WARN_ON_ONCE(dm_is_zone_write(md, bio))) ||
		    handle_polled_eagain || first_stage) {
			dm_requeue_add_io(io, first_stage);
			requeued = true;
		} else {
			/*
			 * noflush suspend was interrupted or this is
			 * a write to a zoned target.
			 */
			io->status = BLK_STS_IOERR;
		}
		spin_unlock_irqrestore(&md->deferred_lock, flags);
	}

	if (requeued)
		dm_kick_requeue(md, first_stage);

	return requeued;
}

static void __dm_io_complete(struct dm_io *io, bool first_stage)
{
	struct bio *bio = io->orig_bio;
	struct mapped_device *md = io->md;
	blk_status_t io_error;
	bool requeued;

	requeued = dm_handle_requeue(io, first_stage);
	if (requeued && first_stage)
		return;

	io_error = io->status;
	if (dm_io_flagged(io, DM_IO_ACCOUNTED))
		dm_end_io_acct(io);
	else if (!io_error) {
		/*
		 * Must handle target that DM_MAPIO_SUBMITTED only to
		 * then bio_endio() rather than dm_submit_bio_remap()
		 */
		__dm_start_io_acct(io);
		dm_end_io_acct(io);
	}
	free_io(io);
	smp_wmb();
	this_cpu_dec(*md->pending_io);

	/* nudge anyone waiting on suspend queue */
	if (unlikely(wq_has_sleeper(&md->wait)))
		wake_up(&md->wait);

	/* Return early if the original bio was requeued */
	if (requeued)
		return;

	if (bio_is_flush_with_data(bio)) {
		/*
		 * Preflush done for flush with data, reissue
		 * without REQ_PREFLUSH.
		 */
		bio->bi_opf &= ~REQ_PREFLUSH;
		queue_io(md, bio);
	} else {
		/* done with normal IO or empty flush */
		if (io_error)
			bio->bi_status = io_error;
		bio_endio(bio);
	}
}

static void dm_wq_requeue_work(struct work_struct *work)
{
	struct mapped_device *md = container_of(work, struct mapped_device,
						requeue_work);
	unsigned long flags;
	struct dm_io *io;

	/* reuse deferred lock to simplify dm_handle_requeue */
	spin_lock_irqsave(&md->deferred_lock, flags);
	io = md->requeue_list;
	md->requeue_list = NULL;
	spin_unlock_irqrestore(&md->deferred_lock, flags);

	while (io) {
		struct dm_io *next = io->next;

		dm_io_rewind(io, &md->disk->bio_split);

		io->next = NULL;
		__dm_io_complete(io, false);
		io = next;
	}
}

/*
 * Two staged requeue:
 *
 * 1) io->orig_bio points to the real original bio, and the part mapped to
 *    this io must be requeued, instead of other parts of the original bio.
 *
 * 2) io->orig_bio points to new cloned bio which matches the requeued dm_io.
 */
static void dm_io_complete(struct dm_io *io)
{
	bool first_requeue;

	/*
	 * Only dm_io that has been split needs two stage requeue, otherwise
	 * we may run into long bio clone chain during suspend and OOM could
	 * be triggered.
	 *
	 * Also flush data dm_io won't be marked as DM_IO_WAS_SPLIT, so they
	 * also aren't handled via the first stage requeue.
	 */
	if (dm_io_flagged(io, DM_IO_WAS_SPLIT))
		first_requeue = true;
	else
		first_requeue = false;

	__dm_io_complete(io, first_requeue);
}

/*
 * Decrements the number of outstanding ios that a bio has been
 * cloned into, completing the original io if necc.
 */
static inline void __dm_io_dec_pending(struct dm_io *io)
{
	if (atomic_dec_and_test(&io->io_count))
		dm_io_complete(io);
}

static void dm_io_set_error(struct dm_io *io, blk_status_t error)
{
	unsigned long flags;

	/* Push-back supersedes any I/O errors */
	spin_lock_irqsave(&io->lock, flags);
	if (!(io->status == BLK_STS_DM_REQUEUE &&
	      __noflush_suspending(io->md))) {
		io->status = error;
	}
	spin_unlock_irqrestore(&io->lock, flags);
}

static void dm_io_dec_pending(struct dm_io *io, blk_status_t error)
{
	if (unlikely(error))
		dm_io_set_error(io, error);

	__dm_io_dec_pending(io);
}

void disable_discard(struct mapped_device *md)
{
	struct queue_limits *limits = dm_get_queue_limits(md);

	/* device doesn't really support DISCARD, disable it */
	limits->max_discard_sectors = 0;
}

void disable_write_zeroes(struct mapped_device *md)
{
	struct queue_limits *limits = dm_get_queue_limits(md);

	/* device doesn't really support WRITE ZEROES, disable it */
	limits->max_write_zeroes_sectors = 0;
}

static bool swap_bios_limit(struct dm_target *ti, struct bio *bio)
{
	return unlikely((bio->bi_opf & REQ_SWAP) != 0) && unlikely(ti->limit_swap_bios);
}

static void clone_endio(struct bio *bio)
{
	blk_status_t error = bio->bi_status;
	struct dm_target_io *tio = clone_to_tio(bio);
	struct dm_target *ti = tio->ti;
	dm_endio_fn endio = ti->type->end_io;
	struct dm_io *io = tio->io;
	struct mapped_device *md = io->md;

	if (unlikely(error == BLK_STS_TARGET)) {
		if (bio_op(bio) == REQ_OP_DISCARD &&
		    !bdev_max_discard_sectors(bio->bi_bdev))
			disable_discard(md);
		else if (bio_op(bio) == REQ_OP_WRITE_ZEROES &&
			 !bdev_write_zeroes_sectors(bio->bi_bdev))
			disable_write_zeroes(md);
	}

	if (static_branch_unlikely(&zoned_enabled) &&
	    unlikely(bdev_is_zoned(bio->bi_bdev)))
		dm_zone_endio(io, bio);

	if (endio) {
		int r = endio(ti, bio, &error);
		switch (r) {
		case DM_ENDIO_REQUEUE:
			if (static_branch_unlikely(&zoned_enabled)) {
				/*
				 * Requeuing writes to a sequential zone of a zoned
				 * target will break the sequential write pattern:
				 * fail such IO.
				 */
				if (WARN_ON_ONCE(dm_is_zone_write(md, bio)))
					error = BLK_STS_IOERR;
				else
					error = BLK_STS_DM_REQUEUE;
			} else
				error = BLK_STS_DM_REQUEUE;
			fallthrough;
		case DM_ENDIO_DONE:
			break;
		case DM_ENDIO_INCOMPLETE:
			/* The target will handle the io */
			return;
		default:
			DMCRIT("unimplemented target endio return value: %d", r);
			BUG();
		}
	}

	if (static_branch_unlikely(&swap_bios_enabled) &&
	    unlikely(swap_bios_limit(ti, bio)))
		up(&md->swap_bios_semaphore);

	free_tio(bio);
	dm_io_dec_pending(io, error);
}

/*
 * Return maximum size of I/O possible at the supplied sector up to the current
 * target boundary.
 */
static inline sector_t max_io_len_target_boundary(struct dm_target *ti,
						  sector_t target_offset)
{
	return ti->len - target_offset;
}

static sector_t max_io_len(struct dm_target *ti, sector_t sector)
{
	sector_t target_offset = dm_target_offset(ti, sector);
	sector_t len = max_io_len_target_boundary(ti, target_offset);

	/*
	 * Does the target need to split IO even further?
	 * - varied (per target) IO splitting is a tenet of DM; this
	 *   explains why stacked chunk_sectors based splitting via
	 *   bio_split_to_limits() isn't possible here.
	 */
	if (!ti->max_io_len)
		return len;
	return min_t(sector_t, len,
		min(queue_max_sectors(ti->table->md->queue),
		    blk_chunk_sectors_left(target_offset, ti->max_io_len)));
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
	__acquires(md->io_barrier)
{
	struct dm_table *map;
	struct dm_target *ti;

	map = dm_get_live_table(md, srcu_idx);
	if (!map)
		return NULL;

	ti = dm_table_find_target(map, sector);
	if (!ti)
		return NULL;

	return ti;
}

static long dm_dax_direct_access(struct dax_device *dax_dev, pgoff_t pgoff,
		long nr_pages, enum dax_access_mode mode, void **kaddr,
		pfn_t *pfn)
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
	len = max_io_len(ti, sector) / PAGE_SECTORS;
	if (len < 1)
		goto out;
	nr_pages = min(len, nr_pages);
	ret = ti->type->direct_access(ti, pgoff, nr_pages, mode, kaddr, pfn);

 out:
	dm_put_live_table(md, srcu_idx);

	return ret;
}

static int dm_dax_zero_page_range(struct dax_device *dax_dev, pgoff_t pgoff,
				  size_t nr_pages)
{
	struct mapped_device *md = dax_get_private(dax_dev);
	sector_t sector = pgoff * PAGE_SECTORS;
	struct dm_target *ti;
	int ret = -EIO;
	int srcu_idx;

	ti = dm_dax_get_live_target(md, sector, &srcu_idx);

	if (!ti)
		goto out;
	if (WARN_ON(!ti->type->dax_zero_page_range)) {
		/*
		 * ->zero_page_range() is mandatory dax operation. If we are
		 *  here, something is wrong.
		 */
		goto out;
	}
	ret = ti->type->dax_zero_page_range(ti, pgoff, nr_pages);
 out:
	dm_put_live_table(md, srcu_idx);

	return ret;
}

static size_t dm_dax_recovery_write(struct dax_device *dax_dev, pgoff_t pgoff,
		void *addr, size_t bytes, struct iov_iter *i)
{
	struct mapped_device *md = dax_get_private(dax_dev);
	sector_t sector = pgoff * PAGE_SECTORS;
	struct dm_target *ti;
	int srcu_idx;
	long ret = 0;

	ti = dm_dax_get_live_target(md, sector, &srcu_idx);
	if (!ti || !ti->type->dax_recovery_write)
		goto out;

	ret = ti->type->dax_recovery_write(ti, pgoff, addr, bytes, i);
out:
	dm_put_live_table(md, srcu_idx);
	return ret;
}

/*
 * A target may call dm_accept_partial_bio only from the map routine.  It is
 * allowed for all bio types except REQ_PREFLUSH, REQ_OP_ZONE_* zone management
 * operations, REQ_OP_ZONE_APPEND (zone append writes) and any bio serviced by
 * __send_duplicate_bios().
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
 *                      <----- bio_sectors ----->
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
	struct dm_target_io *tio = clone_to_tio(bio);
	struct dm_io *io = tio->io;
	unsigned bio_sectors = bio_sectors(bio);

	BUG_ON(dm_tio_flagged(tio, DM_TIO_IS_DUPLICATE_BIO));
	BUG_ON(op_is_zone_mgmt(bio_op(bio)));
	BUG_ON(bio_op(bio) == REQ_OP_ZONE_APPEND);
	BUG_ON(bio_sectors > *tio->len_ptr);
	BUG_ON(n_sectors > bio_sectors);

	*tio->len_ptr -= bio_sectors - n_sectors;
	bio->bi_iter.bi_size = n_sectors << SECTOR_SHIFT;

	/*
	 * __split_and_process_bio() may have already saved mapped part
	 * for accounting but it is being reduced so update accordingly.
	 */
	dm_io_set_flag(io, DM_IO_WAS_SPLIT);
	io->sectors = n_sectors;
	io->sector_offset = bio_sectors(io->orig_bio);
}
EXPORT_SYMBOL_GPL(dm_accept_partial_bio);

/*
 * @clone: clone bio that DM core passed to target's .map function
 * @tgt_clone: clone of @clone bio that target needs submitted
 *
 * Targets should use this interface to submit bios they take
 * ownership of when returning DM_MAPIO_SUBMITTED.
 *
 * Target should also enable ti->accounts_remapped_io
 */
void dm_submit_bio_remap(struct bio *clone, struct bio *tgt_clone)
{
	struct dm_target_io *tio = clone_to_tio(clone);
	struct dm_io *io = tio->io;

	/* establish bio that will get submitted */
	if (!tgt_clone)
		tgt_clone = clone;

	/*
	 * Account io->origin_bio to DM dev on behalf of target
	 * that took ownership of IO with DM_MAPIO_SUBMITTED.
	 */
	dm_start_io_acct(io, clone);

	trace_block_bio_remap(tgt_clone, disk_devt(io->md->disk),
			      tio->old_sector);
	submit_bio_noacct(tgt_clone);
}
EXPORT_SYMBOL_GPL(dm_submit_bio_remap);

static noinline void __set_swap_bios_limit(struct mapped_device *md, int latch)
{
	mutex_lock(&md->swap_bios_lock);
	while (latch < md->swap_bios) {
		cond_resched();
		down(&md->swap_bios_semaphore);
		md->swap_bios--;
	}
	while (latch > md->swap_bios) {
		cond_resched();
		up(&md->swap_bios_semaphore);
		md->swap_bios++;
	}
	mutex_unlock(&md->swap_bios_lock);
}

static void __map_bio(struct bio *clone)
{
	struct dm_target_io *tio = clone_to_tio(clone);
	struct dm_target *ti = tio->ti;
	struct dm_io *io = tio->io;
	struct mapped_device *md = io->md;
	int r;

	clone->bi_end_io = clone_endio;

	/*
	 * Map the clone.
	 */
	tio->old_sector = clone->bi_iter.bi_sector;

	if (static_branch_unlikely(&swap_bios_enabled) &&
	    unlikely(swap_bios_limit(ti, clone))) {
		int latch = get_swap_bios();
		if (unlikely(latch != md->swap_bios))
			__set_swap_bios_limit(md, latch);
		down(&md->swap_bios_semaphore);
	}

	if (static_branch_unlikely(&zoned_enabled)) {
		/*
		 * Check if the IO needs a special mapping due to zone append
		 * emulation on zoned target. In this case, dm_zone_map_bio()
		 * calls the target map operation.
		 */
		if (unlikely(dm_emulate_zone_append(md)))
			r = dm_zone_map_bio(tio);
		else
			r = ti->type->map(ti, clone);
	} else
		r = ti->type->map(ti, clone);

	switch (r) {
	case DM_MAPIO_SUBMITTED:
		/* target has assumed ownership of this io */
		if (!ti->accounts_remapped_io)
			dm_start_io_acct(io, clone);
		break;
	case DM_MAPIO_REMAPPED:
		dm_submit_bio_remap(clone, NULL);
		break;
	case DM_MAPIO_KILL:
	case DM_MAPIO_REQUEUE:
		if (static_branch_unlikely(&swap_bios_enabled) &&
		    unlikely(swap_bios_limit(ti, clone)))
			up(&md->swap_bios_semaphore);
		free_tio(clone);
		if (r == DM_MAPIO_KILL)
			dm_io_dec_pending(io, BLK_STS_IOERR);
		else
			dm_io_dec_pending(io, BLK_STS_DM_REQUEUE);
		break;
	default:
		DMCRIT("unimplemented target map return value: %d", r);
		BUG();
	}
}

static void setup_split_accounting(struct clone_info *ci, unsigned len)
{
	struct dm_io *io = ci->io;

	if (ci->sector_count > len) {
		/*
		 * Split needed, save the mapped part for accounting.
		 * NOTE: dm_accept_partial_bio() will update accordingly.
		 */
		dm_io_set_flag(io, DM_IO_WAS_SPLIT);
		io->sectors = len;
		io->sector_offset = bio_sectors(ci->bio);
	}
}

static void alloc_multiple_bios(struct bio_list *blist, struct clone_info *ci,
				struct dm_target *ti, unsigned num_bios)
{
	struct bio *bio;
	int try;

	for (try = 0; try < 2; try++) {
		int bio_nr;

		if (try)
			mutex_lock(&ci->io->md->table_devices_lock);
		for (bio_nr = 0; bio_nr < num_bios; bio_nr++) {
			bio = alloc_tio(ci, ti, bio_nr, NULL,
					try ? GFP_NOIO : GFP_NOWAIT);
			if (!bio)
				break;

			bio_list_add(blist, bio);
		}
		if (try)
			mutex_unlock(&ci->io->md->table_devices_lock);
		if (bio_nr == num_bios)
			return;

		while ((bio = bio_list_pop(blist)))
			free_tio(bio);
	}
}

static int __send_duplicate_bios(struct clone_info *ci, struct dm_target *ti,
				 unsigned int num_bios, unsigned *len)
{
	struct bio_list blist = BIO_EMPTY_LIST;
	struct bio *clone;
	unsigned int ret = 0;

	switch (num_bios) {
	case 0:
		break;
	case 1:
		if (len)
			setup_split_accounting(ci, *len);
		clone = alloc_tio(ci, ti, 0, len, GFP_NOIO);
		__map_bio(clone);
		ret = 1;
		break;
	default:
		/* dm_accept_partial_bio() is not supported with shared tio->len_ptr */
		alloc_multiple_bios(&blist, ci, ti, num_bios);
		while ((clone = bio_list_pop(&blist))) {
			dm_tio_set_flag(clone_to_tio(clone), DM_TIO_IS_DUPLICATE_BIO);
			__map_bio(clone);
			ret += 1;
		}
		break;
	}

	return ret;
}

static void __send_empty_flush(struct clone_info *ci)
{
	struct dm_table *t = ci->map;
	struct bio flush_bio;

	/*
	 * Use an on-stack bio for this, it's safe since we don't
	 * need to reference it after submit. It's just used as
	 * the basis for the clone(s).
	 */
	bio_init(&flush_bio, ci->io->md->disk->part0, NULL, 0,
		 REQ_OP_WRITE | REQ_PREFLUSH | REQ_SYNC);

	ci->bio = &flush_bio;
	ci->sector_count = 0;
	ci->io->tio.clone.bi_iter.bi_size = 0;

	for (unsigned int i = 0; i < t->num_targets; i++) {
		unsigned int bios;
		struct dm_target *ti = dm_table_get_target(t, i);

		atomic_add(ti->num_flush_bios, &ci->io->io_count);
		bios = __send_duplicate_bios(ci, ti, ti->num_flush_bios, NULL);
		atomic_sub(ti->num_flush_bios - bios, &ci->io->io_count);
	}

	/*
	 * alloc_io() takes one extra reference for submission, so the
	 * reference won't reach 0 without the following subtraction
	 */
	atomic_sub(1, &ci->io->io_count);

	bio_uninit(ci->bio);
}

static void __send_changing_extent_only(struct clone_info *ci, struct dm_target *ti,
					unsigned num_bios)
{
	unsigned len;
	unsigned int bios;

	len = min_t(sector_t, ci->sector_count,
		    max_io_len_target_boundary(ti, dm_target_offset(ti, ci->sector)));

	atomic_add(num_bios, &ci->io->io_count);
	bios = __send_duplicate_bios(ci, ti, num_bios, &len);
	/*
	 * alloc_io() takes one extra reference for submission, so the
	 * reference won't reach 0 without the following (+1) subtraction
	 */
	atomic_sub(num_bios - bios + 1, &ci->io->io_count);

	ci->sector += len;
	ci->sector_count -= len;
}

static bool is_abnormal_io(struct bio *bio)
{
	enum req_op op = bio_op(bio);

	if (op != REQ_OP_READ && op != REQ_OP_WRITE && op != REQ_OP_FLUSH) {
		switch (op) {
		case REQ_OP_DISCARD:
		case REQ_OP_SECURE_ERASE:
		case REQ_OP_WRITE_ZEROES:
			return true;
		default:
			break;
		}
	}

	return false;
}

static blk_status_t __process_abnormal_io(struct clone_info *ci,
					  struct dm_target *ti)
{
	unsigned num_bios = 0;

	switch (bio_op(ci->bio)) {
	case REQ_OP_DISCARD:
		num_bios = ti->num_discard_bios;
		break;
	case REQ_OP_SECURE_ERASE:
		num_bios = ti->num_secure_erase_bios;
		break;
	case REQ_OP_WRITE_ZEROES:
		num_bios = ti->num_write_zeroes_bios;
		break;
	default:
		break;
	}

	/*
	 * Even though the device advertised support for this type of
	 * request, that does not mean every target supports it, and
	 * reconfiguration might also have changed that since the
	 * check was performed.
	 */
	if (unlikely(!num_bios))
		return BLK_STS_NOTSUPP;

	__send_changing_extent_only(ci, ti, num_bios);
	return BLK_STS_OK;
}

/*
 * Reuse ->bi_private as dm_io list head for storing all dm_io instances
 * associated with this bio, and this bio's bi_private needs to be
 * stored in dm_io->data before the reuse.
 *
 * bio->bi_private is owned by fs or upper layer, so block layer won't
 * touch it after splitting. Meantime it won't be changed by anyone after
 * bio is submitted. So this reuse is safe.
 */
static inline struct dm_io **dm_poll_list_head(struct bio *bio)
{
	return (struct dm_io **)&bio->bi_private;
}

static void dm_queue_poll_io(struct bio *bio, struct dm_io *io)
{
	struct dm_io **head = dm_poll_list_head(bio);

	if (!(bio->bi_opf & REQ_DM_POLL_LIST)) {
		bio->bi_opf |= REQ_DM_POLL_LIST;
		/*
		 * Save .bi_private into dm_io, so that we can reuse
		 * .bi_private as dm_io list head for storing dm_io list
		 */
		io->data = bio->bi_private;

		/* tell block layer to poll for completion */
		bio->bi_cookie = ~BLK_QC_T_NONE;

		io->next = NULL;
	} else {
		/*
		 * bio recursed due to split, reuse original poll list,
		 * and save bio->bi_private too.
		 */
		io->data = (*head)->data;
		io->next = *head;
	}

	*head = io;
}

/*
 * Select the correct strategy for processing a non-flush bio.
 */
static blk_status_t __split_and_process_bio(struct clone_info *ci)
{
	struct bio *clone;
	struct dm_target *ti;
	unsigned len;

	ti = dm_table_find_target(ci->map, ci->sector);
	if (unlikely(!ti))
		return BLK_STS_IOERR;

	if (unlikely((ci->bio->bi_opf & REQ_NOWAIT) != 0) &&
	    unlikely(!dm_target_supports_nowait(ti->type)))
		return BLK_STS_NOTSUPP;

	if (unlikely(ci->is_abnormal_io))
		return __process_abnormal_io(ci, ti);

	/*
	 * Only support bio polling for normal IO, and the target io is
	 * exactly inside the dm_io instance (verified in dm_poll_dm_io)
	 */
	ci->submit_as_polled = !!(ci->bio->bi_opf & REQ_POLLED);

	len = min_t(sector_t, max_io_len(ti, ci->sector), ci->sector_count);
	setup_split_accounting(ci, len);
	clone = alloc_tio(ci, ti, 0, &len, GFP_NOIO);
	__map_bio(clone);

	ci->sector += len;
	ci->sector_count -= len;

	return BLK_STS_OK;
}

static void init_clone_info(struct clone_info *ci, struct mapped_device *md,
			    struct dm_table *map, struct bio *bio, bool is_abnormal)
{
	ci->map = map;
	ci->io = alloc_io(md, bio);
	ci->bio = bio;
	ci->is_abnormal_io = is_abnormal;
	ci->submit_as_polled = false;
	ci->sector = bio->bi_iter.bi_sector;
	ci->sector_count = bio_sectors(bio);

	/* Shouldn't happen but sector_count was being set to 0 so... */
	if (static_branch_unlikely(&zoned_enabled) &&
	    WARN_ON_ONCE(op_is_zone_mgmt(bio_op(bio)) && ci->sector_count))
		ci->sector_count = 0;
}

/*
 * Entry point to split a bio into clones and submit them to the targets.
 */
static void dm_split_and_process_bio(struct mapped_device *md,
				     struct dm_table *map, struct bio *bio)
{
	struct clone_info ci;
	struct dm_io *io;
	blk_status_t error = BLK_STS_OK;
	bool is_abnormal;

	is_abnormal = is_abnormal_io(bio);
	if (unlikely(is_abnormal)) {
		/*
		 * Use bio_split_to_limits() for abnormal IO (e.g. discard, etc)
		 * otherwise associated queue_limits won't be imposed.
		 */
		bio = bio_split_to_limits(bio);
		if (!bio)
			return;
	}

	init_clone_info(&ci, md, map, bio, is_abnormal);
	io = ci.io;

	if (bio->bi_opf & REQ_PREFLUSH) {
		__send_empty_flush(&ci);
		/* dm_io_complete submits any data associated with flush */
		goto out;
	}

	error = __split_and_process_bio(&ci);
	if (error || !ci.sector_count)
		goto out;
	/*
	 * Remainder must be passed to submit_bio_noacct() so it gets handled
	 * *after* bios already submitted have been completely processed.
	 */
	bio_trim(bio, io->sectors, ci.sector_count);
	trace_block_split(bio, bio->bi_iter.bi_sector);
	bio_inc_remaining(bio);
	submit_bio_noacct(bio);
out:
	/*
	 * Drop the extra reference count for non-POLLED bio, and hold one
	 * reference for POLLED bio, which will be released in dm_poll_bio
	 *
	 * Add every dm_io instance into the dm_io list head which is stored
	 * in bio->bi_private, so that dm_poll_bio can poll them all.
	 */
	if (error || !ci.submit_as_polled) {
		/*
		 * In case of submission failure, the extra reference for
		 * submitting io isn't consumed yet
		 */
		if (error)
			atomic_dec(&io->io_count);
		dm_io_dec_pending(io, error);
	} else
		dm_queue_poll_io(bio, io);
}

static void dm_submit_bio(struct bio *bio)
{
	struct mapped_device *md = bio->bi_bdev->bd_disk->private_data;
	int srcu_idx;
	struct dm_table *map;
	blk_opf_t bio_opf = bio->bi_opf;

	map = dm_get_live_table_bio(md, &srcu_idx, bio_opf);

	/* If suspended, or map not yet available, queue this IO for later */
	if (unlikely(test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) ||
	    unlikely(!map)) {
		if (bio->bi_opf & REQ_NOWAIT)
			bio_wouldblock_error(bio);
		else if (bio->bi_opf & REQ_RAHEAD)
			bio_io_error(bio);
		else
			queue_io(md, bio);
		goto out;
	}

	dm_split_and_process_bio(md, map, bio);
out:
	dm_put_live_table_bio(md, srcu_idx, bio_opf);
}

static bool dm_poll_dm_io(struct dm_io *io, struct io_comp_batch *iob,
			  unsigned int flags)
{
	WARN_ON_ONCE(!dm_tio_is_normal(&io->tio));

	/* don't poll if the mapped io is done */
	if (atomic_read(&io->io_count) > 1)
		bio_poll(&io->tio.clone, iob, flags);

	/* bio_poll holds the last reference */
	return atomic_read(&io->io_count) == 1;
}

static int dm_poll_bio(struct bio *bio, struct io_comp_batch *iob,
		       unsigned int flags)
{
	struct dm_io **head = dm_poll_list_head(bio);
	struct dm_io *list = *head;
	struct dm_io *tmp = NULL;
	struct dm_io *curr, *next;

	/* Only poll normal bio which was marked as REQ_DM_POLL_LIST */
	if (!(bio->bi_opf & REQ_DM_POLL_LIST))
		return 0;

	WARN_ON_ONCE(!list);

	/*
	 * Restore .bi_private before possibly completing dm_io.
	 *
	 * bio_poll() is only possible once @bio has been completely
	 * submitted via submit_bio_noacct()'s depth-first submission.
	 * So there is no dm_queue_poll_io() race associated with
	 * clearing REQ_DM_POLL_LIST here.
	 */
	bio->bi_opf &= ~REQ_DM_POLL_LIST;
	bio->bi_private = list->data;

	for (curr = list, next = curr->next; curr; curr = next, next =
			curr ? curr->next : NULL) {
		if (dm_poll_dm_io(curr, iob, flags)) {
			/*
			 * clone_endio() has already occurred, so no
			 * error handling is needed here.
			 */
			__dm_io_dec_pending(curr);
		} else {
			curr->next = tmp;
			tmp = curr;
		}
	}

	/* Not done? */
	if (tmp) {
		bio->bi_opf |= REQ_DM_POLL_LIST;
		/* Reset bio->bi_private to dm_io list head */
		*head = tmp;
		return 0;
	}
	return 1;
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
static const struct block_device_operations dm_rq_blk_dops;
static const struct dax_operations dm_dax_ops;

static void dm_wq_work(struct work_struct *work);

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
static void dm_queue_destroy_crypto_profile(struct request_queue *q)
{
	dm_destroy_crypto_profile(q->crypto_profile);
}

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline void dm_queue_destroy_crypto_profile(struct request_queue *q)
{
}
#endif /* !CONFIG_BLK_INLINE_ENCRYPTION */

static void cleanup_mapped_device(struct mapped_device *md)
{
	if (md->wq)
		destroy_workqueue(md->wq);
	dm_free_md_mempools(md->mempools);

	if (md->dax_dev) {
		dax_remove_host(md->disk);
		kill_dax(md->dax_dev);
		put_dax(md->dax_dev);
		md->dax_dev = NULL;
	}

	dm_cleanup_zoned_dev(md);
	if (md->disk) {
		spin_lock(&_minor_lock);
		md->disk->private_data = NULL;
		spin_unlock(&_minor_lock);
		if (dm_get_md_type(md) != DM_TYPE_NONE) {
			struct table_device *td;

			dm_sysfs_exit(md);
			list_for_each_entry(td, &md->table_devices, list) {
				bd_unlink_disk_holder(td->dm_dev.bdev,
						      md->disk);
			}

			/*
			 * Hold lock to make sure del_gendisk() won't concurrent
			 * with open/close_table_device().
			 */
			mutex_lock(&md->table_devices_lock);
			del_gendisk(md->disk);
			mutex_unlock(&md->table_devices_lock);
		}
		dm_queue_destroy_crypto_profile(md->queue);
		put_disk(md->disk);
	}

	if (md->pending_io) {
		free_percpu(md->pending_io);
		md->pending_io = NULL;
	}

	cleanup_srcu_struct(&md->io_barrier);

	mutex_destroy(&md->suspend_lock);
	mutex_destroy(&md->type_lock);
	mutex_destroy(&md->table_devices_lock);
	mutex_destroy(&md->swap_bios_lock);

	dm_mq_cleanup_mapped_device(md);
}

/*
 * Allocate and initialise a blank device with a given minor.
 */
static struct mapped_device *alloc_dev(int minor)
{
	int r, numa_node_id = dm_get_numa_node();
	struct mapped_device *md;
	void *old_md;

	md = kvzalloc_node(sizeof(*md), GFP_KERNEL, numa_node_id);
	if (!md) {
		DMERR("unable to allocate device, out of memory.");
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

	/*
	 * default to bio-based until DM table is loaded and md->type
	 * established. If request-based table is loaded: blk-mq will
	 * override accordingly.
	 */
	md->disk = blk_alloc_disk(md->numa_node_id);
	if (!md->disk)
		goto bad;
	md->queue = md->disk->queue;

	init_waitqueue_head(&md->wait);
	INIT_WORK(&md->work, dm_wq_work);
	INIT_WORK(&md->requeue_work, dm_wq_requeue_work);
	init_waitqueue_head(&md->eventq);
	init_completion(&md->kobj_holder.completion);

	md->requeue_list = NULL;
	md->swap_bios = get_swap_bios();
	sema_init(&md->swap_bios_semaphore, md->swap_bios);
	mutex_init(&md->swap_bios_lock);

	md->disk->major = _major;
	md->disk->first_minor = minor;
	md->disk->minors = 1;
	md->disk->flags |= GENHD_FL_NO_PART;
	md->disk->fops = &dm_blk_dops;
	md->disk->private_data = md;
	sprintf(md->disk->disk_name, "dm-%d", minor);

	if (IS_ENABLED(CONFIG_FS_DAX)) {
		md->dax_dev = alloc_dax(md, &dm_dax_ops);
		if (IS_ERR(md->dax_dev)) {
			md->dax_dev = NULL;
			goto bad;
		}
		set_dax_nocache(md->dax_dev);
		set_dax_nomc(md->dax_dev);
		if (dax_add_host(md->dax_dev, md->disk))
			goto bad;
	}

	format_dev_t(md->name, MKDEV(_major, minor));

	md->wq = alloc_workqueue("kdmflush/%s", WQ_MEM_RECLAIM, 0, md->name);
	if (!md->wq)
		goto bad;

	md->pending_io = alloc_percpu(unsigned long);
	if (!md->pending_io)
		goto bad;

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

	WARN_ON_ONCE(!list_empty(&md->table_devices));
	dm_stats_cleanup(&md->stats);
	free_minor(minor);

	module_put(THIS_MODULE);
	kvfree(md);
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
 * Returns old map, which caller must destroy.
 */
static struct dm_table *__bind(struct mapped_device *md, struct dm_table *t,
			       struct queue_limits *limits)
{
	struct dm_table *old_map;
	sector_t size;
	int ret;

	lockdep_assert_held(&md->suspend_lock);

	size = dm_table_get_size(t);

	/*
	 * Wipe any geometry if the size of the table changed.
	 */
	if (size != dm_get_size(md))
		memset(&md->geometry, 0, sizeof(md->geometry));

	if (!get_capacity(md->disk))
		set_capacity(md->disk, size);
	else
		set_capacity_and_notify(md->disk, size);

	dm_table_event_callback(t, event_callback, md);

	if (dm_table_request_based(t)) {
		/*
		 * Leverage the fact that request-based DM targets are
		 * immutable singletons - used to optimize dm_mq_queue_rq.
		 */
		md->immutable_target = dm_table_get_immutable_target(t);

		/*
		 * There is no need to reload with request-based dm because the
		 * size of front_pad doesn't change.
		 *
		 * Note for future: If you are to reload bioset, prep-ed
		 * requests in the queue may refer to bio from the old bioset,
		 * so you must walk through the queue to unprep.
		 */
		if (!md->mempools) {
			md->mempools = t->mempools;
			t->mempools = NULL;
		}
	} else {
		/*
		 * The md may already have mempools that need changing.
		 * If so, reload bioset because front_pad may have changed
		 * because a different table was loaded.
		 */
		dm_free_md_mempools(md->mempools);
		md->mempools = t->mempools;
		t->mempools = NULL;
	}

	ret = dm_table_set_restrictions(t, md->queue, limits);
	if (ret) {
		old_map = ERR_PTR(ret);
		goto out;
	}

	old_map = rcu_dereference_protected(md->map, lockdep_is_held(&md->suspend_lock));
	rcu_assign_pointer(md->map, (void *)t);
	md->immutable_target_type = dm_table_get_immutable_target_type(t);

	if (old_map)
		dm_sync_table(md);
out:
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

	dm_ima_reset_data(md);

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
	enum dm_queue_mode type = dm_table_get_type(t);
	struct queue_limits limits;
	struct table_device *td;
	int r;

	switch (type) {
	case DM_TYPE_REQUEST_BASED:
		md->disk->fops = &dm_rq_blk_dops;
		r = dm_mq_init_request_queue(md, t);
		if (r) {
			DMERR("Cannot initialize queue for request-based dm mapped device");
			return r;
		}
		break;
	case DM_TYPE_BIO_BASED:
	case DM_TYPE_DAX_BIO_BASED:
		break;
	case DM_TYPE_NONE:
		WARN_ON_ONCE(true);
		break;
	}

	r = dm_calculate_queue_limits(t, &limits);
	if (r) {
		DMERR("Cannot calculate initial queue limits");
		return r;
	}
	r = dm_table_set_restrictions(t, md->queue, &limits);
	if (r)
		return r;

	/*
	 * Hold lock to make sure add_disk() and del_gendisk() won't concurrent
	 * with open_table_device() and close_table_device().
	 */
	mutex_lock(&md->table_devices_lock);
	r = add_disk(md->disk);
	mutex_unlock(&md->table_devices_lock);
	if (r)
		return r;

	/*
	 * Register the holder relationship for devices added before the disk
	 * was live.
	 */
	list_for_each_entry(td, &md->table_devices, list) {
		r = bd_link_disk_holder(td->dm_dev.bdev, md->disk);
		if (r)
			goto out_undo_holders;
	}

	r = dm_sysfs_init(md);
	if (r)
		goto out_undo_holders;

	md->type = type;
	return 0;

out_undo_holders:
	list_for_each_entry_continue_reverse(td, &md->table_devices, list)
		bd_unlink_disk_holder(td->dm_dev.bdev, md->disk);
	mutex_lock(&md->table_devices_lock);
	del_gendisk(md->disk);
	mutex_unlock(&md->table_devices_lock);
	return r;
}

struct mapped_device *dm_get_md(dev_t dev)
{
	struct mapped_device *md;
	unsigned minor = MINOR(dev);

	if (MAJOR(dev) != _major || minor >= (1 << MINORBITS))
		return NULL;

	spin_lock(&_minor_lock);

	md = idr_find(&_minor_idr, minor);
	if (!md || md == MINOR_ALLOCED || (MINOR(disk_devt(dm_disk(md))) != minor) ||
	    test_bit(DMF_FREEING, &md->flags) || dm_deleting_md(md)) {
		md = NULL;
		goto out;
	}
	dm_get(md);
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
	struct dm_table *map;
	int srcu_idx;

	might_sleep();

	spin_lock(&_minor_lock);
	idr_replace(&_minor_idr, MINOR_ALLOCED, MINOR(disk_devt(dm_disk(md))));
	set_bit(DMF_FREEING, &md->flags);
	spin_unlock(&_minor_lock);

	blk_mark_disk_dead(md->disk);

	/*
	 * Take suspend_lock so that presuspend and postsuspend methods
	 * do not race with internal suspend.
	 */
	mutex_lock(&md->suspend_lock);
	map = dm_get_live_table(md, &srcu_idx);
	if (!dm_suspended_md(md)) {
		dm_table_presuspend_targets(map);
		set_bit(DMF_SUSPENDED, &md->flags);
		set_bit(DMF_POST_SUSPENDING, &md->flags);
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

static bool dm_in_flight_bios(struct mapped_device *md)
{
	int cpu;
	unsigned long sum = 0;

	for_each_possible_cpu(cpu)
		sum += *per_cpu_ptr(md->pending_io, cpu);

	return sum != 0;
}

static int dm_wait_for_bios_completion(struct mapped_device *md, unsigned int task_state)
{
	int r = 0;
	DEFINE_WAIT(wait);

	while (true) {
		prepare_to_wait(&md->wait, &wait, task_state);

		if (!dm_in_flight_bios(md))
			break;

		if (signal_pending_state(task_state, current)) {
			r = -EINTR;
			break;
		}

		io_schedule();
	}
	finish_wait(&md->wait, &wait);

	smp_rmb();

	return r;
}

static int dm_wait_for_completion(struct mapped_device *md, unsigned int task_state)
{
	int r = 0;

	if (!queue_is_mq(md->queue))
		return dm_wait_for_bios_completion(md, task_state);

	while (true) {
		if (!blk_mq_queue_inflight(md->queue))
			break;

		if (signal_pending_state(task_state, current)) {
			r = -EINTR;
			break;
		}

		msleep(5);
	}

	return r;
}

/*
 * Process the deferred bios
 */
static void dm_wq_work(struct work_struct *work)
{
	struct mapped_device *md = container_of(work, struct mapped_device, work);
	struct bio *bio;

	while (!test_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags)) {
		spin_lock_irq(&md->deferred_lock);
		bio = bio_list_pop(&md->deferred);
		spin_unlock_irq(&md->deferred_lock);

		if (!bio)
			break;

		submit_bio_noacct(bio);
	}
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

	WARN_ON(test_bit(DMF_FROZEN, &md->flags));

	r = freeze_bdev(md->disk->part0);
	if (!r)
		set_bit(DMF_FROZEN, &md->flags);
	return r;
}

static void unlock_fs(struct mapped_device *md)
{
	if (!test_bit(DMF_FROZEN, &md->flags))
		return;
	thaw_bdev(md->disk->part0);
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
			unsigned suspend_flags, unsigned int task_state,
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
		DMDEBUG("%s: suspending with flush", dm_device_name(md));

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
	 * dm_split_and_process_bio from dm_submit_bio.
	 *
	 * To get all processes out of dm_split_and_process_bio in dm_submit_bio,
	 * we take the write lock. To prevent any process from reentering
	 * dm_split_and_process_bio from dm_submit_bio and quiesce the thread
	 * (dm_wq_work), we set DMF_BLOCK_IO_FOR_SUSPEND and call
	 * flush_workqueue(md->wq).
	 */
	set_bit(DMF_BLOCK_IO_FOR_SUSPEND, &md->flags);
	if (map)
		synchronize_srcu(&md->io_barrier);

	/*
	 * Stop md->queue before flushing md->wq in case request-based
	 * dm defers requests to md->wq from md->queue.
	 */
	if (dm_request_based(md))
		dm_stop_queue(md->queue);

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

	set_bit(DMF_POST_SUSPENDING, &md->flags);
	dm_table_postsuspend_targets(map);
	clear_bit(DMF_POST_SUSPENDING, &md->flags);

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

	set_bit(DMF_POST_SUSPENDING, &md->flags);
	dm_table_postsuspend_targets(map);
	clear_bit(DMF_POST_SUSPENDING, &md->flags);
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
	int r;
	unsigned noio_flag;
	char udev_cookie[DM_COOKIE_LENGTH];
	char *envp[] = { udev_cookie, NULL };

	noio_flag = memalloc_noio_save();

	if (!cookie)
		r = kobject_uevent(&disk_to_dev(md->disk)->kobj, action);
	else {
		snprintf(udev_cookie, DM_COOKIE_LENGTH, "%s=%u",
			 DM_COOKIE_ENV_VAR_NAME, cookie);
		r = kobject_uevent_env(&disk_to_dev(md->disk)->kobj,
				       action, envp);
	}

	memalloc_noio_restore(noio_flag);

	return r;
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

static int dm_post_suspending_md(struct mapped_device *md)
{
	return test_bit(DMF_POST_SUSPENDING, &md->flags);
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
	return dm_suspended_md(ti->table->md);
}
EXPORT_SYMBOL_GPL(dm_suspended);

int dm_post_suspending(struct dm_target *ti)
{
	return dm_post_suspending_md(ti->table->md);
}
EXPORT_SYMBOL_GPL(dm_post_suspending);

int dm_noflush_suspending(struct dm_target *ti)
{
	return __noflush_suspending(ti->table->md);
}
EXPORT_SYMBOL_GPL(dm_noflush_suspending);

void dm_free_md_mempools(struct dm_md_mempools *pools)
{
	if (!pools)
		return;

	bioset_exit(&pools->bs);
	bioset_exit(&pools->io_bs);

	kfree(pools);
}

struct dm_pr {
	u64	old_key;
	u64	new_key;
	u32	flags;
	bool	abort;
	bool	fail_early;
	int	ret;
	enum pr_type type;
};

static int dm_call_pr(struct block_device *bdev, iterate_devices_callout_fn fn,
		      struct dm_pr *pr)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	struct dm_table *table;
	struct dm_target *ti;
	int ret = -ENOTTY, srcu_idx;

	table = dm_get_live_table(md, &srcu_idx);
	if (!table || !dm_table_get_size(table))
		goto out;

	/* We only support devices that have a single target */
	if (table->num_targets != 1)
		goto out;
	ti = dm_table_get_target(table, 0);

	if (dm_suspended_md(md)) {
		ret = -EAGAIN;
		goto out;
	}

	ret = -EINVAL;
	if (!ti->type->iterate_devices)
		goto out;

	ti->type->iterate_devices(ti, fn, pr);
	ret = 0;
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
	int ret;

	if (!ops || !ops->pr_register) {
		pr->ret = -EOPNOTSUPP;
		return -1;
	}

	ret = ops->pr_register(dev->bdev, pr->old_key, pr->new_key, pr->flags);
	if (!ret)
		return 0;

	if (!pr->ret)
		pr->ret = ret;

	if (pr->fail_early)
		return -1;

	return 0;
}

static int dm_pr_register(struct block_device *bdev, u64 old_key, u64 new_key,
			  u32 flags)
{
	struct dm_pr pr = {
		.old_key	= old_key,
		.new_key	= new_key,
		.flags		= flags,
		.fail_early	= true,
		.ret		= 0,
	};
	int ret;

	ret = dm_call_pr(bdev, __dm_pr_register, &pr);
	if (ret) {
		/* Didn't even get to register a path */
		return ret;
	}

	if (!pr.ret)
		return 0;
	ret = pr.ret;

	if (!new_key)
		return ret;

	/* unregister all paths if we failed to register any path */
	pr.old_key = new_key;
	pr.new_key = 0;
	pr.flags = 0;
	pr.fail_early = false;
	(void) dm_call_pr(bdev, __dm_pr_register, &pr);
	return ret;
}


static int __dm_pr_reserve(struct dm_target *ti, struct dm_dev *dev,
			   sector_t start, sector_t len, void *data)
{
	struct dm_pr *pr = data;
	const struct pr_ops *ops = dev->bdev->bd_disk->fops->pr_ops;

	if (!ops || !ops->pr_reserve) {
		pr->ret = -EOPNOTSUPP;
		return -1;
	}

	pr->ret = ops->pr_reserve(dev->bdev, pr->old_key, pr->type, pr->flags);
	if (!pr->ret)
		return -1;

	return 0;
}

static int dm_pr_reserve(struct block_device *bdev, u64 key, enum pr_type type,
			 u32 flags)
{
	struct dm_pr pr = {
		.old_key	= key,
		.flags		= flags,
		.type		= type,
		.fail_early	= false,
		.ret		= 0,
	};
	int ret;

	ret = dm_call_pr(bdev, __dm_pr_reserve, &pr);
	if (ret)
		return ret;

	return pr.ret;
}

/*
 * If there is a non-All Registrants type of reservation, the release must be
 * sent down the holding path. For the cases where there is no reservation or
 * the path is not the holder the device will also return success, so we must
 * try each path to make sure we got the correct path.
 */
static int __dm_pr_release(struct dm_target *ti, struct dm_dev *dev,
			   sector_t start, sector_t len, void *data)
{
	struct dm_pr *pr = data;
	const struct pr_ops *ops = dev->bdev->bd_disk->fops->pr_ops;

	if (!ops || !ops->pr_release) {
		pr->ret = -EOPNOTSUPP;
		return -1;
	}

	pr->ret = ops->pr_release(dev->bdev, pr->old_key, pr->type);
	if (pr->ret)
		return -1;

	return 0;
}

static int dm_pr_release(struct block_device *bdev, u64 key, enum pr_type type)
{
	struct dm_pr pr = {
		.old_key	= key,
		.type		= type,
		.fail_early	= false,
	};
	int ret;

	ret = dm_call_pr(bdev, __dm_pr_release, &pr);
	if (ret)
		return ret;

	return pr.ret;
}

static int __dm_pr_preempt(struct dm_target *ti, struct dm_dev *dev,
			   sector_t start, sector_t len, void *data)
{
	struct dm_pr *pr = data;
	const struct pr_ops *ops = dev->bdev->bd_disk->fops->pr_ops;

	if (!ops || !ops->pr_preempt) {
		pr->ret = -EOPNOTSUPP;
		return -1;
	}

	pr->ret = ops->pr_preempt(dev->bdev, pr->old_key, pr->new_key, pr->type,
				  pr->abort);
	if (!pr->ret)
		return -1;

	return 0;
}

static int dm_pr_preempt(struct block_device *bdev, u64 old_key, u64 new_key,
			 enum pr_type type, bool abort)
{
	struct dm_pr pr = {
		.new_key	= new_key,
		.old_key	= old_key,
		.type		= type,
		.fail_early	= false,
	};
	int ret;

	ret = dm_call_pr(bdev, __dm_pr_preempt, &pr);
	if (ret)
		return ret;

	return pr.ret;
}

static int dm_pr_clear(struct block_device *bdev, u64 key)
{
	struct mapped_device *md = bdev->bd_disk->private_data;
	const struct pr_ops *ops;
	int r, srcu_idx;

	r = dm_prepare_ioctl(md, &srcu_idx, &bdev);
	if (r < 0)
		goto out;

	ops = bdev->bd_disk->fops->pr_ops;
	if (ops && ops->pr_clear)
		r = ops->pr_clear(bdev, key);
	else
		r = -EOPNOTSUPP;
out:
	dm_unprepare_ioctl(md, srcu_idx);
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
	.submit_bio = dm_submit_bio,
	.poll_bio = dm_poll_bio,
	.open = dm_blk_open,
	.release = dm_blk_close,
	.ioctl = dm_blk_ioctl,
	.getgeo = dm_blk_getgeo,
	.report_zones = dm_blk_report_zones,
	.pr_ops = &dm_pr_ops,
	.owner = THIS_MODULE
};

static const struct block_device_operations dm_rq_blk_dops = {
	.open = dm_blk_open,
	.release = dm_blk_close,
	.ioctl = dm_blk_ioctl,
	.getgeo = dm_blk_getgeo,
	.pr_ops = &dm_pr_ops,
	.owner = THIS_MODULE
};

static const struct dax_operations dm_dax_ops = {
	.direct_access = dm_dax_direct_access,
	.zero_page_range = dm_dax_zero_page_range,
	.recovery_write = dm_dax_recovery_write,
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

module_param(swap_bios, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(swap_bios, "Maximum allowed inflight swap IOs");

MODULE_DESCRIPTION(DM_NAME " driver");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
