/*
 * Copyright IBM Corp. 1999, 2016
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *	      Denis Joseph Barrow,
 *	      Arnd Bergmann,
 */

#ifndef __ARCH_S390_ATOMIC__
#define __ARCH_S390_ATOMIC__

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/atomic_ops.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#define ATOMIC_INIT(i)  { (i) }

static inline int atomic_read(const atomic_t *v)
{
	int c;

	asm volatile(
		"	l	%0,%1\n"
		: "=d" (c) : "Q" (v->counter));
	return c;
}

static inline void atomic_set(atomic_t *v, int i)
{
	asm volatile(
		"	st	%1,%0\n"
		: "=Q" (v->counter) : "d" (i));
}

static inline int atomic_add_return(int i, atomic_t *v)
{
	return __atomic_add_barrier(i, &v->counter) + i;
}

static inline int atomic_fetch_add(int i, atomic_t *v)
{
	return __atomic_add_barrier(i, &v->counter);
}

static inline void atomic_add(int i, atomic_t *v)
{
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	if (__builtin_constant_p(i) && (i > -129) && (i < 128)) {
		__atomic_add_const(i, &v->counter);
		return;
	}
#endif
	__atomic_add(i, &v->counter);
}

#define atomic_add_negative(_i, _v)	(atomic_add_return(_i, _v) < 0)
#define atomic_inc(_v)			atomic_add(1, _v)
#define atomic_inc_return(_v)		atomic_add_return(1, _v)
#define atomic_inc_and_test(_v)		(atomic_add_return(1, _v) == 0)
#define atomic_sub(_i, _v)		atomic_add(-(int)(_i), _v)
#define atomic_sub_return(_i, _v)	atomic_add_return(-(int)(_i), _v)
#define atomic_fetch_sub(_i, _v)	atomic_fetch_add(-(int)(_i), _v)
#define atomic_sub_and_test(_i, _v)	(atomic_sub_return(_i, _v) == 0)
#define atomic_dec(_v)			atomic_sub(1, _v)
#define atomic_dec_return(_v)		atomic_sub_return(1, _v)
#define atomic_dec_and_test(_v)		(atomic_sub_return(1, _v) == 0)

#define ATOMIC_OPS(op)							\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	__atomic_##op(i, &v->counter);					\
}									\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	return __atomic_##op##_barrier(i, &v->counter);			\
}

ATOMIC_OPS(and)
ATOMIC_OPS(or)
ATOMIC_OPS(xor)

#undef ATOMIC_OPS

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	return __atomic_cmpxchg(&v->counter, old, new);
}

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == u))
			break;
		old = atomic_cmpxchg(v, c, c + a);
		if (likely(old == c))
			break;
		c = old;
	}
	return c;
}

#define ATOMIC64_INIT(i)  { (i) }

static inline long atomic64_read(const atomic64_t *v)
{
	long c;

	asm volatile(
		"	lg	%0,%1\n"
		: "=d" (c) : "Q" (v->counter));
	return c;
}

static inline void atomic64_set(atomic64_t *v, long i)
{
	asm volatile(
		"	stg	%1,%0\n"
		: "=Q" (v->counter) : "d" (i));
}

static inline long atomic64_add_return(long i, atomic64_t *v)
{
	return __atomic64_add_barrier(i, &v->counter) + i;
}

static inline long atomic64_fetch_add(long i, atomic64_t *v)
{
	return __atomic64_add_barrier(i, &v->counter);
}

static inline void atomic64_add(long i, atomic64_t *v)
{
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	if (__builtin_constant_p(i) && (i > -129) && (i < 128)) {
		__atomic64_add_const(i, &v->counter);
		return;
	}
#endif
	__atomic64_add(i, &v->counter);
}

#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static inline long atomic64_cmpxchg(atomic64_t *v, long old, long new)
{
	return __atomic64_cmpxchg(&v->counter, old, new);
}

#define ATOMIC64_OPS(op)						\
static inline void atomic64_##op(long i, atomic64_t *v)			\
{									\
	__atomic64_##op(i, &v->counter);				\
}									\
static inline long atomic64_fetch_##op(long i, atomic64_t *v)		\
{									\
	return __atomic64_##op##_barrier(i, &v->counter);		\
}

ATOMIC64_OPS(and)
ATOMIC64_OPS(or)
ATOMIC64_OPS(xor)

#undef ATOMIC64_OPS

static inline int atomic64_add_unless(atomic64_t *v, long i, long u)
{
	long c, old;

	c = atomic64_read(v);
	for (;;) {
		if (unlikely(c == u))
			break;
		old = atomic64_cmpxchg(v, c, c + i);
		if (likely(old == c))
			break;
		c = old;
	}
	return c != u;
}

static inline long atomic64_dec_if_positive(atomic64_t *v)
{
	long c, old, dec;

	c = atomic64_read(v);
	for (;;) {
		dec = c - 1;
		if (unlikely(dec < 0))
			break;
		old = atomic64_cmpxchg((v), c, dec);
		if (likely(old == c))
			break;
		c = old;
	}
	return dec;
}

#define atomic64_add_negative(_i, _v)	(atomic64_add_return(_i, _v) < 0)
#define atomic64_inc(_v)		atomic64_add(1, _v)
#define atomic64_inc_return(_v)		atomic64_add_return(1, _v)
#define atomic64_inc_and_test(_v)	(atomic64_add_return(1, _v) == 0)
#define atomic64_sub_return(_i, _v)	atomic64_add_return(-(long)(_i), _v)
#define atomic64_fetch_sub(_i, _v)	atomic64_fetch_add(-(long)(_i), _v)
#define atomic64_sub(_i, _v)		atomic64_add(-(long)(_i), _v)
#define atomic64_sub_and_test(_i, _v)	(atomic64_sub_return(_i, _v) == 0)
#define atomic64_dec(_v)		atomic64_sub(1, _v)
#define atomic64_dec_return(_v)		atomic64_sub_return(1, _v)
#define atomic64_dec_and_test(_v)	(atomic64_sub_return(1, _v) == 0)
#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)

#endif /* __ARCH_S390_ATOMIC__  */
