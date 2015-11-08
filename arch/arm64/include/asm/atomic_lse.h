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

#define __LL_SC_ATOMIC(op)	__LL_SC_CALL(atomic_##op)

static inline void atomic_andnot(int i, atomic_t *v)
{
	register int w0 asm ("w0") = i;
	register atomic_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC(andnot),
	"	stclr	%w[i], %[v]\n")
	: [i] "+r" (w0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic_or(int i, atomic_t *v)
{
	register int w0 asm ("w0") = i;
	register atomic_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC(or),
	"	stset	%w[i], %[v]\n")
	: [i] "+r" (w0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic_xor(int i, atomic_t *v)
{
	register int w0 asm ("w0") = i;
	register atomic_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC(xor),
	"	steor	%w[i], %[v]\n")
	: [i] "+r" (w0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic_add(int i, atomic_t *v)
{
	register int w0 asm ("w0") = i;
	register atomic_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC(add),
	"	stadd	%w[i], %[v]\n")
	: [i] "+r" (w0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

#define ATOMIC_OP_ADD_RETURN(name, mb, cl...)				\
static inline int atomic_add_return##name(int i, atomic_t *v)		\
{									\
	register int w0 asm ("w0") = i;					\
	register atomic_t *x1 asm ("x1") = v;				\
									\
	asm volatile(ARM64_LSE_ATOMIC_INSN(				\
	/* LL/SC */							\
	"	nop\n"							\
	__LL_SC_ATOMIC(add_return##name),				\
	/* LSE atomics */						\
	"	ldadd" #mb "	%w[i], w30, %[v]\n"			\
	"	add	%w[i], %w[i], w30")				\
	: [i] "+r" (w0), [v] "+Q" (v->counter)				\
	: "r" (x1)							\
	: "x30" , ##cl);						\
									\
	return w0;							\
}

ATOMIC_OP_ADD_RETURN(_relaxed,   )
ATOMIC_OP_ADD_RETURN(_acquire,  a, "memory")
ATOMIC_OP_ADD_RETURN(_release,  l, "memory")
ATOMIC_OP_ADD_RETURN(        , al, "memory")

#undef ATOMIC_OP_ADD_RETURN

static inline void atomic_and(int i, atomic_t *v)
{
	register int w0 asm ("w0") = i;
	register atomic_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	nop\n"
	__LL_SC_ATOMIC(and),
	/* LSE atomics */
	"	mvn	%w[i], %w[i]\n"
	"	stclr	%w[i], %[v]")
	: [i] "+r" (w0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic_sub(int i, atomic_t *v)
{
	register int w0 asm ("w0") = i;
	register atomic_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	nop\n"
	__LL_SC_ATOMIC(sub),
	/* LSE atomics */
	"	neg	%w[i], %w[i]\n"
	"	stadd	%w[i], %[v]")
	: [i] "+r" (w0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

#define ATOMIC_OP_SUB_RETURN(name, mb, cl...)				\
static inline int atomic_sub_return##name(int i, atomic_t *v)		\
{									\
	register int w0 asm ("w0") = i;					\
	register atomic_t *x1 asm ("x1") = v;				\
									\
	asm volatile(ARM64_LSE_ATOMIC_INSN(				\
	/* LL/SC */							\
	"	nop\n"							\
	__LL_SC_ATOMIC(sub_return##name)				\
	"	nop",							\
	/* LSE atomics */						\
	"	neg	%w[i], %w[i]\n"					\
	"	ldadd" #mb "	%w[i], w30, %[v]\n"			\
	"	add	%w[i], %w[i], w30")				\
	: [i] "+r" (w0), [v] "+Q" (v->counter)				\
	: "r" (x1)							\
	: "x30" , ##cl);						\
									\
	return w0;							\
}

ATOMIC_OP_SUB_RETURN(_relaxed,   )
ATOMIC_OP_SUB_RETURN(_acquire,  a, "memory")
ATOMIC_OP_SUB_RETURN(_release,  l, "memory")
ATOMIC_OP_SUB_RETURN(        , al, "memory")

#undef ATOMIC_OP_SUB_RETURN
#undef __LL_SC_ATOMIC

#define __LL_SC_ATOMIC64(op)	__LL_SC_CALL(atomic64_##op)

static inline void atomic64_andnot(long i, atomic64_t *v)
{
	register long x0 asm ("x0") = i;
	register atomic64_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC64(andnot),
	"	stclr	%[i], %[v]\n")
	: [i] "+r" (x0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic64_or(long i, atomic64_t *v)
{
	register long x0 asm ("x0") = i;
	register atomic64_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC64(or),
	"	stset	%[i], %[v]\n")
	: [i] "+r" (x0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic64_xor(long i, atomic64_t *v)
{
	register long x0 asm ("x0") = i;
	register atomic64_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC64(xor),
	"	steor	%[i], %[v]\n")
	: [i] "+r" (x0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic64_add(long i, atomic64_t *v)
{
	register long x0 asm ("x0") = i;
	register atomic64_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(__LL_SC_ATOMIC64(add),
	"	stadd	%[i], %[v]\n")
	: [i] "+r" (x0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

#define ATOMIC64_OP_ADD_RETURN(name, mb, cl...)				\
static inline long atomic64_add_return##name(long i, atomic64_t *v)	\
{									\
	register long x0 asm ("x0") = i;				\
	register atomic64_t *x1 asm ("x1") = v;				\
									\
	asm volatile(ARM64_LSE_ATOMIC_INSN(				\
	/* LL/SC */							\
	"	nop\n"							\
	__LL_SC_ATOMIC64(add_return##name),				\
	/* LSE atomics */						\
	"	ldadd" #mb "	%[i], x30, %[v]\n"			\
	"	add	%[i], %[i], x30")				\
	: [i] "+r" (x0), [v] "+Q" (v->counter)				\
	: "r" (x1)							\
	: "x30" , ##cl);						\
									\
	return x0;							\
}

ATOMIC64_OP_ADD_RETURN(_relaxed,   )
ATOMIC64_OP_ADD_RETURN(_acquire,  a, "memory")
ATOMIC64_OP_ADD_RETURN(_release,  l, "memory")
ATOMIC64_OP_ADD_RETURN(        , al, "memory")

#undef ATOMIC64_OP_ADD_RETURN

static inline void atomic64_and(long i, atomic64_t *v)
{
	register long x0 asm ("x0") = i;
	register atomic64_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	nop\n"
	__LL_SC_ATOMIC64(and),
	/* LSE atomics */
	"	mvn	%[i], %[i]\n"
	"	stclr	%[i], %[v]")
	: [i] "+r" (x0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

static inline void atomic64_sub(long i, atomic64_t *v)
{
	register long x0 asm ("x0") = i;
	register atomic64_t *x1 asm ("x1") = v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	nop\n"
	__LL_SC_ATOMIC64(sub),
	/* LSE atomics */
	"	neg	%[i], %[i]\n"
	"	stadd	%[i], %[v]")
	: [i] "+r" (x0), [v] "+Q" (v->counter)
	: "r" (x1)
	: "x30");
}

#define ATOMIC64_OP_SUB_RETURN(name, mb, cl...)				\
static inline long atomic64_sub_return##name(long i, atomic64_t *v)	\
{									\
	register long x0 asm ("x0") = i;				\
	register atomic64_t *x1 asm ("x1") = v;				\
									\
	asm volatile(ARM64_LSE_ATOMIC_INSN(				\
	/* LL/SC */							\
	"	nop\n"							\
	__LL_SC_ATOMIC64(sub_return##name)				\
	"	nop",							\
	/* LSE atomics */						\
	"	neg	%[i], %[i]\n"					\
	"	ldadd" #mb "	%[i], x30, %[v]\n"			\
	"	add	%[i], %[i], x30")				\
	: [i] "+r" (x0), [v] "+Q" (v->counter)				\
	: "r" (x1)							\
	: "x30" , ##cl);						\
									\
	return x0;							\
}

ATOMIC64_OP_SUB_RETURN(_relaxed,   )
ATOMIC64_OP_SUB_RETURN(_acquire,  a, "memory")
ATOMIC64_OP_SUB_RETURN(_release,  l, "memory")
ATOMIC64_OP_SUB_RETURN(        , al, "memory")

#undef ATOMIC64_OP_SUB_RETURN

static inline long atomic64_dec_if_positive(atomic64_t *v)
{
	register long x0 asm ("x0") = (long)v;

	asm volatile(ARM64_LSE_ATOMIC_INSN(
	/* LL/SC */
	"	nop\n"
	__LL_SC_ATOMIC64(dec_if_positive)
	"	nop\n"
	"	nop\n"
	"	nop\n"
	"	nop\n"
	"	nop",
	/* LSE atomics */
	"1:	ldr	x30, %[v]\n"
	"	subs	%[ret], x30, #1\n"
	"	b.lt	2f\n"
	"	casal	x30, %[ret], %[v]\n"
	"	sub	x30, x30, #1\n"
	"	sub	x30, x30, %[ret]\n"
	"	cbnz	x30, 1b\n"
	"2:")
	: [ret] "+&r" (x0), [v] "+Q" (v->counter)
	:
	: "x30", "cc", "memory");

	return x0;
}

#undef __LL_SC_ATOMIC64

#define __LL_SC_CMPXCHG(op)	__LL_SC_CALL(__cmpxchg_case_##op)

#define __CMPXCHG_CASE(w, sz, name, mb, cl...)				\
static inline unsigned long __cmpxchg_case_##name(volatile void *ptr,	\
						  unsigned long old,	\
						  unsigned long new)	\
{									\
	register unsigned long x0 asm ("x0") = (unsigned long)ptr;	\
	register unsigned long x1 asm ("x1") = old;			\
	register unsigned long x2 asm ("x2") = new;			\
									\
	asm volatile(ARM64_LSE_ATOMIC_INSN(				\
	/* LL/SC */							\
	"	nop\n"							\
		__LL_SC_CMPXCHG(name)					\
	"	nop",							\
	/* LSE atomics */						\
	"	mov	" #w "30, %" #w "[old]\n"			\
	"	cas" #mb #sz "\t" #w "30, %" #w "[new], %[v]\n"		\
	"	mov	%" #w "[ret], " #w "30")			\
	: [ret] "+r" (x0), [v] "+Q" (*(unsigned long *)ptr)		\
	: [old] "r" (x1), [new] "r" (x2)				\
	: "x30" , ##cl);						\
									\
	return x0;							\
}

__CMPXCHG_CASE(w, b,     1,   )
__CMPXCHG_CASE(w, h,     2,   )
__CMPXCHG_CASE(w,  ,     4,   )
__CMPXCHG_CASE(x,  ,     8,   )
__CMPXCHG_CASE(w, b, acq_1,  a, "memory")
__CMPXCHG_CASE(w, h, acq_2,  a, "memory")
__CMPXCHG_CASE(w,  , acq_4,  a, "memory")
__CMPXCHG_CASE(x,  , acq_8,  a, "memory")
__CMPXCHG_CASE(w, b, rel_1,  l, "memory")
__CMPXCHG_CASE(w, h, rel_2,  l, "memory")
__CMPXCHG_CASE(w,  , rel_4,  l, "memory")
__CMPXCHG_CASE(x,  , rel_8,  l, "memory")
__CMPXCHG_CASE(w, b,  mb_1, al, "memory")
__CMPXCHG_CASE(w, h,  mb_2, al, "memory")
__CMPXCHG_CASE(w,  ,  mb_4, al, "memory")
__CMPXCHG_CASE(x,  ,  mb_8, al, "memory")

#undef __LL_SC_CMPXCHG
#undef __CMPXCHG_CASE

#define __LL_SC_CMPXCHG_DBL(op)	__LL_SC_CALL(__cmpxchg_double##op)

#define __CMPXCHG_DBL(name, mb, cl...)					\
static inline int __cmpxchg_double##name(unsigned long old1,		\
					 unsigned long old2,		\
					 unsigned long new1,		\
					 unsigned long new2,		\
					 volatile void *ptr)		\
{									\
	unsigned long oldval1 = old1;					\
	unsigned long oldval2 = old2;					\
	register unsigned long x0 asm ("x0") = old1;			\
	register unsigned long x1 asm ("x1") = old2;			\
	register unsigned long x2 asm ("x2") = new1;			\
	register unsigned long x3 asm ("x3") = new2;			\
	register unsigned long x4 asm ("x4") = (unsigned long)ptr;	\
									\
	asm volatile(ARM64_LSE_ATOMIC_INSN(				\
	/* LL/SC */							\
	"	nop\n"							\
	"	nop\n"							\
	"	nop\n"							\
	__LL_SC_CMPXCHG_DBL(name),					\
	/* LSE atomics */						\
	"	casp" #mb "\t%[old1], %[old2], %[new1], %[new2], %[v]\n"\
	"	eor	%[old1], %[old1], %[oldval1]\n"			\
	"	eor	%[old2], %[old2], %[oldval2]\n"			\
	"	orr	%[old1], %[old1], %[old2]")			\
	: [old1] "+r" (x0), [old2] "+r" (x1),				\
	  [v] "+Q" (*(unsigned long *)ptr)				\
	: [new1] "r" (x2), [new2] "r" (x3), [ptr] "r" (x4),		\
	  [oldval1] "r" (oldval1), [oldval2] "r" (oldval2)		\
	: "x30" , ##cl);						\
									\
	return x0;							\
}

__CMPXCHG_DBL(   ,   )
__CMPXCHG_DBL(_mb, al, "memory")

#undef __LL_SC_CMPXCHG_DBL
#undef __CMPXCHG_DBL

#endif	/* __ASM_ATOMIC_LSE_H */
