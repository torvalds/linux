// SPDX-License-Identifier: GPL-2.0
/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LOV

#include <linux/libcfs/libcfs.h>

#include <obd_class.h>

#include "lov_internal.h"

/* compute object size given "stripeno" and the ost size */
u64 lov_stripe_size(struct lov_stripe_md *lsm, u64 ost_size, int stripeno)
{
	unsigned long ssize = lsm->lsm_stripe_size;
	unsigned long stripe_size;
	u64 swidth;
	u64 lov_size;
	int magic = lsm->lsm_magic;

	if (ost_size == 0)
		return 0;

	lsm_op_find(magic)->lsm_stripe_by_index(lsm, &stripeno, NULL, &swidth);

	/* lov_do_div64(a, b) returns a % b, and a = a / b */
	stripe_size = lov_do_div64(ost_size, ssize);
	if (stripe_size)
		lov_size = ost_size * swidth + stripeno * ssize + stripe_size;
	else
		lov_size = (ost_size - 1) * swidth + (stripeno + 1) * ssize;

	return lov_size;
}

/**
 * Compute file level page index by stripe level page offset
 */
pgoff_t lov_stripe_pgoff(struct lov_stripe_md *lsm, pgoff_t stripe_index,
			 int stripe)
{
	loff_t offset;

	offset = lov_stripe_size(lsm, (stripe_index << PAGE_SHIFT) + 1, stripe);
	return offset >> PAGE_SHIFT;
}

/* we have an offset in file backed by an lov and want to find out where
 * that offset lands in our given stripe of the file.  for the easy
 * case where the offset is within the stripe, we just have to scale the
 * offset down to make it relative to the stripe instead of the lov.
 *
 * the harder case is what to do when the offset doesn't intersect the
 * stripe.  callers will want start offsets clamped ahead to the start
 * of the nearest stripe in the file.  end offsets similarly clamped to the
 * nearest ending byte of a stripe in the file:
 *
 * all this function does is move offsets to the nearest region of the
 * stripe, and it does its work "mod" the full length of all the stripes.
 * consider a file with 3 stripes:
 *
 *	     S					      E
 * ---------------------------------------------------------------------
 * |    0    |     1     |     2     |    0    |     1     |     2     |
 * ---------------------------------------------------------------------
 *
 * to find stripe 1's offsets for S and E, it divides by the full stripe
 * width and does its math in the context of a single set of stripes:
 *
 *	     S	 E
 * -----------------------------------
 * |    0    |     1     |     2     |
 * -----------------------------------
 *
 * it'll notice that E is outside stripe 1 and clamp it to the end of the
 * stripe, then multiply it back out by lov_off to give the real offsets in
 * the stripe:
 *
 *   S		   E
 * ---------------------------------------------------------------------
 * |    1    |     1     |     1     |    1    |     1     |     1     |
 * ---------------------------------------------------------------------
 *
 * it would have done similarly and pulled S forward to the start of a 1
 * stripe if, say, S had landed in a 0 stripe.
 *
 * this rounding isn't always correct.  consider an E lov offset that lands
 * on a 0 stripe, the "mod stripe width" math will pull it forward to the
 * start of a 1 stripe, when in fact it wanted to be rounded back to the end
 * of a previous 1 stripe.  this logic is handled by callers and this is why:
 *
 * this function returns < 0 when the offset was "before" the stripe and
 * was moved forward to the start of the stripe in question;  0 when it
 * falls in the stripe and no shifting was done; > 0 when the offset
 * was outside the stripe and was pulled back to its final byte.
 */
int lov_stripe_offset(struct lov_stripe_md *lsm, u64 lov_off,
		      int stripeno, u64 *obdoff)
{
	unsigned long ssize  = lsm->lsm_stripe_size;
	u64 stripe_off, this_stripe, swidth;
	int magic = lsm->lsm_magic;
	int ret = 0;

	if (lov_off == OBD_OBJECT_EOF) {
		*obdoff = OBD_OBJECT_EOF;
		return 0;
	}

	lsm_op_find(magic)->lsm_stripe_by_index(lsm, &stripeno, &lov_off,
						&swidth);

	/* lov_do_div64(a, b) returns a % b, and a = a / b */
	stripe_off = lov_do_div64(lov_off, swidth);

	this_stripe = (u64)stripeno * ssize;
	if (stripe_off < this_stripe) {
		stripe_off = 0;
		ret = -1;
	} else {
		stripe_off -= this_stripe;

		if (stripe_off >= ssize) {
			stripe_off = ssize;
			ret = 1;
		}
	}

	*obdoff = lov_off * ssize + stripe_off;
	return ret;
}

/* Given a whole-file size and a stripe number, give the file size which
 * corresponds to the individual object of that stripe.
 *
 * This behaves basically in the same was as lov_stripe_offset, except that
 * file sizes falling before the beginning of a stripe are clamped to the end
 * of the previous stripe, not the beginning of the next:
 *
 *					       S
 * ---------------------------------------------------------------------
 * |    0    |     1     |     2     |    0    |     1     |     2     |
 * ---------------------------------------------------------------------
 *
 * if clamped to stripe 2 becomes:
 *
 *				   S
 * ---------------------------------------------------------------------
 * |    0    |     1     |     2     |    0    |     1     |     2     |
 * ---------------------------------------------------------------------
 */
u64 lov_size_to_stripe(struct lov_stripe_md *lsm, u64 file_size,
		       int stripeno)
{
	unsigned long ssize  = lsm->lsm_stripe_size;
	u64 stripe_off, this_stripe, swidth;
	int magic = lsm->lsm_magic;

	if (file_size == OBD_OBJECT_EOF)
		return OBD_OBJECT_EOF;

	lsm_op_find(magic)->lsm_stripe_by_index(lsm, &stripeno, &file_size,
						&swidth);

	/* lov_do_div64(a, b) returns a % b, and a = a / b */
	stripe_off = lov_do_div64(file_size, swidth);

	this_stripe = (u64)stripeno * ssize;
	if (stripe_off < this_stripe) {
		/* Move to end of previous stripe, or zero */
		if (file_size > 0) {
			file_size--;
			stripe_off = ssize;
		} else {
			stripe_off = 0;
		}
	} else {
		stripe_off -= this_stripe;

		if (stripe_off >= ssize) {
			/* Clamp to end of this stripe */
			stripe_off = ssize;
		}
	}

	return (file_size * ssize + stripe_off);
}

/* given an extent in an lov and a stripe, calculate the extent of the stripe
 * that is contained within the lov extent.  this returns true if the given
 * stripe does intersect with the lov extent.
 */
int lov_stripe_intersects(struct lov_stripe_md *lsm, int stripeno,
			  u64 start, u64 end, u64 *obd_start, u64 *obd_end)
{
	int start_side, end_side;

	start_side = lov_stripe_offset(lsm, start, stripeno, obd_start);
	end_side = lov_stripe_offset(lsm, end, stripeno, obd_end);

	CDEBUG(D_INODE, "[%llu->%llu] -> [(%d) %llu->%llu (%d)]\n",
	       start, end, start_side, *obd_start, *obd_end, end_side);

	/* this stripe doesn't intersect the file extent when neither
	 * start or the end intersected the stripe and obd_start and
	 * obd_end got rounded up to the save value.
	 */
	if (start_side != 0 && end_side != 0 && *obd_start == *obd_end)
		return 0;

	/* as mentioned in the lov_stripe_offset commentary, end
	 * might have been shifted in the wrong direction.  This
	 * happens when an end offset is before the stripe when viewed
	 * through the "mod stripe size" math. we detect it being shifted
	 * in the wrong direction and touch it up.
	 * interestingly, this can't underflow since end must be > start
	 * if we passed through the previous check.
	 * (should we assert for that somewhere?)
	 */
	if (end_side != 0)
		(*obd_end)--;

	return 1;
}

/* compute which stripe number "lov_off" will be written into */
int lov_stripe_number(struct lov_stripe_md *lsm, u64 lov_off)
{
	unsigned long ssize  = lsm->lsm_stripe_size;
	u64 stripe_off, swidth;
	int magic = lsm->lsm_magic;

	lsm_op_find(magic)->lsm_stripe_by_offset(lsm, NULL, &lov_off, &swidth);

	stripe_off = lov_do_div64(lov_off, swidth);

	/* Puts stripe_off/ssize result into stripe_off */
	lov_do_div64(stripe_off, ssize);

	return stripe_off;
}
