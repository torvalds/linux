/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_POWERPC_LOCAL_H
#define _ARCH_POWERPC_LOCAL_H

#include <linux/percpu.h>
#include <linux/atomic.h>

typedef struct
{
	atomic_long_t a;
} local_t;

#define LOCAL_INIT(i)	{ ATOMIC_LONG_INIT(i) }

#define local_read(l)	atomic_long_read(&(l)->a)
#define local_set(l,i)	atomic_long_set(&(l)->a, (i))

#define local_add(i,l)	atomic_long_add((i),(&(l)->a))
#define local_sub(i,l)	atomic_long_sub((i),(&(l)->a))
#define local_inc(l)	atomic_long_inc(&(l)->a)
#define local_dec(l)	atomic_long_dec(&(l)->a)

static __inline__ long local_add_return(long a, local_t *l)
{
	long t;

	__asm__ __volatile__(
"1:"	PPC_LLARX(%0,0,%2,0) "			# local_add_return\n\
	add	%0,%1,%0\n"
	PPC405_ERR77(0,%2)
	PPC_STLCX	"%0,0,%2 \n\
	bne-	1b"
	: "=&r" (t)
	: "r" (a), "r" (&(l->a.counter))
	: "cc", "memory");

	return t;
}

#define local_add_negative(a, l)	(local_add_return((a), (l)) < 0)

static __inline__ long local_sub_return(long a, local_t *l)
{
	long t;

	__asm__ __volatile__(
"1:"	PPC_LLARX(%0,0,%2,0) "			# local_sub_return\n\
	subf	%0,%1,%0\n"
	PPC405_ERR77(0,%2)
	PPC_STLCX	"%0,0,%2 \n\
	bne-	1b"
	: "=&r" (t)
	: "r" (a), "r" (&(l->a.counter))
	: "cc", "memory");

	return t;
}

static __inline__ long local_inc_return(local_t *l)
{
	long t;

	__asm__ __volatile__(
"1:"	PPC_LLARX(%0,0,%1,0) "			# local_inc_return\n\
	addic	%0,%0,1\n"
	PPC405_ERR77(0,%1)
	PPC_STLCX	"%0,0,%1 \n\
	bne-	1b"
	: "=&r" (t)
	: "r" (&(l->a.counter))
	: "cc", "xer", "memory");

	return t;
}

/*
 * local_inc_and_test - increment and test
 * @l: pointer of type local_t
 *
 * Atomically increments @l by 1
 * and returns true if the result is zero, or false for all
 * other cases.
 */
#define local_inc_and_test(l) (local_inc_return(l) == 0)

static __inline__ long local_dec_return(local_t *l)
{
	long t;

	__asm__ __volatile__(
"1:"	PPC_LLARX(%0,0,%1,0) "			# local_dec_return\n\
	addic	%0,%0,-1\n"
	PPC405_ERR77(0,%1)
	PPC_STLCX	"%0,0,%1\n\
	bne-	1b"
	: "=&r" (t)
	: "r" (&(l->a.counter))
	: "cc", "xer", "memory");

	return t;
}

#define local_cmpxchg(l, o, n) \
	(cmpxchg_local(&((l)->a.counter), (o), (n)))
#define local_xchg(l, n) (xchg_local(&((l)->a.counter), (n)))

/**
 * local_add_unless - add unless the number is a given value
 * @l: pointer of type local_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @l, so long as it was not @u.
 * Returns non-zero if @l was not @u, and zero otherwise.
 */
static __inline__ int local_add_unless(local_t *l, long a, long u)
{
	long t;

	__asm__ __volatile__ (
"1:"	PPC_LLARX(%0,0,%1,0) "			# local_add_unless\n\
	cmpw	0,%0,%3 \n\
	beq-	2f \n\
	add	%0,%2,%0 \n"
	PPC405_ERR77(0,%2)
	PPC_STLCX	"%0,0,%1 \n\
	bne-	1b \n"
"	subf	%0,%2,%0 \n\
2:"
	: "=&r" (t)
	: "r" (&(l->a.counter)), "r" (a), "r" (u)
	: "cc", "memory");

	return t != u;
}

#define local_inc_not_zero(l) local_add_unless((l), 1, 0)

#define local_sub_and_test(a, l)	(local_sub_return((a), (l)) == 0)
#define local_dec_and_test(l)		(local_dec_return((l)) == 0)

/*
 * Atomically test *l and decrement if it is greater than 0.
 * The function returns the old value of *l minus 1.
 */
static __inline__ long local_dec_if_positive(local_t *l)
{
	long t;

	__asm__ __volatile__(
"1:"	PPC_LLARX(%0,0,%1,0) "			# local_dec_if_positive\n\
	cmpwi	%0,1\n\
	addi	%0,%0,-1\n\
	blt-	2f\n"
	PPC405_ERR77(0,%1)
	PPC_STLCX	"%0,0,%1\n\
	bne-	1b"
	"\n\
2:"	: "=&b" (t)
	: "r" (&(l->a.counter))
	: "cc", "memory");

	return t;
}

/* Use these for per-cpu local_t variables: on some archs they are
 * much more efficient than these naive implementations.  Note they take
 * a variable, not an address.
 */

#define __local_inc(l)		((l)->a.counter++)
#define __local_dec(l)		((l)->a.counter++)
#define __local_add(i,l)	((l)->a.counter+=(i))
#define __local_sub(i,l)	((l)->a.counter-=(i))

#endif /* _ARCH_POWERPC_LOCAL_H */
