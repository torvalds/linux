// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1991-1998  Linus Torvalds
 * Re-organised Feb 1998 Russell King
 */
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/genhd.h>
#include <linux/vmalloc.h>
#include <linux/blktrace_api.h>
#include <linux/raid/detect.h>
#include "check.h"

static int (*check_part[])(struct parsed_partitions *) = {
	/*
	 * Probe partition formats with tables at disk address 0
	 * that also have an ADFS boot block at 0xdc0.
	 */
#ifdef CONFIG_ACORN_PARTITION_ICS
	adfspart_check_ICS,
#endif
#ifdef CONFIG_ACORN_PARTITION_POWERTEC
	adfspart_check_POWERTEC,
#endif
#ifdef CONFIG_ACORN_PARTITION_EESOX
	adfspart_check_EESOX,
#endif

	/*
	 * Now move on to formats that only have partition info at
	 * disk address 0xdc0.  Since these may also have stale
	 * PC/BIOS partition tables, they need to come before
	 * the msdos entry.
	 */
#ifdef CONFIG_ACORN_PARTITION_CUMANA
	adfspart_check_CUMANA,
#endif
#ifdef CONFIG_ACORN_PARTITION_ADFS
	adfspart_check_ADFS,
#endif

#ifdef CONFIG_CMDLINE_PARTITION
	cmdline_partition,
#endif
#ifdef CONFIG_EFI_PARTITION
	efi_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_SGI_PARTITION
	sgi_partition,
#endif
#ifdef CONFIG_LDM_PARTITION
	ldm_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_MSDOS_PARTITION
	msdos_partition,
#endif
#ifdef CONFIG_OSF_PARTITION
	osf_partition,
#endif
#ifdef CONFIG_SUN_PARTITION
	sun_partition,
#endif
#ifdef CONFIG_AMIGA_PARTITION
	amiga_partition,
#endif
#ifdef CONFIG_ATARI_PARTITION
	atari_partition,
#endif
#ifdef CONFIG_MAC_PARTITION
	mac_partition,
#endif
#ifdef CONFIG_ULTRIX_PARTITION
	ultrix_partition,
#endif
#ifdef CONFIG_IBM_PARTITION
	ibm_partition,
#endif
#ifdef CONFIG_KARMA_PARTITION
	karma_partition,
#endif
#ifdef CONFIG_SYSV68_PARTITION
	sysv68_partition,
#endif
	NULL
};

static struct parsed_partitions *allocate_partitions(struct gendisk *hd)
{
	struct parsed_partitions *state;
	int nr;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	nr = disk_max_parts(hd);
	state->parts = vzalloc(array_size(nr, sizeof(state->parts[0])));
	if (!state->parts) {
		kfree(state);
		return NULL;
	}

	state->limit = nr;

	return state;
}

static void free_partitions(struct parsed_partitions *state)
{
	vfree(state->parts);
	kfree(state);
}

static struct parsed_partitions *check_partition(struct gendisk *hd,
		struct block_device *bdev)
{
	struct parsed_partitions *state;
	int i, res, err;

	state = allocate_partitions(hd);
	if (!state)
		return NULL;
	state->pp_buf = (char *)__get_free_page(GFP_KERNEL);
	if (!state->pp_buf) {
		free_partitions(state);
		return NULL;
	}
	state->pp_buf[0] = '\0';

	state->bdev = bdev;
	disk_name(hd, 0, state->name);
	snprintf(state->pp_buf, PAGE_SIZE, " %s:", state->name);
	if (isdigit(state->name[strlen(state->name)-1]))
		sprintf(state->name, "p");

	i = res = err = 0;
	while (!res && check_part[i]) {
		memset(state->parts, 0, state->limit * sizeof(state->parts[0]));
		res = check_part[i++](state);
		if (res < 0) {
			/*
			 * We have hit an I/O error which we don't report now.
			 * But record it, and let the others do their job.
			 */
			err = res;
			res = 0;
		}

	}
	if (res > 0) {
		printk(KERN_INFO "%s", state->pp_buf);

		free_page((unsigned long)state->pp_buf);
		return state;
	}
	if (state->access_beyond_eod)
		err = -ENOSPC;
	/*
	 * The partition is unrecognized. So report I/O errors if there were any
	 */
	if (err)
		res = err;
	if (res) {
		strlcat(state->pp_buf,
			" unable to read partition table\n", PAGE_SIZE);
		printk(KERN_INFO "%s", state->pp_buf);
	}

	free_page((unsigned long)state->pp_buf);
	free_partitions(state);
	return ERR_PTR(res);
}

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

static DEVICE_ATTR(partition, 0444, part_partition_show, NULL);
static DEVICE_ATTR(start, 0444, part_start_show, NULL);
static DEVICE_ATTR(size, 0444, part_size_show, NULL);
static DEVICE_ATTR(ro, 0444, part_ro_show, NULL);
static DEVICE_ATTR(alignment_offset, 0444, part_alignment_offset_show, NULL);
static DEVICE_ATTR(discard_alignment, 0444, part_discard_alignment_show, NULL);
static DEVICE_ATTR(stat, 0444, part_stat_show, NULL);
static DEVICE_ATTR(inflight, 0444, part_inflight_show, NULL);
#ifdef CONFIG_FAIL_MAKE_REQUEST
static struct device_attribute dev_attr_fail =
	__ATTR(make-it-fail, 0644, part_fail_show, part_fail_store);
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

static void hd_struct_free_work(struct work_struct *work)
{
	struct hd_struct *part =
		container_of(to_rcu_work(work), struct hd_struct, rcu_work);

	part->start_sect = 0;
	part->nr_sects = 0;
	part_stat_set_all(part, 0);
	put_device(part_to_dev(part));
}

static void hd_struct_free(struct percpu_ref *ref)
{
	struct hd_struct *part = container_of(ref, struct hd_struct, ref);
	struct gendisk *disk = part_to_disk(part);
	struct disk_part_tbl *ptbl =
		rcu_dereference_protected(disk->part_tbl, 1);

	rcu_assign_pointer(ptbl->last_lookup, NULL);
	put_device(disk_to_dev(disk));

	INIT_RCU_WORK(&part->rcu_work, hd_struct_free_work);
	queue_rcu_work(system_wq, &part->rcu_work);
}

int hd_ref_init(struct hd_struct *part)
{
	if (percpu_ref_init(&part->ref, hd_struct_free, 0, GFP_KERNEL))
		return -ENOMEM;
	return 0;
}

/*
 * Must be called either with bd_mutex held, before a disk can be opened or
 * after all disk users are gone.
 */
void delete_partition(struct gendisk *disk, struct hd_struct *part)
{
	struct disk_part_tbl *ptbl =
		rcu_dereference_protected(disk->part_tbl, 1);

	/*
	 * ->part_tbl is referenced in this part's release handler, so
	 *  we have to hold the disk device
	 */
	get_device(disk_to_dev(part_to_disk(part)));
	rcu_assign_pointer(ptbl->part[part->partno], NULL);
	kobject_put(part->holder_dir);
	device_del(part_to_dev(part));

	/*
	 * Remove gendisk pointer from idr so that it cannot be looked up
	 * while RCU period before freeing gendisk is running to prevent
	 * use-after-free issues. Note that the device number stays
	 * "in-use" until we really free the gendisk.
	 */
	blk_invalidate_devt(part_devt(part));
	percpu_ref_kill(&part->ref);
}

static ssize_t whole_disk_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}
static DEVICE_ATTR(whole_disk, 0444, whole_disk_show, NULL);

/*
 * Must be called either with bd_mutex held, before a disk can be opened or
 * after all disk users are gone.
 */
static struct hd_struct *add_partition(struct gendisk *disk, int partno,
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

	/*
	 * Partitions are not supported on zoned block devices that are used as
	 * such.
	 */
	switch (disk->queue->limits.zoned) {
	case BLK_ZONED_HM:
		pr_warn("%s: partitions not supported on host managed zoned block device\n",
			disk->disk_name);
		return ERR_PTR(-ENXIO);
	case BLK_ZONED_HA:
		pr_info("%s: disabling host aware zoned block device support due to partitions\n",
			disk->disk_name);
		disk->queue->limits.zoned = BLK_ZONED_NONE;
		break;
	case BLK_ZONED_NONE:
		break;
	}

	err = disk_expand_part_tbl(disk, partno);
	if (err)
		return ERR_PTR(err);
	ptbl = rcu_dereference_protected(disk->part_tbl, 1);

	if (ptbl->part[partno])
		return ERR_PTR(-EBUSY);

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-EBUSY);

	p->dkstats = alloc_percpu(struct disk_stats);
	if (!p->dkstats) {
		err = -ENOMEM;
		goto out_free;
	}

	hd_sects_seq_init(p);
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
		struct partition_meta_info *pinfo;

		pinfo = kzalloc_node(sizeof(*pinfo), GFP_KERNEL, disk->node_id);
		if (!pinfo) {
			err = -ENOMEM;
			goto out_free_stats;
		}
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

	err = hd_ref_init(p);
	if (err) {
		if (flags & ADDPART_FLAG_WHOLEDISK)
			goto out_remove_file;
		goto out_del;
	}

	/* everything is up and running, commence */
	rcu_assign_pointer(ptbl->part[partno], p);

	/* suppress uevent if the disk suppresses it */
	if (!dev_get_uevent_suppress(ddev))
		kobject_uevent(&pdev->kobj, KOBJ_ADD);
	return p;

out_free_info:
	kfree(p->info);
out_free_stats:
	free_percpu(p->dkstats);
out_free:
	kfree(p);
	return ERR_PTR(err);
out_remove_file:
	device_remove_file(pdev, &dev_attr_whole_disk);
out_del:
	kobject_put(p->holder_dir);
	device_del(pdev);
out_put:
	put_device(pdev);
	return ERR_PTR(err);
}

static bool partition_overlaps(struct gendisk *disk, sector_t start,
		sector_t length, int skip_partno)
{
	struct disk_part_iter piter;
	struct hd_struct *part;
	bool overlap = false;

	disk_part_iter_init(&piter, disk, DISK_PITER_INCL_EMPTY);
	while ((part = disk_part_iter_next(&piter))) {
		if (part->partno == skip_partno ||
		    start >= part->start_sect + part->nr_sects ||
		    start + length <= part->start_sect)
			continue;
		overlap = true;
		break;
	}

	disk_part_iter_exit(&piter);
	return overlap;
}

int bdev_add_partition(struct block_device *bdev, int partno,
		sector_t start, sector_t length)
{
	struct hd_struct *part;

	mutex_lock(&bdev->bd_mutex);
	if (partition_overlaps(bdev->bd_disk, start, length, -1)) {
		mutex_unlock(&bdev->bd_mutex);
		return -EBUSY;
	}

	part = add_partition(bdev->bd_disk, partno, start, length,
			ADDPART_FLAG_NONE, NULL);
	mutex_unlock(&bdev->bd_mutex);
	return PTR_ERR_OR_ZERO(part);
}

int bdev_del_partition(struct block_device *bdev, int partno)
{
	struct block_device *bdevp;
	struct hd_struct *part;
	int ret = 0;

	part = disk_get_part(bdev->bd_disk, partno);
	if (!part)
		return -ENXIO;

	ret = -ENOMEM;
	bdevp = bdget(part_devt(part));
	if (!bdevp)
		goto out_put_part;

	mutex_lock(&bdevp->bd_mutex);

	ret = -EBUSY;
	if (bdevp->bd_openers)
		goto out_unlock;

	sync_blockdev(bdevp);
	invalidate_bdev(bdevp);

	mutex_lock_nested(&bdev->bd_mutex, 1);
	delete_partition(bdev->bd_disk, part);
	mutex_unlock(&bdev->bd_mutex);

	ret = 0;
out_unlock:
	mutex_unlock(&bdevp->bd_mutex);
	bdput(bdevp);
out_put_part:
	disk_put_part(part);
	return ret;
}

int bdev_resize_partition(struct block_device *bdev, int partno,
		sector_t start, sector_t length)
{
	struct block_device *bdevp;
	struct hd_struct *part;
	int ret = 0;

	part = disk_get_part(bdev->bd_disk, partno);
	if (!part)
		return -ENXIO;

	ret = -ENOMEM;
	bdevp = bdget(part_devt(part));
	if (!bdevp)
		goto out_put_part;

	mutex_lock(&bdevp->bd_mutex);
	mutex_lock_nested(&bdev->bd_mutex, 1);

	ret = -EINVAL;
	if (start != part->start_sect)
		goto out_unlock;

	ret = -EBUSY;
	if (partition_overlaps(bdev->bd_disk, start, length, partno))
		goto out_unlock;

	part_nr_sects_write(part, (sector_t)length);
	i_size_write(bdevp->bd_inode, length << SECTOR_SHIFT);

	ret = 0;
out_unlock:
	mutex_unlock(&bdevp->bd_mutex);
	mutex_unlock(&bdev->bd_mutex);
	bdput(bdevp);
out_put_part:
	disk_put_part(part);
	return ret;
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

int blk_drop_partitions(struct block_device *bdev)
{
	struct disk_part_iter piter;
	struct hd_struct *part;

	if (!disk_part_scan_enabled(bdev->bd_disk))
		return 0;
	if (bdev->bd_part_count)
		return -EBUSY;

	sync_blockdev(bdev);
	invalidate_bdev(bdev);

	disk_part_iter_init(&piter, bdev->bd_disk, DISK_PITER_INCL_EMPTY);
	while ((part = disk_part_iter_next(&piter)))
		delete_partition(bdev->bd_disk, part);
	disk_part_iter_exit(&piter);

	return 0;
}
#ifdef CONFIG_S390
/* for historic reasons in the DASD driver */
EXPORT_SYMBOL_GPL(blk_drop_partitions);
#endif

static bool blk_add_partition(struct gendisk *disk, struct block_device *bdev,
		struct parsed_partitions *state, int p)
{
	sector_t size = state->parts[p].size;
	sector_t from = state->parts[p].from;
	struct hd_struct *part;

	if (!size)
		return true;

	if (from >= get_capacity(disk)) {
		printk(KERN_WARNING
		       "%s: p%d start %llu is beyond EOD, ",
		       disk->disk_name, p, (unsigned long long) from);
		if (disk_unlock_native_capacity(disk))
			return false;
		return true;
	}

	if (from + size > get_capacity(disk)) {
		printk(KERN_WARNING
		       "%s: p%d size %llu extends beyond EOD, ",
		       disk->disk_name, p, (unsigned long long) size);

		if (disk_unlock_native_capacity(disk))
			return false;

		/*
		 * We can not ignore partitions of broken tables created by for
		 * example camera firmware, but we limit them to the end of the
		 * disk to avoid creating invalid block devices.
		 */
		size = get_capacity(disk) - from;
	}

	part = add_partition(disk, p, from, size, state->parts[p].flags,
			     &state->parts[p].info);
	if (IS_ERR(part) && PTR_ERR(part) != -ENXIO) {
		printk(KERN_ERR " %s: p%d could not be added: %ld\n",
		       disk->disk_name, p, -PTR_ERR(part));
		return true;
	}

	if (IS_BUILTIN(CONFIG_BLK_DEV_MD) &&
	    (state->parts[p].flags & ADDPART_FLAG_RAID))
		md_autodetect_dev(part_to_dev(part)->devt);

	return true;
}

int blk_add_partitions(struct gendisk *disk, struct block_device *bdev)
{
	struct parsed_partitions *state;
	int ret = -EAGAIN, p, highest;

	if (!disk_part_scan_enabled(disk))
		return 0;

	state = check_partition(disk, bdev);
	if (!state)
		return 0;
	if (IS_ERR(state)) {
		/*
		 * I/O error reading the partition table.  If we tried to read
		 * beyond EOD, retry after unlocking the native capacity.
		 */
		if (PTR_ERR(state) == -ENOSPC) {
			printk(KERN_WARNING "%s: partition table beyond EOD, ",
			       disk->disk_name);
			if (disk_unlock_native_capacity(disk))
				return -EAGAIN;
		}
		return -EIO;
	}

	/*
	 * Partitions are not supported on host managed zoned block devices.
	 */
	if (disk->queue->limits.zoned == BLK_ZONED_HM) {
		pr_warn("%s: ignoring partition table on host managed zoned block device\n",
			disk->disk_name);
		ret = 0;
		goto out_free_state;
	}

	/*
	 * If we read beyond EOD, try unlocking native capacity even if the
	 * partition table was successfully read as we could be missing some
	 * partitions.
	 */
	if (state->access_beyond_eod) {
		printk(KERN_WARNING
		       "%s: partition table partially beyond EOD, ",
		       disk->disk_name);
		if (disk_unlock_native_capacity(disk))
			goto out_free_state;
	}

	/* tell userspace that the media / partition table may have changed */
	kobject_uevent(&disk_to_dev(disk)->kobj, KOBJ_CHANGE);

	/*
	 * Detect the highest partition number and preallocate disk->part_tbl.
	 * This is an optimization and not strictly necessary.
	 */
	for (p = 1, highest = 0; p < state->limit; p++)
		if (state->parts[p].size)
			highest = p;
	disk_expand_part_tbl(disk, highest);

	for (p = 1; p < state->limit; p++)
		if (!blk_add_partition(disk, bdev, state, p))
			goto out_free_state;

	ret = 0;
out_free_state:
	free_partitions(state);
	return ret;
}

void *read_part_sector(struct parsed_partitions *state, sector_t n, Sector *p)
{
	struct address_space *mapping = state->bdev->bd_inode->i_mapping;
	struct page *page;

	if (n >= get_capacity(state->bdev->bd_disk)) {
		state->access_beyond_eod = true;
		return NULL;
	}

	page = read_mapping_page(mapping,
			(pgoff_t)(n >> (PAGE_SHIFT - 9)), NULL);
	if (IS_ERR(page))
		goto out;
	if (PageError(page))
		goto out_put_page;

	p->v = page;
	return (unsigned char *)page_address(page) +
			((n & ((1 << (PAGE_SHIFT - 9)) - 1)) << SECTOR_SHIFT);
out_put_page:
	put_page(page);
out:
	p->v = NULL;
	return NULL;
}
