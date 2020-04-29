/*
 * include/asm-xtensa/io.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_IO_H
#define _XTENSA_IO_H

#include <asm/byteorder.h>
#include <asm/page.h>
#include <asm/vectors.h>
#include <linux/bug.h>
#include <linux/kernel.h>

#include <linux/types.h>

#define IOADDR(x)		(XCHAL_KIO_BYPASS_VADDR + (x))
#define IO_SPACE_LIMIT ~0
#define PCI_IOBASE		((void __iomem *)XCHAL_KIO_BYPASS_VADDR)

#ifdef CONFIG_MMU

void __iomem *xtensa_ioremap_nocache(unsigned long addr, unsigned long size);
void __iomem *xtensa_ioremap_cache(unsigned long addr, unsigned long size);
void xtensa_iounmap(volatile void __iomem *addr);

/*
 * Return the virtual address for the specified bus memory.
 */
static inline void __iomem *ioremap(unsigned long offset, unsigned long size)
{
	if (offset >= XCHAL_KIO_PADDR
	    && offset - XCHAL_KIO_PADDR < XCHAL_KIO_SIZE)
		return (void*)(offset-XCHAL_KIO_PADDR+XCHAL_KIO_BYPASS_VADDR);
	else
		return xtensa_ioremap_nocache(offset, size);
}

static inline void __iomem *ioremap_cache(unsigned long offset,
		unsigned long size)
{
	if (offset >= XCHAL_KIO_PADDR
	    && offset - XCHAL_KIO_PADDR < XCHAL_KIO_SIZE)
		return (void*)(offset-XCHAL_KIO_PADDR+XCHAL_KIO_CACHED_VADDR);
	else
		return xtensa_ioremap_cache(offset, size);
}
#define ioremap_cache ioremap_cache

static inline void iounmap(volatile void __iomem *addr)
{
	unsigned long va = (unsigned long) addr;

	if (!(va >= XCHAL_KIO_CACHED_VADDR &&
	      va - XCHAL_KIO_CACHED_VADDR < XCHAL_KIO_SIZE) &&
	    !(va >= XCHAL_KIO_BYPASS_VADDR &&
	      va - XCHAL_KIO_BYPASS_VADDR < XCHAL_KIO_SIZE))
		xtensa_iounmap(addr);
}

#define virt_to_bus     virt_to_phys
#define bus_to_virt     phys_to_virt

#endif /* CONFIG_MMU */

#include <asm-generic/io.h>

#endif	/* _XTENSA_IO_H */
