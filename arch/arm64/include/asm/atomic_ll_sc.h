/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/atomic.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2002 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 */

#ifndef __ASM_ATOMIC_LL_SC_H
#define __ASM_ATOMIC_LL_SC_H

#include <linux/stringify.h>

#ifndef CONFIG_CC_HAS_K_CONSTRAINT
#define K
#endif

/*
 * AArch64 UP and SMP safe atomic ops.  We use load exclusive and
 * store exclusive to ensure that these are atomic.  We may loop
 * to ensure that the update happens.
 */

#define ATOMIC_OP(op, asm_op, constraint)				\
static inline void							\
__ll_sc_atomic_##op(int i, atomic_t *v)					\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// atomic_" #op "\n"				\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ldxr	%w0, %2\n"					\
	"	" #asm_op "	%w0, %w0, %w3\n"			\
	"	stxr	%w1, %w0, %2\n"					\
	"	cbnz	%w1, 1b\n"					\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i));				\
}

#define ATOMIC_OP_RETURN(name, mb, acq, rel, cl, op, asm_op, constraint)\
static inline int							\
__ll_sc_atomic_##op##_return##name(int i, atomic_t *v)			\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// atomic_" #op "_return" #name "\n"		\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ld" #acq "xr	%w0, %2\n"				\
	"	" #asm_op "	%w0, %w0, %w3\n"			\
	"	st" #rel "xr	%w1, %w0, %2\n"				\
	"	cbnz	%w1, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(name, mb, acq, rel, cl, op, asm_op, constraint) \
static inline int							\
__ll_sc_atomic_fetch_##op##name(int i, atomic_t *v)			\
{									\
	unsigned long tmp;						\
	int val, result;						\
									\
	asm volatile("// atomic_fetch_" #op #name "\n"			\
	"	prfm	pstl1strm, %3\n"				\
	"1:	ld" #acq "xr	%w0, %3\n"				\
	"	" #asm_op "	%w1, %w0, %w4\n"			\
	"	st" #rel "xr	%w2, %w1, %3\n"				\
	"	cbnz	%w2, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (val), "=&r" (tmp), "+Q" (v->counter)	\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define ATOMIC_OPS(...)							\
	ATOMIC_OP(__VA_ARGS__)						\
	ATOMIC_OP_RETURN(        , dmb ish,  , l, "memory", __VA_ARGS__)\
	ATOMIC_OP_RETURN(_relaxed,        ,  ,  ,         , __VA_ARGS__)\
	ATOMIC_OP_RETURN(_acquire,        , a,  , "memory", __VA_ARGS__)\
	ATOMIC_OP_RETURN(_release,        ,  , l, "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (        , dmb ish,  , l, "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_relaxed,        ,  ,  ,         , __VA_ARGS__)\
	ATOMIC_FETCH_OP (_acquire,        , a,  , "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_release,        ,  , l, "memory", __VA_ARGS__)

ATOMIC_OPS(add, add, I)
ATOMIC_OPS(sub, sub, J)

#undef ATOMIC_OPS
#define ATOMIC_OPS(...)							\
	ATOMIC_OP(__VA_ARGS__)						\
	ATOMIC_FETCH_OP (        , dmb ish,  , l, "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_relaxed,        ,  ,  ,         , __VA_ARGS__)\
	ATOMIC_FETCH_OP (_acquire,        , a,  , "memory", __VA_ARGS__)\
	ATOMIC_FETCH_OP (_release,        ,  , l, "memory", __VA_ARGS__)

ATOMIC_OPS(and, and, K)
ATOMIC_OPS(or, orr, K)
ATOMIC_OPS(xor, eor, K)
/*
 * GAS converts the mysterious and undocumented BIC (immediate) alias to
 * an AND (immediate) instruction with the immediate inverted. We don't
 * have a constraint for this, so fall back to register.
 */
ATOMIC_OPS(andnot, bic, )

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define ATOMIC64_OP(op, asm_op, constraint)				\
static inline void							\
__ll_sc_atomic64_##op(s64 i, atomic64_t *v)				\
{									\
	s64 result;							\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_" #op "\n"				\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ldxr	%0, %2\n"					\
	"	" #asm_op "	%0, %0, %3\n"				\
	"	stxr	%w1, %0, %2\n"					\
	"	cbnz	%w1, 1b"					\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i));				\
}

#define ATOMIC64_OP_RETURN(name, mb, acq, rel, cl, op, asm_op, constraint)\
static inline long							\
__ll_sc_atomic64_##op##_return##name(s64 i, atomic64_t *v)		\
{									\
	s64 result;							\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_" #op "_return" #name "\n"		\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ld" #acq "xr	%0, %2\n"				\
	"	" #asm_op "	%0, %0, %3\n"				\
	"	st" #rel "xr	%w1, %0, %2\n"				\
	"	cbnz	%w1, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define ATOMIC64_FETCH_OP(name, mb, acq, rel, cl, op, asm_op, constraint)\
static inline long							\
__ll_sc_atomic64_fetch_##op##name(s64 i, atomic64_t *v)			\
{									\
	s64 result, val;						\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_fetch_" #op #name "\n"		\
	"	prfm	pstl1strm, %3\n"				\
	"1:	ld" #acq "xr	%0, %3\n"				\
	"	" #asm_op "	%1, %0, %4\n"				\
	"	st" #rel "xr	%w2, %1, %3\n"				\
	"	cbnz	%w2, 1b\n"					\
	"	" #mb							\
	: "=&r" (result), "=&r" (val), "=&r" (tmp), "+Q" (v->counter)	\
	: __stringify(constraint) "r" (i)				\
	: cl);								\
									\
	return result;							\
}

#define ATOMIC64_OPS(...)						\
	ATOMIC64_OP(__VA_ARGS__)					\
	ATOMIC64_OP_RETURN(, dmb ish,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_OP_RETURN(_relaxed,,  ,  ,         , __VA_ARGS__)	\
	ATOMIC64_OP_RETURN(_acquire,, a,  , "memory", __VA_ARGS__)	\
	ATOMIC64_OP_RETURN(_release,,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (, dmb ish,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_relaxed,,  ,  ,         , __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_acquire,, a,  , "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_release,,  , l, "memory", __VA_ARGS__)

ATOMIC64_OPS(add, add, I)
ATOMIC64_OPS(sub, sub, J)

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(...)						\
	ATOMIC64_OP(__VA_ARGS__)					\
	ATOMIC64_FETCH_OP (, dmb ish,  , l, "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_relaxed,,  ,  ,         , __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_acquire,, a,  , "memory", __VA_ARGS__)	\
	ATOMIC64_FETCH_OP (_release,,  , l, "memory", __VA_ARGS__)

ATOMIC64_OPS(and, and, L)
ATOMIC64_OPS(or, orr, L)
ATOMIC64_OPS(xor, eor, L)
/*
 * GAS converts the mysterious and undocumented BIC (immediate) alias to
 * an AND (immediate) instruction with the immediate inverted. We don't
 * have a constraint for this, so fall back to register.
 */
ATOMIC64_OPS(andnot, bic, )

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

static inline s64
__ll_sc_atomic64_dec_if_positive(atomic64_t *v)
{
	s64 result;
	unsigned long tmp;

	asm volatile("// atomic64_dec_if_positive\n"
	"	prfm	pstl1strm, %2\n"
	"1:	ldxr	%0, %2\n"
	"	subs	%0, %0, #1\n"
	"	b.lt	2f\n"
	"	stlxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	"	dmb	ish\n"
	"2:"
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)
	:
	: "cc", "memory");

	return result;
}

#define __CMPXCHG_CASE(w, sfx, name, sz, mb, acq, rel, cl, constraint)	\
static inline u##sz							\
__ll_sc__cmpxchg_case_##name##sz(volatile void *ptr,			\
					 unsigned long old,		\
					 u##sz new)			\
{									\
	unsigned long tmp;						\
	u##sz oldval;							\
									\
	/*								\
	 * Sub-word sizes require explicit casting so that the compare  \
	 * part of the cmpxchg doesn't end up interpreting non-zero	\
	 * upper bits of the register containing "old".			\
	 */								\
	if (sz < 32)							\
		old = (u##sz)old;					\
									\
	asm volatile(							\
	"	prfm	pstl1strm, %[v]\n"				\
	"1:	ld" #acq "xr" #sfx "\t%" #w "[oldval], %[v]\n"		\
	"	eor	%" #w "[tmp], %" #w "[oldval], %" #w "[old]\n"	\
	"	cbnz	%" #w "[tmp], 2f\n"				\
	"	st" #rel "xr" #sfx "\t%w[tmp], %" #w "[new], %[v]\n"	\
	"	cbnz	%w[tmp], 1b\n"					\
	"	" #mb "\n"						\
	"2:"								\
	: [tmp] "=&r" (tmp), [oldval] "=&r" (oldval),			\
	  [v] "+Q" (*(u##sz *)ptr)					\
	: [old] __stringify(constraint) "r" (old), [new] "r" (new)	\
	: cl);								\
									\
	return oldval;							\
}

/*
 * Earlier versions of GCC (no later than 8.1.0) appear to incorrectly
 * handle the 'K' constraint for the value 4294967295 - thus we use no
 * constraint for 32 bit operations.
 */
__CMPXCHG_CASE(w, b,     ,  8,        ,  ,  ,         , K)
__CMPXCHG_CASE(w, h,     , 16,        ,  ,  ,         , K)
__CMPXCHG_CASE(w,  ,     , 32,        ,  ,  ,         , K)
__CMPXCHG_CASE( ,  ,     , 64,        ,  ,  ,         , L)
__CMPXCHG_CASE(w, b, acq_,  8,        , a,  , "memory", K)
__CMPXCHG_CASE(w, h, acq_, 16,        , a,  , "memory", K)
__CMPXCHG_CASE(w,  , acq_, 32,        , a,  , "memory", K)
__CMPXCHG_CASE( ,  , acq_, 64,        , a,  , "memory", L)
__CMPXCHG_CASE(w, b, rel_,  8,        ,  , l, "memory", K)
__CMPXCHG_CASE(w, h, rel_, 16,        ,  , l, "memory", K)
__CMPXCHG_CASE(w,  , rel_, 32,        ,  , l, "memory", K)
__CMPXCHG_CASE( ,  , rel_, 64,        ,  , l, "memory", L)
__CMPXCHG_CASE(w, b,  mb_,  8, dmb ish,  , l, "memory", K)
__CMPXCHG_CASE(w, h,  mb_, 16, dmb ish,  , l, "memory", K)
__CMPXCHG_CASE(w,  ,  mb_, 32, dmb ish,  , l, "memory", K)
__CMPXCHG_CASE( ,  ,  mb_, 64, dmb ish,  , l, "memory", L)

#undef __CMPXCHG_CASE

#define __CMPXCHG_DBL(name, mb, rel, cl)				\
static inline long							\
__ll_sc__cmpxchg_double##name(unsigned long old1,			\
				      unsigned long old2,		\
				      unsigned long new1,		\
				      unsigned long new2,		\
				      volatile void *ptr)		\
{									\
	unsigned long tmp, ret;						\
									\
	asm volatile("// __cmpxchg_double" #name "\n"			\
	"	prfm	pstl1strm, %2\n"				\
	"1:	ldxp	%0, %1, %2\n"					\
	"	eor	%0, %0, %3\n"					\
	"	eor	%1, %1, %4\n"					\
	"	orr	%1, %0, %1\n"					\
	"	cbnz	%1, 2f\n"					\
	"	st" #rel "xp	%w0, %5, %6, %2\n"			\
	"	cbnz	%w0, 1b\n"					\
	"	" #mb "\n"						\
	"2:"								\
	: "=&r" (tmp), "=&r" (ret), "+Q" (*(__uint128_t *)ptr)		\
	: "r" (old1), "r" (old2), "r" (new1), "r" (new2)		\
	: cl);								\
									\
	return ret;							\
}

__CMPXCHG_DBL(   ,        ,  ,         )
__CMPXCHG_DBL(_mb, dmb ish, l, "memory")

#undef __CMPXCHG_DBL
#undef K

#endif	/* __ASM_ATOMIC_LL_SC_H */
