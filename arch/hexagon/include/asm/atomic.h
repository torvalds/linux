/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Atomic operations for the Hexagon architecture
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/types.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

/*  Normal writes in our arch don't clear lock reservations  */

static inline void arch_atomic_set(atomic_t *v, int new)
{
	asm volatile(
		"1:	r6 = memw_locked(%0);\n"
		"	memw_locked(%0,p0) = %1;\n"
		"	if (!P0) jump 1b;\n"
		:
		: "r" (&v->counter), "r" (new)
		: "memory", "p0", "r6"
	);
}

#define arch_atomic_set_release(v, i)	arch_atomic_set((v), (i))

#define arch_atomic_read(v)		READ_ONCE((v)->counter)

#define ATOMIC_OP(op)							\
static inline void arch_atomic_##op(int i, atomic_t *v)			\
{									\
	int output;							\
									\
	__asm__ __volatile__ (						\
		"1:	%0 = memw_locked(%1);\n"			\
		"	%0 = "#op "(%0,%2);\n"				\
		"	memw_locked(%1,P3)=%0;\n"			\
		"	if (!P3) jump 1b;\n"				\
		: "=&r" (output)					\
		: "r" (&v->counter), "r" (i)				\
		: "memory", "p3"					\
	);								\
}									\

#define ATOMIC_OP_RETURN(op)						\
static inline int arch_atomic_##op##_return(int i, atomic_t *v)		\
{									\
	int output;							\
									\
	__asm__ __volatile__ (						\
		"1:	%0 = memw_locked(%1);\n"			\
		"	%0 = "#op "(%0,%2);\n"				\
		"	memw_locked(%1,P3)=%0;\n"			\
		"	if (!P3) jump 1b;\n"				\
		: "=&r" (output)					\
		: "r" (&v->counter), "r" (i)				\
		: "memory", "p3"					\
	);								\
	return output;							\
}

#define ATOMIC_FETCH_OP(op)						\
static inline int arch_atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	int output, val;						\
									\
	__asm__ __volatile__ (						\
		"1:	%0 = memw_locked(%2);\n"			\
		"	%1 = "#op "(%0,%3);\n"				\
		"	memw_locked(%2,P3)=%1;\n"			\
		"	if (!P3) jump 1b;\n"				\
		: "=&r" (output), "=&r" (val)				\
		: "r" (&v->counter), "r" (i)				\
		: "memory", "p3"					\
	);								\
	return output;							\
}

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

#define arch_atomic_add_return			arch_atomic_add_return
#define arch_atomic_sub_return			arch_atomic_sub_return
#define arch_atomic_fetch_add			arch_atomic_fetch_add
#define arch_atomic_fetch_sub			arch_atomic_fetch_sub

#undef ATOMIC_OPS
#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_FETCH_OP(op)

ATOMIC_OPS(and)
ATOMIC_OPS(or)
ATOMIC_OPS(xor)

#define arch_atomic_fetch_and			arch_atomic_fetch_and
#define arch_atomic_fetch_or			arch_atomic_fetch_or
#define arch_atomic_fetch_xor			arch_atomic_fetch_xor

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

static inline int arch_atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int __oldval;
	register int tmp;

	asm volatile(
		"1:	%0 = memw_locked(%2);"
		"	{"
		"		p3 = cmp.eq(%0, %4);"
		"		if (p3.new) jump:nt 2f;"
		"		%1 = add(%0, %3);"
		"	}"
		"	memw_locked(%2, p3) = %1;"
		"	{"
		"		if (!p3) jump 1b;"
		"	}"
		"2:"
		: "=&r" (__oldval), "=&r" (tmp)
		: "r" (v), "r" (a), "r" (u)
		: "memory", "p3"
	);
	return __oldval;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

#endif
