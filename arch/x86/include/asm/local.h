/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_LOCAL_H
#define _ASM_X86_LOCAL_H

#include <linux/percpu.h>

#include <linux/atomic.h>
#include <asm/asm.h>

typedef struct {
	atomic_long_t a;
} local_t;

#define LOCAL_INIT(i)	{ ATOMIC_LONG_INIT(i) }

#define local_read(l)	atomic_long_read(&(l)->a)
#define local_set(l, i)	atomic_long_set(&(l)->a, (i))

static inline void local_inc(local_t *l)
{
	asm volatile(_ASM_INC "%0"
		     : "+m" (l->a.counter));
}

static inline void local_dec(local_t *l)
{
	asm volatile(_ASM_DEC "%0"
		     : "+m" (l->a.counter));
}

static inline void local_add(long i, local_t *l)
{
	asm volatile(_ASM_ADD "%1,%0"
		     : "+m" (l->a.counter)
		     : "ir" (i));
}

static inline void local_sub(long i, local_t *l)
{
	asm volatile(_ASM_SUB "%1,%0"
		     : "+m" (l->a.counter)
		     : "ir" (i));
}

/**
 * local_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @l: pointer to type local_t
 *
 * Atomically subtracts @i from @l and returns
 * true if the result is zero, or false for all
 * other cases.
 */
static inline bool local_sub_and_test(long i, local_t *l)
{
	return GEN_BINARY_RMWcc(_ASM_SUB, l->a.counter, e, "er", i);
}

/**
 * local_dec_and_test - decrement and test
 * @l: pointer to type local_t
 *
 * Atomically decrements @l by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static inline bool local_dec_and_test(local_t *l)
{
	return GEN_UNARY_RMWcc(_ASM_DEC, l->a.counter, e);
}

/**
 * local_inc_and_test - increment and test
 * @l: pointer to type local_t
 *
 * Atomically increments @l by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
static inline bool local_inc_and_test(local_t *l)
{
	return GEN_UNARY_RMWcc(_ASM_INC, l->a.counter, e);
}

/**
 * local_add_negative - add and test if negative
 * @i: integer value to add
 * @l: pointer to type local_t
 *
 * Atomically adds @i to @l and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
static inline bool local_add_negative(long i, local_t *l)
{
	return GEN_BINARY_RMWcc(_ASM_ADD, l->a.counter, s, "er", i);
}

/**
 * local_add_return - add and return
 * @i: integer value to add
 * @l: pointer to type local_t
 *
 * Atomically adds @i to @l and returns @i + @l
 */
static inline long local_add_return(long i, local_t *l)
{
	long __i = i;
	asm volatile(_ASM_XADD "%0, %1;"
		     : "+r" (i), "+m" (l->a.counter)
		     : : "memory");
	return i + __i;
}

static inline long local_sub_return(long i, local_t *l)
{
	return local_add_return(-i, l);
}

#define local_inc_return(l)  (local_add_return(1, l))
#define local_dec_return(l)  (local_sub_return(1, l))

static inline long local_cmpxchg(local_t *l, long old, long new)
{
	return cmpxchg_local(&l->a.counter, old, new);
}

static inline bool local_try_cmpxchg(local_t *l, long *old, long new)
{
	return try_cmpxchg_local(&l->a.counter,
				 (typeof(l->a.counter) *) old, new);
}

/*
 * Implement local_xchg using CMPXCHG instruction without the LOCK prefix.
 * XCHG is expensive due to the implied LOCK prefix.  The processor
 * cannot prefetch cachelines if XCHG is used.
 */
static __always_inline long
local_xchg(local_t *l, long n)
{
	long c = local_read(l);

	do { } while (!local_try_cmpxchg(l, &c, n));

	return c;
}

/**
 * local_add_unless - add unless the number is already a given value
 * @l: pointer of type local_t
 * @a: the amount to add to l...
 * @u: ...unless l is equal to u.
 *
 * Atomically adds @a to @l, if @v was not already @u.
 * Returns true if the addition was done.
 */
static __always_inline bool
local_add_unless(local_t *l, long a, long u)
{
	long c = local_read(l);

	do {
		if (unlikely(c == u))
			return false;
	} while (!local_try_cmpxchg(l, &c, c + a));

	return true;
}

#define local_inc_not_zero(l) local_add_unless((l), 1, 0)

/* On x86_32, these are no better than the atomic variants.
 * On x86-64 these are better than the atomic variants on SMP kernels
 * because they dont use a lock prefix.
 */
#define __local_inc(l)		local_inc(l)
#define __local_dec(l)		local_dec(l)
#define __local_add(i, l)	local_add((i), (l))
#define __local_sub(i, l)	local_sub((i), (l))

#endif /* _ASM_X86_LOCAL_H */
