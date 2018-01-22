/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_METAG_ATOMIC_LNKGET_H
#define __ASM_METAG_ATOMIC_LNKGET_H

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_set(v, i)		WRITE_ONCE((v)->counter, (i))

#include <linux/compiler.h>

#include <asm/barrier.h>

/*
 * None of these asm statements clobber memory as LNKSET writes around
 * the cache so the memory it modifies cannot safely be read by any means
 * other than these accessors.
 */

static inline int atomic_read(const atomic_t *v)
{
	int temp;

	asm volatile (
		"LNKGETD %0, [%1]\n"
		: "=da" (temp)
		: "da" (&v->counter));

	return temp;
}

#define ATOMIC_OP(op)							\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	int temp;							\
									\
	asm volatile (							\
		"1:	LNKGETD %0, [%1]\n"				\
		"	" #op "	%0, %0, %2\n"				\
		"	LNKSETD [%1], %0\n"				\
		"	DEFR	%0, TXSTAT\n"				\
		"	ANDT	%0, %0, #HI(0x3f000000)\n"		\
		"	CMPT	%0, #HI(0x02000000)\n"			\
		"	BNZ	1b\n"					\
		: "=&d" (temp)						\
		: "da" (&v->counter), "bd" (i)				\
		: "cc");						\
}									\

#define ATOMIC_OP_RETURN(op)						\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	int result, temp;						\
									\
	smp_mb();							\
									\
	asm volatile (							\
		"1:	LNKGETD %1, [%2]\n"				\
		"	" #op "	%1, %1, %3\n"				\
		"	LNKSETD [%2], %1\n"				\
		"	DEFR	%0, TXSTAT\n"				\
		"	ANDT	%0, %0, #HI(0x3f000000)\n"		\
		"	CMPT	%0, #HI(0x02000000)\n"			\
		"	BNZ 1b\n"					\
		: "=&d" (temp), "=&da" (result)				\
		: "da" (&v->counter), "br" (i)				\
		: "cc");						\
									\
	smp_mb();							\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(op)						\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	int result, temp;						\
									\
	smp_mb();							\
									\
	asm volatile (							\
		"1:	LNKGETD %1, [%2]\n"				\
		"	" #op "	%0, %1, %3\n"				\
		"	LNKSETD [%2], %0\n"				\
		"	DEFR	%0, TXSTAT\n"				\
		"	ANDT	%0, %0, #HI(0x3f000000)\n"		\
		"	CMPT	%0, #HI(0x02000000)\n"			\
		"	BNZ 1b\n"					\
		: "=&d" (temp), "=&d" (result)				\
		: "da" (&v->counter), "bd" (i)				\
		: "cc");						\
									\
	smp_mb();							\
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

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int result, temp;

	smp_mb();

	asm volatile (
		"1:	LNKGETD	%1, [%2]\n"
		"	CMP	%1, %3\n"
		"	LNKSETDEQ [%2], %4\n"
		"	BNE	2f\n"
		"	DEFR	%0, TXSTAT\n"
		"	ANDT	%0, %0, #HI(0x3f000000)\n"
		"	CMPT	%0, #HI(0x02000000)\n"
		"	BNZ	1b\n"
		"2:\n"
		: "=&d" (temp), "=&d" (result)
		: "da" (&v->counter), "bd" (old), "da" (new)
		: "cc");

	smp_mb();

	return result;
}

static inline int atomic_xchg(atomic_t *v, int new)
{
	int temp, old;

	asm volatile (
		"1:	LNKGETD %1, [%2]\n"
		"	LNKSETD	[%2], %3\n"
		"	DEFR	%0, TXSTAT\n"
		"	ANDT	%0, %0, #HI(0x3f000000)\n"
		"	CMPT	%0, #HI(0x02000000)\n"
		"	BNZ	1b\n"
		: "=&d" (temp), "=&d" (old)
		: "da" (&v->counter), "da" (new)
		: "cc");

	return old;
}

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int result, temp;

	smp_mb();

	asm volatile (
		"1:	LNKGETD %1, [%2]\n"
		"	CMP	%1, %3\n"
		"	ADD	%0, %1, %4\n"
		"	LNKSETDNE [%2], %0\n"
		"	BEQ	2f\n"
		"	DEFR	%0, TXSTAT\n"
		"	ANDT	%0, %0, #HI(0x3f000000)\n"
		"	CMPT	%0, #HI(0x02000000)\n"
		"	BNZ	1b\n"
		"2:\n"
		: "=&d" (temp), "=&d" (result)
		: "da" (&v->counter), "bd" (u), "bd" (a)
		: "cc");

	smp_mb();

	return result;
}

static inline int atomic_sub_if_positive(int i, atomic_t *v)
{
	int result, temp;

	asm volatile (
		"1:	LNKGETD %1, [%2]\n"
		"	SUBS	%1, %1, %3\n"
		"	LNKSETDGE [%2], %1\n"
		"	BLT	2f\n"
		"	DEFR	%0, TXSTAT\n"
		"	ANDT	%0, %0, #HI(0x3f000000)\n"
		"	CMPT	%0, #HI(0x02000000)\n"
		"	BNZ	1b\n"
		"2:\n"
		: "=&d" (temp), "=&da" (result)
		: "da" (&v->counter), "bd" (i)
		: "cc");

	return result;
}

#endif /* __ASM_METAG_ATOMIC_LNKGET_H */
