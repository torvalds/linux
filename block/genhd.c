// SPDX-License-Identifier: GPL-2.0
/*
 *  gendisk handling
 *
 * Portions Copyright (C) 2020 Christoph Hellwig
 */

#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/idr.h>
#include <linux/log2.h>
#include <linux/pm_runtime.h>
#include <linux/badblocks.h>

#include "blk.h"
#include "blk-rq-qos.h"

static struct kobject *block_depr;

/*
 * Unique, monotonically increasing sequential number associated with block
 * devices instances (i.e. incremented each time a device is attached).
 * Associating uevents with block devices in userspace is difficult and racy:
 * the uevent netlink socket is lossy, and on slow and overloaded systems has
 * a very high latency.
 * Block devices do not have exclusive owners in userspace, any process can set
 * one up (e.g. loop devices). Moreover, device names can be reused (e.g. loop0
 * can be reused again and again).
 * A userspace process setting up a block device and watching for its events
 * cannot thus reliably tell whether an event relates to the device it just set
 * up or another earlier instance with the same name.
 * This sequential number allows userspace processes to solve this problem, and
 * uniquely associate an uevent to the lifetime to a device.
 */
static atomic64_t diskseq;

/* for extended dynamic devt allocation, currently only one major is used */
#define NR_EXT_DEVT		(1 << MINORBITS)
static DEFINE_IDA(ext_devt_ida);

void set_capacity(struct gendisk *disk, sector_t sectors)
{
	struct block_device *bdev = disk->part0;

	spin_lock(&bdev->bd_size_lock);
	i_size_write(bdev->bd_inode, (loff_t)sectors << SECTOR_SHIFT);
	spin_unlock(&bdev->bd_size_lock);
}
EXPORT_SYMBOL(set_capacity);

/*
 * Set disk capacity and notify if the size is not currently zero and will not
 * be set to zero.  Returns true if a uevent was sent, otherwise false.
 */
bool set_capacity_and_notify(struct gendisk *disk, sector_t size)
{
	sector_t capacity = get_capacity(disk);
	char *envp[] = { "RESIZE=1", NULL };

	set_capacity(disk, size);

	/*
	 * Only print a message and send a uevent if the gendisk is user visible
	 * and alive.  This avoids spamming the log and udev when setting the
	 * initial capacity during probing.
	 */
	if (size == capacity ||
	    !disk_live(disk) ||
	    (disk->flags & GENHD_FL_HIDDEN))
		return false;

	pr_info("%s: detected capacity change from %lld to %lld\n",
		disk->disk_name, capacity, size);

	/*
	 * Historically we did not send a uevent for changes to/from an empty
	 * device.
	 */
	if (!capacity || !size)
		return false;
	kobject_uevent_env(&disk_to_dev(disk)->kobj, KOBJ_CHANGE, envp);
	return true;
}
EXPORT_SYMBOL_GPL(set_capacity_and_notify);

/*
 * Format the device name of the indicated block device into the supplied buffer
 * and return a pointer to that same buffer for convenience.
 *
 * Note: do not use this in new code, use the %pg specifier to sprintf and
 * printk insted.
 */
const char *bdevname(struct block_device *bdev, char *buf)
{
	struct gendisk *hd = bdev->bd_disk;
	int partno = bdev->bd_partno;

	if (!partno)
		snprintf(buf, BDEVNAME_SIZE, "%s", hd->disk_name);
	else if (isdigit(hd->disk_name[strlen(hd->disk_name)-1]))
		snprintf(buf, BDEVNAME_SIZE, "%sp%d", hd->disk_name, partno);
	else
		snprintf(buf, BDEVNAME_SIZE, "%s%d", hd->disk_name, partno);

	return buf;
}
EXPORT_SYMBOL(bdevname);

static void part_stat_read_all(struct block_device *part,
		struct disk_stats *stat)
{
	int cpu;

	memset(stat, 0, sizeof(struct disk_stats));
	for_each_possible_cpu(cpu) {
		struct disk_stats *ptr = per_cpu_ptr(part->bd_stats, cpu);
		int group;

		for (group = 0; group < NR_STAT_GROUPS; group++) {
			stat->nsecs[group] += ptr->nsecs[group];
			stat->sectors[group] += ptr->sectors[group];
			stat->ios[group] += ptr->ios[group];
			stat->merges[group] += ptr->merges[group];
		}

		stat->io_ticks += ptr->io_ticks;
	}
}

static unsigned int part_in_flight(struct block_device *part)
{
	unsigned int inflight = 0;
	int cpu;

	for_each_possible_cpu(cpu) {
		inflight += part_stat_local_read_cpu(part, in_flight[0], cpu) +
			    part_stat_local_read_cpu(part, in_flight[1], cpu);
	}
	if ((int)inflight < 0)
		inflight = 0;

	return inflight;
}

static void part_in_flight_rw(struct block_device *part,
		unsigned int inflight[2])
{
	int cpu;

	inflight[0] = 0;
	inflight[1] = 0;
	for_each_possible_cpu(cpu) {
		inflight[0] += part_stat_local_read_cpu(part, in_flight[0], cpu);
		inflight[1] += part_stat_local_read_cpu(part, in_flight[1], cpu);
	}
	if ((int)inflight[0] < 0)
		inflight[0] = 0;
	if ((int)inflight[1] < 0)
		inflight[1] = 0;
}

/*
 * Can be deleted altogether. Later.
 *
 */
#define BLKDEV_MAJOR_HASH_SIZE 255
static struct blk_major_name {
	struct blk_major_name *next;
	int major;
	char name[16];
	void (*probe)(dev_t devt);
} *major_names[BLKDEV_MAJOR_HASH_SIZE];
static DEFINE_MUTEX(major_names_lock);
static DEFINE_SPINLOCK(major_names_spinlock);

/* index in the above - for now: assume no multimajor ranges */
static inline int major_to_index(unsigned major)
{
	return major % BLKDEV_MAJOR_HASH_SIZE;
}

#ifdef CONFIG_PROC_FS
void blkdev_show(struct seq_file *seqf, off_t offset)
{
	struct blk_major_name *dp;

	spin_lock(&major_names_spinlock);
	for (dp = major_names[major_to_index(offset)]; dp; dp = dp->next)
		if (dp->major == offset)
			seq_printf(seqf, "%3d %s\n", dp->major, dp->name);
	spin_unlock(&major_names_spinlock);
}
#endif /* CONFIG_PROC_FS */

/**
 * __register_blkdev - register a new block device
 *
 * @major: the requested major device number [1..BLKDEV_MAJOR_MAX-1]. If
 *         @major = 0, try to allocate any unused major number.
 * @name: the name of the new block device as a zero terminated string
 * @probe: allback that is called on access to any minor number of @major
 *
 * The @name must be unique within the system.
 *
 * The return value depends on the @major input parameter:
 *
 *  - if a major device number was requested in range [1..BLKDEV_MAJOR_MAX-1]
 *    then the function returns zero on success, or a negative error code
 *  - if any unused major number was requested with @major = 0 parameter
 *    then the return value is the allocated major number in range
 *    [1..BLKDEV_MAJOR_MAX-1] or a negative error code otherwise
 *
 * See Documentation/admin-guide/devices.txt for the list of allocated
 * major numbers.
 *
 * Use register_blkdev instead for any new code.
 */
int __register_blkdev(unsigned int major, const char *name,
		void (*probe)(dev_t devt))
{
	struct blk_major_name **n, *p;
	int index, ret = 0;

	mutex_lock(&major_names_lock);

	/* temporary */
	if (major == 0) {
		for (index = ARRAY_SIZE(major_names)-1; index > 0; index--) {
			if (major_names[index] == NULL)
				break;
		}

		if (index == 0) {
			printk("%s: failed to get major for %s\n",
			       __func__, name);
			ret = -EBUSY;
			goto out;
		}
		major = index;
		ret = major;
	}

	if (major >= BLKDEV_MAJOR_MAX) {
		pr_err("%s: major requested (%u) is greater than the maximum (%u) for %s\n",
		       __func__, major, BLKDEV_MAJOR_MAX-1, name);

		ret = -EINVAL;
		goto out;
	}

	p = kmalloc(sizeof(struct blk_major_name), GFP_KERNEL);
	if (p == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	p->major = major;
	p->probe = probe;
	strlcpy(p->name, name, sizeof(p->name));
	p->next = NULL;
	index = major_to_index(major);

	spin_lock(&major_names_spinlock);
	for (n = &major_names[index]; *n; n = &(*n)->next) {
		if ((*n)->major == major)
			break;
	}
	if (!*n)
		*n = p;
	else
		ret = -EBUSY;
	spin_unlock(&major_names_spinlock);

	if (ret < 0) {
		printk("register_blkdev: cannot get major %u for %s\n",
		       major, name);
		kfree(p);
	}
out:
	mutex_unlock(&major_names_lock);
	return ret;
}
EXPORT_SYMBOL(__register_blkdev);

void unregister_blkdev(unsigned int major, const char *name)
{
	struct blk_major_name **n;
	struct blk_major_name *p = NULL;
	int index = major_to_index(major);

	mutex_lock(&major_names_lock);
	spin_lock(&major_names_spinlock);
	for (n = &major_names[index]; *n; n = &(*n)->next)
		if ((*n)->major == major)
			break;
	if (!*n || strcmp((*n)->name, name)) {
		WARN_ON(1);
	} else {
		p = *n;
		*n = p->next;
	}
	spin_unlock(&major_names_spinlock);
	mutex_unlock(&major_names_lock);
	kfree(p);
}

EXPORT_SYMBOL(unregister_blkdev);

int blk_alloc_ext_minor(void)
{
	int idx;

	idx = ida_alloc_range(&ext_devt_ida, 0, NR_EXT_DEVT - 1, GFP_KERNEL);
	if (idx == -ENOSPC)
		return -EBUSY;
	return idx;
}

void blk_free_ext_minor(unsigned int minor)
{
	ida_free(&ext_devt_ida, minor);
}

static char *bdevt_str(dev_t devt, char *buf)
{
	if (MAJOR(devt) <= 0xff && MINOR(devt) <= 0xff) {
		char tbuf[BDEVT_SIZE];
		snprintf(tbuf, BDEVT_SIZE, "%02x%02x", MAJOR(devt), MINOR(devt));
		snprintf(buf, BDEVT_SIZE, "%-9s", tbuf);
	} else
		snprintf(buf, BDEVT_SIZE, "%03x:%05x", MAJOR(devt), MINOR(devt));

	return buf;
}

void disk_uevent(struct gendisk *disk, enum kobject_action action)
{
	struct block_device *part;
	unsigned long idx;

	rcu_read_lock();
	xa_for_each(&disk->part_tbl, idx, part) {
		if (bdev_is_partition(part) && !bdev_nr_sectors(part))
			continue;
		if (!kobject_get_unless_zero(&part->bd_device.kobj))
			continue;

		rcu_read_unlock();
		kobject_uevent(bdev_kobj(part), action);
		put_device(&part->bd_device);
		rcu_read_lock();
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(disk_uevent);

static void disk_scan_partitions(struct gendisk *disk)
{
	struct block_device *bdev;

	if (!get_capacity(disk) || !disk_part_scan_enabled(disk))
		return;

	set_bit(GD_NEED_PART_SCAN, &disk->state);
	bdev = blkdev_get_by_dev(disk_devt(disk), FMODE_READ, NULL);
	if (!IS_ERR(bdev))
		blkdev_put(bdev, FMODE_READ);
}

/**
 * device_add_disk - add disk information to kernel list
 * @parent: parent device for the disk
 * @disk: per-device partitioning information
 * @groups: Additional per-device sysfs groups
 *
 * This function registers the partitioning information in @disk
 * with the kernel.
 */
int device_add_disk(struct device *parent, struct gendisk *disk,
		     const struct attribute_group **groups)

{
	struct device *ddev = disk_to_dev(disk);
	int ret;

	/*
	 * The disk queue should now be all set with enough information about
	 * the device for the elevator code to pick an adequate default
	 * elevator if one is needed, that is, for devices requesting queue
	 * registration.
	 */
	elevator_init_mq(disk->queue);

	/*
	 * If the driver provides an explicit major number it also must provide
	 * the number of minors numbers supported, and those will be used to
	 * setup the gendisk.
	 * Otherwise just allocate the device numbers for both the whole device
	 * and all partitions from the extended dev_t space.
	 */
	if (disk->major) {
		if (WARN_ON(!disk->minors))
			return -EINVAL;

		if (disk->minors > DISK_MAX_PARTS) {
			pr_err("block: can't allocate more than %d partitions\n",
				DISK_MAX_PARTS);
			disk->minors = DISK_MAX_PARTS;
		}
		if (disk->first_minor + disk->minors > MINORMASK + 1)
			return -EINVAL;
	} else {
		if (WARN_ON(disk->minors))
			return -EINVAL;

		ret = blk_alloc_ext_minor();
		if (ret < 0)
			return ret;
		disk->major = BLOCK_EXT_MAJOR;
		disk->first_minor = ret;
		disk->flags |= GENHD_FL_EXT_DEVT;
	}

	/* delay uevents, until we scanned partition table */
	dev_set_uevent_suppress(ddev, 1);

	ddev->parent = parent;
	ddev->groups = groups;
	dev_set_name(ddev, "%s", disk->disk_name);
	if (!(disk->flags & GENHD_FL_HIDDEN))
		ddev->devt = MKDEV(disk->major, disk->first_minor);
	ret = device_add(ddev);
	if (ret)
		goto out_free_ext_minor;

	ret = disk_alloc_events(disk);
	if (ret)
		goto out_device_del;

	if (!sysfs_deprecated) {
		ret = sysfs_create_link(block_depr, &ddev->kobj,
					kobject_name(&ddev->kobj));
		if (ret)
			goto out_device_del;
	}

	/*
	 * avoid probable deadlock caused by allocating memory with
	 * GFP_KERNEL in runtime_resume callback of its all ancestor
	 * devices
	 */
	pm_runtime_set_memalloc_noio(ddev, true);

	ret = blk_integrity_add(disk);
	if (ret)
		goto out_del_block_link;

	disk->part0->bd_holder_dir =
		kobject_create_and_add("holders", &ddev->kobj);
	if (!disk->part0->bd_holder_dir) {
		ret = -ENOMEM;
		goto out_del_integrity;
	}
	disk->slave_dir = kobject_create_and_add("slaves", &ddev->kobj);
	if (!disk->slave_dir) {
		ret = -ENOMEM;
		goto out_put_holder_dir;
	}

	ret = bd_register_pending_holders(disk);
	if (ret < 0)
		goto out_put_slave_dir;

	ret = blk_register_queue(disk);
	if (ret)
		goto out_put_slave_dir;

	if (disk->flags & GENHD_FL_HIDDEN) {
		/*
		 * Don't let hidden disks show up in /proc/partitions,
		 * and don't bother scanning for partitions either.
		 */
		disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;
		disk->flags |= GENHD_FL_NO_PART_SCAN;
	} else {
		ret = bdi_register(disk->bdi, "%u:%u",
				   disk->major, disk->first_minor);
		if (ret)
			goto out_unregister_queue;
		bdi_set_owner(disk->bdi, ddev);
		ret = sysfs_create_link(&ddev->kobj,
					&disk->bdi->dev->kobj, "bdi");
		if (ret)
			goto out_unregister_bdi;

		bdev_add(disk->part0, ddev->devt);
		disk_scan_partitions(disk);

		/*
		 * Announce the disk and partitions after all partitions are
		 * created. (for hidden disks uevents remain suppressed forever)
		 */
		dev_set_uevent_suppress(ddev, 0);
		disk_uevent(disk, KOBJ_ADD);
	}

	disk_update_readahead(disk);
	disk_add_events(disk);
	return 0;

out_unregister_bdi:
	if (!(disk->flags & GENHD_FL_HIDDEN))
		bdi_unregister(disk->bdi);
out_unregister_queue:
	blk_unregister_queue(disk);
	rq_qos_exit(disk->queue);
out_put_slave_dir:
	kobject_put(disk->slave_dir);
out_put_holder_dir:
	kobject_put(disk->part0->bd_holder_dir);
out_del_integrity:
	blk_integrity_del(disk);
out_del_block_link:
	if (!sysfs_deprecated)
		sysfs_remove_link(block_depr, dev_name(ddev));
out_device_del:
	device_del(ddev);
out_free_ext_minor:
	if (disk->major == BLOCK_EXT_MAJOR)
		blk_free_ext_minor(disk->first_minor);
	return WARN_ON_ONCE(ret); /* keep until all callers handle errors */
}
EXPORT_SYMBOL(device_add_disk);

/**
 * blk_mark_disk_dead - mark a disk as dead
 * @disk: disk to mark as dead
 *
 * Mark as disk as dead (e.g. surprise removed) and don't accept any new I/O
 * to this disk.
 */
void blk_mark_disk_dead(struct gendisk *disk)
{
	set_bit(GD_DEAD, &disk->state);
	blk_queue_start_drain(disk->queue);
}
EXPORT_SYMBOL_GPL(blk_mark_disk_dead);

/**
 * del_gendisk - remove the gendisk
 * @disk: the struct gendisk to remove
 *
 * Removes the gendisk and all its associated resources. This deletes the
 * partitions associated with the gendisk, and unregisters the associated
 * request_queue.
 *
 * This is the counter to the respective __device_add_disk() call.
 *
 * The final removal of the struct gendisk happens when its refcount reaches 0
 * with put_disk(), which should be called after del_gendisk(), if
 * __device_add_disk() was used.
 *
 * Drivers exist which depend on the release of the gendisk to be synchronous,
 * it should not be deferred.
 *
 * Context: can sleep
 */
void del_gendisk(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;

	might_sleep();

	if (WARN_ON_ONCE(!disk_live(disk) && !(disk->flags & GENHD_FL_HIDDEN)))
		return;

	blk_integrity_del(disk);
	disk_del_events(disk);

	mutex_lock(&disk->open_mutex);
	remove_inode_hash(disk->part0->bd_inode);
	blk_drop_partitions(disk);
	mutex_unlock(&disk->open_mutex);

	fsync_bdev(disk->part0);
	__invalidate_device(disk->part0, true);

	/*
	 * Fail any new I/O.
	 */
	set_bit(GD_DEAD, &disk->state);
	set_capacity(disk, 0);

	/*
	 * Prevent new I/O from crossing bio_queue_enter().
	 */
	blk_queue_start_drain(q);

	if (!(disk->flags & GENHD_FL_HIDDEN)) {
		sysfs_remove_link(&disk_to_dev(disk)->kobj, "bdi");

		/*
		 * Unregister bdi before releasing device numbers (as they can
		 * get reused and we'd get clashes in sysfs).
		 */
		bdi_unregister(disk->bdi);
	}

	blk_unregister_queue(disk);

	kobject_put(disk->part0->bd_holder_dir);
	kobject_put(disk->slave_dir);

	part_stat_set_all(disk->part0, 0);
	disk->part0->bd_stamp = 0;
	if (!sysfs_deprecated)
		sysfs_remove_link(block_depr, dev_name(disk_to_dev(disk)));
	pm_runtime_set_memalloc_noio(disk_to_dev(disk), false);
	device_del(disk_to_dev(disk));

	blk_mq_freeze_queue_wait(q);

	rq_qos_exit(q);
	blk_sync_queue(q);
	blk_flush_integrity();
	/*
	 * Allow using passthrough request again after the queue is torn down.
	 */
	blk_queue_flag_clear(QUEUE_FLAG_INIT_DONE, q);
	__blk_mq_unfreeze_queue(q, true);

}
EXPORT_SYMBOL(del_gendisk);

/* sysfs access to bad-blocks list. */
static ssize_t disk_badblocks_show(struct device *dev,
					struct device_attribute *attr,
					char *page)
{
	struct gendisk *disk = dev_to_disk(dev);

	if (!disk->bb)
		return sprintf(page, "\n");

	return badblocks_show(disk->bb, page, 0);
}

static ssize_t disk_badblocks_store(struct device *dev,
					struct device_attribute *attr,
					const char *page, size_t len)
{
	struct gendisk *disk = dev_to_disk(dev);

	if (!disk->bb)
		return -ENXIO;

	return badblocks_store(disk->bb, page, len, 0);
}

void blk_request_module(dev_t devt)
{
	unsigned int major = MAJOR(devt);
	struct blk_major_name **n;

	mutex_lock(&major_names_lock);
	for (n = &major_names[major_to_index(major)]; *n; n = &(*n)->next) {
		if ((*n)->major == major && (*n)->probe) {
			(*n)->probe(devt);
			mutex_unlock(&major_names_lock);
			return;
		}
	}
	mutex_unlock(&major_names_lock);

	if (request_module("block-major-%d-%d", MAJOR(devt), MINOR(devt)) > 0)
		/* Make old-style 2.4 aliases work */
		request_module("block-major-%d", MAJOR(devt));
}

/*
 * print a full list of all partitions - intended for places where the root
 * filesystem can't be mounted and thus to give the victim some idea of what
 * went wrong
 */
void __init printk_all_partitions(void)
{
	struct class_dev_iter iter;
	struct device *dev;

	class_dev_iter_init(&iter, &block_class, NULL, &disk_type);
	while ((dev = class_dev_iter_next(&iter))) {
		struct gendisk *disk = dev_to_disk(dev);
		struct block_device *part;
		char devt_buf[BDEVT_SIZE];
		unsigned long idx;

		/*
		 * Don't show empty devices or things that have been
		 * suppressed
		 */
		if (get_capacity(disk) == 0 ||
		    (disk->flags & GENHD_FL_SUPPRESS_PARTITION_INFO))
			continue;

		/*
		 * Note, unlike /proc/partitions, I am showing the numbers in
		 * hex - the same format as the root= option takes.
		 */
		rcu_read_lock();
		xa_for_each(&disk->part_tbl, idx, part) {
			if (!bdev_nr_sectors(part))
				continue;
			printk("%s%s %10llu %pg %s",
			       bdev_is_partition(part) ? "  " : "",
			       bdevt_str(part->bd_dev, devt_buf),
			       bdev_nr_sectors(part) >> 1, part,
			       part->bd_meta_info ?
					part->bd_meta_info->uuid : "");
			if (bdev_is_partition(part))
				printk("\n");
			else if (dev->parent && dev->parent->driver)
				printk(" driver: %s\n",
					dev->parent->driver->name);
			else
				printk(" (driver?)\n");
		}
		rcu_read_unlock();
	}
	class_dev_iter_exit(&iter);
}

#ifdef CONFIG_PROC_FS
/* iterator */
static void *disk_seqf_start(struct seq_file *seqf, loff_t *pos)
{
	loff_t skip = *pos;
	struct class_dev_iter *iter;
	struct device *dev;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return ERR_PTR(-ENOMEM);

	seqf->private = iter;
	class_dev_iter_init(iter, &block_class, NULL, &disk_type);
	do {
		dev = class_dev_iter_next(iter);
		if (!dev)
			return NULL;
	} while (skip--);

	return dev_to_disk(dev);
}

static void *disk_seqf_next(struct seq_file *seqf, void *v, loff_t *pos)
{
	struct device *dev;

	(*pos)++;
	dev = class_dev_iter_next(seqf->private);
	if (dev)
		return dev_to_disk(dev);

	return NULL;
}

static void disk_seqf_stop(struct seq_file *seqf, void *v)
{
	struct class_dev_iter *iter = seqf->private;

	/* stop is called even after start failed :-( */
	if (iter) {
		class_dev_iter_exit(iter);
		kfree(iter);
		seqf->private = NULL;
	}
}

static void *show_partition_start(struct seq_file *seqf, loff_t *pos)
{
	void *p;

	p = disk_seqf_start(seqf, pos);
	if (!IS_ERR_OR_NULL(p) && !*pos)
		seq_puts(seqf, "major minor  #blocks  name\n\n");
	return p;
}

static int show_partition(struct seq_file *seqf, void *v)
{
	struct gendisk *sgp = v;
	struct block_device *part;
	unsigned long idx;

	/* Don't show non-partitionable removeable devices or empty devices */
	if (!get_capacity(sgp) || (!disk_max_parts(sgp) &&
				   (sgp->flags & GENHD_FL_REMOVABLE)))
		return 0;
	if (sgp->flags & GENHD_FL_SUPPRESS_PARTITION_INFO)
		return 0;

	rcu_read_lock();
	xa_for_each(&sgp->part_tbl, idx, part) {
		if (!bdev_nr_sectors(part))
			continue;
		seq_printf(seqf, "%4d  %7d %10llu %pg\n",
			   MAJOR(part->bd_dev), MINOR(part->bd_dev),
			   bdev_nr_sectors(part) >> 1, part);
	}
	rcu_read_unlock();
	return 0;
}

static const struct seq_operations partitions_op = {
	.start	= show_partition_start,
	.next	= disk_seqf_next,
	.stop	= disk_seqf_stop,
	.show	= show_partition
};
#endif

static int __init genhd_device_init(void)
{
	int error;

	block_class.dev_kobj = sysfs_dev_block_kobj;
	error = class_register(&block_class);
	if (unlikely(error))
		return error;
	blk_dev_init();

	register_blkdev(BLOCK_EXT_MAJOR, "blkext");

	/* create top-level block dir */
	if (!sysfs_deprecated)
		block_depr = kobject_create_and_add("block", NULL);
	return 0;
}

subsys_initcall(genhd_device_init);

static ssize_t disk_range_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n", disk->minors);
}

static ssize_t disk_ext_range_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n", disk_max_parts(disk));
}

static ssize_t disk_removable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n",
		       (disk->flags & GENHD_FL_REMOVABLE ? 1 : 0));
}

static ssize_t disk_hidden_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n",
		       (disk->flags & GENHD_FL_HIDDEN ? 1 : 0));
}

static ssize_t disk_ro_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n", get_disk_ro(disk) ? 1 : 0);
}

ssize_t part_size_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", bdev_nr_sectors(dev_to_bdev(dev)));
}

ssize_t part_stat_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct block_device *bdev = dev_to_bdev(dev);
	struct request_queue *q = bdev->bd_disk->queue;
	struct disk_stats stat;
	unsigned int inflight;

	part_stat_read_all(bdev, &stat);
	if (queue_is_mq(q))
		inflight = blk_mq_in_flight(q, bdev);
	else
		inflight = part_in_flight(bdev);

	return sprintf(buf,
		"%8lu %8lu %8llu %8u "
		"%8lu %8lu %8llu %8u "
		"%8u %8u %8u "
		"%8lu %8lu %8llu %8u "
		"%8lu %8u"
		"\n",
		stat.ios[STAT_READ],
		stat.merges[STAT_READ],
		(unsigned long long)stat.sectors[STAT_READ],
		(unsigned int)div_u64(stat.nsecs[STAT_READ], NSEC_PER_MSEC),
		stat.ios[STAT_WRITE],
		stat.merges[STAT_WRITE],
		(unsigned long long)stat.sectors[STAT_WRITE],
		(unsigned int)div_u64(stat.nsecs[STAT_WRITE], NSEC_PER_MSEC),
		inflight,
		jiffies_to_msecs(stat.io_ticks),
		(unsigned int)div_u64(stat.nsecs[STAT_READ] +
				      stat.nsecs[STAT_WRITE] +
				      stat.nsecs[STAT_DISCARD] +
				      stat.nsecs[STAT_FLUSH],
						NSEC_PER_MSEC),
		stat.ios[STAT_DISCARD],
		stat.merges[STAT_DISCARD],
		(unsigned long long)stat.sectors[STAT_DISCARD],
		(unsigned int)div_u64(stat.nsecs[STAT_DISCARD], NSEC_PER_MSEC),
		stat.ios[STAT_FLUSH],
		(unsigned int)div_u64(stat.nsecs[STAT_FLUSH], NSEC_PER_MSEC));
}

ssize_t part_inflight_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct block_device *bdev = dev_to_bdev(dev);
	struct request_queue *q = bdev->bd_disk->queue;
	unsigned int inflight[2];

	if (queue_is_mq(q))
		blk_mq_in_flight_rw(q, bdev, inflight);
	else
		part_in_flight_rw(bdev, inflight);

	return sprintf(buf, "%8u %8u\n", inflight[0], inflight[1]);
}

static ssize_t disk_capability_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%x\n", disk->flags);
}

static ssize_t disk_alignment_offset_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n", queue_alignment_offset(disk->queue));
}

static ssize_t disk_discard_alignment_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n", queue_discard_alignment(disk->queue));
}

static ssize_t diskseq_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%llu\n", disk->diskseq);
}

static DEVICE_ATTR(range, 0444, disk_range_show, NULL);
static DEVICE_ATTR(ext_range, 0444, disk_ext_range_show, NULL);
static DEVICE_ATTR(removable, 0444, disk_removable_show, NULL);
static DEVICE_ATTR(hidden, 0444, disk_hidden_show, NULL);
static DEVICE_ATTR(ro, 0444, disk_ro_show, NULL);
static DEVICE_ATTR(size, 0444, part_size_show, NULL);
static DEVICE_ATTR(alignment_offset, 0444, disk_alignment_offset_show, NULL);
static DEVICE_ATTR(discard_alignment, 0444, disk_discard_alignment_show, NULL);
static DEVICE_ATTR(capability, 0444, disk_capability_show, NULL);
static DEVICE_ATTR(stat, 0444, part_stat_show, NULL);
static DEVICE_ATTR(inflight, 0444, part_inflight_show, NULL);
static DEVICE_ATTR(badblocks, 0644, disk_badblocks_show, disk_badblocks_store);
static DEVICE_ATTR(diskseq, 0444, diskseq_show, NULL);

#ifdef CONFIG_FAIL_MAKE_REQUEST
ssize_t part_fail_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dev_to_bdev(dev)->bd_make_it_fail);
}

ssize_t part_fail_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	int i;

	if (count > 0 && sscanf(buf, "%d", &i) > 0)
		dev_to_bdev(dev)->bd_make_it_fail = i;

	return count;
}

static struct device_attribute dev_attr_fail =
	__ATTR(make-it-fail, 0644, part_fail_show, part_fail_store);
#endif /* CONFIG_FAIL_MAKE_REQUEST */

#ifdef CONFIG_FAIL_IO_TIMEOUT
static struct device_attribute dev_attr_fail_timeout =
	__ATTR(io-timeout-fail, 0644, part_timeout_show, part_timeout_store);
#endif

static struct attribute *disk_attrs[] = {
	&dev_attr_range.attr,
	&dev_attr_ext_range.attr,
	&dev_attr_removable.attr,
	&dev_attr_hidden.attr,
	&dev_attr_ro.attr,
	&dev_attr_size.attr,
	&dev_attr_alignment_offset.attr,
	&dev_attr_discard_alignment.attr,
	&dev_attr_capability.attr,
	&dev_attr_stat.attr,
	&dev_attr_inflight.attr,
	&dev_attr_badblocks.attr,
	&dev_attr_events.attr,
	&dev_attr_events_async.attr,
	&dev_attr_events_poll_msecs.attr,
	&dev_attr_diskseq.attr,
#ifdef CONFIG_FAIL_MAKE_REQUEST
	&dev_attr_fail.attr,
#endif
#ifdef CONFIG_FAIL_IO_TIMEOUT
	&dev_attr_fail_timeout.attr,
#endif
	NULL
};

static umode_t disk_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, typeof(*dev), kobj);
	struct gendisk *disk = dev_to_disk(dev);

	if (a == &dev_attr_badblocks.attr && !disk->bb)
		return 0;
	return a->mode;
}

static struct attribute_group disk_attr_group = {
	.attrs = disk_attrs,
	.is_visible = disk_visible,
};

static const struct attribute_group *disk_attr_groups[] = {
	&disk_attr_group,
	NULL
};

/**
 * disk_release - releases all allocated resources of the gendisk
 * @dev: the device representing this disk
 *
 * This function releases all allocated resources of the gendisk.
 *
 * Drivers which used __device_add_disk() have a gendisk with a request_queue
 * assigned. Since the request_queue sits on top of the gendisk for these
 * drivers we also call blk_put_queue() for them, and we expect the
 * request_queue refcount to reach 0 at this point, and so the request_queue
 * will also be freed prior to the disk.
 *
 * Context: can sleep
 */
static void disk_release(struct device *dev)
{
	struct gendisk *disk = dev_to_disk(dev);

	might_sleep();
	WARN_ON_ONCE(disk_live(disk));

	blk_mq_cancel_work_sync(disk->queue);

	disk_release_events(disk);
	kfree(disk->random);
	xa_destroy(&disk->part_tbl);
	disk->queue->disk = NULL;
	blk_put_queue(disk->queue);
	iput(disk->part0->bd_inode);	/* frees the disk */
}

static int block_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct gendisk *disk = dev_to_disk(dev);

	return add_uevent_var(env, "DISKSEQ=%llu", disk->diskseq);
}

struct class block_class = {
	.name		= "block",
	.dev_uevent	= block_uevent,
};

static char *block_devnode(struct device *dev, umode_t *mode,
			   kuid_t *uid, kgid_t *gid)
{
	struct gendisk *disk = dev_to_disk(dev);

	if (disk->fops->devnode)
		return disk->fops->devnode(disk, mode);
	return NULL;
}

const struct device_type disk_type = {
	.name		= "disk",
	.groups		= disk_attr_groups,
	.release	= disk_release,
	.devnode	= block_devnode,
};

#ifdef CONFIG_PROC_FS
/*
 * aggregate disk stat collector.  Uses the same stats that the sysfs
 * entries do, above, but makes them available through one seq_file.
 *
 * The output looks suspiciously like /proc/partitions with a bunch of
 * extra fields.
 */
static int diskstats_show(struct seq_file *seqf, void *v)
{
	struct gendisk *gp = v;
	struct block_device *hd;
	unsigned int inflight;
	struct disk_stats stat;
	unsigned long idx;

	/*
	if (&disk_to_dev(gp)->kobj.entry == block_class.devices.next)
		seq_puts(seqf,	"major minor name"
				"     rio rmerge rsect ruse wio wmerge "
				"wsect wuse running use aveq"
				"\n\n");
	*/

	rcu_read_lock();
	xa_for_each(&gp->part_tbl, idx, hd) {
		if (bdev_is_partition(hd) && !bdev_nr_sectors(hd))
			continue;
		part_stat_read_all(hd, &stat);
		if (queue_is_mq(gp->queue))
			inflight = blk_mq_in_flight(gp->queue, hd);
		else
			inflight = part_in_flight(hd);

		seq_printf(seqf, "%4d %7d %pg "
			   "%lu %lu %lu %u "
			   "%lu %lu %lu %u "
			   "%u %u %u "
			   "%lu %lu %lu %u "
			   "%lu %u"
			   "\n",
			   MAJOR(hd->bd_dev), MINOR(hd->bd_dev), hd,
			   stat.ios[STAT_READ],
			   stat.merges[STAT_READ],
			   stat.sectors[STAT_READ],
			   (unsigned int)div_u64(stat.nsecs[STAT_READ],
							NSEC_PER_MSEC),
			   stat.ios[STAT_WRITE],
			   stat.merges[STAT_WRITE],
			   stat.sectors[STAT_WRITE],
			   (unsigned int)div_u64(stat.nsecs[STAT_WRITE],
							NSEC_PER_MSEC),
			   inflight,
			   jiffies_to_msecs(stat.io_ticks),
			   (unsigned int)div_u64(stat.nsecs[STAT_READ] +
						 stat.nsecs[STAT_WRITE] +
						 stat.nsecs[STAT_DISCARD] +
						 stat.nsecs[STAT_FLUSH],
							NSEC_PER_MSEC),
			   stat.ios[STAT_DISCARD],
			   stat.merges[STAT_DISCARD],
			   stat.sectors[STAT_DISCARD],
			   (unsigned int)div_u64(stat.nsecs[STAT_DISCARD],
						 NSEC_PER_MSEC),
			   stat.ios[STAT_FLUSH],
			   (unsigned int)div_u64(stat.nsecs[STAT_FLUSH],
						 NSEC_PER_MSEC)
			);
	}
	rcu_read_unlock();

	return 0;
}

static const struct seq_operations diskstats_op = {
	.start	= disk_seqf_start,
	.next	= disk_seqf_next,
	.stop	= disk_seqf_stop,
	.show	= diskstats_show
};

static int __init proc_genhd_init(void)
{
	proc_create_seq("diskstats", 0, NULL, &diskstats_op);
	proc_create_seq("partitions", 0, NULL, &partitions_op);
	return 0;
}
module_init(proc_genhd_init);
#endif /* CONFIG_PROC_FS */

dev_t part_devt(struct gendisk *disk, u8 partno)
{
	struct block_device *part;
	dev_t devt = 0;

	rcu_read_lock();
	part = xa_load(&disk->part_tbl, partno);
	if (part)
		devt = part->bd_dev;
	rcu_read_unlock();

	return devt;
}

dev_t blk_lookup_devt(const char *name, int partno)
{
	dev_t devt = MKDEV(0, 0);
	struct class_dev_iter iter;
	struct device *dev;

	class_dev_iter_init(&iter, &block_class, NULL, &disk_type);
	while ((dev = class_dev_iter_next(&iter))) {
		struct gendisk *disk = dev_to_disk(dev);

		if (strcmp(dev_name(dev), name))
			continue;

		if (partno < disk->minors) {
			/* We need to return the right devno, even
			 * if the partition doesn't exist yet.
			 */
			devt = MKDEV(MAJOR(dev->devt),
				     MINOR(dev->devt) + partno);
		} else {
			devt = part_devt(disk, partno);
			if (devt)
				break;
		}
	}
	class_dev_iter_exit(&iter);
	return devt;
}

struct gendisk *__alloc_disk_node(struct request_queue *q, int node_id,
		struct lock_class_key *lkclass)
{
	struct gendisk *disk;

	if (!blk_get_queue(q))
		return NULL;

	disk = kzalloc_node(sizeof(struct gendisk), GFP_KERNEL, node_id);
	if (!disk)
		goto out_put_queue;

	disk->bdi = bdi_alloc(node_id);
	if (!disk->bdi)
		goto out_free_disk;

	disk->part0 = bdev_alloc(disk, 0);
	if (!disk->part0)
		goto out_free_bdi;

	disk->node_id = node_id;
	mutex_init(&disk->open_mutex);
	xa_init(&disk->part_tbl);
	if (xa_insert(&disk->part_tbl, 0, disk->part0, GFP_KERNEL))
		goto out_destroy_part_tbl;

	rand_initialize_disk(disk);
	disk_to_dev(disk)->class = &block_class;
	disk_to_dev(disk)->type = &disk_type;
	device_initialize(disk_to_dev(disk));
	inc_diskseq(disk);
	disk->queue = q;
	q->disk = disk;
	lockdep_init_map(&disk->lockdep_map, "(bio completion)", lkclass, 0);
#ifdef CONFIG_BLOCK_HOLDER_DEPRECATED
	INIT_LIST_HEAD(&disk->slave_bdevs);
#endif
	return disk;

out_destroy_part_tbl:
	xa_destroy(&disk->part_tbl);
	disk->part0->bd_disk = NULL;
	iput(disk->part0->bd_inode);
out_free_bdi:
	bdi_put(disk->bdi);
out_free_disk:
	kfree(disk);
out_put_queue:
	blk_put_queue(q);
	return NULL;
}
EXPORT_SYMBOL(__alloc_disk_node);

struct gendisk *__blk_alloc_disk(int node, struct lock_class_key *lkclass)
{
	struct request_queue *q;
	struct gendisk *disk;

	q = blk_alloc_queue(node);
	if (!q)
		return NULL;

	disk = __alloc_disk_node(q, node, lkclass);
	if (!disk) {
		blk_cleanup_queue(q);
		return NULL;
	}
	return disk;
}
EXPORT_SYMBOL(__blk_alloc_disk);

/**
 * put_disk - decrements the gendisk refcount
 * @disk: the struct gendisk to decrement the refcount for
 *
 * This decrements the refcount for the struct gendisk. When this reaches 0
 * we'll have disk_release() called.
 *
 * Context: Any context, but the last reference must not be dropped from
 *          atomic context.
 */
void put_disk(struct gendisk *disk)
{
	if (disk)
		put_device(disk_to_dev(disk));
}
EXPORT_SYMBOL(put_disk);

/**
 * blk_cleanup_disk - shutdown a gendisk allocated by blk_alloc_disk
 * @disk: gendisk to shutdown
 *
 * Mark the queue hanging off @disk DYING, drain all pending requests, then mark
 * the queue DEAD, destroy and put it and the gendisk structure.
 *
 * Context: can sleep
 */
void blk_cleanup_disk(struct gendisk *disk)
{
	blk_cleanup_queue(disk->queue);
	put_disk(disk);
}
EXPORT_SYMBOL(blk_cleanup_disk);

static void set_disk_ro_uevent(struct gendisk *gd, int ro)
{
	char event[] = "DISK_RO=1";
	char *envp[] = { event, NULL };

	if (!ro)
		event[8] = '0';
	kobject_uevent_env(&disk_to_dev(gd)->kobj, KOBJ_CHANGE, envp);
}

/**
 * set_disk_ro - set a gendisk read-only
 * @disk:	gendisk to operate on
 * @read_only:	%true to set the disk read-only, %false set the disk read/write
 *
 * This function is used to indicate whether a given disk device should have its
 * read-only flag set. set_disk_ro() is typically used by device drivers to
 * indicate whether the underlying physical device is write-protected.
 */
void set_disk_ro(struct gendisk *disk, bool read_only)
{
	if (read_only) {
		if (test_and_set_bit(GD_READ_ONLY, &disk->state))
			return;
	} else {
		if (!test_and_clear_bit(GD_READ_ONLY, &disk->state))
			return;
	}
	set_disk_ro_uevent(disk, read_only);
}
EXPORT_SYMBOL(set_disk_ro);

int bdev_read_only(struct block_device *bdev)
{
	return bdev->bd_read_only || get_disk_ro(bdev->bd_disk);
}
EXPORT_SYMBOL(bdev_read_only);

void inc_diskseq(struct gendisk *disk)
{
	disk->diskseq = atomic64_inc_return(&diskseq);
}
