#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

/*
 * Word-at-a-time interfaces for PowerPC.
 */

#include <linux/kernel.h>
#include <asm/asm-compat.h>

#ifdef __BIG_ENDIAN__

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

#else

struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

#ifdef CONFIG_64BIT

/* Alan Modra's little-endian strlen tail for 64-bit */
#define create_zero_mask(mask) (mask)

static inline unsigned long find_zero(unsigned long mask)
{
	unsigned long leading_zero_bits;
	long trailing_zero_bit_mask;

	asm ("addi %1,%2,-1\n\t"
	     "andc %1,%1,%2\n\t"
	     "popcntd %0,%1"
	     : "=r" (leading_zero_bits), "=&r" (trailing_zero_bit_mask)
	     : "r" (mask));
	return leading_zero_bits >> 3;
}

#else	/* 32-bit case */

/*
 * This is largely generic for little-endian machines, but the
 * optimal byte mask counting is probably going to be something
 * that is architecture-specific. If you have a reliably fast
 * bit count instruction, that might be better than the multiply
 * and shift, for example.
 */

/* Carl Chatfield / Jan Achrenius G+ version for 32-bit */
static inline long count_masked_bytes(long mask)
{
	/* (000000 0000ff 00ffff ffffff) -> ( 1 1 2 3 ) */
	long a = (0x0ff0001+mask) >> 23;
	/* Fix the 1 for 00 case */
	return a & mask;
}

static inline unsigned long create_zero_mask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

static inline unsigned long find_zero(unsigned long mask)
{
	return count_masked_bytes(mask);
}

#endif

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

/* The mask we created is directly usable as a bytemask */
#define zero_bytemask(mask) (mask)

#endif

#endif /* _ASM_WORD_AT_A_TIME_H */
