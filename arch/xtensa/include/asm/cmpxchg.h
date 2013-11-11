/*
 * Atomic xchg and cmpxchg operations.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_CMPXCHG_H
#define _XTENSA_CMPXCHG_H

#ifndef __ASSEMBLY__

#include <linux/stringify.h>

/*
 * cmpxchg
 */

static inline unsigned long
__cmpxchg_u32(volatile int *p, int old, int new)
{
#if XCHAL_HAVE_S32C1I
	__asm__ __volatile__(
			"       wsr     %2, scompare1\n"
			"       s32c1i  %0, %1, 0\n"
			: "+a" (new)
			: "a" (p), "a" (old)
			: "memory"
			);

	return new;
#else
	__asm__ __volatile__(
			"       rsil    a15, "__stringify(LOCKLEVEL)"\n"
			"       l32i    %0, %1, 0\n"
			"       bne     %0, %2, 1f\n"
			"       s32i    %3, %1, 0\n"
			"1:\n"
			"       wsr     a15, ps\n"
			"       rsync\n"
			: "=&a" (old)
			: "a" (p), "a" (old), "r" (new)
			: "a15", "memory");
	return old;
#endif
}
/* This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid cmpxchg(). */

extern void __cmpxchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
	case 4:  return __cmpxchg_u32(ptr, old, new);
	default: __cmpxchg_called_with_bad_pointer();
		 return old;
	}
}

#define cmpxchg(ptr,o,n)						      \
	({ __typeof__(*(ptr)) _o_ = (o);				      \
	   __typeof__(*(ptr)) _n_ = (n);				      \
	   (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,	      \
	   			        (unsigned long)_n_, sizeof (*(ptr))); \
	})

#include <asm-generic/cmpxchg-local.h>

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	default:
		return __cmpxchg_local_generic(ptr, old, new, size);
	}

	return old;
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define cmpxchg_local(ptr, o, n)				  	       \
	((__typeof__(*(ptr)))__cmpxchg_local_generic((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define cmpxchg64_local(ptr, o, n) __cmpxchg64_local_generic((ptr), (o), (n))

/*
 * xchg_u32
 *
 * Note that a15 is used here because the register allocation
 * done by the compiler is not guaranteed and a window overflow
 * may not occur between the rsil and wsr instructions. By using
 * a15 in the rsil, the machine is guaranteed to be in a state
 * where no register reference will cause an overflow.
 */

static inline unsigned long xchg_u32(volatile int * m, unsigned long val)
{
#if XCHAL_HAVE_S32C1I
	unsigned long tmp, result;
	__asm__ __volatile__(
			"1:     l32i    %1, %2, 0\n"
			"       mov     %0, %3\n"
			"       wsr     %1, scompare1\n"
			"       s32c1i  %0, %2, 0\n"
			"       bne     %0, %1, 1b\n"
			: "=&a" (result), "=&a" (tmp)
			: "a" (m), "a" (val)
			: "memory"
			);
	return result;
#else
	unsigned long tmp;
	__asm__ __volatile__(
			"       rsil    a15, "__stringify(LOCKLEVEL)"\n"
			"       l32i    %0, %1, 0\n"
			"       s32i    %2, %1, 0\n"
			"       wsr     a15, ps\n"
			"       rsync\n"
			: "=&a" (tmp)
			: "a" (m), "a" (val)
			: "a15", "memory");
	return tmp;
#endif
}

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

/*
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but I define that one to
 * be dead anyway.
 */

extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 4:
			return xchg_u32(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#endif /* __ASSEMBLY__ */

#endif /* _XTENSA_CMPXCHG_H */
