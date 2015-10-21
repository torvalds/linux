/*
 * Based on arch/arm/include/asm/atomic.h
 *
 * Copyright (C) 1996 Russell King.
 * Copyright (C) 2002 Deep Blue Solutions Ltd.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ASM_ATOMIC_LL_SC_H
#define __ASM_ATOMIC_LL_SC_H

#ifndef __ARM64_IN_ATOMIC_IMPL
#error "please don't include this file directly"
#endif

/*
 * AArch64 UP and SMP safe atomic ops.  We use load exclusive and
 * store exclusive to ensure that these are atomic.  We may loop
 * to ensure that the update happens.
 *
 * NOTE: these functions do *not* follow the PCS and must explicitly
 * save any clobbered registers other than x0 (regardless of return
 * value).  This is achieved through -fcall-saved-* compiler flags for
 * this file, which unfortunately don't work on a per-function basis
 * (the optimize attribute silently ignores these options).
 */

#define ATOMIC_OP(op, asm_op)						\
__LL_SC_INLINE void							\
__LL_SC_PREFIX(atomic_##op(int i, atomic_t *v))				\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// atomic_" #op "\n"				\
"	prfm	pstl1strm, %2\n"					\
"1:	ldxr	%w0, %2\n"						\
"	" #asm_op "	%w0, %w0, %w3\n"				\
"	stxr	%w1, %w0, %2\n"						\
"	cbnz	%w1, 1b"						\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i));							\
}									\
__LL_SC_EXPORT(atomic_##op);

#define ATOMIC_OP_RETURN(op, asm_op)					\
__LL_SC_INLINE int							\
__LL_SC_PREFIX(atomic_##op##_return(int i, atomic_t *v))		\
{									\
	unsigned long tmp;						\
	int result;							\
									\
	asm volatile("// atomic_" #op "_return\n"			\
"	prfm	pstl1strm, %2\n"					\
"1:	ldxr	%w0, %2\n"						\
"	" #asm_op "	%w0, %w0, %w3\n"				\
"	stlxr	%w1, %w0, %2\n"						\
"	cbnz	%w1, 1b"						\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i)							\
	: "memory");							\
									\
	smp_mb();							\
	return result;							\
}									\
__LL_SC_EXPORT(atomic_##op##_return);

#define ATOMIC_OPS(op, asm_op)						\
	ATOMIC_OP(op, asm_op)						\
	ATOMIC_OP_RETURN(op, asm_op)

ATOMIC_OPS(add, add)
ATOMIC_OPS(sub, sub)

ATOMIC_OP(and, and)
ATOMIC_OP(andnot, bic)
ATOMIC_OP(or, orr)
ATOMIC_OP(xor, eor)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define ATOMIC64_OP(op, asm_op)						\
__LL_SC_INLINE void							\
__LL_SC_PREFIX(atomic64_##op(long i, atomic64_t *v))			\
{									\
	long result;							\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_" #op "\n"				\
"	prfm	pstl1strm, %2\n"					\
"1:	ldxr	%0, %2\n"						\
"	" #asm_op "	%0, %0, %3\n"					\
"	stxr	%w1, %0, %2\n"						\
"	cbnz	%w1, 1b"						\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i));							\
}									\
__LL_SC_EXPORT(atomic64_##op);

#define ATOMIC64_OP_RETURN(op, asm_op)					\
__LL_SC_INLINE long							\
__LL_SC_PREFIX(atomic64_##op##_return(long i, atomic64_t *v))		\
{									\
	long result;							\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_" #op "_return\n"			\
"	prfm	pstl1strm, %2\n"					\
"1:	ldxr	%0, %2\n"						\
"	" #asm_op "	%0, %0, %3\n"					\
"	stlxr	%w1, %0, %2\n"						\
"	cbnz	%w1, 1b"						\
	: "=&r" (result), "=&r" (tmp), "+Q" (v->counter)		\
	: "Ir" (i)							\
	: "memory");							\
									\
	smp_mb();							\
	return result;							\
}									\
__LL_SC_EXPORT(atomic64_##op##_return);

#define ATOMIC64_OPS(op, asm_op)					\
	ATOMIC64_OP(op, asm_op)						\
	ATOMIC64_OP_RETURN(op, asm_op)

ATOMIC64_OPS(add, add)
ATOMIC64_OPS(sub, sub)

ATOMIC64_OP(and, and)
ATOMIC64_OP(andnot, bic)
ATOMIC64_OP(or, orr)
ATOMIC64_OP(xor, eor)

#undef ATOMIC64_OPS
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

__LL_SC_INLINE long
__LL_SC_PREFIX(atomic64_dec_if_positive(atomic64_t *v))
{
	long result;
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
__LL_SC_EXPORT(atomic64_dec_if_positive);

#define __CMPXCHG_CASE(w, sz, name, mb, rel, cl)			\
__LL_SC_INLINE unsigned long						\
__LL_SC_PREFIX(__cmpxchg_case_##name(volatile void *ptr,		\
				     unsigned long old,			\
				     unsigned long new))		\
{									\
	unsigned long tmp, oldval;					\
									\
	asm volatile(							\
	"	prfm	pstl1strm, %[v]\n"				\
	"1:	ldxr" #sz "\t%" #w "[oldval], %[v]\n"			\
	"	eor	%" #w "[tmp], %" #w "[oldval], %" #w "[old]\n"	\
	"	cbnz	%" #w "[tmp], 2f\n"				\
	"	st" #rel "xr" #sz "\t%w[tmp], %" #w "[new], %[v]\n"	\
	"	cbnz	%w[tmp], 1b\n"					\
	"	" #mb "\n"						\
	"	mov	%" #w "[oldval], %" #w "[old]\n"		\
	"2:"								\
	: [tmp] "=&r" (tmp), [oldval] "=&r" (oldval),			\
	  [v] "+Q" (*(unsigned long *)ptr)				\
	: [old] "Lr" (old), [new] "r" (new)				\
	: cl);								\
									\
	return oldval;							\
}									\
__LL_SC_EXPORT(__cmpxchg_case_##name);

__CMPXCHG_CASE(w, b,    1,        ,  ,         )
__CMPXCHG_CASE(w, h,    2,        ,  ,         )
__CMPXCHG_CASE(w,  ,    4,        ,  ,         )
__CMPXCHG_CASE( ,  ,    8,        ,  ,         )
__CMPXCHG_CASE(w, b, mb_1, dmb ish, l, "memory")
__CMPXCHG_CASE(w, h, mb_2, dmb ish, l, "memory")
__CMPXCHG_CASE(w,  , mb_4, dmb ish, l, "memory")
__CMPXCHG_CASE( ,  , mb_8, dmb ish, l, "memory")

#undef __CMPXCHG_CASE

#define __CMPXCHG_DBL(name, mb, rel, cl)				\
__LL_SC_INLINE int							\
__LL_SC_PREFIX(__cmpxchg_double##name(unsigned long old1,		\
				      unsigned long old2,		\
				      unsigned long new1,		\
				      unsigned long new2,		\
				      volatile void *ptr))		\
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
	: "=&r" (tmp), "=&r" (ret), "+Q" (*(unsigned long *)ptr)	\
	: "r" (old1), "r" (old2), "r" (new1), "r" (new2)		\
	: cl);								\
									\
	return ret;							\
}									\
__LL_SC_EXPORT(__cmpxchg_double##name);

__CMPXCHG_DBL(   ,        ,  ,         )
__CMPXCHG_DBL(_mb, dmb ish, l, "memory")

#undef __CMPXCHG_DBL

#endif	/* __ASM_ATOMIC_LL_SC_H */
