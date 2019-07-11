/*
 *  cx18 driver PCI memory mapped IO access routines
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef CX18_IO_H
#define CX18_IO_H

#include "cx18-driver.h"

/*
 * Readback and retry of MMIO access for reliability:
 * The concept was suggested by Steve Toth <stoth@linuxtv.org>.
 * The implementation is the fault of Andy Walls <awalls@md.metrocast.net>.
 *
 * *write* functions are implied to retry the mmio unless suffixed with _noretry
 * *read* functions never retry the mmio (it never helps to do so)
 */

/* Non byteswapping memory mapped IO */
static inline u32 cx18_raw_readl(struct cx18 *cx, const void __iomem *addr)
{
	return __raw_readl(addr);
}

static inline
void cx18_raw_writel_noretry(struct cx18 *cx, u32 val, void __iomem *addr)
{
	__raw_writel(val, addr);
}

static inline void cx18_raw_writel(struct cx18 *cx, u32 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_WR_RETRIES; i++) {
		cx18_raw_writel_noretry(cx, val, addr);
		if (val == cx18_raw_readl(cx, addr))
			break;
	}
}

/* Normal memory mapped IO */
static inline u32 cx18_readl(struct cx18 *cx, const void __iomem *addr)
{
	return readl(addr);
}

static inline
void cx18_writel_noretry(struct cx18 *cx, u32 val, void __iomem *addr)
{
	writel(val, addr);
}

static inline void cx18_writel(struct cx18 *cx, u32 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_WR_RETRIES; i++) {
		cx18_writel_noretry(cx, val, addr);
		if (val == cx18_readl(cx, addr))
			break;
	}
}

static inline
void cx18_writel_expect(struct cx18 *cx, u32 val, void __iomem *addr,
			u32 eval, u32 mask)
{
	int i;
	u32 r;
	eval &= mask;
	for (i = 0; i < CX18_MAX_MMIO_WR_RETRIES; i++) {
		cx18_writel_noretry(cx, val, addr);
		r = cx18_readl(cx, addr);
		if (r == 0xffffffff && eval != 0xffffffff)
			continue;
		if (eval == (r & mask))
			break;
	}
}

static inline u16 cx18_readw(struct cx18 *cx, const void __iomem *addr)
{
	return readw(addr);
}

static inline
void cx18_writew_noretry(struct cx18 *cx, u16 val, void __iomem *addr)
{
	writew(val, addr);
}

static inline void cx18_writew(struct cx18 *cx, u16 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_WR_RETRIES; i++) {
		cx18_writew_noretry(cx, val, addr);
		if (val == cx18_readw(cx, addr))
			break;
	}
}

static inline u8 cx18_readb(struct cx18 *cx, const void __iomem *addr)
{
	return readb(addr);
}

static inline
void cx18_writeb_noretry(struct cx18 *cx, u8 val, void __iomem *addr)
{
	writeb(val, addr);
}

static inline void cx18_writeb(struct cx18 *cx, u8 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_WR_RETRIES; i++) {
		cx18_writeb_noretry(cx, val, addr);
		if (val == cx18_readb(cx, addr))
			break;
	}
}

static inline
void cx18_memcpy_fromio(struct cx18 *cx, void *to,
			const void __iomem *from, unsigned int len)
{
	memcpy_fromio(to, from, len);
}

void cx18_memset_io(struct cx18 *cx, void __iomem *addr, int val, size_t count);


/* Access "register" region of CX23418 memory mapped I/O */
static inline void cx18_write_reg_noretry(struct cx18 *cx, u32 val, u32 reg)
{
	cx18_writel_noretry(cx, val, cx->reg_mem + reg);
}

static inline void cx18_write_reg(struct cx18 *cx, u32 val, u32 reg)
{
	cx18_writel(cx, val, cx->reg_mem + reg);
}

static inline void cx18_write_reg_expect(struct cx18 *cx, u32 val, u32 reg,
					 u32 eval, u32 mask)
{
	cx18_writel_expect(cx, val, cx->reg_mem + reg, eval, mask);
}

static inline u32 cx18_read_reg(struct cx18 *cx, u32 reg)
{
	return cx18_readl(cx, cx->reg_mem + reg);
}


/* Access "encoder memory" region of CX23418 memory mapped I/O */
static inline void cx18_write_enc(struct cx18 *cx, u32 val, u32 addr)
{
	cx18_writel(cx, val, cx->enc_mem + addr);
}

static inline u32 cx18_read_enc(struct cx18 *cx, u32 addr)
{
	return cx18_readl(cx, cx->enc_mem + addr);
}

void cx18_sw1_irq_enable(struct cx18 *cx, u32 val);
void cx18_sw1_irq_disable(struct cx18 *cx, u32 val);
void cx18_sw2_irq_enable(struct cx18 *cx, u32 val);
void cx18_sw2_irq_disable(struct cx18 *cx, u32 val);
void cx18_sw2_irq_disable_cpu(struct cx18 *cx, u32 val);
void cx18_setup_page(struct cx18 *cx, u32 addr);

#endif /* CX18_IO_H */
