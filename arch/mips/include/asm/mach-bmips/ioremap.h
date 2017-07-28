#ifndef __ASM_MACH_BMIPS_IOREMAP_H
#define __ASM_MACH_BMIPS_IOREMAP_H

#include <linux/types.h>

static inline phys_addr_t fixup_bigphys_addr(phys_addr_t phys_addr, phys_addr_t size)
{
	return phys_addr;
}

static inline int is_bmips_internal_registers(phys_addr_t offset)
{
	if (offset >= 0xfff80000)
		return 1;

	return 0;
}

static inline void __iomem *plat_ioremap(phys_addr_t offset, unsigned long size,
					 unsigned long flags)
{
	if (is_bmips_internal_registers(offset))
		return (void __iomem *)offset;

	return NULL;
}

static inline int plat_iounmap(const volatile void __iomem *addr)
{
	return is_bmips_internal_registers((unsigned long)addr);
}

#endif /* __ASM_MACH_BMIPS_IOREMAP_H */
