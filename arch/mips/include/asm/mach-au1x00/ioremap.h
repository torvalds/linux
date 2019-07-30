/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/mach-au1x00/ioremap.h
 */
#ifndef __ASM_MACH_AU1X00_IOREMAP_H
#define __ASM_MACH_AU1X00_IOREMAP_H

#include <linux/types.h>

#if defined(CONFIG_PHYS_ADDR_T_64BIT) && defined(CONFIG_PCI)
extern phys_addr_t __fixup_bigphys_addr(phys_addr_t, phys_addr_t);
#else
static inline phys_addr_t __fixup_bigphys_addr(phys_addr_t phys_addr, phys_addr_t size)
{
	return phys_addr;
}
#endif

/*
 * Allow physical addresses to be fixed up to help 36-bit peripherals.
 */
static inline phys_addr_t fixup_bigphys_addr(phys_addr_t phys_addr, phys_addr_t size)
{
	return __fixup_bigphys_addr(phys_addr, size);
}

static inline void __iomem *plat_ioremap(phys_addr_t offset, unsigned long size,
	unsigned long flags)
{
	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return 0;
}

#endif /* __ASM_MACH_AU1X00_IOREMAP_H */
