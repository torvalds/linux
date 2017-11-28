/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 * Derived from arch/x86/include/asm/word-at-a-time.h
 */

#ifndef _ASM_RISCV_WORD_AT_A_TIME_H
#define _ASM_RISCV_WORD_AT_A_TIME_H


#include <linux/kernel.h>

struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

static inline unsigned long has_zero(unsigned long val,
	unsigned long *bits, const struct word_at_a_time *c)
{
	unsigned long mask = ((val - c->one_bits) & ~val) & c->high_bits;
	*bits = mask;
	return mask;
}

static inline unsigned long prep_zero_mask(unsigned long val,
	unsigned long bits, const struct word_at_a_time *c)
{
	return bits;
}

static inline unsigned long create_zero_mask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

static inline unsigned long find_zero(unsigned long mask)
{
	return fls64(mask) >> 3;
}

/* The mask we created is directly usable as a bytemask */
#define zero_bytemask(mask) (mask)

#endif /* _ASM_RISCV_WORD_AT_A_TIME_H */
