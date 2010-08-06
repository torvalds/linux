/*
 * UWB DRP IE management.
 *
 * Copyright (C) 2005-2006 Intel Corporation
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
 */
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/uwb.h>

#include "uwb-internal.h"


/*
 * Return the reason code for a reservations's DRP IE.
 */
int uwb_rsv_reason_code(struct uwb_rsv *rsv)
{
	static const int reason_codes[] = {
		[UWB_RSV_STATE_O_INITIATED]          = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_O_PENDING]            = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_O_MODIFIED]           = UWB_DRP_REASON_MODIFIED,
		[UWB_RSV_STATE_O_ESTABLISHED]        = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_O_TO_BE_MOVED]        = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_O_MOVE_COMBINING]     = UWB_DRP_REASON_MODIFIED,
		[UWB_RSV_STATE_O_MOVE_REDUCING]      = UWB_DRP_REASON_MODIFIED,
		[UWB_RSV_STATE_O_MOVE_EXPANDING]     = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_T_ACCEPTED]           = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_T_CONFLICT]           = UWB_DRP_REASON_CONFLICT,
		[UWB_RSV_STATE_T_PENDING]            = UWB_DRP_REASON_PENDING,
		[UWB_RSV_STATE_T_DENIED]             = UWB_DRP_REASON_DENIED,
		[UWB_RSV_STATE_T_RESIZED]            = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_T_EXPANDING_ACCEPTED] = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_T_EXPANDING_CONFLICT] = UWB_DRP_REASON_CONFLICT,
		[UWB_RSV_STATE_T_EXPANDING_PENDING]  = UWB_DRP_REASON_PENDING,
		[UWB_RSV_STATE_T_EXPANDING_DENIED]   = UWB_DRP_REASON_DENIED,
	};

	return reason_codes[rsv->state];
}

/*
 * Return the reason code for a reservations's companion DRP IE .
 */
int uwb_rsv_companion_reason_code(struct uwb_rsv *rsv)
{
	static const int companion_reason_codes[] = {
		[UWB_RSV_STATE_O_MOVE_EXPANDING]     = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_T_EXPANDING_ACCEPTED] = UWB_DRP_REASON_ACCEPTED,
		[UWB_RSV_STATE_T_EXPANDING_CONFLICT] = UWB_DRP_REASON_CONFLICT,
		[UWB_RSV_STATE_T_EXPANDING_PENDING]  = UWB_DRP_REASON_PENDING,
		[UWB_RSV_STATE_T_EXPANDING_DENIED]   = UWB_DRP_REASON_DENIED,
	};

	return companion_reason_codes[rsv->state];
}

/*
 * Return the status bit for a reservations's DRP IE.
 */
int uwb_rsv_status(struct uwb_rsv *rsv)
{
	static const int statuses[] = {
		[UWB_RSV_STATE_O_INITIATED]          = 0,
		[UWB_RSV_STATE_O_PENDING]            = 0,
		[UWB_RSV_STATE_O_MODIFIED]           = 1,
		[UWB_RSV_STATE_O_ESTABLISHED]        = 1,
		[UWB_RSV_STATE_O_TO_BE_MOVED]        = 0,
		[UWB_RSV_STATE_O_MOVE_COMBINING]     = 1,
		[UWB_RSV_STATE_O_MOVE_REDUCING]      = 1,
		[UWB_RSV_STATE_O_MOVE_EXPANDING]     = 1,
		[UWB_RSV_STATE_T_ACCEPTED]           = 1,
		[UWB_RSV_STATE_T_CONFLICT]           = 0,
		[UWB_RSV_STATE_T_PENDING]            = 0,
		[UWB_RSV_STATE_T_DENIED]             = 0,
		[UWB_RSV_STATE_T_RESIZED]            = 1,
		[UWB_RSV_STATE_T_EXPANDING_ACCEPTED] = 1,
		[UWB_RSV_STATE_T_EXPANDING_CONFLICT] = 1,
		[UWB_RSV_STATE_T_EXPANDING_PENDING]  = 1,
		[UWB_RSV_STATE_T_EXPANDING_DENIED]   = 1,

	};

	return statuses[rsv->state];
}

/*
 * Return the status bit for a reservations's companion DRP IE .
 */
int uwb_rsv_companion_status(struct uwb_rsv *rsv)
{
	static const int companion_statuses[] = {
		[UWB_RSV_STATE_O_MOVE_EXPANDING]     = 0,
		[UWB_RSV_STATE_T_EXPANDING_ACCEPTED] = 1,
		[UWB_RSV_STATE_T_EXPANDING_CONFLICT] = 0,
		[UWB_RSV_STATE_T_EXPANDING_PENDING]  = 0,
		[UWB_RSV_STATE_T_EXPANDING_DENIED]   = 0,
	};

	return companion_statuses[rsv->state];
}

/*
 * Allocate a DRP IE.
 *
 * To save having to free/allocate a DRP IE when its MAS changes,
 * enough memory is allocated for the maxiumum number of DRP
 * allocation fields.  This gives an overhead per reservation of up to
 * (UWB_NUM_ZONES - 1) * 4 = 60 octets.
 */
static struct uwb_ie_drp *uwb_drp_ie_alloc(void)
{
	struct uwb_ie_drp *drp_ie;

	drp_ie = kzalloc(sizeof(struct uwb_ie_drp) +
			UWB_NUM_ZONES * sizeof(struct uwb_drp_alloc),
			GFP_KERNEL);
	if (drp_ie) {
		drp_ie->hdr.element_id = UWB_IE_DRP;
	}
	return drp_ie;
}


/*
 * Fill a DRP IE's allocation fields from a MAS bitmap.
 */
static void uwb_drp_ie_from_bm(struct uwb_ie_drp *drp_ie,
			       struct uwb_mas_bm *mas)
{
	int z, i, num_fields = 0, next = 0;
	struct uwb_drp_alloc *zones;
	__le16 current_bmp;
	DECLARE_BITMAP(tmp_bmp, UWB_NUM_MAS);
	DECLARE_BITMAP(tmp_mas_bm, UWB_MAS_PER_ZONE);

	zones = drp_ie->allocs;

	bitmap_copy(tmp_bmp, mas->bm, UWB_NUM_MAS);

	/* Determine unique MAS bitmaps in zones from bitmap. */
	for (z = 0; z < UWB_NUM_ZONES; z++) {
		bitmap_copy(tmp_mas_bm, tmp_bmp, UWB_MAS_PER_ZONE);
		if (bitmap_weight(tmp_mas_bm, UWB_MAS_PER_ZONE) > 0) {
			bool found = false;
			current_bmp = (__le16) *tmp_mas_bm;
			for (i = 0; i < next; i++) {
				if (current_bmp == zones[i].mas_bm) {
					zones[i].zone_bm |= 1 << z;
					found = true;
					break;
				}
			}
			if (!found)  {
				num_fields++;
				zones[next].zone_bm = 1 << z;
				zones[next].mas_bm = current_bmp;
				next++;
			}
		}
		bitmap_shift_right(tmp_bmp, tmp_bmp, UWB_MAS_PER_ZONE, UWB_NUM_MAS);
	}

	/* Store in format ready for transmission (le16). */
	for (i = 0; i < num_fields; i++) {
		drp_ie->allocs[i].zone_bm = cpu_to_le16(zones[i].zone_bm);
		drp_ie->allocs[i].mas_bm = cpu_to_le16(zones[i].mas_bm);
	}

	drp_ie->hdr.length = sizeof(struct uwb_ie_drp) - sizeof(struct uwb_ie_hdr)
		+ num_fields * sizeof(struct uwb_drp_alloc);
}

/**
 * uwb_drp_ie_update - update a reservation's DRP IE
 * @rsv: the reservation
 */
int uwb_drp_ie_update(struct uwb_rsv *rsv)
{
	struct uwb_ie_drp *drp_ie;
	struct uwb_rsv_move *mv;
	int unsafe;

	if (rsv->state == UWB_RSV_STATE_NONE) {
		kfree(rsv->drp_ie);
		rsv->drp_ie = NULL;
		return 0;
	}
	
	unsafe = rsv->mas.unsafe ? 1 : 0;

	if (rsv->drp_ie == NULL) {
		rsv->drp_ie = uwb_drp_ie_alloc();
		if (rsv->drp_ie == NULL)
			return -ENOMEM;
	}
	drp_ie = rsv->drp_ie;

	uwb_ie_drp_set_unsafe(drp_ie,       unsafe);
	uwb_ie_drp_set_tiebreaker(drp_ie,   rsv->tiebreaker);
	uwb_ie_drp_set_owner(drp_ie,        uwb_rsv_is_owner(rsv));
	uwb_ie_drp_set_status(drp_ie,       uwb_rsv_status(rsv));
	uwb_ie_drp_set_reason_code(drp_ie,  uwb_rsv_reason_code(rsv));
	uwb_ie_drp_set_stream_index(drp_ie, rsv->stream);
	uwb_ie_drp_set_type(drp_ie,         rsv->type);

	if (uwb_rsv_is_owner(rsv)) {
		switch (rsv->target.type) {
		case UWB_RSV_TARGET_DEV:
			drp_ie->dev_addr = rsv->target.dev->dev_addr;
			break;
		case UWB_RSV_TARGET_DEVADDR:
			drp_ie->dev_addr = rsv->target.devaddr;
			break;
		}
	} else
		drp_ie->dev_addr = rsv->owner->dev_addr;

	uwb_drp_ie_from_bm(drp_ie, &rsv->mas);

	if (uwb_rsv_has_two_drp_ies(rsv)) {
		mv = &rsv->mv; 
		if (mv->companion_drp_ie == NULL) {
			mv->companion_drp_ie = uwb_drp_ie_alloc();
			if (mv->companion_drp_ie == NULL)
				return -ENOMEM;
		}
		drp_ie = mv->companion_drp_ie;
		
		/* keep all the same configuration of the main drp_ie */
		memcpy(drp_ie, rsv->drp_ie, sizeof(struct uwb_ie_drp));
		

		/* FIXME: handle properly the unsafe bit */
		uwb_ie_drp_set_unsafe(drp_ie,       1);
		uwb_ie_drp_set_status(drp_ie,       uwb_rsv_companion_status(rsv));
		uwb_ie_drp_set_reason_code(drp_ie,  uwb_rsv_companion_reason_code(rsv));
	
		uwb_drp_ie_from_bm(drp_ie, &mv->companion_mas);
	}

	rsv->ie_valid = true;
	return 0;
}

/*
 * Set MAS bits from given MAS bitmap in a single zone of large bitmap.
 *
 * We are given a zone id and the MAS bitmap of bits that need to be set in
 * this zone. Note that this zone may already have bits set and this only
 * adds settings - we cannot simply assign the MAS bitmap contents to the
 * zone contents. We iterate over the the bits (MAS) in the zone and set the
 * bits that are set in the given MAS bitmap.
 */
static
void uwb_drp_ie_single_zone_to_bm(struct uwb_mas_bm *bm, u8 zone, u16 mas_bm)
{
	int mas;
	u16 mas_mask;

	for (mas = 0; mas < UWB_MAS_PER_ZONE; mas++) {
		mas_mask = 1 << mas;
		if (mas_bm & mas_mask)
			set_bit(zone * UWB_NUM_ZONES + mas, bm->bm);
	}
}

/**
 * uwb_drp_ie_zones_to_bm - convert DRP allocation fields to a bitmap
 * @mas:    MAS bitmap that will be populated to correspond to the
 *          allocation fields in the DRP IE
 * @drp_ie: the DRP IE that contains the allocation fields.
 *
 * The input format is an array of MAS allocation fields (16 bit Zone
 * bitmap, 16 bit MAS bitmap) as described in [ECMA-368] section
 * 16.8.6. The output is a full 256 bit MAS bitmap.
 *
 * We go over all the allocation fields, for each allocation field we
 * know which zones are impacted. We iterate over all the zones
 * impacted and call a function that will set the correct MAS bits in
 * each zone.
 */
void uwb_drp_ie_to_bm(struct uwb_mas_bm *bm, const struct uwb_ie_drp *drp_ie)
{
	int numallocs = (drp_ie->hdr.length - 4) / 4;
	const struct uwb_drp_alloc *alloc;
	int cnt;
	u16 zone_bm, mas_bm;
	u8 zone;
	u16 zone_mask;

	bitmap_zero(bm->bm, UWB_NUM_MAS);

	for (cnt = 0; cnt < numallocs; cnt++) {
		alloc = &drp_ie->allocs[cnt];
		zone_bm = le16_to_cpu(alloc->zone_bm);
		mas_bm = le16_to_cpu(alloc->mas_bm);
		for (zone = 0; zone < UWB_NUM_ZONES; zone++)   {
			zone_mask = 1 << zone;
			if (zone_bm & zone_mask)
				uwb_drp_ie_single_zone_to_bm(bm, zone, mas_bm);
		}
	}
}

