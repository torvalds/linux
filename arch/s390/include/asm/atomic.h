/* SPDX-License-Identifier: GPL-2.0 */
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
	/*
	 * Order of conditions is important to circumvent gcc 10 bug:
	 * https://gcc.gnu.org/pipermail/gcc-patches/2020-July/549318.html
	 */
	if ((i > -129) && (i < 128) && __builtin_constant_p(i)) {
		__atomic_add_const(i, &v->counter);
		return;
	}
#endif
	__atomic_add(i, &v->counter);
}

#define atomic_sub(_i, _v)		atomic_add(-(int)(_i), _v)
#define atomic_sub_return(_i, _v)	atomic_add_return(-(int)(_i), _v)
#define atomic_fetch_sub(_i, _v)	atomic_fetch_add(-(int)(_i), _v)

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

#define ATOMIC64_INIT(i)  { (i) }

static inline s64 atomic64_read(const atomic64_t *v)
{
	s64 c;

	asm volatile(
		"	lg	%0,%1\n"
		: "=d" (c) : "Q" (v->counter));
	return c;
}

static inline void atomic64_set(atomic64_t *v, s64 i)
{
	asm volatile(
		"	stg	%1,%0\n"
		: "=Q" (v->counter) : "d" (i));
}

static inline s64 atomic64_add_return(s64 i, atomic64_t *v)
{
	return __atomic64_add_barrier(i, (long *)&v->counter) + i;
}

static inline s64 atomic64_fetch_add(s64 i, atomic64_t *v)
{
	return __atomic64_add_barrier(i, (long *)&v->counter);
}

static inline void atomic64_add(s64 i, atomic64_t *v)
{
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	/*
	 * Order of conditions is important to circumvent gcc 10 bug:
	 * https://gcc.gnu.org/pipermail/gcc-patches/2020-July/549318.html
	 */
	if ((i > -129) && (i < 128) && __builtin_constant_p(i)) {
		__atomic64_add_const(i, (long *)&v->counter);
		return;
	}
#endif
	__atomic64_add(i, (long *)&v->counter);
}

#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static inline s64 atomic64_cmpxchg(atomic64_t *v, s64 old, s64 new)
{
	return __atomic64_cmpxchg((long *)&v->counter, old, new);
}

#define ATOMIC64_OPS(op)						\
static inline void atomic64_##op(s64 i, atomic64_t *v)			\
{									\
	__atomic64_##op(i, (long *)&v->counter);			\
}									\
static inline long atomic64_fetch_##op(s64 i, atomic64_t *v)		\
{									\
	return __atomic64_##op##_barrier(i, (long *)&v->counter);	\
}

ATOMIC64_OPS(and)
ATOMIC64_OPS(or)
ATOMIC64_OPS(xor)

#undef ATOMIC64_OPS

#define atomic64_sub_return(_i, _v)	atomic64_add_return(-(s64)(_i), _v)
#define atomic64_fetch_sub(_i, _v)	atomic64_fetch_add(-(s64)(_i), _v)
#define atomic64_sub(_i, _v)		atomic64_add(-(s64)(_i), _v)

#endif /* __ARCH_S390_ATOMIC__  */
