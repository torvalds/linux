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
	int num;

	asm_volatile_goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      "ctz %0, %1\n"
		      ".option pop\n"
		      : "=r" (word) : "r" (word) :);

	return word;

legacy:
	num = 0;
#if BITS_PER_LONG == 64
	if ((word & 0xffffffff) == 0) {
		num += 32;
		word >>= 32;
	}
#endif
	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}
	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
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
	int num;

	asm_volatile_goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      "clz %0, %1\n"
		      ".option pop\n"
		      : "=r" (word) : "r" (word) :);

	return BITS_PER_LONG - 1 - word;

legacy:
	num = BITS_PER_LONG - 1;
#if BITS_PER_LONG == 64
	if (!(word & (~0ul << 32))) {
		num -= 32;
		word <<= 32;
	}
#endif
	if (!(word & (~0ul << (BITS_PER_LONG - 16)))) {
		num -= 16;
		word <<= 16;
	}
	if (!(word & (~0ul << (BITS_PER_LONG - 8)))) {
		num -= 8;
		word <<= 8;
	}
	if (!(word & (~0ul << (BITS_PER_LONG - 4)))) {
		num -= 4;
		word <<= 4;
	}
	if (!(word & (~0ul << (BITS_PER_LONG - 2)))) {
		num -= 2;
		word <<= 2;
	}
	if (!(word & (~0ul << (BITS_PER_LONG - 1))))
		num -= 1;
	return num;
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
	int r;

	if (!x)
		return 0;

	asm_volatile_goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      CTZW "%0, %1\n"
		      ".option pop\n"
		      : "=r" (r) : "r" (x) :);

	return r + 1;

legacy:
	r = 1;
	if (!(x & 0xffff)) {
		x >>= 16;
		r += 16;
	}
	if (!(x & 0xff)) {
		x >>= 8;
		r += 8;
	}
	if (!(x & 0xf)) {
		x >>= 4;
		r += 4;
	}
	if (!(x & 3)) {
		x >>= 2;
		r += 2;
	}
	if (!(x & 1)) {
		x >>= 1;
		r += 1;
	}
	return r;
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
	int r;

	if (!x)
		return 0;

	asm_volatile_goto(ALTERNATIVE("j %l[legacy]", "nop", 0,
				      RISCV_ISA_EXT_ZBB, 1)
			  : : : : legacy);

	asm volatile (".option push\n"
		      ".option arch,+zbb\n"
		      CLZW "%0, %1\n"
		      ".option pop\n"
		      : "=r" (r) : "r" (x) :);

	return 32 - r;

legacy:
	r = 32;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
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
	 (int)((x_ != 0) ? (32 - __builtin_clz(x_)) : 0)	\
	 :							\
	 variable_fls(x_);					\
})

#endif /* !defined(CONFIG_RISCV_ISA_ZBB) || defined(NO_ALTERNATIVE) */

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>

#include <asm-generic/bitops/hweight.h>

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
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation may be reordered on other architectures than x86.
 */
static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(or, __NOP, nr, addr);
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation can be reordered on other architectures other than x86.
 */
static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(and, __NOT, nr, addr);
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	return __test_and_op_bit(xor, __NOP, nr, addr);
}

/**
 * set_bit - Atomically set a bit in memory
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
static inline void set_bit(int nr, volatile unsigned long *addr)
{
	__op_bit(or, __NOP, nr, addr);
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 */
static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	__op_bit(and, __NOT, nr, addr);
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit()  may be reordered on other architectures than x86.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void change_bit(int nr, volatile unsigned long *addr)
{
	__op_bit(xor, __NOP, nr, addr);
}

/**
 * test_and_set_bit_lock - Set a bit and return its old value, for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and provides acquire barrier semantics.
 * It can be used to implement bit locks.
 */
static inline int test_and_set_bit_lock(
	unsigned long nr, volatile unsigned long *addr)
{
	return __test_and_op_bit_ord(or, __NOP, nr, addr, .aq);
}

/**
 * clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This operation is atomic and provides release barrier semantics.
 */
static inline void clear_bit_unlock(
	unsigned long nr, volatile unsigned long *addr)
{
	__op_bit_ord(and, __NOT, nr, addr, .rl);
}

/**
 * __clear_bit_unlock - Clear a bit in memory, for unlock
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
static inline void __clear_bit_unlock(
	unsigned long nr, volatile unsigned long *addr)
{
	clear_bit_unlock(nr, addr);
}

static inline bool xor_unlock_is_negative_byte(unsigned long mask,
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

#include <asm-generic/bitops/non-atomic.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/ext2-atomic.h>

#endif /* _ASM_RISCV_BITOPS_H */
