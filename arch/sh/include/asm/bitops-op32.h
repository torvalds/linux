/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_BITOPS_OP32_H
#define __ASM_SH_BITOPS_OP32_H

#include <linux/bits.h>

/*
 * The bit modifying instructions on SH-2A are only capable of working
 * with a 3-bit immediate, which signifies the shift position for the bit
 * being worked on.
 */
#if defined(__BIG_ENDIAN)
#define BITOP_LE_SWIZZLE	((BITS_PER_LONG-1) & ~0x7)
#define BYTE_NUMBER(nr)		((nr ^ BITOP_LE_SWIZZLE) / BITS_PER_BYTE)
#define BYTE_OFFSET(nr)		((nr ^ BITOP_LE_SWIZZLE) % BITS_PER_BYTE)
#else
#define BYTE_NUMBER(nr)		((nr) / BITS_PER_BYTE)
#define BYTE_OFFSET(nr)		((nr) % BITS_PER_BYTE)
#endif

static __always_inline void
arch___set_bit(unsigned long nr, volatile unsigned long *addr)
{
	if (__builtin_constant_p(nr)) {
		__asm__ __volatile__ (
			"bset.b %1, @(%O2,%0)		! __set_bit\n\t"
			: "+r" (addr)
			: "i" (BYTE_OFFSET(nr)), "i" (BYTE_NUMBER(nr))
			: "t", "memory"
		);
	} else {
		unsigned long mask = BIT_MASK(nr);
		unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

		*p |= mask;
	}
}

static __always_inline void
arch___clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	if (__builtin_constant_p(nr)) {
		__asm__ __volatile__ (
			"bclr.b %1, @(%O2,%0)		! __clear_bit\n\t"
			: "+r" (addr)
			: "i" (BYTE_OFFSET(nr)),
			  "i" (BYTE_NUMBER(nr))
			: "t", "memory"
		);
	} else {
		unsigned long mask = BIT_MASK(nr);
		unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

		*p &= ~mask;
	}
}

/**
 * arch___change_bit - Toggle a bit in memory
 * @nr: the bit to change
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __always_inline void
arch___change_bit(unsigned long nr, volatile unsigned long *addr)
{
	if (__builtin_constant_p(nr)) {
		__asm__ __volatile__ (
			"bxor.b %1, @(%O2,%0)		! __change_bit\n\t"
			: "+r" (addr)
			: "i" (BYTE_OFFSET(nr)),
			  "i" (BYTE_NUMBER(nr))
			: "t", "memory"
		);
	} else {
		unsigned long mask = BIT_MASK(nr);
		unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

		*p ^= mask;
	}
}

/**
 * arch___test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __always_inline bool
arch___test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old = *p;

	*p = old | mask;
	return (old & mask) != 0;
}

/**
 * arch___test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __always_inline bool
arch___test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old = *p;

	*p = old & ~mask;
	return (old & mask) != 0;
}

/* WARNING: non atomic and it can be reordered! */
static __always_inline bool
arch___test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old = *p;

	*p = old ^ mask;
	return (old & mask) != 0;
}

#define arch_test_bit generic_test_bit
#define arch_test_bit_acquire generic_test_bit_acquire

#include <asm-generic/bitops/non-instrumented-non-atomic.h>

#endif /* __ASM_SH_BITOPS_OP32_H */
