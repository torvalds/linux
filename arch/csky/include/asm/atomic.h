/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_ATOMIC_H
#define __ASM_CSKY_ATOMIC_H

#ifdef CONFIG_SMP
#include <asm-generic/atomic64.h>

#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#define __atomic_acquire_fence()	__bar_brarw()

#define __atomic_release_fence()	__bar_brwaw()

static __always_inline int arch_atomic_read(const atomic_t *v)
{
	return READ_ONCE(v->counter);
}
static __always_inline void arch_atomic_set(atomic_t *v, int i)
{
	WRITE_ONCE(v->counter, i);
}

#define ATOMIC_OP(op)							\
static __always_inline							\
void arch_atomic_##op(int i, atomic_t *v)				\
{									\
	unsigned long tmp;						\
	__asm__ __volatile__ (						\
	"1:	ldex.w		%0, (%2)	\n"			\
	"	" #op "		%0, %1		\n"			\
	"	stex.w		%0, (%2)	\n"			\
	"	bez		%0, 1b		\n"			\
	: "=&r" (tmp)							\
	: "r" (i), "r" (&v->counter)					\
	: "memory");							\
}

ATOMIC_OP(add)
ATOMIC_OP(sub)
ATOMIC_OP(and)
ATOMIC_OP( or)
ATOMIC_OP(xor)

#undef ATOMIC_OP

#define ATOMIC_FETCH_OP(op)						\
static __always_inline							\
int arch_atomic_fetch_##op##_relaxed(int i, atomic_t *v)		\
{									\
	register int ret, tmp;						\
	__asm__ __volatile__ (						\
	"1:	ldex.w		%0, (%3) \n"				\
	"	mov		%1, %0   \n"				\
	"	" #op "		%0, %2   \n"				\
	"	stex.w		%0, (%3) \n"				\
	"	bez		%0, 1b   \n"				\
		: "=&r" (tmp), "=&r" (ret)				\
		: "r" (i), "r"(&v->counter) 				\
		: "memory");						\
	return ret;							\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static __always_inline							\
int arch_atomic_##op##_return_relaxed(int i, atomic_t *v)		\
{									\
	return arch_atomic_fetch_##op##_relaxed(i, v) c_op i;		\
}

#define ATOMIC_OPS(op, c_op)						\
	ATOMIC_FETCH_OP(op)						\
	ATOMIC_OP_RETURN(op, c_op)

ATOMIC_OPS(add, +)
ATOMIC_OPS(sub, -)

#define arch_atomic_fetch_add_relaxed	arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_sub_relaxed	arch_atomic_fetch_sub_relaxed

#define arch_atomic_add_return_relaxed	arch_atomic_add_return_relaxed
#define arch_atomic_sub_return_relaxed	arch_atomic_sub_return_relaxed

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN

#define ATOMIC_OPS(op)							\
	ATOMIC_FETCH_OP(op)

ATOMIC_OPS(and)
ATOMIC_OPS( or)
ATOMIC_OPS(xor)

#define arch_atomic_fetch_and_relaxed	arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_or_relaxed	arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_xor_relaxed	arch_atomic_fetch_xor_relaxed

#undef ATOMIC_OPS

#undef ATOMIC_FETCH_OP

static __always_inline int
arch_atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int prev, tmp;

	__asm__ __volatile__ (
		RELEASE_FENCE
		"1:	ldex.w		%0, (%3)	\n"
		"	cmpne		%0, %4		\n"
		"	bf		2f		\n"
		"	mov		%1, %0		\n"
		"	add		%1, %2		\n"
		"	stex.w		%1, (%3)	\n"
		"	bez		%1, 1b		\n"
		FULL_FENCE
		"2:\n"
		: "=&r" (prev), "=&r" (tmp)
		: "r" (a), "r" (&v->counter), "r" (u)
		: "memory");

	return prev;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

static __always_inline bool
arch_atomic_inc_unless_negative(atomic_t *v)
{
	int rc, tmp;

	__asm__ __volatile__ (
		RELEASE_FENCE
		"1:	ldex.w		%0, (%2)	\n"
		"	movi		%1, 0		\n"
		"	blz		%0, 2f		\n"
		"	movi		%1, 1		\n"
		"	addi		%0, 1		\n"
		"	stex.w		%0, (%2)	\n"
		"	bez		%0, 1b		\n"
		FULL_FENCE
		"2:\n"
		: "=&r" (tmp), "=&r" (rc)
		: "r" (&v->counter)
		: "memory");

	return tmp ? true : false;

}
#define arch_atomic_inc_unless_negative arch_atomic_inc_unless_negative

static __always_inline bool
arch_atomic_dec_unless_positive(atomic_t *v)
{
	int rc, tmp;

	__asm__ __volatile__ (
		RELEASE_FENCE
		"1:	ldex.w		%0, (%2)	\n"
		"	movi		%1, 0		\n"
		"	bhz		%0, 2f		\n"
		"	movi		%1, 1		\n"
		"	subi		%0, 1		\n"
		"	stex.w		%0, (%2)	\n"
		"	bez		%0, 1b		\n"
		FULL_FENCE
		"2:\n"
		: "=&r" (tmp), "=&r" (rc)
		: "r" (&v->counter)
		: "memory");

	return tmp ? true : false;
}
#define arch_atomic_dec_unless_positive arch_atomic_dec_unless_positive

static __always_inline int
arch_atomic_dec_if_positive(atomic_t *v)
{
	int dec, tmp;

	__asm__ __volatile__ (
		RELEASE_FENCE
		"1:	ldex.w		%0, (%2)	\n"
		"	subi		%1, %0, 1	\n"
		"	blz		%1, 2f		\n"
		"	stex.w		%1, (%2)	\n"
		"	bez		%1, 1b		\n"
		FULL_FENCE
		"2:\n"
		: "=&r" (dec), "=&r" (tmp)
		: "r" (&v->counter)
		: "memory");

	return dec - 1;
}
#define arch_atomic_dec_if_positive arch_atomic_dec_if_positive

#else
#include <asm-generic/atomic.h>
#endif

#endif /* __ASM_CSKY_ATOMIC_H */
