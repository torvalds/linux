/* SPDX-License-Identifier: GPL-2.0 */
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

void __xchg_called_with_bad_pointer(void);

static __always_inline unsigned long __xchg(unsigned long x,
					    unsigned long address, int size)
{
	unsigned long old;
	int shift;

	switch (size) {
	case 1:
		shift = (3 ^ (address & 3)) << 3;
		address ^= address & 3;
		asm volatile(
			"       l       %0,%1\n"
			"0:     lr      0,%0\n"
			"       nr      0,%3\n"
			"       or      0,%2\n"
			"       cs      %0,0,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+Q" (*(int *) address)
			: "d" ((x & 0xff) << shift), "d" (~(0xff << shift))
			: "memory", "cc", "0");
		return old >> shift;
	case 2:
		shift = (2 ^ (address & 2)) << 3;
		address ^= address & 2;
		asm volatile(
			"       l       %0,%1\n"
			"0:     lr      0,%0\n"
			"       nr      0,%3\n"
			"       or      0,%2\n"
			"       cs      %0,0,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+Q" (*(int *) address)
			: "d" ((x & 0xffff) << shift), "d" (~(0xffff << shift))
			: "memory", "cc", "0");
		return old >> shift;
	case 4:
		asm volatile(
			"       l       %0,%1\n"
			"0:     cs      %0,%2,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+Q" (*(int *) address)
			: "d" (x)
			: "memory", "cc");
		return old;
	case 8:
		asm volatile(
			"       lg      %0,%1\n"
			"0:     csg     %0,%2,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+QS" (*(long *) address)
			: "d" (x)
			: "memory", "cc");
		return old;
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define arch_xchg(ptr, x)						\
({									\
	__typeof__(*(ptr)) __ret;					\
									\
	__ret = (__typeof__(*(ptr)))					\
		__xchg((unsigned long)(x), (unsigned long)(ptr),	\
		       sizeof(*(ptr)));					\
	__ret;								\
})

void __cmpxchg_called_with_bad_pointer(void);

static __always_inline unsigned long __cmpxchg(unsigned long address,
					       unsigned long old,
					       unsigned long new, int size)
{
	unsigned long prev, tmp;
	int shift;

	switch (size) {
	case 1:
		shift = (3 ^ (address & 3)) << 3;
		address ^= address & 3;
		asm volatile(
			"       l       %0,%2\n"
			"0:     nr      %0,%5\n"
			"       lr      %1,%0\n"
			"       or      %0,%3\n"
			"       or      %1,%4\n"
			"       cs      %0,%1,%2\n"
			"       jnl     1f\n"
			"       xr      %1,%0\n"
			"       nr      %1,%5\n"
			"       jnz     0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp), "+Q" (*(int *) address)
			: "d" ((old & 0xff) << shift),
			  "d" ((new & 0xff) << shift),
			  "d" (~(0xff << shift))
			: "memory", "cc");
		return prev >> shift;
	case 2:
		shift = (2 ^ (address & 2)) << 3;
		address ^= address & 2;
		asm volatile(
			"       l       %0,%2\n"
			"0:     nr      %0,%5\n"
			"       lr      %1,%0\n"
			"       or      %0,%3\n"
			"       or      %1,%4\n"
			"       cs      %0,%1,%2\n"
			"       jnl     1f\n"
			"       xr      %1,%0\n"
			"       nr      %1,%5\n"
			"       jnz     0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp), "+Q" (*(int *) address)
			: "d" ((old & 0xffff) << shift),
			  "d" ((new & 0xffff) << shift),
			  "d" (~(0xffff << shift))
			: "memory", "cc");
		return prev >> shift;
	case 4:
		asm volatile(
			"       cs      %0,%3,%1\n"
			: "=&d" (prev), "+Q" (*(int *) address)
			: "0" (old), "d" (new)
			: "memory", "cc");
		return prev;
	case 8:
		asm volatile(
			"       csg     %0,%3,%1\n"
			: "=&d" (prev), "+QS" (*(long *) address)
			: "0" (old), "d" (new)
			: "memory", "cc");
		return prev;
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define arch_cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) __ret;					\
									\
	__ret = (__typeof__(*(ptr)))					\
		__cmpxchg((unsigned long)(ptr), (unsigned long)(o),	\
			  (unsigned long)(n), sizeof(*(ptr)));		\
	__ret;								\
})

#define arch_cmpxchg64		arch_cmpxchg
#define arch_cmpxchg_local	arch_cmpxchg
#define arch_cmpxchg64_local	arch_cmpxchg

#define system_has_cmpxchg_double()	1

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

#define arch_cmpxchg_double(p1, p2, o1, o2, n1, n2)			\
({									\
	__typeof__(p1) __p1 = (p1);					\
	__typeof__(p2) __p2 = (p2);					\
	BUILD_BUG_ON(sizeof(*(p1)) != sizeof(long));			\
	BUILD_BUG_ON(sizeof(*(p2)) != sizeof(long));			\
	VM_BUG_ON((unsigned long)((__p1) + 1) != (unsigned long)(__p2));\
	__cmpxchg_double(__p1, __p2, o1, o2, n1, n2);			\
})

#endif /* __ASM_CMPXCHG_H */
