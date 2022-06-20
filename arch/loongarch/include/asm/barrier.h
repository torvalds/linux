/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

#define __sync()	__asm__ __volatile__("dbar 0" : : : "memory")

#define fast_wmb()	__sync()
#define fast_rmb()	__sync()
#define fast_mb()	__sync()
#define fast_iob()	__sync()
#define wbflush()	__sync()

#define wmb()		fast_wmb()
#define rmb()		fast_rmb()
#define mb()		fast_mb()
#define iob()		fast_iob()

#define __smp_mb()	__asm__ __volatile__("dbar 0" : : : "memory")
#define __smp_rmb()	__asm__ __volatile__("dbar 0" : : : "memory")
#define __smp_wmb()	__asm__ __volatile__("dbar 0" : : : "memory")

#ifdef CONFIG_SMP
#define __WEAK_LLSC_MB		"	dbar 0  \n"
#else
#define __WEAK_LLSC_MB		"		\n"
#endif

#define __smp_mb__before_atomic()	barrier()
#define __smp_mb__after_atomic()	barrier()

/**
 * array_index_mask_nospec() - generate a ~0 mask when index < size, 0 otherwise
 * @index: array element index
 * @size: number of elements in array
 *
 * Returns:
 *     0 - (@index < @size)
 */
#define array_index_mask_nospec array_index_mask_nospec
static inline unsigned long array_index_mask_nospec(unsigned long index,
						    unsigned long size)
{
	unsigned long mask;

	__asm__ __volatile__(
		"sltu	%0, %1, %2\n\t"
#if (__SIZEOF_LONG__ == 4)
		"sub.w	%0, $r0, %0\n\t"
#elif (__SIZEOF_LONG__ == 8)
		"sub.d	%0, $r0, %0\n\t"
#endif
		: "=r" (mask)
		: "r" (index), "r" (size)
		:);

	return mask;
}

#define __smp_load_acquire(p)							\
({										\
	union { typeof(*p) __val; char __c[1]; } __u;				\
	unsigned long __tmp = 0;							\
	compiletime_assert_atomic_type(*p);					\
	switch (sizeof(*p)) {							\
	case 1:									\
		*(__u8 *)__u.__c = *(volatile __u8 *)p;				\
		__smp_mb();							\
		break;								\
	case 2:									\
		*(__u16 *)__u.__c = *(volatile __u16 *)p;			\
		__smp_mb();							\
		break;								\
	case 4:									\
		__asm__ __volatile__(						\
		"amor_db.w %[val], %[tmp], %[mem]	\n"				\
		: [val] "=&r" (*(__u32 *)__u.__c)				\
		: [mem] "ZB" (*(u32 *) p), [tmp] "r" (__tmp)			\
		: "memory");							\
		break;								\
	case 8:									\
		__asm__ __volatile__(						\
		"amor_db.d %[val], %[tmp], %[mem]	\n"				\
		: [val] "=&r" (*(__u64 *)__u.__c)				\
		: [mem] "ZB" (*(u64 *) p), [tmp] "r" (__tmp)			\
		: "memory");							\
		break;								\
	}									\
	(typeof(*p))__u.__val;								\
})

#define __smp_store_release(p, v)						\
do {										\
	union { typeof(*p) __val; char __c[1]; } __u =				\
		{ .__val = (__force typeof(*p)) (v) };				\
	unsigned long __tmp;							\
	compiletime_assert_atomic_type(*p);					\
	switch (sizeof(*p)) {							\
	case 1:									\
		__smp_mb();							\
		*(volatile __u8 *)p = *(__u8 *)__u.__c;				\
		break;								\
	case 2:									\
		__smp_mb();							\
		*(volatile __u16 *)p = *(__u16 *)__u.__c;			\
		break;								\
	case 4:									\
		__asm__ __volatile__(						\
		"amswap_db.w %[tmp], %[val], %[mem]	\n"			\
		: [mem] "+ZB" (*(u32 *)p), [tmp] "=&r" (__tmp)			\
		: [val] "r" (*(__u32 *)__u.__c)					\
		: );								\
		break;								\
	case 8:									\
		__asm__ __volatile__(						\
		"amswap_db.d %[tmp], %[val], %[mem]	\n"			\
		: [mem] "+ZB" (*(u64 *)p), [tmp] "=&r" (__tmp)			\
		: [val] "r" (*(__u64 *)__u.__c)					\
		: );								\
		break;								\
	}									\
} while (0)

#define __smp_store_mb(p, v)							\
do {										\
	union { typeof(p) __val; char __c[1]; } __u =				\
		{ .__val = (__force typeof(p)) (v) };				\
	unsigned long __tmp;							\
	switch (sizeof(p)) {							\
	case 1:									\
		*(volatile __u8 *)&p = *(__u8 *)__u.__c;			\
		__smp_mb();							\
		break;								\
	case 2:									\
		*(volatile __u16 *)&p = *(__u16 *)__u.__c;			\
		__smp_mb();							\
		break;								\
	case 4:									\
		__asm__ __volatile__(						\
		"amswap_db.w %[tmp], %[val], %[mem]	\n"			\
		: [mem] "+ZB" (*(u32 *)&p), [tmp] "=&r" (__tmp)			\
		: [val] "r" (*(__u32 *)__u.__c)					\
		: );								\
		break;								\
	case 8:									\
		__asm__ __volatile__(						\
		"amswap_db.d %[tmp], %[val], %[mem]	\n"			\
		: [mem] "+ZB" (*(u64 *)&p), [tmp] "=&r" (__tmp)			\
		: [val] "r" (*(__u64 *)__u.__c)					\
		: );								\
		break;								\
	}									\
} while (0)

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
