// SPDX-License-Identifier: GPL-2.0
/*
 * linux/arch/m32r/lib/delay.c
 *
 * Copyright (c) 2002  Hitoshi Yamamoto, Hirokazu Takata
 * Copyright (c) 2004  Hirokazu Takata
 */

#include <linux/param.h>
#include <linux/module.h>
#ifdef CONFIG_SMP
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/smp.h>
#endif  /* CONFIG_SMP */
#include <asm/processor.h>

void __delay(unsigned long loops)
{
#ifdef CONFIG_ISA_DUAL_ISSUE
	__asm__ __volatile__ (
		"beqz	%0, 2f			\n\t"
		"addi	%0, #-1			\n\t"

		" .fillinsn			\n\t"
		"1:				\n\t"
		"cmpz	%0  ||  addi  %0, #-1	\n\t"
		"bc	2f  ||  cmpz  %0	\n\t"
		"bc	2f  ||  addi  %0, #-1	\n\t"
		"cmpz	%0  ||  addi  %0, #-1	\n\t"
		"bc	2f  ||  cmpz  %0	\n\t"
		"bnc	1b  ||  addi  %0, #-1	\n\t"
		" .fillinsn			\n\t"
		"2:				\n\t"
		: "+r" (loops)
		: "r" (0)
		: "cbit"
	);
#else
	__asm__ __volatile__ (
		"beqz	%0, 2f			\n\t"
		" .fillinsn			\n\t"
		"1:				\n\t"
		"addi	%0, #-1			\n\t"
		"blez	%0, 2f			\n\t"
		"addi	%0, #-1			\n\t"
		"blez	%0, 2f			\n\t"
		"addi	%0, #-1			\n\t"
		"blez	%0, 2f			\n\t"
		"addi	%0, #-1			\n\t"
		"bgtz	%0, 1b			\n\t"
		" .fillinsn			\n\t"
		"2:				\n\t"
		: "+r" (loops)
		: "r" (0)
	);
#endif
}

void __const_udelay(unsigned long xloops)
{
#if defined(CONFIG_ISA_M32R2) && defined(CONFIG_ISA_DSP_LEVEL2)
	/*
	 * loops [1] = (xloops >> 32) [sec] * loops_per_jiffy [1/jiffy]
	 *            * HZ [jiffy/sec]
	 *          = (xloops >> 32) [sec] * (loops_per_jiffy * HZ) [1/sec]
	 *          = (((xloops * loops_per_jiffy) >> 32) * HZ) [1]
	 *
	 * NOTE:
	 *   - '[]' depicts variable's dimension in the above equation.
	 *   - "rac" instruction rounds the accumulator in word size.
	 */
	__asm__ __volatile__ (
		"srli	%0, #1				\n\t"
		"mulwhi	%0, %1	; a0			\n\t"
		"mulwu1	%0, %1	; a1			\n\t"
		"sadd		; a0 += (a1 >> 16)	\n\t"
		"rac	a0, a0, #1			\n\t"
		"mvfacmi %0, a0				\n\t"
		: "+r" (xloops)
		: "r" (current_cpu_data.loops_per_jiffy)
		: "a0", "a1"
	);
#elif defined(CONFIG_ISA_M32R2) || defined(CONFIG_ISA_M32R)
	/*
	 * u64 ull;
	 * ull = (u64)xloops * (u64)current_cpu_data.loops_per_jiffy;
	 * xloops = (ull >> 32);
	 */
	__asm__ __volatile__ (
		"and3	r4, %0, #0xffff		\n\t"
		"and3	r5, %1, #0xffff		\n\t"
		"mul	r4, r5			\n\t"
		"srl3	r6, %0, #16		\n\t"
		"srli	r4, #16			\n\t"
		"mul	r5, r6			\n\t"
		"add	r4, r5			\n\t"
		"and3	r5, %0, #0xffff		\n\t"
		"srl3	r6, %1, #16		\n\t"
		"mul	r5, r6			\n\t"
		"add	r4, r5			\n\t"
		"srl3	r5, %0, #16		\n\t"
		"srli	r4, #16			\n\t"
		"mul	r5, r6			\n\t"
		"add	r4, r5			\n\t"
		"mv	%0, r4			\n\t"
		: "+r" (xloops)
		: "r" (current_cpu_data.loops_per_jiffy)
		: "r4", "r5", "r6"
	);
#else
#error unknown isa configuration
#endif
	__delay(xloops * HZ);
}

void __udelay(unsigned long usecs)
{
	__const_udelay(usecs * 0x000010c7);  /* 2**32 / 1000000 (rounded up) */
}

void __ndelay(unsigned long nsecs)
{
	__const_udelay(nsecs * 0x00005);  /* 2**32 / 1000000000 (rounded up) */
}

EXPORT_SYMBOL(__delay);
EXPORT_SYMBOL(__const_udelay);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
