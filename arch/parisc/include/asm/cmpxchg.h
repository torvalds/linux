/*
 * forked from parisc asm/atomic.h which was:
 *	Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *	Copyright (C) 2006 Kyle McMartin <kyle@parisc-linux.org>
 */

#ifndef _ASM_PARISC_CMPXCHG_H_
#define _ASM_PARISC_CMPXCHG_H_

/* This should get optimized out since it's never called.
** Or get a link error if xchg is used "wrong".
*/
extern void __xchg_called_with_bad_pointer(void);

/* __xchg32/64 defined in arch/parisc/lib/bitops.c */
extern unsigned long __xchg8(char, char *);
extern unsigned long __xchg32(int, int *);
#ifdef CONFIG_64BIT
extern unsigned long __xchg64(unsigned long, unsigned long *);
#endif

/* optimizer better get rid of switch since size is a constant */
static inline unsigned long
__xchg(unsigned long x, __volatile__ void *ptr, int size)
{
	switch (size) {
#ifdef CONFIG_64BIT
	case 8: return __xchg64(x, (unsigned long *) ptr);
#endif
	case 4: return __xchg32((int) x, (int *) ptr);
	case 1: return __xchg8((char) x, (char *) ptr);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

/*
** REVISIT - Abandoned use of LDCW in xchg() for now:
** o need to test sizeof(*ptr) to avoid clearing adjacent bytes
** o and while we are at it, could CONFIG_64BIT code use LDCD too?
**
**	if (__builtin_constant_p(x) && (x == NULL))
**		if (((unsigned long)p & 0xf) == 0)
**			return __ldcw(p);
*/
#define xchg(ptr, x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))))

/* bug catcher for when unsupported size is used - won't link */
extern void __cmpxchg_called_with_bad_pointer(void);

/* __cmpxchg_u32/u64 defined in arch/parisc/lib/bitops.c */
extern unsigned long __cmpxchg_u32(volatile unsigned int *m, unsigned int old,
				   unsigned int new_);
extern unsigned long __cmpxchg_u64(volatile unsigned long *ptr,
				   unsigned long old, unsigned long new_);

/* don't worry...optimizer will get rid of most of this */
static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new_, int size)
{
	switch (size) {
#ifdef CONFIG_64BIT
	case 8: return __cmpxchg_u64((unsigned long *)ptr, old, new_);
#endif
	case 4: return __cmpxchg_u32((unsigned int *)ptr,
				     (unsigned int)old, (unsigned int)new_);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr, o, n)						 \
({									 \
	__typeof__(*(ptr)) _o_ = (o);					 \
	__typeof__(*(ptr)) _n_ = (n);					 \
	(__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,	 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
})

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new_, int size)
{
	switch (size) {
#ifdef CONFIG_64BIT
	case 8:	return __cmpxchg_u64((unsigned long *)ptr, old, new_);
#endif
	case 4:	return __cmpxchg_u32(ptr, old, new_);
	default:
		return __cmpxchg_local_generic(ptr, old, new_, size);
	}
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)					\
	((__typeof__(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(o),	\
			(unsigned long)(n), sizeof(*(ptr))))
#ifdef CONFIG_64BIT
#define cmpxchg64_local(ptr, o, n)					\
({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	cmpxchg_local((ptr), (o), (n));					\
})
#else
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))
#endif

#endif /* _ASM_PARISC_CMPXCHG_H_ */
