/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Selection between LSE and LL/SC atomics.
 *
 * Copyright (C) 2018 ARM Ltd.
 * Author: Andrew Murray <andrew.murray@arm.com>
 */

#ifndef __ASM_ATOMIC_ARCH_H
#define __ASM_ATOMIC_ARCH_H


#include <linux/jump_label.h>

#include <asm/cpucaps.h>
#include <asm/atomic_ll_sc.h>
#include <asm/atomic_lse.h>

extern struct static_key_false cpu_hwcap_keys[ARM64_NCAPS];
extern struct static_key_false arm64_const_caps_ready;

static inline bool system_uses_lse_atomics(void)
{
	return (IS_ENABLED(CONFIG_ARM64_LSE_ATOMICS) &&
		IS_ENABLED(CONFIG_AS_LSE) &&
		static_branch_likely(&arm64_const_caps_ready)) &&
		static_branch_likely(&cpu_hwcap_keys[ARM64_HAS_LSE_ATOMICS]);
}

#define __lse_ll_sc_body(op, ...)					\
({									\
	system_uses_lse_atomics() ?					\
		__lse_##op(__VA_ARGS__) :				\
		__ll_sc_##op(__VA_ARGS__);				\
})

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


static inline long arch_atomic64_dec_if_positive(atomic64_t *v)
{
	return __lse_ll_sc_body(atomic64_dec_if_positive, v);
}

#define __CMPXCHG_CASE(name, sz)			\
static inline u##sz __cmpxchg_case_##name##sz(volatile void *ptr,	\
					      u##sz old,		\
					      u##sz new)		\
{									\
	return __lse_ll_sc_body(_cmpxchg_case_##name##sz,		\
				ptr, old, new);				\
}

__CMPXCHG_CASE(    ,  8)
__CMPXCHG_CASE(    , 16)
__CMPXCHG_CASE(    , 32)
__CMPXCHG_CASE(    , 64)
__CMPXCHG_CASE(acq_,  8)
__CMPXCHG_CASE(acq_, 16)
__CMPXCHG_CASE(acq_, 32)
__CMPXCHG_CASE(acq_, 64)
__CMPXCHG_CASE(rel_,  8)
__CMPXCHG_CASE(rel_, 16)
__CMPXCHG_CASE(rel_, 32)
__CMPXCHG_CASE(rel_, 64)
__CMPXCHG_CASE(mb_,  8)
__CMPXCHG_CASE(mb_, 16)
__CMPXCHG_CASE(mb_, 32)
__CMPXCHG_CASE(mb_, 64)


#define __CMPXCHG_DBL(name)						\
static inline long __cmpxchg_double##name(unsigned long old1,		\
					 unsigned long old2,		\
					 unsigned long new1,		\
					 unsigned long new2,		\
					 volatile void *ptr)		\
{									\
	return __lse_ll_sc_body(_cmpxchg_double##name, 			\
				old1, old2, new1, new2, ptr);		\
}

__CMPXCHG_DBL(   )
__CMPXCHG_DBL(_mb)

#endif	/* __ASM_ATOMIC_LSE_H */
