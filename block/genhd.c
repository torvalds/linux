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

#include "blk.h"

static DEFINE_MUTEX(block_class_lock);
#ifndef CONFIG_SYSFS_DEPRECATED
struct kobject *block_depr;
#endif

static struct device_type disk_type;

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
void blkdev_show(struct seq_file *f, off_t offset)
{
	struct blk_major_name *dp;

	if (offset < BLKDEV_MAJOR_HASH_SIZE) {
		mutex_lock(&block_class_lock);
		for (dp = major_names[offset]; dp; dp = dp->next)
			seq_printf(f, "%3d %s\n", dp->major, dp->name);
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

static struct kobject *exact_match(dev_t devt, int *part, void *data)
{
	struct gendisk *p = data;

	return &p->dev.kobj;
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
	blk_register_region(MKDEV(disk->major, disk->first_minor),
			    disk->minors, NULL, exact_match, exact_lock, disk);
	register_disk(disk);
	blk_register_queue(disk);

	bdi = &disk->queue->backing_dev_info;
	bdi_register_dev(bdi, MKDEV(disk->major, disk->first_minor));
	retval = sysfs_create_link(&disk->dev.kobj, &bdi->dev->kobj, "bdi");
	WARN_ON(retval);
}

EXPORT_SYMBOL(add_disk);
EXPORT_SYMBOL(del_gendisk);	/* in partitions/check.c */

void unlink_gendisk(struct gendisk *disk)
{
	sysfs_remove_link(&disk->dev.kobj, "bdi");
	bdi_unregister(&disk->queue->backing_dev_info);
	blk_unregister_queue(disk);
	blk_unregister_region(MKDEV(disk->major, disk->first_minor),
			      disk->minors);
}

/**
 * get_gendisk - get partitioning information for a given device
 * @dev: device to get partitioning information for
 *
 * This function gets the structure containing partitioning
 * information for the given device @dev.
 */
struct gendisk *get_gendisk(dev_t devt, int *part)
{
	struct kobject *kobj = kobj_lookup(bdev_map, devt, part);
	struct device *dev = kobj_to_dev(kobj);

	return  kobj ? dev_to_disk(dev) : NULL;
}

/*
 * print a partitions - intended for places where the root filesystem can't be
 * mounted and thus to give the victim some idea of what went wrong
 */
static int printk_partition(struct device *dev, void *data)
{
	struct gendisk *sgp;
	char buf[BDEVNAME_SIZE];
	int n;

	if (dev->type != &disk_type)
		goto exit;

	sgp = dev_to_disk(dev);
	/*
	 * Don't show empty devices or things that have been surpressed
	 */
	if (get_capacity(sgp) == 0 ||
	    (sgp->flags & GENHD_FL_SUPPRESS_PARTITION_INFO))
		goto exit;

	/*
	 * Note, unlike /proc/partitions, I am showing the numbers in
	 * hex - the same format as the root= option takes.
	 */
	printk("%02x%02x %10llu %s",
		sgp->major, sgp->first_minor,
		(unsigned long long)get_capacity(sgp) >> 1,
		disk_name(sgp, 0, buf));
	if (sgp->driverfs_dev != NULL &&
	    sgp->driverfs_dev->driver != NULL)
		printk(" driver: %s\n",
			sgp->driverfs_dev->driver->name);
	else
		printk(" (driver?)\n");

	/* now show the partitions */
	for (n = 0; n < sgp->minors - 1; ++n) {
		if (sgp->part[n] == NULL)
			goto exit;
		if (sgp->part[n]->nr_sects == 0)
			goto exit;
		printk("  %02x%02x %10llu %s\n",
			sgp->major, n + 1 + sgp->first_minor,
			(unsigned long long)sgp->part[n]->nr_sects >> 1,
			disk_name(sgp, n + 1, buf));
	}
exit:
	return 0;
}

/*
 * print a full list of all partitions - intended for places where the root
 * filesystem can't be mounted and thus to give the victim some idea of what
 * went wrong
 */
void __init printk_all_partitions(void)
{
	mutex_lock(&block_class_lock);
	class_for_each_device(&block_class, NULL, NULL, printk_partition);
	mutex_unlock(&block_class_lock);
}

#ifdef CONFIG_PROC_FS
/* iterator */
static int find_start(struct device *dev, void *data)
{
	loff_t *k = data;

	if (dev->type != &disk_type)
		return 0;
	if (!*k)
		return 1;
	(*k)--;
	return 0;
}

static void *part_start(struct seq_file *part, loff_t *pos)
{
	struct device *dev;
	loff_t k = *pos;

	if (!k)
		part->private = (void *)1LU;	/* tell show to print header */

	mutex_lock(&block_class_lock);
	dev = class_find_device(&block_class, NULL, &k, find_start);
	if (dev) {
		put_device(dev);
		return dev_to_disk(dev);
	}
	return NULL;
}

static int find_next(struct device *dev, void *data)
{
	if (dev->type == &disk_type)
		return 1;
	return 0;
}

static void *part_next(struct seq_file *part, void *v, loff_t *pos)
{
	struct gendisk *gp = v;
	struct device *dev;
	++*pos;
	dev = class_find_device(&block_class, &gp->dev, NULL, find_next);
	if (dev) {
		put_device(dev);
		return dev_to_disk(dev);
	}
	return NULL;
}

static void part_stop(struct seq_file *part, void *v)
{
	mutex_unlock(&block_class_lock);
}

static int show_partition(struct seq_file *part, void *v)
{
	struct gendisk *sgp = v;
	int n;
	char buf[BDEVNAME_SIZE];

	/*
	 * Print header if start told us to do.  This is to preserve
	 * the original behavior of not printing header if no
	 * partition exists.  This hackery will be removed later with
	 * class iteration clean up.
	 */
	if (part->private) {
		seq_puts(part, "major minor  #blocks  name\n\n");
		part->private = NULL;
	}

	/* Don't show non-partitionable removeable devices or empty devices */
	if (!get_capacity(sgp) ||
			(sgp->minors == 1 && (sgp->flags & GENHD_FL_REMOVABLE)))
		return 0;
	if (sgp->flags & GENHD_FL_SUPPRESS_PARTITION_INFO)
		return 0;

	/* show the full disk and all non-0 size partitions of it */
	seq_printf(part, "%4d  %4d %10llu %s\n",
		sgp->major, sgp->first_minor,
		(unsigned long long)get_capacity(sgp) >> 1,
		disk_name(sgp, 0, buf));
	for (n = 0; n < sgp->minors - 1; n++) {
		if (!sgp->part[n])
			continue;
		if (sgp->part[n]->nr_sects == 0)
			continue;
		seq_printf(part, "%4d  %4d %10llu %s\n",
			sgp->major, n + 1 + sgp->first_minor,
			(unsigned long long)sgp->part[n]->nr_sects >> 1 ,
			disk_name(sgp, n + 1, buf));
	}

	return 0;
}

const struct seq_operations partitions_op = {
	.start	= part_start,
	.next	= part_next,
	.stop	= part_stop,
	.show	= show_partition
};
#endif


static struct kobject *base_probe(dev_t devt, int *part, void *data)
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

	preempt_disable();
	disk_round_stats(disk);
	preempt_enable();
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
	kfree(disk->part);
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

static void *diskstats_start(struct seq_file *part, loff_t *pos)
{
	struct device *dev;
	loff_t k = *pos;

	mutex_lock(&block_class_lock);
	dev = class_find_device(&block_class, NULL, &k, find_start);
	if (dev) {
		put_device(dev);
		return dev_to_disk(dev);
	}
	return NULL;
}

static void *diskstats_next(struct seq_file *part, void *v, loff_t *pos)
{
	struct gendisk *gp = v;
	struct device *dev;

	++*pos;
	dev = class_find_device(&block_class, &gp->dev, NULL, find_next);
	if (dev) {
		put_device(dev);
		return dev_to_disk(dev);
	}
	return NULL;
}

static void diskstats_stop(struct seq_file *part, void *v)
{
	mutex_unlock(&block_class_lock);
}

static int diskstats_show(struct seq_file *s, void *v)
{
	struct gendisk *gp = v;
	char buf[BDEVNAME_SIZE];
	int n = 0;

	/*
	if (&gp->dev.kobj.entry == block_class.devices.next)
		seq_puts(s,	"major minor name"
				"     rio rmerge rsect ruse wio wmerge "
				"wsect wuse running use aveq"
				"\n\n");
	*/
 
	preempt_disable();
	disk_round_stats(gp);
	preempt_enable();
	seq_printf(s, "%4d %4d %s %lu %lu %llu %u %lu %lu %llu %u %u %u %u\n",
		gp->major, n + gp->first_minor, disk_name(gp, n, buf),
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
	for (n = 0; n < gp->minors - 1; n++) {
		struct hd_struct *hd = gp->part[n];

		if (!hd || !hd->nr_sects)
			continue;

		preempt_disable();
		part_round_stats(hd);
		preempt_enable();
		seq_printf(s, "%4d %4d %s %lu %lu %llu "
			   "%u %lu %lu %llu %u %u %u %u\n",
			   gp->major, n + gp->first_minor + 1,
			   disk_name(gp, n + 1, buf),
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
 
	return 0;
}

const struct seq_operations diskstats_op = {
	.start	= diskstats_start,
	.next	= diskstats_next,
	.stop	= diskstats_stop,
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
	kobject_uevent_env(&gd->dev.kobj, KOBJ_CHANGE, envp);
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

struct find_block {
	const char *name;
	int part;
};

static int match_id(struct device *dev, void *data)
{
	struct find_block *find = data;

	if (dev->type != &disk_type)
		return 0;
	if (strcmp(dev->bus_id, find->name) == 0) {
		struct gendisk *disk = dev_to_disk(dev);
		if (find->part < disk->minors)
			return 1;
	}
	return 0;
}

dev_t blk_lookup_devt(const char *name, int part)
{
	struct device *dev;
	dev_t devt = MKDEV(0, 0);
	struct find_block find;

	mutex_lock(&block_class_lock);
	find.name = name;
	find.part = part;
	dev = class_find_device(&block_class, NULL, &find, match_id);
	if (dev) {
		put_device(dev);
		devt = MKDEV(MAJOR(dev->devt),
			     MINOR(dev->devt) + part);
	}
	mutex_unlock(&block_class_lock);

	return devt;
}
EXPORT_SYMBOL(blk_lookup_devt);

struct gendisk *alloc_disk(int minors)
{
	return alloc_disk_node(minors, -1);
}

struct gendisk *alloc_disk_node(int minors, int node_id)
{
	struct gendisk *disk;

	disk = kmalloc_node(sizeof(struct gendisk),
				GFP_KERNEL | __GFP_ZERO, node_id);
	if (disk) {
		if (!init_disk_stats(disk)) {
			kfree(disk);
			return NULL;
		}
		if (minors > 1) {
			int size = (minors - 1) * sizeof(struct hd_struct *);
			disk->part = kmalloc_node(size,
				GFP_KERNEL | __GFP_ZERO, node_id);
			if (!disk->part) {
				free_disk_stats(disk);
				kfree(disk);
				return NULL;
			}
		}
		disk->minors = minors;
		rand_initialize_disk(disk);
		disk->dev.class = &block_class;
		disk->dev.type = &disk_type;
		device_initialize(&disk->dev);
		INIT_WORK(&disk->async_notify,
			media_change_notify_thread);
	}
	return disk;
}

EXPORT_SYMBOL(alloc_disk);
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
	kobj = kobject_get(&disk->dev.kobj);
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
		kobject_put(&disk->dev.kobj);
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
	int i;
	disk->policy = flag;
	for (i = 0; i < disk->minors - 1; i++)
		if (disk->part[i]) disk->part[i]->policy = flag;
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

int invalidate_partition(struct gendisk *disk, int index)
{
	int res = 0;
	struct block_device *bdev = bdget_disk(disk, index);
	if (bdev) {
		fsync_bdev(bdev);
		res = __invalidate_device(bdev);
		bdput(bdev);
	}
	return res;
}

EXPORT_SYMBOL(invalidate_partition);
