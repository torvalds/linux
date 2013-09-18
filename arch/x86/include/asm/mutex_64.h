/*
 * Assembly implementation of the mutex fastpath, based on atomic
 * decrement/increment.
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#ifndef _ASM_X86_MUTEX_64_H
#define _ASM_X86_MUTEX_64_H

/**
 * __mutex_fastpath_lock - decrement and call function if negative
 * @v: pointer of type atomic_t
 * @fail_fn: function to call if the result is negative
 *
 * Atomically decrements @v and calls <fail_fn> if the result is negative.
 */
#define __mutex_fastpath_lock(v, fail_fn)			\
do {								\
	unsigned long dummy;					\
								\
	typecheck(atomic_t *, v);				\
	typecheck_fn(void (*)(atomic_t *), fail_fn);		\
								\
	asm volatile(LOCK_PREFIX "   decl (%%rdi)\n"		\
		     "   jns 1f		\n"			\
		     "   call " #fail_fn "\n"			\
		     "1:"					\
		     : "=D" (dummy)				\
		     : "D" (v)					\
		     : "rax", "rsi", "rdx", "rcx",		\
		       "r8", "r9", "r10", "r11", "memory");	\
} while (0)

/**
 *  __mutex_fastpath_lock_retval - try to take the lock by moving the count
 *                                 from 1 to a 0 value
 *  @count: pointer of type atomic_t
 *
 * Change the count from 1 to a value lower than 1. This function returns 0
 * if the fastpath succeeds, or -1 otherwise.
 */
static inline int __mutex_fastpath_lock_retval(atomic_t *count)
{
	if (unlikely(atomic_dec_return(count) < 0))
		return -1;
	else
		return 0;
}

/**
 * __mutex_fastpath_unlock - increment and call function if nonpositive
 * @v: pointer of type atomic_t
 * @fail_fn: function to call if the result is nonpositive
 *
 * Atomically increments @v and calls <fail_fn> if the result is nonpositive.
 */
#define __mutex_fastpath_unlock(v, fail_fn)			\
do {								\
	unsigned long dummy;					\
								\
	typecheck(atomic_t *, v);				\
	typecheck_fn(void (*)(atomic_t *), fail_fn);		\
								\
	asm volatile(LOCK_PREFIX "   incl (%%rdi)\n"		\
		     "   jg 1f\n"				\
		     "   call " #fail_fn "\n"			\
		     "1:"					\
		     : "=D" (dummy)				\
		     : "D" (v)					\
		     : "rax", "rsi", "rdx", "rcx",		\
		       "r8", "r9", "r10", "r11", "memory");	\
} while (0)

#define __mutex_slowpath_needs_to_unlock()	1

/**
 * __mutex_fastpath_trylock - try to acquire the mutex, without waiting
 *
 *  @count: pointer of type atomic_t
 *  @fail_fn: fallback function
 *
 * Change the count from 1 to 0 and return 1 (success), or return 0 (failure)
 * if it wasn't 1 originally. [the fallback function is never used on
 * x86_64, because all x86_64 CPUs have a CMPXCHG instruction.]
 */
static inline int __mutex_fastpath_trylock(atomic_t *count,
					   int (*fail_fn)(atomic_t *))
{
	if (likely(atomic_cmpxchg(count, 1, 0) == 1))
		return 1;
	else
		return 0;
}

#endif /* _ASM_X86_MUTEX_64_H */
