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
#include <linux/set_memory.h>

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
	if (pg) {
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
		v += PAGE_SIZE;
		p += PAGE_SIZE;
	}
}

void __init mapin_ram(void)
{
	phys_addr_t base, end;
	u64 i;

	for_each_mem_range(i, &base, &end) {
		phys_addr_t top = min(end, total_lowmem);

		if (base >= top)
			continue;
		base = mmu_mapin_ram(base, top);
		__mapin_ram_chunk(base, top);
	}
}

void mark_initmem_nx(void)
{
	unsigned long numpages = PFN_UP((unsigned long)_einittext) -
				 PFN_DOWN((unsigned long)_sinittext);

	if (v_block_mapped((unsigned long)_sinittext))
		mmu_mark_initmem_nx();
	else
		set_memory_attr((unsigned long)_sinittext, numpages, PAGE_KERNEL);
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void mark_rodata_ro(void)
{
	unsigned long numpages;

	if (v_block_mapped((unsigned long)_stext + 1)) {
		mmu_mark_rodata_ro();
		ptdump_check_wx();
		return;
	}

	numpages = PFN_UP((unsigned long)_etext) -
		   PFN_DOWN((unsigned long)_stext);

	set_memory_attr((unsigned long)_stext, numpages, PAGE_KERNEL_ROX);
	/*
	 * mark .rodata as read only. Use __init_begin rather than __end_rodata
	 * to cover NOTES and EXCEPTION_TABLE.
	 */
	numpages = PFN_UP((unsigned long)__init_begin) -
		   PFN_DOWN((unsigned long)__start_rodata);

	set_memory_attr((unsigned long)__start_rodata, numpages, PAGE_KERNEL_RO);

	// mark_initmem_nx() should have already run by now
	ptdump_check_wx();
}
#endif

#ifdef CONFIG_DEBUG_PAGEALLOC
void __kernel_map_pages(struct page *page, int numpages, int enable)
{
	unsigned long addr = (unsigned long)page_address(page);

	if (PageHighMem(page))
		return;

	if (enable)
		set_memory_attr(addr, numpages, PAGE_KERNEL);
	else
		set_memory_attr(addr, numpages, __pgprot(0));
}
#endif /* CONFIG_DEBUG_PAGEALLOC */
