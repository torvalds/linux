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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-io.h"
#include "cx18-irq.h"

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
	cx18_write_reg_expect(cx, val, SW1_INT_STATUS, ~val, val);
	cx->sw1_irq_mask = cx18_read_reg(cx, SW1_INT_ENABLE_PCI) | val;
	cx18_write_reg(cx, cx->sw1_irq_mask, SW1_INT_ENABLE_PCI);
}

void cx18_sw1_irq_disable(struct cx18 *cx, u32 val)
{
	cx->sw1_irq_mask = cx18_read_reg(cx, SW1_INT_ENABLE_PCI) & ~val;
	cx18_write_reg(cx, cx->sw1_irq_mask, SW1_INT_ENABLE_PCI);
}

void cx18_sw2_irq_enable(struct cx18 *cx, u32 val)
{
	cx18_write_reg_expect(cx, val, SW2_INT_STATUS, ~val, val);
	cx->sw2_irq_mask = cx18_read_reg(cx, SW2_INT_ENABLE_PCI) | val;
	cx18_write_reg(cx, cx->sw2_irq_mask, SW2_INT_ENABLE_PCI);
}

void cx18_sw2_irq_disable(struct cx18 *cx, u32 val)
{
	cx->sw2_irq_mask = cx18_read_reg(cx, SW2_INT_ENABLE_PCI) & ~val;
	cx18_write_reg(cx, cx->sw2_irq_mask, SW2_INT_ENABLE_PCI);
}

void cx18_sw2_irq_disable_cpu(struct cx18 *cx, u32 val)
{
	u32 r;
	r = cx18_read_reg(cx, SW2_INT_ENABLE_CPU);
	cx18_write_reg(cx, r & ~val, SW2_INT_ENABLE_CPU);
}

void cx18_setup_page(struct cx18 *cx, u32 addr)
{
	u32 val;
	val = cx18_read_reg(cx, 0xD000F8);
	val = (val & ~0x1f00) | ((addr >> 17) & 0x1f00);
	cx18_write_reg(cx, val, 0xD000F8);
}
