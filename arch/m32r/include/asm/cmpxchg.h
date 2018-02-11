/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_M32R_CMPXCHG_H
#define _ASM_M32R_CMPXCHG_H

/*
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/irqflags.h>
#include <asm/assembler.h>
#include <asm/dcache_clear.h>

extern void  __xchg_called_with_bad_pointer(void);

static __always_inline unsigned long
__xchg(unsigned long x, volatile void *ptr, int size)
{
	unsigned long flags;
	unsigned long tmp = 0;

	local_irq_save(flags);

	switch (size) {
#ifndef CONFIG_SMP
	case 1:
		__asm__ __volatile__ (
			"ldb	%0, @%2 \n\t"
			"stb	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
	case 2:
		__asm__ __volatile__ (
			"ldh	%0, @%2 \n\t"
			"sth	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
	case 4:
		__asm__ __volatile__ (
			"ld	%0, @%2 \n\t"
			"st	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
#else  /* CONFIG_SMP */
	case 4:
		__asm__ __volatile__ (
			DCACHE_CLEAR("%0", "r4", "%2")
			"lock	%0, @%2;	\n\t"
			"unlock	%1, @%2;	\n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr)
			: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
			, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
		);
		break;
#endif  /* CONFIG_SMP */
	default:
		__xchg_called_with_bad_pointer();
	}

	local_irq_restore(flags);

	return (tmp);
}

#define xchg(ptr, x) ({							\
	((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr),		\
				    sizeof(*(ptr))));			\
})

static __always_inline unsigned long
__xchg_local(unsigned long x, volatile void *ptr, int size)
{
	unsigned long flags;
	unsigned long tmp = 0;

	local_irq_save(flags);

	switch (size) {
	case 1:
		__asm__ __volatile__ (
			"ldb	%0, @%2 \n\t"
			"stb	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
	case 2:
		__asm__ __volatile__ (
			"ldh	%0, @%2 \n\t"
			"sth	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
	case 4:
		__asm__ __volatile__ (
			"ld	%0, @%2 \n\t"
			"st	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
	default:
		__xchg_called_with_bad_pointer();
	}

	local_irq_restore(flags);

	return (tmp);
}

#define xchg_local(ptr, x)						\
	((__typeof__(*(ptr)))__xchg_local((unsigned long)(x), (ptr),	\
			sizeof(*(ptr))))

static inline unsigned long
__cmpxchg_u32(volatile unsigned int *p, unsigned int old, unsigned int new)
{
	unsigned long flags;
	unsigned int retval;

	local_irq_save(flags);
	__asm__ __volatile__ (
			DCACHE_CLEAR("%0", "r4", "%1")
			M32R_LOCK" %0, @%1;	\n"
		"	bne	%0, %2, 1f;	\n"
			M32R_UNLOCK" %3, @%1;	\n"
		"	bra	2f;		\n"
                "       .fillinsn		\n"
		"1:"
			M32R_UNLOCK" %0, @%1;	\n"
                "       .fillinsn		\n"
		"2:"
			: "=&r" (retval)
			: "r" (p), "r" (old), "r" (new)
			: "cbit", "memory"
#ifdef CONFIG_CHIP_M32700_TS1
			, "r4"
#endif  /* CONFIG_CHIP_M32700_TS1 */
		);
	local_irq_restore(flags);

	return retval;
}

static inline unsigned long
__cmpxchg_local_u32(volatile unsigned int *p, unsigned int old,
			unsigned int new)
{
	unsigned long flags;
	unsigned int retval;

	local_irq_save(flags);
	__asm__ __volatile__ (
			DCACHE_CLEAR("%0", "r4", "%1")
			"ld %0, @%1;		\n"
		"	bne	%0, %2, 1f;	\n"
			"st %3, @%1;		\n"
		"	bra	2f;		\n"
		"       .fillinsn		\n"
		"1:"
			"st %0, @%1;		\n"
		"       .fillinsn		\n"
		"2:"
			: "=&r" (retval)
			: "r" (p), "r" (old), "r" (new)
			: "cbit", "memory"
#ifdef CONFIG_CHIP_M32700_TS1
			, "r4"
#endif  /* CONFIG_CHIP_M32700_TS1 */
		);
	local_irq_restore(flags);

	return retval;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
#if 0	/* we don't have __cmpxchg_u64 */
	case 8:
		return __cmpxchg_u64(ptr, old, new);
#endif /* 0 */
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, o, n) ({				\
	((__typeof__(*(ptr)))				\
		 __cmpxchg((ptr), (unsigned long)(o),	\
			   (unsigned long)(n),		\
			   sizeof(*(ptr))));		\
})

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_local_u32(ptr, old, new);
	default:
		return __cmpxchg_local_generic(ptr, old, new, size);
	}

	return old;
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	    \
	((__typeof__(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(o),	    \
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

#endif /* _ASM_M32R_CMPXCHG_H */
