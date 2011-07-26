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
 *
 * Atomic primitives.
 */

#ifndef _ASM_TILE_ATOMIC_H
#define _ASM_TILE_ATOMIC_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <asm/system.h>

#define ATOMIC_INIT(i)	{ (i) }

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
static inline int atomic_read(const atomic_t *v)
{
	return ACCESS_ONCE(v->counter);
}

/**
 * atomic_sub_return - subtract integer and return
 * @v: pointer of type atomic_t
 * @i: integer value to subtract
 *
 * Atomically subtracts @i from @v and returns @v - @i
 */
#define atomic_sub_return(i, v)		atomic_add_return((int)(-(i)), (v))

/**
 * atomic_sub - subtract integer from atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.
 */
#define atomic_sub(i, v)		atomic_add((int)(-(i)), (v))

/**
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns true if the result is
 * zero, or false for all other cases.
 */
#define atomic_sub_and_test(i, v)	(atomic_sub_return((i), (v)) == 0)

/**
 * atomic_inc_return - increment memory and return
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1 and returns the new value.
 */
#define atomic_inc_return(v)		atomic_add_return(1, (v))

/**
 * atomic_dec_return - decrement memory and return
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and returns the new value.
 */
#define atomic_dec_return(v)		atomic_sub_return(1, (v))

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
#define atomic_inc(v)			atomic_add(1, (v))

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
#define atomic_dec(v)			atomic_sub(1, (v))

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and returns true if the result is 0.
 */
#define atomic_dec_and_test(v)		(atomic_dec_return(v) == 0)

/**
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1 and returns true if the result is 0.
 */
#define atomic_inc_and_test(v)		(atomic_inc_return(v) == 0)

/**
 * atomic_add_negative - add and test if negative
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true if the result is
 * negative, or false when result is greater than or equal to zero.
 */
#define atomic_add_negative(i, v)	(atomic_add_return((i), (v)) < 0)

/* Nonexistent functions intended to cause link errors. */
extern unsigned long __xchg_called_with_bad_pointer(void);
extern unsigned long __cmpxchg_called_with_bad_pointer(void);

#define xchg(ptr, x)							\
	({								\
		typeof(*(ptr)) __x;					\
		switch (sizeof(*(ptr))) {				\
		case 4:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic_xchg( \
				(atomic_t *)(ptr),			\
				(u32)(typeof((x)-(x)))(x));		\
			break;						\
		case 8:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic64_xchg( \
				(atomic64_t *)(ptr),			\
				(u64)(typeof((x)-(x)))(x));		\
			break;						\
		default:						\
			__xchg_called_with_bad_pointer();		\
		}							\
		__x;							\
	})

#define cmpxchg(ptr, o, n)						\
	({								\
		typeof(*(ptr)) __x;					\
		switch (sizeof(*(ptr))) {				\
		case 4:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic_cmpxchg( \
				(atomic_t *)(ptr),			\
				(u32)(typeof((o)-(o)))(o),		\
				(u32)(typeof((n)-(n)))(n));		\
			break;						\
		case 8:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic64_cmpxchg( \
				(atomic64_t *)(ptr),			\
				(u64)(typeof((o)-(o)))(o),		\
				(u64)(typeof((n)-(n)))(n));		\
			break;						\
		default:						\
			__cmpxchg_called_with_bad_pointer();		\
		}							\
		__x;							\
	})

#define tas(ptr) (xchg((ptr), 1))

#endif /* __ASSEMBLY__ */

#ifndef __tilegx__
#include <asm/atomic_32.h>
#else
#include <asm/atomic_64.h>
#endif

#endif /* _ASM_TILE_ATOMIC_H */
