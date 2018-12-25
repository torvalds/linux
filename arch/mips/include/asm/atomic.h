/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * But use these as seldom as possible since they are much more slower
 * than regular operations.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 99, 2000, 03, 04, 06 by Ralf Baechle
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/irqflags.h>
#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/compiler.h>
#include <asm/cpu-features.h>
#include <asm/cmpxchg.h>
#include <asm/war.h>

/*
 * Using a branch-likely instruction to check the result of an sc instruction
 * works around a bug present in R10000 CPUs prior to revision 3.0 that could
 * cause ll-sc sequences to execute non-atomically.
 */
#if R10000_LLSC_WAR
# define __scbeqz "beqzl"
#else
# define __scbeqz "beqz"
#endif

#define ATOMIC_INIT(i)	  { (i) }

/*
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.
 */
#define atomic_read(v)		READ_ONCE((v)->counter)

/*
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.
 */
#define atomic_set(v, i)	WRITE_ONCE((v)->counter, (i))

#define ATOMIC_OP(op, c_op, asm_op)					      \
static __inline__ void atomic_##op(int i, atomic_t * v)			      \
{									      \
	if (kernel_uses_llsc) {						      \
		int temp;						      \
									      \
		__asm__ __volatile__(					      \
		"	.set	"MIPS_ISA_LEVEL"			\n"   \
		"1:	ll	%0, %1		# atomic_" #op "	\n"   \
		"	" #asm_op " %0, %2				\n"   \
		"	sc	%0, %1					\n"   \
		"\t" __scbeqz "	%0, 1b					\n"   \
		"	.set	mips0					\n"   \
		: "=&r" (temp), "+" GCC_OFF_SMALL_ASM() (v->counter)	      \
		: "Ir" (i));						      \
	} else {							      \
		unsigned long flags;					      \
									      \
		raw_local_irq_save(flags);				      \
		v->counter c_op i;					      \
		raw_local_irq_restore(flags);				      \
	}								      \
}

#define ATOMIC_OP_RETURN(op, c_op, asm_op)				      \
static __inline__ int atomic_##op##_return_relaxed(int i, atomic_t * v)	      \
{									      \
	int result;							      \
									      \
	if (kernel_uses_llsc) {						      \
		int temp;						      \
									      \
		__asm__ __volatile__(					      \
		"	.set	"MIPS_ISA_LEVEL"			\n"   \
		"1:	ll	%1, %2		# atomic_" #op "_return	\n"   \
		"	" #asm_op " %0, %1, %3				\n"   \
		"	sc	%0, %2					\n"   \
		"\t" __scbeqz "	%0, 1b					\n"   \
		"	" #asm_op " %0, %1, %3				\n"   \
		"	.set	mips0					\n"   \
		: "=&r" (result), "=&r" (temp),				      \
		  "+" GCC_OFF_SMALL_ASM() (v->counter)			      \
		: "Ir" (i));						      \
	} else {							      \
		unsigned long flags;					      \
									      \
		raw_local_irq_save(flags);				      \
		result = v->counter;					      \
		result c_op i;						      \
		v->counter = result;					      \
		raw_local_irq_restore(flags);				      \
	}								      \
									      \
	return result;							      \
}

#define ATOMIC_FETCH_OP(op, c_op, asm_op)				      \
static __inline__ int atomic_fetch_##op##_relaxed(int i, atomic_t * v)	      \
{									      \
	int result;							      \
									      \
	if (kernel_uses_llsc) {						      \
		int temp;						      \
									      \
		__asm__ __volatile__(					      \
		"	.set	"MIPS_ISA_LEVEL"			\n"   \
		"1:	ll	%1, %2		# atomic_fetch_" #op "	\n"   \
		"	" #asm_op " %0, %1, %3				\n"   \
		"	sc	%0, %2					\n"   \
		"\t" __scbeqz "	%0, 1b					\n"   \
		"	.set	mips0					\n"   \
		"	move	%0, %1					\n"   \
		: "=&r" (result), "=&r" (temp),				      \
		  "+" GCC_OFF_SMALL_ASM() (v->counter)			      \
		: "Ir" (i));						      \
	} else {							      \
		unsigned long flags;					      \
									      \
		raw_local_irq_save(flags);				      \
		result = v->counter;					      \
		v->counter c_op i;					      \
		raw_local_irq_restore(flags);				      \
	}								      \
									      \
	return result;							      \
}

#define ATOMIC_OPS(op, c_op, asm_op)					      \
	ATOMIC_OP(op, c_op, asm_op)					      \
	ATOMIC_OP_RETURN(op, c_op, asm_op)				      \
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(add, +=, addu)
ATOMIC_OPS(sub, -=, subu)

#define atomic_add_return_relaxed	atomic_add_return_relaxed
#define atomic_sub_return_relaxed	atomic_sub_return_relaxed
#define atomic_fetch_add_relaxed	atomic_fetch_add_relaxed
#define atomic_fetch_sub_relaxed	atomic_fetch_sub_relaxed

#undef ATOMIC_OPS
#define ATOMIC_OPS(op, c_op, asm_op)					      \
	ATOMIC_OP(op, c_op, asm_op)					      \
	ATOMIC_FETCH_OP(op, c_op, asm_op)

ATOMIC_OPS(and, &=, and)
ATOMIC_OPS(or, |=, or)
ATOMIC_OPS(xor, ^=, xor)

#define atomic_fetch_and_relaxed	atomic_fetch_and_relaxed
#define atomic_fetch_or_relaxed		atomic_fetch_or_relaxed
#define atomic_fetch_xor_relaxed	atomic_fetch_xor_relaxed

#undef ATOMIC_OPS
#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

/*
 * atomic_sub_if_positive - conditionally subtract integer from atomic variable
 * @i: integer value to subtract
 * @v: pointer of type atomic_t
 *
 * Atomically test @v and subtract @i if @v is greater or equal than @i.
 * The function returns the old value of @v minus @i.
 */
static __inline__ int atomic_sub_if_positive(int i, atomic_t * v)
{
	int result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc) {
		int temp;

		__asm__ __volatile__(
		"	.set	"MIPS_ISA_LEVEL"			\n"
		"1:	ll	%1, %2		# atomic_sub_if_positive\n"
		"	.set	mips0					\n"
		"	subu	%0, %1, %3				\n"
		"	move	%1, %0					\n"
		"	bltz	%0, 1f					\n"
		"	.set	"MIPS_ISA_LEVEL"			\n"
		"	sc	%1, %2					\n"
		"\t" __scbeqz "	%1, 1b					\n"
		"1:							\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp),
		  "+" GCC_OFF_SMALL_ASM() (v->counter)
		: "Ir" (i));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result -= i;
		if (result >= 0)
			v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

#define atomic_cmpxchg(v, o, n) (cmpxchg(&((v)->counter), (o), (n)))
#define atomic_xchg(v, new) (xchg(&((v)->counter), (new)))

/*
 * atomic_dec_if_positive - decrement by 1 if old value positive
 * @v: pointer of type atomic_t
 */
#define atomic_dec_if_positive(v)	atomic_sub_if_positive(1, v)

#ifdef CONFIG_64BIT

#define ATOMIC64_INIT(i)    { (i) }

/*
 * atomic64_read - read atomic variable
 * @v: pointer of type atomic64_t
 *
 */
#define atomic64_read(v)	READ_ONCE((v)->counter)

/*
 * atomic64_set - set atomic variable
 * @v: pointer of type atomic64_t
 * @i: required value
 */
#define atomic64_set(v, i)	WRITE_ONCE((v)->counter, (i))

#define ATOMIC64_OP(op, c_op, asm_op)					      \
static __inline__ void atomic64_##op(long i, atomic64_t * v)		      \
{									      \
	if (kernel_uses_llsc) {						      \
		long temp;						      \
									      \
		__asm__ __volatile__(					      \
		"	.set	"MIPS_ISA_LEVEL"			\n"   \
		"1:	lld	%0, %1		# atomic64_" #op "	\n"   \
		"	" #asm_op " %0, %2				\n"   \
		"	scd	%0, %1					\n"   \
		"\t" __scbeqz "	%0, 1b					\n"   \
		"	.set	mips0					\n"   \
		: "=&r" (temp), "+" GCC_OFF_SMALL_ASM() (v->counter)	      \
		: "Ir" (i));						      \
	} else {							      \
		unsigned long flags;					      \
									      \
		raw_local_irq_save(flags);				      \
		v->counter c_op i;					      \
		raw_local_irq_restore(flags);				      \
	}								      \
}

#define ATOMIC64_OP_RETURN(op, c_op, asm_op)				      \
static __inline__ long atomic64_##op##_return_relaxed(long i, atomic64_t * v) \
{									      \
	long result;							      \
									      \
	if (kernel_uses_llsc) {						      \
		long temp;						      \
									      \
		__asm__ __volatile__(					      \
		"	.set	"MIPS_ISA_LEVEL"			\n"   \
		"1:	lld	%1, %2		# atomic64_" #op "_return\n"  \
		"	" #asm_op " %0, %1, %3				\n"   \
		"	scd	%0, %2					\n"   \
		"\t" __scbeqz "	%0, 1b					\n"   \
		"	" #asm_op " %0, %1, %3				\n"   \
		"	.set	mips0					\n"   \
		: "=&r" (result), "=&r" (temp),				      \
		  "+" GCC_OFF_SMALL_ASM() (v->counter)			      \
		: "Ir" (i));						      \
	} else {							      \
		unsigned long flags;					      \
									      \
		raw_local_irq_save(flags);				      \
		result = v->counter;					      \
		result c_op i;						      \
		v->counter = result;					      \
		raw_local_irq_restore(flags);				      \
	}								      \
									      \
	return result;							      \
}

#define ATOMIC64_FETCH_OP(op, c_op, asm_op)				      \
static __inline__ long atomic64_fetch_##op##_relaxed(long i, atomic64_t * v)  \
{									      \
	long result;							      \
									      \
	if (kernel_uses_llsc) {						      \
		long temp;						      \
									      \
		__asm__ __volatile__(					      \
		"	.set	"MIPS_ISA_LEVEL"			\n"   \
		"1:	lld	%1, %2		# atomic64_fetch_" #op "\n"   \
		"	" #asm_op " %0, %1, %3				\n"   \
		"	scd	%0, %2					\n"   \
		"\t" __scbeqz "	%0, 1b					\n"   \
		"	move	%0, %1					\n"   \
		"	.set	mips0					\n"   \
		: "=&r" (result), "=&r" (temp),				      \
		  "+" GCC_OFF_SMALL_ASM() (v->counter)			      \
		: "Ir" (i));						      \
	} else {							      \
		unsigned long flags;					      \
									      \
		raw_local_irq_save(flags);				      \
		result = v->counter;					      \
		v->counter c_op i;					      \
		raw_local_irq_restore(flags);				      \
	}								      \
									      \
	return result;							      \
}

#define ATOMIC64_OPS(op, c_op, asm_op)					      \
	ATOMIC64_OP(op, c_op, asm_op)					      \
	ATOMIC64_OP_RETURN(op, c_op, asm_op)				      \
	ATOMIC64_FETCH_OP(op, c_op, asm_op)

ATOMIC64_OPS(add, +=, daddu)
ATOMIC64_OPS(sub, -=, dsubu)

#define atomic64_add_return_relaxed	atomic64_add_return_relaxed
#define atomic64_sub_return_relaxed	atomic64_sub_return_relaxed
#define atomic64_fetch_add_relaxed	atomic64_fetch_add_relaxed
#define atomic64_fetch_sub_relaxed	atomic64_fetch_sub_relaxed

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(op, c_op, asm_op)					      \
	ATOMIC64_OP(op, c_op, asm_op)					      \
	ATOMIC64_FETCH_OP(op, c_op, asm_op)

ATOMIC64_OPS(and, &=, and)
ATOMIC64_OPS(or, |=, or)
ATOMIC64_OPS(xor, ^=, xor)

#define atomic64_fetch_and_relaxed	atomic64_fetch_and_relaxed
#define atomic64_fetch_or_relaxed	atomic64_fetch_or_relaxed
#define atomic64_fetch_xor_relaxed	atomic64_fetch_xor_relaxed

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

/*
 * atomic64_sub_if_positive - conditionally subtract integer from atomic
 *                            variable
 * @i: integer value to subtract
 * @v: pointer of type atomic64_t
 *
 * Atomically test @v and subtract @i if @v is greater or equal than @i.
 * The function returns the old value of @v minus @i.
 */
static __inline__ long atomic64_sub_if_positive(long i, atomic64_t * v)
{
	long result;

	smp_mb__before_llsc();

	if (kernel_uses_llsc) {
		long temp;

		__asm__ __volatile__(
		"	.set	"MIPS_ISA_LEVEL"			\n"
		"1:	lld	%1, %2		# atomic64_sub_if_positive\n"
		"	dsubu	%0, %1, %3				\n"
		"	move	%1, %0					\n"
		"	bltz	%0, 1f					\n"
		"	scd	%1, %2					\n"
		"\t" __scbeqz "	%1, 1b					\n"
		"1:							\n"
		"	.set	mips0					\n"
		: "=&r" (result), "=&r" (temp),
		  "+" GCC_OFF_SMALL_ASM() (v->counter)
		: "Ir" (i));
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		result = v->counter;
		result -= i;
		if (result >= 0)
			v->counter = result;
		raw_local_irq_restore(flags);
	}

	smp_llsc_mb();

	return result;
}

#define atomic64_cmpxchg(v, o, n) \
	((__typeof__((v)->counter))cmpxchg(&((v)->counter), (o), (n)))
#define atomic64_xchg(v, new) (xchg(&((v)->counter), (new)))

/*
 * atomic64_dec_if_positive - decrement by 1 if old value positive
 * @v: pointer of type atomic64_t
 */
#define atomic64_dec_if_positive(v)	atomic64_sub_if_positive(1, v)

#endif /* CONFIG_64BIT */

#endif /* _ASM_ATOMIC_H */
