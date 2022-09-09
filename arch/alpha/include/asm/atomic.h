/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_ATOMIC_H
#define _ALPHA_ATOMIC_H

#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc...
 *
 * But use these as seldom as possible since they are much slower
 * than regular operations.
 */

/*
 * To ensure dependency ordering is preserved for the _relaxed and
 * _release atomics, an smp_mb() is unconditionally inserted into the
 * _relaxed variants, which are used to build the barriered versions.
 * Avoid redundant back-to-back fences in the _acquire and _fence
 * versions.
 */
#define __atomic_acquire_fence()
#define __atomic_post_full_fence()

#define ATOMIC64_INIT(i)	{ (i) }

#define arch_atomic_read(v)	READ_ONCE((v)->counter)
#define arch_atomic64_read(v)	READ_ONCE((v)->counter)

#define arch_atomic_set(v,i)	WRITE_ONCE((v)->counter, (i))
#define arch_atomic64_set(v,i)	WRITE_ONCE((v)->counter, (i))

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */

#define ATOMIC_OP(op, asm_op)						\
static __inline__ void arch_atomic_##op(int i, atomic_t * v)		\
{									\
	unsigned long temp;						\
	__asm__ __volatile__(						\
	"1:	ldl_l %0,%1\n"						\
	"	" #asm_op " %0,%2,%0\n"					\
	"	stl_c %0,%1\n"						\
	"	beq %0,2f\n"						\
	".subsection 2\n"						\
	"2:	br 1b\n"						\
	".previous"							\
	:"=&r" (temp), "=m" (v->counter)				\
	:"Ir" (i), "m" (v->counter));					\
}									\

#define ATOMIC_OP_RETURN(op, asm_op)					\
static inline int arch_atomic_##op##_return_relaxed(int i, atomic_t *v)	\
{									\
	long temp, result;						\
	__asm__ __volatile__(						\
	"1:	ldl_l %0,%1\n"						\
	"	" #asm_op " %0,%3,%2\n"					\
	"	" #asm_op " %0,%3,%0\n"					\
	"	stl_c %0,%1\n"						\
	"	beq %0,2f\n"						\
	".subsection 2\n"						\
	"2:	br 1b\n"						\
	".previous"							\
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)		\
	:"Ir" (i), "m" (v->counter) : "memory");			\
	smp_mb();							\
	return result;							\
}

#define ATOMIC_FETCH_OP(op, asm_op)					\
static inline int arch_atomic_fetch_##op##_relaxed(int i, atomic_t *v)	\
{									\
	long temp, result;						\
	__asm__ __volatile__(						\
	"1:	ldl_l %2,%1\n"						\
	"	" #asm_op " %2,%3,%0\n"					\
	"	stl_c %0,%1\n"						\
	"	beq %0,2f\n"						\
	".subsection 2\n"						\
	"2:	br 1b\n"						\
	".previous"							\
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)		\
	:"Ir" (i), "m" (v->counter) : "memory");			\
	smp_mb();							\
	return result;							\
}

#define ATOMIC64_OP(op, asm_op)						\
static __inline__ void arch_atomic64_##op(s64 i, atomic64_t * v)	\
{									\
	s64 temp;							\
	__asm__ __volatile__(						\
	"1:	ldq_l %0,%1\n"						\
	"	" #asm_op " %0,%2,%0\n"					\
	"	stq_c %0,%1\n"						\
	"	beq %0,2f\n"						\
	".subsection 2\n"						\
	"2:	br 1b\n"						\
	".previous"							\
	:"=&r" (temp), "=m" (v->counter)				\
	:"Ir" (i), "m" (v->counter));					\
}									\

#define ATOMIC64_OP_RETURN(op, asm_op)					\
static __inline__ s64							\
arch_atomic64_##op##_return_relaxed(s64 i, atomic64_t * v)		\
{									\
	s64 temp, result;						\
	__asm__ __volatile__(						\
	"1:	ldq_l %0,%1\n"						\
	"	" #asm_op " %0,%3,%2\n"					\
	"	" #asm_op " %0,%3,%0\n"					\
	"	stq_c %0,%1\n"						\
	"	beq %0,2f\n"						\
	".subsection 2\n"						\
	"2:	br 1b\n"						\
	".previous"							\
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)		\
	:"Ir" (i), "m" (v->counter) : "memory");			\
	smp_mb();							\
	return result;							\
}

#define ATOMIC64_FETCH_OP(op, asm_op)					\
static __inline__ s64							\
arch_atomic64_fetch_##op##_relaxed(s64 i, atomic64_t * v)		\
{									\
	s64 temp, result;						\
	__asm__ __volatile__(						\
	"1:	ldq_l %2,%1\n"						\
	"	" #asm_op " %2,%3,%0\n"					\
	"	stq_c %0,%1\n"						\
	"	beq %0,2f\n"						\
	".subsection 2\n"						\
	"2:	br 1b\n"						\
	".previous"							\
	:"=&r" (temp), "=m" (v->counter), "=&r" (result)		\
	:"Ir" (i), "m" (v->counter) : "memory");			\
	smp_mb();							\
	return result;							\
}

#define ATOMIC_OPS(op)							\
	ATOMIC_OP(op, op##l)						\
	ATOMIC_OP_RETURN(op, op##l)					\
	ATOMIC_FETCH_OP(op, op##l)					\
	ATOMIC64_OP(op, op##q)						\
	ATOMIC64_OP_RETURN(op, op##q)					\
	ATOMIC64_FETCH_OP(op, op##q)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

#define arch_atomic_add_return_relaxed		arch_atomic_add_return_relaxed
#define arch_atomic_sub_return_relaxed		arch_atomic_sub_return_relaxed
#define arch_atomic_fetch_add_relaxed		arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_sub_relaxed		arch_atomic_fetch_sub_relaxed

#define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
#define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed
#define arch_atomic64_fetch_add_relaxed		arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_sub_relaxed		arch_atomic64_fetch_sub_relaxed

#define arch_atomic_andnot			arch_atomic_andnot
#define arch_atomic64_andnot			arch_atomic64_andnot

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, asm)						\
	ATOMIC_OP(op, asm)						\
	ATOMIC_FETCH_OP(op, asm)					\
	ATOMIC64_OP(op, asm)						\
	ATOMIC64_FETCH_OP(op, asm)

ATOMIC_OPS(and, and)
ATOMIC_OPS(andnot, bic)
ATOMIC_OPS(or, bis)
ATOMIC_OPS(xor, xor)

#define arch_atomic_fetch_and_relaxed		arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_andnot_relaxed	arch_atomic_fetch_andnot_relaxed
#define arch_atomic_fetch_or_relaxed		arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_xor_relaxed		arch_atomic_fetch_xor_relaxed

#define arch_atomic64_fetch_and_relaxed		arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_andnot_relaxed	arch_atomic64_fetch_andnot_relaxed
#define arch_atomic64_fetch_or_relaxed		arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_xor_relaxed		arch_atomic64_fetch_xor_relaxed

#undef ATOMIC_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define arch_atomic64_cmpxchg(v, old, new) \
	(arch_cmpxchg(&((v)->counter), old, new))
#define arch_atomic64_xchg(v, new) \
	(arch_xchg(&((v)->counter), new))

#define arch_atomic_cmpxchg(v, old, new) \
	(arch_cmpxchg(&((v)->counter), old, new))
#define arch_atomic_xchg(v, new) \
	(arch_xchg(&((v)->counter), new))

/**
 * arch_atomic_fetch_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int arch_atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
	int c, new, old;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldl_l	%[old],%[mem]\n"
	"	cmpeq	%[old],%[u],%[c]\n"
	"	addl	%[old],%[a],%[new]\n"
	"	bne	%[c],2f\n"
	"	stl_c	%[new],%[mem]\n"
	"	beq	%[new],3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: [old] "=&r"(old), [new] "=&r"(new), [c] "=&r"(c)
	: [mem] "m"(*v), [a] "rI"(a), [u] "rI"((long)u)
	: "memory");
	smp_mb();
	return old;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

/**
 * arch_atomic64_fetch_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ s64 arch_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
	s64 c, new, old;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldq_l	%[old],%[mem]\n"
	"	cmpeq	%[old],%[u],%[c]\n"
	"	addq	%[old],%[a],%[new]\n"
	"	bne	%[c],2f\n"
	"	stq_c	%[new],%[mem]\n"
	"	beq	%[new],3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: [old] "=&r"(old), [new] "=&r"(new), [c] "=&r"(c)
	: [mem] "m"(*v), [a] "rI"(a), [u] "rI"(u)
	: "memory");
	smp_mb();
	return old;
}
#define arch_atomic64_fetch_add_unless arch_atomic64_fetch_add_unless

/*
 * arch_atomic64_dec_if_positive - decrement by 1 if old value positive
 * @v: pointer of type atomic_t
 *
 * The function returns the old value of *v minus 1, even if
 * the atomic variable, v, was not decremented.
 */
static inline s64 arch_atomic64_dec_if_positive(atomic64_t *v)
{
	s64 old, tmp;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldq_l	%[old],%[mem]\n"
	"	subq	%[old],1,%[tmp]\n"
	"	ble	%[old],2f\n"
	"	stq_c	%[tmp],%[mem]\n"
	"	beq	%[tmp],3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: [old] "=&r"(old), [tmp] "=&r"(tmp)
	: [mem] "m"(*v)
	: "memory");
	smp_mb();
	return old - 1;
}
#define arch_atomic64_dec_if_positive arch_atomic64_dec_if_positive

#endif /* _ALPHA_ATOMIC_H */
