/* SPDX-License-Identifier: GPL-2.0 */
/* 64-bit atomic xchg() and cmpxchg() definitions.
 *
 * Copyright (C) 1996, 1997, 2000 David S. Miller (davem@redhat.com)
 */

#ifndef __ARCH_SPARC64_CMPXCHG__
#define __ARCH_SPARC64_CMPXCHG__

static inline unsigned long
__cmpxchg_u32(volatile int *m, int old, int new)
{
	__asm__ __volatile__("cas [%2], %3, %0"
			     : "=&r" (new)
			     : "0" (new), "r" (m), "r" (old)
			     : "memory");

	return new;
}

static inline unsigned long xchg32(__volatile__ unsigned int *m, unsigned int val)
{
	unsigned long tmp1, tmp2;

	__asm__ __volatile__(
"	mov		%0, %1\n"
"1:	lduw		[%4], %2\n"
"	cas		[%4], %2, %0\n"
"	cmp		%2, %0\n"
"	bne,a,pn	%%icc, 1b\n"
"	 mov		%1, %0\n"
	: "=&r" (val), "=&r" (tmp1), "=&r" (tmp2)
	: "0" (val), "r" (m)
	: "cc", "memory");
	return val;
}

static inline unsigned long xchg64(__volatile__ unsigned long *m, unsigned long val)
{
	unsigned long tmp1, tmp2;

	__asm__ __volatile__(
"	mov		%0, %1\n"
"1:	ldx		[%4], %2\n"
"	casx		[%4], %2, %0\n"
"	cmp		%2, %0\n"
"	bne,a,pn	%%xcc, 1b\n"
"	 mov		%1, %0\n"
	: "=&r" (val), "=&r" (tmp1), "=&r" (tmp2)
	: "0" (val), "r" (m)
	: "cc", "memory");
	return val;
}

#define arch_xchg(ptr,x)							\
({	__typeof__(*(ptr)) __ret;					\
	__ret = (__typeof__(*(ptr)))					\
		__arch_xchg((unsigned long)(x), (ptr), sizeof(*(ptr)));	\
	__ret;								\
})

void __xchg_called_with_bad_pointer(void);

/*
 * Use 4 byte cas instruction to achieve 2 byte xchg. Main logic
 * here is to get the bit shift of the byte we are interested in.
 * The XOR is handy for reversing the bits for big-endian byte order.
 */
static inline unsigned long
xchg16(__volatile__ unsigned short *m, unsigned short val)
{
	unsigned long maddr = (unsigned long)m;
	int bit_shift = (((unsigned long)m & 2) ^ 2) << 3;
	unsigned int mask = 0xffff << bit_shift;
	unsigned int *ptr = (unsigned int  *) (maddr & ~2);
	unsigned int old32, new32, load32;

	/* Read the old value */
	load32 = *ptr;

	do {
		old32 = load32;
		new32 = (load32 & (~mask)) | val << bit_shift;
		load32 = __cmpxchg_u32(ptr, old32, new32);
	} while (load32 != old32);

	return (load32 & mask) >> bit_shift;
}

static inline unsigned long
__arch_xchg(unsigned long x, __volatile__ void * ptr, int size)
{
	switch (size) {
	case 2:
		return xchg16(ptr, x);
	case 4:
		return xchg32(ptr, x);
	case 8:
		return xchg64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#include <asm-generic/cmpxchg-local.h>


static inline unsigned long
__cmpxchg_u64(volatile long *m, unsigned long old, unsigned long new)
{
	__asm__ __volatile__("casx [%2], %3, %0"
			     : "=&r" (new)
			     : "0" (new), "r" (m), "r" (old)
			     : "memory");

	return new;
}

/*
 * Use 4 byte cas instruction to achieve 1 byte cmpxchg. Main logic
 * here is to get the bit shift of the byte we are interested in.
 * The XOR is handy for reversing the bits for big-endian byte order
 */
static inline unsigned long
__cmpxchg_u8(volatile unsigned char *m, unsigned char old, unsigned char new)
{
	unsigned long maddr = (unsigned long)m;
	int bit_shift = (((unsigned long)m & 3) ^ 3) << 3;
	unsigned int mask = 0xff << bit_shift;
	unsigned int *ptr = (unsigned int *) (maddr & ~3);
	unsigned int old32, new32, load;
	unsigned int load32 = *ptr;

	do {
		new32 = (load32 & ~mask) | (new << bit_shift);
		old32 = (load32 & ~mask) | (old << bit_shift);
		load32 = __cmpxchg_u32(ptr, old32, new32);
		if (load32 == old32)
			return old;
		load = (load32 & mask) >> bit_shift;
	} while (load == old);

	return load;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
		case 1:
			return __cmpxchg_u8(ptr, old, new);
		case 4:
			return __cmpxchg_u32(ptr, old, new);
		case 8:
			return __cmpxchg_u64(ptr, old, new);
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

/*
 * cmpxchg_local and cmpxchg64_local are atomic wrt current CPU. Always make
 * them available.
 */

static inline unsigned long __cmpxchg_local(volatile void *ptr,
				      unsigned long old,
				      unsigned long new, int size)
{
	switch (size) {
	case 4:
	case 8:	return __cmpxchg(ptr, old, new, size);
	default:
		return __generic_cmpxchg_local(ptr, old, new, size);
	}

	return old;
}

#define arch_cmpxchg_local(ptr, o, n)				  	\
	((__typeof__(*(ptr)))__cmpxchg_local((ptr), (unsigned long)(o),	\
			(unsigned long)(n), sizeof(*(ptr))))
#define arch_cmpxchg64_local(ptr, o, n)					\
  ({									\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);				\
	arch_cmpxchg_local((ptr), (o), (n));					\
  })
#define arch_cmpxchg64(ptr, o, n)	arch_cmpxchg64_local((ptr), (o), (n))

#endif /* __ARCH_SPARC64_CMPXCHG__ */
