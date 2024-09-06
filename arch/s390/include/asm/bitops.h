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
#include <asm/atomic_ops.h>
#include <asm/barrier.h>

#define __BITOPS_WORDS(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)

static inline unsigned long *
__bitops_word(unsigned long nr, const volatile unsigned long *ptr)
{
	unsigned long addr;

	addr = (unsigned long)ptr + ((nr ^ (nr & (BITS_PER_LONG - 1))) >> 3);
	return (unsigned long *)addr;
}

static inline unsigned long __bitops_mask(unsigned long nr)
{
	return 1UL << (nr & (BITS_PER_LONG - 1));
}

static __always_inline void arch_set_bit(unsigned long nr, volatile unsigned long *ptr)
{
	unsigned long *addr = __bitops_word(nr, ptr);
	unsigned long mask = __bitops_mask(nr);

	__atomic64_or(mask, (long *)addr);
}

static __always_inline void arch_clear_bit(unsigned long nr, volatile unsigned long *ptr)
{
	unsigned long *addr = __bitops_word(nr, ptr);
	unsigned long mask = __bitops_mask(nr);

	__atomic64_and(~mask, (long *)addr);
}

static __always_inline void arch_change_bit(unsigned long nr,
					    volatile unsigned long *ptr)
{
	unsigned long *addr = __bitops_word(nr, ptr);
	unsigned long mask = __bitops_mask(nr);

	__atomic64_xor(mask, (long *)addr);
}

static inline bool arch_test_and_set_bit(unsigned long nr,
					 volatile unsigned long *ptr)
{
	unsigned long *addr = __bitops_word(nr, ptr);
	unsigned long mask = __bitops_mask(nr);
	unsigned long old;

	old = __atomic64_or_barrier(mask, (long *)addr);
	return old & mask;
}

static inline bool arch_test_and_clear_bit(unsigned long nr,
					   volatile unsigned long *ptr)
{
	unsigned long *addr = __bitops_word(nr, ptr);
	unsigned long mask = __bitops_mask(nr);
	unsigned long old;

	old = __atomic64_and_barrier(~mask, (long *)addr);
	return old & mask;
}

static inline bool arch_test_and_change_bit(unsigned long nr,
					    volatile unsigned long *ptr)
{
	unsigned long *addr = __bitops_word(nr, ptr);
	unsigned long mask = __bitops_mask(nr);
	unsigned long old;

	old = __atomic64_xor_barrier(mask, (long *)addr);
	return old & mask;
}

static __always_inline void
arch___set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *p = __bitops_word(nr, addr);
	unsigned long mask = __bitops_mask(nr);

	*p |= mask;
}

static __always_inline void
arch___clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *p = __bitops_word(nr, addr);
	unsigned long mask = __bitops_mask(nr);

	*p &= ~mask;
}

static __always_inline void
arch___change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *p = __bitops_word(nr, addr);
	unsigned long mask = __bitops_mask(nr);

	*p ^= mask;
}

static __always_inline bool
arch___test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *p = __bitops_word(nr, addr);
	unsigned long mask = __bitops_mask(nr);
	unsigned long old;

	old = *p;
	*p |= mask;
	return old & mask;
}

static __always_inline bool
arch___test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *p = __bitops_word(nr, addr);
	unsigned long mask = __bitops_mask(nr);
	unsigned long old;

	old = *p;
	*p &= ~mask;
	return old & mask;
}

static __always_inline bool
arch___test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long *p = __bitops_word(nr, addr);
	unsigned long mask = __bitops_mask(nr);
	unsigned long old;

	old = *p;
	*p ^= mask;
	return old & mask;
}

#define arch_test_bit generic_test_bit
#define arch_test_bit_acquire generic_test_bit_acquire

static inline bool arch_test_and_set_bit_lock(unsigned long nr,
					      volatile unsigned long *ptr)
{
	if (arch_test_bit(nr, ptr))
		return true;
	return arch_test_and_set_bit(nr, ptr);
}

static inline void arch_clear_bit_unlock(unsigned long nr,
					 volatile unsigned long *ptr)
{
	smp_mb__before_atomic();
	arch_clear_bit(nr, ptr);
}

static inline void arch___clear_bit_unlock(unsigned long nr,
					   volatile unsigned long *ptr)
{
	smp_mb();
	arch___clear_bit(nr, ptr);
}

static inline bool arch_xor_unlock_is_negative_byte(unsigned long mask,
		volatile unsigned long *ptr)
{
	unsigned long old;

	old = __atomic64_xor_barrier(mask, (long *)ptr);
	return old & BIT(7);
}
#define arch_xor_unlock_is_negative_byte arch_xor_unlock_is_negative_byte

#include <asm-generic/bitops/instrumented-atomic.h>
#include <asm-generic/bitops/instrumented-non-atomic.h>
#include <asm-generic/bitops/instrumented-lock.h>

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
