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

/* Nonexistent functions intended to cause link errors. */
extern unsigned long __xchg_called_with_bad_pointer(void);
extern unsigned long __cmpxchg_called_with_bad_pointer(void);

#define xchg(ptr, x)							\
	({								\
		typeof(*(ptr)) __x;					\
		switch (sizeof(*(ptr))) {				\
		case 4:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic_xchg( \
				(atomic_t *)(ptr),			\
				(u32)(typeof((x)-(x)))(x));		\
			break;						\
		case 8:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic64_xchg( \
				(atomic64_t *)(ptr),			\
				(u64)(typeof((x)-(x)))(x));		\
			break;						\
		default:						\
			__xchg_called_with_bad_pointer();		\
		}							\
		__x;							\
	})

#define cmpxchg(ptr, o, n)						\
	({								\
		typeof(*(ptr)) __x;					\
		switch (sizeof(*(ptr))) {				\
		case 4:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic_cmpxchg( \
				(atomic_t *)(ptr),			\
				(u32)(typeof((o)-(o)))(o),		\
				(u32)(typeof((n)-(n)))(n));		\
			break;						\
		case 8:							\
			__x = (typeof(__x))(typeof(__x-__x))atomic64_cmpxchg( \
				(atomic64_t *)(ptr),			\
				(u64)(typeof((o)-(o)))(o),		\
				(u64)(typeof((n)-(n)))(n));		\
			break;						\
		default:						\
			__cmpxchg_called_with_bad_pointer();		\
		}							\
		__x;							\
	})

#define tas(ptr) (xchg((ptr), 1))

#endif /* __ASSEMBLY__ */

#endif /* _ASM_TILE_CMPXCHG_H */
