/*
 * Copyright IBM Corp. 1999, 2009
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *	      Denis Joseph Barrow,
 *	      Arnd Bergmann <arndb@de.ibm.com>,
 *
 * Atomic operations that C can't guarantee us.
 * Useful for resource counting etc.
 * s390 uses 'Compare And Swap' for atomicity in SMP environment.
 *
 */

#ifndef __ARCH_S390_ATOMIC__
#define __ARCH_S390_ATOMIC__

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#define ATOMIC_INIT(i)  { (i) }

#define __ATOMIC_NO_BARRIER	"\n"

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES

#define __ATOMIC_OR	"lao"
#define __ATOMIC_AND	"lan"
#define __ATOMIC_ADD	"laa"
#define __ATOMIC_XOR	"lax"
#define __ATOMIC_BARRIER "bcr	14,0\n"

#define __ATOMIC_LOOP(ptr, op_val, op_string, __barrier)		\
({									\
	int old_val;							\
									\
	typecheck(atomic_t *, ptr);					\
	asm volatile(							\
		__barrier						\
		op_string "	%0,%2,%1\n"				\
		__barrier						\
		: "=d" (old_val), "+Q" ((ptr)->counter)			\
		: "d" (op_val)						\
		: "cc", "memory");					\
	old_val;							\
})

#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

#define __ATOMIC_OR	"or"
#define __ATOMIC_AND	"nr"
#define __ATOMIC_ADD	"ar"
#define __ATOMIC_XOR	"xr"
#define __ATOMIC_BARRIER "\n"

#define __ATOMIC_LOOP(ptr, op_val, op_string, __barrier)		\
({									\
	int old_val, new_val;						\
									\
	typecheck(atomic_t *, ptr);					\
	asm volatile(							\
		"	l	%0,%2\n"				\
		"0:	lr	%1,%0\n"				\
		op_string "	%1,%3\n"				\
		"	cs	%0,%1,%2\n"				\
		"	jl	0b"					\
		: "=&d" (old_val), "=&d" (new_val), "+Q" ((ptr)->counter)\
		: "d" (op_val)						\
		: "cc", "memory");					\
	old_val;							\
})

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

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
	return __ATOMIC_LOOP(v, i, __ATOMIC_ADD, __ATOMIC_BARRIER) + i;
}

static inline void atomic_add(int i, atomic_t *v)
{
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	if (__builtin_constant_p(i) && (i > -129) && (i < 128)) {
		asm volatile(
			"asi	%0,%1\n"
			: "+Q" (v->counter)
			: "i" (i)
			: "cc", "memory");
		return;
	}
#endif
	__ATOMIC_LOOP(v, i, __ATOMIC_ADD, __ATOMIC_NO_BARRIER);
}

#define atomic_add_negative(_i, _v)	(atomic_add_return(_i, _v) < 0)
#define atomic_inc(_v)			atomic_add(1, _v)
#define atomic_inc_return(_v)		atomic_add_return(1, _v)
#define atomic_inc_and_test(_v)		(atomic_add_return(1, _v) == 0)
#define atomic_sub(_i, _v)		atomic_add(-(int)(_i), _v)
#define atomic_sub_return(_i, _v)	atomic_add_return(-(int)(_i), _v)
#define atomic_sub_and_test(_i, _v)	(atomic_sub_return(_i, _v) == 0)
#define atomic_dec(_v)			atomic_sub(1, _v)
#define atomic_dec_return(_v)		atomic_sub_return(1, _v)
#define atomic_dec_and_test(_v)		(atomic_sub_return(1, _v) == 0)

#define ATOMIC_OP(op, OP)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	__ATOMIC_LOOP(v, i, __ATOMIC_##OP, __ATOMIC_NO_BARRIER);	\
}

ATOMIC_OP(and, AND)
ATOMIC_OP(or, OR)
ATOMIC_OP(xor, XOR)

#undef ATOMIC_OP

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	asm volatile(
		"	cs	%0,%2,%1"
		: "+d" (old), "+Q" (v->counter)
		: "d" (new)
		: "cc", "memory");
	return old;
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


#undef __ATOMIC_LOOP

#define ATOMIC64_INIT(i)  { (i) }

#define __ATOMIC64_NO_BARRIER	"\n"

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES

#define __ATOMIC64_OR	"laog"
#define __ATOMIC64_AND	"lang"
#define __ATOMIC64_ADD	"laag"
#define __ATOMIC64_XOR	"laxg"
#define __ATOMIC64_BARRIER "bcr	14,0\n"

#define __ATOMIC64_LOOP(ptr, op_val, op_string, __barrier)		\
({									\
	long long old_val;						\
									\
	typecheck(atomic64_t *, ptr);					\
	asm volatile(							\
		__barrier						\
		op_string "	%0,%2,%1\n"				\
		__barrier						\
		: "=d" (old_val), "+Q" ((ptr)->counter)			\
		: "d" (op_val)						\
		: "cc", "memory");					\
	old_val;							\
})

#else /* CONFIG_HAVE_MARCH_Z196_FEATURES */

#define __ATOMIC64_OR	"ogr"
#define __ATOMIC64_AND	"ngr"
#define __ATOMIC64_ADD	"agr"
#define __ATOMIC64_XOR	"xgr"
#define __ATOMIC64_BARRIER "\n"

#define __ATOMIC64_LOOP(ptr, op_val, op_string, __barrier)		\
({									\
	long long old_val, new_val;					\
									\
	typecheck(atomic64_t *, ptr);					\
	asm volatile(							\
		"	lg	%0,%2\n"				\
		"0:	lgr	%1,%0\n"				\
		op_string "	%1,%3\n"				\
		"	csg	%0,%1,%2\n"				\
		"	jl	0b"					\
		: "=&d" (old_val), "=&d" (new_val), "+Q" ((ptr)->counter)\
		: "d" (op_val)						\
		: "cc", "memory");					\
	old_val;							\
})

#endif /* CONFIG_HAVE_MARCH_Z196_FEATURES */

static inline long long atomic64_read(const atomic64_t *v)
{
	long long c;

	asm volatile(
		"	lg	%0,%1\n"
		: "=d" (c) : "Q" (v->counter));
	return c;
}

static inline void atomic64_set(atomic64_t *v, long long i)
{
	asm volatile(
		"	stg	%1,%0\n"
		: "=Q" (v->counter) : "d" (i));
}

static inline long long atomic64_add_return(long long i, atomic64_t *v)
{
	return __ATOMIC64_LOOP(v, i, __ATOMIC64_ADD, __ATOMIC64_BARRIER) + i;
}

static inline void atomic64_add(long long i, atomic64_t *v)
{
#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
	if (__builtin_constant_p(i) && (i > -129) && (i < 128)) {
		asm volatile(
			"agsi	%0,%1\n"
			: "+Q" (v->counter)
			: "i" (i)
			: "cc", "memory");
		return;
	}
#endif
	__ATOMIC64_LOOP(v, i, __ATOMIC64_ADD, __ATOMIC64_NO_BARRIER);
}

#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static inline long long atomic64_cmpxchg(atomic64_t *v,
					     long long old, long long new)
{
	asm volatile(
		"	csg	%0,%2,%1"
		: "+d" (old), "+Q" (v->counter)
		: "d" (new)
		: "cc", "memory");
	return old;
}

#define ATOMIC64_OP(op, OP)						\
static inline void atomic64_##op(long i, atomic64_t *v)			\
{									\
	__ATOMIC64_LOOP(v, i, __ATOMIC64_##OP, __ATOMIC64_NO_BARRIER);	\
}

ATOMIC64_OP(and, AND)
ATOMIC64_OP(or, OR)
ATOMIC64_OP(xor, XOR)

#undef ATOMIC64_OP
#undef __ATOMIC64_LOOP

static inline int atomic64_add_unless(atomic64_t *v, long long i, long long u)
{
	long long c, old;

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

static inline long long atomic64_dec_if_positive(atomic64_t *v)
{
	long long c, old, dec;

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
#define atomic64_sub_return(_i, _v)	atomic64_add_return(-(long long)(_i), _v)
#define atomic64_sub(_i, _v)		atomic64_add(-(long long)(_i), _v)
#define atomic64_sub_and_test(_i, _v)	(atomic64_sub_return(_i, _v) == 0)
#define atomic64_dec(_v)		atomic64_sub(1, _v)
#define atomic64_dec_return(_v)		atomic64_sub_return(1, _v)
#define atomic64_dec_and_test(_v)	(atomic64_sub_return(1, _v) == 0)
#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)

#endif /* __ARCH_S390_ATOMIC__  */
