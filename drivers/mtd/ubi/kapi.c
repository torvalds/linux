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

/* This file mostly implements UBI kernel API functions */

#include <linux/module.h>
#include <linux/err.h>
#include <asm/div64.h>
#include "ubi.h"

/**
 * ubi_get_device_info - get information about UBI device.
 * @ubi_num: UBI device number
 * @di: the information is stored here
 *
 * This function returns %0 in case of success and a %-ENODEV if there is no
 * such UBI device.
 */
int ubi_get_device_info(int ubi_num, struct ubi_device_info *di)
{
	const struct ubi_device *ubi;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	if (ubi_num < 0 || ubi_num >= UBI_MAX_DEVICES ||
	    !ubi_devices[ubi_num]) {
		module_put(THIS_MODULE);
		return -ENODEV;
	}

	ubi = ubi_devices[ubi_num];
	di->ubi_num = ubi->ubi_num;
	di->leb_size = ubi->leb_size;
	di->min_io_size = ubi->min_io_size;
	di->ro_mode = ubi->ro_mode;
	di->cdev = MKDEV(ubi->major, 0);
	module_put(THIS_MODULE);
	return 0;
}
EXPORT_SYMBOL_GPL(ubi_get_device_info);

/**
 * ubi_get_volume_info - get information about UBI volume.
 * @desc: volume descriptor
 * @vi: the information is stored here
 */
void ubi_get_volume_info(struct ubi_volume_desc *desc,
			 struct ubi_volume_info *vi)
{
	const struct ubi_volume *vol = desc->vol;
	const struct ubi_device *ubi = vol->ubi;

	vi->vol_id = vol->vol_id;
	vi->ubi_num = ubi->ubi_num;
	vi->size = vol->reserved_pebs;
	vi->used_bytes = vol->used_bytes;
	vi->vol_type = vol->vol_type;
	vi->corrupted = vol->corrupted;
	vi->upd_marker = vol->upd_marker;
	vi->alignment = vol->alignment;
	vi->usable_leb_size = vol->usable_leb_size;
	vi->name_len = vol->name_len;
	vi->name = vol->name;
	vi->cdev = MKDEV(ubi->major, vi->vol_id + 1);
}
EXPORT_SYMBOL_GPL(ubi_get_volume_info);

/**
 * ubi_open_volume - open UBI volume.
 * @ubi_num: UBI device number
 * @vol_id: volume ID
 * @mode: open mode
 *
 * The @mode parameter specifies if the volume should be opened in read-only
 * mode, read-write mode, or exclusive mode. The exclusive mode guarantees that
 * nobody else will be able to open this volume. UBI allows to have many volume
 * readers and one writer at a time.
 *
 * If a static volume is being opened for the first time since boot, it will be
 * checked by this function, which means it will be fully read and the CRC
 * checksum of each logical eraseblock will be checked.
 *
 * This function returns volume descriptor in case of success and a negative
 * error code in case of failure.
 */
struct ubi_volume_desc *ubi_open_volume(int ubi_num, int vol_id, int mode)
{
	int err;
	struct ubi_volume_desc *desc;
	struct ubi_device *ubi = ubi_devices[ubi_num];
	struct ubi_volume *vol;

	dbg_msg("open device %d volume %d, mode %d", ubi_num, vol_id, mode);

	err = -ENODEV;
	if (!try_module_get(THIS_MODULE))
		return ERR_PTR(err);

	if (ubi_num < 0 || ubi_num >= UBI_MAX_DEVICES || !ubi)
		goto out_put;

	err = -EINVAL;
	if (vol_id < 0 || vol_id >= ubi->vtbl_slots)
		goto out_put;
	if (mode != UBI_READONLY && mode != UBI_READWRITE &&
	    mode != UBI_EXCLUSIVE)
		goto out_put;

	desc = kmalloc(sizeof(struct ubi_volume_desc), GFP_KERNEL);
	if (!desc) {
		err = -ENOMEM;
		goto out_put;
	}

	spin_lock(&ubi->volumes_lock);
	vol = ubi->volumes[vol_id];
	if (!vol) {
		err = -ENODEV;
		goto out_unlock;
	}

	err = -EBUSY;
	switch (mode) {
	case UBI_READONLY:
		if (vol->exclusive)
			goto out_unlock;
		vol->readers += 1;
		break;

	case UBI_READWRITE:
		if (vol->exclusive || vol->writers > 0)
			goto out_unlock;
		vol->writers += 1;
		break;

	case UBI_EXCLUSIVE:
		if (vol->exclusive || vol->writers || vol->readers)
			goto out_unlock;
		vol->exclusive = 1;
		break;
	}
	spin_unlock(&ubi->volumes_lock);

	desc->vol = vol;
	desc->mode = mode;

	/*
	 * To prevent simultaneous checks of the same volume we use @vtbl_mutex,
	 * although it is not the purpose it was introduced for.
	 */
	mutex_lock(&ubi->vtbl_mutex);
	if (!vol->checked) {
		/* This is the first open - check the volume */
		err = ubi_check_volume(ubi, vol_id);
		if (err < 0) {
			mutex_unlock(&ubi->vtbl_mutex);
			ubi_close_volume(desc);
			return ERR_PTR(err);
		}
		if (err == 1) {
			ubi_warn("volume %d on UBI device %d is corrupted",
				 vol_id, ubi->ubi_num);
			vol->corrupted = 1;
		}
		vol->checked = 1;
	}
	mutex_unlock(&ubi->vtbl_mutex);
	return desc;

out_unlock:
	spin_unlock(&ubi->volumes_lock);
	kfree(desc);
out_put:
	module_put(THIS_MODULE);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(ubi_open_volume);

/**
 * ubi_open_volume_nm - open UBI volume by name.
 * @ubi_num: UBI device number
 * @name: volume name
 * @mode: open mode
 *
 * This function is similar to 'ubi_open_volume()', but opens a volume by name.
 */
struct ubi_volume_desc *ubi_open_volume_nm(int ubi_num, const char *name,
					   int mode)
{
	int i, vol_id = -1, len;
	struct ubi_volume_desc *ret;
	struct ubi_device *ubi;

	dbg_msg("open volume %s, mode %d", name, mode);

	if (!name)
		return ERR_PTR(-EINVAL);

	len = strnlen(name, UBI_VOL_NAME_MAX + 1);
	if (len > UBI_VOL_NAME_MAX)
		return ERR_PTR(-EINVAL);

	ret = ERR_PTR(-ENODEV);
	if (!try_module_get(THIS_MODULE))
		return ret;

	if (ubi_num < 0 || ubi_num >= UBI_MAX_DEVICES || !ubi_devices[ubi_num])
		goto out_put;

	ubi = ubi_devices[ubi_num];

	spin_lock(&ubi->volumes_lock);
	/* Walk all volumes of this UBI device */
	for (i = 0; i < ubi->vtbl_slots; i++) {
		struct ubi_volume *vol = ubi->volumes[i];

		if (vol && len == vol->name_len && !strcmp(name, vol->name)) {
			vol_id = i;
			break;
		}
	}
	spin_unlock(&ubi->volumes_lock);

	if (vol_id < 0)
		goto out_put;

	ret = ubi_open_volume(ubi_num, vol_id, mode);

out_put:
	module_put(THIS_MODULE);
	return ret;
}
EXPORT_SYMBOL_GPL(ubi_open_volume_nm);

/**
 * ubi_close_volume - close UBI volume.
 * @desc: volume descriptor
 */
void ubi_close_volume(struct ubi_volume_desc *desc)
{
	struct ubi_volume *vol = desc->vol;

	dbg_msg("close volume %d, mode %d", vol->vol_id, desc->mode);

	spin_lock(&vol->ubi->volumes_lock);
	switch (desc->mode) {
	case UBI_READONLY:
		vol->readers -= 1;
		break;
	case UBI_READWRITE:
		vol->writers -= 1;
		break;
	case UBI_EXCLUSIVE:
		vol->exclusive = 0;
	}
	spin_unlock(&vol->ubi->volumes_lock);

	kfree(desc);
	module_put(THIS_MODULE);
}
EXPORT_SYMBOL_GPL(ubi_close_volume);

/**
 * ubi_leb_read - read data.
 * @desc: volume descriptor
 * @lnum: logical eraseblock number to read from
 * @buf: buffer where to store the read data
 * @offset: offset within the logical eraseblock to read from
 * @len: how many bytes to read
 * @check: whether UBI has to check the read data's CRC or not.
 *
 * This function reads data from offset @offset of logical eraseblock @lnum and
 * stores the data at @buf. When reading from static volumes, @check specifies
 * whether the data has to be checked or not. If yes, the whole logical
 * eraseblock will be read and its CRC checksum will be checked (i.e., the CRC
 * checksum is per-eraseblock). So checking may substantially slow down the
 * read speed. The @check argument is ignored for dynamic volumes.
 *
 * In case of success, this function returns zero. In case of failure, this
 * function returns a negative error code.
 *
 * %-EBADMSG error code is returned:
 * o for both static and dynamic volumes if MTD driver has detected a data
 *   integrity problem (unrecoverable ECC checksum mismatch in case of NAND);
 * o for static volumes in case of data CRC mismatch.
 *
 * If the volume is damaged because of an interrupted update this function just
 * returns immediately with %-EBADF error code.
 */
int ubi_leb_read(struct ubi_volume_desc *desc, int lnum, char *buf, int offset,
		 int len, int check)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int err, vol_id = vol->vol_id;

	dbg_msg("read %d bytes from LEB %d:%d:%d", len, vol_id, lnum, offset);

	if (vol_id < 0 || vol_id >= ubi->vtbl_slots || lnum < 0 ||
	    lnum >= vol->used_ebs || offset < 0 || len < 0 ||
	    offset + len > vol->usable_leb_size)
		return -EINVAL;

	if (vol->vol_type == UBI_STATIC_VOLUME && lnum == vol->used_ebs - 1 &&
	    offset + len > vol->last_eb_bytes)
		return -EINVAL;

	if (vol->upd_marker)
		return -EBADF;
	if (len == 0)
		return 0;

	err = ubi_eba_read_leb(ubi, vol_id, lnum, buf, offset, len, check);
	if (err && err == -EBADMSG && vol->vol_type == UBI_STATIC_VOLUME) {
		ubi_warn("mark volume %d as corrupted", vol_id);
		vol->corrupted = 1;
	}

	return err;
}
EXPORT_SYMBOL_GPL(ubi_leb_read);

/**
 * ubi_leb_write - write data.
 * @desc: volume descriptor
 * @lnum: logical eraseblock number to write to
 * @buf: data to write
 * @offset: offset within the logical eraseblock where to write
 * @len: how many bytes to write
 * @dtype: expected data type
 *
 * This function writes @len bytes of data from @buf to offset @offset of
 * logical eraseblock @lnum. The @dtype argument describes expected lifetime of
 * the data.
 *
 * This function takes care of physical eraseblock write failures. If write to
 * the physical eraseblock write operation fails, the logical eraseblock is
 * re-mapped to another physical eraseblock, the data is recovered, and the
 * write finishes. UBI has a pool of reserved physical eraseblocks for this.
 *
 * If all the data were successfully written, zero is returned. If an error
 * occurred and UBI has not been able to recover from it, this function returns
 * a negative error code. Note, in case of an error, it is possible that
 * something was still written to the flash media, but that may be some
 * garbage.
 *
 * If the volume is damaged because of an interrupted update this function just
 * returns immediately with %-EBADF code.
 */
int ubi_leb_write(struct ubi_volume_desc *desc, int lnum, const void *buf,
		  int offset, int len, int dtype)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int vol_id = vol->vol_id;

	dbg_msg("write %d bytes to LEB %d:%d:%d", len, vol_id, lnum, offset);

	if (vol_id < 0 || vol_id >= ubi->vtbl_slots)
		return -EINVAL;

	if (desc->mode == UBI_READONLY || vol->vol_type == UBI_STATIC_VOLUME)
		return -EROFS;

	if (lnum < 0 || lnum >= vol->reserved_pebs || offset < 0 || len < 0 ||
	    offset + len > vol->usable_leb_size || offset % ubi->min_io_size ||
	    len % ubi->min_io_size)
		return -EINVAL;

	if (dtype != UBI_LONGTERM && dtype != UBI_SHORTTERM &&
	    dtype != UBI_UNKNOWN)
		return -EINVAL;

	if (vol->upd_marker)
		return -EBADF;

	if (len == 0)
		return 0;

	return ubi_eba_write_leb(ubi, vol_id, lnum, buf, offset, len, dtype);
}
EXPORT_SYMBOL_GPL(ubi_leb_write);

/*
 * ubi_leb_change - change logical eraseblock atomically.
 * @desc: volume descriptor
 * @lnum: logical eraseblock number to change
 * @buf: data to write
 * @len: how many bytes to write
 * @dtype: expected data type
 *
 * This function changes the contents of a logical eraseblock atomically. @buf
 * has to contain new logical eraseblock data, and @len - the length of the
 * data, which has to be aligned. The length may be shorter then the logical
 * eraseblock size, ant the logical eraseblock may be appended to more times
 * later on. This function guarantees that in case of an unclean reboot the old
 * contents is preserved. Returns zero in case of success and a negative error
 * code in case of failure.
 */
int ubi_leb_change(struct ubi_volume_desc *desc, int lnum, const void *buf,
		   int len, int dtype)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int vol_id = vol->vol_id;

	dbg_msg("atomically write %d bytes to LEB %d:%d", len, vol_id, lnum);

	if (vol_id < 0 || vol_id >= ubi->vtbl_slots)
		return -EINVAL;

	if (desc->mode == UBI_READONLY || vol->vol_type == UBI_STATIC_VOLUME)
		return -EROFS;

	if (lnum < 0 || lnum >= vol->reserved_pebs || len < 0 ||
	    len > vol->usable_leb_size || len % ubi->min_io_size)
		return -EINVAL;

	if (dtype != UBI_LONGTERM && dtype != UBI_SHORTTERM &&
	    dtype != UBI_UNKNOWN)
		return -EINVAL;

	if (vol->upd_marker)
		return -EBADF;

	if (len == 0)
		return 0;

	return ubi_eba_atomic_leb_change(ubi, vol_id, lnum, buf, len, dtype);
}
EXPORT_SYMBOL_GPL(ubi_leb_change);

/**
 * ubi_leb_erase - erase logical eraseblock.
 * @desc: volume descriptor
 * @lnum: logical eraseblock number
 *
 * This function un-maps logical eraseblock @lnum and synchronously erases the
 * correspondent physical eraseblock. Returns zero in case of success and a
 * negative error code in case of failure.
 *
 * If the volume is damaged because of an interrupted update this function just
 * returns immediately with %-EBADF code.
 */
int ubi_leb_erase(struct ubi_volume_desc *desc, int lnum)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int err, vol_id = vol->vol_id;

	dbg_msg("erase LEB %d:%d", vol_id, lnum);

	if (desc->mode == UBI_READONLY || vol->vol_type == UBI_STATIC_VOLUME)
		return -EROFS;

	if (lnum < 0 || lnum >= vol->reserved_pebs)
		return -EINVAL;

	if (vol->upd_marker)
		return -EBADF;

	err = ubi_eba_unmap_leb(ubi, vol_id, lnum);
	if (err)
		return err;

	return ubi_wl_flush(ubi);
}
EXPORT_SYMBOL_GPL(ubi_leb_erase);

/**
 * ubi_leb_unmap - un-map logical eraseblock.
 * @desc: volume descriptor
 * @lnum: logical eraseblock number
 *
 * This function un-maps logical eraseblock @lnum and schedules the
 * corresponding physical eraseblock for erasure, so that it will eventually be
 * physically erased in background. This operation is much faster then the
 * erase operation.
 *
 * Unlike erase, the un-map operation does not guarantee that the logical
 * eraseblock will contain all 0xFF bytes when UBI is initialized again. For
 * example, if several logical eraseblocks are un-mapped, and an unclean reboot
 * happens after this, the logical eraseblocks will not necessarily be
 * un-mapped again when this MTD device is attached. They may actually be
 * mapped to the same physical eraseblocks again. So, this function has to be
 * used with care.
 *
 * In other words, when un-mapping a logical eraseblock, UBI does not store
 * any information about this on the flash media, it just marks the logical
 * eraseblock as "un-mapped" in RAM. If UBI is detached before the physical
 * eraseblock is physically erased, it will be mapped again to the same logical
 * eraseblock when the MTD device is attached again.
 *
 * The main and obvious use-case of this function is when the contents of a
 * logical eraseblock has to be re-written. Then it is much more efficient to
 * first un-map it, then write new data, rather then first erase it, then write
 * new data. Note, once new data has been written to the logical eraseblock,
 * UBI guarantees that the old contents has gone forever. In other words, if an
 * unclean reboot happens after the logical eraseblock has been un-mapped and
 * then written to, it will contain the last written data.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure. If the volume is damaged because of an interrupted update
 * this function just returns immediately with %-EBADF code.
 */
int ubi_leb_unmap(struct ubi_volume_desc *desc, int lnum)
{
	struct ubi_volume *vol = desc->vol;
	struct ubi_device *ubi = vol->ubi;
	int vol_id = vol->vol_id;

	dbg_msg("unmap LEB %d:%d", vol_id, lnum);

	if (desc->mode == UBI_READONLY || vol->vol_type == UBI_STATIC_VOLUME)
		return -EROFS;

	if (lnum < 0 || lnum >= vol->reserved_pebs)
		return -EINVAL;

	if (vol->upd_marker)
		return -EBADF;

	return ubi_eba_unmap_leb(ubi, vol_id, lnum);
}
EXPORT_SYMBOL_GPL(ubi_leb_unmap);

/**
 * ubi_is_mapped - check if logical eraseblock is mapped.
 * @desc: volume descriptor
 * @lnum: logical eraseblock number
 *
 * This function checks if logical eraseblock @lnum is mapped to a physical
 * eraseblock. If a logical eraseblock is un-mapped, this does not necessarily
 * mean it will still be un-mapped after the UBI device is re-attached. The
 * logical eraseblock may become mapped to the physical eraseblock it was last
 * mapped to.
 *
 * This function returns %1 if the LEB is mapped, %0 if not, and a negative
 * error code in case of failure. If the volume is damaged because of an
 * interrupted update this function just returns immediately with %-EBADF error
 * code.
 */
int ubi_is_mapped(struct ubi_volume_desc *desc, int lnum)
{
	struct ubi_volume *vol = desc->vol;

	dbg_msg("test LEB %d:%d", vol->vol_id, lnum);

	if (lnum < 0 || lnum >= vol->reserved_pebs)
		return -EINVAL;

	if (vol->upd_marker)
		return -EBADF;

	return vol->eba_tbl[lnum] >= 0;
}
EXPORT_SYMBOL_GPL(ubi_is_mapped);
