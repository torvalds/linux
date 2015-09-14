/*
 * include/asm-xtensa/atomic.h
 *
 * Atomic operations that C can't guarantee us.  Useful for resource counting..
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2008 Tensilica Inc.
 */

#ifndef _XTENSA_ATOMIC_H
#define _XTENSA_ATOMIC_H

#include <linux/stringify.h>
#include <linux/types.h>

#ifdef __KERNEL__
#include <asm/processor.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#define ATOMIC_INIT(i)	{ (i) }

/*
 * This Xtensa implementation assumes that the right mechanism
 * for exclusion is for locking interrupts to level EXCM_LEVEL.
 *
 * Locking interrupts looks like this:
 *
 *    rsil a15, TOPLEVEL
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
#define atomic_read(v)		ACCESS_ONCE((v)->counter)

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#define atomic_set(v,i)		((v)->counter = (i))

#if XCHAL_HAVE_S32C1I
#define ATOMIC_OP(op)							\
static inline void atomic_##op(int i, atomic_t * v)			\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	__asm__ __volatile__(						\
			"1:     l32i    %1, %3, 0\n"			\
			"       wsr     %1, scompare1\n"		\
			"       " #op " %0, %1, %2\n"			\
			"       s32c1i  %0, %3, 0\n"			\
			"       bne     %0, %1, 1b\n"			\
			: "=&a" (result), "=&a" (tmp)			\
			: "a" (i), "a" (v)				\
			: "memory"					\
			);						\
}									\

#define ATOMIC_OP_RETURN(op)						\
static inline int atomic_##op##_return(int i, atomic_t * v)		\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	__asm__ __volatile__(						\
			"1:     l32i    %1, %3, 0\n"			\
			"       wsr     %1, scompare1\n"		\
			"       " #op " %0, %1, %2\n"			\
			"       s32c1i  %0, %3, 0\n"			\
			"       bne     %0, %1, 1b\n"			\
			"       " #op " %0, %0, %2\n"			\
			: "=&a" (result), "=&a" (tmp)			\
			: "a" (i), "a" (v)				\
			: "memory"					\
			);						\
									\
	return result;							\
}

#else /* XCHAL_HAVE_S32C1I */

#define ATOMIC_OP(op)							\
static inline void atomic_##op(int i, atomic_t * v)			\
{									\
	unsigned int vval;						\
									\
	__asm__ __volatile__(						\
			"       rsil    a15, "__stringify(TOPLEVEL)"\n"\
			"       l32i    %0, %2, 0\n"			\
			"       " #op " %0, %0, %1\n"			\
			"       s32i    %0, %2, 0\n"			\
			"       wsr     a15, ps\n"			\
			"       rsync\n"				\
			: "=&a" (vval)					\
			: "a" (i), "a" (v)				\
			: "a15", "memory"				\
			);						\
}									\

#define ATOMIC_OP_RETURN(op)						\
static inline int atomic_##op##_return(int i, atomic_t * v)		\
{									\
	unsigned int vval;						\
									\
	__asm__ __volatile__(						\
			"       rsil    a15,"__stringify(TOPLEVEL)"\n"	\
			"       l32i    %0, %2, 0\n"			\
			"       " #op " %0, %0, %1\n"			\
			"       s32i    %0, %2, 0\n"			\
			"       wsr     a15, ps\n"			\
			"       rsync\n"				\
			: "=&a" (vval)					\
			: "a" (i), "a" (v)				\
			: "a15", "memory"				\
			);						\
									\
	return vval;							\
}

#endif /* XCHAL_HAVE_S32C1I */

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

ATOMIC_OP(and)
ATOMIC_OP(or)
ATOMIC_OP(xor)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

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
 * __atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int __atomic_add_unless(atomic_t *v, int a, int u)
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
	return c;
}

#endif /* __KERNEL__ */

#endif /* _XTENSA_ATOMIC_H */
