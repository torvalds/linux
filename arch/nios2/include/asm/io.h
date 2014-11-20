/*
 * Copyright (C) 2014 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_IO_H
#define _ASM_NIOS2_IO_H

#include <asm/pgtable-bits.h>
#include <asm-generic/iomap.h>

/* PCI is not supported in nios2, set this to 0. */
#define IO_SPACE_LIMIT 0

#define readb_relaxed(addr)	readb(addr)
#define readw_relaxed(addr)	readw(addr)
#define readl_relaxed(addr)	readl(addr)

#define writeb_relaxed(x, addr)	writeb(x, addr)
#define writew_relaxed(x, addr)	writew(x, addr)
#define writel_relaxed(x, addr)	writel(x, addr)

#include <asm-generic/io.h>

extern void __iomem *__ioremap(unsigned long physaddr, unsigned long size,
			unsigned long cacheflag);
extern void __iounmap(void __iomem *addr);

static inline void __iomem *ioremap(unsigned long physaddr, unsigned long size)
{
	return __ioremap(physaddr, size, 0);
}

static inline void __iomem *ioremap_nocache(unsigned long physaddr,
						unsigned long size)
{
	return __ioremap(physaddr, size, 0);
}

static inline void iounmap(void __iomem *addr)
{
	__iounmap(addr);
}

/* Pages to physical address... */
#define page_to_phys(page)	virt_to_phys(page_to_virt(page))
#define page_to_bus(page)	page_to_virt(page)

/* Macros used for converting between virtual and physical mappings. */
#define phys_to_virt(vaddr)	\
	((void *)((unsigned long)(vaddr) | CONFIG_NIOS2_KERNEL_REGION_BASE))
/* Clear top 3 bits */
#define virt_to_phys(vaddr)	\
	((unsigned long)((unsigned long)(vaddr) & ~0xE0000000))

#endif /* _ASM_NIOS2_IO_H */
