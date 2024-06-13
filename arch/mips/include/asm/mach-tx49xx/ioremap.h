/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/mach-tx49xx/ioremap.h
 */
#ifndef __ASM_MACH_TX49XX_IOREMAP_H
#define __ASM_MACH_TX49XX_IOREMAP_H

#include <linux/types.h>

static inline void __iomem *plat_ioremap(phys_addr_t offset, unsigned long size,
	unsigned long flags)
{
#ifdef CONFIG_64BIT
#define TXX9_DIRECTMAP_BASE	0xfff000000ul
#else
#define TXX9_DIRECTMAP_BASE	0xff000000ul
#endif
	if (offset >= TXX9_DIRECTMAP_BASE &&
	    offset < TXX9_DIRECTMAP_BASE + 0x400000)
		return (void __iomem *)(unsigned long)(int)offset;
	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return (unsigned long)addr >=
		(unsigned long)(int)(TXX9_DIRECTMAP_BASE & 0xffffffff);
}

#endif /* __ASM_MACH_TX49XX_IOREMAP_H */
