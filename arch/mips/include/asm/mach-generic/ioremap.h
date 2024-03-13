/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/mach-generic/ioremap.h
 */
#ifndef __ASM_MACH_GENERIC_IOREMAP_H
#define __ASM_MACH_GENERIC_IOREMAP_H

#include <linux/types.h>

static inline void __iomem *plat_ioremap(phys_addr_t offset, unsigned long size,
	unsigned long flags)
{
	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return 0;
}

#endif /* __ASM_MACH_GENERIC_IOREMAP_H */
