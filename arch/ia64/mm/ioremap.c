/*
 * (c) Copyright 2006 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <asm/io.h>
#include <asm/meminit.h>

static inline void __iomem *
__ioremap (unsigned long phys_addr, unsigned long size)
{
	return (void __iomem *) (__IA64_UNCACHED_OFFSET | phys_addr);
}

void __iomem *
ioremap (unsigned long phys_addr, unsigned long size)
{
	u64 attr;
	unsigned long gran_base, gran_size;

	/*
	 * For things in kern_memmap, we must use the same attribute
	 * as the rest of the kernel.  For more details, see
	 * Documentation/ia64/aliasing.txt.
	 */
	attr = kern_mem_attribute(phys_addr, size);
	if (attr & EFI_MEMORY_WB)
		return (void __iomem *) phys_to_virt(phys_addr);
	else if (attr & EFI_MEMORY_UC)
		return __ioremap(phys_addr, size);

	/*
	 * Some chipsets don't support UC access to memory.  If
	 * WB is supported for the whole granule, we prefer that.
	 */
	gran_base = GRANULEROUNDDOWN(phys_addr);
	gran_size = GRANULEROUNDUP(phys_addr + size) - gran_base;
	if (efi_mem_attribute(gran_base, gran_size) & EFI_MEMORY_WB)
		return (void __iomem *) phys_to_virt(phys_addr);

	return __ioremap(phys_addr, size);
}
EXPORT_SYMBOL(ioremap);

void __iomem *
ioremap_nocache (unsigned long phys_addr, unsigned long size)
{
	if (kern_mem_attribute(phys_addr, size) & EFI_MEMORY_WB)
		return NULL;

	return __ioremap(phys_addr, size);
}
EXPORT_SYMBOL(ioremap_nocache);
