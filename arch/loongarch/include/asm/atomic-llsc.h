/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Atomic operations (LLSC).
 *
 * Copyright (C) 2024-2025 Loongson Technology Corporation Limited
 */

#ifndef _ASM_ATOMIC_LLSC_H
#define _ASM_ATOMIC_LLSC_H

#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#define ATOMIC_OP(op, I, asm_op)					\
static inline void arch_atomic_##op(int i, atomic_t *v)			\
{									\
	int temp;							\
									\
	__asm__ __volatile__(						\
	"1:	ll.w		%0, %1      #atomic_" #op "	\n"	\
	"       " #asm_op "	%0, %0, %2			\n"	\
	"	sc.w		%0, %1				\n"	\
	"       beq		%0, $r0, 1b			\n"	\
	:"=&r" (temp) , "+ZC"(v->counter)				\
	:"r" (I)							\
	);								\
}

#define ATOMIC_OP_RETURN(op, I, asm_op)					\
static inline int arch_atomic_##op##_return_relaxed(int i, atomic_t *v)	\
{									\
	int result, temp;						\
									\
	__asm__ __volatile__(						\
	"1:     ll.w		%1, %2      # atomic_" #op "_return \n"	\
	"       " #asm_op "	%0, %1, %3                          \n"	\
	"       sc.w		%0, %2                              \n"	\
	"       beq		%0, $r0 ,1b                         \n"	\
	"       " #asm_op "	%0, %1, %3                          \n"	\
	: "=&r" (result), "=&r" (temp),	"+ZC"(v->counter)		\
	: "r" (I));							\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(op, I, asm_op)					\
static inline int arch_atomic_fetch_##op##_relaxed(int i, atomic_t *v)	\
{									\
	int result, temp;						\
									\
	__asm__ __volatile__(						\
	"1:     ll.w		%1, %2      # atomic_fetch_" #op "  \n"	\
	"       " #asm_op "	%0, %1, %3                          \n" \
	"       sc.w		%0, %2                              \n"	\
	"       beq		%0, $r0 ,1b                         \n"	\
	"       add.w		%0, %1  ,$r0                        \n"	\
	: "=&r" (result), "=&r" (temp), "+ZC" (v->counter)		\
	: "r" (I));							\
									\
	return result;							\
}

#define ATOMIC_OPS(op,I ,asm_op, c_op)					\
	ATOMIC_OP(op, I, asm_op)					\
	ATOMIC_OP_RETURN(op, I , asm_op)				\
	ATOMIC_FETCH_OP(op, I, asm_op)

ATOMIC_OPS(add, i , add.w ,+=)
ATOMIC_OPS(sub, -i , add.w ,+=)

#define arch_atomic_add_return_relaxed	arch_atomic_add_return_relaxed
#define arch_atomic_sub_return_relaxed	arch_atomic_sub_return_relaxed
#define arch_atomic_fetch_add_relaxed	arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_sub_relaxed	arch_atomic_fetch_sub_relaxed

#undef ATOMIC_OPS

#define ATOMIC_OPS(op, I, asm_op)					\
	ATOMIC_OP(op, I, asm_op)					\
	ATOMIC_FETCH_OP(op, I, asm_op)

ATOMIC_OPS(and, i, and)
ATOMIC_OPS(or, i, or)
ATOMIC_OPS(xor, i, xor)

#define arch_atomic_fetch_and_relaxed	arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_or_relaxed	arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_xor_relaxed	arch_atomic_fetch_xor_relaxed

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#ifdef CONFIG_64BIT
#error "64-bit LLSC atomic operations are not supported"
#endif

#endif /* _ASM_ATOMIC_LLSC_H */
