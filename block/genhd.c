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

struct subsystem block_subsys;
static DEFINE_MUTEX(block_subsys_lock);

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
		mutex_lock(&block_subsys_lock);
		for (dp = major_names[offset]; dp; dp = dp->next)
			seq_printf(f, "%3d %s\n", dp->major, dp->name);
		mutex_unlock(&block_subsys_lock);
	}
}

#endif /* CONFIG_PROC_FS */

int register_blkdev(unsigned int major, const char *name)
{
	struct blk_major_name **n, *p;
	int index, ret = 0;

	mutex_lock(&block_subsys_lock);

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
	mutex_unlock(&block_subsys_lock);
	return ret;
}

EXPORT_SYMBOL(register_blkdev);

/* todo: make void - error printk here */
int unregister_blkdev(unsigned int major, const char *name)
{
	struct blk_major_name **n;
	struct blk_major_name *p = NULL;
	int index = major_to_index(major);
	int ret = 0;

	mutex_lock(&block_subsys_lock);
	for (n = &major_names[index]; *n; n = &(*n)->next)
		if ((*n)->major == major)
			break;
	if (!*n || strcmp((*n)->name, name))
		ret = -EINVAL;
	else {
		p = *n;
		*n = p->next;
	}
	mutex_unlock(&block_subsys_lock);
	kfree(p);

	return ret;
}

EXPORT_SYMBOL(unregister_blkdev);

static struct kobj_map *bdev_map;

/*
 * Register device numbers dev..(dev+range-1)
 * range must be nonzero
 * The hash chain is sorted on range, so that subranges can override.
 */
void blk_register_region(dev_t dev, unsigned long range, struct module *module,
			 struct kobject *(*probe)(dev_t, int *, void *),
			 int (*lock)(dev_t, void *), void *data)
{
	kobj_map(bdev_map, dev, range, module, probe, lock, data);
}

EXPORT_SYMBOL(blk_register_region);

void blk_unregister_region(dev_t dev, unsigned long range)
{
	kobj_unmap(bdev_map, dev, range);
}

EXPORT_SYMBOL(blk_unregister_region);

static struct kobject *exact_match(dev_t dev, int *part, void *data)
{
	struct gendisk *p = data;
	return &p->kobj;
}

static int exact_lock(dev_t dev, void *data)
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
	disk->flags |= GENHD_FL_UP;
	blk_register_region(MKDEV(disk->major, disk->first_minor),
			    disk->minors, NULL, exact_match, exact_lock, disk);
	register_disk(disk);
	blk_register_queue(disk);
}

EXPORT_SYMBOL(add_disk);
EXPORT_SYMBOL(del_gendisk);	/* in partitions/check.c */

void unlink_gendisk(struct gendisk *disk)
{
	blk_unregister_queue(disk);
	blk_unregister_region(MKDEV(disk->major, disk->first_minor),
			      disk->minors);
}

#define to_disk(obj) container_of(obj,struct gendisk,kobj)

/**
 * get_gendisk - get partitioning information for a given device
 * @dev: device to get partitioning information for
 *
 * This function gets the structure containing partitioning
 * information for the given device @dev.
 */
struct gendisk *get_gendisk(dev_t dev, int *part)
{
	struct kobject *kobj = kobj_lookup(bdev_map, dev, part);
	return  kobj ? to_disk(kobj) : NULL;
}

#ifdef CONFIG_PROC_FS
/* iterator */
static void *part_start(struct seq_file *part, loff_t *pos)
{
	struct list_head *p;
	loff_t l = *pos;

	mutex_lock(&block_subsys_lock);
	list_for_each(p, &block_subsys.kset.list)
		if (!l--)
			return list_entry(p, struct gendisk, kobj.entry);
	return NULL;
}

static void *part_next(struct seq_file *part, void *v, loff_t *pos)
{
	struct list_head *p = ((struct gendisk *)v)->kobj.entry.next;
	++*pos;
	return p==&block_subsys.kset.list ? NULL : 
		list_entry(p, struct gendisk, kobj.entry);
}

static void part_stop(struct seq_file *part, void *v)
{
	mutex_unlock(&block_subsys_lock);
}

static int show_partition(struct seq_file *part, void *v)
{
	struct gendisk *sgp = v;
	int n;
	char buf[BDEVNAME_SIZE];

	if (&sgp->kobj.entry == block_subsys.kset.list.next)
		seq_puts(part, "major minor  #blocks  name\n\n");

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

struct seq_operations partitions_op = {
	.start =part_start,
	.next =	part_next,
	.stop =	part_stop,
	.show =	show_partition
};
#endif


extern int blk_dev_init(void);

static struct kobject *base_probe(dev_t dev, int *part, void *data)
{
	if (request_module("block-major-%d-%d", MAJOR(dev), MINOR(dev)) > 0)
		/* Make old-style 2.4 aliases work */
		request_module("block-major-%d", MAJOR(dev));
	return NULL;
}

static int __init genhd_device_init(void)
{
	int err;

	bdev_map = kobj_map_init(base_probe, &block_subsys_lock);
	blk_dev_init();
	err = subsystem_register(&block_subsys);
	if (err < 0)
		printk(KERN_WARNING "%s: subsystem_register error: %d\n",
			__FUNCTION__, err);
	return err;
}

subsys_initcall(genhd_device_init);



/*
 * kobject & sysfs bindings for block devices
 */
static ssize_t disk_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *page)
{
	struct gendisk *disk = to_disk(kobj);
	struct disk_attribute *disk_attr =
		container_of(attr,struct disk_attribute,attr);
	ssize_t ret = -EIO;

	if (disk_attr->show)
		ret = disk_attr->show(disk,page);
	return ret;
}

static ssize_t disk_attr_store(struct kobject * kobj, struct attribute * attr,
			       const char *page, size_t count)
{
	struct gendisk *disk = to_disk(kobj);
	struct disk_attribute *disk_attr =
		container_of(attr,struct disk_attribute,attr);
	ssize_t ret = 0;

	if (disk_attr->store)
		ret = disk_attr->store(disk, page, count);
	return ret;
}

static struct sysfs_ops disk_sysfs_ops = {
	.show	= &disk_attr_show,
	.store	= &disk_attr_store,
};

static ssize_t disk_uevent_store(struct gendisk * disk,
				 const char *buf, size_t count)
{
	kobject_uevent(&disk->kobj, KOBJ_ADD);
	return count;
}
static ssize_t disk_dev_read(struct gendisk * disk, char *page)
{
	dev_t base = MKDEV(disk->major, disk->first_minor); 
	return print_dev_t(page, base);
}
static ssize_t disk_range_read(struct gendisk * disk, char *page)
{
	return sprintf(page, "%d\n", disk->minors);
}
static ssize_t disk_removable_read(struct gendisk * disk, char *page)
{
	return sprintf(page, "%d\n",
		       (disk->flags & GENHD_FL_REMOVABLE ? 1 : 0));

}
static ssize_t disk_size_read(struct gendisk * disk, char *page)
{
	return sprintf(page, "%llu\n", (unsigned long long)get_capacity(disk));
}

static ssize_t disk_stats_read(struct gendisk * disk, char *page)
{
	preempt_disable();
	disk_round_stats(disk);
	preempt_enable();
	return sprintf(page,
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
static struct disk_attribute disk_attr_uevent = {
	.attr = {.name = "uevent", .mode = S_IWUSR },
	.store	= disk_uevent_store
};
static struct disk_attribute disk_attr_dev = {
	.attr = {.name = "dev", .mode = S_IRUGO },
	.show	= disk_dev_read
};
static struct disk_attribute disk_attr_range = {
	.attr = {.name = "range", .mode = S_IRUGO },
	.show	= disk_range_read
};
static struct disk_attribute disk_attr_removable = {
	.attr = {.name = "removable", .mode = S_IRUGO },
	.show	= disk_removable_read
};
static struct disk_attribute disk_attr_size = {
	.attr = {.name = "size", .mode = S_IRUGO },
	.show	= disk_size_read
};
static struct disk_attribute disk_attr_stat = {
	.attr = {.name = "stat", .mode = S_IRUGO },
	.show	= disk_stats_read
};

#ifdef CONFIG_FAIL_MAKE_REQUEST

static ssize_t disk_fail_store(struct gendisk * disk,
			       const char *buf, size_t count)
{
	int i;

	if (count > 0 && sscanf(buf, "%d", &i) > 0) {
		if (i == 0)
			disk->flags &= ~GENHD_FL_FAIL;
		else
			disk->flags |= GENHD_FL_FAIL;
	}

	return count;
}
static ssize_t disk_fail_read(struct gendisk * disk, char *page)
{
	return sprintf(page, "%d\n", disk->flags & GENHD_FL_FAIL ? 1 : 0);
}
static struct disk_attribute disk_attr_fail = {
	.attr = {.name = "make-it-fail", .mode = S_IRUGO | S_IWUSR },
	.store	= disk_fail_store,
	.show	= disk_fail_read
};

#endif

static struct attribute * default_attrs[] = {
	&disk_attr_uevent.attr,
	&disk_attr_dev.attr,
	&disk_attr_range.attr,
	&disk_attr_removable.attr,
	&disk_attr_size.attr,
	&disk_attr_stat.attr,
#ifdef CONFIG_FAIL_MAKE_REQUEST
	&disk_attr_fail.attr,
#endif
	NULL,
};

static void disk_release(struct kobject * kobj)
{
	struct gendisk *disk = to_disk(kobj);
	kfree(disk->random);
	kfree(disk->part);
	free_disk_stats(disk);
	kfree(disk);
}

static struct kobj_type ktype_block = {
	.release	= disk_release,
	.sysfs_ops	= &disk_sysfs_ops,
	.default_attrs	= default_attrs,
};

extern struct kobj_type ktype_part;

static int block_uevent_filter(struct kset *kset, struct kobject *kobj)
{
	struct kobj_type *ktype = get_ktype(kobj);

	return ((ktype == &ktype_block) || (ktype == &ktype_part));
}

static int block_uevent(struct kset *kset, struct kobject *kobj, char **envp,
			 int num_envp, char *buffer, int buffer_size)
{
	struct kobj_type *ktype = get_ktype(kobj);
	struct device *physdev;
	struct gendisk *disk;
	struct hd_struct *part;
	int length = 0;
	int i = 0;

	if (ktype == &ktype_block) {
		disk = container_of(kobj, struct gendisk, kobj);
		add_uevent_var(envp, num_envp, &i, buffer, buffer_size,
			       &length, "MINOR=%u", disk->first_minor);
	} else if (ktype == &ktype_part) {
		disk = container_of(kobj->parent, struct gendisk, kobj);
		part = container_of(kobj, struct hd_struct, kobj);
		add_uevent_var(envp, num_envp, &i, buffer, buffer_size,
			       &length, "MINOR=%u",
			       disk->first_minor + part->partno);
	} else
		return 0;

	add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &length,
		       "MAJOR=%u", disk->major);

	/* add physical device, backing this device  */
	physdev = disk->driverfs_dev;
	if (physdev) {
		char *path = kobject_get_path(&physdev->kobj, GFP_KERNEL);

		add_uevent_var(envp, num_envp, &i, buffer, buffer_size,
			       &length, "PHYSDEVPATH=%s", path);
		kfree(path);

		if (physdev->bus)
			add_uevent_var(envp, num_envp, &i,
				       buffer, buffer_size, &length,
				       "PHYSDEVBUS=%s",
				       physdev->bus->name);

		if (physdev->driver)
			add_uevent_var(envp, num_envp, &i,
				       buffer, buffer_size, &length,
				       "PHYSDEVDRIVER=%s",
				       physdev->driver->name);
	}

	/* terminate, set to next free slot, shrink available space */
	envp[i] = NULL;
	envp = &envp[i];
	num_envp -= i;
	buffer = &buffer[length];
	buffer_size -= length;

	return 0;
}

static struct kset_uevent_ops block_uevent_ops = {
	.filter		= block_uevent_filter,
	.uevent		= block_uevent,
};

decl_subsys(block, &ktype_block, &block_uevent_ops);

/*
 * aggregate disk stat collector.  Uses the same stats that the sysfs
 * entries do, above, but makes them available through one seq_file.
 * Watching a few disks may be efficient through sysfs, but watching
 * all of them will be more efficient through this interface.
 *
 * The output looks suspiciously like /proc/partitions with a bunch of
 * extra fields.
 */

/* iterator */
static void *diskstats_start(struct seq_file *part, loff_t *pos)
{
	loff_t k = *pos;
	struct list_head *p;

	mutex_lock(&block_subsys_lock);
	list_for_each(p, &block_subsys.kset.list)
		if (!k--)
			return list_entry(p, struct gendisk, kobj.entry);
	return NULL;
}

static void *diskstats_next(struct seq_file *part, void *v, loff_t *pos)
{
	struct list_head *p = ((struct gendisk *)v)->kobj.entry.next;
	++*pos;
	return p==&block_subsys.kset.list ? NULL :
		list_entry(p, struct gendisk, kobj.entry);
}

static void diskstats_stop(struct seq_file *part, void *v)
{
	mutex_unlock(&block_subsys_lock);
}

static int diskstats_show(struct seq_file *s, void *v)
{
	struct gendisk *gp = v;
	char buf[BDEVNAME_SIZE];
	int n = 0;

	/*
	if (&sgp->kobj.entry == block_subsys.kset.list.next)
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

		if (hd && hd->nr_sects)
			seq_printf(s, "%4d %4d %s %u %u %u %u\n",
				gp->major, n + gp->first_minor + 1,
				disk_name(gp, n + 1, buf),
				hd->ios[0], hd->sectors[0],
				hd->ios[1], hd->sectors[1]);
	}
 
	return 0;
}

struct seq_operations diskstats_op = {
	.start	= diskstats_start,
	.next	= diskstats_next,
	.stop	= diskstats_stop,
	.show	= diskstats_show
};

struct gendisk *alloc_disk(int minors)
{
	return alloc_disk_node(minors, -1);
}

struct gendisk *alloc_disk_node(int minors, int node_id)
{
	struct gendisk *disk;

	disk = kmalloc_node(sizeof(struct gendisk), GFP_KERNEL, node_id);
	if (disk) {
		memset(disk, 0, sizeof(struct gendisk));
		if (!init_disk_stats(disk)) {
			kfree(disk);
			return NULL;
		}
		if (minors > 1) {
			int size = (minors - 1) * sizeof(struct hd_struct *);
			disk->part = kmalloc_node(size, GFP_KERNEL, node_id);
			if (!disk->part) {
				kfree(disk);
				return NULL;
			}
			memset(disk->part, 0, size);
		}
		disk->minors = minors;
		kobj_set_kset_s(disk,block_subsys);
		kobject_init(&disk->kobj);
		rand_initialize_disk(disk);
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
	kobj = kobject_get(&disk->kobj);
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
		kobject_put(&disk->kobj);
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
