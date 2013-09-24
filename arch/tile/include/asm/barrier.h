/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_BARRIER_H
#define _ASM_TILE_BARRIER_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <arch/chip.h>
#include <arch/spr_def.h>
#include <asm/timex.h>

/*
 * read_barrier_depends - Flush all pending reads that subsequents reads
 * depend on.
 *
 * No data-dependent reads from memory-like regions are ever reordered
 * over this barrier.  All reads preceding this primitive are guaranteed
 * to access memory (but not necessarily other CPUs' caches) before any
 * reads following this primitive that depend on the data return by
 * any of the preceding reads.  This primitive is much lighter weight than
 * rmb() on most CPUs, and is never heavier weight than is
 * rmb().
 *
 * These ordering constraints are respected by both the local CPU
 * and the compiler.
 *
 * Ordering is not guaranteed by anything other than these primitives,
 * not even by data dependencies.  See the documentation for
 * memory_barrier() for examples and URLs to more information.
 *
 * For example, the following code would force ordering (the initial
 * value of "a" is zero, "b" is one, and "p" is "&a"):
 *
 * <programlisting>
 *	CPU 0				CPU 1
 *
 *	b = 2;
 *	memory_barrier();
 *	p = &b;				q = p;
 *					read_barrier_depends();
 *					d = *q;
 * </programlisting>
 *
 * because the read of "*q" depends on the read of "p" and these
 * two reads are separated by a read_barrier_depends().  However,
 * the following code, with the same initial values for "a" and "b":
 *
 * <programlisting>
 *	CPU 0				CPU 1
 *
 *	a = 2;
 *	memory_barrier();
 *	b = 3;				y = b;
 *					read_barrier_depends();
 *					x = a;
 * </programlisting>
 *
 * does not enforce ordering, since there is no data dependency between
 * the read of "a" and the read of "b".  Therefore, on some CPUs, such
 * as Alpha, "y" could be set to 3 and "x" to 0.  Use rmb()
 * in cases like this where there are no data dependencies.
 */
#define read_barrier_depends()	do { } while (0)

#define __sync()	__insn_mf()

#include <hv/syscall_public.h>
/*
 * Issue an uncacheable load to each memory controller, then
 * wait until those loads have completed.
 */
static inline void __mb_incoherent(void)
{
	long clobber_r10;
	asm volatile("swint2"
		     : "=R10" (clobber_r10)
		     : "R10" (HV_SYS_fence_incoherent)
		     : "r0", "r1", "r2", "r3", "r4",
		       "r5", "r6", "r7", "r8", "r9",
		       "r11", "r12", "r13", "r14",
		       "r15", "r16", "r17", "r18", "r19",
		       "r20", "r21", "r22", "r23", "r24",
		       "r25", "r26", "r27", "r28", "r29");
}

/* Fence to guarantee visibility of stores to incoherent memory. */
static inline void
mb_incoherent(void)
{
	__insn_mf();

	{
#if CHIP_HAS_TILE_WRITE_PENDING()
		const unsigned long WRITE_TIMEOUT_CYCLES = 400;
		unsigned long start = get_cycles_low();
		do {
			if (__insn_mfspr(SPR_TILE_WRITE_PENDING) == 0)
				return;
		} while ((get_cycles_low() - start) < WRITE_TIMEOUT_CYCLES);
#endif /* CHIP_HAS_TILE_WRITE_PENDING() */
		(void) __mb_incoherent();
	}
}

#define fast_wmb()	__sync()
#define fast_rmb()	__sync()
#define fast_mb()	__sync()
#define fast_iob()	mb_incoherent()

#define wmb()		fast_wmb()
#define rmb()		fast_rmb()
#define mb()		fast_mb()
#define iob()		fast_iob()

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while (0)
#endif

#define set_mb(var, value) \
	do { var = value; mb(); } while (0)

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_TILE_BARRIER_H */
