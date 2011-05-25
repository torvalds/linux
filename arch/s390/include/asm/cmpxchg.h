/*
 * Copyright IBM Corp. 1999, 2011
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 */

#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#include <linux/types.h>

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, void *ptr, int size)
{
	unsigned long addr, old;
	int shift;

	switch (size) {
	case 1:
		addr = (unsigned long) ptr;
		shift = (3 ^ (addr & 3)) << 3;
		addr ^= addr & 3;
		asm volatile(
			"	l	%0,%4\n"
			"0:	lr	0,%0\n"
			"	nr	0,%3\n"
			"	or	0,%2\n"
			"	cs	%0,0,%4\n"
			"	jl	0b\n"
			: "=&d" (old), "=Q" (*(int *) addr)
			: "d" (x << shift), "d" (~(255 << shift)),
			  "Q" (*(int *) addr) : "memory", "cc", "0");
		return old >> shift;
	case 2:
		addr = (unsigned long) ptr;
		shift = (2 ^ (addr & 2)) << 3;
		addr ^= addr & 2;
		asm volatile(
			"	l	%0,%4\n"
			"0:	lr	0,%0\n"
			"	nr	0,%3\n"
			"	or	0,%2\n"
			"	cs	%0,0,%4\n"
			"	jl	0b\n"
			: "=&d" (old), "=Q" (*(int *) addr)
			: "d" (x << shift), "d" (~(65535 << shift)),
			  "Q" (*(int *) addr) : "memory", "cc", "0");
		return old >> shift;
	case 4:
		asm volatile(
			"	l	%0,%3\n"
			"0:	cs	%0,%2,%3\n"
			"	jl	0b\n"
			: "=&d" (old), "=Q" (*(int *) ptr)
			: "d" (x), "Q" (*(int *) ptr)
			: "memory", "cc");
		return old;
#ifdef CONFIG_64BIT
	case 8:
		asm volatile(
			"	lg	%0,%3\n"
			"0:	csg	%0,%2,%3\n"
			"	jl	0b\n"
			: "=&d" (old), "=m" (*(long *) ptr)
			: "d" (x), "Q" (*(long *) ptr)
			: "memory", "cc");
		return old;
#endif /* CONFIG_64BIT */
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr, x)							  \
({									  \
	__typeof__(*(ptr)) __ret;					  \
	__ret = (__typeof__(*(ptr)))					  \
		__xchg((unsigned long)(x), (void *)(ptr), sizeof(*(ptr)));\
	__ret;								  \
})

/*
 * Atomic compare and exchange.	 Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.	Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG

extern void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long __cmpxchg(void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long addr, prev, tmp;
	int shift;

	switch (size) {
	case 1:
		addr = (unsigned long) ptr;
		shift = (3 ^ (addr & 3)) << 3;
		addr ^= addr & 3;
		asm volatile(
			"	l	%0,%2\n"
			"0:	nr	%0,%5\n"
			"	lr	%1,%0\n"
			"	or	%0,%3\n"
			"	or	%1,%4\n"
			"	cs	%0,%1,%2\n"
			"	jnl	1f\n"
			"	xr	%1,%0\n"
			"	nr	%1,%5\n"
			"	jnz	0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp), "=Q" (*(int *) ptr)
			: "d" (old << shift), "d" (new << shift),
			  "d" (~(255 << shift)), "Q" (*(int *) ptr)
			: "memory", "cc");
		return prev >> shift;
	case 2:
		addr = (unsigned long) ptr;
		shift = (2 ^ (addr & 2)) << 3;
		addr ^= addr & 2;
		asm volatile(
			"	l	%0,%2\n"
			"0:	nr	%0,%5\n"
			"	lr	%1,%0\n"
			"	or	%0,%3\n"
			"	or	%1,%4\n"
			"	cs	%0,%1,%2\n"
			"	jnl	1f\n"
			"	xr	%1,%0\n"
			"	nr	%1,%5\n"
			"	jnz	0b\n"
			"1:"
			: "=&d" (prev), "=&d" (tmp), "=Q" (*(int *) ptr)
			: "d" (old << shift), "d" (new << shift),
			  "d" (~(65535 << shift)), "Q" (*(int *) ptr)
			: "memory", "cc");
		return prev >> shift;
	case 4:
		asm volatile(
			"	cs	%0,%3,%1\n"
			: "=&d" (prev), "=Q" (*(int *) ptr)
			: "0" (old), "d" (new), "Q" (*(int *) ptr)
			: "memory", "cc");
		return prev;
#ifdef CONFIG_64BIT
	case 8:
		asm volatile(
			"	csg	%0,%3,%1\n"
			: "=&d" (prev), "=Q" (*(long *) ptr)
			: "0" (old), "d" (new), "Q" (*(long *) ptr)
			: "memory", "cc");
		return prev;
#endif /* CONFIG_64BIT */
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, o, n)						\
	((__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(o),	\
				       (unsigned long)(n), sizeof(*(ptr))))

#ifdef CONFIG_64BIT
#define cmpxchg64(ptr, o, n)						\
({									\
	cmpxchg((ptr), (o), (n));					\
})
#else /* CONFIG_64BIT */
static inline unsigned long long __cmpxchg64(void *ptr,
					     unsigned long long old,
					     unsigned long long new)
{
	register_pair rp_old = {.pair = old};
	register_pair rp_new = {.pair = new};

	asm volatile(
		"	cds	%0,%2,%1"
		: "+&d" (rp_old), "=Q" (ptr)
		: "d" (rp_new), "Q" (ptr)
		: "cc");
	return rp_old.pair;
}
#define cmpxchg64(ptr, o, n)						\
	((__typeof__(*(ptr)))__cmpxchg64((ptr),				\
					 (unsigned long long)(o),	\
					 (unsigned long long)(n)))
#endif /* CONFIG_64BIT */

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(void *ptr,
					    unsigned long old,
					    unsigned long new, int size)
{
	switch (size) {
	case 1:
	case 2:
	case 4:
#ifdef CONFIG_64BIT
	case 8:
#endif
		return __cmpxchg(ptr, old, new, size);
	default:
		return __cmpxchg_local_generic(ptr, old, new, size);
	}

	return old;
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(o),	\
			(unsigned long)(n), sizeof(*(ptr))))

#define cmpxchg64_local(ptr, o, n)	cmpxchg64((ptr), (o), (n))

#endif /* __ASM_CMPXCHG_H */
