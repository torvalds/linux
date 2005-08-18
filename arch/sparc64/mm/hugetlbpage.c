/*
 * SPARC64 Huge TLB page support.
 *
 * Copyright (C) 2002, 2003 David S. Miller (davem@redhat.com)
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/sysctl.h>

#include <asm/mman.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>

pte_t *huge_pte_alloc(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd) {
		pud = pud_offset(pgd, addr);
		if (pud) {
			pmd = pmd_alloc(mm, pud, addr);
			if (pmd)
				pte = pte_alloc_map(mm, pmd, addr);
		}
	}
	return pte;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd) {
		pud = pud_offset(pgd, addr);
		if (pud) {
			pmd = pmd_offset(pud, addr);
			if (pmd)
				pte = pte_offset_map(pmd, addr);
		}
	}
	return pte;
}

#define mk_pte_huge(entry) do { pte_val(entry) |= _PAGE_SZHUGE; } while (0)

void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
		     pte_t *ptep, pte_t entry)
{
	int i;

	for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
		set_pte_at(mm, addr, ptep, entry);
		ptep++;
		addr += PAGE_SIZE;
		pte_val(entry) += PAGE_SIZE;
	}
}

pte_t huge_ptep_get_and_clear(struct mm_struct *mm, unsigned long addr,
			      pte_t *ptep)
{
	pte_t entry;
	int i;

	entry = *ptep;

	for (i = 0; i < (1 << HUGETLB_PAGE_ORDER); i++) {
		pte_clear(mm, addr, ptep);
		addr += PAGE_SIZE;
		ptep++;
	}

	return entry;
}

/*
 * This function checks for proper alignment of input addr and len parameters.
 */
int is_aligned_hugepage_range(unsigned long addr, unsigned long len)
{
	if (len & ~HPAGE_MASK)
		return -EINVAL;
	if (addr & ~HPAGE_MASK)
		return -EINVAL;
	return 0;
}

struct page *follow_huge_addr(struct mm_struct *mm,
			      unsigned long address, int write)
{
	return ERR_PTR(-EINVAL);
}

int pmd_huge(pmd_t pmd)
{
	return 0;
}

struct page *follow_huge_pmd(struct mm_struct *mm, unsigned long address,
			     pmd_t *pmd, int write)
{
	return NULL;
}

static void context_reload(void *__data)
{
	struct mm_struct *mm = __data;

	if (mm == current->mm)
		load_secondary_context(mm);
}

void hugetlb_prefault_arch_hook(struct mm_struct *mm)
{
	/* On UltraSPARC-III+ and later, configure the second half of
	 * the Data-TLB for huge pages.
	 */
	if (tlb_type == cheetah_plus) {
		unsigned long ctx;

		spin_lock(&ctx_alloc_lock);
		ctx = mm->context.sparc64_ctx_val;
		ctx &= ~CTX_PGSZ_MASK;
		ctx |= CTX_PGSZ_BASE << CTX_PGSZ0_SHIFT;
		ctx |= CTX_PGSZ_HUGE << CTX_PGSZ1_SHIFT;

		if (ctx != mm->context.sparc64_ctx_val) {
			/* When changing the page size fields, we
			 * must perform a context flush so that no
			 * stale entries match.  This flush must
			 * occur with the original context register
			 * settings.
			 */
			do_flush_tlb_mm(mm);

			/* Reload the context register of all processors
			 * also executing in this address space.
			 */
			mm->context.sparc64_ctx_val = ctx;
			on_each_cpu(context_reload, mm, 0, 0);
		}
		spin_unlock(&ctx_alloc_lock);
	}
}
