/*
 * Copyright (C) 2014 Stefan Kristiansson <stefan.kristiansson@saunalahti.fi>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_OPENRISC_CMPXCHG_H
#define __ASM_OPENRISC_CMPXCHG_H

#include  <linux/types.h>

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg().
 */
extern void __cmpxchg_called_with_bad_pointer(void);

#define __HAVE_ARCH_CMPXCHG 1

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	if (size != 4) {
		__cmpxchg_called_with_bad_pointer();
		return old;
	}

	__asm__ __volatile__(
		"1:	l.lwa %0, 0(%1)		\n"
		"	l.sfeq %0, %2		\n"
		"	l.bnf 2f		\n"
		"	 l.nop			\n"
		"	l.swa 0(%1), %3		\n"
		"	l.bnf 1b		\n"
		"	 l.nop			\n"
		"2:				\n"
		: "=&r"(old)
		: "r"(ptr), "r"(old), "r"(new)
		: "cc", "memory");

	return old;
}

#define cmpxchg(ptr, o, n)						\
	({								\
		(__typeof__(*(ptr))) __cmpxchg((ptr),			\
					       (unsigned long)(o),	\
					       (unsigned long)(n),	\
					       sizeof(*(ptr)));		\
	})

/*
 * This function doesn't exist, so you'll get a linker error if
 * something tries to do an invalidly-sized xchg().
 */
extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long val, volatile void *ptr,
				   int size)
{
	if (size != 4) {
		__xchg_called_with_bad_pointer();
		return val;
	}

	__asm__ __volatile__(
		"1:	l.lwa %0, 0(%1)		\n"
		"	l.swa 0(%1), %2		\n"
		"	l.bnf 1b		\n"
		"	 l.nop			\n"
		: "=&r"(val)
		: "r"(ptr), "r"(val)
		: "cc", "memory");

	return val;
}

#define xchg(ptr, with) 						\
	({								\
		(__typeof__(*(ptr))) __xchg((unsigned long)(with),	\
					    (ptr),			\
					    sizeof(*(ptr)));		\
	})

#endif /* __ASM_OPENRISC_CMPXCHG_H */
