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
 *
 * Well, there is the third kind of character devices - the UBI control
 * character device, which allows to manipulate by UBI devices - create and
 * delete them. In other words, it is used for attaching and detaching MTD
 * devices.
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/ioctl.h>
#include <linux/capability.h>
#include <mtd/ubi-user.h>
#include <asm/uaccess.h>
#include <asm/div64.h>
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
	int vol_id = iminor(inode) - 1, mode, ubi_num;

	ubi_num = ubi_major2num(imajor(inode));
	if (ubi_num < 0)
		return ubi_num;

	if (file->f_mode & FMODE_WRITE)
		mode = UBI_READWRITE;
	else
		mode = UBI_READONLY;

	dbg_msg("open volume %d, mode %d", vol_id, mode);

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

	dbg_msg("release volume %d, mode %d", vol->vol_id, desc->mode);

	if (vol->updating) {
		ubi_warn("update of volume %d not finished, volume is damaged",
			 vol->vol_id);
		ubi_assert(!vol->changing_leb);
		vol->updating = 0;
		vfree(vol->upd_buf);
	} else if (vol->changing_leb) {
		dbg_msg("only %lld of %lld bytes received for atomic LEB change"
			" for volume %d:%d, cancel", vol->upd_received,
			vol->upd_bytes, vol->ubi->ubi_num, vol->vol_id);
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
	int err, lnum, off, len,  tbuf_size;
	size_t count_save = count;
	void *tbuf;
	uint64_t tmp;

	dbg_msg("read %zd bytes from offset %lld of volume %d",
		count, *offp, vol->vol_id);

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
		dbg_msg("read from corrupted volume %d", vol->vol_id);

	if (*offp + count > vol->used_bytes)
		count_save = count = vol->used_bytes - *offp;

	tbuf_size = vol->usable_leb_size;
	if (count < tbuf_size)
		tbuf_size = ALIGN(count, ubi->min_io_size);
	tbuf = vmalloc(tbuf_size);
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
	int lnum, off, len, tbuf_size, err = 0;
	size_t count_save = count;
	char *tbuf;
	uint64_t tmp;

	dbg_msg("requested: write %zd bytes to offset %lld of volume %u",
		count, *offp, vol->vol_id);

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

		err = ubi_eba_write_leb(ubi, vol, lnum, tbuf, off, len,
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

	vfree(tbuf);
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

	if (!vol->updating && !vol->changing_leb)
		return vol_cdev_direct_write(file, buf, count, offp);

	if (vol->updating)
		err = ubi_more_update_data(ubi, vol, buf, count);
	else
		err = ubi_more_leb_change_data(ubi, vol, buf, count);

	if (err < 0) {
		ubi_err("cannot accept more %zd bytes of data, error %d",
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

		err = ubi_check_volume(ubi, vol->vol_id);
		if (err < 0)
			return err;

		if (err) {
			ubi_warn("volume %d on UBI device %d is corrupted",
				 vol->vol_id, ubi->ubi_num);
			vol->corrupted = 1;
		}
		vol->checked = 1;
		ubi_gluebi_updated(vol);
		revoke_exclusive(desc, UBI_READWRITE);
	}

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

		err = ubi_start_update(ubi, vol, bytes);
		if (bytes == 0)
			revoke_exclusive(desc, UBI_READWRITE);
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
		if (req.lnum < 0 || req.lnum >= vol->reserved_pebs ||
		    req.bytes < 0 || req.lnum >= vol->usable_leb_size)
			break;
		if (req.dtype != UBI_LONGTERM && req.dtype != UBI_SHORTTERM &&
		    req.dtype != UBI_UNKNOWN)
			break;

		err = get_exclusive(desc);
		if (err < 0)
			break;

		err = ubi_start_leb_change(ubi, vol, &req);
		if (req.bytes == 0)
			revoke_exclusive(desc, UBI_READWRITE);
		break;
	}

#ifdef CONFIG_MTD_UBI_DEBUG_USERSPACE_IO
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

		if (lnum < 0 || lnum >= vol->reserved_pebs) {
			err = -EINVAL;
			break;
		}

		dbg_msg("erase LEB %d:%d", vol->vol_id, lnum);
		err = ubi_eba_unmap_leb(ubi, vol, lnum);
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

	if (!capable(CAP_SYS_RESOURCE))
		return -EPERM;

	ubi = ubi_get_by_major(imajor(inode));
	if (!ubi)
		return -ENODEV;

	switch (cmd) {
	/* Create volume command */
	case UBI_IOCMKVOL:
	{
		struct ubi_mkvol_req req;

		dbg_msg("create volume");
		err = copy_from_user(&req, argp, sizeof(struct ubi_mkvol_req));
		if (err) {
			err = -EFAULT;
			break;
		}

		err = verify_mkvol_req(ubi, &req);
		if (err)
			break;

		req.name[req.name_len] = '\0';

		mutex_lock(&ubi->volumes_mutex);
		err = ubi_create_volume(ubi, &req);
		mutex_unlock(&ubi->volumes_mutex);
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

		dbg_msg("remove volume");
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

		mutex_lock(&ubi->volumes_mutex);
		err = ubi_remove_volume(desc);
		mutex_unlock(&ubi->volumes_mutex);

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
		uint64_t tmp;
		struct ubi_rsvol_req req;

		dbg_msg("re-size volume");
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

		tmp = req.bytes;
		pebs = !!do_div(tmp, desc->vol->usable_leb_size);
		pebs += tmp;

		mutex_lock(&ubi->volumes_mutex);
		err = ubi_resize_volume(desc, pebs);
		mutex_unlock(&ubi->volumes_mutex);
		ubi_close_volume(desc);
		break;
	}

	default:
		err = -ENOTTY;
		break;
	}

	ubi_put_device(ubi);
	return err;
}

static int ctrl_cdev_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
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

		dbg_msg("attach MTD device");
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
		err = ubi_attach_mtd_dev(mtd, req.ubi_num, req.vid_hdr_offset);
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

		dbg_msg("dettach MTD device");
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

/* UBI control character device operations */
struct file_operations ubi_ctrl_cdev_operations = {
	.ioctl = ctrl_cdev_ioctl,
	.owner = THIS_MODULE,
};

/* UBI character device operations */
struct file_operations ubi_cdev_operations = {
	.owner = THIS_MODULE,
	.ioctl = ubi_cdev_ioctl,
	.llseek = no_llseek,
};

/* UBI volume character device operations */
struct file_operations ubi_vol_cdev_operations = {
	.owner   = THIS_MODULE,
	.open    = vol_cdev_open,
	.release = vol_cdev_release,
	.llseek  = vol_cdev_llseek,
	.read    = vol_cdev_read,
	.write   = vol_cdev_write,
	.ioctl   = vol_cdev_ioctl,
};
