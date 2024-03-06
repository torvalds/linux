/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

#include <linux/kernel.h>
#include <asm/asm-extable.h>
#include <asm/bitsperlong.h>

struct word_at_a_time {
	const unsigned long bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x7f) }

static inline unsigned long prep_zero_mask(unsigned long val, unsigned long data, const struct word_at_a_time *c)
{
	return data;
}

static inline unsigned long create_zero_mask(unsigned long data)
{
	return __fls(data);
}

static inline unsigned long find_zero(unsigned long data)
{
	return (data ^ (BITS_PER_LONG - 1)) >> 3;
}

static inline unsigned long has_zero(unsigned long val, unsigned long *data, const struct word_at_a_time *c)
{
	unsigned long mask = (val & c->bits) + c->bits;

	*data = ~(mask | val | c->bits);
	return *data;
}

static inline unsigned long zero_bytemask(unsigned long data)
{
	return ~1UL << data;
}

/*
 * Load an unaligned word from kernel space.
 *
 * In the (very unlikely) case of the word being a page-crosser
 * and the next page not being mapped, take the exception and
 * return zeroes in the non-existing part.
 */
static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long data;

	asm volatile(
		"0:	lg	%[data],0(%[addr])\n"
		"1:	nopr	%%r7\n"
		EX_TABLE_ZEROPAD(0b, 1b, %[data], %[addr])
		EX_TABLE_ZEROPAD(1b, 1b, %[data], %[addr])
		: [data] "=d" (data)
		: [addr] "a" (addr), "m" (*(unsigned long *)addr));
	return data;
}

#endif /* _ASM_WORD_AT_A_TIME_H */
