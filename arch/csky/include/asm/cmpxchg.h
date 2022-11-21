/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_CMPXCHG_H
#define __ASM_CSKY_CMPXCHG_H

#ifdef CONFIG_SMP
#include <asm/barrier.h>

extern void __bad_xchg(void);

#define __xchg_relaxed(new, ptr, size)				\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(new) __new = (new);				\
	__typeof__(*(ptr)) __ret;				\
	unsigned long tmp;					\
	switch (size) {						\
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
		__bad_xchg();					\
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
		__bad_xchg();					\
	}							\
	__ret;							\
})

#define arch_cmpxchg_relaxed(ptr, o, n) \
	(__cmpxchg_relaxed((ptr), (o), (n), sizeof(*(ptr))))

#define arch_cmpxchg(ptr, o, n) 				\
({								\
	__typeof__(*(ptr)) __ret;				\
	__smp_release_fence();					\
	__ret = arch_cmpxchg_relaxed(ptr, o, n);		\
	__smp_acquire_fence();					\
	__ret;							\
})

#else
#include <asm-generic/cmpxchg.h>
#endif

#endif /* __ASM_CSKY_CMPXCHG_H */
