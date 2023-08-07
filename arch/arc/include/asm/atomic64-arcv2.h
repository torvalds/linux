/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * ARCv2 supports 64-bit exclusive load (LLOCKD) / store (SCONDD)
 *  - The address HAS to be 64-bit aligned
 */

#ifndef _ASM_ARC_ATOMIC64_ARCV2_H
#define _ASM_ARC_ATOMIC64_ARCV2_H

typedef struct {
	s64 __aligned(8) counter;
} atomic64_t;

#define ATOMIC64_INIT(a) { (a) }

static inline s64 arch_atomic64_read(const atomic64_t *v)
{
	s64 val;

	__asm__ __volatile__(
	"	ldd   %0, [%1]	\n"
	: "=r"(val)
	: "r"(&v->counter));

	return val;
}

static inline void arch_atomic64_set(atomic64_t *v, s64 a)
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
static inline void arch_atomic64_##op(s64 a, atomic64_t *v)		\
{									\
	s64 val;							\
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
	: "cc");							\
}									\

#define ATOMIC64_OP_RETURN(op, op1, op2)		        	\
static inline s64 arch_atomic64_##op##_return_relaxed(s64 a, atomic64_t *v)	\
{									\
	s64 val;							\
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
	return val;							\
}

#define arch_atomic64_add_return_relaxed	arch_atomic64_add_return_relaxed
#define arch_atomic64_sub_return_relaxed	arch_atomic64_sub_return_relaxed

#define ATOMIC64_FETCH_OP(op, op1, op2)		        		\
static inline s64 arch_atomic64_fetch_##op##_relaxed(s64 a, atomic64_t *v)	\
{									\
	s64 val, orig;							\
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
	return orig;							\
}

#define arch_atomic64_fetch_add_relaxed		arch_atomic64_fetch_add_relaxed
#define arch_atomic64_fetch_sub_relaxed		arch_atomic64_fetch_sub_relaxed

#define arch_atomic64_fetch_and_relaxed		arch_atomic64_fetch_and_relaxed
#define arch_atomic64_fetch_andnot_relaxed	arch_atomic64_fetch_andnot_relaxed
#define arch_atomic64_fetch_or_relaxed		arch_atomic64_fetch_or_relaxed
#define arch_atomic64_fetch_xor_relaxed		arch_atomic64_fetch_xor_relaxed

#define ATOMIC64_OPS(op, op1, op2)					\
	ATOMIC64_OP(op, op1, op2)					\
	ATOMIC64_OP_RETURN(op, op1, op2)				\
	ATOMIC64_FETCH_OP(op, op1, op2)

ATOMIC64_OPS(add, add.f, adc)
ATOMIC64_OPS(sub, sub.f, sbc)

#undef ATOMIC64_OPS
#define ATOMIC64_OPS(op, op1, op2)					\
	ATOMIC64_OP(op, op1, op2)					\
	ATOMIC64_FETCH_OP(op, op1, op2)

ATOMIC64_OPS(and, and, and)
ATOMIC64_OPS(andnot, bic, bic)
ATOMIC64_OPS(or, or, or)
ATOMIC64_OPS(xor, xor, xor)

#define arch_atomic64_andnot		arch_atomic64_andnot

#undef ATOMIC64_OPS
#undef ATOMIC64_FETCH_OP
#undef ATOMIC64_OP_RETURN
#undef ATOMIC64_OP

static inline s64
arch_atomic64_cmpxchg(atomic64_t *ptr, s64 expected, s64 new)
{
	s64 prev;

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
#define arch_atomic64_cmpxchg arch_atomic64_cmpxchg

static inline s64 arch_atomic64_xchg(atomic64_t *ptr, s64 new)
{
	s64 prev;

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
#define arch_atomic64_xchg arch_atomic64_xchg

static inline s64 arch_atomic64_dec_if_positive(atomic64_t *v)
{
	s64 val;

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
#define arch_atomic64_dec_if_positive arch_atomic64_dec_if_positive

static inline s64 arch_atomic64_fetch_add_unless(atomic64_t *v, s64 a, s64 u)
{
	s64 old, temp;

	smp_mb();

	__asm__ __volatile__(
	"1:	llockd  %0, [%2]	\n"
	"	brne	%L0, %L4, 2f	# continue to add since v != u \n"
	"	breq.d	%H0, %H4, 3f	# return since v == u \n"
	"2:				\n"
	"	add.f   %L1, %L0, %L3	\n"
	"	adc     %H1, %H0, %H3	\n"
	"	scondd  %1, [%2]	\n"
	"	bnz     1b		\n"
	"3:				\n"
	: "=&r"(old), "=&r" (temp)
	: "r"(&v->counter), "r"(a), "r"(u)
	: "cc");	/* memory clobber comes from smp_mb() */

	smp_mb();

	return old;
}
#define arch_atomic64_fetch_add_unless arch_atomic64_fetch_add_unless

#endif
