// SPDX-License-Identifier: GPL-2.0
/*
 * Based upon linux/arch/m68k/mm/sun3mmu.c
 * Based upon linux/arch/ppc/mm/mmu_context.c
 *
 * Implementations of mm routines specific to the Coldfire MMU.
 *
 * Copyright (c) 2008 Freescale Semiconductor, Inc.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/bootmem.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/mcf_pgalloc.h>
#include <asm/tlbflush.h>

#define KMAPAREA(x)	((x >= VMALLOC_START) && (x < KMAP_END))

mm_context_t next_mmu_context;
unsigned long context_map[LAST_CONTEXT / BITS_PER_LONG + 1];
atomic_t nr_free_contexts;
struct mm_struct *context_mm[LAST_CONTEXT+1];
unsigned long num_pages;

/*
 * ColdFire paging_init derived from sun3.
 */
void __init paging_init(void)
{
	pgd_t *pg_dir;
	pte_t *pg_table;
	unsigned long address, size;
	unsigned long next_pgtable, bootmem_end;
	unsigned long zones_size[MAX_NR_ZONES];
	enum zone_type zone;
	int i;

	empty_zero_page = (void *) alloc_bootmem_pages(PAGE_SIZE);
	memset((void *) empty_zero_page, 0, PAGE_SIZE);

	pg_dir = swapper_pg_dir;
	memset(swapper_pg_dir, 0, sizeof(swapper_pg_dir));

	size = num_pages * sizeof(pte_t);
	size = (size + PAGE_SIZE) & ~(PAGE_SIZE-1);
	next_pgtable = (unsigned long) alloc_bootmem_pages(size);

	bootmem_end = (next_pgtable + size + PAGE_SIZE) & PAGE_MASK;
	pg_dir += PAGE_OFFSET >> PGDIR_SHIFT;

	address = PAGE_OFFSET;
	while (address < (unsigned long)high_memory) {
		pg_table = (pte_t *) next_pgtable;
		next_pgtable += PTRS_PER_PTE * sizeof(pte_t);
		pgd_val(*pg_dir) = (unsigned long) pg_table;
		pg_dir++;

		/* now change pg_table to kernel virtual addresses */
		for (i = 0; i < PTRS_PER_PTE; ++i, ++pg_table) {
			pte_t pte = pfn_pte(virt_to_pfn(address), PAGE_INIT);
			if (address >= (unsigned long) high_memory)
				pte_val(pte) = 0;

			set_pte(pg_table, pte);
			address += PAGE_SIZE;
		}
	}

	current->mm = NULL;

	for (zone = 0; zone < MAX_NR_ZONES; zone++)
		zones_size[zone] = 0x0;
	zones_size[ZONE_DMA] = num_pages;
	free_area_init(zones_size);
}

int cf_tlb_miss(struct pt_regs *regs, int write, int dtlb, int extension_word)
{
	unsigned long flags, mmuar, mmutr;
	struct mm_struct *mm;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	int asid;

	local_irq_save(flags);

	mmuar = (dtlb) ? mmu_read(MMUAR) :
		regs->pc + (extension_word * sizeof(long));

	mm = (!user_mode(regs) && KMAPAREA(mmuar)) ? &init_mm : current->mm;
	if (!mm) {
		local_irq_restore(flags);
		return -1;
	}

	pgd = pgd_offset(mm, mmuar);
	if (pgd_none(*pgd))  {
		local_irq_restore(flags);
		return -1;
	}

	pmd = pmd_offset(pgd, mmuar);
	if (pmd_none(*pmd)) {
		local_irq_restore(flags);
		return -1;
	}

	pte = (KMAPAREA(mmuar)) ? pte_offset_kernel(pmd, mmuar)
				: pte_offset_map(pmd, mmuar);
	if (pte_none(*pte) || !pte_present(*pte)) {
		local_irq_restore(flags);
		return -1;
	}

	if (write) {
		if (!pte_write(*pte)) {
			local_irq_restore(flags);
			return -1;
		}
		set_pte(pte, pte_mkdirty(*pte));
	}

	set_pte(pte, pte_mkyoung(*pte));
	asid = mm->context & 0xff;
	if (!pte_dirty(*pte) && !KMAPAREA(mmuar))
		set_pte(pte, pte_wrprotect(*pte));

	mmutr = (mmuar & PAGE_MASK) | (asid << MMUTR_IDN) | MMUTR_V;
	if ((mmuar < TASK_UNMAPPED_BASE) || (mmuar >= TASK_SIZE))
		mmutr |= (pte->pte & CF_PAGE_MMUTR_MASK) >> CF_PAGE_MMUTR_SHIFT;
	mmu_write(MMUTR, mmutr);

	mmu_write(MMUDR, (pte_val(*pte) & PAGE_MASK) |
		((pte->pte) & CF_PAGE_MMUDR_MASK) | MMUDR_SZ_8KB | MMUDR_X);

	if (dtlb)
		mmu_write(MMUOR, MMUOR_ACC | MMUOR_UAA);
	else
		mmu_write(MMUOR, MMUOR_ITLB | MMUOR_ACC | MMUOR_UAA);

	local_irq_restore(flags);
	return 0;
}

void __init cf_bootmem_alloc(void)
{
	unsigned long start_pfn;
	unsigned long memstart;

	/* _rambase and _ramend will be naturally page aligned */
	m68k_memory[0].addr = _rambase;
	m68k_memory[0].size = _ramend - _rambase;

	/* compute total pages in system */
	num_pages = PFN_DOWN(_ramend - _rambase);

	/* page numbers */
	memstart = PAGE_ALIGN(_ramstart);
	min_low_pfn = PFN_DOWN(_rambase);
	start_pfn = PFN_DOWN(memstart);
	max_pfn = max_low_pfn = PFN_DOWN(_ramend);
	high_memory = (void *)_ramend;

	m68k_virt_to_node_shift = fls(_ramend - 1) - 6;
	module_fixup(NULL, __start_fixup, __stop_fixup);

	/* setup bootmem data */
	m68k_setup_node(0);
	memstart += init_bootmem_node(NODE_DATA(0), start_pfn,
		min_low_pfn, max_low_pfn);
	free_bootmem_node(NODE_DATA(0), memstart, _ramend - memstart);
}

/*
 * Initialize the context management stuff.
 * The following was taken from arch/ppc/mmu_context.c
 */
void __init mmu_context_init(void)
{
	/*
	 * Some processors have too few contexts to reserve one for
	 * init_mm, and require using context 0 for a normal task.
	 * Other processors reserve the use of context zero for the kernel.
	 * This code assumes FIRST_CONTEXT < 32.
	 */
	context_map[0] = (1 << FIRST_CONTEXT) - 1;
	next_mmu_context = FIRST_CONTEXT;
	atomic_set(&nr_free_contexts, LAST_CONTEXT - FIRST_CONTEXT + 1);
}

/*
 * Steal a context from a task that has one at the moment.
 * This is only used on 8xx and 4xx and we presently assume that
 * they don't do SMP.  If they do then thicfpgalloc.hs will have to check
 * whether the MM we steal is in use.
 * We also assume that this is only used on systems that don't
 * use an MMU hash table - this is true for 8xx and 4xx.
 * This isn't an LRU system, it just frees up each context in
 * turn (sort-of pseudo-random replacement :).  This would be the
 * place to implement an LRU scheme if anyone was motivated to do it.
 *  -- paulus
 */
void steal_context(void)
{
	struct mm_struct *mm;
	/*
	 * free up context `next_mmu_context'
	 * if we shouldn't free context 0, don't...
	 */
	if (next_mmu_context < FIRST_CONTEXT)
		next_mmu_context = FIRST_CONTEXT;
	mm = context_mm[next_mmu_context];
	flush_tlb_mm(mm);
	destroy_context(mm);
}

