/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

#include <linux/kernel.h>

/*
 * This is largely generic for little-endian machines, but the
 * optimal byte mask counting is probably going to be something
 * that is architecture-specific. If you have a reliably fast
 * bit count instruction, that might be better than the multiply
 * and shift, for example.
 */
struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

#ifdef CONFIG_64BIT

/*
 * Jan Achrenius on G+: microoptimized version of
 * the simpler "(mask & ONEBYTES) * ONEBYTES >> 56"
 * that works for the bytemasks without having to
 * mask them first.
 */
static inline long count_masked_bytes(unsigned long mask)
{
	return mask*0x0001020304050608ul >> 56;
}

#else	/* 32-bit case */

/* Carl Chatfield / Jan Achrenius G+ version for 32-bit */
static inline long count_masked_bytes(long mask)
{
	/* (000000 0000ff 00ffff ffffff) -> ( 1 1 2 3 ) */
	long a = (0x0ff0001+mask) >> 23;
	/* Fix the 1 for 00 case */
	return a & mask;
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

static inline unsigned long create_zero_mask(unsigned long bits)
{
	bits = (bits - 1) & ~bits;
	return bits >> 7;
}

/* The mask we created is directly usable as a bytemask */
#define zero_bytemask(mask) (mask)

static inline unsigned long find_zero(unsigned long mask)
{
	return count_masked_bytes(mask);
}

/*
 * Load an unaligned word from kernel space.
 *
 * In the (very unlikely) case of the word being a page-crosser
 * and the next page not being mapped, take the exception and
 * return zeroes in the non-existing part.
 */
#ifdef CONFIG_CC_HAS_ASM_GOTO_OUTPUT

static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long offset, data;
	unsigned long ret;

	asm_volatile_goto(
		"1:	mov %[mem], %[ret]\n"

		_ASM_EXTABLE(1b, %l[do_exception])

		: [ret] "=r" (ret)
		: [mem] "m" (*(unsigned long *)addr)
		: : do_exception);

	return ret;

do_exception:
	offset = (unsigned long)addr & (sizeof(long) - 1);
	addr = (void *)((unsigned long)addr & ~(sizeof(long) - 1));
	data = *(unsigned long *)addr;
	ret = data >> offset * 8;

	return ret;
}

#else /* !CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long offset, data;
	unsigned long ret, err = 0;

	asm(	"1:	mov %[mem], %[ret]\n"
		"2:\n"

		_ASM_EXTABLE_FAULT(1b, 2b)

		: [ret] "=&r" (ret), "+a" (err)
		: [mem] "m" (*(unsigned long *)addr));

	if (unlikely(err)) {
		offset = (unsigned long)addr & (sizeof(long) - 1);
		addr = (void *)((unsigned long)addr & ~(sizeof(long) - 1));
		data = *(unsigned long *)addr;
		ret = data >> offset * 8;
	}

	return ret;
}

#endif /* CONFIG_CC_HAS_ASM_GOTO_OUTPUT */

#endif /* _ASM_WORD_AT_A_TIME_H */
