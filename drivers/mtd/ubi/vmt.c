// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file contains implementation of volume creation, deletion, updating and
 * resizing.
 */

#include <linux/err.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/export.h>
#include "ubi.h"

static int self_check_volumes(struct ubi_device *ubi);

static ssize_t vol_attribute_show(struct device *dev,
				  struct device_attribute *attr, char *buf);

/* Device attributes corresponding to files in '/<sysfs>/class/ubi/ubiX_Y' */
static struct device_attribute attr_vol_reserved_ebs =
	__ATTR(reserved_ebs, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute attr_vol_type =
	__ATTR(type, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute attr_vol_name =
	__ATTR(name, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute attr_vol_corrupted =
	__ATTR(corrupted, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute attr_vol_alignment =
	__ATTR(alignment, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute attr_vol_usable_eb_size =
	__ATTR(usable_eb_size, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute attr_vol_data_bytes =
	__ATTR(data_bytes, S_IRUGO, vol_attribute_show, NULL);
static struct device_attribute attr_vol_upd_marker =
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
 * In this situation, this function will return %-ENODEV because it will find
 * out that the volume was removed from the @ubi->volumes array.
 */
static ssize_t vol_attribute_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret;
	struct ubi_volume *vol = container_of(dev, struct ubi_volume, dev);
	struct ubi_device *ubi = vol->ubi;

	spin_lock(&ubi->volumes_lock);
	if (!ubi->volumes[vol->vol_id]) {
		spin_unlock(&ubi->volumes_lock);
		return -ENODEV;
	}
	/* Take a reference to prevent volume removal */
	vol->ref_count += 1;
	spin_unlock(&ubi->volumes_lock);

	if (attr == &attr_vol_reserved_ebs)
		ret = sprintf(buf, "%d\n", vol->reserved_pebs);
	else if (attr == &attr_vol_type) {
		const char *tp;

		if (vol->vol_type == UBI_DYNAMIC_VOLUME)
			tp = "dynamic";
		else
			tp = "static";
		ret = sprintf(buf, "%s\n", tp);
	} else if (attr == &attr_vol_name)
		ret = sprintf(buf, "%s\n", vol->name);
	else if (attr == &attr_vol_corrupted)
		ret = sprintf(buf, "%d\n", vol->corrupted);
	else if (attr == &attr_vol_alignment)
		ret = sprintf(buf, "%d\n", vol->alignment);
	else if (attr == &attr_vol_usable_eb_size)
		ret = sprintf(buf, "%d\n", vol->usable_leb_size);
	else if (attr == &attr_vol_data_bytes)
		ret = sprintf(buf, "%lld\n", vol->used_bytes);
	else if (attr == &attr_vol_upd_marker)
		ret = sprintf(buf, "%d\n", vol->upd_marker);
	else
		/* This must be a bug */
		ret = -EINVAL;

	/* We've done the operation, drop volume and UBI device references */
	spin_lock(&ubi->volumes_lock);
	vol->ref_count -= 1;
	ubi_assert(vol->ref_count >= 0);
	spin_unlock(&ubi->volumes_lock);
	return ret;
}

static struct attribute *volume_dev_attrs[] = {
	&attr_vol_reserved_ebs.attr,
	&attr_vol_type.attr,
	&attr_vol_name.attr,
	&attr_vol_corrupted.attr,
	&attr_vol_alignment.attr,
	&attr_vol_usable_eb_size.attr,
	&attr_vol_data_bytes.attr,
	&attr_vol_upd_marker.attr,
	NULL
};
ATTRIBUTE_GROUPS(volume_dev);

/* Release method for volume devices */
static void vol_release(struct device *dev)
{
	struct ubi_volume *vol = container_of(dev, struct ubi_volume, dev);

	ubi_eba_replace_table(vol, NULL);
	ubi_fastmap_destroy_checkmap(vol);
	kfree(vol);
}

/**
 * ubi_create_volume - create volume.
 * @ubi: UBI device description object
 * @req: volume creation request
 *
 * This function creates volume described by @req. If @req->vol_id id
 * %UBI_VOL_NUM_AUTO, this function automatically assign ID to the new volume
 * and saves it in @req->vol_id. Returns zero in case of success and a negative
 * error code in case of failure. Note, the caller has to have the
 * @ubi->device_mutex locked.
 */
int ubi_create_volume(struct ubi_device *ubi, struct ubi_mkvol_req *req)
{
	int i, err, vol_id = req->vol_id;
	struct ubi_volume *vol;
	struct ubi_vtbl_record vtbl_rec;
	struct ubi_eba_table *eba_tbl = NULL;

	if (ubi->ro_mode)
		return -EROFS;

	vol = kzalloc(sizeof(struct ubi_volume), GFP_KERNEL);
	if (!vol)
		return -ENOMEM;

	device_initialize(&vol->dev);
	vol->dev.release = vol_release;
	vol->dev.parent = &ubi->dev;
	vol->dev.class = &ubi_class;
	vol->dev.groups = volume_dev_groups;

	if (req->flags & UBI_VOL_SKIP_CRC_CHECK_FLG)
		vol->skip_check = 1;

	spin_lock(&ubi->volumes_lock);
	if (vol_id == UBI_VOL_NUM_AUTO) {
		/* Find unused volume ID */
		dbg_gen("search for vacant volume ID");
		for (i = 0; i < ubi->vtbl_slots; i++)
			if (!ubi->volumes[i]) {
				vol_id = i;
				break;
			}

		if (vol_id == UBI_VOL_NUM_AUTO) {
			ubi_err(ubi, "out of volume IDs");
			err = -ENFILE;
			goto out_unlock;
		}
		req->vol_id = vol_id;
	}

	dbg_gen("create device %d, volume %d, %llu bytes, type %d, name %s",
		ubi->ubi_num, vol_id, (unsigned long long)req->bytes,
		(int)req->vol_type, req->name);

	/* Ensure that this volume does not exist */
	err = -EEXIST;
	if (ubi->volumes[vol_id]) {
		ubi_err(ubi, "volume %d already exists", vol_id);
		goto out_unlock;
	}

	/* Ensure that the name is unique */
	for (i = 0; i < ubi->vtbl_slots; i++)
		if (ubi->volumes[i] &&
		    ubi->volumes[i]->name_len == req->name_len &&
		    !strcmp(ubi->volumes[i]->name, req->name)) {
			ubi_err(ubi, "volume \"%s\" exists (ID %d)",
				req->name, i);
			goto out_unlock;
		}

	/* Calculate how many eraseblocks are requested */
	vol->usable_leb_size = ubi->leb_size - ubi->leb_size % req->alignment;
	vol->reserved_pebs = div_u64(req->bytes + vol->usable_leb_size - 1,
				     vol->usable_leb_size);

	/* Reserve physical eraseblocks */
	if (vol->reserved_pebs > ubi->avail_pebs) {
		ubi_err(ubi, "not enough PEBs, only %d available",
			ubi->avail_pebs);
		if (ubi->corr_peb_count)
			ubi_err(ubi, "%d PEBs are corrupted and not used",
				ubi->corr_peb_count);
		err = -ENOSPC;
		goto out_unlock;
	}
	ubi->avail_pebs -= vol->reserved_pebs;
	ubi->rsvd_pebs += vol->reserved_pebs;
	spin_unlock(&ubi->volumes_lock);

	vol->vol_id    = vol_id;
	vol->alignment = req->alignment;
	vol->data_pad  = ubi->leb_size % vol->alignment;
	vol->vol_type  = req->vol_type;
	vol->name_len  = req->name_len;
	memcpy(vol->name, req->name, vol->name_len);
	vol->ubi = ubi;

	/*
	 * Finish all pending erases because there may be some LEBs belonging
	 * to the same volume ID.
	 */
	err = ubi_wl_flush(ubi, vol_id, UBI_ALL);
	if (err)
		goto out_acc;

	eba_tbl = ubi_eba_create_table(vol, vol->reserved_pebs);
	if (IS_ERR(eba_tbl)) {
		err = PTR_ERR(eba_tbl);
		goto out_acc;
	}

	ubi_eba_replace_table(vol, eba_tbl);

	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		vol->used_ebs = vol->reserved_pebs;
		vol->last_eb_bytes = vol->usable_leb_size;
		vol->used_bytes =
			(long long)vol->used_ebs * vol->usable_leb_size;
	} else {
		vol->used_ebs = div_u64_rem(vol->used_bytes,
					    vol->usable_leb_size,
					    &vol->last_eb_bytes);
		if (vol->last_eb_bytes != 0)
			vol->used_ebs += 1;
		else
			vol->last_eb_bytes = vol->usable_leb_size;
	}

	/* Make volume "available" before it becomes accessible via sysfs */
	spin_lock(&ubi->volumes_lock);
	ubi->volumes[vol_id] = vol;
	ubi->vol_count += 1;
	spin_unlock(&ubi->volumes_lock);

	/* Register character device for the volume */
	cdev_init(&vol->cdev, &ubi_vol_cdev_operations);
	vol->cdev.owner = THIS_MODULE;

	vol->dev.devt = MKDEV(MAJOR(ubi->cdev.dev), vol_id + 1);
	dev_set_name(&vol->dev, "%s_%d", ubi->ubi_name, vol->vol_id);
	err = cdev_device_add(&vol->cdev, &vol->dev);
	if (err) {
		ubi_err(ubi, "cannot add device");
		goto out_mapping;
	}

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

	if (vol->skip_check)
		vtbl_rec.flags |= UBI_VTBL_SKIP_CRC_CHECK_FLG;

	memcpy(vtbl_rec.name, vol->name, vol->name_len);

	err = ubi_change_vtbl_record(ubi, vol_id, &vtbl_rec);
	if (err)
		goto out_sysfs;

	ubi_volume_notify(ubi, vol, UBI_VOLUME_ADDED);
	self_check_volumes(ubi);
	return err;

out_sysfs:
	/*
	 * We have registered our device, we should not free the volume
	 * description object in this function in case of an error - it is
	 * freed by the release function.
	 */
	cdev_device_del(&vol->cdev, &vol->dev);
out_mapping:
	spin_lock(&ubi->volumes_lock);
	ubi->volumes[vol_id] = NULL;
	ubi->vol_count -= 1;
	spin_unlock(&ubi->volumes_lock);
out_acc:
	spin_lock(&ubi->volumes_lock);
	ubi->rsvd_pebs -= vol->reserved_pebs;
	ubi->avail_pebs += vol->reserved_pebs;
out_unlock:
	spin_unlock(&ubi->volumes_lock);
	put_device(&vol->dev);
	ubi_err(ubi, "cannot create volume %d, error %d", vol_id, err);
	return err;
}

/**
 * ubi_remove_volume - remove volume.
 * @desc: volume descriptor
 * @no_vtbl: do not change volume table if not zero
 *
 * This function removes volume described by @desc. The volume has to be opened
 * in "exclusive" mode. Returns zero in case of success and a negative error
 * code in case of failure. The caller has to have the @ubi->device_mutex
 * locked.
 */
int ubi_remove_volume(struct ubi_volume_desc *desc, int no_vtbl)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int i, err, vol_id = vol->vol_id, reserved_pebs = vol->reserved_pebs;

	dbg_gen("remove device %d, volume %d", ubi->ubi_num, vol_id);
	ubi_assert(desc->mode == UBI_EXCLUSIVE);
	ubi_assert(vol == ubi->volumes[vol_id]);

	if (ubi->ro_mode)
		return -EROFS;

	spin_lock(&ubi->volumes_lock);
	if (vol->ref_count > 1) {
		/*
		 * The volume is busy, probably someone is reading one of its
		 * sysfs files.
		 */
		err = -EBUSY;
		goto out_unlock;
	}
	ubi->volumes[vol_id] = NULL;
	spin_unlock(&ubi->volumes_lock);

	if (!no_vtbl) {
		err = ubi_change_vtbl_record(ubi, vol_id, NULL);
		if (err)
			goto out_err;
	}

	for (i = 0; i < vol->reserved_pebs; i++) {
		err = ubi_eba_unmap_leb(ubi, vol, i);
		if (err)
			goto out_err;
	}

	cdev_device_del(&vol->cdev, &vol->dev);
	put_device(&vol->dev);

	spin_lock(&ubi->volumes_lock);
	ubi->rsvd_pebs -= reserved_pebs;
	ubi->avail_pebs += reserved_pebs;
	ubi_update_reserved(ubi);
	ubi->vol_count -= 1;
	spin_unlock(&ubi->volumes_lock);

	ubi_volume_notify(ubi, vol, UBI_VOLUME_REMOVED);
	if (!no_vtbl)
		self_check_volumes(ubi);

	return 0;

out_err:
	ubi_err(ubi, "cannot remove volume %d, error %d", vol_id, err);
	spin_lock(&ubi->volumes_lock);
	ubi->volumes[vol_id] = vol;
out_unlock:
	spin_unlock(&ubi->volumes_lock);
	return err;
}

/**
 * ubi_resize_volume - re-size volume.
 * @desc: volume descriptor
 * @reserved_pebs: new size in physical eraseblocks
 *
 * This function re-sizes the volume and returns zero in case of success, and a
 * negative error code in case of failure. The caller has to have the
 * @ubi->device_mutex locked.
 */
int ubi_resize_volume(struct ubi_volume_desc *desc, int reserved_pebs)
{
	int i, err, pebs;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	struct ubi_vtbl_record vtbl_rec;
	struct ubi_eba_table *new_eba_tbl = NULL;
	int vol_id = vol->vol_id;

	if (ubi->ro_mode)
		return -EROFS;

	dbg_gen("re-size device %d, volume %d to from %d to %d PEBs",
		ubi->ubi_num, vol_id, vol->reserved_pebs, reserved_pebs);

	if (vol->vol_type == UBI_STATIC_VOLUME &&
	    reserved_pebs < vol->used_ebs) {
		ubi_err(ubi, "too small size %d, %d LEBs contain data",
			reserved_pebs, vol->used_ebs);
		return -EINVAL;
	}

	/* If the size is the same, we have nothing to do */
	if (reserved_pebs == vol->reserved_pebs)
		return 0;

	new_eba_tbl = ubi_eba_create_table(vol, reserved_pebs);
	if (IS_ERR(new_eba_tbl))
		return PTR_ERR(new_eba_tbl);

	spin_lock(&ubi->volumes_lock);
	if (vol->ref_count > 1) {
		spin_unlock(&ubi->volumes_lock);
		err = -EBUSY;
		goto out_free;
	}
	spin_unlock(&ubi->volumes_lock);

	/* Reserve physical eraseblocks */
	pebs = reserved_pebs - vol->reserved_pebs;
	if (pebs > 0) {
		spin_lock(&ubi->volumes_lock);
		if (pebs > ubi->avail_pebs) {
			ubi_err(ubi, "not enough PEBs: requested %d, available %d",
				pebs, ubi->avail_pebs);
			if (ubi->corr_peb_count)
				ubi_err(ubi, "%d PEBs are corrupted and not used",
					ubi->corr_peb_count);
			spin_unlock(&ubi->volumes_lock);
			err = -ENOSPC;
			goto out_free;
		}
		ubi->avail_pebs -= pebs;
		ubi->rsvd_pebs += pebs;
		ubi_eba_copy_table(vol, new_eba_tbl, vol->reserved_pebs);
		ubi_eba_replace_table(vol, new_eba_tbl);
		spin_unlock(&ubi->volumes_lock);
	}

	if (pebs < 0) {
		for (i = 0; i < -pebs; i++) {
			err = ubi_eba_unmap_leb(ubi, vol, reserved_pebs + i);
			if (err)
				goto out_acc;
		}
		spin_lock(&ubi->volumes_lock);
		ubi->rsvd_pebs += pebs;
		ubi->avail_pebs -= pebs;
		ubi_update_reserved(ubi);
		ubi_eba_copy_table(vol, new_eba_tbl, reserved_pebs);
		ubi_eba_replace_table(vol, new_eba_tbl);
		spin_unlock(&ubi->volumes_lock);
	}

	/*
	 * When we shrink a volume we have to flush all pending (erase) work.
	 * Otherwise it can happen that upon next attach UBI finds a LEB with
	 * lnum > highest_lnum and refuses to attach.
	 */
	if (pebs < 0) {
		err = ubi_wl_flush(ubi, vol_id, UBI_ALL);
		if (err)
			goto out_acc;
	}

	/* Change volume table record */
	vtbl_rec = ubi->vtbl[vol_id];
	vtbl_rec.reserved_pebs = cpu_to_be32(reserved_pebs);
	err = ubi_change_vtbl_record(ubi, vol_id, &vtbl_rec);
	if (err)
		goto out_acc;

	vol->reserved_pebs = reserved_pebs;
	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		vol->used_ebs = reserved_pebs;
		vol->last_eb_bytes = vol->usable_leb_size;
		vol->used_bytes =
			(long long)vol->used_ebs * vol->usable_leb_size;
	}

	ubi_volume_notify(ubi, vol, UBI_VOLUME_RESIZED);
	self_check_volumes(ubi);
	return err;

out_acc:
	if (pebs > 0) {
		spin_lock(&ubi->volumes_lock);
		ubi->rsvd_pebs -= pebs;
		ubi->avail_pebs += pebs;
		spin_unlock(&ubi->volumes_lock);
	}
out_free:
	kfree(new_eba_tbl);
	return err;
}

/**
 * ubi_rename_volumes - re-name UBI volumes.
 * @ubi: UBI device description object
 * @rename_list: list of &struct ubi_rename_entry objects
 *
 * This function re-names or removes volumes specified in the re-name list.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
int ubi_rename_volumes(struct ubi_device *ubi, struct list_head *rename_list)
{
	int err;
	struct ubi_rename_entry *re;

	err = ubi_vtbl_rename_volumes(ubi, rename_list);
	if (err)
		return err;

	list_for_each_entry(re, rename_list, list) {
		if (re->remove) {
			err = ubi_remove_volume(re->desc, 1);
			if (err)
				break;
		} else {
			struct ubi_volume *vol = re->desc->vol;

			spin_lock(&ubi->volumes_lock);
			vol->name_len = re->new_name_len;
			memcpy(vol->name, re->new_name, re->new_name_len + 1);
			spin_unlock(&ubi->volumes_lock);
			ubi_volume_notify(ubi, vol, UBI_VOLUME_RENAMED);
		}
	}

	if (!err)
		self_check_volumes(ubi);
	return err;
}

/**
 * ubi_add_volume - add volume.
 * @ubi: UBI device description object
 * @vol: volume description object
 *
 * This function adds an existing volume and initializes all its data
 * structures. Returns zero in case of success and a negative error code in
 * case of failure.
 */
int ubi_add_volume(struct ubi_device *ubi, struct ubi_volume *vol)
{
	int err, vol_id = vol->vol_id;
	dev_t dev;

	dbg_gen("add volume %d", vol_id);

	/* Register character device for the volume */
	cdev_init(&vol->cdev, &ubi_vol_cdev_operations);
	vol->cdev.owner = THIS_MODULE;
	dev = MKDEV(MAJOR(ubi->cdev.dev), vol->vol_id + 1);
	err = cdev_add(&vol->cdev, dev, 1);
	if (err) {
		ubi_err(ubi, "cannot add character device for volume %d, error %d",
			vol_id, err);
		return err;
	}

	vol->dev.release = vol_release;
	vol->dev.parent = &ubi->dev;
	vol->dev.devt = dev;
	vol->dev.class = &ubi_class;
	vol->dev.groups = volume_dev_groups;
	dev_set_name(&vol->dev, "%s_%d", ubi->ubi_name, vol->vol_id);
	err = device_register(&vol->dev);
	if (err)
		goto out_cdev;

	self_check_volumes(ubi);
	return err;

out_cdev:
	cdev_del(&vol->cdev);
	return err;
}

/**
 * ubi_free_volume - free volume.
 * @ubi: UBI device description object
 * @vol: volume description object
 *
 * This function frees all resources for volume @vol but does not remove it.
 * Used only when the UBI device is detached.
 */
void ubi_free_volume(struct ubi_device *ubi, struct ubi_volume *vol)
{
	dbg_gen("free volume %d", vol->vol_id);

	ubi->volumes[vol->vol_id] = NULL;
	cdev_del(&vol->cdev);
	device_unregister(&vol->dev);
}

/**
 * self_check_volume - check volume information.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 *
 * Returns zero if volume is all right and a negative error code if not.
 */
static int self_check_volume(struct ubi_device *ubi, int vol_id)
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
			ubi_err(ubi, "no volume info, but volume exists");
			goto fail;
		}
		spin_unlock(&ubi->volumes_lock);
		return 0;
	}

	if (vol->reserved_pebs < 0 || vol->alignment < 0 || vol->data_pad < 0 ||
	    vol->name_len < 0) {
		ubi_err(ubi, "negative values");
		goto fail;
	}
	if (vol->alignment > ubi->leb_size || vol->alignment == 0) {
		ubi_err(ubi, "bad alignment");
		goto fail;
	}

	n = vol->alignment & (ubi->min_io_size - 1);
	if (vol->alignment != 1 && n) {
		ubi_err(ubi, "alignment is not multiple of min I/O unit");
		goto fail;
	}

	n = ubi->leb_size % vol->alignment;
	if (vol->data_pad != n) {
		ubi_err(ubi, "bad data_pad, has to be %lld", n);
		goto fail;
	}

	if (vol->vol_type != UBI_DYNAMIC_VOLUME &&
	    vol->vol_type != UBI_STATIC_VOLUME) {
		ubi_err(ubi, "bad vol_type");
		goto fail;
	}

	if (vol->upd_marker && vol->corrupted) {
		ubi_err(ubi, "update marker and corrupted simultaneously");
		goto fail;
	}

	if (vol->reserved_pebs > ubi->good_peb_count) {
		ubi_err(ubi, "too large reserved_pebs");
		goto fail;
	}

	n = ubi->leb_size - vol->data_pad;
	if (vol->usable_leb_size != ubi->leb_size - vol->data_pad) {
		ubi_err(ubi, "bad usable_leb_size, has to be %lld", n);
		goto fail;
	}

	if (vol->name_len > UBI_VOL_NAME_MAX) {
		ubi_err(ubi, "too long volume name, max is %d",
			UBI_VOL_NAME_MAX);
		goto fail;
	}

	n = strnlen(vol->name, vol->name_len + 1);
	if (n != vol->name_len) {
		ubi_err(ubi, "bad name_len %lld", n);
		goto fail;
	}

	n = (long long)vol->used_ebs * vol->usable_leb_size;
	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		if (vol->corrupted) {
			ubi_err(ubi, "corrupted dynamic volume");
			goto fail;
		}
		if (vol->used_ebs != vol->reserved_pebs) {
			ubi_err(ubi, "bad used_ebs");
			goto fail;
		}
		if (vol->last_eb_bytes != vol->usable_leb_size) {
			ubi_err(ubi, "bad last_eb_bytes");
			goto fail;
		}
		if (vol->used_bytes != n) {
			ubi_err(ubi, "bad used_bytes");
			goto fail;
		}

		if (vol->skip_check) {
			ubi_err(ubi, "bad skip_check");
			goto fail;
		}
	} else {
		if (vol->used_ebs < 0 || vol->used_ebs > vol->reserved_pebs) {
			ubi_err(ubi, "bad used_ebs");
			goto fail;
		}
		if (vol->last_eb_bytes < 0 ||
		    vol->last_eb_bytes > vol->usable_leb_size) {
			ubi_err(ubi, "bad last_eb_bytes");
			goto fail;
		}
		if (vol->used_bytes < 0 || vol->used_bytes > n ||
		    vol->used_bytes < n - vol->usable_leb_size) {
			ubi_err(ubi, "bad used_bytes");
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
	    name_len != vol->name_len || strncmp(name, vol->name, name_len)) {
		ubi_err(ubi, "volume info is different");
		goto fail;
	}

	spin_unlock(&ubi->volumes_lock);
	return 0;

fail:
	ubi_err(ubi, "self-check failed for volume %d", vol_id);
	if (vol)
		ubi_dump_vol_info(vol);
	ubi_dump_vtbl_record(&ubi->vtbl[vol_id], vol_id);
	dump_stack();
	spin_unlock(&ubi->volumes_lock);
	return -EINVAL;
}

/**
 * self_check_volumes - check information about all volumes.
 * @ubi: UBI device description object
 *
 * Returns zero if volumes are all right and a negative error code if not.
 */
static int self_check_volumes(struct ubi_device *ubi)
{
	int i, err = 0;

	if (!ubi_dbg_chk_gen(ubi))
		return 0;

	for (i = 0; i < ubi->vtbl_slots; i++) {
		err = self_check_volume(ubi, i);
		if (err)
			break;
	}

	return err;
}
