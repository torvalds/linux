/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/atomic.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2002 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_ATOMIC_H
#define __ASM_ATOMIC_H

#include <linux/compiler.h>
#include <linux/types.h>

#include <asm/barrier.h>
#include <asm/cmpxchg.h>
#include <asm/lse.h>

#define ATOMIC_OP(op)							\
static inline void arch_##op(int i, atomic_t *v)			\
{									\
	__lse_ll_sc_body(op, i, v);					\
}

ATOMIC_OP(atomic_andnot)
ATOMIC_OP(atomic_or)
ATOMIC_OP(atomic_xor)
ATOMIC_OP(atomic_add)
ATOMIC_OP(atomic_and)
ATOMIC_OP(atomic_sub)

#undef ATOMIC_OP

#define ATOMIC_FETCH_OP(name, op)					\
static inline int arch_##op##name(int i, atomic_t *v)			\
{									\
	return __lse_ll_sc_body(op##name, i, v);			\
}

#define ATOMIC_FETCH_OPS(op)						\
	ATOMIC_FETCH_OP(_relaxed, op)					\
	ATOMIC_FETCH_OP(_acquire, op)					\
	ATOMIC_FETCH_OP(_release, op)					\
	ATOMIC_FETCH_OP(        , op)

ATOMIC_FETCH_OPS(atomic_fetch_andnot)
ATOMIC_FETCH_OPS(atomic_fetch_or)
ATOMIC_FETCH_OPS(atomic_fetch_xor)
ATOMIC_FETCH_OPS(atomic_fetch_add)
ATOMIC_FETCH_OPS(atomic_fetch_and)
ATOMIC_FETCH_OPS(atomic_fetch_sub)
ATOMIC_FETCH_OPS(atomic_add_return)
ATOMIC_FETCH_OPS(atomic_sub_return)

#undef ATOMIC_FETCH_OP
#undef ATOMIC_FETCH_OPS

#define ATOMIC64_OP(op)							\
static inline void arch_##op(long i, atomic64_t *v)			\
{									\
	__lse_ll_sc_body(op, i, v);					\
}

ATOMIC64_OP(atomic64_andnot)
ATOMIC64_OP(atomic64_or)
ATOMIC64_OP(atomic64_xor)
ATOMIC64_OP(atomic64_add)
ATOMIC64_OP(atomic64_and)
ATOMIC64_OP(atomic64_sub)

#undef ATOMIC64_OP

#define ATOMIC64_FETCH_OP(name, op)					\
static inline long arch_##op##name(long i, atomic64_t *v)		\
{									\
	return __lse_ll_sc_body(op##name, i, v);			\
}

#define ATOMIC64_FETCH_OPS(op)						\
	ATOMIC64_FETCH_OP(_relaxed, op)					\
	ATOMIC64_FETCH_OP(_acquire, op)					\
	ATOMIC64_FETCH_OP(_release, op)					\
	ATOMIC64_FETCH_OP(        , op)

ATOMIC64_FETCH_OPS(atomic64_fetch_andnot)
ATOMIC64_FETCH_OPS(atomic64_fetch_or)
ATOMIC64_FETCH_OPS(atomic64_fetch_xor)
ATOMIC64_FETCH_OPS(atomic64_fetch_add)
ATOMIC64_FETCH_OPS(atomic64_fetch_and)
ATOMIC64_FETCH_OPS(atomic64_fetch_sub)
ATOMIC64_FETCH_OPS(atomic64_add_return)
ATOMIC64_FETCH_OPS(atomic64_sub_return)

#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_FETCH_OPS

static inline long arch_atomic64_dec_if_positive(atomic64_t *v)
{
	return __lse_ll_sc_body(atomic64_dec_if_positive, v);
}

#define arch_atomic_read(v)			__READ_ONCE((v)->counter)
#define arch_atomic_set(v, i)			__WRITE_ONCE(((v)->counter), (i))

#define arch_atomic_add_return_relaxed		arch_atomic_add_return_relaxed
#define arch_atomic_add_return_acquire		arch_atomic_add_return_acquire
#define arch_atomic_add_return_release		arch_atomic_add_return_release
#define arch_atomic_add_return			arch_atomic_add_return

#define arch_atomic_sub_return_relaxed		arch_atomic_sub_return_relaxed
#define arch_atomic_sub_return_acquire		arch_atomic_sub_return_acquire
#define arch_atomic_sub_return_release		arch_atomic_sub_return_release
#define arch_atomic_sub_return			arch_atomic_sub_return

#define arch_atomic_fetch_add_relaxed		arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_add_acquire		arch_atomic_fetch_add_acquire
#define arch_atomic_fetch_add_release		arch_atomic_fetch_add_release
#define arch_atomic_fetch_add			arch_atomic_fetch_add

#define arch_atomic_fetch_sub_relaxed		arch_atomic_fetch_sub_relaxed
#define arch_atomic_fetch_sub_acquire		arch_atomic_fetch_sub_acquire
#define arch_atomic_fetch_sub_release		arch_atomic_fetch_sub_release
#define arch_atomic_fetch_sub			arch_atomic_fetch_sub

#define arch_atomic_fetch_and_relaxed		arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_and_acquire		arch_atomic_fetch_and_acquire
#define arch_atomic_fetch_and_release		arch_atomic_fetch_and_release
#define arch_atomic_fetch_and			arch_atomic_fetch_and

#define arch_atomic_fetch_andnot_relaxed	arch_atomic_fetch_andnot_relaxed
#define arch_atomic_fetch_andnot_acquire	arch_atomic_fetch_andnot_acquire
#define arch_atomic_fetch_andnot_release	arch_atomic_fetch_andnot_release
#define arch_atomic_fetch_andnot		arch_atomic_fetch_andnot

#define arch_atomic_fetch_or_relaxed		arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_or_acquire		arch_atomic_fetch_or_acquire
#define arch_atomic_fetch_or_release		arch_atomic_fetch_or_release
#define arch_atomic_fetch_or			arch_atomic_fetch_or

#define arch_atomic_fetch_xor_relaxed		arch_atomic_fetch_xor_relaxed
#define arch_atomic_fetch_xor_acquire		arch_atomic_fetch_xor_acquire
#define arch_atomic_fetch_xor_release		arch_atomic_fetch_xor_release
#define arch_atomic_fetch_xor			arch_atomic_fetch_xor

#define arch_atomic_xchg_relaxed(v, new) \
	arch_xchg_relaxed(&((v)->counter), (new))
#define arch_atomic_xchg_acquire(v, new) \
	arch_xchg_acquire(&((v)->counter), (new))
#define arch_atomic_xchg_release(v, new) \
	arch_xchg_release(&((v)->counter), (new))
#define arch_atomic_xchg(v, new) \
	arch_xchg(&((v)->counter), (new))

#define arch_atomic_cmpxchg_relaxed(v, old, new) \
	arch_cmpxchg_relaxed(&((v)->counter), (old), (new))
#define arch_atomic_cmpxchg_acquire(v, old, new) \
	arch_cmpxchg_acquire(&((v)->counter), (old), (new))
#define arch_atomic_cmpxchg_release(v, old, new) \
	arch_cmpxchg_release(&((v)->counter), (old), (new))
#define arch_atomic_cmpxchg(v, old, new) \
	arch_cmpxchg(&((v)->counter), (old), (new))

#define arch_atomic_andnot			arch_atomic_andnot

/*
 * 64-bit arch_atomic operations.
 */
#define ATOMIC64_INIT				ATOMIC_INIT
#define arch_atomic64_read			arch_atomic_read
#define arch_atomic64_set			arch_atomic_set

#define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
#define arch_atomic64_add_return_acquire	arch_atomic64_add_return_acquire
#define arch_atomic64_add_return_release	arch_atomic64_add_return_release
#define arch_atomic64_add_return		arch_atomic64_add_return

#define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed
#define arch_atomic64_sub_return_acquire	arch_atomic64_sub_return_acquire
#define arch_atomic64_sub_return_release	arch_atomic64_sub_return_release
#define arch_atomic64_sub_return		arch_atomic64_sub_return

#define arch_atomic64_fetch_add_relaxed		arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_add_acquire		arch_atomic64_fetch_add_acquire
#define arch_atomic64_fetch_add_release		arch_atomic64_fetch_add_release
#define arch_atomic64_fetch_add			arch_atomic64_fetch_add

#define arch_atomic64_fetch_sub_relaxed		arch_atomic64_fetch_sub_relaxed
#define arch_atomic64_fetch_sub_acquire		arch_atomic64_fetch_sub_acquire
#define arch_atomic64_fetch_sub_release		arch_atomic64_fetch_sub_release
#define arch_atomic64_fetch_sub			arch_atomic64_fetch_sub

#define arch_atomic64_fetch_and_relaxed		arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_and_acquire		arch_atomic64_fetch_and_acquire
#define arch_atomic64_fetch_and_release		arch_atomic64_fetch_and_release
#define arch_atomic64_fetch_and			arch_atomic64_fetch_and

#define arch_atomic64_fetch_andnot_relaxed	arch_atomic64_fetch_andnot_relaxed
#define arch_atomic64_fetch_andnot_acquire	arch_atomic64_fetch_andnot_acquire
#define arch_atomic64_fetch_andnot_release	arch_atomic64_fetch_andnot_release
#define arch_atomic64_fetch_andnot		arch_atomic64_fetch_andnot

#define arch_atomic64_fetch_or_relaxed		arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_or_acquire		arch_atomic64_fetch_or_acquire
#define arch_atomic64_fetch_or_release		arch_atomic64_fetch_or_release
#define arch_atomic64_fetch_or			arch_atomic64_fetch_or

#define arch_atomic64_fetch_xor_relaxed		arch_atomic64_fetch_xor_relaxed
#define arch_atomic64_fetch_xor_acquire		arch_atomic64_fetch_xor_acquire
#define arch_atomic64_fetch_xor_release		arch_atomic64_fetch_xor_release
#define arch_atomic64_fetch_xor			arch_atomic64_fetch_xor

#define arch_atomic64_xchg_relaxed		arch_atomic_xchg_relaxed
#define arch_atomic64_xchg_acquire		arch_atomic_xchg_acquire
#define arch_atomic64_xchg_release		arch_atomic_xchg_release
#define arch_atomic64_xchg			arch_atomic_xchg

#define arch_atomic64_cmpxchg_relaxed		arch_atomic_cmpxchg_relaxed
#define arch_atomic64_cmpxchg_acquire		arch_atomic_cmpxchg_acquire
#define arch_atomic64_cmpxchg_release		arch_atomic_cmpxchg_release
#define arch_atomic64_cmpxchg			arch_atomic_cmpxchg

#define arch_atomic64_andnot			arch_atomic64_andnot

#define arch_atomic64_dec_if_positive		arch_atomic64_dec_if_positive

#define ARCH_ATOMIC

#endif /* __ASM_ATOMIC_H */
