// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * This file contains the routines setting up the linux page tables.
 *  -- paulus
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/slab.h>

#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/early_ioremap.h>

#include <mm/mmu_decl.h>

extern char etext[], _stext[], _sinittext[], _einittext[];

static u8 early_fixmap_pagetable[FIXMAP_PTE_SIZE] __page_aligned_data;

notrace void __init early_ioremap_init(void)
{
	unsigned long addr = ALIGN_DOWN(FIXADDR_START, PGDIR_SIZE);
	pte_t *ptep = (pte_t *)early_fixmap_pagetable;
	pmd_t *pmdp = pmd_off_k(addr);

	for (; (s32)(FIXADDR_TOP - addr) > 0;
	     addr += PGDIR_SIZE, ptep += PTRS_PER_PTE, pmdp++)
		pmd_populate_kernel(&init_mm, pmdp, ptep);

	early_ioremap_setup();
}

static void __init *early_alloc_pgtable(unsigned long size)
{
	void *ptr = memblock_alloc(size, size);

	if (!ptr)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, size, size);

	return ptr;
}

pte_t __init *early_pte_alloc_kernel(pmd_t *pmdp, unsigned long va)
{
	if (pmd_none(*pmdp)) {
		pte_t *ptep = early_alloc_pgtable(PTE_FRAG_SIZE);

		pmd_populate_kernel(&init_mm, pmdp, ptep);
	}
	return pte_offset_kernel(pmdp, va);
}


int __ref map_kernel_page(unsigned long va, phys_addr_t pa, pgprot_t prot)
{
	pmd_t *pd;
	pte_t *pg;
	int err = -ENOMEM;

	/* Use upper 10 bits of VA to index the first level map */
	pd = pmd_off_k(va);
	/* Use middle 10 bits of VA to index the second-level map */
	if (likely(slab_is_available()))
		pg = pte_alloc_kernel(pd, va);
	else
		pg = early_pte_alloc_kernel(pd, va);
	if (pg != 0) {
		err = 0;
		/* The PTE should never be already set nor present in the
		 * hash table
		 */
		BUG_ON((pte_present(*pg) | pte_hashpte(*pg)) && pgprot_val(prot));
		set_pte_at(&init_mm, va, pg, pfn_pte(pa >> PAGE_SHIFT, prot));
	}
	smp_wmb();
	return err;
}

/*
 * Map in a chunk of physical memory starting at start.
 */
static void __init __mapin_ram_chunk(unsigned long offset, unsigned long top)
{
	unsigned long v, s;
	phys_addr_t p;
	int ktext;

	s = offset;
	v = PAGE_OFFSET + s;
	p = memstart_addr + s;
	for (; s < top; s += PAGE_SIZE) {
		ktext = ((char *)v >= _stext && (char *)v < etext) ||
			((char *)v >= _sinittext && (char *)v < _einittext);
		map_kernel_page(v, p, ktext ? PAGE_KERNEL_TEXT : PAGE_KERNEL);
#ifdef CONFIG_PPC_BOOK3S_32
		if (ktext)
			hash_preload(&init_mm, v);
#endif
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

void __init mapin_ram(void)
{
	struct memblock_region *reg;

	for_each_memblock(memory, reg) {
		phys_addr_t base = reg->base;
		phys_addr_t top = min(base + reg->size, total_lowmem);

		if (base >= top)
			continue;
		base = mmu_mapin_ram(base, top);
		__mapin_ram_chunk(base, top);
	}
}

static int __change_page_attr_noflush(struct page *page, pgprot_t prot)
{
	pte_t *kpte;
	unsigned long address;

	BUG_ON(PageHighMem(page));
	address = (unsigned long)page_address(page);

	if (v_block_mapped(address))
		return 0;
	kpte = virt_to_kpte(address);
	if (!kpte)
		return -EINVAL;
	__set_pte_at(&init_mm, address, kpte, mk_pte(page, prot), 0);

	return 0;
}

/*
 * Change the page attributes of an page in the linear mapping.
 *
 * THIS DOES NOTHING WITH BAT MAPPINGS, DEBUG USE ONLY
 */
static int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	int i, err = 0;
	unsigned long flags;
	struct page *start = page;

	local_irq_save(flags);
	for (i = 0; i < numpages; i++, page++) {
		err = __change_page_attr_noflush(page, prot);
		if (err)
			break;
	}
	wmb();
	local_irq_restore(flags);
	flush_tlb_kernel_range((unsigned long)page_address(start),
			       (unsigned long)page_address(page));
	return err;
}

void mark_initmem_nx(void)
{
	struct page *page = virt_to_page(_sinittext);
	unsigned long numpages = PFN_UP((unsigned long)_einittext) -
				 PFN_DOWN((unsigned long)_sinittext);

	if (v_block_mapped((unsigned long)_sinittext))
		mmu_mark_initmem_nx();
	else
		change_page_attr(page, numpages, PAGE_KERNEL);
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void mark_rodata_ro(void)
{
	struct page *page;
	unsigned long numpages;

	if (v_block_mapped((unsigned long)_stext + 1)) {
		mmu_mark_rodata_ro();
		ptdump_check_wx();
		return;
	}

	page = virt_to_page(_stext);
	numpages = PFN_UP((unsigned long)_etext) -
		   PFN_DOWN((unsigned long)_stext);

	change_page_attr(page, numpages, PAGE_KERNEL_ROX);
	/*
	 * mark .rodata as read only. Use __init_begin rather than __end_rodata
	 * to cover NOTES and EXCEPTION_TABLE.
	 */
	page = virt_to_page(__start_rodata);
	numpages = PFN_UP((unsigned long)__init_begin) -
		   PFN_DOWN((unsigned long)__start_rodata);

	change_page_attr(page, numpages, PAGE_KERNEL_RO);

	// mark_initmem_nx() should have already run by now
	ptdump_check_wx();
}
#endif

#ifdef CONFIG_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;

	change_page_attr(page, numpages, enable ? PAGE_KERNEL : __pgprot(0));
}
#endif /* CONFIG_DEBUG_PAGEALLOC */
