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
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/tlbflush.h>

static inline void remap_area_pte(pte_t * pte, unsigned long address,
	phys_addr_t size, phys_addr_t phys_addr, unsigned long flags)
{
	phys_addr_t end;
	unsigned long pfn;
	pgprot_t pgprot = __pgprot(_PAGE_GLOBAL | _PAGE_PRESENT | __READABLE
				   | __WRITEABLE | flags);

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	BUG_ON(address >= end);
	pfn = phys_addr >> PAGE_SHIFT;
	do {
		if (!pte_none(*pte)) {
			printk("remap_area_pte: page already exists\n");
			BUG();
		}
		set_pte(pte, pfn_pte(pfn, pgprot));
		address += PAGE_SIZE;
		pfn++;
		pte++;
	} while (address && (address < end));
}

static inline int remap_area_pmd(pmd_t * pmd, unsigned long address,
	phys_addr_t size, phys_addr_t phys_addr, unsigned long flags)
{
	phys_addr_t end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	BUG_ON(address >= end);
	do {
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr, flags);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int remap_area_pages(unsigned long address, phys_addr_t phys_addr,
	phys_addr_t size, unsigned long flags)
{
	int error;
	pgd_t * dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	BUG_ON(address >= end);
	do {
		p4d_t *p4d;
		pud_t *pud;
		pmd_t *pmd;

		error = -ENOMEM;
		p4d = p4d_alloc(&init_mm, dir, address);
		if (!p4d)
			break;
		pud = pud_alloc(&init_mm, p4d, address);
		if (!pud)
			break;
		pmd = pmd_alloc(&init_mm, pud, address);
		if (!pmd)
			break;
		if (remap_area_pmd(pmd, address, end - address,
					 phys_addr + address, flags))
			break;
		error = 0;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));
	flush_tlb_all();
	return error;
}

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
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */

#define IS_LOW512(addr) (!((phys_addr_t)(addr) & (phys_addr_t) ~0x1fffffffULL))

void __iomem * __ioremap(phys_addr_t phys_addr, phys_addr_t size, unsigned long flags)
{
	unsigned long offset, pfn, last_pfn;
	struct vm_struct * area;
	phys_addr_t last_addr;
	void * addr;

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
	addr = area->addr;
	if (remap_area_pages((unsigned long) addr, phys_addr, size, flags)) {
		vunmap(addr);
		return NULL;
	}

	return (void __iomem *) (offset + (char *)addr);
}

#define IS_KSEG1(addr) (((unsigned long)(addr) & ~0x1fffffffUL) == CKSEG1)

void __iounmap(const volatile void __iomem *addr)
{
	struct vm_struct *p;

	if (IS_KSEG1(addr))
		return;

	p = remove_vm_area((void *) (PAGE_MASK & (unsigned long __force) addr));
	if (!p)
		printk(KERN_ERR "iounmap: bad address %p\n", addr);

	kfree(p);
}

EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(__iounmap);
