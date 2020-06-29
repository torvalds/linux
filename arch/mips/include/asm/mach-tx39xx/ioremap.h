/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/mach-tx39xx/ioremap.h
 */
#ifndef __ASM_MACH_TX39XX_IOREMAP_H
#define __ASM_MACH_TX39XX_IOREMAP_H

#include <linux/types.h>

static inline void __iomem *plat_ioremap(phys_addr_t offset, unsigned long size,
	unsigned long flags)
{
#define TXX9_DIRECTMAP_BASE	0xff000000ul
	if (offset >= TXX9_DIRECTMAP_BASE &&
	    offset < TXX9_DIRECTMAP_BASE + 0xff0000)
		return (void __iomem *)offset;
	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return (unsigned long)addr >= TXX9_DIRECTMAP_BASE;
}

#endif /* __ASM_MACH_TX39XX_IOREMAP_H */
