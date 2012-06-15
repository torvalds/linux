/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Do not include directly; use <linux/atomic.h>.
 */

#ifndef _ASM_TILE_ATOMIC_64_H
#define _ASM_TILE_ATOMIC_64_H

#ifndef __ASSEMBLY__

#include <asm/barrier.h>
#include <arch/spr_def.h>

/* First, the 32-bit atomic ops that are "real" on our 64-bit platform. */

#define atomic_set(v, i) ((v)->counter = (i))

/*
 * The smp_mb() operations throughout are to support the fact that
 * Linux requires memory barriers before and after the operation,
 * on any routine which updates memory and returns a value.
 */

static inline int atomic_cmpxchg(atomic_t *v, int o, int n)
{
	int val;
	__insn_mtspr(SPR_CMPEXCH_VALUE, o);
	smp_mb();  /* barrier for proper semantics */
	val = __insn_cmpexch4((void *)&v->counter, n);
	smp_mb();  /* barrier for proper semantics */
	return val;
}

static inline int atomic_xchg(atomic_t *v, int n)
{
	int val;
	smp_mb();  /* barrier for proper semantics */
	val = __insn_exch4((void *)&v->counter, n);
	smp_mb();  /* barrier for proper semantics */
	return val;
}

static inline void atomic_add(int i, atomic_t *v)
{
	__insn_fetchadd4((void *)&v->counter, i);
}

static inline int atomic_add_return(int i, atomic_t *v)
{
	int val;
	smp_mb();  /* barrier for proper semantics */
	val = __insn_fetchadd4((void *)&v->counter, i) + i;
	barrier();  /* the "+ i" above will wait on memory */
	return val;
}

static inline int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int guess, oldval = v->counter;
	do {
		if (oldval == u)
			break;
		guess = oldval;
		oldval = atomic_cmpxchg(v, guess, guess + a);
	} while (guess != oldval);
	return oldval;
}

/* Now the true 64-bit operations. */

#define ATOMIC64_INIT(i)	{ (i) }

#define atomic64_read(v)		((v)->counter)
#define atomic64_set(v, i) ((v)->counter = (i))

static inline long atomic64_cmpxchg(atomic64_t *v, long o, long n)
{
	long val;
	smp_mb();  /* barrier for proper semantics */
	__insn_mtspr(SPR_CMPEXCH_VALUE, o);
	val = __insn_cmpexch((void *)&v->counter, n);
	smp_mb();  /* barrier for proper semantics */
	return val;
}

static inline long atomic64_xchg(atomic64_t *v, long n)
{
	long val;
	smp_mb();  /* barrier for proper semantics */
	val = __insn_exch((void *)&v->counter, n);
	smp_mb();  /* barrier for proper semantics */
	return val;
}

static inline void atomic64_add(long i, atomic64_t *v)
{
	__insn_fetchadd((void *)&v->counter, i);
}

static inline long atomic64_add_return(long i, atomic64_t *v)
{
	int val;
	smp_mb();  /* barrier for proper semantics */
	val = __insn_fetchadd((void *)&v->counter, i) + i;
	barrier();  /* the "+ i" above will wait on memory */
	return val;
}

static inline long atomic64_add_unless(atomic64_t *v, long a, long u)
{
	long guess, oldval = v->counter;
	do {
		if (oldval == u)
			break;
		guess = oldval;
		oldval = atomic64_cmpxchg(v, guess, guess + a);
	} while (guess != oldval);
	return oldval != u;
}

#define atomic64_sub_return(i, v)	atomic64_add_return(-(i), (v))
#define atomic64_sub(i, v)		atomic64_add(-(i), (v))
#define atomic64_inc_return(v)		atomic64_add_return(1, (v))
#define atomic64_dec_return(v)		atomic64_sub_return(1, (v))
#define atomic64_inc(v)			atomic64_add(1, (v))
#define atomic64_dec(v)			atomic64_sub(1, (v))

#define atomic64_inc_and_test(v)	(atomic64_inc_return(v) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return(v) == 0)
#define atomic64_sub_and_test(i, v)	(atomic64_sub_return((i), (v)) == 0)
#define atomic64_add_negative(i, v)	(atomic64_add_return((i), (v)) < 0)

#define atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)

/* Atomic dec and inc don't implement barrier, so provide them if needed. */
#define smp_mb__before_atomic_dec()	smp_mb()
#define smp_mb__after_atomic_dec()	smp_mb()
#define smp_mb__before_atomic_inc()	smp_mb()
#define smp_mb__after_atomic_inc()	smp_mb()

/* Define this to indicate that cmpxchg is an efficient operation. */
#define __HAVE_ARCH_CMPXCHG

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_TILE_ATOMIC_64_H */
