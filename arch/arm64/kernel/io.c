// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/kernel/io.c
 *
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/export.h>
#include <linux/types.h>
#include <linux/io.h>

/*
 * This generates a memcpy that works on a from/to address which is aligned to
 * bits. Count is in terms of the number of bits sized quantities to copy. It
 * optimizes to use the STR groupings when possible so that it is WC friendly.
 */
#define memcpy_toio_aligned(to, from, count, bits)                        \
	({                                                                \
		volatile u##bits __iomem *_to = to;                       \
		const u##bits *_from = from;                              \
		size_t _count = count;                                    \
		const u##bits *_end_from = _from + ALIGN_DOWN(_count, 8); \
                                                                          \
		for (; _from < _end_from; _from += 8, _to += 8)           \
			__const_memcpy_toio_aligned##bits(_to, _from, 8); \
		if ((_count % 8) >= 4) {                                  \
			__const_memcpy_toio_aligned##bits(_to, _from, 4); \
			_from += 4;                                       \
			_to += 4;                                         \
		}                                                         \
		if ((_count % 4) >= 2) {                                  \
			__const_memcpy_toio_aligned##bits(_to, _from, 2); \
			_from += 2;                                       \
			_to += 2;                                         \
		}                                                         \
		if (_count % 2)                                           \
			__const_memcpy_toio_aligned##bits(_to, _from, 1); \
	})

void __iowrite64_copy_full(void __iomem *to, const void *from, size_t count)
{
	memcpy_toio_aligned(to, from, count, 64);
	dgh();
}
EXPORT_SYMBOL(__iowrite64_copy_full);

void __iowrite32_copy_full(void __iomem *to, const void *from, size_t count)
{
	memcpy_toio_aligned(to, from, count, 32);
	dgh();
}
EXPORT_SYMBOL(__iowrite32_copy_full);
