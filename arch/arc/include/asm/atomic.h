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
#define ATOMIC_INIT(i)	{ (i) }

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


#ifdef CONFIG_GENERIC_ATOMIC64

#include <asm-generic/atomic64.h>

#else	/* Kconfig ensures this is only enabled with needed h/w assist */

/*
 * ARCv2 supports 64-bit exclusive load (LLOCKD) / store (SCONDD)
 *  - The address HAS to be 64-bit aligned
 *  - There are 2 semantics involved here:
 *    = exclusive implies no interim update between load/store to same addr
 *    = both words are observed/updated together: this is guaranteed even
 *      for regular 64-bit load (LDD) / store (STD). Thus atomic64_set()
 *      is NOT required to use LLOCKD+SCONDD, STD suffices
 */

typedef struct {
	aligned_u64 counter;
} atomic64_t;

#define ATOMIC64_INIT(a) { (a) }

static inline long long atomic64_read(const atomic64_t *v)
{
	unsigned long long val;

	__asm__ __volatile__(
	"	ldd   %0, [%1]	\n"
	: "=r"(val)
	: "r"(&v->counter));

	return val;
}

static inline void atomic64_set(atomic64_t *v, long long a)
{
	/*
	 * This could have been a simple assignment in "C" but would need
	 * explicit volatile. Otherwise gcc optimizers could elide the store
	 * which borked atomic64 self-test
	 * In the inline asm version, memory clobber needed for exact same
	 * reason, to tell gcc about the store.
	 *
	 * This however is not needed for sibling atomic64_add() etc since both
	 * load/store are explicitly done in inline asm. As long as API is used
	 * for each access, gcc has no way to optimize away any load/store
	 */
	__asm__ __volatile__(
	"	std   %0, [%1]	\n"
	:
	: "r"(a), "r"(&v->counter)
	: "memory");
}

#define ATOMIC64_OP(op, op1, op2)					\
static inline void atomic64_##op(long long a, atomic64_t *v)		\
{									\
	unsigned long long val;						\
									\
	__asm__ __volatile__(						\
	"1:				\n"				\
	"	llockd  %0, [%1]	\n"				\
	"	" #op1 " %L0, %L0, %L2	\n"				\
	"	" #op2 " %H0, %H0, %H2	\n"				\
	"	scondd   %0, [%1]	\n"				\
	"	bnz     1b		\n"				\
	: "=&r"(val)							\
	: "r"(&v->counter), "ir"(a)					\
	: "cc");						\
}									\

#define ATOMIC64_OP_RETURN(op, op1, op2)		        	\
static inline long long atomic64_##op##_return(long long a, atomic64_t *v)	\
{									\
	unsigned long long val;						\
									\
	smp_mb();							\
									\
	__asm__ __volatile__(						\
	"1:				\n"				\
	"	llockd   %0, [%1]	\n"				\
	"	" #op1 " %L0, %L0, %L2	\n"				\
	"	" #op2 " %H0, %H0, %H2	\n"				\
	"	scondd   %0, [%1]	\n"				\
	"	bnz     1b		\n"				\
	: [val] "=&r"(val)						\
	: "r"(&v->counter), "ir"(a)					\
	: "cc");	/* memory clobber comes from smp_mb() */	\
									\
	smp_mb();							\
									\
	return val;							\
}

#define ATOMIC64_FETCH_OP(op, op1, op2)		        		\
static inline long long atomic64_fetch_##op(long long a, atomic64_t *v)	\
{									\
	unsigned long long val, orig;					\
									\
	smp_mb();							\
									\
	__asm__ __volatile__(						\
	"1:				\n"				\
	"	llockd   %0, [%2]	\n"				\
	"	" #op1 " %L1, %L0, %L3	\n"				\
	"	" #op2 " %H1, %H0, %H3	\n"				\
	"	scondd   %1, [%2]	\n"				\
	"	bnz     1b		\n"				\
	: "=&r"(orig), "=&r"(val)					\
	: "r"(&v->counter), "ir"(a)					\
	: "cc");	/* memory clobber comes from smp_mb() */	\
									\
	smp_mb();							\
									\
	return orig;							\
}

#define ATOMIC64_OPS(op, op1, op2)					\
	ATOMIC64_OP(op, op1, op2)					\
	ATOMIC64_OP_RETURN(op, op1, op2)				\
	ATOMIC64_FETCH_OP(op, op1, op2)

#define atomic64_andnot atomic64_andnot

ATOMIC64_OPS(add, add.f, adc)
ATOMIC64_OPS(sub, sub.f, sbc)
ATOMIC64_OPS(and, and, and)
ATOMIC64_OPS(andnot, bic, bic)
ATOMIC64_OPS(or, or, or)
ATOMIC64_OPS(xor, xor, xor)

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

static inline long long
atomic64_cmpxchg(atomic64_t *ptr, long long expected, long long new)
{
	long long prev;

	smp_mb();

	__asm__ __volatile__(
	"1:	llockd  %0, [%1]	\n"
	"	brne    %L0, %L2, 2f	\n"
	"	brne    %H0, %H2, 2f	\n"
	"	scondd  %3, [%1]	\n"
	"	bnz     1b		\n"
	"2:				\n"
	: "=&r"(prev)
	: "r"(ptr), "ir"(expected), "r"(new)
	: "cc");	/* memory clobber comes from smp_mb() */

	smp_mb();

	return prev;
}

static inline long long atomic64_xchg(atomic64_t *ptr, long long new)
{
	long long prev;

	smp_mb();

	__asm__ __volatile__(
	"1:	llockd  %0, [%1]	\n"
	"	scondd  %2, [%1]	\n"
	"	bnz     1b		\n"
	"2:				\n"
	: "=&r"(prev)
	: "r"(ptr), "r"(new)
	: "cc");	/* memory clobber comes from smp_mb() */

	smp_mb();

	return prev;
}

/**
 * atomic64_dec_if_positive - decrement by 1 if old value positive
 * @v: pointer of type atomic64_t
 *
 * The function returns the old value of *v minus 1, even if
 * the atomic variable, v, was not decremented.
 */

static inline long long atomic64_dec_if_positive(atomic64_t *v)
{
	long long val;

	smp_mb();

	__asm__ __volatile__(
	"1:	llockd  %0, [%1]	\n"
	"	sub.f   %L0, %L0, 1	# w0 - 1, set C on borrow\n"
	"	sub.c   %H0, %H0, 1	# if C set, w1 - 1\n"
	"	brlt    %H0, 0, 2f	\n"
	"	scondd  %0, [%1]	\n"
	"	bnz     1b		\n"
	"2:				\n"
	: "=&r"(val)
	: "r"(&v->counter)
	: "cc");	/* memory clobber comes from smp_mb() */

	smp_mb();

	return val;
}

/**
 * atomic64_add_unless - add unless the number is a given value
 * @v: pointer of type atomic64_t
 * @a: the amount to add to v...
 * @u: ...unless v is equal to u.
 *
 * if (v != u) { v += a; ret = 1} else {ret = 0}
 * Returns 1 iff @v was not @u (i.e. if add actually happened)
 */
static inline int atomic64_add_unless(atomic64_t *v, long long a, long long u)
{
	long long val;
	int op_done;

	smp_mb();

	__asm__ __volatile__(
	"1:	llockd  %0, [%2]	\n"
	"	mov	%1, 1		\n"
	"	brne	%L0, %L4, 2f	# continue to add since v != u \n"
	"	breq.d	%H0, %H4, 3f	# return since v == u \n"
	"	mov	%1, 0		\n"
	"2:				\n"
	"	add.f   %L0, %L0, %L3	\n"
	"	adc     %H0, %H0, %H3	\n"
	"	scondd  %0, [%2]	\n"
	"	bnz     1b		\n"
	"3:				\n"
	: "=&r"(val), "=&r" (op_done)
	: "r"(&v->counter), "r"(a), "r"(u)
	: "cc");	/* memory clobber comes from smp_mb() */

	smp_mb();

	return op_done;
}

#define atomic64_add_negative(a, v)	(atomic64_add_return((a), (v)) < 0)
#define atomic64_inc(v)			atomic64_add(1LL, (v))
#define atomic64_inc_return(v)		atomic64_add_return(1LL, (v))
#define atomic64_inc_and_test(v)	(atomic64_inc_return(v) == 0)
#define atomic64_sub_and_test(a, v)	(atomic64_sub_return((a), (v)) == 0)
#define atomic64_dec(v)			atomic64_sub(1LL, (v))
#define atomic64_dec_return(v)		atomic64_sub_return(1LL, (v))
#define atomic64_dec_and_test(v)	(atomic64_dec_return((v)) == 0)
#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1LL, 0LL)

#endif	/* !CONFIG_GENERIC_ATOMIC64 */

#endif	/* !__ASSEMBLY__ */

#endif
