/*
 * arch/sh/mm/ioremap.c
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2005 - 2010  Paul Mundt
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 */
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/addrspace.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mmu.h>

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem * __ref
__ioremap_caller(phys_addr_t phys_addr, unsigned long size,
		 pgprot_t pgprot, void *caller)
{
	struct vm_struct *area;
	unsigned long offset, last_addr, addr, orig_addr;
	void __iomem *mapped;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * If we can't yet use the regular approach, go the fixmap route.
	 */
	if (!mem_init_done)
		return ioremap_fixed(phys_addr, size, pgprot);

	/*
	 * First try to remap through the PMB.
	 * PMB entries are all pre-faulted.
	 */
	mapped = pmb_remap_caller(phys_addr, size, pgprot, caller);
	if (mapped && !IS_ERR(mapped))
		return mapped;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	orig_addr = addr = (unsigned long)area->addr;

	if (ioremap_page_range(addr, addr + size, phys_addr, pgprot)) {
		vunmap((void *)orig_addr);
		return NULL;
	}

	return (void __iomem *)(offset + (char *)orig_addr);
}
EXPORT_SYMBOL(__ioremap_caller);

/*
 * Simple checks for non-translatable mappings.
 */
static inline int iomapping_nontranslatable(unsigned long offset)
{
#ifdef CONFIG_29BIT
	/*
	 * In 29-bit mode this includes the fixed P1/P2 areas, as well as
	 * parts of P3.
	 */
	if (PXSEG(offset) < P3SEG || offset >= P3_ADDR_MAX)
		return 1;
#endif

	return 0;
}

void __iounmap(void __iomem *addr)
{
	unsigned long vaddr = (unsigned long __force)addr;
	struct vm_struct *p;

	/*
	 * Nothing to do if there is no translatable mapping.
	 */
	if (iomapping_nontranslatable(vaddr))
		return;

	/*
	 * There's no VMA if it's from an early fixed mapping.
	 */
	if (iounmap_fixed(addr) == 0)
		return;

	/*
	 * If the PMB handled it, there's nothing else to do.
	 */
	if (pmb_unmap(addr) == 0)
		return;

	p = remove_vm_area((void *)(vaddr & PAGE_MASK));
	if (!p) {
		printk(KERN_ERR "%s: bad address %p\n", __func__, addr);
		return;
	}

	kfree(p);
}
EXPORT_SYMBOL(__iounmap);
