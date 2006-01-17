/*
 * arch/sh/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2005, 2006 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 */
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/addrspace.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

static inline void remap_area_pte(pte_t * pte, unsigned long address,
	unsigned long size, unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;
	unsigned long pfn;
	pgprot_t pgprot = __pgprot(_PAGE_PRESENT | _PAGE_RW |
				   _PAGE_DIRTY | _PAGE_ACCESSED |
				   _PAGE_HW_SHARED | _PAGE_FLAGS_HARD | flags);

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	if (address >= end)
		BUG();
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
	unsigned long size, unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	phys_addr -= address;
	if (address >= end)
		BUG();
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

int remap_area_pages(unsigned long address, unsigned long phys_addr,
		     unsigned long size, unsigned long flags)
{
	int error;
	pgd_t * dir;
	unsigned long end = address + size;

	phys_addr -= address;
	dir = pgd_offset_k(address);
	flush_cache_all();
	if (address >= end)
		BUG();
	do {
		pud_t *pud;
		pmd_t *pmd;

		error = -ENOMEM;

		pud = pud_alloc(&init_mm, dir, address);
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

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem *__ioremap(unsigned long phys_addr, unsigned long size,
			unsigned long flags)
{
	struct vm_struct * area;
	unsigned long offset, last_addr, addr, orig_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Don't remap the low PCI/ISA area, it's always mapped..
	 */
	if (phys_addr >= 0xA0000 && last_addr < 0x100000)
		return (void __iomem *)phys_to_virt(phys_addr);

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory))
		return NULL;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	orig_addr = addr = (unsigned long)area->addr;

#ifdef CONFIG_32BIT
	/*
	 * First try to remap through the PMB once a valid VMA has been
	 * established. Smaller allocations (or the rest of the size
	 * remaining after a PMB mapping due to the size not being
	 * perfectly aligned on a PMB size boundary) are then mapped
	 * through the UTLB using conventional page tables.
	 *
	 * PMB entries are all pre-faulted.
	 */
	if (unlikely(size >= 0x1000000)) {
		unsigned long mapped = pmb_remap(addr, phys_addr, size, flags);

		if (likely(mapped)) {
			addr		+= mapped;
			phys_addr	+= mapped;
			size		-= mapped;
		}
	}
#endif

	if (likely(size))
		if (remap_area_pages(addr, phys_addr, size, flags)) {
			vunmap((void *)orig_addr);
			return NULL;
		}

	return (void __iomem *)(offset + (char *)orig_addr);
}
EXPORT_SYMBOL(__ioremap);

void __iounmap(void __iomem *addr)
{
	unsigned long vaddr = (unsigned long __force)addr;
	struct vm_struct *p;

	if (PXSEG(vaddr) < P3SEG)
		return;

#ifdef CONFIG_32BIT
	/*
	 * Purge any PMB entries that may have been established for this
	 * mapping, then proceed with conventional VMA teardown.
	 *
	 * XXX: Note that due to the way that remove_vm_area() does
	 * matching of the resultant VMA, we aren't able to fast-forward
	 * the address past the PMB space until the end of the VMA where
	 * the page tables reside. As such, unmap_vm_area() will be
	 * forced to linearly scan over the area until it finds the page
	 * tables where PTEs that need to be unmapped actually reside,
	 * which is far from optimal. Perhaps we need to use a separate
	 * VMA for the PMB mappings?
	 *					-- PFM.
	 */
	pmb_unmap(vaddr);
#endif

	p = remove_vm_area((void *)(vaddr & PAGE_MASK));
	if (!p) {
		printk(KERN_ERR "%s: bad address %p\n", __FUNCTION__, addr);
		return;
	}

	kfree(p);
}
EXPORT_SYMBOL(__iounmap);
