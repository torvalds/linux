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
#include <asm/llsc.h>
#include <asm/sync.h>
#include <asm/war.h>

#define ATOMIC_OPS(pfx, type)						\
static __always_inline type arch_##pfx##_read(const pfx##_t *v)		\
{									\
	return READ_ONCE(v->counter);					\
}									\
									\
static __always_inline void arch_##pfx##_set(pfx##_t *v, type i)	\
{									\
	WRITE_ONCE(v->counter, i);					\
}									\
									\
static __always_inline type						\
arch_##pfx##_cmpxchg(pfx##_t *v, type o, type n)			\
{									\
	return arch_cmpxchg(&v->counter, o, n);				\
}									\
									\
static __always_inline type arch_##pfx##_xchg(pfx##_t *v, type n)	\
{									\
	return arch_xchg(&v->counter, n);				\
}

ATOMIC_OPS(atomic, int)

#ifdef CONFIG_64BIT
# define ATOMIC64_INIT(i)	{ (i) }
ATOMIC_OPS(atomic64, s64)
#endif

#define ATOMIC_OP(pfx, op, type, c_op, asm_op, ll, sc)			\
static __inline__ void arch_##pfx##_##op(type i, pfx##_t * v)		\
{									\
	type temp;							\
									\
	if (!kernel_uses_llsc) {					\
		unsigned long flags;					\
									\
		raw_local_irq_save(flags);				\
		v->counter c_op i;					\
		raw_local_irq_restore(flags);				\
		return;							\
	}								\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	" MIPS_ISA_LEVEL "			\n"	\
	"	" __SYNC(full, loongson3_war) "			\n"	\
	"1:	" #ll "	%0, %1		# " #pfx "_" #op "	\n"	\
	"	" #asm_op " %0, %2				\n"	\
	"	" #sc "	%0, %1					\n"	\
	"\t" __SC_BEQZ "%0, 1b					\n"	\
	"	.set	pop					\n"	\
	: "=&r" (temp), "+" GCC_OFF_SMALL_ASM() (v->counter)		\
	: "Ir" (i) : __LLSC_CLOBBER);					\
}

#define ATOMIC_OP_RETURN(pfx, op, type, c_op, asm_op, ll, sc)		\
static __inline__ type							\
arch_##pfx##_##op##_return_relaxed(type i, pfx##_t * v)			\
{									\
	type temp, result;						\
									\
	if (!kernel_uses_llsc) {					\
		unsigned long flags;					\
									\
		raw_local_irq_save(flags);				\
		result = v->counter;					\
		result c_op i;						\
		v->counter = result;					\
		raw_local_irq_restore(flags);				\
		return result;						\
	}								\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	" MIPS_ISA_LEVEL "			\n"	\
	"	" __SYNC(full, loongson3_war) "			\n"	\
	"1:	" #ll "	%1, %2		# " #pfx "_" #op "_return\n"	\
	"	" #asm_op " %0, %1, %3				\n"	\
	"	" #sc "	%0, %2					\n"	\
	"\t" __SC_BEQZ "%0, 1b					\n"	\
	"	" #asm_op " %0, %1, %3				\n"	\
	"	.set	pop					\n"	\
	: "=&r" (result), "=&r" (temp),					\
	  "+" GCC_OFF_SMALL_ASM() (v->counter)				\
	: "Ir" (i) : __LLSC_CLOBBER);					\
									\
	return result;							\
}

#define ATOMIC_FETCH_OP(pfx, op, type, c_op, asm_op, ll, sc)		\
static __inline__ type							\
arch_##pfx##_fetch_##op##_relaxed(type i, pfx##_t * v)			\
{									\
	int temp, result;						\
									\
	if (!kernel_uses_llsc) {					\
		unsigned long flags;					\
									\
		raw_local_irq_save(flags);				\
		result = v->counter;					\
		v->counter c_op i;					\
		raw_local_irq_restore(flags);				\
		return result;						\
	}								\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	" MIPS_ISA_LEVEL "			\n"	\
	"	" __SYNC(full, loongson3_war) "			\n"	\
	"1:	" #ll "	%1, %2		# " #pfx "_fetch_" #op "\n"	\
	"	" #asm_op " %0, %1, %3				\n"	\
	"	" #sc "	%0, %2					\n"	\
	"\t" __SC_BEQZ "%0, 1b					\n"	\
	"	.set	pop					\n"	\
	"	move	%0, %1					\n"	\
	: "=&r" (result), "=&r" (temp),					\
	  "+" GCC_OFF_SMALL_ASM() (v->counter)				\
	: "Ir" (i) : __LLSC_CLOBBER);					\
									\
	return result;							\
}

#undef ATOMIC_OPS
#define ATOMIC_OPS(pfx, op, type, c_op, asm_op, ll, sc)			\
	ATOMIC_OP(pfx, op, type, c_op, asm_op, ll, sc)			\
	ATOMIC_OP_RETURN(pfx, op, type, c_op, asm_op, ll, sc)		\
	ATOMIC_FETCH_OP(pfx, op, type, c_op, asm_op, ll, sc)

ATOMIC_OPS(atomic, add, int, +=, addu, ll, sc)
ATOMIC_OPS(atomic, sub, int, -=, subu, ll, sc)

#define arch_atomic_add_return_relaxed	arch_atomic_add_return_relaxed
#define arch_atomic_sub_return_relaxed	arch_atomic_sub_return_relaxed
#define arch_atomic_fetch_add_relaxed	arch_atomic_fetch_add_relaxed
#define arch_atomic_fetch_sub_relaxed	arch_atomic_fetch_sub_relaxed

#ifdef CONFIG_64BIT
ATOMIC_OPS(atomic64, add, s64, +=, daddu, lld, scd)
ATOMIC_OPS(atomic64, sub, s64, -=, dsubu, lld, scd)
# define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
# define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed
# define arch_atomic64_fetch_add_relaxed	arch_atomic64_fetch_add_relaxed
# define arch_atomic64_fetch_sub_relaxed	arch_atomic64_fetch_sub_relaxed
#endif /* CONFIG_64BIT */

#undef ATOMIC_OPS
#define ATOMIC_OPS(pfx, op, type, c_op, asm_op, ll, sc)			\
	ATOMIC_OP(pfx, op, type, c_op, asm_op, ll, sc)			\
	ATOMIC_FETCH_OP(pfx, op, type, c_op, asm_op, ll, sc)

ATOMIC_OPS(atomic, and, int, &=, and, ll, sc)
ATOMIC_OPS(atomic, or, int, |=, or, ll, sc)
ATOMIC_OPS(atomic, xor, int, ^=, xor, ll, sc)

#define arch_atomic_fetch_and_relaxed	arch_atomic_fetch_and_relaxed
#define arch_atomic_fetch_or_relaxed	arch_atomic_fetch_or_relaxed
#define arch_atomic_fetch_xor_relaxed	arch_atomic_fetch_xor_relaxed

#ifdef CONFIG_64BIT
ATOMIC_OPS(atomic64, and, s64, &=, and, lld, scd)
ATOMIC_OPS(atomic64, or, s64, |=, or, lld, scd)
ATOMIC_OPS(atomic64, xor, s64, ^=, xor, lld, scd)
# define arch_atomic64_fetch_and_relaxed	arch_atomic64_fetch_and_relaxed
# define arch_atomic64_fetch_or_relaxed		arch_atomic64_fetch_or_relaxed
# define arch_atomic64_fetch_xor_relaxed	arch_atomic64_fetch_xor_relaxed
#endif

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
#define ATOMIC_SIP_OP(pfx, type, op, ll, sc)				\
static __inline__ type arch_##pfx##_sub_if_positive(type i, pfx##_t * v)	\
{									\
	type temp, result;						\
									\
	smp_mb__before_atomic();					\
									\
	if (!kernel_uses_llsc) {					\
		unsigned long flags;					\
									\
		raw_local_irq_save(flags);				\
		result = v->counter;					\
		result -= i;						\
		if (result >= 0)					\
			v->counter = result;				\
		raw_local_irq_restore(flags);				\
		smp_mb__after_atomic();					\
		return result;						\
	}								\
									\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	" MIPS_ISA_LEVEL "			\n"	\
	"	" __SYNC(full, loongson3_war) "			\n"	\
	"1:	" #ll "	%1, %2		# atomic_sub_if_positive\n"	\
	"	.set	pop					\n"	\
	"	" #op "	%0, %1, %3				\n"	\
	"	move	%1, %0					\n"	\
	"	bltz	%0, 2f					\n"	\
	"	.set	push					\n"	\
	"	.set	" MIPS_ISA_LEVEL "			\n"	\
	"	" #sc "	%1, %2					\n"	\
	"	" __SC_BEQZ "%1, 1b				\n"	\
	"2:	" __SYNC(full, loongson3_war) "			\n"	\
	"	.set	pop					\n"	\
	: "=&r" (result), "=&r" (temp),					\
	  "+" GCC_OFF_SMALL_ASM() (v->counter)				\
	: "Ir" (i)							\
	: __LLSC_CLOBBER);						\
									\
	/*								\
	 * In the Loongson3 workaround case we already have a		\
	 * completion barrier at 2: above, which is needed due to the	\
	 * bltz that can branch	to code outside of the LL/SC loop. As	\
	 * such, we don't need to emit another barrier here.		\
	 */								\
	if (__SYNC_loongson3_war == 0)					\
		smp_mb__after_atomic();					\
									\
	return result;							\
}

ATOMIC_SIP_OP(atomic, int, subu, ll, sc)
#define arch_atomic_dec_if_positive(v)	arch_atomic_sub_if_positive(1, v)

#ifdef CONFIG_64BIT
ATOMIC_SIP_OP(atomic64, s64, dsubu, lld, scd)
#define arch_atomic64_dec_if_positive(v)	arch_atomic64_sub_if_positive(1, v)
#endif

#undef ATOMIC_SIP_OP

#endif /* _ASM_ATOMIC_H */
