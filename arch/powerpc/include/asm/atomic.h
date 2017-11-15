#ifndef _ASM_POWERPC_ATOMIC_H_
#define _ASM_POWERPC_ATOMIC_H_

/*
 * PowerPC atomic operations
 */

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#define ATOMIC_INIT(i)		{ (i) }

static __inline__ int atomic_read(const atomic_t *v)
{
	int t;

	__asm__ __volatile__("lwz%U1%X1 %0,%1" : "=r"(t) : "m"(v->counter));

	return t;
}

static __inline__ void atomic_set(atomic_t *v, int i)
{
	__asm__ __volatile__("stw%U0%X0 %1,%0" : "=m"(v->counter) : "r"(i));
}

#define ATOMIC_OP(op, asm_op)						\
static __inline__ void atomic_##op(int a, atomic_t *v)			\
{									\
	int t;								\
									\
	__asm__ __volatile__(						\
"1:	lwarx	%0,0,%3		# atomic_" #op "\n"			\
	#asm_op " %0,%2,%0\n"						\
	PPC405_ERR77(0,%3)						\
"	stwcx.	%0,0,%3 \n"						\
"	bne-	1b\n"							\
	: "=&r" (t), "+m" (v->counter)					\
	: "r" (a), "r" (&v->counter)					\
	: "cc");							\
}									\

#define ATOMIC_OP_RETURN(op, asm_op)					\
static __inline__ int atomic_##op##_return(int a, atomic_t *v)		\
{									\
	int t;								\
									\
	__asm__ __volatile__(						\
	PPC_ATOMIC_ENTRY_BARRIER					\
"1:	lwarx	%0,0,%2		# atomic_" #op "_return\n"		\
	#asm_op " %0,%1,%0\n"						\
	PPC405_ERR77(0,%2)						\
"	stwcx.	%0,0,%2 \n"						\
"	bne-	1b\n"							\
	PPC_ATOMIC_EXIT_BARRIER						\
	: "=&r" (t)							\
	: "r" (a), "r" (&v->counter)					\
	: "cc", "memory");						\
									\
	return t;							\
}

#define ATOMIC_OPS(op, asm_op) ATOMIC_OP(op, asm_op) ATOMIC_OP_RETURN(op, asm_op)

ATOMIC_OPS(add, add)
ATOMIC_OPS(sub, subf)

ATOMIC_OP(and, and)
ATOMIC_OP(or, or)
ATOMIC_OP(xor, xor)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define atomic_add_negative(a, v)	(atomic_add_return((a), (v)) < 0)

static __inline__ void atomic_inc(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2		# atomic_inc\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%2 \n\
	bne-	1b"
	: "=&r" (t), "+m" (v->counter)
	: "r" (&v->counter)
	: "cc", "xer");
}

static __inline__ int atomic_inc_return(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lwarx	%0,0,%1		# atomic_inc_return\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1 \n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "xer", "memory");

	return t;
}

/*
 * atomic_inc_and_test - increment and test
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic_inc_and_test(v) (atomic_inc_return(v) == 0)

static __inline__ void atomic_dec(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
"1:	lwarx	%0,0,%2		# atomic_dec\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%2)\
"	stwcx.	%0,0,%2\n\
	bne-	1b"
	: "=&r" (t), "+m" (v->counter)
	: "r" (&v->counter)
	: "cc", "xer");
}

static __inline__ int atomic_dec_return(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lwarx	%0,0,%1		# atomic_dec_return\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "xer", "memory");

	return t;
}

#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
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
	int t;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lwarx	%0,0,%1		# __atomic_add_unless\n\
	cmpw	0,%0,%3 \n\
	beq-	2f \n\
	add	%0,%2,%0 \n"
	PPC405_ERR77(0,%2)
"	stwcx.	%0,0,%1 \n\
	bne-	1b \n"
	PPC_ATOMIC_EXIT_BARRIER
"	subf	%0,%2,%0 \n\
2:"
	: "=&r" (t)
	: "r" (&v->counter), "r" (a), "r" (u)
	: "cc", "memory");

	return t;
}

/**
 * atomic_inc_not_zero - increment unless the number is zero
 * @v: pointer of type atomic_t
 *
 * Atomically increments @v by 1, so long as @v is non-zero.
 * Returns non-zero if @v was non-zero, and zero otherwise.
 */
static __inline__ int atomic_inc_not_zero(atomic_t *v)
{
	int t1, t2;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lwarx	%0,0,%2		# atomic_inc_not_zero\n\
	cmpwi	0,%0,0\n\
	beq-	2f\n\
	addic	%1,%0,1\n"
	PPC405_ERR77(0,%2)
"	stwcx.	%1,0,%2\n\
	bne-	1b\n"
	PPC_ATOMIC_EXIT_BARRIER
	"\n\
2:"
	: "=&r" (t1), "=&r" (t2)
	: "r" (&v->counter)
	: "cc", "xer", "memory");

	return t1;
}
#define atomic_inc_not_zero(v) atomic_inc_not_zero((v))

#define atomic_sub_and_test(a, v)	(atomic_sub_return((a), (v)) == 0)
#define atomic_dec_and_test(v)		(atomic_dec_return((v)) == 0)

/*
 * Atomically test *v and decrement if it is greater than 0.
 * The function returns the old value of *v minus 1, even if
 * the atomic variable, v, was not decremented.
 */
static __inline__ int atomic_dec_if_positive(atomic_t *v)
{
	int t;

	__asm__ __volatile__(
	PPC_ATOMIC_ENTRY_BARRIER
"1:	lwarx	%0,0,%1		# atomic_dec_if_positive\n\
	cmpwi	%0,1\n\
	addi	%0,%0,-1\n\
	blt-	2f\n"
	PPC405_ERR77(0,%1)
"	stwcx.	%0,0,%1\n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	"\n\
2:"	: "=&b" (t)
	: "r" (&v->counter)
	: "cc", "memory");

	return t;
}
#define atomic_dec_if_positive atomic_dec_if_positive

#ifdef __powerpc64__

#define ATOMIC64_INIT(i)	{ (i) }

static __inline__ long atomic64_read(const atomic64_t *v)
{
	long t;

	__asm__ __volatile__("ld%U1%X1 %0,%1" : "=r"(t) : "m"(v->counter));

	return t;
}

static __inline__ void atomic64_set(atomic64_t *v, long i)
{
	__asm__ __volatile__("std%U0%X0 %1,%0" : "=m"(v->counter) : "r"(i));
}

#define ATOMIC64_OP(op, asm_op)						\
static __inline__ void atomic64_##op(long a, atomic64_t *v)		\
{									\
	long t;								\
									\
	__asm__ __volatile__(						\
"1:	ldarx	%0,0,%3		# atomic64_" #op "\n"			\
	#asm_op " %0,%2,%0\n"						\
"	stdcx.	%0,0,%3 \n"						\
"	bne-	1b\n"							\
	: "=&r" (t), "+m" (v->counter)					\
	: "r" (a), "r" (&v->counter)					\
	: "cc");							\
}

#define ATOMIC64_OP_RETURN(op, asm_op)					\
static __inline__ long atomic64_##op##_return(long a, atomic64_t *v)	\
{									\
	long t;								\
									\
	__asm__ __volatile__(						\
	PPC_ATOMIC_ENTRY_BARRIER					\
"1:	ldarx	%0,0,%2		# atomic64_" #op "_return\n"		\
	#asm_op " %0,%1,%0\n"						\
"	stdcx.	%0,0,%2 \n"						\
"	bne-	1b\n"							\
	PPC_ATOMIC_EXIT_BARRIER						\
	: "=&r" (t)							\
	: "r" (a), "r" (&v->counter)					\
	: "cc", "memory");						\
									\
	return t;							\
}

#define ATOMIC64_OPS(op, asm_op) ATOMIC64_OP(op, asm_op) ATOMIC64_OP_RETURN(op, asm_op)

ATOMIC64_OPS(add, add)
ATOMIC64_OPS(sub, subf)
ATOMIC64_OP(and, and)
ATOMIC64_OP(or, or)
ATOMIC64_OP(xor, xor)

#undef ATOMIC64_OPS
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

#define atomic64_add_negative(a, v)	(atomic64_add_return((a), (v)) < 0)

static __inline__ void atomic64_inc(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%2		# atomic64_inc\n\
	addic	%0,%0,1\n\
	stdcx.	%0,0,%2 \n\
	bne-	1b"
	: "=&r" (t), "+m" (v->counter)
	: "r" (&v->counter)
	: "cc", "xer");
}

static __inline__ long atomic64_inc_return(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	PPC_ATOMIC_ENTRY_BARRIER
"1:	ldarx	%0,0,%1		# atomic64_inc_return\n\
	addic	%0,%0,1\n\
	stdcx.	%0,0,%1 \n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "xer", "memory");

	return t;
}

/*
 * atomic64_inc_and_test - increment and test
 * @v: pointer of type atomic64_t
 *
 * Atomically increments @v by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define atomic64_inc_and_test(v) (atomic64_inc_return(v) == 0)

static __inline__ void atomic64_dec(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
"1:	ldarx	%0,0,%2		# atomic64_dec\n\
	addic	%0,%0,-1\n\
	stdcx.	%0,0,%2\n\
	bne-	1b"
	: "=&r" (t), "+m" (v->counter)
	: "r" (&v->counter)
	: "cc", "xer");
}

static __inline__ long atomic64_dec_return(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	PPC_ATOMIC_ENTRY_BARRIER
"1:	ldarx	%0,0,%1		# atomic64_dec_return\n\
	addic	%0,%0,-1\n\
	stdcx.	%0,0,%1\n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "xer", "memory");

	return t;
}

#define atomic64_sub_and_test(a, v)	(atomic64_sub_return((a), (v)) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return((v)) == 0)

/*
 * Atomically test *v and decrement if it is greater than 0.
 * The function returns the old value of *v minus 1.
 */
static __inline__ long atomic64_dec_if_positive(atomic64_t *v)
{
	long t;

	__asm__ __volatile__(
	PPC_ATOMIC_ENTRY_BARRIER
"1:	ldarx	%0,0,%1		# atomic64_dec_if_positive\n\
	addic.	%0,%0,-1\n\
	blt-	2f\n\
	stdcx.	%0,0,%1\n\
	bne-	1b"
	PPC_ATOMIC_EXIT_BARRIER
	"\n\
2:"	: "=&r" (t)
	: "r" (&v->counter)
	: "cc", "xer", "memory");

	return t;
}

#define atomic64_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * atomic64_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int atomic64_add_unless(atomic64_t *v, long a, long u)
{
	long t;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	ldarx	%0,0,%1		# __atomic_add_unless\n\
	cmpd	0,%0,%3 \n\
	beq-	2f \n\
	add	%0,%2,%0 \n"
"	stdcx.	%0,0,%1 \n\
	bne-	1b \n"
	PPC_ATOMIC_EXIT_BARRIER
"	subf	%0,%2,%0 \n\
2:"
	: "=&r" (t)
	: "r" (&v->counter), "r" (a), "r" (u)
	: "cc", "memory");

	return t != u;
}

/**
 * atomic_inc64_not_zero - increment unless the number is zero
 * @v: pointer of type atomic64_t
 *
 * Atomically increments @v by 1, so long as @v is non-zero.
 * Returns non-zero if @v was non-zero, and zero otherwise.
 */
static __inline__ int atomic64_inc_not_zero(atomic64_t *v)
{
	long t1, t2;

	__asm__ __volatile__ (
	PPC_ATOMIC_ENTRY_BARRIER
"1:	ldarx	%0,0,%2		# atomic64_inc_not_zero\n\
	cmpdi	0,%0,0\n\
	beq-	2f\n\
	addic	%1,%0,1\n\
	stdcx.	%1,0,%2\n\
	bne-	1b\n"
	PPC_ATOMIC_EXIT_BARRIER
	"\n\
2:"
	: "=&r" (t1), "=&r" (t2)
	: "r" (&v->counter)
	: "cc", "xer", "memory");

	return t1 != 0;
}

#endif /* __powerpc64__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_ATOMIC_H_ */
