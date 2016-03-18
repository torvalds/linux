/*
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 *
 *  We now have independent partition support from the
 *  block drivers, which allows all the partition code to
 *  be grouped in one location, and it to be mostly self
 *  contained.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/ctype.h>
#include <linux/genhd.h>
#include <linux/blktrace_api.h>

#include "partitions/check.h"

#ifdef CONFIG_BLK_DEV_MD
extern void md_autodetect_dev(dev_t dev);
#endif
 
/*
 * disk_name() is used by partition check code and the genhd driver.
 * It formats the devicename of the indicated disk into
 * the supplied buffer (of size at least 32), and returns
 * a pointer to that same buffer (for convenience).
 */

char *disk_name(struct gendisk *hd, int partno, char *buf)
{
	if (!partno)
		snprintf(buf, BDEVNAME_SIZE, "%s", hd->disk_name);
	else if (isdigit(hd->disk_name[strlen(hd->disk_name)-1]))
		snprintf(buf, BDEVNAME_SIZE, "%sp%d", hd->disk_name, partno);
	else
		snprintf(buf, BDEVNAME_SIZE, "%s%d", hd->disk_name, partno);

	return buf;
}

const char *bdevname(struct block_device *bdev, char *buf)
{
	return disk_name(bdev->bd_disk, bdev->bd_part->partno, buf);
}

EXPORT_SYMBOL(bdevname);

/*
 * There's very little reason to use this, you should really
 * have a struct block_device just about everywhere and use
 * bdevname() instead.
 */
const char *__bdevname(dev_t dev, char *buffer)
{
	scnprintf(buffer, BDEVNAME_SIZE, "unknown-block(%u,%u)",
				MAJOR(dev), MINOR(dev));
	return buffer;
}

EXPORT_SYMBOL(__bdevname);

static ssize_t part_partition_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);

	return sprintf(buf, "%d\n", p->partno);
}

static ssize_t part_start_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);

	return sprintf(buf, "%llu\n",(unsigned long long)p->start_sect);
}

ssize_t part_size_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);
	return sprintf(buf, "%llu\n",(unsigned long long)part_nr_sects_read(p));
}

static ssize_t part_ro_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);
	return sprintf(buf, "%d\n", p->policy ? 1 : 0);
}

static ssize_t part_alignment_offset_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);
	return sprintf(buf, "%llu\n", (unsigned long long)p->alignment_offset);
}

static ssize_t part_discard_alignment_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);
	return sprintf(buf, "%u\n", p->discard_alignment);
}

ssize_t part_stat_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);
	int cpu;

	cpu = part_stat_lock();
	part_round_stats(cpu, p);
	part_stat_unlock();
	return sprintf(buf,
		"%8lu %8lu %8llu %8u "
		"%8lu %8lu %8llu %8u "
		"%8u %8u %8u"
		"\n",
		part_stat_read(p, ios[READ]),
		part_stat_read(p, merges[READ]),
		(unsigned long long)part_stat_read(p, sectors[READ]),
		jiffies_to_msecs(part_stat_read(p, ticks[READ])),
		part_stat_read(p, ios[WRITE]),
		part_stat_read(p, merges[WRITE]),
		(unsigned long long)part_stat_read(p, sectors[WRITE]),
		jiffies_to_msecs(part_stat_read(p, ticks[WRITE])),
		part_in_flight(p),
		jiffies_to_msecs(part_stat_read(p, io_ticks)),
		jiffies_to_msecs(part_stat_read(p, time_in_queue)));
}

ssize_t part_inflight_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);

	return sprintf(buf, "%8u %8u\n", atomic_read(&p->in_flight[0]),
		atomic_read(&p->in_flight[1]));
}

#ifdef CONFIG_FAIL_MAKE_REQUEST
ssize_t part_fail_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct hd_struct *p = dev_to_part(dev);

	return sprintf(buf, "%d\n", p->make_it_fail);
}

ssize_t part_fail_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct hd_struct *p = dev_to_part(dev);
	int i;

	if (count > 0 && sscanf(buf, "%d", &i) > 0)
		p->make_it_fail = (i == 0) ? 0 : 1;

	return count;
}
#endif

static DEVICE_ATTR(partition, S_IRUGO, part_partition_show, NULL);
static DEVICE_ATTR(start, S_IRUGO, part_start_show, NULL);
static DEVICE_ATTR(size, S_IRUGO, part_size_show, NULL);
static DEVICE_ATTR(ro, S_IRUGO, part_ro_show, NULL);
static DEVICE_ATTR(alignment_offset, S_IRUGO, part_alignment_offset_show, NULL);
static DEVICE_ATTR(discard_alignment, S_IRUGO, part_discard_alignment_show,
		   NULL);
static DEVICE_ATTR(stat, S_IRUGO, part_stat_show, NULL);
static DEVICE_ATTR(inflight, S_IRUGO, part_inflight_show, NULL);
#ifdef CONFIG_FAIL_MAKE_REQUEST
static struct device_attribute dev_attr_fail =
	__ATTR(make-it-fail, S_IRUGO|S_IWUSR, part_fail_show, part_fail_store);
#endif

static struct attribute *part_attrs[] = {
	&dev_attr_partition.attr,
	&dev_attr_start.attr,
	&dev_attr_size.attr,
	&dev_attr_ro.attr,
	&dev_attr_alignment_offset.attr,
	&dev_attr_discard_alignment.attr,
	&dev_attr_stat.attr,
	&dev_attr_inflight.attr,
#ifdef CONFIG_FAIL_MAKE_REQUEST
	&dev_attr_fail.attr,
#endif
	NULL
};

static struct attribute_group part_attr_group = {
	.attrs = part_attrs,
};

static const struct attribute_group *part_attr_groups[] = {
	&part_attr_group,
#ifdef CONFIG_BLK_DEV_IO_TRACE
	&blk_trace_attr_group,
#endif
	NULL
};

static void part_release(struct device *dev)
{
	struct hd_struct *p = dev_to_part(dev);
	blk_free_devt(dev->devt);
	hd_free_part(p);
	kfree(p);
}

static int part_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct hd_struct *part = dev_to_part(dev);

	add_uevent_var(env, "PARTN=%u", part->partno);
	if (part->info && part->info->volname[0])
		add_uevent_var(env, "PARTNAME=%s", part->info->volname);
	return 0;
}

struct device_type part_type = {
	.name		= "partition",
	.groups		= part_attr_groups,
	.release	= part_release,
	.uevent		= part_uevent,
};

static void delete_partition_rcu_cb(struct rcu_head *head)
{
	struct hd_struct *part = container_of(head, struct hd_struct, rcu_head);

	part->start_sect = 0;
	part->nr_sects = 0;
	part_stat_set_all(part, 0);
	put_device(part_to_dev(part));
}

void __delete_partition(struct percpu_ref *ref)
{
	struct hd_struct *part = container_of(ref, struct hd_struct, ref);
	call_rcu(&part->rcu_head, delete_partition_rcu_cb);
}

void delete_partition(struct gendisk *disk, int partno)
{
	struct disk_part_tbl *ptbl = disk->part_tbl;
	struct hd_struct *part;

	if (partno >= ptbl->len)
		return;

	part = ptbl->part[partno];
	if (!part)
		return;

	rcu_assign_pointer(ptbl->part[partno], NULL);
	rcu_assign_pointer(ptbl->last_lookup, NULL);
	kobject_put(part->holder_dir);
	device_del(part_to_dev(part));

	hd_struct_kill(part);
}

static ssize_t whole_disk_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}
static DEVICE_ATTR(whole_disk, S_IRUSR | S_IRGRP | S_IROTH,
		   whole_disk_show, NULL);

struct hd_struct *add_partition(struct gendisk *disk, int partno,
				sector_t start, sector_t len, int flags,
				struct partition_meta_info *info)
{
	struct hd_struct *p;
	dev_t devt = MKDEV(0, 0);
	struct device *ddev = disk_to_dev(disk);
	struct device *pdev;
	struct disk_part_tbl *ptbl;
	const char *dname;
	int err;

	err = disk_expand_part_tbl(disk, partno);
	if (err)
		return ERR_PTR(err);
	ptbl = disk->part_tbl;

	if (ptbl->part[partno])
		return ERR_PTR(-EBUSY);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-EBUSY);

	if (!init_part_stats(p)) {
		err = -ENOMEM;
		goto out_free;
	}

	seqcount_init(&p->nr_sects_seq);
	pdev = part_to_dev(p);

	p->start_sect = start;
	p->alignment_offset =
		queue_limit_alignment_offset(&disk->queue->limits, start);
	p->discard_alignment =
		queue_limit_discard_alignment(&disk->queue->limits, start);
	p->nr_sects = len;
	p->partno = partno;
	p->policy = get_disk_ro(disk);

	if (info) {
		struct partition_meta_info *pinfo = alloc_part_info(disk);
		if (!pinfo)
			goto out_free_stats;
		memcpy(pinfo, info, sizeof(*info));
		p->info = pinfo;
	}

	dname = dev_name(ddev);
	if (isdigit(dname[strlen(dname) - 1]))
		dev_set_name(pdev, "%sp%d", dname, partno);
	else
		dev_set_name(pdev, "%s%d", dname, partno);

	device_initialize(pdev);
	pdev->class = &block_class;
	pdev->type = &part_type;
	pdev->parent = ddev;

	err = blk_alloc_devt(p, &devt);
	if (err)
		goto out_free_info;
	pdev->devt = devt;

	/* delay uevent until 'holders' subdir is created */
	dev_set_uevent_suppress(pdev, 1);
	err = device_add(pdev);
	if (err)
		goto out_put;

	err = -ENOMEM;
	p->holder_dir = kobject_create_and_add("holders", &pdev->kobj);
	if (!p->holder_dir)
		goto out_del;

	dev_set_uevent_suppress(pdev, 0);
	if (flags & ADDPART_FLAG_WHOLEDISK) {
		err = device_create_file(pdev, &dev_attr_whole_disk);
		if (err)
			goto out_del;
	}

	/* everything is up and running, commence */
	rcu_assign_pointer(ptbl->part[partno], p);

	/* suppress uevent if the disk suppresses it */
	if (!dev_get_uevent_suppress(ddev))
		kobject_uevent(&pdev->kobj, KOBJ_ADD);

	if (!hd_ref_init(p))
		return p;

out_free_info:
	free_part_info(p);
out_free_stats:
	free_part_stats(p);
out_free:
	kfree(p);
	return ERR_PTR(err);
out_del:
	kobject_put(p->holder_dir);
	device_del(pdev);
out_put:
	put_device(pdev);
	blk_free_devt(devt);
	return ERR_PTR(err);
}

static bool disk_unlock_native_capacity(struct gendisk *disk)
{
	const struct block_device_operations *bdops = disk->fops;

	if (bdops->unlock_native_capacity &&
	    !(disk->flags & GENHD_FL_NATIVE_CAPACITY)) {
		printk(KERN_CONT "enabling native capacity\n");
		bdops->unlock_native_capacity(disk);
		disk->flags |= GENHD_FL_NATIVE_CAPACITY;
		return true;
	} else {
		printk(KERN_CONT "truncated\n");
		return false;
	}
}

static int drop_partitions(struct gendisk *disk, struct block_device *bdev)
{
	struct disk_part_iter piter;
	struct hd_struct *part;
	int res;

	if (bdev->bd_part_count || bdev->bd_super)
		return -EBUSY;
	res = invalidate_partition(disk, 0);
	if (res)
		return res;

	disk_part_iter_init(&piter, disk, DISK_PITER_INCL_EMPTY);
	while ((part = disk_part_iter_next(&piter)))
		delete_partition(disk, part->partno);
	disk_part_iter_exit(&piter);

	return 0;
}

int rescan_partitions(struct gendisk *disk, struct block_device *bdev)
{
	struct parsed_partitions *state = NULL;
	struct hd_struct *part;
	int p, highest, res;
rescan:
	if (state && !IS_ERR(state)) {
		free_partitions(state);
		state = NULL;
	}

	res = drop_partitions(disk, bdev);
	if (res)
		return res;

	if (disk->fops->revalidate_disk)
		disk->fops->revalidate_disk(disk);
	blk_integrity_revalidate(disk);
	check_disk_size_change(disk, bdev);
	bdev->bd_invalidated = 0;
	if (!get_capacity(disk) || !(state = check_partition(disk, bdev)))
		return 0;
	if (IS_ERR(state)) {
		/*
		 * I/O error reading the partition table.  If any
		 * partition code tried to read beyond EOD, retry
		 * after unlocking native capacity.
		 */
		if (PTR_ERR(state) == -ENOSPC) {
			printk(KERN_WARNING "%s: partition table beyond EOD, ",
			       disk->disk_name);
			if (disk_unlock_native_capacity(disk))
				goto rescan;
		}
		return -EIO;
	}
	/*
	 * If any partition code tried to read beyond EOD, try
	 * unlocking native capacity even if partition table is
	 * successfully read as we could be missing some partitions.
	 */
	if (state->access_beyond_eod) {
		printk(KERN_WARNING
		       "%s: partition table partially beyond EOD, ",
		       disk->disk_name);
		if (disk_unlock_native_capacity(disk))
			goto rescan;
	}

	/* tell userspace that the media / partition table may have changed */
	kobject_uevent(&disk_to_dev(disk)->kobj, KOBJ_CHANGE);

	/* Detect the highest partition number and preallocate
	 * disk->part_tbl.  This is an optimization and not strictly
	 * necessary.
	 */
	for (p = 1, highest = 0; p < state->limit; p++)
		if (state->parts[p].size)
			highest = p;

	disk_expand_part_tbl(disk, highest);

	/* add partitions */
	for (p = 1; p < state->limit; p++) {
		sector_t size, from;
		struct partition_meta_info *info = NULL;

		size = state->parts[p].size;
		if (!size)
			continue;

		from = state->parts[p].from;
		if (from >= get_capacity(disk)) {
			printk(KERN_WARNING
			       "%s: p%d start %llu is beyond EOD, ",
			       disk->disk_name, p, (unsigned long long) from);
			if (disk_unlock_native_capacity(disk))
				goto rescan;
			continue;
		}

		if (from + size > get_capacity(disk)) {
			printk(KERN_WARNING
			       "%s: p%d size %llu extends beyond EOD, ",
			       disk->disk_name, p, (unsigned long long) size);

			if (disk_unlock_native_capacity(disk)) {
				/* free state and restart */
				goto rescan;
			} else {
				/*
				 * we can not ignore partitions of broken tables
				 * created by for example camera firmware, but
				 * we limit them to the end of the disk to avoid
				 * creating invalid block devices
				 */
				size = get_capacity(disk) - from;
			}
		}

		if (state->parts[p].has_info)
			info = &state->parts[p].info;
		part = add_partition(disk, p, from, size,
				     state->parts[p].flags,
				     &state->parts[p].info);
		if (IS_ERR(part)) {
			printk(KERN_ERR " %s: p%d could not be added: %ld\n",
			       disk->disk_name, p, -PTR_ERR(part));
			continue;
		}
#ifdef CONFIG_BLK_DEV_MD
		if (state->parts[p].flags & ADDPART_FLAG_RAID)
			md_autodetect_dev(part_to_dev(part)->devt);
#endif
	}
	free_partitions(state);
	return 0;
}

int invalidate_partitions(struct gendisk *disk, struct block_device *bdev)
{
	int res;

	if (!bdev->bd_invalidated)
		return 0;

	res = drop_partitions(disk, bdev);
	if (res)
		return res;

	set_capacity(disk, 0);
	check_disk_size_change(disk, bdev);
	bdev->bd_invalidated = 0;
	/* tell userspace that the media / partition table may have changed */
	kobject_uevent(&disk_to_dev(disk)->kobj, KOBJ_CHANGE);

	return 0;
}

unsigned char *read_dev_sector(struct block_device *bdev, sector_t n, Sector *p)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	struct page *page;

	page = read_mapping_page(mapping, (pgoff_t)(n >> (PAGE_CACHE_SHIFT-9)),
				 NULL);
	if (!IS_ERR(page)) {
		if (PageError(page))
			goto fail;
		p->v = page;
		return (unsigned char *)page_address(page) +  ((n & ((1 << (PAGE_CACHE_SHIFT - 9)) - 1)) << 9);
fail:
		page_cache_release(page);
	}
	p->v = NULL;
	return NULL;
}

EXPORT_SYMBOL(read_dev_sector);
