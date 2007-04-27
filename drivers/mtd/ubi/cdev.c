/*
 * Copyright (c) International Business Machines Corp., 2006
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
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/ioctl.h>
#include <linux/capability.h>
#include <mtd/ubi-user.h>
#include <asm/uaccess.h>
#include <asm/div64.h>
#include "ubi.h"

/*
 * Maximum sequence numbers of UBI and volume character device IOCTLs (direct
 * logical eraseblock erase is a debug-only feature).
 */
#define UBI_CDEV_IOC_MAX_SEQ 2
#ifndef CONFIG_MTD_UBI_DEBUG_USERSPACE_IO
#define VOL_CDEV_IOC_MAX_SEQ 1
#else
#define VOL_CDEV_IOC_MAX_SEQ 2
#endif

/**
 * major_to_device - get UBI device object by character device major number.
 * @major: major number
 *
 * This function returns a pointer to the UBI device object.
 */
static struct ubi_device *major_to_device(int major)
{
	int i;

	for (i = 0; i < ubi_devices_cnt; i++)
		if (ubi_devices[i] && ubi_devices[i]->major == major)
			return ubi_devices[i];
	BUG();
}

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
	users = vol->readers + vol->writers + vol->exclusive;
	ubi_assert(users > 0);
	if (users > 1) {
		dbg_err("%d users for volume %d", users, vol->vol_id);
		err = -EBUSY;
	} else {
		vol->readers = vol->writers = 0;
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
	ubi_assert(vol->readers == 0 && vol->writers == 0);
	ubi_assert(vol->exclusive == 1 && desc->mode == UBI_EXCLUSIVE);
	vol->exclusive = 0;
	if (mode == UBI_READONLY)
		vol->readers = 1;
	else if (mode == UBI_READWRITE)
		vol->writers = 1;
	else
		vol->exclusive = 1;
	spin_unlock(&vol->ubi->volumes_lock);

	desc->mode = mode;
}

static int vol_cdev_open(struct inode *inode, struct file *file)
{
	struct ubi_volume_desc *desc;
	const struct ubi_device *ubi = major_to_device(imajor(inode));
	int vol_id = iminor(inode) - 1;
	int mode;

	if (file->f_mode & FMODE_WRITE)
		mode = UBI_READWRITE;
	else
		mode = UBI_READONLY;

	dbg_msg("open volume %d, mode %d", vol_id, mode);

	desc = ubi_open_volume(ubi->ubi_num, vol_id, mode);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	file->private_data = desc;
	return 0;
}

static int vol_cdev_release(struct inode *inode, struct file *file)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;

	dbg_msg("release volume %d, mode %d", vol->vol_id, desc->mode);

	if (vol->updating) {
		ubi_warn("update of volume %d not finished, volume is damaged",
			 vol->vol_id);
		vol->updating = 0;
		kfree(vol->upd_buf);
	}

	ubi_close_volume(desc);
	return 0;
}

static loff_t vol_cdev_llseek(struct file *file, loff_t offset, int origin)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	loff_t new_offset;

	if (vol->updating) {
		 /* Update is in progress, seeking is prohibited */
		dbg_err("updating");
		return -EBUSY;
	}

	switch (origin) {
	case 0: /* SEEK_SET */
		new_offset = offset;
		break;
	case 1: /* SEEK_CUR */
		new_offset = file->f_pos + offset;
		break;
	case 2: /* SEEK_END */
		new_offset = vol->used_bytes + offset;
		break;
	default:
		return -EINVAL;
	}

	if (new_offset < 0 || new_offset > vol->used_bytes) {
		dbg_err("bad seek %lld", new_offset);
		return -EINVAL;
	}

	dbg_msg("seek volume %d, offset %lld, origin %d, new offset %lld",
		vol->vol_id, offset, origin, new_offset);

	file->f_pos = new_offset;
	return new_offset;
}

static ssize_t vol_cdev_read(struct file *file, __user char *buf, size_t count,
			     loff_t *offp)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int err, lnum, off, len,  vol_id = desc->vol->vol_id, tbuf_size;
	size_t count_save = count;
	void *tbuf;
	uint64_t tmp;

	dbg_msg("read %zd bytes from offset %lld of volume %d",
		count, *offp, vol_id);

	if (vol->updating) {
		dbg_err("updating");
		return -EBUSY;
	}
	if (vol->upd_marker) {
		dbg_err("damaged volume, update marker is set");
		return -EBADF;
	}
	if (*offp == vol->used_bytes || count == 0)
		return 0;

	if (vol->corrupted)
		dbg_msg("read from corrupted volume %d", vol_id);

	if (*offp + count > vol->used_bytes)
		count_save = count = vol->used_bytes - *offp;

	tbuf_size = vol->usable_leb_size;
	if (count < tbuf_size)
		tbuf_size = ALIGN(count, ubi->min_io_size);
	tbuf = kmalloc(tbuf_size, GFP_KERNEL);
	if (!tbuf)
		return -ENOMEM;

	len = count > tbuf_size ? tbuf_size : count;

	tmp = *offp;
	off = do_div(tmp, vol->usable_leb_size);
	lnum = tmp;

	do {
		cond_resched();

		if (off + len >= vol->usable_leb_size)
			len = vol->usable_leb_size - off;

		err = ubi_eba_read_leb(ubi, vol_id, lnum, tbuf, off, len, 0);
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

	kfree(tbuf);
	return err ? err : count_save - count;
}

#ifdef CONFIG_MTD_UBI_DEBUG_USERSPACE_IO

/*
 * This function allows to directly write to dynamic UBI volumes, without
 * issuing the volume update operation. Available only as a debugging feature.
 * Very useful for testing UBI.
 */
static ssize_t vol_cdev_direct_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *offp)
{
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int lnum, off, len, tbuf_size, vol_id = vol->vol_id, err = 0;
	size_t count_save = count;
	char *tbuf;
	uint64_t tmp;

	dbg_msg("requested: write %zd bytes to offset %lld of volume %u",
		count, *offp, desc->vol->vol_id);

	if (vol->vol_type == UBI_STATIC_VOLUME)
		return -EROFS;

	tmp = *offp;
	off = do_div(tmp, vol->usable_leb_size);
	lnum = tmp;

	if (off % ubi->min_io_size) {
		dbg_err("unaligned position");
		return -EINVAL;
	}

	if (*offp + count > vol->used_bytes)
		count_save = count = vol->used_bytes - *offp;

	/* We can write only in fractions of the minimum I/O unit */
	if (count % ubi->min_io_size) {
		dbg_err("unaligned write length");
		return -EINVAL;
	}

	tbuf_size = vol->usable_leb_size;
	if (count < tbuf_size)
		tbuf_size = ALIGN(count, ubi->min_io_size);
	tbuf = kmalloc(tbuf_size, GFP_KERNEL);
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

		err = ubi_eba_write_leb(ubi, vol_id, lnum, tbuf, off, len,
					UBI_UNKNOWN);
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

	kfree(tbuf);
	return err ? err : count_save - count;
}

#else
#define vol_cdev_direct_write(file, buf, count, offp) -EPERM
#endif /* CONFIG_MTD_UBI_DEBUG_USERSPACE_IO */

static ssize_t vol_cdev_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *offp)
{
	int err = 0;
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;

	if (!vol->updating)
		return vol_cdev_direct_write(file, buf, count, offp);

	err = ubi_more_update_data(ubi, vol->vol_id, buf, count);
	if (err < 0) {
		ubi_err("cannot write %zd bytes of update data", count);
		return err;
	}

	if (err) {
		/*
		 * Update is finished, @err contains number of actually written
		 * bytes now.
		 */
		count = err;

		err = ubi_check_volume(ubi, vol->vol_id);
		if (err < 0)
			return err;

		if (err) {
			ubi_warn("volume %d on UBI device %d is corrupted",
				 vol->vol_id, ubi->ubi_num);
			vol->corrupted = 1;
		}
		vol->checked = 1;
		revoke_exclusive(desc, UBI_READWRITE);
	}

	*offp += count;
	return count;
}

static int vol_cdev_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct ubi_volume_desc *desc = file->private_data;
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	void __user *argp = (void __user *)arg;

	if (_IOC_NR(cmd) > VOL_CDEV_IOC_MAX_SEQ ||
	    _IOC_TYPE(cmd) != UBI_VOL_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_DIR(cmd) && _IOC_READ)
		err = !access_ok(VERIFY_WRITE, argp, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) && _IOC_WRITE)
		err = !access_ok(VERIFY_READ, argp, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

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

		rsvd_bytes = vol->reserved_pebs * (ubi->leb_size-vol->data_pad);
		if (bytes < 0 || bytes > rsvd_bytes) {
			err = -EINVAL;
			break;
		}

		err = get_exclusive(desc);
		if (err < 0)
			break;

		err = ubi_start_update(ubi, vol->vol_id, bytes);
		if (bytes == 0)
			revoke_exclusive(desc, UBI_READWRITE);

		file->f_pos = 0;
		break;
	}

#ifdef CONFIG_MTD_UBI_DEBUG_USERSPACE_IO
	/* Logical eraseblock erasure command */
	case UBI_IOCEBER:
	{
		int32_t lnum;

		err = __get_user(lnum, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}

		if (desc->mode == UBI_READONLY) {
			err = -EROFS;
			break;
		}

		if (lnum < 0 || lnum >= vol->reserved_pebs) {
			err = -EINVAL;
			break;
		}

		if (vol->vol_type != UBI_DYNAMIC_VOLUME) {
			err = -EROFS;
			break;
		}

		dbg_msg("erase LEB %d:%d", vol->vol_id, lnum);
		err = ubi_eba_unmap_leb(ubi, vol->vol_id, lnum);
		if (err)
			break;

		err = ubi_wl_flush(ubi);
		break;
	}
#endif

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

	if (req->alignment > ubi->leb_size)
		goto bad;

	n = req->alignment % ubi->min_io_size;
	if (req->alignment != 1 && n)
		goto bad;

	if (req->name_len > UBI_VOL_NAME_MAX) {
		err = -ENAMETOOLONG;
		goto bad;
	}

	return 0;

bad:
	dbg_err("bad volume creation request");
	ubi_dbg_dump_mkvol_req(req);
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

static int ubi_cdev_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct ubi_device *ubi;
	struct ubi_volume_desc *desc;
	void __user *argp = (void __user *)arg;

	if (_IOC_NR(cmd) > UBI_CDEV_IOC_MAX_SEQ ||
	    _IOC_TYPE(cmd) != UBI_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_DIR(cmd) && _IOC_READ)
		err = !access_ok(VERIFY_WRITE, argp, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) && _IOC_WRITE)
		err = !access_ok(VERIFY_READ, argp, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	ubi = major_to_device(imajor(inode));
	if (IS_ERR(ubi))
		return PTR_ERR(ubi);

	switch (cmd) {
	/* Create volume command */
	case UBI_IOCMKVOL:
	{
		struct ubi_mkvol_req req;

		dbg_msg("create volume");
		err = __copy_from_user(&req, argp,
				       sizeof(struct ubi_mkvol_req));
		if (err) {
			err = -EFAULT;
			break;
		}

		err = verify_mkvol_req(ubi, &req);
		if (err)
			break;

		req.name[req.name_len] = '\0';

		err = ubi_create_volume(ubi, &req);
		if (err)
			break;

		err = __put_user(req.vol_id, (__user int32_t *)argp);
		if (err)
			err = -EFAULT;

		break;
	}

	/* Remove volume command */
	case UBI_IOCRMVOL:
	{
		int vol_id;

		dbg_msg("remove volume");
		err = __get_user(vol_id, (__user int32_t *)argp);
		if (err) {
			err = -EFAULT;
			break;
		}

		desc = ubi_open_volume(ubi->ubi_num, vol_id, UBI_EXCLUSIVE);
		if (IS_ERR(desc)) {
			err = PTR_ERR(desc);
			break;
		}

		err = ubi_remove_volume(desc);
		if (err)
			ubi_close_volume(desc);

		break;
	}

	/* Re-size volume command */
	case UBI_IOCRSVOL:
	{
		int pebs;
		uint64_t tmp;
		struct ubi_rsvol_req req;

		dbg_msg("re-size volume");
		err = __copy_from_user(&req, argp,
				       sizeof(struct ubi_rsvol_req));
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

		tmp = req.bytes;
		pebs = !!do_div(tmp, desc->vol->usable_leb_size);
		pebs += tmp;

		err = ubi_resize_volume(desc, pebs);
		ubi_close_volume(desc);
		break;
	}

	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

/* UBI character device operations */
struct file_operations ubi_cdev_operations = {
	.owner = THIS_MODULE,
	.ioctl = ubi_cdev_ioctl,
	.llseek = no_llseek
};

/* UBI volume character device operations */
struct file_operations ubi_vol_cdev_operations = {
	.owner   = THIS_MODULE,
	.open    = vol_cdev_open,
	.release = vol_cdev_release,
	.llseek  = vol_cdev_llseek,
	.read    = vol_cdev_read,
	.write   = vol_cdev_write,
	.ioctl   = vol_cdev_ioctl
};
