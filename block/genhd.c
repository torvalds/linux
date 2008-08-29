/*
 *  gendisk handling
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/kobj_map.h>
#include <linux/buffer_head.h>
#include <linux/mutex.h>
#include <linux/idr.h>

#include "blk.h"

static DEFINE_MUTEX(block_class_lock);
#ifndef CONFIG_SYSFS_DEPRECATED
struct kobject *block_depr;
#endif

/* for extended dynamic devt allocation, currently only one major is used */
#define MAX_EXT_DEVT		(1 << MINORBITS)

/* For extended devt allocation.  ext_devt_mutex prevents look up
 * results from going away underneath its user.
 */
static DEFINE_MUTEX(ext_devt_mutex);
static DEFINE_IDR(ext_devt_idr);

static struct device_type disk_type;

/**
 * disk_get_part - get partition
 * @disk: disk to look partition from
 * @partno: partition number
 *
 * Look for partition @partno from @disk.  If found, increment
 * reference count and return it.
 *
 * CONTEXT:
 * Don't care.
 *
 * RETURNS:
 * Pointer to the found partition on success, NULL if not found.
 */
struct hd_struct *disk_get_part(struct gendisk *disk, int partno)
{
	struct hd_struct *part;

	if (unlikely(partno < 0 || partno >= disk_max_parts(disk)))
		return NULL;
	rcu_read_lock();
	part = rcu_dereference(disk->__part[partno]);
	if (part)
		get_device(part_to_dev(part));
	rcu_read_unlock();

	return part;
}
EXPORT_SYMBOL_GPL(disk_get_part);

/**
 * disk_part_iter_init - initialize partition iterator
 * @piter: iterator to initialize
 * @disk: disk to iterate over
 * @flags: DISK_PITER_* flags
 *
 * Initialize @piter so that it iterates over partitions of @disk.
 *
 * CONTEXT:
 * Don't care.
 */
void disk_part_iter_init(struct disk_part_iter *piter, struct gendisk *disk,
			  unsigned int flags)
{
	piter->disk = disk;
	piter->part = NULL;

	if (flags & DISK_PITER_REVERSE)
		piter->idx = disk_max_parts(piter->disk) - 1;
	else if (flags & DISK_PITER_INCL_PART0)
		piter->idx = 0;
	else
		piter->idx = 1;

	piter->flags = flags;
}
EXPORT_SYMBOL_GPL(disk_part_iter_init);

/**
 * disk_part_iter_next - proceed iterator to the next partition and return it
 * @piter: iterator of interest
 *
 * Proceed @piter to the next partition and return it.
 *
 * CONTEXT:
 * Don't care.
 */
struct hd_struct *disk_part_iter_next(struct disk_part_iter *piter)
{
	int inc, end;

	/* put the last partition */
	disk_put_part(piter->part);
	piter->part = NULL;

	rcu_read_lock();

	/* determine iteration parameters */
	if (piter->flags & DISK_PITER_REVERSE) {
		inc = -1;
		if (piter->flags & DISK_PITER_INCL_PART0)
			end = -1;
		else
			end = 0;
	} else {
		inc = 1;
		end = disk_max_parts(piter->disk);
	}

	/* iterate to the next partition */
	for (; piter->idx != end; piter->idx += inc) {
		struct hd_struct *part;

		part = rcu_dereference(piter->disk->__part[piter->idx]);
		if (!part)
			continue;
		if (!(piter->flags & DISK_PITER_INCL_EMPTY) && !part->nr_sects)
			continue;

		get_device(part_to_dev(part));
		piter->part = part;
		piter->idx += inc;
		break;
	}

	rcu_read_unlock();

	return piter->part;
}
EXPORT_SYMBOL_GPL(disk_part_iter_next);

/**
 * disk_part_iter_exit - finish up partition iteration
 * @piter: iter of interest
 *
 * Called when iteration is over.  Cleans up @piter.
 *
 * CONTEXT:
 * Don't care.
 */
void disk_part_iter_exit(struct disk_part_iter *piter)
{
	disk_put_part(piter->part);
	piter->part = NULL;
}
EXPORT_SYMBOL_GPL(disk_part_iter_exit);

/**
 * disk_map_sector_rcu - map sector to partition
 * @disk: gendisk of interest
 * @sector: sector to map
 *
 * Find out which partition @sector maps to on @disk.  This is
 * primarily used for stats accounting.
 *
 * CONTEXT:
 * RCU read locked.  The returned partition pointer is valid only
 * while preemption is disabled.
 *
 * RETURNS:
 * Found partition on success, NULL if there's no matching partition.
 */
struct hd_struct *disk_map_sector_rcu(struct gendisk *disk, sector_t sector)
{
	int i;

	for (i = 1; i < disk_max_parts(disk); i++) {
		struct hd_struct *part = rcu_dereference(disk->__part[i]);

		if (part && part->start_sect <= sector &&
		    sector < part->start_sect + part->nr_sects)
			return part;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(disk_map_sector_rcu);

/*
 * Can be deleted altogether. Later.
 *
 */
static struct blk_major_name {
	struct blk_major_name *next;
	int major;
	char name[16];
} *major_names[BLKDEV_MAJOR_HASH_SIZE];

/* index in the above - for now: assume no multimajor ranges */
static inline int major_to_index(int major)
{
	return major % BLKDEV_MAJOR_HASH_SIZE;
}

#ifdef CONFIG_PROC_FS
void blkdev_show(struct seq_file *seqf, off_t offset)
{
	struct blk_major_name *dp;

	if (offset < BLKDEV_MAJOR_HASH_SIZE) {
		mutex_lock(&block_class_lock);
		for (dp = major_names[offset]; dp; dp = dp->next)
			seq_printf(seqf, "%3d %s\n", dp->major, dp->name);
		mutex_unlock(&block_class_lock);
	}
}
#endif /* CONFIG_PROC_FS */

int register_blkdev(unsigned int major, const char *name)
{
	struct blk_major_name **n, *p;
	int index, ret = 0;

	mutex_lock(&block_class_lock);

	/* temporary */
	if (major == 0) {
		for (index = ARRAY_SIZE(major_names)-1; index > 0; index--) {
			if (major_names[index] == NULL)
				break;
		}

		if (index == 0) {
			printk("register_blkdev: failed to get major for %s\n",
			       name);
			ret = -EBUSY;
			goto out;
		}
		major = index;
		ret = major;
	}

	p = kmalloc(sizeof(struct blk_major_name), GFP_KERNEL);
	if (p == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	p->major = major;
	strlcpy(p->name, name, sizeof(p->name));
	p->next = NULL;
	index = major_to_index(major);

	for (n = &major_names[index]; *n; n = &(*n)->next) {
		if ((*n)->major == major)
			break;
	}
	if (!*n)
		*n = p;
	else
		ret = -EBUSY;

	if (ret < 0) {
		printk("register_blkdev: cannot get major %d for %s\n",
		       major, name);
		kfree(p);
	}
out:
	mutex_unlock(&block_class_lock);
	return ret;
}

EXPORT_SYMBOL(register_blkdev);

void unregister_blkdev(unsigned int major, const char *name)
{
	struct blk_major_name **n;
	struct blk_major_name *p = NULL;
	int index = major_to_index(major);

	mutex_lock(&block_class_lock);
	for (n = &major_names[index]; *n; n = &(*n)->next)
		if ((*n)->major == major)
			break;
	if (!*n || strcmp((*n)->name, name)) {
		WARN_ON(1);
	} else {
		p = *n;
		*n = p->next;
	}
	mutex_unlock(&block_class_lock);
	kfree(p);
}

EXPORT_SYMBOL(unregister_blkdev);

static struct kobj_map *bdev_map;

/**
 * blk_mangle_minor - scatter minor numbers apart
 * @minor: minor number to mangle
 *
 * Scatter consecutively allocated @minor number apart if MANGLE_DEVT
 * is enabled.  Mangling twice gives the original value.
 *
 * RETURNS:
 * Mangled value.
 *
 * CONTEXT:
 * Don't care.
 */
static int blk_mangle_minor(int minor)
{
#ifdef CONFIG_DEBUG_BLOCK_EXT_DEVT
	int i;

	for (i = 0; i < MINORBITS / 2; i++) {
		int low = minor & (1 << i);
		int high = minor & (1 << (MINORBITS - 1 - i));
		int distance = MINORBITS - 1 - 2 * i;

		minor ^= low | high;	/* clear both bits */
		low <<= distance;	/* swap the positions */
		high >>= distance;
		minor |= low | high;	/* and set */
	}
#endif
	return minor;
}

/**
 * blk_alloc_devt - allocate a dev_t for a partition
 * @part: partition to allocate dev_t for
 * @gfp_mask: memory allocation flag
 * @devt: out parameter for resulting dev_t
 *
 * Allocate a dev_t for block device.
 *
 * RETURNS:
 * 0 on success, allocated dev_t is returned in *@devt.  -errno on
 * failure.
 *
 * CONTEXT:
 * Might sleep.
 */
int blk_alloc_devt(struct hd_struct *part, dev_t *devt)
{
	struct gendisk *disk = part_to_disk(part);
	int idx, rc;

	/* in consecutive minor range? */
	if (part->partno < disk->minors) {
		*devt = MKDEV(disk->major, disk->first_minor + part->partno);
		return 0;
	}

	/* allocate ext devt */
	do {
		if (!idr_pre_get(&ext_devt_idr, GFP_KERNEL))
			return -ENOMEM;
		rc = idr_get_new(&ext_devt_idr, part, &idx);
	} while (rc == -EAGAIN);

	if (rc)
		return rc;

	if (idx > MAX_EXT_DEVT) {
		idr_remove(&ext_devt_idr, idx);
		return -EBUSY;
	}

	*devt = MKDEV(BLOCK_EXT_MAJOR, blk_mangle_minor(idx));
	return 0;
}

/**
 * blk_free_devt - free a dev_t
 * @devt: dev_t to free
 *
 * Free @devt which was allocated using blk_alloc_devt().
 *
 * CONTEXT:
 * Might sleep.
 */
void blk_free_devt(dev_t devt)
{
	might_sleep();

	if (devt == MKDEV(0, 0))
		return;

	if (MAJOR(devt) == BLOCK_EXT_MAJOR) {
		mutex_lock(&ext_devt_mutex);
		idr_remove(&ext_devt_idr, blk_mangle_minor(MINOR(devt)));
		mutex_unlock(&ext_devt_mutex);
	}
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

/*
 * Register device numbers dev..(dev+range-1)
 * range must be nonzero
 * The hash chain is sorted on range, so that subranges can override.
 */
void blk_register_region(dev_t devt, unsigned long range, struct module *module,
			 struct kobject *(*probe)(dev_t, int *, void *),
			 int (*lock)(dev_t, void *), void *data)
{
	kobj_map(bdev_map, devt, range, module, probe, lock, data);
}

EXPORT_SYMBOL(blk_register_region);

void blk_unregister_region(dev_t devt, unsigned long range)
{
	kobj_unmap(bdev_map, devt, range);
}

EXPORT_SYMBOL(blk_unregister_region);

static struct kobject *exact_match(dev_t devt, int *partno, void *data)
{
	struct gendisk *p = data;

	return &disk_to_dev(p)->kobj;
}

static int exact_lock(dev_t devt, void *data)
{
	struct gendisk *p = data;

	if (!get_disk(p))
		return -1;
	return 0;
}

/**
 * add_disk - add partitioning information to kernel list
 * @disk: per-device partitioning information
 *
 * This function registers the partitioning information in @disk
 * with the kernel.
 */
void add_disk(struct gendisk *disk)
{
	struct backing_dev_info *bdi;
	int retval;

	disk->flags |= GENHD_FL_UP;
	disk_to_dev(disk)->devt = MKDEV(disk->major, disk->first_minor);
	blk_register_region(disk_devt(disk), disk->minors, NULL,
			    exact_match, exact_lock, disk);
	register_disk(disk);
	blk_register_queue(disk);

	bdi = &disk->queue->backing_dev_info;
	bdi_register_dev(bdi, disk_devt(disk));
	retval = sysfs_create_link(&disk_to_dev(disk)->kobj, &bdi->dev->kobj,
				   "bdi");
	WARN_ON(retval);
}

EXPORT_SYMBOL(add_disk);
EXPORT_SYMBOL(del_gendisk);	/* in partitions/check.c */

void unlink_gendisk(struct gendisk *disk)
{
	sysfs_remove_link(&disk_to_dev(disk)->kobj, "bdi");
	bdi_unregister(&disk->queue->backing_dev_info);
	blk_unregister_queue(disk);
	blk_unregister_region(disk_devt(disk), disk->minors);
}

/**
 * get_gendisk - get partitioning information for a given device
 * @devt: device to get partitioning information for
 * @part: returned partition index
 *
 * This function gets the structure containing partitioning
 * information for the given device @devt.
 */
struct gendisk *get_gendisk(dev_t devt, int *partno)
{
	struct gendisk *disk = NULL;

	if (MAJOR(devt) != BLOCK_EXT_MAJOR) {
		struct kobject *kobj;

		kobj = kobj_lookup(bdev_map, devt, partno);
		if (kobj)
			disk = dev_to_disk(kobj_to_dev(kobj));
	} else {
		struct hd_struct *part;

		mutex_lock(&ext_devt_mutex);
		part = idr_find(&ext_devt_idr, blk_mangle_minor(MINOR(devt)));
		if (part && get_disk(part_to_disk(part))) {
			*partno = part->partno;
			disk = part_to_disk(part);
		}
		mutex_unlock(&ext_devt_mutex);
	}

	return disk;
}

/**
 * bdget_disk - do bdget() by gendisk and partition number
 * @disk: gendisk of interest
 * @partno: partition number
 *
 * Find partition @partno from @disk, do bdget() on it.
 *
 * CONTEXT:
 * Don't care.
 *
 * RETURNS:
 * Resulting block_device on success, NULL on failure.
 */
extern struct block_device *bdget_disk(struct gendisk *disk, int partno)
{
	struct hd_struct *part;
	struct block_device *bdev = NULL;

	part = disk_get_part(disk, partno);
	if (part && (part->nr_sects || partno == 0))
		bdev = bdget(part_devt(part));
	disk_put_part(part);

	return bdev;
}
EXPORT_SYMBOL(bdget_disk);

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
		struct disk_part_iter piter;
		struct hd_struct *part;
		char name_buf[BDEVNAME_SIZE];
		char devt_buf[BDEVT_SIZE];

		/*
		 * Don't show empty devices or things that have been
		 * surpressed
		 */
		if (get_capacity(disk) == 0 ||
		    (disk->flags & GENHD_FL_SUPPRESS_PARTITION_INFO))
			continue;

		/*
		 * Note, unlike /proc/partitions, I am showing the
		 * numbers in hex - the same format as the root=
		 * option takes.
		 */
		printk("%s %10llu %s",
		       bdevt_str(disk_devt(disk), devt_buf),
		       (unsigned long long)get_capacity(disk) >> 1,
		       disk_name(disk, 0, name_buf));
		if (disk->driverfs_dev != NULL &&
		    disk->driverfs_dev->driver != NULL)
			printk(" driver: %s\n",
			       disk->driverfs_dev->driver->name);
		else
			printk(" (driver?)\n");

		/* now show the partitions */
		disk_part_iter_init(&piter, disk, 0);
		while ((part = disk_part_iter_next(&piter)))
			printk("  %s %10llu %s\n",
			       bdevt_str(part_devt(part), devt_buf),
			       (unsigned long long)part->nr_sects >> 1,
			       disk_name(disk, part->partno, name_buf));
		disk_part_iter_exit(&piter);
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

	iter = kmalloc(GFP_KERNEL, sizeof(*iter));
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
	}
}

static void *show_partition_start(struct seq_file *seqf, loff_t *pos)
{
	static void *p;

	p = disk_seqf_start(seqf, pos);
	if (!IS_ERR(p) && p)
		seq_puts(seqf, "major minor  #blocks  name\n\n");
	return p;
}

static int show_partition(struct seq_file *seqf, void *v)
{
	struct gendisk *sgp = v;
	struct disk_part_iter piter;
	struct hd_struct *part;
	char buf[BDEVNAME_SIZE];

	/* Don't show non-partitionable removeable devices or empty devices */
	if (!get_capacity(sgp) || (!disk_partitionable(sgp) &&
				   (sgp->flags & GENHD_FL_REMOVABLE)))
		return 0;
	if (sgp->flags & GENHD_FL_SUPPRESS_PARTITION_INFO)
		return 0;

	/* show the full disk and all non-0 size partitions of it */
	seq_printf(seqf, "%4d  %7d %10llu %s\n",
		MAJOR(disk_devt(sgp)), MINOR(disk_devt(sgp)),
		(unsigned long long)get_capacity(sgp) >> 1,
		disk_name(sgp, 0, buf));

	disk_part_iter_init(&piter, sgp, 0);
	while ((part = disk_part_iter_next(&piter)))
		seq_printf(seqf, "%4d  %7d %10llu %s\n",
			   MAJOR(part_devt(part)), MINOR(part_devt(part)),
			   (unsigned long long)part->nr_sects >> 1,
			   disk_name(sgp, part->partno, buf));
	disk_part_iter_exit(&piter);

	return 0;
}

const struct seq_operations partitions_op = {
	.start	= show_partition_start,
	.next	= disk_seqf_next,
	.stop	= disk_seqf_stop,
	.show	= show_partition
};
#endif


static struct kobject *base_probe(dev_t devt, int *partno, void *data)
{
	if (request_module("block-major-%d-%d", MAJOR(devt), MINOR(devt)) > 0)
		/* Make old-style 2.4 aliases work */
		request_module("block-major-%d", MAJOR(devt));
	return NULL;
}

static int __init genhd_device_init(void)
{
	int error;

	block_class.dev_kobj = sysfs_dev_block_kobj;
	error = class_register(&block_class);
	if (unlikely(error))
		return error;
	bdev_map = kobj_map_init(base_probe, &block_class_lock);
	blk_dev_init();

#ifndef CONFIG_SYSFS_DEPRECATED
	/* create top-level block dir */
	block_depr = kobject_create_and_add("block", NULL);
#endif
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

static ssize_t disk_ro_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n", disk->policy ? 1 : 0);
}

static ssize_t disk_size_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%llu\n", (unsigned long long)get_capacity(disk));
}

static ssize_t disk_capability_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%x\n", disk->flags);
}

static ssize_t disk_stat_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	int cpu;

	cpu = disk_stat_lock();
	disk_round_stats(cpu, disk);
	disk_stat_unlock();
	return sprintf(buf,
		"%8lu %8lu %8llu %8u "
		"%8lu %8lu %8llu %8u "
		"%8u %8u %8u"
		"\n",
		disk_stat_read(disk, ios[READ]),
		disk_stat_read(disk, merges[READ]),
		(unsigned long long)disk_stat_read(disk, sectors[READ]),
		jiffies_to_msecs(disk_stat_read(disk, ticks[READ])),
		disk_stat_read(disk, ios[WRITE]),
		disk_stat_read(disk, merges[WRITE]),
		(unsigned long long)disk_stat_read(disk, sectors[WRITE]),
		jiffies_to_msecs(disk_stat_read(disk, ticks[WRITE])),
		disk->in_flight,
		jiffies_to_msecs(disk_stat_read(disk, io_ticks)),
		jiffies_to_msecs(disk_stat_read(disk, time_in_queue)));
}

#ifdef CONFIG_FAIL_MAKE_REQUEST
static ssize_t disk_fail_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%d\n", disk->flags & GENHD_FL_FAIL ? 1 : 0);
}

static ssize_t disk_fail_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct gendisk *disk = dev_to_disk(dev);
	int i;

	if (count > 0 && sscanf(buf, "%d", &i) > 0) {
		if (i == 0)
			disk->flags &= ~GENHD_FL_FAIL;
		else
			disk->flags |= GENHD_FL_FAIL;
	}

	return count;
}

#endif

static DEVICE_ATTR(range, S_IRUGO, disk_range_show, NULL);
static DEVICE_ATTR(ext_range, S_IRUGO, disk_ext_range_show, NULL);
static DEVICE_ATTR(removable, S_IRUGO, disk_removable_show, NULL);
static DEVICE_ATTR(ro, S_IRUGO, disk_ro_show, NULL);
static DEVICE_ATTR(size, S_IRUGO, disk_size_show, NULL);
static DEVICE_ATTR(capability, S_IRUGO, disk_capability_show, NULL);
static DEVICE_ATTR(stat, S_IRUGO, disk_stat_show, NULL);
#ifdef CONFIG_FAIL_MAKE_REQUEST
static struct device_attribute dev_attr_fail =
	__ATTR(make-it-fail, S_IRUGO|S_IWUSR, disk_fail_show, disk_fail_store);
#endif

static struct attribute *disk_attrs[] = {
	&dev_attr_range.attr,
	&dev_attr_ext_range.attr,
	&dev_attr_removable.attr,
	&dev_attr_ro.attr,
	&dev_attr_size.attr,
	&dev_attr_capability.attr,
	&dev_attr_stat.attr,
#ifdef CONFIG_FAIL_MAKE_REQUEST
	&dev_attr_fail.attr,
#endif
	NULL
};

static struct attribute_group disk_attr_group = {
	.attrs = disk_attrs,
};

static struct attribute_group *disk_attr_groups[] = {
	&disk_attr_group,
	NULL
};

static void disk_release(struct device *dev)
{
	struct gendisk *disk = dev_to_disk(dev);

	kfree(disk->random);
	kfree(disk->__part);
	free_disk_stats(disk);
	kfree(disk);
}
struct class block_class = {
	.name		= "block",
};

static struct device_type disk_type = {
	.name		= "disk",
	.groups		= disk_attr_groups,
	.release	= disk_release,
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
	struct disk_part_iter piter;
	struct hd_struct *hd;
	char buf[BDEVNAME_SIZE];
	int cpu;

	/*
	if (&disk_to_dev(gp)->kobj.entry == block_class.devices.next)
		seq_puts(seqf,	"major minor name"
				"     rio rmerge rsect ruse wio wmerge "
				"wsect wuse running use aveq"
				"\n\n");
	*/
 
	cpu = disk_stat_lock();
	disk_round_stats(cpu, gp);
	disk_stat_unlock();
	seq_printf(seqf, "%4d %7d %s %lu %lu %llu %u %lu %lu %llu %u %u %u %u\n",
		MAJOR(disk_devt(gp)), MINOR(disk_devt(gp)),
		disk_name(gp, 0, buf),
		disk_stat_read(gp, ios[0]), disk_stat_read(gp, merges[0]),
		(unsigned long long)disk_stat_read(gp, sectors[0]),
		jiffies_to_msecs(disk_stat_read(gp, ticks[0])),
		disk_stat_read(gp, ios[1]), disk_stat_read(gp, merges[1]),
		(unsigned long long)disk_stat_read(gp, sectors[1]),
		jiffies_to_msecs(disk_stat_read(gp, ticks[1])),
		gp->in_flight,
		jiffies_to_msecs(disk_stat_read(gp, io_ticks)),
		jiffies_to_msecs(disk_stat_read(gp, time_in_queue)));

	/* now show all non-0 size partitions of it */
	disk_part_iter_init(&piter, gp, 0);
	while ((hd = disk_part_iter_next(&piter))) {
		cpu = disk_stat_lock();
		part_round_stats(cpu, hd);
		disk_stat_unlock();
		seq_printf(seqf, "%4d %7d %s %lu %lu %llu "
			   "%u %lu %lu %llu %u %u %u %u\n",
			   MAJOR(part_devt(hd)), MINOR(part_devt(hd)),
			   disk_name(gp, hd->partno, buf),
			   part_stat_read(hd, ios[0]),
			   part_stat_read(hd, merges[0]),
			   (unsigned long long)part_stat_read(hd, sectors[0]),
			   jiffies_to_msecs(part_stat_read(hd, ticks[0])),
			   part_stat_read(hd, ios[1]),
			   part_stat_read(hd, merges[1]),
			   (unsigned long long)part_stat_read(hd, sectors[1]),
			   jiffies_to_msecs(part_stat_read(hd, ticks[1])),
			   hd->in_flight,
			   jiffies_to_msecs(part_stat_read(hd, io_ticks)),
			   jiffies_to_msecs(part_stat_read(hd, time_in_queue))
			);
	}
	disk_part_iter_exit(&piter);
 
	return 0;
}

const struct seq_operations diskstats_op = {
	.start	= disk_seqf_start,
	.next	= disk_seqf_next,
	.stop	= disk_seqf_stop,
	.show	= diskstats_show
};
#endif /* CONFIG_PROC_FS */

static void media_change_notify_thread(struct work_struct *work)
{
	struct gendisk *gd = container_of(work, struct gendisk, async_notify);
	char event[] = "MEDIA_CHANGE=1";
	char *envp[] = { event, NULL };

	/*
	 * set enviroment vars to indicate which event this is for
	 * so that user space will know to go check the media status.
	 */
	kobject_uevent_env(&disk_to_dev(gd)->kobj, KOBJ_CHANGE, envp);
	put_device(gd->driverfs_dev);
}

#if 0
void genhd_media_change_notify(struct gendisk *disk)
{
	get_device(disk->driverfs_dev);
	schedule_work(&disk->async_notify);
}
EXPORT_SYMBOL_GPL(genhd_media_change_notify);
#endif  /*  0  */

dev_t blk_lookup_devt(const char *name, int partno)
{
	dev_t devt = MKDEV(0, 0);
	struct class_dev_iter iter;
	struct device *dev;

	class_dev_iter_init(&iter, &block_class, NULL, &disk_type);
	while ((dev = class_dev_iter_next(&iter))) {
		struct gendisk *disk = dev_to_disk(dev);
		struct hd_struct *part;

		if (strcmp(dev->bus_id, name))
			continue;

		part = disk_get_part(disk, partno);
		if (part && (part->nr_sects || partno == 0)) {
			devt = part_devt(part);
			disk_put_part(part);
			break;
		}
		disk_put_part(part);
	}
	class_dev_iter_exit(&iter);
	return devt;
}
EXPORT_SYMBOL(blk_lookup_devt);

struct gendisk *alloc_disk(int minors)
{
	return alloc_disk_node(minors, -1);
}

struct gendisk *alloc_disk_node(int minors, int node_id)
{
	return alloc_disk_ext_node(minors, 0, node_id);
}

struct gendisk *alloc_disk_ext(int minors, int ext_minors)
{
	return alloc_disk_ext_node(minors, ext_minors, -1);
}

struct gendisk *alloc_disk_ext_node(int minors, int ext_minors, int node_id)
{
	struct gendisk *disk;

	disk = kmalloc_node(sizeof(struct gendisk),
				GFP_KERNEL | __GFP_ZERO, node_id);
	if (disk) {
		int tot_minors = minors + ext_minors;
		int size = tot_minors * sizeof(struct hd_struct *);

		if (!init_disk_stats(disk)) {
			kfree(disk);
			return NULL;
		}

		disk->__part = kmalloc_node(size, GFP_KERNEL | __GFP_ZERO,
					    node_id);
		if (!disk->__part) {
			free_disk_stats(disk);
			kfree(disk);
			return NULL;
		}
		disk->__part[0] = &disk->part0;

		disk->minors = minors;
		disk->ext_minors = ext_minors;
		rand_initialize_disk(disk);
		disk_to_dev(disk)->class = &block_class;
		disk_to_dev(disk)->type = &disk_type;
		device_initialize(disk_to_dev(disk));
		INIT_WORK(&disk->async_notify,
			media_change_notify_thread);
	}
	return disk;
}

EXPORT_SYMBOL(alloc_disk);
EXPORT_SYMBOL(alloc_disk_node);
EXPORT_SYMBOL(alloc_disk_ext);
EXPORT_SYMBOL(alloc_disk_ext_node);

struct kobject *get_disk(struct gendisk *disk)
{
	struct module *owner;
	struct kobject *kobj;

	if (!disk->fops)
		return NULL;
	owner = disk->fops->owner;
	if (owner && !try_module_get(owner))
		return NULL;
	kobj = kobject_get(&disk_to_dev(disk)->kobj);
	if (kobj == NULL) {
		module_put(owner);
		return NULL;
	}
	return kobj;

}

EXPORT_SYMBOL(get_disk);

void put_disk(struct gendisk *disk)
{
	if (disk)
		kobject_put(&disk_to_dev(disk)->kobj);
}

EXPORT_SYMBOL(put_disk);

void set_device_ro(struct block_device *bdev, int flag)
{
	if (bdev->bd_contains != bdev)
		bdev->bd_part->policy = flag;
	else
		bdev->bd_disk->policy = flag;
}

EXPORT_SYMBOL(set_device_ro);

void set_disk_ro(struct gendisk *disk, int flag)
{
	struct disk_part_iter piter;
	struct hd_struct *part;

	disk->policy = flag;
	disk_part_iter_init(&piter, disk, DISK_PITER_INCL_EMPTY);
	while ((part = disk_part_iter_next(&piter)))
		part->policy = flag;
	disk_part_iter_exit(&piter);
}

EXPORT_SYMBOL(set_disk_ro);

int bdev_read_only(struct block_device *bdev)
{
	if (!bdev)
		return 0;
	else if (bdev->bd_contains != bdev)
		return bdev->bd_part->policy;
	else
		return bdev->bd_disk->policy;
}

EXPORT_SYMBOL(bdev_read_only);

int invalidate_partition(struct gendisk *disk, int partno)
{
	int res = 0;
	struct block_device *bdev = bdget_disk(disk, partno);
	if (bdev) {
		fsync_bdev(bdev);
		res = __invalidate_device(bdev);
		bdput(bdev);
	}
	return res;
}

EXPORT_SYMBOL(invalidate_partition);
