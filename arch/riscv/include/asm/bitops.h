/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_BITOPS_H
#define _ASM_RISCV_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error "Only <linux/bitops.h> can be included directly"
#endif /* _LINUX_BITOPS_H */

#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <asm/barrier.h>
#include <asm/bitsperlong.h>

#if !defined(CONFIG_RISCV_ISA_ZBB) || defined(NO_ALTERNATIVE)
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/fls.h>

#else
#define __HAVE_ARCH___FFS
#define __HAVE_ARCH___FLS
#define __HAVE_ARCH_FFS
#define __HAVE_ARCH_FLS

#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/__fls.h>
#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/fls.h>

#include <asm/alternative-macros.h>
#include <asm/hwcap.h>

#if (BITS_PER_LONG == 64)
#define CTZW	"ctzw "
#define CLZW	"clzw "
#elif (BITS_PER_LONG == 32)
#define CTZW	"ctz "
#define CLZW	"clz "
#else
#error "Unexpected BITS_PER_LONG"
#endif

static __always_inline unsigned long variable__ffs(unsigned long word)
{
	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      "ctz %0, %1\n"
		      ".option pop\n"
		      : "=r" (word) : "r" (word) :);

	return word;

legacy:
	return generic___ffs(word);
}

/**
 * __ffs - find first set bit in a long word
 * @word: The word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
#define __ffs(word)				\
	(__builtin_constant_p(word) ?		\
	 (unsigned long)__builtin_ctzl(word) :	\
	 variable__ffs(word))

static __always_inline unsigned long variable__fls(unsigned long word)
{
	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      "clz %0, %1\n"
		      ".option pop\n"
		      : "=r" (word) : "r" (word) :);

	return BITS_PER_LONG - 1 - word;

legacy:
	return generic___fls(word);
}

/**
 * __fls - find last set bit in a long word
 * @word: the word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
#define __fls(word)							\
	(__builtin_constant_p(word) ?					\
	 (unsigned long)(BITS_PER_LONG - 1 - __builtin_clzl(word)) :	\
	 variable__fls(word))

static __always_inline int variable_ffs(int x)
{
	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	if (!x)
		return 0;

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      CTZW "%0, %1\n"
		      ".option pop\n"
		      : "=r" (x) : "r" (x) :);

	return x + 1;

legacy:
	return generic_ffs(x);
}

/**
 * ffs - find first set bit in a word
 * @x: the word to search
 *
 * This is defined the same way as the libc and compiler builtin ffs routines.
 *
 * ffs(value) returns 0 if value is 0 or the position of the first set bit if
 * value is nonzero. The first (least significant) bit is at position 1.
 */
#define ffs(x) (__builtin_constant_p(x) ? __builtin_ffs(x) : variable_ffs(x))

static __always_inline int variable_fls(unsigned int x)
{
	asm goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	if (!x)
		return 0;

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      CLZW "%0, %1\n"
		      ".option pop\n"
		      : "=r" (x) : "r" (x) :);

	return 32 - x;

legacy:
	return generic_fls(x);
}

/**
 * fls - find last set bit in a word
 * @x: the word to search
 *
 * This is defined in a similar way as ffs, but returns the position of the most
 * significant set bit.
 *
 * fls(value) returns 0 if value is 0 or the position of the last set bit if
 * value is nonzero. The last (most significant) bit is at position 32.
 */
#define fls(x)							\
({								\
	typeof(x) x_ = (x);					\
	__builtin_constant_p(x_) ?				\
	 ((x_ != 0) ? (32 - __builtin_clz(x_)) : 0)		\
	 :							\
	 variable_fls(x_);					\
})

#endif /* !defined(CONFIG_RISCV_ISA_ZBB) || defined(NO_ALTERNATIVE) */

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>

#include <asm/arch_hweight.h>

#include <asm-generic/bitops/const_hweight.h>

#if (BITS_PER_LONG == 64)
#define __AMO(op)	"amo" #op ".d"
#elif (BITS_PER_LONG == 32)
#define __AMO(op)	"amo" #op ".w"
#else
#error "Unexpected BITS_PER_LONG"
#endif

#define __test_and_op_bit_ord(op, mod, nr, addr, ord)		\
({								\
	unsigned long __res, __mask;				\
	__mask = BIT_MASK(nr);					\
	__asm__ __volatile__ (					\
		__AMO(op) #ord " %0, %2, %1"			\
		: "=r" (__res), "+A" (addr[BIT_WORD(nr)])	\
		: "r" (mod(__mask))				\
		: "memory");					\
	((__res & __mask) != 0);				\
})

#define __op_bit_ord(op, mod, nr, addr, ord)			\
	__asm__ __volatile__ (					\
		__AMO(op) #ord " zero, %1, %0"			\
		: "+A" (addr[BIT_WORD(nr)])			\
		: "r" (mod(BIT_MASK(nr)))			\
		: "memory");

#define __test_and_op_bit(op, mod, nr, addr) 			\
	__test_and_op_bit_ord(op, mod, nr, addr, .aqrl)
#define __op_bit(op, mod, nr, addr)				\
	__op_bit_ord(op, mod, nr, addr, )

/* Bitmask modifiers */
#define __NOP(x)	(x)
#define __NOT(x)	(~(x))

/**
 * arch_test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation may be reordered on other architectures than x86.
 */
static __always_inline int arch_test_and_set_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(or, __NOP, nr, addr);
}

/**
 * arch_test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation can be reordered on other architectures other than x86.
 */
static __always_inline int arch_test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(and, __NOT, nr, addr);
}

/**
 * arch_test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static __always_inline int arch_test_and_change_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(xor, __NOP, nr, addr);
}

/**
 * arch_set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __always_inline void arch_set_bit(int nr, volatile unsigned long *addr)
{
	__op_bit(or, __NOP, nr, addr);
}

/**
 * arch_clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 */
static __always_inline void arch_clear_bit(int nr, volatile unsigned long *addr)
{
	__op_bit(and, __NOT, nr, addr);
}

/**
 * arch_change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit()  may be reordered on other architectures than x86.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __always_inline void arch_change_bit(int nr, volatile unsigned long *addr)
{
	__op_bit(xor, __NOP, nr, addr);
}

/**
 * arch_test_and_set_bit_lock - Set a bit and return its old value, for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and provides acquire barrier semantics.
 * It can be used to implement bit locks.
 */
static __always_inline int arch_test_and_set_bit_lock(
	unsigned long nr, volatile unsigned long *addr)
{
	return __test_and_op_bit_ord(or, __NOP, nr, addr, .aq);
}

/**
 * arch_clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This operation is atomic and provides release barrier semantics.
 */
static __always_inline void arch_clear_bit_unlock(
	unsigned long nr, volatile unsigned long *addr)
{
	__op_bit_ord(and, __NOT, nr, addr, .rl);
}

/**
 * arch___clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This operation is like clear_bit_unlock, however it is not atomic.
 * It does provide release barrier semantics so it can be used to unlock
 * a bit lock, however it would only be used if no other CPU can modify
 * any bits in the memory until the lock is released (a good example is
 * if the bit lock itself protects access to the other bits in the word).
 *
 * On RISC-V systems there seems to be no benefit to taking advantage of the
 * non-atomic property here: it's a lot more instructions and we still have to
 * provide release semantics anyway.
 */
static __always_inline void arch___clear_bit_unlock(
	unsigned long nr, volatile unsigned long *addr)
{
	arch_clear_bit_unlock(nr, addr);
}

static __always_inline bool arch_xor_unlock_is_negative_byte(unsigned long mask,
		volatile unsigned long *addr)
{
	unsigned long res;
	__asm__ __volatile__ (
		__AMO(xor) ".rl %0, %2, %1"
		: "=r" (res), "+A" (*addr)
		: "r" (__NOP(mask))
		: "memory");
	return (res & BIT(7)) != 0;
}

#undef __test_and_op_bit
#undef __op_bit
#undef __NOP
#undef __NOT
#undef __AMO

#include <asm-generic/bitops/instrumented-atomic.h>
#include <asm-generic/bitops/instrumented-lock.h>

#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif /* _ASM_RISCV_BITOPS_H */
