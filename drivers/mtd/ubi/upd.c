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
 * This file contains implementation of the volume update functionality.
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
#include <asm/uaccess.h>
#include <asm/div64.h>
#include "ubi.h"

/**
 * set_update_marker - set update marker.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 *
 * This function sets the update marker flag for volume @vol_id. Returns zero
 * in case of success and a negative error code in case of failure.
 */
static int set_update_marker(struct ubi_device *ubi, int vol_id)
{
	int err;
	struct ubi_vtbl_record vtbl_rec;
	struct ubi_volume *vol = ubi->volumes[vol_id];

	dbg_msg("set update marker for volume %d", vol_id);

	if (vol->upd_marker) {
		ubi_assert(ubi->vtbl[vol_id].upd_marker);
		dbg_msg("already set");
		return 0;
	}

	memcpy(&vtbl_rec, &ubi->vtbl[vol_id], sizeof(struct ubi_vtbl_record));
	vtbl_rec.upd_marker = 1;

	err = ubi_change_vtbl_record(ubi, vol_id, &vtbl_rec);
	vol->upd_marker = 1;
	return err;
}

/**
 * clear_update_marker - clear update marker.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 * @bytes: new data size in bytes
 *
 * This function clears the update marker for volume @vol_id, sets new volume
 * data size and clears the "corrupted" flag (static volumes only). Returns
 * zero in case of success and a negative error code in case of failure.
 */
static int clear_update_marker(struct ubi_device *ubi, int vol_id, long long bytes)
{
	int err;
	uint64_t tmp;
	struct ubi_vtbl_record vtbl_rec;
	struct ubi_volume *vol = ubi->volumes[vol_id];

	dbg_msg("clear update marker for volume %d", vol_id);

	memcpy(&vtbl_rec, &ubi->vtbl[vol_id], sizeof(struct ubi_vtbl_record));
	ubi_assert(vol->upd_marker && vtbl_rec.upd_marker);
	vtbl_rec.upd_marker = 0;

	if (vol->vol_type == UBI_STATIC_VOLUME) {
		vol->corrupted = 0;
		vol->used_bytes = tmp = bytes;
		vol->last_eb_bytes = do_div(tmp, vol->usable_leb_size);
		vol->used_ebs = tmp;
		if (vol->last_eb_bytes)
			vol->used_ebs += 1;
		else
			vol->last_eb_bytes = vol->usable_leb_size;
	}

	err = ubi_change_vtbl_record(ubi, vol_id, &vtbl_rec);
	vol->upd_marker = 0;
	return err;
}

/**
 * ubi_start_update - start volume update.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 * @bytes: update bytes
 *
 * This function starts volume update operation. If @bytes is zero, the volume
 * is just wiped out. Returns zero in case of success and a negative error code
 * in case of failure.
 */
int ubi_start_update(struct ubi_device *ubi, int vol_id, long long bytes)
{
	int i, err;
	uint64_t tmp;
	struct ubi_volume *vol = ubi->volumes[vol_id];

	dbg_msg("start update of volume %d, %llu bytes", vol_id, bytes);
	vol->updating = 1;

	err = set_update_marker(ubi, vol_id);
	if (err)
		return err;

	/* Before updating - wipe out the volume */
	for (i = 0; i < vol->reserved_pebs; i++) {
		err = ubi_eba_unmap_leb(ubi, vol_id, i);
		if (err)
			return err;
	}

	if (bytes == 0) {
		err = clear_update_marker(ubi, vol_id, 0);
		if (err)
			return err;
		err = ubi_wl_flush(ubi);
		if (!err)
			vol->updating = 0;
	}

	vol->upd_buf = vmalloc(ubi->leb_size);
	if (!vol->upd_buf)
		return -ENOMEM;

	tmp = bytes;
	vol->upd_ebs = !!do_div(tmp, vol->usable_leb_size);
	vol->upd_ebs += tmp;
	vol->upd_bytes = bytes;
	vol->upd_received = 0;
	return 0;
}

/**
 * write_leb - write update data.
 * @ubi: UBI device description object
 * @vol_id: volume ID
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
static int write_leb(struct ubi_device *ubi, int vol_id, int lnum, void *buf,
		     int len, int used_ebs)
{
	int err, l;
	struct ubi_volume *vol = ubi->volumes[vol_id];

	if (vol->vol_type == UBI_DYNAMIC_VOLUME) {
		l = ALIGN(len, ubi->min_io_size);
		memset(buf + len, 0xFF, l - len);

		l = ubi_calc_data_len(ubi, buf, l);
		if (l == 0) {
			dbg_msg("all %d bytes contain 0xFF - skip", len);
			return 0;
		}
		if (len != l)
			dbg_msg("skip last %d bytes (0xFF)", len - l);

		err = ubi_eba_write_leb(ubi, vol_id, lnum, buf, 0, l,
					UBI_UNKNOWN);
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
		err = ubi_eba_write_leb_st(ubi, vol_id, lnum, buf, len,
					   UBI_UNKNOWN, used_ebs);
	}

	return err;
}

/**
 * ubi_more_update_data - write more update data.
 * @vol: volume description object
 * @buf: write data (user-space memory buffer)
 * @count: how much bytes to write
 *
 * This function writes more data to the volume which is being updated. It may
 * be called arbitrary number of times until all of the update data arrive.
 * This function returns %0 in case of success, number of bytes written during
 * the last call if the whole volume update was successfully finished, and a
 * negative error code in case of failure.
 */
int ubi_more_update_data(struct ubi_device *ubi, int vol_id,
			 const void __user *buf, int count)
{
	uint64_t tmp;
	struct ubi_volume *vol = ubi->volumes[vol_id];
	int lnum, offs, err = 0, len, to_write = count;

	dbg_msg("write %d of %lld bytes, %lld already passed",
		count, vol->upd_bytes, vol->upd_received);

	if (ubi->ro_mode)
		return -EROFS;

	tmp = vol->upd_received;
	offs = do_div(tmp, vol->usable_leb_size);
	lnum = tmp;

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
			err = write_leb(ubi, vol_id, lnum, vol->upd_buf,
					flush_len, vol->upd_ebs);
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
			err = write_leb(ubi, vol_id, lnum, vol->upd_buf, len,
					vol->upd_ebs);
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
		/* The update is finished, clear the update marker */
		err = clear_update_marker(ubi, vol_id, vol->upd_bytes);
		if (err)
			return err;
		err = ubi_wl_flush(ubi);
		if (err == 0) {
			err = to_write;
			vfree(vol->upd_buf);
			vol->updating = 0;
		}
	}

	return err;
}
