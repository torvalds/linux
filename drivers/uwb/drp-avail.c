/*
 * Ultra Wide Band
 * DRP availability management
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Reinette Chatre <reinette.chatre@intel.com>
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Manage DRP Availability (the MAS available for DRP
 * reservations). Thus:
 *
 * - Handle DRP Availability Change notifications
 *
 * - Allow the reservation manager to indicate MAS reserved/released
 *   by local (owned by/targeted at the radio controller)
 *   reservations.
 *
 * - Based on the two sources above, generate a DRP Availability IE to
 *   be included in the beacon.
 *
 * See also the documentation for struct uwb_drp_avail.
 */

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/bitmap.h>
#include "uwb-internal.h"

/**
 * uwb_drp_avail_init - initialize an RC's MAS availability
 *
 * All MAS are available initially.  The RC will inform use which
 * slots are used for the BP (it may change in size).
 */
void uwb_drp_avail_init(struct uwb_rc *rc)
{
	bitmap_fill(rc->drp_avail.global, UWB_NUM_MAS);
	bitmap_fill(rc->drp_avail.local, UWB_NUM_MAS);
	bitmap_fill(rc->drp_avail.pending, UWB_NUM_MAS);
}

/*
 * Determine MAS available for new local reservations.
 *
 * avail = global & local & pending
 */
void uwb_drp_available(struct uwb_rc *rc, struct uwb_mas_bm *avail)
{
	bitmap_and(avail->bm, rc->drp_avail.global, rc->drp_avail.local, UWB_NUM_MAS);
	bitmap_and(avail->bm, avail->bm, rc->drp_avail.pending, UWB_NUM_MAS);
}

/**
 * uwb_drp_avail_reserve_pending - reserve MAS for a new reservation
 * @rc: the radio controller
 * @mas: the MAS to reserve
 *
 * Returns 0 on success, or -EBUSY if the MAS requested aren't available.
 */
int uwb_drp_avail_reserve_pending(struct uwb_rc *rc, struct uwb_mas_bm *mas)
{
	struct uwb_mas_bm avail;

	uwb_drp_available(rc, &avail);
	if (!bitmap_subset(mas->bm, avail.bm, UWB_NUM_MAS))
		return -EBUSY;

	bitmap_andnot(rc->drp_avail.pending, rc->drp_avail.pending, mas->bm, UWB_NUM_MAS);
	return 0;
}

/**
 * uwb_drp_avail_reserve - reserve MAS for an established reservation
 * @rc: the radio controller
 * @mas: the MAS to reserve
 */
void uwb_drp_avail_reserve(struct uwb_rc *rc, struct uwb_mas_bm *mas)
{
	bitmap_or(rc->drp_avail.pending, rc->drp_avail.pending, mas->bm, UWB_NUM_MAS);
	bitmap_andnot(rc->drp_avail.local, rc->drp_avail.local, mas->bm, UWB_NUM_MAS);
	rc->drp_avail.ie_valid = false;
}

/**
 * uwb_drp_avail_release - release MAS from a pending or established reservation
 * @rc: the radio controller
 * @mas: the MAS to release
 */
void uwb_drp_avail_release(struct uwb_rc *rc, struct uwb_mas_bm *mas)
{
	bitmap_or(rc->drp_avail.local, rc->drp_avail.local, mas->bm, UWB_NUM_MAS);
	bitmap_or(rc->drp_avail.pending, rc->drp_avail.pending, mas->bm, UWB_NUM_MAS);
	rc->drp_avail.ie_valid = false;
	uwb_rsv_handle_drp_avail_change(rc);
}

/**
 * uwb_drp_avail_ie_update - update the DRP Availability IE
 * @rc: the radio controller
 *
 * avail = global & local
 */
void uwb_drp_avail_ie_update(struct uwb_rc *rc)
{
	struct uwb_mas_bm avail;

	bitmap_and(avail.bm, rc->drp_avail.global, rc->drp_avail.local, UWB_NUM_MAS);

	rc->drp_avail.ie.hdr.element_id = UWB_IE_DRP_AVAILABILITY;
	rc->drp_avail.ie.hdr.length = UWB_NUM_MAS / 8;
	uwb_mas_bm_copy_le(rc->drp_avail.ie.bmp, &avail);
	rc->drp_avail.ie_valid = true;
}

/**
 * Create an unsigned long from a buffer containing a byte stream.
 *
 * @array: pointer to buffer
 * @itr:   index of buffer from where we start
 * @len:   the buffer's remaining size may not be exact multiple of
 *         sizeof(unsigned long), @len is the length of buffer that needs
 *         to be converted. This will be sizeof(unsigned long) or smaller
 *         (BUG if not). If it is smaller then we will pad the remaining
 *         space of the result with zeroes.
 */
static
unsigned long get_val(u8 *array, size_t itr, size_t len)
{
	unsigned long val = 0;
	size_t top = itr + len;

	BUG_ON(len > sizeof(val));

	while (itr < top) {
		val <<= 8;
		val |= array[top - 1];
		top--;
	}
	val <<= 8 * (sizeof(val) - len); /* padding */
	return val;
}

/**
 * Initialize bitmap from data buffer.
 *
 * The bitmap to be converted could come from a IE, for example a
 * DRP Availability IE.
 * From ECMA-368 1.0 [16.8.7]: "
 * octets: 1            1               N * (0 to 32)
 *         Element ID   Length (=N)     DRP Availability Bitmap
 *
 * The DRP Availability Bitmap field is up to 256 bits long, one
 * bit for each MAS in the superframe, where the least-significant
 * bit of the field corresponds to the first MAS in the superframe
 * and successive bits correspond to successive MASs."
 *
 * The DRP Availability bitmap is in octets from 0 to 32, so octet
 * 32 contains bits for MAS 1-8, etc. If the bitmap is smaller than 32
 * octets, the bits in octets not included at the end of the bitmap are
 * treated as zero. In this case (when the bitmap is smaller than 32
 * octets) the MAS represented range from MAS 1 to MAS (size of bitmap)
 * with the last octet still containing bits for MAS 1-8, etc.
 *
 * For example:
 * F00F0102 03040506 0708090A 0B0C0D0E 0F010203
 * ^^^^
 * ||||
 * ||||
 * |||\LSB of byte is MAS 9
 * ||\MSB of byte is MAS 16
 * |\LSB of first byte is MAS 1
 * \ MSB of byte is MAS 8
 *
 * An example of this encoding can be found in ECMA-368 Annex-D [Table D.11]
 *
 * The resulting bitmap will have the following mapping:
 *	bit position 0 == MAS 1
 *	bit position 1 == MAS 2
 *	...
 *	bit position (UWB_NUM_MAS - 1) == MAS UWB_NUM_MAS
 *
 * @bmp_itr:	pointer to bitmap (can be declared with DECLARE_BITMAP)
 * @buffer:	pointer to buffer containing bitmap data in big endian
 *              format (MSB first)
 * @buffer_size:number of bytes with which bitmap should be initialized
 */
static
void buffer_to_bmp(unsigned long *bmp_itr, void *_buffer,
		   size_t buffer_size)
{
	u8 *buffer = _buffer;
	size_t itr, len;
	unsigned long val;

	itr = 0;
	while (itr < buffer_size) {
		len = buffer_size - itr >= sizeof(val) ?
			sizeof(val) : buffer_size - itr;
		val = get_val(buffer, itr, len);
		bmp_itr[itr / sizeof(val)] = val;
		itr += sizeof(val);
	}
}


/**
 * Extract DRP Availability bitmap from the notification.
 *
 * The notification that comes in contains a bitmap of (UWB_NUM_MAS / 8) bytes
 * We convert that to our internal representation.
 */
static
int uwbd_evt_get_drp_avail(struct uwb_event *evt, unsigned long *bmp)
{
	struct device *dev = &evt->rc->uwb_dev.dev;
	struct uwb_rc_evt_drp_avail *drp_evt;
	int result = -EINVAL;

	/* Is there enough data to decode the event? */
	if (evt->notif.size < sizeof(*drp_evt)) {
		dev_err(dev, "DRP Availability Change: Not enough "
			"data to decode event [%zu bytes, %zu "
			"needed]\n", evt->notif.size, sizeof(*drp_evt));
		goto error;
	}
	drp_evt = container_of(evt->notif.rceb, struct uwb_rc_evt_drp_avail, rceb);
	buffer_to_bmp(bmp, drp_evt->bmp, UWB_NUM_MAS/8);
	result = 0;
error:
	return result;
}


/**
 * Process an incoming DRP Availability notification.
 *
 * @evt:	Event information (packs the actual event data, which
 *              radio controller it came to, etc).
 *
 * @returns:    0 on success (so uwbd() frees the event buffer), < 0
 *              on error.
 *
 * According to ECMA-368 1.0 [16.8.7], bits set to ONE indicate that
 * the MAS slot is available, bits set to ZERO indicate that the slot
 * is busy.
 *
 * So we clear available slots, we set used slots :)
 *
 * The notification only marks non-availability based on the BP and
 * received DRP IEs that are not for this radio controller.  A copy of
 * this bitmap is needed to generate the real availability (which
 * includes local and pending reservations).
 *
 * The DRP Availability IE that this radio controller emits will need
 * to be updated.
 */
int uwbd_evt_handle_rc_drp_avail(struct uwb_event *evt)
{
	int result;
	struct uwb_rc *rc = evt->rc;
	DECLARE_BITMAP(bmp, UWB_NUM_MAS);

	result = uwbd_evt_get_drp_avail(evt, bmp);
	if (result < 0)
		return result;

	mutex_lock(&rc->rsvs_mutex);
	bitmap_copy(rc->drp_avail.global, bmp, UWB_NUM_MAS);
	rc->drp_avail.ie_valid = false;
	uwb_rsv_handle_drp_avail_change(rc);
	mutex_unlock(&rc->rsvs_mutex);

	uwb_rsv_sched_update(rc);

	return 0;
}
