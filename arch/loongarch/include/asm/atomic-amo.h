/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Atomic operations (AMO).
 *
 * Copyright (C) 2020-2025 Loongson Technology Corporation Limited
 */

#ifndef _ASM_ATOMIC_AMO_H
#define _ASM_ATOMIC_AMO_H

#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#define ATOMIC_OP(op, I, asm_op)					\
static inline void arch_atomic_##op(int i, atomic_t *v)			\
{									\
	__asm__ __volatile__(						\
	"am"#asm_op".w" " $zero, %1, %0	\n"				\
	: "+ZB" (v->counter)						\
	: "r" (I)							\
	: "memory");							\
}

#define ATOMIC_OP_RETURN(op, I, asm_op, c_op, mb, suffix)		\
static inline int arch_atomic_##op##_return##suffix(int i, atomic_t *v)	\
{									\
	int result;							\
									\
	__asm__ __volatile__(						\
	"am"#asm_op#mb".w" " %1, %2, %0		\n"			\
	: "+ZB" (v->counter), "=&r" (result)				\
	: "r" (I)							\
	: "memory");							\
									\
	return result c_op I;						\
}

#define ATOMIC_FETCH_OP(op, I, asm_op, mb, suffix)			\
static inline int arch_atomic_fetch_##op##suffix(int i, atomic_t *v)	\
{									\
	int result;							\
									\
	__asm__ __volatile__(						\
	"am"#asm_op#mb".w" " %1, %2, %0		\n"			\
	: "+ZB" (v->counter), "=&r" (result)				\
	: "r" (I)							\
	: "memory");							\
									\
	return result;							\
}

#define ATOMIC_OPS(op, I, asm_op, c_op)					\
	ATOMIC_OP(op, I, asm_op)					\
	ATOMIC_OP_RETURN(op, I, asm_op, c_op, _db,         )		\
	ATOMIC_OP_RETURN(op, I, asm_op, c_op,    , _relaxed)		\
	ATOMIC_FETCH_OP(op, I, asm_op, _db,         )			\
	ATOMIC_FETCH_OP(op, I, asm_op,    , _relaxed)

ATOMIC_OPS(add, i, add, +)
ATOMIC_OPS(sub, -i, add, +)

#define arch_atomic_add_return		arch_atomic_add_return
#define arch_atomic_add_return_acquire	arch_atomic_add_return
#define arch_atomic_add_return_release	arch_atomic_add_return
#define arch_atomic_add_return_relaxed	arch_atomic_add_return_relaxed
#define arch_atomic_sub_return		arch_atomic_sub_return
#define arch_atomic_sub_return_acquire	arch_atomic_sub_return
#define arch_atomic_sub_return_release	arch_atomic_sub_return
#define arch_atomic_sub_return_relaxed	arch_atomic_sub_return_relaxed
#define arch_atomic_fetch_add		arch_atomic_fetch_add
#define arch_atomic_fetch_add_acquire	arch_atomic_fetch_add
#define arch_atomic_fetch_add_release	arch_atomic_fetch_add
#define arch_atomic_fetch_add_relaxed	arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_sub		arch_atomic_fetch_sub
#define arch_atomic_fetch_sub_acquire	arch_atomic_fetch_sub
#define arch_atomic_fetch_sub_release	arch_atomic_fetch_sub
#define arch_atomic_fetch_sub_relaxed	arch_atomic_fetch_sub_relaxed

#undef ATOMIC_OPS

#define ATOMIC_OPS(op, I, asm_op)					\
	ATOMIC_OP(op, I, asm_op)					\
	ATOMIC_FETCH_OP(op, I, asm_op, _db,         )			\
	ATOMIC_FETCH_OP(op, I, asm_op,    , _relaxed)

ATOMIC_OPS(and, i, and)
ATOMIC_OPS(or, i, or)
ATOMIC_OPS(xor, i, xor)

#define arch_atomic_fetch_and		arch_atomic_fetch_and
#define arch_atomic_fetch_and_acquire	arch_atomic_fetch_and
#define arch_atomic_fetch_and_release	arch_atomic_fetch_and
#define arch_atomic_fetch_and_relaxed	arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_or		arch_atomic_fetch_or
#define arch_atomic_fetch_or_acquire	arch_atomic_fetch_or
#define arch_atomic_fetch_or_release	arch_atomic_fetch_or
#define arch_atomic_fetch_or_relaxed	arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_xor		arch_atomic_fetch_xor
#define arch_atomic_fetch_xor_acquire	arch_atomic_fetch_xor
#define arch_atomic_fetch_xor_release	arch_atomic_fetch_xor
#define arch_atomic_fetch_xor_relaxed	arch_atomic_fetch_xor_relaxed

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#ifdef CONFIG_64BIT

#define ATOMIC64_OP(op, I, asm_op)					\
static inline void arch_atomic64_##op(long i, atomic64_t *v)		\
{									\
	__asm__ __volatile__(						\
	"am"#asm_op".d " " $zero, %1, %0	\n"			\
	: "+ZB" (v->counter)						\
	: "r" (I)							\
	: "memory");							\
}

#define ATOMIC64_OP_RETURN(op, I, asm_op, c_op, mb, suffix)			\
static inline long arch_atomic64_##op##_return##suffix(long i, atomic64_t *v)	\
{										\
	long result;								\
	__asm__ __volatile__(							\
	"am"#asm_op#mb".d " " %1, %2, %0		\n"			\
	: "+ZB" (v->counter), "=&r" (result)					\
	: "r" (I)								\
	: "memory");								\
										\
	return result c_op I;							\
}

#define ATOMIC64_FETCH_OP(op, I, asm_op, mb, suffix)				\
static inline long arch_atomic64_fetch_##op##suffix(long i, atomic64_t *v)	\
{										\
	long result;								\
										\
	__asm__ __volatile__(							\
	"am"#asm_op#mb".d " " %1, %2, %0		\n"			\
	: "+ZB" (v->counter), "=&r" (result)					\
	: "r" (I)								\
	: "memory");								\
										\
	return result;								\
}

#define ATOMIC64_OPS(op, I, asm_op, c_op)				      \
	ATOMIC64_OP(op, I, asm_op)					      \
	ATOMIC64_OP_RETURN(op, I, asm_op, c_op, _db,         )		      \
	ATOMIC64_OP_RETURN(op, I, asm_op, c_op,    , _relaxed)		      \
	ATOMIC64_FETCH_OP(op, I, asm_op, _db,         )			      \
	ATOMIC64_FETCH_OP(op, I, asm_op,    , _relaxed)

ATOMIC64_OPS(add, i, add, +)
ATOMIC64_OPS(sub, -i, add, +)

#define arch_atomic64_add_return		arch_atomic64_add_return
#define arch_atomic64_add_return_acquire	arch_atomic64_add_return
#define arch_atomic64_add_return_release	arch_atomic64_add_return
#define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
#define arch_atomic64_sub_return		arch_atomic64_sub_return
#define arch_atomic64_sub_return_acquire	arch_atomic64_sub_return
#define arch_atomic64_sub_return_release	arch_atomic64_sub_return
#define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed
#define arch_atomic64_fetch_add			arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_acquire		arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_release		arch_atomic64_fetch_add
#define arch_atomic64_fetch_add_relaxed		arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_sub			arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_acquire		arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_release		arch_atomic64_fetch_sub
#define arch_atomic64_fetch_sub_relaxed		arch_atomic64_fetch_sub_relaxed

#undef ATOMIC64_OPS

#define ATOMIC64_OPS(op, I, asm_op)					      \
	ATOMIC64_OP(op, I, asm_op)					      \
	ATOMIC64_FETCH_OP(op, I, asm_op, _db,         )			      \
	ATOMIC64_FETCH_OP(op, I, asm_op,    , _relaxed)

ATOMIC64_OPS(and, i, and)
ATOMIC64_OPS(or, i, or)
ATOMIC64_OPS(xor, i, xor)

#define arch_atomic64_fetch_and		arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_acquire	arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_release	arch_atomic64_fetch_and
#define arch_atomic64_fetch_and_relaxed	arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_or		arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_acquire	arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_release	arch_atomic64_fetch_or
#define arch_atomic64_fetch_or_relaxed	arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_xor		arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_acquire	arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_release	arch_atomic64_fetch_xor
#define arch_atomic64_fetch_xor_relaxed	arch_atomic64_fetch_xor_relaxed

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

#endif

#endif /* _ASM_ATOMIC_AMO_H */
