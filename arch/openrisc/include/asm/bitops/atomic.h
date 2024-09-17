/*
 * Copyright (C) 2014 Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_OPENRISC_BITOPS_ATOMIC_H
#define __ASM_OPENRISC_BITOPS_ATOMIC_H

static inline void set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%1)	\n"
		"	l.or	%0,%0,%2	\n"
		"	l.swa	0(%1),%0	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(tmp)
		: "r"(p), "r"(mask)
		: "cc", "memory");
}

static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%1)	\n"
		"	l.and	%0,%0,%2	\n"
		"	l.swa	0(%1),%0	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(tmp)
		: "r"(p), "r"(~mask)
		: "cc", "memory");
}

static inline void change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%1)	\n"
		"	l.xor	%0,%0,%2	\n"
		"	l.swa	0(%1),%0	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(tmp)
		: "r"(p), "r"(mask)
		: "cc", "memory");
}

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old;
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%2)	\n"
		"	l.or	%1,%0,%3	\n"
		"	l.swa	0(%2),%1	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(old), "=&r"(tmp)
		: "r"(p), "r"(mask)
		: "cc", "memory");

	return (old & mask) != 0;
}

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old;
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%2)	\n"
		"	l.and	%1,%0,%3	\n"
		"	l.swa	0(%2),%1	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(old), "=&r"(tmp)
		: "r"(p), "r"(~mask)
		: "cc", "memory");

	return (old & mask) != 0;
}

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);
	unsigned long old;
	unsigned long tmp;

	__asm__ __volatile__(
		"1:	l.lwa	%0,0(%2)	\n"
		"	l.xor	%1,%0,%3	\n"
		"	l.swa	0(%2),%1	\n"
		"	l.bnf	1b		\n"
		"	 l.nop			\n"
		: "=&r"(old), "=&r"(tmp)
		: "r"(p), "r"(mask)
		: "cc", "memory");

	return (old & mask) != 0;
}

#endif /* __ASM_OPENRISC_BITOPS_ATOMIC_H */
