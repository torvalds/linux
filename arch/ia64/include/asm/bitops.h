/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_BITOPS_H
#define _ASM_IA64_BITOPS_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 02/06/02 find_next_bit() and find_first_bit() added from Erich Focht's ia64
 * O(1) scheduler patch
 */

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/intrinsics.h>
#include <asm/barrier.h>

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 *
 * The address must be (at least) "long" aligned.
 * Note that there are driver (e.g., eepro100) which use these operations to
 * operate on hw-defined data-structures, so we can't easily change these
 * operations to force a bigger alignment.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */
static __inline__ void
set_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = 1 << (nr & 31);
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old | bit;
	} while (cmpxchg_acq(m, old, new) != old);
}

/**
 * arch___set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __always_inline void
arch___set_bit(unsigned long nr, volatile unsigned long *addr)
{
	*((__u32 *) addr + (nr >> 5)) |= (1 << (nr & 31));
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_atomic() and/or smp_mb__after_atomic()
 * in order to ensure changes are visible on other processors.
 */
static __inline__ void
clear_bit (int nr, volatile void *addr)
{
	__u32 mask, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	mask = ~(1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old & mask;
	} while (cmpxchg_acq(m, old, new) != old);
}

/**
 * clear_bit_unlock - Clears a bit in memory with release
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit_unlock() is atomic and may not be reordered.  It does
 * contain a memory barrier suitable for unlock type operations.
 */
static __inline__ void
clear_bit_unlock (int nr, volatile void *addr)
{
	__u32 mask, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	mask = ~(1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old & mask;
	} while (cmpxchg_rel(m, old, new) != old);
}

/**
 * __clear_bit_unlock - Non-atomically clears a bit in memory with release
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * Similarly to clear_bit_unlock, the implementation uses a store
 * with release semantics. See also arch_spin_unlock().
 */
static __inline__ void
__clear_bit_unlock(int nr, void *addr)
{
	__u32 * const m = (__u32 *) addr + (nr >> 5);
	__u32 const new = *m & ~(1 << (nr & 31));

	ia64_st4_rel_nta(m, new);
}

/**
 * arch___clear_bit - Clears a bit in memory (non-atomic version)
 * @nr: the bit to clear
 * @addr: the address to start counting from
 *
 * Unlike clear_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __always_inline void
arch___clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	*((__u32 *) addr + (nr >> 5)) &= ~(1 << (nr & 31));
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to toggle
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __inline__ void
change_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = (1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old ^ bit;
	} while (cmpxchg_acq(m, old, new) != old);
}

/**
 * arch___change_bit - Toggle a bit in memory
 * @nr: the bit to toggle
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __always_inline void
arch___change_bit(unsigned long nr, volatile unsigned long *addr)
{
	*((__u32 *) addr + (nr >> 5)) ^= (1 << (nr & 31));
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies the acquisition side of the memory barrier.
 */
static __inline__ int
test_and_set_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = 1 << (nr & 31);
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old | bit;
	} while (cmpxchg_acq(m, old, new) != old);
	return (old & bit) != 0;
}

/**
 * test_and_set_bit_lock - Set a bit and return its old value for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This is the same as test_and_set_bit on ia64
 */
#define test_and_set_bit_lock test_and_set_bit

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
	__u32 *p = (__u32 *) addr + (nr >> 5);
	__u32 m = 1 << (nr & 31);
	int oldbitset = (*p & m) != 0;

	*p |= m;
	return oldbitset;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies the acquisition side of the memory barrier.
 */
static __inline__ int
test_and_clear_bit (int nr, volatile void *addr)
{
	__u32 mask, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	mask = ~(1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old & mask;
	} while (cmpxchg_acq(m, old, new) != old);
	return (old & ~mask) != 0;
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
	__u32 *p = (__u32 *) addr + (nr >> 5);
	__u32 m = 1 << (nr & 31);
	int oldbitset = (*p & m) != 0;

	*p &= ~m;
	return oldbitset;
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies the acquisition side of the memory barrier.
 */
static __inline__ int
test_and_change_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = (1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old ^ bit;
	} while (cmpxchg_acq(m, old, new) != old);
	return (old & bit) != 0;
}

/**
 * arch___test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 */
static __always_inline bool
arch___test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	__u32 old, bit = (1 << (nr & 31));
	__u32 *m = (__u32 *) addr + (nr >> 5);

	old = *m;
	*m = old ^ bit;
	return (old & bit) != 0;
}

#define arch_test_bit generic_test_bit
#define arch_test_bit_acquire generic_test_bit_acquire

/**
 * ffz - find the first zero bit in a long word
 * @x: The long word to find the bit in
 *
 * Returns the bit-number (0..63) of the first (least significant) zero bit.
 * Undefined if no zero exists, so code should check against ~0UL first...
 */
static inline unsigned long
ffz (unsigned long x)
{
	unsigned long result;

	result = ia64_popcnt(x & (~x - 1));
	return result;
}

/**
 * __ffs - find first bit in word.
 * @x: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static __inline__ unsigned long
__ffs (unsigned long x)
{
	unsigned long result;

	result = ia64_popcnt((x-1) & ~x);
	return result;
}

#ifdef __KERNEL__

/*
 * Return bit number of last (most-significant) bit set.  Undefined
 * for x==0.  Bits are numbered from 0..63 (e.g., ia64_fls(9) == 3).
 */
static inline unsigned long
ia64_fls (unsigned long x)
{
	long double d = x;
	long exp;

	exp = ia64_getf_exp(d);
	return exp - 0xffff;
}

/*
 * Find the last (most significant) bit set.  Returns 0 for x==0 and
 * bits are numbered from 1..32 (e.g., fls(9) == 4).
 */
static inline int fls(unsigned int t)
{
	unsigned long x = t & 0xffffffffu;

	if (!x)
		return 0;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return ia64_popcnt(x);
}

/*
 * Find the last (most significant) bit set.  Undefined for x==0.
 * Bits are numbered from 0..63 (e.g., __fls(9) == 3).
 */
static inline unsigned long
__fls (unsigned long x)
{
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x |= x >> 32;
	return ia64_popcnt(x) - 1;
}

#include <asm-generic/bitops/fls64.h>

#include <asm-generic/bitops/builtin-ffs.h>

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */
static __inline__ unsigned long __arch_hweight64(unsigned long x)
{
	unsigned long result;
	result = ia64_popcnt(x);
	return result;
}

#define __arch_hweight32(x) ((unsigned int) __arch_hweight64((x) & 0xfffffffful))
#define __arch_hweight16(x) ((unsigned int) __arch_hweight64((x) & 0xfffful))
#define __arch_hweight8(x)  ((unsigned int) __arch_hweight64((x) & 0xfful))

#include <asm-generic/bitops/const_hweight.h>

#endif /* __KERNEL__ */

#ifdef __KERNEL__

#include <asm-generic/bitops/non-instrumented-non-atomic.h>

#include <asm-generic/bitops/le.h>

#include <asm-generic/bitops/ext2-atomic-setbit.h>

#include <asm-generic/bitops/sched.h>

#endif /* __KERNEL__ */

#endif /* _ASM_IA64_BITOPS_H */
