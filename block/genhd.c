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
	struct hd_struct *part = NULL;
	struct disk_part_tbl *ptbl;

	if (unlikely(partno < 0))
		return NULL;

	rcu_read_lock();

	ptbl = rcu_dereference(disk->part_tbl);
	if (likely(partno < ptbl->len)) {
		part = rcu_dereference(ptbl->part[partno]);
		if (part)
			get_device(part_to_dev(part));
	}

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
	struct disk_part_tbl *ptbl;

	rcu_read_lock();
	ptbl = rcu_dereference(disk->part_tbl);

	piter->disk = disk;
	piter->part = NULL;

	if (flags & DISK_PITER_REVERSE)
		piter->idx = ptbl->len - 1;
	else if (flags & DISK_PITER_INCL_PART0)
		piter->idx = 0;
	else
		piter->idx = 1;

	piter->flags = flags;

	rcu_read_unlock();
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
	struct disk_part_tbl *ptbl;
	int inc, end;

	/* put the last partition */
	disk_put_part(piter->part);
	piter->part = NULL;

	/* get part_tbl */
	rcu_read_lock();
	ptbl = rcu_dereference(piter->disk->part_tbl);

	/* determine iteration parameters */
	if (piter->flags & DISK_PITER_REVERSE) {
		inc = -1;
		if (piter->flags & DISK_PITER_INCL_PART0)
			end = -1;
		else
			end = 0;
	} else {
		inc = 1;
		end = ptbl->len;
	}

	/* iterate to the next partition */
	for (; piter->idx != end; piter->idx += inc) {
		struct hd_struct *part;

		part = rcu_dereference(ptbl->part[piter->idx]);
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
 * Found partition on success, part0 is returned if no partition matches
 */
struct hd_struct *disk_map_sector_rcu(struct gendisk *disk, sector_t sector)
{
	struct disk_part_tbl *ptbl;
	int i;

	ptbl = rcu_dereference(disk->part_tbl);

	for (i = 1; i < ptbl->len; i++) {
		struct hd_struct *part = rcu_dereference(ptbl->part[i]);

		if (part && part->start_sect <= sector &&
		    sector < part->start_sect + part->nr_sects)
			return part;
	}
	return &disk->part0;
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
 *
 * FIXME: error handling
 */
void add_disk(struct gendisk *disk)
{
	struct backing_dev_info *bdi;
	dev_t devt;
	int retval;

	/* minors == 0 indicates to use ext devt from part0 and should
	 * be accompanied with EXT_DEVT flag.  Make sure all
	 * parameters make sense.
	 */
	WARN_ON(disk->minors && !(disk->major || disk->first_minor));
	WARN_ON(!disk->minors && !(disk->flags & GENHD_FL_EXT_DEVT));

	disk->flags |= GENHD_FL_UP;

	retval = blk_alloc_devt(&disk->part0, &devt);
	if (retval) {
		WARN_ON(1);
		return;
	}
	disk_to_dev(disk)->devt = devt;

	/* ->major and ->first_minor aren't supposed to be
	 * dereferenced from here on, but set them just in case.
	 */
	disk->major = MAJOR(devt);
	disk->first_minor = MINOR(devt);

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
struct block_device *bdget_disk(struct gendisk *disk, int partno)
{
	struct hd_struct *part;
	struct block_device *bdev = NULL;

	part = disk_get_part(disk, partno);
	if (part)
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
		disk_part_iter_init(&piter, disk, DISK_PITER_INCL_PART0);
		while ((part = disk_part_iter_next(&piter))) {
			bool is_part0 = part == &disk->part0;

			printk("%s%s %10llu %s", is_part0 ? "" : "  ",
			       bdevt_str(part_devt(part), devt_buf),
			       (unsigned long long)part->nr_sects >> 1,
			       disk_name(disk, part->partno, name_buf));
			if (is_part0) {
				if (disk->driverfs_dev != NULL &&
				    disk->driverfs_dev->driver != NULL)
					printk(" driver: %s\n",
					      disk->driverfs_dev->driver->name);
				else
					printk(" (driver?)\n");
			} else
				printk("\n");
		}
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
	}
}

static void *show_partition_start(struct seq_file *seqf, loff_t *pos)
{
	static void *p;

	p = disk_seqf_start(seqf, pos);
	if (!IS_ERR(p) && p && !*pos)
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
	disk_part_iter_init(&piter, sgp, DISK_PITER_INCL_PART0);
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

	return sprintf(buf, "%d\n", get_disk_ro(disk) ? 1 : 0);
}

static ssize_t disk_capability_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%x\n", disk->flags);
}

static DEVICE_ATTR(range, S_IRUGO, disk_range_show, NULL);
static DEVICE_ATTR(ext_range, S_IRUGO, disk_ext_range_show, NULL);
static DEVICE_ATTR(removable, S_IRUGO, disk_removable_show, NULL);
static DEVICE_ATTR(ro, S_IRUGO, disk_ro_show, NULL);
static DEVICE_ATTR(size, S_IRUGO, part_size_show, NULL);
static DEVICE_ATTR(capability, S_IRUGO, disk_capability_show, NULL);
static DEVICE_ATTR(stat, S_IRUGO, part_stat_show, NULL);
#ifdef CONFIG_FAIL_MAKE_REQUEST
static struct device_attribute dev_attr_fail =
	__ATTR(make-it-fail, S_IRUGO|S_IWUSR, part_fail_show, part_fail_store);
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

static void disk_free_ptbl_rcu_cb(struct rcu_head *head)
{
	struct disk_part_tbl *ptbl =
		container_of(head, struct disk_part_tbl, rcu_head);

	kfree(ptbl);
}

/**
 * disk_replace_part_tbl - replace disk->part_tbl in RCU-safe way
 * @disk: disk to replace part_tbl for
 * @new_ptbl: new part_tbl to install
 *
 * Replace disk->part_tbl with @new_ptbl in RCU-safe way.  The
 * original ptbl is freed using RCU callback.
 *
 * LOCKING:
 * Matching bd_mutx locked.
 */
static void disk_replace_part_tbl(struct gendisk *disk,
				  struct disk_part_tbl *new_ptbl)
{
	struct disk_part_tbl *old_ptbl = disk->part_tbl;

	rcu_assign_pointer(disk->part_tbl, new_ptbl);
	if (old_ptbl)
		call_rcu(&old_ptbl->rcu_head, disk_free_ptbl_rcu_cb);
}

/**
 * disk_expand_part_tbl - expand disk->part_tbl
 * @disk: disk to expand part_tbl for
 * @partno: expand such that this partno can fit in
 *
 * Expand disk->part_tbl such that @partno can fit in.  disk->part_tbl
 * uses RCU to allow unlocked dereferencing for stats and other stuff.
 *
 * LOCKING:
 * Matching bd_mutex locked, might sleep.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int disk_expand_part_tbl(struct gendisk *disk, int partno)
{
	struct disk_part_tbl *old_ptbl = disk->part_tbl;
	struct disk_part_tbl *new_ptbl;
	int len = old_ptbl ? old_ptbl->len : 0;
	int target = partno + 1;
	size_t size;
	int i;

	/* disk_max_parts() is zero during initialization, ignore if so */
	if (disk_max_parts(disk) && target > disk_max_parts(disk))
		return -EINVAL;

	if (target <= len)
		return 0;

	size = sizeof(*new_ptbl) + target * sizeof(new_ptbl->part[0]);
	new_ptbl = kzalloc_node(size, GFP_KERNEL, disk->node_id);
	if (!new_ptbl)
		return -ENOMEM;

	INIT_RCU_HEAD(&new_ptbl->rcu_head);
	new_ptbl->len = target;

	for (i = 0; i < len; i++)
		rcu_assign_pointer(new_ptbl->part[i], old_ptbl->part[i]);

	disk_replace_part_tbl(disk, new_ptbl);
	return 0;
}

static void disk_release(struct device *dev)
{
	struct gendisk *disk = dev_to_disk(dev);

	kfree(disk->random);
	disk_replace_part_tbl(disk, NULL);
	free_part_stats(&disk->part0);
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
 
	disk_part_iter_init(&piter, gp, DISK_PITER_INCL_PART0);
	while ((hd = disk_part_iter_next(&piter))) {
		cpu = part_stat_lock();
		part_round_stats(cpu, hd);
		part_stat_unlock();
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
		if (part) {
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
EXPORT_SYMBOL(alloc_disk);

struct gendisk *alloc_disk_node(int minors, int node_id)
{
	struct gendisk *disk;

	disk = kmalloc_node(sizeof(struct gendisk),
				GFP_KERNEL | __GFP_ZERO, node_id);
	if (disk) {
		if (!init_part_stats(&disk->part0)) {
			kfree(disk);
			return NULL;
		}
		if (disk_expand_part_tbl(disk, 0)) {
			free_part_stats(&disk->part0);
			kfree(disk);
			return NULL;
		}
		disk->part_tbl->part[0] = &disk->part0;

		disk->minors = minors;
		rand_initialize_disk(disk);
		disk_to_dev(disk)->class = &block_class;
		disk_to_dev(disk)->type = &disk_type;
		device_initialize(disk_to_dev(disk));
		INIT_WORK(&disk->async_notify,
			media_change_notify_thread);
		disk->node_id = node_id;
	}
	return disk;
}
EXPORT_SYMBOL(alloc_disk_node);

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
	bdev->bd_part->policy = flag;
}

EXPORT_SYMBOL(set_device_ro);

void set_disk_ro(struct gendisk *disk, int flag)
{
	struct disk_part_iter piter;
	struct hd_struct *part;

	disk_part_iter_init(&piter, disk,
			    DISK_PITER_INCL_EMPTY | DISK_PITER_INCL_PART0);
	while ((part = disk_part_iter_next(&piter)))
		part->policy = flag;
	disk_part_iter_exit(&piter);
}

EXPORT_SYMBOL(set_disk_ro);

int bdev_read_only(struct block_device *bdev)
{
	if (!bdev)
		return 0;
	return bdev->bd_part->policy;
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
