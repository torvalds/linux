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

#include <linux/bits.h>
#include <linux/stringify.h>
#include <linux/cmpxchg-emu.h>

/*
 * cmpxchg
 */

static inline unsigned long
__cmpxchg_u32(volatile int *p, int old, int new)
{
#if XCHAL_HAVE_EXCLUSIVE
	unsigned long tmp, result;

	__asm__ __volatile__(
			"1:     l32ex   %[result], %[addr]\n"
			"       bne     %[result], %[cmp], 2f\n"
			"       mov     %[tmp], %[new]\n"
			"       s32ex   %[tmp], %[addr]\n"
			"       getex   %[tmp]\n"
			"       beqz    %[tmp], 1b\n"
			"2:\n"
			: [result] "=&a" (result), [tmp] "=&a" (tmp)
			: [new] "a" (new), [addr] "a" (p), [cmp] "a" (old)
			: "memory"
			);

	return result;
#elif XCHAL_HAVE_S32C1I
	__asm__ __volatile__(
			"       wsr     %[cmp], scompare1\n"
			"       s32c1i  %[new], %[mem]\n"
			: [new] "+a" (new), [mem] "+m" (*p)
			: [cmp] "a" (old)
			: "memory"
			);

	return new;
#else
	__asm__ __volatile__(
			"       rsil    a14, "__stringify(TOPLEVEL)"\n"
			"       l32i    %[old], %[mem]\n"
			"       bne     %[old], %[cmp], 1f\n"
			"       s32i    %[new], %[mem]\n"
			"1:\n"
			"       wsr     a14, ps\n"
			"       rsync\n"
			: [old] "=&a" (old), [mem] "+m" (*p)
			: [cmp] "a" (old), [new] "r" (new)
			: "a14", "memory");
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
	case 1:  return cmpxchg_emu_u8(ptr, old, new);
	case 4:  return __cmpxchg_u32(ptr, old, new);
	default: __cmpxchg_called_with_bad_pointer();
		 return old;
	}
}

#define arch_cmpxchg(ptr,o,n)						      \
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
		return __generic_cmpxchg_local(ptr, old, new, size);
	}

	return old;
}

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */
#define arch_cmpxchg_local(ptr, o, n)				  	       \
	((__typeof__(*(ptr)))__generic_cmpxchg_local((ptr), (unsigned long)(o),\
			(unsigned long)(n), sizeof(*(ptr))))
#define arch_cmpxchg64_local(ptr, o, n) __generic_cmpxchg64_local((ptr), (o), (n))
#define arch_cmpxchg64(ptr, o, n)    arch_cmpxchg64_local((ptr), (o), (n))

/*
 * xchg_u32
 *
 * Note that a14 is used here because the register allocation
 * done by the compiler is not guaranteed and a window overflow
 * may not occur between the rsil and wsr instructions. By using
 * a14 in the rsil, the machine is guaranteed to be in a state
 * where no register reference will cause an overflow.
 */

static inline unsigned long xchg_u32(volatile int * m, unsigned long val)
{
#if XCHAL_HAVE_EXCLUSIVE
	unsigned long tmp, result;

	__asm__ __volatile__(
			"1:     l32ex   %[result], %[addr]\n"
			"       mov     %[tmp], %[val]\n"
			"       s32ex   %[tmp], %[addr]\n"
			"       getex   %[tmp]\n"
			"       beqz    %[tmp], 1b\n"
			: [result] "=&a" (result), [tmp] "=&a" (tmp)
			: [val] "a" (val), [addr] "a" (m)
			: "memory"
			);

	return result;
#elif XCHAL_HAVE_S32C1I
	unsigned long tmp, result;
	__asm__ __volatile__(
			"1:     l32i    %[tmp], %[mem]\n"
			"       mov     %[result], %[val]\n"
			"       wsr     %[tmp], scompare1\n"
			"       s32c1i  %[result], %[mem]\n"
			"       bne     %[result], %[tmp], 1b\n"
			: [result] "=&a" (result), [tmp] "=&a" (tmp),
			  [mem] "+m" (*m)
			: [val] "a" (val)
			: "memory"
			);
	return result;
#else
	unsigned long tmp;
	__asm__ __volatile__(
			"       rsil    a14, "__stringify(TOPLEVEL)"\n"
			"       l32i    %[tmp], %[mem]\n"
			"       s32i    %[val], %[mem]\n"
			"       wsr     a14, ps\n"
			"       rsync\n"
			: [tmp] "=&a" (tmp), [mem] "+m" (*m)
			: [val] "a" (val)
			: "a14", "memory");
	return tmp;
#endif
}

#define arch_xchg(ptr,x) \
	((__typeof__(*(ptr)))__arch_xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

static inline u32 xchg_small(volatile void *ptr, u32 x, int size)
{
	int off = (unsigned long)ptr % sizeof(u32);
	volatile u32 *p = ptr - off;
#ifdef __BIG_ENDIAN
	int bitoff = (sizeof(u32) - size - off) * BITS_PER_BYTE;
#else
	int bitoff = off * BITS_PER_BYTE;
#endif
	u32 bitmask = ((0x1 << size * BITS_PER_BYTE) - 1) << bitoff;
	u32 oldv, newv;
	u32 ret;

	do {
		oldv = READ_ONCE(*p);
		ret = (oldv & bitmask) >> bitoff;
		newv = (oldv & ~bitmask) | (x << bitoff);
	} while (__cmpxchg_u32(p, oldv, newv) != oldv);

	return ret;
}

/*
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but I define that one to
 * be dead anyway.
 */

extern void __xchg_called_with_bad_pointer(void);

static __inline__ unsigned long
__arch_xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
	case 1:
		return xchg_small(ptr, x, 1);
	case 2:
		return xchg_small(ptr, x, 2);
	case 4:
		return xchg_u32(ptr, x);
	default:
		__xchg_called_with_bad_pointer();
		return x;
	}
}

#endif /* __ASSEMBLY__ */

#endif /* _XTENSA_CMPXCHG_H */
