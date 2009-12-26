#ifndef __ARCH_S390_ATOMIC__
#define __ARCH_S390_ATOMIC__

/*
 * Copyright 1999,2009 IBM Corp.
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *	      Denis Joseph Barrow,
 *	      Arnd Bergmann <arndb@de.ibm.com>,
 *
 * Atomic operations that C can't guarantee us.
 * Useful for resource counting etc.
 * s390 uses 'Compare And Swap' for atomicity in SMP enviroment.
 *
 */

#include <linux/compiler.h>
#include <linux/types.h>

#define ATOMIC_INIT(i)  { (i) }

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)

#define __CS_LOOP(ptr, op_val, op_string) ({				\
	int old_val, new_val;						\
	asm volatile(							\
		"	l	%0,%2\n"				\
		"0:	lr	%1,%0\n"				\
		op_string "	%1,%3\n"				\
		"	cs	%0,%1,%2\n"				\
		"	jl	0b"					\
		: "=&d" (old_val), "=&d" (new_val),			\
		  "=Q" (((atomic_t *)(ptr))->counter)			\
		: "d" (op_val),	 "Q" (((atomic_t *)(ptr))->counter)	\
		: "cc", "memory");					\
	new_val;							\
})

#else /* __GNUC__ */

#define __CS_LOOP(ptr, op_val, op_string) ({				\
	int old_val, new_val;						\
	asm volatile(							\
		"	l	%0,0(%3)\n"				\
		"0:	lr	%1,%0\n"				\
		op_string "	%1,%4\n"				\
		"	cs	%0,%1,0(%3)\n"				\
		"	jl	0b"					\
		: "=&d" (old_val), "=&d" (new_val),			\
		  "=m" (((atomic_t *)(ptr))->counter)			\
		: "a" (ptr), "d" (op_val),				\
		  "m" (((atomic_t *)(ptr))->counter)			\
		: "cc", "memory");					\
	new_val;							\
})

#endif /* __GNUC__ */

static inline int atomic_read(const atomic_t *v)
{
	barrier();
	return v->counter;
}

static inline void atomic_set(atomic_t *v, int i)
{
	v->counter = i;
	barrier();
}

static inline int atomic_add_return(int i, atomic_t *v)
{
	return __CS_LOOP(v, i, "ar");
}
#define atomic_add(_i, _v)		atomic_add_return(_i, _v)
#define atomic_add_negative(_i, _v)	(atomic_add_return(_i, _v) < 0)
#define atomic_inc(_v)			atomic_add_return(1, _v)
#define atomic_inc_return(_v)		atomic_add_return(1, _v)
#define atomic_inc_and_test(_v)		(atomic_add_return(1, _v) == 0)

static inline int atomic_sub_return(int i, atomic_t *v)
{
	return __CS_LOOP(v, i, "sr");
}
#define atomic_sub(_i, _v)		atomic_sub_return(_i, _v)
#define atomic_sub_and_test(_i, _v)	(atomic_sub_return(_i, _v) == 0)
#define atomic_dec(_v)			atomic_sub_return(1, _v)
#define atomic_dec_return(_v)		atomic_sub_return(1, _v)
#define atomic_dec_and_test(_v)		(atomic_sub_return(1, _v) == 0)

static inline void atomic_clear_mask(unsigned long mask, atomic_t *v)
{
	__CS_LOOP(v, ~mask, "nr");
}

static inline void atomic_set_mask(unsigned long mask, atomic_t *v)
{
	__CS_LOOP(v, mask, "or");
}

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)
	asm volatile(
		"	cs	%0,%2,%1"
		: "+d" (old), "=Q" (v->counter)
		: "d" (new), "Q" (v->counter)
		: "cc", "memory");
#else /* __GNUC__ */
	asm volatile(
		"	cs	%0,%3,0(%2)"
		: "+d" (old), "=m" (v->counter)
		: "a" (v), "d" (new), "m" (v->counter)
		: "cc", "memory");
#endif /* __GNUC__ */
	return old;
}

static inline int atomic_add_unless(atomic_t *v, int a, int u)
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
	return c != u;
}

#define atomic_inc_not_zero(v) atomic_add_unless((v), 1, 0)

#undef __CS_LOOP

#define ATOMIC64_INIT(i)  { (i) }

#ifdef CONFIG_64BIT

#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)

#define __CSG_LOOP(ptr, op_val, op_string) ({				\
	long long old_val, new_val;					\
	asm volatile(							\
		"	lg	%0,%2\n"				\
		"0:	lgr	%1,%0\n"				\
		op_string "	%1,%3\n"				\
		"	csg	%0,%1,%2\n"				\
		"	jl	0b"					\
		: "=&d" (old_val), "=&d" (new_val),			\
		  "=Q" (((atomic_t *)(ptr))->counter)			\
		: "d" (op_val),	"Q" (((atomic_t *)(ptr))->counter)	\
		: "cc", "memory");					\
	new_val;							\
})

#else /* __GNUC__ */

#define __CSG_LOOP(ptr, op_val, op_string) ({				\
	long long old_val, new_val;					\
	asm volatile(							\
		"	lg	%0,0(%3)\n"				\
		"0:	lgr	%1,%0\n"				\
		op_string "	%1,%4\n"				\
		"	csg	%0,%1,0(%3)\n"				\
		"	jl	0b"					\
		: "=&d" (old_val), "=&d" (new_val),			\
		  "=m" (((atomic_t *)(ptr))->counter)			\
		: "a" (ptr), "d" (op_val),				\
		  "m" (((atomic_t *)(ptr))->counter)			\
		: "cc", "memory");					\
	new_val;							\
})

#endif /* __GNUC__ */

static inline long long atomic64_read(const atomic64_t *v)
{
	barrier();
	return v->counter;
}

static inline void atomic64_set(atomic64_t *v, long long i)
{
	v->counter = i;
	barrier();
}

static inline long long atomic64_add_return(long long i, atomic64_t *v)
{
	return __CSG_LOOP(v, i, "agr");
}

static inline long long atomic64_sub_return(long long i, atomic64_t *v)
{
	return __CSG_LOOP(v, i, "sgr");
}

static inline void atomic64_clear_mask(unsigned long mask, atomic64_t *v)
{
	__CSG_LOOP(v, ~mask, "ngr");
}

static inline void atomic64_set_mask(unsigned long mask, atomic64_t *v)
{
	__CSG_LOOP(v, mask, "ogr");
}

#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

static inline long long atomic64_cmpxchg(atomic64_t *v,
					     long long old, long long new)
{
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 2)
	asm volatile(
		"	csg	%0,%2,%1"
		: "+d" (old), "=Q" (v->counter)
		: "d" (new), "Q" (v->counter)
		: "cc", "memory");
#else /* __GNUC__ */
	asm volatile(
		"	csg	%0,%3,0(%2)"
		: "+d" (old), "=m" (v->counter)
		: "a" (v), "d" (new), "m" (v->counter)
		: "cc", "memory");
#endif /* __GNUC__ */
	return old;
}

#undef __CSG_LOOP

#else /* CONFIG_64BIT */

typedef struct {
	long long counter;
} atomic64_t;

static inline long long atomic64_read(const atomic64_t *v)
{
	register_pair rp;

	asm volatile(
		"	lm	%0,%N0,0(%1)"
		: "=&d" (rp)
		: "a" (&v->counter), "m" (v->counter)
		);
	return rp.pair;
}

static inline void atomic64_set(atomic64_t *v, long long i)
{
	register_pair rp = {.pair = i};

	asm volatile(
		"	stm	%1,%N1,0(%2)"
		: "=m" (v->counter)
		: "d" (rp), "a" (&v->counter)
		);
}

static inline long long atomic64_xchg(atomic64_t *v, long long new)
{
	register_pair rp_new = {.pair = new};
	register_pair rp_old;

	asm volatile(
		"	lm	%0,%N0,0(%2)\n"
		"0:	cds	%0,%3,0(%2)\n"
		"	jl	0b\n"
		: "=&d" (rp_old), "+m" (v->counter)
		: "a" (&v->counter), "d" (rp_new)
		: "cc");
	return rp_old.pair;
}

static inline long long atomic64_cmpxchg(atomic64_t *v,
					 long long old, long long new)
{
	register_pair rp_old = {.pair = old};
	register_pair rp_new = {.pair = new};

	asm volatile(
		"	cds	%0,%3,0(%2)"
		: "+&d" (rp_old), "+m" (v->counter)
		: "a" (&v->counter), "d" (rp_new)
		: "cc");
	return rp_old.pair;
}


static inline long long atomic64_add_return(long long i, atomic64_t *v)
{
	long long old, new;

	do {
		old = atomic64_read(v);
		new = old + i;
	} while (atomic64_cmpxchg(v, old, new) != old);
	return new;
}

static inline long long atomic64_sub_return(long long i, atomic64_t *v)
{
	long long old, new;

	do {
		old = atomic64_read(v);
		new = old - i;
	} while (atomic64_cmpxchg(v, old, new) != old);
	return new;
}

static inline void atomic64_set_mask(unsigned long long mask, atomic64_t *v)
{
	long long old, new;

	do {
		old = atomic64_read(v);
		new = old | mask;
	} while (atomic64_cmpxchg(v, old, new) != old);
}

static inline void atomic64_clear_mask(unsigned long long mask, atomic64_t *v)
{
	long long old, new;

	do {
		old = atomic64_read(v);
		new = old & mask;
	} while (atomic64_cmpxchg(v, old, new) != old);
}

#endif /* CONFIG_64BIT */

static inline int atomic64_add_unless(atomic64_t *v, long long a, long long u)
{
	long long c, old;
	c = atomic64_read(v);
	for (;;) {
		if (unlikely(c == u))
			break;
		old = atomic64_cmpxchg(v, c, c + a);
		if (likely(old == c))
			break;
		c = old;
	}
	return c != u;
}

#define atomic64_add(_i, _v)		atomic64_add_return(_i, _v)
#define atomic64_add_negative(_i, _v)	(atomic64_add_return(_i, _v) < 0)
#define atomic64_inc(_v)		atomic64_add_return(1, _v)
#define atomic64_inc_return(_v)		atomic64_add_return(1, _v)
#define atomic64_inc_and_test(_v)	(atomic64_add_return(1, _v) == 0)
#define atomic64_sub(_i, _v)		atomic64_sub_return(_i, _v)
#define atomic64_sub_and_test(_i, _v)	(atomic64_sub_return(_i, _v) == 0)
#define atomic64_dec(_v)		atomic64_sub_return(1, _v)
#define atomic64_dec_return(_v)		atomic64_sub_return(1, _v)
#define atomic64_dec_and_test(_v)	(atomic64_sub_return(1, _v) == 0)
#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)

#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__after_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_inc()	smp_mb()

#include <asm-generic/atomic-long.h>

#endif /* __ARCH_S390_ATOMIC__  */
