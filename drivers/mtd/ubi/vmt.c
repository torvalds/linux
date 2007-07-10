/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;  either version 2 of the License, or
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
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file contains implementation of volume creation, deletion, updating and
 * resizing.
 */

#include <linux/err.h>
#include <asm/div64.h>
#include "ubi.h"

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID
static void paranoid_check_volumes(struct ubi_device *ubi);
#else
#define paranoid_check_volumes(ubi)
#endif

static ssize_t vol_attribute_show(struct device *dev,
				  struct device_attribute *attr, char *buf);

/* Device attributes corresponding to files in '/<sysfs>/class/ubi/ubiX_Y' */
static struct device_attribute vol_reserved_ebs =
	__ATTR(reserved_ebs, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute vol_type =
	__ATTR(type, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute vol_name =
	__ATTR(name, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute vol_corrupted =
	__ATTR(corrupted, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute vol_alignment =
	__ATTR(alignment, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute vol_usable_eb_size =
	__ATTR(usable_eb_size, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute vol_data_bytes =
	__ATTR(data_bytes, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute vol_upd_marker =
	__ATTR(upd_marker, S_IRUGO, vol_attribute_show, NULL);

/*
 * "Show" method for files in '/<sysfs>/class/ubi/ubiX_Y/'.
 *
 * Consider a situation:
 * A. process 1 opens a sysfs file related to volume Y, say
 *    /<sysfs>/class/ubi/ubiX_Y/reserved_ebs;
 * B. process 2 removes volume Y;
 * C. process 1 starts reading the /<sysfs>/class/ubi/ubiX_Y/reserved_ebs file;
 *
 * What we want to do in a situation like that is to return error when the file
 * is read. This is done by means of the 'removed' flag and the 'vol_lock' of
 * the UBI volume description object.
 */
static ssize_t vol_attribute_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;
	struct ubi_volume *vol = container_of(dev, struct ubi_volume, dev);

	spin_lock(&vol->ubi->volumes_lock);
	if (vol->removed) {
		spin_unlock(&vol->ubi->volumes_lock);
		return -ENODEV;
	}
	if (attr == &vol_reserved_ebs)
		ret = sprintf(buf, "%d\n", vol->reserved_pebs);
	else if (attr == &vol_type) {
		const char *tp;
		tp = vol->vol_type == UBI_DYNAMIC_VOLUME ? "dynamic" : "static";
		ret = sprintf(buf, "%s\n", tp);
	} else if (attr == &vol_name)
		ret = sprintf(buf, "%s\n", vol->name);
	else if (attr == &vol_corrupted)
		ret = sprintf(buf, "%d\n", vol->corrupted);
	else if (attr == &vol_alignment)
		ret = sprintf(buf, "%d\n", vol->alignment);
	else if (attr == &vol_usable_eb_size) {
		ret = sprintf(buf, "%d\n", vol->usable_leb_size);
	} else if (attr == &vol_data_bytes)
		ret = sprintf(buf, "%lld\n", vol->used_bytes);
	else if (attr == &vol_upd_marker)
		ret = sprintf(buf, "%d\n", vol->upd_marker);
	else
		BUG();
	spin_unlock(&vol->ubi->volumes_lock);
	return ret;
}

/* Release method for volume devices */
static void vol_release(struct device *dev)
{
	struct ubi_volume *vol = container_of(dev, struct ubi_volume, dev);
	ubi_assert(vol->removed);
	kfree(vol);
}

/**
 * volume_sysfs_init - initialize sysfs for new volume.
 * @ubi: UBI device description object
 * @vol: volume description object
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 *
 * Note, this function does not free allocated resources in case of failure -
 * the caller does it. This is because this would cause release() here and the
 * caller would oops.
 */
static int volume_sysfs_init(struct ubi_device *ubi, struct ubi_volume *vol)
{
	int err;

	err = device_create_file(&vol->dev, &vol_reserved_ebs);
	if (err)
		return err;
	err = device_create_file(&vol->dev, &vol_type);
	if (err)
		return err;
	err = device_create_file(&vol->dev, &vol_name);
	if (err)
		return err;
	err = device_create_file(&vol->dev, &vol_corrupted);
	if (err)
		return err;
	err = device_create_file(&vol->dev, &vol_alignment);
	if (err)
		return err;
	err = device_create_file(&vol->dev, &vol_usable_eb_size);
	if (err)
		return err;
	err = device_create_file(&vol->dev, &vol_data_bytes);
	if (err)
		return err;
	err = device_create_file(&vol->dev, &vol_upd_marker);
	if (err)
		return err;
	return 0;
}

/**
 * volume_sysfs_close - close sysfs for a volume.
 * @vol: volume description object
 */
static void volume_sysfs_close(struct ubi_volume *vol)
{
	device_remove_file(&vol->dev, &vol_upd_marker);
	device_remove_file(&vol->dev, &vol_data_bytes);
	device_remove_file(&vol->dev, &vol_usable_eb_size);
	device_remove_file(&vol->dev, &vol_alignment);
	device_remove_file(&vol->dev, &vol_corrupted);
	device_remove_file(&vol->dev, &vol_name);
	device_remove_file(&vol->dev, &vol_type);
	device_remove_file(&vol->dev, &vol_reserved_ebs);
	device_unregister(&vol->dev);
}

/**
 * ubi_create_volume - create volume.
 * @ubi: UBI device description object
 * @req: volume creation request
 *
 * This function creates volume described by @req. If @req->vol_id id
 * %UBI_VOL_NUM_AUTO, this function automatically assigne ID to the new volume
 * and saves it in @req->vol_id. Returns zero in case of success and a negative
 * error code in case of failure.
 */
int ubi_create_volume(struct ubi_device *ubi, struct ubi_mkvol_req *req)
{
	int i, err, vol_id = req->vol_id;
	struct ubi_volume *vol;
	struct ubi_vtbl_record vtbl_rec;
	uint64_t bytes;

	if (ubi->ro_mode)
		return -EROFS;

	vol = kzalloc(sizeof(struct ubi_volume), GFP_KERNEL);
	if (!vol)
		return -ENOMEM;

	spin_lock(&ubi->volumes_lock);

	if (vol_id == UBI_VOL_NUM_AUTO) {
		/* Find unused volume ID */
		dbg_msg("search for vacant volume ID");
		for (i = 0; i < ubi->vtbl_slots; i++)
			if (!ubi->volumes[i]) {
				vol_id = i;
				break;
			}

		if (vol_id == UBI_VOL_NUM_AUTO) {
			dbg_err("out of volume IDs");
			err = -ENFILE;
			goto out_unlock;
		}
		req->vol_id = vol_id;
	}

	dbg_msg("volume ID %d, %llu bytes, type %d, name %s",
		vol_id, (unsigned long long)req->bytes,
		(int)req->vol_type, req->name);

	/* Ensure that this volume does not exist */
	err = -EEXIST;
	if (ubi->volumes[vol_id]) {
		dbg_err("volume %d already exists", vol_id);
		goto out_unlock;
	}

	/* Ensure that the name is unique */
	for (i = 0; i < ubi->vtbl_slots; i++)
		if (ubi->volumes[i] &&
		    ubi->volumes[i]->name_len == req->name_len &&
		    !strcmp(ubi->volumes[i]->name, req->name)) {
			dbg_err("volume \"%s\" exists (ID %d)", req->name, i);
			goto out_unlock;
		}

        /* Calculate how many eraseblocks are requested */
	vol->usable_leb_size = ubi->leb_size - ubi->leb_size % req->alignment;
	bytes = req->bytes;
	if (do_div(bytes, vol->usable_leb_size))
		vol->reserved_pebs = 1;
	vol->reserved_pebs += bytes;

	/* Reserve physical eraseblocks */
	if (vol->reserved_pebs > ubi->avail_pebs) {
		dbg_err("not enough PEBs, only %d available", ubi->avail_pebs);
		err = -ENOSPC;
		goto out_unlock;
	}
	ubi->avail_pebs -= vol->reserved_pebs;
	ubi->rsvd_pebs += vol->reserved_pebs;

	vol->vol_id    = vol_id;
	vol->alignment = req->alignment;
	vol->data_pad  = ubi->leb_size % vol->alignment;
	vol->vol_type  = req->vol_type;
	vol->name_len  = req->name_len;
	memcpy(vol->name, req->name, vol->name_len + 1);
	vol->exclusive = 1;
	vol->ubi = ubi;
	ubi->volumes[vol_id] = vol;
	spin_unlock(&ubi->volumes_lock);

	/*
	 * Finish all pending erases because there may be some LEBs belonging
	 * to the same volume ID.
	 */
	err = ubi_wl_flush(ubi);
	if (err)
		goto out_acc;

	vol->eba_tbl = kmalloc(vol->reserved_pebs * sizeof(int), GFP_KERNEL);
	if (!vol->eba_tbl) {
		err = -ENOMEM;
		goto out_acc;
	}

	for (i = 0; i < vol->reserved_pebs; i++)
		vol->eba_tbl[i] = UBI_LEB_UNMAPPED;

	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		vol->used_ebs = vol->reserved_pebs;
		vol->last_eb_bytes = vol->usable_leb_size;
		vol->used_bytes =
			(long long)vol->used_ebs * vol->usable_leb_size;
	} else {
		bytes = vol->used_bytes;
		vol->last_eb_bytes = do_div(bytes, vol->usable_leb_size);
		vol->used_ebs = bytes;
		if (vol->last_eb_bytes)
			vol->used_ebs += 1;
		else
			vol->last_eb_bytes = vol->usable_leb_size;
	}

	/* Register character device for the volume */
	cdev_init(&vol->cdev, &ubi_vol_cdev_operations);
	vol->cdev.owner = THIS_MODULE;
	err = cdev_add(&vol->cdev, MKDEV(ubi->major, vol_id + 1), 1);
	if (err) {
		ubi_err("cannot add character device for volume %d", vol_id);
		goto out_mapping;
	}

	err = ubi_create_gluebi(ubi, vol);
	if (err)
		goto out_cdev;

	vol->dev.release = vol_release;
	vol->dev.parent = &ubi->dev;
	vol->dev.devt = MKDEV(ubi->major, vol->vol_id + 1);
	vol->dev.class = ubi_class;
	sprintf(&vol->dev.bus_id[0], "%s_%d", ubi->ubi_name, vol->vol_id);
	err = device_register(&vol->dev);
	if (err)
		goto out_gluebi;

	err = volume_sysfs_init(ubi, vol);
	if (err)
		goto out_sysfs;

	/* Fill volume table record */
	memset(&vtbl_rec, 0, sizeof(struct ubi_vtbl_record));
	vtbl_rec.reserved_pebs = cpu_to_be32(vol->reserved_pebs);
	vtbl_rec.alignment     = cpu_to_be32(vol->alignment);
	vtbl_rec.data_pad      = cpu_to_be32(vol->data_pad);
	vtbl_rec.name_len      = cpu_to_be16(vol->name_len);
	if (vol->vol_type == UBI_DYNAMIC_VOLUME)
		vtbl_rec.vol_type = UBI_VID_DYNAMIC;
	else
		vtbl_rec.vol_type = UBI_VID_STATIC;
	memcpy(vtbl_rec.name, vol->name, vol->name_len + 1);

	err = ubi_change_vtbl_record(ubi, vol_id, &vtbl_rec);
	if (err)
		goto out_sysfs;

	spin_lock(&ubi->volumes_lock);
	ubi->vol_count += 1;
	vol->exclusive = 0;
	spin_unlock(&ubi->volumes_lock);

	paranoid_check_volumes(ubi);
	return 0;

out_gluebi:
	err = ubi_destroy_gluebi(vol);
out_cdev:
	cdev_del(&vol->cdev);
out_mapping:
	kfree(vol->eba_tbl);
out_acc:
	spin_lock(&ubi->volumes_lock);
	ubi->rsvd_pebs -= vol->reserved_pebs;
	ubi->avail_pebs += vol->reserved_pebs;
	ubi->volumes[vol_id] = NULL;
out_unlock:
	spin_unlock(&ubi->volumes_lock);
	kfree(vol);
	return err;

	/*
	 * We are registered, so @vol is destroyed in the release function and
	 * we have to de-initialize differently.
	 */
out_sysfs:
	err = ubi_destroy_gluebi(vol);
	cdev_del(&vol->cdev);
	kfree(vol->eba_tbl);
	spin_lock(&ubi->volumes_lock);
	ubi->rsvd_pebs -= vol->reserved_pebs;
	ubi->avail_pebs += vol->reserved_pebs;
	ubi->volumes[vol_id] = NULL;
	spin_unlock(&ubi->volumes_lock);
	volume_sysfs_close(vol);
	return err;
}

/**
 * ubi_remove_volume - remove volume.
 * @desc: volume descriptor
 *
 * This function removes volume described by @desc. The volume has to be opened
 * in "exclusive" mode. Returns zero in case of success and a negative error
 * code in case of failure.
 */
int ubi_remove_volume(struct ubi_volume_desc *desc)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int i, err, vol_id = vol->vol_id, reserved_pebs = vol->reserved_pebs;

	dbg_msg("remove UBI volume %d", vol_id);
	ubi_assert(desc->mode == UBI_EXCLUSIVE);
	ubi_assert(vol == ubi->volumes[vol_id]);

	if (ubi->ro_mode)
		return -EROFS;

	err = ubi_destroy_gluebi(vol);
	if (err)
		return err;

	err = ubi_change_vtbl_record(ubi, vol_id, NULL);
	if (err)
		return err;

	for (i = 0; i < vol->reserved_pebs; i++) {
		err = ubi_eba_unmap_leb(ubi, vol_id, i);
		if (err)
			return err;
	}

	spin_lock(&ubi->volumes_lock);
	vol->removed = 1;
	ubi->volumes[vol_id] = NULL;
	spin_unlock(&ubi->volumes_lock);

	kfree(vol->eba_tbl);
	vol->eba_tbl = NULL;
	cdev_del(&vol->cdev);
	volume_sysfs_close(vol);
	kfree(desc);

	spin_lock(&ubi->volumes_lock);
	ubi->rsvd_pebs -= reserved_pebs;
	ubi->avail_pebs += reserved_pebs;
	i = ubi->beb_rsvd_level - ubi->beb_rsvd_pebs;
	if (i > 0) {
		i = ubi->avail_pebs >= i ? i : ubi->avail_pebs;
		ubi->avail_pebs -= i;
		ubi->rsvd_pebs += i;
		ubi->beb_rsvd_pebs += i;
		if (i > 0)
			ubi_msg("reserve more %d PEBs", i);
	}
	ubi->vol_count -= 1;
	spin_unlock(&ubi->volumes_lock);

	paranoid_check_volumes(ubi);
	module_put(THIS_MODULE);
	return 0;
}

/**
 * ubi_resize_volume - re-size volume.
 * @desc: volume descriptor
 * @reserved_pebs: new size in physical eraseblocks
 *
 * This function returns zero in case of success, and a negative error code in
 * case of failure.
 */
int ubi_resize_volume(struct ubi_volume_desc *desc, int reserved_pebs)
{
	int i, err, pebs, *new_mapping;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	struct ubi_vtbl_record vtbl_rec;
	int vol_id = vol->vol_id;

	if (ubi->ro_mode)
		return -EROFS;

	dbg_msg("re-size volume %d to from %d to %d PEBs",
		vol_id, vol->reserved_pebs, reserved_pebs);
	ubi_assert(desc->mode == UBI_EXCLUSIVE);
	ubi_assert(vol == ubi->volumes[vol_id]);

	if (vol->vol_type == UBI_STATIC_VOLUME &&
	    reserved_pebs < vol->used_ebs) {
		dbg_err("too small size %d, %d LEBs contain data",
			reserved_pebs, vol->used_ebs);
		return -EINVAL;
	}

	/* If the size is the same, we have nothing to do */
	if (reserved_pebs == vol->reserved_pebs)
		return 0;

	new_mapping = kmalloc(reserved_pebs * sizeof(int), GFP_KERNEL);
	if (!new_mapping)
		return -ENOMEM;

	for (i = 0; i < reserved_pebs; i++)
		new_mapping[i] = UBI_LEB_UNMAPPED;

	/* Reserve physical eraseblocks */
	pebs = reserved_pebs - vol->reserved_pebs;
	if (pebs > 0) {
		spin_lock(&ubi->volumes_lock);
		if (pebs > ubi->avail_pebs) {
			dbg_err("not enough PEBs: requested %d, available %d",
				pebs, ubi->avail_pebs);
			spin_unlock(&ubi->volumes_lock);
			err = -ENOSPC;
			goto out_free;
		}
		ubi->avail_pebs -= pebs;
		ubi->rsvd_pebs += pebs;
		for (i = 0; i < vol->reserved_pebs; i++)
			new_mapping[i] = vol->eba_tbl[i];
		kfree(vol->eba_tbl);
		vol->eba_tbl = new_mapping;
		spin_unlock(&ubi->volumes_lock);
	}

	/* Change volume table record */
	memcpy(&vtbl_rec, &ubi->vtbl[vol_id], sizeof(struct ubi_vtbl_record));
	vtbl_rec.reserved_pebs = cpu_to_be32(reserved_pebs);
	err = ubi_change_vtbl_record(ubi, vol_id, &vtbl_rec);
	if (err)
		goto out_acc;

	if (pebs < 0) {
		for (i = 0; i < -pebs; i++) {
			err = ubi_eba_unmap_leb(ubi, vol_id, reserved_pebs + i);
			if (err)
				goto out_acc;
		}
		spin_lock(&ubi->volumes_lock);
		ubi->rsvd_pebs += pebs;
		ubi->avail_pebs -= pebs;
		pebs = ubi->beb_rsvd_level - ubi->beb_rsvd_pebs;
		if (pebs > 0) {
			pebs = ubi->avail_pebs >= pebs ? pebs : ubi->avail_pebs;
			ubi->avail_pebs -= pebs;
			ubi->rsvd_pebs += pebs;
			ubi->beb_rsvd_pebs += pebs;
			if (pebs > 0)
				ubi_msg("reserve more %d PEBs", pebs);
		}
		for (i = 0; i < reserved_pebs; i++)
			new_mapping[i] = vol->eba_tbl[i];
		kfree(vol->eba_tbl);
		vol->eba_tbl = new_mapping;
		spin_unlock(&ubi->volumes_lock);
	}

	vol->reserved_pebs = reserved_pebs;
	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		vol->used_ebs = reserved_pebs;
		vol->last_eb_bytes = vol->usable_leb_size;
		vol->used_bytes =
			(long long)vol->used_ebs * vol->usable_leb_size;
	}

	paranoid_check_volumes(ubi);
	return 0;

out_acc:
	if (pebs > 0) {
		spin_lock(&ubi->volumes_lock);
		ubi->rsvd_pebs -= pebs;
		ubi->avail_pebs += pebs;
		spin_unlock(&ubi->volumes_lock);
	}
out_free:
	kfree(new_mapping);
	return err;
}

/**
 * ubi_add_volume - add volume.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 *
 * This function adds an existin volume and initializes all its data
 * structures. Returnes zero in case of success and a negative error code in
 * case of failure.
 */
int ubi_add_volume(struct ubi_device *ubi, int vol_id)
{
	int err;
	struct ubi_volume *vol = ubi->volumes[vol_id];

	dbg_msg("add volume %d", vol_id);
	ubi_dbg_dump_vol_info(vol);
	ubi_assert(vol);

	/* Register character device for the volume */
	cdev_init(&vol->cdev, &ubi_vol_cdev_operations);
	vol->cdev.owner = THIS_MODULE;
	err = cdev_add(&vol->cdev, MKDEV(ubi->major, vol->vol_id + 1), 1);
	if (err) {
		ubi_err("cannot add character device for volume %d", vol_id);
		return err;
	}

	err = ubi_create_gluebi(ubi, vol);
	if (err)
		goto out_cdev;

	vol->dev.release = vol_release;
	vol->dev.parent = &ubi->dev;
	vol->dev.devt = MKDEV(ubi->major, vol->vol_id + 1);
	vol->dev.class = ubi_class;
	sprintf(&vol->dev.bus_id[0], "%s_%d", ubi->ubi_name, vol->vol_id);
	err = device_register(&vol->dev);
	if (err)
		goto out_gluebi;

	err = volume_sysfs_init(ubi, vol);
	if (err) {
		cdev_del(&vol->cdev);
		err = ubi_destroy_gluebi(vol);
		volume_sysfs_close(vol);
		return err;
	}

	paranoid_check_volumes(ubi);
	return 0;

out_gluebi:
	err = ubi_destroy_gluebi(vol);
out_cdev:
	cdev_del(&vol->cdev);
	return err;
}

/**
 * ubi_free_volume - free volume.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 *
 * This function frees all resources for volume @vol_id but does not remove it.
 * Used only when the UBI device is detached.
 */
void ubi_free_volume(struct ubi_device *ubi, int vol_id)
{
	int err;
	struct ubi_volume *vol = ubi->volumes[vol_id];

	dbg_msg("free volume %d", vol_id);
	ubi_assert(vol);

	vol->removed = 1;
	err = ubi_destroy_gluebi(vol);
	ubi->volumes[vol_id] = NULL;
	cdev_del(&vol->cdev);
	volume_sysfs_close(vol);
}

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID

/**
 * paranoid_check_volume - check volume information.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 */
static void paranoid_check_volume(struct ubi_device *ubi, int vol_id)
{
	int idx = vol_id2idx(ubi, vol_id);
	int reserved_pebs, alignment, data_pad, vol_type, name_len, upd_marker;
	const struct ubi_volume *vol;
	long long n;
	const char *name;

	spin_lock(&ubi->volumes_lock);
	reserved_pebs = be32_to_cpu(ubi->vtbl[vol_id].reserved_pebs);
	vol = ubi->volumes[idx];

	if (!vol) {
		if (reserved_pebs) {
			ubi_err("no volume info, but volume exists");
			goto fail;
		}
		spin_unlock(&ubi->volumes_lock);
		return;
	}

	if (vol->exclusive) {
		/*
		 * The volume may be being created at the moment, do not check
		 * it (e.g., it may be in the middle of ubi_create_volume().
		 */
		spin_unlock(&ubi->volumes_lock);
		return;
	}

	if (vol->reserved_pebs < 0 || vol->alignment < 0 || vol->data_pad < 0 ||
	    vol->name_len < 0) {
		ubi_err("negative values");
		goto fail;
	}
	if (vol->alignment > ubi->leb_size || vol->alignment == 0) {
		ubi_err("bad alignment");
		goto fail;
	}

	n = vol->alignment % ubi->min_io_size;
	if (vol->alignment != 1 && n) {
		ubi_err("alignment is not multiple of min I/O unit");
		goto fail;
	}

	n = ubi->leb_size % vol->alignment;
	if (vol->data_pad != n) {
		ubi_err("bad data_pad, has to be %lld", n);
		goto fail;
	}

	if (vol->vol_type != UBI_DYNAMIC_VOLUME &&
	    vol->vol_type != UBI_STATIC_VOLUME) {
		ubi_err("bad vol_type");
		goto fail;
	}

	if (vol->upd_marker != 0 && vol->upd_marker != 1) {
		ubi_err("bad upd_marker");
		goto fail;
	}

	if (vol->upd_marker && vol->corrupted) {
		dbg_err("update marker and corrupted simultaneously");
		goto fail;
	}

	if (vol->reserved_pebs > ubi->good_peb_count) {
		ubi_err("too large reserved_pebs");
		goto fail;
	}

	n = ubi->leb_size - vol->data_pad;
	if (vol->usable_leb_size != ubi->leb_size - vol->data_pad) {
		ubi_err("bad usable_leb_size, has to be %lld", n);
		goto fail;
	}

	if (vol->name_len > UBI_VOL_NAME_MAX) {
		ubi_err("too long volume name, max is %d", UBI_VOL_NAME_MAX);
		goto fail;
	}

	if (!vol->name) {
		ubi_err("NULL volume name");
		goto fail;
	}

	n = strnlen(vol->name, vol->name_len + 1);
	if (n != vol->name_len) {
		ubi_err("bad name_len %lld", n);
		goto fail;
	}

	n = (long long)vol->used_ebs * vol->usable_leb_size;
	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		if (vol->corrupted != 0) {
			ubi_err("corrupted dynamic volume");
			goto fail;
		}
		if (vol->used_ebs != vol->reserved_pebs) {
			ubi_err("bad used_ebs");
			goto fail;
		}
		if (vol->last_eb_bytes != vol->usable_leb_size) {
			ubi_err("bad last_eb_bytes");
			goto fail;
		}
		if (vol->used_bytes != n) {
			ubi_err("bad used_bytes");
			goto fail;
		}
	} else {
		if (vol->corrupted != 0 && vol->corrupted != 1) {
			ubi_err("bad corrupted");
			goto fail;
		}
		if (vol->used_ebs < 0 || vol->used_ebs > vol->reserved_pebs) {
			ubi_err("bad used_ebs");
			goto fail;
		}
		if (vol->last_eb_bytes < 0 ||
		    vol->last_eb_bytes > vol->usable_leb_size) {
			ubi_err("bad last_eb_bytes");
			goto fail;
		}
		if (vol->used_bytes < 0 || vol->used_bytes > n ||
		    vol->used_bytes < n - vol->usable_leb_size) {
			ubi_err("bad used_bytes");
			goto fail;
		}
	}

	alignment  = be32_to_cpu(ubi->vtbl[vol_id].alignment);
	data_pad   = be32_to_cpu(ubi->vtbl[vol_id].data_pad);
	name_len   = be16_to_cpu(ubi->vtbl[vol_id].name_len);
	upd_marker = ubi->vtbl[vol_id].upd_marker;
	name       = &ubi->vtbl[vol_id].name[0];
	if (ubi->vtbl[vol_id].vol_type == UBI_VID_DYNAMIC)
		vol_type = UBI_DYNAMIC_VOLUME;
	else
		vol_type = UBI_STATIC_VOLUME;

	if (alignment != vol->alignment || data_pad != vol->data_pad ||
	    upd_marker != vol->upd_marker || vol_type != vol->vol_type ||
	    name_len!= vol->name_len || strncmp(name, vol->name, name_len)) {
		ubi_err("volume info is different");
		goto fail;
	}

	spin_unlock(&ubi->volumes_lock);
	return;

fail:
	ubi_err("paranoid check failed for volume %d", vol_id);
	ubi_dbg_dump_vol_info(vol);
	ubi_dbg_dump_vtbl_record(&ubi->vtbl[vol_id], vol_id);
	spin_unlock(&ubi->volumes_lock);
	BUG();
}

/**
 * paranoid_check_volumes - check information about all volumes.
 * @ubi: UBI device description object
 */
static void paranoid_check_volumes(struct ubi_device *ubi)
{
	int i;

	mutex_lock(&ubi->vtbl_mutex);
	for (i = 0; i < ubi->vtbl_slots; i++)
		paranoid_check_volume(ubi, i);
	mutex_unlock(&ubi->vtbl_mutex);
}
#endif
