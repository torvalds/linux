/*
 * Copyright (c) International Business Machines Corp., 2006
 * Copyright (c) Nokia Corporation, 2006
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
 *
 * Jan 2007: Alexander Schmidt, hacked per-volume update.
 */

/*
 * This file contains implementation of the volume update and atomic LEB change
 * functionality.
 *
 * The update operation is based on the per-volume update marker which is
 * stored in the volume table. The update marker is set before the update
 * starts, and removed after the update has been finished. So if the update was
 * interrupted by an unclean re-boot or due to some other reasons, the update
 * marker stays on the flash media and UBI finds it when it attaches the MTD
 * device next time. If the update marker is set for a volume, the volume is
 * treated as damaged and most I/O operations are prohibited. Only a new update
 * operation is allowed.
 *
 * Note, in general it is possible to implement the update operation as a
 * transaction with a roll-back capability.
 */

#include <linux/err.h>
#include <linux/uaccess.h>
#include <linux/math64.h>
#include "ubi.h"

/**
 * set_update_marker - set update marker.
 * @ubi: UBI device description object
 * @vol: volume description object
 *
 * This function sets the update marker flag for volume @vol. Returns zero
 * in case of success and a negative error code in case of failure.
 */
static int set_update_marker(struct ubi_device *ubi, struct ubi_volume *vol)
{
	int err;
	struct ubi_vtbl_record vtbl_rec;

	dbg_gen("set update marker for volume %d", vol->vol_id);

	if (vol->upd_marker) {
		ubi_assert(ubi->vtbl[vol->vol_id].upd_marker);
		dbg_gen("already set");
		return 0;
	}

	vtbl_rec = ubi->vtbl[vol->vol_id];
	vtbl_rec.upd_marker = 1;

	mutex_lock(&ubi->device_mutex);
	err = ubi_change_vtbl_record(ubi, vol->vol_id, &vtbl_rec);
	vol->upd_marker = 1;
	mutex_unlock(&ubi->device_mutex);
	return err;
}

/**
 * clear_update_marker - clear update marker.
 * @ubi: UBI device description object
 * @vol: volume description object
 * @bytes: new data size in bytes
 *
 * This function clears the update marker for volume @vol, sets new volume
 * data size and clears the "corrupted" flag (static volumes only). Returns
 * zero in case of success and a negative error code in case of failure.
 */
static int clear_update_marker(struct ubi_device *ubi, struct ubi_volume *vol,
			       long long bytes)
{
	int err;
	struct ubi_vtbl_record vtbl_rec;

	dbg_gen("clear update marker for volume %d", vol->vol_id);

	vtbl_rec = ubi->vtbl[vol->vol_id];
	ubi_assert(vol->upd_marker && vtbl_rec.upd_marker);
	vtbl_rec.upd_marker = 0;

	if (vol->vol_type == UBI_STATIC_VOLUME) {
		vol->corrupted = 0;
		vol->used_bytes = bytes;
		vol->used_ebs = div_u64_rem(bytes, vol->usable_leb_size,
					    &vol->last_eb_bytes);
		if (vol->last_eb_bytes)
			vol->used_ebs += 1;
		else
			vol->last_eb_bytes = vol->usable_leb_size;
	}

	mutex_lock(&ubi->device_mutex);
	err = ubi_change_vtbl_record(ubi, vol->vol_id, &vtbl_rec);
	vol->upd_marker = 0;
	mutex_unlock(&ubi->device_mutex);
	return err;
}

/**
 * ubi_start_update - start volume update.
 * @ubi: UBI device description object
 * @vol: volume description object
 * @bytes: update bytes
 *
 * This function starts volume update operation. If @bytes is zero, the volume
 * is just wiped out. Returns zero in case of success and a negative error code
 * in case of failure.
 */
int ubi_start_update(struct ubi_device *ubi, struct ubi_volume *vol,
		     long long bytes)
{
	int i, err;

	dbg_gen("start update of volume %d, %llu bytes", vol->vol_id, bytes);
	ubi_assert(!vol->updating && !vol->changing_leb);
	vol->updating = 1;

	vol->upd_buf = vmalloc(ubi->leb_size);
	if (!vol->upd_buf)
		return -ENOMEM;

	err = set_update_marker(ubi, vol);
	if (err)
		return err;

	/* Before updating - wipe out the volume */
	for (i = 0; i < vol->reserved_pebs; i++) {
		err = ubi_eba_unmap_leb(ubi, vol, i);
		if (err)
			return err;
	}

	err = ubi_wl_flush(ubi, UBI_ALL, UBI_ALL);
	if (err)
		return err;

	if (bytes == 0) {
		err = clear_update_marker(ubi, vol, 0);
		if (err)
			return err;

		vfree(vol->upd_buf);
		vol->updating = 0;
		return 0;
	}

	vol->upd_ebs = div_u64(bytes + vol->usable_leb_size - 1,
			       vol->usable_leb_size);
	vol->upd_bytes = bytes;
	vol->upd_received = 0;
	return 0;
}

/**
 * ubi_start_leb_change - start atomic LEB change.
 * @ubi: UBI device description object
 * @vol: volume description object
 * @req: operation request
 *
 * This function starts atomic LEB change operation. Returns zero in case of
 * success and a negative error code in case of failure.
 */
int ubi_start_leb_change(struct ubi_device *ubi, struct ubi_volume *vol,
			 const struct ubi_leb_change_req *req)
{
	ubi_assert(!vol->updating && !vol->changing_leb);

	dbg_gen("start changing LEB %d:%d, %u bytes",
		vol->vol_id, req->lnum, req->bytes);
	if (req->bytes == 0)
		return ubi_eba_atomic_leb_change(ubi, vol, req->lnum, NULL, 0);

	vol->upd_bytes = req->bytes;
	vol->upd_received = 0;
	vol->changing_leb = 1;
	vol->ch_lnum = req->lnum;

	vol->upd_buf = vmalloc(ALIGN((int)req->bytes, ubi->min_io_size));
	if (!vol->upd_buf)
		return -ENOMEM;

	return 0;
}

/**
 * write_leb - write update data.
 * @ubi: UBI device description object
 * @vol: volume description object
 * @lnum: logical eraseblock number
 * @buf: data to write
 * @len: data size
 * @used_ebs: how many logical eraseblocks will this volume contain (static
 * volumes only)
 *
 * This function writes update data to corresponding logical eraseblock. In
 * case of dynamic volume, this function checks if the data contains 0xFF bytes
 * at the end. If yes, the 0xFF bytes are cut and not written. So if the whole
 * buffer contains only 0xFF bytes, the LEB is left unmapped.
 *
 * The reason why we skip the trailing 0xFF bytes in case of dynamic volume is
 * that we want to make sure that more data may be appended to the logical
 * eraseblock in future. Indeed, writing 0xFF bytes may have side effects and
 * this PEB won't be writable anymore. So if one writes the file-system image
 * to the UBI volume where 0xFFs mean free space - UBI makes sure this free
 * space is writable after the update.
 *
 * We do not do this for static volumes because they are read-only. But this
 * also cannot be done because we have to store per-LEB CRC and the correct
 * data length.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int write_leb(struct ubi_device *ubi, struct ubi_volume *vol, int lnum,
		     void *buf, int len, int used_ebs)
{
	int err;

	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		int l = ALIGN(len, ubi->min_io_size);

		memset(buf + len, 0xFF, l - len);
		len = ubi_calc_data_len(ubi, buf, l);
		if (len == 0) {
			dbg_gen("all %d bytes contain 0xFF - skip", len);
			return 0;
		}

		err = ubi_eba_write_leb(ubi, vol, lnum, buf, 0, len);
	} else {
		/*
		 * When writing static volume, and this is the last logical
		 * eraseblock, the length (@len) does not have to be aligned to
		 * the minimal flash I/O unit. The 'ubi_eba_write_leb_st()'
		 * function accepts exact (unaligned) length and stores it in
		 * the VID header. And it takes care of proper alignment by
		 * padding the buffer. Here we just make sure the padding will
		 * contain zeros, not random trash.
		 */
		memset(buf + len, 0, vol->usable_leb_size - len);
		err = ubi_eba_write_leb_st(ubi, vol, lnum, buf, len, used_ebs);
	}

	return err;
}

/**
 * ubi_more_update_data - write more update data.
 * @ubi: UBI device description object
 * @vol: volume description object
 * @buf: write data (user-space memory buffer)
 * @count: how much bytes to write
 *
 * This function writes more data to the volume which is being updated. It may
 * be called arbitrary number of times until all the update data arriveis. This
 * function returns %0 in case of success, number of bytes written during the
 * last call if the whole volume update has been successfully finished, and a
 * negative error code in case of failure.
 */
int ubi_more_update_data(struct ubi_device *ubi, struct ubi_volume *vol,
			 const void __user *buf, int count)
{
	int lnum, offs, err = 0, len, to_write = count;

	dbg_gen("write %d of %lld bytes, %lld already passed",
		count, vol->upd_bytes, vol->upd_received);

	if (ubi->ro_mode)
		return -EROFS;

	lnum = div_u64_rem(vol->upd_received,  vol->usable_leb_size, &offs);
	if (vol->upd_received + count > vol->upd_bytes)
		to_write = count = vol->upd_bytes - vol->upd_received;

	/*
	 * When updating volumes, we accumulate whole logical eraseblock of
	 * data and write it at once.
	 */
	if (offs != 0) {
		/*
		 * This is a write to the middle of the logical eraseblock. We
		 * copy the data to our update buffer and wait for more data or
		 * flush it if the whole eraseblock is written or the update
		 * is finished.
		 */

		len = vol->usable_leb_size - offs;
		if (len > count)
			len = count;

		err = copy_from_user(vol->upd_buf + offs, buf, len);
		if (err)
			return -EFAULT;

		if (offs + len == vol->usable_leb_size ||
		    vol->upd_received + len == vol->upd_bytes) {
			int flush_len = offs + len;

			/*
			 * OK, we gathered either the whole eraseblock or this
			 * is the last chunk, it's time to flush the buffer.
			 */
			ubi_assert(flush_len <= vol->usable_leb_size);
			err = write_leb(ubi, vol, lnum, vol->upd_buf, flush_len,
					vol->upd_ebs);
			if (err)
				return err;
		}

		vol->upd_received += len;
		count -= len;
		buf += len;
		lnum += 1;
	}

	/*
	 * If we've got more to write, let's continue. At this point we know we
	 * are starting from the beginning of an eraseblock.
	 */
	while (count) {
		if (count > vol->usable_leb_size)
			len = vol->usable_leb_size;
		else
			len = count;

		err = copy_from_user(vol->upd_buf, buf, len);
		if (err)
			return -EFAULT;

		if (len == vol->usable_leb_size ||
		    vol->upd_received + len == vol->upd_bytes) {
			err = write_leb(ubi, vol, lnum, vol->upd_buf,
					len, vol->upd_ebs);
			if (err)
				break;
		}

		vol->upd_received += len;
		count -= len;
		lnum += 1;
		buf += len;
	}

	ubi_assert(vol->upd_received <= vol->upd_bytes);
	if (vol->upd_received == vol->upd_bytes) {
		err = ubi_wl_flush(ubi, UBI_ALL, UBI_ALL);
		if (err)
			return err;
		/* The update is finished, clear the update marker */
		err = clear_update_marker(ubi, vol, vol->upd_bytes);
		if (err)
			return err;
		vol->updating = 0;
		err = to_write;
		vfree(vol->upd_buf);
	}

	return err;
}

/**
 * ubi_more_leb_change_data - accept more data for atomic LEB change.
 * @ubi: UBI device description object
 * @vol: volume description object
 * @buf: write data (user-space memory buffer)
 * @count: how much bytes to write
 *
 * This function accepts more data to the volume which is being under the
 * "atomic LEB change" operation. It may be called arbitrary number of times
 * until all data arrives. This function returns %0 in case of success, number
 * of bytes written during the last call if the whole "atomic LEB change"
 * operation has been successfully finished, and a negative error code in case
 * of failure.
 */
int ubi_more_leb_change_data(struct ubi_device *ubi, struct ubi_volume *vol,
			     const void __user *buf, int count)
{
	int err;

	dbg_gen("write %d of %lld bytes, %lld already passed",
		count, vol->upd_bytes, vol->upd_received);

	if (ubi->ro_mode)
		return -EROFS;

	if (vol->upd_received + count > vol->upd_bytes)
		count = vol->upd_bytes - vol->upd_received;

	err = copy_from_user(vol->upd_buf + vol->upd_received, buf, count);
	if (err)
		return -EFAULT;

	vol->upd_received += count;

	if (vol->upd_received == vol->upd_bytes) {
		int len = ALIGN((int)vol->upd_bytes, ubi->min_io_size);

		memset(vol->upd_buf + vol->upd_bytes, 0xFF,
		       len - vol->upd_bytes);
		len = ubi_calc_data_len(ubi, vol->upd_buf, len);
		err = ubi_eba_atomic_leb_change(ubi, vol, vol->ch_lnum,
						vol->upd_buf, len);
		if (err)
			return err;
	}

	ubi_assert(vol->upd_received <= vol->upd_bytes);
	if (vol->upd_received == vol->upd_bytes) {
		vol->changing_leb = 0;
		err = count;
		vfree(vol->upd_buf);
	}

	return err;
}
