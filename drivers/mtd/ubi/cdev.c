// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file includes implementation of UBI character device operations.
 *
 * There are two kinds of character devices in UBI: UBI character devices and
 * UBI volume character devices. UBI character devices allow users to
 * manipulate whole volumes: create, remove, and re-size them. Volume character
 * devices provide volume I/O capabilities.
 *
 * Major and minor numbers are assigned dynamically to both UBI and volume
 * character devices.
 *
 * Well, there is the third kind of character devices - the UBI control
 * character device, which allows to manipulate by UBI devices - create and
 * delete them. In other words, it is used for attaching and detaching MTD
 * devices.
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/capability.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/math64.h>
#include <mtd/ubi-user.h>
#include "ubi.h"

/**
 * get_exclusive - get exclusive access to an UBI volume.
 * @desc: volume descriptor
 *
 * This function changes UBI volume open mode to "exclusive". Returns previous
 * mode value (positive integer) in case of success and a negative error code
 * in case of failure.
 */
static int get_exclusive(struct ubi_volume_desc *desc)
{
	int users, err;
	struct ubi_volume *vol = desc->vol;

	spin_lock(&vol->ubi->volumes_lock);
	users = vol->readers + vol->writers + vol->exclusive + vol->metaonly;
	ubi_assert(users > 0);
	if (users > 1) {
		ubi_err(vol->ubi, "%d users for volume %d", users, vol->vol_id);
		err = -EBUSY;
	} else {
		vol->readers = vol->writers = vol->metaonly = 0;
		vol->exclusive = 1;
		err = desc->mode;
		desc->mode = UBI_EXCLUSIVE;
	}
	spin_unlock(&vol->ubi->volumes_lock);

	return err;
}

/**
 * revoke_exclusive - revoke exclusive mode.
 * @desc: volume descriptor
 * @mode: new mode to switch to
 */
static void revoke_exclusive(struct ubi_volume_desc *desc, int mode)
{
	struct ubi_volume *vol = desc->vol;

	spin_lock(&vol->ubi->volumes_lock);
	ubi_assert(vol->readers == 0 && vol->writers == 0 && vol->metaonly == 0);
	ubi_assert(vol->exclusive == 1 && desc->mode == UBI_EXCLUSIVE);
	vol->exclusive = 0;
	if (mode == UBI_READONLY)
		vol->readers = 1;
	else if (mode == UBI_READWRITE)
		vol->writers = 1;
	else if (mode == UBI_METAONLY)
		vol->metaonly = 1;
	else
		vol->exclusive = 1;
	spin_unlock(&vol->ubi->volumes_lock);

	desc->mode = mode;
}

static int vol_cdev_open(struct inode *inode, struct file *file)
{
	struct ubi_volume_desc *desc;
	int vol_id = iminor(inode) - 1, mode, ubi_num;

	ubi_num = ubi_major2num(imajor(inode));
	if (ubi_num < 0)
		return ubi_num;

	if (file->f_mode & FMODE_WRITE)
		mode = UBI_READWRITE;
	else
		mode = UBI_READONLY;

	dbg_gen("open device %d, volume %d, mode %d",
		ubi_num, vol_id, mode);

	desc = ubi_open_volume(ubi_num, vol_id, mode);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	file->private_data = desc;
	return 0;
}

static int vol_cdev_release(struct inode *inode, struct file *file)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;

	dbg_gen("release device %d, volume %d, mode %d",
		vol->ubi->ubi_num, vol->vol_id, desc->mode);

	if (vol->updating) {
		ubi_warn(vol->ubi, "update of volume %d not finished, volume is damaged",
			 vol->vol_id);
		ubi_assert(!vol->changing_leb);
		vol->updating = 0;
		vfree(vol->upd_buf);
	} else if (vol->changing_leb) {
		dbg_gen("only %lld of %lld bytes received for atomic LEB change for volume %d:%d, cancel",
			vol->upd_received, vol->upd_bytes, vol->ubi->ubi_num,
			vol->vol_id);
		vol->changing_leb = 0;
		vfree(vol->upd_buf);
	}

	ubi_close_volume(desc);
	return 0;
}

static loff_t vol_cdev_llseek(struct file *file, loff_t offset, int origin)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;

	if (vol->updating) {
		/* Update is in progress, seeking is prohibited */
		ubi_err(vol->ubi, "updating");
		return -EBUSY;
	}

	return fixed_size_llseek(file, offset, origin, vol->used_bytes);
}

static int vol_cdev_fsync(struct file *file, loff_t start, loff_t end,
			  int datasync)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_device *ubi = desc->vol->ubi;
	struct inode *inode = file_inode(file);
	int err;
	inode_lock(inode);
	err = ubi_sync(ubi->ubi_num);
	inode_unlock(inode);
	return err;
}


static ssize_t vol_cdev_read(struct file *file, __user char *buf, size_t count,
			     loff_t *offp)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int err, lnum, off, len,  tbuf_size;
	size_t count_save = count;
	void *tbuf;

	dbg_gen("read %zd bytes from offset %lld of volume %d",
		count, *offp, vol->vol_id);

	if (vol->updating) {
		ubi_err(vol->ubi, "updating");
		return -EBUSY;
	}
	if (vol->upd_marker) {
		ubi_err(vol->ubi, "damaged volume, update marker is set");
		return -EBADF;
	}
	if (*offp == vol->used_bytes || count == 0)
		return 0;

	if (vol->corrupted)
		dbg_gen("read from corrupted volume %d", vol->vol_id);

	if (*offp + count > vol->used_bytes)
		count_save = count = vol->used_bytes - *offp;

	tbuf_size = vol->usable_leb_size;
	if (count < tbuf_size)
		tbuf_size = ALIGN(count, ubi->min_io_size);
	tbuf = vmalloc(tbuf_size);
	if (!tbuf)
		return -ENOMEM;

	len = count > tbuf_size ? tbuf_size : count;
	lnum = div_u64_rem(*offp, vol->usable_leb_size, &off);

	do {
		cond_resched();

		if (off + len >= vol->usable_leb_size)
			len = vol->usable_leb_size - off;

		err = ubi_eba_read_leb(ubi, vol, lnum, tbuf, off, len, 0);
		if (err)
			break;

		off += len;
		if (off == vol->usable_leb_size) {
			lnum += 1;
			off -= vol->usable_leb_size;
		}

		count -= len;
		*offp += len;

		err = copy_to_user(buf, tbuf, len);
		if (err) {
			err = -EFAULT;
			break;
		}

		buf += len;
		len = count > tbuf_size ? tbuf_size : count;
	} while (count);

	vfree(tbuf);
	return err ? err : count_save - count;
}

/*
 * This function allows to directly write to dynamic UBI volumes, without
 * issuing the volume update operation.
 */
static ssize_t vol_cdev_direct_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *offp)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int lnum, off, len, tbuf_size, err = 0;
	size_t count_save = count;
	char *tbuf;

	if (!vol->direct_writes)
		return -EPERM;

	dbg_gen("requested: write %zd bytes to offset %lld of volume %u",
		count, *offp, vol->vol_id);

	if (vol->vol_type == UBI_STATIC_VOLUME)
		return -EROFS;

	lnum = div_u64_rem(*offp, vol->usable_leb_size, &off);
	if (off & (ubi->min_io_size - 1)) {
		ubi_err(ubi, "unaligned position");
		return -EINVAL;
	}

	if (*offp + count > vol->used_bytes)
		count_save = count = vol->used_bytes - *offp;

	/* We can write only in fractions of the minimum I/O unit */
	if (count & (ubi->min_io_size - 1)) {
		ubi_err(ubi, "unaligned write length");
		return -EINVAL;
	}

	tbuf_size = vol->usable_leb_size;
	if (count < tbuf_size)
		tbuf_size = ALIGN(count, ubi->min_io_size);
	tbuf = vmalloc(tbuf_size);
	if (!tbuf)
		return -ENOMEM;

	len = count > tbuf_size ? tbuf_size : count;

	while (count) {
		cond_resched();

		if (off + len >= vol->usable_leb_size)
			len = vol->usable_leb_size - off;

		err = copy_from_user(tbuf, buf, len);
		if (err) {
			err = -EFAULT;
			break;
		}

		err = ubi_eba_write_leb(ubi, vol, lnum, tbuf, off, len);
		if (err)
			break;

		off += len;
		if (off == vol->usable_leb_size) {
			lnum += 1;
			off -= vol->usable_leb_size;
		}

		count -= len;
		*offp += len;
		buf += len;
		len = count > tbuf_size ? tbuf_size : count;
	}

	vfree(tbuf);
	return err ? err : count_save - count;
}

static ssize_t vol_cdev_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *offp)
{
	int err = 0;
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;

	if (!vol->updating && !vol->changing_leb)
		return vol_cdev_direct_write(file, buf, count, offp);

	if (vol->updating)
		err = ubi_more_update_data(ubi, vol, buf, count);
	else
		err = ubi_more_leb_change_data(ubi, vol, buf, count);

	if (err < 0) {
		ubi_err(ubi, "cannot accept more %zd bytes of data, error %d",
			count, err);
		return err;
	}

	if (err) {
		/*
		 * The operation is finished, @err contains number of actually
		 * written bytes.
		 */
		count = err;

		if (vol->changing_leb) {
			revoke_exclusive(desc, UBI_READWRITE);
			return count;
		}

		/*
		 * We voluntarily do not take into account the skip_check flag
		 * as we want to make sure what we wrote was correctly written.
		 */
		err = ubi_check_volume(ubi, vol->vol_id);
		if (err < 0)
			return err;

		if (err) {
			ubi_warn(ubi, "volume %d on UBI device %d is corrupted",
				 vol->vol_id, ubi->ubi_num);
			vol->corrupted = 1;
		}
		vol->checked = 1;
		ubi_volume_notify(ubi, vol, UBI_VOLUME_UPDATED);
		revoke_exclusive(desc, UBI_READWRITE);
	}

	return count;
}

static long vol_cdev_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	int err = 0;
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	/* Volume update command */
	case UBI_IOCVOLUP:
	{
		int64_t bytes, rsvd_bytes;

		if (!capable(CAP_SYS_RESOURCE)) {
			err = -EPERM;
			break;
		}

		err = copy_from_user(&bytes, argp, sizeof(int64_t));
		if (err) {
			err = -EFAULT;
			break;
		}

		if (desc->mode == UBI_READONLY) {
			err = -EROFS;
			break;
		}

		rsvd_bytes = (long long)vol->reserved_pebs *
					vol->usable_leb_size;
		if (bytes < 0 || bytes > rsvd_bytes) {
			err = -EINVAL;
			break;
		}

		err = get_exclusive(desc);
		if (err < 0)
			break;

		err = ubi_start_update(ubi, vol, bytes);
		if (bytes == 0) {
			ubi_volume_notify(ubi, vol, UBI_VOLUME_UPDATED);
			revoke_exclusive(desc, UBI_READWRITE);
		}
		break;
	}

	/* Atomic logical eraseblock change command */
	case UBI_IOCEBCH:
	{
		struct ubi_leb_change_req req;

		err = copy_from_user(&req, argp,
				     sizeof(struct ubi_leb_change_req));
		if (err) {
			err = -EFAULT;
			break;
		}

		if (desc->mode == UBI_READONLY ||
		    vol->vol_type == UBI_STATIC_VOLUME) {
			err = -EROFS;
			break;
		}

		/* Validate the request */
		err = -EINVAL;
		if (!ubi_leb_valid(vol, req.lnum) ||
		    req.bytes < 0 || req.bytes > vol->usable_leb_size)
			break;

		err = get_exclusive(desc);
		if (err < 0)
			break;

		err = ubi_start_leb_change(ubi, vol, &req);
		if (req.bytes == 0)
			revoke_exclusive(desc, UBI_READWRITE);
		break;
	}

	/* Logical eraseblock erasure command */
	case UBI_IOCEBER:
	{
		int32_t lnum;

		err = get_user(lnum, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}

		if (desc->mode == UBI_READONLY ||
		    vol->vol_type == UBI_STATIC_VOLUME) {
			err = -EROFS;
			break;
		}

		if (!ubi_leb_valid(vol, lnum)) {
			err = -EINVAL;
			break;
		}

		dbg_gen("erase LEB %d:%d", vol->vol_id, lnum);
		err = ubi_eba_unmap_leb(ubi, vol, lnum);
		if (err)
			break;

		err = ubi_wl_flush(ubi, UBI_ALL, UBI_ALL);
		break;
	}

	/* Logical eraseblock map command */
	case UBI_IOCEBMAP:
	{
		struct ubi_map_req req;

		err = copy_from_user(&req, argp, sizeof(struct ubi_map_req));
		if (err) {
			err = -EFAULT;
			break;
		}
		err = ubi_leb_map(desc, req.lnum);
		break;
	}

	/* Logical eraseblock un-map command */
	case UBI_IOCEBUNMAP:
	{
		int32_t lnum;

		err = get_user(lnum, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}
		err = ubi_leb_unmap(desc, lnum);
		break;
	}

	/* Check if logical eraseblock is mapped command */
	case UBI_IOCEBISMAP:
	{
		int32_t lnum;

		err = get_user(lnum, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}
		err = ubi_is_mapped(desc, lnum);
		break;
	}

	/* Set volume property command */
	case UBI_IOCSETVOLPROP:
	{
		struct ubi_set_vol_prop_req req;

		err = copy_from_user(&req, argp,
				     sizeof(struct ubi_set_vol_prop_req));
		if (err) {
			err = -EFAULT;
			break;
		}
		switch (req.property) {
		case UBI_VOL_PROP_DIRECT_WRITE:
			mutex_lock(&ubi->device_mutex);
			desc->vol->direct_writes = !!req.value;
			mutex_unlock(&ubi->device_mutex);
			break;
		default:
			err = -EINVAL;
			break;
		}
		break;
	}

	/* Create a R/O block device on top of the UBI volume */
	case UBI_IOCVOLCRBLK:
	{
		struct ubi_volume_info vi;

		ubi_get_volume_info(desc, &vi);
		err = ubiblock_create(&vi);
		break;
	}

	/* Remove the R/O block device */
	case UBI_IOCVOLRMBLK:
	{
		struct ubi_volume_info vi;

		ubi_get_volume_info(desc, &vi);
		err = ubiblock_remove(&vi);
		break;
	}

	default:
		err = -ENOTTY;
		break;
	}
	return err;
}

/**
 * verify_mkvol_req - verify volume creation request.
 * @ubi: UBI device description object
 * @req: the request to check
 *
 * This function zero if the request is correct, and %-EINVAL if not.
 */
static int verify_mkvol_req(const struct ubi_device *ubi,
			    const struct ubi_mkvol_req *req)
{
	int n, err = -EINVAL;

	if (req->bytes < 0 || req->alignment < 0 || req->vol_type < 0 ||
	    req->name_len < 0)
		goto bad;

	if ((req->vol_id < 0 || req->vol_id >= ubi->vtbl_slots) &&
	    req->vol_id != UBI_VOL_NUM_AUTO)
		goto bad;

	if (req->alignment == 0)
		goto bad;

	if (req->bytes == 0)
		goto bad;

	if (req->vol_type != UBI_DYNAMIC_VOLUME &&
	    req->vol_type != UBI_STATIC_VOLUME)
		goto bad;

	if (req->flags & ~UBI_VOL_VALID_FLGS)
		goto bad;

	if (req->flags & UBI_VOL_SKIP_CRC_CHECK_FLG &&
	    req->vol_type != UBI_STATIC_VOLUME)
		goto bad;

	if (req->alignment > ubi->leb_size)
		goto bad;

	n = req->alignment & (ubi->min_io_size - 1);
	if (req->alignment != 1 && n)
		goto bad;

	if (!req->name[0] || !req->name_len)
		goto bad;

	if (req->name_len > UBI_VOL_NAME_MAX) {
		err = -ENAMETOOLONG;
		goto bad;
	}

	n = strnlen(req->name, req->name_len + 1);
	if (n != req->name_len)
		goto bad;

	return 0;

bad:
	ubi_err(ubi, "bad volume creation request");
	ubi_dump_mkvol_req(req);
	return err;
}

/**
 * verify_rsvol_req - verify volume re-size request.
 * @ubi: UBI device description object
 * @req: the request to check
 *
 * This function returns zero if the request is correct, and %-EINVAL if not.
 */
static int verify_rsvol_req(const struct ubi_device *ubi,
			    const struct ubi_rsvol_req *req)
{
	if (req->bytes <= 0)
		return -EINVAL;

	if (req->vol_id < 0 || req->vol_id >= ubi->vtbl_slots)
		return -EINVAL;

	return 0;
}

/**
 * rename_volumes - rename UBI volumes.
 * @ubi: UBI device description object
 * @req: volumes re-name request
 *
 * This is a helper function for the volume re-name IOCTL which validates the
 * the request, opens the volume and calls corresponding volumes management
 * function. Returns zero in case of success and a negative error code in case
 * of failure.
 */
static int rename_volumes(struct ubi_device *ubi,
			  struct ubi_rnvol_req *req)
{
	int i, n, err;
	struct list_head rename_list;
	struct ubi_rename_entry *re, *re1;

	if (req->count < 0 || req->count > UBI_MAX_RNVOL)
		return -EINVAL;

	if (req->count == 0)
		return 0;

	/* Validate volume IDs and names in the request */
	for (i = 0; i < req->count; i++) {
		if (req->ents[i].vol_id < 0 ||
		    req->ents[i].vol_id >= ubi->vtbl_slots)
			return -EINVAL;
		if (req->ents[i].name_len < 0)
			return -EINVAL;
		if (req->ents[i].name_len > UBI_VOL_NAME_MAX)
			return -ENAMETOOLONG;
		req->ents[i].name[req->ents[i].name_len] = '\0';
		n = strlen(req->ents[i].name);
		if (n != req->ents[i].name_len)
			return -EINVAL;
	}

	/* Make sure volume IDs and names are unique */
	for (i = 0; i < req->count - 1; i++) {
		for (n = i + 1; n < req->count; n++) {
			if (req->ents[i].vol_id == req->ents[n].vol_id) {
				ubi_err(ubi, "duplicated volume id %d",
					req->ents[i].vol_id);
				return -EINVAL;
			}
			if (!strcmp(req->ents[i].name, req->ents[n].name)) {
				ubi_err(ubi, "duplicated volume name \"%s\"",
					req->ents[i].name);
				return -EINVAL;
			}
		}
	}

	/* Create the re-name list */
	INIT_LIST_HEAD(&rename_list);
	for (i = 0; i < req->count; i++) {
		int vol_id = req->ents[i].vol_id;
		int name_len = req->ents[i].name_len;
		const char *name = req->ents[i].name;

		re = kzalloc(sizeof(struct ubi_rename_entry), GFP_KERNEL);
		if (!re) {
			err = -ENOMEM;
			goto out_free;
		}

		re->desc = ubi_open_volume(ubi->ubi_num, vol_id, UBI_METAONLY);
		if (IS_ERR(re->desc)) {
			err = PTR_ERR(re->desc);
			ubi_err(ubi, "cannot open volume %d, error %d",
				vol_id, err);
			kfree(re);
			goto out_free;
		}

		/* Skip this re-naming if the name does not really change */
		if (re->desc->vol->name_len == name_len &&
		    !memcmp(re->desc->vol->name, name, name_len)) {
			ubi_close_volume(re->desc);
			kfree(re);
			continue;
		}

		re->new_name_len = name_len;
		memcpy(re->new_name, name, name_len);
		list_add_tail(&re->list, &rename_list);
		dbg_gen("will rename volume %d from \"%s\" to \"%s\"",
			vol_id, re->desc->vol->name, name);
	}

	if (list_empty(&rename_list))
		return 0;

	/* Find out the volumes which have to be removed */
	list_for_each_entry(re, &rename_list, list) {
		struct ubi_volume_desc *desc;
		int no_remove_needed = 0;

		/*
		 * Volume @re->vol_id is going to be re-named to
		 * @re->new_name, while its current name is @name. If a volume
		 * with name @re->new_name currently exists, it has to be
		 * removed, unless it is also re-named in the request (@req).
		 */
		list_for_each_entry(re1, &rename_list, list) {
			if (re->new_name_len == re1->desc->vol->name_len &&
			    !memcmp(re->new_name, re1->desc->vol->name,
				    re1->desc->vol->name_len)) {
				no_remove_needed = 1;
				break;
			}
		}

		if (no_remove_needed)
			continue;

		/*
		 * It seems we need to remove volume with name @re->new_name,
		 * if it exists.
		 */
		desc = ubi_open_volume_nm(ubi->ubi_num, re->new_name,
					  UBI_EXCLUSIVE);
		if (IS_ERR(desc)) {
			err = PTR_ERR(desc);
			if (err == -ENODEV)
				/* Re-naming into a non-existing volume name */
				continue;

			/* The volume exists but busy, or an error occurred */
			ubi_err(ubi, "cannot open volume \"%s\", error %d",
				re->new_name, err);
			goto out_free;
		}

		re1 = kzalloc(sizeof(struct ubi_rename_entry), GFP_KERNEL);
		if (!re1) {
			err = -ENOMEM;
			ubi_close_volume(desc);
			goto out_free;
		}

		re1->remove = 1;
		re1->desc = desc;
		list_add(&re1->list, &rename_list);
		dbg_gen("will remove volume %d, name \"%s\"",
			re1->desc->vol->vol_id, re1->desc->vol->name);
	}

	mutex_lock(&ubi->device_mutex);
	err = ubi_rename_volumes(ubi, &rename_list);
	mutex_unlock(&ubi->device_mutex);

out_free:
	list_for_each_entry_safe(re, re1, &rename_list, list) {
		ubi_close_volume(re->desc);
		list_del(&re->list);
		kfree(re);
	}
	return err;
}

static long ubi_cdev_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	int err = 0;
	struct ubi_device *ubi;
	struct ubi_volume_desc *desc;
	void __user *argp = (void __user *)arg;

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	ubi = ubi_get_by_major(imajor(file->f_mapping->host));
	if (!ubi)
		return -ENODEV;

	switch (cmd) {
	/* Create volume command */
	case UBI_IOCMKVOL:
	{
		struct ubi_mkvol_req req;

		dbg_gen("create volume");
		err = copy_from_user(&req, argp, sizeof(struct ubi_mkvol_req));
		if (err) {
			err = -EFAULT;
			break;
		}

		err = verify_mkvol_req(ubi, &req);
		if (err)
			break;

		mutex_lock(&ubi->device_mutex);
		err = ubi_create_volume(ubi, &req);
		mutex_unlock(&ubi->device_mutex);
		if (err)
			break;

		err = put_user(req.vol_id, (__user int32_t *)argp);
		if (err)
			err = -EFAULT;

		break;
	}

	/* Remove volume command */
	case UBI_IOCRMVOL:
	{
		int vol_id;

		dbg_gen("remove volume");
		err = get_user(vol_id, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}

		desc = ubi_open_volume(ubi->ubi_num, vol_id, UBI_EXCLUSIVE);
		if (IS_ERR(desc)) {
			err = PTR_ERR(desc);
			break;
		}

		mutex_lock(&ubi->device_mutex);
		err = ubi_remove_volume(desc, 0);
		mutex_unlock(&ubi->device_mutex);

		/*
		 * The volume is deleted (unless an error occurred), and the
		 * 'struct ubi_volume' object will be freed when
		 * 'ubi_close_volume()' will call 'put_device()'.
		 */
		ubi_close_volume(desc);
		break;
	}

	/* Re-size volume command */
	case UBI_IOCRSVOL:
	{
		int pebs;
		struct ubi_rsvol_req req;

		dbg_gen("re-size volume");
		err = copy_from_user(&req, argp, sizeof(struct ubi_rsvol_req));
		if (err) {
			err = -EFAULT;
			break;
		}

		err = verify_rsvol_req(ubi, &req);
		if (err)
			break;

		desc = ubi_open_volume(ubi->ubi_num, req.vol_id, UBI_EXCLUSIVE);
		if (IS_ERR(desc)) {
			err = PTR_ERR(desc);
			break;
		}

		pebs = div_u64(req.bytes + desc->vol->usable_leb_size - 1,
			       desc->vol->usable_leb_size);

		mutex_lock(&ubi->device_mutex);
		err = ubi_resize_volume(desc, pebs);
		mutex_unlock(&ubi->device_mutex);
		ubi_close_volume(desc);
		break;
	}

	/* Re-name volumes command */
	case UBI_IOCRNVOL:
	{
		struct ubi_rnvol_req *req;

		dbg_gen("re-name volumes");
		req = kmalloc(sizeof(struct ubi_rnvol_req), GFP_KERNEL);
		if (!req) {
			err = -ENOMEM;
			break;
		}

		err = copy_from_user(req, argp, sizeof(struct ubi_rnvol_req));
		if (err) {
			err = -EFAULT;
			kfree(req);
			break;
		}

		err = rename_volumes(ubi, req);
		kfree(req);
		break;
	}

	/* Check a specific PEB for bitflips and scrub it if needed */
	case UBI_IOCRPEB:
	{
		int pnum;

		err = get_user(pnum, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}

		err = ubi_bitflip_check(ubi, pnum, 0);
		break;
	}

	/* Force scrubbing for a specific PEB */
	case UBI_IOCSPEB:
	{
		int pnum;

		err = get_user(pnum, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}

		err = ubi_bitflip_check(ubi, pnum, 1);
		break;
	}

	default:
		err = -ENOTTY;
		break;
	}

	ubi_put_device(ubi);
	return err;
}

static long ctrl_cdev_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	int err = 0;
	void __user *argp = (void __user *)arg;

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	switch (cmd) {
	/* Attach an MTD device command */
	case UBI_IOCATT:
	{
		struct ubi_attach_req req;
		struct mtd_info *mtd;

		dbg_gen("attach MTD device");
		err = copy_from_user(&req, argp, sizeof(struct ubi_attach_req));
		if (err) {
			err = -EFAULT;
			break;
		}

		if (req.mtd_num < 0 ||
		    (req.ubi_num < 0 && req.ubi_num != UBI_DEV_NUM_AUTO)) {
			err = -EINVAL;
			break;
		}

		mtd = get_mtd_device(NULL, req.mtd_num);
		if (IS_ERR(mtd)) {
			err = PTR_ERR(mtd);
			break;
		}

		/*
		 * Note, further request verification is done by
		 * 'ubi_attach_mtd_dev()'.
		 */
		mutex_lock(&ubi_devices_mutex);
		err = ubi_attach_mtd_dev(mtd, req.ubi_num, req.vid_hdr_offset,
					 req.max_beb_per1024);
		mutex_unlock(&ubi_devices_mutex);
		if (err < 0)
			put_mtd_device(mtd);
		else
			/* @err contains UBI device number */
			err = put_user(err, (__user int32_t *)argp);

		break;
	}

	/* Detach an MTD device command */
	case UBI_IOCDET:
	{
		int ubi_num;

		dbg_gen("detach MTD device");
		err = get_user(ubi_num, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}

		mutex_lock(&ubi_devices_mutex);
		err = ubi_detach_mtd_dev(ubi_num, 0);
		mutex_unlock(&ubi_devices_mutex);
		break;
	}

	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

/* UBI volume character device operations */
const struct file_operations ubi_vol_cdev_operations = {
	.owner          = THIS_MODULE,
	.open           = vol_cdev_open,
	.release        = vol_cdev_release,
	.llseek         = vol_cdev_llseek,
	.read           = vol_cdev_read,
	.write          = vol_cdev_write,
	.fsync		= vol_cdev_fsync,
	.unlocked_ioctl = vol_cdev_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

/* UBI character device operations */
const struct file_operations ubi_cdev_operations = {
	.owner          = THIS_MODULE,
	.llseek         = no_llseek,
	.unlocked_ioctl = ubi_cdev_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
};

/* UBI control character device operations */
const struct file_operations ubi_ctrl_cdev_operations = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = ctrl_cdev_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
	.llseek		= no_llseek,
};
