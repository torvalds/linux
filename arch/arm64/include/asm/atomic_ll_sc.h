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

__LL_SC_INLINE int
__LL_SC_PREFIX(atomic_cmpxchg(atomic_t *ptr, int old, int new))
{
	unsigned long tmp;
	int oldval;

	smp_mb();

	asm volatile("// atomic_cmpxchg\n"
"1:	ldxr	%w1, %2\n"
"	cmp	%w1, %w3\n"
"	b.ne	2f\n"
"	stxr	%w0, %w4, %2\n"
"	cbnz	%w0, 1b\n"
"2:"
	: "=&r" (tmp), "=&r" (oldval), "+Q" (ptr->counter)
	: "Ir" (old), "r" (new)
	: "cc");

	smp_mb();
	return oldval;
}
__LL_SC_EXPORT(atomic_cmpxchg);

#define ATOMIC64_OP(op, asm_op)						\
__LL_SC_INLINE void							\
__LL_SC_PREFIX(atomic64_##op(long i, atomic64_t *v))			\
{									\
	long result;							\
	unsigned long tmp;						\
									\
	asm volatile("// atomic64_" #op "\n"				\
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
__LL_SC_PREFIX(atomic64_cmpxchg(atomic64_t *ptr, long old, long new))
{
	long oldval;
	unsigned long res;

	smp_mb();

	asm volatile("// atomic64_cmpxchg\n"
"1:	ldxr	%1, %2\n"
"	cmp	%1, %3\n"
"	b.ne	2f\n"
"	stxr	%w0, %4, %2\n"
"	cbnz	%w0, 1b\n"
"2:"
	: "=&r" (res), "=&r" (oldval), "+Q" (ptr->counter)
	: "Ir" (old), "r" (new)
	: "cc");

	smp_mb();
	return oldval;
}
__LL_SC_EXPORT(atomic64_cmpxchg);

__LL_SC_INLINE long
__LL_SC_PREFIX(atomic64_dec_if_positive(atomic64_t *v))
{
	long result;
	unsigned long tmp;

	asm volatile("// atomic64_dec_if_positive\n"
"1:	ldxr	%0, %2\n"
"	subs	%0, %0, #1\n"
"	b.mi	2f\n"
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

#define __CMPXCHG_CASE(w, sz, name, mb, cl)				\
__LL_SC_INLINE unsigned long						\
__LL_SC_PREFIX(__cmpxchg_case_##name(volatile void *ptr,		\
				     unsigned long old,			\
				     unsigned long new))		\
{									\
	unsigned long tmp, oldval;					\
									\
	asm volatile(							\
	"	" #mb "\n"						\
	"1:	ldxr" #sz "\t%" #w "[oldval], %[v]\n"			\
	"	eor	%" #w "[tmp], %" #w "[oldval], %" #w "[old]\n"	\
	"	cbnz	%" #w "[tmp], 2f\n"				\
	"	stxr" #sz "\t%w[tmp], %" #w "[new], %[v]\n"		\
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

__CMPXCHG_CASE(w, b,    1,        ,         )
__CMPXCHG_CASE(w, h,    2,        ,         )
__CMPXCHG_CASE(w,  ,    4,        ,         )
__CMPXCHG_CASE( ,  ,    8,        ,         )
__CMPXCHG_CASE(w, b, mb_1, dmb ish, "memory")
__CMPXCHG_CASE(w, h, mb_2, dmb ish, "memory")
__CMPXCHG_CASE(w,  , mb_4, dmb ish, "memory")
__CMPXCHG_CASE( ,  , mb_8, dmb ish, "memory")

#undef __CMPXCHG_CASE

#endif	/* __ASM_ATOMIC_LL_SC_H */
