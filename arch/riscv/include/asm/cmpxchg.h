/*
 * Copyright (C) 2014 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_CMPXCHG_H
#define _ASM_RISCV_CMPXCHG_H

#include <linux/bug.h>

#include <asm/barrier.h>

#define __xchg(new, ptr, size, asm_or)				\
({								\
	__typeof__(ptr) __ptr = (ptr);				\
	__typeof__(new) __new = (new);				\
	__typeof__(*(ptr)) __ret;				\
	switch (size) {						\
	case 4:							\
		__asm__ __volatile__ (				\
			"amoswap.w" #asm_or " %0, %2, %1"	\
			: "=r" (__ret), "+A" (*__ptr)		\
			: "r" (__new)				\
			: "memory");				\
		break;						\
	case 8:							\
		__asm__ __volatile__ (				\
			"amoswap.d" #asm_or " %0, %2, %1"	\
			: "=r" (__ret), "+A" (*__ptr)		\
			: "r" (__new)				\
			: "memory");				\
		break;						\
	default:						\
		BUILD_BUG();					\
	}							\
	__ret;							\
})

#define xchg(ptr, x)    (__xchg((x), (ptr), sizeof(*(ptr)), .aqrl))

#define xchg32(ptr, x)				\
({						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 4);	\
	xchg((ptr), (x));			\
})

#define xchg64(ptr, x)				\
({						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);	\
	xchg((ptr), (x));			\
})

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */
#define __cmpxchg(ptr, old, new, size, lrb, scb)			\
({									\
	__typeof__(ptr) __ptr = (ptr);					\
	__typeof__(*(ptr)) __old = (old);				\
	__typeof__(*(ptr)) __new = (new);				\
	__typeof__(*(ptr)) __ret;					\
	register unsigned int __rc;					\
	switch (size) {							\
	case 4:								\
		__asm__ __volatile__ (					\
		"0:"							\
			"lr.w" #scb " %0, %2\n"				\
			"bne         %0, %z3, 1f\n"			\
			"sc.w" #lrb " %1, %z4, %2\n"			\
			"bnez        %1, 0b\n"				\
		"1:"							\
			: "=&r" (__ret), "=&r" (__rc), "+A" (*__ptr)	\
			: "rJ" (__old), "rJ" (__new)			\
			: "memory");					\
		break;							\
	case 8:								\
		__asm__ __volatile__ (					\
		"0:"							\
			"lr.d" #scb " %0, %2\n"				\
			"bne         %0, %z3, 1f\n"			\
			"sc.d" #lrb " %1, %z4, %2\n"			\
			"bnez        %1, 0b\n"				\
		"1:"							\
			: "=&r" (__ret), "=&r" (__rc), "+A" (*__ptr)	\
			: "rJ" (__old), "rJ" (__new)			\
			: "memory");					\
		break;							\
	default:							\
		BUILD_BUG();						\
	}								\
	__ret;								\
})

#define cmpxchg(ptr, o, n) \
	(__cmpxchg((ptr), (o), (n), sizeof(*(ptr)), .aqrl, .aqrl))

#define cmpxchg_local(ptr, o, n) \
	(__cmpxchg((ptr), (o), (n), sizeof(*(ptr)), , ))

#define cmpxchg32(ptr, o, n)			\
({						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 4);	\
	cmpxchg((ptr), (o), (n));		\
})

#define cmpxchg32_local(ptr, o, n)		\
({						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 4);	\
	cmpxchg_local((ptr), (o), (n));		\
})

#define cmpxchg64(ptr, o, n)			\
({						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);	\
	cmpxchg((ptr), (o), (n));		\
})

#define cmpxchg64_local(ptr, o, n)		\
({						\
	BUILD_BUG_ON(sizeof(*(ptr)) != 8);	\
	cmpxchg_local((ptr), (o), (n));		\
})

#endif /* _ASM_RISCV_CMPXCHG_H */
