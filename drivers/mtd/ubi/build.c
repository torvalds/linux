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
 * This file includes UBI initialization and building of UBI devices. At the
 * moment UBI devices may only be added while UBI is initialized, but dynamic
 * device add/remove functionality is planned. Also, at the moment we only
 * attach UBI devices by scanning, which will become a bottleneck when flashes
 * reach certain large size. Then one may improve UBI and add other methods.
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stringify.h>
#include <linux/stat.h>
#include <linux/log2.h>
#include "ubi.h"

/* Maximum length of the 'mtd=' parameter */
#define MTD_PARAM_LEN_MAX 64

/**
 * struct mtd_dev_param - MTD device parameter description data structure.
 * @name: MTD device name or number string
 * @vid_hdr_offs: VID header offset
 * @data_offs: data offset
 */
struct mtd_dev_param
{
	char name[MTD_PARAM_LEN_MAX];
	int vid_hdr_offs;
	int data_offs;
};

/* Numbers of elements set in the @mtd_dev_param array */
static int mtd_devs = 0;

/* MTD devices specification parameters */
static struct mtd_dev_param mtd_dev_param[UBI_MAX_DEVICES];

/* Number of UBI devices in system */
int ubi_devices_cnt;

/* All UBI devices in system */
struct ubi_device *ubi_devices[UBI_MAX_DEVICES];

/* Root UBI "class" object (corresponds to '/<sysfs>/class/ubi/') */
struct class *ubi_class;

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

/* "Show" method for files in '/<sysfs>/class/ubi/ubiX/' */
static ssize_t dev_attribute_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	const struct ubi_device *ubi;

	ubi = container_of(dev, struct ubi_device, dev);
	if (attr == &dev_eraseblock_size)
		return sprintf(buf, "%d\n", ubi->leb_size);
	else if (attr == &dev_avail_eraseblocks)
		return sprintf(buf, "%d\n", ubi->avail_pebs);
	else if (attr == &dev_total_eraseblocks)
		return sprintf(buf, "%d\n", ubi->good_peb_count);
	else if (attr == &dev_volumes_count)
		return sprintf(buf, "%d\n", ubi->vol_count);
	else if (attr == &dev_max_ec)
		return sprintf(buf, "%d\n", ubi->max_ec);
	else if (attr == &dev_reserved_for_bad)
		return sprintf(buf, "%d\n", ubi->beb_rsvd_pebs);
	else if (attr == &dev_bad_peb_count)
		return sprintf(buf, "%d\n", ubi->bad_peb_count);
	else if (attr == &dev_max_vol_count)
		return sprintf(buf, "%d\n", ubi->vtbl_slots);
	else if (attr == &dev_min_io_size)
		return sprintf(buf, "%d\n", ubi->min_io_size);
	else if (attr == &dev_bgt_enabled)
		return sprintf(buf, "%d\n", ubi->thread_enabled);
	else
		BUG();

	return 0;
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
	ubi->dev.devt = MKDEV(ubi->major, 0);
	ubi->dev.class = ubi_class;
	sprintf(&ubi->dev.bus_id[0], UBI_NAME_STR"%d", ubi->ubi_num);
	err = device_register(&ubi->dev);
	if (err)
		goto out;

	err = device_create_file(&ubi->dev, &dev_eraseblock_size);
	if (err)
		goto out_unregister;
	err = device_create_file(&ubi->dev, &dev_avail_eraseblocks);
	if (err)
		goto out_eraseblock_size;
	err = device_create_file(&ubi->dev, &dev_total_eraseblocks);
	if (err)
		goto out_avail_eraseblocks;
	err = device_create_file(&ubi->dev, &dev_volumes_count);
	if (err)
		goto out_total_eraseblocks;
	err = device_create_file(&ubi->dev, &dev_max_ec);
	if (err)
		goto out_volumes_count;
	err = device_create_file(&ubi->dev, &dev_reserved_for_bad);
	if (err)
		goto out_volumes_max_ec;
	err = device_create_file(&ubi->dev, &dev_bad_peb_count);
	if (err)
		goto out_reserved_for_bad;
	err = device_create_file(&ubi->dev, &dev_max_vol_count);
	if (err)
		goto out_bad_peb_count;
	err = device_create_file(&ubi->dev, &dev_min_io_size);
	if (err)
		goto out_max_vol_count;
	err = device_create_file(&ubi->dev, &dev_bgt_enabled);
	if (err)
		goto out_min_io_size;

	return 0;

out_min_io_size:
	device_remove_file(&ubi->dev, &dev_min_io_size);
out_max_vol_count:
	device_remove_file(&ubi->dev, &dev_max_vol_count);
out_bad_peb_count:
	device_remove_file(&ubi->dev, &dev_bad_peb_count);
out_reserved_for_bad:
	device_remove_file(&ubi->dev, &dev_reserved_for_bad);
out_volumes_max_ec:
	device_remove_file(&ubi->dev, &dev_max_ec);
out_volumes_count:
	device_remove_file(&ubi->dev, &dev_volumes_count);
out_total_eraseblocks:
	device_remove_file(&ubi->dev, &dev_total_eraseblocks);
out_avail_eraseblocks:
	device_remove_file(&ubi->dev, &dev_avail_eraseblocks);
out_eraseblock_size:
	device_remove_file(&ubi->dev, &dev_eraseblock_size);
out_unregister:
	device_unregister(&ubi->dev);
out:
	ubi_err("failed to initialize sysfs for %s", ubi->ubi_name);
	return err;
}

/**
 * ubi_sysfs_close - close sysfs for an UBI device.
 * @ubi: UBI device description object
 */
static void ubi_sysfs_close(struct ubi_device *ubi)
{
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
			ubi_free_volume(ubi, i);
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

	mutex_init(&ubi->vtbl_mutex);
	spin_lock_init(&ubi->volumes_lock);

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

	cdev_init(&ubi->cdev, &ubi_cdev_operations);
	ubi->major = MAJOR(dev);
	dbg_msg("%s major is %u", ubi->ubi_name, ubi->major);
	ubi->cdev.owner = THIS_MODULE;

	dev = MKDEV(ubi->major, 0);
	err = cdev_add(&ubi->cdev, dev, 1);
	if (err) {
		ubi_err("cannot add character device %s", ubi->ubi_name);
		goto out_unreg;
	}

	err = ubi_sysfs_init(ubi);
	if (err)
		goto out_cdev;

	for (i = 0; i < ubi->vtbl_slots; i++)
		if (ubi->volumes[i]) {
			err = ubi_add_volume(ubi, i);
			if (err)
				goto out_volumes;
		}

	return 0;

out_volumes:
	kill_volumes(ubi);
	ubi_sysfs_close(ubi);
out_cdev:
	cdev_del(&ubi->cdev);
out_unreg:
	unregister_chrdev_region(MKDEV(ubi->major, 0),
				 ubi->vtbl_slots + 1);
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
	unregister_chrdev_region(MKDEV(ubi->major, 0), ubi->vtbl_slots + 1);
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
 *   aligned to @io->@hdrs_min_io_size;
 *   o data starts just after the VID header at the closest address aligned to
 *     @io->@min_io_size
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
		ubi_err("bad min. I/O unit");
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
	if (ubi->leb_start == 0) {
		ubi->leb_start = ubi->vid_hdr_offset + ubi->vid_hdr_alsize;
		ubi->leb_start = ALIGN(ubi->leb_start, ubi->min_io_size);
	}

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
 * attach_mtd_dev - attach an MTD device.
 * @mtd_dev: MTD device name or number string
 * @vid_hdr_offset: VID header offset
 * @data_offset: data offset
 *
 * This function attaches an MTD device to UBI. It first treats @mtd_dev as the
 * MTD device name, and tries to open it by this name. If it is unable to open,
 * it tries to convert @mtd_dev to an integer and open the MTD device by its
 * number. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int attach_mtd_dev(const char *mtd_dev, int vid_hdr_offset,
			  int data_offset)
{
	struct ubi_device *ubi;
	struct mtd_info *mtd;
	int i, err;

	mtd = get_mtd_device_nm(mtd_dev);
	if (IS_ERR(mtd)) {
		int mtd_num;
		char *endp;

		if (PTR_ERR(mtd) != -ENODEV)
			return PTR_ERR(mtd);

		/*
		 * Probably this is not MTD device name but MTD device number -
		 * check this out.
		 */
		mtd_num = simple_strtoul(mtd_dev, &endp, 0);
		if (*endp != '\0' || mtd_dev == endp) {
			ubi_err("incorrect MTD device: \"%s\"", mtd_dev);
			return -ENODEV;
		}

		mtd = get_mtd_device(NULL, mtd_num);
		if (IS_ERR(mtd))
			return PTR_ERR(mtd);
	}

	/* Check if we already have the same MTD device attached */
	for (i = 0; i < ubi_devices_cnt; i++)
		if (ubi_devices[i]->mtd->index == mtd->index) {
			ubi_err("mtd%d is already attached to ubi%d",
				mtd->index, i);
			err = -EINVAL;
			goto out_mtd;
		}

	ubi = ubi_devices[ubi_devices_cnt] = kzalloc(sizeof(struct ubi_device),
						     GFP_KERNEL);
	if (!ubi) {
		err = -ENOMEM;
		goto out_mtd;
	}

	ubi->ubi_num = ubi_devices_cnt;
	ubi->mtd = mtd;

	dbg_msg("attaching mtd%d to ubi%d: VID header offset %d data offset %d",
		ubi->mtd->index, ubi_devices_cnt, vid_hdr_offset, data_offset);

	ubi->vid_hdr_offset = vid_hdr_offset;
	ubi->leb_start = data_offset;
	err = io_init(ubi);
	if (err)
		goto out_free;

	mutex_init(&ubi->buf_mutex);
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

	err = uif_init(ubi);
	if (err)
		goto out_detach;

	ubi_msg("attached mtd%d to ubi%d", ubi->mtd->index, ubi_devices_cnt);
	ubi_msg("MTD device name:            \"%s\"", ubi->mtd->name);
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

	ubi_devices_cnt += 1;
	return 0;

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
out_mtd:
	put_mtd_device(mtd);
	ubi_devices[ubi_devices_cnt] = NULL;
	return err;
}

/**
 * detach_mtd_dev - detach an MTD device.
 * @ubi: UBI device description object
 */
static void detach_mtd_dev(struct ubi_device *ubi)
{
	int ubi_num = ubi->ubi_num, mtd_num = ubi->mtd->index;

	dbg_msg("detaching mtd%d from ubi%d", ubi->mtd->index, ubi_num);
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
	kfree(ubi_devices[ubi_num]);
	ubi_devices[ubi_num] = NULL;
	ubi_devices_cnt -= 1;
	ubi_assert(ubi_devices_cnt >= 0);
	ubi_msg("mtd%d is detached from ubi%d", mtd_num, ubi_num);
}

static int __init ubi_init(void)
{
	int err, i, k;

	/* Ensure that EC and VID headers have correct size */
	BUILD_BUG_ON(sizeof(struct ubi_ec_hdr) != 64);
	BUILD_BUG_ON(sizeof(struct ubi_vid_hdr) != 64);

	if (mtd_devs > UBI_MAX_DEVICES) {
		printk("UBI error: too many MTD devices, maximum is %d\n",
		       UBI_MAX_DEVICES);
		return -EINVAL;
	}

	ubi_class = class_create(THIS_MODULE, UBI_NAME_STR);
	if (IS_ERR(ubi_class))
		return PTR_ERR(ubi_class);

	err = class_create_file(ubi_class, &ubi_version);
	if (err)
		goto out_class;

	/* Attach MTD devices */
	for (i = 0; i < mtd_devs; i++) {
		struct mtd_dev_param *p = &mtd_dev_param[i];

		cond_resched();
		err = attach_mtd_dev(p->name, p->vid_hdr_offs, p->data_offs);
		if (err)
			goto out_detach;
	}

	return 0;

out_detach:
	for (k = 0; k < i; k++)
		detach_mtd_dev(ubi_devices[k]);
	class_remove_file(ubi_class, &ubi_version);
out_class:
	class_destroy(ubi_class);
	return err;
}
module_init(ubi_init);

static void __exit ubi_exit(void)
{
	int i, n = ubi_devices_cnt;

	for (i = 0; i < n; i++)
		detach_mtd_dev(ubi_devices[i]);
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
		printk("UBI error: incorrect bytes count: \"%s\"\n", str);
		return -EINVAL;
	}

	switch (*endp) {
	case 'G':
		result *= 1024;
	case 'M':
		result *= 1024;
	case 'K':
	case 'k':
		result *= 1024;
		if (endp[1] == 'i' && (endp[2] == '\0' ||
			  endp[2] == 'B'  || endp[2] == 'b'))
			endp += 2;
	case '\0':
		break;
	default:
		printk("UBI error: incorrect bytes count: \"%s\"\n", str);
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
	char *tokens[3] = {NULL, NULL, NULL};

	if (mtd_devs == UBI_MAX_DEVICES) {
		printk("UBI error: too many parameters, max. is %d\n",
		       UBI_MAX_DEVICES);
		return -EINVAL;
	}

	len = strnlen(val, MTD_PARAM_LEN_MAX);
	if (len == MTD_PARAM_LEN_MAX) {
		printk("UBI error: parameter \"%s\" is too long, max. is %d\n",
		       val, MTD_PARAM_LEN_MAX);
		return -EINVAL;
	}

	if (len == 0) {
		printk("UBI warning: empty 'mtd=' parameter - ignored\n");
		return 0;
	}

	strcpy(buf, val);

	/* Get rid of the final newline */
	if (buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	for (i = 0; i < 3; i++)
		tokens[i] = strsep(&pbuf, ",");

	if (pbuf) {
		printk("UBI error: too many arguments at \"%s\"\n", val);
		return -EINVAL;
	}

	p = &mtd_dev_param[mtd_devs];
	strcpy(&p->name[0], tokens[0]);

	if (tokens[1])
		p->vid_hdr_offs = bytes_str_to_int(tokens[1]);
	if (tokens[2])
		p->data_offs = bytes_str_to_int(tokens[2]);

	if (p->vid_hdr_offs < 0)
		return p->vid_hdr_offs;
	if (p->data_offs < 0)
		return p->data_offs;

	mtd_devs += 1;
	return 0;
}

module_param_call(mtd, ubi_mtd_param_parse, NULL, NULL, 000);
MODULE_PARM_DESC(mtd, "MTD devices to attach. Parameter format: "
		      "mtd=<name|num>[,<vid_hdr_offs>,<data_offs>]. "
		      "Multiple \"mtd\" parameters may be specified.\n"
		      "MTD devices may be specified by their number or name. "
		      "Optional \"vid_hdr_offs\" and \"data_offs\" parameters "
		      "specify UBI VID header position and data starting "
		      "position to be used by UBI.\n"
		      "Example: mtd=content,1984,2048 mtd=4 - attach MTD device"
		      "with name content using VID header offset 1984 and data "
		      "start 2048, and MTD device number 4 using default "
		      "offsets");

MODULE_VERSION(__stringify(UBI_VERSION));
MODULE_DESCRIPTION("UBI - Unsorted Block Images");
MODULE_AUTHOR("Artem Bityutskiy");
MODULE_LICENSE("GPL");
