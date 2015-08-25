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

#define atomic_read(v)  ((v)->counter)

#ifdef CONFIG_ARC_HAS_LLSC

#define atomic_set(v, i) (((v)->counter) = (i))

#ifdef CONFIG_ARC_STAR_9000923308

#define SCOND_FAIL_RETRY_VAR_DEF						\
	unsigned int delay = 1, tmp;						\

#define SCOND_FAIL_RETRY_ASM							\
	"	bz	4f			\n"				\
	"   ; --- scond fail delay ---		\n"				\
	"	mov	%[tmp], %[delay]	\n"	/* tmp = delay */	\
	"2: 	brne.d	%[tmp], 0, 2b		\n"	/* while (tmp != 0) */	\
	"	sub	%[tmp], %[tmp], 1	\n"	/* tmp-- */		\
	"	rol	%[delay], %[delay]	\n"	/* delay *= 2 */	\
	"	b	1b			\n"	/* start over */	\
	"4: ; --- success ---			\n"				\

#define SCOND_FAIL_RETRY_VARS							\
	  ,[delay] "+&r" (delay),[tmp] "=&r"	(tmp)				\

#else	/* !CONFIG_ARC_STAR_9000923308 */

#define SCOND_FAIL_RETRY_VAR_DEF

#define SCOND_FAIL_RETRY_ASM							\
	"	bnz     1b			\n"				\

#define SCOND_FAIL_RETRY_VARS

#endif

#define ATOMIC_OP(op, c_op, asm_op)					\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	unsigned int val;				                \
	SCOND_FAIL_RETRY_VAR_DEF                                        \
									\
	__asm__ __volatile__(						\
	"1:	llock   %[val], [%[ctr]]		\n"		\
	"	" #asm_op " %[val], %[val], %[i]	\n"		\
	"	scond   %[val], [%[ctr]]		\n"		\
	"						\n"		\
	SCOND_FAIL_RETRY_ASM						\
									\
	: [val]	"=&r"	(val) /* Early clobber to prevent reg reuse */	\
	  SCOND_FAIL_RETRY_VARS						\
	: [ctr]	"r"	(&v->counter), /* Not "m": llock only supports reg direct addr mode */	\
	  [i]	"ir"	(i)						\
	: "cc");							\
}									\

#define ATOMIC_OP_RETURN(op, c_op, asm_op)				\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned int val;				                \
	SCOND_FAIL_RETRY_VAR_DEF                                        \
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
	"						\n"		\
	SCOND_FAIL_RETRY_ASM						\
									\
	: [val]	"=&r"	(val)						\
	  SCOND_FAIL_RETRY_VARS						\
	: [ctr]	"r"	(&v->counter),					\
	  [i]	"ir"	(i)						\
	: "cc");							\
									\
	smp_mb();							\
									\
	return val;							\
}

#else	/* !CONFIG_ARC_HAS_LLSC */

#ifndef CONFIG_SMP

 /* violating atomic_xxx API locking protocol in UP for optimization sake */
#define atomic_set(v, i) (((v)->counter) = (i))

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
	v->counter = i;
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

#endif /* !CONFIG_ARC_HAS_LLSC */

#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_OP_RETURN(op, c_op, asm_op)

ATOMIC_OPS(add, +=, add)
ATOMIC_OPS(sub, -=, sub)
ATOMIC_OP(and, &=, and)

#define atomic_clear_mask(mask, v) atomic_and(~(mask), (v))

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP
#undef SCOND_FAIL_RETRY_VAR_DEF
#undef SCOND_FAIL_RETRY_ASM
#undef SCOND_FAIL_RETRY_VARS

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
