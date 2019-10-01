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

#ifdef CONFIG_CPU_HAS_SYNC
#define __sync()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		".set	mips2\n\t"		\
		"sync\n\t"			\
		".set	pop"			\
		: /* no output */		\
		: /* no input */		\
		: "memory")
#else
#define __sync()	do { } while(0)
#endif

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
# define fast_mb()	__sync()
# define fast_iob()	do { } while (0)
#else /* ! CONFIG_CPU_CAVIUM_OCTEON */
# define fast_mb()	__sync()
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
#define __WEAK_LLSC_MB		"	sync	\n"
#define smp_llsc_mb()		__asm__ __volatile__(__WEAK_LLSC_MB : : :"memory")
#define __LLSC_CLOBBER
#else
#define __WEAK_LLSC_MB		"		\n"
#define smp_llsc_mb()		do { } while (0)
#define __LLSC_CLOBBER		"memory"
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

#define __smp_mb__before_atomic()	__smp_mb__before_llsc()
#define __smp_mb__after_atomic()	smp_llsc_mb()

/*
 * Some Loongson 3 CPUs have a bug wherein execution of a memory access (load,
 * store or prefetch) in between an LL & SC can cause the SC instruction to
 * erroneously succeed, breaking atomicity. Whilst it's unusual to write code
 * containing such sequences, this bug bites harder than we might otherwise
 * expect due to reordering & speculation:
 *
 * 1) A memory access appearing prior to the LL in program order may actually
 *    be executed after the LL - this is the reordering case.
 *
 *    In order to avoid this we need to place a memory barrier (ie. a SYNC
 *    instruction) prior to every LL instruction, in between it and any earlier
 *    memory access instructions.
 *
 *    This reordering case is fixed by 3A R2 CPUs, ie. 3A2000 models and later.
 *
 * 2) If a conditional branch exists between an LL & SC with a target outside
 *    of the LL-SC loop, for example an exit upon value mismatch in cmpxchg()
 *    or similar, then misprediction of the branch may allow speculative
 *    execution of memory accesses from outside of the LL-SC loop.
 *
 *    In order to avoid this we need a memory barrier (ie. a SYNC instruction)
 *    at each affected branch target, for which we also use loongson_llsc_mb()
 *    defined below.
 *
 *    This case affects all current Loongson 3 CPUs.
 *
 * The above described cases cause an error in the cache coherence protocol;
 * such that the Invalidate of a competing LL-SC goes 'missing' and SC
 * erroneously observes its core still has Exclusive state and lets the SC
 * proceed.
 *
 * Therefore the error only occurs on SMP systems.
 */
#ifdef CONFIG_CPU_LOONGSON3_WORKAROUNDS /* Loongson-3's LLSC workaround */
#define loongson_llsc_mb()	__asm__ __volatile__("sync" : : :"memory")
#else
#define loongson_llsc_mb()	do { } while (0)
#endif

static inline void sync_ginv(void)
{
	asm volatile("sync\t%0" :: "i"(__SYNC_ginv));
}

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
