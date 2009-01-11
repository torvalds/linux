/*
 * Pull in the generic implementation for the mutex fastpath.
 *
 * TODO: implement optimized primitives instead, or leave the generic
 * implementation in place, or pick the atomic_xchg() based generic
 * implementation. (see asm-generic/mutex-xchg.h for details)
 */

#ifndef _ASM_MUTEX_H
#define _ASM_MUTEX_H

#ifndef CONFIG_SMP
#include <asm-generic/mutex-dec.h>
#else

static inline void
__mutex_fastpath_lock(atomic_t *count, void (*fail_fn)(atomic_t *))
{
	if (unlikely(atomic_dec_return(count) < 0))
		fail_fn(count);
	else
		smp_mb();
}

static inline int
__mutex_fastpath_lock_retval(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	if (unlikely(atomic_dec_return(count) < 0))
		return fail_fn(count);
	else {
		smp_mb();
		return 0;
	}
}

static inline void
__mutex_fastpath_unlock(atomic_t *count, void (*fail_fn)(atomic_t *))
{
	smp_mb();
	if (unlikely(atomic_inc_return(count) <= 0))
		fail_fn(count);
}

#define __mutex_slowpath_needs_to_unlock()		1

static inline int
__mutex_fastpath_trylock(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	/*
	 * We have two variants here. The cmpxchg based one is the best one
	 * because it never induce a false contention state.  It is included
	 * here because architectures using the inc/dec algorithms over the
	 * xchg ones are much more likely to support cmpxchg natively.
	 *
	 * If not we fall back to the spinlock based variant - that is
	 * just as efficient (and simpler) as a 'destructive' probing of
	 * the mutex state would be.
	 */
#ifdef __HAVE_ARCH_CMPXCHG
	if (likely(atomic_cmpxchg(count, 1, 0) == 1)) {
		smp_mb();
		return 1;
	}
	return 0;
#else
	return fail_fn(count);
#endif
}

#endif

#endif
