/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2011
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 */

#ifndef __ASM_CMPXCHG_H
#define __ASM_CMPXCHG_H

#include <linux/mmdebug.h>
#include <linux/types.h>
#include <linux/bug.h>
#include <asm/asm.h>

void __xchg_called_with_bad_pointer(void);

static __always_inline unsigned long
__arch_xchg(unsigned long x, unsigned long address, int size)
{
	unsigned long old;
	int shift;

	switch (size) {
	case 1:
		shift = (3 ^ (address & 3)) << 3;
		address ^= address & 3;
		asm volatile(
			"       l       %0,%1\n"
			"0:     lr      0,%0\n"
			"       nr      0,%3\n"
			"       or      0,%2\n"
			"       cs      %0,0,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+Q" (*(int *) address)
			: "d" ((x & 0xff) << shift), "d" (~(0xff << shift))
			: "memory", "cc", "0");
		return old >> shift;
	case 2:
		shift = (2 ^ (address & 2)) << 3;
		address ^= address & 2;
		asm volatile(
			"       l       %0,%1\n"
			"0:     lr      0,%0\n"
			"       nr      0,%3\n"
			"       or      0,%2\n"
			"       cs      %0,0,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+Q" (*(int *) address)
			: "d" ((x & 0xffff) << shift), "d" (~(0xffff << shift))
			: "memory", "cc", "0");
		return old >> shift;
	case 4:
		asm volatile(
			"       l       %0,%1\n"
			"0:     cs      %0,%2,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+Q" (*(int *) address)
			: "d" (x)
			: "memory", "cc");
		return old;
	case 8:
		asm volatile(
			"       lg      %0,%1\n"
			"0:     csg     %0,%2,%1\n"
			"       jl      0b\n"
			: "=&d" (old), "+QS" (*(long *) address)
			: "d" (x)
			: "memory", "cc");
		return old;
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define arch_xchg(ptr, x)						\
({									\
	__typeof__(*(ptr)) __ret;					\
									\
	__ret = (__typeof__(*(ptr)))					\
		__arch_xchg((unsigned long)(x), (unsigned long)(ptr),	\
			    sizeof(*(ptr)));				\
	__ret;								\
})

void __cmpxchg_called_with_bad_pointer(void);

static __always_inline u32 __cs_asm(u64 ptr, u32 old, u32 new)
{
	asm volatile(
		"	cs	%[old],%[new],%[ptr]\n"
		: [old] "+d" (old), [ptr] "+Q" (*(u32 *)ptr)
		: [new] "d" (new)
		: "memory", "cc");
	return old;
}

static __always_inline u64 __csg_asm(u64 ptr, u64 old, u64 new)
{
	asm volatile(
		"	csg	%[old],%[new],%[ptr]\n"
		: [old] "+d" (old), [ptr] "+QS" (*(u64 *)ptr)
		: [new] "d" (new)
		: "memory", "cc");
	return old;
}

static inline u8 __arch_cmpxchg1(u64 ptr, u8 old, u8 new)
{
	union {
		u8 b[4];
		u32 w;
	} old32, new32;
	u32 prev;
	int i;

	i = ptr & 3;
	ptr &= ~0x3;
	prev = READ_ONCE(*(u32 *)ptr);
	do {
		old32.w = prev;
		if (old32.b[i] != old)
			return old32.b[i];
		new32.w = old32.w;
		new32.b[i] = new;
		prev = __cs_asm(ptr, old32.w, new32.w);
	} while (prev != old32.w);
	return old;
}

static inline u16 __arch_cmpxchg2(u64 ptr, u16 old, u16 new)
{
	union {
		u16 b[2];
		u32 w;
	} old32, new32;
	u32 prev;
	int i;

	i = (ptr & 3) >> 1;
	ptr &= ~0x3;
	prev = READ_ONCE(*(u32 *)ptr);
	do {
		old32.w = prev;
		if (old32.b[i] != old)
			return old32.b[i];
		new32.w = old32.w;
		new32.b[i] = new;
		prev = __cs_asm(ptr, old32.w, new32.w);
	} while (prev != old32.w);
	return old;
}

static __always_inline u64 __arch_cmpxchg(u64 ptr, u64 old, u64 new, int size)
{
	switch (size) {
	case 1:	 return __arch_cmpxchg1(ptr, old & 0xff, new & 0xff);
	case 2:  return __arch_cmpxchg2(ptr, old & 0xffff, new & 0xffff);
	case 4:  return __cs_asm(ptr, old & 0xffffffff, new & 0xffffffff);
	case 8:  return __csg_asm(ptr, old, new);
	default: __cmpxchg_called_with_bad_pointer();
	}
	return old;
}

#define arch_cmpxchg(ptr, o, n)						\
({									\
	(__typeof__(*(ptr)))__arch_cmpxchg((unsigned long)(ptr),	\
					   (unsigned long)(o),		\
					   (unsigned long)(n),		\
					   sizeof(*(ptr)));		\
})

#define arch_cmpxchg64		arch_cmpxchg
#define arch_cmpxchg_local	arch_cmpxchg
#define arch_cmpxchg64_local	arch_cmpxchg

#ifdef __HAVE_ASM_FLAG_OUTPUTS__

#define arch_try_cmpxchg(ptr, oldp, new)				\
({									\
	__typeof__(ptr) __oldp = (__typeof__(ptr))(oldp);		\
	__typeof__(*(ptr)) __old = *__oldp;				\
	__typeof__(*(ptr)) __new = (new);				\
	__typeof__(*(ptr)) __prev;					\
	int __cc;							\
									\
	switch (sizeof(*(ptr))) {					\
	case 1:								\
	case 2: {							\
		__prev = arch_cmpxchg((ptr), (__old), (__new));		\
		__cc = (__prev != __old);				\
		if (unlikely(__cc))					\
			*__oldp = __prev;				\
		break;							\
	}								\
	case 4:	{							\
		asm volatile(						\
			"	cs	%[__old],%[__new],%[__ptr]\n"	\
			: [__old] "+d" (*__oldp),			\
			  [__ptr] "+Q" (*(ptr)),			\
			  "=@cc" (__cc)					\
			: [__new] "d" (__new)				\
			: "memory");					\
		break;							\
	}								\
	case 8:	{							\
		 asm volatile(						\
			 "	csg	%[__old],%[__new],%[__ptr]\n"	\
			 : [__old] "+d" (*__oldp),			\
			   [__ptr] "+QS" (*(ptr)),			\
			   "=@cc" (__cc)				\
			 : [__new] "d" (__new)				\
			 : "memory");					\
		 break;							\
	}								\
	default:							\
		__cmpxchg_called_with_bad_pointer();			\
	}								\
	likely(__cc == 0);						\
})

#define arch_try_cmpxchg64		arch_try_cmpxchg
#define arch_try_cmpxchg_local		arch_try_cmpxchg
#define arch_try_cmpxchg64_local	arch_try_cmpxchg

#endif /* __HAVE_ASM_FLAG_OUTPUTS__ */

#define system_has_cmpxchg128()		1

static __always_inline u128 arch_cmpxchg128(volatile u128 *ptr, u128 old, u128 new)
{
	asm volatile(
		"	cdsg	%[old],%[new],%[ptr]\n"
		: [old] "+d" (old), [ptr] "+QS" (*ptr)
		: [new] "d" (new)
		: "memory", "cc");
	return old;
}

#define arch_cmpxchg128		arch_cmpxchg128

#endif /* __ASM_CMPXCHG_H */
