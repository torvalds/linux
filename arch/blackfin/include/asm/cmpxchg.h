/*
 * Copyright 2004-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ARCH_BLACKFIN_CMPXCHG__
#define __ARCH_BLACKFIN_CMPXCHG__

#ifdef CONFIG_SMP

#include <linux/linkage.h>

asmlinkage unsigned long __raw_xchg_1_asm(volatile void *ptr, unsigned long value);
asmlinkage unsigned long __raw_xchg_2_asm(volatile void *ptr, unsigned long value);
asmlinkage unsigned long __raw_xchg_4_asm(volatile void *ptr, unsigned long value);
asmlinkage unsigned long __raw_cmpxchg_1_asm(volatile void *ptr,
					unsigned long new, unsigned long old);
asmlinkage unsigned long __raw_cmpxchg_2_asm(volatile void *ptr,
					unsigned long new, unsigned long old);
asmlinkage unsigned long __raw_cmpxchg_4_asm(volatile void *ptr,
					unsigned long new, unsigned long old);

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
				   int size)
{
	unsigned long tmp;

	switch (size) {
	case 1:
		tmp = __raw_xchg_1_asm(ptr, x);
		break;
	case 2:
		tmp = __raw_xchg_2_asm(ptr, x);
		break;
	case 4:
		tmp = __raw_xchg_4_asm(ptr, x);
		break;
	}

	return tmp;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	unsigned long tmp;

	switch (size) {
	case 1:
		tmp = __raw_cmpxchg_1_asm(ptr, new, old);
		break;
	case 2:
		tmp = __raw_cmpxchg_2_asm(ptr, new, old);
		break;
	case 4:
		tmp = __raw_cmpxchg_4_asm(ptr, new, old);
		break;
	}

	return tmp;
}
#define cmpxchg(ptr, o, n) \
	((__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(o), \
		(unsigned long)(n), sizeof(*(ptr))))

#else /* !CONFIG_SMP */

#include <mach/blackfin.h>
#include <asm/irqflags.h>

struct __xchg_dummy {
	unsigned long a[100];
};
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

static inline unsigned long __xchg(unsigned long x, volatile void *ptr,
				   int size)
{
	unsigned long tmp = 0;
	unsigned long flags;

	flags = hard_local_irq_save();

	switch (size) {
	case 1:
		__asm__ __volatile__
			("%0 = b%2 (z);\n\t"
			 "b%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 2:
		__asm__ __volatile__
			("%0 = w%2 (z);\n\t"
			 "w%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	case 4:
		__asm__ __volatile__
			("%0 = %2;\n\t"
			 "%2 = %1;\n\t"
			 : "=&d" (tmp) : "d" (x), "m" (*__xg(ptr)) : "memory");
		break;
	}
	hard_local_irq_restore(flags);
	return tmp;
}

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	       \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#include <asm-generic/cmpxchg.h>

#endif /* !CONFIG_SMP */

#define xchg(ptr, x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))))
#define tas(ptr) ((void)xchg((ptr), 1))

#endif /* __ARCH_BLACKFIN_CMPXCHG__ */
