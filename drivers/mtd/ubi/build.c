/*
 * Copyright (c) International Business Machines Corp., 2006
 * Copyright (c) Nokia Corporation, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём),
 *         Frank Haverkamp
 */

/*
 * This file includes UBI initialization and building of UBI devices.
 *
 * When UBI is initialized, it attaches all the MTD devices specified as the
 * module load parameters or the kernel boot parameters. If MTD devices were
 * specified, UBI does not attach any MTD device, but it is possible to do
 * later using the "UBI control device".
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stringify.h>
#include <linux/namei.h>
#include <linux/stat.h>
#include <linux/miscdevice.h>
#include <linux/mtd/partitions.h>
#include <linux/log2.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "ubi.h"

/* Maximum length of the 'mtd=' parameter */
#define MTD_PARAM_LEN_MAX 64

/* Maximum number of comma-separated items in the 'mtd=' parameter */
#define MTD_PARAM_MAX_COUNT 3

/* Maximum value for the number of bad PEBs per 1024 PEBs */
#define MAX_MTD_UBI_BEB_LIMIT 768

#ifdef CONFIG_MTD_UBI_MODULE
#define ubi_is_module() 1
#else
#define ubi_is_module() 0
#endif

/**
 * struct mtd_dev_param - MTD device parameter description data structure.
 * @name: MTD character device node path, MTD device name, or MTD device number
 *        string
 * @vid_hdr_offs: VID header offset
 * @max_beb_per1024: maximum expected number of bad PEBs per 1024 PEBs
 */
struct mtd_dev_param {
	char name[MTD_PARAM_LEN_MAX];
	int vid_hdr_offs;
	int max_beb_per1024;
};

/* Numbers of elements set in the @mtd_dev_param array */
static int __initdata mtd_devs;

/* MTD devices specification parameters */
static struct mtd_dev_param __initdata mtd_dev_param[UBI_MAX_DEVICES];

/* Root UBI "class" object (corresponds to '/<sysfs>/class/ubi/') */
struct class *ubi_class;

/* Slab cache for wear-leveling entries */
struct kmem_cache *ubi_wl_entry_slab;

/* UBI control character device */
static struct miscdevice ubi_ctrl_cdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ubi_ctrl",
	.fops = &ubi_ctrl_cdev_operations,
};

/* All UBI devices in system */
static struct ubi_device *ubi_devices[UBI_MAX_DEVICES];

/* Serializes UBI devices creations and removals */
DEFINE_MUTEX(ubi_devices_mutex);

/* Protects @ubi_devices and @ubi->ref_count */
static DEFINE_SPINLOCK(ubi_devices_lock);

/* "Show" method for files in '/<sysfs>/class/ubi/' */
static ssize_t ubi_version_show(struct class *class,
				struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", UBI_VERSION);
}

/* UBI version attribute ('/<sysfs>/class/ubi/version') */
static struct class_attribute ubi_version =
	__ATTR(version, S_IRUGO, ubi_version_show, NULL);

static ssize_t dev_attribute_show(struct device *dev,
				  struct device_attribute *attr, char *buf);

/* UBI device attributes (correspond to files in '/<sysfs>/class/ubi/ubiX') */
static struct device_attribute dev_eraseblock_size =
	__ATTR(eraseblock_size, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_avail_eraseblocks =
	__ATTR(avail_eraseblocks, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_total_eraseblocks =
	__ATTR(total_eraseblocks, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_volumes_count =
	__ATTR(volumes_count, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_max_ec =
	__ATTR(max_ec, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_reserved_for_bad =
	__ATTR(reserved_for_bad, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_bad_peb_count =
	__ATTR(bad_peb_count, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_max_vol_count =
	__ATTR(max_vol_count, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_min_io_size =
	__ATTR(min_io_size, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_bgt_enabled =
	__ATTR(bgt_enabled, S_IRUGO, dev_attribute_show, NULL);
static struct device_attribute dev_mtd_num =
	__ATTR(mtd_num, S_IRUGO, dev_attribute_show, NULL);

/**
 * ubi_volume_notify - send a volume change notification.
 * @ubi: UBI device description object
 * @vol: volume description object of the changed volume
 * @ntype: notification type to send (%UBI_VOLUME_ADDED, etc)
 *
 * This is a helper function which notifies all subscribers about a volume
 * change event (creation, removal, re-sizing, re-naming, updating). Returns
 * zero in case of success and a negative error code in case of failure.
 */
int ubi_volume_notify(struct ubi_device *ubi, struct ubi_volume *vol, int ntype)
{
	struct ubi_notification nt;

	ubi_do_get_device_info(ubi, &nt.di);
	ubi_do_get_volume_info(ubi, vol, &nt.vi);
	return blocking_notifier_call_chain(&ubi_notifiers, ntype, &nt);
}

/**
 * ubi_notify_all - send a notification to all volumes.
 * @ubi: UBI device description object
 * @ntype: notification type to send (%UBI_VOLUME_ADDED, etc)
 * @nb: the notifier to call
 *
 * This function walks all volumes of UBI device @ubi and sends the @ntype
 * notification for each volume. If @nb is %NULL, then all registered notifiers
 * are called, otherwise only the @nb notifier is called. Returns the number of
 * sent notifications.
 */
int ubi_notify_all(struct ubi_device *ubi, int ntype, struct notifier_block *nb)
{
	struct ubi_notification nt;
	int i, count = 0;

	ubi_do_get_device_info(ubi, &nt.di);

	mutex_lock(&ubi->device_mutex);
	for (i = 0; i < ubi->vtbl_slots; i++) {
		/*
		 * Since the @ubi->device is locked, and we are not going to
		 * change @ubi->volumes, we do not have to lock
		 * @ubi->volumes_lock.
		 */
		if (!ubi->volumes[i])
			continue;

		ubi_do_get_volume_info(ubi, ubi->volumes[i], &nt.vi);
		if (nb)
			nb->notifier_call(nb, ntype, &nt);
		else
			blocking_notifier_call_chain(&ubi_notifiers, ntype,
						     &nt);
		count += 1;
	}
	mutex_unlock(&ubi->device_mutex);

	return count;
}

/**
 * ubi_enumerate_volumes - send "add" notification for all existing volumes.
 * @nb: the notifier to call
 *
 * This function walks all UBI devices and volumes and sends the
 * %UBI_VOLUME_ADDED notification for each volume. If @nb is %NULL, then all
 * registered notifiers are called, otherwise only the @nb notifier is called.
 * Returns the number of sent notifications.
 */
int ubi_enumerate_volumes(struct notifier_block *nb)
{
	int i, count = 0;

	/*
	 * Since the @ubi_devices_mutex is locked, and we are not going to
	 * change @ubi_devices, we do not have to lock @ubi_devices_lock.
	 */
	for (i = 0; i < UBI_MAX_DEVICES; i++) {
		struct ubi_device *ubi = ubi_devices[i];

		if (!ubi)
			continue;
		count += ubi_notify_all(ubi, UBI_VOLUME_ADDED, nb);
	}

	return count;
}

/**
 * ubi_get_device - get UBI device.
 * @ubi_num: UBI device number
 *
 * This function returns UBI device description object for UBI device number
 * @ubi_num, or %NULL if the device does not exist. This function increases the
 * device reference count to prevent removal of the device. In other words, the
 * device cannot be removed if its reference count is not zero.
 */
struct ubi_device *ubi_get_device(int ubi_num)
{
	struct ubi_device *ubi;

	spin_lock(&ubi_devices_lock);
	ubi = ubi_devices[ubi_num];
	if (ubi) {
		ubi_assert(ubi->ref_count >= 0);
		ubi->ref_count += 1;
		get_device(&ubi->dev);
	}
	spin_unlock(&ubi_devices_lock);

	return ubi;
}

/**
 * ubi_put_device - drop an UBI device reference.
 * @ubi: UBI device description object
 */
void ubi_put_device(struct ubi_device *ubi)
{
	spin_lock(&ubi_devices_lock);
	ubi->ref_count -= 1;
	put_device(&ubi->dev);
	spin_unlock(&ubi_devices_lock);
}

/**
 * ubi_get_by_major - get UBI device by character device major number.
 * @major: major number
 *
 * This function is similar to 'ubi_get_device()', but it searches the device
 * by its major number.
 */
struct ubi_device *ubi_get_by_major(int major)
{
	int i;
	struct ubi_device *ubi;

	spin_lock(&ubi_devices_lock);
	for (i = 0; i < UBI_MAX_DEVICES; i++) {
		ubi = ubi_devices[i];
		if (ubi && MAJOR(ubi->cdev.dev) == major) {
			ubi_assert(ubi->ref_count >= 0);
			ubi->ref_count += 1;
			get_device(&ubi->dev);
			spin_unlock(&ubi_devices_lock);
			return ubi;
		}
	}
	spin_unlock(&ubi_devices_lock);

	return NULL;
}

/**
 * ubi_major2num - get UBI device number by character device major number.
 * @major: major number
 *
 * This function searches UBI device number object by its major number. If UBI
 * device was not found, this function returns -ENODEV, otherwise the UBI device
 * number is returned.
 */
int ubi_major2num(int major)
{
	int i, ubi_num = -ENODEV;

	spin_lock(&ubi_devices_lock);
	for (i = 0; i < UBI_MAX_DEVICES; i++) {
		struct ubi_device *ubi = ubi_devices[i];

		if (ubi && MAJOR(ubi->cdev.dev) == major) {
			ubi_num = ubi->ubi_num;
			break;
		}
	}
	spin_unlock(&ubi_devices_lock);

	return ubi_num;
}

/* "Show" method for files in '/<sysfs>/class/ubi/ubiX/' */
static ssize_t dev_attribute_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t ret;
	struct ubi_device *ubi;

	/*
	 * The below code looks weird, but it actually makes sense. We get the
	 * UBI device reference from the contained 'struct ubi_device'. But it
	 * is unclear if the device was removed or not yet. Indeed, if the
	 * device was removed before we increased its reference count,
	 * 'ubi_get_device()' will return -ENODEV and we fail.
	 *
	 * Remember, 'struct ubi_device' is freed in the release function, so
	 * we still can use 'ubi->ubi_num'.
	 */
	ubi = container_of(dev, struct ubi_device, dev);
	ubi = ubi_get_device(ubi->ubi_num);
	if (!ubi)
		return -ENODEV;

	if (attr == &dev_eraseblock_size)
		ret = sprintf(buf, "%d\n", ubi->leb_size);
	else if (attr == &dev_avail_eraseblocks)
		ret = sprintf(buf, "%d\n", ubi->avail_pebs);
	else if (attr == &dev_total_eraseblocks)
		ret = sprintf(buf, "%d\n", ubi->good_peb_count);
	else if (attr == &dev_volumes_count)
		ret = sprintf(buf, "%d\n", ubi->vol_count - UBI_INT_VOL_COUNT);
	else if (attr == &dev_max_ec)
		ret = sprintf(buf, "%d\n", ubi->max_ec);
	else if (attr == &dev_reserved_for_bad)
		ret = sprintf(buf, "%d\n", ubi->beb_rsvd_pebs);
	else if (attr == &dev_bad_peb_count)
		ret = sprintf(buf, "%d\n", ubi->bad_peb_count);
	else if (attr == &dev_max_vol_count)
		ret = sprintf(buf, "%d\n", ubi->vtbl_slots);
	else if (attr == &dev_min_io_size)
		ret = sprintf(buf, "%d\n", ubi->min_io_size);
	else if (attr == &dev_bgt_enabled)
		ret = sprintf(buf, "%d\n", ubi->thread_enabled);
	else if (attr == &dev_mtd_num)
		ret = sprintf(buf, "%d\n", ubi->mtd->index);
	else
		ret = -EINVAL;

	ubi_put_device(ubi);
	return ret;
}

static void dev_release(struct device *dev)
{
	struct ubi_device *ubi = container_of(dev, struct ubi_device, dev);

	kfree(ubi);
}

/**
 * ubi_sysfs_init - initialize sysfs for an UBI device.
 * @ubi: UBI device description object
 * @ref: set to %1 on exit in case of failure if a reference to @ubi->dev was
 *       taken
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int ubi_sysfs_init(struct ubi_device *ubi, int *ref)
{
	int err;

	ubi->dev.release = dev_release;
	ubi->dev.devt = ubi->cdev.dev;
	ubi->dev.class = ubi_class;
	dev_set_name(&ubi->dev, UBI_NAME_STR"%d", ubi->ubi_num);
	err = device_register(&ubi->dev);
	if (err)
		return err;

	*ref = 1;
	err = device_create_file(&ubi->dev, &dev_eraseblock_size);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_avail_eraseblocks);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_total_eraseblocks);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_volumes_count);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_max_ec);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_reserved_for_bad);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_bad_peb_count);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_max_vol_count);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_min_io_size);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_bgt_enabled);
	if (err)
		return err;
	err = device_create_file(&ubi->dev, &dev_mtd_num);
	return err;
}

/**
 * ubi_sysfs_close - close sysfs for an UBI device.
 * @ubi: UBI device description object
 */
static void ubi_sysfs_close(struct ubi_device *ubi)
{
	device_remove_file(&ubi->dev, &dev_mtd_num);
	device_remove_file(&ubi->dev, &dev_bgt_enabled);
	device_remove_file(&ubi->dev, &dev_min_io_size);
	device_remove_file(&ubi->dev, &dev_max_vol_count);
	device_remove_file(&ubi->dev, &dev_bad_peb_count);
	device_remove_file(&ubi->dev, &dev_reserved_for_bad);
	device_remove_file(&ubi->dev, &dev_max_ec);
	device_remove_file(&ubi->dev, &dev_volumes_count);
	device_remove_file(&ubi->dev, &dev_total_eraseblocks);
	device_remove_file(&ubi->dev, &dev_avail_eraseblocks);
	device_remove_file(&ubi->dev, &dev_eraseblock_size);
	device_unregister(&ubi->dev);
}

/**
 * kill_volumes - destroy all user volumes.
 * @ubi: UBI device description object
 */
static void kill_volumes(struct ubi_device *ubi)
{
	int i;

	for (i = 0; i < ubi->vtbl_slots; i++)
		if (ubi->volumes[i])
			ubi_free_volume(ubi, ubi->volumes[i]);
}

/**
 * uif_init - initialize user interfaces for an UBI device.
 * @ubi: UBI device description object
 * @ref: set to %1 on exit in case of failure if a reference to @ubi->dev was
 *       taken, otherwise set to %0
 *
 * This function initializes various user interfaces for an UBI device. If the
 * initialization fails at an early stage, this function frees all the
 * resources it allocated, returns an error, and @ref is set to %0. However,
 * if the initialization fails after the UBI device was registered in the
 * driver core subsystem, this function takes a reference to @ubi->dev, because
 * otherwise the release function ('dev_release()') would free whole @ubi
 * object. The @ref argument is set to %1 in this case. The caller has to put
 * this reference.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int uif_init(struct ubi_device *ubi, int *ref)
{
	int i, err;
	dev_t dev;

	*ref = 0;
	sprintf(ubi->ubi_name, UBI_NAME_STR "%d", ubi->ubi_num);

	/*
	 * Major numbers for the UBI character devices are allocated
	 * dynamically. Major numbers of volume character devices are
	 * equivalent to ones of the corresponding UBI character device. Minor
	 * numbers of UBI character devices are 0, while minor numbers of
	 * volume character devices start from 1. Thus, we allocate one major
	 * number and ubi->vtbl_slots + 1 minor numbers.
	 */
	err = alloc_chrdev_region(&dev, 0, ubi->vtbl_slots + 1, ubi->ubi_name);
	if (err) {
		ubi_err("cannot register UBI character devices");
		return err;
	}

	ubi_assert(MINOR(dev) == 0);
	cdev_init(&ubi->cdev, &ubi_cdev_operations);
	dbg_gen("%s major is %u", ubi->ubi_name, MAJOR(dev));
	ubi->cdev.owner = THIS_MODULE;

	err = cdev_add(&ubi->cdev, dev, 1);
	if (err) {
		ubi_err("cannot add character device");
		goto out_unreg;
	}

	err = ubi_sysfs_init(ubi, ref);
	if (err)
		goto out_sysfs;

	for (i = 0; i < ubi->vtbl_slots; i++)
		if (ubi->volumes[i]) {
			err = ubi_add_volume(ubi, ubi->volumes[i]);
			if (err) {
				ubi_err("cannot add volume %d", i);
				goto out_volumes;
			}
		}

	return 0;

out_volumes:
	kill_volumes(ubi);
out_sysfs:
	if (*ref)
		get_device(&ubi->dev);
	ubi_sysfs_close(ubi);
	cdev_del(&ubi->cdev);
out_unreg:
	unregister_chrdev_region(ubi->cdev.dev, ubi->vtbl_slots + 1);
	ubi_err("cannot initialize UBI %s, error %d", ubi->ubi_name, err);
	return err;
}

/**
 * uif_close - close user interfaces for an UBI device.
 * @ubi: UBI device description object
 *
 * Note, since this function un-registers UBI volume device objects (@vol->dev),
 * the memory allocated voe the volumes is freed as well (in the release
 * function).
 */
static void uif_close(struct ubi_device *ubi)
{
	kill_volumes(ubi);
	ubi_sysfs_close(ubi);
	cdev_del(&ubi->cdev);
	unregister_chrdev_region(ubi->cdev.dev, ubi->vtbl_slots + 1);
}

/**
 * ubi_free_internal_volumes - free internal volumes.
 * @ubi: UBI device description object
 */
void ubi_free_internal_volumes(struct ubi_device *ubi)
{
	int i;

	for (i = ubi->vtbl_slots;
	     i < ubi->vtbl_slots + UBI_INT_VOL_COUNT; i++) {
		kfree(ubi->volumes[i]->eba_tbl);
		kfree(ubi->volumes[i]);
	}
}

static int get_bad_peb_limit(const struct ubi_device *ubi, int max_beb_per1024)
{
	int limit, device_pebs;
	uint64_t device_size;

	if (!max_beb_per1024)
		return 0;

	/*
	 * Here we are using size of the entire flash chip and
	 * not just the MTD partition size because the maximum
	 * number of bad eraseblocks is a percentage of the
	 * whole device and bad eraseblocks are not fairly
	 * distributed over the flash chip. So the worst case
	 * is that all the bad eraseblocks of the chip are in
	 * the MTD partition we are attaching (ubi->mtd).
	 */
	device_size = mtd_get_device_size(ubi->mtd);
	device_pebs = mtd_div_by_eb(device_size, ubi->mtd);
	limit = mult_frac(device_pebs, max_beb_per1024, 1024);

	/* Round it up */
	if (mult_frac(limit, 1024, max_beb_per1024) < device_pebs)
		limit += 1;

	return limit;
}

/**
 * io_init - initialize I/O sub-system for a given UBI device.
 * @ubi: UBI device description object
 * @max_beb_per1024: maximum expected number of bad PEB per 1024 PEBs
 *
 * If @ubi->vid_hdr_offset or @ubi->leb_start is zero, default offsets are
 * assumed:
 *   o EC header is always at offset zero - this cannot be changed;
 *   o VID header starts just after the EC header at the closest address
 *     aligned to @io->hdrs_min_io_size;
 *   o data starts just after the VID header at the closest address aligned to
 *     @io->min_io_size
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int io_init(struct ubi_device *ubi, int max_beb_per1024)
{
	if (ubi->mtd->numeraseregions != 0) {
		/*
		 * Some flashes have several erase regions. Different regions
		 * may have different eraseblock size and other
		 * characteristics. It looks like mostly multi-region flashes
		 * have one "main" region and one or more small regions to
		 * store boot loader code or boot parameters or whatever. I
		 * guess we should just pick the largest region. But this is
		 * not implemented.
		 */
		ubi_err("multiple regions, not implemented");
		return -EINVAL;
	}

	if (ubi->vid_hdr_offset < 0)
		return -EINVAL;

	/*
	 * Note, in this implementation we support MTD devices with 0x7FFFFFFF
	 * physical eraseblocks maximum.
	 */

	ubi->peb_size   = ubi->mtd->erasesize;
	ubi->peb_count  = mtd_div_by_eb(ubi->mtd->size, ubi->mtd);
	ubi->flash_size = ubi->mtd->size;

	if (mtd_can_have_bb(ubi->mtd)) {
		ubi->bad_allowed = 1;
		ubi->bad_peb_limit = get_bad_peb_limit(ubi, max_beb_per1024);
	}

	if (ubi->mtd->type == MTD_NORFLASH) {
		ubi_assert(ubi->mtd->writesize == 1);
		ubi->nor_flash = 1;
	}

	ubi->min_io_size = ubi->mtd->writesize;
	ubi->hdrs_min_io_size = ubi->mtd->writesize >> ubi->mtd->subpage_sft;

	/*
	 * Make sure minimal I/O unit is power of 2. Note, there is no
	 * fundamental reason for this assumption. It is just an optimization
	 * which allows us to avoid costly division operations.
	 */
	if (!is_power_of_2(ubi->min_io_size)) {
		ubi_err("min. I/O unit (%d) is not power of 2",
			ubi->min_io_size);
		return -EINVAL;
	}

	ubi_assert(ubi->hdrs_min_io_size > 0);
	ubi_assert(ubi->hdrs_min_io_size <= ubi->min_io_size);
	ubi_assert(ubi->min_io_size % ubi->hdrs_min_io_size == 0);

	ubi->max_write_size = ubi->mtd->writebufsize;
	/*
	 * Maximum write size has to be greater or equivalent to min. I/O
	 * size, and be multiple of min. I/O size.
	 */
	if (ubi->max_write_size < ubi->min_io_size ||
	    ubi->max_write_size % ubi->min_io_size ||
	    !is_power_of_2(ubi->max_write_size)) {
		ubi_err("bad write buffer size %d for %d min. I/O unit",
			ubi->max_write_size, ubi->min_io_size);
		return -EINVAL;
	}

	/* Calculate default aligned sizes of EC and VID headers */
	ubi->ec_hdr_alsize = ALIGN(UBI_EC_HDR_SIZE, ubi->hdrs_min_io_size);
	ubi->vid_hdr_alsize = ALIGN(UBI_VID_HDR_SIZE, ubi->hdrs_min_io_size);

	dbg_msg("min_io_size      %d", ubi->min_io_size);
	dbg_msg("max_write_size   %d", ubi->max_write_size);
	dbg_msg("hdrs_min_io_size %d", ubi->hdrs_min_io_size);
	dbg_msg("ec_hdr_alsize    %d", ubi->ec_hdr_alsize);
	dbg_msg("vid_hdr_alsize   %d", ubi->vid_hdr_alsize);

	if (ubi->vid_hdr_offset == 0)
		/* Default offset */
		ubi->vid_hdr_offset = ubi->vid_hdr_aloffset =
				      ubi->ec_hdr_alsize;
	else {
		ubi->vid_hdr_aloffset = ubi->vid_hdr_offset &
						~(ubi->hdrs_min_io_size - 1);
		ubi->vid_hdr_shift = ubi->vid_hdr_offset -
						ubi->vid_hdr_aloffset;
	}

	/* Similar for the data offset */
	ubi->leb_start = ubi->vid_hdr_offset + UBI_VID_HDR_SIZE;
	ubi->leb_start = ALIGN(ubi->leb_start, ubi->min_io_size);

	dbg_msg("vid_hdr_offset   %d", ubi->vid_hdr_offset);
	dbg_msg("vid_hdr_aloffset %d", ubi->vid_hdr_aloffset);
	dbg_msg("vid_hdr_shift    %d", ubi->vid_hdr_shift);
	dbg_msg("leb_start        %d", ubi->leb_start);

	/* The shift must be aligned to 32-bit boundary */
	if (ubi->vid_hdr_shift % 4) {
		ubi_err("unaligned VID header shift %d",
			ubi->vid_hdr_shift);
		return -EINVAL;
	}

	/* Check sanity */
	if (ubi->vid_hdr_offset < UBI_EC_HDR_SIZE ||
	    ubi->leb_start < ubi->vid_hdr_offset + UBI_VID_HDR_SIZE ||
	    ubi->leb_start > ubi->peb_size - UBI_VID_HDR_SIZE ||
	    ubi->leb_start & (ubi->min_io_size - 1)) {
		ubi_err("bad VID header (%d) or data offsets (%d)",
			ubi->vid_hdr_offset, ubi->leb_start);
		return -EINVAL;
	}

	/*
	 * Set maximum amount of physical erroneous eraseblocks to be 10%.
	 * Erroneous PEB are those which have read errors.
	 */
	ubi->max_erroneous = ubi->peb_count / 10;
	if (ubi->max_erroneous < 16)
		ubi->max_erroneous = 16;
	dbg_msg("max_erroneous    %d", ubi->max_erroneous);

	/*
	 * It may happen that EC and VID headers are situated in one minimal
	 * I/O unit. In this case we can only accept this UBI image in
	 * read-only mode.
	 */
	if (ubi->vid_hdr_offset + UBI_VID_HDR_SIZE <= ubi->hdrs_min_io_size) {
		ubi_warn("EC and VID headers are in the same minimal I/O unit, "
			 "switch to read-only mode");
		ubi->ro_mode = 1;
	}

	ubi->leb_size = ubi->peb_size - ubi->leb_start;

	if (!(ubi->mtd->flags & MTD_WRITEABLE)) {
		ubi_msg("MTD device %d is write-protected, attach in "
			"read-only mode", ubi->mtd->index);
		ubi->ro_mode = 1;
	}

	ubi_msg("physical eraseblock size:   %d bytes (%d KiB)",
		ubi->peb_size, ubi->peb_size >> 10);
	ubi_msg("logical eraseblock size:    %d bytes", ubi->leb_size);
	ubi_msg("smallest flash I/O unit:    %d", ubi->min_io_size);
	if (ubi->hdrs_min_io_size != ubi->min_io_size)
		ubi_msg("sub-page size:              %d",
			ubi->hdrs_min_io_size);
	ubi_msg("VID header offset:          %d (aligned %d)",
		ubi->vid_hdr_offset, ubi->vid_hdr_aloffset);
	ubi_msg("data offset:                %d", ubi->leb_start);

	/*
	 * Note, ideally, we have to initialize @ubi->bad_peb_count here. But
	 * unfortunately, MTD does not provide this information. We should loop
	 * over all physical eraseblocks and invoke mtd->block_is_bad() for
	 * each physical eraseblock. So, we leave @ubi->bad_peb_count
	 * uninitialized so far.
	 */

	return 0;
}

/**
 * autoresize - re-size the volume which has the "auto-resize" flag set.
 * @ubi: UBI device description object
 * @vol_id: ID of the volume to re-size
 *
 * This function re-sizes the volume marked by the %UBI_VTBL_AUTORESIZE_FLG in
 * the volume table to the largest possible size. See comments in ubi-header.h
 * for more description of the flag. Returns zero in case of success and a
 * negative error code in case of failure.
 */
static int autoresize(struct ubi_device *ubi, int vol_id)
{
	struct ubi_volume_desc desc;
	struct ubi_volume *vol = ubi->volumes[vol_id];
	int err, old_reserved_pebs = vol->reserved_pebs;

	if (ubi->ro_mode) {
		ubi_warn("skip auto-resize because of R/O mode");
		return 0;
	}

	/*
	 * Clear the auto-resize flag in the volume in-memory copy of the
	 * volume table, and 'ubi_resize_volume()' will propagate this change
	 * to the flash.
	 */
	ubi->vtbl[vol_id].flags &= ~UBI_VTBL_AUTORESIZE_FLG;

	if (ubi->avail_pebs == 0) {
		struct ubi_vtbl_record vtbl_rec;

		/*
		 * No available PEBs to re-size the volume, clear the flag on
		 * flash and exit.
		 */
		memcpy(&vtbl_rec, &ubi->vtbl[vol_id],
		       sizeof(struct ubi_vtbl_record));
		err = ubi_change_vtbl_record(ubi, vol_id, &vtbl_rec);
		if (err)
			ubi_err("cannot clean auto-resize flag for volume %d",
				vol_id);
	} else {
		desc.vol = vol;
		err = ubi_resize_volume(&desc,
					old_reserved_pebs + ubi->avail_pebs);
		if (err)
			ubi_err("cannot auto-resize volume %d", vol_id);
	}

	if (err)
		return err;

	ubi_msg("volume %d (\"%s\") re-sized from %d to %d LEBs", vol_id,
		vol->name, old_reserved_pebs, vol->reserved_pebs);
	return 0;
}

/**
 * ubi_attach_mtd_dev - attach an MTD device.
 * @mtd: MTD device description object
 * @ubi_num: number to assign to the new UBI device
 * @vid_hdr_offset: VID header offset
 * @max_beb_per1024: maximum expected number of bad PEB per 1024 PEBs
 *
 * This function attaches MTD device @mtd_dev to UBI and assign @ubi_num number
 * to the newly created UBI device, unless @ubi_num is %UBI_DEV_NUM_AUTO, in
 * which case this function finds a vacant device number and assigns it
 * automatically. Returns the new UBI device number in case of success and a
 * negative error code in case of failure.
 *
 * Note, the invocations of this function has to be serialized by the
 * @ubi_devices_mutex.
 */
int ubi_attach_mtd_dev(struct mtd_info *mtd, int ubi_num,
		       int vid_hdr_offset, int max_beb_per1024)
{
	struct ubi_device *ubi;
	int i, err, ref = 0;

	if (max_beb_per1024 < 0 || max_beb_per1024 > MAX_MTD_UBI_BEB_LIMIT)
		return -EINVAL;

	if (!max_beb_per1024)
		max_beb_per1024 = CONFIG_MTD_UBI_BEB_LIMIT;

	/*
	 * Check if we already have the same MTD device attached.
	 *
	 * Note, this function assumes that UBI devices creations and deletions
	 * are serialized, so it does not take the &ubi_devices_lock.
	 */
	for (i = 0; i < UBI_MAX_DEVICES; i++) {
		ubi = ubi_devices[i];
		if (ubi && mtd->index == ubi->mtd->index) {
			ubi_err("mtd%d is already attached to ubi%d",
				mtd->index, i);
			return -EEXIST;
		}
	}

	/*
	 * Make sure this MTD device is not emulated on top of an UBI volume
	 * already. Well, generally this recursion works fine, but there are
	 * different problems like the UBI module takes a reference to itself
	 * by attaching (and thus, opening) the emulated MTD device. This
	 * results in inability to unload the module. And in general it makes
	 * no sense to attach emulated MTD devices, so we prohibit this.
	 */
	if (mtd->type == MTD_UBIVOLUME) {
		ubi_err("refuse attaching mtd%d - it is already emulated on "
			"top of UBI", mtd->index);
		return -EINVAL;
	}

	if (ubi_num == UBI_DEV_NUM_AUTO) {
		/* Search for an empty slot in the @ubi_devices array */
		for (ubi_num = 0; ubi_num < UBI_MAX_DEVICES; ubi_num++)
			if (!ubi_devices[ubi_num])
				break;
		if (ubi_num == UBI_MAX_DEVICES) {
			ubi_err("only %d UBI devices may be created",
				UBI_MAX_DEVICES);
			return -ENFILE;
		}
	} else {
		if (ubi_num >= UBI_MAX_DEVICES)
			return -EINVAL;

		/* Make sure ubi_num is not busy */
		if (ubi_devices[ubi_num]) {
			ubi_err("ubi%d already exists", ubi_num);
			return -EEXIST;
		}
	}

	ubi = kzalloc(sizeof(struct ubi_device), GFP_KERNEL);
	if (!ubi)
		return -ENOMEM;

	ubi->mtd = mtd;
	ubi->ubi_num = ubi_num;
	ubi->vid_hdr_offset = vid_hdr_offset;
	ubi->autoresize_vol_id = -1;

	mutex_init(&ubi->buf_mutex);
	mutex_init(&ubi->ckvol_mutex);
	mutex_init(&ubi->device_mutex);
	spin_lock_init(&ubi->volumes_lock);

	ubi_msg("attaching mtd%d to ubi%d", mtd->index, ubi_num);
	dbg_msg("sizeof(struct ubi_ainf_peb) %zu", sizeof(struct ubi_ainf_peb));
	dbg_msg("sizeof(struct ubi_wl_entry) %zu", sizeof(struct ubi_wl_entry));

	err = io_init(ubi, max_beb_per1024);
	if (err)
		goto out_free;

	err = -ENOMEM;
	ubi->peb_buf = vmalloc(ubi->peb_size);
	if (!ubi->peb_buf)
		goto out_free;

	err = ubi_debugging_init_dev(ubi);
	if (err)
		goto out_free;

	err = ubi_attach(ubi);
	if (err) {
		ubi_err("failed to attach mtd%d, error %d", mtd->index, err);
		goto out_debugging;
	}

	if (ubi->autoresize_vol_id != -1) {
		err = autoresize(ubi, ubi->autoresize_vol_id);
		if (err)
			goto out_detach;
	}

	err = uif_init(ubi, &ref);
	if (err)
		goto out_detach;

	err = ubi_debugfs_init_dev(ubi);
	if (err)
		goto out_uif;

	ubi->bgt_thread = kthread_create(ubi_thread, ubi, ubi->bgt_name);
	if (IS_ERR(ubi->bgt_thread)) {
		err = PTR_ERR(ubi->bgt_thread);
		ubi_err("cannot spawn \"%s\", error %d", ubi->bgt_name,
			err);
		goto out_debugfs;
	}

	ubi_msg("attached mtd%d to ubi%d", mtd->index, ubi_num);
	ubi_msg("MTD device name:            \"%s\"", mtd->name);
	ubi_msg("MTD device size:            %llu MiB", ubi->flash_size >> 20);
	ubi_msg("number of good PEBs:        %d", ubi->good_peb_count);
	ubi_msg("number of bad PEBs:         %d", ubi->bad_peb_count);
	ubi_msg("number of corrupted PEBs:   %d", ubi->corr_peb_count);
	ubi_msg("max. allowed volumes:       %d", ubi->vtbl_slots);
	ubi_msg("wear-leveling threshold:    %d", CONFIG_MTD_UBI_WL_THRESHOLD);
	ubi_msg("number of internal volumes: %d", UBI_INT_VOL_COUNT);
	ubi_msg("number of user volumes:     %d",
		ubi->vol_count - UBI_INT_VOL_COUNT);
	ubi_msg("available PEBs:             %d", ubi->avail_pebs);
	ubi_msg("total number of reserved PEBs: %d", ubi->rsvd_pebs);
	ubi_msg("number of PEBs reserved for bad PEB handling: %d",
		ubi->beb_rsvd_pebs);
	ubi_msg("max/mean erase counter: %d/%d", ubi->max_ec, ubi->mean_ec);
	ubi_msg("image sequence number:  %u", ubi->image_seq);

	/*
	 * The below lock makes sure we do not race with 'ubi_thread()' which
	 * checks @ubi->thread_enabled. Otherwise we may fail to wake it up.
	 */
	spin_lock(&ubi->wl_lock);
	ubi->thread_enabled = 1;
	wake_up_process(ubi->bgt_thread);
	spin_unlock(&ubi->wl_lock);

	ubi_devices[ubi_num] = ubi;
	ubi_notify_all(ubi, UBI_VOLUME_ADDED, NULL);
	return ubi_num;

out_debugfs:
	ubi_debugfs_exit_dev(ubi);
out_uif:
	get_device(&ubi->dev);
	ubi_assert(ref);
	uif_close(ubi);
out_detach:
	ubi_wl_close(ubi);
	ubi_free_internal_volumes(ubi);
	vfree(ubi->vtbl);
out_debugging:
	ubi_debugging_exit_dev(ubi);
out_free:
	vfree(ubi->peb_buf);
	if (ref)
		put_device(&ubi->dev);
	else
		kfree(ubi);
	return err;
}

/**
 * ubi_detach_mtd_dev - detach an MTD device.
 * @ubi_num: UBI device number to detach from
 * @anyway: detach MTD even if device reference count is not zero
 *
 * This function destroys an UBI device number @ubi_num and detaches the
 * underlying MTD device. Returns zero in case of success and %-EBUSY if the
 * UBI device is busy and cannot be destroyed, and %-EINVAL if it does not
 * exist.
 *
 * Note, the invocations of this function has to be serialized by the
 * @ubi_devices_mutex.
 */
int ubi_detach_mtd_dev(int ubi_num, int anyway)
{
	struct ubi_device *ubi;

	if (ubi_num < 0 || ubi_num >= UBI_MAX_DEVICES)
		return -EINVAL;

	ubi = ubi_get_device(ubi_num);
	if (!ubi)
		return -EINVAL;

	spin_lock(&ubi_devices_lock);
	put_device(&ubi->dev);
	ubi->ref_count -= 1;
	if (ubi->ref_count) {
		if (!anyway) {
			spin_unlock(&ubi_devices_lock);
			return -EBUSY;
		}
		/* This may only happen if there is a bug */
		ubi_err("%s reference count %d, destroy anyway",
			ubi->ubi_name, ubi->ref_count);
	}
	ubi_devices[ubi_num] = NULL;
	spin_unlock(&ubi_devices_lock);

	ubi_assert(ubi_num == ubi->ubi_num);
	ubi_notify_all(ubi, UBI_VOLUME_REMOVED, NULL);
	dbg_msg("detaching mtd%d from ubi%d", ubi->mtd->index, ubi_num);

	/*
	 * Before freeing anything, we have to stop the background thread to
	 * prevent it from doing anything on this device while we are freeing.
	 */
	if (ubi->bgt_thread)
		kthread_stop(ubi->bgt_thread);

	/*
	 * Get a reference to the device in order to prevent 'dev_release()'
	 * from freeing the @ubi object.
	 */
	get_device(&ubi->dev);

	ubi_debugfs_exit_dev(ubi);
	uif_close(ubi);
	ubi_wl_close(ubi);
	ubi_free_internal_volumes(ubi);
	vfree(ubi->vtbl);
	put_mtd_device(ubi->mtd);
	ubi_debugging_exit_dev(ubi);
	vfree(ubi->peb_buf);
	ubi_msg("mtd%d is detached from ubi%d", ubi->mtd->index, ubi->ubi_num);
	put_device(&ubi->dev);
	return 0;
}

/**
 * open_mtd_by_chdev - open an MTD device by its character device node path.
 * @mtd_dev: MTD character device node path
 *
 * This helper function opens an MTD device by its character node device path.
 * Returns MTD device description object in case of success and a negative
 * error code in case of failure.
 */
static struct mtd_info * __init open_mtd_by_chdev(const char *mtd_dev)
{
	int err, major, minor, mode;
	struct path path;

	/* Probably this is an MTD character device node path */
	err = kern_path(mtd_dev, LOOKUP_FOLLOW, &path);
	if (err)
		return ERR_PTR(err);

	/* MTD device number is defined by the major / minor numbers */
	major = imajor(path.dentry->d_inode);
	minor = iminor(path.dentry->d_inode);
	mode = path.dentry->d_inode->i_mode;
	path_put(&path);
	if (major != MTD_CHAR_MAJOR || !S_ISCHR(mode))
		return ERR_PTR(-EINVAL);

	if (minor & 1)
		/*
		 * Just do not think the "/dev/mtdrX" devices support is need,
		 * so do not support them to avoid doing extra work.
		 */
		return ERR_PTR(-EINVAL);

	return get_mtd_device(NULL, minor / 2);
}

/**
 * open_mtd_device - open MTD device by name, character device path, or number.
 * @mtd_dev: name, character device node path, or MTD device device number
 *
 * This function tries to open and MTD device described by @mtd_dev string,
 * which is first treated as ASCII MTD device number, and if it is not true, it
 * is treated as MTD device name, and if that is also not true, it is treated
 * as MTD character device node path. Returns MTD device description object in
 * case of success and a negative error code in case of failure.
 */
static struct mtd_info * __init open_mtd_device(const char *mtd_dev)
{
	struct mtd_info *mtd;
	int mtd_num;
	char *endp;

	mtd_num = simple_strtoul(mtd_dev, &endp, 0);
	if (*endp != '\0' || mtd_dev == endp) {
		/*
		 * This does not look like an ASCII integer, probably this is
		 * MTD device name.
		 */
		mtd = get_mtd_device_nm(mtd_dev);
		if (IS_ERR(mtd) && PTR_ERR(mtd) == -ENODEV)
			/* Probably this is an MTD character device node path */
			mtd = open_mtd_by_chdev(mtd_dev);
	} else
		mtd = get_mtd_device(NULL, mtd_num);

	return mtd;
}

static int __init ubi_init(void)
{
	int err, i, k;

	/* Ensure that EC and VID headers have correct size */
	BUILD_BUG_ON(sizeof(struct ubi_ec_hdr) != 64);
	BUILD_BUG_ON(sizeof(struct ubi_vid_hdr) != 64);

	if (mtd_devs > UBI_MAX_DEVICES) {
		ubi_err("too many MTD devices, maximum is %d", UBI_MAX_DEVICES);
		return -EINVAL;
	}

	/* Create base sysfs directory and sysfs files */
	ubi_class = class_create(THIS_MODULE, UBI_NAME_STR);
	if (IS_ERR(ubi_class)) {
		err = PTR_ERR(ubi_class);
		ubi_err("cannot create UBI class");
		goto out;
	}

	err = class_create_file(ubi_class, &ubi_version);
	if (err) {
		ubi_err("cannot create sysfs file");
		goto out_class;
	}

	err = misc_register(&ubi_ctrl_cdev);
	if (err) {
		ubi_err("cannot register device");
		goto out_version;
	}

	ubi_wl_entry_slab = kmem_cache_create("ubi_wl_entry_slab",
					      sizeof(struct ubi_wl_entry),
					      0, 0, NULL);
	if (!ubi_wl_entry_slab)
		goto out_dev_unreg;

	err = ubi_debugfs_init();
	if (err)
		goto out_slab;


	/* Attach MTD devices */
	for (i = 0; i < mtd_devs; i++) {
		struct mtd_dev_param *p = &mtd_dev_param[i];
		struct mtd_info *mtd;

		cond_resched();

		mtd = open_mtd_device(p->name);
		if (IS_ERR(mtd)) {
			err = PTR_ERR(mtd);
			goto out_detach;
		}

		mutex_lock(&ubi_devices_mutex);
		err = ubi_attach_mtd_dev(mtd, UBI_DEV_NUM_AUTO,
					 p->vid_hdr_offs, p->max_beb_per1024);
		mutex_unlock(&ubi_devices_mutex);
		if (err < 0) {
			ubi_err("cannot attach mtd%d", mtd->index);
			put_mtd_device(mtd);

			/*
			 * Originally UBI stopped initializing on any error.
			 * However, later on it was found out that this
			 * behavior is not very good when UBI is compiled into
			 * the kernel and the MTD devices to attach are passed
			 * through the command line. Indeed, UBI failure
			 * stopped whole boot sequence.
			 *
			 * To fix this, we changed the behavior for the
			 * non-module case, but preserved the old behavior for
			 * the module case, just for compatibility. This is a
			 * little inconsistent, though.
			 */
			if (ubi_is_module())
				goto out_detach;
		}
	}

	return 0;

out_detach:
	for (k = 0; k < i; k++)
		if (ubi_devices[k]) {
			mutex_lock(&ubi_devices_mutex);
			ubi_detach_mtd_dev(ubi_devices[k]->ubi_num, 1);
			mutex_unlock(&ubi_devices_mutex);
		}
	ubi_debugfs_exit();
out_slab:
	kmem_cache_destroy(ubi_wl_entry_slab);
out_dev_unreg:
	misc_deregister(&ubi_ctrl_cdev);
out_version:
	class_remove_file(ubi_class, &ubi_version);
out_class:
	class_destroy(ubi_class);
out:
	ubi_err("UBI error: cannot initialize UBI, error %d", err);
	return err;
}
module_init(ubi_init);

static void __exit ubi_exit(void)
{
	int i;

	for (i = 0; i < UBI_MAX_DEVICES; i++)
		if (ubi_devices[i]) {
			mutex_lock(&ubi_devices_mutex);
			ubi_detach_mtd_dev(ubi_devices[i]->ubi_num, 1);
			mutex_unlock(&ubi_devices_mutex);
		}
	ubi_debugfs_exit();
	kmem_cache_destroy(ubi_wl_entry_slab);
	misc_deregister(&ubi_ctrl_cdev);
	class_remove_file(ubi_class, &ubi_version);
	class_destroy(ubi_class);
}
module_exit(ubi_exit);

/**
 * bytes_str_to_int - convert a number of bytes string into an integer.
 * @str: the string to convert
 *
 * This function returns positive resulting integer in case of success and a
 * negative error code in case of failure.
 */
static int __init bytes_str_to_int(const char *str)
{
	char *endp;
	unsigned long result;

	result = simple_strtoul(str, &endp, 0);
	if (str == endp || result >= INT_MAX) {
		printk(KERN_ERR "UBI error: incorrect bytes count: \"%s\"\n",
		       str);
		return -EINVAL;
	}

	switch (*endp) {
	case 'G':
		result *= 1024;
	case 'M':
		result *= 1024;
	case 'K':
		result *= 1024;
		if (endp[1] == 'i' && endp[2] == 'B')
			endp += 2;
	case '\0':
		break;
	default:
		printk(KERN_ERR "UBI error: incorrect bytes count: \"%s\"\n",
		       str);
		return -EINVAL;
	}

	return result;
}

/**
 * ubi_mtd_param_parse - parse the 'mtd=' UBI parameter.
 * @val: the parameter value to parse
 * @kp: not used
 *
 * This function returns zero in case of success and a negative error code in
 * case of error.
 */
static int __init ubi_mtd_param_parse(const char *val, struct kernel_param *kp)
{
	int i, len;
	struct mtd_dev_param *p;
	char buf[MTD_PARAM_LEN_MAX];
	char *pbuf = &buf[0];
	char *tokens[MTD_PARAM_MAX_COUNT];

	if (!val)
		return -EINVAL;

	if (mtd_devs == UBI_MAX_DEVICES) {
		printk(KERN_ERR "UBI error: too many parameters, max. is %d\n",
		       UBI_MAX_DEVICES);
		return -EINVAL;
	}

	len = strnlen(val, MTD_PARAM_LEN_MAX);
	if (len == MTD_PARAM_LEN_MAX) {
		printk(KERN_ERR "UBI error: parameter \"%s\" is too long, "
		       "max. is %d\n", val, MTD_PARAM_LEN_MAX);
		return -EINVAL;
	}

	if (len == 0) {
		printk(KERN_WARNING "UBI warning: empty 'mtd=' parameter - "
		       "ignored\n");
		return 0;
	}

	strcpy(buf, val);

	/* Get rid of the final newline */
	if (buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	for (i = 0; i < MTD_PARAM_MAX_COUNT; i++)
		tokens[i] = strsep(&pbuf, ",");

	if (pbuf) {
		printk(KERN_ERR "UBI error: too many arguments at \"%s\"\n",
		       val);
		return -EINVAL;
	}

	p = &mtd_dev_param[mtd_devs];
	strcpy(&p->name[0], tokens[0]);

	if (tokens[1])
		p->vid_hdr_offs = bytes_str_to_int(tokens[1]);

	if (p->vid_hdr_offs < 0)
		return p->vid_hdr_offs;

	if (tokens[2]) {
		int err = kstrtoint(tokens[2], 10, &p->max_beb_per1024);

		if (err) {
			printk(KERN_ERR "UBI error: bad value for "
			       "max_beb_per1024 parameter: %s", tokens[2]);
			return -EINVAL;
		}
	}

	mtd_devs += 1;
	return 0;
}

module_param_call(mtd, ubi_mtd_param_parse, NULL, NULL, 000);
MODULE_PARM_DESC(mtd, "MTD devices to attach. Parameter format: mtd=<name|num|path>[,<vid_hdr_offs>[,max_beb_per1024]].\n"
		      "Multiple \"mtd\" parameters may be specified.\n"
		      "MTD devices may be specified by their number, name, or path to the MTD character device node.\n"
		      "Optional \"vid_hdr_offs\" parameter specifies UBI VID header position to be used by UBI. (default value if 0)\n"
		      "Optional \"max_beb_per1024\" parameter specifies the maximum expected bad eraseblock per 1024 eraseblocks. (default value ("
		      __stringify(CONFIG_MTD_UBI_BEB_LIMIT) ") if 0)\n"
		      "\n"
		      "Example 1: mtd=/dev/mtd0 - attach MTD device /dev/mtd0.\n"
		      "Example 2: mtd=content,1984 mtd=4 - attach MTD device with name \"content\" using VID header offset 1984, and MTD device number 4 with default VID header offset.\n"
		      "Example 3: mtd=/dev/mtd1,0,25 - attach MTD device /dev/mtd1 using default VID header offset and reserve 25*nand_size_in_blocks/1024 erase blocks for bad block handling.\n"
		      "\t(e.g. if the NAND *chipset* has 4096 PEB, 100 will be reserved for this UBI device).");

MODULE_VERSION(__stringify(UBI_VERSION));
MODULE_DESCRIPTION("UBI - Unsorted Block Images");
MODULE_AUTHOR("Artem Bityutskiy");
MODULE_LICENSE("GPL");
