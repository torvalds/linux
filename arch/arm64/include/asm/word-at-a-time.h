/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 ARM Ltd.
 */
#ifndef __ASM_WORD_AT_A_TIME_H
#define __ASM_WORD_AT_A_TIME_H

#include <linux/uaccess.h>

#ifndef __AARCH64EB__

#include <linux/kernel.h>

struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

static inline unsigned long has_zero(unsigned long a, unsigned long *bits,
				     const struct word_at_a_time *c)
{
	unsigned long mask = ((a - c->one_bits) & ~a) & c->high_bits;
	*bits = mask;
	return mask;
}

#define prep_zero_mask(a, bits, c) (bits)

static inline unsigned long create_zero_mask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

static inline unsigned long find_zero(unsigned long mask)
{
	return fls64(mask) >> 3;
}

#define zero_bytemask(mask) (mask)

#else	/* __AARCH64EB__ */
#include <asm-generic/word-at-a-time.h>
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

	__mte_enable_tco_async();

	/* Load word from unaligned pointer addr */
	asm(
	"1:	ldr	%0, %2\n"
	"2:\n"
	_ASM_EXTABLE_LOAD_UNALIGNED_ZEROPAD(1b, 2b, %0, %1)
	: "=&r" (ret)
	: "r" (addr), "Q" (*(unsigned long *)addr));

	__mte_disable_tco_async();

	return ret;
}

#endif /* __ASM_WORD_AT_A_TIME_H */
