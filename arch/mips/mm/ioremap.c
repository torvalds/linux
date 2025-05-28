/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2001, 2002 Ralf Baechle
 */
#include <linux/export.h>
#include <asm/addrspace.h>
#include <asm/byteorder.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm_types.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <ioremap.h>

#define IS_LOW512(addr) (!((phys_addr_t)(addr) & (phys_addr_t) ~0x1fffffffULL))
#define IS_KSEG1(addr) (((unsigned long)(addr) & ~0x1fffffffUL) == CKSEG1)

static int __ioremap_check_ram(unsigned long start_pfn, unsigned long nr_pages,
			       void *arg)
{
	unsigned long i;

	for (i = 0; i < nr_pages; i++) {
		if (pfn_valid(start_pfn + i) &&
		    !PageReserved(pfn_to_page(start_pfn + i)))
			return 1;
	}

	return 0;
}

/*
 * ioremap_prot     -   map bus memory into CPU space
 * @phys_addr:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_prot gives the caller control over cache coherency attributes (CCA)
 */
void __iomem *ioremap_prot(phys_addr_t phys_addr, unsigned long size,
			   pgprot_t prot)
{
	unsigned long flags = pgprot_val(prot) & _CACHE_MASK;
	unsigned long offset, pfn, last_pfn;
	struct vm_struct *area;
	phys_addr_t last_addr;
	unsigned long vaddr;
	void __iomem *cpu_addr;

	cpu_addr = plat_ioremap(phys_addr, size, flags);
	if (cpu_addr)
		return cpu_addr;

	phys_addr = fixup_bigphys_addr(phys_addr, size);

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Map uncached objects in the low 512mb of address space using KSEG1,
	 * otherwise map using page tables.
	 */
	if (IS_LOW512(phys_addr) && IS_LOW512(last_addr) &&
	    flags == _CACHE_UNCACHED)
		return (void __iomem *) CKSEG1ADDR(phys_addr);

	/* Early remaps should use the unmapped regions til' VM is available */
	if (WARN_ON_ONCE(!slab_is_available()))
		return NULL;

	/*
	 * Don't allow anybody to remap RAM that may be allocated by the page
	 * allocator, since that could lead to races & data clobbering.
	 */
	pfn = PFN_DOWN(phys_addr);
	last_pfn = PFN_DOWN(last_addr);
	if (walk_system_ram_range(pfn, last_pfn - pfn + 1, NULL,
				  __ioremap_check_ram) == 1) {
		WARN_ONCE(1, "ioremap on RAM at %pa - %pa\n",
			  &phys_addr, &last_addr);
		return NULL;
	}

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	vaddr = (unsigned long)area->addr;

	flags |= _PAGE_GLOBAL | _PAGE_PRESENT | __READABLE | __WRITEABLE;
	if (ioremap_page_range(vaddr, vaddr + size, phys_addr,
			__pgprot(flags))) {
		free_vm_area(area);
		return NULL;
	}

	return (void __iomem *)(vaddr + offset);
}
EXPORT_SYMBOL(ioremap_prot);

void iounmap(const volatile void __iomem *addr)
{
	if (!plat_iounmap(addr) && !IS_KSEG1(addr))
		vunmap((void *)((unsigned long)addr & PAGE_MASK));
}
EXPORT_SYMBOL(iounmap);
