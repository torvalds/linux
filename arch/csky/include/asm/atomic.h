/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_ATOMIC_H
#define __ASM_CSKY_ATOMIC_H

#include <linux/version.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#ifdef CONFIG_CPU_HAS_LDSTEX

#define __atomic_add_unless __atomic_add_unless
static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	unsigned long tmp, ret;

	smp_mb();

	asm volatile (
	"1:	ldex.w		%0, (%3) \n"
	"	mov		%1, %0   \n"
	"	cmpne		%0, %4   \n"
	"	bf		2f	 \n"
	"	add		%0, %2   \n"
	"	stex.w		%0, (%3) \n"
	"	bez		%0, 1b   \n"
	"2:				 \n"
		: "=&r" (tmp), "=&r" (ret)
		: "r" (a), "r"(&v->counter), "r"(u)
		: "memory");

	if (ret != u)
		smp_mb();

	return ret;
}

#define ATOMIC_OP(op, c_op)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long tmp;						\
									\
	asm volatile (							\
	"1:	ldex.w		%0, (%2) \n"				\
	"	" #op "		%0, %1   \n"				\
	"	stex.w		%0, (%2) \n"				\
	"	bez		%0, 1b   \n"				\
		: "=&r" (tmp)						\
		: "r" (i), "r"(&v->counter)				\
		: "memory");						\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long tmp, ret;						\
									\
	smp_mb();							\
	asm volatile (							\
	"1:	ldex.w		%0, (%3) \n"				\
	"	" #op "		%0, %2   \n"				\
	"	mov		%1, %0   \n"				\
	"	stex.w		%0, (%3) \n"				\
	"	bez		%0, 1b   \n"				\
		: "=&r" (tmp), "=&r" (ret)				\
		: "r" (i), "r"(&v->counter)				\
		: "memory");						\
	smp_mb();							\
									\
	return ret;							\
}

#define ATOMIC_FETCH_OP(op, c_op)					\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	unsigned long tmp, ret;						\
									\
	smp_mb();							\
	asm volatile (							\
	"1:	ldex.w		%0, (%3) \n"				\
	"	mov		%1, %0   \n"				\
	"	" #op "		%0, %2   \n"				\
	"	stex.w		%0, (%3) \n"				\
	"	bez		%0, 1b   \n"				\
		: "=&r" (tmp), "=&r" (ret)				\
		: "r" (i), "r"(&v->counter)				\
		: "memory");						\
	smp_mb();							\
									\
	return ret;							\
}

#else /* CONFIG_CPU_HAS_LDSTEX */

#include <linux/irqflags.h>

#define __atomic_add_unless __atomic_add_unless
static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	unsigned long tmp, ret, flags;

	raw_local_irq_save(flags);

	asm volatile (
	"	ldw		%0, (%3) \n"
	"	mov		%1, %0   \n"
	"	cmpne		%0, %4   \n"
	"	bf		2f	 \n"
	"	add		%0, %2   \n"
	"	stw		%0, (%3) \n"
	"2:				 \n"
		: "=&r" (tmp), "=&r" (ret)
		: "r" (a), "r"(&v->counter), "r"(u)
		: "memory");

	raw_local_irq_restore(flags);

	return ret;
}

#define ATOMIC_OP(op, c_op)						\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long tmp, flags;					\
									\
	raw_local_irq_save(flags);					\
									\
	asm volatile (							\
	"	ldw		%0, (%2) \n"				\
	"	" #op "		%0, %1   \n"				\
	"	stw		%0, (%2) \n"				\
		: "=&r" (tmp)						\
		: "r" (i), "r"(&v->counter)				\
		: "memory");						\
									\
	raw_local_irq_restore(flags);					\
}

#define ATOMIC_OP_RETURN(op, c_op)					\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long tmp, ret, flags;					\
									\
	raw_local_irq_save(flags);					\
									\
	asm volatile (							\
	"	ldw		%0, (%3) \n"				\
	"	" #op "		%0, %2   \n"				\
	"	stw		%0, (%3) \n"				\
	"	mov		%1, %0   \n"				\
		: "=&r" (tmp), "=&r" (ret)				\
		: "r" (i), "r"(&v->counter)				\
		: "memory");						\
									\
	raw_local_irq_restore(flags);					\
									\
	return ret;							\
}

#define ATOMIC_FETCH_OP(op, c_op)					\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	unsigned long tmp, ret, flags;					\
									\
	raw_local_irq_save(flags);					\
									\
	asm volatile (							\
	"	ldw		%0, (%3) \n"				\
	"	mov		%1, %0   \n"				\
	"	" #op "		%0, %2   \n"				\
	"	stw		%0, (%3) \n"				\
		: "=&r" (tmp), "=&r" (ret)				\
		: "r" (i), "r"(&v->counter)				\
		: "memory");						\
									\
	raw_local_irq_restore(flags);					\
									\
	return ret;							\
}

#endif /* CONFIG_CPU_HAS_LDSTEX */

#define atomic_add_return atomic_add_return
ATOMIC_OP_RETURN(add, +)
#define atomic_sub_return atomic_sub_return
ATOMIC_OP_RETURN(sub, -)

#define atomic_fetch_add atomic_fetch_add
ATOMIC_FETCH_OP(add, +)
#define atomic_fetch_sub atomic_fetch_sub
ATOMIC_FETCH_OP(sub, -)
#define atomic_fetch_and atomic_fetch_and
ATOMIC_FETCH_OP(and, &)
#define atomic_fetch_or atomic_fetch_or
ATOMIC_FETCH_OP(or, |)
#define atomic_fetch_xor atomic_fetch_xor
ATOMIC_FETCH_OP(xor, ^)

#define atomic_and atomic_and
ATOMIC_OP(and, &)
#define atomic_or atomic_or
ATOMIC_OP(or, |)
#define atomic_xor atomic_xor
ATOMIC_OP(xor, ^)

#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#include <asm-generic/atomic.h>

#endif /* __ASM_CSKY_ATOMIC_H */
