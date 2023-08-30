/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_CMPXCHG_H
#define __ASM_SH_CMPXCHG_H

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

#include <linux/compiler.h>
#include <linux/types.h>

#if defined(CONFIG_GUSA_RB)
#include <asm/cmpxchg-grb.h>
#elif defined(CONFIG_CPU_SH4A)
#include <asm/cmpxchg-llsc.h>
#elif defined(CONFIG_CPU_J2) && defined(CONFIG_SMP)
#include <asm/cmpxchg-cas.h>
#else
#include <asm/cmpxchg-irq.h>
#endif

extern void __xchg_called_with_bad_pointer(void);

#define __arch_xchg(ptr, x, size)				\
({							\
	unsigned long __xchg__res;			\
	volatile void *__xchg_ptr = (ptr);		\
	switch (size) {					\
	case 4:						\
		__xchg__res = xchg_u32(__xchg_ptr, x);	\
		break;					\
	case 2:						\
		__xchg__res = xchg_u16(__xchg_ptr, x);	\
		break;					\
	case 1:						\
		__xchg__res = xchg_u8(__xchg_ptr, x);	\
		break;					\
	default:					\
		__xchg_called_with_bad_pointer();	\
		__xchg__res = x;			\
		break;					\
	}						\
							\
	__xchg__res;					\
})

#define arch_xchg(ptr,x)	\
	((__typeof__(*(ptr)))__arch_xchg((ptr),(unsigned long)(x), sizeof(*(ptr))))

/* This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg(). */
extern void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long __cmpxchg(volatile void * ptr, unsigned long old,
		unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define arch_cmpxchg(ptr,o,n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#endif /* __ASM_SH_CMPXCHG_H */
