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

/* Here we keep miscellaneous functions which are used all over the UBI code */

#include "ubi.h"

/**
 * calc_data_len - calculate how much real data is stored in a buffer.
 * @ubi: UBI device description object
 * @buf: a buffer with the contents of the physical eraseblock
 * @length: the buffer length
 *
 * This function calculates how much "real data" is stored in @buf and returnes
 * the length. Continuous 0xFF bytes at the end of the buffer are not
 * considered as "real data".
 */
int ubi_calc_data_len(const struct ubi_device *ubi, const void *buf,
		      int length)
{
	int i;

	ubi_assert(!(length & (ubi->min_io_size - 1)));

	for (i = length - 1; i >= 0; i--)
		if (((const uint8_t *)buf)[i] != 0xFF)
			break;

	/* The resulting length must be aligned to the minimum flash I/O size */
	length = ALIGN(i + 1, ubi->min_io_size);
	return length;
}

/**
 * ubi_check_volume - check the contents of a static volume.
 * @ubi: UBI device description object
 * @vol_id: ID of the volume to check
 *
 * This function checks if static volume @vol_id is corrupted by fully reading
 * it and checking data CRC. This function returns %0 if the volume is not
 * corrupted, %1 if it is corrupted and a negative error code in case of
 * failure. Dynamic volumes are not checked and zero is returned immediately.
 */
int ubi_check_volume(struct ubi_device *ubi, int vol_id)
{
	void *buf;
	int err = 0, i;
	struct ubi_volume *vol = ubi->volumes[vol_id];

	if (vol->vol_type != UBI_STATIC_VOLUME)
		return 0;

	buf = vmalloc(vol->usable_leb_size);
	if (!buf)
		return -ENOMEM;

	for (i = 0; i < vol->used_ebs; i++) {
		int size;

		if (i == vol->used_ebs - 1)
			size = vol->last_eb_bytes;
		else
			size = vol->usable_leb_size;

		err = ubi_eba_read_leb(ubi, vol, i, buf, 0, size, 1);
		if (err) {
			if (mtd_is_eccerr(err))
				err = 1;
			break;
		}
	}

	vfree(buf);
	return err;
}

/**
 * ubi_update_reserved - update bad eraseblock handling accounting data.
 * @ubi: UBI device description object
 *
 * This function calculates the gap between current number of PEBs reserved for
 * bad eraseblock handling and the required level of PEBs that must be
 * reserved, and if necessary, reserves more PEBs to fill that gap, according
 * to availability. Should be called with ubi->volumes_lock held.
 */
void ubi_update_reserved(struct ubi_device *ubi)
{
	int need = ubi->beb_rsvd_level - ubi->beb_rsvd_pebs;

	if (need <= 0 || ubi->avail_pebs == 0)
		return;

	need = min_t(int, need, ubi->avail_pebs);
	ubi->avail_pebs -= need;
	ubi->rsvd_pebs += need;
	ubi->beb_rsvd_pebs += need;
	ubi_msg(ubi, "reserved more %d PEBs for bad PEB handling", need);
}

/**
 * ubi_calculate_reserved - calculate how many PEBs must be reserved for bad
 * eraseblock handling.
 * @ubi: UBI device description object
 */
void ubi_calculate_reserved(struct ubi_device *ubi)
{
	/*
	 * Calculate the actual number of PEBs currently needed to be reserved
	 * for future bad eraseblock handling.
	 */
	ubi->beb_rsvd_level = ubi->bad_peb_limit - ubi->bad_peb_count;
	if (ubi->beb_rsvd_level < 0) {
		ubi->beb_rsvd_level = 0;
		ubi_warn(ubi, "number of bad PEBs (%d) is above the expected limit (%d), not reserving any PEBs for bad PEB handling, will use available PEBs (if any)",
			 ubi->bad_peb_count, ubi->bad_peb_limit);
	}
}

/**
 * ubi_check_pattern - check if buffer contains only a certain byte pattern.
 * @buf: buffer to check
 * @patt: the pattern to check
 * @size: buffer size in bytes
 *
 * This function returns %1 in there are only @patt bytes in @buf, and %0 if
 * something else was also found.
 */
int ubi_check_pattern(const void *buf, uint8_t patt, int size)
{
	int i;

	for (i = 0; i < size; i++)
		if (((const uint8_t *)buf)[i] != patt)
			return 0;
	return 1;
}
