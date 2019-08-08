/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 */
#ifndef _ASM_C6X_CMPXCHG_H
#define _ASM_C6X_CMPXCHG_H

#include <linux/irqflags.h>

/*
 * Misc. functions
 */
static inline unsigned int __xchg(unsigned int x, volatile void *ptr, int size)
{
	unsigned int tmp;
	unsigned long flags;

	local_irq_save(flags);

	switch (size) {
	case 1:
		tmp = 0;
		tmp = *((unsigned char *) ptr);
		*((unsigned char *) ptr) = (unsigned char) x;
		break;
	case 2:
		tmp = 0;
		tmp = *((unsigned short *) ptr);
		*((unsigned short *) ptr) = x;
		break;
	case 4:
		tmp = 0;
		tmp = *((unsigned int *) ptr);
		*((unsigned int *) ptr) = x;
		break;
	}
	local_irq_restore(flags);
	return tmp;
}

#define xchg(ptr, x) \
	((__typeof__(*(ptr)))__xchg((unsigned int)(x), (void *) (ptr), \
				    sizeof(*(ptr))))

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr),		\
						     (unsigned long)(o), \
						     (unsigned long)(n), \
						     sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#include <asm-generic/cmpxchg.h>

#endif /* _ASM_C6X_CMPXCHG_H */
