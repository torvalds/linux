/*
 * Optimised mutex implementation of include/asm-generic/mutex-dec.h algorithm
 */
#ifndef _ASM_POWERPC_MUTEX_H
#define _ASM_POWERPC_MUTEX_H

static inline int __mutex_cmpxchg_lock(atomic_t *v, int old, int new)
{
	int t;

	__asm__ __volatile__ (
"1:	lwarx	%0,0,%1		# mutex trylock\n\
	cmpw	0,%0,%2\n\
	bne-	2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%3,0,%1\n\
	bne-	1b"
	PPC_ACQUIRE_BARRIER
	"\n\
2:"
	: "=&r" (t)
	: "r" (&v->counter), "r" (old), "r" (new)
	: "cc", "memory");

	return t;
}

static inline int __mutex_dec_return_lock(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%1		# mutex lock\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	PPC_ACQUIRE_BARRIER
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

static inline int __mutex_inc_return_unlock(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	PPC_RELEASE_BARRIER
"1:	lwarx	%0,0,%1		# mutex unlock\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1 \n\
	bne-	1b"
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}

/**
 *  __mutex_fastpath_lock - try to take the lock by moving the count
 *                          from 1 to a 0 value
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 1
 *
 * Change the count from 1 to a value lower than 1, and call <fail_fn> if
 * it wasn't 1 originally. This function MUST leave the value lower than
 * 1 even when the "1" assertion wasn't true.
 */
static inline void
__mutex_fastpath_lock(atomic_t *count, void (*fail_fn)(atomic_t *))
{
	if (unlikely(__mutex_dec_return_lock(count) < 0))
		fail_fn(count);
}

/**
 *  __mutex_fastpath_lock_retval - try to take the lock by moving the count
 *                                 from 1 to a 0 value
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 1
 *
 * Change the count from 1 to a value lower than 1, and call <fail_fn> if
 * it wasn't 1 originally. This function returns 0 if the fastpath succeeds,
 * or anything the slow path function returns.
 */
static inline int
__mutex_fastpath_lock_retval(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	if (unlikely(__mutex_dec_return_lock(count) < 0))
		return fail_fn(count);
	return 0;
}

/**
 *  __mutex_fastpath_unlock - try to promote the count from 0 to 1
 *  @count: pointer of type atomic_t
 *  @fail_fn: function to call if the original value was not 0
 *
 * Try to promote the count from 0 to 1. If it wasn't 0, call <fail_fn>.
 * In the failure case, this function is allowed to either set the value to
 * 1, or to set it to a value lower than 1.
 */
static inline void
__mutex_fastpath_unlock(atomic_t *count, void (*fail_fn)(atomic_t *))
{
	if (unlikely(__mutex_inc_return_unlock(count) <= 0))
		fail_fn(count);
}

#define __mutex_slowpath_needs_to_unlock()		1

/**
 * __mutex_fastpath_trylock - try to acquire the mutex, without waiting
 *
 *  @count: pointer of type atomic_t
 *  @fail_fn: fallback function
 *
 * Change the count from 1 to 0, and return 1 (success), or if the count
 * was not 1, then return 0 (failure).
 */
static inline int
__mutex_fastpath_trylock(atomic_t *count, int (*fail_fn)(atomic_t *))
{
	if (likely(__mutex_cmpxchg_lock(count, 1, 0) == 1))
		return 1;
	return 0;
}

#endif
