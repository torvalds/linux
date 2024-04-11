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
 * Copy data from IO memory space to "real" memory space.
 */
void __memcpy_fromio(void *to, const volatile void __iomem *from, size_t count)
{
	while (count && !IS_ALIGNED((unsigned long)from, 8)) {
		*(u8 *)to = __raw_readb(from);
		from++;
		to++;
		count--;
	}

	while (count >= 8) {
		*(u64 *)to = __raw_readq(from);
		from += 8;
		to += 8;
		count -= 8;
	}

	while (count) {
		*(u8 *)to = __raw_readb(from);
		from++;
		to++;
		count--;
	}
}
EXPORT_SYMBOL(__memcpy_fromio);

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

/*
 * Copy data from "real" memory space to IO memory space.
 */
void __memcpy_toio(volatile void __iomem *to, const void *from, size_t count)
{
	while (count && !IS_ALIGNED((unsigned long)to, 8)) {
		__raw_writeb(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}

	while (count >= 8) {
		__raw_writeq(*(u64 *)from, to);
		from += 8;
		to += 8;
		count -= 8;
	}

	while (count) {
		__raw_writeb(*(u8 *)from, to);
		from++;
		to++;
		count--;
	}
}
EXPORT_SYMBOL(__memcpy_toio);

/*
 * "memset" on IO memory space.
 */
void __memset_io(volatile void __iomem *dst, int c, size_t count)
{
	u64 qc = (u8)c;

	qc |= qc << 8;
	qc |= qc << 16;
	qc |= qc << 32;

	while (count && !IS_ALIGNED((unsigned long)dst, 8)) {
		__raw_writeb(c, dst);
		dst++;
		count--;
	}

	while (count >= 8) {
		__raw_writeq(qc, dst);
		dst += 8;
		count -= 8;
	}

	while (count) {
		__raw_writeb(c, dst);
		dst++;
		count--;
	}
}
EXPORT_SYMBOL(__memset_io);
