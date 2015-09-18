/*
 * Copyright 2004-2009 Analog Devices Inc.
 *               Tony Kou (tonyko@lineo.ca)
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _BLACKFIN_BARRIER_H
#define _BLACKFIN_BARRIER_H

#include <asm/cache.h>

#define nop()  __asm__ __volatile__ ("nop;\n\t" : : )

/*
 * Force strict CPU ordering.
 */
#ifdef CONFIG_SMP

#ifdef __ARCH_SYNC_CORE_DCACHE
/* Force Core data cache coherence */
# define mb()	do { barrier(); smp_check_barrier(); smp_mark_barrier(); } while (0)
# define rmb()	do { barrier(); smp_check_barrier(); } while (0)
# define wmb()	do { barrier(); smp_mark_barrier(); } while (0)
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
# define read_barrier_depends()	do { barrier(); smp_check_barrier(); } while (0)
#endif

#endif /* !CONFIG_SMP */

#define smp_mb__before_atomic()	barrier()
#define smp_mb__after_atomic()	barrier()

#include <asm-generic/barrier.h>

#endif /* _BLACKFIN_BARRIER_H */
