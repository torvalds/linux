/*
 *
 *  Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

#include <mach/hardware.h>

#define IO_SPACE_LIMIT 0xffffffff

#define __io(a)         ((void __iomem *)HW_IO_PHYS_TO_VIRT(a))

/* Do not enable mem_pci for a big endian arm architecture or unexpected byteswaps will */
/* happen in readw/writew etc. */

#define readb(c)        __raw_readb(c)
#define readw(c)        __raw_readw(c)
#define readl(c)        __raw_readl(c)
#define readb_relaxed(addr) readb(addr)
#define readw_relaxed(addr) readw(addr)
#define readl_relaxed(addr) readl(addr)

#define readsb(p, d, l)   __raw_readsb(p, d, l)
#define readsw(p, d, l)   __raw_readsw(p, d, l)
#define readsl(p, d, l)   __raw_readsl(p, d, l)

#define writeb(v, c)     __raw_writeb(v, c)
#define writew(v, c)     __raw_writew(v, c)
#define writel(v, c)     __raw_writel(v, c)

#define writesb(p, d, l)  __raw_writesb(p, d, l)
#define writesw(p, d, l)  __raw_writesw(p, d, l)
#define writesl(p, d, l)  __raw_writesl(p, d, l)

#define memset_io(c, v, l)    _memset_io((c), (v), (l))
#define memcpy_fromio(a, c, l)    _memcpy_fromio((a), (c), (l))
#define memcpy_toio(c, a, l)  _memcpy_toio((c), (a), (l))

#define eth_io_copy_and_sum(s, c, l, b) eth_copy_and_sum((s), (c), (l), (b))

#endif
