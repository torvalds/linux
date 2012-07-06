#ifndef __ASM_ARM_WORD_AT_A_TIME_H
#define __ASM_ARM_WORD_AT_A_TIME_H

#ifndef __ARMEB__

/*
 * Little-endian word-at-a-time zero byte handling.
 * Heavily based on the x86 algorithm.
 */
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
	unsigned long ret;

#if __LINUX_ARM_ARCH__ >= 5
	/* We have clz available. */
	ret = fls(mask) >> 3;
#else
	/* (000000 0000ff 00ffff ffffff) -> ( 1 1 2 3 ) */
	ret = (0x0ff0001 + mask) >> 23;
	/* Fix the 1 for 00 case */
	ret &= mask;
#endif

	return ret;
}

#else	/* __ARMEB__ */
#include <asm-generic/word-at-a-time.h>
#endif

#endif /* __ASM_ARM_WORD_AT_A_TIME_H */
