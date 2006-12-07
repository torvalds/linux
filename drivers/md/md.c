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

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/linkage.h>
#include <linux/raid/md.h>
#include <linux/raid/bitmap.h>
#include <linux/sysctl.h>
#include <linux/buffer_head.h> /* for invalidate_bdev */
#include <linux/poll.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include <linux/freezer.h>

#include <linux/init.h>

#include <linux/file.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include <asm/unaligned.h>

#define MAJOR_NR MD_MAJOR
#define MD_DRIVER

/* 63 partitions with the alternate major number (mdp) */
#define MdpMinorShift 6

#define DEBUG 0
#define dprintk(x...) ((void)(DEBUG && printk(x)))


#ifndef MODULE
static void autostart_arrays (int part);
#endif

static LIST_HEAD(pers_list);
static DEFINE_SPINLOCK(pers_lock);

static void md_print_devices(void);

#define MD_BUG(x...) { printk("md: bug in file %s, line %d\n", __FILE__, __LINE__); md_print_devices(); }

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
static inline int speed_min(mddev_t *mddev)
{
	return mddev->sync_speed_min ?
		mddev->sync_speed_min : sysctl_speed_limit_min;
}

static inline int speed_max(mddev_t *mddev)
{
	return mddev->sync_speed_max ?
		mddev->sync_speed_max : sysctl_speed_limit_max;
}

static struct ctl_table_header *raid_table_header;

static ctl_table raid_table[] = {
	{
		.ctl_name	= DEV_RAID_SPEED_LIMIT_MIN,
		.procname	= "speed_limit_min",
		.data		= &sysctl_speed_limit_min,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO|S_IWUSR,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= DEV_RAID_SPEED_LIMIT_MAX,
		.procname	= "speed_limit_max",
		.data		= &sysctl_speed_limit_max,
		.maxlen		= sizeof(int),
		.mode		= S_IRUGO|S_IWUSR,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

static ctl_table raid_dir_table[] = {
	{
		.ctl_name	= DEV_RAID,
		.procname	= "raid",
		.maxlen		= 0,
		.mode		= S_IRUGO|S_IXUGO,
		.child		= raid_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table raid_root_table[] = {
	{
		.ctl_name	= CTL_DEV,
		.procname	= "dev",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= raid_dir_table,
	},
	{ .ctl_name = 0 }
};

static struct block_device_operations md_fops;

static int start_readonly;

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
void md_new_event(mddev_t *mddev)
{
	atomic_inc(&md_event_count);
	wake_up(&md_event_waiters);
	sysfs_notify(&mddev->kobj, NULL, "sync_action");
}
EXPORT_SYMBOL_GPL(md_new_event);

/* Alternate version that can be called from interrupts
 * when calling sysfs_notify isn't needed.
 */
static void md_new_event_inintr(mddev_t *mddev)
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
#define ITERATE_MDDEV(mddev,tmp)					\
									\
	for (({ spin_lock(&all_mddevs_lock); 				\
		tmp = all_mddevs.next;					\
		mddev = NULL;});					\
	     ({ if (tmp != &all_mddevs)					\
			mddev_get(list_entry(tmp, mddev_t, all_mddevs));\
		spin_unlock(&all_mddevs_lock);				\
		if (mddev) mddev_put(mddev);				\
		mddev = list_entry(tmp, mddev_t, all_mddevs);		\
		tmp != &all_mddevs;});					\
	     ({ spin_lock(&all_mddevs_lock);				\
		tmp = tmp->next;})					\
		)


static int md_fail_request (request_queue_t *q, struct bio *bio)
{
	bio_io_error(bio, bio->bi_size);
	return 0;
}

static inline mddev_t *mddev_get(mddev_t *mddev)
{
	atomic_inc(&mddev->active);
	return mddev;
}

static void mddev_put(mddev_t *mddev)
{
	if (!atomic_dec_and_lock(&mddev->active, &all_mddevs_lock))
		return;
	if (!mddev->raid_disks && list_empty(&mddev->disks)) {
		list_del(&mddev->all_mddevs);
		spin_unlock(&all_mddevs_lock);
		blk_cleanup_queue(mddev->queue);
		kobject_unregister(&mddev->kobj);
	} else
		spin_unlock(&all_mddevs_lock);
}

static mddev_t * mddev_find(dev_t unit)
{
	mddev_t *mddev, *new = NULL;

 retry:
	spin_lock(&all_mddevs_lock);
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

	mutex_init(&new->reconfig_mutex);
	INIT_LIST_HEAD(&new->disks);
	INIT_LIST_HEAD(&new->all_mddevs);
	init_timer(&new->safemode_timer);
	atomic_set(&new->active, 1);
	spin_lock_init(&new->write_lock);
	init_waitqueue_head(&new->sb_wait);

	new->queue = blk_alloc_queue(GFP_KERNEL);
	if (!new->queue) {
		kfree(new);
		return NULL;
	}
	set_bit(QUEUE_FLAG_CLUSTER, &new->queue->queue_flags);

	blk_queue_make_request(new->queue, md_fail_request);

	goto retry;
}

static inline int mddev_lock(mddev_t * mddev)
{
	return mutex_lock_interruptible(&mddev->reconfig_mutex);
}

static inline int mddev_trylock(mddev_t * mddev)
{
	return mutex_trylock(&mddev->reconfig_mutex);
}

static inline void mddev_unlock(mddev_t * mddev)
{
	mutex_unlock(&mddev->reconfig_mutex);

	md_wakeup_thread(mddev->thread);
}

static mdk_rdev_t * find_rdev_nr(mddev_t *mddev, int nr)
{
	mdk_rdev_t * rdev;
	struct list_head *tmp;

	ITERATE_RDEV(mddev,rdev,tmp) {
		if (rdev->desc_nr == nr)
			return rdev;
	}
	return NULL;
}

static mdk_rdev_t * find_rdev(mddev_t * mddev, dev_t dev)
{
	struct list_head *tmp;
	mdk_rdev_t *rdev;

	ITERATE_RDEV(mddev,rdev,tmp) {
		if (rdev->bdev->bd_dev == dev)
			return rdev;
	}
	return NULL;
}

static struct mdk_personality *find_pers(int level, char *clevel)
{
	struct mdk_personality *pers;
	list_for_each_entry(pers, &pers_list, list) {
		if (level != LEVEL_NONE && pers->level == level)
			return pers;
		if (strcmp(pers->name, clevel)==0)
			return pers;
	}
	return NULL;
}

static inline sector_t calc_dev_sboffset(struct block_device *bdev)
{
	sector_t size = bdev->bd_inode->i_size >> BLOCK_SIZE_BITS;
	return MD_NEW_SIZE_BLOCKS(size);
}

static sector_t calc_dev_size(mdk_rdev_t *rdev, unsigned chunk_size)
{
	sector_t size;

	size = rdev->sb_offset;

	if (chunk_size)
		size &= ~((sector_t)chunk_size/1024 - 1);
	return size;
}

static int alloc_disk_sb(mdk_rdev_t * rdev)
{
	if (rdev->sb_page)
		MD_BUG();

	rdev->sb_page = alloc_page(GFP_KERNEL);
	if (!rdev->sb_page) {
		printk(KERN_ALERT "md: out of memory.\n");
		return -EINVAL;
	}

	return 0;
}

static void free_disk_sb(mdk_rdev_t * rdev)
{
	if (rdev->sb_page) {
		put_page(rdev->sb_page);
		rdev->sb_loaded = 0;
		rdev->sb_page = NULL;
		rdev->sb_offset = 0;
		rdev->size = 0;
	}
}


static int super_written(struct bio *bio, unsigned int bytes_done, int error)
{
	mdk_rdev_t *rdev = bio->bi_private;
	mddev_t *mddev = rdev->mddev;
	if (bio->bi_size)
		return 1;

	if (error || !test_bit(BIO_UPTODATE, &bio->bi_flags)) {
		printk("md: super_written gets error=%d, uptodate=%d\n",
		       error, test_bit(BIO_UPTODATE, &bio->bi_flags));
		WARN_ON(test_bit(BIO_UPTODATE, &bio->bi_flags));
		md_error(mddev, rdev);
	}

	if (atomic_dec_and_test(&mddev->pending_writes))
		wake_up(&mddev->sb_wait);
	bio_put(bio);
	return 0;
}

static int super_written_barrier(struct bio *bio, unsigned int bytes_done, int error)
{
	struct bio *bio2 = bio->bi_private;
	mdk_rdev_t *rdev = bio2->bi_private;
	mddev_t *mddev = rdev->mddev;
	if (bio->bi_size)
		return 1;

	if (!test_bit(BIO_UPTODATE, &bio->bi_flags) &&
	    error == -EOPNOTSUPP) {
		unsigned long flags;
		/* barriers don't appear to be supported :-( */
		set_bit(BarriersNotsupp, &rdev->flags);
		mddev->barriers_work = 0;
		spin_lock_irqsave(&mddev->write_lock, flags);
		bio2->bi_next = mddev->biolist;
		mddev->biolist = bio2;
		spin_unlock_irqrestore(&mddev->write_lock, flags);
		wake_up(&mddev->sb_wait);
		bio_put(bio);
		return 0;
	}
	bio_put(bio2);
	bio->bi_private = rdev;
	return super_written(bio, bytes_done, error);
}

void md_super_write(mddev_t *mddev, mdk_rdev_t *rdev,
		   sector_t sector, int size, struct page *page)
{
	/* write first size bytes of page to sector of rdev
	 * Increment mddev->pending_writes before returning
	 * and decrement it on completion, waking up sb_wait
	 * if zero is reached.
	 * If an error occurred, call md_error
	 *
	 * As we might need to resubmit the request if BIO_RW_BARRIER
	 * causes ENOTSUPP, we allocate a spare bio...
	 */
	struct bio *bio = bio_alloc(GFP_NOIO, 1);
	int rw = (1<<BIO_RW) | (1<<BIO_RW_SYNC);

	bio->bi_bdev = rdev->bdev;
	bio->bi_sector = sector;
	bio_add_page(bio, page, size, 0);
	bio->bi_private = rdev;
	bio->bi_end_io = super_written;
	bio->bi_rw = rw;

	atomic_inc(&mddev->pending_writes);
	if (!test_bit(BarriersNotsupp, &rdev->flags)) {
		struct bio *rbio;
		rw |= (1<<BIO_RW_BARRIER);
		rbio = bio_clone(bio, GFP_NOIO);
		rbio->bi_private = bio;
		rbio->bi_end_io = super_written_barrier;
		submit_bio(rw, rbio);
	} else
		submit_bio(rw, bio);
}

void md_super_wait(mddev_t *mddev)
{
	/* wait for all superblock writes that were scheduled to complete.
	 * if any had to be retried (due to BARRIER problems), retry them
	 */
	DEFINE_WAIT(wq);
	for(;;) {
		prepare_to_wait(&mddev->sb_wait, &wq, TASK_UNINTERRUPTIBLE);
		if (atomic_read(&mddev->pending_writes)==0)
			break;
		while (mddev->biolist) {
			struct bio *bio;
			spin_lock_irq(&mddev->write_lock);
			bio = mddev->biolist;
			mddev->biolist = bio->bi_next ;
			bio->bi_next = NULL;
			spin_unlock_irq(&mddev->write_lock);
			submit_bio(bio->bi_rw, bio);
		}
		schedule();
	}
	finish_wait(&mddev->sb_wait, &wq);
}

static int bi_complete(struct bio *bio, unsigned int bytes_done, int error)
{
	if (bio->bi_size)
		return 1;

	complete((struct completion*)bio->bi_private);
	return 0;
}

int sync_page_io(struct block_device *bdev, sector_t sector, int size,
		   struct page *page, int rw)
{
	struct bio *bio = bio_alloc(GFP_NOIO, 1);
	struct completion event;
	int ret;

	rw |= (1 << BIO_RW_SYNC);

	bio->bi_bdev = bdev;
	bio->bi_sector = sector;
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

static int read_disk_sb(mdk_rdev_t * rdev, int size)
{
	char b[BDEVNAME_SIZE];
	if (!rdev->sb_page) {
		MD_BUG();
		return -EINVAL;
	}
	if (rdev->sb_loaded)
		return 0;


	if (!sync_page_io(rdev->bdev, rdev->sb_offset<<1, size, rdev->sb_page, READ))
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
	if (	(sb1->set_uuid0 == sb2->set_uuid0) &&
		(sb1->set_uuid1 == sb2->set_uuid1) &&
		(sb1->set_uuid2 == sb2->set_uuid2) &&
		(sb1->set_uuid3 == sb2->set_uuid3))

		return 1;

	return 0;
}


static int sb_equal(mdp_super_t *sb1, mdp_super_t *sb2)
{
	int ret;
	mdp_super_t *tmp1, *tmp2;

	tmp1 = kmalloc(sizeof(*tmp1),GFP_KERNEL);
	tmp2 = kmalloc(sizeof(*tmp2),GFP_KERNEL);

	if (!tmp1 || !tmp2) {
		ret = 0;
		printk(KERN_INFO "md.c: sb1 is not equal to sb2!\n");
		goto abort;
	}

	*tmp1 = *sb1;
	*tmp2 = *sb2;

	/*
	 * nr_disks is not constant
	 */
	tmp1->nr_disks = 0;
	tmp2->nr_disks = 0;

	if (memcmp(tmp1, tmp2, MD_SB_GENERIC_CONSTANT_WORDS * 4))
		ret = 0;
	else
		ret = 1;

abort:
	kfree(tmp1);
	kfree(tmp2);
	return ret;
}

static unsigned int calc_sb_csum(mdp_super_t * sb)
{
	unsigned int disk_csum, csum;

	disk_csum = sb->sb_csum;
	sb->sb_csum = 0;
	csum = csum_partial((void *)sb, MD_SB_BYTES, 0);
	sb->sb_csum = disk_csum;
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
 *   int load_super(mdk_rdev_t *dev, mdk_rdev_t *refdev, int minor_version)
 *      loads and validates a superblock on dev.
 *      if refdev != NULL, compare superblocks on both devices
 *    Return:
 *      0 - dev has a superblock that is compatible with refdev
 *      1 - dev has a superblock that is compatible and newer than refdev
 *          so dev should be used as the refdev in future
 *     -EINVAL superblock incompatible or invalid
 *     -othererror e.g. -EIO
 *
 *   int validate_super(mddev_t *mddev, mdk_rdev_t *dev)
 *      Verify that dev is acceptable into mddev.
 *       The first time, mddev->raid_disks will be 0, and data from
 *       dev should be merged in.  Subsequent calls check that dev
 *       is new enough.  Return 0 or -EINVAL
 *
 *   void sync_super(mddev_t *mddev, mdk_rdev_t *dev)
 *     Update the superblock for rdev with data in mddev
 *     This does not write to disc.
 *
 */

struct super_type  {
	char 		*name;
	struct module	*owner;
	int		(*load_super)(mdk_rdev_t *rdev, mdk_rdev_t *refdev, int minor_version);
	int		(*validate_super)(mddev_t *mddev, mdk_rdev_t *rdev);
	void		(*sync_super)(mddev_t *mddev, mdk_rdev_t *rdev);
};

/*
 * load_super for 0.90.0 
 */
static int super_90_load(mdk_rdev_t *rdev, mdk_rdev_t *refdev, int minor_version)
{
	char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
	mdp_super_t *sb;
	int ret;
	sector_t sb_offset;

	/*
	 * Calculate the position of the superblock,
	 * it's at the end of the disk.
	 *
	 * It also happens to be a multiple of 4Kb.
	 */
	sb_offset = calc_dev_sboffset(rdev->bdev);
	rdev->sb_offset = sb_offset;

	ret = read_disk_sb(rdev, MD_SB_BYTES);
	if (ret) return ret;

	ret = -EINVAL;

	bdevname(rdev->bdev, b);
	sb = (mdp_super_t*)page_address(rdev->sb_page);

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

	if (csum_fold(calc_sb_csum(sb)) != csum_fold(sb->sb_csum)) {
		printk(KERN_WARNING "md: invalid superblock checksum on %s\n",
			b);
		goto abort;
	}

	rdev->preferred_minor = sb->md_minor;
	rdev->data_offset = 0;
	rdev->sb_size = MD_SB_BYTES;

	if (sb->level == LEVEL_MULTIPATH)
		rdev->desc_nr = -1;
	else
		rdev->desc_nr = sb->this_disk.number;

	if (refdev == 0)
		ret = 1;
	else {
		__u64 ev1, ev2;
		mdp_super_t *refsb = (mdp_super_t*)page_address(refdev->sb_page);
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
	rdev->size = calc_dev_size(rdev, sb->chunk_size);

	if (rdev->size < sb->size && sb->level > 1)
		/* "this cannot possibly happen" ... */
		ret = -EINVAL;

 abort:
	return ret;
}

/*
 * validate_super for 0.90.0
 */
static int super_90_validate(mddev_t *mddev, mdk_rdev_t *rdev)
{
	mdp_disk_t *desc;
	mdp_super_t *sb = (mdp_super_t *)page_address(rdev->sb_page);
	__u64 ev1 = md_event(sb);

	rdev->raid_disk = -1;
	rdev->flags = 0;
	if (mddev->raid_disks == 0) {
		mddev->major_version = 0;
		mddev->minor_version = sb->minor_version;
		mddev->patch_version = sb->patch_version;
		mddev->persistent = ! sb->not_persistent;
		mddev->chunk_size = sb->chunk_size;
		mddev->ctime = sb->ctime;
		mddev->utime = sb->utime;
		mddev->level = sb->level;
		mddev->clevel[0] = 0;
		mddev->layout = sb->layout;
		mddev->raid_disks = sb->raid_disks;
		mddev->size = sb->size;
		mddev->events = ev1;
		mddev->bitmap_offset = 0;
		mddev->default_bitmap_offset = MD_SB_BYTES >> 9;

		if (mddev->minor_version >= 91) {
			mddev->reshape_position = sb->reshape_position;
			mddev->delta_disks = sb->delta_disks;
			mddev->new_level = sb->new_level;
			mddev->new_layout = sb->new_layout;
			mddev->new_chunk = sb->new_chunk;
		} else {
			mddev->reshape_position = MaxSector;
			mddev->delta_disks = 0;
			mddev->new_level = mddev->level;
			mddev->new_layout = mddev->layout;
			mddev->new_chunk = mddev->chunk_size;
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
		    mddev->bitmap_file == NULL) {
			if (mddev->level != 1 && mddev->level != 4
			    && mddev->level != 5 && mddev->level != 6
			    && mddev->level != 10) {
				/* FIXME use a better test */
				printk(KERN_WARNING "md: bitmaps not supported for this level.\n");
				return -EINVAL;
			}
			mddev->bitmap_offset = mddev->default_bitmap_offset;
		}

	} else if (mddev->pers == NULL) {
		/* Insist on good event counter while assembling */
		++ev1;
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
static void super_90_sync(mddev_t *mddev, mdk_rdev_t *rdev)
{
	mdp_super_t *sb;
	struct list_head *tmp;
	mdk_rdev_t *rdev2;
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

	sb = (mdp_super_t*)page_address(rdev->sb_page);

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
	sb->size  = mddev->size;
	sb->raid_disks = mddev->raid_disks;
	sb->md_minor = mddev->md_minor;
	sb->not_persistent = !mddev->persistent;
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
		sb->new_chunk = mddev->new_chunk;
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
	sb->chunk_size = mddev->chunk_size;

	if (mddev->bitmap && mddev->bitmap_file == NULL)
		sb->state |= (1<<MD_SB_BITMAP_PRESENT);

	sb->disks[0].state = (1<<MD_DISK_REMOVED);
	ITERATE_RDEV(mddev,rdev2,tmp) {
		mdp_disk_t *d;
		int desc_nr;
		if (rdev2->raid_disk >= 0 && test_bit(In_sync, &rdev2->flags)
		    && !test_bit(Faulty, &rdev2->flags))
			desc_nr = rdev2->raid_disk;
		else
			desc_nr = next_spare++;
		rdev2->desc_nr = desc_nr;
		d = &sb->disks[rdev2->desc_nr];
		nr_disks++;
		d->number = rdev2->desc_nr;
		d->major = MAJOR(rdev2->bdev->bd_dev);
		d->minor = MINOR(rdev2->bdev->bd_dev);
		if (rdev2->raid_disk >= 0 && test_bit(In_sync, &rdev2->flags)
		    && !test_bit(Faulty, &rdev2->flags))
			d->raid_disk = rdev2->raid_disk;
		else
			d->raid_disk = rdev2->desc_nr; /* compatibility */
		if (test_bit(Faulty, &rdev2->flags))
			d->state = (1<<MD_DISK_FAULTY);
		else if (test_bit(In_sync, &rdev2->flags)) {
			d->state = (1<<MD_DISK_ACTIVE);
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

static int super_1_load(mdk_rdev_t *rdev, mdk_rdev_t *refdev, int minor_version)
{
	struct mdp_superblock_1 *sb;
	int ret;
	sector_t sb_offset;
	char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
	int bmask;

	/*
	 * Calculate the position of the superblock.
	 * It is always aligned to a 4K boundary and
	 * depeding on minor_version, it can be:
	 * 0: At least 8K, but less than 12K, from end of device
	 * 1: At start of device
	 * 2: 4K from start of device.
	 */
	switch(minor_version) {
	case 0:
		sb_offset = rdev->bdev->bd_inode->i_size >> 9;
		sb_offset -= 8*2;
		sb_offset &= ~(sector_t)(4*2-1);
		/* convert from sectors to K */
		sb_offset /= 2;
		break;
	case 1:
		sb_offset = 0;
		break;
	case 2:
		sb_offset = 4;
		break;
	default:
		return -EINVAL;
	}
	rdev->sb_offset = sb_offset;

	/* superblock is rarely larger than 1K, but it can be larger,
	 * and it is safe to read 4k, so we do that
	 */
	ret = read_disk_sb(rdev, 4096);
	if (ret) return ret;


	sb = (struct mdp_superblock_1*)page_address(rdev->sb_page);

	if (sb->magic != cpu_to_le32(MD_SB_MAGIC) ||
	    sb->major_version != cpu_to_le32(1) ||
	    le32_to_cpu(sb->max_dev) > (4096-256)/2 ||
	    le64_to_cpu(sb->super_offset) != (rdev->sb_offset<<1) ||
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
	rdev->preferred_minor = 0xffff;
	rdev->data_offset = le64_to_cpu(sb->data_offset);
	atomic_set(&rdev->corrected_errors, le32_to_cpu(sb->cnt_corrected_read));

	rdev->sb_size = le32_to_cpu(sb->max_dev) * 2 + 256;
	bmask = queue_hardsect_size(rdev->bdev->bd_disk->queue)-1;
	if (rdev->sb_size & bmask)
		rdev-> sb_size = (rdev->sb_size | bmask)+1;

	if (sb->level == cpu_to_le32(LEVEL_MULTIPATH))
		rdev->desc_nr = -1;
	else
		rdev->desc_nr = le32_to_cpu(sb->dev_number);

	if (refdev == 0)
		ret = 1;
	else {
		__u64 ev1, ev2;
		struct mdp_superblock_1 *refsb = 
			(struct mdp_superblock_1*)page_address(refdev->sb_page);

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
	if (minor_version) 
		rdev->size = ((rdev->bdev->bd_inode->i_size>>9) - le64_to_cpu(sb->data_offset)) / 2;
	else
		rdev->size = rdev->sb_offset;
	if (rdev->size < le64_to_cpu(sb->data_size)/2)
		return -EINVAL;
	rdev->size = le64_to_cpu(sb->data_size)/2;
	if (le32_to_cpu(sb->chunksize))
		rdev->size &= ~((sector_t)le32_to_cpu(sb->chunksize)/2 - 1);

	if (le64_to_cpu(sb->size) > rdev->size*2)
		return -EINVAL;
	return ret;
}

static int super_1_validate(mddev_t *mddev, mdk_rdev_t *rdev)
{
	struct mdp_superblock_1 *sb = (struct mdp_superblock_1*)page_address(rdev->sb_page);
	__u64 ev1 = le64_to_cpu(sb->events);

	rdev->raid_disk = -1;
	rdev->flags = 0;
	if (mddev->raid_disks == 0) {
		mddev->major_version = 1;
		mddev->patch_version = 0;
		mddev->persistent = 1;
		mddev->chunk_size = le32_to_cpu(sb->chunksize) << 9;
		mddev->ctime = le64_to_cpu(sb->ctime) & ((1ULL << 32)-1);
		mddev->utime = le64_to_cpu(sb->utime) & ((1ULL << 32)-1);
		mddev->level = le32_to_cpu(sb->level);
		mddev->clevel[0] = 0;
		mddev->layout = le32_to_cpu(sb->layout);
		mddev->raid_disks = le32_to_cpu(sb->raid_disks);
		mddev->size = le64_to_cpu(sb->size)/2;
		mddev->events = ev1;
		mddev->bitmap_offset = 0;
		mddev->default_bitmap_offset = 1024 >> 9;
		
		mddev->recovery_cp = le64_to_cpu(sb->resync_offset);
		memcpy(mddev->uuid, sb->set_uuid, 16);

		mddev->max_disks =  (4096-256)/2;

		if ((le32_to_cpu(sb->feature_map) & MD_FEATURE_BITMAP_OFFSET) &&
		    mddev->bitmap_file == NULL ) {
			if (mddev->level != 1 && mddev->level != 5 && mddev->level != 6
			    && mddev->level != 10) {
				printk(KERN_WARNING "md: bitmaps not supported for this level.\n");
				return -EINVAL;
			}
			mddev->bitmap_offset = (__s32)le32_to_cpu(sb->bitmap_offset);
		}
		if ((le32_to_cpu(sb->feature_map) & MD_FEATURE_RESHAPE_ACTIVE)) {
			mddev->reshape_position = le64_to_cpu(sb->reshape_position);
			mddev->delta_disks = le32_to_cpu(sb->delta_disks);
			mddev->new_level = le32_to_cpu(sb->new_level);
			mddev->new_layout = le32_to_cpu(sb->new_layout);
			mddev->new_chunk = le32_to_cpu(sb->new_chunk)<<9;
		} else {
			mddev->reshape_position = MaxSector;
			mddev->delta_disks = 0;
			mddev->new_level = mddev->level;
			mddev->new_layout = mddev->layout;
			mddev->new_chunk = mddev->chunk_size;
		}

	} else if (mddev->pers == NULL) {
		/* Insist of good event counter while assembling */
		++ev1;
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
	} else /* MULTIPATH are always insync */
		set_bit(In_sync, &rdev->flags);

	return 0;
}

static void super_1_sync(mddev_t *mddev, mdk_rdev_t *rdev)
{
	struct mdp_superblock_1 *sb;
	struct list_head *tmp;
	mdk_rdev_t *rdev2;
	int max_dev, i;
	/* make rdev->sb match mddev and rdev data. */

	sb = (struct mdp_superblock_1*)page_address(rdev->sb_page);

	sb->feature_map = 0;
	sb->pad0 = 0;
	sb->recovery_offset = cpu_to_le64(0);
	memset(sb->pad1, 0, sizeof(sb->pad1));
	memset(sb->pad2, 0, sizeof(sb->pad2));
	memset(sb->pad3, 0, sizeof(sb->pad3));

	sb->utime = cpu_to_le64((__u64)mddev->utime);
	sb->events = cpu_to_le64(mddev->events);
	if (mddev->in_sync)
		sb->resync_offset = cpu_to_le64(mddev->recovery_cp);
	else
		sb->resync_offset = cpu_to_le64(0);

	sb->cnt_corrected_read = cpu_to_le32(atomic_read(&rdev->corrected_errors));

	sb->raid_disks = cpu_to_le32(mddev->raid_disks);
	sb->size = cpu_to_le64(mddev->size<<1);

	if (mddev->bitmap && mddev->bitmap_file == NULL) {
		sb->bitmap_offset = cpu_to_le32((__u32)mddev->bitmap_offset);
		sb->feature_map = cpu_to_le32(MD_FEATURE_BITMAP_OFFSET);
	}

	if (rdev->raid_disk >= 0 &&
	    !test_bit(In_sync, &rdev->flags) &&
	    rdev->recovery_offset > 0) {
		sb->feature_map |= cpu_to_le32(MD_FEATURE_RECOVERY_OFFSET);
		sb->recovery_offset = cpu_to_le64(rdev->recovery_offset);
	}

	if (mddev->reshape_position != MaxSector) {
		sb->feature_map |= cpu_to_le32(MD_FEATURE_RESHAPE_ACTIVE);
		sb->reshape_position = cpu_to_le64(mddev->reshape_position);
		sb->new_layout = cpu_to_le32(mddev->new_layout);
		sb->delta_disks = cpu_to_le32(mddev->delta_disks);
		sb->new_level = cpu_to_le32(mddev->new_level);
		sb->new_chunk = cpu_to_le32(mddev->new_chunk>>9);
	}

	max_dev = 0;
	ITERATE_RDEV(mddev,rdev2,tmp)
		if (rdev2->desc_nr+1 > max_dev)
			max_dev = rdev2->desc_nr+1;
	
	sb->max_dev = cpu_to_le32(max_dev);
	for (i=0; i<max_dev;i++)
		sb->dev_roles[i] = cpu_to_le16(0xfffe);
	
	ITERATE_RDEV(mddev,rdev2,tmp) {
		i = rdev2->desc_nr;
		if (test_bit(Faulty, &rdev2->flags))
			sb->dev_roles[i] = cpu_to_le16(0xfffe);
		else if (test_bit(In_sync, &rdev2->flags))
			sb->dev_roles[i] = cpu_to_le16(rdev2->raid_disk);
		else if (rdev2->raid_disk >= 0 && rdev2->recovery_offset > 0)
			sb->dev_roles[i] = cpu_to_le16(rdev2->raid_disk);
		else
			sb->dev_roles[i] = cpu_to_le16(0xffff);
	}

	sb->sb_csum = calc_sb_1_csum(sb);
}


static struct super_type super_types[] = {
	[0] = {
		.name	= "0.90.0",
		.owner	= THIS_MODULE,
		.load_super	= super_90_load,
		.validate_super	= super_90_validate,
		.sync_super	= super_90_sync,
	},
	[1] = {
		.name	= "md-1",
		.owner	= THIS_MODULE,
		.load_super	= super_1_load,
		.validate_super	= super_1_validate,
		.sync_super	= super_1_sync,
	},
};
	
static mdk_rdev_t * match_dev_unit(mddev_t *mddev, mdk_rdev_t *dev)
{
	struct list_head *tmp;
	mdk_rdev_t *rdev;

	ITERATE_RDEV(mddev,rdev,tmp)
		if (rdev->bdev->bd_contains == dev->bdev->bd_contains)
			return rdev;

	return NULL;
}

static int match_mddev_units(mddev_t *mddev1, mddev_t *mddev2)
{
	struct list_head *tmp;
	mdk_rdev_t *rdev;

	ITERATE_RDEV(mddev1,rdev,tmp)
		if (match_dev_unit(mddev2, rdev))
			return 1;

	return 0;
}

static LIST_HEAD(pending_raid_disks);

static int bind_rdev_to_array(mdk_rdev_t * rdev, mddev_t * mddev)
{
	mdk_rdev_t *same_pdev;
	char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
	struct kobject *ko;
	char *s;

	if (rdev->mddev) {
		MD_BUG();
		return -EINVAL;
	}
	/* make sure rdev->size exceeds mddev->size */
	if (rdev->size && (mddev->size == 0 || rdev->size < mddev->size)) {
		if (mddev->pers)
			/* Cannot change size, so fail */
			return -ENOSPC;
		else
			mddev->size = rdev->size;
	}
	same_pdev = match_dev_unit(mddev, rdev);
	if (same_pdev)
		printk(KERN_WARNING
			"%s: WARNING: %s appears to be on the same physical"
	 		" disk as %s. True\n     protection against single-disk"
			" failure might be compromised.\n",
			mdname(mddev), bdevname(rdev->bdev,b),
			bdevname(same_pdev->bdev,b2));

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
	bdevname(rdev->bdev,b);
	if (kobject_set_name(&rdev->kobj, "dev-%s", b) < 0)
		return -ENOMEM;
	while ( (s=strchr(rdev->kobj.k_name, '/')) != NULL)
		*s = '!';
			
	list_add(&rdev->same_set, &mddev->disks);
	rdev->mddev = mddev;
	printk(KERN_INFO "md: bind<%s>\n", b);

	rdev->kobj.parent = &mddev->kobj;
	kobject_add(&rdev->kobj);

	if (rdev->bdev->bd_part)
		ko = &rdev->bdev->bd_part->kobj;
	else
		ko = &rdev->bdev->bd_disk->kobj;
	sysfs_create_link(&rdev->kobj, ko, "block");
	bd_claim_by_disk(rdev->bdev, rdev, mddev->gendisk);
	return 0;
}

static void unbind_rdev_from_array(mdk_rdev_t * rdev)
{
	char b[BDEVNAME_SIZE];
	if (!rdev->mddev) {
		MD_BUG();
		return;
	}
	bd_release_from_disk(rdev->bdev, rdev->mddev->gendisk);
	list_del_init(&rdev->same_set);
	printk(KERN_INFO "md: unbind<%s>\n", bdevname(rdev->bdev,b));
	rdev->mddev = NULL;
	sysfs_remove_link(&rdev->kobj, "block");
	kobject_del(&rdev->kobj);
}

/*
 * prevent the device from being mounted, repartitioned or
 * otherwise reused by a RAID array (or any other kernel
 * subsystem), by bd_claiming the device.
 */
static int lock_rdev(mdk_rdev_t *rdev, dev_t dev)
{
	int err = 0;
	struct block_device *bdev;
	char b[BDEVNAME_SIZE];

	bdev = open_partition_by_devnum(dev, FMODE_READ|FMODE_WRITE);
	if (IS_ERR(bdev)) {
		printk(KERN_ERR "md: could not open %s.\n",
			__bdevname(dev, b));
		return PTR_ERR(bdev);
	}
	err = bd_claim(bdev, rdev);
	if (err) {
		printk(KERN_ERR "md: could not bd_claim %s.\n",
			bdevname(bdev, b));
		blkdev_put_partition(bdev);
		return err;
	}
	rdev->bdev = bdev;
	return err;
}

static void unlock_rdev(mdk_rdev_t *rdev)
{
	struct block_device *bdev = rdev->bdev;
	rdev->bdev = NULL;
	if (!bdev)
		MD_BUG();
	bd_release(bdev);
	blkdev_put_partition(bdev);
}

void md_autodetect_dev(dev_t dev);

static void export_rdev(mdk_rdev_t * rdev)
{
	char b[BDEVNAME_SIZE];
	printk(KERN_INFO "md: export_rdev(%s)\n",
		bdevname(rdev->bdev,b));
	if (rdev->mddev)
		MD_BUG();
	free_disk_sb(rdev);
	list_del_init(&rdev->same_set);
#ifndef MODULE
	md_autodetect_dev(rdev->bdev->bd_dev);
#endif
	unlock_rdev(rdev);
	kobject_put(&rdev->kobj);
}

static void kick_rdev_from_array(mdk_rdev_t * rdev)
{
	unbind_rdev_from_array(rdev);
	export_rdev(rdev);
}

static void export_array(mddev_t *mddev)
{
	struct list_head *tmp;
	mdk_rdev_t *rdev;

	ITERATE_RDEV(mddev,rdev,tmp) {
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

static void print_sb(mdp_super_t *sb)
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

static void print_rdev(mdk_rdev_t *rdev)
{
	char b[BDEVNAME_SIZE];
	printk(KERN_INFO "md: rdev %s, SZ:%08llu F:%d S:%d DN:%u\n",
		bdevname(rdev->bdev,b), (unsigned long long)rdev->size,
	        test_bit(Faulty, &rdev->flags), test_bit(In_sync, &rdev->flags),
	        rdev->desc_nr);
	if (rdev->sb_loaded) {
		printk(KERN_INFO "md: rdev superblock:\n");
		print_sb((mdp_super_t*)page_address(rdev->sb_page));
	} else
		printk(KERN_INFO "md: no rdev superblock!\n");
}

static void md_print_devices(void)
{
	struct list_head *tmp, *tmp2;
	mdk_rdev_t *rdev;
	mddev_t *mddev;
	char b[BDEVNAME_SIZE];

	printk("\n");
	printk("md:	**********************************\n");
	printk("md:	* <COMPLETE RAID STATE PRINTOUT> *\n");
	printk("md:	**********************************\n");
	ITERATE_MDDEV(mddev,tmp) {

		if (mddev->bitmap)
			bitmap_print_sb(mddev->bitmap);
		else
			printk("%s: ", mdname(mddev));
		ITERATE_RDEV(mddev,rdev,tmp2)
			printk("<%s>", bdevname(rdev->bdev,b));
		printk("\n");

		ITERATE_RDEV(mddev,rdev,tmp2)
			print_rdev(rdev);
	}
	printk("md:	**********************************\n");
	printk("\n");
}


static void sync_sbs(mddev_t * mddev, int nospares)
{
	/* Update each superblock (in-memory image), but
	 * if we are allowed to, skip spares which already
	 * have the right event counter, or have one earlier
	 * (which would mean they aren't being marked as dirty
	 * with the rest of the array)
	 */
	mdk_rdev_t *rdev;
	struct list_head *tmp;

	ITERATE_RDEV(mddev,rdev,tmp) {
		if (rdev->sb_events == mddev->events ||
		    (nospares &&
		     rdev->raid_disk < 0 &&
		     (rdev->sb_events&1)==0 &&
		     rdev->sb_events+1 == mddev->events)) {
			/* Don't update this superblock */
			rdev->sb_loaded = 2;
		} else {
			super_types[mddev->major_version].
				sync_super(mddev, rdev);
			rdev->sb_loaded = 1;
		}
	}
}

static void md_update_sb(mddev_t * mddev, int force_change)
{
	int err;
	struct list_head *tmp;
	mdk_rdev_t *rdev;
	int sync_req;
	int nospares = 0;

repeat:
	spin_lock_irq(&mddev->write_lock);

	set_bit(MD_CHANGE_PENDING, &mddev->flags);
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
	mddev->utime = get_seconds();

	/* If this is just a dirty<->clean transition, and the array is clean
	 * and 'events' is odd, we can roll back to the previous clean state */
	if (nospares
	    && (mddev->in_sync && mddev->recovery_cp == MaxSector)
	    && (mddev->events & 1))
		mddev->events--;
	else {
		/* otherwise we have to go forward and ... */
		mddev->events ++;
		if (!mddev->in_sync || mddev->recovery_cp != MaxSector) { /* not clean */
			/* .. if the array isn't clean, insist on an odd 'events' */
			if ((mddev->events&1)==0) {
				mddev->events++;
				nospares = 0;
			}
		} else {
			/* otherwise insist on an even 'events' (for clean states) */
			if ((mddev->events&1)) {
				mddev->events++;
				nospares = 0;
			}
		}
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
	sync_sbs(mddev, nospares);

	/*
	 * do not write anything to disk if using
	 * nonpersistent superblocks
	 */
	if (!mddev->persistent) {
		clear_bit(MD_CHANGE_PENDING, &mddev->flags);
		spin_unlock_irq(&mddev->write_lock);
		wake_up(&mddev->sb_wait);
		return;
	}
	spin_unlock_irq(&mddev->write_lock);

	dprintk(KERN_INFO 
		"md: updating %s RAID superblock on device (in sync %d)\n",
		mdname(mddev),mddev->in_sync);

	err = bitmap_update_sb(mddev->bitmap);
	ITERATE_RDEV(mddev,rdev,tmp) {
		char b[BDEVNAME_SIZE];
		dprintk(KERN_INFO "md: ");
		if (rdev->sb_loaded != 1)
			continue; /* no noise on spare devices */
		if (test_bit(Faulty, &rdev->flags))
			dprintk("(skipping faulty ");

		dprintk("%s ", bdevname(rdev->bdev,b));
		if (!test_bit(Faulty, &rdev->flags)) {
			md_super_write(mddev,rdev,
				       rdev->sb_offset<<1, rdev->sb_size,
				       rdev->sb_page);
			dprintk(KERN_INFO "(write) %s's sb offset: %llu\n",
				bdevname(rdev->bdev,b),
				(unsigned long long)rdev->sb_offset);
			rdev->sb_events = mddev->events;

		} else
			dprintk(")\n");
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

}

/* words written to sysfs files may, or my not, be \n terminated.
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
	ssize_t (*show)(mdk_rdev_t *, char *);
	ssize_t (*store)(mdk_rdev_t *, const char *, size_t);
};

static ssize_t
state_show(mdk_rdev_t *rdev, char *page)
{
	char *sep = "";
	int len=0;

	if (test_bit(Faulty, &rdev->flags)) {
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
	if (!test_bit(Faulty, &rdev->flags) &&
	    !test_bit(In_sync, &rdev->flags)) {
		len += sprintf(page+len, "%sspare", sep);
		sep = ",";
	}
	return len+sprintf(page+len, "\n");
}

static ssize_t
state_store(mdk_rdev_t *rdev, const char *buf, size_t len)
{
	/* can write
	 *  faulty  - simulates and error
	 *  remove  - disconnects the device
	 *  writemostly - sets write_mostly
	 *  -writemostly - clears write_mostly
	 */
	int err = -EINVAL;
	if (cmd_match(buf, "faulty") && rdev->mddev->pers) {
		md_error(rdev->mddev, rdev);
		err = 0;
	} else if (cmd_match(buf, "remove")) {
		if (rdev->raid_disk >= 0)
			err = -EBUSY;
		else {
			mddev_t *mddev = rdev->mddev;
			kick_rdev_from_array(rdev);
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
	}
	return err ? err : len;
}
static struct rdev_sysfs_entry rdev_state =
__ATTR(state, S_IRUGO|S_IWUSR, state_show, state_store);

static ssize_t
super_show(mdk_rdev_t *rdev, char *page)
{
	if (rdev->sb_loaded && rdev->sb_size) {
		memcpy(page, page_address(rdev->sb_page), rdev->sb_size);
		return rdev->sb_size;
	} else
		return 0;
}
static struct rdev_sysfs_entry rdev_super = __ATTR_RO(super);

static ssize_t
errors_show(mdk_rdev_t *rdev, char *page)
{
	return sprintf(page, "%d\n", atomic_read(&rdev->corrected_errors));
}

static ssize_t
errors_store(mdk_rdev_t *rdev, const char *buf, size_t len)
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
slot_show(mdk_rdev_t *rdev, char *page)
{
	if (rdev->raid_disk < 0)
		return sprintf(page, "none\n");
	else
		return sprintf(page, "%d\n", rdev->raid_disk);
}

static ssize_t
slot_store(mdk_rdev_t *rdev, const char *buf, size_t len)
{
	char *e;
	int slot = simple_strtoul(buf, &e, 10);
	if (strncmp(buf, "none", 4)==0)
		slot = -1;
	else if (e==buf || (*e && *e!= '\n'))
		return -EINVAL;
	if (rdev->mddev->pers)
		/* Cannot set slot in active array (yet) */
		return -EBUSY;
	if (slot >= rdev->mddev->raid_disks)
		return -ENOSPC;
	rdev->raid_disk = slot;
	/* assume it is working */
	rdev->flags = 0;
	set_bit(In_sync, &rdev->flags);
	return len;
}


static struct rdev_sysfs_entry rdev_slot =
__ATTR(slot, S_IRUGO|S_IWUSR, slot_show, slot_store);

static ssize_t
offset_show(mdk_rdev_t *rdev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)rdev->data_offset);
}

static ssize_t
offset_store(mdk_rdev_t *rdev, const char *buf, size_t len)
{
	char *e;
	unsigned long long offset = simple_strtoull(buf, &e, 10);
	if (e==buf || (*e && *e != '\n'))
		return -EINVAL;
	if (rdev->mddev->pers)
		return -EBUSY;
	rdev->data_offset = offset;
	return len;
}

static struct rdev_sysfs_entry rdev_offset =
__ATTR(offset, S_IRUGO|S_IWUSR, offset_show, offset_store);

static ssize_t
rdev_size_show(mdk_rdev_t *rdev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)rdev->size);
}

static ssize_t
rdev_size_store(mdk_rdev_t *rdev, const char *buf, size_t len)
{
	char *e;
	unsigned long long size = simple_strtoull(buf, &e, 10);
	if (e==buf || (*e && *e != '\n'))
		return -EINVAL;
	if (rdev->mddev->pers)
		return -EBUSY;
	rdev->size = size;
	if (size < rdev->mddev->size || rdev->mddev->size == 0)
		rdev->mddev->size = size;
	return len;
}

static struct rdev_sysfs_entry rdev_size =
__ATTR(size, S_IRUGO|S_IWUSR, rdev_size_show, rdev_size_store);

static struct attribute *rdev_default_attrs[] = {
	&rdev_state.attr,
	&rdev_super.attr,
	&rdev_errors.attr,
	&rdev_slot.attr,
	&rdev_offset.attr,
	&rdev_size.attr,
	NULL,
};
static ssize_t
rdev_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct rdev_sysfs_entry *entry = container_of(attr, struct rdev_sysfs_entry, attr);
	mdk_rdev_t *rdev = container_of(kobj, mdk_rdev_t, kobj);

	if (!entry->show)
		return -EIO;
	return entry->show(rdev, page);
}

static ssize_t
rdev_attr_store(struct kobject *kobj, struct attribute *attr,
	      const char *page, size_t length)
{
	struct rdev_sysfs_entry *entry = container_of(attr, struct rdev_sysfs_entry, attr);
	mdk_rdev_t *rdev = container_of(kobj, mdk_rdev_t, kobj);

	if (!entry->store)
		return -EIO;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	return entry->store(rdev, page, length);
}

static void rdev_free(struct kobject *ko)
{
	mdk_rdev_t *rdev = container_of(ko, mdk_rdev_t, kobj);
	kfree(rdev);
}
static struct sysfs_ops rdev_sysfs_ops = {
	.show		= rdev_attr_show,
	.store		= rdev_attr_store,
};
static struct kobj_type rdev_ktype = {
	.release	= rdev_free,
	.sysfs_ops	= &rdev_sysfs_ops,
	.default_attrs	= rdev_default_attrs,
};

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
static mdk_rdev_t *md_import_device(dev_t newdev, int super_format, int super_minor)
{
	char b[BDEVNAME_SIZE];
	int err;
	mdk_rdev_t *rdev;
	sector_t size;

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev) {
		printk(KERN_ERR "md: could not alloc mem for new device!\n");
		return ERR_PTR(-ENOMEM);
	}

	if ((err = alloc_disk_sb(rdev)))
		goto abort_free;

	err = lock_rdev(rdev, newdev);
	if (err)
		goto abort_free;

	rdev->kobj.parent = NULL;
	rdev->kobj.ktype = &rdev_ktype;
	kobject_init(&rdev->kobj);

	rdev->desc_nr = -1;
	rdev->saved_raid_disk = -1;
	rdev->flags = 0;
	rdev->data_offset = 0;
	rdev->sb_events = 0;
	atomic_set(&rdev->nr_pending, 0);
	atomic_set(&rdev->read_errors, 0);
	atomic_set(&rdev->corrected_errors, 0);

	size = rdev->bdev->bd_inode->i_size >> BLOCK_SIZE_BITS;
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
				"md: %s has invalid sb, not importing!\n",
				bdevname(rdev->bdev,b));
			goto abort_free;
		}
		if (err < 0) {
			printk(KERN_WARNING 
				"md: could not read %s's sb, not importing!\n",
				bdevname(rdev->bdev,b));
			goto abort_free;
		}
	}
	INIT_LIST_HEAD(&rdev->same_set);

	return rdev;

abort_free:
	if (rdev->sb_page) {
		if (rdev->bdev)
			unlock_rdev(rdev);
		free_disk_sb(rdev);
	}
	kfree(rdev);
	return ERR_PTR(err);
}

/*
 * Check a full RAID array for plausibility
 */


static void analyze_sbs(mddev_t * mddev)
{
	int i;
	struct list_head *tmp;
	mdk_rdev_t *rdev, *freshest;
	char b[BDEVNAME_SIZE];

	freshest = NULL;
	ITERATE_RDEV(mddev,rdev,tmp)
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
	ITERATE_RDEV(mddev,rdev,tmp) {
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
		}
	}



	if (mddev->recovery_cp != MaxSector &&
	    mddev->level >= 1)
		printk(KERN_ERR "md: %s: raid array is not clean"
		       " -- starting background reconstruction\n",
		       mdname(mddev));

}

static ssize_t
safe_delay_show(mddev_t *mddev, char *page)
{
	int msec = (mddev->safemode_delay*1000)/HZ;
	return sprintf(page, "%d.%03d\n", msec/1000, msec%1000);
}
static ssize_t
safe_delay_store(mddev_t *mddev, const char *cbuf, size_t len)
{
	int scale=1;
	int dot=0;
	int i;
	unsigned long msec;
	char buf[30];
	char *e;
	/* remove a period, and count digits after it */
	if (len >= sizeof(buf))
		return -EINVAL;
	strlcpy(buf, cbuf, len);
	buf[len] = 0;
	for (i=0; i<len; i++) {
		if (dot) {
			if (isdigit(buf[i])) {
				buf[i-1] = buf[i];
				scale *= 10;
			}
			buf[i] = 0;
		} else if (buf[i] == '.') {
			dot=1;
			buf[i] = 0;
		}
	}
	msec = simple_strtoul(buf, &e, 10);
	if (e == buf || (*e && *e != '\n'))
		return -EINVAL;
	msec = (msec * 1000) / scale;
	if (msec == 0)
		mddev->safemode_delay = 0;
	else {
		mddev->safemode_delay = (msec*HZ)/1000;
		if (mddev->safemode_delay == 0)
			mddev->safemode_delay = 1;
	}
	return len;
}
static struct md_sysfs_entry md_safe_delay =
__ATTR(safe_mode_delay, S_IRUGO|S_IWUSR,safe_delay_show, safe_delay_store);

static ssize_t
level_show(mddev_t *mddev, char *page)
{
	struct mdk_personality *p = mddev->pers;
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
level_store(mddev_t *mddev, const char *buf, size_t len)
{
	int rv = len;
	if (mddev->pers)
		return -EBUSY;
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

static struct md_sysfs_entry md_level =
__ATTR(level, S_IRUGO|S_IWUSR, level_show, level_store);


static ssize_t
layout_show(mddev_t *mddev, char *page)
{
	/* just a number, not meaningful for all levels */
	return sprintf(page, "%d\n", mddev->layout);
}

static ssize_t
layout_store(mddev_t *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long n = simple_strtoul(buf, &e, 10);
	if (mddev->pers)
		return -EBUSY;

	if (!*buf || (*e && *e != '\n'))
		return -EINVAL;

	mddev->layout = n;
	return len;
}
static struct md_sysfs_entry md_layout =
__ATTR(layout, S_IRUGO|S_IWUSR, layout_show, layout_store);


static ssize_t
raid_disks_show(mddev_t *mddev, char *page)
{
	if (mddev->raid_disks == 0)
		return 0;
	return sprintf(page, "%d\n", mddev->raid_disks);
}

static int update_raid_disks(mddev_t *mddev, int raid_disks);

static ssize_t
raid_disks_store(mddev_t *mddev, const char *buf, size_t len)
{
	/* can only set raid_disks if array is not yet active */
	char *e;
	int rv = 0;
	unsigned long n = simple_strtoul(buf, &e, 10);

	if (!*buf || (*e && *e != '\n'))
		return -EINVAL;

	if (mddev->pers)
		rv = update_raid_disks(mddev, n);
	else
		mddev->raid_disks = n;
	return rv ? rv : len;
}
static struct md_sysfs_entry md_raid_disks =
__ATTR(raid_disks, S_IRUGO|S_IWUSR, raid_disks_show, raid_disks_store);

static ssize_t
chunk_size_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%d\n", mddev->chunk_size);
}

static ssize_t
chunk_size_store(mddev_t *mddev, const char *buf, size_t len)
{
	/* can only set chunk_size if array is not yet active */
	char *e;
	unsigned long n = simple_strtoul(buf, &e, 10);

	if (mddev->pers)
		return -EBUSY;
	if (!*buf || (*e && *e != '\n'))
		return -EINVAL;

	mddev->chunk_size = n;
	return len;
}
static struct md_sysfs_entry md_chunk_size =
__ATTR(chunk_size, S_IRUGO|S_IWUSR, chunk_size_show, chunk_size_store);

static ssize_t
resync_start_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)mddev->recovery_cp);
}

static ssize_t
resync_start_store(mddev_t *mddev, const char *buf, size_t len)
{
	/* can only set chunk_size if array is not yet active */
	char *e;
	unsigned long long n = simple_strtoull(buf, &e, 10);

	if (mddev->pers)
		return -EBUSY;
	if (!*buf || (*e && *e != '\n'))
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
 *     Writing this, if accepted, will block until array is quiessent
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
array_state_show(mddev_t *mddev, char *page)
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
			else if (mddev->safemode)
				st = active_idle;
			else
				st = active;
		}
	else {
		if (list_empty(&mddev->disks) &&
		    mddev->raid_disks == 0 &&
		    mddev->size == 0)
			st = clear;
		else
			st = inactive;
	}
	return sprintf(page, "%s\n", array_states[st]);
}

static int do_md_stop(mddev_t * mddev, int ro);
static int do_md_run(mddev_t * mddev);
static int restart_array(mddev_t *mddev);

static ssize_t
array_state_store(mddev_t *mddev, const char *buf, size_t len)
{
	int err = -EINVAL;
	enum array_state st = match_word(buf, array_states);
	switch(st) {
	case bad_word:
		break;
	case clear:
		/* stopping an active array */
		if (mddev->pers) {
			if (atomic_read(&mddev->active) > 1)
				return -EBUSY;
			err = do_md_stop(mddev, 0);
		}
		break;
	case inactive:
		/* stopping an active array */
		if (mddev->pers) {
			if (atomic_read(&mddev->active) > 1)
				return -EBUSY;
			err = do_md_stop(mddev, 2);
		}
		break;
	case suspended:
		break; /* not supported yet */
	case readonly:
		if (mddev->pers)
			err = do_md_stop(mddev, 1);
		else {
			mddev->ro = 1;
			err = do_md_run(mddev);
		}
		break;
	case read_auto:
		/* stopping an active array */
		if (mddev->pers) {
			err = do_md_stop(mddev, 1);
			if (err == 0)
				mddev->ro = 2; /* FIXME mark devices writable */
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
				mddev->in_sync = 1;
				set_bit(MD_CHANGE_CLEAN, &mddev->flags);
			}
			spin_unlock_irq(&mddev->write_lock);
		} else {
			mddev->ro = 0;
			mddev->recovery_cp = MaxSector;
			err = do_md_run(mddev);
		}
		break;
	case active:
		if (mddev->pers) {
			restart_array(mddev);
			clear_bit(MD_CHANGE_CLEAN, &mddev->flags);
			wake_up(&mddev->sb_wait);
			err = 0;
		} else {
			mddev->ro = 0;
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
	else
		return len;
}
static struct md_sysfs_entry md_array_state =
__ATTR(array_state, S_IRUGO|S_IWUSR, array_state_show, array_state_store);

static ssize_t
null_show(mddev_t *mddev, char *page)
{
	return -EINVAL;
}

static ssize_t
new_dev_store(mddev_t *mddev, const char *buf, size_t len)
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
	mdk_rdev_t *rdev;
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
			mdk_rdev_t *rdev0 = list_entry(mddev->disks.next,
						       mdk_rdev_t, same_set);
			err = super_types[mddev->major_version]
				.load_super(rdev, rdev0, mddev->minor_version);
			if (err < 0)
				goto out;
		}
	} else
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
bitmap_store(mddev_t *mddev, const char *buf, size_t len)
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
		buf = end;
		while (isspace(*buf)) buf++;
	}
	bitmap_unplug(mddev->bitmap); /* flush the bits to disk */
out:
	return len;
}

static struct md_sysfs_entry md_bitmap =
__ATTR(bitmap_set_bits, S_IWUSR, null_show, bitmap_store);

static ssize_t
size_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)mddev->size);
}

static int update_size(mddev_t *mddev, unsigned long size);

static ssize_t
size_store(mddev_t *mddev, const char *buf, size_t len)
{
	/* If array is inactive, we can reduce the component size, but
	 * not increase it (except from 0).
	 * If array is active, we can try an on-line resize
	 */
	char *e;
	int err = 0;
	unsigned long long size = simple_strtoull(buf, &e, 10);
	if (!*buf || *buf == '\n' ||
	    (*e && *e != '\n'))
		return -EINVAL;

	if (mddev->pers) {
		err = update_size(mddev, size);
		md_update_sb(mddev, 1);
	} else {
		if (mddev->size == 0 ||
		    mddev->size > size)
			mddev->size = size;
		else
			err = -ENOSPC;
	}
	return err ? err : len;
}

static struct md_sysfs_entry md_size =
__ATTR(component_size, S_IRUGO|S_IWUSR, size_show, size_store);


/* Metdata version.
 * This is either 'none' for arrays with externally managed metadata,
 * or N.M for internally known formats
 */
static ssize_t
metadata_show(mddev_t *mddev, char *page)
{
	if (mddev->persistent)
		return sprintf(page, "%d.%d\n",
			       mddev->major_version, mddev->minor_version);
	else
		return sprintf(page, "none\n");
}

static ssize_t
metadata_store(mddev_t *mddev, const char *buf, size_t len)
{
	int major, minor;
	char *e;
	if (!list_empty(&mddev->disks))
		return -EBUSY;

	if (cmd_match(buf, "none")) {
		mddev->persistent = 0;
		mddev->major_version = 0;
		mddev->minor_version = 90;
		return len;
	}
	major = simple_strtoul(buf, &e, 10);
	if (e==buf || *e != '.')
		return -EINVAL;
	buf = e+1;
	minor = simple_strtoul(buf, &e, 10);
	if (e==buf || *e != '\n')
		return -EINVAL;
	if (major >= sizeof(super_types)/sizeof(super_types[0]) ||
	    super_types[major].name == NULL)
		return -ENOENT;
	mddev->major_version = major;
	mddev->minor_version = minor;
	mddev->persistent = 1;
	return len;
}

static struct md_sysfs_entry md_metadata =
__ATTR(metadata_version, S_IRUGO|S_IWUSR, metadata_show, metadata_store);

static ssize_t
action_show(mddev_t *mddev, char *page)
{
	char *type = "idle";
	if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery) ||
	    test_bit(MD_RECOVERY_NEEDED, &mddev->recovery)) {
		if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
			type = "reshape";
		else if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
			if (!test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
				type = "resync";
			else if (test_bit(MD_RECOVERY_CHECK, &mddev->recovery))
				type = "check";
			else
				type = "repair";
		} else
			type = "recover";
	}
	return sprintf(page, "%s\n", type);
}

static ssize_t
action_store(mddev_t *mddev, const char *page, size_t len)
{
	if (!mddev->pers || !mddev->pers->sync_request)
		return -EINVAL;

	if (cmd_match(page, "idle")) {
		if (mddev->sync_thread) {
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			md_unregister_thread(mddev->sync_thread);
			mddev->sync_thread = NULL;
			mddev->recovery = 0;
		}
	} else if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery) ||
		   test_bit(MD_RECOVERY_NEEDED, &mddev->recovery))
		return -EBUSY;
	else if (cmd_match(page, "resync") || cmd_match(page, "recover"))
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	else if (cmd_match(page, "reshape")) {
		int err;
		if (mddev->pers->start_reshape == NULL)
			return -EINVAL;
		err = mddev->pers->start_reshape(mddev);
		if (err)
			return err;
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
	return len;
}

static ssize_t
mismatch_cnt_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%llu\n",
		       (unsigned long long) mddev->resync_mismatches);
}

static struct md_sysfs_entry md_scan_mode =
__ATTR(sync_action, S_IRUGO|S_IWUSR, action_show, action_store);


static struct md_sysfs_entry md_mismatches = __ATTR_RO(mismatch_cnt);

static ssize_t
sync_min_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%d (%s)\n", speed_min(mddev),
		       mddev->sync_speed_min ? "local": "system");
}

static ssize_t
sync_min_store(mddev_t *mddev, const char *buf, size_t len)
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
sync_max_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%d (%s)\n", speed_max(mddev),
		       mddev->sync_speed_max ? "local": "system");
}

static ssize_t
sync_max_store(mddev_t *mddev, const char *buf, size_t len)
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
sync_speed_show(mddev_t *mddev, char *page)
{
	unsigned long resync, dt, db;
	resync = (mddev->curr_mark_cnt - atomic_read(&mddev->recovery_active));
	dt = ((jiffies - mddev->resync_mark) / HZ);
	if (!dt) dt++;
	db = resync - (mddev->resync_mark_cnt);
	return sprintf(page, "%ld\n", db/dt/2); /* K/sec */
}

static struct md_sysfs_entry md_sync_speed = __ATTR_RO(sync_speed);

static ssize_t
sync_completed_show(mddev_t *mddev, char *page)
{
	unsigned long max_blocks, resync;

	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery))
		max_blocks = mddev->resync_max_sectors;
	else
		max_blocks = mddev->size << 1;

	resync = (mddev->curr_resync - atomic_read(&mddev->recovery_active));
	return sprintf(page, "%lu / %lu\n", resync, max_blocks);
}

static struct md_sysfs_entry md_sync_completed = __ATTR_RO(sync_completed);

static ssize_t
suspend_lo_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)mddev->suspend_lo);
}

static ssize_t
suspend_lo_store(mddev_t *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long long new = simple_strtoull(buf, &e, 10);

	if (mddev->pers->quiesce == NULL)
		return -EINVAL;
	if (buf == e || (*e && *e != '\n'))
		return -EINVAL;
	if (new >= mddev->suspend_hi ||
	    (new > mddev->suspend_lo && new < mddev->suspend_hi)) {
		mddev->suspend_lo = new;
		mddev->pers->quiesce(mddev, 2);
		return len;
	} else
		return -EINVAL;
}
static struct md_sysfs_entry md_suspend_lo =
__ATTR(suspend_lo, S_IRUGO|S_IWUSR, suspend_lo_show, suspend_lo_store);


static ssize_t
suspend_hi_show(mddev_t *mddev, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)mddev->suspend_hi);
}

static ssize_t
suspend_hi_store(mddev_t *mddev, const char *buf, size_t len)
{
	char *e;
	unsigned long long new = simple_strtoull(buf, &e, 10);

	if (mddev->pers->quiesce == NULL)
		return -EINVAL;
	if (buf == e || (*e && *e != '\n'))
		return -EINVAL;
	if ((new <= mddev->suspend_lo && mddev->suspend_lo >= mddev->suspend_hi) ||
	    (new > mddev->suspend_lo && new > mddev->suspend_hi)) {
		mddev->suspend_hi = new;
		mddev->pers->quiesce(mddev, 1);
		mddev->pers->quiesce(mddev, 0);
		return len;
	} else
		return -EINVAL;
}
static struct md_sysfs_entry md_suspend_hi =
__ATTR(suspend_hi, S_IRUGO|S_IWUSR, suspend_hi_show, suspend_hi_store);


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
	NULL,
};

static struct attribute *md_redundancy_attrs[] = {
	&md_scan_mode.attr,
	&md_mismatches.attr,
	&md_sync_min.attr,
	&md_sync_max.attr,
	&md_sync_speed.attr,
	&md_sync_completed.attr,
	&md_suspend_lo.attr,
	&md_suspend_hi.attr,
	&md_bitmap.attr,
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
	mddev_t *mddev = container_of(kobj, struct mddev_s, kobj);
	ssize_t rv;

	if (!entry->show)
		return -EIO;
	rv = mddev_lock(mddev);
	if (!rv) {
		rv = entry->show(mddev, page);
		mddev_unlock(mddev);
	}
	return rv;
}

static ssize_t
md_attr_store(struct kobject *kobj, struct attribute *attr,
	      const char *page, size_t length)
{
	struct md_sysfs_entry *entry = container_of(attr, struct md_sysfs_entry, attr);
	mddev_t *mddev = container_of(kobj, struct mddev_s, kobj);
	ssize_t rv;

	if (!entry->store)
		return -EIO;
	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	rv = mddev_lock(mddev);
	if (!rv) {
		rv = entry->store(mddev, page, length);
		mddev_unlock(mddev);
	}
	return rv;
}

static void md_free(struct kobject *ko)
{
	mddev_t *mddev = container_of(ko, mddev_t, kobj);
	kfree(mddev);
}

static struct sysfs_ops md_sysfs_ops = {
	.show	= md_attr_show,
	.store	= md_attr_store,
};
static struct kobj_type md_ktype = {
	.release	= md_free,
	.sysfs_ops	= &md_sysfs_ops,
	.default_attrs	= md_default_attrs,
};

int mdp_major = 0;

static struct kobject *md_probe(dev_t dev, int *part, void *data)
{
	static DEFINE_MUTEX(disks_mutex);
	mddev_t *mddev = mddev_find(dev);
	struct gendisk *disk;
	int partitioned = (MAJOR(dev) != MD_MAJOR);
	int shift = partitioned ? MdpMinorShift : 0;
	int unit = MINOR(dev) >> shift;

	if (!mddev)
		return NULL;

	mutex_lock(&disks_mutex);
	if (mddev->gendisk) {
		mutex_unlock(&disks_mutex);
		mddev_put(mddev);
		return NULL;
	}
	disk = alloc_disk(1 << shift);
	if (!disk) {
		mutex_unlock(&disks_mutex);
		mddev_put(mddev);
		return NULL;
	}
	disk->major = MAJOR(dev);
	disk->first_minor = unit << shift;
	if (partitioned)
		sprintf(disk->disk_name, "md_d%d", unit);
	else
		sprintf(disk->disk_name, "md%d", unit);
	disk->fops = &md_fops;
	disk->private_data = mddev;
	disk->queue = mddev->queue;
	add_disk(disk);
	mddev->gendisk = disk;
	mutex_unlock(&disks_mutex);
	mddev->kobj.parent = &disk->kobj;
	mddev->kobj.k_name = NULL;
	snprintf(mddev->kobj.name, KOBJ_NAME_LEN, "%s", "md");
	mddev->kobj.ktype = &md_ktype;
	kobject_register(&mddev->kobj);
	return NULL;
}

static void md_safemode_timeout(unsigned long data)
{
	mddev_t *mddev = (mddev_t *) data;

	mddev->safemode = 1;
	md_wakeup_thread(mddev->thread);
}

static int start_dirty_degraded;

static int do_md_run(mddev_t * mddev)
{
	int err;
	int chunk_size;
	struct list_head *tmp;
	mdk_rdev_t *rdev;
	struct gendisk *disk;
	struct mdk_personality *pers;
	char b[BDEVNAME_SIZE];

	if (list_empty(&mddev->disks))
		/* cannot run an array with no devices.. */
		return -EINVAL;

	if (mddev->pers)
		return -EBUSY;

	/*
	 * Analyze all RAID superblock(s)
	 */
	if (!mddev->raid_disks)
		analyze_sbs(mddev);

	chunk_size = mddev->chunk_size;

	if (chunk_size) {
		if (chunk_size > MAX_CHUNK_SIZE) {
			printk(KERN_ERR "too big chunk_size: %d > %d\n",
				chunk_size, MAX_CHUNK_SIZE);
			return -EINVAL;
		}
		/*
		 * chunk-size has to be a power of 2 and multiples of PAGE_SIZE
		 */
		if ( (1 << ffz(~chunk_size)) != chunk_size) {
			printk(KERN_ERR "chunk_size of %d not valid\n", chunk_size);
			return -EINVAL;
		}
		if (chunk_size < PAGE_SIZE) {
			printk(KERN_ERR "too small chunk_size: %d < %ld\n",
				chunk_size, PAGE_SIZE);
			return -EINVAL;
		}

		/* devices must have minimum size of one chunk */
		ITERATE_RDEV(mddev,rdev,tmp) {
			if (test_bit(Faulty, &rdev->flags))
				continue;
			if (rdev->size < chunk_size / 1024) {
				printk(KERN_WARNING
					"md: Dev %s smaller than chunk_size:"
					" %lluk < %dk\n",
					bdevname(rdev->bdev,b),
					(unsigned long long)rdev->size,
					chunk_size / 1024);
				return -EINVAL;
			}
		}
	}

#ifdef CONFIG_KMOD
	if (mddev->level != LEVEL_NONE)
		request_module("md-level-%d", mddev->level);
	else if (mddev->clevel[0])
		request_module("md-%s", mddev->clevel);
#endif

	/*
	 * Drop all container device buffers, from now on
	 * the only valid external interface is through the md
	 * device.
	 * Also find largest hardsector size
	 */
	ITERATE_RDEV(mddev,rdev,tmp) {
		if (test_bit(Faulty, &rdev->flags))
			continue;
		sync_blockdev(rdev->bdev);
		invalidate_bdev(rdev->bdev, 0);
	}

	md_probe(mddev->unit, NULL, NULL);
	disk = mddev->gendisk;
	if (!disk)
		return -ENOMEM;

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
	mddev->level = pers->level;
	strlcpy(mddev->clevel, pers->name, sizeof(mddev->clevel));

	if (mddev->reshape_position != MaxSector &&
	    pers->start_reshape == NULL) {
		/* This personality cannot handle reshaping... */
		mddev->pers = NULL;
		module_put(pers->owner);
		return -EINVAL;
	}

	mddev->recovery = 0;
	mddev->resync_max_sectors = mddev->size << 1; /* may be over-ridden by personality */
	mddev->barriers_work = 1;
	mddev->ok_start_degraded = start_dirty_degraded;

	if (start_readonly)
		mddev->ro = 2; /* read-only, but switch on first write */

	err = mddev->pers->run(mddev);
	if (!err && mddev->pers->sync_request) {
		err = bitmap_create(mddev);
		if (err) {
			printk(KERN_ERR "%s: failed to create bitmap (%d)\n",
			       mdname(mddev), err);
			mddev->pers->stop(mddev);
		}
	}
	if (err) {
		printk(KERN_ERR "md: pers->run() failed ...\n");
		module_put(mddev->pers->owner);
		mddev->pers = NULL;
		bitmap_destroy(mddev);
		return err;
	}
	if (mddev->pers->sync_request)
		sysfs_create_group(&mddev->kobj, &md_redundancy_group);
	else if (mddev->ro == 2) /* auto-readonly not meaningful */
		mddev->ro = 0;

 	atomic_set(&mddev->writes_pending,0);
	mddev->safemode = 0;
	mddev->safemode_timer.function = md_safemode_timeout;
	mddev->safemode_timer.data = (unsigned long) mddev;
	mddev->safemode_delay = (200 * HZ)/1000 +1; /* 200 msec delay */
	mddev->in_sync = 1;

	ITERATE_RDEV(mddev,rdev,tmp)
		if (rdev->raid_disk >= 0) {
			char nm[20];
			sprintf(nm, "rd%d", rdev->raid_disk);
			sysfs_create_link(&mddev->kobj, &rdev->kobj, nm);
		}
	
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	
	if (mddev->flags)
		md_update_sb(mddev, 0);

	set_capacity(disk, mddev->array_size<<1);

	/* If we call blk_queue_make_request here, it will
	 * re-initialise max_sectors etc which may have been
	 * refined inside -> run.  So just set the bits we need to set.
	 * Most initialisation happended when we called
	 * blk_queue_make_request(..., md_fail_request)
	 * earlier.
	 */
	mddev->queue->queuedata = mddev;
	mddev->queue->make_request_fn = mddev->pers->make_request;

	/* If there is a partially-recovered drive we need to
	 * start recovery here.  If we leave it to md_check_recovery,
	 * it will remove the drives and not do the right thing
	 */
	if (mddev->degraded && !mddev->sync_thread) {
		struct list_head *rtmp;
		int spares = 0;
		ITERATE_RDEV(mddev,rdev,rtmp)
			if (rdev->raid_disk >= 0 &&
			    !test_bit(In_sync, &rdev->flags) &&
			    !test_bit(Faulty, &rdev->flags))
				/* complete an interrupted recovery */
				spares++;
		if (spares && mddev->pers->sync_request) {
			mddev->recovery = 0;
			set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
			mddev->sync_thread = md_register_thread(md_do_sync,
								mddev,
								"%s_resync");
			if (!mddev->sync_thread) {
				printk(KERN_ERR "%s: could not start resync"
				       " thread...\n",
				       mdname(mddev));
				/* leave the spares where they are, it shouldn't hurt */
				mddev->recovery = 0;
			}
		}
	}
	md_wakeup_thread(mddev->thread);
	md_wakeup_thread(mddev->sync_thread); /* possibly kick off a reshape */

	mddev->changed = 1;
	md_new_event(mddev);
	kobject_uevent(&mddev->gendisk->kobj, KOBJ_CHANGE);
	return 0;
}

static int restart_array(mddev_t *mddev)
{
	struct gendisk *disk = mddev->gendisk;
	int err;

	/*
	 * Complain if it has no devices
	 */
	err = -ENXIO;
	if (list_empty(&mddev->disks))
		goto out;

	if (mddev->pers) {
		err = -EBUSY;
		if (!mddev->ro)
			goto out;

		mddev->safemode = 0;
		mddev->ro = 0;
		set_disk_ro(disk, 0);

		printk(KERN_INFO "md: %s switched to read-write mode.\n",
			mdname(mddev));
		/*
		 * Kick recovery or resync if necessary
		 */
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
		md_wakeup_thread(mddev->sync_thread);
		err = 0;
	} else
		err = -EINVAL;

out:
	return err;
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

static void restore_bitmap_write_access(struct file *file)
{
	struct inode *inode = file->f_mapping->host;

	spin_lock(&inode->i_lock);
	atomic_set(&inode->i_writecount, 1);
	spin_unlock(&inode->i_lock);
}

/* mode:
 *   0 - completely stop and dis-assemble array
 *   1 - switch to readonly
 *   2 - stop but do not disassemble array
 */
static int do_md_stop(mddev_t * mddev, int mode)
{
	int err = 0;
	struct gendisk *disk = mddev->gendisk;

	if (mddev->pers) {
		if (atomic_read(&mddev->active)>2) {
			printk("md: %s still in use.\n",mdname(mddev));
			return -EBUSY;
		}

		if (mddev->sync_thread) {
			set_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			md_unregister_thread(mddev->sync_thread);
			mddev->sync_thread = NULL;
		}

		del_timer_sync(&mddev->safemode_timer);

		invalidate_partition(disk, 0);

		switch(mode) {
		case 1: /* readonly */
			err  = -ENXIO;
			if (mddev->ro==1)
				goto out;
			mddev->ro = 1;
			break;
		case 0: /* disassemble */
		case 2: /* stop */
			bitmap_flush(mddev);
			md_super_wait(mddev);
			if (mddev->ro)
				set_disk_ro(disk, 0);
			blk_queue_make_request(mddev->queue, md_fail_request);
			mddev->pers->stop(mddev);
			if (mddev->pers->sync_request)
				sysfs_remove_group(&mddev->kobj, &md_redundancy_group);

			module_put(mddev->pers->owner);
			mddev->pers = NULL;
			if (mddev->ro)
				mddev->ro = 0;
		}
		if (!mddev->in_sync || mddev->flags) {
			/* mark array as shutdown cleanly */
			mddev->in_sync = 1;
			md_update_sb(mddev, 1);
		}
		if (mode == 1)
			set_disk_ro(disk, 1);
		clear_bit(MD_RECOVERY_FROZEN, &mddev->recovery);
	}

	/*
	 * Free resources if final stop
	 */
	if (mode == 0) {
		mdk_rdev_t *rdev;
		struct list_head *tmp;
		struct gendisk *disk;
		printk(KERN_INFO "md: %s stopped.\n", mdname(mddev));

		bitmap_destroy(mddev);
		if (mddev->bitmap_file) {
			restore_bitmap_write_access(mddev->bitmap_file);
			fput(mddev->bitmap_file);
			mddev->bitmap_file = NULL;
		}
		mddev->bitmap_offset = 0;

		ITERATE_RDEV(mddev,rdev,tmp)
			if (rdev->raid_disk >= 0) {
				char nm[20];
				sprintf(nm, "rd%d", rdev->raid_disk);
				sysfs_remove_link(&mddev->kobj, nm);
			}

		export_array(mddev);

		mddev->array_size = 0;
		mddev->size = 0;
		mddev->raid_disks = 0;
		mddev->recovery_cp = 0;

		disk = mddev->gendisk;
		if (disk)
			set_capacity(disk, 0);
		mddev->changed = 1;
	} else if (mddev->pers)
		printk(KERN_INFO "md: %s switched to read-only mode.\n",
			mdname(mddev));
	err = 0;
	md_new_event(mddev);
out:
	return err;
}

static void autorun_array(mddev_t *mddev)
{
	mdk_rdev_t *rdev;
	struct list_head *tmp;
	int err;

	if (list_empty(&mddev->disks))
		return;

	printk(KERN_INFO "md: running: ");

	ITERATE_RDEV(mddev,rdev,tmp) {
		char b[BDEVNAME_SIZE];
		printk("<%s>", bdevname(rdev->bdev,b));
	}
	printk("\n");

	err = do_md_run (mddev);
	if (err) {
		printk(KERN_WARNING "md: do_md_run() returned %d\n", err);
		do_md_stop (mddev, 0);
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
	struct list_head *tmp;
	mdk_rdev_t *rdev0, *rdev;
	mddev_t *mddev;
	char b[BDEVNAME_SIZE];

	printk(KERN_INFO "md: autorun ...\n");
	while (!list_empty(&pending_raid_disks)) {
		int unit;
		dev_t dev;
		LIST_HEAD(candidates);
		rdev0 = list_entry(pending_raid_disks.next,
					 mdk_rdev_t, same_set);

		printk(KERN_INFO "md: considering %s ...\n",
			bdevname(rdev0->bdev,b));
		INIT_LIST_HEAD(&candidates);
		ITERATE_RDEV_PENDING(rdev,tmp)
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
		if (!mddev) {
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
			ITERATE_RDEV_GENERIC(candidates,rdev,tmp) {
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
		ITERATE_RDEV_GENERIC(candidates,rdev,tmp)
			export_rdev(rdev);
		mddev_put(mddev);
	}
	printk(KERN_INFO "md: ... autorun DONE.\n");
}

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

static int get_array_info(mddev_t * mddev, void __user * arg)
{
	mdu_array_info_t info;
	int nr,working,active,failed,spare;
	mdk_rdev_t *rdev;
	struct list_head *tmp;

	nr=working=active=failed=spare=0;
	ITERATE_RDEV(mddev,rdev,tmp) {
		nr++;
		if (test_bit(Faulty, &rdev->flags))
			failed++;
		else {
			working++;
			if (test_bit(In_sync, &rdev->flags))
				active++;	
			else
				spare++;
		}
	}

	info.major_version = mddev->major_version;
	info.minor_version = mddev->minor_version;
	info.patch_version = MD_PATCHLEVEL_VERSION;
	info.ctime         = mddev->ctime;
	info.level         = mddev->level;
	info.size          = mddev->size;
	if (info.size != mddev->size) /* overflow */
		info.size = -1;
	info.nr_disks      = nr;
	info.raid_disks    = mddev->raid_disks;
	info.md_minor      = mddev->md_minor;
	info.not_persistent= !mddev->persistent;

	info.utime         = mddev->utime;
	info.state         = 0;
	if (mddev->in_sync)
		info.state = (1<<MD_SB_CLEAN);
	if (mddev->bitmap && mddev->bitmap_offset)
		info.state = (1<<MD_SB_BITMAP_PRESENT);
	info.active_disks  = active;
	info.working_disks = working;
	info.failed_disks  = failed;
	info.spare_disks   = spare;

	info.layout        = mddev->layout;
	info.chunk_size    = mddev->chunk_size;

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int get_bitmap_file(mddev_t * mddev, void __user * arg)
{
	mdu_bitmap_file_t *file = NULL; /* too big for stack allocation */
	char *ptr, *buf = NULL;
	int err = -ENOMEM;

	file = kmalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		goto out;

	/* bitmap disabled, zero the first byte and copy out */
	if (!mddev->bitmap || !mddev->bitmap->file) {
		file->pathname[0] = '\0';
		goto copy_out;
	}

	buf = kmalloc(sizeof(file->pathname), GFP_KERNEL);
	if (!buf)
		goto out;

	ptr = file_path(mddev->bitmap->file, buf, sizeof(file->pathname));
	if (!ptr)
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

static int get_disk_info(mddev_t * mddev, void __user * arg)
{
	mdu_disk_info_t info;
	unsigned int nr;
	mdk_rdev_t *rdev;

	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;

	nr = info.number;

	rdev = find_rdev_nr(mddev, nr);
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

static int add_new_disk(mddev_t * mddev, mdu_disk_info_t *info)
{
	char b[BDEVNAME_SIZE], b2[BDEVNAME_SIZE];
	mdk_rdev_t *rdev;
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
			mdk_rdev_t *rdev0 = list_entry(mddev->disks.next,
							mdk_rdev_t, same_set);
			int err = super_types[mddev->major_version]
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
		/* set save_raid_disk if appropriate */
		if (!mddev->persistent) {
			if (info->state & (1<<MD_DISK_SYNC)  &&
			    info->raid_disk < mddev->raid_disks)
				rdev->raid_disk = info->raid_disk;
			else
				rdev->raid_disk = -1;
		} else
			super_types[mddev->major_version].
				validate_super(mddev, rdev);
		rdev->saved_raid_disk = rdev->raid_disk;

		clear_bit(In_sync, &rdev->flags); /* just to be sure */
		if (info->state & (1<<MD_DISK_WRITEMOSTLY))
			set_bit(WriteMostly, &rdev->flags);

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

		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
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
		rdev = md_import_device (dev, -1, 0);
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

		rdev->flags = 0;

		if (rdev->raid_disk < mddev->raid_disks)
			if (info->state & (1<<MD_DISK_SYNC))
				set_bit(In_sync, &rdev->flags);

		if (info->state & (1<<MD_DISK_WRITEMOSTLY))
			set_bit(WriteMostly, &rdev->flags);

		if (!mddev->persistent) {
			printk(KERN_INFO "md: nonpersistent superblock ...\n");
			rdev->sb_offset = rdev->bdev->bd_inode->i_size >> BLOCK_SIZE_BITS;
		} else 
			rdev->sb_offset = calc_dev_sboffset(rdev->bdev);
		rdev->size = calc_dev_size(rdev, mddev->chunk_size);

		err = bind_rdev_to_array(rdev, mddev);
		if (err) {
			export_rdev(rdev);
			return err;
		}
	}

	return 0;
}

static int hot_remove_disk(mddev_t * mddev, dev_t dev)
{
	char b[BDEVNAME_SIZE];
	mdk_rdev_t *rdev;

	if (!mddev->pers)
		return -ENODEV;

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
	printk(KERN_WARNING "md: cannot remove active disk %s from %s ... \n",
		bdevname(rdev->bdev,b), mdname(mddev));
	return -EBUSY;
}

static int hot_add_disk(mddev_t * mddev, dev_t dev)
{
	char b[BDEVNAME_SIZE];
	int err;
	unsigned int size;
	mdk_rdev_t *rdev;

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

	rdev = md_import_device (dev, -1, 0);
	if (IS_ERR(rdev)) {
		printk(KERN_WARNING 
			"md: error, md_import_device() returned %ld\n",
			PTR_ERR(rdev));
		return -EINVAL;
	}

	if (mddev->persistent)
		rdev->sb_offset = calc_dev_sboffset(rdev->bdev);
	else
		rdev->sb_offset =
			rdev->bdev->bd_inode->i_size >> BLOCK_SIZE_BITS;

	size = calc_dev_size(rdev, mddev->chunk_size);
	rdev->size = size;

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

	if (rdev->desc_nr == mddev->max_disks) {
		printk(KERN_WARNING "%s: can not hot-add to full array!\n",
			mdname(mddev));
		err = -EBUSY;
		goto abort_unbind_export;
	}

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

abort_unbind_export:
	unbind_rdev_from_array(rdev);

abort_export:
	export_rdev(rdev);
	return err;
}

static int set_bitmap_file(mddev_t *mddev, int fd)
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
		mddev->bitmap_file = fget(fd);

		if (mddev->bitmap_file == NULL) {
			printk(KERN_ERR "%s: error: failed to get bitmap file\n",
			       mdname(mddev));
			return -EBADF;
		}

		err = deny_bitmap_write_access(mddev->bitmap_file);
		if (err) {
			printk(KERN_ERR "%s: error: bitmap file is already in use\n",
			       mdname(mddev));
			fput(mddev->bitmap_file);
			mddev->bitmap_file = NULL;
			return err;
		}
		mddev->bitmap_offset = 0; /* file overrides offset */
	} else if (mddev->bitmap == NULL)
		return -ENOENT; /* cannot remove what isn't there */
	err = 0;
	if (mddev->pers) {
		mddev->pers->quiesce(mddev, 1);
		if (fd >= 0)
			err = bitmap_create(mddev);
		if (fd < 0 || err) {
			bitmap_destroy(mddev);
			fd = -1; /* make sure to put the file */
		}
		mddev->pers->quiesce(mddev, 0);
	}
	if (fd < 0) {
		if (mddev->bitmap_file) {
			restore_bitmap_write_access(mddev->bitmap_file);
			fput(mddev->bitmap_file);
		}
		mddev->bitmap_file = NULL;
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
static int set_array_info(mddev_t * mddev, mdu_array_info_t *info)
{

	if (info->raid_disks == 0) {
		/* just setting version number for superblock loading */
		if (info->major_version < 0 ||
		    info->major_version >= sizeof(super_types)/sizeof(super_types[0]) ||
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
		return 0;
	}
	mddev->major_version = MD_MAJOR_VERSION;
	mddev->minor_version = MD_MINOR_VERSION;
	mddev->patch_version = MD_PATCHLEVEL_VERSION;
	mddev->ctime         = get_seconds();

	mddev->level         = info->level;
	mddev->clevel[0]     = 0;
	mddev->size          = info->size;
	mddev->raid_disks    = info->raid_disks;
	/* don't set md_minor, it is determined by which /dev/md* was
	 * openned
	 */
	if (info->state & (1<<MD_SB_CLEAN))
		mddev->recovery_cp = MaxSector;
	else
		mddev->recovery_cp = 0;
	mddev->persistent    = ! info->not_persistent;

	mddev->layout        = info->layout;
	mddev->chunk_size    = info->chunk_size;

	mddev->max_disks     = MD_SB_DISKS;

	mddev->flags         = 0;
	set_bit(MD_CHANGE_DEVS, &mddev->flags);

	mddev->default_bitmap_offset = MD_SB_BYTES >> 9;
	mddev->bitmap_offset = 0;

	mddev->reshape_position = MaxSector;

	/*
	 * Generate a 128 bit UUID
	 */
	get_random_bytes(mddev->uuid, 16);

	mddev->new_level = mddev->level;
	mddev->new_chunk = mddev->chunk_size;
	mddev->new_layout = mddev->layout;
	mddev->delta_disks = 0;

	return 0;
}

static int update_size(mddev_t *mddev, unsigned long size)
{
	mdk_rdev_t * rdev;
	int rv;
	struct list_head *tmp;
	int fit = (size == 0);

	if (mddev->pers->resize == NULL)
		return -EINVAL;
	/* The "size" is the amount of each device that is used.
	 * This can only make sense for arrays with redundancy.
	 * linear and raid0 always use whatever space is available
	 * We can only consider changing the size if no resync
	 * or reconstruction is happening, and if the new size
	 * is acceptable. It must fit before the sb_offset or,
	 * if that is <data_offset, it must fit before the
	 * size of each device.
	 * If size is zero, we find the largest size that fits.
	 */
	if (mddev->sync_thread)
		return -EBUSY;
	ITERATE_RDEV(mddev,rdev,tmp) {
		sector_t avail;
		avail = rdev->size * 2;

		if (fit && (size == 0 || size > avail/2))
			size = avail/2;
		if (avail < ((sector_t)size << 1))
			return -ENOSPC;
	}
	rv = mddev->pers->resize(mddev, (sector_t)size *2);
	if (!rv) {
		struct block_device *bdev;

		bdev = bdget_disk(mddev->gendisk, 0);
		if (bdev) {
			mutex_lock(&bdev->bd_inode->i_mutex);
			i_size_write(bdev->bd_inode, (loff_t)mddev->array_size << 10);
			mutex_unlock(&bdev->bd_inode->i_mutex);
			bdput(bdev);
		}
	}
	return rv;
}

static int update_raid_disks(mddev_t *mddev, int raid_disks)
{
	int rv;
	/* change the number of raid disks */
	if (mddev->pers->check_reshape == NULL)
		return -EINVAL;
	if (raid_disks <= 0 ||
	    raid_disks >= mddev->max_disks)
		return -EINVAL;
	if (mddev->sync_thread || mddev->reshape_position != MaxSector)
		return -EBUSY;
	mddev->delta_disks = raid_disks - mddev->raid_disks;

	rv = mddev->pers->check_reshape(mddev);
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
static int update_array_info(mddev_t *mddev, mdu_array_info_t *info)
{
	int rv = 0;
	int cnt = 0;
	int state = 0;

	/* calculate expected state,ignoring low bits */
	if (mddev->bitmap && mddev->bitmap_offset)
		state |= (1 << MD_SB_BITMAP_PRESENT);

	if (mddev->major_version != info->major_version ||
	    mddev->minor_version != info->minor_version ||
/*	    mddev->patch_version != info->patch_version || */
	    mddev->ctime         != info->ctime         ||
	    mddev->level         != info->level         ||
/*	    mddev->layout        != info->layout        || */
	    !mddev->persistent	 != info->not_persistent||
	    mddev->chunk_size    != info->chunk_size    ||
	    /* ignore bottom 8 bits of state, and allow SB_BITMAP_PRESENT to change */
	    ((state^info->state) & 0xfffffe00)
		)
		return -EINVAL;
	/* Check there is only one change */
	if (info->size >= 0 && mddev->size != info->size) cnt++;
	if (mddev->raid_disks != info->raid_disks) cnt++;
	if (mddev->layout != info->layout) cnt++;
	if ((state ^ info->state) & (1<<MD_SB_BITMAP_PRESENT)) cnt++;
	if (cnt == 0) return 0;
	if (cnt > 1) return -EINVAL;

	if (mddev->layout != info->layout) {
		/* Change layout
		 * we don't need to do anything at the md level, the
		 * personality will take care of it all.
		 */
		if (mddev->pers->reconfig == NULL)
			return -EINVAL;
		else
			return mddev->pers->reconfig(mddev, info->layout, -1);
	}
	if (info->size >= 0 && mddev->size != info->size)
		rv = update_size(mddev, info->size);

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
			if (mddev->default_bitmap_offset == 0)
				return -EINVAL;
			mddev->bitmap_offset = mddev->default_bitmap_offset;
			mddev->pers->quiesce(mddev, 1);
			rv = bitmap_create(mddev);
			if (rv)
				bitmap_destroy(mddev);
			mddev->pers->quiesce(mddev, 0);
		} else {
			/* remove the bitmap */
			if (!mddev->bitmap)
				return -ENOENT;
			if (mddev->bitmap->file)
				return -EINVAL;
			mddev->pers->quiesce(mddev, 1);
			bitmap_destroy(mddev);
			mddev->pers->quiesce(mddev, 0);
			mddev->bitmap_offset = 0;
		}
	}
	md_update_sb(mddev, 1);
	return rv;
}

static int set_disk_faulty(mddev_t *mddev, dev_t dev)
{
	mdk_rdev_t *rdev;

	if (mddev->pers == NULL)
		return -ENODEV;

	rdev = find_rdev(mddev, dev);
	if (!rdev)
		return -ENODEV;

	md_error(mddev, rdev);
	return 0;
}

static int md_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	mddev_t *mddev = bdev->bd_disk->private_data;

	geo->heads = 2;
	geo->sectors = 4;
	geo->cylinders = get_capacity(mddev->gendisk) / 8;
	return 0;
}

static int md_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *argp = (void __user *)arg;
	mddev_t *mddev = NULL;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

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

	mddev = inode->i_bdev->bd_disk->private_data;

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
	 * RUN_ARRAY, and SET_BITMAP_FILE are allowed */
	if (!mddev->raid_disks && cmd != ADD_NEW_DISK && cmd != STOP_ARRAY
			&& cmd != RUN_ARRAY && cmd != SET_BITMAP_FILE) {
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
			err = do_md_stop (mddev, 0);
			goto done_unlock;

		case STOP_ARRAY_RO:
			err = do_md_stop (mddev, 1);
			goto done_unlock;

	/*
	 * We have a problem here : there is no easy way to give a CHS
	 * virtual geometry. We currently pretend that we have a 2 heads
	 * 4 sectors (with a BIG number of cylinders...). This drives
	 * dosfs just mad... ;-)
	 */
	}

	/*
	 * The remaining ioctls are changing the state of the
	 * superblock, so we do not allow them on read-only arrays.
	 * However non-MD ioctls (e.g. get-size) will still come through
	 * here and hit the 'default' below, so only disallow
	 * 'md' ioctls, and switch to rw mode if started auto-readonly.
	 */
	if (_IOC_TYPE(cmd) == MD_MAJOR &&
	    mddev->ro && mddev->pers) {
		if (mddev->ro == 2) {
			mddev->ro = 0;
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
			err = do_md_run (mddev);
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
	mddev_unlock(mddev);

	return err;
done:
	if (err)
		MD_BUG();
abort:
	return err;
}

static int md_open(struct inode *inode, struct file *file)
{
	/*
	 * Succeed if we can lock the mddev, which confirms that
	 * it isn't being stopped right now.
	 */
	mddev_t *mddev = inode->i_bdev->bd_disk->private_data;
	int err;

	if ((err = mddev_lock(mddev)))
		goto out;

	err = 0;
	mddev_get(mddev);
	mddev_unlock(mddev);

	check_disk_change(inode->i_bdev);
 out:
	return err;
}

static int md_release(struct inode *inode, struct file * file)
{
 	mddev_t *mddev = inode->i_bdev->bd_disk->private_data;

	BUG_ON(!mddev);
	mddev_put(mddev);

	return 0;
}

static int md_media_changed(struct gendisk *disk)
{
	mddev_t *mddev = disk->private_data;

	return mddev->changed;
}

static int md_revalidate(struct gendisk *disk)
{
	mddev_t *mddev = disk->private_data;

	mddev->changed = 0;
	return 0;
}
static struct block_device_operations md_fops =
{
	.owner		= THIS_MODULE,
	.open		= md_open,
	.release	= md_release,
	.ioctl		= md_ioctl,
	.getgeo		= md_getgeo,
	.media_changed	= md_media_changed,
	.revalidate_disk= md_revalidate,
};

static int md_thread(void * arg)
{
	mdk_thread_t *thread = arg;

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

	current->flags |= PF_NOFREEZE;
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

		thread->run(thread->mddev);
	}

	return 0;
}

void md_wakeup_thread(mdk_thread_t *thread)
{
	if (thread) {
		dprintk("md: waking up MD thread %s.\n", thread->tsk->comm);
		set_bit(THREAD_WAKEUP, &thread->flags);
		wake_up(&thread->wqueue);
	}
}

mdk_thread_t *md_register_thread(void (*run) (mddev_t *), mddev_t *mddev,
				 const char *name)
{
	mdk_thread_t *thread;

	thread = kzalloc(sizeof(mdk_thread_t), GFP_KERNEL);
	if (!thread)
		return NULL;

	init_waitqueue_head(&thread->wqueue);

	thread->run = run;
	thread->mddev = mddev;
	thread->timeout = MAX_SCHEDULE_TIMEOUT;
	thread->tsk = kthread_run(md_thread, thread, name, mdname(thread->mddev));
	if (IS_ERR(thread->tsk)) {
		kfree(thread);
		return NULL;
	}
	return thread;
}

void md_unregister_thread(mdk_thread_t *thread)
{
	dprintk("interrupting MD-thread pid %d\n", thread->tsk->pid);

	kthread_stop(thread->tsk);
	kfree(thread);
}

void md_error(mddev_t *mddev, mdk_rdev_t *rdev)
{
	if (!mddev) {
		MD_BUG();
		return;
	}

	if (!rdev || test_bit(Faulty, &rdev->flags))
		return;
/*
	dprintk("md_error dev:%s, rdev:(%d:%d), (caller: %p,%p,%p,%p).\n",
		mdname(mddev),
		MAJOR(rdev->bdev->bd_dev), MINOR(rdev->bdev->bd_dev),
		__builtin_return_address(0),__builtin_return_address(1),
		__builtin_return_address(2),__builtin_return_address(3));
*/
	if (!mddev->pers)
		return;
	if (!mddev->pers->error_handler)
		return;
	mddev->pers->error_handler(mddev,rdev);
	set_bit(MD_RECOVERY_INTR, &mddev->recovery);
	set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
	md_new_event_inintr(mddev);
}

/* seq_file implementation /proc/mdstat */

static void status_unused(struct seq_file *seq)
{
	int i = 0;
	mdk_rdev_t *rdev;
	struct list_head *tmp;

	seq_printf(seq, "unused devices: ");

	ITERATE_RDEV_PENDING(rdev,tmp) {
		char b[BDEVNAME_SIZE];
		i++;
		seq_printf(seq, "%s ",
			      bdevname(rdev->bdev,b));
	}
	if (!i)
		seq_printf(seq, "<none>");

	seq_printf(seq, "\n");
}


static void status_resync(struct seq_file *seq, mddev_t * mddev)
{
	sector_t max_blocks, resync, res;
	unsigned long dt, db, rt;
	int scale;
	unsigned int per_milli;

	resync = (mddev->curr_resync - atomic_read(&mddev->recovery_active))/2;

	if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery))
		max_blocks = mddev->resync_max_sectors >> 1;
	else
		max_blocks = mddev->size;

	/*
	 * Should not happen.
	 */
	if (!max_blocks) {
		MD_BUG();
		return;
	}
	/* Pick 'scale' such that (resync>>scale)*1000 will fit
	 * in a sector_t, and (max_blocks>>scale) will fit in a
	 * u32, as those are the requirements for sector_div.
	 * Thus 'scale' must be at least 10
	 */
	scale = 10;
	if (sizeof(sector_t) > sizeof(unsigned long)) {
		while ( max_blocks/2 > (1ULL<<(scale+32)))
			scale++;
	}
	res = (resync>>scale)*1000;
	sector_div(res, (u32)((max_blocks>>scale)+1));

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
		   (unsigned long long) resync,
		   (unsigned long long) max_blocks);

	/*
	 * We do not want to overflow, so the order of operands and
	 * the * 100 / 100 trick are important. We do a +1 to be
	 * safe against division by zero. We only estimate anyway.
	 *
	 * dt: time from mark until now
	 * db: blocks written from mark until now
	 * rt: remaining time
	 */
	dt = ((jiffies - mddev->resync_mark) / HZ);
	if (!dt) dt++;
	db = (mddev->curr_mark_cnt - atomic_read(&mddev->recovery_active))
		- mddev->resync_mark_cnt;
	rt = (dt * ((unsigned long)(max_blocks-resync) / (db/2/100+1)))/100;

	seq_printf(seq, " finish=%lu.%lumin", rt / 60, (rt % 60)/6);

	seq_printf(seq, " speed=%ldK/sec", db/2/dt);
}

static void *md_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct list_head *tmp;
	loff_t l = *pos;
	mddev_t *mddev;

	if (l >= 0x10000)
		return NULL;
	if (!l--)
		/* header */
		return (void*)1;

	spin_lock(&all_mddevs_lock);
	list_for_each(tmp,&all_mddevs)
		if (!l--) {
			mddev = list_entry(tmp, mddev_t, all_mddevs);
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
	mddev_t *next_mddev, *mddev = v;
	
	++*pos;
	if (v == (void*)2)
		return NULL;

	spin_lock(&all_mddevs_lock);
	if (v == (void*)1)
		tmp = all_mddevs.next;
	else
		tmp = mddev->all_mddevs.next;
	if (tmp != &all_mddevs)
		next_mddev = mddev_get(list_entry(tmp,mddev_t,all_mddevs));
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
	mddev_t *mddev = v;

	if (mddev && v != (void*)1 && v != (void*)2)
		mddev_put(mddev);
}

struct mdstat_info {
	int event;
};

static int md_seq_show(struct seq_file *seq, void *v)
{
	mddev_t *mddev = v;
	sector_t size;
	struct list_head *tmp2;
	mdk_rdev_t *rdev;
	struct mdstat_info *mi = seq->private;
	struct bitmap *bitmap;

	if (v == (void*)1) {
		struct mdk_personality *pers;
		seq_printf(seq, "Personalities : ");
		spin_lock(&pers_lock);
		list_for_each_entry(pers, &pers_list, list)
			seq_printf(seq, "[%s] ", pers->name);

		spin_unlock(&pers_lock);
		seq_printf(seq, "\n");
		mi->event = atomic_read(&md_event_count);
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
				seq_printf(seq, "(auto-read-only)");
			seq_printf(seq, " %s", mddev->pers->name);
		}

		size = 0;
		ITERATE_RDEV(mddev,rdev,tmp2) {
			char b[BDEVNAME_SIZE];
			seq_printf(seq, " %s[%d]",
				bdevname(rdev->bdev,b), rdev->desc_nr);
			if (test_bit(WriteMostly, &rdev->flags))
				seq_printf(seq, "(W)");
			if (test_bit(Faulty, &rdev->flags)) {
				seq_printf(seq, "(F)");
				continue;
			} else if (rdev->raid_disk < 0)
				seq_printf(seq, "(S)"); /* spare */
			size += rdev->size;
		}

		if (!list_empty(&mddev->disks)) {
			if (mddev->pers)
				seq_printf(seq, "\n      %llu blocks",
					(unsigned long long)mddev->array_size);
			else
				seq_printf(seq, "\n      %llu blocks",
					(unsigned long long)size);
		}
		if (mddev->persistent) {
			if (mddev->major_version != 0 ||
			    mddev->minor_version != 90) {
				seq_printf(seq," super %d.%d",
					   mddev->major_version,
					   mddev->minor_version);
			}
		} else
			seq_printf(seq, " super non-persistent");

		if (mddev->pers) {
			mddev->pers->status (seq, mddev);
	 		seq_printf(seq, "\n      ");
			if (mddev->pers->sync_request) {
				if (mddev->curr_resync > 2) {
					status_resync (seq, mddev);
					seq_printf(seq, "\n      ");
				} else if (mddev->curr_resync == 1 || mddev->curr_resync == 2)
					seq_printf(seq, "\tresync=DELAYED\n      ");
				else if (mddev->recovery_cp < MaxSector)
					seq_printf(seq, "\tresync=PENDING\n      ");
			}
		} else
			seq_printf(seq, "\n       ");

		if ((bitmap = mddev->bitmap)) {
			unsigned long chunk_kb;
			unsigned long flags;
			spin_lock_irqsave(&bitmap->lock, flags);
			chunk_kb = bitmap->chunksize >> 10;
			seq_printf(seq, "bitmap: %lu/%lu pages [%luKB], "
				"%lu%s chunk",
				bitmap->pages - bitmap->missing_pages,
				bitmap->pages,
				(bitmap->pages - bitmap->missing_pages)
					<< (PAGE_SHIFT - 10),
				chunk_kb ? chunk_kb : bitmap->chunksize,
				chunk_kb ? "KB" : "B");
			if (bitmap->file) {
				seq_printf(seq, ", file: ");
				seq_path(seq, bitmap->file->f_vfsmnt,
					 bitmap->file->f_dentry," \t\n");
			}

			seq_printf(seq, "\n");
			spin_unlock_irqrestore(&bitmap->lock, flags);
		}

		seq_printf(seq, "\n");
	}
	mddev_unlock(mddev);
	
	return 0;
}

static struct seq_operations md_seq_ops = {
	.start  = md_seq_start,
	.next   = md_seq_next,
	.stop   = md_seq_stop,
	.show   = md_seq_show,
};

static int md_seq_open(struct inode *inode, struct file *file)
{
	int error;
	struct mdstat_info *mi = kmalloc(sizeof(*mi), GFP_KERNEL);
	if (mi == NULL)
		return -ENOMEM;

	error = seq_open(file, &md_seq_ops);
	if (error)
		kfree(mi);
	else {
		struct seq_file *p = file->private_data;
		p->private = mi;
		mi->event = atomic_read(&md_event_count);
	}
	return error;
}

static int md_seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct mdstat_info *mi = m->private;
	m->private = NULL;
	kfree(mi);
	return seq_release(inode, file);
}

static unsigned int mdstat_poll(struct file *filp, poll_table *wait)
{
	struct seq_file *m = filp->private_data;
	struct mdstat_info *mi = m->private;
	int mask;

	poll_wait(filp, &md_event_waiters, wait);

	/* always allow read */
	mask = POLLIN | POLLRDNORM;

	if (mi->event != atomic_read(&md_event_count))
		mask |= POLLERR | POLLPRI;
	return mask;
}

static struct file_operations md_seq_fops = {
	.owner		= THIS_MODULE,
	.open           = md_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= md_seq_release,
	.poll		= mdstat_poll,
};

int register_md_personality(struct mdk_personality *p)
{
	spin_lock(&pers_lock);
	list_add_tail(&p->list, &pers_list);
	printk(KERN_INFO "md: %s personality registered for level %d\n", p->name, p->level);
	spin_unlock(&pers_lock);
	return 0;
}

int unregister_md_personality(struct mdk_personality *p)
{
	printk(KERN_INFO "md: %s personality unregistered\n", p->name);
	spin_lock(&pers_lock);
	list_del_init(&p->list);
	spin_unlock(&pers_lock);
	return 0;
}

static int is_mddev_idle(mddev_t *mddev)
{
	mdk_rdev_t * rdev;
	struct list_head *tmp;
	int idle;
	unsigned long curr_events;

	idle = 1;
	ITERATE_RDEV(mddev,rdev,tmp) {
		struct gendisk *disk = rdev->bdev->bd_contains->bd_disk;
		curr_events = disk_stat_read(disk, sectors[0]) + 
				disk_stat_read(disk, sectors[1]) - 
				atomic_read(&disk->sync_io);
		/* The difference between curr_events and last_events
		 * will be affected by any new non-sync IO (making
		 * curr_events bigger) and any difference in the amount of
		 * in-flight syncio (making current_events bigger or smaller)
		 * The amount in-flight is currently limited to
		 * 32*64K in raid1/10 and 256*PAGE_SIZE in raid5/6
		 * which is at most 4096 sectors.
		 * These numbers are fairly fragile and should be made
		 * more robust, probably by enforcing the
		 * 'window size' that md_do_sync sort-of uses.
		 *
		 * Note: the following is an unsigned comparison.
		 */
		if ((curr_events - rdev->last_events + 4096) > 8192) {
			rdev->last_events = curr_events;
			idle = 0;
		}
	}
	return idle;
}

void md_done_sync(mddev_t *mddev, int blocks, int ok)
{
	/* another "blocks" (512byte) blocks have been synced */
	atomic_sub(blocks, &mddev->recovery_active);
	wake_up(&mddev->recovery_wait);
	if (!ok) {
		set_bit(MD_RECOVERY_ERR, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
		// stop recovery, signal do_sync ....
	}
}


/* md_write_start(mddev, bi)
 * If we need to update some array metadata (e.g. 'active' flag
 * in superblock) before writing, schedule a superblock update
 * and wait for it to complete.
 */
void md_write_start(mddev_t *mddev, struct bio *bi)
{
	if (bio_data_dir(bi) != WRITE)
		return;

	BUG_ON(mddev->ro == 1);
	if (mddev->ro == 2) {
		/* need to switch to read/write */
		mddev->ro = 0;
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		md_wakeup_thread(mddev->thread);
	}
	atomic_inc(&mddev->writes_pending);
	if (mddev->in_sync) {
		spin_lock_irq(&mddev->write_lock);
		if (mddev->in_sync) {
			mddev->in_sync = 0;
			set_bit(MD_CHANGE_CLEAN, &mddev->flags);
			md_wakeup_thread(mddev->thread);
		}
		spin_unlock_irq(&mddev->write_lock);
	}
	wait_event(mddev->sb_wait, mddev->flags==0);
}

void md_write_end(mddev_t *mddev)
{
	if (atomic_dec_and_test(&mddev->writes_pending)) {
		if (mddev->safemode == 2)
			md_wakeup_thread(mddev->thread);
		else if (mddev->safemode_delay)
			mod_timer(&mddev->safemode_timer, jiffies + mddev->safemode_delay);
	}
}

static DECLARE_WAIT_QUEUE_HEAD(resync_wait);

#define SYNC_MARKS	10
#define	SYNC_MARK_STEP	(3*HZ)
void md_do_sync(mddev_t *mddev)
{
	mddev_t *mddev2;
	unsigned int currspeed = 0,
		 window;
	sector_t max_sectors,j, io_sectors;
	unsigned long mark[SYNC_MARKS];
	sector_t mark_cnt[SYNC_MARKS];
	int last_mark,m;
	struct list_head *tmp;
	sector_t last_check;
	int skipped = 0;
	struct list_head *rtmp;
	mdk_rdev_t *rdev;
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
		if (kthread_should_stop()) {
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			goto skip;
		}
		ITERATE_MDDEV(mddev2,tmp) {
			if (mddev2 == mddev)
				continue;
			if (mddev2->curr_resync && 
			    match_mddev_units(mddev,mddev2)) {
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
				prepare_to_wait(&resync_wait, &wq, TASK_UNINTERRUPTIBLE);
				if (!kthread_should_stop() &&
				    mddev2->curr_resync >= mddev->curr_resync) {
					printk(KERN_INFO "md: delaying %s of %s"
					       " until %s has finished (they"
					       " share one or more physical units)\n",
					       desc, mdname(mddev), mdname(mddev2));
					mddev_put(mddev2);
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
		if (!mddev->bitmap &&
		    !test_bit(MD_RECOVERY_REQUESTED, &mddev->recovery))
			j = mddev->recovery_cp;
	} else if (test_bit(MD_RECOVERY_RESHAPE, &mddev->recovery))
		max_sectors = mddev->size << 1;
	else {
		/* recovery follows the physical size of devices */
		max_sectors = mddev->size << 1;
		j = MaxSector;
		ITERATE_RDEV(mddev,rdev,rtmp)
			if (rdev->raid_disk >= 0 &&
			    !test_bit(Faulty, &rdev->flags) &&
			    !test_bit(In_sync, &rdev->flags) &&
			    rdev->recovery_offset < j)
				j = rdev->recovery_offset;
	}

	printk(KERN_INFO "md: %s of RAID array %s\n", desc, mdname(mddev));
	printk(KERN_INFO "md: minimum _guaranteed_  speed:"
		" %d KB/sec/disk.\n", speed_min(mddev));
	printk(KERN_INFO "md: using maximum available idle IO bandwidth "
	       "(but not more than %d KB/sec) for %s.\n",
	       speed_max(mddev), desc);

	is_mddev_idle(mddev); /* this also initializes IO event counters */

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
	printk(KERN_INFO "md: using %dk window, over a total of %llu blocks.\n",
		window/2,(unsigned long long) max_sectors/2);

	atomic_set(&mddev->recovery_active, 0);
	init_waitqueue_head(&mddev->recovery_wait);
	last_check = 0;

	if (j>2) {
		printk(KERN_INFO 
		       "md: resuming %s of %s from checkpoint.\n",
		       desc, mdname(mddev));
		mddev->curr_resync = j;
	}

	while (j < max_sectors) {
		sector_t sectors;

		skipped = 0;
		sectors = mddev->pers->sync_request(mddev, j, &skipped,
					    currspeed < speed_min(mddev));
		if (sectors == 0) {
			set_bit(MD_RECOVERY_ERR, &mddev->recovery);
			goto out;
		}

		if (!skipped) { /* actual IO requested */
			io_sectors += sectors;
			atomic_add(sectors, &mddev->recovery_active);
		}

		j += sectors;
		if (j>1) mddev->curr_resync = j;
		mddev->curr_mark_cnt = io_sectors;
		if (last_check == 0)
			/* this is the earliers that rebuilt will be
			 * visible in /proc/mdstat
			 */
			md_new_event(mddev);

		if (last_check + window > io_sectors || j == max_sectors)
			continue;

		last_check = io_sectors;

		if (test_bit(MD_RECOVERY_INTR, &mddev->recovery) ||
		    test_bit(MD_RECOVERY_ERR, &mddev->recovery))
			break;

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


		if (kthread_should_stop()) {
			/*
			 * got a signal, exit.
			 */
			printk(KERN_INFO 
				"md: md_do_sync() got signal ... exiting\n");
			set_bit(MD_RECOVERY_INTR, &mddev->recovery);
			goto out;
		}

		/*
		 * this loop exits only if either when we are slower than
		 * the 'hard' speed limit, or the system was IO-idle for
		 * a jiffy.
		 * the system might be non-idle CPU-wise, but we only care
		 * about not overloading the IO subsystem. (things like an
		 * e2fsck being done on the RAID array should execute fast)
		 */
		mddev->queue->unplug_fn(mddev->queue);
		cond_resched();

		currspeed = ((unsigned long)(io_sectors-mddev->resync_mark_cnt))/2
			/((jiffies-mddev->resync_mark)/HZ +1) +1;

		if (currspeed > speed_min(mddev)) {
			if ((currspeed > speed_max(mddev)) ||
					!is_mddev_idle(mddev)) {
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
	mddev->queue->unplug_fn(mddev->queue);

	wait_event(mddev->recovery_wait, !atomic_read(&mddev->recovery_active));

	/* tell personality that we are finished */
	mddev->pers->sync_request(mddev, max_sectors, &skipped, 1);

	if (!test_bit(MD_RECOVERY_ERR, &mddev->recovery) &&
	    test_bit(MD_RECOVERY_SYNC, &mddev->recovery) &&
	    !test_bit(MD_RECOVERY_CHECK, &mddev->recovery) &&
	    mddev->curr_resync > 2) {
		if (test_bit(MD_RECOVERY_SYNC, &mddev->recovery)) {
			if (test_bit(MD_RECOVERY_INTR, &mddev->recovery)) {
				if (mddev->curr_resync >= mddev->recovery_cp) {
					printk(KERN_INFO
					       "md: checkpointing %s of %s.\n",
					       desc, mdname(mddev));
					mddev->recovery_cp = mddev->curr_resync;
				}
			} else
				mddev->recovery_cp = MaxSector;
		} else {
			if (!test_bit(MD_RECOVERY_INTR, &mddev->recovery))
				mddev->curr_resync = MaxSector;
			ITERATE_RDEV(mddev,rdev,rtmp)
				if (rdev->raid_disk >= 0 &&
				    !test_bit(Faulty, &rdev->flags) &&
				    !test_bit(In_sync, &rdev->flags) &&
				    rdev->recovery_offset < mddev->curr_resync)
					rdev->recovery_offset = mddev->curr_resync;
		}
	}

 skip:
	mddev->curr_resync = 0;
	wake_up(&resync_wait);
	set_bit(MD_RECOVERY_DONE, &mddev->recovery);
	md_wakeup_thread(mddev->thread);
}
EXPORT_SYMBOL_GPL(md_do_sync);


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
 * When the thread finishes it sets MD_RECOVERY_DONE (and might set MD_RECOVERY_ERR)
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
void md_check_recovery(mddev_t *mddev)
{
	mdk_rdev_t *rdev;
	struct list_head *rtmp;


	if (mddev->bitmap)
		bitmap_daemon_work(mddev->bitmap);

	if (mddev->ro)
		return;

	if (signal_pending(current)) {
		if (mddev->pers->sync_request) {
			printk(KERN_INFO "md: %s in immediate safe mode\n",
			       mdname(mddev));
			mddev->safemode = 2;
		}
		flush_signals(current);
	}

	if ( ! (
		mddev->flags ||
		test_bit(MD_RECOVERY_NEEDED, &mddev->recovery) ||
		test_bit(MD_RECOVERY_DONE, &mddev->recovery) ||
		(mddev->safemode == 1) ||
		(mddev->safemode == 2 && ! atomic_read(&mddev->writes_pending)
		 && !mddev->in_sync && mddev->recovery_cp == MaxSector)
		))
		return;

	if (mddev_trylock(mddev)) {
		int spares =0;

		spin_lock_irq(&mddev->write_lock);
		if (mddev->safemode && !atomic_read(&mddev->writes_pending) &&
		    !mddev->in_sync && mddev->recovery_cp == MaxSector) {
			mddev->in_sync = 1;
			set_bit(MD_CHANGE_CLEAN, &mddev->flags);
		}
		if (mddev->safemode == 1)
			mddev->safemode = 0;
		spin_unlock_irq(&mddev->write_lock);

		if (mddev->flags)
			md_update_sb(mddev, 0);


		if (test_bit(MD_RECOVERY_RUNNING, &mddev->recovery) &&
		    !test_bit(MD_RECOVERY_DONE, &mddev->recovery)) {
			/* resync/recovery still happening */
			clear_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			goto unlock;
		}
		if (mddev->sync_thread) {
			/* resync has finished, collect result */
			md_unregister_thread(mddev->sync_thread);
			mddev->sync_thread = NULL;
			if (!test_bit(MD_RECOVERY_ERR, &mddev->recovery) &&
			    !test_bit(MD_RECOVERY_INTR, &mddev->recovery)) {
				/* success...*/
				/* activate any spares */
				mddev->pers->spare_active(mddev);
			}
			md_update_sb(mddev, 1);

			/* if array is no-longer degraded, then any saved_raid_disk
			 * information must be scrapped
			 */
			if (!mddev->degraded)
				ITERATE_RDEV(mddev,rdev,rtmp)
					rdev->saved_raid_disk = -1;

			mddev->recovery = 0;
			/* flag recovery needed just to double check */
			set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
			md_new_event(mddev);
			goto unlock;
		}
		/* Clear some bits that don't mean anything, but
		 * might be left set
		 */
		clear_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
		clear_bit(MD_RECOVERY_ERR, &mddev->recovery);
		clear_bit(MD_RECOVERY_INTR, &mddev->recovery);
		clear_bit(MD_RECOVERY_DONE, &mddev->recovery);

		if (test_bit(MD_RECOVERY_FROZEN, &mddev->recovery))
			goto unlock;
		/* no recovery is running.
		 * remove any failed drives, then
		 * add spares if possible.
		 * Spare are also removed and re-added, to allow
		 * the personality to fail the re-add.
		 */
		ITERATE_RDEV(mddev,rdev,rtmp)
			if (rdev->raid_disk >= 0 &&
			    (test_bit(Faulty, &rdev->flags) || ! test_bit(In_sync, &rdev->flags)) &&
			    atomic_read(&rdev->nr_pending)==0) {
				if (mddev->pers->hot_remove_disk(mddev, rdev->raid_disk)==0) {
					char nm[20];
					sprintf(nm,"rd%d", rdev->raid_disk);
					sysfs_remove_link(&mddev->kobj, nm);
					rdev->raid_disk = -1;
				}
			}

		if (mddev->degraded) {
			ITERATE_RDEV(mddev,rdev,rtmp)
				if (rdev->raid_disk < 0
				    && !test_bit(Faulty, &rdev->flags)) {
					rdev->recovery_offset = 0;
					if (mddev->pers->hot_add_disk(mddev,rdev)) {
						char nm[20];
						sprintf(nm, "rd%d", rdev->raid_disk);
						sysfs_create_link(&mddev->kobj, &rdev->kobj, nm);
						spares++;
						md_new_event(mddev);
					} else
						break;
				}
		}

		if (spares) {
			clear_bit(MD_RECOVERY_SYNC, &mddev->recovery);
			clear_bit(MD_RECOVERY_CHECK, &mddev->recovery);
		} else if (mddev->recovery_cp < MaxSector) {
			set_bit(MD_RECOVERY_SYNC, &mddev->recovery);
		} else if (!test_bit(MD_RECOVERY_SYNC, &mddev->recovery))
			/* nothing to be done ... */
			goto unlock;

		if (mddev->pers->sync_request) {
			set_bit(MD_RECOVERY_RUNNING, &mddev->recovery);
			if (spares && mddev->bitmap && ! mddev->bitmap->file) {
				/* We are adding a device or devices to an array
				 * which has the bitmap stored on all devices.
				 * So make sure all bitmap pages get written
				 */
				bitmap_write_all(mddev->bitmap);
			}
			mddev->sync_thread = md_register_thread(md_do_sync,
								mddev,
								"%s_resync");
			if (!mddev->sync_thread) {
				printk(KERN_ERR "%s: could not start resync"
					" thread...\n", 
					mdname(mddev));
				/* leave the spares where they are, it shouldn't hurt */
				mddev->recovery = 0;
			} else
				md_wakeup_thread(mddev->sync_thread);
			md_new_event(mddev);
		}
	unlock:
		mddev_unlock(mddev);
	}
}

static int md_notify_reboot(struct notifier_block *this,
			    unsigned long code, void *x)
{
	struct list_head *tmp;
	mddev_t *mddev;

	if ((code == SYS_DOWN) || (code == SYS_HALT) || (code == SYS_POWER_OFF)) {

		printk(KERN_INFO "md: stopping all md devices.\n");

		ITERATE_MDDEV(mddev,tmp)
			if (mddev_trylock(mddev)) {
				do_md_stop (mddev, 1);
				mddev_unlock(mddev);
			}
		/*
		 * certain more exotic SCSI devices are known to be
		 * volatile wrt too early system reboots. While the
		 * right place to handle this issue is the given
		 * driver, we do want to have a safe RAID driver ...
		 */
		mdelay(1000*1);
	}
	return NOTIFY_DONE;
}

static struct notifier_block md_notifier = {
	.notifier_call	= md_notify_reboot,
	.next		= NULL,
	.priority	= INT_MAX, /* before any real devices */
};

static void md_geninit(void)
{
	struct proc_dir_entry *p;

	dprintk("md: sizeof(mdp_super_t) = %d\n", (int)sizeof(mdp_super_t));

	p = create_proc_entry("mdstat", S_IRUGO, NULL);
	if (p)
		p->proc_fops = &md_seq_fops;
}

static int __init md_init(void)
{
	if (register_blkdev(MAJOR_NR, "md"))
		return -1;
	if ((mdp_major=register_blkdev(0, "mdp"))<=0) {
		unregister_blkdev(MAJOR_NR, "md");
		return -1;
	}
	blk_register_region(MKDEV(MAJOR_NR, 0), 1UL<<MINORBITS, THIS_MODULE,
			    md_probe, NULL, NULL);
	blk_register_region(MKDEV(mdp_major, 0), 1UL<<MINORBITS, THIS_MODULE,
			    md_probe, NULL, NULL);

	register_reboot_notifier(&md_notifier);
	raid_table_header = register_sysctl_table(raid_root_table, 1);

	md_geninit();
	return (0);
}


#ifndef MODULE

/*
 * Searches all registered partitions for autorun RAID arrays
 * at boot time.
 */
static dev_t detected_devices[128];
static int dev_cnt;

void md_autodetect_dev(dev_t dev)
{
	if (dev_cnt >= 0 && dev_cnt < 127)
		detected_devices[dev_cnt++] = dev;
}


static void autostart_arrays(int part)
{
	mdk_rdev_t *rdev;
	int i;

	printk(KERN_INFO "md: Autodetecting RAID arrays.\n");

	for (i = 0; i < dev_cnt; i++) {
		dev_t dev = detected_devices[i];

		rdev = md_import_device(dev,0, 0);
		if (IS_ERR(rdev))
			continue;

		if (test_bit(Faulty, &rdev->flags)) {
			MD_BUG();
			continue;
		}
		list_add(&rdev->same_set, &pending_raid_disks);
	}
	dev_cnt = 0;

	autorun_devices(part);
}

#endif

static __exit void md_exit(void)
{
	mddev_t *mddev;
	struct list_head *tmp;

	blk_unregister_region(MKDEV(MAJOR_NR,0), 1U << MINORBITS);
	blk_unregister_region(MKDEV(mdp_major,0), 1U << MINORBITS);

	unregister_blkdev(MAJOR_NR,"md");
	unregister_blkdev(mdp_major, "mdp");
	unregister_reboot_notifier(&md_notifier);
	unregister_sysctl_table(raid_table_header);
	remove_proc_entry("mdstat", NULL);
	ITERATE_MDDEV(mddev,tmp) {
		struct gendisk *disk = mddev->gendisk;
		if (!disk)
			continue;
		export_array(mddev);
		del_gendisk(disk);
		put_disk(disk);
		mddev->gendisk = NULL;
		mddev_put(mddev);
	}
}

module_init(md_init)
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
MODULE_ALIAS("md");
MODULE_ALIAS_BLOCKDEV_MAJOR(MD_MAJOR);
