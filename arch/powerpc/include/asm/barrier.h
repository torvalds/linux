/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_BARRIER_H
#define _ASM_POWERPC_BARRIER_H

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
 * read_barrier_depends() prevents data-dependent loads being reordered
 *	across this point (nop on PPC).
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

#define set_mb(var, value)	do { var = value; mb(); } while (0)

#ifdef CONFIG_SMP

#ifdef __SUBARCH_HAS_LWSYNC
#    define SMPWMB      LWSYNC
#else
#    define SMPWMB      eieio
#endif

#define __lwsync()	__asm__ __volatile__ (stringify_in_c(LWSYNC) : : :"memory")

#define smp_mb()	mb()
#define smp_rmb()	__lwsync()
#define smp_wmb()	__asm__ __volatile__ (stringify_in_c(SMPWMB) : : :"memory")
#else
#define __lwsync()	barrier()

#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#endif /* CONFIG_SMP */

#define read_barrier_depends()		do { } while (0)
#define smp_read_barrier_depends()	do { } while (0)

/*
 * This is a barrier which prevents following instructions from being
 * started until the value of the argument x is known.  For example, if
 * x is a variable loaded from memory, this prevents following
 * instructions from being executed until the load has been performed.
 */
#define data_barrier(x)	\
	asm volatile("twi 0,%0,0; isync" : : "r" (x) : "memory");

#define smp_store_release(p, v)						\
do {									\
	compiletime_assert_atomic_type(*p);				\
	__lwsync();							\
	ACCESS_ONCE(*p) = (v);						\
} while (0)

#define smp_load_acquire(p)						\
({									\
	typeof(*p) ___p1 = ACCESS_ONCE(*p);				\
	compiletime_assert_atomic_type(*p);				\
	__lwsync();							\
	___p1;								\
})

#define smp_mb__before_atomic()     smp_mb()
#define smp_mb__after_atomic()      smp_mb()

#endif /* _ASM_POWERPC_BARRIER_H */
