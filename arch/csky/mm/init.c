// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/bug.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pfn.h>
#include <linux/initrd.h>

#include <asm/setup.h>
#include <asm/cachectl.h>
#include <asm/dma.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/tlb.h>
#include <asm/cacheflush.h>

#define PTRS_KERN_TABLE \
		((PTRS_PER_PGD - USER_PTRS_PER_PGD) * PTRS_PER_PTE)

pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned_bss;
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned_bss;
pte_t kernel_pte_tables[PTRS_KERN_TABLE] __page_aligned_bss;

EXPORT_SYMBOL(invalid_pte_table);
unsigned long empty_zero_page[PAGE_SIZE / sizeof(unsigned long)]
						__page_aligned_bss;
EXPORT_SYMBOL(empty_zero_page);

void __init mem_init(void)
{
#ifdef CONFIG_HIGHMEM
	unsigned long tmp;
#endif

	memblock_free_all();

#ifdef CONFIG_HIGHMEM
	for (tmp = highstart_pfn; tmp < highend_pfn; tmp++) {
		struct page *page = pfn_to_page(tmp);

		/* FIXME not sure about */
		if (!memblock_is_reserved(tmp << PAGE_SHIFT))
			free_highmem_page(page);
	}
#endif
}

void free_initmem(void)
{
	free_initmem_default(-1);
}

void pgd_init(unsigned long *p)
{
	int i;

	for (i = 0; i < PTRS_PER_PGD; i++)
		p[i] = __pa(invalid_pte_table);

	flush_tlb_all();
	local_icache_inv_all(NULL);
}

void __init mmu_init(unsigned long min_pfn, unsigned long max_pfn)
{
	int i;

	for (i = 0; i < USER_PTRS_PER_PGD; i++)
		swapper_pg_dir[i].pgd = __pa(invalid_pte_table);

	for (i = USER_PTRS_PER_PGD; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i].pgd =
			__pa(kernel_pte_tables + (PTRS_PER_PTE * (i - USER_PTRS_PER_PGD)));

	for (i = 0; i < PTRS_KERN_TABLE; i++)
		set_pte(&kernel_pte_tables[i], __pte(_PAGE_GLOBAL));

	for (i = min_pfn; i < max_pfn; i++)
		set_pte(&kernel_pte_tables[i - PFN_DOWN(va_pa_offset)], pfn_pte(i, PAGE_KERNEL));

	flush_tlb_all();
	local_icache_inv_all(NULL);

	/* Setup page mask to 4k */
	write_mmu_pagemask(0);

	setup_pgd(swapper_pg_dir, 0);
}

void __init fixrange_init(unsigned long start, unsigned long end,
			pgd_t *pgd_base)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int i, j, k;
	unsigned long vaddr;

	vaddr = start;
	i = pgd_index(vaddr);
	j = pud_index(vaddr);
	k = pmd_index(vaddr);
	pgd = pgd_base + i;

	for ( ; (i < PTRS_PER_PGD) && (vaddr != end); pgd++, i++) {
		pud = (pud_t *)pgd;
		for ( ; (j < PTRS_PER_PUD) && (vaddr != end); pud++, j++) {
			pmd = (pmd_t *)pud;
			for (; (k < PTRS_PER_PMD) && (vaddr != end); pmd++, k++) {
				if (pmd_none(*pmd)) {
					pte = (pte_t *) memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
					if (!pte)
						panic("%s: Failed to allocate %lu bytes align=%lx\n",
						      __func__, PAGE_SIZE,
						      PAGE_SIZE);

					set_pmd(pmd, __pmd(__pa(pte)));
					BUG_ON(pte != pte_offset_kernel(pmd, 0));
				}
				vaddr += PMD_SIZE;
			}
			k = 0;
		}
		j = 0;
	}
}

void __init fixaddr_init(void)
{
	unsigned long vaddr;

	vaddr = __fix_to_virt(__end_of_fixed_addresses - 1) & PMD_MASK;
	fixrange_init(vaddr, vaddr + PMD_SIZE, swapper_pg_dir);
}

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READ,
	[VM_WRITE]					= PAGE_READ,
	[VM_WRITE | VM_READ]				= PAGE_READ,
	[VM_EXEC]					= PAGE_READ,
	[VM_EXEC | VM_READ]				= PAGE_READ,
	[VM_EXEC | VM_WRITE]				= PAGE_READ,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_READ,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READ,
	[VM_SHARED | VM_WRITE]				= PAGE_WRITE,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_WRITE,
	[VM_SHARED | VM_EXEC]				= PAGE_READ,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READ,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_WRITE,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_WRITE
};
DECLARE_VM_GET_PAGE_PROT
