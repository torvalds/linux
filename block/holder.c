// SPDX-License-Identifier: GPL-2.0-only
#include <linux/blkdev.h>
#include <linux/slab.h>

struct bd_holder_disk {
	struct list_head	list;
	struct kobject		*holder_dir;
	int			refcnt;
};

static DEFINE_MUTEX(blk_holder_mutex);

static struct bd_holder_disk *bd_find_holder_disk(struct block_device *bdev,
						  struct gendisk *disk)
{
	struct bd_holder_disk *holder;

	list_for_each_entry(holder, &disk->slave_bdevs, list)
		if (holder->holder_dir == bdev->bd_holder_dir)
			return holder;
	return NULL;
}

static int add_symlink(struct kobject *from, struct kobject *to)
{
	return sysfs_create_link(from, to, kobject_name(to));
}

static void del_symlink(struct kobject *from, struct kobject *to)
{
	sysfs_remove_link(from, kobject_name(to));
}

/**
 * bd_link_disk_holder - create symlinks between holding disk and slave bdev
 * @bdev: the claimed slave bdev
 * @disk: the holding disk
 *
 * DON'T USE THIS UNLESS YOU'RE ALREADY USING IT.
 *
 * This functions creates the following sysfs symlinks.
 *
 * - from "slaves" directory of the holder @disk to the claimed @bdev
 * - from "holders" directory of the @bdev to the holder @disk
 *
 * For example, if /dev/dm-0 maps to /dev/sda and disk for dm-0 is
 * passed to bd_link_disk_holder(), then:
 *
 *   /sys/block/dm-0/slaves/sda --> /sys/block/sda
 *   /sys/block/sda/holders/dm-0 --> /sys/block/dm-0
 *
 * The caller must have claimed @bdev before calling this function and
 * ensure that both @bdev and @disk are valid during the creation and
 * lifetime of these symlinks.
 *
 * CONTEXT:
 * Might sleep.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int bd_link_disk_holder(struct block_device *bdev, struct gendisk *disk)
{
	struct bd_holder_disk *holder;
	int ret = 0;

	if (WARN_ON_ONCE(!disk->slave_dir))
		return -EINVAL;

	if (bdev->bd_disk == disk)
		return -EINVAL;

	/*
	 * del_gendisk drops the initial reference to bd_holder_dir, so we
	 * need to keep our own here to allow for cleanup past that point.
	 */
	mutex_lock(&bdev->bd_disk->open_mutex);
	if (!disk_live(bdev->bd_disk)) {
		mutex_unlock(&bdev->bd_disk->open_mutex);
		return -ENODEV;
	}
	kobject_get(bdev->bd_holder_dir);
	mutex_unlock(&bdev->bd_disk->open_mutex);

	mutex_lock(&blk_holder_mutex);
	WARN_ON_ONCE(!bdev->bd_holder);

	holder = bd_find_holder_disk(bdev, disk);
	if (holder) {
		kobject_put(bdev->bd_holder_dir);
		holder->refcnt++;
		goto out_unlock;
	}

	holder = kzalloc(sizeof(*holder), GFP_KERNEL);
	if (!holder) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	INIT_LIST_HEAD(&holder->list);
	holder->refcnt = 1;
	holder->holder_dir = bdev->bd_holder_dir;

	ret = add_symlink(disk->slave_dir, bdev_kobj(bdev));
	if (ret)
		goto out_free_holder;
	ret = add_symlink(bdev->bd_holder_dir, &disk_to_dev(disk)->kobj);
	if (ret)
		goto out_del_symlink;
	list_add(&holder->list, &disk->slave_bdevs);

	mutex_unlock(&blk_holder_mutex);
	return 0;

out_del_symlink:
	del_symlink(disk->slave_dir, bdev_kobj(bdev));
out_free_holder:
	kfree(holder);
out_unlock:
	mutex_unlock(&blk_holder_mutex);
	if (ret)
		kobject_put(bdev->bd_holder_dir);
	return ret;
}
EXPORT_SYMBOL_GPL(bd_link_disk_holder);

/**
 * bd_unlink_disk_holder - destroy symlinks created by bd_link_disk_holder()
 * @bdev: the calimed slave bdev
 * @disk: the holding disk
 *
 * DON'T USE THIS UNLESS YOU'RE ALREADY USING IT.
 *
 * CONTEXT:
 * Might sleep.
 */
void bd_unlink_disk_holder(struct block_device *bdev, struct gendisk *disk)
{
	struct bd_holder_disk *holder;

	if (WARN_ON_ONCE(!disk->slave_dir))
		return;

	mutex_lock(&blk_holder_mutex);
	holder = bd_find_holder_disk(bdev, disk);
	if (!WARN_ON_ONCE(holder == NULL) && !--holder->refcnt) {
		del_symlink(disk->slave_dir, bdev_kobj(bdev));
		del_symlink(holder->holder_dir, &disk_to_dev(disk)->kobj);
		kobject_put(holder->holder_dir);
		list_del_init(&holder->list);
		kfree(holder);
	}
	mutex_unlock(&blk_holder_mutex);
}
EXPORT_SYMBOL_GPL(bd_unlink_disk_holder);
