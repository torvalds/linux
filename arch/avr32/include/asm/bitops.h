/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_BITOPS_H
#define __ASM_AVR32_BITOPS_H

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm/byteorder.h>
#include <asm/system.h>

/*
 * clear_bit() doesn't provide any barrier for the compiler
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

/*
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void set_bit(int nr, volatile void * addr)
{
	unsigned long *p = ((unsigned long *)addr) + nr / BITS_PER_LONG;
	unsigned long tmp;

	if (__builtin_constant_p(nr)) {
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%0, %2\n"
			"	sbr	%0, %3\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p)
			: "m"(*p), "i"(nr)
			: "cc");
	} else {
		unsigned long mask = 1UL << (nr % BITS_PER_LONG);
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%0, %2\n"
			"	or	%0, %3\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p)
			: "m"(*p), "r"(mask)
			: "cc");
	}
}

/*
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_clear_bit() and/or smp_mb__after_clear_bit()
 * in order to ensure changes are visible on other processors.
 */
static inline void clear_bit(int nr, volatile void * addr)
{
	unsigned long *p = ((unsigned long *)addr) + nr / BITS_PER_LONG;
	unsigned long tmp;

	if (__builtin_constant_p(nr)) {
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%0, %2\n"
			"	cbr	%0, %3\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p)
			: "m"(*p), "i"(nr)
			: "cc");
	} else {
		unsigned long mask = 1UL << (nr % BITS_PER_LONG);
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%0, %2\n"
			"	andn	%0, %3\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p)
			: "m"(*p), "r"(mask)
			: "cc");
	}
}

/*
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void change_bit(int nr, volatile void * addr)
{
	unsigned long *p = ((unsigned long *)addr) + nr / BITS_PER_LONG;
	unsigned long mask = 1UL << (nr % BITS_PER_LONG);
	unsigned long tmp;

	asm volatile(
		"1:	ssrf	5\n"
		"	ld.w	%0, %2\n"
		"	eor	%0, %3\n"
		"	stcond	%1, %0\n"
		"	brne	1b"
		: "=&r"(tmp), "=o"(*p)
		: "m"(*p), "r"(mask)
		: "cc");
}

/*
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_set_bit(int nr, volatile void * addr)
{
	unsigned long *p = ((unsigned long *)addr) + nr / BITS_PER_LONG;
	unsigned long mask = 1UL << (nr % BITS_PER_LONG);
	unsigned long tmp, old;

	if (__builtin_constant_p(nr)) {
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%0, %3\n"
			"	mov	%2, %0\n"
			"	sbr	%0, %4\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p), "=&r"(old)
			: "m"(*p), "i"(nr)
			: "memory", "cc");
	} else {
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%2, %3\n"
			"	or	%0, %2, %4\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p), "=&r"(old)
			: "m"(*p), "r"(mask)
			: "memory", "cc");
	}

	return (old & mask) != 0;
}

/*
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_clear_bit(int nr, volatile void * addr)
{
	unsigned long *p = ((unsigned long *)addr) + nr / BITS_PER_LONG;
	unsigned long mask = 1UL << (nr % BITS_PER_LONG);
	unsigned long tmp, old;

	if (__builtin_constant_p(nr)) {
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%0, %3\n"
			"	mov	%2, %0\n"
			"	cbr	%0, %4\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p), "=&r"(old)
			: "m"(*p), "i"(nr)
			: "memory", "cc");
	} else {
		asm volatile(
			"1:	ssrf	5\n"
			"	ld.w	%0, %3\n"
			"	mov	%2, %0\n"
			"	andn	%0, %4\n"
			"	stcond	%1, %0\n"
			"	brne	1b"
			: "=&r"(tmp), "=o"(*p), "=&r"(old)
			: "m"(*p), "r"(mask)
			: "memory", "cc");
	}

	return (old & mask) != 0;
}

/*
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_change_bit(int nr, volatile void * addr)
{
	unsigned long *p = ((unsigned long *)addr) + nr / BITS_PER_LONG;
	unsigned long mask = 1UL << (nr % BITS_PER_LONG);
	unsigned long tmp, old;

	asm volatile(
		"1:	ssrf	5\n"
		"	ld.w	%2, %3\n"
		"	eor	%0, %2, %4\n"
		"	stcond	%1, %0\n"
		"	brne	1b"
		: "=&r"(tmp), "=o"(*p), "=&r"(old)
		: "m"(*p), "r"(mask)
		: "memory", "cc");

	return (old & mask) != 0;
}

#include <asm-generic/bitops/non-atomic.h>

/* Find First bit Set */
static inline unsigned long __ffs(unsigned long word)
{
	unsigned long result;

	asm("brev %1\n\t"
	    "clz %0,%1"
	    : "=r"(result), "=&r"(word)
	    : "1"(word));
	return result;
}

/* Find First Zero */
static inline unsigned long ffz(unsigned long word)
{
	return __ffs(~word);
}

/* Find Last bit Set */
static inline int fls(unsigned long word)
{
	unsigned long result;

	asm("clz %0,%1" : "=r"(result) : "r"(word));
	return 32 - result;
}

static inline int __fls(unsigned long word)
{
	return fls(word) - 1;
}

unsigned long find_first_zero_bit(const unsigned long *addr,
				  unsigned long size);
unsigned long find_next_zero_bit(const unsigned long *addr,
				 unsigned long size,
				 unsigned long offset);
unsigned long find_first_bit(const unsigned long *addr,
			     unsigned long size);
unsigned long find_next_bit(const unsigned long *addr,
				 unsigned long size,
				 unsigned long offset);

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 *
 * The difference is that bit numbering starts at 1, and if no bit is set,
 * the function returns 0.
 */
static inline int ffs(unsigned long word)
{
	if(word == 0)
		return 0;
	return __ffs(word) + 1;
}

#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/ext2-non-atomic.h>
#include <asm-generic/bitops/ext2-atomic.h>
#include <asm-generic/bitops/minix-le.h>

#endif /* __ASM_AVR32_BITOPS_H */
