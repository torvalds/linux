// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret, *init;

	ret = (pgd_t *) __get_free_page(GFP_KERNEL);
	if (ret) {
		init = pgd_offset(&init_mm, 0UL);
		pgd_init(ret);
		memcpy(ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
		       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
	}

	return ret;
}
EXPORT_SYMBOL_GPL(pgd_alloc);

void pgd_init(void *addr)
{
	unsigned long *p, *end;
	unsigned long entry;

#if !defined(__PAGETABLE_PUD_FOLDED)
	entry = (unsigned long)invalid_pud_table;
#elif !defined(__PAGETABLE_PMD_FOLDED)
	entry = (unsigned long)invalid_pmd_table;
#else
	entry = (unsigned long)invalid_pte_table;
#endif

	p = (unsigned long *)addr;
	end = p + PTRS_PER_PGD;

	do {
		p[0] = entry;
		p[1] = entry;
		p[2] = entry;
		p[3] = entry;
		p[4] = entry;
		p += 8;
		p[-3] = entry;
		p[-2] = entry;
		p[-1] = entry;
	} while (p != end);
}
EXPORT_SYMBOL_GPL(pgd_init);

#ifndef __PAGETABLE_PMD_FOLDED
void pmd_init(void *addr)
{
	unsigned long *p, *end;
	unsigned long pagetable = (unsigned long)invalid_pte_table;

	p = (unsigned long *)addr;
	end = p + PTRS_PER_PMD;

	do {
		p[0] = pagetable;
		p[1] = pagetable;
		p[2] = pagetable;
		p[3] = pagetable;
		p[4] = pagetable;
		p += 8;
		p[-3] = pagetable;
		p[-2] = pagetable;
		p[-1] = pagetable;
	} while (p != end);
}
EXPORT_SYMBOL_GPL(pmd_init);
#endif

#ifndef __PAGETABLE_PUD_FOLDED
void pud_init(void *addr)
{
	unsigned long *p, *end;
	unsigned long pagetable = (unsigned long)invalid_pmd_table;

	p = (unsigned long *)addr;
	end = p + PTRS_PER_PUD;

	do {
		p[0] = pagetable;
		p[1] = pagetable;
		p[2] = pagetable;
		p[3] = pagetable;
		p[4] = pagetable;
		p += 8;
		p[-3] = pagetable;
		p[-2] = pagetable;
		p[-1] = pagetable;
	} while (p != end);
}
EXPORT_SYMBOL_GPL(pud_init);
#endif

pmd_t mk_pmd(struct page *page, pgprot_t prot)
{
	pmd_t pmd;

	pmd_val(pmd) = (page_to_pfn(page) << _PFN_SHIFT) | pgprot_val(prot);

	return pmd;
}

void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		pmd_t *pmdp, pmd_t pmd)
{
	*pmdp = pmd;
	flush_tlb_all();
}

void __init pagetable_init(void)
{
	/* Initialize the entire pgd.  */
	pgd_init(swapper_pg_dir);
	pgd_init(invalid_pg_dir);
#ifndef __PAGETABLE_PUD_FOLDED
	pud_init(invalid_pud_table);
#endif
#ifndef __PAGETABLE_PMD_FOLDED
	pmd_init(invalid_pmd_table);
#endif
}
