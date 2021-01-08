/*
 * SImple Tiler Allocator (SiTA): 2D and 1D allocation(reservation) algorithm
 *
 * Authors: Ravi Ramachandra <r.ramachandra@ti.com>,
 *          Lajos Molnar <molnar@ti.com>
 *          Andy Gross <andy.gross@ti.com>
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com/
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include "tcm.h"

static unsigned long mask[8];
/*
 * pos		position in bitmap
 * w		width in slots
 * h		height in slots
 * map		ptr to bitmap
 * stride		slots in a row
 */
static void free_slots(unsigned long pos, u16 w, u16 h,
		unsigned long *map, u16 stride)
{
	int i;

	for (i = 0; i < h; i++, pos += stride)
		bitmap_clear(map, pos, w);
}

/*
 * w		width in slots
 * pos		ptr to position
 * map		ptr to bitmap
 * num_bits	number of bits in bitmap
 */
static int r2l_b2t_1d(u16 w, unsigned long *pos, unsigned long *map,
		size_t num_bits)
{
	unsigned long search_count = 0;
	unsigned long bit;
	bool area_found = false;

	*pos = num_bits - w;

	while (search_count < num_bits) {
		bit = find_next_bit(map, num_bits, *pos);

		if (bit - *pos >= w) {
			/* found a long enough free area */
			bitmap_set(map, *pos, w);
			area_found = true;
			break;
		}

		search_count = num_bits - bit + w;
		*pos = bit - w;
	}

	return (area_found) ? 0 : -ENOMEM;
}

/*
 * w = width in slots
 * h = height in slots
 * a = align in slots	(mask, 2^n-1, 0 is unaligned)
 * offset = offset in bytes from 4KiB
 * pos = position in bitmap for buffer
 * map = bitmap ptr
 * num_bits = size of bitmap
 * stride = bits in one row of container
 */
static int l2r_t2b(u16 w, u16 h, u16 a, s16 offset,
		unsigned long *pos, unsigned long slot_bytes,
		unsigned long *map, size_t num_bits, size_t slot_stride)
{
	int i;
	unsigned long index;
	bool area_free = false;
	unsigned long slots_per_band = PAGE_SIZE / slot_bytes;
	unsigned long bit_offset = (offset > 0) ? offset / slot_bytes : 0;
	unsigned long curr_bit = bit_offset;

	/* reset alignment to 1 if we are matching a specific offset */
	/* adjust alignment - 1 to get to the format expected in bitmaps */
	a = (offset > 0) ? 0 : a - 1;

	/* FIXME Return error if slots_per_band > stride */

	while (curr_bit < num_bits) {
		*pos = bitmap_find_next_zero_area(map, num_bits, curr_bit, w,
				a);

		/* skip forward if we are not at right offset */
		if (bit_offset > 0 && (*pos % slots_per_band != bit_offset)) {
			curr_bit = ALIGN(*pos, slots_per_band) + bit_offset;
			continue;
		}

		/* skip forward to next row if we overlap end of row */
		if ((*pos % slot_stride) + w > slot_stride) {
			curr_bit = ALIGN(*pos, slot_stride) + bit_offset;
			continue;
		}

		/* TODO: Handle overlapping 4K boundaries */

		/* break out of look if we will go past end of container */
		if ((*pos + slot_stride * h) > num_bits)
			break;

		/* generate mask that represents out matching pattern */
		bitmap_clear(mask, 0, slot_stride);
		bitmap_set(mask, (*pos % BITS_PER_LONG), w);

		/* assume the area is free until we find an overlap */
		area_free = true;

		/* check subsequent rows to see if complete area is free */
		for (i = 1; i < h; i++) {
			index = *pos / BITS_PER_LONG + i * 8;
			if (bitmap_intersects(&map[index], mask,
				(*pos % BITS_PER_LONG) + w)) {
				area_free = false;
				break;
			}
		}

		if (area_free)
			break;

		/* go forward past this match */
		if (bit_offset > 0)
			curr_bit = ALIGN(*pos, slots_per_band) + bit_offset;
		else
			curr_bit = *pos + a + 1;
	}

	if (area_free) {
		/* set area as in-use. iterate over rows */
		for (i = 0, index = *pos; i < h; i++, index += slot_stride)
			bitmap_set(map, index, w);
	}

	return (area_free) ? 0 : -ENOMEM;
}

static s32 sita_reserve_1d(struct tcm *tcm, u32 num_slots,
			   struct tcm_area *area)
{
	unsigned long pos;
	int ret;

	spin_lock(&(tcm->lock));
	ret = r2l_b2t_1d(num_slots, &pos, tcm->bitmap, tcm->map_size);
	if (!ret) {
		area->p0.x = pos % tcm->width;
		area->p0.y = pos / tcm->width;
		area->p1.x = (pos + num_slots - 1) % tcm->width;
		area->p1.y = (pos + num_slots - 1) / tcm->width;
	}
	spin_unlock(&(tcm->lock));

	return ret;
}

static s32 sita_reserve_2d(struct tcm *tcm, u16 h, u16 w, u16 align,
				s16 offset, u16 slot_bytes,
				struct tcm_area *area)
{
	unsigned long pos;
	int ret;

	spin_lock(&(tcm->lock));
	ret = l2r_t2b(w, h, align, offset, &pos, slot_bytes, tcm->bitmap,
			tcm->map_size, tcm->width);

	if (!ret) {
		area->p0.x = pos % tcm->width;
		area->p0.y = pos / tcm->width;
		area->p1.x = area->p0.x + w - 1;
		area->p1.y = area->p0.y + h - 1;
	}
	spin_unlock(&(tcm->lock));

	return ret;
}

static void sita_deinit(struct tcm *tcm)
{
	kfree(tcm);
}

static s32 sita_free(struct tcm *tcm, struct tcm_area *area)
{
	unsigned long pos;
	u16 w, h;

	pos = area->p0.x + area->p0.y * tcm->width;
	if (area->is2d) {
		w = area->p1.x - area->p0.x + 1;
		h = area->p1.y - area->p0.y + 1;
	} else {
		w = area->p1.x + area->p1.y * tcm->width - pos + 1;
		h = 1;
	}

	spin_lock(&(tcm->lock));
	free_slots(pos, w, h, tcm->bitmap, tcm->width);
	spin_unlock(&(tcm->lock));
	return 0;
}

struct tcm *sita_init(u16 width, u16 height)
{
	struct tcm *tcm;
	size_t map_size = BITS_TO_LONGS(width*height) * sizeof(unsigned long);

	if (width == 0 || height == 0)
		return NULL;

	tcm = kzalloc(sizeof(*tcm) + map_size, GFP_KERNEL);
	if (!tcm)
		goto error;

	/* Updating the pointers to SiTA implementation APIs */
	tcm->height = height;
	tcm->width = width;
	tcm->reserve_2d = sita_reserve_2d;
	tcm->reserve_1d = sita_reserve_1d;
	tcm->free = sita_free;
	tcm->deinit = sita_deinit;

	spin_lock_init(&tcm->lock);
	tcm->bitmap = (unsigned long *)(tcm + 1);
	bitmap_clear(tcm->bitmap, 0, width*height);

	tcm->map_size = width*height;

	return tcm;

error:
	return NULL;
}
