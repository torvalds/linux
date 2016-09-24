/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_ATOMIC_H
#define _ASM_ARC_ATOMIC_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <linux/compiler.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>
#include <asm/smp.h>

#ifndef CONFIG_ARC_PLAT_EZNPS

#define atomic_read(v)  READ_ONCE((v)->counter)

#ifdef CONFIG_ARC_HAS_LLSC

#define atomic_set(v, i) WRITE_ONCE(((v)->counter), (i))

#define ATOMIC_OP(op, c_op, asm_op)					\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned int val;						\
									\
	__asm__ __volatile__(						\
	"1:	llock   %[val], [%[ctr]]		\n"		\
	"	" #asm_op " %[val], %[val], %[i]	\n"		\
	"	scond   %[val], [%[ctr]]		\n"		\
	"	bnz     1b				\n"		\
	: [val]	"=&r"	(val) /* Early clobber to prevent reg reuse */	\
	: [ctr]	"r"	(&v->counter), /* Not "m": llock only supports reg direct addr mode */	\
	  [i]	"ir"	(i)						\
	: "cc");							\
}									\

#define ATOMIC_OP_RETURN(op, c_op, asm_op)				\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned int val;						\
									\
	/*								\
	 * Explicit full memory barrier needed before/after as		\
	 * LLOCK/SCOND thmeselves don't provide any such semantics	\
	 */								\
	smp_mb();							\
									\
	__asm__ __volatile__(						\
	"1:	llock   %[val], [%[ctr]]		\n"		\
	"	" #asm_op " %[val], %[val], %[i]	\n"		\
	"	scond   %[val], [%[ctr]]		\n"		\
	"	bnz     1b				\n"		\
	: [val]	"=&r"	(val)						\
	: [ctr]	"r"	(&v->counter),					\
	  [i]	"ir"	(i)						\
	: "cc");							\
									\
	smp_mb();							\
									\
	return val;							\
}

#define ATOMIC_FETCH_OP(op, c_op, asm_op)				\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	unsigned int val, orig;						\
									\
	/*								\
	 * Explicit full memory barrier needed before/after as		\
	 * LLOCK/SCOND thmeselves don't provide any such semantics	\
	 */								\
	smp_mb();							\
									\
	__asm__ __volatile__(						\
	"1:	llock   %[orig], [%[ctr]]		\n"		\
	"	" #asm_op " %[val], %[orig], %[i]	\n"		\
	"	scond   %[val], [%[ctr]]		\n"		\
	"						\n"		\
	: [val]	"=&r"	(val),						\
	  [orig] "=&r" (orig)						\
	: [ctr]	"r"	(&v->counter),					\
	  [i]	"ir"	(i)						\
	: "cc");							\
									\
	smp_mb();							\
									\
	return orig;							\
}

#else	/* !CONFIG_ARC_HAS_LLSC */

#ifndef CONFIG_SMP

 /* violating atomic_xxx API locking protocol in UP for optimization sake */
#define atomic_set(v, i) WRITE_ONCE(((v)->counter), (i))

#else

static inline void atomic_set(atomic_t *v, int i)
{
	/*
	 * Independent of hardware support, all of the atomic_xxx() APIs need
	 * to follow the same locking rules to make sure that a "hardware"
	 * atomic insn (e.g. LD) doesn't clobber an "emulated" atomic insn
	 * sequence
	 *
	 * Thus atomic_set() despite being 1 insn (and seemingly atomic)
	 * requires the locking.
	 */
	unsigned long flags;

	atomic_ops_lock(flags);
	WRITE_ONCE(v->counter, i);
	atomic_ops_unlock(flags);
}

#endif

/*
 * Non hardware assisted Atomic-R-M-W
 * Locking would change to irq-disabling only (UP) and spinlocks (SMP)
 */

#define ATOMIC_OP(op, c_op, asm_op)					\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned long flags;						\
									\
	atomic_ops_lock(flags);						\
	v->counter c_op i;						\
	atomic_ops_unlock(flags);					\
}

#define ATOMIC_OP_RETURN(op, c_op, asm_op)				\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned long flags;						\
	unsigned long temp;						\
									\
	/*								\
	 * spin lock/unlock provides the needed smp_mb() before/after	\
	 */								\
	atomic_ops_lock(flags);						\
	temp = v->counter;						\
	temp c_op i;							\
	v->counter = temp;						\
	atomic_ops_unlock(flags);					\
									\
	return temp;							\
}

#define ATOMIC_FETCH_OP(op, c_op, asm_op)				\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	unsigned long flags;						\
	unsigned long orig;						\
									\
	/*								\
	 * spin lock/unlock provides the needed smp_mb() before/after	\
	 */								\
	atomic_ops_lock(flags);						\
	orig = v->counter;						\
	v->counter c_op i;						\
	atomic_ops_unlock(flags);					\
									\
	return orig;							\
}

#endif /* !CONFIG_ARC_HAS_LLSC */

#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_OP_RETURN(op, c_op, asm_op)				\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(add, +=, add)
ATOMIC_OPS(sub, -=, sub)

#define atomic_andnot atomic_andnot

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(and, &=, and)
ATOMIC_OPS(andnot, &= ~, bic)
ATOMIC_OPS(or, |=, or)
ATOMIC_OPS(xor, ^=, xor)

#else /* CONFIG_ARC_PLAT_EZNPS */

static inline int atomic_read(const atomic_t *v)
{
	int temp;

	__asm__ __volatile__(
	"	ld.di %0, [%1]"
	: "=r"(temp)
	: "r"(&v->counter)
	: "memory");
	return temp;
}

static inline void atomic_set(atomic_t *v, int i)
{
	__asm__ __volatile__(
	"	st.di %0,[%1]"
	:
	: "r"(i), "r"(&v->counter)
	: "memory");
}

#define ATOMIC_OP(op, c_op, asm_op)					\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	__asm__ __volatile__(						\
	"	mov r2, %0\n"						\
	"	mov r3, %1\n"						\
	"       .word %2\n"						\
	:								\
	: "r"(i), "r"(&v->counter), "i"(asm_op)				\
	: "r2", "r3", "memory");					\
}									\

#define ATOMIC_OP_RETURN(op, c_op, asm_op)				\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned int temp = i;						\
									\
	/* Explicit full memory barrier needed before/after */		\
	smp_mb();							\
									\
	__asm__ __volatile__(						\
	"	mov r2, %0\n"						\
	"	mov r3, %1\n"						\
	"       .word %2\n"						\
	"	mov %0, r2"						\
	: "+r"(temp)							\
	: "r"(&v->counter), "i"(asm_op)					\
	: "r2", "r3", "memory");					\
									\
	smp_mb();							\
									\
	temp c_op i;							\
									\
	return temp;							\
}

#define ATOMIC_FETCH_OP(op, c_op, asm_op)				\
static inline int atomic_fetch_##op(int i, atomic_t *v)			\
{									\
	unsigned int temp = i;						\
									\
	/* Explicit full memory barrier needed before/after */		\
	smp_mb();							\
									\
	__asm__ __volatile__(						\
	"	mov r2, %0\n"						\
	"	mov r3, %1\n"						\
	"       .word %2\n"						\
	"	mov %0, r2"						\
	: "+r"(temp)							\
	: "r"(&v->counter), "i"(asm_op)					\
	: "r2", "r3", "memory");					\
									\
	smp_mb();							\
									\
	return temp;							\
}

#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_OP_RETURN(op, c_op, asm_op)				\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(add, +=, CTOP_INST_AADD_DI_R2_R2_R3)
#define atomic_sub(i, v) atomic_add(-(i), (v))
#define atomic_sub_return(i, v) atomic_add_return(-(i), (v))

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(and, &=, CTOP_INST_AAND_DI_R2_R2_R3)
#define atomic_andnot(mask, v) atomic_and(~(mask), (v))
ATOMIC_OPS(or, |=, CTOP_INST_AOR_DI_R2_R2_R3)
ATOMIC_OPS(xor, ^=, CTOP_INST_AXOR_DI_R2_R2_R3)

#endif /* CONFIG_ARC_PLAT_EZNPS */

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

/**
 * __atomic_add_unless - add unless the number is a given value
 * @v: pointer of type atomic_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * Atomically adds @a to @v, so long as it was not @u.
 * Returns the old value of @v
 */
#define __atomic_add_unless(v, a, u)					\
({									\
	int c, old;							\
									\
	/*								\
	 * Explicit full memory barrier needed before/after as		\
	 * LLOCK/SCOND thmeselves don't provide any such semantics	\
	 */								\
	smp_mb();							\
									\
	c = atomic_read(v);						\
	while (c != (u) && (old = atomic_cmpxchg((v), c, c + (a))) != c)\
		c = old;						\
									\
	smp_mb();							\
									\
	c;								\
})

#define atomic_inc_not_zero(v)		atomic_add_unless((v), 1, 0)

#define atomic_inc(v)			atomic_add(1, v)
#define atomic_dec(v)			atomic_sub(1, v)

#define atomic_inc_and_test(v)		(atomic_add_return(1, v) == 0)
#define atomic_dec_and_test(v)		(atomic_sub_return(1, v) == 0)
#define atomic_inc_return(v)		atomic_add_return(1, (v))
#define atomic_dec_return(v)		atomic_sub_return(1, (v))
#define atomic_sub_and_test(i, v)	(atomic_sub_return(i, v) == 0)

#define atomic_add_negative(i, v)	(atomic_add_return(i, v) < 0)

#define ATOMIC_INIT(i)			{ (i) }

#include <asm-generic/atomic64.h>

#endif

#endif
