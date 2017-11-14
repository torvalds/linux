/*
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
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
#include <asm/barrier.h>

#define ATOMIC_INIT(i)	{ (i) }
static __always_inline int atomic_read(const atomic_t *v)
{
	return READ_ONCE(v->counter);
}
static __always_inline void atomic_set(atomic_t *v, int i)
{
	WRITE_ONCE(v->counter, i);
}

#ifndef CONFIG_GENERIC_ATOMIC64
#define ATOMIC64_INIT(i) { (i) }
static __always_inline long atomic64_read(const atomic64_t *v)
{
	return READ_ONCE(v->counter);
}
static __always_inline void atomic64_set(atomic64_t *v, long i)
{
	WRITE_ONCE(v->counter, i);
}
#endif

/*
 * First, the atomic ops that have no ordering constraints and therefor don't
 * have the AQ or RL bits set.  These don't return anything, so there's only
 * one version to worry about.
 */
#define ATOMIC_OP(op, asm_op, I, asm_type, c_type, prefix)				\
static __always_inline void atomic##prefix##_##op(c_type i, atomic##prefix##_t *v)	\
{											\
	__asm__ __volatile__ (								\
		"amo" #asm_op "." #asm_type " zero, %1, %0"				\
		: "+A" (v->counter)							\
		: "r" (I)								\
		: "memory");								\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, I)			\
        ATOMIC_OP (op, asm_op, I, w,  int,   )
#else
#define ATOMIC_OPS(op, asm_op, I)			\
        ATOMIC_OP (op, asm_op, I, w,  int,   )	\
        ATOMIC_OP (op, asm_op, I, d, long, 64)
#endif

ATOMIC_OPS(add, add,  i)
ATOMIC_OPS(sub, add, -i)
ATOMIC_OPS(and, and,  i)
ATOMIC_OPS( or,  or,  i)
ATOMIC_OPS(xor, xor,  i)

#undef ATOMIC_OP
#undef ATOMIC_OPS

/*
 * Atomic ops that have ordered, relaxed, acquire, and relese variants.
 * There's two flavors of these: the arithmatic ops have both fetch and return
 * versions, while the logical ops only have fetch versions.
 */
#define ATOMIC_FETCH_OP(op, asm_op, I, asm_or, c_or, asm_type, c_type, prefix)				\
static __always_inline c_type atomic##prefix##_fetch_##op##c_or(c_type i, atomic##prefix##_t *v)	\
{													\
	register c_type ret;										\
	__asm__ __volatile__ (										\
		"amo" #asm_op "." #asm_type #asm_or " %1, %2, %0"					\
		: "+A" (v->counter), "=r" (ret)								\
		: "r" (I)										\
		: "memory");										\
	return ret;											\
}

#define ATOMIC_OP_RETURN(op, asm_op, c_op, I, asm_or, c_or, asm_type, c_type, prefix)			\
static __always_inline c_type atomic##prefix##_##op##_return##c_or(c_type i, atomic##prefix##_t *v)	\
{													\
        return atomic##prefix##_fetch_##op##c_or(i, v) c_op I;						\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, c_op, I, asm_or, c_or)				\
        ATOMIC_FETCH_OP (op, asm_op,       I, asm_or, c_or, w,  int,   )	\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, asm_or, c_or, w,  int,   )
#else
#define ATOMIC_OPS(op, asm_op, c_op, I, asm_or, c_or)				\
        ATOMIC_FETCH_OP (op, asm_op,       I, asm_or, c_or, w,  int,   )	\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, asm_or, c_or, w,  int,   )	\
        ATOMIC_FETCH_OP (op, asm_op,       I, asm_or, c_or, d, long, 64)	\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, asm_or, c_or, d, long, 64)
#endif

ATOMIC_OPS(add, add, +,  i,      , _relaxed)
ATOMIC_OPS(add, add, +,  i, .aq  , _acquire)
ATOMIC_OPS(add, add, +,  i, .rl  , _release)
ATOMIC_OPS(add, add, +,  i, .aqrl,         )

ATOMIC_OPS(sub, add, +, -i,      , _relaxed)
ATOMIC_OPS(sub, add, +, -i, .aq  , _acquire)
ATOMIC_OPS(sub, add, +, -i, .rl  , _release)
ATOMIC_OPS(sub, add, +, -i, .aqrl,         )

#undef ATOMIC_OPS

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, I, asm_or, c_or)				\
        ATOMIC_FETCH_OP(op, asm_op, I, asm_or, c_or, w,  int,   )
#else
#define ATOMIC_OPS(op, asm_op, I, asm_or, c_or)				\
        ATOMIC_FETCH_OP(op, asm_op, I, asm_or, c_or, w,  int,   )	\
        ATOMIC_FETCH_OP(op, asm_op, I, asm_or, c_or, d, long, 64)
#endif

ATOMIC_OPS(and, and, i,      , _relaxed)
ATOMIC_OPS(and, and, i, .aq  , _acquire)
ATOMIC_OPS(and, and, i, .rl  , _release)
ATOMIC_OPS(and, and, i, .aqrl,         )

ATOMIC_OPS( or,  or, i,      , _relaxed)
ATOMIC_OPS( or,  or, i, .aq  , _acquire)
ATOMIC_OPS( or,  or, i, .rl  , _release)
ATOMIC_OPS( or,  or, i, .aqrl,         )

ATOMIC_OPS(xor, xor, i,      , _relaxed)
ATOMIC_OPS(xor, xor, i, .aq  , _acquire)
ATOMIC_OPS(xor, xor, i, .rl  , _release)
ATOMIC_OPS(xor, xor, i, .aqrl,         )

#undef ATOMIC_OPS

#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN

/*
 * The extra atomic operations that are constructed from one of the core
 * AMO-based operations above (aside from sub, which is easier to fit above).
 * These are required to perform a barrier, but they're OK this way because
 * atomic_*_return is also required to perform a barrier.
 */
#define ATOMIC_OP(op, func_op, comp_op, I, c_type, prefix)			\
static __always_inline bool atomic##prefix##_##op(c_type i, atomic##prefix##_t *v) \
{										\
	return atomic##prefix##_##func_op##_return(i, v) comp_op I;		\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, func_op, comp_op, I)			\
        ATOMIC_OP (op, func_op, comp_op, I,  int,   )
#else
#define ATOMIC_OPS(op, func_op, comp_op, I)			\
        ATOMIC_OP (op, func_op, comp_op, I,  int,   )		\
        ATOMIC_OP (op, func_op, comp_op, I, long, 64)
#endif

ATOMIC_OPS(add_and_test, add, ==, 0)
ATOMIC_OPS(sub_and_test, sub, ==, 0)
ATOMIC_OPS(add_negative, add,  <, 0)

#undef ATOMIC_OP
#undef ATOMIC_OPS

#define ATOMIC_OP(op, func_op, I, c_type, prefix)				\
static __always_inline void atomic##prefix##_##op(atomic##prefix##_t *v)	\
{										\
	atomic##prefix##_##func_op(I, v);					\
}

#define ATOMIC_FETCH_OP(op, func_op, I, c_type, prefix)					\
static __always_inline c_type atomic##prefix##_fetch_##op(atomic##prefix##_t *v)	\
{											\
	return atomic##prefix##_fetch_##func_op(I, v);					\
}

#define ATOMIC_OP_RETURN(op, asm_op, c_op, I, c_type, prefix)				\
static __always_inline c_type atomic##prefix##_##op##_return(atomic##prefix##_t *v)	\
{											\
        return atomic##prefix##_fetch_##op(v) c_op I;					\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(op, asm_op, c_op, I)						\
        ATOMIC_OP       (op, asm_op,       I,  int,   )				\
        ATOMIC_FETCH_OP (op, asm_op,       I,  int,   )				\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I,  int,   )
#else
#define ATOMIC_OPS(op, asm_op, c_op, I)						\
        ATOMIC_OP       (op, asm_op,       I,  int,   )				\
        ATOMIC_FETCH_OP (op, asm_op,       I,  int,   )				\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I,  int,   )				\
        ATOMIC_OP       (op, asm_op,       I, long, 64)				\
        ATOMIC_FETCH_OP (op, asm_op,       I, long, 64)				\
        ATOMIC_OP_RETURN(op, asm_op, c_op, I, long, 64)
#endif

ATOMIC_OPS(inc, add, +,  1)
ATOMIC_OPS(dec, add, +, -1)

#undef ATOMIC_OPS
#undef ATOMIC_OP
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN

#define ATOMIC_OP(op, func_op, comp_op, I, prefix)				\
static __always_inline bool atomic##prefix##_##op(atomic##prefix##_t *v)	\
{										\
	return atomic##prefix##_##func_op##_return(v) comp_op I;		\
}

ATOMIC_OP(inc_and_test, inc, ==, 0,   )
ATOMIC_OP(dec_and_test, dec, ==, 0,   )
#ifndef CONFIG_GENERIC_ATOMIC64
ATOMIC_OP(inc_and_test, inc, ==, 0, 64)
ATOMIC_OP(dec_and_test, dec, ==, 0, 64)
#endif

#undef ATOMIC_OP

/* This is required to provide a barrier on success. */
static __always_inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
       int prev, rc;

	__asm__ __volatile__ (
		"0:\n\t"
		"lr.w.aqrl  %[p],  %[c]\n\t"
		"beq        %[p],  %[u], 1f\n\t"
		"add       %[rc],  %[p], %[a]\n\t"
		"sc.w.aqrl %[rc], %[rc], %[c]\n\t"
		"bnez      %[rc], 0b\n\t"
		"1:"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");
	return prev;
}

#ifndef CONFIG_GENERIC_ATOMIC64
static __always_inline long __atomic64_add_unless(atomic64_t *v, long a, long u)
{
       long prev, rc;

	__asm__ __volatile__ (
		"0:\n\t"
		"lr.d.aqrl  %[p],  %[c]\n\t"
		"beq        %[p],  %[u], 1f\n\t"
		"add       %[rc],  %[p], %[a]\n\t"
		"sc.d.aqrl %[rc], %[rc], %[c]\n\t"
		"bnez      %[rc], 0b\n\t"
		"1:"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");
	return prev;
}

static __always_inline int atomic64_add_unless(atomic64_t *v, long a, long u)
{
	return __atomic64_add_unless(v, a, u) != u;
}
#endif

/*
 * The extra atomic operations that are constructed from one of the core
 * LR/SC-based operations above.
 */
static __always_inline int atomic_inc_not_zero(atomic_t *v)
{
        return __atomic_add_unless(v, 1, 0);
}

#ifndef CONFIG_GENERIC_ATOMIC64
static __always_inline long atomic64_inc_not_zero(atomic64_t *v)
{
        return atomic64_add_unless(v, 1, 0);
}
#endif

/*
 * atomic_{cmp,}xchg is required to have exactly the same ordering semantics as
 * {cmp,}xchg and the operations that return, so they need a barrier.  We just
 * use the other implementations directly.
 */
#define ATOMIC_OP(c_t, prefix, c_or, size, asm_or)						\
static __always_inline c_t atomic##prefix##_cmpxchg##c_or(atomic##prefix##_t *v, c_t o, c_t n) 	\
{												\
	return __cmpxchg(&(v->counter), o, n, size, asm_or, asm_or);				\
}												\
static __always_inline c_t atomic##prefix##_xchg##c_or(atomic##prefix##_t *v, c_t n) 		\
{												\
	return __xchg(n, &(v->counter), size, asm_or);						\
}

#ifdef CONFIG_GENERIC_ATOMIC64
#define ATOMIC_OPS(c_or, asm_or)			\
	ATOMIC_OP( int,   , c_or, 4, asm_or)
#else
#define ATOMIC_OPS(c_or, asm_or)			\
	ATOMIC_OP( int,   , c_or, 4, asm_or)		\
	ATOMIC_OP(long, 64, c_or, 8, asm_or)
#endif

ATOMIC_OPS(        , .aqrl)
ATOMIC_OPS(_acquire,   .aq)
ATOMIC_OPS(_release,   .rl)
ATOMIC_OPS(_relaxed,      )

#undef ATOMIC_OPS
#undef ATOMIC_OP

static __always_inline int atomic_sub_if_positive(atomic_t *v, int offset)
{
       int prev, rc;

	__asm__ __volatile__ (
		"0:\n\t"
		"lr.w.aqrl  %[p],  %[c]\n\t"
		"sub       %[rc],  %[p], %[o]\n\t"
		"bltz      %[rc],    1f\n\t"
		"sc.w.aqrl %[rc], %[rc], %[c]\n\t"
		"bnez      %[rc],    0b\n\t"
		"1:"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [o]"r" (offset)
		: "memory");
	return prev - offset;
}

#define atomic_dec_if_positive(v)	atomic_sub_if_positive(v, 1)

#ifndef CONFIG_GENERIC_ATOMIC64
static __always_inline long atomic64_sub_if_positive(atomic64_t *v, int offset)
{
       long prev, rc;

	__asm__ __volatile__ (
		"0:\n\t"
		"lr.d.aqrl  %[p],  %[c]\n\t"
		"sub       %[rc],  %[p], %[o]\n\t"
		"bltz      %[rc],    1f\n\t"
		"sc.d.aqrl %[rc], %[rc], %[c]\n\t"
		"bnez      %[rc],    0b\n\t"
		"1:"
		: [p]"=&r" (prev), [rc]"=&r" (rc), [c]"+A" (v->counter)
		: [o]"r" (offset)
		: "memory");
	return prev - offset;
}

#define atomic64_dec_if_positive(v)	atomic64_sub_if_positive(v, 1)
#endif

#endif /* _ASM_RISCV_ATOMIC_H */
