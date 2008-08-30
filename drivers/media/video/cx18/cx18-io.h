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

/* This is a PCI post thing, where if the pci register is not read, then
   the write doesn't always take effect right away. By reading back the
   register any pending PCI writes will be performed (in order), and so
   you can be sure that the writes are guaranteed to be done.

   Rarely needed, only in some timing sensitive cases.
   Apparently if this is not done some motherboards seem
   to kill the firmware and get into the broken state until computer is
   rebooted. */
u32 cx18_write_sync(struct cx18 *cx, u32 val, void __iomem *addr);

void cx18_writel(struct cx18 *cx, u32 val, void __iomem *addr);
u32 cx18_readl(struct cx18 *cx, const void __iomem *addr);

/* No endiannes conversion calls */
void cx18_raw_writel(struct cx18 *cx, u32 val, void __iomem *addr);
u32 cx18_raw_readl(struct cx18 *cx, const void __iomem *addr);

/* Access "register" region of CX23418 memory mapped I/O */
u32 cx18_read_reg(struct cx18 *cx, u32 reg);
void cx18_write_reg(struct cx18 *cx, u32 val, u32 reg);
u32 cx18_write_reg_sync(struct cx18 *cx, u32 val, u32 reg);

/* Access "encoder memory" region of CX23418 memory mapped I/O */
u32 cx18_read_enc(struct cx18 *cx, u32 addr);
void cx18_write_enc(struct cx18 *cx, u32 val, u32 addr);
u32 cx18_write_enc_sync(struct cx18 *cx, u32 val, u32 addr);

void cx18_memcpy_fromio(struct cx18 *cx, void *to,
			const void __iomem *from, unsigned int len);
void cx18_memset_io(struct cx18 *cx, void __iomem *addr, int val, size_t count);

void cx18_sw1_irq_enable(struct cx18 *cx, u32 val);
void cx18_sw1_irq_disable(struct cx18 *cx, u32 val);
void cx18_sw2_irq_enable(struct cx18 *cx, u32 val);
void cx18_sw2_irq_disable(struct cx18 *cx, u32 val);
void cx18_setup_page(struct cx18 *cx, u32 addr);

/* Tries to recover from the CX23418 responding improperly on the PCI bus */
int cx18_pci_try_recover(struct cx18 *cx);

#endif /* CX18_IO_H */
