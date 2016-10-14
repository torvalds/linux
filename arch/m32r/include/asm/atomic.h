#ifndef _ASM_M32R_ATOMIC_H
#define _ASM_M32R_ATOMIC_H

/*
 *  linux/include/asm-m32r/atomic.h
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/types.h>
#include <asm/assembler.h>
#include <asm/cmpxchg.h>
#include <asm/dcache_clear.h>
#include <asm/barrier.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#define ATOMIC_INIT(i)	{ (i) }

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
#define atomic_read(v)	READ_ONCE((v)->counter)

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#define atomic_set(v,i)	WRITE_ONCE(((v)->counter), (i))

#ifdef CONFIG_CHIP_M32700_TS1
#define __ATOMIC_CLOBBER	, "r4"
#else
#define __ATOMIC_CLOBBER
#endif

#define ATOMIC_OP(op)							\
static __inline__ void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long flags;						\
	int result;							\
									\
	local_irq_save(flags);						\
	__asm__ __volatile__ (						\
		"# atomic_" #op "		\n\t"			\
		DCACHE_CLEAR("%0", "r4", "%1")				\
		M32R_LOCK" %0, @%1;		\n\t"			\
		#op " %0, %2;			\n\t"			\
		M32R_UNLOCK" %0, @%1;		\n\t"			\
		: "=&r" (result)					\
		: "r" (&v->counter), "r" (i)				\
		: "memory"						\
		__ATOMIC_CLOBBER					\
	);								\
	local_irq_restore(flags);					\
}									\

#define ATOMIC_OP_RETURN(op)						\
static __inline__ int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
	int result;							\
									\
	local_irq_save(flags);						\
	__asm__ __volatile__ (						\
		"# atomic_" #op "_return	\n\t"			\
		DCACHE_CLEAR("%0", "r4", "%1")				\
		M32R_LOCK" %0, @%1;		\n\t"			\
		#op " %0, %2;			\n\t"			\
		M32R_UNLOCK" %0, @%1;		\n\t"			\
		: "=&r" (result)					\
		: "r" (&v->counter), "r" (i)				\
		: "memory"						\
		__ATOMIC_CLOBBER					\
	);								\
	local_irq_restore(flags);					\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(op)						\
static __inline__ int atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
	int result, val;						\
									\
	local_irq_save(flags);						\
	__asm__ __volatile__ (						\
		"# atomic_fetch_" #op "		\n\t"			\
		DCACHE_CLEAR("%0", "r4", "%2")				\
		M32R_LOCK" %1, @%2;		\n\t"			\
		"mv %0, %1			\n\t" 			\
		#op " %1, %3;			\n\t"			\
		M32R_UNLOCK" %1, @%2;		\n\t"			\
		: "=&r" (result), "=&r" (val)				\
		: "r" (&v->counter), "r" (i)				\
		: "memory"						\
		__ATOMIC_CLOBBER					\
	);								\
	local_irq_restore(flags);					\
									\
	return result;							\
}

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

#undef ATOMIC_OPS
#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(and)
ATOMIC_OPS(or)
ATOMIC_OPS(xor)

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
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
#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)

/**
 * atomic_inc_return - increment atomic variable and return it
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1 and returns the result.
 */
static __inline__ int atomic_inc_return(atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_inc_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"addi	%0, #1;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter)
		: "memory"
		__ATOMIC_CLOBBER
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_dec_return - decrement atomic variable and return it
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and returns the result.
 */
static __inline__ int atomic_dec_return(atomic_t *v)
{
	unsigned long flags;
	int result;

	local_irq_save(flags);
	__asm__ __volatile__ (
		"# atomic_dec_return		\n\t"
		DCACHE_CLEAR("%0", "r4", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"addi	%0, #-1;		\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (result)
		: "r" (&v->counter)
		: "memory"
		__ATOMIC_CLOBBER
	);
	local_irq_restore(flags);

	return result;
}

/**
 * atomic_inc - increment atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1.
 */
#define atomic_inc(v) ((void)atomic_inc_return(v))

/**
 * atomic_dec - decrement atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1.
 */
#define atomic_dec(v) ((void)atomic_dec_return(v))

/**
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

/**
 * atomic_dec_and_test - decrement and test
 * @v: pointer of type atomic_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all
 * other cases.
 */
#define atomic_dec_and_test(v) (atomic_dec_return(v) == 0)

/**
 * atomic_add_negative - add and test if negative
 * @v: pointer of type atomic_t
 * @i: integer value to add
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
#define atomic_add_negative(i,v) (atomic_add_return((i), (v)) < 0)

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

#endif	/* _ASM_M32R_ATOMIC_H */
