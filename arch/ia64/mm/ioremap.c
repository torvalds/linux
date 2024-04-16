// SPDX-License-Identifier: GPL-2.0-only
/*
 * (c) Copyright 2006, 2007 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/meminit.h>

static inline void __iomem *
__ioremap_uc(unsigned long phys_addr)
{
	return (void __iomem *) (__IA64_UNCACHED_OFFSET | phys_addr);
}

void __iomem *
early_ioremap (unsigned long phys_addr, unsigned long size)
{
	u64 attr;
	attr = kern_mem_attribute(phys_addr, size);
	if (attr & EFI_MEMORY_WB)
		return (void __iomem *) phys_to_virt(phys_addr);
	return __ioremap_uc(phys_addr);
}

void __iomem *
ioremap (unsigned long phys_addr, unsigned long size)
{
	void __iomem *addr;
	struct vm_struct *area;
	unsigned long offset;
	pgprot_t prot;
	u64 attr;
	unsigned long gran_base, gran_size;
	unsigned long page_base;

	/*
	 * For things in kern_memmap, we must use the same attribute
	 * as the rest of the kernel.  For more details, see
	 * Documentation/ia64/aliasing.rst.
	 */
	attr = kern_mem_attribute(phys_addr, size);
	if (attr & EFI_MEMORY_WB)
		return (void __iomem *) phys_to_virt(phys_addr);
	else if (attr & EFI_MEMORY_UC)
		return __ioremap_uc(phys_addr);

	/*
	 * Some chipsets don't support UC access to memory.  If
	 * WB is supported for the whole granule, we prefer that.
	 */
	gran_base = GRANULEROUNDDOWN(phys_addr);
	gran_size = GRANULEROUNDUP(phys_addr + size) - gran_base;
	if (efi_mem_attribute(gran_base, gran_size) & EFI_MEMORY_WB)
		return (void __iomem *) phys_to_virt(phys_addr);

	/*
	 * WB is not supported for the whole granule, so we can't use
	 * the region 7 identity mapping.  If we can safely cover the
	 * area with kernel page table mappings, we can use those
	 * instead.
	 */
	page_base = phys_addr & PAGE_MASK;
	size = PAGE_ALIGN(phys_addr + size) - page_base;
	if (efi_mem_attribute(page_base, size) & EFI_MEMORY_WB) {
		prot = PAGE_KERNEL;

		/*
		 * Mappings have to be page-aligned
		 */
		offset = phys_addr & ~PAGE_MASK;
		phys_addr &= PAGE_MASK;

		/*
		 * Ok, go for it..
		 */
		area = get_vm_area(size, VM_IOREMAP);
		if (!area)
			return NULL;

		area->phys_addr = phys_addr;
		addr = (void __iomem *) area->addr;
		if (ioremap_page_range((unsigned long) addr,
				(unsigned long) addr + size, phys_addr, prot)) {
			vunmap((void __force *) addr);
			return NULL;
		}

		return (void __iomem *) (offset + (char __iomem *)addr);
	}

	return __ioremap_uc(phys_addr);
}
EXPORT_SYMBOL(ioremap);

void __iomem *
ioremap_uc(unsigned long phys_addr, unsigned long size)
{
	if (kern_mem_attribute(phys_addr, size) & EFI_MEMORY_WB)
		return NULL;

	return __ioremap_uc(phys_addr);
}
EXPORT_SYMBOL(ioremap_uc);

void
early_iounmap (volatile void __iomem *addr, unsigned long size)
{
}

void
iounmap (volatile void __iomem *addr)
{
	if (REGION_NUMBER(addr) == RGN_GATE)
		vunmap((void *) ((unsigned long) addr & PAGE_MASK));
}
EXPORT_SYMBOL(iounmap);
