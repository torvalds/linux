/*
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_CMPXCHG_H
#define _ASM_NIOS2_CMPXCHG_H

#include <linux/irqflags.h>

#define xchg(ptr, x)	\
	((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x)		((volatile struct __xchg_dummy *)(x))

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
					int size)
{
	unsigned long tmp, flags;

	local_irq_save(flags);

	switch (size) {
	case 1:
		__asm__ __volatile__(
			"ldb	%0, %2\n"
			"stb	%1, %2\n"
			: "=&r" (tmp)
			: "r" (x), "m" (*__xg(ptr))
			: "memory");
		break;
	case 2:
		__asm__ __volatile__(
			"ldh	%0, %2\n"
			"sth	%1, %2\n"
			: "=&r" (tmp)
			: "r" (x), "m" (*__xg(ptr))
			: "memory");
		break;
	case 4:
		__asm__ __volatile__(
			"ldw	%0, %2\n"
			"stw	%1, %2\n"
			: "=&r" (tmp)
			: "r" (x), "m" (*__xg(ptr))
			: "memory");
		break;
	}

	local_irq_restore(flags);
	return tmp;
}

#include <asm-generic/cmpxchg.h>
#include <asm-generic/cmpxchg-local.h>

#endif /* _ASM_NIOS2_CMPXCHG_H */
