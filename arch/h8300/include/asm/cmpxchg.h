/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARCH_H8300_CMPXCHG__
#define __ARCH_H8300_CMPXCHG__

#include <linux/irqflags.h>

#define xchg(ptr, x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), \
				    sizeof(*(ptr))))

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((volatile struct __xchg_dummy *)(x))

static inline unsigned long __xchg(unsigned long x,
				   volatile void *ptr, int size)
{
	unsigned long tmp, flags;

	local_irq_save(flags);

	switch (size) {
	case 1:
		__asm__ __volatile__
			("mov.b %2,%0\n\t"
			 "mov.b %1,%2"
			 : "=&r" (tmp) : "r" (x), "m" (*__xg(ptr)));
		break;
	case 2:
		__asm__ __volatile__
			("mov.w %2,%0\n\t"
			 "mov.w %1,%2"
			 : "=&r" (tmp) : "r" (x), "m" (*__xg(ptr)));
		break;
	case 4:
		__asm__ __volatile__
			("mov.l %2,%0\n\t"
			 "mov.l %1,%2"
			 : "=&r" (tmp) : "r" (x), "m" (*__xg(ptr)));
		break;
	default:
		tmp = 0;
	}
	local_irq_restore(flags);
	return tmp;
}

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					 \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr),		 \
						     (unsigned long)(o), \
						     (unsigned long)(n), \
						     sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#ifndef CONFIG_SMP
#include <asm-generic/cmpxchg.h>
#endif

#define atomic_xchg(v, new) (xchg(&((v)->counter), new))

#endif /* __ARCH_H8300_CMPXCHG__ */
