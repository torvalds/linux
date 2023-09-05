/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_BARRIER_H
#define __ASM_BARRIER_H

/*
 * Hint encoding:
 *
 * Bit4: ordering or completion (0: completion, 1: ordering)
 * Bit3: barrier for previous read (0: true, 1: false)
 * Bit2: barrier for previous write (0: true, 1: false)
 * Bit1: barrier for succeeding read (0: true, 1: false)
 * Bit0: barrier for succeeding write (0: true, 1: false)
 *
 * Hint 0x700: barrier for "read after read" from the same address
 */

#define DBAR(hint) __asm__ __volatile__("dbar %0 " : : "I"(hint) : "memory")

#define crwrw		0b00000
#define cr_r_		0b00101
#define c_w_w		0b01010

#define orwrw		0b10000
#define or_r_		0b10101
#define o_w_w		0b11010

#define orw_w		0b10010
#define or_rw		0b10100

#define c_sync()	DBAR(crwrw)
#define c_rsync()	DBAR(cr_r_)
#define c_wsync()	DBAR(c_w_w)

#define o_sync()	DBAR(orwrw)
#define o_rsync()	DBAR(or_r_)
#define o_wsync()	DBAR(o_w_w)

#define ldacq_mb()	DBAR(or_rw)
#define strel_mb()	DBAR(orw_w)

#define mb()		c_sync()
#define rmb()		c_rsync()
#define wmb()		c_wsync()
#define iob()		c_sync()
#define wbflush()	c_sync()

#define __smp_mb()	o_sync()
#define __smp_rmb()	o_rsync()
#define __smp_wmb()	o_wsync()

#ifdef CONFIG_SMP
#define __WEAK_LLSC_MB		"	dbar 0x700	\n"
#else
#define __WEAK_LLSC_MB		"			\n"
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
		"sub.w	%0, $zero, %0\n\t"
#elif (__SIZEOF_LONG__ == 8)
		"sub.d	%0, $zero, %0\n\t"
#endif
		: "=r" (mask)
		: "r" (index), "r" (size)
		:);

	return mask;
}

#define __smp_load_acquire(p)				\
({							\
	typeof(*p) ___p1 = READ_ONCE(*p);		\
	compiletime_assert_atomic_type(*p);		\
	ldacq_mb();					\
	___p1;						\
})

#define __smp_store_release(p, v)			\
do {							\
	compiletime_assert_atomic_type(*p);		\
	strel_mb();					\
	WRITE_ONCE(*p, v);				\
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
