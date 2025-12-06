/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Atomic operations.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/types.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#ifdef CONFIG_CPU_HAS_AMO
#include <asm/atomic-amo.h>
#else
#include <asm/atomic-llsc.h>
#endif

#ifdef CONFIG_GENERIC_ATOMIC64
#include <asm-generic/atomic64.h>
#endif

#if __SIZEOF_LONG__ == 4
#define __LL		"ll.w	"
#define __SC		"sc.w	"
#define __AMADD		"amadd.w	"
#define __AMOR		"amor.w		"
#define __AMAND_DB	"amand_db.w	"
#define __AMOR_DB	"amor_db.w	"
#define __AMXOR_DB	"amxor_db.w	"
#elif __SIZEOF_LONG__ == 8
#define __LL		"ll.d	"
#define __SC		"sc.d	"
#define __AMADD		"amadd.d	"
#define __AMOR		"amor.d		"
#define __AMAND_DB	"amand_db.d	"
#define __AMOR_DB	"amor_db.d	"
#define __AMXOR_DB	"amxor_db.d	"
#endif

#define ATOMIC_INIT(i)	  { (i) }

#define arch_atomic_read(v)	READ_ONCE((v)->counter)
#define arch_atomic_set(v, i)	WRITE_ONCE((v)->counter, (i))

static inline int arch_atomic_fetch_add_unless(atomic_t *v, int a, int u)
{
       int prev, rc;

	__asm__ __volatile__ (
		"0:	ll.w	%[p],  %[c]\n"
		"	beq	%[p],  %[u], 1f\n"
		"	add.w	%[rc], %[p], %[a]\n"
		"	sc.w	%[rc], %[c]\n"
		"	beqz	%[rc], 0b\n"
		"	b	2f\n"
		"1:\n"
		__WEAK_LLSC_MB
		"2:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc),
		  [c]"=ZB" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");

	return prev;
}
#define arch_atomic_fetch_add_unless arch_atomic_fetch_add_unless

static inline int arch_atomic_sub_if_positive(int i, atomic_t *v)
{
	int result;
	int temp;

	if (__builtin_constant_p(i)) {
		__asm__ __volatile__(
		"1:	ll.w	%1, %2		# atomic_sub_if_positive\n"
		"	addi.w	%0, %1, %3				\n"
		"	move	%1, %0					\n"
		"	bltz	%0, 2f					\n"
		"	sc.w	%1, %2					\n"
		"	beqz	%1, 1b					\n"
		"2:							\n"
		__WEAK_LLSC_MB
		: "=&r" (result), "=&r" (temp), "+ZC" (v->counter)
		: "I" (-i));
	} else {
		__asm__ __volatile__(
		"1:	ll.w	%1, %2		# atomic_sub_if_positive\n"
		"	sub.w	%0, %1, %3				\n"
		"	move	%1, %0					\n"
		"	bltz	%0, 2f					\n"
		"	sc.w	%1, %2					\n"
		"	beqz	%1, 1b					\n"
		"2:							\n"
		__WEAK_LLSC_MB
		: "=&r" (result), "=&r" (temp), "+ZC" (v->counter)
		: "r" (i));
	}

	return result;
}

#define arch_atomic_dec_if_positive(v)	arch_atomic_sub_if_positive(1, v)

#ifdef CONFIG_64BIT

#define ATOMIC64_INIT(i)    { (i) }

#define arch_atomic64_read(v)	READ_ONCE((v)->counter)
#define arch_atomic64_set(v, i)	WRITE_ONCE((v)->counter, (i))

static inline long arch_atomic64_fetch_add_unless(atomic64_t *v, long a, long u)
{
       long prev, rc;

	__asm__ __volatile__ (
		"0:	ll.d	%[p],  %[c]\n"
		"	beq	%[p],  %[u], 1f\n"
		"	add.d	%[rc], %[p], %[a]\n"
		"	sc.d	%[rc], %[c]\n"
		"	beqz	%[rc], 0b\n"
		"	b	2f\n"
		"1:\n"
		__WEAK_LLSC_MB
		"2:\n"
		: [p]"=&r" (prev), [rc]"=&r" (rc),
		  [c] "=ZB" (v->counter)
		: [a]"r" (a), [u]"r" (u)
		: "memory");

	return prev;
}
#define arch_atomic64_fetch_add_unless arch_atomic64_fetch_add_unless

static inline long arch_atomic64_sub_if_positive(long i, atomic64_t *v)
{
	long result;
	long temp;

	if (__builtin_constant_p(i)) {
		__asm__ __volatile__(
		"1:	ll.d	%1, %2	# atomic64_sub_if_positive	\n"
		"	addi.d	%0, %1, %3				\n"
		"	move	%1, %0					\n"
		"	bltz	%0, 2f					\n"
		"	sc.d	%1, %2					\n"
		"	beqz	%1, 1b					\n"
		"2:							\n"
		__WEAK_LLSC_MB
		: "=&r" (result), "=&r" (temp), "+ZC" (v->counter)
		: "I" (-i));
	} else {
		__asm__ __volatile__(
		"1:	ll.d	%1, %2	# atomic64_sub_if_positive	\n"
		"	sub.d	%0, %1, %3				\n"
		"	move	%1, %0					\n"
		"	bltz	%0, 2f					\n"
		"	sc.d	%1, %2					\n"
		"	beqz	%1, 1b					\n"
		"2:							\n"
		__WEAK_LLSC_MB
		: "=&r" (result), "=&r" (temp), "+ZC" (v->counter)
		: "r" (i));
	}

	return result;
}

#define arch_atomic64_dec_if_positive(v)	arch_atomic64_sub_if_positive(1, v)

#endif /* CONFIG_64BIT */

#endif /* _ASM_ATOMIC_H */
