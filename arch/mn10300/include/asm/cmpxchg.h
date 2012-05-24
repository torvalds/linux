/* MN10300 Atomic xchg/cmpxchg operations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_CMPXCHG_H
#define _ASM_CMPXCHG_H

#include <asm/irqflags.h>

#ifdef CONFIG_SMP
#ifdef CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT
static inline
unsigned long __xchg(volatile unsigned long *m, unsigned long val)
{
	unsigned long status;
	unsigned long oldval;

	asm volatile(
		"1:	mov	%4,(_AAR,%3)	\n"
		"	mov	(_ADR,%3),%1	\n"
		"	mov	%5,(_ADR,%3)	\n"
		"	mov	(_ADR,%3),%0	\n"	/* flush */
		"	mov	(_ASR,%3),%0	\n"
		"	or	%0,%0		\n"
		"	bne	1b		\n"
		: "=&r"(status), "=&r"(oldval), "=m"(*m)
		: "a"(ATOMIC_OPS_BASE_ADDR), "r"(m), "r"(val)
		: "memory", "cc");

	return oldval;
}

static inline unsigned long __cmpxchg(volatile unsigned long *m,
				      unsigned long old, unsigned long new)
{
	unsigned long status;
	unsigned long oldval;

	asm volatile(
		"1:	mov	%4,(_AAR,%3)	\n"
		"	mov	(_ADR,%3),%1	\n"
		"	cmp	%5,%1		\n"
		"	bne	2f		\n"
		"	mov	%6,(_ADR,%3)	\n"
		"2:	mov	(_ADR,%3),%0	\n"	/* flush */
		"	mov	(_ASR,%3),%0	\n"
		"	or	%0,%0		\n"
		"	bne	1b		\n"
		: "=&r"(status), "=&r"(oldval), "=m"(*m)
		: "a"(ATOMIC_OPS_BASE_ADDR), "r"(m),
		  "r"(old), "r"(new)
		: "memory", "cc");

	return oldval;
}
#else  /* CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT */
#error "No SMP atomic operation support!"
#endif /* CONFIG_MN10300_HAS_ATOMIC_OPS_UNIT */

#else  /* CONFIG_SMP */

/*
 * Emulate xchg for non-SMP MN10300
 */
struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))

static inline
unsigned long __xchg(volatile unsigned long *m, unsigned long val)
{
	unsigned long oldval;
	unsigned long flags;

	flags = arch_local_cli_save();
	oldval = *m;
	*m = val;
	arch_local_irq_restore(flags);
	return oldval;
}

/*
 * Emulate cmpxchg for non-SMP MN10300
 */
static inline unsigned long __cmpxchg(volatile unsigned long *m,
				      unsigned long old, unsigned long new)
{
	unsigned long oldval;
	unsigned long flags;

	flags = arch_local_cli_save();
	oldval = *m;
	if (oldval == old)
		*m = new;
	arch_local_irq_restore(flags);
	return oldval;
}

#endif /* CONFIG_SMP */

#define xchg(ptr, v)						\
	((__typeof__(*(ptr))) __xchg((unsigned long *)(ptr),	\
				     (unsigned long)(v)))

#define cmpxchg(ptr, o, n)					\
	((__typeof__(*(ptr))) __cmpxchg((unsigned long *)(ptr), \
					(unsigned long)(o),	\
					(unsigned long)(n)))

#endif /* _ASM_CMPXCHG_H */
