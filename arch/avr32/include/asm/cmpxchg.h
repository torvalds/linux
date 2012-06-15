/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc.
 *
 * But use these as seldom as possible since they are slower than
 * regular operations.
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_CMPXCHG_H
#define __ASM_AVR32_CMPXCHG_H

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long xchg_u32(u32 val, volatile u32 *m)
{
	u32 ret;

	asm volatile("xchg %[ret], %[m], %[val]"
			: [ret] "=&r"(ret), "=m"(*m)
			: "m"(*m), [m] "r"(m), [val] "r"(val)
			: "memory");
	return ret;
}

static inline unsigned long __xchg(unsigned long x,
				       volatile void *ptr,
				       int size)
{
	switch(size) {
	case 4:
		return xchg_u32(x, ptr);
	default:
		__xchg_called_with_bad_pointer();
		return x;
	}
}

static inline unsigned long __cmpxchg_u32(volatile int *m, unsigned long old,
					  unsigned long new)
{
	__u32 ret;

	asm volatile(
		"1:	ssrf	5\n"
		"	ld.w	%[ret], %[m]\n"
		"	cp.w	%[ret], %[old]\n"
		"	brne	2f\n"
		"	stcond	%[m], %[new]\n"
		"	brne	1b\n"
		"2:\n"
		: [ret] "=&r"(ret), [m] "=m"(*m)
		: "m"(m), [old] "ir"(old), [new] "r"(new)
		: "memory", "cc");
	return ret;
}

extern unsigned long __cmpxchg_u64_unsupported_on_32bit_kernels(
        volatile int * m, unsigned long old, unsigned long new);
#define __cmpxchg_u64 __cmpxchg_u64_unsupported_on_32bit_kernels

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

#define __HAVE_ARCH_CMPXCHG 1

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	case 8:
		return __cmpxchg_u64(ptr, old, new);
	}

	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, old, new)					\
	((typeof(*(ptr)))__cmpxchg((ptr), (unsigned long)(old),	\
				   (unsigned long)(new),	\
				   sizeof(*(ptr))))

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	default:
		return __cmpxchg_local_generic(ptr, old, new, size);
	}

	return old;
}

#define cmpxchg_local(ptr, old, new)					\
	((typeof(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(old),	\
				   (unsigned long)(new),		\
				   sizeof(*(ptr))))

#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#endif /* __ASM_AVR32_CMPXCHG_H */
