/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/atomic.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2002 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 */

#ifndef __ASM_ATOMIC_LSE_H
#define __ASM_ATOMIC_LSE_H

#define ATOMIC_OP(op, asm_op)						\
static __always_inline void						\
__lse_atomic_##op(int i, atomic_t *v)					\
{									\
	asm volatile(							\
	__LSE_PREAMBLE							\
	"	" #asm_op "	%w[i], %[v]\n"				\
	: [v] "+Q" (v->counter)						\
	: [i] "r" (i));							\
}

ATOMIC_OP(andnot, stclr)
ATOMIC_OP(or, stset)
ATOMIC_OP(xor, steor)
ATOMIC_OP(add, stadd)

static __always_inline void __lse_atomic_sub(int i, atomic_t *v)
{
	__lse_atomic_add(-i, v);
}

#undef ATOMIC_OP

#define ATOMIC_FETCH_OP(name, mb, op, asm_op, cl...)			\
static __always_inline int						\
__lse_atomic_fetch_##op##name(int i, atomic_t *v)			\
{									\
	int old;							\
									\
	asm volatile(							\
	__LSE_PREAMBLE							\
	"	" #asm_op #mb "	%w[i], %w[old], %[v]"			\
	: [v] "+Q" (v->counter),					\
	  [old] "=r" (old)						\
	: [i] "r" (i)							\
	: cl);								\
									\
	return old;							\
}

#define ATOMIC_FETCH_OPS(op, asm_op)					\
	ATOMIC_FETCH_OP(_relaxed,   , op, asm_op)			\
	ATOMIC_FETCH_OP(_acquire,  a, op, asm_op, "memory")		\
	ATOMIC_FETCH_OP(_release,  l, op, asm_op, "memory")		\
	ATOMIC_FETCH_OP(        , al, op, asm_op, "memory")

ATOMIC_FETCH_OPS(andnot, ldclr)
ATOMIC_FETCH_OPS(or, ldset)
ATOMIC_FETCH_OPS(xor, ldeor)
ATOMIC_FETCH_OPS(add, ldadd)

#undef ATOMIC_FETCH_OP
#undef ATOMIC_FETCH_OPS

#define ATOMIC_FETCH_OP_SUB(name)					\
static __always_inline int						\
__lse_atomic_fetch_sub##name(int i, atomic_t *v)			\
{									\
	return __lse_atomic_fetch_add##name(-i, v);			\
}

ATOMIC_FETCH_OP_SUB(_relaxed)
ATOMIC_FETCH_OP_SUB(_acquire)
ATOMIC_FETCH_OP_SUB(_release)
ATOMIC_FETCH_OP_SUB(        )

#undef ATOMIC_FETCH_OP_SUB

#define ATOMIC_OP_ADD_SUB_RETURN(name)					\
static __always_inline int						\
__lse_atomic_add_return##name(int i, atomic_t *v)			\
{									\
	return __lse_atomic_fetch_add##name(i, v) + i;			\
}									\
									\
static __always_inline int						\
__lse_atomic_sub_return##name(int i, atomic_t *v)			\
{									\
	return __lse_atomic_fetch_sub(i, v) - i;			\
}

ATOMIC_OP_ADD_SUB_RETURN(_relaxed)
ATOMIC_OP_ADD_SUB_RETURN(_acquire)
ATOMIC_OP_ADD_SUB_RETURN(_release)
ATOMIC_OP_ADD_SUB_RETURN(        )

#undef ATOMIC_OP_ADD_SUB_RETURN

static __always_inline void __lse_atomic_and(int i, atomic_t *v)
{
	return __lse_atomic_andnot(~i, v);
}

#define ATOMIC_FETCH_OP_AND(name, mb, cl...)				\
static __always_inline int						\
__lse_atomic_fetch_and##name(int i, atomic_t *v)			\
{									\
	return __lse_atomic_fetch_andnot##name(~i, v);			\
}

ATOMIC_FETCH_OP_AND(_relaxed,   )
ATOMIC_FETCH_OP_AND(_acquire,  a, "memory")
ATOMIC_FETCH_OP_AND(_release,  l, "memory")
ATOMIC_FETCH_OP_AND(        , al, "memory")

#undef ATOMIC_FETCH_OP_AND

#define ATOMIC64_OP(op, asm_op)						\
static __always_inline void						\
__lse_atomic64_##op(s64 i, atomic64_t *v)				\
{									\
	asm volatile(							\
	__LSE_PREAMBLE							\
	"	" #asm_op "	%[i], %[v]\n"				\
	: [v] "+Q" (v->counter)						\
	: [i] "r" (i));							\
}

ATOMIC64_OP(andnot, stclr)
ATOMIC64_OP(or, stset)
ATOMIC64_OP(xor, steor)
ATOMIC64_OP(add, stadd)

static __always_inline void __lse_atomic64_sub(s64 i, atomic64_t *v)
{
	__lse_atomic64_add(-i, v);
}

#undef ATOMIC64_OP

#define ATOMIC64_FETCH_OP(name, mb, op, asm_op, cl...)			\
static __always_inline long						\
__lse_atomic64_fetch_##op##name(s64 i, atomic64_t *v)			\
{									\
	s64 old;							\
									\
	asm volatile(							\
	__LSE_PREAMBLE							\
	"	" #asm_op #mb "	%[i], %[old], %[v]"			\
	: [v] "+Q" (v->counter),					\
	  [old] "=r" (old)						\
	: [i] "r" (i) 							\
	: cl);								\
									\
	return old;							\
}

#define ATOMIC64_FETCH_OPS(op, asm_op)					\
	ATOMIC64_FETCH_OP(_relaxed,   , op, asm_op)			\
	ATOMIC64_FETCH_OP(_acquire,  a, op, asm_op, "memory")		\
	ATOMIC64_FETCH_OP(_release,  l, op, asm_op, "memory")		\
	ATOMIC64_FETCH_OP(        , al, op, asm_op, "memory")

ATOMIC64_FETCH_OPS(andnot, ldclr)
ATOMIC64_FETCH_OPS(or, ldset)
ATOMIC64_FETCH_OPS(xor, ldeor)
ATOMIC64_FETCH_OPS(add, ldadd)

#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_FETCH_OPS

#define ATOMIC64_FETCH_OP_SUB(name)					\
static __always_inline long						\
__lse_atomic64_fetch_sub##name(s64 i, atomic64_t *v)			\
{									\
	return __lse_atomic64_fetch_add##name(-i, v);			\
}

ATOMIC64_FETCH_OP_SUB(_relaxed)
ATOMIC64_FETCH_OP_SUB(_acquire)
ATOMIC64_FETCH_OP_SUB(_release)
ATOMIC64_FETCH_OP_SUB(        )

#undef ATOMIC64_FETCH_OP_SUB

#define ATOMIC64_OP_ADD_SUB_RETURN(name)				\
static __always_inline long						\
__lse_atomic64_add_return##name(s64 i, atomic64_t *v)			\
{									\
	return __lse_atomic64_fetch_add##name(i, v) + i;		\
}									\
									\
static __always_inline long						\
__lse_atomic64_sub_return##name(s64 i, atomic64_t *v)			\
{									\
	return __lse_atomic64_fetch_sub##name(i, v) - i;		\
}

ATOMIC64_OP_ADD_SUB_RETURN(_relaxed)
ATOMIC64_OP_ADD_SUB_RETURN(_acquire)
ATOMIC64_OP_ADD_SUB_RETURN(_release)
ATOMIC64_OP_ADD_SUB_RETURN(        )

#undef ATOMIC64_OP_ADD_SUB_RETURN

static __always_inline void __lse_atomic64_and(s64 i, atomic64_t *v)
{
	return __lse_atomic64_andnot(~i, v);
}

#define ATOMIC64_FETCH_OP_AND(name, mb, cl...)				\
static __always_inline long						\
__lse_atomic64_fetch_and##name(s64 i, atomic64_t *v)			\
{									\
	return __lse_atomic64_fetch_andnot##name(~i, v);		\
}

ATOMIC64_FETCH_OP_AND(_relaxed,   )
ATOMIC64_FETCH_OP_AND(_acquire,  a, "memory")
ATOMIC64_FETCH_OP_AND(_release,  l, "memory")
ATOMIC64_FETCH_OP_AND(        , al, "memory")

#undef ATOMIC64_FETCH_OP_AND

static __always_inline s64 __lse_atomic64_dec_if_positive(atomic64_t *v)
{
	unsigned long tmp;

	asm volatile(
	__LSE_PREAMBLE
	"1:	ldr	%x[tmp], %[v]\n"
	"	subs	%[ret], %x[tmp], #1\n"
	"	b.lt	2f\n"
	"	casal	%x[tmp], %[ret], %[v]\n"
	"	sub	%x[tmp], %x[tmp], #1\n"
	"	sub	%x[tmp], %x[tmp], %[ret]\n"
	"	cbnz	%x[tmp], 1b\n"
	"2:"
	: [ret] "+&r" (v), [v] "+Q" (v->counter), [tmp] "=&r" (tmp)
	:
	: "cc", "memory");

	return (long)v;
}

#define __CMPXCHG_CASE(w, sfx, name, sz, mb, cl...)			\
static __always_inline u##sz						\
__lse__cmpxchg_case_##name##sz(volatile void *ptr,			\
					      u##sz old,		\
					      u##sz new)		\
{									\
	asm volatile(							\
	__LSE_PREAMBLE							\
	"	cas" #mb #sfx "	%" #w "[old], %" #w "[new], %[v]\n"	\
	: [v] "+Q" (*(u##sz *)ptr),					\
	  [old] "+r" (old)						\
	: [new] "rZ" (new)						\
	: cl);								\
									\
	return old;							\
}

__CMPXCHG_CASE(w, b,     ,  8,   )
__CMPXCHG_CASE(w, h,     , 16,   )
__CMPXCHG_CASE(w,  ,     , 32,   )
__CMPXCHG_CASE(x,  ,     , 64,   )
__CMPXCHG_CASE(w, b, acq_,  8,  a, "memory")
__CMPXCHG_CASE(w, h, acq_, 16,  a, "memory")
__CMPXCHG_CASE(w,  , acq_, 32,  a, "memory")
__CMPXCHG_CASE(x,  , acq_, 64,  a, "memory")
__CMPXCHG_CASE(w, b, rel_,  8,  l, "memory")
__CMPXCHG_CASE(w, h, rel_, 16,  l, "memory")
__CMPXCHG_CASE(w,  , rel_, 32,  l, "memory")
__CMPXCHG_CASE(x,  , rel_, 64,  l, "memory")
__CMPXCHG_CASE(w, b,  mb_,  8, al, "memory")
__CMPXCHG_CASE(w, h,  mb_, 16, al, "memory")
__CMPXCHG_CASE(w,  ,  mb_, 32, al, "memory")
__CMPXCHG_CASE(x,  ,  mb_, 64, al, "memory")

#undef __CMPXCHG_CASE

#define __CMPXCHG128(name, mb, cl...)					\
static __always_inline u128						\
__lse__cmpxchg128##name(volatile u128 *ptr, u128 old, u128 new)		\
{									\
	union __u128_halves r, o = { .full = (old) },			\
			       n = { .full = (new) };			\
	register unsigned long x0 asm ("x0") = o.low;			\
	register unsigned long x1 asm ("x1") = o.high;			\
	register unsigned long x2 asm ("x2") = n.low;			\
	register unsigned long x3 asm ("x3") = n.high;			\
	register unsigned long x4 asm ("x4") = (unsigned long)ptr;	\
									\
	asm volatile(							\
	__LSE_PREAMBLE							\
	"	casp" #mb "\t%[old1], %[old2], %[new1], %[new2], %[v]\n"\
	: [old1] "+&r" (x0), [old2] "+&r" (x1),				\
	  [v] "+Q" (*(u128 *)ptr)					\
	: [new1] "r" (x2), [new2] "r" (x3), [ptr] "r" (x4),		\
	  [oldval1] "r" (o.low), [oldval2] "r" (o.high)			\
	: cl);								\
									\
	r.low = x0; r.high = x1;					\
									\
	return r.full;							\
}

__CMPXCHG128(   ,   )
__CMPXCHG128(_mb, al, "memory")

#undef __CMPXCHG128

#endif	/* __ASM_ATOMIC_LSE_H */
