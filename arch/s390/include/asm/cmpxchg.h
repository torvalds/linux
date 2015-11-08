/*
 * Copyright IBM Corp. 1999, 2011
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 */

#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#include <linux/mmdebug.h>
#include <linux/types.h>
#include <linux/bug.h>

#define cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) __o = (o);					\
	__typeof__(*(ptr)) __n = (n);					\
	(__typeof__(*(ptr))) __sync_val_compare_and_swap((ptr),__o,__n);\
})

#define cmpxchg64	cmpxchg
#define cmpxchg_local	cmpxchg
#define cmpxchg64_local	cmpxchg

#define xchg(ptr, x)							\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	__typeof__(*(ptr)) __old;					\
	do {								\
		__old = *__ptr;						\
	} while (!__sync_bool_compare_and_swap(__ptr, __old, x));	\
	__old;								\
})

#define __cmpxchg_double(p1, p2, o1, o2, n1, n2)			\
({									\
	register __typeof__(*(p1)) __old1 asm("2") = (o1);		\
	register __typeof__(*(p2)) __old2 asm("3") = (o2);		\
	register __typeof__(*(p1)) __new1 asm("4") = (n1);		\
	register __typeof__(*(p2)) __new2 asm("5") = (n2);		\
	int cc;								\
	asm volatile(							\
		"	cdsg	%[old],%[new],%[ptr]\n"			\
		"	ipm	%[cc]\n"				\
		"	srl	%[cc],28"				\
		: [cc] "=d" (cc), [old] "+d" (__old1), "+d" (__old2)	\
		: [new] "d" (__new1), "d" (__new2),			\
		  [ptr] "Q" (*(p1)), "Q" (*(p2))			\
		: "memory", "cc");					\
	!cc;								\
})

#define cmpxchg_double(p1, p2, o1, o2, n1, n2)				\
({									\
	__typeof__(p1) __p1 = (p1);					\
	__typeof__(p2) __p2 = (p2);					\
	BUILD_BUG_ON(sizeof(*(p1)) != sizeof(long));			\
	BUILD_BUG_ON(sizeof(*(p2)) != sizeof(long));			\
	VM_BUG_ON((unsigned long)((__p1) + 1) != (unsigned long)(__p2));\
	__cmpxchg_double(__p1, __p2, o1, o2, n1, n2);			\
})

#define system_has_cmpxchg_double()	1

#endif /* __ASM_CMPXCHG_H */
