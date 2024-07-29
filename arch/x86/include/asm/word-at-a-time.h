/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

#include <linux/bitops.h>
#include <linux/wordpart.h>

struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

/* Return nonzero if it has a zero */
static inline unsigned long has_zero(unsigned long a, unsigned long *bits, const struct word_at_a_time *c)
{
	unsigned long mask = ((a - c->one_bits) & ~a) & c->high_bits;
	*bits = mask;
	return mask;
}

static inline unsigned long prep_zero_mask(unsigned long a, unsigned long bits, const struct word_at_a_time *c)
{
	return bits;
}

#ifdef CONFIG_64BIT

/* Keep the initial has_zero() value for both bitmask and size calc */
#define create_zero_mask(bits) (bits)

static inline unsigned long zero_bytemask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

#define find_zero(bits) (__ffs(bits) >> 3)

#else

/* Create the final mask for both bytemask and size */
static inline unsigned long create_zero_mask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

/* The mask we created is directly usable as a bytemask */
#define zero_bytemask(mask) (mask)

/* Carl Chatfield / Jan Achrenius G+ version for 32-bit */
static inline unsigned long find_zero(unsigned long mask)
{
	/* (000000 0000ff 00ffff ffffff) -> ( 1 1 2 3 ) */
	long a = (0x0ff0001+mask) >> 23;
	/* Fix the 1 for 00 case */
	return a & mask;
}

#endif

/*
 * Load an unaligned word from kernel space.
 *
 * In the (very unlikely) case of the word being a page-crosser
 * and the next page not being mapped, take the exception and
 * return zeroes in the non-existing part.
 */
static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long ret;

	asm volatile(
		"1:	mov %[mem], %[ret]\n"
		"2:\n"
		_ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_ZEROPAD)
		: [ret] "=r" (ret)
		: [mem] "m" (*(unsigned long *)addr));

	return ret;
}

#endif /* _ASM_WORD_AT_A_TIME_H */
