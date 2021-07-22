/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 by Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#include <asm/addrspace.h>
#include <asm/sync.h>

static inline void __sync(void)
{
	asm volatile(__SYNC(full, always) ::: "memory");
}

static inline void rmb(void)
{
	asm volatile(__SYNC(rmb, always) ::: "memory");
}
#define rmb rmb

static inline void wmb(void)
{
	asm volatile(__SYNC(wmb, always) ::: "memory");
}
#define wmb wmb

#define fast_mb()	__sync()

#define __fast_iob()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		"lw	$0,%0\n\t"		\
		"nop\n\t"			\
		".set	pop"			\
		: /* no output */		\
		: "m" (*(int *)CKSEG1)		\
		: "memory")
#ifdef CONFIG_CPU_CAVIUM_OCTEON
# define fast_iob()	do { } while (0)
#else /* ! CONFIG_CPU_CAVIUM_OCTEON */
# ifdef CONFIG_SGI_IP28
#  define fast_iob()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		"lw	$0,%0\n\t"		\
		"sync\n\t"			\
		"lw	$0,%0\n\t"		\
		".set	pop"			\
		: /* no output */		\
		: "m" (*(int *)CKSEG1ADDR(0x1fa00004)) \
		: "memory")
# else
#  define fast_iob()				\
	do {					\
		__sync();			\
		__fast_iob();			\
	} while (0)
# endif
#endif /* CONFIG_CPU_CAVIUM_OCTEON */

#ifdef CONFIG_CPU_HAS_WB

#include <asm/wbflush.h>

#define mb()		wbflush()
#define iob()		wbflush()

#else /* !CONFIG_CPU_HAS_WB */

#define mb()		fast_mb()
#define iob()		fast_iob()

#endif /* !CONFIG_CPU_HAS_WB */

#if defined(CONFIG_WEAK_ORDERING)
# define __smp_mb()	__sync()
# define __smp_rmb()	rmb()
# define __smp_wmb()	wmb()
#else
# define __smp_mb()	barrier()
# define __smp_rmb()	barrier()
# define __smp_wmb()	barrier()
#endif

/*
 * When LL/SC does imply order, it must also be a compiler barrier to avoid the
 * compiler from reordering where the CPU will not. When it does not imply
 * order, the compiler is also free to reorder across the LL/SC loop and
 * ordering will be done by smp_llsc_mb() and friends.
 */
#if defined(CONFIG_WEAK_REORDERING_BEYOND_LLSC) && defined(CONFIG_SMP)
# define __WEAK_LLSC_MB		sync
# define smp_llsc_mb() \
	__asm__ __volatile__(__stringify(__WEAK_LLSC_MB) : : :"memory")
# define __LLSC_CLOBBER
#else
# define __WEAK_LLSC_MB
# define smp_llsc_mb()		do { } while (0)
# define __LLSC_CLOBBER		"memory"
#endif

#ifdef CONFIG_CPU_CAVIUM_OCTEON
#define smp_mb__before_llsc() smp_wmb()
#define __smp_mb__before_llsc() __smp_wmb()
/* Cause previous writes to become visible on all CPUs as soon as possible */
#define nudge_writes() __asm__ __volatile__(".set push\n\t"		\
					    ".set arch=octeon\n\t"	\
					    "syncw\n\t"			\
					    ".set pop" : : : "memory")
#else
#define smp_mb__before_llsc() smp_llsc_mb()
#define __smp_mb__before_llsc() smp_llsc_mb()
#define nudge_writes() mb()
#endif

/*
 * In the Loongson3 LL/SC workaround case, all of our LL/SC loops already have
 * a completion barrier immediately preceding the LL instruction. Therefore we
 * can skip emitting a barrier from __smp_mb__before_atomic().
 */
#ifdef CONFIG_CPU_LOONGSON3_WORKAROUNDS
# define __smp_mb__before_atomic()
#else
# define __smp_mb__before_atomic()	__smp_mb__before_llsc()
#endif

#define __smp_mb__after_atomic()	smp_llsc_mb()

static inline void sync_ginv(void)
{
	asm volatile(__SYNC(ginv, always));
}

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
