/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * xchg/cmpxchg operations for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_CMPXCHG_H
#define _ASM_CMPXCHG_H

/*
 * __arch_xchg - atomically exchange a register and a memory location
 * @x: value to swap
 * @ptr: pointer to memory
 * @size:  size of the value
 *
 * Only 4 bytes supported currently.
 *
 * Note:  there was an errata for V2 about .new's and memw_locked.
 *
 */
static inline unsigned long
__arch_xchg(unsigned long x, volatile void *ptr, int size)
{
	unsigned long retval;

	/*  Can't seem to use printk or panic here, so just stop  */
	if (size != 4) do { asm volatile("brkpt;\n"); } while (1);

	__asm__ __volatile__ (
	"1:	%0 = memw_locked(%1);\n"    /*  load into retval */
	"	memw_locked(%1,P0) = %2;\n" /*  store into memory */
	"	if (!P0) jump 1b;\n"
	: "=&r" (retval)
	: "r" (ptr), "r" (x)
	: "memory", "p0"
	);
	return retval;
}

/*
 * Atomically swap the contents of a register with memory.  Should be atomic
 * between multiple CPU's and within interrupts on the same CPU.
 */
#define arch_xchg(ptr, v) ((__typeof__(*(ptr)))__arch_xchg((unsigned long)(v), (ptr), \
							   sizeof(*(ptr))))

/*
 *  see rt-mutex-design.txt; cmpxchg supposedly checks if *ptr == A and swaps.
 *  looks just like atomic_cmpxchg on our arch currently with a bunch of
 *  variable casting.
 */

#define arch_cmpxchg(ptr, old, new)				\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(*(ptr)) __old = (old);			\
	__typeof__(*(ptr)) __new = (new);			\
	__typeof__(*(ptr)) __oldval = 0;			\
								\
	asm volatile(						\
		"1:	%0 = memw_locked(%1);\n"		\
		"	{ P0 = cmp.eq(%0,%2);\n"		\
		"	  if (!P0.new) jump:nt 2f; }\n"		\
		"	memw_locked(%1,p0) = %3;\n"		\
		"	if (!P0) jump 1b;\n"			\
		"2:\n"						\
		: "=&r" (__oldval)				\
		: "r" (__ptr), "r" (__old), "r" (__new)		\
		: "memory", "p0"				\
	);							\
	__oldval;						\
})

#endif /* _ASM_CMPXCHG_H */
