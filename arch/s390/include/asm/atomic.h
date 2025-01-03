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

static __always_inline int arch_atomic_read(const atomic_t *v)
{
	return __atomic_read(v);
}
#define arch_atomic_read arch_atomic_read

static __always_inline void arch_atomic_set(atomic_t *v, int i)
{
	__atomic_set(v, i);
}
#define arch_atomic_set arch_atomic_set

static __always_inline int arch_atomic_add_return(int i, atomic_t *v)
{
	return __atomic_add_barrier(i, &v->counter) + i;
}
#define arch_atomic_add_return arch_atomic_add_return

static __always_inline int arch_atomic_fetch_add(int i, atomic_t *v)
{
	return __atomic_add_barrier(i, &v->counter);
}
#define arch_atomic_fetch_add arch_atomic_fetch_add

static __always_inline void arch_atomic_add(int i, atomic_t *v)
{
	__atomic_add(i, &v->counter);
}
#define arch_atomic_add arch_atomic_add

#define arch_atomic_sub(_i, _v)		arch_atomic_add(-(int)(_i), _v)
#define arch_atomic_sub_return(_i, _v)	arch_atomic_add_return(-(int)(_i), _v)
#define arch_atomic_fetch_sub(_i, _v)	arch_atomic_fetch_add(-(int)(_i), _v)

#define ATOMIC_OPS(op)							\
static __always_inline void arch_atomic_##op(int i, atomic_t *v)	\
{									\
	__atomic_##op(i, &v->counter);					\
}									\
static __always_inline int arch_atomic_fetch_##op(int i, atomic_t *v)	\
{									\
	return __atomic_##op##_barrier(i, &v->counter);			\
}

ATOMIC_OPS(and)
ATOMIC_OPS(or)
ATOMIC_OPS(xor)

#undef ATOMIC_OPS

#define arch_atomic_and			arch_atomic_and
#define arch_atomic_or			arch_atomic_or
#define arch_atomic_xor			arch_atomic_xor
#define arch_atomic_fetch_and		arch_atomic_fetch_and
#define arch_atomic_fetch_or		arch_atomic_fetch_or
#define arch_atomic_fetch_xor		arch_atomic_fetch_xor

static __always_inline int arch_atomic_xchg(atomic_t *v, int new)
{
	return arch_xchg(&v->counter, new);
}
#define arch_atomic_xchg arch_atomic_xchg

static __always_inline int arch_atomic_cmpxchg(atomic_t *v, int old, int new)
{
	return arch_cmpxchg(&v->counter, old, new);
}
#define arch_atomic_cmpxchg arch_atomic_cmpxchg

static __always_inline bool arch_atomic_try_cmpxchg(atomic_t *v, int *old, int new)
{
	return arch_try_cmpxchg(&v->counter, old, new);
}
#define arch_atomic_try_cmpxchg arch_atomic_try_cmpxchg

#define ATOMIC64_INIT(i)  { (i) }

static __always_inline s64 arch_atomic64_read(const atomic64_t *v)
{
	return __atomic64_read(v);
}
#define arch_atomic64_read arch_atomic64_read

static __always_inline void arch_atomic64_set(atomic64_t *v, s64 i)
{
	__atomic64_set(v, i);
}
#define arch_atomic64_set arch_atomic64_set

static __always_inline s64 arch_atomic64_add_return(s64 i, atomic64_t *v)
{
	return __atomic64_add_barrier(i, (long *)&v->counter) + i;
}
#define arch_atomic64_add_return arch_atomic64_add_return

static __always_inline s64 arch_atomic64_fetch_add(s64 i, atomic64_t *v)
{
	return __atomic64_add_barrier(i, (long *)&v->counter);
}
#define arch_atomic64_fetch_add arch_atomic64_fetch_add

static __always_inline void arch_atomic64_add(s64 i, atomic64_t *v)
{
	__atomic64_add(i, (long *)&v->counter);
}
#define arch_atomic64_add arch_atomic64_add

static __always_inline s64 arch_atomic64_xchg(atomic64_t *v, s64 new)
{
	return arch_xchg(&v->counter, new);
}
#define arch_atomic64_xchg arch_atomic64_xchg

static __always_inline s64 arch_atomic64_cmpxchg(atomic64_t *v, s64 old, s64 new)
{
	return arch_cmpxchg(&v->counter, old, new);
}
#define arch_atomic64_cmpxchg arch_atomic64_cmpxchg

static __always_inline bool arch_atomic64_try_cmpxchg(atomic64_t *v, s64 *old, s64 new)
{
	return arch_try_cmpxchg(&v->counter, old, new);
}
#define arch_atomic64_try_cmpxchg arch_atomic64_try_cmpxchg

#define ATOMIC64_OPS(op)							\
static __always_inline void arch_atomic64_##op(s64 i, atomic64_t *v)		\
{										\
	__atomic64_##op(i, (long *)&v->counter);				\
}										\
static __always_inline long arch_atomic64_fetch_##op(s64 i, atomic64_t *v)	\
{										\
	return __atomic64_##op##_barrier(i, (long *)&v->counter);		\
}

ATOMIC64_OPS(and)
ATOMIC64_OPS(or)
ATOMIC64_OPS(xor)

#undef ATOMIC64_OPS

#define arch_atomic64_and		arch_atomic64_and
#define arch_atomic64_or		arch_atomic64_or
#define arch_atomic64_xor		arch_atomic64_xor
#define arch_atomic64_fetch_and		arch_atomic64_fetch_and
#define arch_atomic64_fetch_or		arch_atomic64_fetch_or
#define arch_atomic64_fetch_xor		arch_atomic64_fetch_xor

#define arch_atomic64_sub_return(_i, _v) arch_atomic64_add_return(-(s64)(_i), _v)
#define arch_atomic64_fetch_sub(_i, _v)  arch_atomic64_fetch_add(-(s64)(_i), _v)
#define arch_atomic64_sub(_i, _v)	 arch_atomic64_add(-(s64)(_i), _v)

#endif /* __ARCH_S390_ATOMIC__  */
