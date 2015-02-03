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

#ifndef __ASM_ATOMIC_LSE_H
#define __ASM_ATOMIC_LSE_H

#ifndef __ARM64_IN_ATOMIC_IMPL
#error "please don't include this file directly"
#endif

/* Move the ll/sc atomics out-of-line */
#define __LL_SC_INLINE
#define __LL_SC_PREFIX(x)	__ll_sc_##x
#define __LL_SC_EXPORT(x)	EXPORT_SYMBOL(__LL_SC_PREFIX(x))

/* Macros for constructing calls to out-of-line ll/sc atomics */
#define __LL_SC_CALL(op)						\
	"bl\t" __stringify(__LL_SC_PREFIX(atomic_##op)) "\n"
#define __LL_SC_CALL64(op)						\
	"bl\t" __stringify(__LL_SC_PREFIX(atomic64_##op)) "\n"

#define ATOMIC_OP(op, asm_op)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	register int w0 asm ("w0") = i;					\
	register atomic_t *x1 asm ("x1") = v;				\
									\
	asm volatile(							\
	__LL_SC_CALL(op)						\
	: "+r" (w0), "+Q" (v->counter)					\
	: "r" (x1)							\
	: "x30");							\
}									\

#define ATOMIC_OP_RETURN(op, asm_op)					\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	register int w0 asm ("w0") = i;					\
	register atomic_t *x1 asm ("x1") = v;				\
									\
	asm volatile(							\
	__LL_SC_CALL(op##_return)					\
	: "+r" (w0)							\
	: "r" (x1)							\
	: "x30", "memory");						\
									\
	return w0;							\
}

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

static inline int atomic_cmpxchg(atomic_t *ptr, int old, int new)
{
	register unsigned long x0 asm ("x0") = (unsigned long)ptr;
	register int w1 asm ("w1") = old;
	register int w2 asm ("w2") = new;

	asm volatile(
	__LL_SC_CALL(cmpxchg)
	: "+r" (x0)
	: "r" (w1), "r" (w2)
	: "x30", "cc", "memory");

	return x0;
}

#define ATOMIC64_OP(op, asm_op)						\
static inline void atomic64_##op(long i, atomic64_t *v)			\
{									\
	register long x0 asm ("x0") = i;				\
	register atomic64_t *x1 asm ("x1") = v;				\
									\
	asm volatile(							\
	__LL_SC_CALL64(op)						\
	: "+r" (x0), "+Q" (v->counter)					\
	: "r" (x1)							\
	: "x30");							\
}									\

#define ATOMIC64_OP_RETURN(op, asm_op)					\
static inline long atomic64_##op##_return(long i, atomic64_t *v)	\
{									\
	register long x0 asm ("x0") = i;				\
	register atomic64_t *x1 asm ("x1") = v;				\
									\
	asm volatile(							\
	__LL_SC_CALL64(op##_return)					\
	: "+r" (x0)							\
	: "r" (x1)							\
	: "x30", "memory");						\
									\
	return x0;							\
}

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

static inline long atomic64_cmpxchg(atomic64_t *ptr, long old, long new)
{
	register unsigned long x0 asm ("x0") = (unsigned long)ptr;
	register long x1 asm ("x1") = old;
	register long x2 asm ("x2") = new;

	asm volatile(
	__LL_SC_CALL64(cmpxchg)
	: "+r" (x0)
	: "r" (x1), "r" (x2)
	: "x30", "cc", "memory");

	return x0;
}

static inline long atomic64_dec_if_positive(atomic64_t *v)
{
	register unsigned long x0 asm ("x0") = (unsigned long)v;

	asm volatile(
	__LL_SC_CALL64(dec_if_positive)
	: "+r" (x0)
	:
	: "x30", "cc", "memory");

	return x0;
}

#endif	/* __ASM_ATOMIC_LSE_H */
