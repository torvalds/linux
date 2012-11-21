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
  __asm__ __volatile__("rsil    a15, "__stringify(LOCKLEVEL)"\n\t"
		       "l32i    %0, %1, 0              \n\t"
		       "bne	%0, %2, 1f             \n\t"
		       "s32i    %3, %1, 0              \n\t"
		       "1:                             \n\t"
		       "wsr     a15, ps                \n\t"
		       "rsync                          \n\t"
		       : "=&a" (old)
		       : "a" (p), "a" (old), "r" (new)
		       : "a15", "memory");
  return old;
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
  unsigned long tmp;
  __asm__ __volatile__("rsil    a15, "__stringify(LOCKLEVEL)"\n\t"
		       "l32i    %0, %1, 0              \n\t"
		       "s32i    %2, %1, 0              \n\t"
		       "wsr     a15, ps                \n\t"
		       "rsync                          \n\t"
		       : "=&a" (tmp)
		       : "a" (m), "a" (val)
		       : "a15", "memory");
  return tmp;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

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
