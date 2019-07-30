/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_BITOPS_H
#define _ASM_X86_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 *
 * Note: inlines with more than a single statement should be marked
 * __always_inline to avoid problems with older gcc's inlining heuristics.
 */

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <asm/alternative.h>
#include <asm/rmwcc.h>
#include <asm/barrier.h>

#if BITS_PER_LONG == 32
# define _BITOPS_LONG_SHIFT 5
#elif BITS_PER_LONG == 64
# define _BITOPS_LONG_SHIFT 6
#else
# error "Unexpected BITS_PER_LONG"
#endif

#define BIT_64(n)			(U64_C(1) << (n))

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

#define RLONG_ADDR(x)			 "m" (*(volatile long *) (x))
#define WBYTE_ADDR(x)			"+m" (*(volatile char *) (x))

#define ADDR				RLONG_ADDR(addr)

/*
 * We do the locked ops that don't return the old value as
 * a mask operation on a byte.
 */
#define IS_IMMEDIATE(nr)		(__builtin_constant_p(nr))
#define CONST_MASK_ADDR(nr, addr)	WBYTE_ADDR((void *)(addr) + ((nr)>>3))
#define CONST_MASK(nr)			(1 << ((nr) & 7))

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __always_inline void
set_bit(long nr, volatile unsigned long *addr)
{
	if (IS_IMMEDIATE(nr)) {
		asm volatile(LOCK_PREFIX "orb %1,%0"
			: CONST_MASK_ADDR(nr, addr)
			: "iq" ((u8)CONST_MASK(nr))
			: "memory");
	} else {
		asm volatile(LOCK_PREFIX __ASM_SIZE(bts) " %1,%0"
			: : RLONG_ADDR(addr), "Ir" (nr) : "memory");
	}
}

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __always_inline void __set_bit(long nr, volatile unsigned long *addr)
{
	asm volatile(__ASM_SIZE(bts) " %1,%0" : : ADDR, "Ir" (nr) : "memory");
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
static __always_inline void
clear_bit(long nr, volatile unsigned long *addr)
{
	if (IS_IMMEDIATE(nr)) {
		asm volatile(LOCK_PREFIX "andb %1,%0"
			: CONST_MASK_ADDR(nr, addr)
			: "iq" ((u8)~CONST_MASK(nr)));
	} else {
		asm volatile(LOCK_PREFIX __ASM_SIZE(btr) " %1,%0"
			: : RLONG_ADDR(addr), "Ir" (nr) : "memory");
	}
}

/*
 * clear_bit_unlock - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and implies release semantics before the memory
 * operation. It can be used for an unlock.
 */
static __always_inline void clear_bit_unlock(long nr, volatile unsigned long *addr)
{
	barrier();
	clear_bit(nr, addr);
}

static __always_inline void __clear_bit(long nr, volatile unsigned long *addr)
{
	asm volatile(__ASM_SIZE(btr) " %1,%0" : : ADDR, "Ir" (nr) : "memory");
}

static __always_inline bool clear_bit_unlock_is_negative_byte(long nr, volatile unsigned long *addr)
{
	bool negative;
	asm volatile(LOCK_PREFIX "andb %2,%1"
		CC_SET(s)
		: CC_OUT(s) (negative), WBYTE_ADDR(addr)
		: "ir" ((char) ~(1 << nr)) : "memory");
	return negative;
}

// Let everybody know we have it
#define clear_bit_unlock_is_negative_byte clear_bit_unlock_is_negative_byte

/*
 * __clear_bit_unlock - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * __clear_bit() is non-atomic and implies release semantics before the memory
 * operation. It can be used for an unlock if no other CPUs can concurrently
 * modify other bits in the word.
 */
static __always_inline void __clear_bit_unlock(long nr, volatile unsigned long *addr)
{
	__clear_bit(nr, addr);
}

/**
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to change
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __always_inline void __change_bit(long nr, volatile unsigned long *addr)
{
	asm volatile(__ASM_SIZE(btc) " %1,%0" : : ADDR, "Ir" (nr) : "memory");
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __always_inline void change_bit(long nr, volatile unsigned long *addr)
{
	if (IS_IMMEDIATE(nr)) {
		asm volatile(LOCK_PREFIX "xorb %1,%0"
			: CONST_MASK_ADDR(nr, addr)
			: "iq" ((u8)CONST_MASK(nr)));
	} else {
		asm volatile(LOCK_PREFIX __ASM_SIZE(btc) " %1,%0"
			: : RLONG_ADDR(addr), "Ir" (nr) : "memory");
	}
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static __always_inline bool test_and_set_bit(long nr, volatile unsigned long *addr)
{
	return GEN_BINARY_RMWcc(LOCK_PREFIX __ASM_SIZE(bts), *addr, c, "Ir", nr);
}

/**
 * test_and_set_bit_lock - Set a bit and return its old value for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This is the same as test_and_set_bit on x86.
 */
static __always_inline bool
test_and_set_bit_lock(long nr, volatile unsigned long *addr)
{
	return test_and_set_bit(nr, addr);
}

/**
 * __test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __always_inline bool __test_and_set_bit(long nr, volatile unsigned long *addr)
{
	bool oldbit;

	asm(__ASM_SIZE(bts) " %2,%1"
	    CC_SET(c)
	    : CC_OUT(c) (oldbit)
	    : ADDR, "Ir" (nr) : "memory");
	return oldbit;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static __always_inline bool test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	return GEN_BINARY_RMWcc(LOCK_PREFIX __ASM_SIZE(btr), *addr, c, "Ir", nr);
}

/**
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 *
 * Note: the operation is performed atomically with respect to
 * the local CPU, but not other CPUs. Portable code should not
 * rely on this behaviour.
 * KVM relies on this behaviour on x86 for modifying memory that is also
 * accessed from a hypervisor on the same CPU if running in a VM: don't change
 * this without also updating arch/x86/kernel/kvm.c
 */
static __always_inline bool __test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	bool oldbit;

	asm volatile(__ASM_SIZE(btr) " %2,%1"
		     CC_SET(c)
		     : CC_OUT(c) (oldbit)
		     : ADDR, "Ir" (nr) : "memory");
	return oldbit;
}

/* WARNING: non atomic and it can be reordered! */
static __always_inline bool __test_and_change_bit(long nr, volatile unsigned long *addr)
{
	bool oldbit;

	asm volatile(__ASM_SIZE(btc) " %2,%1"
		     CC_SET(c)
		     : CC_OUT(c) (oldbit)
		     : ADDR, "Ir" (nr) : "memory");

	return oldbit;
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static __always_inline bool test_and_change_bit(long nr, volatile unsigned long *addr)
{
	return GEN_BINARY_RMWcc(LOCK_PREFIX __ASM_SIZE(btc), *addr, c, "Ir", nr);
}

static __always_inline bool constant_test_bit(long nr, const volatile unsigned long *addr)
{
	return ((1UL << (nr & (BITS_PER_LONG-1))) &
		(addr[nr >> _BITOPS_LONG_SHIFT])) != 0;
}

static __always_inline bool variable_test_bit(long nr, volatile const unsigned long *addr)
{
	bool oldbit;

	asm volatile(__ASM_SIZE(bt) " %2,%1"
		     CC_SET(c)
		     : CC_OUT(c) (oldbit)
		     : "m" (*(unsigned long *)addr), "Ir" (nr) : "memory");

	return oldbit;
}

#if 0 /* Fool kernel-doc since it doesn't do macros yet */
/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static bool test_bit(int nr, const volatile unsigned long *addr);
#endif

#define test_bit(nr, addr)			\
	(__builtin_constant_p((nr))		\
	 ? constant_test_bit((nr), (addr))	\
	 : variable_test_bit((nr), (addr)))

/**
 * __ffs - find first set bit in word
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static __always_inline unsigned long __ffs(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "rm" (word));
	return word;
}

/**
 * ffz - find first zero bit in word
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
static __always_inline unsigned long ffz(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "r" (~word));
	return word;
}

/*
 * __fls: find last set bit in word
 * @word: The word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static __always_inline unsigned long __fls(unsigned long word)
{
	asm("bsr %1,%0"
	    : "=r" (word)
	    : "rm" (word));
	return word;
}

#undef ADDR

#ifdef __KERNEL__
/**
 * ffs - find first set bit in word
 * @x: the word to search
 *
 * This is defined the same way as the libc and compiler builtin ffs
 * routines, therefore differs in spirit from the other bitops.
 *
 * ffs(value) returns 0 if value is 0 or the position of the first
 * set bit if value is nonzero. The first (least significant) bit
 * is at position 1.
 */
static __always_inline int ffs(int x)
{
	int r;

#ifdef CONFIG_X86_64
	/*
	 * AMD64 says BSFL won't clobber the dest reg if x==0; Intel64 says the
	 * dest reg is undefined if x==0, but their CPU architect says its
	 * value is written to set it to the same as before, except that the
	 * top 32 bits will be cleared.
	 *
	 * We cannot do this on 32 bits because at the very least some
	 * 486 CPUs did not behave this way.
	 */
	asm("bsfl %1,%0"
	    : "=r" (r)
	    : "rm" (x), "0" (-1));
#elif defined(CONFIG_X86_CMOV)
	asm("bsfl %1,%0\n\t"
	    "cmovzl %2,%0"
	    : "=&r" (r) : "rm" (x), "r" (-1));
#else
	asm("bsfl %1,%0\n\t"
	    "jnz 1f\n\t"
	    "movl $-1,%0\n"
	    "1:" : "=r" (r) : "rm" (x));
#endif
	return r + 1;
}

/**
 * fls - find last set bit in word
 * @x: the word to search
 *
 * This is defined in a similar way as the libc and compiler builtin
 * ffs, but returns the position of the most significant set bit.
 *
 * fls(value) returns 0 if value is 0 or the position of the last
 * set bit if value is nonzero. The last (most significant) bit is
 * at position 32.
 */
static __always_inline int fls(unsigned int x)
{
	int r;

#ifdef CONFIG_X86_64
	/*
	 * AMD64 says BSRL won't clobber the dest reg if x==0; Intel64 says the
	 * dest reg is undefined if x==0, but their CPU architect says its
	 * value is written to set it to the same as before, except that the
	 * top 32 bits will be cleared.
	 *
	 * We cannot do this on 32 bits because at the very least some
	 * 486 CPUs did not behave this way.
	 */
	asm("bsrl %1,%0"
	    : "=r" (r)
	    : "rm" (x), "0" (-1));
#elif defined(CONFIG_X86_CMOV)
	asm("bsrl %1,%0\n\t"
	    "cmovzl %2,%0"
	    : "=&r" (r) : "rm" (x), "rm" (-1));
#else
	asm("bsrl %1,%0\n\t"
	    "jnz 1f\n\t"
	    "movl $-1,%0\n"
	    "1:" : "=r" (r) : "rm" (x));
#endif
	return r + 1;
}

/**
 * fls64 - find last set bit in a 64-bit word
 * @x: the word to search
 *
 * This is defined in a similar way as the libc and compiler builtin
 * ffsll, but returns the position of the most significant set bit.
 *
 * fls64(value) returns 0 if value is 0 or the position of the last
 * set bit if value is nonzero. The last (most significant) bit is
 * at position 64.
 */
#ifdef CONFIG_X86_64
static __always_inline int fls64(__u64 x)
{
	int bitpos = -1;
	/*
	 * AMD64 says BSRQ won't clobber the dest reg if x==0; Intel64 says the
	 * dest reg is undefined if x==0, but their CPU architect says its
	 * value is written to set it to the same as before.
	 */
	asm("bsrq %1,%q0"
	    : "+r" (bitpos)
	    : "rm" (x));
	return bitpos + 1;
}
#else
#include <asm-generic/bitops/fls64.h>
#endif

#include <asm-generic/bitops/find.h>

#include <asm-generic/bitops/sched.h>

#include <asm/arch_hweight.h>

#include <asm-generic/bitops/const_hweight.h>

#include <asm-generic/bitops/le.h>

#include <asm-generic/bitops/ext2-atomic-setbit.h>

#endif /* __KERNEL__ */
#endif /* _ASM_X86_BITOPS_H */
