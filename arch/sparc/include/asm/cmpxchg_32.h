/* 32-bit atomic xchg() and cmpxchg() definitions.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 2000 Anton Blanchard (anton@linuxcare.com.au)
 * Copyright (C) 2007 Kyle McMartin (kyle@parisc-linux.org)
 *
 * Additions by Keith M Wesolowski (wesolows@foobazco.org) based
 * on asm-parisc/atomic.h Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>.
 */

#ifndef __ARCH_SPARC_CMPXCHG__
#define __ARCH_SPARC_CMPXCHG__

static inline unsigned long xchg_u32(__volatile__ unsigned long *m, unsigned long val)
{
	__asm__ __volatile__("swap [%2], %0"
			     : "=&r" (val)
			     : "0" (val), "r" (m)
			     : "memory");
	return val;
}

extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, __volatile__ void * ptr, int size)
{
	switch (size) {
	case 4:
		return xchg_u32(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

/* Emulate cmpxchg() the same way we emulate atomics,
 * by hashing the object address and indexing into an array
 * of spinlocks to get a bit of performance...
 *
 * See arch/sparc/lib/atomic32.c for implementation.
 *
 * Cribbed from <asm-parisc/atomic.h>
 */
#define __HAVE_ARCH_CMPXCHG	1

/* bug catcher for when unsupported size is used - won't link */
extern void __cmpxchg_called_with_bad_pointer(void);
/* we only need to support cmpxchg of a u32 on sparc */
extern unsigned long __cmpxchg_u32(volatile u32 *m, u32 old, u32 new_);

/* don't worry...optimizer will get rid of most of this */
static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new_, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32((u32 *)ptr, (u32)old, (u32)new_);
	default:
		__cmpxchg_called_with_bad_pointer();
		break;
	}
	return old;
}

#define cmpxchg(ptr, o, n)						\
({									\
	__typeof__(*(ptr)) _o_ = (o);					\
	__typeof__(*(ptr)) _n_ = (n);					\
	(__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,	\
			(unsigned long)_n_, sizeof(*(ptr)));		\
})

#include <asm-generic/cmpxchg-local.h>

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	       \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#endif /* __ARCH_SPARC_CMPXCHG__ */
