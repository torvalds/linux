/*
 * cmpxchg.h -- forked from asm/atomic.h with this copyright:
 *
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 */

#ifndef _ASM_TILE_CMPXCHG_H
#define _ASM_TILE_CMPXCHG_H

#ifndef __ASSEMBLY__

#include <asm/barrier.h>

/* Nonexistent functions intended to cause compile errors. */
extern void __xchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for xchg");
extern void __cmpxchg_called_with_bad_pointer(void)
	__compiletime_error("Bad argument size for cmpxchg");

#ifndef __tilegx__

/* Note the _atomic_xxx() routines include a final mb(). */
int _atomic_xchg(int *ptr, int n);
int _atomic_xchg_add(int *v, int i);
int _atomic_xchg_add_unless(int *v, int a, int u);
int _atomic_cmpxchg(int *ptr, int o, int n);
u64 _atomic64_xchg(u64 *v, u64 n);
u64 _atomic64_xchg_add(u64 *v, u64 i);
u64 _atomic64_xchg_add_unless(u64 *v, u64 a, u64 u);
u64 _atomic64_cmpxchg(u64 *v, u64 o, u64 n);

#define xchg(ptr, n)							\
	({								\
		if (sizeof(*(ptr)) != 4)				\
			__xchg_called_with_bad_pointer();		\
		smp_mb();						\
		(typeof(*(ptr)))_atomic_xchg((int *)(ptr), (int)(n));	\
	})

#define cmpxchg(ptr, o, n)						\
	({								\
		if (sizeof(*(ptr)) != 4)				\
			__cmpxchg_called_with_bad_pointer();		\
		smp_mb();						\
		(typeof(*(ptr)))_atomic_cmpxchg((int *)ptr, (int)o, (int)n); \
	})

#define xchg64(ptr, n)							\
	({								\
		if (sizeof(*(ptr)) != 8)				\
			__xchg_called_with_bad_pointer();		\
		smp_mb();						\
		(typeof(*(ptr)))_atomic64_xchg((u64 *)(ptr), (u64)(n));	\
	})

#define cmpxchg64(ptr, o, n)						\
	({								\
		if (sizeof(*(ptr)) != 8)				\
			__cmpxchg_called_with_bad_pointer();		\
		smp_mb();						\
		(typeof(*(ptr)))_atomic64_cmpxchg((u64 *)ptr, (u64)o, (u64)n); \
	})

#else

#define xchg(ptr, n)							\
	({								\
		typeof(*(ptr)) __x;					\
		smp_mb();						\
		switch (sizeof(*(ptr))) {				\
		case 4:							\
			__x = (typeof(__x))(unsigned long)		\
				__insn_exch4((ptr), (u32)(unsigned long)(n)); \
			break;						\
		case 8:							\
			__x = (typeof(__x))			\
				__insn_exch((ptr), (unsigned long)(n));	\
			break;						\
		default:						\
			__xchg_called_with_bad_pointer();		\
			break;						\
		}							\
		smp_mb();						\
		__x;							\
	})

#define cmpxchg(ptr, o, n)						\
	({								\
		typeof(*(ptr)) __x;					\
		__insn_mtspr(SPR_CMPEXCH_VALUE, (unsigned long)(o));	\
		smp_mb();						\
		switch (sizeof(*(ptr))) {				\
		case 4:							\
			__x = (typeof(__x))(unsigned long)		\
				__insn_cmpexch4((ptr), (u32)(unsigned long)(n)); \
			break;						\
		case 8:							\
			__x = (typeof(__x))__insn_cmpexch((ptr), (u64)(n)); \
			break;						\
		default:						\
			__cmpxchg_called_with_bad_pointer();		\
			break;						\
		}							\
		smp_mb();						\
		__x;							\
	})

#define xchg64 xchg
#define cmpxchg64 cmpxchg

#endif

#define tas(ptr) xchg((ptr), 1)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TILE_CMPXCHG_H */
