/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_CMPXCHG_XCHG_H
#define __ASM_SH_CMPXCHG_XCHG_H

/*
 * Copyright (C) 2016 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 */
#include <linux/bits.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>

/*
 * Portable implementations of 1 and 2 byte xchg using a 4 byte cmpxchg.
 * Note: this header isn't self-contained: before including it, __cmpxchg_u32
 * must be defined first.
 */
static inline u32 __xchg_cmpxchg(volatile void *ptr, u32 x, int size)
{
	int off = (unsigned long)ptr % sizeof(u32);
	volatile u32 *p = ptr - off;
#ifdef __BIG_ENDIAN
	int bitoff = (sizeof(u32) - size - off) * BITS_PER_BYTE;
#else
	int bitoff = off * BITS_PER_BYTE;
#endif
	u32 bitmask = ((0x1 << size * BITS_PER_BYTE) - 1) << bitoff;
	u32 oldv, newv;
	u32 ret;

	do {
		oldv = READ_ONCE(*p);
		ret = (oldv & bitmask) >> bitoff;
		newv = (oldv & ~bitmask) | (x << bitoff);
	} while (__cmpxchg_u32(p, oldv, newv) != oldv);

	return ret;
}

static inline unsigned long xchg_u16(volatile u16 *m, unsigned long val)
{
	return __xchg_cmpxchg(m, val, sizeof *m);
}

static inline unsigned long xchg_u8(volatile u8 *m, unsigned long val)
{
	return __xchg_cmpxchg(m, val, sizeof *m);
}

#endif /* __ASM_SH_CMPXCHG_XCHG_H */
