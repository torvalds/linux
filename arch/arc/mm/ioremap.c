// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
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
	phys_addr_t end;

	/* Don't allow wraparound or zero size */
	end = paddr + size - 1;
	if (!size || (end < paddr))
		return NULL;

	/*
	 * If the region is h/w uncached, MMU mapping can be elided as optim
	 * The cast to u32 is fine as this region can only be inside 4GB
	 */
	if (arc_uncached_addr_space(paddr))
		return (void __iomem *)(u32)paddr;

	return ioremap_prot(paddr, size, PAGE_KERNEL_NO_CACHE);
}
EXPORT_SYMBOL(ioremap);

/*
 * ioremap with access flags
 * Cache semantics wise it is same as ioremap - "forced" uncached.
 * However unlike vanilla ioremap which bypasses ARC MMU for addresses in
 * ARC hardware uncached region, this one still goes thru the MMU as caller
 * might need finer access control (R/W/X)
 */
void __iomem *ioremap_prot(phys_addr_t paddr, unsigned long size,
			   unsigned long flags)
{
	unsigned int off;
	unsigned long vaddr;
	struct vm_struct *area;
	phys_addr_t end;
	pgprot_t prot = __pgprot(flags);

	/* Don't allow wraparound, zero size */
	end = paddr + size - 1;
	if ((!size) || (end < paddr))
		return NULL;

	/* An early platform driver might end up here */
	if (!slab_is_available())
		return NULL;

	/* force uncached */
	prot = pgprot_noncached(prot);

	/* Mappings have to be page-aligned */
	off = paddr & ~PAGE_MASK;
	paddr &= PAGE_MASK_PHYS;
	size = PAGE_ALIGN(end + 1) - paddr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	area->phys_addr = paddr;
	vaddr = (unsigned long)area->addr;
	if (ioremap_page_range(vaddr, vaddr + size, paddr, prot)) {
		vunmap((void __force *)vaddr);
		return NULL;
	}
	return (void __iomem *)(off + (char __iomem *)vaddr);
}
EXPORT_SYMBOL(ioremap_prot);


void iounmap(const volatile void __iomem *addr)
{
	/* weird double cast to handle phys_addr_t > 32 bits */
	if (arc_uncached_addr_space((phys_addr_t)(u32)addr))
		return;

	vfree((void *)(PAGE_MASK & (unsigned long __force)addr));
}
EXPORT_SYMBOL(iounmap);
