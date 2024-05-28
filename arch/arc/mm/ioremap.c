// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/cache.h>

static inline bool arc_uncached_addr_space(phys_addr_t paddr)
{
	if (is_isa_arcompact()) {
		if (paddr >= ARC_UNCACHED_ADDR_SPACE)
			return true;
	} else if (paddr >= perip_base && paddr <= perip_end) {
		return true;
	}

	return false;
}

void __iomem *ioremap(phys_addr_t paddr, unsigned long size)
{
	/*
	 * If the region is h/w uncached, MMU mapping can be elided as optim
	 * The cast to u32 is fine as this region can only be inside 4GB
	 */
	if (arc_uncached_addr_space(paddr))
		return (void __iomem *)(u32)paddr;

	return ioremap_prot(paddr, size,
			    pgprot_val(pgprot_noncached(PAGE_KERNEL)));
}
EXPORT_SYMBOL(ioremap);

/*
 * ioremap with access flags
 * Cache semantics wise it is same as ioremap - "forced" uncached.
 * However unlike vanilla ioremap which bypasses ARC MMU for addresses in
 * ARC hardware uncached region, this one still goes thru the MMU as caller
 * might need finer access control (R/W/X)
 */
void __iomem *ioremap_prot(phys_addr_t paddr, size_t size,
			   unsigned long flags)
{
	pgprot_t prot = __pgprot(flags);

	/* force uncached */
	return generic_ioremap_prot(paddr, size, pgprot_noncached(prot));
}
EXPORT_SYMBOL(ioremap_prot);

void iounmap(volatile void __iomem *addr)
{
	/* weird double cast to handle phys_addr_t > 32 bits */
	if (arc_uncached_addr_space((phys_addr_t)(u32)addr))
		return;

	generic_iounmap(addr);
}
EXPORT_SYMBOL(iounmap);
