// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 1991-1998  Linus Torvalds
 * Re-organised Feb 1998 Russell King
 * Copyright (C) 2020 Christoph Hellwig
 */
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/vmalloc.h>
#include <linux/raid/detect.h>
#include "check.h"

static int (*const check_part[])(struct parsed_partitions *) = {
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
#ifdef CONFIG_OF_PARTITION
	of_partition,		/* cmdline have priority to OF */
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
	int nr = DISK_MAX_PARTS;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

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

static struct parsed_partitions *check_partition(struct gendisk *hd)
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

	state->disk = hd;
	snprintf(state->name, BDEVNAME_SIZE, "%s", hd->disk_name);
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
	return sprintf(buf, "%d\n", bdev_partno(dev_to_bdev(dev)));
}

static ssize_t part_start_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", dev_to_bdev(dev)->bd_start_sect);
}

static ssize_t part_ro_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", bdev_read_only(dev_to_bdev(dev)));
}

static ssize_t part_alignment_offset_show(struct device *dev,
					  struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", bdev_alignment_offset(dev_to_bdev(dev)));
}

static ssize_t part_discard_alignment_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", bdev_discard_alignment(dev_to_bdev(dev)));
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

static const struct attribute_group part_attr_group = {
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
	put_disk(dev_to_bdev(dev)->bd_disk);
	bdev_drop(dev_to_bdev(dev));
}

static int part_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct block_device *part = dev_to_bdev(dev);

	add_uevent_var(env, "PARTN=%u", bdev_partno(part));
	if (part->bd_meta_info && part->bd_meta_info->volname[0])
		add_uevent_var(env, "PARTNAME=%s", part->bd_meta_info->volname);
	return 0;
}

const struct device_type part_type = {
	.name		= "partition",
	.groups		= part_attr_groups,
	.release	= part_release,
	.uevent		= part_uevent,
};

void drop_partition(struct block_device *part)
{
	lockdep_assert_held(&part->bd_disk->open_mutex);

	xa_erase(&part->bd_disk->part_tbl, bdev_partno(part));
	kobject_put(part->bd_holder_dir);

	device_del(&part->bd_device);
	put_device(&part->bd_device);
}

static ssize_t whole_disk_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return 0;
}
static const DEVICE_ATTR(whole_disk, 0444, whole_disk_show, NULL);

/*
 * Must be called either with open_mutex held, before a disk can be opened or
 * after all disk users are gone.
 */
static struct block_device *add_partition(struct gendisk *disk, int partno,
				sector_t start, sector_t len, int flags,
				struct partition_meta_info *info)
{
	dev_t devt = MKDEV(0, 0);
	struct device *ddev = disk_to_dev(disk);
	struct device *pdev;
	struct block_device *bdev;
	const char *dname;
	int err;

	lockdep_assert_held(&disk->open_mutex);

	if (partno >= DISK_MAX_PARTS)
		return ERR_PTR(-EINVAL);

	/*
	 * Partitions are not supported on zoned block devices that are used as
	 * such.
	 */
	if (bdev_is_zoned(disk->part0)) {
		pr_warn("%s: partitions not supported on host managed zoned block device\n",
			disk->disk_name);
		return ERR_PTR(-ENXIO);
	}

	if (xa_load(&disk->part_tbl, partno))
		return ERR_PTR(-EBUSY);

	/* ensure we always have a reference to the whole disk */
	get_device(disk_to_dev(disk));

	err = -ENOMEM;
	bdev = bdev_alloc(disk, partno);
	if (!bdev)
		goto out_put_disk;

	bdev->bd_start_sect = start;
	bdev_set_nr_sectors(bdev, len);

	pdev = &bdev->bd_device;
	dname = dev_name(ddev);
	if (isdigit(dname[strlen(dname) - 1]))
		dev_set_name(pdev, "%sp%d", dname, partno);
	else
		dev_set_name(pdev, "%s%d", dname, partno);

	device_initialize(pdev);
	pdev->class = &block_class;
	pdev->type = &part_type;
	pdev->parent = ddev;

	/* in consecutive minor range? */
	if (bdev_partno(bdev) < disk->minors) {
		devt = MKDEV(disk->major, disk->first_minor + bdev_partno(bdev));
	} else {
		err = blk_alloc_ext_minor();
		if (err < 0)
			goto out_put;
		devt = MKDEV(BLOCK_EXT_MAJOR, err);
	}
	pdev->devt = devt;

	if (info) {
		err = -ENOMEM;
		bdev->bd_meta_info = kmemdup(info, sizeof(*info), GFP_KERNEL);
		if (!bdev->bd_meta_info)
			goto out_put;
	}

	/* delay uevent until 'holders' subdir is created */
	dev_set_uevent_suppress(pdev, 1);
	err = device_add(pdev);
	if (err)
		goto out_put;

	err = -ENOMEM;
	bdev->bd_holder_dir = kobject_create_and_add("holders", &pdev->kobj);
	if (!bdev->bd_holder_dir)
		goto out_del;

	dev_set_uevent_suppress(pdev, 0);
	if (flags & ADDPART_FLAG_WHOLEDISK) {
		err = device_create_file(pdev, &dev_attr_whole_disk);
		if (err)
			goto out_del;
	}

	if (flags & ADDPART_FLAG_READONLY)
		bdev_set_flag(bdev, BD_READ_ONLY);

	/* everything is up and running, commence */
	err = xa_insert(&disk->part_tbl, partno, bdev, GFP_KERNEL);
	if (err)
		goto out_del;
	bdev_add(bdev, devt);

	/* suppress uevent if the disk suppresses it */
	if (!dev_get_uevent_suppress(ddev))
		kobject_uevent(&pdev->kobj, KOBJ_ADD);
	return bdev;

out_del:
	kobject_put(bdev->bd_holder_dir);
	device_del(pdev);
out_put:
	put_device(pdev);
	return ERR_PTR(err);
out_put_disk:
	put_disk(disk);
	return ERR_PTR(err);
}

static bool partition_overlaps(struct gendisk *disk, sector_t start,
		sector_t length, int skip_partno)
{
	struct block_device *part;
	bool overlap = false;
	unsigned long idx;

	rcu_read_lock();
	xa_for_each_start(&disk->part_tbl, idx, part, 1) {
		if (bdev_partno(part) != skip_partno &&
		    start < part->bd_start_sect + bdev_nr_sectors(part) &&
		    start + length > part->bd_start_sect) {
			overlap = true;
			break;
		}
	}
	rcu_read_unlock();

	return overlap;
}

int bdev_add_partition(struct gendisk *disk, int partno, sector_t start,
		sector_t length)
{
	struct block_device *part;
	int ret;

	mutex_lock(&disk->open_mutex);
	if (!disk_live(disk)) {
		ret = -ENXIO;
		goto out;
	}

	if (disk->flags & GENHD_FL_NO_PART) {
		ret = -EINVAL;
		goto out;
	}

	if (partition_overlaps(disk, start, length, -1)) {
		ret = -EBUSY;
		goto out;
	}

	part = add_partition(disk, partno, start, length,
			ADDPART_FLAG_NONE, NULL);
	ret = PTR_ERR_OR_ZERO(part);
out:
	mutex_unlock(&disk->open_mutex);
	return ret;
}

int bdev_del_partition(struct gendisk *disk, int partno)
{
	struct block_device *part = NULL;
	int ret = -ENXIO;

	mutex_lock(&disk->open_mutex);
	part = xa_load(&disk->part_tbl, partno);
	if (!part)
		goto out_unlock;

	ret = -EBUSY;
	if (atomic_read(&part->bd_openers))
		goto out_unlock;

	/*
	 * We verified that @part->bd_openers is zero above and so
	 * @part->bd_holder{_ops} can't be set. And since we hold
	 * @disk->open_mutex the device can't be claimed by anyone.
	 *
	 * So no need to call @part->bd_holder_ops->mark_dead() here.
	 * Just delete the partition and invalidate it.
	 */

	bdev_unhash(part);
	invalidate_bdev(part);
	drop_partition(part);
	ret = 0;
out_unlock:
	mutex_unlock(&disk->open_mutex);
	return ret;
}

int bdev_resize_partition(struct gendisk *disk, int partno, sector_t start,
		sector_t length)
{
	struct block_device *part = NULL;
	int ret = -ENXIO;

	mutex_lock(&disk->open_mutex);
	part = xa_load(&disk->part_tbl, partno);
	if (!part)
		goto out_unlock;

	ret = -EINVAL;
	if (start != part->bd_start_sect)
		goto out_unlock;

	ret = -EBUSY;
	if (partition_overlaps(disk, start, length, partno))
		goto out_unlock;

	bdev_set_nr_sectors(part, length);

	ret = 0;
out_unlock:
	mutex_unlock(&disk->open_mutex);
	return ret;
}

static bool disk_unlock_native_capacity(struct gendisk *disk)
{
	if (!disk->fops->unlock_native_capacity ||
	    test_and_set_bit(GD_NATIVE_CAPACITY, &disk->state)) {
		printk(KERN_CONT "truncated\n");
		return false;
	}

	printk(KERN_CONT "enabling native capacity\n");
	disk->fops->unlock_native_capacity(disk);
	return true;
}

static bool blk_add_partition(struct gendisk *disk,
		struct parsed_partitions *state, int p)
{
	sector_t size = state->parts[p].size;
	sector_t from = state->parts[p].from;
	struct block_device *part;

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
	if (IS_ERR(part)) {
		if (PTR_ERR(part) != -ENXIO) {
			printk(KERN_ERR " %s: p%d could not be added: %pe\n",
			       disk->disk_name, p, part);
		}
		return true;
	}

	if (IS_BUILTIN(CONFIG_BLK_DEV_MD) &&
	    (state->parts[p].flags & ADDPART_FLAG_RAID))
		md_autodetect_dev(part->bd_dev);

	return true;
}

static int blk_add_partitions(struct gendisk *disk)
{
	struct parsed_partitions *state;
	int ret = -EAGAIN, p;

	if (!disk_has_partscan(disk))
		return 0;

	state = check_partition(disk);
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
	if (bdev_is_zoned(disk->part0)) {
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

	for (p = 1; p < state->limit; p++)
		if (!blk_add_partition(disk, state, p))
			goto out_free_state;

	ret = 0;
out_free_state:
	free_partitions(state);
	return ret;
}

int bdev_disk_changed(struct gendisk *disk, bool invalidate)
{
	struct block_device *part;
	unsigned long idx;
	int ret = 0;

	lockdep_assert_held(&disk->open_mutex);

	if (!disk_live(disk))
		return -ENXIO;

rescan:
	if (disk->open_partitions)
		return -EBUSY;
	sync_blockdev(disk->part0);
	invalidate_bdev(disk->part0);

	xa_for_each_start(&disk->part_tbl, idx, part, 1) {
		/*
		 * Remove the block device from the inode hash, so that
		 * it cannot be looked up any more even when openers
		 * still hold references.
		 */
		bdev_unhash(part);

		/*
		 * If @disk->open_partitions isn't elevated but there's
		 * still an active holder of that block device things
		 * are broken.
		 */
		WARN_ON_ONCE(atomic_read(&part->bd_openers));
		invalidate_bdev(part);
		drop_partition(part);
	}
	clear_bit(GD_NEED_PART_SCAN, &disk->state);

	/*
	 * Historically we only set the capacity to zero for devices that
	 * support partitions (independ of actually having partitions created).
	 * Doing that is rather inconsistent, but changing it broke legacy
	 * udisks polling for legacy ide-cdrom devices.  Use the crude check
	 * below to get the sane behavior for most device while not breaking
	 * userspace for this particular setup.
	 */
	if (invalidate) {
		if (!(disk->flags & GENHD_FL_NO_PART) ||
		    !(disk->flags & GENHD_FL_REMOVABLE))
			set_capacity(disk, 0);
	}

	if (get_capacity(disk)) {
		ret = blk_add_partitions(disk);
		if (ret == -EAGAIN)
			goto rescan;
	} else if (invalidate) {
		/*
		 * Tell userspace that the media / partition table may have
		 * changed.
		 */
		kobject_uevent(&disk_to_dev(disk)->kobj, KOBJ_CHANGE);
	}

	return ret;
}
/*
 * Only exported for loop and dasd for historic reasons.  Don't use in new
 * code!
 */
EXPORT_SYMBOL_GPL(bdev_disk_changed);

void *read_part_sector(struct parsed_partitions *state, sector_t n, Sector *p)
{
	struct address_space *mapping = state->disk->part0->bd_mapping;
	struct folio *folio;

	if (n >= get_capacity(state->disk)) {
		state->access_beyond_eod = true;
		goto out;
	}

	folio = read_mapping_folio(mapping, n >> PAGE_SECTORS_SHIFT, NULL);
	if (IS_ERR(folio))
		goto out;

	p->v = folio;
	return folio_address(folio) + offset_in_folio(folio, n * SECTOR_SIZE);
out:
	p->v = NULL;
	return NULL;
}
