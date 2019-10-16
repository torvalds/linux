/*
 * include/asm-xtensa/bitops.h
 *
 * Atomic operations that C can't guarantee us.Useful for resource counting etc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007 Tensilica Inc.
 */

#ifndef _XTENSA_BITOPS_H
#define _XTENSA_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm/processor.h>
#include <asm/byteorder.h>
#include <asm/barrier.h>

#include <asm-generic/bitops/non-atomic.h>

#if XCHAL_HAVE_NSA

static inline unsigned long __cntlz (unsigned long x)
{
	int lz;
	asm ("nsau %0, %1" : "=r" (lz) : "r" (x));
	return lz;
}

/*
 * ffz: Find first zero in word. Undefined if no zero exists.
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

static inline int ffz(unsigned long x)
{
	return 31 - __cntlz(~x & -~x);
}

/*
 * __ffs: Find first bit set in word. Return 0 for bit 0
 */

static inline unsigned long __ffs(unsigned long x)
{
	return 31 - __cntlz(x & -x);
}

/*
 * ffs: Find first bit set in word. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

static inline int ffs(unsigned long x)
{
	return 32 - __cntlz(x & -x);
}

/*
 * fls: Find last (most-significant) bit set in word.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */

static inline int fls (unsigned int x)
{
	return 32 - __cntlz(x);
}

/**
 * __fls - find last (most-significant) set bit in a long word
 * @word: the word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static inline unsigned long __fls(unsigned long word)
{
	return 31 - __cntlz(word);
}
#else

/* Use the generic implementation if we don't have the nsa/nsau instructions. */

# include <asm-generic/bitops/ffs.h>
# include <asm-generic/bitops/__ffs.h>
# include <asm-generic/bitops/ffz.h>
# include <asm-generic/bitops/fls.h>
# include <asm-generic/bitops/__fls.h>

#endif

#include <asm-generic/bitops/fls64.h>

#if XCHAL_HAVE_EXCLUSIVE

#define BIT_OP(op, insn, inv)						\
static inline void op##_bit(unsigned int bit, volatile unsigned long *p)\
{									\
	unsigned long tmp;						\
	unsigned long mask = 1UL << (bit & 31);				\
									\
	p += bit >> 5;							\
									\
	__asm__ __volatile__(						\
			"1:     l32ex   %0, %2\n"			\
			"      "insn"   %0, %0, %1\n"			\
			"       s32ex   %0, %2\n"			\
			"       getex   %0\n"				\
			"       beqz    %0, 1b\n"			\
			: "=&a" (tmp)					\
			: "a" (inv mask), "a" (p)			\
			: "memory");					\
}

#define TEST_AND_BIT_OP(op, insn, inv)					\
static inline int							\
test_and_##op##_bit(unsigned int bit, volatile unsigned long *p)	\
{									\
	unsigned long tmp, value;					\
	unsigned long mask = 1UL << (bit & 31);				\
									\
	p += bit >> 5;							\
									\
	__asm__ __volatile__(						\
			"1:     l32ex   %1, %3\n"			\
			"      "insn"   %0, %1, %2\n"			\
			"       s32ex   %0, %3\n"			\
			"       getex   %0\n"				\
			"       beqz    %0, 1b\n"			\
			: "=&a" (tmp), "=&a" (value)			\
			: "a" (inv mask), "a" (p)			\
			: "memory");					\
									\
	return value & mask;						\
}

#elif XCHAL_HAVE_S32C1I

#define BIT_OP(op, insn, inv)						\
static inline void op##_bit(unsigned int bit, volatile unsigned long *p)\
{									\
	unsigned long tmp, value;					\
	unsigned long mask = 1UL << (bit & 31);				\
									\
	p += bit >> 5;							\
									\
	__asm__ __volatile__(						\
			"1:     l32i    %1, %3, 0\n"			\
			"       wsr     %1, scompare1\n"		\
			"      "insn"   %0, %1, %2\n"			\
			"       s32c1i  %0, %3, 0\n"			\
			"       bne     %0, %1, 1b\n"			\
			: "=&a" (tmp), "=&a" (value)			\
			: "a" (inv mask), "a" (p)			\
			: "memory");					\
}

#define TEST_AND_BIT_OP(op, insn, inv)					\
static inline int							\
test_and_##op##_bit(unsigned int bit, volatile unsigned long *p)	\
{									\
	unsigned long tmp, value;					\
	unsigned long mask = 1UL << (bit & 31);				\
									\
	p += bit >> 5;							\
									\
	__asm__ __volatile__(						\
			"1:     l32i    %1, %3, 0\n"			\
			"       wsr     %1, scompare1\n"		\
			"      "insn"   %0, %1, %2\n"			\
			"       s32c1i  %0, %3, 0\n"			\
			"       bne     %0, %1, 1b\n"			\
			: "=&a" (tmp), "=&a" (value)			\
			: "a" (inv mask), "a" (p)			\
			: "memory");					\
									\
	return tmp & mask;						\
}

#else

#define BIT_OP(op, insn, inv)
#define TEST_AND_BIT_OP(op, insn, inv)

#include <asm-generic/bitops/atomic.h>

#endif /* XCHAL_HAVE_S32C1I */

#define BIT_OPS(op, insn, inv)		\
	BIT_OP(op, insn, inv)		\
	TEST_AND_BIT_OP(op, insn, inv)

BIT_OPS(set, "or", )
BIT_OPS(clear, "and", ~)
BIT_OPS(change, "xor", )

#undef BIT_OPS
#undef BIT_OP
#undef TEST_AND_BIT_OP

#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/le.h>

#include <asm-generic/bitops/ext2-atomic-setbit.h>

#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/bitops/sched.h>

#endif	/* _XTENSA_BITOPS_H */
