/* MN10300 Atomic counter operations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_ATOMIC_H
#define _ASM_ATOMIC_H

#include <asm/irqflags.h>
#include <asm/cmpxchg.h>
#include <asm/barrier.h>

#ifndef CONFIG_SMP
#include <asm-generic/atomic.h>
#else

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#define ATOMIC_INIT(i)	{ (i) }

#ifdef __KERNEL__

/**
 * atomic_read - read atomic variable
 * @v: pointer of type atomic_t
 *
 * Atomically reads the value of @v.  Note that the guaranteed
 */
#define atomic_read(v)	READ_ONCE((v)->counter)

/**
 * atomic_set - set atomic variable
 * @v: pointer of type atomic_t
 * @i: required value
 *
 * Atomically sets the value of @v to @i.  Note that the guaranteed
 */
#define atomic_set(v, i) WRITE_ONCE(((v)->counter), (i))

#define ATOMIC_OP(op)							\
static inline void atomic_##op(int i, atomic_t *v)			\
{									\
	int retval, status;						\
									\
	asm volatile(							\
		"1:	mov	%4,(_AAR,%3)	\n"			\
		"	mov	(_ADR,%3),%1	\n"			\
		"	" #op "	%5,%1		\n"			\
		"	mov	%1,(_ADR,%3)	\n"			\
		"	mov	(_ADR,%3),%0	\n"	/* flush */	\
		"	mov	(_ASR,%3),%0	\n"			\
		"	or	%0,%0		\n"			\
		"	bne	1b		\n"			\
		: "=&r"(status), "=&r"(retval), "=m"(v->counter)	\
		: "a"(ATOMIC_OPS_BASE_ADDR), "r"(&v->counter), "r"(i)	\
		: "memory", "cc");					\
}

#define ATOMIC_OP_RETURN(op)						\
static inline int atomic_##op##_return(int i, atomic_t *v)		\
{									\
	int retval, status;						\
									\
	asm volatile(							\
		"1:	mov	%4,(_AAR,%3)	\n"			\
		"	mov	(_ADR,%3),%1	\n"			\
		"	" #op "	%5,%1		\n"			\
		"	mov	%1,(_ADR,%3)	\n"			\
		"	mov	(_ADR,%3),%0	\n"	/* flush */	\
		"	mov	(_ASR,%3),%0	\n"			\
		"	or	%0,%0		\n"			\
		"	bne	1b		\n"			\
		: "=&r"(status), "=&r"(retval), "=m"(v->counter)	\
		: "a"(ATOMIC_OPS_BASE_ADDR), "r"(&v->counter), "r"(i)	\
		: "memory", "cc");					\
	return retval;							\
}

#define ATOMIC_OPS(op) ATOMIC_OP(op) ATOMIC_OP_RETURN(op)

ATOMIC_OPS(add)
ATOMIC_OPS(sub)

ATOMIC_OP(and)
ATOMIC_OP(or)
ATOMIC_OP(xor)

#undef ATOMIC_OPS
#undef ATOMIC_OP_RETURN
#undef ATOMIC_OP

static inline int atomic_add_negative(int i, atomic_t *v)
{
	return atomic_add_return(i, v) < 0;
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

#define atomic_sub_and_test(i, v)	(atomic_sub_return((i), (v)) == 0)
#define atomic_dec_and_test(v)		(atomic_sub_return(1, (v)) == 0)
#define atomic_inc_and_test(v)		(atomic_add_return(1, (v)) == 0)

#define __atomic_add_unless(v, a, u)				\
({								\
	int c, old;						\
	c = atomic_read(v);					\
	while (c != (u) && (old = atomic_cmpxchg((v), c, c + (a))) != c) \
		c = old;					\
	c;							\
})

#define atomic_xchg(ptr, v)		(xchg(&(ptr)->counter, (v)))
#define atomic_cmpxchg(v, old, new)	(cmpxchg(&((v)->counter), (old), (new)))

#endif /* __KERNEL__ */
#endif /* CONFIG_SMP */
#endif /* _ASM_ATOMIC_H */
