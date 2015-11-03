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


#define ATOMIC_INIT(i)		{ (i) }
#define ATOMIC64_INIT(i)	{ (i) }

#define atomic_read(v)		ACCESS_ONCE((v)->counter)
#define atomic64_read(v)	ACCESS_ONCE((v)->counter)

#define atomic_set(v,i)		((v)->counter = (i))
#define atomic64_set(v,i)	((v)->counter = (i))

/*
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 */

#define ATOMIC_OP(op, asm_op)						\
static __inline__ void atomic_##op(int i, atomic_t * v)			\
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
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	long temp, result;						\
	smp_mb();							\
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

#define ATOMIC64_OP(op, asm_op)						\
static __inline__ void atomic64_##op(long i, atomic64_t * v)		\
{									\
	unsigned long temp;						\
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
static __inline__ long atomic64_##op##_return(long i, atomic64_t * v)	\
{									\
	long temp, result;						\
	smp_mb();							\
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

#define ATOMIC_OPS(op)							\
	ATOMIC_OP(op, op##l)						\
	ATOMIC_OP_RETURN(op, op##l)					\
	ATOMIC64_OP(op, op##q)						\
	ATOMIC64_OP_RETURN(op, op##q)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

#define atomic_andnot atomic_andnot
#define atomic64_andnot atomic64_andnot

ATOMIC_OP(and, and)
ATOMIC_OP(andnot, bic)
ATOMIC_OP(or, bis)
ATOMIC_OP(xor, xor)
ATOMIC64_OP(and, and)
ATOMIC64_OP(andnot, bic)
ATOMIC64_OP(or, bis)
ATOMIC64_OP(xor, xor)

#undef ATOMIC_OPS
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#define atomic64_cmpxchg(v, old, new) (cmpxchg(&((v)->counter), old, new))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), new))

#define atomic_cmpxchg(v, old, new) (cmpxchg(&((v)->counter), old, new))
#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

/**
 * __atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v.
 */
static __inline__ int __atomic_add_unless(atomic_t *v, int a, int u)
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


/**
 * atomic64_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns true iff @v was not @u.
 */
static __inline__ int atomic64_add_unless(atomic64_t *v, long a, long u)
{
	long c, tmp;
	smp_mb();
	__asm__ __volatile__(
	"1:	ldq_l	%[tmp],%[mem]\n"
	"	cmpeq	%[tmp],%[u],%[c]\n"
	"	addq	%[tmp],%[a],%[tmp]\n"
	"	bne	%[c],2f\n"
	"	stq_c	%[tmp],%[mem]\n"
	"	beq	%[tmp],3f\n"
	"2:\n"
	".subsection 2\n"
	"3:	br	1b\n"
	".previous"
	: [tmp] "=&r"(tmp), [c] "=&r"(c)
	: [mem] "m"(*v), [a] "rI"(a), [u] "rI"(u)
	: "memory");
	smp_mb();
	return !c;
}

/*
 * atomic64_dec_if_positive - decrement by 1 if old value positive
 * @v: pointer of type atomic_t
 *
 * The function returns the old value of *v minus 1, even if
 * the atomic variable, v, was not decremented.
 */
static inline long atomic64_dec_if_positive(atomic64_t *v)
{
	long old, tmp;
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

#define atomic64_inc_not_zero(v) atomic64_add_unless((v), 1, 0)

#define atomic_add_negative(a, v) (atomic_add_return((a), (v)) < 0)
#define atomic64_add_negative(a, v) (atomic64_add_return((a), (v)) < 0)

#define atomic_dec_return(v) atomic_sub_return(1,(v))
#define atomic64_dec_return(v) atomic64_sub_return(1,(v))

#define atomic_inc_return(v) atomic_add_return(1,(v))
#define atomic64_inc_return(v) atomic64_add_return(1,(v))

#define atomic_sub_and_test(i,v) (atomic_sub_return((i), (v)) == 0)
#define atomic64_sub_and_test(i,v) (atomic64_sub_return((i), (v)) == 0)

#define atomic_inc_and_test(v) (atomic_add_return(1, (v)) == 0)
#define atomic64_inc_and_test(v) (atomic64_add_return(1, (v)) == 0)

#define atomic_dec_and_test(v) (atomic_sub_return(1, (v)) == 0)
#define atomic64_dec_and_test(v) (atomic64_sub_return(1, (v)) == 0)

#define atomic_inc(v) atomic_add(1,(v))
#define atomic64_inc(v) atomic64_add(1,(v))

#define atomic_dec(v) atomic_sub(1,(v))
#define atomic64_dec(v) atomic64_sub(1,(v))

#endif /* _ALPHA_ATOMIC_H */
