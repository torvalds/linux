/*
 *  arch/arm/include/asm/atomic.h
 *
 *  Copyright (C) 1996 Russell King.
 *  Copyright (C) 2002 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_ATOMIC_H
#define __ASM_ARM_ATOMIC_H

#include <linux/compiler.h>
#include <linux/prefetch.h>
#include <linux/types.h>
#include <linux/irqflags.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>

#define ATOMIC_INIT(i)	{ (i) }

#ifdef __KERNEL__

/*
 * On ARM, ordinary assignment (str instruction) doesn't clear the local
 * strex/ldrex monitor on some implementations. The reason we can use it for
 * atomic_set() is the clrex or dummy strex done on every exception return.
 */
#define atomic_read(v)	(*(volatile int *)&(v)->counter)
#define atomic_set(v,i)	(((v)->counter) = (i))

#if __LINUX_ARM_ARCH__ >= 6

/*
 * ARMv6 UP and SMP safe atomic ops.  We use load exclusive and
 * store exclusive to ensure that these are atomic.  We may loop
 * to ensure that the update happens.
 */
static inline void atomic_add(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	prefetchw(&v->counter);
	__asm__ __volatile__("@ atomic_add\n"
"1:	ldrex	%0, [%3]\n"
"	add	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");
}

static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	smp_mb();
	prefetchw(&v->counter);

	__asm__ __volatile__("@ atomic_add_return\n"
"1:	ldrex	%0, [%3]\n"
"	add	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline void atomic_sub(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	prefetchw(&v->counter);
	__asm__ __volatile__("@ atomic_sub\n"
"1:	ldrex	%0, [%3]\n"
"	sub	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long tmp;
	int result;

	smp_mb();
	prefetchw(&v->counter);

	__asm__ __volatile__("@ atomic_sub_return\n"
"1:	ldrex	%0, [%3]\n"
"	sub	%0, %0, %4\n"
"	strex	%1, %0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "Ir" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline int atomic_cmpxchg(atomic_t *ptr, int old, int new)
{
	int oldval;
	unsigned long res;

	smp_mb();
	prefetchw(&ptr->counter);

	do {
		__asm__ __volatile__("@ atomic_cmpxchg\n"
		"ldrex	%1, [%3]\n"
		"mov	%0, #0\n"
		"teq	%1, %4\n"
		"strexeq %0, %5, [%3]\n"
		    : "=&r" (res), "=&r" (oldval), "+Qo" (ptr->counter)
		    : "r" (&ptr->counter), "Ir" (old), "r" (new)
		    : "cc");
	} while (res);

	smp_mb();

	return oldval;
}

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int oldval, newval;
	unsigned long tmp;

	smp_mb();
	prefetchw(&v->counter);

	__asm__ __volatile__ ("@ atomic_add_unless\n"
"1:	ldrex	%0, [%4]\n"
"	teq	%0, %5\n"
"	beq	2f\n"
"	add	%1, %0, %6\n"
"	strex	%2, %1, [%4]\n"
"	teq	%2, #0\n"
"	bne	1b\n"
"2:"
	: "=&r" (oldval), "=&r" (newval), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (u), "r" (a)
	: "cc");

	if (oldval != u)
		smp_mb();

	return oldval;
}

#else /* ARM_ARCH_6 */

#ifdef CONFIG_SMP
#error SMP not supported on pre-ARMv6 CPUs
#endif

static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long flags;
	int val;

	raw_local_irq_save(flags);
	val = v->counter;
	v->counter = val += i;
	raw_local_irq_restore(flags);

	return val;
}
#define atomic_add(i, v)	(void) atomic_add_return(i, v)

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long flags;
	int val;

	raw_local_irq_save(flags);
	val = v->counter;
	v->counter = val -= i;
	raw_local_irq_restore(flags);

	return val;
}
#define atomic_sub(i, v)	(void) atomic_sub_return(i, v)

static inline int atomic_cmpxchg(atomic_t *v, int old, int new)
{
	int ret;
	unsigned long flags;

	raw_local_irq_save(flags);
	ret = v->counter;
	if (likely(ret == old))
		v->counter = new;
	raw_local_irq_restore(flags);

	return ret;
}

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;

	c = atomic_read(v);
	while (c != u && (old = atomic_cmpxchg((v), c, c + a)) != c)
		c = old;
	return c;
}

#endif /* __LINUX_ARM_ARCH__ */

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

#define atomic_inc(v)		atomic_add(1, v)
#define atomic_dec(v)		atomic_sub(1, v)

#define atomic_inc_and_test(v)	(atomic_add_return(1, v) == 0)
#define atomic_dec_and_test(v)	(atomic_sub_return(1, v) == 0)
#define atomic_inc_return(v)    (atomic_add_return(1, v))
#define atomic_dec_return(v)    (atomic_sub_return(1, v))
#define atomic_sub_and_test(i, v) (atomic_sub_return(i, v) == 0)

#define atomic_add_negative(i,v) (atomic_add_return(i, v) < 0)

#ifndef CONFIG_GENERIC_ATOMIC64
typedef struct {
	long long counter;
} atomic64_t;

#define ATOMIC64_INIT(i) { (i) }

#ifdef CONFIG_ARM_LPAE
static inline long long atomic64_read(const atomic64_t *v)
{
	long long result;

	__asm__ __volatile__("@ atomic64_read\n"
"	ldrd	%0, %H0, [%1]"
	: "=&r" (result)
	: "r" (&v->counter), "Qo" (v->counter)
	);

	return result;
}

static inline void atomic64_set(atomic64_t *v, long long i)
{
	__asm__ __volatile__("@ atomic64_set\n"
"	strd	%2, %H2, [%1]"
	: "=Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	);
}
#else
static inline long long atomic64_read(const atomic64_t *v)
{
	long long result;

	__asm__ __volatile__("@ atomic64_read\n"
"	ldrexd	%0, %H0, [%1]"
	: "=&r" (result)
	: "r" (&v->counter), "Qo" (v->counter)
	);

	return result;
}

static inline void atomic64_set(atomic64_t *v, long long i)
{
	long long tmp;

	prefetchw(&v->counter);
	__asm__ __volatile__("@ atomic64_set\n"
"1:	ldrexd	%0, %H0, [%2]\n"
"	strexd	%0, %3, %H3, [%2]\n"
"	teq	%0, #0\n"
"	bne	1b"
	: "=&r" (tmp), "=Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}
#endif

static inline void atomic64_add(long long i, atomic64_t *v)
{
	long long result;
	unsigned long tmp;

	prefetchw(&v->counter);
	__asm__ __volatile__("@ atomic64_add\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	adds	%Q0, %Q0, %Q4\n"
"	adc	%R0, %R0, %R4\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static inline long long atomic64_add_return(long long i, atomic64_t *v)
{
	long long result;
	unsigned long tmp;

	smp_mb();
	prefetchw(&v->counter);

	__asm__ __volatile__("@ atomic64_add_return\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	adds	%Q0, %Q0, %Q4\n"
"	adc	%R0, %R0, %R4\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline void atomic64_sub(long long i, atomic64_t *v)
{
	long long result;
	unsigned long tmp;

	prefetchw(&v->counter);
	__asm__ __volatile__("@ atomic64_sub\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	subs	%Q0, %Q0, %Q4\n"
"	sbc	%R0, %R0, %R4\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");
}

static inline long long atomic64_sub_return(long long i, atomic64_t *v)
{
	long long result;
	unsigned long tmp;

	smp_mb();
	prefetchw(&v->counter);

	__asm__ __volatile__("@ atomic64_sub_return\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	subs	%Q0, %Q0, %Q4\n"
"	sbc	%R0, %R0, %R4\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (i)
	: "cc");

	smp_mb();

	return result;
}

static inline long long atomic64_cmpxchg(atomic64_t *ptr, long long old,
					long long new)
{
	long long oldval;
	unsigned long res;

	smp_mb();
	prefetchw(&ptr->counter);

	do {
		__asm__ __volatile__("@ atomic64_cmpxchg\n"
		"ldrexd		%1, %H1, [%3]\n"
		"mov		%0, #0\n"
		"teq		%1, %4\n"
		"teqeq		%H1, %H4\n"
		"strexdeq	%0, %5, %H5, [%3]"
		: "=&r" (res), "=&r" (oldval), "+Qo" (ptr->counter)
		: "r" (&ptr->counter), "r" (old), "r" (new)
		: "cc");
	} while (res);

	smp_mb();

	return oldval;
}

static inline long long atomic64_xchg(atomic64_t *ptr, long long new)
{
	long long result;
	unsigned long tmp;

	smp_mb();
	prefetchw(&ptr->counter);

	__asm__ __volatile__("@ atomic64_xchg\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	strexd	%1, %4, %H4, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b"
	: "=&r" (result), "=&r" (tmp), "+Qo" (ptr->counter)
	: "r" (&ptr->counter), "r" (new)
	: "cc");

	smp_mb();

	return result;
}

static inline long long atomic64_dec_if_positive(atomic64_t *v)
{
	long long result;
	unsigned long tmp;

	smp_mb();
	prefetchw(&v->counter);

	__asm__ __volatile__("@ atomic64_dec_if_positive\n"
"1:	ldrexd	%0, %H0, [%3]\n"
"	subs	%Q0, %Q0, #1\n"
"	sbc	%R0, %R0, #0\n"
"	teq	%R0, #0\n"
"	bmi	2f\n"
"	strexd	%1, %0, %H0, [%3]\n"
"	teq	%1, #0\n"
"	bne	1b\n"
"2:"
	: "=&r" (result), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter)
	: "cc");

	smp_mb();

	return result;
}

static inline int atomic64_add_unless(atomic64_t *v, long long a, long long u)
{
	long long val;
	unsigned long tmp;
	int ret = 1;

	smp_mb();
	prefetchw(&v->counter);

	__asm__ __volatile__("@ atomic64_add_unless\n"
"1:	ldrexd	%0, %H0, [%4]\n"
"	teq	%0, %5\n"
"	teqeq	%H0, %H5\n"
"	moveq	%1, #0\n"
"	beq	2f\n"
"	adds	%Q0, %Q0, %Q6\n"
"	adc	%R0, %R0, %R6\n"
"	strexd	%2, %0, %H0, [%4]\n"
"	teq	%2, #0\n"
"	bne	1b\n"
"2:"
	: "=&r" (val), "+r" (ret), "=&r" (tmp), "+Qo" (v->counter)
	: "r" (&v->counter), "r" (u), "r" (a)
	: "cc");

	if (ret)
		smp_mb();

	return ret;
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

#endif /* !CONFIG_GENERIC_ATOMIC64 */
#endif
#endif
