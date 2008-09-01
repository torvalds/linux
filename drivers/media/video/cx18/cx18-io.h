/*
 *  cx18 driver PCI memory mapped IO access routines
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@radix.net>
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#ifndef CX18_IO_H
#define CX18_IO_H

#include "cx18-driver.h"

static inline void cx18_io_delay(struct cx18 *cx)
{
	if (cx->options.mmio_ndelay)
		ndelay(cx->options.mmio_ndelay);
}

/* Non byteswapping memory mapped IO */
static inline void cx18_raw_writel(struct cx18 *cx, u32 val, void __iomem *addr)
{
	__raw_writel(val, addr);
	cx18_io_delay(cx);
}

static inline u32 cx18_raw_readl(struct cx18 *cx, const void __iomem *addr)
{
	u32 ret = __raw_readl(addr);
	cx18_io_delay(cx);
	return ret;
}

static inline u16 cx18_raw_readw(struct cx18 *cx, const void __iomem *addr)
{
	u16 ret = __raw_readw(addr);
	cx18_io_delay(cx);
	return ret;
}

/* Normal memory mapped IO */
static inline void cx18_writel(struct cx18 *cx, u32 val, void __iomem *addr)
{
	writel(val, addr);
	cx18_io_delay(cx);
}

static inline void cx18_writew(struct cx18 *cx, u16 val, void __iomem *addr)
{
	writew(val, addr);
	cx18_io_delay(cx);
}

static inline void cx18_writeb(struct cx18 *cx, u8 val, void __iomem *addr)
{
	writeb(val, addr);
	cx18_io_delay(cx);
}

static inline u32 cx18_readl(struct cx18 *cx, const void __iomem *addr)
{
	u32 ret = readl(addr);
	cx18_io_delay(cx);
	return ret;
}

static inline u8 cx18_readb(struct cx18 *cx, const void __iomem *addr)
{
	u8 ret = readb(addr);
	cx18_io_delay(cx);
	return ret;
}

static inline u32 cx18_write_sync(struct cx18 *cx, u32 val, void __iomem *addr)
{
	cx18_writel(cx, val, addr);
	return cx18_readl(cx, addr);
}

void cx18_memcpy_fromio(struct cx18 *cx, void *to,
			const void __iomem *from, unsigned int len);
void cx18_memset_io(struct cx18 *cx, void __iomem *addr, int val, size_t count);

/* Access "register" region of CX23418 memory mapped I/O */
static inline void cx18_write_reg(struct cx18 *cx, u32 val, u32 reg)
{
	cx18_writel(cx, val, cx->reg_mem + reg);
}

static inline u32 cx18_read_reg(struct cx18 *cx, u32 reg)
{
	return cx18_readl(cx, cx->reg_mem + reg);
}

static inline u32 cx18_write_reg_sync(struct cx18 *cx, u32 val, u32 reg)
{
	return cx18_write_sync(cx, val, cx->reg_mem + reg);
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

static inline u32 cx18_write_enc_sync(struct cx18 *cx, u32 val, u32 addr)
{
	return cx18_write_sync(cx, val, cx->enc_mem + addr);
}


void cx18_sw1_irq_enable(struct cx18 *cx, u32 val);
void cx18_sw1_irq_disable(struct cx18 *cx, u32 val);
void cx18_sw2_irq_enable(struct cx18 *cx, u32 val);
void cx18_sw2_irq_disable(struct cx18 *cx, u32 val);
void cx18_setup_page(struct cx18 *cx, u32 addr);

/* Tries to recover from the CX23418 responding improperly on the PCI bus */
int cx18_pci_try_recover(struct cx18 *cx);

#endif /* CX18_IO_H */
