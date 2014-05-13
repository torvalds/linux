/*
 * Copyright (C) 2014 Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_OPENRISC_ATOMIC_H
#define __ASM_OPENRISC_ATOMIC_H

#include <linux/types.h>

static inline int atomic_add_return(int i, atomic_t *v)
{
	int tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%1)	\n"
		"	l.add	%0,%0,%2	\n"
		"	l.swa	0(%1),%0	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(tmp)
		: "r"(&v->counter), "r"(i)
		: "cc", "memory");

	return tmp;
}

static inline int atomic_sub_return(int i, atomic_t *v)
{
	int tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%1)	\n"
		"	l.sub	%0,%0,%2	\n"
		"	l.swa	0(%1),%0	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(tmp)
		: "r"(&v->counter), "r"(i)
		: "cc", "memory");

	return tmp;
}

#define atomic_add_return	atomic_add_return
#define atomic_sub_return	atomic_sub_return

#include <asm-generic/atomic.h>

#endif /* __ASM_OPENRISC_ATOMIC_H */
