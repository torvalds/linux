// SPDX-License-Identifier: GPL-2.0-only
/*
 * String functions optimized for hardware which doesn't
 * handle unaligned memory accesses efficiently.
 *
 * Copyright (C) 2021 Matteo Croce
 */

#include <linux/types.h>
#include <linux/module.h>

/* Minimum size for a word copy to be convenient */
#define BYTES_LONG	sizeof(long)
#define WORD_MASK	(BYTES_LONG - 1)
#define MIN_THRESHOLD	(BYTES_LONG * 2)

/* convenience union to avoid cast between different pointer types */
union types {
	u8 *as_u8;
	unsigned long *as_ulong;
	uintptr_t as_uptr;
};

union const_types {
	const u8 *as_u8;
	unsigned long *as_ulong;
	uintptr_t as_uptr;
};

void *memcpy(void *dest, const void *src, size_t count)
{
	union const_types s = { .as_u8 = src };
	union types d = { .as_u8 = dest };
	int distance = 0;

	if (count < MIN_THRESHOLD)
		goto copy_remainder;

	/* Copy a byte at time until destination is aligned. */
	for (; d.as_uptr & WORD_MASK; count--)
		*d.as_u8++ = *s.as_u8++;

	distance = s.as_uptr & WORD_MASK;

	if (distance) {
		unsigned long last, next;

		/*
		 * s is distance bytes ahead of d, and d just reached
		 * the alignment boundary. Move s backward to word align it
		 * and shift data to compensate for distance, in order to do
		 * word-by-word copy.
		 */
		s.as_u8 -= distance;

		next = s.as_ulong[0];
		for (; count >= BYTES_LONG; count -= BYTES_LONG) {
			last = next;
			next = s.as_ulong[1];

			d.as_ulong[0] = last >> (distance * 8) |
				next << ((BYTES_LONG - distance) * 8);

			d.as_ulong++;
			s.as_ulong++;
		}

		/* Restore s with the original offset. */
		s.as_u8 += distance;
	} else {
		/*
		 * If the source and dest lower bits are the same, do a simple
		 * 32/64 bit wide copy.
		 */
		for (; count >= BYTES_LONG; count -= BYTES_LONG)
			*d.as_ulong++ = *s.as_ulong++;
	}

copy_remainder:
	while (count--)
		*d.as_u8++ = *s.as_u8++;

	return dest;
}
EXPORT_SYMBOL(memcpy);

/*
 * Simply check if the buffer overlaps an call memcpy() in case,
 * otherwise do a simple one byte at time backward copy.
 */
void *memmove(void *dest, const void *src, size_t count)
{
	if (dest < src || src + count <= dest)
		return memcpy(dest, src, count);

	if (dest > src) {
		const char *s = src + count;
		char *tmp = dest + count;

		while (count--)
			*--tmp = *--s;
	}
	return dest;
}
EXPORT_SYMBOL(memmove);

void *memset(void *s, int c, size_t count)
{
	union types dest = { .as_u8 = s };

	if (count >= MIN_THRESHOLD) {
		unsigned long cu = (unsigned long)c;

		/* Compose an ulong with 'c' repeated 4/8 times */
		cu |= cu << 8;
		cu |= cu << 16;
		/* Suppress warning on 32 bit machines */
		cu |= (cu << 16) << 16;

		for (; count && dest.as_uptr & WORD_MASK; count--)
			*dest.as_u8++ = c;

		/* Copy using the largest size allowed */
		for (; count >= BYTES_LONG; count -= BYTES_LONG)
			*dest.as_ulong++ = cu;
	}

	/* copy the remainder */
	while (count--)
		*dest.as_u8++ = c;

	return s;
}
EXPORT_SYMBOL(memset);
