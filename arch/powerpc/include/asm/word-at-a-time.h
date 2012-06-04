#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

/*
 * Word-at-a-time interfaces for PowerPC.
 */

#include <linux/kernel.h>
#include <asm/asm-compat.h>

struct word_at_a_time {
	const unsigned long high_bits, low_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0xfe) + 1, REPEAT_BYTE(0x7f) }

/* Bit set in the bytes that have a zero */
static inline long prep_zero_mask(unsigned long val, unsigned long rhs, const struct word_at_a_time *c)
{
	unsigned long mask = (val & c->low_bits) + c->low_bits;
	return ~(mask | rhs);
}

#define create_zero_mask(mask) (mask)

static inline long find_zero(unsigned long mask)
{
	long leading_zero_bits;

	asm (PPC_CNTLZL "%0,%1" : "=r" (leading_zero_bits) : "r" (mask));
	return leading_zero_bits >> 3;
}

static inline bool has_zero(unsigned long val, unsigned long *data, const struct word_at_a_time *c)
{
	unsigned long rhs = val | c->low_bits;
	*data = rhs;
	return (val + c->high_bits) & ~rhs;
}

#endif /* _ASM_WORD_AT_A_TIME_H */
