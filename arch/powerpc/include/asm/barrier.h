/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_BARRIER_H
#define _ASM_POWERPC_BARRIER_H

#include <asm/asm-const.h>

#ifndef __ASSEMBLY__
#include <asm/ppc-opcode.h>
#endif

/*
 * Memory barrier.
 * The sync instruction guarantees that all memory accesses initiated
 * by this processor have been performed (with respect to all other
 * mechanisms that access memory).  The eieio instruction is a barrier
 * providing an ordering (separately) for (a) cacheable stores and (b)
 * loads and stores to non-cacheable memory (e.g. I/O devices).
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 *
 * *mb() variants without smp_ prefix must order all types of memory
 * operations with one another. sync is the only instruction sufficient
 * to do this.
 *
 * For the smp_ barriers, ordering is for cacheable memory operations
 * only. We have to use the sync instruction for smp_mb(), since lwsync
 * doesn't order loads with respect to previous stores.  Lwsync can be
 * used for smp_rmb() and smp_wmb().
 *
 * However, on CPUs that don't support lwsync, lwsync actually maps to a
 * heavy-weight sync, so smp_wmb() can be a lighter-weight eieio.
 */
#define mb()   __asm__ __volatile__ ("sync" : : : "memory")
#define rmb()  __asm__ __volatile__ ("sync" : : : "memory")
#define wmb()  __asm__ __volatile__ ("sync" : : : "memory")

/* The sub-arch has lwsync */
#if defined(CONFIG_PPC64) || defined(CONFIG_PPC_E500MC)
#    define SMPWMB      LWSYNC
#elif defined(CONFIG_BOOKE)
#    define SMPWMB      mbar
#else
#    define SMPWMB      eieio
#endif

/* clang defines this macro for a builtin, which will not work with runtime patching */
#undef __lwsync
#define __lwsync()	__asm__ __volatile__ (stringify_in_c(LWSYNC) : : :"memory")
#define dma_rmb()	__lwsync()
#define dma_wmb()	__asm__ __volatile__ (stringify_in_c(SMPWMB) : : :"memory")

#define __smp_lwsync()	__lwsync()

#define __smp_mb()	mb()
#define __smp_rmb()	__lwsync()
#define __smp_wmb()	__asm__ __volatile__ (stringify_in_c(SMPWMB) : : :"memory")

/*
 * This is a barrier which prevents following instructions from being
 * started until the value of the argument x is known.  For example, if
 * x is a variable loaded from memory, this prevents following
 * instructions from being executed until the load has been performed.
 */
#define data_barrier(x)	\
	asm volatile("twi 0,%0,0; isync" : : "r" (x) : "memory");

#define __smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	__smp_lwsync();							\
	WRITE_ONCE(*p, v);						\
} while (0)

#define __smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = READ_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	__smp_lwsync();							\
	___p1;								\
})

#ifdef CONFIG_PPC_BOOK3S_64
#define NOSPEC_BARRIER_SLOT   nop
#elif defined(CONFIG_PPC_E500)
#define NOSPEC_BARRIER_SLOT   nop; nop
#endif

#ifdef CONFIG_PPC_BARRIER_NOSPEC
/*
 * Prevent execution of subsequent instructions until preceding branches have
 * been fully resolved and are no longer executing speculatively.
 */
#define barrier_nospec_asm NOSPEC_BARRIER_FIXUP_SECTION; NOSPEC_BARRIER_SLOT

// This also acts as a compiler barrier due to the memory clobber.
#define barrier_nospec() asm (stringify_in_c(barrier_nospec_asm) ::: "memory")

#else /* !CONFIG_PPC_BARRIER_NOSPEC */
#define barrier_nospec_asm
#define barrier_nospec()
#endif /* CONFIG_PPC_BARRIER_NOSPEC */

/*
 * pmem_wmb() ensures that all stores for which the modification
 * are written to persistent storage by preceding dcbfps/dcbstps
 * instructions have updated persistent storage before any data
 * access or data transfer caused by subsequent instructions is
 * initiated.
 */
#define pmem_wmb() __asm__ __volatile__(PPC_PHWSYNC ::: "memory")

#include <asm-generic/barrier.h>

#endif /* _ASM_POWERPC_BARRIER_H */
