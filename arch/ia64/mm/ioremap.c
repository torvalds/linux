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

static inline void __iomem *
__ioremap (unsigned long offset, unsigned long size)
{
	return (void __iomem *) (__IA64_UNCACHED_OFFSET | offset);
}

void __iomem *
ioremap (unsigned long offset, unsigned long size)
{
	if (efi_mem_attribute_range(offset, size, EFI_MEMORY_WB))
		return phys_to_virt(offset);

	if (efi_mem_attribute_range(offset, size, EFI_MEMORY_UC))
		return __ioremap(offset, size);

	/*
	 * Someday this should check ACPI resources so we
	 * can do the right thing for hot-plugged regions.
	 */
	return __ioremap(offset, size);
}
EXPORT_SYMBOL(ioremap);

void __iomem *
ioremap_nocache (unsigned long offset, unsigned long size)
{
	return __ioremap(offset, size);
}
EXPORT_SYMBOL(ioremap_nocache);
