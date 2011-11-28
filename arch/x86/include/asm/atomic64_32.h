#ifndef _ASM_X86_ATOMIC64_32_H
#define _ASM_X86_ATOMIC64_32_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/processor.h>
//#include <asm/cmpxchg.h>

/* An 64bit atomic type */

typedef struct {
	u64 __aligned(8) counter;
} atomic64_t;

#define ATOMIC64_INIT(val)	{ (val) }

#ifdef CONFIG_X86_CMPXCHG64
#define ATOMIC64_ALTERNATIVE_(f, g) "call atomic64_" #g "_cx8"
#else
#define ATOMIC64_ALTERNATIVE_(f, g) ALTERNATIVE("call atomic64_" #f "_386", "call atomic64_" #g "_cx8", X86_FEATURE_CX8)
#endif

#define ATOMIC64_ALTERNATIVE(f) ATOMIC64_ALTERNATIVE_(f, f)

/**
 * atomic64_cmpxchg - cmpxchg atomic64 variable
 * @p: pointer to type atomic64_t
 * @o: expected value
 * @n: new value
 *
 * Atomically sets @v to @n if it was equal to @o and returns
 * the old value.
 */

static inline long long atomic64_cmpxchg(atomic64_t *v, long long o, long long n)
{
	return cmpxchg64(&v->counter, o, n);
}

/**
 * atomic64_xchg - xchg atomic64 variable
 * @v: pointer to type atomic64_t
 * @n: value to assign
 *
 * Atomically xchgs the value of @v to @n and returns
 * the old value.
 */
static inline long long atomic64_xchg(atomic64_t *v, long long n)
{
	long long o;
	unsigned high = (unsigned)(n >> 32);
	unsigned low = (unsigned)n;
	asm volatile(ATOMIC64_ALTERNATIVE(xchg)
		     : "=A" (o), "+b" (low), "+c" (high)
		     : "S" (v)
		     : "memory"
		     );
	return o;
}

/**
 * atomic64_set - set atomic64 variable
 * @v: pointer to type atomic64_t
 * @n: value to assign
 *
 * Atomically sets the value of @v to @n.
 */
static inline void atomic64_set(atomic64_t *v, long long i)
{
	unsigned high = (unsigned)(i >> 32);
	unsigned low = (unsigned)i;
	asm volatile(ATOMIC64_ALTERNATIVE(set)
		     : "+b" (low), "+c" (high)
		     : "S" (v)
		     : "eax", "edx", "memory"
		     );
}

/**
 * atomic64_read - read atomic64 variable
 * @v: pointer to type atomic64_t
 *
 * Atomically reads the value of @v and returns it.
 */
static inline long long atomic64_read(atomic64_t *v)
{
	long long r;
	asm volatile(ATOMIC64_ALTERNATIVE(read)
		     : "=A" (r), "+c" (v)
		     : : "memory"
		     );
	return r;
 }

/**
 * atomic64_add_return - add and return
 * @i: integer value to add
 * @v: pointer to type atomic64_t
 *
 * Atomically adds @i to @v and returns @i + *@v
 */
static inline long long atomic64_add_return(long long i, atomic64_t *v)
{
	asm volatile(ATOMIC64_ALTERNATIVE(add_return)
		     : "+A" (i), "+c" (v)
		     : : "memory"
		     );
	return i;
}

/*
 * Other variants with different arithmetic operators:
 */
static inline long long atomic64_sub_return(long long i, atomic64_t *v)
{
	asm volatile(ATOMIC64_ALTERNATIVE(sub_return)
		     : "+A" (i), "+c" (v)
		     : : "memory"
		     );
	return i;
}

static inline long long atomic64_inc_return(atomic64_t *v)
{
	long long a;
	asm volatile(ATOMIC64_ALTERNATIVE(inc_return)
		     : "=A" (a)
		     : "S" (v)
		     : "memory", "ecx"
		     );
	return a;
}

static inline long long atomic64_dec_return(atomic64_t *v)
{
	long long a;
	asm volatile(ATOMIC64_ALTERNATIVE(dec_return)
		     : "=A" (a)
		     : "S" (v)
		     : "memory", "ecx"
		     );
	return a;
}

/**
 * atomic64_add - add integer to atomic64 variable
 * @i: integer value to add
 * @v: pointer to type atomic64_t
 *
 * Atomically adds @i to @v.
 */
static inline long long atomic64_add(long long i, atomic64_t *v)
{
	asm volatile(ATOMIC64_ALTERNATIVE_(add, add_return)
		     : "+A" (i), "+c" (v)
		     : : "memory"
		     );
	return i;
}

/**
 * atomic64_sub - subtract the atomic64 variable
 * @i: integer value to subtract
 * @v: pointer to type atomic64_t
 *
 * Atomically subtracts @i from @v.
 */
static inline long long atomic64_sub(long long i, atomic64_t *v)
{
	asm volatile(ATOMIC64_ALTERNATIVE_(sub, sub_return)
		     : "+A" (i), "+c" (v)
		     : : "memory"
		     );
	return i;
}

/**
 * atomic64_sub_and_test - subtract value from variable and test result
 * @i: integer value to subtract
 * @v: pointer to type atomic64_t
  *
 * Atomically subtracts @i from @v and returns
 * true if the result is zero, or false for all
 * other cases.
 */
static inline int atomic64_sub_and_test(long long i, atomic64_t *v)
{
	return atomic64_sub_return(i, v) == 0;
}

/**
 * atomic64_inc - increment atomic64 variable
 * @v: pointer to type atomic64_t
 *
 * Atomically increments @v by 1.
 */
static inline void atomic64_inc(atomic64_t *v)
{
	asm volatile(ATOMIC64_ALTERNATIVE_(inc, inc_return)
		     : : "S" (v)
		     : "memory", "eax", "ecx", "edx"
		     );
}

/**
 * atomic64_dec - decrement atomic64 variable
 * @ptr: pointer to type atomic64_t
 *
 * Atomically decrements @ptr by 1.
 */
static inline void atomic64_dec(atomic64_t *v)
{
	asm volatile(ATOMIC64_ALTERNATIVE_(dec, dec_return)
		     : : "S" (v)
		     : "memory", "eax", "ecx", "edx"
		     );
}

/**
 * atomic64_dec_and_test - decrement and test
 * @v: pointer to type atomic64_t
 *
 * Atomically decrements @v by 1 and
 * returns true if the result is 0, or false for all other
 * cases.
 */
static inline int atomic64_dec_and_test(atomic64_t *v)
{
	return atomic64_dec_return(v) == 0;
}

/**
 * atomic64_inc_and_test - increment and test
 * @v: pointer to type atomic64_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
static inline int atomic64_inc_and_test(atomic64_t *v)
{
	return atomic64_inc_return(v) == 0;
}

/**
 * atomic64_add_negative - add and test if negative
 * @i: integer value to add
 * @v: pointer to type atomic64_t
 *
 * Atomically adds @i to @v and returns true
 * if the result is negative, or false when
 * result is greater than or equal to zero.
 */
static inline int atomic64_add_negative(long long i, atomic64_t *v)
{
	return atomic64_add_return(i, v) < 0;
}

/**
 * atomic64_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static inline int atomic64_add_unless(atomic64_t *v, long long a, long long u)
{
	unsigned low = (unsigned)u;
	unsigned high = (unsigned)(u >> 32);
	asm volatile(ATOMIC64_ALTERNATIVE(add_unless) "\n\t"
		     : "+A" (a), "+c" (v), "+S" (low), "+D" (high)
		     : : "memory");
	return (int)a;
}


static inline int atomic64_inc_not_zero(atomic64_t *v)
{
	int r;
	asm volatile(ATOMIC64_ALTERNATIVE(inc_not_zero)
		     : "=a" (r)
		     : "S" (v)
		     : "ecx", "edx", "memory"
		     );
	return r;
}

static inline long long atomic64_dec_if_positive(atomic64_t *v)
{
	long long r;
	asm volatile(ATOMIC64_ALTERNATIVE(dec_if_positive)
		     : "=A" (r)
		     : "S" (v)
		     : "ecx", "memory"
		     );
	return r;
}

#undef ATOMIC64_ALTERNATIVE
#undef ATOMIC64_ALTERNATIVE_

#endif /* _ASM_X86_ATOMIC64_32_H */
