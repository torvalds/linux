/*
 * linux/arch/unicore32/mm/ioremap.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Re-map IO memory to kernel address space so that we can access it.
 *
 * This allows a driver to remap an arbitrary region of bus memory into
 * virtual space.  One should *only* use readl, writel, memcpy_toio and
 * so on with such remapped areas.
 *
 * Because UniCore only has a 32-bit address space we can't address the
 * whole of the (physical) PCI space at once.  PCI huge-mode addressing
 * allows us to circumvent this restriction by splitting PCI space into
 * two 2GB chunks and mapping only one at a time into processor memory.
 * We use MMU protection domains to trap any attempt to access the bank
 * that is not currently mapped.  (This isn't fully implemented yet.)
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/io.h>

#include <asm/cputype.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/sizes.h>

#include <mach/map.h>
#include "mm.h"

/*
 * Used by ioremap() and iounmap() code to mark (super)section-mapped
 * I/O regions in vm_struct->flags field.
 */
#define VM_UNICORE_SECTION_MAPPING	0x80000000

int ioremap_page(unsigned long virt, unsigned long phys,
		 const struct mem_type *mtype)
{
	return ioremap_page_range(virt, virt + PAGE_SIZE, phys,
				  __pgprot(mtype->prot_pte));
}
EXPORT_SYMBOL(ioremap_page);

/*
 * Section support is unsafe on SMP - If you iounmap and ioremap a region,
 * the other CPUs will not see this change until their next context switch.
 * Meanwhile, (eg) if an interrupt comes in on one of those other CPUs
 * which requires the new ioremap'd region to be referenced, the CPU will
 * reference the _old_ region.
 *
 * Note that get_vm_area_caller() allocates a guard 4K page, so we need to
 * mask the size back to 4MB aligned or we will overflow in the loop below.
 */
static void unmap_area_sections(unsigned long virt, unsigned long size)
{
	unsigned long addr = virt, end = virt + (size & ~(SZ_4M - 1));
	pgd_t *pgd;

	flush_cache_vunmap(addr, end);
	pgd = pgd_offset_k(addr);
	do {
		pmd_t pmd, *pmdp = pmd_offset((pud_t *)pgd, addr);

		pmd = *pmdp;
		if (!pmd_none(pmd)) {
			/*
			 * Clear the PMD from the page table, and
			 * increment the kvm sequence so others
			 * notice this change.
			 *
			 * Note: this is still racy on SMP machines.
			 */
			pmd_clear(pmdp);

			/*
			 * Free the page table, if there was one.
			 */
			if ((pmd_val(pmd) & PMD_TYPE_MASK) == PMD_TYPE_TABLE)
				pte_free_kernel(&init_mm, pmd_page_vaddr(pmd));
		}

		addr += PGDIR_SIZE;
		pgd++;
	} while (addr < end);

	flush_tlb_kernel_range(virt, end);
}

static int
remap_area_sections(unsigned long virt, unsigned long pfn,
		    size_t size, const struct mem_type *type)
{
	unsigned long addr = virt, end = virt + size;
	pgd_t *pgd;

	/*
	 * Remove and free any PTE-based mapping, and
	 * sync the current kernel mapping.
	 */
	unmap_area_sections(virt, size);

	pgd = pgd_offset_k(addr);
	do {
		pmd_t *pmd = pmd_offset((pud_t *)pgd, addr);

		set_pmd(pmd, __pmd(__pfn_to_phys(pfn) | type->prot_sect));
		pfn += SZ_4M >> PAGE_SHIFT;
		flush_pmd_entry(pmd);

		addr += PGDIR_SIZE;
		pgd++;
	} while (addr < end);

	return 0;
}

void __iomem *__uc32_ioremap_pfn_caller(unsigned long pfn,
	unsigned long offset, size_t size, unsigned int mtype, void *caller)
{
	const struct mem_type *type;
	int err;
	unsigned long addr;
	struct vm_struct *area;

	/*
	 * High mappings must be section aligned
	 */
	if (pfn >= 0x100000 && (__pfn_to_phys(pfn) & ~SECTION_MASK))
		return NULL;

	/*
	 * Don't allow RAM to be mapped
	 */
	if (pfn_valid(pfn)) {
		printk(KERN_WARNING "BUG: Your driver calls ioremap() on\n"
			"system memory.  This leads to architecturally\n"
			"unpredictable behaviour, and ioremap() will fail in\n"
			"the next kernel release. Please fix your driver.\n");
		WARN_ON(1);
	}

	type = get_mem_type(mtype);
	if (!type)
		return NULL;

	/*
	 * Page align the mapping size, taking account of any offset.
	 */
	size = PAGE_ALIGN(offset + size);

	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		return NULL;
	addr = (unsigned long)area->addr;

	if (!((__pfn_to_phys(pfn) | size | addr) & ~PMD_MASK)) {
		area->flags |= VM_UNICORE_SECTION_MAPPING;
		err = remap_area_sections(addr, pfn, size, type);
	} else
		err = ioremap_page_range(addr, addr + size, __pfn_to_phys(pfn),
					 __pgprot(type->prot_pte));

	if (err) {
		vunmap((void *)addr);
		return NULL;
	}

	flush_cache_vmap(addr, addr + size);
	return (void __iomem *) (offset + addr);
}

void __iomem *__uc32_ioremap_caller(unsigned long phys_addr, size_t size,
	unsigned int mtype, void *caller)
{
	unsigned long last_addr;
	unsigned long offset = phys_addr & ~PAGE_MASK;
	unsigned long pfn = __phys_to_pfn(phys_addr);

	/*
	 * Don't allow wraparound or zero size
	 */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	return __uc32_ioremap_pfn_caller(pfn, offset, size, mtype, caller);
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem *
__uc32_ioremap_pfn(unsigned long pfn, unsigned long offset, size_t size,
		  unsigned int mtype)
{
	return __uc32_ioremap_pfn_caller(pfn, offset, size, mtype,
			__builtin_return_address(0));
}
EXPORT_SYMBOL(__uc32_ioremap_pfn);

void __iomem *
__uc32_ioremap(unsigned long phys_addr, size_t size)
{
	return __uc32_ioremap_caller(phys_addr, size, MT_DEVICE,
			__builtin_return_address(0));
}
EXPORT_SYMBOL(__uc32_ioremap);

void __iomem *
__uc32_ioremap_cached(unsigned long phys_addr, size_t size)
{
	return __uc32_ioremap_caller(phys_addr, size, MT_DEVICE_CACHED,
			__builtin_return_address(0));
}
EXPORT_SYMBOL(__uc32_ioremap_cached);

void __uc32_iounmap(volatile void __iomem *io_addr)
{
	void *addr = (void *)(PAGE_MASK & (unsigned long)io_addr);
	struct vm_struct **p, *tmp;

	/*
	 * If this is a section based mapping we need to handle it
	 * specially as the VM subsystem does not know how to handle
	 * such a beast. We need the lock here b/c we need to clear
	 * all the mappings before the area can be reclaimed
	 * by someone else.
	 */
	write_lock(&vmlist_lock);
	for (p = &vmlist ; (tmp = *p) ; p = &tmp->next) {
		if ((tmp->flags & VM_IOREMAP) && (tmp->addr == addr)) {
			if (tmp->flags & VM_UNICORE_SECTION_MAPPING) {
				unmap_area_sections((unsigned long)tmp->addr,
						    tmp->size);
			}
			break;
		}
	}
	write_unlock(&vmlist_lock);

	vunmap(addr);
}
EXPORT_SYMBOL(__uc32_iounmap);
