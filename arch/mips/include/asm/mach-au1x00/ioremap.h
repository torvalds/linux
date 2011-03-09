/*
 *	include/asm-mips/mach-au1x00/ioremap.h
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_MACH_AU1X00_IOREMAP_H
#define __ASM_MACH_AU1X00_IOREMAP_H

#include <linux/types.h>

#if defined(CONFIG_64BIT_PHYS_ADDR) && defined(CONFIG_PCI)
extern phys_t __fixup_bigphys_addr(phys_t, phys_t);
#else
static inline phys_t __fixup_bigphys_addr(phys_t phys_addr, phys_t size)
{
	return phys_addr;
}
#endif

/*
 * Allow physical addresses to be fixed up to help 36-bit peripherals.
 */
static inline phys_t fixup_bigphys_addr(phys_t phys_addr, phys_t size)
{
	return __fixup_bigphys_addr(phys_addr, size);
}

static inline void __iomem *plat_ioremap(phys_t offset, unsigned long size,
	unsigned long flags)
{
	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return 0;
}

#endif /* __ASM_MACH_AU1X00_IOREMAP_H */
