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
#include "cx18-irq.h"

void cx18_raw_writel(struct cx18 *cx, u32 val, void __iomem *addr)
{
	__raw_writel(val, addr);
}

u32 cx18_raw_readl(struct cx18 *cx, const void __iomem *addr)
{
	return __raw_readl(addr);
}

u32 cx18_write_sync(struct cx18 *cx, u32 val, void __iomem *addr)
{
	writel(val, addr);
	return readl(addr);
}

void cx18_writel(struct cx18 *cx, u32 val, void __iomem *addr)
{
	writel(val, addr);
}

u32 cx18_readl(struct cx18 *cx, const void __iomem *addr)
{
	return readl(addr);
}


/* Access "register" region of CX23418 memory mapped I/O */
u32 cx18_read_reg(struct cx18 *cx, u32 reg)
{
	return readl(cx->reg_mem + reg);
}

void cx18_write_reg(struct cx18 *cx, u32 val, u32 reg)
{
	writel(val, cx->reg_mem + reg);
}

u32 cx18_write_reg_sync(struct cx18 *cx, u32 val, u32 reg)
{
	return cx18_write_sync(cx, val, cx->reg_mem + reg);
}

/* Access "encoder memory" region of CX23418 memory mapped I/O */
u32 cx18_read_enc(struct cx18 *cx, u32 addr)
{
	return readl(cx->enc_mem + addr);
}

void cx18_write_enc(struct cx18 *cx, u32 val, u32 addr)
{
	writel(val, cx->enc_mem + addr);
}

u32 cx18_write_enc_sync(struct cx18 *cx, u32 val, u32 addr)
{
	return cx18_write_sync(cx, val, cx->enc_mem + addr);
}

void cx18_memcpy_fromio(struct cx18 *cx, void *to,
			const void __iomem *from, unsigned int len)
{
	memcpy_fromio(to, from, len);
}

void cx18_memset_io(struct cx18 *cx, void __iomem *addr, int val, size_t count)
{
	memset_io(addr, val, count);
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

/* Tries to recover from the CX23418 responding improperly on the PCI bus */
int cx18_pci_try_recover(struct cx18 *cx)
{
	u16 status;

	pci_read_config_word(cx->dev, PCI_STATUS, &status);
	pci_write_config_word(cx->dev, PCI_STATUS, status);
	return 0;
}
