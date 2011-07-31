/*
 * include/asm-xtensa/atomic.h
 *
 * Atomic operations that C can't guarantee us.  Useful for resource counting..
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_ATOMIC_H
#define _XTENSA_ATOMIC_H

#include <linux/stringify.h>
#include <linux/types.h>

#ifdef __KERNEL__
#include <asm/processor.h>
#include <asm/system.h>

#define ATOMIC_INIT(i)	{ (i) }

/*
 * This Xtensa implementation assumes that the right mechanism
 * for exclusion is for locking interrupts to level 1.
 *
 * Locking interrupts looks like this:
 *
 *    rsil a15, 1
 *    <code>
 *    wsr  a15, PS
 *    rsync
 *
 * Note that a15 is used here because the register allocation
 * done by the compiler is not guaranteed and a window overflow
 * may not occur between the rsil and wsr instructions. By using
 * a15 in the rsil, the machine is guaranteed to be in a state
 * where no register reference will cause an overflow.
 */

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
#define atomic_read(v)		(*(volatile int *)&(v)->counter)

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#define atomic_set(v,i)		((v)->counter = (i))

/**
 * atomic_add - add integer to atomic variable
 * @i: integer value to add
 * @v: pointer of type atomic_t
 *
 * Atomically adds @i to @v.
 */
static inline void atomic_add(int i, atomic_t * v)
{
    unsigned int vval;

    __asm__ __volatile__(
	"rsil    a15, "__stringify(LOCKLEVEL)"\n\t"
	"l32i    %0, %2, 0              \n\t"
	"add     %0, %0, %1             \n\t"
	"s32i    %0, %2, 0              \n\t"
	"wsr     a15, "__stringify(PS)"       \n\t"
	"rsync                          \n"
	: "=&a" (vval)
	: "a" (i), "a" (v)
	: "a15", "memory"
	);
}

/**
 * atomic_sub - subtract the atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v.
 */
static inline void atomic_sub(int i, atomic_t *v)
{
    unsigned int vval;

    __asm__ __volatile__(
	"rsil    a15, "__stringify(LOCKLEVEL)"\n\t"
	"l32i    %0, %2, 0              \n\t"
	"sub     %0, %0, %1             \n\t"
	"s32i    %0, %2, 0              \n\t"
	"wsr     a15, "__stringify(PS)"       \n\t"
	"rsync                          \n"
	: "=&a" (vval)
	: "a" (i), "a" (v)
	: "a15", "memory"
	);
}

/*
 * We use atomic_{add|sub}_return to define other functions.
 */

static inline int atomic_add_return(int i, atomic_t * v)
{
     unsigned int vval;

    __asm__ __volatile__(
	"rsil    a15,"__stringify(LOCKLEVEL)"\n\t"
	"l32i    %0, %2, 0             \n\t"
	"add     %0, %0, %1            \n\t"
	"s32i    %0, %2, 0             \n\t"
	"wsr     a15, "__stringify(PS)"      \n\t"
	"rsync                         \n"
	: "=&a" (vval)
	: "a" (i), "a" (v)
	: "a15", "memory"
	);

    return vval;
}

static inline int atomic_sub_return(int i, atomic_t * v)
{
    unsigned int vval;

    __asm__ __volatile__(
	"rsil    a15,"__stringify(LOCKLEVEL)"\n\t"
	"l32i    %0, %2, 0             \n\t"
	"sub     %0, %0, %1            \n\t"
	"s32i    %0, %2, 0             \n\t"
	"wsr     a15, "__stringify(PS)"       \n\t"
	"rsync                         \n"
	: "=&a" (vval)
	: "a" (i), "a" (v)
	: "a15", "memory"
	);

    return vval;
}

/**
 * atomic_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
#define atomic_sub_and_test(i,v) (atomic_sub_return((i),(v)) == 0)

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
#define atomic_inc(v) atomic_add(1,(v))

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
#define atomic_inc_return(v) atomic_add_return(1,(v))

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
#define atomic_dec(v) atomic_sub(1,(v))

/**
 * atomic_dec_return - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
#define atomic_dec_return(v) atomic_sub_return(1,(v))

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
#define atomic_dec_and_test(v) (atomic_sub_return(1,(v)) == 0)

/**
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_add_return(1,(v)) == 0)

/**
 * atomic_add_negative - add and test if negative
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#define atomic_add_negative(i,v) (atomic_add_return((i),(v)) < 0)

#define atomic_cmpxchg(v, o, n) ((int)cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns non-zero if @v was not @u, and zero otherwise.
 */
static __inline__ int atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c != (u);
}

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

static inline void atomic_clear_mask(unsigned int mask, atomic_t *v)
{
    unsigned int all_f = -1;
    unsigned int vval;

    __asm__ __volatile__(
	"rsil    a15,"__stringify(LOCKLEVEL)"\n\t"
	"l32i    %0, %2, 0             \n\t"
	"xor     %1, %4, %3            \n\t"
	"and     %0, %0, %4            \n\t"
	"s32i    %0, %2, 0             \n\t"
	"wsr     a15, "__stringify(PS)"      \n\t"
	"rsync                         \n"
	: "=&a" (vval), "=a" (mask)
	: "a" (v), "a" (all_f), "1" (mask)
	: "a15", "memory"
	);
}

static inline void atomic_set_mask(unsigned int mask, atomic_t *v)
{
    unsigned int vval;

    __asm__ __volatile__(
	"rsil    a15,"__stringify(LOCKLEVEL)"\n\t"
	"l32i    %0, %2, 0             \n\t"
	"or      %0, %0, %1            \n\t"
	"s32i    %0, %2, 0             \n\t"
	"wsr     a15, "__stringify(PS)"       \n\t"
	"rsync                         \n"
	: "=&a" (vval)
	: "a" (mask), "a" (v)
	: "a15", "memory"
	);
}

/* Atomic operations are already serializing */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#include <asm-generic/atomic-long.h>
#endif /* __KERNEL__ */

#endif /* _XTENSA_ATOMIC_H */

