/* atomic.h: atomic operation emulation for FR-V
 *
 * For an explanation of how atomic ops work in this arch, see:
 *   Documentation/frv/atomic-ops.txt
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <linux/types.h>
#include <asm/spr-regs.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#ifdef CONFIG_SMP
#error not SMP safe
#endif

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 *
 * We do not have SMP systems, so we don't have to deal with that.
 */

#define ATOMIC_INIT(i)		{ (i) }
#define atomic_read(v)		(*(volatile int *)&(v)->counter)
#define atomic_set(v, i)	(((v)->counter) = (i))

#ifndef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS
static inline int atomic_add_return(int i, atomic_t *v)
{
	unsigned long val;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	add%I2		%1,%2,%1		\n"
	    "	cst.p		%1,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    : "+U"(v->counter), "=&r"(val)
	    : "NPr"(i)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	return val;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	unsigned long val;

	asm("0:						\n"
	    "	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
	    "	ckeq		icc3,cc7		\n"
	    "	ld.p		%M0,%1			\n"	/* LD.P/ORCR must be atomic */
	    "	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
	    "	sub%I2		%1,%2,%1		\n"
	    "	cst.p		%1,%M0		,cc3,#1	\n"
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* clear ICC3.Z if store happens */
	    "	beq		icc3,#0,0b		\n"
	    : "+U"(v->counter), "=&r"(val)
	    : "NPr"(i)
	    : "memory", "cc7", "cc3", "icc3"
	    );

	return val;
}

#else

extern int atomic_add_return(int i, atomic_t *v);
extern int atomic_sub_return(int i, atomic_t *v);

#endif

static inline int atomic_add_negative(int i, atomic_t *v)
{
	return atomic_add_return(i, v) < 0;
}

static inline void atomic_add(int i, atomic_t *v)
{
	atomic_add_return(i, v);
}

static inline void atomic_sub(int i, atomic_t *v)
{
	atomic_sub_return(i, v);
}

static inline void atomic_inc(atomic_t *v)
{
	atomic_add_return(1, v);
}

static inline void atomic_dec(atomic_t *v)
{
	atomic_sub_return(1, v);
}

#define atomic_dec_return(v)		atomic_sub_return(1, (v))
#define atomic_inc_return(v)		atomic_add_return(1, (v))

#define atomic_sub_and_test(i,v)	(atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v)		(atomic_sub_return(1, (v)) == 0)
#define atomic_inc_and_test(v)		(atomic_add_return(1, (v)) == 0)

/*
 * 64-bit atomic ops
 */
typedef struct {
	volatile long long counter;
} atomic64_t;

#define ATOMIC64_INIT(i)	{ (i) }

static inline long long atomic64_read(atomic64_t *v)
{
	long long counter;

	asm("ldd%I1 %M1,%0"
	    : "=e"(counter)
	    : "m"(v->counter));
	return counter;
}

static inline void atomic64_set(atomic64_t *v, long long i)
{
	asm volatile("std%I0 %1,%M0"
		     : "=m"(v->counter)
		     : "e"(i));
}

extern long long atomic64_inc_return(atomic64_t *v);
extern long long atomic64_dec_return(atomic64_t *v);
extern long long atomic64_add_return(long long i, atomic64_t *v);
extern long long atomic64_sub_return(long long i, atomic64_t *v);

static inline long long atomic64_add_negative(long long i, atomic64_t *v)
{
	return atomic64_add_return(i, v) < 0;
}

static inline void atomic64_add(long long i, atomic64_t *v)
{
	atomic64_add_return(i, v);
}

static inline void atomic64_sub(long long i, atomic64_t *v)
{
	atomic64_sub_return(i, v);
}

static inline void atomic64_inc(atomic64_t *v)
{
	atomic64_inc_return(v);
}

static inline void atomic64_dec(atomic64_t *v)
{
	atomic64_dec_return(v);
}

#define atomic64_sub_and_test(i,v)	(atomic64_sub_return((i), (v)) == 0)
#define atomic64_dec_and_test(v)	(atomic64_dec_return((v)) == 0)
#define atomic64_inc_and_test(v)	(atomic64_inc_return((v)) == 0)

#define atomic_cmpxchg(v, old, new)	(cmpxchg(&(v)->counter, old, new))
#define atomic_xchg(v, new)		(xchg(&(v)->counter, new))
#define atomic64_cmpxchg(v, old, new)	(__cmpxchg_64(old, new, &(v)->counter))
#define atomic64_xchg(v, new)		(__xchg_64(new, &(v)->counter))

static __inline__ int __atomic_add_unless(atomic_t *v, int a, int u)
{
	int c, old;
	c = atomic_read(v);
	for (;;) {
		if (unlikely(c == (u)))
			break;
		old = atomic_cmpxchg((v), c, c + (a));
		if (likely(old == c))
			break;
		c = old;
	}
	return c;
}


#endif /* _ASM_ATOMIC_H */
