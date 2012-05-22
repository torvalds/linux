/*
   md.c : Multiple Devices driver for Linux
	  Copyright (C) 1998, 1999, 2000 Ingo Molnar

     completely rewritten, based on the MD driver code from Marc Zyngier

   Changes:

   - RAID-1/RAID-5 extensions by Miguel de Icaza, Gadi Oxman, Ingo Molnar
   - RAID-6 extensions by H. Peter Anvin <hpa@zytor.com>
   - boot support for linear and striped mode by Harald Hoyer <HarryH@Royal.Net>
   - kerneld support by Boris Tobotras <boris@xtalk.msk.su>
   - kmod support by: Cyrus Durgin
   - RAID0 bugfixes: Mark Anthony Lisher <markal@iname.com>
   - Devfs support by Richard Gooch <rgooch@atnf.csiro.au>

   - lots of fixes and improvements to the RAID1/RAID5 and generic
     RAID code (such as request based resynchronization):

     Neil Brown <neilb@cse.unsw.edu.au>.

   - persistent bitmap code
     Copyright (C) 2003-2004, Paul Clements, SteelEye Technology, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/kthread.h>
#include <linux/blkdev.h>
#include <linux/sysctl.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/hdreg.h>
#include <linux/proc_fs.h>
#include <linux/random.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/file.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/raid/md_p.h>
#include <linux/raid/md_u.h>
#include <linux/slab.h>
#include "md.h"
#include "bitmap.h"

#ifndef MODULE
static void autostart_arrays(int part);
#endif

/* pers_list is a list of registered personalities protected
 * by pers_lock.
 * pers_lock does extra service to protect accesses to
 * mddev->thread when the mutex cannot be held.
 */
static LIST_HEAD(pers_list);
static DEFINE_SPINLOCK(pers_lock);

static void md_print_devices(void);

static DECLARE_WAIT_QUEUE_HEAD(resync_wait);
static struct workqueue_struct *md_wq;
static struct workqueue_struct *md_misc_wq;

#define MD_BUG(x...) { printk("md: bug in file %s, line %d\n", __FILE__, __LINE__); md_print_devices(); }

/*
 * Default number of read corrections we'll attempt on an rdev
 * before ejecting it from the array. We divide the read error
 * count by 2 for every hour elapsed between read errors.
 */
#define MD_DEFAULT_MAX_CORRECTED_READ_ERRORS 20
/*
 * Current RAID-1,4,5 parallel reconstruction 'guaranteed speed limit'
 * is 1000 KB/sec, so the extra system load does not show up that much.
 * Increase it if you want to have more _guaranteed_ speed. Note that
 * the RAID driver will use the maximum available bandwidth if the IO
 * subsystem is idle. There is also an 'absolute maximum' reconstruction
 * speed limit - in case reconstruction slows down your system despite
 * idle IO detection.
 *
 * you can change it via /proc/sys/dev/raid/speed_limit_min and _max.
 * or /sys/block/mdX/md/sync_speed_{min,max}
 */

static int sysctl_speed_limit_min = 1000;
static int sysctl_speed_limit_max = 200000;
static inline int speed_min(struct mddev *mddev)
{
	return mddev->sync_speed_min ?
		mddev->sync_speed_min : sysctl_speed_limit_min;
}

static inline int speed_max(struct mddev *mddev)
{
	return mddev->sync_speed_max ?
		mddev->sync_speed_max : sysctl_speed_limit_max;
}

static struct ctl_table_header *raid_table_header;

static ctl_table raid_table[] = {
	{
		.procname	= "speed_limit_min",
		.data		= &sysctl_speed_limit_min,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO|S_IWUSR,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "speed_limit_max",
		.data		= &sysctl_speed_limit_max,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO|S_IWUSR,
		.proc_handler	= proc_dointvec,
	},
	{ }
};

static ctl_table raid_dir_table[] = {
	{
		.procname	= "raid",
		.maxlen		= 0,
		.mode		= S_IRUGO|S_IXUGO,
		.child		= raid_table,
	},
	{ }
};

static ctl_table raid_root_table[] = {
	{
		.procname	= "dev",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= raid_dir_table,
	},
	{  }
};

static const struct block_device_operations md_fops;

static int start_readonly;

/* bio_clone_mddev
 * like bio_clone, but with a local bio set
 */

static void mddev_bio_destructor(struct bio *bio)
{
	struct mddev *mddev, **mddevp;

	mddevp = (void*)bio;
	mddev = mddevp[-1];

	bio_free(bio, mddev->bio_set);
}

struct bio *bio_alloc_mddev(gfp_t gfp_mask, int nr_iovecs,
			    struct mddev *mddev)
{
	struct bio *b;
	struct mddev **mddevp;

	if (!mddev || !mddev->bio_set)
		return bio_alloc(gfp_mask, nr_iovecs);

	b = bio_alloc_bioset(gfp_mask, nr_iovecs,
			     mddev->bio_set);
	if (!b)
		return NULL;
	mddevp = (void*)b;
	mddevp[-1] = mddev;
	b->bi_destructor = mddev_bio_destructor;
	return b;
}
EXPORT_SYMBOL_GPL(bio_alloc_mddev);

struct bio *bio_clone_mddev(struct bio *bio, gfp_t gfp_mask,
			    struct mddev *mddev)
{
	struct bio *b;
	struct mddev **mddevp;

	if (!mddev || !mddev->bio_set)
		return bio_clone(bio, gfp_mask);

	b = bio_alloc_bioset(gfp_mask, bio->bi_max_vecs,
			     mddev->bio_set);
	if (!b)
		return NULL;
	mddevp = (void*)b;
	mddevp[-1] = mddev;
	b->bi_destructor = mddev_bio_destructor;
	__bio_clone(b, bio);
	if (bio_integrity(bio)) {
		int ret;

		ret = bio_integrity_clone(b, bio, gfp_mask, mddev->bio_set);

		if (ret < 0) {
			bio_put(b);
			return NULL;
		}
	}

	return b;
}
EXPORT_SYMBOL_GPL(bio_clone_mddev);

void md_trim_bio(struct bio *bio, int offset, int size)
{
	/* 'bio' is a cloned bio which we need to trim to match
	 * the given offset and size.
	 * This requires adjusting bi_sector, bi_size, and bi_io_vec
	 */
	int i;
	struct bio_vec *bvec;
	int sofar = 0;

	size <<= 9;
	if (offset == 0 && size == bio->bi_size)
		return;

	bio->bi_sector += offset;
	bio->bi_size = size;
	offset <<= 9;
	clear_bit(BIO_SEG_VALID, &bio->bi_flags);

	while (bio->bi_idx < bio->bi_vcnt &&
	       bio->bi_io_vec[bio->bi_idx].bv_len <= offset) {
		/* remove this whole bio_vec */
		offset -= bio->bi_io_vec[bio->bi_idx].bv_len;
		bio->bi_idx++;
	}
	if (bio->bi_idx < bio->bi_vcnt) {
		bio->bi_io_vec[bio->bi_idx].bv_offset += offset;
		bio->bi_io_vec[bio->bi_idx].bv_len -= offset;
	}
	/* avoid any complications with bi_idx being non-zero*/
	if (bio->bi_idx) {
		memmove(bio->bi_io_vec, bio->bi_io_vec+bio->bi_idx,
			(bio->bi_vcnt - bio->bi_idx) * sizeof(struct bio_vec));
		bio->bi_vcnt -= bio->bi_idx;
		bio->bi_idx = 0;
	}
	/* Make sure vcnt and last bv are not too big */
	bio_for_each_segment(bvec, bio, i) {
		if (sofar + bvec->bv_len > size)
			bvec->bv_len = size - sofar;
		if (bvec->bv_len == 0) {
			bio->bi_vcnt = i;
			break;
		}
		sofar += bvec->bv_len;
	}
}
EXPORT_SYMBOL_GPL(md_trim_bio);

/*
 * We have a system wide 'event count' that is incremented
 * on any 'interesting' event, and readers of /proc/mdstat
 * can use 'poll' or 'select' to find out when the event
 * count increases.
 *
 * Events are:
 *  start array, stop array, error, add device, remove device,
 *  start build, activate spare
 */
static DECLARE_WAIT_QUEUE_HEAD(md_event_waiters);
static atomic_t md_event_count;
void md_new_event(struct mddev *mddev)
{
	atomic_inc(&md_event_count);
	wake_up(&md_event_waiters);
}
EXPORT_SYMBOL_GPL(md_new_event);

/* Alternate version that can be called from interrupts
 * when calling sysfs_notify isn't needed.
 */
static void md_new_event_inintr(struct mddev *mddev)
{
	atomic_inc(&md_event_count);
	wake_up(&md_event_waiters);
}

/*
 * Enables to iterate over all existing md arrays
 * all_mddevs_lock protects this list.
 */
static LIST_HEAD(all_mddevs);
static DEFINE_SPINLOCK(all_mddevs_lock);


/*
 * iterates through all used mddevs in the system.
 * We take care to grab the all_mddevs_lock whenever navigating
 * the list, and to always hold a refcount when unlocked.
 * Any code which breaks out of this loop while own
 * a reference to the current mddev and must mddev_put it.
 */
#define for_each_mddev(_mddev,_tmp)					\
									\
	for (({ spin_lock(&all_mddevs_lock); 				\
		_tmp = all_mddevs.next;					\
		_mddev = NULL;});					\
	     ({ if (_tmp != &all_mddevs)				\
			mddev_get(list_entry(_tmp, struct mddev, all_mddevs));\
		spin_unlock(&all_mddevs_lock);				\
		if (_mddev) mddev_put(_mddev);				\
		_mddev = list_entry(_tmp, struct mddev, all_mddevs);	\
		_tmp != &all_mddevs;});					\
	     ({ spin_lock(&all_mddevs_lock);				\
		_tmp = _tmp->next;})					\
		)


/* Rather than calling directly into the personality make_request function,
 * IO requests come here first so that we can check if the device is
 * being suspended pending a reconfiguration.
 * We hold a refcount over the call to ->make_request.  By the time that
 * call has finished, the bio has been linked into some internal structure
 * and so is visible to ->quiesce(), so we don't need the refcount any more.
 */
static void md_make_request(struct request_queue *q, struct bio *bio)
{
	const int rw = bio_data_dir(bio);
	struct mddev *mddev = q->queuedata;
	int cpu;
	unsigned int sectors;

	if (mddev == NULL || mddev->pers == NULL
	    || !mddev->ready) {
		bio_io_error(bio);
		return;
	}
	smp_rmb(); /* Ensure implications of  'active' are visible */
	rcu_read_lock();
	if (mddev->suspended) {
		DEFINE_WAIT(__wait);
		for (;;) {
			prepare_to_wait(&mddev->sb_wait, &__wait,
					TASK_UNINTERRUPTIBLE);
			if (!mddev->suspended)
				break;
			rcu_read_unlock();
			schedule();
			rcu_read_lock();
		}
		finish_wait(&mddev->sb_wait, &__wait);
	}
	atomic_inc(&mddev->active_io);
	rcu_read_unlock();

	/*
	 * save the sectors now since our bio can
	 * go away inside make_request
	 */
	sectors = bio_sectors(bio);
	mddev->pers->make_request(mddev, bio);

	cpu = part_stat_lock();
	part_stat_inc(cpu, &mddev->gendisk->part0, ios[rw]);
	part_stat_add(cpu, &mddev->gendisk->part0, sectors[rw], sectors);
	part_stat_unlock();

	if (atomic_dec_and_test(&mddev->active_io) && mddev->suspended)
		wake_up(&mddev->sb_wait);
}

/* mddev_suspend makes sure no new requests are submitted
 * to the device, and that any requests that have been submitted
 * are completely handled.
 * Once ->stop is called and completes, the module will be completely
 * unused.
 */
void mddev_suspend(struct mddev *mddev)
{
	BUG_ON(mddev->suspended);
	mddev->suspended = 1;
	synchronize_rcu();
	wait_event(mddev->sb_wait, atomic_read(&mddev->active_io) == 0);
	mddev->pers->quiesce(mddev, 1);

	del_timer_sync(&mddev->safemode_timer);
}
EXPORT_SYMBOL_GPL(mddev_suspend);

void mddev_resume(struct mddev *mddev)
{
	mddev->suspended = 0;
	wake_up(&mddev->sb_wait);
	mddev->pers->quiesce(mddev, 0);

	md_wakeup_thread(mddev->thread);
	md_wakeup_thread(mddev->sync_thread); /* possibly kick off a reshape */
}
EXPORT_SYMBOL_GPL(mddev_resume);

int mddev_congested(struct mddev *mddev, int bits)
{
	return mddev->suspended;
}
EXPORT_SYMBOL(mddev_congested);

/*
 * Generic flush handling for md
 */

static void md_end_flush(struct bio *bio, int err)
{
	struct md_rdev *rdev = bio->bi_private;
	struct mddev *mddev = rdev->mddev;

	rdev_dec_pending(rdev, mddev);

	if (atomic_dec_and_test(&mddev->flush_pending)) {
		/* The pre-request flush has finished */
		queue_work(md_wq, &mddev->flush_work);
	}
	bio_put(bio);
}

static void md_submit_flush_data(struct work_struct *ws);

static void submit_flushes(struct work_struct *ws)
{
	struct mddev *mddev = container_of(ws, struct mddev, flush_work);
	struct md_rdev *rdev;

	INIT_WORK(&mddev->flush_work, md_submit_flush_data);
	atomic_set(&mddev->flush_pending, 1);
	rcu_read_lock();
	rdev_for_each_rcu(rdev, mddev)
		if (rdev->raid_disk >= 0 &&
		    !test_bit(Faulty, &rdev->flags)) {
			/* Take two references, one is dropped
			 * when request finishes, one after
			 * we reclaim rcu_read_lock
			 */
			struct bio *bi;
			atomic_inc(&rdev->nr_pending);
			atomic_inc(&rdev->nr_pending);
			rcu_read_unlock();
			bi = bio_alloc_mddev(GFP_NOIO, 0, mddev);
			bi->bi_end_io = md_end_flush;
			bi->bi_private = rdev;
			bi->bi_bdev = rdev->bdev;
			atomic_inc(&mddev->flush_pending);
			submit_bio(WRITE_FLUSH, bi);
			rcu_read_lock();
			rdev_dec_pending(rdev, mddev);
		}
	rcu_read_unlock();
	if (atomic_dec_and_test(&mddev->flush_pending))
		queue_work(md_wq, &mddev->flush_work);
}

static void md_submit_flush_data(struct work_struct *ws)
{
	struct mddev *mddev = container_of(ws, struct mddev, flush_work);
	struct bio *bio = mddev->flush_bio;

	if (bio->bi_size == 0)
		/* an empty barrier - all done */
		bio_endio(bio, 0);
	else {
		bio->bi_rw &= ~REQ_FLUSH;
		mddev->pers->make_request(mddev, bio);
	}

	mddev->flush_bio = NULL;
	wake_up(&mddev->sb_wait);
}

void md_flush_request(struct mddev *mddev, struct bio *bio)
{
	spin_lock_irq(&mddev->write_lock);
	wait_event_lock_irq(mddev->sb_wait,
			    !mddev->flush_bio,
			    mddev->write_lock, /*nothing*/);
	mddev->flush_bio = bio;
	spin_unlock_irq(&mddev->write_lock);

	INIT_WORK(&mddev->flush_work, submit_flushes);
	queue_work(md_wq, &mddev->flush_work);
}
EXPORT_SYMBOL(md_flush_request);

/* Support for plugging.
 * This mirrors the plugging support in request_queue, but does not
 * require having a whole queue or request structures.
 * We allocate an md_plug_cb for each md device and each thread it gets
 * plugged on.  This links tot the private plug_handle structure in the
 * personality data where we keep a count of the number of outstanding
 * plugs so other code can see if a plug is active.
 */
struct md_plug_cb {
	struct blk_plug_cb cb;
	struct mddev *mddev;
};

static void plugger_unplug(struct blk_plug_cb *cb)
{
	struct md_plug_cb *mdcb = container_of(cb, struct md_plug_cb, cb);
	if (atomic_dec_and_test(&mdcb->mddev->plug_cnt))
		md_wakeup_thread(mdcb->mddev->thread);
	kfree(mdcb);
}

/* Check that an unplug wakeup will come shortly.
 * If not, wakeup the md thread immediately
 */
int mddev_check_plugged(struct mddev *mddev)
{
	struct blk_plug *plug = current->plug;
	struct md_plug_cb *mdcb;

	if (!plug)
		return 0;

	list_for_each_entry(mdcb, &plug->cb_list, cb.list) {
		if (mdcb->cb.callback == plugger_unplug &&
		    mdcb->mddev == mddev) {
			/* Already on the list, move to top */
			if (mdcb != list_first_entry(&plug->cb_list,
						    struct md_plug_cb,
						    cb.list))
				list_move(&mdcb->cb.list, &plug->cb_list);
			return 1;
		}
	}
	/* Not currently on the callback list */
	mdcb = kmalloc(sizeof(*mdcb), GFP_ATOMIC);
	if (!mdcb)
		return 0;

	mdcb->mddev = mddev;
	mdcb->cb.callback = plugger_unplug;
	atomic_inc(&mddev->plug_cnt);
	list_add(&mdcb->cb.list, &plug->cb_list);
	return 1;
}
EXPORT_SYMBOL_GPL(mddev_check_plugged);

static inline struct mddev *mddev_get(struct mddev *mddev)
{
	atomic_inc(&mddev->active);
	return mddev;
}

static void mddev_delayed_delete(struct work_struct *ws);

static void mddev_put(struct mddev *mddev)
{
	struct bio_set *bs = NULL;

	if (!atomic_dec_and_lock(&mddev->active, &all_mddevs_lock))
		return;
	if (!mddev->raid_disks && list_empty(&mddev->disks) &&
	    mddev->ctime == 0 && !mddev->hold_active) {
		/* Array is not configured at all, and not held active,
		 * so destroy it */
		list_del_init(&mddev->all_mddevs);
		bs = mddev->bio_set;
		mddev->bio_set = NULL;
		if (mddev->gendisk) {
			/* We did a probe so need to clean up.  Call
			 * queue_work inside the spinlock so that
			 * flush_workqueue() after mddev_find will
			 * succeed in waiting for the work to be done.
			 */
			INIT_WORK(&mddev->del_work, mddev_delayed_delete);
			queue_work(md_misc_wq, &mddev->del_work);
		} else
			kfree(mddev);
	}
	spin_unlock(&all_mddevs_lock);
	if (bs)
		bioset_free(bs);
}

void mddev_init(struct mddev *mddev)
{
	mutex_init(&mddev->open_mutex);
	mutex_init(&mddev->reconfig_mutex);
	mutex_init(&mddev->bitmap_info.mutex);
	INIT_LIST_HEAD(&mddev->disks);
	INIT_LIST_HEAD(&mddev->all_mddevs);
	init_timer(&mddev->safemode_timer);
	atomic_set(&mddev->active, 1);
	atomic_set(&mddev->openers, 0);
	atomic_set(&mddev->active_io, 0);
	atomic_set(&mddev->plug_cnt, 0);
	spin_lock_init(&mddev->write_lock);
	atomic_set(&mddev->flush_pending, 0);
	init_waitqueue_head(&mddev->sb_wait);
	init_waitqueue_head(&mddev->recovery_wait);
	mddev->reshape_position = MaxSector;
	mddev->reshape_backwards = 0;
	mddev->resync_min = 0;
	mddev->resync_max = MaxSector;
	mddev->level = LEVEL_NONE;
}
EXPORT_SYMBOL_GPL(mddev_init);

static struct mddev * mddev_find(dev_t unit)
{
	struct mddev *mddev, *new = NULL;

	if (unit && MAJOR(unit) != MD_MAJOR)
		unit &= ~((1<<MdpMinorShift)-1);

 retry:
	spin_lock(&all_mddevs_lock);

	if (unit) {
		list_for_each_entry(mddev, &all_mddevs, all_mddevs)
			if (mddev->unit == unit) {
				mddev_get(mddev);
				spin_unlock(&all_mddevs_lock);
				kfree(new);
				return mddev;
			}

		if (new) {
			list_add(&new->all_mddevs, &all_mddevs);
			spin_unlock(&all_mddevs_lock);
			new->hold_active = UNTIL_IOCTL;
			return new;
		}
	} else if (new) {
		/* find an unused unit number */
		static int next_minor = 512;
		int start = next_minor;
		int is_free = 0;
		int dev = 0;
		while (!is_free) {
			dev = MKDEV(MD_MAJOR, next_minor);
			next_minor++;
			if (next_minor > MINORMASK)
				next_minor = 0;
			if (next_minor == start) {
				/* Oh dear, all in use. */
				spin_unlock(&all_mddevs_lock);
				kfree(new);
				return NULL;
			}
				
			is_free = 1;
			list_for_each_entry(mddev, &all_mddevs, all_mddevs)
				if (mddev->unit == dev) {
					is_free = 0;
					break;
				}
		}
		new->unit = dev;
		new->md_minor = MINOR(dev);
		new->hold_active = UNTIL_STOP;
		list_add(&new->all_mddevs, &all_mddevs);
		spin_unlock(&all_mddevs_lock);
		return new;
	}
	spin_unlock(&all_mddevs_lock);

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	new->unit = unit;
	if (MAJOR(unit) == MD_MAJOR)
		new->md_minor = MINOR(unit);
	else
		new->md_minor = MINOR(unit) >> MdpMinorShift;

	mddev_init(new);

	goto retry;
}

static inline int mddev_lock(struct mddev * mddev)
{
	return mutex_lock_interruptible(&mddev->reconfig_mutex);
}

static inline int mddev_is_locked(struct mddev *mddev)
{
	return mutex_is_locked(&mddev->reconfig_mutex);
}

static inline int mddev_trylock(struct mddev * mddev)
{
	return mutex_trylock(&mddev->reconfig_mutex);
}

static struct attribute_group md_redundancy_group;

static void mddev_unlock(struct mddev * mddev)
{
	if (mddev->to_remove) {
		/* These cannot be removed under reconfig_mutex as
		 * an access to the files will try to take reconfig_mutex
		 * while holding the file unremovable, which leads to
		 * a deadlock.
		 * So hold set sysfs_active while the remove in happeing,
		 * and anything else which might set ->to_remove or my
		 * otherwise change the sysfs namespace will fail with
		 * -EBUSY if sysfs_active is still set.
		 * We set sysfs_active under reconfig_mutex and elsewhere
		 * test it under the same mutex to ensure its correct value
		 * is seen.
		 */
		struct attribute_group *to_remove = mddev->to_remove;
		mddev->to_remove = NULL;
		mddev->sysfs_active = 1;
		mutex_unlock(&mddev->reconfig_mutex);

		if (mddev->kobj.sd) {
			if (to_remove != &md_redundancy_group)
				sysfs_remove_group(&mddev->kobj, to_remove);
			if (mddev->pers == NULL ||
			    mddev->pers->sync_request == NULL) {
				sysfs_remove_group(&mddev->kobj, &md_redundancy_group);
				if (mddev->sysfs_action)
					sysfs_put(mddev->sysfs_action);
				mddev->sysfs_action = NULL;
			}
		}
		mddev->sysfs_active = 0;
	} else
		mutex_unlock(&mddev->reconfig_mutex);

	/* As we've dropped the mutex we need a spinlock to
	 * make sure the thread doesn't disappear
	 */
	spin_lock(&pers_lock);
	md_wakeup_thread(mddev->thread);
	spin_unlock(&pers_lock);
}

static struct md_rdev * find_rdev_nr(struct mddev *mddev, int nr)
{
	struct md_rdev *rdev;

	rdev_for_each(rdev, mddev)
		if (rdev->desc_nr == nr)
			return rdev;

	return NULL;
}

static struct md_rdev * find_rdev(struct mddev * mddev, dev_t dev)
{
	struct md_rdev *rdev;

	rdev_for_each(rdev, mddev)
		if (rdev->bdev->bd_dev == dev)
			return rdev;

	return NULL;
}

static struct md_personality *find_pers(int level, char *clevel)
{
	struct md_personality *pers;
	list_for_each_entry(pers, &pers_list, list) {
		if (level != LEVEL_NONE && pers->level == level)
			return pers;
		if (strcmp(pers->name, clevel)==0)
			return pers;
	}
	return NULL;
}

/* return the offset of the super block in 512byte sectors */
static inline sector_t calc_dev_sboffset(struct md_rdev *rdev)
{
	sector_t num_sectors = i_size_read(rdev->bdev->bd_inode) / 512;
	return MD_NEW_SIZE_SECTORS(num_sectors);
}

static int alloc_disk_sb(struct md_rdev * rdev)
{
	if (rdev->sb_page)
		MD_BUG();

	rdev->sb_page = alloc_page(GFP_KERNEL);
	if (!rdev->sb_page) {
		printk(KERN_ALERT "md: out of memory.\n");
		return -ENOMEM;
	}

	return 0;
}

void md_rdev_clear(struct md_rdev *rdev)
{
	if (rdev->sb_page) {
		put_page(rdev->sb_page);
		rdev->sb_loaded = 0;
		rdev->sb_page = NULL;
		rdev->sb_start = 0;
		rdev->sectors = 0;
	}
	if (rdev->bb_page) {
		put_page(rdev->bb_page);
		rdev->bb_page = NULL;
	}
	kfree(rdev->badblocks.page);
	rdev->badblocks.page = NULL;
}
EXPORT_SYMBOL_GPL(md_rdev_clear);

static void super_written(struct bio *bio, int error)
{
	struct md_rdev *rdev = bio->bi_private;
	struct mddev *mddev = rdev->mddev;

	if (error || !test_bit(BIO_UPTODATE, &bio->bi_flags)) {
		printk("md: super_written gets error=%d, uptodate=%d\n",
		       error, test_bit(BIO_UPTODATE, &bio->bi_flags));
		WARN_ON(test_bit(BIO_UPTODATE, &bio->bi_flags));
		md_error(mddev, rdev);
	}

	if (atomic_dec_and_test(&mddev->pending_writes))
		wake_up(&mddev->sb_wait);
	bio_put(bio);
}

void md_super_write(struct mddev *mddev, struct md_rdev *rdev,
		   sector_t sector, int size, struct page *page)
{
	/* write first size bytes of page to sector of rdev
	 * Increment mddev->pending_writes before returning
	 * and decrement it on completion, waking up sb_wait
	 * if zero is reached.
	 * If an error occurred, call md_error
	 */
	struct bio *bio = bio_alloc_mddev(GFP_NOIO, 1, mddev);

	bio->bi_bdev = rdev->meta_bdev ? rdev->meta_bdev : rdev->bdev;
	bio->bi_sector = sector;
	bio_add_page(bio, page, size, 0);
	bio->bi_private = rdev;
	bio->bi_end_io = super_written;

	atomic_inc(&mddev->pending_writes);
	submit_bio(WRITE_FLUSH_FUA, bio);
}

void md_super_wait(struct mddev *mddev)
{
	/* wait for all superblock writes that were scheduled to complete */
	DEFINE_WAIT(wq);
	for(;;) {
		prepare_to_wait(&mddev->sb_wait, &wq, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&mddev->pending_writes)==0)
			break;
		schedule();
	}
	finish_wait(&mddev->sb_wait, &wq);
}

static void bi_complete(struct bio *bio, int error)
{
	complete((struct completion*)bio->bi_private);
}

int sync_page_io(struct md_rdev *rdev, sector_t sector, int size,
		 struct page *page, int rw, bool metadata_op)
{
	struct bio *bio = bio_alloc_mddev(GFP_NOIO, 1, rdev->mddev);
	struct completion event;
	int ret;

	rw |= REQ_SYNC;

	bio->bi_bdev = (metadata_op && rdev->meta_bdev) ?
		rdev->meta_bdev : rdev->bdev;
	if (metadata_op)
		bio->bi_sector = sector + rdev->sb_start;
	else if (rdev->mddev->reshape_position != MaxSector &&
		 (rdev->mddev->reshape_backwards ==
		  (sector >= rdev->mddev->reshape_position)))
		bio->bi_sector = sector + rdev->new_data_offset;
	else
		bio->bi_sector = sector + rdev->data_offset;
	bio_add_page(bio, page, size, 0);
	init_completion(&event);
	bio->bi_private = &event;
	bio->bi_end_io = bi_complete;
	submit_bio(rw, bio);
	wait_for_completion(&event);

	ret = test_bit(BIO_UPTODATE, &bio->bi_flags);
	bio_put(bio);
	return ret;
}
EXPORT_SYMBOL_GPL(sync_page_io);

static int read_disk_sb(struct md_rdev * rdev, int size)
{
	char b[BDEVNAME_SIZE];
	if (!rdev->sb_page) {
		MD_BUG();
		return -EINVAL;
	}
	if (rdev->sb_loaded)
		return 0;


	if (!sync_page_io(rdev, 0, size, rdev->sb_page, READ, true))
		goto fail;
	rdev->sb_loaded = 1;
	return 0;

fail:
	printk(KERN_WARNING "md: disabled device %s, could not read superblock.\n",
		bdevname(rdev->bdev,b));
	return -EINVAL;
}

static int uuid_equal(mdp_super_t *sb1, mdp_super_t *sb2)
{
	return 	sb1->set_uuid0 == sb2->set_uuid0 &&
		sb1->set_uuid1 == sb2->set_uuid1 &&
		sb1->set_uuid2 == sb2->set_uuid2 &&
		sb1->set_uuid3 == sb2->set_uuid3;
}

static int sb_equal(mdp_super_t *sb1, mdp_super_t *sb2)
{
	int ret;
	mdp_super_t *tmp1, *tmp2;

	tmp1 = kmalloc(sizeof(*tmp1),GFP_KERNEL);
	tmp2 = kmalloc(sizeof(*tmp2),GFP_KERNEL);

	if (!tmp1 || !tmp2) {
		ret = 0;
		printk(KERN_INFO "md.c sb_equal(): failed to allocate memory!\n");
		goto abort;
	}

	*tmp1 = *sb1;
	*tmp2 = *sb2;

	/*
	 * nr_disks is not constant
	 */
	tmp1->nr_disks = 0;
	tmp2->nr_disks = 0;

	ret = (memcmp(tmp1, tmp2, MD_SB_GENERIC_CONSTANT_WORDS * 4) == 0);
abort:
	kfree(tmp1);
	kfree(tmp2);
	return ret;
}


static u32 md_csum_fold(u32 csum)
{
	csum = (csum & 0xffff) + (csum >> 16);
	return (csum & 0xffff) + (csum >> 16);
}

static unsigned int calc_sb_csum(mdp_super_t * sb)
{
	u64 newcsum = 0;
	u32 *sb32 = (u32*)sb;
	int i;
	unsigned int disk_csum, csum;

	disk_csum = sb->sb_csum;
	sb->sb_csum = 0;

	for (i = 0; i < MD_SB_BYTES/4 ; i++)
		newcsum += sb32[i];
	csum = (newcsum & 0xffffffff) + (newcsum>>32);


#ifdef CONFIG_ALPHA
	/* This used to use csum_partial, which was wrong for several
	 * reasons including that different results are returned on
	 * different architectures.  It isn't critical that we get exactly
	 * the same return value as before (we always csum_fold before
	 * testing, and that removes any differences).  However as we
	 * know that csum_partial always returned a 16bit value on
	 * alphas, do a fold to maximise conformity to previous behaviour.
	 */
	sb->sb_csum = md_csum_fold(disk_csum);
#else
	sb->sb_csum = disk_csum;
#endif
	return csum;
}


/*
 * Handle superblock details.
 * We want to be able to handle multiple superblock formats
 * so we have a common interface to them all, and an array of
 * different handlers.
 * We rely on user-space to write the initial superblock, and support
 * reading and updating of superblocks.
 * Interface methods are:
 *   int load_super(struct md_rdev *dev, struct md_rdev *refdev, int minor_version)
 *      loads and validates a superblock on dev.
 *      if refdev != NULL, compare superblocks on both devices
 *    Return:
 *      0 - dev has a superblock that is compatible with refdev
 *      1 - dev has a superblock that is compatible and newer than refdev
 *          so dev should be used as the refdev in future
 *     -EINVAL superblock incompatible or invalid
 *     -othererror e.g. -EIO
 *
 *   int validate_super(struct mddev *mddev, struct md_rdev *dev)
 *      Verify that dev is acceptable into mddev.
 *       The first time, mddev->raid_disks will be 0, and data from
 *       dev should be merged in.  Subsequent calls check that dev
 *       is new enough.  Return 0 or -EINVAL
 *
 *   void sync_super(struct mddev *mddev, struct md_rdev *dev)
 *     Update the superblock for rdev with data in mddev
 *     This does not write to disc.
 *
 */

struct super_type  {
	char		    *name;
	struct module	    *owner;
	int		    (*load_super)(struct md_rdev *rdev,
					  struct md_rdev *refdev,
					  int minor_version);
	int		    (*validate_super)(struct mddev *mddev,
					      struct md_rdev *rdev);
	void		    (*sync_super)(struct mddev *mddev,
					  struct md_rdev *rdev);
	unsigned long long  (*rdev_size_change)(struct md_rdev *rdev,
						sector_t num_sectors);
	int		    (*allow_new_offset)(struct md_rdev *rdev,
						unsigned long long new_offset);
};

/*
 * Check that the given mddev has no bitmap.
 *
 * This function is called from the run method of all personalities that do not
 * support bitmaps. It prints an error message and returns non-zero if mddev
 * has a bitmap. Otherwise, it returns 0.
 *
 */
int md_check_no_bitmap(struct mddev *mddev)
{
	if (!mddev->bitmap_info.file && !mddev->bitmap_info.offset)
		return 0;
	printk(KERN_ERR "%s: bitmaps are not supported for %s\n",
		mdname(mddev), mddev->pers->name);
	return 1;
}
EXPORT_SYMBOL(md_check_no_bitmap);

/*
 * load_super for 0.90.0 
 */
static int super_90_load(struct md_rdev *rdev, struct md_rdev *refdev, int minor_version)
{
	char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
	mdp_super_t *sb;
	int ret;

	/*
	 * Calculate the position of the superblock (512byte sectors),
	 * it's at the end of the disk.
	 *
	 * It also happens to be a multiple of 4Kb.
	 */
	rdev->sb_start = calc_dev_sboffset(rdev);

	ret = read_disk_sb(rdev, MD_SB_BYTES);
	if (ret) return ret;

	ret = -EINVAL;

	bdevname(rdev->bdev, b);
	sb = page_address(rdev->sb_page);

	if (sb->md_magic != MD_SB_MAGIC) {
		printk(KERN_ERR "md: invalid raid superblock magic on %s\n",
		       b);
		goto abort;
	}

	if (sb->major_version != 0 ||
	    sb->minor_version < 90 ||
	    sb->minor_version > 91) {
		printk(KERN_WARNING "Bad version number %d.%d on %s\n",
			sb->major_version, sb->minor_version,
			b);
		goto abort;
	}

	if (sb->raid_disks <= 0)
		goto abort;

	if (md_csum_fold(calc_sb_csum(sb)) != md_csum_fold(sb->sb_csum)) {
		printk(KERN_WARNING "md: invalid superblock checksum on %s\n",
			b);
		goto abort;
	}

	rdev->preferred_minor = sb->md_minor;
	rdev->data_offset = 0;
	rdev->new_data_offset = 0;
	rdev->sb_size = MD_SB_BYTES;
	rdev->badblocks.shift = -1;

	if (sb->level == LEVEL_MULTIPATH)
		rdev->desc_nr = -1;
	else
		rdev->desc_nr = sb->this_disk.number;

	if (!refdev) {
		ret = 1;
	} else {
		__u64 ev1, ev2;
		mdp_super_t *refsb = page_address(refdev->sb_page);
		if (!uuid_equal(refsb, sb)) {
			printk(KERN_WARNING "md: %s has different UUID to %s\n",
				b, bdevname(refdev->bdev,b2));
			goto abort;
		}
		if (!sb_equal(refsb, sb)) {
			printk(KERN_WARNING "md: %s has same UUID"
			       " but different superblock to %s\n",
			       b, bdevname(refdev->bdev, b2));
			goto abort;
		}
		ev1 = md_event(sb);
		ev2 = md_event(refsb);
		if (ev1 > ev2)
			ret = 1;
		else 
			ret = 0;
	}
	rdev->sectors = rdev->sb_start;
	/* Limit to 4TB as metadata cannot record more than that */
	if (rdev->sectors >= (2ULL << 32))
		rdev->sectors = (2ULL << 32) - 2;

	if (rdev->sectors < ((sector_t)sb->size) * 2 && sb->level >= 1)
		/* "this cannot possibly happen" ... */
		ret = -EINVAL;

 abort:
	return ret;
}

/*
 * validate_super for 0.90.0
 */
static int super_90_validate(struct mddev *mddev, struct md_rdev *rdev)
{
	mdp_disk_t *desc;
	mdp_super_t *sb = page_address(rdev->sb_page);
	__u64 ev1 = md_event(sb);

	rdev->raid_disk = -1;
	clear_bit(Faulty, &rdev->flags);
	clear_bit(In_sync, &rdev->flags);
	clear_bit(WriteMostly, &rdev->flags);

	if (mddev->raid_disks == 0) {
		mddev->major_version = 0;
		mddev->minor_version = sb->minor_version;
		mddev->patch_version = sb->patch_version;
		mddev->external = 0;
		mddev->chunk_sectors = sb->chunk_size >> 9;
		mddev->ctime = sb->ctime;
		mddev->utime = sb->utime;
		mddev->level = sb->level;
		mddev->clevel[0] = 0;
		mddev->layout = sb->layout;
		mddev->raid_disks = sb->raid_disks;
		mddev->dev_sectors = ((sector_t)sb->size) * 2;
		mddev->events = ev1;
		mddev->bitmap_info.offset = 0;
		mddev->bitmap_info.space = 0;
		/* bitmap can use 60 K after the 4K superblocks */
		mddev->bitmap_info.default_offset = MD_SB_BYTES >> 9;
		mddev->bitmap_info.default_space = 64*2 - (MD_SB_BYTES >> 9);
		mddev->reshape_backwards = 0;

		if (mddev->minor_version >= 91) {
			mddev->reshape_position = sb->reshape_position;
			mddev->delta_disks = sb->delta_disks;
			mddev->new_level = sb->new_level;
			mddev->new_layout = sb->new_layout;
			mddev->new_chunk_sectors = sb->new_chunk >> 9;
			if (mddev->delta_disks < 0)
				mddev->reshape_backwards = 1;
		} else {
			mddev->reshape_position = MaxSector;
			mddev->delta_disks = 0;
			mddev->new_level = mddev->level;
			mddev->new_layout = mddev->layout;
			mddev->new_chunk_sectors = mddev->chunk_sectors;
		}

		if (sb->state & (1<<MD_SB_CLEAN))
			mddev->recovery_cp = MaxSector;
		else {
			if (sb->events_hi == sb->cp_events_hi && 
				sb->events_lo == sb->cp_events_lo) {
				mddev->recovery_cp = sb->recovery_cp;
			} else
				mddev->recovery_cp = 0;
		}

		memcpy(mddev->uuid+0, &sb->set_uuid0, 4);
		memcpy(mddev->uuid+4, &sb->set_uuid1, 4);
		memcpy(mddev->uuid+8, &sb->set_uuid2, 4);
		memcpy(mddev->uuid+12,&sb->set_uuid3, 4);

		mddev->max_disks = MD_SB_DISKS;

		if (sb->state & (1<<MD_SB_BITMAP_PRESENT) &&
		    mddev->bitmap_info.file == NULL) {
			mddev->bitmap_info.offset =
				mddev->bitmap_info.default_offset;
			mddev->bitmap_info.space =
				mddev->bitmap_info.space;
		}

	} else if (mddev->pers == NULL) {
		/* Insist on good event counter while assembling, except
		 * for spares (which don't need an event count) */
		++ev1;
		if (sb->disks[rdev->desc_nr].state & (
			    (1<<MD_DISK_SYNC) | (1 << MD_DISK_ACTIVE)))
			if (ev1 < mddev->events) 
				return -EINVAL;
	} else if (mddev->bitmap) {
		/* if adding to array with a bitmap, then we can accept an
		 * older device ... but not too old.
		 */
		if (ev1 < mddev->bitmap->events_cleared)
			return 0;
	} else {
		if (ev1 < mddev->events)
			/* just a hot-add of a new device, leave raid_disk at -1 */
			return 0;
	}

	if (mddev->level != LEVEL_MULTIPATH) {
		desc = sb->disks + rdev->desc_nr;

		if (desc->state & (1<<MD_DISK_FAULTY))
			set_bit(Faulty, &rdev->flags);
		else if (desc->state & (1<<MD_DISK_SYNC) /* &&
			    desc->raid_disk < mddev->raid_disks */) {
			set_bit(In_sync, &rdev->flags);
			rdev->raid_disk = desc->raid_disk;
		} else if (desc->state & (1<<MD_DISK_ACTIVE)) {
			/* active but not in sync implies recovery up to
			 * reshape position.  We don't know exactly where
			 * that is, so set to zero for now */
			if (mddev->minor_version >= 91) {
				rdev->recovery_offset = 0;
				rdev->raid_disk = desc->raid_disk;
			}
		}
		if (desc->state & (1<<MD_DISK_WRITEMOSTLY))
			set_bit(WriteMostly, &rdev->flags);
	} else /* MULTIPATH are always insync */
		set_bit(In_sync, &rdev->flags);
	return 0;
}

/*
 * sync_super for 0.90.0
 */
static void super_90_sync(struct mddev *mddev, struct md_rdev *rdev)
{
	mdp_super_t *sb;
	struct md_rdev *rdev2;
	int next_spare = mddev->raid_disks;


	/* make rdev->sb match mddev data..
	 *
	 * 1/ zero out disks
	 * 2/ Add info for each disk, keeping track of highest desc_nr (next_spare);
	 * 3/ any empty disks < next_spare become removed
	 *
	 * disks[0] gets initialised to REMOVED because
	 * we cannot be sure from other fields if it has
	 * been initialised or not.
	 */
	int i;
	int active=0, working=0,failed=0,spare=0,nr_disks=0;

	rdev->sb_size = MD_SB_BYTES;

	sb = page_address(rdev->sb_page);

	memset(sb, 0, sizeof(*sb));

	sb->md_magic = MD_SB_MAGIC;
	sb->major_version = mddev->major_version;
	sb->patch_version = mddev->patch_version;
	sb->gvalid_words  = 0; /* ignored */
	memcpy(&sb->set_uuid0, mddev->uuid+0, 4);
	memcpy(&sb->set_uuid1, mddev->uuid+4, 4);
	memcpy(&sb->set_uuid2, mddev->uuid+8, 4);
	memcpy(&sb->set_uuid3, mddev->uuid+12,4);

	sb->ctime = mddev->ctime;
	sb->level = mddev->level;
	sb->size = mddev->dev_sectors / 2;
	sb->raid_disks = mddev->raid_disks;
	sb->md_minor = mddev->md_minor;
	sb->not_persistent = 0;
	sb->utime = mddev->utime;
	sb->state = 0;
	sb->events_hi = (mddev->events>>32);
	sb->events_lo = (u32)mddev->events;

	if (mddev->reshape_position == MaxSector)
		sb->minor_version = 90;
	else {
		sb->minor_version = 91;
		sb->reshape_position = mddev->reshape_position;
		sb->new_level = mddev->new_level;
		sb->delta_disks = mddev->delta_disks;
		sb->new_layout = mddev->new_layout;
		sb->new_chunk = mddev->new_chunk_sectors << 9;
	}
	mddev->minor_version = sb->minor_version;
	if (mddev->in_sync)
	{
		sb->recovery_cp = mddev->recovery_cp;
		sb->cp_events_hi = (mddev->events>>32);
		sb->cp_events_lo = (u32)mddev->events;
		if (mddev->recovery_cp == MaxSector)
			sb->state = (1<< MD_SB_CLEAN);
	} else
		sb->recovery_cp = 0;

	sb->layout = mddev->layout;
	sb->chunk_size = mddev->chunk_sectors << 9;

	if (mddev->bitmap && mddev->bitmap_info.file == NULL)
		sb->state |= (1<<MD_SB_BITMAP_PRESENT);

	sb->disks[0].state = (1<<MD_DISK_REMOVED);
	rdev_for_each(rdev2, mddev) {
		mdp_disk_t *d;
		int desc_nr;
		int is_active = test_bit(In_sync, &rdev2->flags);

		if (rdev2->raid_disk >= 0 &&
		    sb->minor_version >= 91)
			/* we have nowhere to store the recovery_offset,
			 * but if it is not below the reshape_position,
			 * we can piggy-back on that.
			 */
			is_active = 1;
		if (rdev2->raid_disk < 0 ||
		    test_bit(Faulty, &rdev2->flags))
			is_active = 0;
		if (is_active)
			desc_nr = rdev2->raid_disk;
		else
			desc_nr = next_spare++;
		rdev2->desc_nr = desc_nr;
		d = &sb->disks[rdev2->desc_nr];
		nr_disks++;
		d->number = rdev2->desc_nr;
		d->major = MAJOR(rdev2->bdev->bd_dev);
		d->minor = MINOR(rdev2->bdev->bd_dev);
		if (is_active)
			d->raid_disk = rdev2->raid_disk;
		else
			d->raid_disk = rdev2->desc_nr; /* compatibility */
		if (test_bit(Faulty, &rdev2->flags))
			d->state = (1<<MD_DISK_FAULTY);
		else if (is_active) {
			d->state = (1<<MD_DISK_ACTIVE);
			if (test_bit(In_sync, &rdev2->flags))
				d->state |= (1<<MD_DISK_SYNC);
			active++;
			working++;
		} else {
			d->state = 0;
			spare++;
			working++;
		}
		if (test_bit(WriteMostly, &rdev2->flags))
			d->state |= (1<<MD_DISK_WRITEMOSTLY);
	}
	/* now set the "removed" and "faulty" bits on any missing devices */
	for (i=0 ; i < mddev->raid_disks ; i++) {
		mdp_disk_t *d = &sb->disks[i];
		if (d->state == 0 && d->number == 0) {
			d->number = i;
			d->raid_disk = i;
			d->state = (1<<MD_DISK_REMOVED);
			d->state |= (1<<MD_DISK_FAULTY);
			failed++;
		}
	}
	sb->nr_disks = nr_disks;
	sb->active_disks = active;
	sb->working_disks = working;
	sb->failed_disks = failed;
	sb->spare_disks = spare;

	sb->this_disk = sb->disks[rdev->desc_nr];
	sb->sb_csum = calc_sb_csum(sb);
}

/*
 * rdev_size_change for 0.90.0
 */
static unsigned long long
super_90_rdev_size_change(struct md_rdev *rdev, sector_t num_sectors)
{
	if (num_sectors && num_sectors < rdev->mddev->dev_sectors)
		return 0; /* component must fit device */
	if (rdev->mddev->bitmap_info.offset)
		return 0; /* can't move bitmap */
	rdev->sb_start = calc_dev_sboffset(rdev);
	if (!num_sectors || num_sectors > rdev->sb_start)
		num_sectors = rdev->sb_start;
	/* Limit to 4TB as metadata cannot record more than that.
	 * 4TB == 2^32 KB, or 2*2^32 sectors.
	 */
	if (num_sectors >= (2ULL << 32))
		num_sectors = (2ULL << 32) - 2;
	md_super_write(rdev->mddev, rdev, rdev->sb_start, rdev->sb_size,
		       rdev->sb_page);
	md_super_wait(rdev->mddev);
	return num_sectors;
}

static int
super_90_allow_new_offset(struct md_rdev *rdev, unsigned long long new_offset)
{
	/* non-zero offset changes not possible with v0.90 */
	return new_offset == 0;
}

/*
 * version 1 superblock
 */

static __le32 calc_sb_1_csum(struct mdp_superblock_1 * sb)
{
	__le32 disk_csum;
	u32 csum;
	unsigned long long newcsum;
	int size = 256 + le32_to_cpu(sb->max_dev)*2;
	__le32 *isuper = (__le32*)sb;
	int i;

	disk_csum = sb->sb_csum;
	sb->sb_csum = 0;
	newcsum = 0;
	for (i=0; size>=4; size -= 4 )
		newcsum += le32_to_cpu(*isuper++);

	if (size == 2)
		newcsum += le16_to_cpu(*(__le16*) isuper);

	csum = (newcsum & 0xffffffff) + (newcsum >> 32);
	sb->sb_csum = disk_csum;
	return cpu_to_le32(csum);
}

static int md_set_badblocks(struct badblocks *bb, sector_t s, int sectors,
			    int acknowledged);
static int super_1_load(struct md_rdev *rdev, struct md_rdev *refdev, int minor_version)
{
	struct mdp_superblock_1 *sb;
	int ret;
	sector_t sb_start;
	sector_t sectors;
	char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
	int bmask;

	/*
	 * Calculate the position of the superblock in 512byte sectors.
	 * It is always aligned to a 4K boundary and
	 * depeding on minor_version, it can be:
	 * 0: At least 8K, but less than 12K, from end of device
	 * 1: At start of device
	 * 2: 4K from start of device.
	 */
	switch(minor_version) {
	case 0:
		sb_start = i_size_read(rdev->bdev->bd_inode) >> 9;
		sb_start -= 8*2;
		sb_start &= ~(sector_t)(4*2-1);
		break;
	case 1:
		sb_start = 0;
		break;
	case 2:
		sb_start = 8;
		break;
	default:
		return -EINVAL;
	}
	rdev->sb_start = sb_start;

	/* superblock is rarely larger than 1K, but it can be larger,
	 * and it is safe to read 4k, so we do that
	 */
	ret = read_disk_sb(rdev, 4096);
	if (ret) return ret;


	sb = page_address(rdev->sb_page);

	if (sb->magic != cpu_to_le32(MD_SB_MAGIC) ||
	    sb->major_version != cpu_to_le32(1) ||
	    le32_to_cpu(sb->max_dev) > (4096-256)/2 ||
	    le64_to_cpu(sb->super_offset) != rdev->sb_start ||
	    (le32_to_cpu(sb->feature_map) & ~MD_FEATURE_ALL) != 0)
		return -EINVAL;

	if (calc_sb_1_csum(sb) != sb->sb_csum) {
		printk("md: invalid superblock checksum on %s\n",
			bdevname(rdev->bdev,b));
		return -EINVAL;
	}
	if (le64_to_cpu(sb->data_size) < 10) {
		printk("md: data_size too small on %s\n",
		       bdevname(rdev->bdev,b));
		return -EINVAL;
	}
	if (sb->pad0 ||
	    sb->pad3[0] ||
	    memcmp(sb->pad3, sb->pad3+1, sizeof(sb->pad3) - sizeof(sb->pad3[1])))
		/* Some padding is non-zero, might be a new feature */
		return -EINVAL;

	rdev->preferred_minor = 0xffff;
	rdev->data_offset = le64_to_cpu(sb->data_offset);
	rdev->new_data_offset = rdev->data_offset;
	if ((le32_to_cpu(sb->feature_map) & MD_FEATURE_RESHAPE_ACTIVE) &&
	    (le32_to_cpu(sb->feature_map) & MD_FEATURE_NEW_OFFSET))
		rdev->new_data_offset += (s32)le32_to_cpu(sb->new_offset);
	atomic_set(&rdev->corrected_errors, le32_to_cpu(sb->cnt_corrected_read));

	rdev->sb_size = le32_to_cpu(sb->max_dev) * 2 + 256;
	bmask = queue_logical_block_size(rdev->bdev->bd_disk->queue)-1;
	if (rdev->sb_size & bmask)
		rdev->sb_size = (rdev->sb_size | bmask) + 1;

	if (minor_version
	    && rdev->data_offset < sb_start + (rdev->sb_size/512))
		return -EINVAL;
	if (minor_version
	    && rdev->new_data_offset < sb_start + (rdev->sb_size/512))
		return -EINVAL;

	if (sb->level == cpu_to_le32(LEVEL_MULTIPATH))
		rdev->desc_nr = -1;
	else
		rdev->desc_nr = le32_to_cpu(sb->dev_number);

	if (!rdev->bb_page) {
		rdev->bb_page = alloc_page(GFP_KERNEL);
		if (!rdev->bb_page)
			return -ENOMEM;
	}
	if ((le32_to_cpu(sb->feature_map) & MD_FEATURE_BAD_BLOCKS) &&
	    rdev->badblocks.count == 0) {
		/* need to load the bad block list.
		 * Currently we limit it to one page.
		 */
		s32 offset;
		sector_t bb_sector;
		u64 *bbp;
		int i;
		int sectors = le16_to_cpu(sb->bblog_size);
		if (sectors > (PAGE_SIZE / 512))
			return -EINVAL;
		offset = le32_to_cpu(sb->bblog_offset);
		if (offset == 0)
			return -EINVAL;
		bb_sector = (long long)offset;
		if (!sync_page_io(rdev, bb_sector, sectors << 9,
				  rdev->bb_page, READ, true))
			return -EIO;
		bbp = (u64 *)page_address(rdev->bb_page);
		rdev->badblocks.shift = sb->bblog_shift;
		for (i = 0 ; i < (sectors << (9-3)) ; i++, bbp++) {
			u64 bb = le64_to_cpu(*bbp);
			int count = bb & (0x3ff);
			u64 sector = bb >> 10;
			sector <<= sb->bblog_shift;
			count <<= sb->bblog_shift;
			if (bb + 1 == 0)
				break;
			if (md_set_badblocks(&rdev->badblocks,
					     sector, count, 1) == 0)
				return -EINVAL;
		}
	} else if (sb->bblog_offset == 0)
		rdev->badblocks.shift = -1;

	if (!refdev) {
		ret = 1;
	} else {
		__u64 ev1, ev2;
		struct mdp_superblock_1 *refsb = page_address(refdev->sb_page);

		if (memcmp(sb->set_uuid, refsb->set_uuid, 16) != 0 ||
		    sb->level != refsb->level ||
		    sb->layout != refsb->layout ||
		    sb->chunksize != refsb->chunksize) {
			printk(KERN_WARNING "md: %s has strangely different"
				" superblock to %s\n",
				bdevname(rdev->bdev,b),
				bdevname(refdev->bdev,b2));
			return -EINVAL;
		}
		ev1 = le64_to_cpu(sb->events);
		ev2 = le64_to_cpu(refsb->events);

		if (ev1 > ev2)
			ret = 1;
		else
			ret = 0;
	}
	if (minor_version) {
		sectors = (i_size_read(rdev->bdev->bd_inode) >> 9);
		sectors -= rdev->data_offset;
	} else
		sectors = rdev->sb_start;
	if (sectors < le64_to_cpu(sb->data_size))
		return -EINVAL;
	rdev->sectors = le64_to_cpu(sb->data_size);
	return ret;
}

static int super_1_validate(struct mddev *mddev, struct md_rdev *rdev)
{
	struct mdp_superblock_1 *sb = page_address(rdev->sb_page);
	__u64 ev1 = le64_to_cpu(sb->events);

	rdev->raid_disk = -1;
	clear_bit(Faulty, &rdev->flags);
	clear_bit(In_sync, &rdev->flags);
	clear_bit(WriteMostly, &rdev->flags);

	if (mddev->raid_disks == 0) {
		mddev->major_version = 1;
		mddev->patch_version = 0;
		mddev->external = 0;
		mddev->chunk_sectors = le32_to_cpu(sb->chunksize);
		mddev->ctime = le64_to_cpu(sb->ctime) & ((1ULL << 32)-1);
		mddev->utime = le64_to_cpu(sb->utime) & ((1ULL << 32)-1);
		mddev->level = le32_to_cpu(sb->level);
		mddev->clevel[0] = 0;
		mddev->layout = le32_to_cpu(sb->layout);
		mddev->raid_disks = le32_to_cpu(sb->raid_disks);
		mddev->dev_sectors = le64_to_cpu(sb->size);
		mddev->events = ev1;
		mddev->bitmap_info.offset = 0;
		mddev->bitmap_info.space = 0;
		/* Default location for bitmap is 1K after superblock
		 * using 3K - total of 4K
		 */
		mddev->bitmap_info.default_offset = 1024 >> 9;
		mddev->bitmap_info.default_space = (4096-1024) >> 9;
		mddev->reshape_backwards = 0;

		mddev->recovery_cp = le64_to_cpu(sb->resync_offset);
		memcpy(mddev->uuid, sb->set_uuid, 16);

		mddev->max_disks =  (4096-256)/2;

		if ((le32_to_cpu(sb->feature_map) & MD_FEATURE_BITMAP_OFFSET) &&
		    mddev->bitmap_info.file == NULL) {
			mddev->bitmap_info.offset =
				(__s32)le32_to_cpu(sb->bitmap_offset);
			/* Metadata doesn't record how much space is available.
			 * For 1.0, we assume we can use up to the superblock
			 * if before, else to 4K beyond superblock.
			 * For others, assume no change is possible.
			 */
			if (mddev->minor_version > 0)
				mddev->bitmap_info.space = 0;
			else if (mddev->bitmap_info.offset > 0)
				mddev->bitmap_info.space =
					8 - mddev->bitmap_info.offset;
			else
				mddev->bitmap_info.space =
					-mddev->bitmap_info.offset;
		}

		if ((le32_to_cpu(sb->feature_map) & MD_FEATURE_RESHAPE_ACTIVE)) {
			mddev->reshape_position = le64_to_cpu(sb->reshape_position);
			mddev->delta_disks = le32_to_cpu(sb->delta_disks);
			mddev->new_level = le32_to_cpu(sb->new_level);
			mddev->new_layout = le32_to_cpu(sb->new_layout);
			mddev->new_chunk_sectors = le32_to_cpu(sb->new_chunk);
			if (mddev->delta_disks < 0 ||
			    (mddev->delta_disks == 0 &&
			     (le32_to_cpu(sb->feature_map)
			      & MD_FEATURE_RESHAPE_BACKWARDS)))
				mddev->reshape_backwards = 1;
		} else {
			mddev->reshape_position = MaxSector;
			mddev->delta_disks = 0;
			mddev->new_level = mddev->level;
			mddev->new_layout = mddev->layout;
			mddev->new_chunk_sectors = mddev->chunk_sectors;
		}

	} else if (mddev->pers == NULL) {
		/* Insist of good event counter while assembling, except for
		 * spares (which don't need an event count) */
		++ev1;
		if (rdev->desc_nr >= 0 &&
		    rdev->desc_nr < le32_to_cpu(sb->max_dev) &&
		    le16_to_cpu(sb->dev_roles[rdev->desc_nr]) < 0xfffe)
			if (ev1 < mddev->events)
				return -EINVAL;
	} else if (mddev->bitmap) {
		/* If adding to array with a bitmap, then we can accept an
		 * older device, but not too old.
		 */
		if (ev1 < mddev->bitmap->events_cleared)
			return 0;
	} else {
		if (ev1 < mddev->events)
			/* just a hot-add of a new device, leave raid_disk at -1 */
			return 0;
	}
	if (mddev->level != LEVEL_MULTIPATH) {
		int role;
		if (rdev->desc_nr < 0 ||
		    rdev->desc_nr >= le32_to_cpu(sb->max_dev)) {
			role = 0xffff;
			rdev->desc_nr = -1;
		} else
			role = le16_to_cpu(sb->dev_roles[rdev->desc_nr]);
		switch(role) {
		case 0xffff: /* spare */
			break;
		case 0xfffe: /* faulty */
			set_bit(Faulty, &rdev->flags);
			break;
		default:
			if ((le32_to_cpu(sb->feature_map) &
			     MD_FEATURE_RECOVERY_OFFSET))
				rdev->recovery_offset = le64_to_cpu(sb->recovery_offset);
			else
				set_bit(In_sync, &rdev->flags);
			rdev->raid_disk = role;
			break;
		}
		if (sb->devflags & WriteMostly1)
			set_bit(WriteMostly, &rdev->flags);
		if (le32_to_cpu(sb->feature_map) & MD_FEATURE_REPLACEMENT)
			set_bit(Replacement, &rdev->flags);
	} else /* MULTIPATH are always insync */
		set_bit(In_sync, &rdev->flags);

	return 0;
}

static void super_1_sync(struct mddev *mddev, struct md_rdev *rdev)
{
	struct mdp_superblock_1 *sb;
	struct md_rdev *rdev2;
	int max_dev, i;
	/* make rdev->sb match mddev and rdev data. */

	sb = page_address(rdev->sb_page);

	sb->feature_map = 0;
	sb->pad0 = 0;
	sb->recovery_offset = cpu_to_le64(0);
	memset(sb->pad3, 0, sizeof(sb->pad3));

	sb->utime = cpu_to_le64((__u64)mddev->utime);
	sb->events = cpu_to_le64(mddev->events);
	if (mddev->in_sync)
		sb->resync_offset = cpu_to_le64(mddev->recovery_cp);
	else
		sb->resync_offset = cpu_to_le64(0);

	sb->cnt_corrected_read = cpu_to_le32(atomic_read(&rdev->corrected_errors));

	sb->raid_disks = cpu_to_le32(mddev->raid_disks);
	sb->size = cpu_to_le64(mddev->dev_sectors);
	sb->chunksize = cpu_to_le32(mddev->chunk_sectors);
	sb->level = cpu_to_le32(mddev->level);
	sb->layout = cpu_to_le32(mddev->layout);

	if (test_bit(WriteMostly, &rdev->flags))
		sb->devflags |= WriteMostly1;
	else
		sb->devflags &= ~WriteMostly1;
	sb->data_offset = cpu_to_le64(rdev->data_offset);
	sb->data_size = cpu_to_le64(rdev->sectors);

	if (mddev->bitmap && mddev->bitmap_info.file == NULL) {
		sb->bitmap_offset = cpu_to_le32((__u32)mddev->bitmap_info.offset);
		sb->feature_map = cpu_to_le32(MD_FEATURE_BITMAP_OFFSET);
	}

	if (rdev->raid_disk >= 0 &&
	    !test_bit(In_sync, &rdev->flags)) {
		sb->feature_map |=
			cpu_to_le32(MD_FEATURE_RECOVERY_OFFSET);
		sb->recovery_offset =
			cpu_to_le64(rdev->recovery_offset);
	}
	if (test_bit(Replacement, &rdev->flags))
		sb->feature_map |=
			cpu_to_le32(MD_FEATURE_REPLACEMENT);

	if (mddev->reshape_position != MaxSector) {
		sb->feature_map |= cpu_to_le32(MD_FEATURE_RESHAPE_ACTIVE);
		sb->reshape_position = cpu_to_le64(mddev->reshape_position);
		sb->new_layout = cpu_to_le32(mddev->new_layout);
		sb->delta_disks = cpu_to_le32(mddev->delta_disks);
		sb->new_level = cpu_to_le32(mddev->new_level);
		sb->new_chunk = cpu_to_le32(mddev->new_chunk_sectors);
		if (mddev->delta_disks == 0 &&
		    mddev->reshape_backwards)
			sb->feature_map
				|= cpu_to_le32(MD_FEATURE_RESHAPE_BACKWARDS);
		if (rdev->new_data_offset != rdev->data_offset) {
			sb->feature_map
				|= cpu_to_le32(MD_FEATURE_NEW_OFFSET);
			sb->new_offset = cpu_to_le32((__u32)(rdev->new_data_offset
							     - rdev->data_offset));
		}
	}

	if (rdev->badblocks.count == 0)
		/* Nothing to do for bad blocks*/ ;
	else if (sb->bblog_offset == 0)
		/* Cannot record bad blocks on this device */
		md_error(mddev, rdev);
	else {
		struct badblocks *bb = &rdev->badblocks;
		u64 *bbp = (u64 *)page_address(rdev->bb_page);
		u64 *p = bb->page;
		sb->feature_map |= cpu_to_le32(MD_FEATURE_BAD_BLOCKS);
		if (bb->changed) {
			unsigned seq;

retry:
			seq = read_seqbegin(&bb->lock);

			memset(bbp, 0xff, PAGE_SIZE);

			for (i = 0 ; i < bb->count ; i++) {
				u64 internal_bb = *p++;
				u64 store_bb = ((BB_OFFSET(internal_bb) << 10)
						| BB_LEN(internal_bb));
				*bbp++ = cpu_to_le64(store_bb);
			}
			bb->changed = 0;
			if (read_seqretry(&bb->lock, seq))
				goto retry;

			bb->sector = (rdev->sb_start +
				      (int)le32_to_cpu(sb->bblog_offset));
			bb->size = le16_to_cpu(sb->bblog_size);
		}
	}

	max_dev = 0;
	rdev_for_each(rdev2, mddev)
		if (rdev2->desc_nr+1 > max_dev)
			max_dev = rdev2->desc_nr+1;

	if (max_dev > le32_to_cpu(sb->max_dev)) {
		int bmask;
		sb->max_dev = cpu_to_le32(max_dev);
		rdev->sb_size = max_dev * 2 + 256;
		bmask = queue_logical_block_size(rdev->bdev->bd_disk->queue)-1;
		if (rdev->sb_size & bmask)
			rdev->sb_size = (rdev->sb_size | bmask) + 1;
	} else
		max_dev = le32_to_cpu(sb->max_dev);

	for (i=0; i<max_dev;i++)
		sb->dev_roles[i] = cpu_to_le16(0xfffe);
	
	rdev_for_each(rdev2, mddev) {
		i = rdev2->desc_nr;
		if (test_bit(Faulty, &rdev2->flags))
			sb->dev_roles[i] = cpu_to_le16(0xfffe);
		else if (test_bit(In_sync, &rdev2->flags))
			sb->dev_roles[i] = cpu_to_le16(rdev2->raid_disk);
		else if (rdev2->raid_disk >= 0)
			sb->dev_roles[i] = cpu_to_le16(rdev2->raid_disk);
		else
			sb->dev_roles[i] = cpu_to_le16(0xffff);
	}

	sb->sb_csum = calc_sb_1_csum(sb);
}

static unsigned long long
super_1_rdev_size_change(struct md_rdev *rdev, sector_t num_sectors)
{
	struct mdp_superblock_1 *sb;
	sector_t max_sectors;
	if (num_sectors && num_sectors < rdev->mddev->dev_sectors)
		return 0; /* component must fit device */
	if (rdev->data_offset != rdev->new_data_offset)
		return 0; /* too confusing */
	if (rdev->sb_start < rdev->data_offset) {
		/* minor versions 1 and 2; superblock before data */
		max_sectors = i_size_read(rdev->bdev->bd_inode) >> 9;
		max_sectors -= rdev->data_offset;
		if (!num_sectors || num_sectors > max_sectors)
			num_sectors = max_sectors;
	} else if (rdev->mddev->bitmap_info.offset) {
		/* minor version 0 with bitmap we can't move */
		return 0;
	} else {
		/* minor version 0; superblock after data */
		sector_t sb_start;
		sb_start = (i_size_read(rdev->bdev->bd_inode) >> 9) - 8*2;
		sb_start &= ~(sector_t)(4*2 - 1);
		max_sectors = rdev->sectors + sb_start - rdev->sb_start;
		if (!num_sectors || num_sectors > max_sectors)
			num_sectors = max_sectors;
		rdev->sb_start = sb_start;
	}
	sb = page_address(rdev->sb_page);
	sb->data_size = cpu_to_le64(num_sectors);
	sb->super_offset = rdev->sb_start;
	sb->sb_csum = calc_sb_1_csum(sb);
	md_super_write(rdev->mddev, rdev, rdev->sb_start, rdev->sb_size,
		       rdev->sb_page);
	md_super_wait(rdev->mddev);
	return num_sectors;

}

static int
super_1_allow_new_offset(struct md_rdev *rdev,
			 unsigned long long new_offset)
{
	/* All necessary checks on new >= old have been done */
	struct bitmap *bitmap;
	if (new_offset >= rdev->data_offset)
		return 1;

	/* with 1.0 metadata, there is no metadata to tread on
	 * so we can always move back */
	if (rdev->mddev->minor_version == 0)
		return 1;

	/* otherwise we must be sure not to step on
	 * any metadata, so stay:
	 * 36K beyond start of superblock
	 * beyond end of badblocks
	 * beyond write-intent bitmap
	 */
	if (rdev->sb_start + (32+4)*2 > new_offset)
		return 0;
	bitmap = rdev->mddev->bitmap;
	if (bitmap && !rdev->mddev->bitmap_info.file &&
	    rdev->sb_start + rdev->mddev->bitmap_info.offset +
	    bitmap->storage.file_pages * (PAGE_SIZE>>9) > new_offset)
		return 0;
	if (rdev->badblocks.sector + rdev->badblocks.size > new_offset)
		return 0;

	return 1;
}

static struct super_type super_types[] = {
	[0] = {
		.name	= "0.90.0",
		.owner	= THIS_MODULE,
		.load_super	    = super_90_load,
		.validate_super	    = super_90_validate,
		.sync_super	    = super_90_sync,
		.rdev_size_change   = super_90_rdev_size_change,
		.allow_new_offset   = super_90_allow_new_offset,
	},
	[1] = {
		.name	= "md-1",
		.owner	= THIS_MODULE,
		.load_super	    = super_1_load,
		.validate_super	    = super_1_validate,
		.sync_super	    = super_1_sync,
		.rdev_size_change   = super_1_rdev_size_change,
		.allow_new_offset   = super_1_allow_new_offset,
	},
};

static void sync_super(struct mddev *mddev, struct md_rdev *rdev)
{
	if (mddev->sync_super) {
		mddev->sync_super(mddev, rdev);
		return;
	}

	BUG_ON(mddev->major_version >= ARRAY_SIZE(super_types));

	super_types[mddev->major_version].sync_super(mddev, rdev);
}

static int match_mddev_units(struct mddev *mddev1, struct mddev *mddev2)
{
	struct md_rdev *rdev, *rdev2;

	rcu_read_lock();
	rdev_for_each_rcu(rdev, mddev1)
		rdev_for_each_rcu(rdev2, mddev2)
			if (rdev->bdev->bd_contains ==
			    rdev2->bdev->bd_contains) {
				rcu_read_unlock();
				return 1;
			}
	rcu_read_unlock();
	return 0;
}

static LIST_HEAD(pending_raid_disks);

/*
 * Try to register data integrity profile for an mddev
 *
 * This is called when an array is started and after a disk has been kicked
 * from the array. It only succeeds if all working and active component devices
 * are integrity capable with matching profiles.
 */
int md_integrity_register(struct mddev *mddev)
{
	struct md_rdev *rdev, *reference = NULL;

	if (list_empty(&mddev->disks))
		return 0; /* nothing to do */
	if (!mddev->gendisk || blk_get_integrity(mddev->gendisk))
		return 0; /* shouldn't register, or already is */
	rdev_for_each(rdev, mddev) {
		/* skip spares and non-functional disks */
		if (test_bit(Faulty, &rdev->flags))
			continue;
		if (rdev->raid_disk < 0)
			continue;
		if (!reference) {
			/* Use the first rdev as the reference */
			reference = rdev;
			continue;
		}
		/* does this rdev's profile match the reference profile? */
		if (blk_integrity_compare(reference->bdev->bd_disk,
				rdev->bdev->bd_disk) < 0)
			return -EINVAL;
	}
	if (!reference || !bdev_get_integrity(reference->bdev))
		return 0;
	/*
	 * All component devices are integrity capable and have matching
	 * profiles, register the common profile for the md device.
	 */
	if (blk_integrity_register(mddev->gendisk,
			bdev_get_integrity(reference->bdev)) != 0) {
		printk(KERN_ERR "md: failed to register integrity for %s\n",
			mdname(mddev));
		return -EINVAL;
	}
	printk(KERN_NOTICE "md: data integrity enabled on %s\n", mdname(mddev));
	if (bioset_integrity_create(mddev->bio_set, BIO_POOL_SIZE)) {
		printk(KERN_ERR "md: failed to create integrity pool for %s\n",
		       mdname(mddev));
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(md_integrity_register);

/* Disable data integrity if non-capable/non-matching disk is being added */
void md_integrity_add_rdev(struct md_rdev *rdev, struct mddev *mddev)
{
	struct blk_integrity *bi_rdev = bdev_get_integrity(rdev->bdev);
	struct blk_integrity *bi_mddev = blk_get_integrity(mddev->gendisk);

	if (!bi_mddev) /* nothing to do */
		return;
	if (rdev->raid_disk < 0) /* skip spares */
		return;
	if (bi_rdev && blk_integrity_compare(mddev->gendisk,
					     rdev->bdev->bd_disk) >= 0)
		return;
	printk(KERN_NOTICE "disabling data integrity on %s\n", mdname(mddev));
	blk_integrity_unregister(mddev->gendisk);
}
EXPORT_SYMBOL(md_integrity_add_rdev);

static int bind_rdev_to_array(struct md_rdev * rdev, struct mddev * mddev)
{
	char b[BDEVNAME_SIZE];
	struct kobject *ko;
	char *s;
	int err;

	if (rdev->mddev) {
		MD_BUG();
		return -EINVAL;
	}

	/* prevent duplicates */
	if (find_rdev(mddev, rdev->bdev->bd_dev))
		return -EEXIST;

	/* make sure rdev->sectors exceeds mddev->dev_sectors */
	if (rdev->sectors && (mddev->dev_sectors == 0 ||
			rdev->sectors < mddev->dev_sectors)) {
		if (mddev->pers) {
			/* Cannot change size, so fail
			 * If mddev->level <= 0, then we don't care
			 * about aligning sizes (e.g. linear)
			 */
			if (mddev->level > 0)
				return -ENOSPC;
		} else
			mddev->dev_sectors = rdev->sectors;
	}

	/* Verify rdev->desc_nr is unique.
	 * If it is -1, assign a free number, else
	 * check number is not in use
	 */
	if (rdev->desc_nr < 0) {
		int choice = 0;
		if (mddev->pers) choice = mddev->raid_disks;
		while (find_rdev_nr(mddev, choice))
			choice++;
		rdev->desc_nr = choice;
	} else {
		if (find_rdev_nr(mddev, rdev->desc_nr))
			return -EBUSY;
	}
	if (mddev->max_disks && rdev->desc_nr >= mddev->max_disks) {
		printk(KERN_WARNING "md: %s: array is limited to %d devices\n",
		       mdname(mddev), mddev->max_disks);
		return -EBUSY;
	}
	bdevname(rdev->bdev,b);
	while ( (s=strchr(b, '/')) != NULL)
		*s = '!';

	rdev->mddev = mddev;
	printk(KERN_INFO "md: bind<%s>\n", b);

	if ((err = kobject_add(&rdev->kobj, &mddev->kobj, "dev-%s", b)))
		goto fail;

	ko = &part_to_dev(rdev->bdev->bd_part)->kobj;
	if (sysfs_create_link(&rdev->kobj, ko, "block"))
		/* failure here is OK */;
	rdev->sysfs_state = sysfs_get_dirent_safe(rdev->kobj.sd, "state");

	list_add_rcu(&rdev->same_set, &mddev->disks);
	bd_link_disk_holder(rdev->bdev, mddev->gendisk);

	/* May as well allow recovery to be retried once */
	mddev->recovery_disabled++;

	return 0;

 fail:
	printk(KERN_WARNING "md: failed to register dev-%s for %s\n",
	       b, mdname(mddev));
	return err;
}

static void md_delayed_delete(struct work_struct *ws)
{
	struct md_rdev *rdev = container_of(ws, struct md_rdev, del_work);
	kobject_del(&rdev->kobj);
	kobject_put(&rdev->kobj);
}

static void unbind_rdev_from_array(struct md_rdev * rdev)
{
	char b[BDEVNAME_SIZE];
	if (!rdev->mddev) {
		MD_BUG();
		return;
	}
	bd_unlink_disk_holder(rdev->bdev, rdev->mddev->gendisk);
	list_del_rcu(&rdev->same_set);
	printk(KERN_INFO "md: unbind<%s>\n", bdevname(rdev->bdev,b));
	rdev->mddev = NULL;
	sysfs_remove_link(&rdev->kobj, "block");
	sysfs_put(rdev->sysfs_state);
	rdev->sysfs_state = NULL;
	rdev->badblocks.count = 0;
	/* We need to delay this, otherwise we can deadlock when
	 * writing to 'remove' to "dev/state".  We also need
	 * to delay it due to rcu usage.
	 */
	synchronize_rcu();
	INIT_WORK(&rdev->del_work, md_delayed_delete);
	kobject_get(&rdev->kobj);
	queue_work(md_misc_wq, &rdev->del_work);
}

/*
 * prevent the device from being mounted, repartitioned or
 * otherwise reused by a RAID array (or any other kernel
 * subsystem), by bd_claiming the device.
 */
static int lock_rdev(struct md_rdev *rdev, dev_t dev, int shared)
{
	int err = 0;
	struct block_device *bdev;
	char b[BDEVNAME_SIZE];

	bdev = blkdev_get_by_dev(dev, FMODE_READ|FMODE_WRITE|FMODE_EXCL,
				 shared ? (struct md_rdev *)lock_rdev : rdev);
	if (IS_ERR(bdev)) {
		printk(KERN_ERR "md: could not open %s.\n",
			__bdevname(dev, b));
		return PTR_ERR(bdev);
	}
	rdev->bdev = bdev;
	return err;
}

static void unlock_rdev(struct md_rdev *rdev)
{
	struct block_device *bdev = rdev->bdev;
	rdev->bdev = NULL;
	if (!bdev)
		MD_BUG();
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
}

void md_autodetect_dev(dev_t dev);

static void export_rdev(struct md_rdev * rdev)
{
	char b[BDEVNAME_SIZE];
	printk(KERN_INFO "md: export_rdev(%s)\n",
		bdevname(rdev->bdev,b));
	if (rdev->mddev)
		MD_BUG();
	md_rdev_clear(rdev);
#ifndef MODULE
	if (test_bit(AutoDetected, &rdev->flags))
		md_autodetect_dev(rdev->bdev->bd_dev);
#endif
	unlock_rdev(rdev);
	kobject_put(&rdev->kobj);
}

static void kick_rdev_from_array(struct md_rdev * rdev)
{
	unbind_rdev_from_array(rdev);
	export_rdev(rdev);
}

static void export_array(struct mddev *mddev)
{
	struct md_rdev *rdev, *tmp;

	rdev_for_each_safe(rdev, tmp, mddev) {
		if (!rdev->mddev) {
			MD_BUG();
			continue;
		}
		kick_rdev_from_array(rdev);
	}
	if (!list_empty(&mddev->disks))
		MD_BUG();
	mddev->raid_disks = 0;
	mddev->major_version = 0;
}

static void print_desc(mdp_disk_t *desc)
{
	printk(" DISK<N:%d,(%d,%d),R:%d,S:%d>\n", desc->number,
		desc->major,desc->minor,desc->raid_disk,desc->state);
}

static void print_sb_90(mdp_super_t *sb)
{
	int i;

	printk(KERN_INFO 
		"md:  SB: (V:%d.%d.%d) ID:<%08x.%08x.%08x.%08x> CT:%08x\n",
		sb->major_version, sb->minor_version, sb->patch_version,
		sb->set_uuid0, sb->set_uuid1, sb->set_uuid2, sb->set_uuid3,
		sb->ctime);
	printk(KERN_INFO "md:     L%d S%08d ND:%d RD:%d md%d LO:%d CS:%d\n",
		sb->level, sb->size, sb->nr_disks, sb->raid_disks,
		sb->md_minor, sb->layout, sb->chunk_size);
	printk(KERN_INFO "md:     UT:%08x ST:%d AD:%d WD:%d"
		" FD:%d SD:%d CSUM:%08x E:%08lx\n",
		sb->utime, sb->state, sb->active_disks, sb->working_disks,
		sb->failed_disks, sb->spare_disks,
		sb->sb_csum, (unsigned long)sb->events_lo);

	printk(KERN_INFO);
	for (i = 0; i < MD_SB_DISKS; i++) {
		mdp_disk_t *desc;

		desc = sb->disks + i;
		if (desc->number || desc->major || desc->minor ||
		    desc->raid_disk || (desc->state && (desc->state != 4))) {
			printk("     D %2d: ", i);
			print_desc(desc);
		}
	}
	printk(KERN_INFO "md:     THIS: ");
	print_desc(&sb->this_disk);
}

static void print_sb_1(struct mdp_superblock_1 *sb)
{
	__u8 *uuid;

	uuid = sb->set_uuid;
	printk(KERN_INFO
	       "md:  SB: (V:%u) (F:0x%08x) Array-ID:<%pU>\n"
	       "md:    Name: \"%s\" CT:%llu\n",
		le32_to_cpu(sb->major_version),
		le32_to_cpu(sb->feature_map),
		uuid,
		sb->set_name,
		(unsigned long long)le64_to_cpu(sb->ctime)
		       & MD_SUPERBLOCK_1_TIME_SEC_MASK);

	uuid = sb->device_uuid;
	printk(KERN_INFO
	       "md:       L%u SZ%llu RD:%u LO:%u CS:%u DO:%llu DS:%llu SO:%llu"
			" RO:%llu\n"
	       "md:     Dev:%08x UUID: %pU\n"
	       "md:       (F:0x%08x) UT:%llu Events:%llu ResyncOffset:%llu CSUM:0x%08x\n"
	       "md:         (MaxDev:%u) \n",
		le32_to_cpu(sb->level),
		(unsigned long long)le64_to_cpu(sb->size),
		le32_to_cpu(sb->raid_disks),
		le32_to_cpu(sb->layout),
		le32_to_cpu(sb->chunksize),
		(unsigned long long)le64_to_cpu(sb->data_offset),
		(unsigned long long)le64_to_cpu(sb->data_size),
		(unsigned long long)le64_to_cpu(sb->super_offset),
		(unsigned long long)le64_to_cpu(sb->recovery_offset),
		le32_to_cpu(sb->dev_number),
		uuid,
		sb->devflags,
		(unsigned long long)le64_to_cpu(sb->utime) & MD_SUPERBLOCK_1_TIME_SEC_MASK,
		(unsigned long long)le64_to_cpu(sb->events),
		(unsigned long long)le64_to_cpu(sb->resync_offset),
		le32_to_cpu(sb->sb_csum),
		le32_to_cpu(sb->max_dev)
		);
}

static void print_rdev(struct md_rdev *rdev, int major_version)
{
	char b[BDEVNAME_SIZE];
	printk(KERN_INFO "md: rdev %s, Sect:%08llu F:%d S:%d DN:%u\n",
		bdevname(rdev->bdev, b), (unsigned long long)rdev->sectors,
	        test_bit(Faulty, &rdev->flags), test_bit(In_sync, &rdev->flags),
	        rdev->desc_nr);
	if (rdev->sb_loaded) {
		printk(KERN_INFO "md: rdev superblock (MJ:%d):\n", major_version);
		switch (major_version) {
		case 0:
			print_sb_90(page_address(rdev->sb_page));
			break;
		case 1:
			print_sb_1(page_address(rdev->sb_page));
			break;
		}
	} else
		printk(KERN_INFO "md: no rdev superblock!\n");
}

static void md_print_devices(void)
{
	struct list_head *tmp;
	struct md_rdev *rdev;
	struct mddev *mddev;
	char b[BDEVNAME_SIZE];

	printk("\n");
	printk("md:	**********************************\n");
	printk("md:	* <COMPLETE RAID STATE PRINTOUT> *\n");
	printk("md:	**********************************\n");
	for_each_mddev(mddev, tmp) {

		if (mddev->bitmap)
			bitmap_print_sb(mddev->bitmap);
		else
			printk("%s: ", mdname(mddev));
		rdev_for_each(rdev, mddev)
			printk("<%s>", bdevname(rdev->bdev,b));
		printk("\n");

		rdev_for_each(rdev, mddev)
			print_rdev(rdev, mddev->major_version);
	}
	printk("md:	**********************************\n");
	printk("\n");
}


static void sync_sbs(struct mddev * mddev, int nospares)
{
	/* Update each superblock (in-memory image), but
	 * if we are allowed to, skip spares which already
	 * have the right event counter, or have one earlier
	 * (which would mean they aren't being marked as dirty
	 * with the rest of the array)
	 */
	struct md_rdev *rdev;
	rdev_for_each(rdev, mddev) {
		if (rdev->sb_events == mddev->events ||
		    (nospares &&
		     rdev->raid_disk < 0 &&
		     rdev->sb_events+1 == mddev->events)) {
			/* Don't update this superblock */
			rdev->sb_loaded = 2;
		} else {
			sync_super(mddev, rdev);
			rdev->sb_loaded = 1;
		}
	}
}

static void md_update_sb(struct mddev * mddev, int force_change)
{
	struct md_rdev *rdev;
	int sync_req;
	int nospares = 0;
	int any_badblocks_changed = 0;

repeat:
	/* First make sure individual recovery_offsets are correct */
	rdev_for_each(rdev, mddev) {
		if (rdev->raid_disk >= 0 &&
		    mddev->delta_disks >= 0 &&
		    !test_bit(In_sync, &rdev->flags) &&
		    mddev->curr_resync_completed > rdev->recovery_offset)
				rdev->recovery_offset = mddev->curr_resync_completed;

	}	
	if (!mddev->persistent) {
		clear_bit(MD_CHANGE_CLEAN, &mddev->flags);
		clear_bit(MD_CHANGE_DEVS, &mddev->flags);
		if (!mddev->external) {
			clear_bit(MD_CHANGE_PENDING, &mddev->flags);
			rdev_for_each(rdev, mddev) {
				if (rdev->badblocks.changed) {
					rdev->badblocks.changed = 0;
					md_ack_all_badblocks(&rdev->badblocks);
					md_error(mddev, rdev);
				}
				clear_bit(Blocked, &rdev->flags);
				clear_bit(BlockedBadBlocks, &rdev->flags);
				wake_up(&rdev->blocked_wait);
			}
		}
		wake_up(&mddev->sb_wait);
		return;
	}

	spin_lock_irq(&mddev->write_lock);

	mddev->utime = get_seconds();

	if (test_and_clear_bit(MD_CHANGE_DEVS, &mddev->flags))
		force_change = 1;
	if (test_and_clear_bit(MD_CHANGE_CLEAN, &mddev->flags))
		/* just a clean<-> dirty transition, possibly leave spares alone,
		 * though if events isn't the right even/odd, we will have to do
		 * spares after all
		 */
		nospares = 1;
	if (force_change)
		nospares = 0;
	if (mddev->degraded)
		/* If the array is degraded, then skipping spares is both
		 * dangerous and fairly pointless.
		 * Dangerous because a device that was removed from the array
		 * might have a event_count that still looks up-to-date,
		 * so it can be re-added without a resync.
		 * Pointless because if there are any spares to skip,
		 * then a recovery will happen and soon that array won't
		 * be degraded any more and the spare can go back to sleep then.
		 */
		nospares = 0;

	sync_req = mddev->in_sync;

	/* If this is just a dirty<->clean transition, and the array is clean
	 * and 'events' is odd, we can roll back to the previous clean state */
	if (nospares
	    && (mddev->in_sync && mddev->recovery_cp == MaxSector)
	    && mddev->can_decrease_events
	    && mddev->events != 1) {
		mddev->events--;
		mddev->can_decrease_events = 0;
	} else {
		/* otherwise we have to go forward and ... */
		mddev->events ++;
		mddev->can_decrease_events = nospares;
	}

	if (!mddev->events) {
		/*
		 * oops, this 64-bit counter should never wrap.
		 * Either we are in around ~1 trillion A.C., assuming
		 * 1 reboot per second, or we have a bug:
		 */
		MD_BUG();
		mddev->events --;
	}

	rdev_for_each(rdev, mddev) {
		if (rdev->badblocks.changed)
			any_badblocks_changed++;
		if (test_bit(Faulty, &rdev->flags))
			set_bit(FaultRecorded, &rdev->flags);
	}

	sync_sbs(mddev, nospares);
	spin_unlock_irq(&mddev->write_lock);

	pr_debug("md: updating %s RAID superblock on device (in sync %d)\n",
		 mdname(mddev), mddev->in_sync);

	bitmap_update_sb(mddev->bitmap);
	rdev_for_each(rdev, mddev) {
		char b[BDEVNAME_SIZE];

		if (rdev->sb_loaded != 1)
			continue; /* no noise on spare devices */

		if (!test_bit(Faulty, &rdev->flags) &&
		    rdev->saved_raid_disk == -1) {
			md_super_write(mddev,rdev,
				       rdev->sb_start, rdev->sb_size,
				       rdev->sb_page);
			pr_debug("md: (write) %s's sb offset: %llu\n",
				 bdevname(rdev->bdev, b),
				 (unsigned long long)rdev->sb_start);
			rdev->sb_events = mddev->events;
			if (rdev->badblocks.size) {
				md_super_write(mddev, rdev,
					       rdev->badblocks.sector,
					       rdev->badblocks.size << 9,
					       rdev->bb_page);
				rdev->badblocks.size = 0;
			}

		} else if (test_bit(Faulty, &rdev->flags))
			pr_debug("md: %s (skipping faulty)\n",
				 bdevname(rdev->bdev, b));
		else
			pr_debug("(skipping incremental s/r ");

		if (mddev->level == LEVEL_MULTIPATH)
			/* only need to write one superblock... */
			break;
	}
	md_super_wait(mddev);
	/* if there was a failure, MD_CHANGE_DEVS was set, and we re-write super */

	spin_lock_irq(&mddev->write_lock);
	if (mddev->in_sync != sync_req ||
	    test_bit(MD_CHANGE_DEVS, &mddev->flags)) {
		/* have to write it out again */
		spin_unlock_irq(&mddev->write_lock);
		goto repeat;
	}
	clear_bit(MD_CHANGE_PENDING, &mddev->flags);
	spin_unlock_irq(&mddev->write_lock);
	wake_up(&mddev->sb_wait);
	if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
		sysfs_notify(&mddev->kobj, NULL, "sync_completed");

	rdev_for_each(rdev, mddev) {
		if (test_and_clear_bit(FaultRecorded, &rdev->flags))
			clear_bit(Blocked, &rdev->flags);

		if (any_badblocks_changed)
			md_ack_all_badblocks(&rdev->badblocks);
		clear_bit(BlockedBadBlocks, &rdev->flags);
		wake_up(&rdev->blocked_wait);
	}
}

/* words written to sysfs files may, or may not, be \n terminated.
 * We want to accept with case. For this we use cmd_match.
 */
static int cmd_match(const char *cmd, const char *str)
{
	/* See if cmd, written into a sysfs file, matches
	 * str.  They must either be the same, or cmd can
	 * have a trailing newline
	 */
	while (*cmd && *str && *cmd == *str) {
		cmd++;
		str++;
	}
	if (*cmd == '\n')
		cmd++;
	if (*str || *cmd)
		return 0;
	return 1;
}

struct rdev_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct md_rdev *, char *);
	ssize_t (*store)(struct md_rdev *, const char *, size_t);
};

static ssize_t
state_show(struct md_rdev *rdev, char *page)
{
	char *sep = "";
	size_t len = 0;

	if (test_bit(Faulty, &rdev->flags) ||
	    rdev->badblocks.unacked_exist) {
		len+= sprintf(page+len, "%sfaulty",sep);
		sep = ",";
	}
	if (test_bit(In_sync, &rdev->flags)) {
		len += sprintf(page+len, "%sin_sync",sep);
		sep = ",";
	}
	if (test_bit(WriteMostly, &rdev->flags)) {
		len += sprintf(page+len, "%swrite_mostly",sep);
		sep = ",";
	}
	if (test_bit(Blocked, &rdev->flags) ||
	    (rdev->badblocks.unacked_exist
	     && !test_bit(Faulty, &rdev->flags))) {
		len += sprintf(page+len, "%sblocked", sep);
		sep = ",";
	}
	if (!test_bit(Faulty, &rdev->flags) &&
	    !test_bit(In_sync, &rdev->flags)) {
		len += sprintf(page+len, "%sspare", sep);
		sep = ",";
	}
	if (test_bit(WriteErrorSeen, &rdev->flags)) {
		len += sprintf(page+len, "%swrite_error", sep);
		sep = ",";
	}
	if (test_bit(WantReplacement, &rdev->flags)) {
		len += sprintf(page+len, "%swant_replacement", sep);
		sep = ",";
	}
	if (test_bit(Replacement, &rdev->flags)) {
		len += sprintf(page+len, "%sreplacement", sep);
		sep = ",";
	}

	return len+sprintf(page+len, "\n");
}

static ssize_t
state_store(struct md_rdev *rdev, const char *buf, size_t len)
{
	/* can write
	 *  faulty  - simulates an error
	 *  remove  - disconnects the device
	 *  writemostly - sets write_mostly
	 *  -writemostly - clears write_mostly
	 *  blocked - sets the Blocked flags
	 *  -blocked - clears the Blocked and possibly simulates an error
	 *  insync - sets Insync providing device isn't active
	 *  write_error - sets WriteErrorSeen
	 *  -write_error - clears WriteErrorSeen
	 */
	int err = -EINVAL;
	if (cmd_match(buf, "faulty") && rdev->mddev->pers) {
		md_error(rdev->mddev, rdev);
		if (test_bit(Faulty, &rdev->flags))
			err = 0;
		else
			err = -EBUSY;
	} else if (cmd_match(buf, "remove")) {
		if (rdev->raid_disk >= 0)
			err = -EBUSY;
		else {
			struct mddev *mddev = rdev->mddev;
			kick_rdev_from_array(rdev);
			if (mddev->pers)
				md_update_sb(mddev, 1);
			md_new_event(mddev);
			err = 0;
		}
	} else if (cmd_match(buf, "writemostly")) {
		set_bit(WriteMostly, &rdev->flags);
		err = 0;
	} else if (cmd_match(buf, "-writemostly")) {
		clear_bit(WriteMostly, &rdev->flags);
		err = 0;
	} else if (cmd_match(buf, "blocked")) {
		set_bit(Blocked, &rdev->flags);
		err = 0;
	} else if (cmd_match(buf, "-blocked")) {
		if (!test_bit(Faulty, &rdev->flags) &&
		    rdev->badblocks.unacked_exist) {
			/* metadata handler doesn't understand badblocks,
			 * so we need to fail the device
			 */
			md_error(rdev->mddev, rdev);
		}
		clear_bit(Blocked, &rdev->flags);
		clear_bit(BlockedBadBlocks, &rdev->flags);
		wake_up(&rdev->blocked_wait);
		set_bit(MD_RECOVERY_NEEDED, &rdev->mddev->recovery);
		md_wakeup_thread(rdev->mddev->thread);

		err = 0;
	} else if (cmd_match(buf, "insync") && rdev->raid_disk == -1) {
		set_bit(In_sync, &rdev->flags);
		err = 0;
	} else if (cmd_match(buf, "write_error")) {
		set_bit(WriteErrorSeen, &rdev->flags);
		err = 0;
	} else if (cmd_match(buf, "-write_error")) {
		clear_bit(WriteErrorSeen, &rdev->flags);
		err = 0;
	} else if (cmd_match(buf, "want_replacement")) {
		/* Any non-spare device that is not a replacement can
		 * become want_replacement at any time, but we then need to
		 * check if recovery is needed.
		 */
		if (rdev->raid_disk >= 0 &&
		    !test_bit(Replacement, &rdev->flags))
			set_bit(WantReplacement, &rdev->flags);
		set_bit(MD_RECOVERY_NEEDED, &rdev->mddev->recovery);
		md_wakeup_thread(rdev->mddev->thread);
		err = 0;
	} else if (cmd_match(buf, "-want_replacement")) {
		/* Clearing 'want_replacement' is always allowed.
		 * Once replacements starts it is too late though.
		 */
		err = 0;
		clear_bit(WantReplacement, &rdev->flags);
	} else if (cmd_match(buf, "replacement")) {
		/* Can only set a device as a replacement when array has not
		 * yet been started.  Once running, replacement is automatic
		 * from spares, or by assigning 'slot'.
		 */
		if (rdev->mddev->pers)
			err = -EBUSY;
		else {
			set_bit(Replacement, &rdev->flags);
			err = 0;
		}
	} else if (cmd_match(buf, "-replacement")) {
		/* Similarly, can only clear Replacement before start */
		if (rdev->mddev->pers)
			err = -EBUSY;
		else {
			clear_bit(Replacement, &rdev->flags);
			err = 0;
		}
	}
	if (!err)
		sysfs_notify_dirent_safe(rdev->sysfs_state);
	return err ? err : len;
}
static struct rdev_sysfs_entry rdev_state =
__ATTR(state, S_IRUGO|S_IWUSR, state_show, state_store);

static ssize_t
errors_show(struct md_rdev *rdev, char *page)
{
	return sprintf(page, "%d\n", atomic_read(&rdev->corrected_errors));
}

static ssize_t
errors_store(struct md_rdev *rdev, const char *buf, size_t len)
{
	char *e;
	unsigned long n = simple_strtoul(buf, &e, 10);
	if (*buf && (*e == 0 || *e == '\n')) {
		atomic_set(&rdev->corrected_errors, n);
		return len;
	}
	return -EINVAL;
}
static struct rdev_sysfs_entry rdev_errors =
__ATTR(errors, S_IRUGO|S_IWUSR, errors_show, errors_store);

static ssize_t
slot_show(struct md_rdev *rdev, char *page)
{
	if (rdev->raid_disk < 0)
		return sprintf(page, "none\n");
	else
		return sprintf(page, "%d\n", rdev->raid_disk);
}

static ssize_t
slot_store(struct md_rdev *rdev, const char *buf, size_t len)
{
	char *e;
	int err;
	int slot = simple_strtoul(buf, &e, 10);
	if (strncmp(buf, "none", 4)==0)
		slot = -1;
	else if (e==buf || (*e && *e!= '\n'))
		return -EINVAL;
	if (rdev->mddev->pers && slot == -1) {
		/* Setting 'slot' on an active array requires also
		 * updating the 'rd%d' link, and communicating
		 * with the personality with ->hot_*_disk.
		 * For now we only support removing
		 * failed/spare devices.  This normally happens automatically,
		 * but not when the metadata is externally managed.
		 */
		if (rdev->raid_disk == -1)
			return -EEXIST;
		/* personality does all needed checks */
		if (rdev->mddev->pers->hot_remove_disk == NULL)
			return -EINVAL;
		err = rdev->mddev->pers->
			hot_remove_disk(rdev->mddev, rdev);
		if (err)
			return err;
		sysfs_unlink_rdev(rdev->mddev, rdev);
		rdev->raid_disk = -1;
		set_bit(MD_RECOVERY_NEEDED, &rdev->mddev->recovery);
		md_wakeup_thread(rdev->mddev->thread);
	} else if (rdev->mddev->pers) {
		/* Activating a spare .. or possibly reactivating
		 * if we ever get bitmaps working here.
		 */

		if (rdev->raid_disk != -1)
			return -EBUSY;

		if (test_bit(MD_RECOVERY_RUNNING, &rdev->mddev->recovery))
			return -EBUSY;

		if (rdev->mddev->pers->hot_add_disk == NULL)
			return -EINVAL;

		if (slot >= rdev->mddev->raid_disks &&
		    slot >= rdev->mddev->raid_disks + rdev->mddev->delta_disks)
			return -ENOSPC;

		rdev->raid_disk = slot;
		if (test_bit(In_sync, &rdev->flags))
			rdev->saved_raid_disk = slot;
		else
			rdev->saved_raid_disk = -1;
		clear_bit(In_sync, &rdev->flags);
		err = rdev->mddev->pers->
			hot_add_disk(rdev->mddev, rdev);
		if (err) {
			rdev->raid_disk = -1;
			return err;
		} else
			sysfs_notify_dirent_safe(rdev->sysfs_state);
		if (sysfs_link_rdev(rdev->mddev, rdev))
			/* failure here is OK */;
		/* don't wakeup anyone, leave that to userspace. */
	} else {
		if (slot >= rdev->mddev->raid_disks &&
		    slot >= rdev->mddev->raid_disks + rdev->mddev->delta_disks)
			return -ENOSPC;
		rdev->raid_disk = slot;
		/* assume it is working */
		clear_bit(Faulty, &rdev->flags);
		clear_bit(WriteMostly, &rdev->flags);
		set_bit(In_sync, &rdev->flags);
		sysfs_notify_dirent_safe(rdev->sysfs_state);
	}
	return len;
}


static struct rdev_sysfs_entry rdev_slot =
__ATTR(slot, S_IRUGO|S_IWUSR, slot_show, slot_store);

static ssize_t
offset_show(struct md_rdev *rdev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)rdev->data_offset);
}

static ssize_t
offset_store(struct md_rdev *rdev, const char *buf, size_t len)
{
	unsigned long long offset;
	if (strict_strtoull(buf, 10, &offset) < 0)
		return -EINVAL;
	if (rdev->mddev->pers && rdev->raid_disk >= 0)
		return -EBUSY;
	if (rdev->sectors && rdev->mddev->external)
		/* Must set offset before size, so overlap checks
		 * can be sane */
		return -EBUSY;
	rdev->data_offset = offset;
	return len;
}

static struct rdev_sysfs_entry rdev_offset =
__ATTR(offset, S_IRUGO|S_IWUSR, offset_show, offset_store);

static ssize_t new_offset_show(struct md_rdev *rdev, char *page)
{
	return sprintf(page, "%llu\n",
		       (unsigned long long)rdev->new_data_offset);
}

static ssize_t new_offset_store(struct md_rdev *rdev,
				const char *buf, size_t len)
{
	unsigned long long new_offset;
	struct mddev *mddev = rdev->mddev;

	if (strict_strtoull(buf, 10, &new_offset) < 0)
		return -EINVAL;

	if (mddev->sync_thread)
		return -EBUSY;
	if (new_offset == rdev->data_offset)
		/* reset is always permitted */
		;
	else if (new_offset > rdev->data_offset) {
		/* must not push array size beyond rdev_sectors */
		if (new_offset - rdev->data_offset
		    + mddev->dev_sectors > rdev->sectors)
				return -E2BIG;
	}
	/* Metadata worries about other space details. */

	/* decreasing the offset is inconsistent with a backwards
	 * reshape.
	 */
	if (new_offset < rdev->data_offset &&
	    mddev->reshape_backwards)
		return -EINVAL;
	/* Increasing offset is inconsistent with forwards
	 * reshape.  reshape_direction should be set to
	 * 'backwards' first.
	 */
	if (new_offset > rdev->data_offset &&
	    !mddev->reshape_backwards)
		return -EINVAL;

	if (mddev->pers && mddev->persistent &&
	    !super_types[mddev->major_version]
	    .allow_new_offset(rdev, new_offset))
		return -E2BIG;
	rdev->new_data_offset = new_offset;
	if (new_offset > rdev->data_offset)
		mddev->reshape_backwards = 1;
	else if (new_offset < rdev->data_offset)
		mddev->reshape_backwards = 0;

	return len;
}
static struct rdev_sysfs_entry rdev_new_offset =
__ATTR(new_offset, S_IRUGO|S_IWUSR, new_offset_show, new_offset_store);

static ssize_t
rdev_size_show(struct md_rdev *rdev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)rdev->sectors / 2);
}

static int overlaps(sector_t s1, sector_t l1, sector_t s2, sector_t l2)
{
	/* check if two start/length pairs overlap */
	if (s1+l1 <= s2)
		return 0;
	if (s2+l2 <= s1)
		return 0;
	return 1;
}

static int strict_blocks_to_sectors(const char *buf, sector_t *sectors)
{
	unsigned long long blocks;
	sector_t new;

	if (strict_strtoull(buf, 10, &blocks) < 0)
		return -EINVAL;

	if (blocks & 1ULL << (8 * sizeof(blocks) - 1))
		return -EINVAL; /* sector conversion overflow */

	new = blocks * 2;
	if (new != blocks * 2)
		return -EINVAL; /* unsigned long long to sector_t overflow */

	*sectors = new;
	return 0;
}

static ssize_t
rdev_size_store(struct md_rdev *rdev, const char *buf, size_t len)
{
	struct mddev *my_mddev = rdev->mddev;
	sector_t oldsectors = rdev->sectors;
	sector_t sectors;

	if (strict_blocks_to_sectors(buf, &sectors) < 0)
		return -EINVAL;
	if (rdev->data_offset != rdev->new_data_offset)
		return -EINVAL; /* too confusing */
	if (my_mddev->pers && rdev->raid_disk >= 0) {
		if (my_mddev->persistent) {
			sectors = super_types[my_mddev->major_version].
				rdev_size_change(rdev, sectors);
			if (!sectors)
				return -EBUSY;
		} else if (!sectors)
			sectors = (i_size_read(rdev->bdev->bd_inode) >> 9) -
				rdev->data_offset;
	}
	if (sectors < my_mddev->dev_sectors)
		return -EINVAL; /* component must fit device */

	rdev->sectors = sectors;
	if (sectors > oldsectors && my_mddev->external) {
		/* need to check that all other rdevs with the same ->bdev
		 * do not overlap.  We need to unlock the mddev to avoid
		 * a deadlock.  We have already changed rdev->sectors, and if
		 * we have to change it back, we will have the lock again.
		 */
		struct mddev *mddev;
		int overlap = 0;
		struct list_head *tmp;

		mddev_unlock(my_mddev);
		for_each_mddev(mddev, tmp) {
			struct md_rdev *rdev2;

			mddev_lock(mddev);
			rdev_for_each(rdev2, mddev)
				if (rdev->bdev == rdev2->bdev &&
				    rdev != rdev2 &&
				    overlaps(rdev->data_offset, rdev->sectors,
					     rdev2->data_offset,
					     rdev2->sectors)) {
					overlap = 1;
					break;
				}
			mddev_unlock(mddev);
			if (overlap) {
				mddev_put(mddev);
				break;
			}
		}
		mddev_lock(my_mddev);
		if (overlap) {
			/* Someone else could have slipped in a size
			 * change here, but doing so is just silly.
			 * We put oldsectors back because we *know* it is
			 * safe, and trust userspace not to race with
			 * itself
			 */
			rdev->sectors = oldsectors;
			return -EBUSY;
		}
	}
	return len;
}

static struct rdev_sysfs_entry rdev_size =
__ATTR(size, S_IRUGO|S_IWUSR, rdev_size_show, rdev_size_store);


static ssize_t recovery_start_show(struct md_rdev *rdev, char *page)
{
	unsigned long long recovery_start = rdev->recovery_offset;

	if (test_bit(In_sync, &rdev->flags) ||
	    recovery_start == MaxSector)
		return sprintf(page, "none\n");

	return sprintf(page, "%llu\n", recovery_start);
}

static ssize_t recovery_start_store(struct md_rdev *rdev, const char *buf, size_t len)
{
	unsigned long long recovery_start;

	if (cmd_match(buf, "none"))
		recovery_start = MaxSector;
	else if (strict_strtoull(buf, 10, &recovery_start))
		return -EINVAL;

	if (rdev->mddev->pers &&
	    rdev->raid_disk >= 0)
		return -EBUSY;

	rdev->recovery_offset = recovery_start;
	if (recovery_start == MaxSector)
		set_bit(In_sync, &rdev->flags);
	else
		clear_bit(In_sync, &rdev->flags);
	return len;
}

static struct rdev_sysfs_entry rdev_recovery_start =
__ATTR(recovery_start, S_IRUGO|S_IWUSR, recovery_start_show, recovery_start_store);


static ssize_t
badblocks_show(struct badblocks *bb, char *page, int unack);
static ssize_t
badblocks_store(struct badblocks *bb, const char *page, size_t len, int unack);

static ssize_t bb_show(struct md_rdev *rdev, char *page)
{
	return badblocks_show(&rdev->badblocks, page, 0);
}
static ssize_t bb_store(struct md_rdev *rdev, const char *page, size_t len)
{
	int rv = badblocks_store(&rdev->badblocks, page, len, 0);
	/* Maybe that ack was all we needed */
	if (test_and_clear_bit(BlockedBadBlocks, &rdev->flags))
		wake_up(&rdev->blocked_wait);
	return rv;
}
static struct rdev_sysfs_entry rdev_bad_blocks =
__ATTR(bad_blocks, S_IRUGO|S_IWUSR, bb_show, bb_store);


static ssize_t ubb_show(struct md_rdev *rdev, char *page)
{
	return badblocks_show(&rdev->badblocks, page, 1);
}
static ssize_t ubb_store(struct md_rdev *rdev, const char *page, size_t len)
{
	return badblocks_store(&rdev->badblocks, page, len, 1);
}
static struct rdev_sysfs_entry rdev_unack_bad_blocks =
__ATTR(unacknowledged_bad_blocks, S_IRUGO|S_IWUSR, ubb_show, ubb_store);

static struct attribute *rdev_default_attrs[] = {
	&rdev_state.attr,
	&rdev_errors.attr,
	&rdev_slot.attr,
	&rdev_offset.attr,
	&rdev_new_offset.attr,
	&rdev_size.attr,
	&rdev_recovery_start.attr,
	&rdev_bad_blocks.attr,
	&rdev_unack_bad_blocks.attr,
	NULL,
};
static ssize_t
rdev_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct rdev_sysfs_entry *entry = container_of(attr, struct rdev_sysfs_entry, attr);
	struct md_rdev *rdev = container_of(kobj, struct md_rdev, kobj);
	struct mddev *mddev = rdev->mddev;
	ssize_t rv;

	if (!entry->show)
		return -EIO;

	rv = mddev ? mddev_lock(mddev) : -EBUSY;
	if (!rv) {
		if (rdev->mddev == NULL)
			rv = -EBUSY;
		else
			rv = entry->show(rdev, page);
		mddev_unlock(mddev);
	}
	return rv;
}

static ssize_t
rdev_attr_store(struct kobject *kobj, struct attribute *attr,
	      const char *page, size_t length)
{
	struct rdev_sysfs_entry *entry = container_of(attr, struct rdev_sysfs_entry, attr);
	struct md_rdev *rdev = container_of(kobj, struct md_rdev, kobj);
	ssize_t rv;
	struct mddev *mddev = rdev->mddev;

	if (!entry->store)
		return -EIO;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	rv = mddev ? mddev_lock(mddev): -EBUSY;
	if (!rv) {
		if (rdev->mddev == NULL)
			rv = -EBUSY;
		else
			rv = entry->store(rdev, page, length);
		mddev_unlock(mddev);
	}
	return rv;
}

static void rdev_free(struct kobject *ko)
{
	struct md_rdev *rdev = container_of(ko, struct md_rdev, kobj);
	kfree(rdev);
}
static const struct sysfs_ops rdev_sysfs_ops = {
	.show		= rdev_attr_show,
	.store		= rdev_attr_store,
};
static struct kobj_type rdev_ktype = {
	.release	= rdev_free,
	.sysfs_ops	= &rdev_sysfs_ops,
	.default_attrs	= rdev_default_attrs,
};

int md_rdev_init(struct md_rdev *rdev)
{
	rdev->desc_nr = -1;
	rdev->saved_raid_disk = -1;
	rdev->raid_disk = -1;
	rdev->flags = 0;
	rdev->data_offset = 0;
	rdev->new_data_offset = 0;
	rdev->sb_events = 0;
	rdev->last_read_error.tv_sec  = 0;
	rdev->last_read_error.tv_nsec = 0;
	rdev->sb_loaded = 0;
	rdev->bb_page = NULL;
	atomic_set(&rdev->nr_pending, 0);
	atomic_set(&rdev->read_errors, 0);
	atomic_set(&rdev->corrected_errors, 0);

	INIT_LIST_HEAD(&rdev->same_set);
	init_waitqueue_head(&rdev->blocked_wait);

	/* Add space to store bad block list.
	 * This reserves the space even on arrays where it cannot
	 * be used - I wonder if that matters
	 */
	rdev->badblocks.count = 0;
	rdev->badblocks.shift = 0;
	rdev->badblocks.page = kmalloc(PAGE_SIZE, GFP_KERNEL);
	seqlock_init(&rdev->badblocks.lock);
	if (rdev->badblocks.page == NULL)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(md_rdev_init);
/*
 * Import a device. If 'super_format' >= 0, then sanity check the superblock
 *
 * mark the device faulty if:
 *
 *   - the device is nonexistent (zero size)
 *   - the device has no valid superblock
 *
 * a faulty rdev _never_ has rdev->sb set.
 */
static struct md_rdev *md_import_device(dev_t newdev, int super_format, int super_minor)
{
	char b[BDEVNAME_SIZE];
	int err;
	struct md_rdev *rdev;
	sector_t size;

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev) {
		printk(KERN_ERR "md: could not alloc mem for new device!\n");
		return ERR_PTR(-ENOMEM);
	}

	err = md_rdev_init(rdev);
	if (err)
		goto abort_free;
	err = alloc_disk_sb(rdev);
	if (err)
		goto abort_free;

	err = lock_rdev(rdev, newdev, super_format == -2);
	if (err)
		goto abort_free;

	kobject_init(&rdev->kobj, &rdev_ktype);

	size = i_size_read(rdev->bdev->bd_inode) >> BLOCK_SIZE_BITS;
	if (!size) {
		printk(KERN_WARNING 
			"md: %s has zero or unknown size, marking faulty!\n",
			bdevname(rdev->bdev,b));
		err = -EINVAL;
		goto abort_free;
	}

	if (super_format >= 0) {
		err = super_types[super_format].
			load_super(rdev, NULL, super_minor);
		if (err == -EINVAL) {
			printk(KERN_WARNING
				"md: %s does not have a valid v%d.%d "
			       "superblock, not importing!\n",
				bdevname(rdev->bdev,b),
			       super_format, super_minor);
			goto abort_free;
		}
		if (err < 0) {
			printk(KERN_WARNING 
				"md: could not read %s's sb, not importing!\n",
				bdevname(rdev->bdev,b));
			goto abort_free;
		}
	}
	if (super_format == -1)
		/* hot-add for 0.90, or non-persistent: so no badblocks */
		rdev->badblocks.shift = -1;

	return rdev;

abort_free:
	if (rdev->bdev)
		unlock_rdev(rdev);
	md_rdev_clear(rdev);
	kfree(rdev);
	return ERR_PTR(err);
}

/*
 * Check a full RAID array for plausibility
 */


static void analyze_sbs(struct mddev * mddev)
{
	int i;
	struct md_rdev *rdev, *freshest, *tmp;
	char b[BDEVNAME_SIZE];

	freshest = NULL;
	rdev_for_each_safe(rdev, tmp, mddev)
		switch (super_types[mddev->major_version].
			load_super(rdev, freshest, mddev->minor_version)) {
		case 1:
			freshest = rdev;
			break;
		case 0:
			break;
		default:
			printk( KERN_ERR \
				"md: fatal superblock inconsistency in %s"
				" -- removing from array\n", 
				bdevname(rdev->bdev,b));
			kick_rdev_from_array(rdev);
		}


	super_types[mddev->major_version].
		validate_super(mddev, freshest);

	i = 0;
	rdev_for_each_safe(rdev, tmp, mddev) {
		if (mddev->max_disks &&
		    (rdev->desc_nr >= mddev->max_disks ||
		     i > mddev->max_disks)) {
			printk(KERN_WARNING
			       "md: %s: %s: only %d devices permitted\n",
			       mdname(mddev), bdevname(rdev->bdev, b),
			       mddev->max_disks);
			kick_rdev_from_array(rdev);
			continue;
		}
		if (rdev != freshest)
			if (super_types[mddev->major_version].
			    validate_super(mddev, rdev)) {
				printk(KERN_WARNING "md: kicking non-fresh %s"
					" from array!\n",
					bdevname(rdev->bdev,b));
				kick_rdev_from_array(rdev);
				continue;
			}
		if (mddev->level == LEVEL_MULTIPATH) {
			rdev->desc_nr = i++;
			rdev->raid_disk = rdev->desc_nr;
			set_bit(In_sync, &rdev->flags);
		} else if (rdev->raid_disk >= (mddev->raid_disks - min(0, mddev->delta_disks))) {
			rdev->raid_disk = -1;
			clear_bit(In_sync, &rdev->flags);
		}
	}
}

/* Read a fixed-point number.
 * Numbers in sysfs attributes should be in "standard" units where
 * possible, so time should be in seconds.
 * However we internally use a a much smaller unit such as 
 * milliseconds or jiffies.
 * This function takes a decimal number with a possible fractional
 * component, and produces an integer which is the result of
 * multiplying that number by 10^'scale'.
 * all without any floating-point arithmetic.
 */
int strict_strtoul_scaled(const char *cp, unsigned long *res, int scale)
{
	unsigned long result = 0;
	long decimals = -1;
	while (isdigit(*cp) || (*cp == '.' && decimals < 0)) {
		if (*cp == '.')
			decimals = 0;
		else if (decimals < scale) {
			unsigned int value;
			value = *cp - '0';
			result = result * 10 + value;
			if (decimals >= 0)
				decimals++;
		}
		cp++;
	}
	if (*cp == '\n')
		cp++;
	if (*cp)
		return -EINVAL;
	if (decimals < 0)
		decimals = 0;
	while (decimals < scale) {
		result *= 10;
		decimals ++;
	}
	*res = result;
	return 0;
}


static void md_safemode_timeout(unsigned long data);

static ssize_t
safe_delay_show(struct mddev *mddev, char *page)
{
	int msec = (mddev->safemode_delay*1000)/HZ;
	return sprintf(page, "%d.%03d\n", msec/1000, msec%1000);
}
static ssize_t
safe_delay_store(struct mddev *mddev, const char *cbuf, size_t len)
{
	unsigned long msec;

	if (strict_strtoul_scaled(cbuf, &msec, 3) < 0)
		return -EINVAL;
	if (msec == 0)
		mddev->safemode_delay = 0;
	else {
		unsigned long old_delay = mddev->safemode_delay;
		mddev->safemode_delay = (msec*HZ)/1000;
		if (mddev->safemode_delay == 0)
			mddev->safemode_delay = 1;
		if (mddev->safemode_delay < old_delay)
			md_safemode_timeout((unsigned long)mddev);
	}
	return len;
}
static struct md_sysfs_entry md_safe_delay =
__ATTR(safe_mode_delay, S_IRUGO|S_IWUSR,safe_delay_show, safe_delay_store);

static ssize_t
level_show(struct mddev *mddev, char *page)
{
	struct md_personality *p = mddev->pers;
	if (p)
		return sprintf(page, "%s\n", p->name);
	else if (mddev->clevel[0])
		return sprintf(page, "%s\n", mddev->clevel);
	else if (mddev->level != LEVEL_NONE)
		return sprintf(page, "%d\n", mddev->level);
	else
		return 0;
}

static ssize_t
level_store(struct mddev *mddev, const char *buf, size_t len)
{
	char clevel[16];
	ssize_t rv = len;
	struct md_personality *pers;
	long level;
	void *priv;
	struct md_rdev *rdev;

	if (mddev->pers == NULL) {
		if (len == 0)
			return 0;
		if (len >= sizeof(mddev->clevel))
			return -ENOSPC;
		strncpy(mddev->clevel, buf, len);
		if (mddev->clevel[len-1] == '\n')
			len--;
		mddev->clevel[len] = 0;
		mddev->level = LEVEL_NONE;
		return rv;
	}

	/* request to change the personality.  Need to ensure:
	 *  - array is not engaged in resync/recovery/reshape
	 *  - old personality can be suspended
	 *  - new personality will access other array.
	 */

	if (mddev->sync_thread ||
	    mddev->reshape_position != MaxSector ||
	    mddev->sysfs_active)
		return -EBUSY;

	if (!mddev->pers->quiesce) {
		printk(KERN_WARNING "md: %s: %s does not support online personality change\n",
		       mdname(mddev), mddev->pers->name);
		return -EINVAL;
	}

	/* Now find the new personality */
	if (len == 0 || len >= sizeof(clevel))
		return -EINVAL;
	strncpy(clevel, buf, len);
	if (clevel[len-1] == '\n')
		len--;
	clevel[len] = 0;
	if (strict_strtol(clevel, 10, &level))
		level = LEVEL_NONE;

	if (request_module("md-%s", clevel) != 0)
		request_module("md-level-%s", clevel);
	spin_lock(&pers_lock);
	pers = find_pers(level, clevel);
	if (!pers || !try_module_get(pers->owner)) {
		spin_unlock(&pers_lock);
		printk(KERN_WARNING "md: personality %s not loaded\n", clevel);
		return -EINVAL;
	}
	spin_unlock(&pers_lock);

	if (pers == mddev->pers) {
		/* Nothing to do! */
		module_put(pers->owner);
		return rv;
	}
	if (!pers->takeover) {
		module_put(pers->owner);
		printk(KERN_WARNING "md: %s: %s does not support personality takeover\n",
		       mdname(mddev), clevel);
		return -EINVAL;
	}

	rdev_for_each(rdev, mddev)
		rdev->new_raid_disk = rdev->raid_disk;

	/* ->takeover must set new_* and/or delta_disks
	 * if it succeeds, and may set them when it fails.
	 */
	priv = pers->takeover(mddev);
	if (IS_ERR(priv)) {
		mddev->new_level = mddev->level;
		mddev->new_layout = mddev->layout;
		mddev->new_chunk_sectors = mddev->chunk_sectors;
		mddev->raid_disks -= mddev->delta_disks;
		mddev->delta_disks = 0;
		mddev->reshape_backwards = 0;
		module_put(pers->owner);
		printk(KERN_WARNING "md: %s: %s would not accept array\n",
		       mdname(mddev), clevel);
		return PTR_ERR(priv);
	}

	/* Looks like we have a winner */
	mddev_suspend(mddev);
	mddev->pers->stop(mddev);
	
	if (mddev->pers->sync_request == NULL &&
	    pers->sync_request != NULL) {
		/* need to add the md_redundancy_group */
		if (sysfs_create_group(&mddev->kobj, &md_redundancy_group))
			printk(KERN_WARNING
			       "md: cannot register extra attributes for %s\n",
			       mdname(mddev));
		mddev->sysfs_action = sysfs_get_dirent(mddev->kobj.sd, NULL, "sync_action");
	}		
	if (mddev->pers->sync_request != NULL &&
	    pers->sync_request == NULL) {
		/* need to remove the md_redundancy_group */
		if (mddev->to_remove == NULL)
			mddev->to_remove = &md_redundancy_group;
	}

	if (mddev->pers->sync_request == NULL &&
	    mddev->external) {
		/* We are converting from a no-redundancy array
		 * to a redundancy array and metadata is managed
		 * externally so we need to be sure that writes
		 * won't block due to a need to transition
		 *      clean->dirty
		 * until external management is started.
		 */
		mddev->in_sync = 0;
		mddev->safemode_delay = 0;
		mddev->safemode = 0;
	}

	rdev_for_each(rdev, mddev) {
		if (rdev->raid_disk < 0)
			continue;
		if (rdev->new_raid_disk >= mddev->raid_disks)
			rdev->new_raid_disk = -1;
		if (rdev->new_raid_disk == rdev->raid_disk)
			continue;
		sysfs_unlink_rdev(mddev, rdev);
	}
	rdev_for_each(rdev, mddev) {
		if (rdev->raid_disk < 0)
			continue;
		if (rdev->new_raid_disk == rdev->raid_disk)
			continue;
		rdev->raid_disk = rdev->new_raid_disk;
		if (rdev->raid_disk < 0)
			clear_bit(In_sync, &rdev->flags);
		else {
			if (sysfs_link_rdev(mddev, rdev))
				printk(KERN_WARNING "md: cannot register rd%d"
				       " for %s after level change\n",
				       rdev->raid_disk, mdname(mddev));
		}
	}

	module_put(mddev->pers->owner);
	mddev->pers = pers;
	mddev->private = priv;
	strlcpy(mddev->clevel, pers->name, sizeof(mddev->clevel));
	mddev->level = mddev->new_level;
	mddev->layout = mddev->new_layout;
	mddev->chunk_sectors = mddev->new_chunk_sectors;
	mddev->delta_disks = 0;
	mddev->reshape_backwards = 0;
	mddev->degraded = 0;
	if (mddev->pers->sync_request == NULL) {
		/* this is now an array without redundancy, so
		 * it must always be in_sync
		 */
		mddev->in_sync = 1;
		del_timer_sync(&mddev->safemode_timer);
	}
	pers->run(mddev);
	mddev_resume(mddev);
	set_bit(MD_CHANGE_DEVS, &mddev->flags);
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
	sysfs_notify(&mddev->kobj, NULL, "level");
	md_new_event(mddev);
	return rv;
}

static struct md_sysfs_entry md_level =
__ATTR(level, S_IRUGO|S_IWUSR, level_show, level_store);


static ssize_t
layout_show(struct mddev *mddev, char *page)
{
	/* just a number, not meaningful for all levels */
	if (mddev->reshape_position != MaxSector &&
	    mddev->layout != mddev->new_layout)
		return sprintf(page, "%d (%d)\n",
			       mddev->new_layout, mddev->layout);
	return sprintf(page, "%d\n", mddev->layout);
}

static ssize_t
layout_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long n = simple_strtoul(buf, &e, 10);

	if (!*buf || (*e && *e != '\n'))
		return -EINVAL;

	if (mddev->pers) {
		int err;
		if (mddev->pers->check_reshape == NULL)
			return -EBUSY;
		mddev->new_layout = n;
		err = mddev->pers->check_reshape(mddev);
		if (err) {
			mddev->new_layout = mddev->layout;
			return err;
		}
	} else {
		mddev->new_layout = n;
		if (mddev->reshape_position == MaxSector)
			mddev->layout = n;
	}
	return len;
}
static struct md_sysfs_entry md_layout =
__ATTR(layout, S_IRUGO|S_IWUSR, layout_show, layout_store);


static ssize_t
raid_disks_show(struct mddev *mddev, char *page)
{
	if (mddev->raid_disks == 0)
		return 0;
	if (mddev->reshape_position != MaxSector &&
	    mddev->delta_disks != 0)
		return sprintf(page, "%d (%d)\n", mddev->raid_disks,
			       mddev->raid_disks - mddev->delta_disks);
	return sprintf(page, "%d\n", mddev->raid_disks);
}

static int update_raid_disks(struct mddev *mddev, int raid_disks);

static ssize_t
raid_disks_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *e;
	int rv = 0;
	unsigned long n = simple_strtoul(buf, &e, 10);

	if (!*buf || (*e && *e != '\n'))
		return -EINVAL;

	if (mddev->pers)
		rv = update_raid_disks(mddev, n);
	else if (mddev->reshape_position != MaxSector) {
		struct md_rdev *rdev;
		int olddisks = mddev->raid_disks - mddev->delta_disks;

		rdev_for_each(rdev, mddev) {
			if (olddisks < n &&
			    rdev->data_offset < rdev->new_data_offset)
				return -EINVAL;
			if (olddisks > n &&
			    rdev->data_offset > rdev->new_data_offset)
				return -EINVAL;
		}
		mddev->delta_disks = n - olddisks;
		mddev->raid_disks = n;
		mddev->reshape_backwards = (mddev->delta_disks < 0);
	} else
		mddev->raid_disks = n;
	return rv ? rv : len;
}
static struct md_sysfs_entry md_raid_disks =
__ATTR(raid_disks, S_IRUGO|S_IWUSR, raid_disks_show, raid_disks_store);

static ssize_t
chunk_size_show(struct mddev *mddev, char *page)
{
	if (mddev->reshape_position != MaxSector &&
	    mddev->chunk_sectors != mddev->new_chunk_sectors)
		return sprintf(page, "%d (%d)\n",
			       mddev->new_chunk_sectors << 9,
			       mddev->chunk_sectors << 9);
	return sprintf(page, "%d\n", mddev->chunk_sectors << 9);
}

static ssize_t
chunk_size_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long n = simple_strtoul(buf, &e, 10);

	if (!*buf || (*e && *e != '\n'))
		return -EINVAL;

	if (mddev->pers) {
		int err;
		if (mddev->pers->check_reshape == NULL)
			return -EBUSY;
		mddev->new_chunk_sectors = n >> 9;
		err = mddev->pers->check_reshape(mddev);
		if (err) {
			mddev->new_chunk_sectors = mddev->chunk_sectors;
			return err;
		}
	} else {
		mddev->new_chunk_sectors = n >> 9;
		if (mddev->reshape_position == MaxSector)
			mddev->chunk_sectors = n >> 9;
	}
	return len;
}
static struct md_sysfs_entry md_chunk_size =
__ATTR(chunk_size, S_IRUGO|S_IWUSR, chunk_size_show, chunk_size_store);

static ssize_t
resync_start_show(struct mddev *mddev, char *page)
{
	if (mddev->recovery_cp == MaxSector)
		return sprintf(page, "none\n");
	return sprintf(page, "%llu\n", (unsigned long long)mddev->recovery_cp);
}

static ssize_t
resync_start_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long long n = simple_strtoull(buf, &e, 10);

	if (mddev->pers && !test_bit(MD_RECOVERY_FROZEN, &mddev->recovery))
		return -EBUSY;
	if (cmd_match(buf, "none"))
		n = MaxSector;
	else if (!*buf || (*e && *e != '\n'))
		return -EINVAL;

	mddev->recovery_cp = n;
	return len;
}
static struct md_sysfs_entry md_resync_start =
__ATTR(resync_start, S_IRUGO|S_IWUSR, resync_start_show, resync_start_store);

/*
 * The array state can be:
 *
 * clear
 *     No devices, no size, no level
 *     Equivalent to STOP_ARRAY ioctl
 * inactive
 *     May have some settings, but array is not active
 *        all IO results in error
 *     When written, doesn't tear down array, but just stops it
 * suspended (not supported yet)
 *     All IO requests will block. The array can be reconfigured.
 *     Writing this, if accepted, will block until array is quiescent
 * readonly
 *     no resync can happen.  no superblocks get written.
 *     write requests fail
 * read-auto
 *     like readonly, but behaves like 'clean' on a write request.
 *
 * clean - no pending writes, but otherwise active.
 *     When written to inactive array, starts without resync
 *     If a write request arrives then
 *       if metadata is known, mark 'dirty' and switch to 'active'.
 *       if not known, block and switch to write-pending
 *     If written to an active array that has pending writes, then fails.
 * active
 *     fully active: IO and resync can be happening.
 *     When written to inactive array, starts with resync
 *
 * write-pending
 *     clean, but writes are blocked waiting for 'active' to be written.
 *
 * active-idle
 *     like active, but no writes have been seen for a while (100msec).
 *
 */
enum array_state { clear, inactive, suspended, readonly, read_auto, clean, active,
		   write_pending, active_idle, bad_word};
static char *array_states[] = {
	"clear", "inactive", "suspended", "readonly", "read-auto", "clean", "active",
	"write-pending", "active-idle", NULL };

static int match_word(const char *word, char **list)
{
	int n;
	for (n=0; list[n]; n++)
		if (cmd_match(word, list[n]))
			break;
	return n;
}

static ssize_t
array_state_show(struct mddev *mddev, char *page)
{
	enum array_state st = inactive;

	if (mddev->pers)
		switch(mddev->ro) {
		case 1:
			st = readonly;
			break;
		case 2:
			st = read_auto;
			break;
		case 0:
			if (mddev->in_sync)
				st = clean;
			else if (test_bit(MD_CHANGE_PENDING, &mddev->flags))
				st = write_pending;
			else if (mddev->safemode)
				st = active_idle;
			else
				st = active;
		}
	else {
		if (list_empty(&mddev->disks) &&
		    mddev->raid_disks == 0 &&
		    mddev->dev_sectors == 0)
			st = clear;
		else
			st = inactive;
	}
	return sprintf(page, "%s\n", array_states[st]);
}

static int do_md_stop(struct mddev * mddev, int ro, int is_open);
static int md_set_readonly(struct mddev * mddev, int is_open);
static int do_md_run(struct mddev * mddev);
static int restart_array(struct mddev *mddev);

static ssize_t
array_state_store(struct mddev *mddev, const char *buf, size_t len)
{
	int err = -EINVAL;
	enum array_state st = match_word(buf, array_states);
	switch(st) {
	case bad_word:
		break;
	case clear:
		/* stopping an active array */
		if (atomic_read(&mddev->openers) > 0)
			return -EBUSY;
		err = do_md_stop(mddev, 0, 0);
		break;
	case inactive:
		/* stopping an active array */
		if (mddev->pers) {
			if (atomic_read(&mddev->openers) > 0)
				return -EBUSY;
			err = do_md_stop(mddev, 2, 0);
		} else
			err = 0; /* already inactive */
		break;
	case suspended:
		break; /* not supported yet */
	case readonly:
		if (mddev->pers)
			err = md_set_readonly(mddev, 0);
		else {
			mddev->ro = 1;
			set_disk_ro(mddev->gendisk, 1);
			err = do_md_run(mddev);
		}
		break;
	case read_auto:
		if (mddev->pers) {
			if (mddev->ro == 0)
				err = md_set_readonly(mddev, 0);
			else if (mddev->ro == 1)
				err = restart_array(mddev);
			if (err == 0) {
				mddev->ro = 2;
				set_disk_ro(mddev->gendisk, 0);
			}
		} else {
			mddev->ro = 2;
			err = do_md_run(mddev);
		}
		break;
	case clean:
		if (mddev->pers) {
			restart_array(mddev);
			spin_lock_irq(&mddev->write_lock);
			if (atomic_read(&mddev->writes_pending) == 0) {
				if (mddev->in_sync == 0) {
					mddev->in_sync = 1;
					if (mddev->safemode == 1)
						mddev->safemode = 0;
					set_bit(MD_CHANGE_CLEAN, &mddev->flags);
				}
				err = 0;
			} else
				err = -EBUSY;
			spin_unlock_irq(&mddev->write_lock);
		} else
			err = -EINVAL;
		break;
	case active:
		if (mddev->pers) {
			restart_array(mddev);
			clear_bit(MD_CHANGE_PENDING, &mddev->flags);
			wake_up(&mddev->sb_wait);
			err = 0;
		} else {
			mddev->ro = 0;
			set_disk_ro(mddev->gendisk, 0);
			err = do_md_run(mddev);
		}
		break;
	case write_pending:
	case active_idle:
		/* these cannot be set */
		break;
	}
	if (err)
		return err;
	else {
		if (mddev->hold_active == UNTIL_IOCTL)
			mddev->hold_active = 0;
		sysfs_notify_dirent_safe(mddev->sysfs_state);
		return len;
	}
}
static struct md_sysfs_entry md_array_state =
__ATTR(array_state, S_IRUGO|S_IWUSR, array_state_show, array_state_store);

static ssize_t
max_corrected_read_errors_show(struct mddev *mddev, char *page) {
	return sprintf(page, "%d\n",
		       atomic_read(&mddev->max_corr_read_errors));
}

static ssize_t
max_corrected_read_errors_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long n = simple_strtoul(buf, &e, 10);

	if (*buf && (*e == 0 || *e == '\n')) {
		atomic_set(&mddev->max_corr_read_errors, n);
		return len;
	}
	return -EINVAL;
}

static struct md_sysfs_entry max_corr_read_errors =
__ATTR(max_read_errors, S_IRUGO|S_IWUSR, max_corrected_read_errors_show,
	max_corrected_read_errors_store);

static ssize_t
null_show(struct mddev *mddev, char *page)
{
	return -EINVAL;
}

static ssize_t
new_dev_store(struct mddev *mddev, const char *buf, size_t len)
{
	/* buf must be %d:%d\n? giving major and minor numbers */
	/* The new device is added to the array.
	 * If the array has a persistent superblock, we read the
	 * superblock to initialise info and check validity.
	 * Otherwise, only checking done is that in bind_rdev_to_array,
	 * which mainly checks size.
	 */
	char *e;
	int major = simple_strtoul(buf, &e, 10);
	int minor;
	dev_t dev;
	struct md_rdev *rdev;
	int err;

	if (!*buf || *e != ':' || !e[1] || e[1] == '\n')
		return -EINVAL;
	minor = simple_strtoul(e+1, &e, 10);
	if (*e && *e != '\n')
		return -EINVAL;
	dev = MKDEV(major, minor);
	if (major != MAJOR(dev) ||
	    minor != MINOR(dev))
		return -EOVERFLOW;


	if (mddev->persistent) {
		rdev = md_import_device(dev, mddev->major_version,
					mddev->minor_version);
		if (!IS_ERR(rdev) && !list_empty(&mddev->disks)) {
			struct md_rdev *rdev0
				= list_entry(mddev->disks.next,
					     struct md_rdev, same_set);
			err = super_types[mddev->major_version]
				.load_super(rdev, rdev0, mddev->minor_version);
			if (err < 0)
				goto out;
		}
	} else if (mddev->external)
		rdev = md_import_device(dev, -2, -1);
	else
		rdev = md_import_device(dev, -1, -1);

	if (IS_ERR(rdev))
		return PTR_ERR(rdev);
	err = bind_rdev_to_array(rdev, mddev);
 out:
	if (err)
		export_rdev(rdev);
	return err ? err : len;
}

static struct md_sysfs_entry md_new_device =
__ATTR(new_dev, S_IWUSR, null_show, new_dev_store);

static ssize_t
bitmap_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *end;
	unsigned long chunk, end_chunk;

	if (!mddev->bitmap)
		goto out;
	/* buf should be <chunk> <chunk> ... or <chunk>-<chunk> ... (range) */
	while (*buf) {
		chunk = end_chunk = simple_strtoul(buf, &end, 0);
		if (buf == end) break;
		if (*end == '-') { /* range */
			buf = end + 1;
			end_chunk = simple_strtoul(buf, &end, 0);
			if (buf == end) break;
		}
		if (*end && !isspace(*end)) break;
		bitmap_dirty_bits(mddev->bitmap, chunk, end_chunk);
		buf = skip_spaces(end);
	}
	bitmap_unplug(mddev->bitmap); /* flush the bits to disk */
out:
	return len;
}

static struct md_sysfs_entry md_bitmap =
__ATTR(bitmap_set_bits, S_IWUSR, null_show, bitmap_store);

static ssize_t
size_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%llu\n",
		(unsigned long long)mddev->dev_sectors / 2);
}

static int update_size(struct mddev *mddev, sector_t num_sectors);

static ssize_t
size_store(struct mddev *mddev, const char *buf, size_t len)
{
	/* If array is inactive, we can reduce the component size, but
	 * not increase it (except from 0).
	 * If array is active, we can try an on-line resize
	 */
	sector_t sectors;
	int err = strict_blocks_to_sectors(buf, &sectors);

	if (err < 0)
		return err;
	if (mddev->pers) {
		err = update_size(mddev, sectors);
		md_update_sb(mddev, 1);
	} else {
		if (mddev->dev_sectors == 0 ||
		    mddev->dev_sectors > sectors)
			mddev->dev_sectors = sectors;
		else
			err = -ENOSPC;
	}
	return err ? err : len;
}

static struct md_sysfs_entry md_size =
__ATTR(component_size, S_IRUGO|S_IWUSR, size_show, size_store);


/* Metdata version.
 * This is one of
 *   'none' for arrays with no metadata (good luck...)
 *   'external' for arrays with externally managed metadata,
 * or N.M for internally known formats
 */
static ssize_t
metadata_show(struct mddev *mddev, char *page)
{
	if (mddev->persistent)
		return sprintf(page, "%d.%d\n",
			       mddev->major_version, mddev->minor_version);
	else if (mddev->external)
		return sprintf(page, "external:%s\n", mddev->metadata_type);
	else
		return sprintf(page, "none\n");
}

static ssize_t
metadata_store(struct mddev *mddev, const char *buf, size_t len)
{
	int major, minor;
	char *e;
	/* Changing the details of 'external' metadata is
	 * always permitted.  Otherwise there must be
	 * no devices attached to the array.
	 */
	if (mddev->external && strncmp(buf, "external:", 9) == 0)
		;
	else if (!list_empty(&mddev->disks))
		return -EBUSY;

	if (cmd_match(buf, "none")) {
		mddev->persistent = 0;
		mddev->external = 0;
		mddev->major_version = 0;
		mddev->minor_version = 90;
		return len;
	}
	if (strncmp(buf, "external:", 9) == 0) {
		size_t namelen = len-9;
		if (namelen >= sizeof(mddev->metadata_type))
			namelen = sizeof(mddev->metadata_type)-1;
		strncpy(mddev->metadata_type, buf+9, namelen);
		mddev->metadata_type[namelen] = 0;
		if (namelen && mddev->metadata_type[namelen-1] == '\n')
			mddev->metadata_type[--namelen] = 0;
		mddev->persistent = 0;
		mddev->external = 1;
		mddev->major_version = 0;
		mddev->minor_version = 90;
		return len;
	}
	major = simple_strtoul(buf, &e, 10);
	if (e==buf || *e != '.')
		return -EINVAL;
	buf = e+1;
	minor = simple_strtoul(buf, &e, 10);
	if (e==buf || (*e && *e != '\n') )
		return -EINVAL;
	if (major >= ARRAY_SIZE(super_types) || super_types[major].name == NULL)
		return -ENOENT;
	mddev->major_version = major;
	mddev->minor_version = minor;
	mddev->persistent = 1;
	mddev->external = 0;
	return len;
}

static struct md_sysfs_entry md_metadata =
__ATTR(metadata_version, S_IRUGO|S_IWUSR, metadata_show, metadata_store);

static ssize_t
action_show(struct mddev *mddev, char *page)
{
	char *type = "idle";
	if (test_bit(MD_RECOVERY_FROZEN, &mddev->recovery))
		type = "frozen";
	else if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery) ||
	    (!mddev->ro && test_bit(MD_RECOVERY_NEEDED, &mddev->recovery))) {
		if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
			type = "reshape";
		else if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
			if (!test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
				type = "resync";
			else if (test_bit(MD_RECOVERY_CHECK, &mddev->recovery))
				type = "check";
			else
				type = "repair";
		} else if (test_bit(MD_RECOVERY_RECOVER, &mddev->recovery))
			type = "recover";
	}
	return sprintf(page, "%s\n", type);
}

static void reap_sync_thread(struct mddev *mddev);

static ssize_t
action_store(struct mddev *mddev, const char *page, size_t len)
{
	if (!mddev->pers || !mddev->pers->sync_request)
		return -EINVAL;

	if (cmd_match(page, "frozen"))
		set_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
	else
		clear_bit(MD_RECOVERY_FROZEN, &mddev->recovery);

	if (cmd_match(page, "idle") || cmd_match(page, "frozen")) {
		if (mddev->sync_thread) {
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			reap_sync_thread(mddev);
		}
	} else if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery) ||
		   test_bit(MD_RECOVERY_NEEDED, &mddev->recovery))
		return -EBUSY;
	else if (cmd_match(page, "resync"))
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	else if (cmd_match(page, "recover")) {
		set_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	} else if (cmd_match(page, "reshape")) {
		int err;
		if (mddev->pers->start_reshape == NULL)
			return -EINVAL;
		err = mddev->pers->start_reshape(mddev);
		if (err)
			return err;
		sysfs_notify(&mddev->kobj, NULL, "degraded");
	} else {
		if (cmd_match(page, "check"))
			set_bit(MD_RECOVERY_CHECK, &mddev->recovery);
		else if (!cmd_match(page, "repair"))
			return -EINVAL;
		set_bit(MD_RECOVERY_REQUESTED, &mddev->recovery);
		set_bit(MD_RECOVERY_SYNC, &mddev->recovery);
	}
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
	sysfs_notify_dirent_safe(mddev->sysfs_action);
	return len;
}

static ssize_t
mismatch_cnt_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%llu\n",
		       (unsigned long long) mddev->resync_mismatches);
}

static struct md_sysfs_entry md_scan_mode =
__ATTR(sync_action, S_IRUGO|S_IWUSR, action_show, action_store);


static struct md_sysfs_entry md_mismatches = __ATTR_RO(mismatch_cnt);

static ssize_t
sync_min_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%d (%s)\n", speed_min(mddev),
		       mddev->sync_speed_min ? "local": "system");
}

static ssize_t
sync_min_store(struct mddev *mddev, const char *buf, size_t len)
{
	int min;
	char *e;
	if (strncmp(buf, "system", 6)==0) {
		mddev->sync_speed_min = 0;
		return len;
	}
	min = simple_strtoul(buf, &e, 10);
	if (buf == e || (*e && *e != '\n') || min <= 0)
		return -EINVAL;
	mddev->sync_speed_min = min;
	return len;
}

static struct md_sysfs_entry md_sync_min =
__ATTR(sync_speed_min, S_IRUGO|S_IWUSR, sync_min_show, sync_min_store);

static ssize_t
sync_max_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%d (%s)\n", speed_max(mddev),
		       mddev->sync_speed_max ? "local": "system");
}

static ssize_t
sync_max_store(struct mddev *mddev, const char *buf, size_t len)
{
	int max;
	char *e;
	if (strncmp(buf, "system", 6)==0) {
		mddev->sync_speed_max = 0;
		return len;
	}
	max = simple_strtoul(buf, &e, 10);
	if (buf == e || (*e && *e != '\n') || max <= 0)
		return -EINVAL;
	mddev->sync_speed_max = max;
	return len;
}

static struct md_sysfs_entry md_sync_max =
__ATTR(sync_speed_max, S_IRUGO|S_IWUSR, sync_max_show, sync_max_store);

static ssize_t
degraded_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%d\n", mddev->degraded);
}
static struct md_sysfs_entry md_degraded = __ATTR_RO(degraded);

static ssize_t
sync_force_parallel_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%d\n", mddev->parallel_resync);
}

static ssize_t
sync_force_parallel_store(struct mddev *mddev, const char *buf, size_t len)
{
	long n;

	if (strict_strtol(buf, 10, &n))
		return -EINVAL;

	if (n != 0 && n != 1)
		return -EINVAL;

	mddev->parallel_resync = n;

	if (mddev->sync_thread)
		wake_up(&resync_wait);

	return len;
}

/* force parallel resync, even with shared block devices */
static struct md_sysfs_entry md_sync_force_parallel =
__ATTR(sync_force_parallel, S_IRUGO|S_IWUSR,
       sync_force_parallel_show, sync_force_parallel_store);

static ssize_t
sync_speed_show(struct mddev *mddev, char *page)
{
	unsigned long resync, dt, db;
	if (mddev->curr_resync == 0)
		return sprintf(page, "none\n");
	resync = mddev->curr_mark_cnt - atomic_read(&mddev->recovery_active);
	dt = (jiffies - mddev->resync_mark) / HZ;
	if (!dt) dt++;
	db = resync - mddev->resync_mark_cnt;
	return sprintf(page, "%lu\n", db/dt/2); /* K/sec */
}

static struct md_sysfs_entry md_sync_speed = __ATTR_RO(sync_speed);

static ssize_t
sync_completed_show(struct mddev *mddev, char *page)
{
	unsigned long long max_sectors, resync;

	if (!test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
		return sprintf(page, "none\n");

	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery) ||
	    test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		max_sectors = mddev->resync_max_sectors;
	else
		max_sectors = mddev->dev_sectors;

	resync = mddev->curr_resync_completed;
	return sprintf(page, "%llu / %llu\n", resync, max_sectors);
}

static struct md_sysfs_entry md_sync_completed = __ATTR_RO(sync_completed);

static ssize_t
min_sync_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%llu\n",
		       (unsigned long long)mddev->resync_min);
}
static ssize_t
min_sync_store(struct mddev *mddev, const char *buf, size_t len)
{
	unsigned long long min;
	if (strict_strtoull(buf, 10, &min))
		return -EINVAL;
	if (min > mddev->resync_max)
		return -EINVAL;
	if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
		return -EBUSY;

	/* Must be a multiple of chunk_size */
	if (mddev->chunk_sectors) {
		sector_t temp = min;
		if (sector_div(temp, mddev->chunk_sectors))
			return -EINVAL;
	}
	mddev->resync_min = min;

	return len;
}

static struct md_sysfs_entry md_min_sync =
__ATTR(sync_min, S_IRUGO|S_IWUSR, min_sync_show, min_sync_store);

static ssize_t
max_sync_show(struct mddev *mddev, char *page)
{
	if (mddev->resync_max == MaxSector)
		return sprintf(page, "max\n");
	else
		return sprintf(page, "%llu\n",
			       (unsigned long long)mddev->resync_max);
}
static ssize_t
max_sync_store(struct mddev *mddev, const char *buf, size_t len)
{
	if (strncmp(buf, "max", 3) == 0)
		mddev->resync_max = MaxSector;
	else {
		unsigned long long max;
		if (strict_strtoull(buf, 10, &max))
			return -EINVAL;
		if (max < mddev->resync_min)
			return -EINVAL;
		if (max < mddev->resync_max &&
		    mddev->ro == 0 &&
		    test_bit(MD_RECOVERY_RUNNING, &mddev->recovery))
			return -EBUSY;

		/* Must be a multiple of chunk_size */
		if (mddev->chunk_sectors) {
			sector_t temp = max;
			if (sector_div(temp, mddev->chunk_sectors))
				return -EINVAL;
		}
		mddev->resync_max = max;
	}
	wake_up(&mddev->recovery_wait);
	return len;
}

static struct md_sysfs_entry md_max_sync =
__ATTR(sync_max, S_IRUGO|S_IWUSR, max_sync_show, max_sync_store);

static ssize_t
suspend_lo_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)mddev->suspend_lo);
}

static ssize_t
suspend_lo_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long long new = simple_strtoull(buf, &e, 10);
	unsigned long long old = mddev->suspend_lo;

	if (mddev->pers == NULL || 
	    mddev->pers->quiesce == NULL)
		return -EINVAL;
	if (buf == e || (*e && *e != '\n'))
		return -EINVAL;

	mddev->suspend_lo = new;
	if (new >= old)
		/* Shrinking suspended region */
		mddev->pers->quiesce(mddev, 2);
	else {
		/* Expanding suspended region - need to wait */
		mddev->pers->quiesce(mddev, 1);
		mddev->pers->quiesce(mddev, 0);
	}
	return len;
}
static struct md_sysfs_entry md_suspend_lo =
__ATTR(suspend_lo, S_IRUGO|S_IWUSR, suspend_lo_show, suspend_lo_store);


static ssize_t
suspend_hi_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)mddev->suspend_hi);
}

static ssize_t
suspend_hi_store(struct mddev *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long long new = simple_strtoull(buf, &e, 10);
	unsigned long long old = mddev->suspend_hi;

	if (mddev->pers == NULL ||
	    mddev->pers->quiesce == NULL)
		return -EINVAL;
	if (buf == e || (*e && *e != '\n'))
		return -EINVAL;

	mddev->suspend_hi = new;
	if (new <= old)
		/* Shrinking suspended region */
		mddev->pers->quiesce(mddev, 2);
	else {
		/* Expanding suspended region - need to wait */
		mddev->pers->quiesce(mddev, 1);
		mddev->pers->quiesce(mddev, 0);
	}
	return len;
}
static struct md_sysfs_entry md_suspend_hi =
__ATTR(suspend_hi, S_IRUGO|S_IWUSR, suspend_hi_show, suspend_hi_store);

static ssize_t
reshape_position_show(struct mddev *mddev, char *page)
{
	if (mddev->reshape_position != MaxSector)
		return sprintf(page, "%llu\n",
			       (unsigned long long)mddev->reshape_position);
	strcpy(page, "none\n");
	return 5;
}

static ssize_t
reshape_position_store(struct mddev *mddev, const char *buf, size_t len)
{
	struct md_rdev *rdev;
	char *e;
	unsigned long long new = simple_strtoull(buf, &e, 10);
	if (mddev->pers)
		return -EBUSY;
	if (buf == e || (*e && *e != '\n'))
		return -EINVAL;
	mddev->reshape_position = new;
	mddev->delta_disks = 0;
	mddev->reshape_backwards = 0;
	mddev->new_level = mddev->level;
	mddev->new_layout = mddev->layout;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
	rdev_for_each(rdev, mddev)
		rdev->new_data_offset = rdev->data_offset;
	return len;
}

static struct md_sysfs_entry md_reshape_position =
__ATTR(reshape_position, S_IRUGO|S_IWUSR, reshape_position_show,
       reshape_position_store);

static ssize_t
reshape_direction_show(struct mddev *mddev, char *page)
{
	return sprintf(page, "%s\n",
		       mddev->reshape_backwards ? "backwards" : "forwards");
}

static ssize_t
reshape_direction_store(struct mddev *mddev, const char *buf, size_t len)
{
	int backwards = 0;
	if (cmd_match(buf, "forwards"))
		backwards = 0;
	else if (cmd_match(buf, "backwards"))
		backwards = 1;
	else
		return -EINVAL;
	if (mddev->reshape_backwards == backwards)
		return len;

	/* check if we are allowed to change */
	if (mddev->delta_disks)
		return -EBUSY;

	if (mddev->persistent &&
	    mddev->major_version == 0)
		return -EINVAL;

	mddev->reshape_backwards = backwards;
	return len;
}

static struct md_sysfs_entry md_reshape_direction =
__ATTR(reshape_direction, S_IRUGO|S_IWUSR, reshape_direction_show,
       reshape_direction_store);

static ssize_t
array_size_show(struct mddev *mddev, char *page)
{
	if (mddev->external_size)
		return sprintf(page, "%llu\n",
			       (unsigned long long)mddev->array_sectors/2);
	else
		return sprintf(page, "default\n");
}

static ssize_t
array_size_store(struct mddev *mddev, const char *buf, size_t len)
{
	sector_t sectors;

	if (strncmp(buf, "default", 7) == 0) {
		if (mddev->pers)
			sectors = mddev->pers->size(mddev, 0, 0);
		else
			sectors = mddev->array_sectors;

		mddev->external_size = 0;
	} else {
		if (strict_blocks_to_sectors(buf, &sectors) < 0)
			return -EINVAL;
		if (mddev->pers && mddev->pers->size(mddev, 0, 0) < sectors)
			return -E2BIG;

		mddev->external_size = 1;
	}

	mddev->array_sectors = sectors;
	if (mddev->pers) {
		set_capacity(mddev->gendisk, mddev->array_sectors);
		revalidate_disk(mddev->gendisk);
	}
	return len;
}

static struct md_sysfs_entry md_array_size =
__ATTR(array_size, S_IRUGO|S_IWUSR, array_size_show,
       array_size_store);

static struct attribute *md_default_attrs[] = {
	&md_level.attr,
	&md_layout.attr,
	&md_raid_disks.attr,
	&md_chunk_size.attr,
	&md_size.attr,
	&md_resync_start.attr,
	&md_metadata.attr,
	&md_new_device.attr,
	&md_safe_delay.attr,
	&md_array_state.attr,
	&md_reshape_position.attr,
	&md_reshape_direction.attr,
	&md_array_size.attr,
	&max_corr_read_errors.attr,
	NULL,
};

static struct attribute *md_redundancy_attrs[] = {
	&md_scan_mode.attr,
	&md_mismatches.attr,
	&md_sync_min.attr,
	&md_sync_max.attr,
	&md_sync_speed.attr,
	&md_sync_force_parallel.attr,
	&md_sync_completed.attr,
	&md_min_sync.attr,
	&md_max_sync.attr,
	&md_suspend_lo.attr,
	&md_suspend_hi.attr,
	&md_bitmap.attr,
	&md_degraded.attr,
	NULL,
};
static struct attribute_group md_redundancy_group = {
	.name = NULL,
	.attrs = md_redundancy_attrs,
};


static ssize_t
md_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct md_sysfs_entry *entry = container_of(attr, struct md_sysfs_entry, attr);
	struct mddev *mddev = container_of(kobj, struct mddev, kobj);
	ssize_t rv;

	if (!entry->show)
		return -EIO;
	spin_lock(&all_mddevs_lock);
	if (list_empty(&mddev->all_mddevs)) {
		spin_unlock(&all_mddevs_lock);
		return -EBUSY;
	}
	mddev_get(mddev);
	spin_unlock(&all_mddevs_lock);

	rv = mddev_lock(mddev);
	if (!rv) {
		rv = entry->show(mddev, page);
		mddev_unlock(mddev);
	}
	mddev_put(mddev);
	return rv;
}

static ssize_t
md_attr_store(struct kobject *kobj, struct attribute *attr,
	      const char *page, size_t length)
{
	struct md_sysfs_entry *entry = container_of(attr, struct md_sysfs_entry, attr);
	struct mddev *mddev = container_of(kobj, struct mddev, kobj);
	ssize_t rv;

	if (!entry->store)
		return -EIO;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	spin_lock(&all_mddevs_lock);
	if (list_empty(&mddev->all_mddevs)) {
		spin_unlock(&all_mddevs_lock);
		return -EBUSY;
	}
	mddev_get(mddev);
	spin_unlock(&all_mddevs_lock);
	rv = mddev_lock(mddev);
	if (!rv) {
		rv = entry->store(mddev, page, length);
		mddev_unlock(mddev);
	}
	mddev_put(mddev);
	return rv;
}

static void md_free(struct kobject *ko)
{
	struct mddev *mddev = container_of(ko, struct mddev, kobj);

	if (mddev->sysfs_state)
		sysfs_put(mddev->sysfs_state);

	if (mddev->gendisk) {
		del_gendisk(mddev->gendisk);
		put_disk(mddev->gendisk);
	}
	if (mddev->queue)
		blk_cleanup_queue(mddev->queue);

	kfree(mddev);
}

static const struct sysfs_ops md_sysfs_ops = {
	.show	= md_attr_show,
	.store	= md_attr_store,
};
static struct kobj_type md_ktype = {
	.release	= md_free,
	.sysfs_ops	= &md_sysfs_ops,
	.default_attrs	= md_default_attrs,
};

int mdp_major = 0;

static void mddev_delayed_delete(struct work_struct *ws)
{
	struct mddev *mddev = container_of(ws, struct mddev, del_work);

	sysfs_remove_group(&mddev->kobj, &md_bitmap_group);
	kobject_del(&mddev->kobj);
	kobject_put(&mddev->kobj);
}

static int md_alloc(dev_t dev, char *name)
{
	static DEFINE_MUTEX(disks_mutex);
	struct mddev *mddev = mddev_find(dev);
	struct gendisk *disk;
	int partitioned;
	int shift;
	int unit;
	int error;

	if (!mddev)
		return -ENODEV;

	partitioned = (MAJOR(mddev->unit) != MD_MAJOR);
	shift = partitioned ? MdpMinorShift : 0;
	unit = MINOR(mddev->unit) >> shift;

	/* wait for any previous instance of this device to be
	 * completely removed (mddev_delayed_delete).
	 */
	flush_workqueue(md_misc_wq);

	mutex_lock(&disks_mutex);
	error = -EEXIST;
	if (mddev->gendisk)
		goto abort;

	if (name) {
		/* Need to ensure that 'name' is not a duplicate.
		 */
		struct mddev *mddev2;
		spin_lock(&all_mddevs_lock);

		list_for_each_entry(mddev2, &all_mddevs, all_mddevs)
			if (mddev2->gendisk &&
			    strcmp(mddev2->gendisk->disk_name, name) == 0) {
				spin_unlock(&all_mddevs_lock);
				goto abort;
			}
		spin_unlock(&all_mddevs_lock);
	}

	error = -ENOMEM;
	mddev->queue = blk_alloc_queue(GFP_KERNEL);
	if (!mddev->queue)
		goto abort;
	mddev->queue->queuedata = mddev;

	blk_queue_make_request(mddev->queue, md_make_request);
	blk_set_stacking_limits(&mddev->queue->limits);

	disk = alloc_disk(1 << shift);
	if (!disk) {
		blk_cleanup_queue(mddev->queue);
		mddev->queue = NULL;
		goto abort;
	}
	disk->major = MAJOR(mddev->unit);
	disk->first_minor = unit << shift;
	if (name)
		strcpy(disk->disk_name, name);
	else if (partitioned)
		sprintf(disk->disk_name, "md_d%d", unit);
	else
		sprintf(disk->disk_name, "md%d", unit);
	disk->fops = &md_fops;
	disk->private_data = mddev;
	disk->queue = mddev->queue;
	blk_queue_flush(mddev->queue, REQ_FLUSH | REQ_FUA);
	/* Allow extended partitions.  This makes the
	 * 'mdp' device redundant, but we can't really
	 * remove it now.
	 */
	disk->flags |= GENHD_FL_EXT_DEVT;
	mddev->gendisk = disk;
	/* As soon as we call add_disk(), another thread could get
	 * through to md_open, so make sure it doesn't get too far
	 */
	mutex_lock(&mddev->open_mutex);
	add_disk(disk);

	error = kobject_init_and_add(&mddev->kobj, &md_ktype,
				     &disk_to_dev(disk)->kobj, "%s", "md");
	if (error) {
		/* This isn't possible, but as kobject_init_and_add is marked
		 * __must_check, we must do something with the result
		 */
		printk(KERN_WARNING "md: cannot register %s/md - name in use\n",
		       disk->disk_name);
		error = 0;
	}
	if (mddev->kobj.sd &&
	    sysfs_create_group(&mddev->kobj, &md_bitmap_group))
		printk(KERN_DEBUG "pointless warning\n");
	mutex_unlock(&mddev->open_mutex);
 abort:
	mutex_unlock(&disks_mutex);
	if (!error && mddev->kobj.sd) {
		kobject_uevent(&mddev->kobj, KOBJ_ADD);
		mddev->sysfs_state = sysfs_get_dirent_safe(mddev->kobj.sd, "array_state");
	}
	mddev_put(mddev);
	return error;
}

static struct kobject *md_probe(dev_t dev, int *part, void *data)
{
	md_alloc(dev, NULL);
	return NULL;
}

static int add_named_array(const char *val, struct kernel_param *kp)
{
	/* val must be "md_*" where * is not all digits.
	 * We allocate an array with a large free minor number, and
	 * set the name to val.  val must not already be an active name.
	 */
	int len = strlen(val);
	char buf[DISK_NAME_LEN];

	while (len && val[len-1] == '\n')
		len--;
	if (len >= DISK_NAME_LEN)
		return -E2BIG;
	strlcpy(buf, val, len+1);
	if (strncmp(buf, "md_", 3) != 0)
		return -EINVAL;
	return md_alloc(0, buf);
}

static void md_safemode_timeout(unsigned long data)
{
	struct mddev *mddev = (struct mddev *) data;

	if (!atomic_read(&mddev->writes_pending)) {
		mddev->safemode = 1;
		if (mddev->external)
			sysfs_notify_dirent_safe(mddev->sysfs_state);
	}
	md_wakeup_thread(mddev->thread);
}

static int start_dirty_degraded;

int md_run(struct mddev *mddev)
{
	int err;
	struct md_rdev *rdev;
	struct md_personality *pers;

	if (list_empty(&mddev->disks))
		/* cannot run an array with no devices.. */
		return -EINVAL;

	if (mddev->pers)
		return -EBUSY;
	/* Cannot run until previous stop completes properly */
	if (mddev->sysfs_active)
		return -EBUSY;

	/*
	 * Analyze all RAID superblock(s)
	 */
	if (!mddev->raid_disks) {
		if (!mddev->persistent)
			return -EINVAL;
		analyze_sbs(mddev);
	}

	if (mddev->level != LEVEL_NONE)
		request_module("md-level-%d", mddev->level);
	else if (mddev->clevel[0])
		request_module("md-%s", mddev->clevel);

	/*
	 * Drop all container device buffers, from now on
	 * the only valid external interface is through the md
	 * device.
	 */
	rdev_for_each(rdev, mddev) {
		if (test_bit(Faulty, &rdev->flags))
			continue;
		sync_blockdev(rdev->bdev);
		invalidate_bdev(rdev->bdev);

		/* perform some consistency tests on the device.
		 * We don't want the data to overlap the metadata,
		 * Internal Bitmap issues have been handled elsewhere.
		 */
		if (rdev->meta_bdev) {
			/* Nothing to check */;
		} else if (rdev->data_offset < rdev->sb_start) {
			if (mddev->dev_sectors &&
			    rdev->data_offset + mddev->dev_sectors
			    > rdev->sb_start) {
				printk("md: %s: data overlaps metadata\n",
				       mdname(mddev));
				return -EINVAL;
			}
		} else {
			if (rdev->sb_start + rdev->sb_size/512
			    > rdev->data_offset) {
				printk("md: %s: metadata overlaps data\n",
				       mdname(mddev));
				return -EINVAL;
			}
		}
		sysfs_notify_dirent_safe(rdev->sysfs_state);
	}

	if (mddev->bio_set == NULL)
		mddev->bio_set = bioset_create(BIO_POOL_SIZE,
					       sizeof(struct mddev *));

	spin_lock(&pers_lock);
	pers = find_pers(mddev->level, mddev->clevel);
	if (!pers || !try_module_get(pers->owner)) {
		spin_unlock(&pers_lock);
		if (mddev->level != LEVEL_NONE)
			printk(KERN_WARNING "md: personality for level %d is not loaded!\n",
			       mddev->level);
		else
			printk(KERN_WARNING "md: personality for level %s is not loaded!\n",
			       mddev->clevel);
		return -EINVAL;
	}
	mddev->pers = pers;
	spin_unlock(&pers_lock);
	if (mddev->level != pers->level) {
		mddev->level = pers->level;
		mddev->new_level = pers->level;
	}
	strlcpy(mddev->clevel, pers->name, sizeof(mddev->clevel));

	if (mddev->reshape_position != MaxSector &&
	    pers->start_reshape == NULL) {
		/* This personality cannot handle reshaping... */
		mddev->pers = NULL;
		module_put(pers->owner);
		return -EINVAL;
	}

	if (pers->sync_request) {
		/* Warn if this is a potentially silly
		 * configuration.
		 */
		char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
		struct md_rdev *rdev2;
		int warned = 0;

		rdev_for_each(rdev, mddev)
			rdev_for_each(rdev2, mddev) {
				if (rdev < rdev2 &&
				    rdev->bdev->bd_contains ==
				    rdev2->bdev->bd_contains) {
					printk(KERN_WARNING
					       "%s: WARNING: %s appears to be"
					       " on the same physical disk as"
					       " %s.\n",
					       mdname(mddev),
					       bdevname(rdev->bdev,b),
					       bdevname(rdev2->bdev,b2));
					warned = 1;
				}
			}

		if (warned)
			printk(KERN_WARNING
			       "True protection against single-disk"
			       " failure might be compromised.\n");
	}

	mddev->recovery = 0;
	/* may be over-ridden by personality */
	mddev->resync_max_sectors = mddev->dev_sectors;

	mddev->ok_start_degraded = start_dirty_degraded;

	if (start_readonly && mddev->ro == 0)
		mddev->ro = 2; /* read-only, but switch on first write */

	err = mddev->pers->run(mddev);
	if (err)
		printk(KERN_ERR "md: pers->run() failed ...\n");
	else if (mddev->pers->size(mddev, 0, 0) < mddev->array_sectors) {
		WARN_ONCE(!mddev->external_size, "%s: default size too small,"
			  " but 'external_size' not in effect?\n", __func__);
		printk(KERN_ERR
		       "md: invalid array_size %llu > default size %llu\n",
		       (unsigned long long)mddev->array_sectors / 2,
		       (unsigned long long)mddev->pers->size(mddev, 0, 0) / 2);
		err = -EINVAL;
		mddev->pers->stop(mddev);
	}
	if (err == 0 && mddev->pers->sync_request &&
	    (mddev->bitmap_info.file || mddev->bitmap_info.offset)) {
		err = bitmap_create(mddev);
		if (err) {
			printk(KERN_ERR "%s: failed to create bitmap (%d)\n",
			       mdname(mddev), err);
			mddev->pers->stop(mddev);
		}
	}
	if (err) {
		module_put(mddev->pers->owner);
		mddev->pers = NULL;
		bitmap_destroy(mddev);
		return err;
	}
	if (mddev->pers->sync_request) {
		if (mddev->kobj.sd &&
		    sysfs_create_group(&mddev->kobj, &md_redundancy_group))
			printk(KERN_WARNING
			       "md: cannot register extra attributes for %s\n",
			       mdname(mddev));
		mddev->sysfs_action = sysfs_get_dirent_safe(mddev->kobj.sd, "sync_action");
	} else if (mddev->ro == 2) /* auto-readonly not meaningful */
		mddev->ro = 0;

 	atomic_set(&mddev->writes_pending,0);
	atomic_set(&mddev->max_corr_read_errors,
		   MD_DEFAULT_MAX_CORRECTED_READ_ERRORS);
	mddev->safemode = 0;
	mddev->safemode_timer.function = md_safemode_timeout;
	mddev->safemode_timer.data = (unsigned long) mddev;
	mddev->safemode_delay = (200 * HZ)/1000 +1; /* 200 msec delay */
	mddev->in_sync = 1;
	smp_wmb();
	mddev->ready = 1;
	rdev_for_each(rdev, mddev)
		if (rdev->raid_disk >= 0)
			if (sysfs_link_rdev(mddev, rdev))
				/* failure here is OK */;
	
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	
	if (mddev->flags)
		md_update_sb(mddev, 0);

	md_new_event(mddev);
	sysfs_notify_dirent_safe(mddev->sysfs_state);
	sysfs_notify_dirent_safe(mddev->sysfs_action);
	sysfs_notify(&mddev->kobj, NULL, "degraded");
	return 0;
}
EXPORT_SYMBOL_GPL(md_run);

static int do_md_run(struct mddev *mddev)
{
	int err;

	err = md_run(mddev);
	if (err)
		goto out;
	err = bitmap_load(mddev);
	if (err) {
		bitmap_destroy(mddev);
		goto out;
	}

	md_wakeup_thread(mddev->thread);
	md_wakeup_thread(mddev->sync_thread); /* possibly kick off a reshape */

	set_capacity(mddev->gendisk, mddev->array_sectors);
	revalidate_disk(mddev->gendisk);
	mddev->changed = 1;
	kobject_uevent(&disk_to_dev(mddev->gendisk)->kobj, KOBJ_CHANGE);
out:
	return err;
}

static int restart_array(struct mddev *mddev)
{
	struct gendisk *disk = mddev->gendisk;

	/* Complain if it has no devices */
	if (list_empty(&mddev->disks))
		return -ENXIO;
	if (!mddev->pers)
		return -EINVAL;
	if (!mddev->ro)
		return -EBUSY;
	mddev->safemode = 0;
	mddev->ro = 0;
	set_disk_ro(disk, 0);
	printk(KERN_INFO "md: %s switched to read-write mode.\n",
		mdname(mddev));
	/* Kick recovery or resync if necessary */
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
	md_wakeup_thread(mddev->sync_thread);
	sysfs_notify_dirent_safe(mddev->sysfs_state);
	return 0;
}

/* similar to deny_write_access, but accounts for our holding a reference
 * to the file ourselves */
static int deny_bitmap_write_access(struct file * file)
{
	struct inode *inode = file->f_mapping->host;

	spin_lock(&inode->i_lock);
	if (atomic_read(&inode->i_writecount) > 1) {
		spin_unlock(&inode->i_lock);
		return -ETXTBSY;
	}
	atomic_set(&inode->i_writecount, -1);
	spin_unlock(&inode->i_lock);

	return 0;
}

void restore_bitmap_write_access(struct file *file)
{
	struct inode *inode = file->f_mapping->host;

	spin_lock(&inode->i_lock);
	atomic_set(&inode->i_writecount, 1);
	spin_unlock(&inode->i_lock);
}

static void md_clean(struct mddev *mddev)
{
	mddev->array_sectors = 0;
	mddev->external_size = 0;
	mddev->dev_sectors = 0;
	mddev->raid_disks = 0;
	mddev->recovery_cp = 0;
	mddev->resync_min = 0;
	mddev->resync_max = MaxSector;
	mddev->reshape_position = MaxSector;
	mddev->external = 0;
	mddev->persistent = 0;
	mddev->level = LEVEL_NONE;
	mddev->clevel[0] = 0;
	mddev->flags = 0;
	mddev->ro = 0;
	mddev->metadata_type[0] = 0;
	mddev->chunk_sectors = 0;
	mddev->ctime = mddev->utime = 0;
	mddev->layout = 0;
	mddev->max_disks = 0;
	mddev->events = 0;
	mddev->can_decrease_events = 0;
	mddev->delta_disks = 0;
	mddev->reshape_backwards = 0;
	mddev->new_level = LEVEL_NONE;
	mddev->new_layout = 0;
	mddev->new_chunk_sectors = 0;
	mddev->curr_resync = 0;
	mddev->resync_mismatches = 0;
	mddev->suspend_lo = mddev->suspend_hi = 0;
	mddev->sync_speed_min = mddev->sync_speed_max = 0;
	mddev->recovery = 0;
	mddev->in_sync = 0;
	mddev->changed = 0;
	mddev->degraded = 0;
	mddev->safemode = 0;
	mddev->merge_check_needed = 0;
	mddev->bitmap_info.offset = 0;
	mddev->bitmap_info.default_offset = 0;
	mddev->bitmap_info.default_space = 0;
	mddev->bitmap_info.chunksize = 0;
	mddev->bitmap_info.daemon_sleep = 0;
	mddev->bitmap_info.max_write_behind = 0;
}

static void __md_stop_writes(struct mddev *mddev)
{
	if (mddev->sync_thread) {
		set_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
		set_bit(MD_RECOVERY_INTR, &mddev->recovery);
		reap_sync_thread(mddev);
	}

	del_timer_sync(&mddev->safemode_timer);

	bitmap_flush(mddev);
	md_super_wait(mddev);

	if (!mddev->in_sync || mddev->flags) {
		/* mark array as shutdown cleanly */
		mddev->in_sync = 1;
		md_update_sb(mddev, 1);
	}
}

void md_stop_writes(struct mddev *mddev)
{
	mddev_lock(mddev);
	__md_stop_writes(mddev);
	mddev_unlock(mddev);
}
EXPORT_SYMBOL_GPL(md_stop_writes);

void md_stop(struct mddev *mddev)
{
	mddev->ready = 0;
	mddev->pers->stop(mddev);
	if (mddev->pers->sync_request && mddev->to_remove == NULL)
		mddev->to_remove = &md_redundancy_group;
	module_put(mddev->pers->owner);
	mddev->pers = NULL;
	clear_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
}
EXPORT_SYMBOL_GPL(md_stop);

static int md_set_readonly(struct mddev *mddev, int is_open)
{
	int err = 0;
	mutex_lock(&mddev->open_mutex);
	if (atomic_read(&mddev->openers) > is_open) {
		printk("md: %s still in use.\n",mdname(mddev));
		err = -EBUSY;
		goto out;
	}
	if (mddev->pers) {
		__md_stop_writes(mddev);

		err  = -ENXIO;
		if (mddev->ro==1)
			goto out;
		mddev->ro = 1;
		set_disk_ro(mddev->gendisk, 1);
		clear_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
		sysfs_notify_dirent_safe(mddev->sysfs_state);
		err = 0;	
	}
out:
	mutex_unlock(&mddev->open_mutex);
	return err;
}

/* mode:
 *   0 - completely stop and dis-assemble array
 *   2 - stop but do not disassemble array
 */
static int do_md_stop(struct mddev * mddev, int mode, int is_open)
{
	struct gendisk *disk = mddev->gendisk;
	struct md_rdev *rdev;

	mutex_lock(&mddev->open_mutex);
	if (atomic_read(&mddev->openers) > is_open ||
	    mddev->sysfs_active) {
		printk("md: %s still in use.\n",mdname(mddev));
		mutex_unlock(&mddev->open_mutex);
		return -EBUSY;
	}

	if (mddev->pers) {
		if (mddev->ro)
			set_disk_ro(disk, 0);

		__md_stop_writes(mddev);
		md_stop(mddev);
		mddev->queue->merge_bvec_fn = NULL;
		mddev->queue->backing_dev_info.congested_fn = NULL;

		/* tell userspace to handle 'inactive' */
		sysfs_notify_dirent_safe(mddev->sysfs_state);

		rdev_for_each(rdev, mddev)
			if (rdev->raid_disk >= 0)
				sysfs_unlink_rdev(mddev, rdev);

		set_capacity(disk, 0);
		mutex_unlock(&mddev->open_mutex);
		mddev->changed = 1;
		revalidate_disk(disk);

		if (mddev->ro)
			mddev->ro = 0;
	} else
		mutex_unlock(&mddev->open_mutex);
	/*
	 * Free resources if final stop
	 */
	if (mode == 0) {
		printk(KERN_INFO "md: %s stopped.\n", mdname(mddev));

		bitmap_destroy(mddev);
		if (mddev->bitmap_info.file) {
			restore_bitmap_write_access(mddev->bitmap_info.file);
			fput(mddev->bitmap_info.file);
			mddev->bitmap_info.file = NULL;
		}
		mddev->bitmap_info.offset = 0;

		export_array(mddev);

		md_clean(mddev);
		kobject_uevent(&disk_to_dev(mddev->gendisk)->kobj, KOBJ_CHANGE);
		if (mddev->hold_active == UNTIL_STOP)
			mddev->hold_active = 0;
	}
	blk_integrity_unregister(disk);
	md_new_event(mddev);
	sysfs_notify_dirent_safe(mddev->sysfs_state);
	return 0;
}

#ifndef MODULE
static void autorun_array(struct mddev *mddev)
{
	struct md_rdev *rdev;
	int err;

	if (list_empty(&mddev->disks))
		return;

	printk(KERN_INFO "md: running: ");

	rdev_for_each(rdev, mddev) {
		char b[BDEVNAME_SIZE];
		printk("<%s>", bdevname(rdev->bdev,b));
	}
	printk("\n");

	err = do_md_run(mddev);
	if (err) {
		printk(KERN_WARNING "md: do_md_run() returned %d\n", err);
		do_md_stop(mddev, 0, 0);
	}
}

/*
 * lets try to run arrays based on all disks that have arrived
 * until now. (those are in pending_raid_disks)
 *
 * the method: pick the first pending disk, collect all disks with
 * the same UUID, remove all from the pending list and put them into
 * the 'same_array' list. Then order this list based on superblock
 * update time (freshest comes first), kick out 'old' disks and
 * compare superblocks. If everything's fine then run it.
 *
 * If "unit" is allocated, then bump its reference count
 */
static void autorun_devices(int part)
{
	struct md_rdev *rdev0, *rdev, *tmp;
	struct mddev *mddev;
	char b[BDEVNAME_SIZE];

	printk(KERN_INFO "md: autorun ...\n");
	while (!list_empty(&pending_raid_disks)) {
		int unit;
		dev_t dev;
		LIST_HEAD(candidates);
		rdev0 = list_entry(pending_raid_disks.next,
					 struct md_rdev, same_set);

		printk(KERN_INFO "md: considering %s ...\n",
			bdevname(rdev0->bdev,b));
		INIT_LIST_HEAD(&candidates);
		rdev_for_each_list(rdev, tmp, &pending_raid_disks)
			if (super_90_load(rdev, rdev0, 0) >= 0) {
				printk(KERN_INFO "md:  adding %s ...\n",
					bdevname(rdev->bdev,b));
				list_move(&rdev->same_set, &candidates);
			}
		/*
		 * now we have a set of devices, with all of them having
		 * mostly sane superblocks. It's time to allocate the
		 * mddev.
		 */
		if (part) {
			dev = MKDEV(mdp_major,
				    rdev0->preferred_minor << MdpMinorShift);
			unit = MINOR(dev) >> MdpMinorShift;
		} else {
			dev = MKDEV(MD_MAJOR, rdev0->preferred_minor);
			unit = MINOR(dev);
		}
		if (rdev0->preferred_minor != unit) {
			printk(KERN_INFO "md: unit number in %s is bad: %d\n",
			       bdevname(rdev0->bdev, b), rdev0->preferred_minor);
			break;
		}

		md_probe(dev, NULL, NULL);
		mddev = mddev_find(dev);
		if (!mddev || !mddev->gendisk) {
			if (mddev)
				mddev_put(mddev);
			printk(KERN_ERR
				"md: cannot allocate memory for md drive.\n");
			break;
		}
		if (mddev_lock(mddev)) 
			printk(KERN_WARNING "md: %s locked, cannot run\n",
			       mdname(mddev));
		else if (mddev->raid_disks || mddev->major_version
			 || !list_empty(&mddev->disks)) {
			printk(KERN_WARNING 
				"md: %s already running, cannot run %s\n",
				mdname(mddev), bdevname(rdev0->bdev,b));
			mddev_unlock(mddev);
		} else {
			printk(KERN_INFO "md: created %s\n", mdname(mddev));
			mddev->persistent = 1;
			rdev_for_each_list(rdev, tmp, &candidates) {
				list_del_init(&rdev->same_set);
				if (bind_rdev_to_array(rdev, mddev))
					export_rdev(rdev);
			}
			autorun_array(mddev);
			mddev_unlock(mddev);
		}
		/* on success, candidates will be empty, on error
		 * it won't...
		 */
		rdev_for_each_list(rdev, tmp, &candidates) {
			list_del_init(&rdev->same_set);
			export_rdev(rdev);
		}
		mddev_put(mddev);
	}
	printk(KERN_INFO "md: ... autorun DONE.\n");
}
#endif /* !MODULE */

static int get_version(void __user * arg)
{
	mdu_version_t ver;

	ver.major = MD_MAJOR_VERSION;
	ver.minor = MD_MINOR_VERSION;
	ver.patchlevel = MD_PATCHLEVEL_VERSION;

	if (copy_to_user(arg, &ver, sizeof(ver)))
		return -EFAULT;

	return 0;
}

static int get_array_info(struct mddev * mddev, void __user * arg)
{
	mdu_array_info_t info;
	int nr,working,insync,failed,spare;
	struct md_rdev *rdev;

	nr=working=insync=failed=spare=0;
	rdev_for_each(rdev, mddev) {
		nr++;
		if (test_bit(Faulty, &rdev->flags))
			failed++;
		else {
			working++;
			if (test_bit(In_sync, &rdev->flags))
				insync++;	
			else
				spare++;
		}
	}

	info.major_version = mddev->major_version;
	info.minor_version = mddev->minor_version;
	info.patch_version = MD_PATCHLEVEL_VERSION;
	info.ctime         = mddev->ctime;
	info.level         = mddev->level;
	info.size          = mddev->dev_sectors / 2;
	if (info.size != mddev->dev_sectors / 2) /* overflow */
		info.size = -1;
	info.nr_disks      = nr;
	info.raid_disks    = mddev->raid_disks;
	info.md_minor      = mddev->md_minor;
	info.not_persistent= !mddev->persistent;

	info.utime         = mddev->utime;
	info.state         = 0;
	if (mddev->in_sync)
		info.state = (1<<MD_SB_CLEAN);
	if (mddev->bitmap && mddev->bitmap_info.offset)
		info.state = (1<<MD_SB_BITMAP_PRESENT);
	info.active_disks  = insync;
	info.working_disks = working;
	info.failed_disks  = failed;
	info.spare_disks   = spare;

	info.layout        = mddev->layout;
	info.chunk_size    = mddev->chunk_sectors << 9;

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int get_bitmap_file(struct mddev * mddev, void __user * arg)
{
	mdu_bitmap_file_t *file = NULL; /* too big for stack allocation */
	char *ptr, *buf = NULL;
	int err = -ENOMEM;

	if (md_allow_write(mddev))
		file = kmalloc(sizeof(*file), GFP_NOIO);
	else
		file = kmalloc(sizeof(*file), GFP_KERNEL);

	if (!file)
		goto out;

	/* bitmap disabled, zero the first byte and copy out */
	if (!mddev->bitmap || !mddev->bitmap->storage.file) {
		file->pathname[0] = '\0';
		goto copy_out;
	}

	buf = kmalloc(sizeof(file->pathname), GFP_KERNEL);
	if (!buf)
		goto out;

	ptr = d_path(&mddev->bitmap->storage.file->f_path,
		     buf, sizeof(file->pathname));
	if (IS_ERR(ptr))
		goto out;

	strcpy(file->pathname, ptr);

copy_out:
	err = 0;
	if (copy_to_user(arg, file, sizeof(*file)))
		err = -EFAULT;
out:
	kfree(buf);
	kfree(file);
	return err;
}

static int get_disk_info(struct mddev * mddev, void __user * arg)
{
	mdu_disk_info_t info;
	struct md_rdev *rdev;

	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;

	rdev = find_rdev_nr(mddev, info.number);
	if (rdev) {
		info.major = MAJOR(rdev->bdev->bd_dev);
		info.minor = MINOR(rdev->bdev->bd_dev);
		info.raid_disk = rdev->raid_disk;
		info.state = 0;
		if (test_bit(Faulty, &rdev->flags))
			info.state |= (1<<MD_DISK_FAULTY);
		else if (test_bit(In_sync, &rdev->flags)) {
			info.state |= (1<<MD_DISK_ACTIVE);
			info.state |= (1<<MD_DISK_SYNC);
		}
		if (test_bit(WriteMostly, &rdev->flags))
			info.state |= (1<<MD_DISK_WRITEMOSTLY);
	} else {
		info.major = info.minor = 0;
		info.raid_disk = -1;
		info.state = (1<<MD_DISK_REMOVED);
	}

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int add_new_disk(struct mddev * mddev, mdu_disk_info_t *info)
{
	char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
	struct md_rdev *rdev;
	dev_t dev = MKDEV(info->major,info->minor);

	if (info->major != MAJOR(dev) || info->minor != MINOR(dev))
		return -EOVERFLOW;

	if (!mddev->raid_disks) {
		int err;
		/* expecting a device which has a superblock */
		rdev = md_import_device(dev, mddev->major_version, mddev->minor_version);
		if (IS_ERR(rdev)) {
			printk(KERN_WARNING 
				"md: md_import_device returned %ld\n",
				PTR_ERR(rdev));
			return PTR_ERR(rdev);
		}
		if (!list_empty(&mddev->disks)) {
			struct md_rdev *rdev0
				= list_entry(mddev->disks.next,
					     struct md_rdev, same_set);
			err = super_types[mddev->major_version]
				.load_super(rdev, rdev0, mddev->minor_version);
			if (err < 0) {
				printk(KERN_WARNING 
					"md: %s has different UUID to %s\n",
					bdevname(rdev->bdev,b), 
					bdevname(rdev0->bdev,b2));
				export_rdev(rdev);
				return -EINVAL;
			}
		}
		err = bind_rdev_to_array(rdev, mddev);
		if (err)
			export_rdev(rdev);
		return err;
	}

	/*
	 * add_new_disk can be used once the array is assembled
	 * to add "hot spares".  They must already have a superblock
	 * written
	 */
	if (mddev->pers) {
		int err;
		if (!mddev->pers->hot_add_disk) {
			printk(KERN_WARNING 
				"%s: personality does not support diskops!\n",
			       mdname(mddev));
			return -EINVAL;
		}
		if (mddev->persistent)
			rdev = md_import_device(dev, mddev->major_version,
						mddev->minor_version);
		else
			rdev = md_import_device(dev, -1, -1);
		if (IS_ERR(rdev)) {
			printk(KERN_WARNING 
				"md: md_import_device returned %ld\n",
				PTR_ERR(rdev));
			return PTR_ERR(rdev);
		}
		/* set saved_raid_disk if appropriate */
		if (!mddev->persistent) {
			if (info->state & (1<<MD_DISK_SYNC)  &&
			    info->raid_disk < mddev->raid_disks) {
				rdev->raid_disk = info->raid_disk;
				set_bit(In_sync, &rdev->flags);
			} else
				rdev->raid_disk = -1;
		} else
			super_types[mddev->major_version].
				validate_super(mddev, rdev);
		if ((info->state & (1<<MD_DISK_SYNC)) &&
		    (!test_bit(In_sync, &rdev->flags) ||
		     rdev->raid_disk != info->raid_disk)) {
			/* This was a hot-add request, but events doesn't
			 * match, so reject it.
			 */
			export_rdev(rdev);
			return -EINVAL;
		}

		if (test_bit(In_sync, &rdev->flags))
			rdev->saved_raid_disk = rdev->raid_disk;
		else
			rdev->saved_raid_disk = -1;

		clear_bit(In_sync, &rdev->flags); /* just to be sure */
		if (info->state & (1<<MD_DISK_WRITEMOSTLY))
			set_bit(WriteMostly, &rdev->flags);
		else
			clear_bit(WriteMostly, &rdev->flags);

		rdev->raid_disk = -1;
		err = bind_rdev_to_array(rdev, mddev);
		if (!err && !mddev->pers->hot_remove_disk) {
			/* If there is hot_add_disk but no hot_remove_disk
			 * then added disks for geometry changes,
			 * and should be added immediately.
			 */
			super_types[mddev->major_version].
				validate_super(mddev, rdev);
			err = mddev->pers->hot_add_disk(mddev, rdev);
			if (err)
				unbind_rdev_from_array(rdev);
		}
		if (err)
			export_rdev(rdev);
		else
			sysfs_notify_dirent_safe(rdev->sysfs_state);

		md_update_sb(mddev, 1);
		if (mddev->degraded)
			set_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		if (!err)
			md_new_event(mddev);
		md_wakeup_thread(mddev->thread);
		return err;
	}

	/* otherwise, add_new_disk is only allowed
	 * for major_version==0 superblocks
	 */
	if (mddev->major_version != 0) {
		printk(KERN_WARNING "%s: ADD_NEW_DISK not supported\n",
		       mdname(mddev));
		return -EINVAL;
	}

	if (!(info->state & (1<<MD_DISK_FAULTY))) {
		int err;
		rdev = md_import_device(dev, -1, 0);
		if (IS_ERR(rdev)) {
			printk(KERN_WARNING 
				"md: error, md_import_device() returned %ld\n",
				PTR_ERR(rdev));
			return PTR_ERR(rdev);
		}
		rdev->desc_nr = info->number;
		if (info->raid_disk < mddev->raid_disks)
			rdev->raid_disk = info->raid_disk;
		else
			rdev->raid_disk = -1;

		if (rdev->raid_disk < mddev->raid_disks)
			if (info->state & (1<<MD_DISK_SYNC))
				set_bit(In_sync, &rdev->flags);

		if (info->state & (1<<MD_DISK_WRITEMOSTLY))
			set_bit(WriteMostly, &rdev->flags);

		if (!mddev->persistent) {
			printk(KERN_INFO "md: nonpersistent superblock ...\n");
			rdev->sb_start = i_size_read(rdev->bdev->bd_inode) / 512;
		} else
			rdev->sb_start = calc_dev_sboffset(rdev);
		rdev->sectors = rdev->sb_start;

		err = bind_rdev_to_array(rdev, mddev);
		if (err) {
			export_rdev(rdev);
			return err;
		}
	}

	return 0;
}

static int hot_remove_disk(struct mddev * mddev, dev_t dev)
{
	char b[BDEVNAME_SIZE];
	struct md_rdev *rdev;

	rdev = find_rdev(mddev, dev);
	if (!rdev)
		return -ENXIO;

	if (rdev->raid_disk >= 0)
		goto busy;

	kick_rdev_from_array(rdev);
	md_update_sb(mddev, 1);
	md_new_event(mddev);

	return 0;
busy:
	printk(KERN_WARNING "md: cannot remove active disk %s from %s ...\n",
		bdevname(rdev->bdev,b), mdname(mddev));
	return -EBUSY;
}

static int hot_add_disk(struct mddev * mddev, dev_t dev)
{
	char b[BDEVNAME_SIZE];
	int err;
	struct md_rdev *rdev;

	if (!mddev->pers)
		return -ENODEV;

	if (mddev->major_version != 0) {
		printk(KERN_WARNING "%s: HOT_ADD may only be used with"
			" version-0 superblocks.\n",
			mdname(mddev));
		return -EINVAL;
	}
	if (!mddev->pers->hot_add_disk) {
		printk(KERN_WARNING 
			"%s: personality does not support diskops!\n",
			mdname(mddev));
		return -EINVAL;
	}

	rdev = md_import_device(dev, -1, 0);
	if (IS_ERR(rdev)) {
		printk(KERN_WARNING 
			"md: error, md_import_device() returned %ld\n",
			PTR_ERR(rdev));
		return -EINVAL;
	}

	if (mddev->persistent)
		rdev->sb_start = calc_dev_sboffset(rdev);
	else
		rdev->sb_start = i_size_read(rdev->bdev->bd_inode) / 512;

	rdev->sectors = rdev->sb_start;

	if (test_bit(Faulty, &rdev->flags)) {
		printk(KERN_WARNING 
			"md: can not hot-add faulty %s disk to %s!\n",
			bdevname(rdev->bdev,b), mdname(mddev));
		err = -EINVAL;
		goto abort_export;
	}
	clear_bit(In_sync, &rdev->flags);
	rdev->desc_nr = -1;
	rdev->saved_raid_disk = -1;
	err = bind_rdev_to_array(rdev, mddev);
	if (err)
		goto abort_export;

	/*
	 * The rest should better be atomic, we can have disk failures
	 * noticed in interrupt contexts ...
	 */

	rdev->raid_disk = -1;

	md_update_sb(mddev, 1);

	/*
	 * Kick recovery, maybe this spare has to be added to the
	 * array immediately.
	 */
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
	md_new_event(mddev);
	return 0;

abort_export:
	export_rdev(rdev);
	return err;
}

static int set_bitmap_file(struct mddev *mddev, int fd)
{
	int err;

	if (mddev->pers) {
		if (!mddev->pers->quiesce)
			return -EBUSY;
		if (mddev->recovery || mddev->sync_thread)
			return -EBUSY;
		/* we should be able to change the bitmap.. */
	}


	if (fd >= 0) {
		if (mddev->bitmap)
			return -EEXIST; /* cannot add when bitmap is present */
		mddev->bitmap_info.file = fget(fd);

		if (mddev->bitmap_info.file == NULL) {
			printk(KERN_ERR "%s: error: failed to get bitmap file\n",
			       mdname(mddev));
			return -EBADF;
		}

		err = deny_bitmap_write_access(mddev->bitmap_info.file);
		if (err) {
			printk(KERN_ERR "%s: error: bitmap file is already in use\n",
			       mdname(mddev));
			fput(mddev->bitmap_info.file);
			mddev->bitmap_info.file = NULL;
			return err;
		}
		mddev->bitmap_info.offset = 0; /* file overrides offset */
	} else if (mddev->bitmap == NULL)
		return -ENOENT; /* cannot remove what isn't there */
	err = 0;
	if (mddev->pers) {
		mddev->pers->quiesce(mddev, 1);
		if (fd >= 0) {
			err = bitmap_create(mddev);
			if (!err)
				err = bitmap_load(mddev);
		}
		if (fd < 0 || err) {
			bitmap_destroy(mddev);
			fd = -1; /* make sure to put the file */
		}
		mddev->pers->quiesce(mddev, 0);
	}
	if (fd < 0) {
		if (mddev->bitmap_info.file) {
			restore_bitmap_write_access(mddev->bitmap_info.file);
			fput(mddev->bitmap_info.file);
		}
		mddev->bitmap_info.file = NULL;
	}

	return err;
}

/*
 * set_array_info is used two different ways
 * The original usage is when creating a new array.
 * In this usage, raid_disks is > 0 and it together with
 *  level, size, not_persistent,layout,chunksize determine the
 *  shape of the array.
 *  This will always create an array with a type-0.90.0 superblock.
 * The newer usage is when assembling an array.
 *  In this case raid_disks will be 0, and the major_version field is
 *  use to determine which style super-blocks are to be found on the devices.
 *  The minor and patch _version numbers are also kept incase the
 *  super_block handler wishes to interpret them.
 */
static int set_array_info(struct mddev * mddev, mdu_array_info_t *info)
{

	if (info->raid_disks == 0) {
		/* just setting version number for superblock loading */
		if (info->major_version < 0 ||
		    info->major_version >= ARRAY_SIZE(super_types) ||
		    super_types[info->major_version].name == NULL) {
			/* maybe try to auto-load a module? */
			printk(KERN_INFO 
				"md: superblock version %d not known\n",
				info->major_version);
			return -EINVAL;
		}
		mddev->major_version = info->major_version;
		mddev->minor_version = info->minor_version;
		mddev->patch_version = info->patch_version;
		mddev->persistent = !info->not_persistent;
		/* ensure mddev_put doesn't delete this now that there
		 * is some minimal configuration.
		 */
		mddev->ctime         = get_seconds();
		return 0;
	}
	mddev->major_version = MD_MAJOR_VERSION;
	mddev->minor_version = MD_MINOR_VERSION;
	mddev->patch_version = MD_PATCHLEVEL_VERSION;
	mddev->ctime         = get_seconds();

	mddev->level         = info->level;
	mddev->clevel[0]     = 0;
	mddev->dev_sectors   = 2 * (sector_t)info->size;
	mddev->raid_disks    = info->raid_disks;
	/* don't set md_minor, it is determined by which /dev/md* was
	 * openned
	 */
	if (info->state & (1<<MD_SB_CLEAN))
		mddev->recovery_cp = MaxSector;
	else
		mddev->recovery_cp = 0;
	mddev->persistent    = ! info->not_persistent;
	mddev->external	     = 0;

	mddev->layout        = info->layout;
	mddev->chunk_sectors = info->chunk_size >> 9;

	mddev->max_disks     = MD_SB_DISKS;

	if (mddev->persistent)
		mddev->flags         = 0;
	set_bit(MD_CHANGE_DEVS, &mddev->flags);

	mddev->bitmap_info.default_offset = MD_SB_BYTES >> 9;
	mddev->bitmap_info.default_space = 64*2 - (MD_SB_BYTES >> 9);
	mddev->bitmap_info.offset = 0;

	mddev->reshape_position = MaxSector;

	/*
	 * Generate a 128 bit UUID
	 */
	get_random_bytes(mddev->uuid, 16);

	mddev->new_level = mddev->level;
	mddev->new_chunk_sectors = mddev->chunk_sectors;
	mddev->new_layout = mddev->layout;
	mddev->delta_disks = 0;
	mddev->reshape_backwards = 0;

	return 0;
}

void md_set_array_sectors(struct mddev *mddev, sector_t array_sectors)
{
	WARN(!mddev_is_locked(mddev), "%s: unlocked mddev!\n", __func__);

	if (mddev->external_size)
		return;

	mddev->array_sectors = array_sectors;
}
EXPORT_SYMBOL(md_set_array_sectors);

static int update_size(struct mddev *mddev, sector_t num_sectors)
{
	struct md_rdev *rdev;
	int rv;
	int fit = (num_sectors == 0);

	if (mddev->pers->resize == NULL)
		return -EINVAL;
	/* The "num_sectors" is the number of sectors of each device that
	 * is used.  This can only make sense for arrays with redundancy.
	 * linear and raid0 always use whatever space is available. We can only
	 * consider changing this number if no resync or reconstruction is
	 * happening, and if the new size is acceptable. It must fit before the
	 * sb_start or, if that is <data_offset, it must fit before the size
	 * of each device.  If num_sectors is zero, we find the largest size
	 * that fits.
	 */
	if (mddev->sync_thread)
		return -EBUSY;
	if (mddev->bitmap)
		/* Sorry, cannot grow a bitmap yet, just remove it,
		 * grow, and re-add.
		 */
		return -EBUSY;
	rdev_for_each(rdev, mddev) {
		sector_t avail = rdev->sectors;

		if (fit && (num_sectors == 0 || num_sectors > avail))
			num_sectors = avail;
		if (avail < num_sectors)
			return -ENOSPC;
	}
	rv = mddev->pers->resize(mddev, num_sectors);
	if (!rv)
		revalidate_disk(mddev->gendisk);
	return rv;
}

static int update_raid_disks(struct mddev *mddev, int raid_disks)
{
	int rv;
	struct md_rdev *rdev;
	/* change the number of raid disks */
	if (mddev->pers->check_reshape == NULL)
		return -EINVAL;
	if (raid_disks <= 0 ||
	    (mddev->max_disks && raid_disks >= mddev->max_disks))
		return -EINVAL;
	if (mddev->sync_thread || mddev->reshape_position != MaxSector)
		return -EBUSY;

	rdev_for_each(rdev, mddev) {
		if (mddev->raid_disks < raid_disks &&
		    rdev->data_offset < rdev->new_data_offset)
			return -EINVAL;
		if (mddev->raid_disks > raid_disks &&
		    rdev->data_offset > rdev->new_data_offset)
			return -EINVAL;
	}

	mddev->delta_disks = raid_disks - mddev->raid_disks;
	if (mddev->delta_disks < 0)
		mddev->reshape_backwards = 1;
	else if (mddev->delta_disks > 0)
		mddev->reshape_backwards = 0;

	rv = mddev->pers->check_reshape(mddev);
	if (rv < 0) {
		mddev->delta_disks = 0;
		mddev->reshape_backwards = 0;
	}
	return rv;
}


/*
 * update_array_info is used to change the configuration of an
 * on-line array.
 * The version, ctime,level,size,raid_disks,not_persistent, layout,chunk_size
 * fields in the info are checked against the array.
 * Any differences that cannot be handled will cause an error.
 * Normally, only one change can be managed at a time.
 */
static int update_array_info(struct mddev *mddev, mdu_array_info_t *info)
{
	int rv = 0;
	int cnt = 0;
	int state = 0;

	/* calculate expected state,ignoring low bits */
	if (mddev->bitmap && mddev->bitmap_info.offset)
		state |= (1 << MD_SB_BITMAP_PRESENT);

	if (mddev->major_version != info->major_version ||
	    mddev->minor_version != info->minor_version ||
/*	    mddev->patch_version != info->patch_version || */
	    mddev->ctime         != info->ctime         ||
	    mddev->level         != info->level         ||
/*	    mddev->layout        != info->layout        || */
	    !mddev->persistent	 != info->not_persistent||
	    mddev->chunk_sectors != info->chunk_size >> 9 ||
	    /* ignore bottom 8 bits of state, and allow SB_BITMAP_PRESENT to change */
	    ((state^info->state) & 0xfffffe00)
		)
		return -EINVAL;
	/* Check there is only one change */
	if (info->size >= 0 && mddev->dev_sectors / 2 != info->size)
		cnt++;
	if (mddev->raid_disks != info->raid_disks)
		cnt++;
	if (mddev->layout != info->layout)
		cnt++;
	if ((state ^ info->state) & (1<<MD_SB_BITMAP_PRESENT))
		cnt++;
	if (cnt == 0)
		return 0;
	if (cnt > 1)
		return -EINVAL;

	if (mddev->layout != info->layout) {
		/* Change layout
		 * we don't need to do anything at the md level, the
		 * personality will take care of it all.
		 */
		if (mddev->pers->check_reshape == NULL)
			return -EINVAL;
		else {
			mddev->new_layout = info->layout;
			rv = mddev->pers->check_reshape(mddev);
			if (rv)
				mddev->new_layout = mddev->layout;
			return rv;
		}
	}
	if (info->size >= 0 && mddev->dev_sectors / 2 != info->size)
		rv = update_size(mddev, (sector_t)info->size * 2);

	if (mddev->raid_disks    != info->raid_disks)
		rv = update_raid_disks(mddev, info->raid_disks);

	if ((state ^ info->state) & (1<<MD_SB_BITMAP_PRESENT)) {
		if (mddev->pers->quiesce == NULL)
			return -EINVAL;
		if (mddev->recovery || mddev->sync_thread)
			return -EBUSY;
		if (info->state & (1<<MD_SB_BITMAP_PRESENT)) {
			/* add the bitmap */
			if (mddev->bitmap)
				return -EEXIST;
			if (mddev->bitmap_info.default_offset == 0)
				return -EINVAL;
			mddev->bitmap_info.offset =
				mddev->bitmap_info.default_offset;
			mddev->bitmap_info.space =
				mddev->bitmap_info.default_space;
			mddev->pers->quiesce(mddev, 1);
			rv = bitmap_create(mddev);
			if (!rv)
				rv = bitmap_load(mddev);
			if (rv)
				bitmap_destroy(mddev);
			mddev->pers->quiesce(mddev, 0);
		} else {
			/* remove the bitmap */
			if (!mddev->bitmap)
				return -ENOENT;
			if (mddev->bitmap->storage.file)
				return -EINVAL;
			mddev->pers->quiesce(mddev, 1);
			bitmap_destroy(mddev);
			mddev->pers->quiesce(mddev, 0);
			mddev->bitmap_info.offset = 0;
		}
	}
	md_update_sb(mddev, 1);
	return rv;
}

static int set_disk_faulty(struct mddev *mddev, dev_t dev)
{
	struct md_rdev *rdev;

	if (mddev->pers == NULL)
		return -ENODEV;

	rdev = find_rdev(mddev, dev);
	if (!rdev)
		return -ENODEV;

	md_error(mddev, rdev);
	if (!test_bit(Faulty, &rdev->flags))
		return -EBUSY;
	return 0;
}

/*
 * We have a problem here : there is no easy way to give a CHS
 * virtual geometry. We currently pretend that we have a 2 heads
 * 4 sectors (with a BIG number of cylinders...). This drives
 * dosfs just mad... ;-)
 */
static int md_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mddev *mddev = bdev->bd_disk->private_data;

	geo->heads = 2;
	geo->sectors = 4;
	geo->cylinders = mddev->array_sectors / 8;
	return 0;
}

static int md_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *argp = (void __user *)arg;
	struct mddev *mddev = NULL;
	int ro;

	switch (cmd) {
	case RAID_VERSION:
	case GET_ARRAY_INFO:
	case GET_DISK_INFO:
		break;
	default:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
	}

	/*
	 * Commands dealing with the RAID driver but not any
	 * particular array:
	 */
	switch (cmd)
	{
		case RAID_VERSION:
			err = get_version(argp);
			goto done;

		case PRINT_RAID_DEBUG:
			err = 0;
			md_print_devices();
			goto done;

#ifndef MODULE
		case RAID_AUTORUN:
			err = 0;
			autostart_arrays(arg);
			goto done;
#endif
		default:;
	}

	/*
	 * Commands creating/starting a new array:
	 */

	mddev = bdev->bd_disk->private_data;

	if (!mddev) {
		BUG();
		goto abort;
	}

	err = mddev_lock(mddev);
	if (err) {
		printk(KERN_INFO 
			"md: ioctl lock interrupted, reason %d, cmd %d\n",
			err, cmd);
		goto abort;
	}

	switch (cmd)
	{
		case SET_ARRAY_INFO:
			{
				mdu_array_info_t info;
				if (!arg)
					memset(&info, 0, sizeof(info));
				else if (copy_from_user(&info, argp, sizeof(info))) {
					err = -EFAULT;
					goto abort_unlock;
				}
				if (mddev->pers) {
					err = update_array_info(mddev, &info);
					if (err) {
						printk(KERN_WARNING "md: couldn't update"
						       " array info. %d\n", err);
						goto abort_unlock;
					}
					goto done_unlock;
				}
				if (!list_empty(&mddev->disks)) {
					printk(KERN_WARNING
					       "md: array %s already has disks!\n",
					       mdname(mddev));
					err = -EBUSY;
					goto abort_unlock;
				}
				if (mddev->raid_disks) {
					printk(KERN_WARNING
					       "md: array %s already initialised!\n",
					       mdname(mddev));
					err = -EBUSY;
					goto abort_unlock;
				}
				err = set_array_info(mddev, &info);
				if (err) {
					printk(KERN_WARNING "md: couldn't set"
					       " array info. %d\n", err);
					goto abort_unlock;
				}
			}
			goto done_unlock;

		default:;
	}

	/*
	 * Commands querying/configuring an existing array:
	 */
	/* if we are not initialised yet, only ADD_NEW_DISK, STOP_ARRAY,
	 * RUN_ARRAY, and GET_ and SET_BITMAP_FILE are allowed */
	if ((!mddev->raid_disks && !mddev->external)
	    && cmd != ADD_NEW_DISK && cmd != STOP_ARRAY
	    && cmd != RUN_ARRAY && cmd != SET_BITMAP_FILE
	    && cmd != GET_BITMAP_FILE) {
		err = -ENODEV;
		goto abort_unlock;
	}

	/*
	 * Commands even a read-only array can execute:
	 */
	switch (cmd)
	{
		case GET_ARRAY_INFO:
			err = get_array_info(mddev, argp);
			goto done_unlock;

		case GET_BITMAP_FILE:
			err = get_bitmap_file(mddev, argp);
			goto done_unlock;

		case GET_DISK_INFO:
			err = get_disk_info(mddev, argp);
			goto done_unlock;

		case RESTART_ARRAY_RW:
			err = restart_array(mddev);
			goto done_unlock;

		case STOP_ARRAY:
			err = do_md_stop(mddev, 0, 1);
			goto done_unlock;

		case STOP_ARRAY_RO:
			err = md_set_readonly(mddev, 1);
			goto done_unlock;

		case BLKROSET:
			if (get_user(ro, (int __user *)(arg))) {
				err = -EFAULT;
				goto done_unlock;
			}
			err = -EINVAL;

			/* if the bdev is going readonly the value of mddev->ro
			 * does not matter, no writes are coming
			 */
			if (ro)
				goto done_unlock;

			/* are we are already prepared for writes? */
			if (mddev->ro != 1)
				goto done_unlock;

			/* transitioning to readauto need only happen for
			 * arrays that call md_write_start
			 */
			if (mddev->pers) {
				err = restart_array(mddev);
				if (err == 0) {
					mddev->ro = 2;
					set_disk_ro(mddev->gendisk, 0);
				}
			}
			goto done_unlock;
	}

	/*
	 * The remaining ioctls are changing the state of the
	 * superblock, so we do not allow them on read-only arrays.
	 * However non-MD ioctls (e.g. get-size) will still come through
	 * here and hit the 'default' below, so only disallow
	 * 'md' ioctls, and switch to rw mode if started auto-readonly.
	 */
	if (_IOC_TYPE(cmd) == MD_MAJOR && mddev->ro && mddev->pers) {
		if (mddev->ro == 2) {
			mddev->ro = 0;
			sysfs_notify_dirent_safe(mddev->sysfs_state);
			set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			md_wakeup_thread(mddev->thread);
		} else {
			err = -EROFS;
			goto abort_unlock;
		}
	}

	switch (cmd)
	{
		case ADD_NEW_DISK:
		{
			mdu_disk_info_t info;
			if (copy_from_user(&info, argp, sizeof(info)))
				err = -EFAULT;
			else
				err = add_new_disk(mddev, &info);
			goto done_unlock;
		}

		case HOT_REMOVE_DISK:
			err = hot_remove_disk(mddev, new_decode_dev(arg));
			goto done_unlock;

		case HOT_ADD_DISK:
			err = hot_add_disk(mddev, new_decode_dev(arg));
			goto done_unlock;

		case SET_DISK_FAULTY:
			err = set_disk_faulty(mddev, new_decode_dev(arg));
			goto done_unlock;

		case RUN_ARRAY:
			err = do_md_run(mddev);
			goto done_unlock;

		case SET_BITMAP_FILE:
			err = set_bitmap_file(mddev, (int)arg);
			goto done_unlock;

		default:
			err = -EINVAL;
			goto abort_unlock;
	}

done_unlock:
abort_unlock:
	if (mddev->hold_active == UNTIL_IOCTL &&
	    err != -EINVAL)
		mddev->hold_active = 0;
	mddev_unlock(mddev);

	return err;
done:
	if (err)
		MD_BUG();
abort:
	return err;
}
#ifdef CONFIG_COMPAT
static int md_compat_ioctl(struct block_device *bdev, fmode_t mode,
		    unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case HOT_REMOVE_DISK:
	case HOT_ADD_DISK:
	case SET_DISK_FAULTY:
	case SET_BITMAP_FILE:
		/* These take in integer arg, do not convert */
		break;
	default:
		arg = (unsigned long)compat_ptr(arg);
		break;
	}

	return md_ioctl(bdev, mode, cmd, arg);
}
#endif /* CONFIG_COMPAT */

static int md_open(struct block_device *bdev, fmode_t mode)
{
	/*
	 * Succeed if we can lock the mddev, which confirms that
	 * it isn't being stopped right now.
	 */
	struct mddev *mddev = mddev_find(bdev->bd_dev);
	int err;

	if (mddev->gendisk != bdev->bd_disk) {
		/* we are racing with mddev_put which is discarding this
		 * bd_disk.
		 */
		mddev_put(mddev);
		/* Wait until bdev->bd_disk is definitely gone */
		flush_workqueue(md_misc_wq);
		/* Then retry the open from the top */
		return -ERESTARTSYS;
	}
	BUG_ON(mddev != bdev->bd_disk->private_data);

	if ((err = mutex_lock_interruptible(&mddev->open_mutex)))
		goto out;

	err = 0;
	atomic_inc(&mddev->openers);
	mutex_unlock(&mddev->open_mutex);

	check_disk_change(bdev);
 out:
	return err;
}

static int md_release(struct gendisk *disk, fmode_t mode)
{
 	struct mddev *mddev = disk->private_data;

	BUG_ON(!mddev);
	atomic_dec(&mddev->openers);
	mddev_put(mddev);

	return 0;
}

static int md_media_changed(struct gendisk *disk)
{
	struct mddev *mddev = disk->private_data;

	return mddev->changed;
}

static int md_revalidate(struct gendisk *disk)
{
	struct mddev *mddev = disk->private_data;

	mddev->changed = 0;
	return 0;
}
static const struct block_device_operations md_fops =
{
	.owner		= THIS_MODULE,
	.open		= md_open,
	.release	= md_release,
	.ioctl		= md_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= md_compat_ioctl,
#endif
	.getgeo		= md_getgeo,
	.media_changed  = md_media_changed,
	.revalidate_disk= md_revalidate,
};

static int md_thread(void * arg)
{
	struct md_thread *thread = arg;

	/*
	 * md_thread is a 'system-thread', it's priority should be very
	 * high. We avoid resource deadlocks individually in each
	 * raid personality. (RAID5 does preallocation) We also use RR and
	 * the very same RT priority as kswapd, thus we will never get
	 * into a priority inversion deadlock.
	 *
	 * we definitely have to have equal or higher priority than
	 * bdflush, otherwise bdflush will deadlock if there are too
	 * many dirty RAID5 blocks.
	 */

	allow_signal(SIGKILL);
	while (!kthread_should_stop()) {

		/* We need to wait INTERRUPTIBLE so that
		 * we don't add to the load-average.
		 * That means we need to be sure no signals are
		 * pending
		 */
		if (signal_pending(current))
			flush_signals(current);

		wait_event_interruptible_timeout
			(thread->wqueue,
			 test_bit(THREAD_WAKEUP, &thread->flags)
			 || kthread_should_stop(),
			 thread->timeout);

		clear_bit(THREAD_WAKEUP, &thread->flags);
		if (!kthread_should_stop())
			thread->run(thread->mddev);
	}

	return 0;
}

void md_wakeup_thread(struct md_thread *thread)
{
	if (thread) {
		pr_debug("md: waking up MD thread %s.\n", thread->tsk->comm);
		set_bit(THREAD_WAKEUP, &thread->flags);
		wake_up(&thread->wqueue);
	}
}

struct md_thread *md_register_thread(void (*run) (struct mddev *), struct mddev *mddev,
				 const char *name)
{
	struct md_thread *thread;

	thread = kzalloc(sizeof(struct md_thread), GFP_KERNEL);
	if (!thread)
		return NULL;

	init_waitqueue_head(&thread->wqueue);

	thread->run = run;
	thread->mddev = mddev;
	thread->timeout = MAX_SCHEDULE_TIMEOUT;
	thread->tsk = kthread_run(md_thread, thread,
				  "%s_%s",
				  mdname(thread->mddev),
				  name ?: mddev->pers->name);
	if (IS_ERR(thread->tsk)) {
		kfree(thread);
		return NULL;
	}
	return thread;
}

void md_unregister_thread(struct md_thread **threadp)
{
	struct md_thread *thread = *threadp;
	if (!thread)
		return;
	pr_debug("interrupting MD-thread pid %d\n", task_pid_nr(thread->tsk));
	/* Locking ensures that mddev_unlock does not wake_up a
	 * non-existent thread
	 */
	spin_lock(&pers_lock);
	*threadp = NULL;
	spin_unlock(&pers_lock);

	kthread_stop(thread->tsk);
	kfree(thread);
}

void md_error(struct mddev *mddev, struct md_rdev *rdev)
{
	if (!mddev) {
		MD_BUG();
		return;
	}

	if (!rdev || test_bit(Faulty, &rdev->flags))
		return;

	if (!mddev->pers || !mddev->pers->error_handler)
		return;
	mddev->pers->error_handler(mddev,rdev);
	if (mddev->degraded)
		set_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
	sysfs_notify_dirent_safe(rdev->sysfs_state);
	set_bit(MD_RECOVERY_INTR, &mddev->recovery);
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
	if (mddev->event_work.func)
		queue_work(md_misc_wq, &mddev->event_work);
	md_new_event_inintr(mddev);
}

/* seq_file implementation /proc/mdstat */

static void status_unused(struct seq_file *seq)
{
	int i = 0;
	struct md_rdev *rdev;

	seq_printf(seq, "unused devices: ");

	list_for_each_entry(rdev, &pending_raid_disks, same_set) {
		char b[BDEVNAME_SIZE];
		i++;
		seq_printf(seq, "%s ",
			      bdevname(rdev->bdev,b));
	}
	if (!i)
		seq_printf(seq, "<none>");

	seq_printf(seq, "\n");
}


static void status_resync(struct seq_file *seq, struct mddev * mddev)
{
	sector_t max_sectors, resync, res;
	unsigned long dt, db;
	sector_t rt;
	int scale;
	unsigned int per_milli;

	resync = mddev->curr_resync - atomic_read(&mddev->recovery_active);

	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery) ||
	    test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		max_sectors = mddev->resync_max_sectors;
	else
		max_sectors = mddev->dev_sectors;

	/*
	 * Should not happen.
	 */
	if (!max_sectors) {
		MD_BUG();
		return;
	}
	/* Pick 'scale' such that (resync>>scale)*1000 will fit
	 * in a sector_t, and (max_sectors>>scale) will fit in a
	 * u32, as those are the requirements for sector_div.
	 * Thus 'scale' must be at least 10
	 */
	scale = 10;
	if (sizeof(sector_t) > sizeof(unsigned long)) {
		while ( max_sectors/2 > (1ULL<<(scale+32)))
			scale++;
	}
	res = (resync>>scale)*1000;
	sector_div(res, (u32)((max_sectors>>scale)+1));

	per_milli = res;
	{
		int i, x = per_milli/50, y = 20-x;
		seq_printf(seq, "[");
		for (i = 0; i < x; i++)
			seq_printf(seq, "=");
		seq_printf(seq, ">");
		for (i = 0; i < y; i++)
			seq_printf(seq, ".");
		seq_printf(seq, "] ");
	}
	seq_printf(seq, " %s =%3u.%u%% (%llu/%llu)",
		   (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery)?
		    "reshape" :
		    (test_bit(MD_RECOVERY_CHECK, &mddev->recovery)?
		     "check" :
		     (test_bit(MD_RECOVERY_SYNC, &mddev->recovery) ?
		      "resync" : "recovery"))),
		   per_milli/10, per_milli % 10,
		   (unsigned long long) resync/2,
		   (unsigned long long) max_sectors/2);

	/*
	 * dt: time from mark until now
	 * db: blocks written from mark until now
	 * rt: remaining time
	 *
	 * rt is a sector_t, so could be 32bit or 64bit.
	 * So we divide before multiply in case it is 32bit and close
	 * to the limit.
	 * We scale the divisor (db) by 32 to avoid losing precision
	 * near the end of resync when the number of remaining sectors
	 * is close to 'db'.
	 * We then divide rt by 32 after multiplying by db to compensate.
	 * The '+1' avoids division by zero if db is very small.
	 */
	dt = ((jiffies - mddev->resync_mark) / HZ);
	if (!dt) dt++;
	db = (mddev->curr_mark_cnt - atomic_read(&mddev->recovery_active))
		- mddev->resync_mark_cnt;

	rt = max_sectors - resync;    /* number of remaining sectors */
	sector_div(rt, db/32+1);
	rt *= dt;
	rt >>= 5;

	seq_printf(seq, " finish=%lu.%lumin", (unsigned long)rt / 60,
		   ((unsigned long)rt % 60)/6);

	seq_printf(seq, " speed=%ldK/sec", db/2/dt);
}

static void *md_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct list_head *tmp;
	loff_t l = *pos;
	struct mddev *mddev;

	if (l >= 0x10000)
		return NULL;
	if (!l--)
		/* header */
		return (void*)1;

	spin_lock(&all_mddevs_lock);
	list_for_each(tmp,&all_mddevs)
		if (!l--) {
			mddev = list_entry(tmp, struct mddev, all_mddevs);
			mddev_get(mddev);
			spin_unlock(&all_mddevs_lock);
			return mddev;
		}
	spin_unlock(&all_mddevs_lock);
	if (!l--)
		return (void*)2;/* tail */
	return NULL;
}

static void *md_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct list_head *tmp;
	struct mddev *next_mddev, *mddev = v;
	
	++*pos;
	if (v == (void*)2)
		return NULL;

	spin_lock(&all_mddevs_lock);
	if (v == (void*)1)
		tmp = all_mddevs.next;
	else
		tmp = mddev->all_mddevs.next;
	if (tmp != &all_mddevs)
		next_mddev = mddev_get(list_entry(tmp,struct mddev,all_mddevs));
	else {
		next_mddev = (void*)2;
		*pos = 0x10000;
	}		
	spin_unlock(&all_mddevs_lock);

	if (v != (void*)1)
		mddev_put(mddev);
	return next_mddev;

}

static void md_seq_stop(struct seq_file *seq, void *v)
{
	struct mddev *mddev = v;

	if (mddev && v != (void*)1 && v != (void*)2)
		mddev_put(mddev);
}

static int md_seq_show(struct seq_file *seq, void *v)
{
	struct mddev *mddev = v;
	sector_t sectors;
	struct md_rdev *rdev;

	if (v == (void*)1) {
		struct md_personality *pers;
		seq_printf(seq, "Personalities : ");
		spin_lock(&pers_lock);
		list_for_each_entry(pers, &pers_list, list)
			seq_printf(seq, "[%s] ", pers->name);

		spin_unlock(&pers_lock);
		seq_printf(seq, "\n");
		seq->poll_event = atomic_read(&md_event_count);
		return 0;
	}
	if (v == (void*)2) {
		status_unused(seq);
		return 0;
	}

	if (mddev_lock(mddev) < 0)
		return -EINTR;

	if (mddev->pers || mddev->raid_disks || !list_empty(&mddev->disks)) {
		seq_printf(seq, "%s : %sactive", mdname(mddev),
						mddev->pers ? "" : "in");
		if (mddev->pers) {
			if (mddev->ro==1)
				seq_printf(seq, " (read-only)");
			if (mddev->ro==2)
				seq_printf(seq, " (auto-read-only)");
			seq_printf(seq, " %s", mddev->pers->name);
		}

		sectors = 0;
		rdev_for_each(rdev, mddev) {
			char b[BDEVNAME_SIZE];
			seq_printf(seq, " %s[%d]",
				bdevname(rdev->bdev,b), rdev->desc_nr);
			if (test_bit(WriteMostly, &rdev->flags))
				seq_printf(seq, "(W)");
			if (test_bit(Faulty, &rdev->flags)) {
				seq_printf(seq, "(F)");
				continue;
			}
			if (rdev->raid_disk < 0)
				seq_printf(seq, "(S)"); /* spare */
			if (test_bit(Replacement, &rdev->flags))
				seq_printf(seq, "(R)");
			sectors += rdev->sectors;
		}

		if (!list_empty(&mddev->disks)) {
			if (mddev->pers)
				seq_printf(seq, "\n      %llu blocks",
					   (unsigned long long)
					   mddev->array_sectors / 2);
			else
				seq_printf(seq, "\n      %llu blocks",
					   (unsigned long long)sectors / 2);
		}
		if (mddev->persistent) {
			if (mddev->major_version != 0 ||
			    mddev->minor_version != 90) {
				seq_printf(seq," super %d.%d",
					   mddev->major_version,
					   mddev->minor_version);
			}
		} else if (mddev->external)
			seq_printf(seq, " super external:%s",
				   mddev->metadata_type);
		else
			seq_printf(seq, " super non-persistent");

		if (mddev->pers) {
			mddev->pers->status(seq, mddev);
	 		seq_printf(seq, "\n      ");
			if (mddev->pers->sync_request) {
				if (mddev->curr_resync > 2) {
					status_resync(seq, mddev);
					seq_printf(seq, "\n      ");
				} else if (mddev->curr_resync == 1 || mddev->curr_resync == 2)
					seq_printf(seq, "\tresync=DELAYED\n      ");
				else if (mddev->recovery_cp < MaxSector)
					seq_printf(seq, "\tresync=PENDING\n      ");
			}
		} else
			seq_printf(seq, "\n       ");

		bitmap_status(seq, mddev->bitmap);

		seq_printf(seq, "\n");
	}
	mddev_unlock(mddev);
	
	return 0;
}

static const struct seq_operations md_seq_ops = {
	.start  = md_seq_start,
	.next   = md_seq_next,
	.stop   = md_seq_stop,
	.show   = md_seq_show,
};

static int md_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int error;

	error = seq_open(file, &md_seq_ops);
	if (error)
		return error;

	seq = file->private_data;
	seq->poll_event = atomic_read(&md_event_count);
	return error;
}

static unsigned int mdstat_poll(struct file *filp, poll_table *wait)
{
	struct seq_file *seq = filp->private_data;
	int mask;

	poll_wait(filp, &md_event_waiters, wait);

	/* always allow read */
	mask = POLLIN | POLLRDNORM;

	if (seq->poll_event != atomic_read(&md_event_count))
		mask |= POLLERR | POLLPRI;
	return mask;
}

static const struct file_operations md_seq_fops = {
	.owner		= THIS_MODULE,
	.open           = md_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release_private,
	.poll		= mdstat_poll,
};

int register_md_personality(struct md_personality *p)
{
	spin_lock(&pers_lock);
	list_add_tail(&p->list, &pers_list);
	printk(KERN_INFO "md: %s personality registered for level %d\n", p->name, p->level);
	spin_unlock(&pers_lock);
	return 0;
}

int unregister_md_personality(struct md_personality *p)
{
	printk(KERN_INFO "md: %s personality unregistered\n", p->name);
	spin_lock(&pers_lock);
	list_del_init(&p->list);
	spin_unlock(&pers_lock);
	return 0;
}

static int is_mddev_idle(struct mddev *mddev, int init)
{
	struct md_rdev * rdev;
	int idle;
	int curr_events;

	idle = 1;
	rcu_read_lock();
	rdev_for_each_rcu(rdev, mddev) {
		struct gendisk *disk = rdev->bdev->bd_contains->bd_disk;
		curr_events = (int)part_stat_read(&disk->part0, sectors[0]) +
			      (int)part_stat_read(&disk->part0, sectors[1]) -
			      atomic_read(&disk->sync_io);
		/* sync IO will cause sync_io to increase before the disk_stats
		 * as sync_io is counted when a request starts, and
		 * disk_stats is counted when it completes.
		 * So resync activity will cause curr_events to be smaller than
		 * when there was no such activity.
		 * non-sync IO will cause disk_stat to increase without
		 * increasing sync_io so curr_events will (eventually)
		 * be larger than it was before.  Once it becomes
		 * substantially larger, the test below will cause
		 * the array to appear non-idle, and resync will slow
		 * down.
		 * If there is a lot of outstanding resync activity when
		 * we set last_event to curr_events, then all that activity
		 * completing might cause the array to appear non-idle
		 * and resync will be slowed down even though there might
		 * not have been non-resync activity.  This will only
		 * happen once though.  'last_events' will soon reflect
		 * the state where there is little or no outstanding
		 * resync requests, and further resync activity will
		 * always make curr_events less than last_events.
		 *
		 */
		if (init || curr_events - rdev->last_events > 64) {
			rdev->last_events = curr_events;
			idle = 0;
		}
	}
	rcu_read_unlock();
	return idle;
}

void md_done_sync(struct mddev *mddev, int blocks, int ok)
{
	/* another "blocks" (512byte) blocks have been synced */
	atomic_sub(blocks, &mddev->recovery_active);
	wake_up(&mddev->recovery_wait);
	if (!ok) {
		set_bit(MD_RECOVERY_INTR, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
		// stop recovery, signal do_sync ....
	}
}


/* md_write_start(mddev, bi)
 * If we need to update some array metadata (e.g. 'active' flag
 * in superblock) before writing, schedule a superblock update
 * and wait for it to complete.
 */
void md_write_start(struct mddev *mddev, struct bio *bi)
{
	int did_change = 0;
	if (bio_data_dir(bi) != WRITE)
		return;

	BUG_ON(mddev->ro == 1);
	if (mddev->ro == 2) {
		/* need to switch to read/write */
		mddev->ro = 0;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
		md_wakeup_thread(mddev->sync_thread);
		did_change = 1;
	}
	atomic_inc(&mddev->writes_pending);
	if (mddev->safemode == 1)
		mddev->safemode = 0;
	if (mddev->in_sync) {
		spin_lock_irq(&mddev->write_lock);
		if (mddev->in_sync) {
			mddev->in_sync = 0;
			set_bit(MD_CHANGE_CLEAN, &mddev->flags);
			set_bit(MD_CHANGE_PENDING, &mddev->flags);
			md_wakeup_thread(mddev->thread);
			did_change = 1;
		}
		spin_unlock_irq(&mddev->write_lock);
	}
	if (did_change)
		sysfs_notify_dirent_safe(mddev->sysfs_state);
	wait_event(mddev->sb_wait,
		   !test_bit(MD_CHANGE_PENDING, &mddev->flags));
}

void md_write_end(struct mddev *mddev)
{
	if (atomic_dec_and_test(&mddev->writes_pending)) {
		if (mddev->safemode == 2)
			md_wakeup_thread(mddev->thread);
		else if (mddev->safemode_delay)
			mod_timer(&mddev->safemode_timer, jiffies + mddev->safemode_delay);
	}
}

/* md_allow_write(mddev)
 * Calling this ensures that the array is marked 'active' so that writes
 * may proceed without blocking.  It is important to call this before
 * attempting a GFP_KERNEL allocation while holding the mddev lock.
 * Must be called with mddev_lock held.
 *
 * In the ->external case MD_CHANGE_CLEAN can not be cleared until mddev->lock
 * is dropped, so return -EAGAIN after notifying userspace.
 */
int md_allow_write(struct mddev *mddev)
{
	if (!mddev->pers)
		return 0;
	if (mddev->ro)
		return 0;
	if (!mddev->pers->sync_request)
		return 0;

	spin_lock_irq(&mddev->write_lock);
	if (mddev->in_sync) {
		mddev->in_sync = 0;
		set_bit(MD_CHANGE_CLEAN, &mddev->flags);
		set_bit(MD_CHANGE_PENDING, &mddev->flags);
		if (mddev->safemode_delay &&
		    mddev->safemode == 0)
			mddev->safemode = 1;
		spin_unlock_irq(&mddev->write_lock);
		md_update_sb(mddev, 0);
		sysfs_notify_dirent_safe(mddev->sysfs_state);
	} else
		spin_unlock_irq(&mddev->write_lock);

	if (test_bit(MD_CHANGE_PENDING, &mddev->flags))
		return -EAGAIN;
	else
		return 0;
}
EXPORT_SYMBOL_GPL(md_allow_write);

#define SYNC_MARKS	10
#define	SYNC_MARK_STEP	(3*HZ)
void md_do_sync(struct mddev *mddev)
{
	struct mddev *mddev2;
	unsigned int currspeed = 0,
		 window;
	sector_t max_sectors,j, io_sectors;
	unsigned long mark[SYNC_MARKS];
	sector_t mark_cnt[SYNC_MARKS];
	int last_mark,m;
	struct list_head *tmp;
	sector_t last_check;
	int skipped = 0;
	struct md_rdev *rdev;
	char *desc;

	/* just incase thread restarts... */
	if (test_bit(MD_RECOVERY_DONE, &mddev->recovery))
		return;
	if (mddev->ro) /* never try to sync a read-only array */
		return;

	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
		if (test_bit(MD_RECOVERY_CHECK, &mddev->recovery))
			desc = "data-check";
		else if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
			desc = "requested-resync";
		else
			desc = "resync";
	} else if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		desc = "reshape";
	else
		desc = "recovery";

	/* we overload curr_resync somewhat here.
	 * 0 == not engaged in resync at all
	 * 2 == checking that there is no conflict with another sync
	 * 1 == like 2, but have yielded to allow conflicting resync to
	 *		commense
	 * other == active in resync - this many blocks
	 *
	 * Before starting a resync we must have set curr_resync to
	 * 2, and then checked that every "conflicting" array has curr_resync
	 * less than ours.  When we find one that is the same or higher
	 * we wait on resync_wait.  To avoid deadlock, we reduce curr_resync
	 * to 1 if we choose to yield (based arbitrarily on address of mddev structure).
	 * This will mean we have to start checking from the beginning again.
	 *
	 */

	do {
		mddev->curr_resync = 2;

	try_again:
		if (kthread_should_stop())
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);

		if (test_bit(MD_RECOVERY_INTR, &mddev->recovery))
			goto skip;
		for_each_mddev(mddev2, tmp) {
			if (mddev2 == mddev)
				continue;
			if (!mddev->parallel_resync
			&&  mddev2->curr_resync
			&&  match_mddev_units(mddev, mddev2)) {
				DEFINE_WAIT(wq);
				if (mddev < mddev2 && mddev->curr_resync == 2) {
					/* arbitrarily yield */
					mddev->curr_resync = 1;
					wake_up(&resync_wait);
				}
				if (mddev > mddev2 && mddev->curr_resync == 1)
					/* no need to wait here, we can wait the next
					 * time 'round when curr_resync == 2
					 */
					continue;
				/* We need to wait 'interruptible' so as not to
				 * contribute to the load average, and not to
				 * be caught by 'softlockup'
				 */
				prepare_to_wait(&resync_wait, &wq, TASK_INTERRUPTIBLE);
				if (!kthread_should_stop() &&
				    mddev2->curr_resync >= mddev->curr_resync) {
					printk(KERN_INFO "md: delaying %s of %s"
					       " until %s has finished (they"
					       " share one or more physical units)\n",
					       desc, mdname(mddev), mdname(mddev2));
					mddev_put(mddev2);
					if (signal_pending(current))
						flush_signals(current);
					schedule();
					finish_wait(&resync_wait, &wq);
					goto try_again;
				}
				finish_wait(&resync_wait, &wq);
			}
		}
	} while (mddev->curr_resync < 2);

	j = 0;
	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
		/* resync follows the size requested by the personality,
		 * which defaults to physical size, but can be virtual size
		 */
		max_sectors = mddev->resync_max_sectors;
		mddev->resync_mismatches = 0;
		/* we don't use the checkpoint if there's a bitmap */
		if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
			j = mddev->resync_min;
		else if (!mddev->bitmap)
			j = mddev->recovery_cp;

	} else if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		max_sectors = mddev->resync_max_sectors;
	else {
		/* recovery follows the physical size of devices */
		max_sectors = mddev->dev_sectors;
		j = MaxSector;
		rcu_read_lock();
		rdev_for_each_rcu(rdev, mddev)
			if (rdev->raid_disk >= 0 &&
			    !test_bit(Faulty, &rdev->flags) &&
			    !test_bit(In_sync, &rdev->flags) &&
			    rdev->recovery_offset < j)
				j = rdev->recovery_offset;
		rcu_read_unlock();
	}

	printk(KERN_INFO "md: %s of RAID array %s\n", desc, mdname(mddev));
	printk(KERN_INFO "md: minimum _guaranteed_  speed:"
		" %d KB/sec/disk.\n", speed_min(mddev));
	printk(KERN_INFO "md: using maximum available idle IO bandwidth "
	       "(but not more than %d KB/sec) for %s.\n",
	       speed_max(mddev), desc);

	is_mddev_idle(mddev, 1); /* this initializes IO event counters */

	io_sectors = 0;
	for (m = 0; m < SYNC_MARKS; m++) {
		mark[m] = jiffies;
		mark_cnt[m] = io_sectors;
	}
	last_mark = 0;
	mddev->resync_mark = mark[last_mark];
	mddev->resync_mark_cnt = mark_cnt[last_mark];

	/*
	 * Tune reconstruction:
	 */
	window = 32*(PAGE_SIZE/512);
	printk(KERN_INFO "md: using %dk window, over a total of %lluk.\n",
		window/2, (unsigned long long)max_sectors/2);

	atomic_set(&mddev->recovery_active, 0);
	last_check = 0;

	if (j>2) {
		printk(KERN_INFO 
		       "md: resuming %s of %s from checkpoint.\n",
		       desc, mdname(mddev));
		mddev->curr_resync = j;
	}
	mddev->curr_resync_completed = j;

	while (j < max_sectors) {
		sector_t sectors;

		skipped = 0;

		if (!test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery) &&
		    ((mddev->curr_resync > mddev->curr_resync_completed &&
		      (mddev->curr_resync - mddev->curr_resync_completed)
		      > (max_sectors >> 4)) ||
		     (j - mddev->curr_resync_completed)*2
		     >= mddev->resync_max - mddev->curr_resync_completed
			    )) {
			/* time to update curr_resync_completed */
			wait_event(mddev->recovery_wait,
				   atomic_read(&mddev->recovery_active) == 0);
			mddev->curr_resync_completed = j;
			set_bit(MD_CHANGE_CLEAN, &mddev->flags);
			sysfs_notify(&mddev->kobj, NULL, "sync_completed");
		}

		while (j >= mddev->resync_max && !kthread_should_stop()) {
			/* As this condition is controlled by user-space,
			 * we can block indefinitely, so use '_interruptible'
			 * to avoid triggering warnings.
			 */
			flush_signals(current); /* just in case */
			wait_event_interruptible(mddev->recovery_wait,
						 mddev->resync_max > j
						 || kthread_should_stop());
		}

		if (kthread_should_stop())
			goto interrupted;

		sectors = mddev->pers->sync_request(mddev, j, &skipped,
						  currspeed < speed_min(mddev));
		if (sectors == 0) {
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			goto out;
		}

		if (!skipped) { /* actual IO requested */
			io_sectors += sectors;
			atomic_add(sectors, &mddev->recovery_active);
		}

		if (test_bit(MD_RECOVERY_INTR, &mddev->recovery))
			break;

		j += sectors;
		if (j>1) mddev->curr_resync = j;
		mddev->curr_mark_cnt = io_sectors;
		if (last_check == 0)
			/* this is the earliest that rebuild will be
			 * visible in /proc/mdstat
			 */
			md_new_event(mddev);

		if (last_check + window > io_sectors || j == max_sectors)
			continue;

		last_check = io_sectors;
	repeat:
		if (time_after_eq(jiffies, mark[last_mark] + SYNC_MARK_STEP )) {
			/* step marks */
			int next = (last_mark+1) % SYNC_MARKS;

			mddev->resync_mark = mark[next];
			mddev->resync_mark_cnt = mark_cnt[next];
			mark[next] = jiffies;
			mark_cnt[next] = io_sectors - atomic_read(&mddev->recovery_active);
			last_mark = next;
		}


		if (kthread_should_stop())
			goto interrupted;


		/*
		 * this loop exits only if either when we are slower than
		 * the 'hard' speed limit, or the system was IO-idle for
		 * a jiffy.
		 * the system might be non-idle CPU-wise, but we only care
		 * about not overloading the IO subsystem. (things like an
		 * e2fsck being done on the RAID array should execute fast)
		 */
		cond_resched();

		currspeed = ((unsigned long)(io_sectors-mddev->resync_mark_cnt))/2
			/((jiffies-mddev->resync_mark)/HZ +1) +1;

		if (currspeed > speed_min(mddev)) {
			if ((currspeed > speed_max(mddev)) ||
					!is_mddev_idle(mddev, 0)) {
				msleep(500);
				goto repeat;
			}
		}
	}
	printk(KERN_INFO "md: %s: %s done.\n",mdname(mddev), desc);
	/*
	 * this also signals 'finished resyncing' to md_stop
	 */
 out:
	wait_event(mddev->recovery_wait, !atomic_read(&mddev->recovery_active));

	/* tell personality that we are finished */
	mddev->pers->sync_request(mddev, max_sectors, &skipped, 1);

	if (!test_bit(MD_RECOVERY_CHECK, &mddev->recovery) &&
	    mddev->curr_resync > 2) {
		if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
			if (test_bit(MD_RECOVERY_INTR, &mddev->recovery)) {
				if (mddev->curr_resync >= mddev->recovery_cp) {
					printk(KERN_INFO
					       "md: checkpointing %s of %s.\n",
					       desc, mdname(mddev));
					mddev->recovery_cp =
						mddev->curr_resync_completed;
				}
			} else
				mddev->recovery_cp = MaxSector;
		} else {
			if (!test_bit(MD_RECOVERY_INTR, &mddev->recovery))
				mddev->curr_resync = MaxSector;
			rcu_read_lock();
			rdev_for_each_rcu(rdev, mddev)
				if (rdev->raid_disk >= 0 &&
				    mddev->delta_disks >= 0 &&
				    !test_bit(Faulty, &rdev->flags) &&
				    !test_bit(In_sync, &rdev->flags) &&
				    rdev->recovery_offset < mddev->curr_resync)
					rdev->recovery_offset = mddev->curr_resync;
			rcu_read_unlock();
		}
	}
 skip:
	set_bit(MD_CHANGE_DEVS, &mddev->flags);

	if (!test_bit(MD_RECOVERY_INTR, &mddev->recovery)) {
		/* We completed so min/max setting can be forgotten if used. */
		if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
			mddev->resync_min = 0;
		mddev->resync_max = MaxSector;
	} else if (test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
		mddev->resync_min = mddev->curr_resync_completed;
	mddev->curr_resync = 0;
	wake_up(&resync_wait);
	set_bit(MD_RECOVERY_DONE, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
	return;

 interrupted:
	/*
	 * got a signal, exit.
	 */
	printk(KERN_INFO
	       "md: md_do_sync() got signal ... exiting\n");
	set_bit(MD_RECOVERY_INTR, &mddev->recovery);
	goto out;

}
EXPORT_SYMBOL_GPL(md_do_sync);

static int remove_and_add_spares(struct mddev *mddev)
{
	struct md_rdev *rdev;
	int spares = 0;
	int removed = 0;

	mddev->curr_resync_completed = 0;

	rdev_for_each(rdev, mddev)
		if (rdev->raid_disk >= 0 &&
		    !test_bit(Blocked, &rdev->flags) &&
		    (test_bit(Faulty, &rdev->flags) ||
		     ! test_bit(In_sync, &rdev->flags)) &&
		    atomic_read(&rdev->nr_pending)==0) {
			if (mddev->pers->hot_remove_disk(
				    mddev, rdev) == 0) {
				sysfs_unlink_rdev(mddev, rdev);
				rdev->raid_disk = -1;
				removed++;
			}
		}
	if (removed)
		sysfs_notify(&mddev->kobj, NULL,
			     "degraded");


	rdev_for_each(rdev, mddev) {
		if (rdev->raid_disk >= 0 &&
		    !test_bit(In_sync, &rdev->flags) &&
		    !test_bit(Faulty, &rdev->flags))
			spares++;
		if (rdev->raid_disk < 0
		    && !test_bit(Faulty, &rdev->flags)) {
			rdev->recovery_offset = 0;
			if (mddev->pers->
			    hot_add_disk(mddev, rdev) == 0) {
				if (sysfs_link_rdev(mddev, rdev))
					/* failure here is OK */;
				spares++;
				md_new_event(mddev);
				set_bit(MD_CHANGE_DEVS, &mddev->flags);
			}
		}
	}
	return spares;
}

static void reap_sync_thread(struct mddev *mddev)
{
	struct md_rdev *rdev;

	/* resync has finished, collect result */
	md_unregister_thread(&mddev->sync_thread);
	if (!test_bit(MD_RECOVERY_INTR, &mddev->recovery) &&
	    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery)) {
		/* success...*/
		/* activate any spares */
		if (mddev->pers->spare_active(mddev))
			sysfs_notify(&mddev->kobj, NULL,
				     "degraded");
	}
	if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery) &&
	    mddev->pers->finish_reshape)
		mddev->pers->finish_reshape(mddev);

	/* If array is no-longer degraded, then any saved_raid_disk
	 * information must be scrapped.  Also if any device is now
	 * In_sync we must scrape the saved_raid_disk for that device
	 * do the superblock for an incrementally recovered device
	 * written out.
	 */
	rdev_for_each(rdev, mddev)
		if (!mddev->degraded ||
		    test_bit(In_sync, &rdev->flags))
			rdev->saved_raid_disk = -1;

	md_update_sb(mddev, 1);
	clear_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
	clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
	clear_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
	clear_bit(MD_RECOVERY_REQUESTED, &mddev->recovery);
	clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
	/* flag recovery needed just to double check */
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	sysfs_notify_dirent_safe(mddev->sysfs_action);
	md_new_event(mddev);
	if (mddev->event_work.func)
		queue_work(md_misc_wq, &mddev->event_work);
}

/*
 * This routine is regularly called by all per-raid-array threads to
 * deal with generic issues like resync and super-block update.
 * Raid personalities that don't have a thread (linear/raid0) do not
 * need this as they never do any recovery or update the superblock.
 *
 * It does not do any resync itself, but rather "forks" off other threads
 * to do that as needed.
 * When it is determined that resync is needed, we set MD_RECOVERY_RUNNING in
 * "->recovery" and create a thread at ->sync_thread.
 * When the thread finishes it sets MD_RECOVERY_DONE
 * and wakeups up this thread which will reap the thread and finish up.
 * This thread also removes any faulty devices (with nr_pending == 0).
 *
 * The overall approach is:
 *  1/ if the superblock needs updating, update it.
 *  2/ If a recovery thread is running, don't do anything else.
 *  3/ If recovery has finished, clean up, possibly marking spares active.
 *  4/ If there are any faulty devices, remove them.
 *  5/ If array is degraded, try to add spares devices
 *  6/ If array has spares or is not in-sync, start a resync thread.
 */
void md_check_recovery(struct mddev *mddev)
{
	if (mddev->suspended)
		return;

	if (mddev->bitmap)
		bitmap_daemon_work(mddev);

	if (signal_pending(current)) {
		if (mddev->pers->sync_request && !mddev->external) {
			printk(KERN_INFO "md: %s in immediate safe mode\n",
			       mdname(mddev));
			mddev->safemode = 2;
		}
		flush_signals(current);
	}

	if (mddev->ro && !test_bit(MD_RECOVERY_NEEDED, &mddev->recovery))
		return;
	if ( ! (
		(mddev->flags & ~ (1<<MD_CHANGE_PENDING)) ||
		test_bit(MD_RECOVERY_NEEDED, &mddev->recovery) ||
		test_bit(MD_RECOVERY_DONE, &mddev->recovery) ||
		(mddev->external == 0 && mddev->safemode == 1) ||
		(mddev->safemode == 2 && ! atomic_read(&mddev->writes_pending)
		 && !mddev->in_sync && mddev->recovery_cp == MaxSector)
		))
		return;

	if (mddev_trylock(mddev)) {
		int spares = 0;

		if (mddev->ro) {
			/* Only thing we do on a ro array is remove
			 * failed devices.
			 */
			struct md_rdev *rdev;
			rdev_for_each(rdev, mddev)
				if (rdev->raid_disk >= 0 &&
				    !test_bit(Blocked, &rdev->flags) &&
				    test_bit(Faulty, &rdev->flags) &&
				    atomic_read(&rdev->nr_pending)==0) {
					if (mddev->pers->hot_remove_disk(
						    mddev, rdev) == 0) {
						sysfs_unlink_rdev(mddev, rdev);
						rdev->raid_disk = -1;
					}
				}
			clear_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			goto unlock;
		}

		if (!mddev->external) {
			int did_change = 0;
			spin_lock_irq(&mddev->write_lock);
			if (mddev->safemode &&
			    !atomic_read(&mddev->writes_pending) &&
			    !mddev->in_sync &&
			    mddev->recovery_cp == MaxSector) {
				mddev->in_sync = 1;
				did_change = 1;
				set_bit(MD_CHANGE_CLEAN, &mddev->flags);
			}
			if (mddev->safemode == 1)
				mddev->safemode = 0;
			spin_unlock_irq(&mddev->write_lock);
			if (did_change)
				sysfs_notify_dirent_safe(mddev->sysfs_state);
		}

		if (mddev->flags)
			md_update_sb(mddev, 0);

		if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery) &&
		    !test_bit(MD_RECOVERY_DONE, &mddev->recovery)) {
			/* resync/recovery still happening */
			clear_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			goto unlock;
		}
		if (mddev->sync_thread) {
			reap_sync_thread(mddev);
			goto unlock;
		}
		/* Set RUNNING before clearing NEEDED to avoid
		 * any transients in the value of "sync_action".
		 */
		set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
		/* Clear some bits that don't mean anything, but
		 * might be left set
		 */
		clear_bit(MD_RECOVERY_INTR, &mddev->recovery);
		clear_bit(MD_RECOVERY_DONE, &mddev->recovery);

		if (!test_and_clear_bit(MD_RECOVERY_NEEDED, &mddev->recovery) ||
		    test_bit(MD_RECOVERY_FROZEN, &mddev->recovery))
			goto unlock;
		/* no recovery is running.
		 * remove any failed drives, then
		 * add spares if possible.
		 * Spare are also removed and re-added, to allow
		 * the personality to fail the re-add.
		 */

		if (mddev->reshape_position != MaxSector) {
			if (mddev->pers->check_reshape == NULL ||
			    mddev->pers->check_reshape(mddev) != 0)
				/* Cannot proceed */
				goto unlock;
			set_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
			clear_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
		} else if ((spares = remove_and_add_spares(mddev))) {
			clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
			clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
			clear_bit(MD_RECOVERY_REQUESTED, &mddev->recovery);
			set_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
		} else if (mddev->recovery_cp < MaxSector) {
			set_bit(MD_RECOVERY_SYNC, &mddev->recovery);
			clear_bit(MD_RECOVERY_RECOVER, &mddev->recovery);
		} else if (!test_bit(MD_RECOVERY_SYNC, &mddev->recovery))
			/* nothing to be done ... */
			goto unlock;

		if (mddev->pers->sync_request) {
			if (spares) {
				/* We are adding a device or devices to an array
				 * which has the bitmap stored on all devices.
				 * So make sure all bitmap pages get written
				 */
				bitmap_write_all(mddev->bitmap);
			}
			mddev->sync_thread = md_register_thread(md_do_sync,
								mddev,
								"resync");
			if (!mddev->sync_thread) {
				printk(KERN_ERR "%s: could not start resync"
					" thread...\n", 
					mdname(mddev));
				/* leave the spares where they are, it shouldn't hurt */
				clear_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
				clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
				clear_bit(MD_RECOVERY_RESHAPE, &mddev->recovery);
				clear_bit(MD_RECOVERY_REQUESTED, &mddev->recovery);
				clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
			} else
				md_wakeup_thread(mddev->sync_thread);
			sysfs_notify_dirent_safe(mddev->sysfs_action);
			md_new_event(mddev);
		}
	unlock:
		if (!mddev->sync_thread) {
			clear_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
			if (test_and_clear_bit(MD_RECOVERY_RECOVER,
					       &mddev->recovery))
				if (mddev->sysfs_action)
					sysfs_notify_dirent_safe(mddev->sysfs_action);
		}
		mddev_unlock(mddev);
	}
}

void md_wait_for_blocked_rdev(struct md_rdev *rdev, struct mddev *mddev)
{
	sysfs_notify_dirent_safe(rdev->sysfs_state);
	wait_event_timeout(rdev->blocked_wait,
			   !test_bit(Blocked, &rdev->flags) &&
			   !test_bit(BlockedBadBlocks, &rdev->flags),
			   msecs_to_jiffies(5000));
	rdev_dec_pending(rdev, mddev);
}
EXPORT_SYMBOL(md_wait_for_blocked_rdev);

void md_finish_reshape(struct mddev *mddev)
{
	/* called be personality module when reshape completes. */
	struct md_rdev *rdev;

	rdev_for_each(rdev, mddev) {
		if (rdev->data_offset > rdev->new_data_offset)
			rdev->sectors += rdev->data_offset - rdev->new_data_offset;
		else
			rdev->sectors -= rdev->new_data_offset - rdev->data_offset;
		rdev->data_offset = rdev->new_data_offset;
	}
}
EXPORT_SYMBOL(md_finish_reshape);

/* Bad block management.
 * We can record which blocks on each device are 'bad' and so just
 * fail those blocks, or that stripe, rather than the whole device.
 * Entries in the bad-block table are 64bits wide.  This comprises:
 * Length of bad-range, in sectors: 0-511 for lengths 1-512
 * Start of bad-range, sector offset, 54 bits (allows 8 exbibytes)
 *  A 'shift' can be set so that larger blocks are tracked and
 *  consequently larger devices can be covered.
 * 'Acknowledged' flag - 1 bit. - the most significant bit.
 *
 * Locking of the bad-block table uses a seqlock so md_is_badblock
 * might need to retry if it is very unlucky.
 * We will sometimes want to check for bad blocks in a bi_end_io function,
 * so we use the write_seqlock_irq variant.
 *
 * When looking for a bad block we specify a range and want to
 * know if any block in the range is bad.  So we binary-search
 * to the last range that starts at-or-before the given endpoint,
 * (or "before the sector after the target range")
 * then see if it ends after the given start.
 * We return
 *  0 if there are no known bad blocks in the range
 *  1 if there are known bad block which are all acknowledged
 * -1 if there are bad blocks which have not yet been acknowledged in metadata.
 * plus the start/length of the first bad section we overlap.
 */
int md_is_badblock(struct badblocks *bb, sector_t s, int sectors,
		   sector_t *first_bad, int *bad_sectors)
{
	int hi;
	int lo = 0;
	u64 *p = bb->page;
	int rv = 0;
	sector_t target = s + sectors;
	unsigned seq;

	if (bb->shift > 0) {
		/* round the start down, and the end up */
		s >>= bb->shift;
		target += (1<<bb->shift) - 1;
		target >>= bb->shift;
		sectors = target - s;
	}
	/* 'target' is now the first block after the bad range */

retry:
	seq = read_seqbegin(&bb->lock);

	hi = bb->count;

	/* Binary search between lo and hi for 'target'
	 * i.e. for the last range that starts before 'target'
	 */
	/* INVARIANT: ranges before 'lo' and at-or-after 'hi'
	 * are known not to be the last range before target.
	 * VARIANT: hi-lo is the number of possible
	 * ranges, and decreases until it reaches 1
	 */
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		sector_t a = BB_OFFSET(p[mid]);
		if (a < target)
			/* This could still be the one, earlier ranges
			 * could not. */
			lo = mid;
		else
			/* This and later ranges are definitely out. */
			hi = mid;
	}
	/* 'lo' might be the last that started before target, but 'hi' isn't */
	if (hi > lo) {
		/* need to check all range that end after 's' to see if
		 * any are unacknowledged.
		 */
		while (lo >= 0 &&
		       BB_OFFSET(p[lo]) + BB_LEN(p[lo]) > s) {
			if (BB_OFFSET(p[lo]) < target) {
				/* starts before the end, and finishes after
				 * the start, so they must overlap
				 */
				if (rv != -1 && BB_ACK(p[lo]))
					rv = 1;
				else
					rv = -1;
				*first_bad = BB_OFFSET(p[lo]);
				*bad_sectors = BB_LEN(p[lo]);
			}
			lo--;
		}
	}

	if (read_seqretry(&bb->lock, seq))
		goto retry;

	return rv;
}
EXPORT_SYMBOL_GPL(md_is_badblock);

/*
 * Add a range of bad blocks to the table.
 * This might extend the table, or might contract it
 * if two adjacent ranges can be merged.
 * We binary-search to find the 'insertion' point, then
 * decide how best to handle it.
 */
static int md_set_badblocks(struct badblocks *bb, sector_t s, int sectors,
			    int acknowledged)
{
	u64 *p;
	int lo, hi;
	int rv = 1;

	if (bb->shift < 0)
		/* badblocks are disabled */
		return 0;

	if (bb->shift) {
		/* round the start down, and the end up */
		sector_t next = s + sectors;
		s >>= bb->shift;
		next += (1<<bb->shift) - 1;
		next >>= bb->shift;
		sectors = next - s;
	}

	write_seqlock_irq(&bb->lock);

	p = bb->page;
	lo = 0;
	hi = bb->count;
	/* Find the last range that starts at-or-before 's' */
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		sector_t a = BB_OFFSET(p[mid]);
		if (a <= s)
			lo = mid;
		else
			hi = mid;
	}
	if (hi > lo && BB_OFFSET(p[lo]) > s)
		hi = lo;

	if (hi > lo) {
		/* we found a range that might merge with the start
		 * of our new range
		 */
		sector_t a = BB_OFFSET(p[lo]);
		sector_t e = a + BB_LEN(p[lo]);
		int ack = BB_ACK(p[lo]);
		if (e >= s) {
			/* Yes, we can merge with a previous range */
			if (s == a && s + sectors >= e)
				/* new range covers old */
				ack = acknowledged;
			else
				ack = ack && acknowledged;

			if (e < s + sectors)
				e = s + sectors;
			if (e - a <= BB_MAX_LEN) {
				p[lo] = BB_MAKE(a, e-a, ack);
				s = e;
			} else {
				/* does not all fit in one range,
				 * make p[lo] maximal
				 */
				if (BB_LEN(p[lo]) != BB_MAX_LEN)
					p[lo] = BB_MAKE(a, BB_MAX_LEN, ack);
				s = a + BB_MAX_LEN;
			}
			sectors = e - s;
		}
	}
	if (sectors && hi < bb->count) {
		/* 'hi' points to the first range that starts after 's'.
		 * Maybe we can merge with the start of that range */
		sector_t a = BB_OFFSET(p[hi]);
		sector_t e = a + BB_LEN(p[hi]);
		int ack = BB_ACK(p[hi]);
		if (a <= s + sectors) {
			/* merging is possible */
			if (e <= s + sectors) {
				/* full overlap */
				e = s + sectors;
				ack = acknowledged;
			} else
				ack = ack && acknowledged;

			a = s;
			if (e - a <= BB_MAX_LEN) {
				p[hi] = BB_MAKE(a, e-a, ack);
				s = e;
			} else {
				p[hi] = BB_MAKE(a, BB_MAX_LEN, ack);
				s = a + BB_MAX_LEN;
			}
			sectors = e - s;
			lo = hi;
			hi++;
		}
	}
	if (sectors == 0 && hi < bb->count) {
		/* we might be able to combine lo and hi */
		/* Note: 's' is at the end of 'lo' */
		sector_t a = BB_OFFSET(p[hi]);
		int lolen = BB_LEN(p[lo]);
		int hilen = BB_LEN(p[hi]);
		int newlen = lolen + hilen - (s - a);
		if (s >= a && newlen < BB_MAX_LEN) {
			/* yes, we can combine them */
			int ack = BB_ACK(p[lo]) && BB_ACK(p[hi]);
			p[lo] = BB_MAKE(BB_OFFSET(p[lo]), newlen, ack);
			memmove(p + hi, p + hi + 1,
				(bb->count - hi - 1) * 8);
			bb->count--;
		}
	}
	while (sectors) {
		/* didn't merge (it all).
		 * Need to add a range just before 'hi' */
		if (bb->count >= MD_MAX_BADBLOCKS) {
			/* No room for more */
			rv = 0;
			break;
		} else {
			int this_sectors = sectors;
			memmove(p + hi + 1, p + hi,
				(bb->count - hi) * 8);
			bb->count++;

			if (this_sectors > BB_MAX_LEN)
				this_sectors = BB_MAX_LEN;
			p[hi] = BB_MAKE(s, this_sectors, acknowledged);
			sectors -= this_sectors;
			s += this_sectors;
		}
	}

	bb->changed = 1;
	if (!acknowledged)
		bb->unacked_exist = 1;
	write_sequnlock_irq(&bb->lock);

	return rv;
}

int rdev_set_badblocks(struct md_rdev *rdev, sector_t s, int sectors,
		       int is_new)
{
	int rv;
	if (is_new)
		s += rdev->new_data_offset;
	else
		s += rdev->data_offset;
	rv = md_set_badblocks(&rdev->badblocks,
			      s, sectors, 0);
	if (rv) {
		/* Make sure they get written out promptly */
		sysfs_notify_dirent_safe(rdev->sysfs_state);
		set_bit(MD_CHANGE_CLEAN, &rdev->mddev->flags);
		md_wakeup_thread(rdev->mddev->thread);
	}
	return rv;
}
EXPORT_SYMBOL_GPL(rdev_set_badblocks);

/*
 * Remove a range of bad blocks from the table.
 * This may involve extending the table if we spilt a region,
 * but it must not fail.  So if the table becomes full, we just
 * drop the remove request.
 */
static int md_clear_badblocks(struct badblocks *bb, sector_t s, int sectors)
{
	u64 *p;
	int lo, hi;
	sector_t target = s + sectors;
	int rv = 0;

	if (bb->shift > 0) {
		/* When clearing we round the start up and the end down.
		 * This should not matter as the shift should align with
		 * the block size and no rounding should ever be needed.
		 * However it is better the think a block is bad when it
		 * isn't than to think a block is not bad when it is.
		 */
		s += (1<<bb->shift) - 1;
		s >>= bb->shift;
		target >>= bb->shift;
		sectors = target - s;
	}

	write_seqlock_irq(&bb->lock);

	p = bb->page;
	lo = 0;
	hi = bb->count;
	/* Find the last range that starts before 'target' */
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		sector_t a = BB_OFFSET(p[mid]);
		if (a < target)
			lo = mid;
		else
			hi = mid;
	}
	if (hi > lo) {
		/* p[lo] is the last range that could overlap the
		 * current range.  Earlier ranges could also overlap,
		 * but only this one can overlap the end of the range.
		 */
		if (BB_OFFSET(p[lo]) + BB_LEN(p[lo]) > target) {
			/* Partial overlap, leave the tail of this range */
			int ack = BB_ACK(p[lo]);
			sector_t a = BB_OFFSET(p[lo]);
			sector_t end = a + BB_LEN(p[lo]);

			if (a < s) {
				/* we need to split this range */
				if (bb->count >= MD_MAX_BADBLOCKS) {
					rv = 0;
					goto out;
				}
				memmove(p+lo+1, p+lo, (bb->count - lo) * 8);
				bb->count++;
				p[lo] = BB_MAKE(a, s-a, ack);
				lo++;
			}
			p[lo] = BB_MAKE(target, end - target, ack);
			/* there is no longer an overlap */
			hi = lo;
			lo--;
		}
		while (lo >= 0 &&
		       BB_OFFSET(p[lo]) + BB_LEN(p[lo]) > s) {
			/* This range does overlap */
			if (BB_OFFSET(p[lo]) < s) {
				/* Keep the early parts of this range. */
				int ack = BB_ACK(p[lo]);
				sector_t start = BB_OFFSET(p[lo]);
				p[lo] = BB_MAKE(start, s - start, ack);
				/* now low doesn't overlap, so.. */
				break;
			}
			lo--;
		}
		/* 'lo' is strictly before, 'hi' is strictly after,
		 * anything between needs to be discarded
		 */
		if (hi - lo > 1) {
			memmove(p+lo+1, p+hi, (bb->count - hi) * 8);
			bb->count -= (hi - lo - 1);
		}
	}

	bb->changed = 1;
out:
	write_sequnlock_irq(&bb->lock);
	return rv;
}

int rdev_clear_badblocks(struct md_rdev *rdev, sector_t s, int sectors,
			 int is_new)
{
	if (is_new)
		s += rdev->new_data_offset;
	else
		s += rdev->data_offset;
	return md_clear_badblocks(&rdev->badblocks,
				  s, sectors);
}
EXPORT_SYMBOL_GPL(rdev_clear_badblocks);

/*
 * Acknowledge all bad blocks in a list.
 * This only succeeds if ->changed is clear.  It is used by
 * in-kernel metadata updates
 */
void md_ack_all_badblocks(struct badblocks *bb)
{
	if (bb->page == NULL || bb->changed)
		/* no point even trying */
		return;
	write_seqlock_irq(&bb->lock);

	if (bb->changed == 0 && bb->unacked_exist) {
		u64 *p = bb->page;
		int i;
		for (i = 0; i < bb->count ; i++) {
			if (!BB_ACK(p[i])) {
				sector_t start = BB_OFFSET(p[i]);
				int len = BB_LEN(p[i]);
				p[i] = BB_MAKE(start, len, 1);
			}
		}
		bb->unacked_exist = 0;
	}
	write_sequnlock_irq(&bb->lock);
}
EXPORT_SYMBOL_GPL(md_ack_all_badblocks);

/* sysfs access to bad-blocks list.
 * We present two files.
 * 'bad-blocks' lists sector numbers and lengths of ranges that
 *    are recorded as bad.  The list is truncated to fit within
 *    the one-page limit of sysfs.
 *    Writing "sector length" to this file adds an acknowledged
 *    bad block list.
 * 'unacknowledged-bad-blocks' lists bad blocks that have not yet
 *    been acknowledged.  Writing to this file adds bad blocks
 *    without acknowledging them.  This is largely for testing.
 */

static ssize_t
badblocks_show(struct badblocks *bb, char *page, int unack)
{
	size_t len;
	int i;
	u64 *p = bb->page;
	unsigned seq;

	if (bb->shift < 0)
		return 0;

retry:
	seq = read_seqbegin(&bb->lock);

	len = 0;
	i = 0;

	while (len < PAGE_SIZE && i < bb->count) {
		sector_t s = BB_OFFSET(p[i]);
		unsigned int length = BB_LEN(p[i]);
		int ack = BB_ACK(p[i]);
		i++;

		if (unack && ack)
			continue;

		len += snprintf(page+len, PAGE_SIZE-len, "%llu %u\n",
				(unsigned long long)s << bb->shift,
				length << bb->shift);
	}
	if (unack && len == 0)
		bb->unacked_exist = 0;

	if (read_seqretry(&bb->lock, seq))
		goto retry;

	return len;
}

#define DO_DEBUG 1

static ssize_t
badblocks_store(struct badblocks *bb, const char *page, size_t len, int unack)
{
	unsigned long long sector;
	int length;
	char newline;
#ifdef DO_DEBUG
	/* Allow clearing via sysfs *only* for testing/debugging.
	 * Normally only a successful write may clear a badblock
	 */
	int clear = 0;
	if (page[0] == '-') {
		clear = 1;
		page++;
	}
#endif /* DO_DEBUG */

	switch (sscanf(page, "%llu %d%c", &sector, &length, &newline)) {
	case 3:
		if (newline != '\n')
			return -EINVAL;
	case 2:
		if (length <= 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

#ifdef DO_DEBUG
	if (clear) {
		md_clear_badblocks(bb, sector, length);
		return len;
	}
#endif /* DO_DEBUG */
	if (md_set_badblocks(bb, sector, length, !unack))
		return len;
	else
		return -ENOSPC;
}

static int md_notify_reboot(struct notifier_block *this,
			    unsigned long code, void *x)
{
	struct list_head *tmp;
	struct mddev *mddev;
	int need_delay = 0;

	for_each_mddev(mddev, tmp) {
		if (mddev_trylock(mddev)) {
			if (mddev->pers)
				__md_stop_writes(mddev);
			mddev->safemode = 2;
			mddev_unlock(mddev);
		}
		need_delay = 1;
	}
	/*
	 * certain more exotic SCSI devices are known to be
	 * volatile wrt too early system reboots. While the
	 * right place to handle this issue is the given
	 * driver, we do want to have a safe RAID driver ...
	 */
	if (need_delay)
		mdelay(1000*1);

	return NOTIFY_DONE;
}

static struct notifier_block md_notifier = {
	.notifier_call	= md_notify_reboot,
	.next		= NULL,
	.priority	= INT_MAX, /* before any real devices */
};

static void md_geninit(void)
{
	pr_debug("md: sizeof(mdp_super_t) = %d\n", (int)sizeof(mdp_super_t));

	proc_create("mdstat", S_IRUGO, NULL, &md_seq_fops);
}

static int __init md_init(void)
{
	int ret = -ENOMEM;

	md_wq = alloc_workqueue("md", WQ_MEM_RECLAIM, 0);
	if (!md_wq)
		goto err_wq;

	md_misc_wq = alloc_workqueue("md_misc", 0, 0);
	if (!md_misc_wq)
		goto err_misc_wq;

	if ((ret = register_blkdev(MD_MAJOR, "md")) < 0)
		goto err_md;

	if ((ret = register_blkdev(0, "mdp")) < 0)
		goto err_mdp;
	mdp_major = ret;

	blk_register_region(MKDEV(MD_MAJOR, 0), 1UL<<MINORBITS, THIS_MODULE,
			    md_probe, NULL, NULL);
	blk_register_region(MKDEV(mdp_major, 0), 1UL<<MINORBITS, THIS_MODULE,
			    md_probe, NULL, NULL);

	register_reboot_notifier(&md_notifier);
	raid_table_header = register_sysctl_table(raid_root_table);

	md_geninit();
	return 0;

err_mdp:
	unregister_blkdev(MD_MAJOR, "md");
err_md:
	destroy_workqueue(md_misc_wq);
err_misc_wq:
	destroy_workqueue(md_wq);
err_wq:
	return ret;
}

#ifndef MODULE

/*
 * Searches all registered partitions for autorun RAID arrays
 * at boot time.
 */

static LIST_HEAD(all_detected_devices);
struct detected_devices_node {
	struct list_head list;
	dev_t dev;
};

void md_autodetect_dev(dev_t dev)
{
	struct detected_devices_node *node_detected_dev;

	node_detected_dev = kzalloc(sizeof(*node_detected_dev), GFP_KERNEL);
	if (node_detected_dev) {
		node_detected_dev->dev = dev;
		list_add_tail(&node_detected_dev->list, &all_detected_devices);
	} else {
		printk(KERN_CRIT "md: md_autodetect_dev: kzalloc failed"
			", skipping dev(%d,%d)\n", MAJOR(dev), MINOR(dev));
	}
}


static void autostart_arrays(int part)
{
	struct md_rdev *rdev;
	struct detected_devices_node *node_detected_dev;
	dev_t dev;
	int i_scanned, i_passed;

	i_scanned = 0;
	i_passed = 0;

	printk(KERN_INFO "md: Autodetecting RAID arrays.\n");

	while (!list_empty(&all_detected_devices) && i_scanned < INT_MAX) {
		i_scanned++;
		node_detected_dev = list_entry(all_detected_devices.next,
					struct detected_devices_node, list);
		list_del(&node_detected_dev->list);
		dev = node_detected_dev->dev;
		kfree(node_detected_dev);
		rdev = md_import_device(dev,0, 90);
		if (IS_ERR(rdev))
			continue;

		if (test_bit(Faulty, &rdev->flags)) {
			MD_BUG();
			continue;
		}
		set_bit(AutoDetected, &rdev->flags);
		list_add(&rdev->same_set, &pending_raid_disks);
		i_passed++;
	}

	printk(KERN_INFO "md: Scanned %d and added %d devices.\n",
						i_scanned, i_passed);

	autorun_devices(part);
}

#endif /* !MODULE */

static __exit void md_exit(void)
{
	struct mddev *mddev;
	struct list_head *tmp;

	blk_unregister_region(MKDEV(MD_MAJOR,0), 1U << MINORBITS);
	blk_unregister_region(MKDEV(mdp_major,0), 1U << MINORBITS);

	unregister_blkdev(MD_MAJOR,"md");
	unregister_blkdev(mdp_major, "mdp");
	unregister_reboot_notifier(&md_notifier);
	unregister_sysctl_table(raid_table_header);
	remove_proc_entry("mdstat", NULL);
	for_each_mddev(mddev, tmp) {
		export_array(mddev);
		mddev->hold_active = 0;
	}
	destroy_workqueue(md_misc_wq);
	destroy_workqueue(md_wq);
}

subsys_initcall(md_init);
module_exit(md_exit)

static int get_ro(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%d", start_readonly);
}
static int set_ro(const char *val, struct kernel_param *kp)
{
	char *e;
	int num = simple_strtoul(val, &e, 10);
	if (*val && (*e == '\0' || *e == '\n')) {
		start_readonly = num;
		return 0;
	}
	return -EINVAL;
}

module_param_call(start_ro, set_ro, get_ro, NULL, S_IRUSR|S_IWUSR);
module_param(start_dirty_degraded, int, S_IRUGO|S_IWUSR);

module_param_call(new_array, add_named_array, NULL, NULL, S_IWUSR);

EXPORT_SYMBOL(register_md_personality);
EXPORT_SYMBOL(unregister_md_personality);
EXPORT_SYMBOL(md_error);
EXPORT_SYMBOL(md_done_sync);
EXPORT_SYMBOL(md_write_start);
EXPORT_SYMBOL(md_write_end);
EXPORT_SYMBOL(md_register_thread);
EXPORT_SYMBOL(md_unregister_thread);
EXPORT_SYMBOL(md_wakeup_thread);
EXPORT_SYMBOL(md_check_recovery);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MD RAID framework");
MODULE_ALIAS("md");
MODULE_ALIAS_BLOCKDEV_MAJOR(MD_MAJOR);
