#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

/*
 * Word-at-a-time interfaces for PowerPC.
 */

#include <linux/kernel.h>
#include <asm/asm-compat.h>
#include <asm/ppc_asm.h>

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

static inline unsigned long zero_bytemask(unsigned long mask)
{
	return ~1ul << __fls(mask);
}

#else

#ifdef CONFIG_64BIT

/* unused */
struct word_at_a_time {
};

#define WORD_AT_A_TIME_CONSTANTS { }

/* This will give us 0xff for a NULL char and 0x00 elsewhere */
static inline unsigned long has_zero(unsigned long a, unsigned long *bits, const struct word_at_a_time *c)
{
	unsigned long ret;
	unsigned long zero = 0;

	asm("cmpb %0,%1,%2" : "=r" (ret) : "r" (a), "r" (zero));
	*bits = ret;

	return ret;
}

static inline unsigned long prep_zero_mask(unsigned long a, unsigned long bits, const struct word_at_a_time *c)
{
	return bits;
}

/* Alan Modra's little-endian strlen tail for 64-bit */
static inline unsigned long create_zero_mask(unsigned long bits)
{
	unsigned long leading_zero_bits;
	long trailing_zero_bit_mask;

	asm("addi	%1,%2,-1\n\t"
	    "andc	%1,%1,%2\n\t"
	    "popcntd	%0,%1"
		: "=r" (leading_zero_bits), "=&r" (trailing_zero_bit_mask)
		: "b" (bits));

	return leading_zero_bits;
}

static inline unsigned long find_zero(unsigned long mask)
{
	return mask >> 3;
}

/* This assumes that we never ask for an all 1s bitmask */
static inline unsigned long zero_bytemask(unsigned long mask)
{
	return (1UL << mask) - 1;
}

#else	/* 32-bit case */

struct word_at_a_time {
	const unsigned long one_bits, high_bits;
};

#define WORD_AT_A_TIME_CONSTANTS { REPEAT_BYTE(0x01), REPEAT_BYTE(0x80) }

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

#endif /* CONFIG_64BIT */

#endif /* __BIG_ENDIAN__ */

/*
 * We use load_unaligned_zero() in a selftest, which builds a userspace
 * program. Some linker scripts seem to discard the .fixup section, so allow
 * the test code to use a different section name.
 */
#ifndef FIXUP_SECTION
#define FIXUP_SECTION ".fixup"
#endif

static inline unsigned long load_unaligned_zeropad(const void *addr)
{
	unsigned long ret, offset, tmp;

	asm(
	"1:	" PPC_LL "%[ret], 0(%[addr])\n"
	"2:\n"
	".section " FIXUP_SECTION ",\"ax\"\n"
	"3:	"
#ifdef __powerpc64__
	"clrrdi		%[tmp], %[addr], 3\n\t"
	"clrlsldi	%[offset], %[addr], 61, 3\n\t"
	"ld		%[ret], 0(%[tmp])\n\t"
#ifdef __BIG_ENDIAN__
	"sld		%[ret], %[ret], %[offset]\n\t"
#else
	"srd		%[ret], %[ret], %[offset]\n\t"
#endif
#else
	"clrrwi		%[tmp], %[addr], 2\n\t"
	"clrlslwi	%[offset], %[addr], 30, 3\n\t"
	"lwz		%[ret], 0(%[tmp])\n\t"
#ifdef __BIG_ENDIAN__
	"slw		%[ret], %[ret], %[offset]\n\t"
#else
	"srw		%[ret], %[ret], %[offset]\n\t"
#endif
#endif
	"b	2b\n"
	".previous\n"
	EX_TABLE(1b, 3b)
	: [tmp] "=&b" (tmp), [offset] "=&r" (offset), [ret] "=&r" (ret)
	: [addr] "b" (addr), "m" (*(unsigned long *)addr));

	return ret;
}

#undef FIXUP_SECTION

#endif /* _ASM_WORD_AT_A_TIME_H */
