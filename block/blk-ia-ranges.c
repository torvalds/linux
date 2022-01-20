// SPDX-License-Identifier: GPL-2.0
/*
 *  Block device concurrent positioning ranges.
 *
 *  Copyright (C) 2021 Western Digital Corporation or its Affiliates.
 */
#include <linux/kernel.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/init.h>

#include "blk.h"

static ssize_t
blk_ia_range_sector_show(struct blk_independent_access_range *iar,
			 char *buf)
{
	return sprintf(buf, "%llu\n", iar->sector);
}

static ssize_t
blk_ia_range_nr_sectors_show(struct blk_independent_access_range *iar,
			     char *buf)
{
	return sprintf(buf, "%llu\n", iar->nr_sectors);
}

struct blk_ia_range_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct blk_independent_access_range *iar, char *buf);
};

static struct blk_ia_range_sysfs_entry blk_ia_range_sector_entry = {
	.attr = { .name = "sector", .mode = 0444 },
	.show = blk_ia_range_sector_show,
};

static struct blk_ia_range_sysfs_entry blk_ia_range_nr_sectors_entry = {
	.attr = { .name = "nr_sectors", .mode = 0444 },
	.show = blk_ia_range_nr_sectors_show,
};

static struct attribute *blk_ia_range_attrs[] = {
	&blk_ia_range_sector_entry.attr,
	&blk_ia_range_nr_sectors_entry.attr,
	NULL,
};
ATTRIBUTE_GROUPS(blk_ia_range);

static ssize_t blk_ia_range_sysfs_show(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	struct blk_ia_range_sysfs_entry *entry =
		container_of(attr, struct blk_ia_range_sysfs_entry, attr);
	struct blk_independent_access_range *iar =
		container_of(kobj, struct blk_independent_access_range, kobj);
	ssize_t ret;

	mutex_lock(&iar->queue->sysfs_lock);
	ret = entry->show(iar, buf);
	mutex_unlock(&iar->queue->sysfs_lock);

	return ret;
}

static const struct sysfs_ops blk_ia_range_sysfs_ops = {
	.show	= blk_ia_range_sysfs_show,
};

/*
 * Independent access range entries are not freed individually, but alltogether
 * with struct blk_independent_access_ranges and its array of ranges. Since
 * kobject_add() takes a reference on the parent kobject contained in
 * struct blk_independent_access_ranges, the array of independent access range
 * entries cannot be freed until kobject_del() is called for all entries.
 * So we do not need to do anything here, but still need this no-op release
 * operation to avoid complaints from the kobject code.
 */
static void blk_ia_range_sysfs_nop_release(struct kobject *kobj)
{
}

static struct kobj_type blk_ia_range_ktype = {
	.sysfs_ops	= &blk_ia_range_sysfs_ops,
	.default_groups	= blk_ia_range_groups,
	.release	= blk_ia_range_sysfs_nop_release,
};

/*
 * This will be executed only after all independent access range entries are
 * removed with kobject_del(), at which point, it is safe to free everything,
 * including the array of ranges.
 */
static void blk_ia_ranges_sysfs_release(struct kobject *kobj)
{
	struct blk_independent_access_ranges *iars =
		container_of(kobj, struct blk_independent_access_ranges, kobj);

	kfree(iars);
}

static struct kobj_type blk_ia_ranges_ktype = {
	.release	= blk_ia_ranges_sysfs_release,
};

/**
 * disk_register_independent_access_ranges - register with sysfs a set of
 *		independent access ranges
 * @disk:	Target disk
 * @new_iars:	New set of independent access ranges
 *
 * Register with sysfs a set of independent access ranges for @disk.
 * If @new_iars is not NULL, this set of ranges is registered and the old set
 * specified by q->ia_ranges is unregistered. Otherwise, q->ia_ranges is
 * registered if it is not already.
 */
int disk_register_independent_access_ranges(struct gendisk *disk,
				struct blk_independent_access_ranges *new_iars)
{
	struct request_queue *q = disk->queue;
	struct blk_independent_access_ranges *iars;
	int i, ret;

	lockdep_assert_held(&q->sysfs_dir_lock);
	lockdep_assert_held(&q->sysfs_lock);

	/* If a new range set is specified, unregister the old one */
	if (new_iars) {
		if (q->ia_ranges)
			disk_unregister_independent_access_ranges(disk);
		q->ia_ranges = new_iars;
	}

	iars = q->ia_ranges;
	if (!iars)
		return 0;

	/*
	 * At this point, iars is the new set of sector access ranges that needs
	 * to be registered with sysfs.
	 */
	WARN_ON(iars->sysfs_registered);
	ret = kobject_init_and_add(&iars->kobj, &blk_ia_ranges_ktype,
				   &q->kobj, "%s", "independent_access_ranges");
	if (ret) {
		q->ia_ranges = NULL;
		kobject_put(&iars->kobj);
		return ret;
	}

	for (i = 0; i < iars->nr_ia_ranges; i++) {
		iars->ia_range[i].queue = q;
		ret = kobject_init_and_add(&iars->ia_range[i].kobj,
					   &blk_ia_range_ktype, &iars->kobj,
					   "%d", i);
		if (ret) {
			while (--i >= 0)
				kobject_del(&iars->ia_range[i].kobj);
			kobject_del(&iars->kobj);
			kobject_put(&iars->kobj);
			return ret;
		}
	}

	iars->sysfs_registered = true;

	return 0;
}

void disk_unregister_independent_access_ranges(struct gendisk *disk)
{
	struct request_queue *q = disk->queue;
	struct blk_independent_access_ranges *iars = q->ia_ranges;
	int i;

	lockdep_assert_held(&q->sysfs_dir_lock);
	lockdep_assert_held(&q->sysfs_lock);

	if (!iars)
		return;

	if (iars->sysfs_registered) {
		for (i = 0; i < iars->nr_ia_ranges; i++)
			kobject_del(&iars->ia_range[i].kobj);
		kobject_del(&iars->kobj);
		kobject_put(&iars->kobj);
	} else {
		kfree(iars);
	}

	q->ia_ranges = NULL;
}

static struct blk_independent_access_range *
disk_find_ia_range(struct blk_independent_access_ranges *iars,
		  sector_t sector)
{
	struct blk_independent_access_range *iar;
	int i;

	for (i = 0; i < iars->nr_ia_ranges; i++) {
		iar = &iars->ia_range[i];
		if (sector >= iar->sector &&
		    sector < iar->sector + iar->nr_sectors)
			return iar;
	}

	return NULL;
}

static bool disk_check_ia_ranges(struct gendisk *disk,
				struct blk_independent_access_ranges *iars)
{
	struct blk_independent_access_range *iar, *tmp;
	sector_t capacity = get_capacity(disk);
	sector_t sector = 0;
	int i;

	/*
	 * While sorting the ranges in increasing LBA order, check that the
	 * ranges do not overlap, that there are no sector holes and that all
	 * sectors belong to one range.
	 */
	for (i = 0; i < iars->nr_ia_ranges; i++) {
		tmp = disk_find_ia_range(iars, sector);
		if (!tmp || tmp->sector != sector) {
			pr_warn("Invalid non-contiguous independent access ranges\n");
			return false;
		}

		iar = &iars->ia_range[i];
		if (tmp != iar) {
			swap(iar->sector, tmp->sector);
			swap(iar->nr_sectors, tmp->nr_sectors);
		}

		sector += iar->nr_sectors;
	}

	if (sector != capacity) {
		pr_warn("Independent access ranges do not match disk capacity\n");
		return false;
	}

	return true;
}

static bool disk_ia_ranges_changed(struct gendisk *disk,
				   struct blk_independent_access_ranges *new)
{
	struct blk_independent_access_ranges *old = disk->queue->ia_ranges;
	int i;

	if (!old)
		return true;

	if (old->nr_ia_ranges != new->nr_ia_ranges)
		return true;

	for (i = 0; i < old->nr_ia_ranges; i++) {
		if (new->ia_range[i].sector != old->ia_range[i].sector ||
		    new->ia_range[i].nr_sectors != old->ia_range[i].nr_sectors)
			return true;
	}

	return false;
}

/**
 * disk_alloc_independent_access_ranges - Allocate an independent access ranges
 *                                        data structure
 * @disk:		target disk
 * @nr_ia_ranges:	Number of independent access ranges
 *
 * Allocate a struct blk_independent_access_ranges structure with @nr_ia_ranges
 * access range descriptors.
 */
struct blk_independent_access_ranges *
disk_alloc_independent_access_ranges(struct gendisk *disk, int nr_ia_ranges)
{
	struct blk_independent_access_ranges *iars;

	iars = kzalloc_node(struct_size(iars, ia_range, nr_ia_ranges),
			    GFP_KERNEL, disk->queue->node);
	if (iars)
		iars->nr_ia_ranges = nr_ia_ranges;
	return iars;
}
EXPORT_SYMBOL_GPL(disk_alloc_independent_access_ranges);

/**
 * disk_set_independent_access_ranges - Set a disk independent access ranges
 * @disk:	target disk
 * @iars:	independent access ranges structure
 *
 * Set the independent access ranges information of the request queue
 * of @disk to @iars. If @iars is NULL and the independent access ranges
 * structure already set is cleared. If there are no differences between
 * @iars and the independent access ranges structure already set, @iars
 * is freed.
 */
void disk_set_independent_access_ranges(struct gendisk *disk,
				struct blk_independent_access_ranges *iars)
{
	struct request_queue *q = disk->queue;

	if (WARN_ON_ONCE(iars && !iars->nr_ia_ranges)) {
		kfree(iars);
		iars = NULL;
	}

	mutex_lock(&q->sysfs_dir_lock);
	mutex_lock(&q->sysfs_lock);

	if (iars) {
		if (!disk_check_ia_ranges(disk, iars)) {
			kfree(iars);
			iars = NULL;
			goto reg;
		}

		if (!disk_ia_ranges_changed(disk, iars)) {
			kfree(iars);
			goto unlock;
		}
	}

	/*
	 * This may be called for a registered queue. E.g. during a device
	 * revalidation. If that is the case, we need to unregister the old
	 * set of independent access ranges and register the new set. If the
	 * queue is not registered, registration of the device request queue
	 * will register the independent access ranges, so only swap in the
	 * new set and free the old one.
	 */
reg:
	if (blk_queue_registered(q)) {
		disk_register_independent_access_ranges(disk, iars);
	} else {
		swap(q->ia_ranges, iars);
		kfree(iars);
	}

unlock:
	mutex_unlock(&q->sysfs_lock);
	mutex_unlock(&q->sysfs_dir_lock);
}
EXPORT_SYMBOL_GPL(disk_set_independent_access_ranges);
