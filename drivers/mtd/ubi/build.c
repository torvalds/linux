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
 *
 * At the moment we only attach UBI devices by scanning, which will become a
 * bottleneck when flashes reach certain large size. Then one may improve UBI
 * and add other methods, although it does not seem to be easy to do.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stringify.h>
#include <linux/stat.h>
#include <linux/miscdevice.h>
#include <linux/log2.h>
#include <linux/kthread.h>
#include "ubi.h"

/* Maximum length of the 'mtd=' parameter */
#define MTD_PARAM_LEN_MAX 64

/**
 * struct mtd_dev_param - MTD device parameter description data structure.
 * @name: MTD device name or number string
 * @vid_hdr_offs: VID header offset
 */
struct mtd_dev_param
{
	char name[MTD_PARAM_LEN_MAX];
	int vid_hdr_offs;
};

/* Numbers of elements set in the @mtd_dev_param array */
static int mtd_devs = 0;

/* MTD devices specification parameters */
static struct mtd_dev_param mtd_dev_param[UBI_MAX_DEVICES];

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
static ssize_t ubi_version_show(struct class *class, char *buf)
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
 * ubi_get_by_major - get UBI device description object by character device
 *                    major number.
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

/* Fake "release" method for UBI devices */
static void dev_release(struct device *dev) { }

/**
 * ubi_sysfs_init - initialize sysfs for an UBI device.
 * @ubi: UBI device description object
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int ubi_sysfs_init(struct ubi_device *ubi)
{
	int err;

	ubi->dev.release = dev_release;
	ubi->dev.devt = ubi->cdev.dev;
	ubi->dev.class = ubi_class;
	sprintf(&ubi->dev.bus_id[0], UBI_NAME_STR"%d", ubi->ubi_num);
	err = device_register(&ubi->dev);
	if (err)
		return err;

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
 * kill_volumes - destroy all volumes.
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
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int uif_init(struct ubi_device *ubi)
{
	int i, err;
	dev_t dev;

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
	dbg_msg("%s major is %u", ubi->ubi_name, MAJOR(dev));
	ubi->cdev.owner = THIS_MODULE;

	err = cdev_add(&ubi->cdev, dev, 1);
	if (err) {
		ubi_err("cannot add character device");
		goto out_unreg;
	}

	err = ubi_sysfs_init(ubi);
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
 */
static void uif_close(struct ubi_device *ubi)
{
	kill_volumes(ubi);
	ubi_sysfs_close(ubi);
	cdev_del(&ubi->cdev);
	unregister_chrdev_region(ubi->cdev.dev, ubi->vtbl_slots + 1);
}

/**
 * attach_by_scanning - attach an MTD device using scanning method.
 * @ubi: UBI device descriptor
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 *
 * Note, currently this is the only method to attach UBI devices. Hopefully in
 * the future we'll have more scalable attaching methods and avoid full media
 * scanning. But even in this case scanning will be needed as a fall-back
 * attaching method if there are some on-flash table corruptions.
 */
static int attach_by_scanning(struct ubi_device *ubi)
{
	int err;
	struct ubi_scan_info *si;

	si = ubi_scan(ubi);
	if (IS_ERR(si))
		return PTR_ERR(si);

	ubi->bad_peb_count = si->bad_peb_count;
	ubi->good_peb_count = ubi->peb_count - ubi->bad_peb_count;
	ubi->max_ec = si->max_ec;
	ubi->mean_ec = si->mean_ec;

	err = ubi_read_volume_table(ubi, si);
	if (err)
		goto out_si;

	err = ubi_wl_init_scan(ubi, si);
	if (err)
		goto out_vtbl;

	err = ubi_eba_init_scan(ubi, si);
	if (err)
		goto out_wl;

	ubi_scan_destroy_si(si);
	return 0;

out_wl:
	ubi_wl_close(ubi);
out_vtbl:
	vfree(ubi->vtbl);
out_si:
	ubi_scan_destroy_si(si);
	return err;
}

/**
 * io_init - initialize I/O unit for a given UBI device.
 * @ubi: UBI device description object
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
static int io_init(struct ubi_device *ubi)
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
	ubi->peb_count  = ubi->mtd->size / ubi->mtd->erasesize;
	ubi->flash_size = ubi->mtd->size;

	if (ubi->mtd->block_isbad && ubi->mtd->block_markbad)
		ubi->bad_allowed = 1;

	ubi->min_io_size = ubi->mtd->writesize;
	ubi->hdrs_min_io_size = ubi->mtd->writesize >> ubi->mtd->subpage_sft;

	/* Make sure minimal I/O unit is power of 2 */
	if (!is_power_of_2(ubi->min_io_size)) {
		ubi_err("min. I/O unit (%d) is not power of 2",
			ubi->min_io_size);
		return -EINVAL;
	}

	ubi_assert(ubi->hdrs_min_io_size > 0);
	ubi_assert(ubi->hdrs_min_io_size <= ubi->min_io_size);
	ubi_assert(ubi->min_io_size % ubi->hdrs_min_io_size == 0);

	/* Calculate default aligned sizes of EC and VID headers */
	ubi->ec_hdr_alsize = ALIGN(UBI_EC_HDR_SIZE, ubi->hdrs_min_io_size);
	ubi->vid_hdr_alsize = ALIGN(UBI_VID_HDR_SIZE, ubi->hdrs_min_io_size);

	dbg_msg("min_io_size      %d", ubi->min_io_size);
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
	ubi->leb_start = ubi->vid_hdr_offset + UBI_EC_HDR_SIZE;
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
	    ubi->leb_start % ubi->min_io_size) {
		ubi_err("bad VID header (%d) or data offsets (%d)",
			ubi->vid_hdr_offset, ubi->leb_start);
		return -EINVAL;
	}

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

	dbg_msg("leb_size         %d", ubi->leb_size);
	dbg_msg("ro_mode          %d", ubi->ro_mode);

	/*
	 * Note, ideally, we have to initialize ubi->bad_peb_count here. But
	 * unfortunately, MTD does not provide this information. We should loop
	 * over all physical eraseblocks and invoke mtd->block_is_bad() for
	 * each physical eraseblock. So, we skip ubi->bad_peb_count
	 * uninitialized and initialize it after scanning.
	 */

	return 0;
}

/**
 * autoresize - re-size the volume which has the "auto-resize" flag set.
 * @ubi: UBI device description object
 * @vol_id: ID of the volume to re-size
 *
 * This function re-sizes the volume marked by the @UBI_VTBL_AUTORESIZE_FLG in
 * the volume table to the largest possible size. See comments in ubi-header.h
 * for more description of the flag. Returns zero in case of success and a
 * negative error code in case of failure.
 */
static int autoresize(struct ubi_device *ubi, int vol_id)
{
	struct ubi_volume_desc desc;
	struct ubi_volume *vol = ubi->volumes[vol_id];
	int err, old_reserved_pebs = vol->reserved_pebs;

	/*
	 * Clear the auto-resize flag in the volume in-memory copy of the
	 * volume table, and 'ubi_resize_volume()' will propogate this change
	 * to the flash.
	 */
	ubi->vtbl[vol_id].flags &= ~UBI_VTBL_AUTORESIZE_FLG;

	if (ubi->avail_pebs == 0) {
		struct ubi_vtbl_record vtbl_rec;

		/*
		 * No avalilable PEBs to re-size the volume, clear the flag on
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
 * @mtd_dev: MTD device description object
 * @ubi_num: number to assign to the new UBI device
 * @vid_hdr_offset: VID header offset
 *
 * This function attaches MTD device @mtd_dev to UBI and assign @ubi_num number
 * to the newly created UBI device, unless @ubi_num is %UBI_DEV_NUM_AUTO, in
 * which case this function finds a vacant device nubert and assings it
 * automatically. Returns the new UBI device number in case of success and a
 * negative error code in case of failure.
 *
 * Note, the invocations of this function has to be serialized by the
 * @ubi_devices_mutex.
 */
int ubi_attach_mtd_dev(struct mtd_info *mtd, int ubi_num, int vid_hdr_offset)
{
	struct ubi_device *ubi;
	int i, err;

	/*
	 * Check if we already have the same MTD device attached.
	 *
	 * Note, this function assumes that UBI devices creations and deletions
	 * are serialized, so it does not take the &ubi_devices_lock.
	 */
	for (i = 0; i < UBI_MAX_DEVICES; i++) {
		ubi = ubi_devices[i];
		if (ubi && mtd->index == ubi->mtd->index) {
			dbg_err("mtd%d is already attached to ubi%d",
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
			dbg_err("only %d UBI devices may be created", UBI_MAX_DEVICES);
			return -ENFILE;
		}
	} else {
		if (ubi_num >= UBI_MAX_DEVICES)
			return -EINVAL;

		/* Make sure ubi_num is not busy */
		if (ubi_devices[ubi_num]) {
			dbg_err("ubi%d already exists", ubi_num);
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
	mutex_init(&ubi->volumes_mutex);
	spin_lock_init(&ubi->volumes_lock);

	dbg_msg("attaching mtd%d to ubi%d: VID header offset %d",
		mtd->index, ubi_num, vid_hdr_offset);

	err = io_init(ubi);
	if (err)
		goto out_free;

	ubi->peb_buf1 = vmalloc(ubi->peb_size);
	if (!ubi->peb_buf1)
		goto out_free;

	ubi->peb_buf2 = vmalloc(ubi->peb_size);
	if (!ubi->peb_buf2)
		 goto out_free;

#ifdef CONFIG_MTD_UBI_DEBUG
	mutex_init(&ubi->dbg_buf_mutex);
	ubi->dbg_peb_buf = vmalloc(ubi->peb_size);
	if (!ubi->dbg_peb_buf)
		 goto out_free;
#endif

	err = attach_by_scanning(ubi);
	if (err) {
		dbg_err("failed to attach by scanning, error %d", err);
		goto out_free;
	}

	if (ubi->autoresize_vol_id != -1) {
		err = autoresize(ubi, ubi->autoresize_vol_id);
		if (err)
			goto out_detach;
	}

	err = uif_init(ubi);
	if (err)
		goto out_detach;

	ubi->bgt_thread = kthread_create(ubi_thread, ubi, ubi->bgt_name);
	if (IS_ERR(ubi->bgt_thread)) {
		err = PTR_ERR(ubi->bgt_thread);
		ubi_err("cannot spawn \"%s\", error %d", ubi->bgt_name,
			err);
		goto out_uif;
	}

	ubi_msg("attached mtd%d to ubi%d", mtd->index, ubi_num);
	ubi_msg("MTD device name:            \"%s\"", mtd->name);
	ubi_msg("MTD device size:            %llu MiB", ubi->flash_size >> 20);
	ubi_msg("physical eraseblock size:   %d bytes (%d KiB)",
		ubi->peb_size, ubi->peb_size >> 10);
	ubi_msg("logical eraseblock size:    %d bytes", ubi->leb_size);
	ubi_msg("number of good PEBs:        %d", ubi->good_peb_count);
	ubi_msg("number of bad PEBs:         %d", ubi->bad_peb_count);
	ubi_msg("smallest flash I/O unit:    %d", ubi->min_io_size);
	ubi_msg("VID header offset:          %d (aligned %d)",
		ubi->vid_hdr_offset, ubi->vid_hdr_aloffset);
	ubi_msg("data offset:                %d", ubi->leb_start);
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

	/* Enable the background thread */
	if (!DBG_DISABLE_BGT) {
		ubi->thread_enabled = 1;
		wake_up_process(ubi->bgt_thread);
	}

	ubi_devices[ubi_num] = ubi;
	return ubi_num;

out_uif:
	uif_close(ubi);
out_detach:
	ubi_eba_close(ubi);
	ubi_wl_close(ubi);
	vfree(ubi->vtbl);
out_free:
	vfree(ubi->peb_buf1);
	vfree(ubi->peb_buf2);
#ifdef CONFIG_MTD_UBI_DEBUG
	vfree(ubi->dbg_peb_buf);
#endif
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

	spin_lock(&ubi_devices_lock);
	ubi = ubi_devices[ubi_num];
	if (!ubi) {
		spin_unlock(&ubi_devices_lock);
		return -EINVAL;
	}

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
	dbg_msg("detaching mtd%d from ubi%d", ubi->mtd->index, ubi_num);

	/*
	 * Before freeing anything, we have to stop the background thread to
	 * prevent it from doing anything on this device while we are freeing.
	 */
	if (ubi->bgt_thread)
		kthread_stop(ubi->bgt_thread);

	uif_close(ubi);
	ubi_eba_close(ubi);
	ubi_wl_close(ubi);
	vfree(ubi->vtbl);
	put_mtd_device(ubi->mtd);
	vfree(ubi->peb_buf1);
	vfree(ubi->peb_buf2);
#ifdef CONFIG_MTD_UBI_DEBUG
	vfree(ubi->dbg_peb_buf);
#endif
	ubi_msg("mtd%d is detached from ubi%d", ubi->mtd->index, ubi->ubi_num);
	kfree(ubi);
	return 0;
}

/**
 * find_mtd_device - open an MTD device by its name or number.
 * @mtd_dev: name or number of the device
 *
 * This function tries to open and MTD device described by @mtd_dev string,
 * which is first treated as an ASCII number, and if it is not true, it is
 * treated as MTD device name. Returns MTD device description object in case of
 * success and a negative error code in case of failure.
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
		printk(KERN_ERR "UBI error: too many MTD devices, "
		       "maximum is %d\n", UBI_MAX_DEVICES);
		return -EINVAL;
	}

	/* Create base sysfs directory and sysfs files */
	ubi_class = class_create(THIS_MODULE, UBI_NAME_STR);
	if (IS_ERR(ubi_class)) {
		err = PTR_ERR(ubi_class);
		printk(KERN_ERR "UBI error: cannot create UBI class\n");
		goto out;
	}

	err = class_create_file(ubi_class, &ubi_version);
	if (err) {
		printk(KERN_ERR "UBI error: cannot create sysfs file\n");
		goto out_class;
	}

	err = misc_register(&ubi_ctrl_cdev);
	if (err) {
		printk(KERN_ERR "UBI error: cannot register device\n");
		goto out_version;
	}

	ubi_wl_entry_slab = kmem_cache_create("ubi_wl_entry_slab",
						sizeof(struct ubi_wl_entry),
						0, 0, NULL);
	if (!ubi_wl_entry_slab)
		goto out_dev_unreg;

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
					 p->vid_hdr_offs);
		mutex_unlock(&ubi_devices_mutex);
		if (err < 0) {
			put_mtd_device(mtd);
			printk(KERN_ERR "UBI error: cannot attach %s\n",
			       p->name);
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
	kmem_cache_destroy(ubi_wl_entry_slab);
out_dev_unreg:
	misc_deregister(&ubi_ctrl_cdev);
out_version:
	class_remove_file(ubi_class, &ubi_version);
out_class:
	class_destroy(ubi_class);
out:
	printk(KERN_ERR "UBI error: cannot initialize UBI, error %d\n", err);
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
	kmem_cache_destroy(ubi_wl_entry_slab);
	misc_deregister(&ubi_ctrl_cdev);
	class_remove_file(ubi_class, &ubi_version);
	class_destroy(ubi_class);
}
module_exit(ubi_exit);

/**
 * bytes_str_to_int - convert a string representing number of bytes to an
 * integer.
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
	if (str == endp || result < 0) {
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
	char *tokens[2] = {NULL, NULL};

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

	for (i = 0; i < 2; i++)
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

	mtd_devs += 1;
	return 0;
}

module_param_call(mtd, ubi_mtd_param_parse, NULL, NULL, 000);
MODULE_PARM_DESC(mtd, "MTD devices to attach. Parameter format: "
		      "mtd=<name|num>[,<vid_hdr_offs>].\n"
		      "Multiple \"mtd\" parameters may be specified.\n"
		      "MTD devices may be specified by their number or name.\n"
		      "Optional \"vid_hdr_offs\" parameter specifies UBI VID "
		      "header position and data starting position to be used "
		      "by UBI.\n"
		      "Example: mtd=content,1984 mtd=4 - attach MTD device"
		      "with name \"content\" using VID header offset 1984, and "
		      "MTD device number 4 with default VID header offset.");

MODULE_VERSION(__stringify(UBI_VERSION));
MODULE_DESCRIPTION("UBI - Unsorted Block Images");
MODULE_AUTHOR("Artem Bityutskiy");
MODULE_LICENSE("GPL");
