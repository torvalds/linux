/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#ifndef _ASM_RISCV_ATOMIC_H
#define _ASM_RISCV_ATOMIC_H

#ifdef CONFIG_GENERIC_ATOMIC64
# include <asm-generic/atomic64.h>
#else
# if (__riscv_xlen < 64)
#  error "64-bit atomics require XLEN to be at least 64"
# endif
#endif

#include <asm/cmpxchg.h>

#define __atomic_acquire_fence()					\
	__asm__ __volatile__(RISCV_ACQUIRE_BARRIER "" ::: "memory")

#define __atomic_release_fence()					\
	__asm__ __volatile__(RISCV_RELEASE_BARRIER "" ::: "memory");

static __always_inline int arch_atomic_read(const atomic_t *v)
{
	return READ_ONCE(v->counter);
}
static __always_inline void arch_atomic_set(atomic_t *v, int i)
{
	WRITE_ONCE(v->counter, i);
}

#ifndef CONFIG_GENERIC_ATOMIC64
#define ATOMIC64_INIT(i) { (i) }
static __always_inline s64 arch_atomic64_read(const atomic64_t *v)
{
	return READ_ONCE(v->counter);
}
static __always_inline void arch_atomic64_set(atomic64_t *v, s64 i)
{
	WRITE_ONCE(v->counter, i);
}
#endif

/*
 * First, the atomic ops that have no ordering constraints and therefor don't
 * have the AQ or RL bits set.  These don't return anything, so there's only
 * one version to worry about.
 */
#define ATOMIC_OP(op, asm_op, I, asm_type, c_type, prefix)		\
static __always_inline							\
void arch_atomic##prefix##_##op(c_type i, atomic##prefix##_t *v)	\
{									\
	__asm__ __volatile__ (						\
		"	amo" #asm_op "." #asm_type " zero, %1, %0"	\
		: "+A" (v->counter)					\
		: "r" (I)						\
		: "memory");						\
}									\

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_OP (op, asm_op, I, w, int,   )
#else
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_OP (op, asm_op, I, w, int,   )				\
        ATOMIC_OP (op, asm_op, I, d, s64, 64)
#endif

ATOMIC_OPS(add, add,  i)
ATOMIC_OPS(sub, add, -i)
ATOMIC_OPS(and, and,  i)
ATOMIC_OPS( or,  or,  i)
ATOMIC_OPS(xor, xor,  i)

#undef ATOMIC_OP
#undef ATOMIC_OPS

/*
 * Atomic ops that have ordered, relaxed, acquire, and release variants.
 * There's two flavors of these: the arithmatic ops have both fetch and return
 * versions, while the logical ops only have fetch versions.
 */
#define ATOMIC_FETCH_OP(op, asm_op, I, asm_type, c_type, prefix)	\
static __always_inline							\
c_type arch_atomic##prefix##_fetch_##op##_relaxed(c_type i,		\
					     atomic##prefix##_t *v)	\
{									\
	register c_type ret;						\
	__asm__ __volatile__ (						\
		"	amo" #asm_op "." #asm_type " %1, %2, %0"	\
		: "+A" (v->counter), "=r" (ret)				\
		: "r" (I)						\
		: "memory");						\
	return ret;							\
}									\
static __always_inline							\
c_type arch_atomic##prefix##_fetch_##op(c_type i, atomic##prefix##_t *v)	\
{									\
	register c_type ret;						\
	__asm__ __volatile__ (						\
		"	amo" #asm_op "." #asm_type ".aqrl  %1, %2, %0"	\
		: "+A" (v->counter), "=r" (ret)				\
		: "r" (I)						\
		: "memory");						\
	return ret;							\
}

#define ATOMIC_OP_RETURN(op, asm_op, c_op, I, asm_type, c_type, prefix)	\
static __always_inline							\
c_type arch_atomic##prefix##_##op##_return_relaxed(c_type i,		\
					      atomic##prefix##_t *v)	\
{									\
        return arch_atomic##prefix##_fetch_##op##_relaxed(i, v) c_op I;	\
}									\
static __always_inline							\
c_type arch_atomic##prefix##_##op##_return(c_type i, atomic##prefix##_t *v)	\
{									\
        return arch_atomic##prefix##_fetch_##op(i, v) c_op I;		\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, c_op, I)					\
        ATOMIC_FETCH_OP( op, asm_op,       I, w, int,   )		\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, w, int,   )
#else
#define ATOMIC_OPS(op, asm_op, c_op, I)					\
        ATOMIC_FETCH_OP( op, asm_op,       I, w, int,   )		\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, w, int,   )		\
        ATOMIC_FETCH_OP( op, asm_op,       I, d, s64, 64)		\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, d, s64, 64)
#endif

ATOMIC_OPS(add, add, +,  i)
ATOMIC_OPS(sub, add, +, -i)

#define arch_atomic_add_return_relaxed	arch_atomic_add_return_relaxed
#define arch_atomic_sub_return_relaxed	arch_atomic_sub_return_relaxed
#define arch_atomic_add_return		arch_atomic_add_return
#define arch_atomic_sub_return		arch_atomic_sub_return

#define arch_atomic_fetch_add_relaxed	arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_sub_relaxed	arch_atomic_fetch_sub_relaxed
#define arch_atomic_fetch_add		arch_atomic_fetch_add
#define arch_atomic_fetch_sub		arch_atomic_fetch_sub

#ifndef CONFIG_GENERIC_ATOMIC64
#define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
#define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed
#define arch_atomic64_add_return		arch_atomic64_add_return
#define arch_atomic64_sub_return		arch_atomic64_sub_return

#define arch_atomic64_fetch_add_relaxed	arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_sub_relaxed	arch_atomic64_fetch_sub_relaxed
#define arch_atomic64_fetch_add		arch_atomic64_fetch_add
#define arch_atomic64_fetch_sub		arch_atomic64_fetch_sub
#endif

#undef ATOMIC_OPS

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_FETCH_OP(op, asm_op, I, w, int,   )
#else
#define ATOMIC_OPS(op, asm_op, I)					\
        ATOMIC_FETCH_OP(op, asm_op, I, w, int,   )			\
        ATOMIC_FETCH_OP(op, asm_op, I, d, s64, 64)
#endif

ATOMIC_OPS(and, and, i)
ATOMIC_OPS( or,  or, i)
ATOMIC_OPS(xor, xor, i)

#define arch_atomic_fetch_and_relaxed	arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_or_relaxed	arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_xor_relaxed	arch_atomic_fetch_xor_relaxed
#define arch_atomic_fetch_and		arch_atomic_fetch_and
#define arch_atomic_fetch_or		arch_atomic_fetch_or
#define arch_atomic_fetch_xor		arch_atomic_fetch_xor

#ifndef CONFIG_GENERIC_ATOMIC64
#define arch_atomic64_fetch_and_relaxed	arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_or_relaxed	arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_xor_relaxed	arch_atomic64_fetch_xor_relaxed
#define arch_atomic64_fetch_and		arch_atomic64_fetch_and
#define arch_atomic64_fetch_or		arch_atomic64_fetch_or
#define arch_atomic64_fetch_xor		arch_atomic64_fetch_xor
#endif

#undef ATOMIC_OPS

#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN

/* This is required to provide a full barrier on success. */
static __always_inline int arch_atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
       int prev, rc;

	__asm__ __volatile__ (
		"0:	lr.w     %[p],  %[c]\n"
		"	beq      %[p],  %[u], 1f\n"
		"	add      %[rc], %[p], %[a]\n"
		"	sc.w.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");
	return prev;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

#ifndef CONFIG_GENERIC_ATOMIC64
static __always_inline s64 arch_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
       s64 prev;
       long rc;

	__asm__ __volatile__ (
		"0:	lr.d     %[p],  %[c]\n"
		"	beq      %[p],  %[u], 1f\n"
		"	add      %[rc], %[p], %[a]\n"
		"	sc.d.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");
	return prev;
}
#define arch_atomic64_fetch_add_unless arch_atomic64_fetch_add_unless
#endif

static __always_inline bool arch_atomic_inc_unless_negative(atomic_t *v)
{
	int prev, rc;

	__asm__ __volatile__ (
		"0:	lr.w      %[p],  %[c]\n"
		"	bltz      %[p],  1f\n"
		"	addi      %[rc], %[p], 1\n"
		"	sc.w.rl   %[rc], %[rc], %[c]\n"
		"	bnez      %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		:
		: "memory");
	return !(prev < 0);
}

#define arch_atomic_inc_unless_negative arch_atomic_inc_unless_negative

static __always_inline bool arch_atomic_dec_unless_positive(atomic_t *v)
{
	int prev, rc;

	__asm__ __volatile__ (
		"0:	lr.w      %[p],  %[c]\n"
		"	bgtz      %[p],  1f\n"
		"	addi      %[rc], %[p], -1\n"
		"	sc.w.rl   %[rc], %[rc], %[c]\n"
		"	bnez      %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		:
		: "memory");
	return !(prev > 0);
}

#define arch_atomic_dec_unless_positive arch_atomic_dec_unless_positive

static __always_inline int arch_atomic_dec_if_positive(atomic_t *v)
{
       int prev, rc;

	__asm__ __volatile__ (
		"0:	lr.w     %[p],  %[c]\n"
		"	addi     %[rc], %[p], -1\n"
		"	bltz     %[rc], 1f\n"
		"	sc.w.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		:
		: "memory");
	return prev - 1;
}

#define arch_atomic_dec_if_positive arch_atomic_dec_if_positive

#ifndef CONFIG_GENERIC_ATOMIC64
static __always_inline bool arch_atomic64_inc_unless_negative(atomic64_t *v)
{
	s64 prev;
	long rc;

	__asm__ __volatile__ (
		"0:	lr.d      %[p],  %[c]\n"
		"	bltz      %[p],  1f\n"
		"	addi      %[rc], %[p], 1\n"
		"	sc.d.rl   %[rc], %[rc], %[c]\n"
		"	bnez      %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		:
		: "memory");
	return !(prev < 0);
}

#define arch_atomic64_inc_unless_negative arch_atomic64_inc_unless_negative

static __always_inline bool arch_atomic64_dec_unless_positive(atomic64_t *v)
{
	s64 prev;
	long rc;

	__asm__ __volatile__ (
		"0:	lr.d      %[p],  %[c]\n"
		"	bgtz      %[p],  1f\n"
		"	addi      %[rc], %[p], -1\n"
		"	sc.d.rl   %[rc], %[rc], %[c]\n"
		"	bnez      %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		:
		: "memory");
	return !(prev > 0);
}

#define arch_atomic64_dec_unless_positive arch_atomic64_dec_unless_positive

static __always_inline s64 arch_atomic64_dec_if_positive(atomic64_t *v)
{
       s64 prev;
       long rc;

	__asm__ __volatile__ (
		"0:	lr.d     %[p],  %[c]\n"
		"	addi      %[rc], %[p], -1\n"
		"	bltz     %[rc], 1f\n"
		"	sc.d.rl  %[rc], %[rc], %[c]\n"
		"	bnez     %[rc], 0b\n"
		RISCV_FULL_BARRIER
		"1:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		:
		: "memory");
	return prev - 1;
}

#define arch_atomic64_dec_if_positive	arch_atomic64_dec_if_positive
#endif

#endif /* _ASM_RISCV_ATOMIC_H */
