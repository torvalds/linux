// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/memblock.h>
#include <linux/memremap.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pfn.h>
#include <linux/hardirq.h>
#include <linux/gfp.h>
#include <linux/initrd.h>
#include <linux/mmzone.h>

#include <asm/asm-offsets.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/dma.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

/*
 * We have up to 8 empty zeroed pages so we can map one of the right colour
 * when needed.	 Since page is never written to after the initialization we
 * don't have to care about aliases on other CPUs.
 */
unsigned long empty_zero_page, zero_page_mask;
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(zero_page_mask);

void setup_zero_pages(void)
{
	unsigned int order, i;
	struct page *page;

	order = 0;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	page = virt_to_page((void *)empty_zero_page);
	split_page(page, order);
	for (i = 0; i < (1 << order); i++, page++)
		mark_page_reserved(page);

	zero_page_mask = ((PAGE_SIZE << order) - 1) & PAGE_MASK;
}

void copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	void *vfrom, *vto;

	vfrom = kmap_local_page(from);
	vto = kmap_local_page(to);
	copy_page(vto, vfrom);
	kunmap_local(vfrom);
	kunmap_local(vto);
	/* Make sure this page is cleared on other CPU's too before using it */
	smp_wmb();
}

int __ref page_is_ram(unsigned long pfn)
{
	unsigned long addr = PFN_PHYS(pfn);

	return memblock_is_memory(addr) && !memblock_is_reserved(addr);
}

#ifndef CONFIG_NUMA
void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
#endif
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;

	free_area_init(max_zone_pfns);
}

void __init mem_init(void)
{
	max_mapnr = max_low_pfn;
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);

	memblock_free_all();
	setup_zero_pages();	/* Setup zeroed pages.  */
}
#endif /* !CONFIG_NUMA */

void __ref free_initmem(void)
{
	free_initmem_default(POISON_FREE_INITMEM);
}

#ifdef CONFIG_MEMORY_HOTPLUG
int arch_add_memory(int nid, u64 start, u64 size, struct mhp_params *params)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	ret = __add_pages(nid, start_pfn, nr_pages, params);

	if (ret)
		pr_warn("%s: Problem encountered in __add_pages() as ret=%d\n",
				__func__,  ret);

	return ret;
}

void arch_remove_memory(u64 start, u64 size, struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	struct page *page = pfn_to_page(start_pfn);

	/* With altmap the first mapped page is offset from @start */
	if (altmap)
		page += vmem_altmap_offset(altmap);
	__remove_pages(start_pfn, nr_pages, altmap);
}

#ifdef CONFIG_NUMA
int memory_add_physaddr_to_nid(u64 start)
{
	return pa_to_nid(start);
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif
#endif

static pte_t *fixmap_pte(unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset_k(addr);
	p4d = p4d_offset(pgd, addr);

	if (pgd_none(*pgd)) {
		pud_t *new __maybe_unused;

		new = memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
		pgd_populate(&init_mm, pgd, new);
#ifndef __PAGETABLE_PUD_FOLDED
		pud_init((unsigned long)new, (unsigned long)invalid_pmd_table);
#endif
	}

	pud = pud_offset(p4d, addr);
	if (pud_none(*pud)) {
		pmd_t *new __maybe_unused;

		new = memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
		pud_populate(&init_mm, pud, new);
#ifndef __PAGETABLE_PMD_FOLDED
		pmd_init((unsigned long)new, (unsigned long)invalid_pte_table);
#endif
	}

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		pte_t *new __maybe_unused;

		new = memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
		pmd_populate_kernel(&init_mm, pmd, new);
	}

	return pte_offset_kernel(pmd, addr);
}

void __init __set_fixmap(enum fixed_addresses idx,
			       phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *ptep;

	BUG_ON(idx <= FIX_HOLE || idx >= __end_of_fixed_addresses);

	ptep = fixmap_pte(addr);
	if (!pte_none(*ptep)) {
		pte_ERROR(*ptep);
		return;
	}

	if (pgprot_val(flags))
		set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, flags));
	else {
		pte_clear(&init_mm, addr, ptep);
		flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	}
}

/*
 * Align swapper_pg_dir in to 64K, allows its address to be loaded
 * with a single LUI instruction in the TLB handlers.  If we used
 * __aligned(64K), its size would get rounded up to the alignment
 * size, and waste space.  So we place it in its own section and align
 * it in the linker script.
 */
pgd_t swapper_pg_dir[_PTRS_PER_PGD] __section(".bss..swapper_pg_dir");

pgd_t invalid_pg_dir[_PTRS_PER_PGD] __page_aligned_bss;
#ifndef __PAGETABLE_PUD_FOLDED
pud_t invalid_pud_table[PTRS_PER_PUD] __page_aligned_bss;
EXPORT_SYMBOL(invalid_pud_table);
#endif
#ifndef __PAGETABLE_PMD_FOLDED
pmd_t invalid_pmd_table[PTRS_PER_PMD] __page_aligned_bss;
EXPORT_SYMBOL(invalid_pmd_table);
#endif
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned_bss;
EXPORT_SYMBOL(invalid_pte_table);
