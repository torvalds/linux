// SPDX-License-Identifier: GPL-2.0
/*
 * arch/openrisc/lib/memcpy.c
 *
 * Optimized memory copy routines for openrisc.  These are mostly copied
 * from ohter sources but slightly entended based on ideas discuassed in
 * #openrisc.
 *
 * The word unroll implementation is an extension to the arm byte
 * unrolled implementation, but using word copies (if things are
 * properly aligned)
 *
 * The great arm loop unroll algorithm can be found at:
 *  arch/arm/boot/compressed/string.c
 */

#include <linux/export.h>

#include <linux/string.h>

#ifdef CONFIG_OR1K_1200
/*
 * Do memcpy with word copies and loop unrolling. This gives the
 * best performance on the OR1200 and MOR1KX archirectures
 */
void *memcpy(void *dest, __const void *src, __kernel_size_t n)
{
	int i = 0;
	unsigned char *d, *s;
	uint32_t *dest_w = (uint32_t *)dest, *src_w = (uint32_t *)src;

	/* If both source and dest are word aligned copy words */
	if (!((unsigned int)dest_w & 3) && !((unsigned int)src_w & 3)) {
		/* Copy 32 bytes per loop */
		for (i = n >> 5; i > 0; i--) {
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
		}

		if (n & 1 << 4) {
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
		}

		if (n & 1 << 3) {
			*dest_w++ = *src_w++;
			*dest_w++ = *src_w++;
		}

		if (n & 1 << 2)
			*dest_w++ = *src_w++;

		d = (unsigned char *)dest_w;
		s = (unsigned char *)src_w;

	} else {
		d = (unsigned char *)dest_w;
		s = (unsigned char *)src_w;

		for (i = n >> 3; i > 0; i--) {
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
		}

		if (n & 1 << 2) {
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
			*d++ = *s++;
		}
	}

	if (n & 1 << 1) {
		*d++ = *s++;
		*d++ = *s++;
	}

	if (n & 1)
		*d++ = *s++;

	return dest;
}
#else
/*
 * Use word copies but no loop unrolling as we cannot assume there
 * will be benefits on the archirecture
 */
void *memcpy(void *dest, __const void *src, __kernel_size_t n)
{
	unsigned char *d = (unsigned char *)dest, *s = (unsigned char *)src;
	uint32_t *dest_w = (uint32_t *)dest, *src_w = (uint32_t *)src;

	/* If both source and dest are word aligned copy words */
	if (!((unsigned int)dest_w & 3) && !((unsigned int)src_w & 3)) {
		for (; n >= 4; n -= 4)
			*dest_w++ = *src_w++;
	}

	d = (unsigned char *)dest_w;
	s = (unsigned char *)src_w;

	/* For remaining or if not aligned, copy bytes */
	for (; n >= 1; n -= 1)
		*d++ = *s++;

	return dest;

}
#endif

EXPORT_SYMBOL(memcpy);
