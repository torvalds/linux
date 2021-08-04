/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_ARC_ATOMIC_LLSC_H
#define _ASM_ARC_ATOMIC_LLSC_H

#define arch_atomic_set(v, i) WRITE_ONCE(((v)->counter), (i))

#define ATOMIC_OP(op, c_op, asm_op)					\
static inline void arch_atomic_##op(int i, atomic_t *v)			\
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
static inline int arch_atomic_##op##_return(int i, atomic_t *v)		\
{									\
	unsigned int val;						\
									\
	/*								\
	 * Explicit full memory barrier needed before/after as		\
	 * LLOCK/SCOND themselves don't provide any such semantics	\
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
static inline int arch_atomic_fetch_##op(int i, atomic_t *v)		\
{									\
	unsigned int val, orig;						\
									\
	/*								\
	 * Explicit full memory barrier needed before/after as		\
	 * LLOCK/SCOND themselves don't provide any such semantics	\
	 */								\
	smp_mb();							\
									\
	__asm__ __volatile__(						\
	"1:	llock   %[orig], [%[ctr]]		\n"		\
	"	" #asm_op " %[val], %[orig], %[i]	\n"		\
	"	scond   %[val], [%[ctr]]		\n"		\
	"	bnz     1b				\n"		\
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

#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_OP_RETURN(op, c_op, asm_op)				\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(add, +=, add)
ATOMIC_OPS(sub, -=, sub)

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, c_op, asm_op)					\
	ATOMIC_OP(op, c_op, asm_op)					\
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(and, &=, and)
ATOMIC_OPS(andnot, &= ~, bic)
ATOMIC_OPS(or, |=, or)
ATOMIC_OPS(xor, ^=, xor)

#define arch_atomic_andnot		arch_atomic_andnot
#define arch_atomic_fetch_andnot	arch_atomic_fetch_andnot

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

#endif
