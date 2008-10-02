#ifndef _XEN_MMU_H

#include <linux/linkage.h>
#include <asm/page.h>

enum pt_level {
	PT_PGD,
	PT_PUD,
	PT_PMD,
	PT_PTE
};


void set_pte_mfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags);


void xen_activate_mm(struct mm_struct *prev, struct mm_struct *next);
void xen_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm);
void xen_exit_mmap(struct mm_struct *mm);

void xen_pgd_pin(pgd_t *pgd);
//void xen_pgd_unpin(pgd_t *pgd);

pteval_t xen_pte_val(pte_t);
pmdval_t xen_pmd_val(pmd_t);
pgdval_t xen_pgd_val(pgd_t);

pte_t xen_make_pte(pteval_t);
pmd_t xen_make_pmd(pmdval_t);
pgd_t xen_make_pgd(pgdval_t);

void xen_set_pte(pte_t *ptep, pte_t pteval);
void xen_set_pte_at(struct mm_struct *mm, unsigned long addr,
		    pte_t *ptep, pte_t pteval);

#ifdef CONFIG_X86_PAE
void xen_set_pte_atomic(pte_t *ptep, pte_t pte);
void xen_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep);
void xen_pmd_clear(pmd_t *pmdp);
#endif	/* CONFIG_X86_PAE */

void xen_set_pmd(pmd_t *pmdp, pmd_t pmdval);
void xen_set_pud(pud_t *ptr, pud_t val);
void xen_set_pmd_hyper(pmd_t *pmdp, pmd_t pmdval);
void xen_set_pud_hyper(pud_t *ptr, pud_t val);

#if PAGETABLE_LEVELS == 4
pudval_t xen_pud_val(pud_t pud);
pud_t xen_make_pud(pudval_t pudval);
void xen_set_pgd(pgd_t *pgdp, pgd_t pgd);
void xen_set_pgd_hyper(pgd_t *pgdp, pgd_t pgd);
#endif

pgd_t *xen_get_user_pgd(pgd_t *pgd);

pte_t xen_ptep_modify_prot_start(struct mm_struct *mm, unsigned long addr, pte_t *ptep);
void  xen_ptep_modify_prot_commit(struct mm_struct *mm, unsigned long addr,
				  pte_t *ptep, pte_t pte);

#endif	/* _XEN_MMU_H */
