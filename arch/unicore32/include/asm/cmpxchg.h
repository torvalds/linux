/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Atomics xchg/cmpxchg for PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2012 GUAN Xue-tao
 */
#ifndef __UNICORE_CMPXCHG_H__
#define __UNICORE_CMPXCHG_H__

/*
 * Generate a link failure on undefined symbol if the pointer points to a value
 * of unsupported size.
 */
extern void __xchg_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
		int size)
{
	unsigned long ret;

	switch (size) {
	case 1:
		asm volatile("swapb	%0, %1, [%2]"
			: "=&r" (ret)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	case 4:
		asm volatile("swapw	%0, %1, [%2]"
			: "=&r" (ret)
			: "r" (x), "r" (ptr)
			: "memory", "cc");
		break;
	default:
		__xchg_bad_pointer();
	}

	return ret;
}

#define xchg(ptr, x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))))

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					\
		((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr),	\
		(unsigned long)(o), (unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n)					\
		__cmpxchg64_local_generic((ptr), (o), (n))

#include <asm-generic/cmpxchg.h>

#endif /* __UNICORE_CMPXCHG_H__ */
