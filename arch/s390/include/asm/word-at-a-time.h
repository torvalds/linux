/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

#include <linux/kernel.h>
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

#endif /* _ASM_WORD_AT_A_TIME_H */
