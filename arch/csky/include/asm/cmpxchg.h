/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_CMPXCHG_H
#define __ASM_CSKY_CMPXCHG_H

#ifdef CONFIG_SMP
#include <linux/bug.h>
#include <asm/barrier.h>
#include <linux/cmpxchg-emu.h>

#define __xchg_relaxed(new, ptr, size)				\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(new) __new = (new);				\
	__typeof__(*(ptr)) __ret;				\
	unsigned long tmp;					\
	switch (size) {						\
	case 2: {						\
		u32 ret;					\
		u32 shif = ((ulong)__ptr & 2) ? 16 : 0;		\
		u32 mask = 0xffff << shif;			\
		__ptr = (__typeof__(ptr))((ulong)__ptr & ~2);	\
		__asm__ __volatile__ (				\
			"1:	ldex.w %0, (%4)\n"		\
			"	and    %1, %0, %2\n"		\
			"	or     %1, %1, %3\n"		\
			"	stex.w %1, (%4)\n"		\
			"	bez    %1, 1b\n"		\
			: "=&r" (ret), "=&r" (tmp)		\
			: "r" (~mask),				\
			  "r" ((u32)__new << shif),		\
			  "r" (__ptr)				\
			: "memory");				\
		__ret = (__typeof__(*(ptr)))			\
			((ret & mask) >> shif);			\
		break;						\
	}							\
	case 4:							\
		asm volatile (					\
		"1:	ldex.w		%0, (%3) \n"		\
		"	mov		%1, %2   \n"		\
		"	stex.w		%1, (%3) \n"		\
		"	bez		%1, 1b   \n"		\
			: "=&r" (__ret), "=&r" (tmp)		\
			: "r" (__new), "r"(__ptr)		\
			:);					\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
	__ret;							\
})

#define arch_xchg_relaxed(ptr, x) \
		(__xchg_relaxed((x), (ptr), sizeof(*(ptr))))

#define __cmpxchg_relaxed(ptr, old, new, size)			\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(new) __new = (new);				\
	__typeof__(new) __tmp;					\
	__typeof__(old) __old = (old);				\
	__typeof__(*(ptr)) __ret;				\
	switch (size) {						\
	case 1:							\
		__ret = (__typeof__(*(ptr)))cmpxchg_emu_u8((volatile u8 *)__ptr, (uintptr_t)__old, (uintptr_t)__new); \
		break;						\
	case 4:							\
		asm volatile (					\
		"1:	ldex.w		%0, (%3) \n"		\
		"	cmpne		%0, %4   \n"		\
		"	bt		2f       \n"		\
		"	mov		%1, %2   \n"		\
		"	stex.w		%1, (%3) \n"		\
		"	bez		%1, 1b   \n"		\
		"2:				 \n"		\
			: "=&r" (__ret), "=&r" (__tmp)		\
			: "r" (__new), "r"(__ptr), "r"(__old)	\
			:);					\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
	__ret;							\
})

#define arch_cmpxchg_relaxed(ptr, o, n) \
	(__cmpxchg_relaxed((ptr), (o), (n), sizeof(*(ptr))))

#define __cmpxchg_acquire(ptr, old, new, size)			\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(new) __new = (new);				\
	__typeof__(new) __tmp;					\
	__typeof__(old) __old = (old);				\
	__typeof__(*(ptr)) __ret;				\
	switch (size) {						\
	case 1:							\
		__ret = (__typeof__(*(ptr)))cmpxchg_emu_u8((volatile u8 *)__ptr, (uintptr_t)__old, (uintptr_t)__new); \
		break;						\
	case 4:							\
		asm volatile (					\
		"1:	ldex.w		%0, (%3) \n"		\
		"	cmpne		%0, %4   \n"		\
		"	bt		2f       \n"		\
		"	mov		%1, %2   \n"		\
		"	stex.w		%1, (%3) \n"		\
		"	bez		%1, 1b   \n"		\
		ACQUIRE_FENCE					\
		"2:				 \n"		\
			: "=&r" (__ret), "=&r" (__tmp)		\
			: "r" (__new), "r"(__ptr), "r"(__old)	\
			:);					\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
	__ret;							\
})

#define arch_cmpxchg_acquire(ptr, o, n) \
	(__cmpxchg_acquire((ptr), (o), (n), sizeof(*(ptr))))

#define __cmpxchg(ptr, old, new, size)				\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(new) __new = (new);				\
	__typeof__(new) __tmp;					\
	__typeof__(old) __old = (old);				\
	__typeof__(*(ptr)) __ret;				\
	switch (size) {						\
	case 1:							\
		__ret = (__typeof__(*(ptr)))cmpxchg_emu_u8((volatile u8 *)__ptr, (uintptr_t)__old, (uintptr_t)__new); \
		break;						\
	case 4:							\
		asm volatile (					\
		RELEASE_FENCE					\
		"1:	ldex.w		%0, (%3) \n"		\
		"	cmpne		%0, %4   \n"		\
		"	bt		2f       \n"		\
		"	mov		%1, %2   \n"		\
		"	stex.w		%1, (%3) \n"		\
		"	bez		%1, 1b   \n"		\
		FULL_FENCE					\
		"2:				 \n"		\
			: "=&r" (__ret), "=&r" (__tmp)		\
			: "r" (__new), "r"(__ptr), "r"(__old)	\
			:);					\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
	__ret;							\
})

#define arch_cmpxchg(ptr, o, n)					\
	(__cmpxchg((ptr), (o), (n), sizeof(*(ptr))))

#define arch_cmpxchg_local(ptr, o, n)				\
	(__cmpxchg_relaxed((ptr), (o), (n), sizeof(*(ptr))))
#else
#include <asm-generic/cmpxchg.h>
#endif

#endif /* __ASM_CSKY_CMPXCHG_H */
