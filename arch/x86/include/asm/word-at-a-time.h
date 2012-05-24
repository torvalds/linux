#ifndef _ASM_WORD_AT_A_TIME_H
#define _ASM_WORD_AT_A_TIME_H

/*
 * This is largely generic for little-endian machines, but the
 * optimal byte mask counting is probably going to be something
 * that is architecture-specific. If you have a reliably fast
 * bit count instruction, that might be better than the multiply
 * and shift, for example.
 */

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

#define REPEAT_BYTE(x)	((~0ul / 0xff) * (x))

/* Return the high bit set in the first byte that is a zero */
static inline unsigned long has_zero(unsigned long a)
{
	return ((a - REPEAT_BYTE(0x01)) & ~a) & REPEAT_BYTE(0x80);
}

#endif /* _ASM_WORD_AT_A_TIME_H */
