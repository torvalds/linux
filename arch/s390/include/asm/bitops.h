/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 1999,2013
 *
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *
 * The description below was taken in large parts from the powerpc
 * bitops header file:
 * Within a word, bits are numbered LSB first.  Lot's of places make
 * this assumption by directly testing bits with (val & (1<<nr)).
 * This can cause confusion for large (> 1 word) bitmaps on a
 * big-endian system because, unlike little endian, the number of each
 * bit depends on the word size.
 *
 * The bitop functions are defined to work on unsigned longs, so the bits
 * end up numbered:
 *   |63..............0|127............64|191...........128|255...........192|
 *
 * We also have special functions which work with an MSB0 encoding.
 * The bits are numbered:
 *   |0..............63|64............127|128...........191|192...........255|
 *
 * The main difference is that bit 0-63 in the bit number field needs to be
 * reversed compared to the LSB0 encoded bit fields. This can be achieved by
 * XOR with 0x3f.
 *
 */

#ifndef _S390_BITOPS_H
#define _S390_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/typecheck.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/asm.h>

#define arch___set_bit			generic___set_bit
#define arch___clear_bit		generic___clear_bit
#define arch___change_bit		generic___change_bit
#define arch___test_and_set_bit		generic___test_and_set_bit
#define arch___test_and_clear_bit	generic___test_and_clear_bit
#define arch___test_and_change_bit	generic___test_and_change_bit
#define arch_test_bit_acquire		generic_test_bit_acquire

static __always_inline bool arch_test_bit(unsigned long nr, const volatile unsigned long *ptr)
{
#ifdef __HAVE_ASM_FLAG_OUTPUTS__
	const volatile unsigned char *addr;
	unsigned long mask;
	int cc;

	/*
	 * With CONFIG_PROFILE_ALL_BRANCHES enabled gcc fails to
	 * handle __builtin_constant_p() in some cases.
	 */
	if (!IS_ENABLED(CONFIG_PROFILE_ALL_BRANCHES) && __builtin_constant_p(nr)) {
		addr = (const volatile unsigned char *)ptr;
		addr += (nr ^ (BITS_PER_LONG - BITS_PER_BYTE)) / BITS_PER_BYTE;
		mask = 1UL << (nr & (BITS_PER_BYTE - 1));
		asm volatile(
			"	tm	%[addr],%[mask]\n"
			: "=@cc" (cc)
			: [addr] "Q" (*addr), [mask] "I" (mask)
			);
		return cc == 3;
	}
#endif
	return generic_test_bit(nr, ptr);
}

#include <asm-generic/bitops/atomic.h>
#include <asm-generic/bitops/non-instrumented-non-atomic.h>
#include <asm-generic/bitops/lock.h>

/*
 * Functions which use MSB0 bit numbering.
 * The bits are numbered:
 *   |0..............63|64............127|128...........191|192...........255|
 */
unsigned long find_first_bit_inv(const unsigned long *addr, unsigned long size);
unsigned long find_next_bit_inv(const unsigned long *addr, unsigned long size,
				unsigned long offset);

#define for_each_set_bit_inv(bit, addr, size)				\
	for ((bit) = find_first_bit_inv((addr), (size));		\
	     (bit) < (size);						\
	     (bit) = find_next_bit_inv((addr), (size), (bit) + 1))

static inline void set_bit_inv(unsigned long nr, volatile unsigned long *ptr)
{
	return set_bit(nr ^ (BITS_PER_LONG - 1), ptr);
}

static inline void clear_bit_inv(unsigned long nr, volatile unsigned long *ptr)
{
	return clear_bit(nr ^ (BITS_PER_LONG - 1), ptr);
}

static inline bool test_and_clear_bit_inv(unsigned long nr,
					  volatile unsigned long *ptr)
{
	return test_and_clear_bit(nr ^ (BITS_PER_LONG - 1), ptr);
}

static inline void __set_bit_inv(unsigned long nr, volatile unsigned long *ptr)
{
	return __set_bit(nr ^ (BITS_PER_LONG - 1), ptr);
}

static inline void __clear_bit_inv(unsigned long nr, volatile unsigned long *ptr)
{
	return __clear_bit(nr ^ (BITS_PER_LONG - 1), ptr);
}

static inline bool test_bit_inv(unsigned long nr,
				const volatile unsigned long *ptr)
{
	return test_bit(nr ^ (BITS_PER_LONG - 1), ptr);
}

/**
 * __flogr - find leftmost one
 * @word - The word to search
 *
 * Returns the bit number of the most significant bit set,
 * where the most significant bit has bit number 0.
 * If no bit is set this function returns 64.
 */
static inline unsigned char __flogr(unsigned long word)
{
	if (__builtin_constant_p(word)) {
		unsigned long bit = 0;

		if (!word)
			return 64;
		if (!(word & 0xffffffff00000000UL)) {
			word <<= 32;
			bit += 32;
		}
		if (!(word & 0xffff000000000000UL)) {
			word <<= 16;
			bit += 16;
		}
		if (!(word & 0xff00000000000000UL)) {
			word <<= 8;
			bit += 8;
		}
		if (!(word & 0xf000000000000000UL)) {
			word <<= 4;
			bit += 4;
		}
		if (!(word & 0xc000000000000000UL)) {
			word <<= 2;
			bit += 2;
		}
		if (!(word & 0x8000000000000000UL)) {
			word <<= 1;
			bit += 1;
		}
		return bit;
	} else {
		union register_pair rp;

		rp.even = word;
		asm volatile(
			"       flogr   %[rp],%[rp]\n"
			: [rp] "+d" (rp.pair) : : "cc");
		return rp.even;
	}
}

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word)
{
	return __flogr(-word & word) ^ (BITS_PER_LONG - 1);
}

/**
 * ffs - find first bit set
 * @word: the word to search
 *
 * This is defined the same way as the libc and
 * compiler builtin ffs routines (man ffs).
 */
static inline int ffs(int word)
{
	unsigned long mask = 2 * BITS_PER_LONG - 1;
	unsigned int val = (unsigned int)word;

	return (1 + (__flogr(-val & val) ^ (BITS_PER_LONG - 1))) & mask;
}

/**
 * __fls - find last (most-significant) set bit in a long word
 * @word: the word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static inline unsigned long __fls(unsigned long word)
{
	return __flogr(word) ^ (BITS_PER_LONG - 1);
}

/**
 * fls64 - find last set bit in a 64-bit word
 * @word: the word to search
 *
 * This is defined in a similar way as the libc and compiler builtin
 * ffsll, but returns the position of the most significant set bit.
 *
 * fls64(value) returns 0 if value is 0 or the position of the last
 * set bit if value is nonzero. The last (most significant) bit is
 * at position 64.
 */
static inline int fls64(unsigned long word)
{
	unsigned long mask = 2 * BITS_PER_LONG - 1;

	return (1 + (__flogr(word) ^ (BITS_PER_LONG - 1))) & mask;
}

/**
 * fls - find last (most-significant) bit set
 * @word: the word to search
 *
 * This is defined the same way as ffs.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */
static inline int fls(unsigned int word)
{
	return fls64(word);
}

#include <asm/arch_hweight.h>
#include <asm-generic/bitops/const_hweight.h>
#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic-setbit.h>

#endif /* _S390_BITOPS_H */
