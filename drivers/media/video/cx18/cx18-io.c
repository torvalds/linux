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

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-irq.h"

void cx18_log_statistics(struct cx18 *cx)
{
	int i;

	if (!(cx18_debug & CX18_DBGFLG_INFO))
		return;

	for (i = 0; i <= CX18_MAX_MMIO_RETRIES; i++)
		CX18_DEBUG_INFO("retried_write[%d] = %d\n", i,
				atomic_read(&cx->mmio_stats.retried_write[i]));
	for (i = 0; i <= CX18_MAX_MMIO_RETRIES; i++)
		CX18_DEBUG_INFO("retried_read[%d] = %d\n", i,
				atomic_read(&cx->mmio_stats.retried_read[i]));
	return;
}

void cx18_raw_writel_retry(struct cx18 *cx, u32 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		cx18_raw_writel_noretry(cx, val, addr);
		if (val == cx18_raw_readl_noretry(cx, addr))
			break;
	}
	cx18_log_write_retries(cx, i, addr);
}

u32 cx18_raw_readl_retry(struct cx18 *cx, const void __iomem *addr)
{
	int i;
	u32 val;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		val = cx18_raw_readl_noretry(cx, addr);
		if (val != 0xffffffff) /* PCI bus read error */
			break;
	}
	cx18_log_read_retries(cx, i, addr);
	return val;
}

u16 cx18_raw_readw_retry(struct cx18 *cx, const void __iomem *addr)
{
	int i;
	u16 val;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		val = cx18_raw_readw_noretry(cx, addr);
		if (val != 0xffff) /* PCI bus read error */
			break;
	}
	cx18_log_read_retries(cx, i, addr);
	return val;
}

void cx18_writel_retry(struct cx18 *cx, u32 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		cx18_writel_noretry(cx, val, addr);
		if (val == cx18_readl_noretry(cx, addr))
			break;
	}
	cx18_log_write_retries(cx, i, addr);
}

void cx18_writew_retry(struct cx18 *cx, u16 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		cx18_writew_noretry(cx, val, addr);
		if (val == cx18_readw_noretry(cx, addr))
			break;
	}
	cx18_log_write_retries(cx, i, addr);
}

void cx18_writeb_retry(struct cx18 *cx, u8 val, void __iomem *addr)
{
	int i;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		cx18_writeb_noretry(cx, val, addr);
		if (val == cx18_readb_noretry(cx, addr))
			break;
	}
	cx18_log_write_retries(cx, i, addr);
}

u32 cx18_readl_retry(struct cx18 *cx, const void __iomem *addr)
{
	int i;
	u32 val;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		val = cx18_readl_noretry(cx, addr);
		if (val != 0xffffffff) /* PCI bus read error */
			break;
	}
	cx18_log_read_retries(cx, i, addr);
	return val;
}

u16 cx18_readw_retry(struct cx18 *cx, const void __iomem *addr)
{
	int i;
	u16 val;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		val = cx18_readw_noretry(cx, addr);
		if (val != 0xffff) /* PCI bus read error */
			break;
	}
	cx18_log_read_retries(cx, i, addr);
	return val;
}

u8 cx18_readb_retry(struct cx18 *cx, const void __iomem *addr)
{
	int i;
	u8 val;
	for (i = 0; i < CX18_MAX_MMIO_RETRIES; i++) {
		val = cx18_readb_noretry(cx, addr);
		if (val != 0xff) /* PCI bus read error */
			break;
	}
	cx18_log_read_retries(cx, i, addr);
	return val;
}

void cx18_memcpy_fromio(struct cx18 *cx, void *to,
			const void __iomem *from, unsigned int len)
{
	const u8 __iomem *src = from;
	u8 *dst = to;

	/* Align reads on the CX23418's addresses */
	if ((len > 0) && ((unsigned long) src & 1)) {
		*dst = cx18_readb(cx, src);
		len--;
		dst++;
		src++;
	}
	if ((len > 1) && ((unsigned long) src & 2)) {
		*((u16 *)dst) = cx18_raw_readw(cx, src);
		len -= 2;
		dst += 2;
		src += 2;
	}
	while (len > 3) {
		*((u32 *)dst) = cx18_raw_readl(cx, src);
		len -= 4;
		dst += 4;
		src += 4;
	}
	if (len > 1) {
		*((u16 *)dst) = cx18_raw_readw(cx, src);
		len -= 2;
		dst += 2;
		src += 2;
	}
	if (len > 0)
		*dst = cx18_readb(cx, src);
}

void cx18_memset_io(struct cx18 *cx, void __iomem *addr, int val, size_t count)
{
	u8 __iomem *dst = addr;
	u16 val2 = val | (val << 8);
	u32 val4 = val2 | (val2 << 16);

	/* Align writes on the CX23418's addresses */
	if ((count > 0) && ((unsigned long)dst & 1)) {
		cx18_writeb(cx, (u8) val, dst);
		count--;
		dst++;
	}
	if ((count > 1) && ((unsigned long)dst & 2)) {
		cx18_writew(cx, val2, dst);
		count -= 2;
		dst += 2;
	}
	while (count > 3) {
		cx18_writel(cx, val4, dst);
		count -= 4;
		dst += 4;
	}
	if (count > 1) {
		cx18_writew(cx, val2, dst);
		count -= 2;
		dst += 2;
	}
	if (count > 0)
		cx18_writeb(cx, (u8) val, dst);
}

void cx18_sw1_irq_enable(struct cx18 *cx, u32 val)
{
	u32 r;
	cx18_write_reg(cx, val, SW1_INT_STATUS);
	r = cx18_read_reg(cx, SW1_INT_ENABLE_PCI);
	cx18_write_reg(cx, r | val, SW1_INT_ENABLE_PCI);
}

void cx18_sw1_irq_disable(struct cx18 *cx, u32 val)
{
	u32 r;
	r = cx18_read_reg(cx, SW1_INT_ENABLE_PCI);
	cx18_write_reg(cx, r & ~val, SW1_INT_ENABLE_PCI);
}

void cx18_sw2_irq_enable(struct cx18 *cx, u32 val)
{
	u32 r;
	cx18_write_reg(cx, val, SW2_INT_STATUS);
	r = cx18_read_reg(cx, SW2_INT_ENABLE_PCI);
	cx18_write_reg(cx, r | val, SW2_INT_ENABLE_PCI);
}

void cx18_sw2_irq_disable(struct cx18 *cx, u32 val)
{
	u32 r;
	r = cx18_read_reg(cx, SW2_INT_ENABLE_PCI);
	cx18_write_reg(cx, r & ~val, SW2_INT_ENABLE_PCI);
}

void cx18_setup_page(struct cx18 *cx, u32 addr)
{
	u32 val;
	val = cx18_read_reg(cx, 0xD000F8);
	val = (val & ~0x1f00) | ((addr >> 17) & 0x1f00);
	cx18_write_reg(cx, val, 0xD000F8);
}
