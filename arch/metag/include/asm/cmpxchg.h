/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_METAG_CMPXCHG_H
#define __ASM_METAG_CMPXCHG_H

#include <asm/barrier.h>

#if defined(CONFIG_METAG_ATOMICITY_IRQSOFF)
#include <asm/cmpxchg_irq.h>
#elif defined(CONFIG_METAG_ATOMICITY_LOCK1)
#include <asm/cmpxchg_lock1.h>
#elif defined(CONFIG_METAG_ATOMICITY_LNKGET)
#include <asm/cmpxchg_lnkget.h>
#endif

extern void __xchg_called_with_bad_pointer(void);

#define __xchg(ptr, x, size)				\
({							\
	unsigned long __xchg__res;			\
	volatile void *__xchg_ptr = (ptr);		\
	switch (size) {					\
	case 4:						\
		__xchg__res = xchg_u32(__xchg_ptr, x);	\
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

#define xchg(ptr, x)	\
	((__typeof__(*(ptr)))__xchg((ptr), (unsigned long)(x), sizeof(*(ptr))))

/* This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg(). */
extern void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long __cmpxchg(volatile void *ptr, unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, o, n)						\
	({								\
		__typeof__(*(ptr)) _o_ = (o);				\
		__typeof__(*(ptr)) _n_ = (n);				\
		(__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_, \
					       (unsigned long)_n_,	\
					       sizeof(*(ptr)));		\
	})

#endif /* __ASM_METAG_CMPXCHG_H */
