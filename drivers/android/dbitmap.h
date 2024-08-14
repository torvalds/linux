/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC
 *
 * dbitmap - dynamically sized bitmap library.
 *
 * Used by the binder driver to optimize the allocation of the smallest
 * available descriptor ID. Each bit in the bitmap represents the state
 * of an ID.
 *
 * A dbitmap can grow or shrink as needed. This part has been designed
 * considering that users might need to briefly release their locks in
 * order to allocate memory for the new bitmap. These operations then,
 * are verified to determine if the grow or shrink is sill valid.
 *
 * This library does not provide protection against concurrent access
 * by itself. Binder uses the proc->outer_lock for this purpose.
 */

#ifndef _LINUX_DBITMAP_H
#define _LINUX_DBITMAP_H
#include <linux/bitmap.h>

#define NBITS_MIN	BITS_PER_TYPE(unsigned long)

struct dbitmap {
	unsigned int nbits;
	unsigned long *map;
};

static inline int dbitmap_enabled(struct dbitmap *dmap)
{
	return !!dmap->nbits;
}

static inline void dbitmap_free(struct dbitmap *dmap)
{
	dmap->nbits = 0;
	kfree(dmap->map);
}

/* Returns the nbits that a dbitmap can shrink to, 0 if not possible. */
static inline unsigned int dbitmap_shrink_nbits(struct dbitmap *dmap)
{
	unsigned int bit;

	if (dmap->nbits <= NBITS_MIN)
		return 0;

	/*
	 * Determine if the bitmap can shrink based on the position of
	 * its last set bit. If the bit is within the first quarter of
	 * the bitmap then shrinking is possible. In this case, the
	 * bitmap should shrink to half its current size.
	 */
	bit = find_last_bit(dmap->map, dmap->nbits);
	if (bit < (dmap->nbits >> 2))
		return dmap->nbits >> 1;

	/* find_last_bit() returns dmap->nbits when no bits are set. */
	if (bit == dmap->nbits)
		return NBITS_MIN;

	return 0;
}

/* Replace the internal bitmap with a new one of different size */
static inline void
dbitmap_replace(struct dbitmap *dmap, unsigned long *new, unsigned int nbits)
{
	bitmap_copy(new, dmap->map, min(dmap->nbits, nbits));
	kfree(dmap->map);
	dmap->map = new;
	dmap->nbits = nbits;
}

static inline void
dbitmap_shrink(struct dbitmap *dmap, unsigned long *new, unsigned int nbits)
{
	if (!new)
		return;

	/*
	 * Verify that shrinking to @nbits is still possible. The @new
	 * bitmap might have been allocated without locks, so this call
	 * could now be outdated. In this case, free @new and move on.
	 */
	if (!dbitmap_enabled(dmap) || dbitmap_shrink_nbits(dmap) != nbits) {
		kfree(new);
		return;
	}

	dbitmap_replace(dmap, new, nbits);
}

/* Returns the nbits that a dbitmap can grow to. */
static inline unsigned int dbitmap_grow_nbits(struct dbitmap *dmap)
{
	return dmap->nbits << 1;
}

static inline void
dbitmap_grow(struct dbitmap *dmap, unsigned long *new, unsigned int nbits)
{
	/*
	 * Verify that growing to @nbits is still possible. The @new
	 * bitmap might have been allocated without locks, so this call
	 * could now be outdated. In this case, free @new and move on.
	 */
	if (!dbitmap_enabled(dmap) || nbits <= dmap->nbits) {
		kfree(new);
		return;
	}

	/*
	 * Check for ENOMEM after confirming the grow operation is still
	 * required. This ensures we only disable the dbitmap when it's
	 * necessary. Once the dbitmap is disabled, binder will fallback
	 * to slow_desc_lookup_olocked().
	 */
	if (!new) {
		dbitmap_free(dmap);
		return;
	}

	dbitmap_replace(dmap, new, nbits);
}

/*
 * Finds and sets the next zero bit in the bitmap. Upon success @bit
 * is populated with the index and 0 is returned. Otherwise, -ENOSPC
 * is returned to indicate that a dbitmap_grow() is needed.
 */
static inline int
dbitmap_acquire_next_zero_bit(struct dbitmap *dmap, unsigned long offset,
			      unsigned long *bit)
{
	unsigned long n;

	n = find_next_zero_bit(dmap->map, dmap->nbits, offset);
	if (n == dmap->nbits)
		return -ENOSPC;

	*bit = n;
	set_bit(n, dmap->map);

	return 0;
}

static inline void
dbitmap_clear_bit(struct dbitmap *dmap, unsigned long bit)
{
	clear_bit(bit, dmap->map);
}

static inline int dbitmap_init(struct dbitmap *dmap)
{
	dmap->map = bitmap_zalloc(NBITS_MIN, GFP_KERNEL);
	if (!dmap->map) {
		dmap->nbits = 0;
		return -ENOMEM;
	}

	dmap->nbits = NBITS_MIN;

	return 0;
}
#endif
