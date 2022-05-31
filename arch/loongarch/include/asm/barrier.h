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

#include <asm-generic/barrier.h>

#endif /* __ASM_BARRIER_H */
