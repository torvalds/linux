/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PGALLOC_H
#define _ASM_MICROBLAZE_PGALLOC_H

#ifdef CONFIG_MMU

#include <linux/kernel.h>	/* For min/max macros */
#include <linux/highmem.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/cache.h>

#define PGDIR_ORDER	0

/*
 * This is handled very differently on MicroBlaze since out page tables
 * are all 0's and I want to be able to use these zero'd pages elsewhere
 * as well - it gives us quite a speedup.
 * -- Cort
 */
extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
} quicklists;

#define pgd_quicklist		(quicklists.pgd_cache)
#define pmd_quicklist		((unsigned long *)0)
#define pte_quicklist		(quicklists.pte_cache)
#define pgtable_cache_size	(quicklists.pgtable_cache_sz)

extern unsigned long *zero_cache; /* head linked list of pre-zero'd pages */
extern atomic_t zero_sz; /* # currently pre-zero'd pages */
extern atomic_t zeropage_hits; /* # zero'd pages request that we've done */
extern atomic_t zeropage_calls; /* # zero'd pages request that've been made */
extern atomic_t zerototal; /* # pages zero'd over time */

#define zero_quicklist		(zero_cache)
#define zero_cache_sz	 	(zero_sz)
#define zero_cache_calls	(zeropage_calls)
#define zero_cache_hits		(zeropage_hits)
#define zero_cache_total	(zerototal)

/*
 * return a pre-zero'd page from the list,
 * return NULL if none available -- Cort
 */
extern unsigned long get_zero_page_fast(void);

extern void __bad_pte(pmd_t *pmd);

extern inline pgd_t *get_pgd_slow(void)
{
	pgd_t *ret;

	ret = (pgd_t *)__get_free_pages(GFP_KERNEL, PGDIR_ORDER);
	if (ret != NULL)
		clear_page(ret);
	return ret;
}

extern inline pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	ret = pgd_quicklist;
	if (ret != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	} else
		ret = (unsigned long *)get_pgd_slow();
	return (pgd_t *)ret;
}

extern inline void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long **)pgd = pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

extern inline void free_pgd_slow(pgd_t *pgd)
{
	free_page((unsigned long)pgd);
}

#define pgd_free(mm, pgd)        free_pgd_fast(pgd)
#define pgd_alloc(mm)		get_pgd_fast()

#define pmd_pgtable(pmd)	pmd_page(pmd)

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
#define pmd_alloc_one_fast(mm, address)	({ BUG(); ((pmd_t *)1); })
#define pmd_alloc_one(mm, address)	({ BUG(); ((pmd_t *)2); })
/* FIXME two definition - look below */
#define pmd_free(mm, x)			do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
		unsigned long address)
{
	pte_t *pte;
	extern int mem_init_done;
	extern void *early_get_page(void);
	if (mem_init_done) {
		pte = (pte_t *)__get_free_page(GFP_KERNEL |
					__GFP_REPEAT | __GFP_ZERO);
	} else {
		pte = (pte_t *)early_get_page();
		if (pte)
			clear_page(pte);
	}
	return pte;
}

static inline struct page *pte_alloc_one(struct mm_struct *mm,
		unsigned long address)
{
	struct page *ptepage;

#ifdef CONFIG_HIGHPTE
	int flags = GFP_KERNEL | __GFP_HIGHMEM | __GFP_REPEAT;
#else
	int flags = GFP_KERNEL | __GFP_REPEAT;
#endif

	ptepage = alloc_pages(flags, 0);
	if (ptepage)
		clear_highpage(ptepage);
	return ptepage;
}

static inline pte_t *pte_alloc_one_fast(struct mm_struct *mm,
		unsigned long address)
{
	unsigned long *ret;

	ret = pte_quicklist;
	if (ret != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

extern inline void pte_free_fast(pte_t *pte)
{
	*(unsigned long **)pte = pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

extern inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

extern inline void pte_free_slow(struct page *ptepage)
{
	__free_page(ptepage);
}

extern inline void pte_free(struct mm_struct *mm, struct page *ptepage)
{
	__free_page(ptepage);
}

#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, (pte))

#define pmd_populate(mm, pmd, pte)	(pmd_val(*(pmd)) = page_address(pte))

#define pmd_populate_kernel(mm, pmd, pte) \
		(pmd_val(*(pmd)) = (unsigned long) (pte))

/*
 * We don't have any real pmd's, and this code never triggers because
 * the pgd will always be present..
 */
#define pmd_alloc_one(mm, address)	({ BUG(); ((pmd_t *)2); })
/*#define pmd_free(mm, x)			do { } while (0)*/
#define __pmd_free_tlb(tlb, x, addr)	do { } while (0)
#define pgd_populate(mm, pmd, pte)	BUG()

extern int do_check_pgt_cache(int, int);

#endif /* CONFIG_MMU */

#define check_pgt_cache()	do {} while (0)

#endif /* _ASM_MICROBLAZE_PGALLOC_H */
