// SPDX-License-Identifier: GPL-2.0

#include <asm/pgalloc.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/pgtable.h>

int ptep_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pte_t *ptep,
			  pte_t entry, int dirty)
{
	if (!pte_same(ptep_get(ptep), entry))
		__set_pte_at(vma->vm_mm, ptep, entry);
	/*
	 * update_mmu_cache will unconditionally execute, handling both
	 * the case that the PTE changed and the spurious fault case.
	 */
	return true;
}

int ptep_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long address,
			      pte_t *ptep)
{
	if (!pte_young(ptep_get(ptep)))
		return 0;
	return test_and_clear_bit(_PAGE_ACCESSED_OFFSET, &pte_val(*ptep));
}
EXPORT_SYMBOL_GPL(ptep_test_and_clear_young);

#ifdef CONFIG_64BIT
pud_t *pud_offset(p4d_t *p4d, unsigned long address)
{
	if (pgtable_l4_enabled)
		return p4d_pgtable(p4dp_get(p4d)) + pud_index(address);

	return (pud_t *)p4d;
}

p4d_t *p4d_offset(pgd_t *pgd, unsigned long address)
{
	if (pgtable_l5_enabled)
		return pgd_pgtable(pgdp_get(pgd)) + p4d_index(address);

	return (p4d_t *)pgd;
}
#endif

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
int p4d_set_huge(p4d_t *p4d, phys_addr_t addr, pgprot_t prot)
{
	return 0;
}

void p4d_clear_huge(p4d_t *p4d)
{
}

int pud_set_huge(pud_t *pud, phys_addr_t phys, pgprot_t prot)
{
	pud_t new_pud = pfn_pud(__phys_to_pfn(phys), prot);

	set_pud(pud, new_pud);
	return 1;
}

int pud_clear_huge(pud_t *pud)
{
	if (!pud_leaf(pudp_get(pud)))
		return 0;
	pud_clear(pud);
	return 1;
}

int pud_free_pmd_page(pud_t *pud, unsigned long addr)
{
	pmd_t *pmd = pud_pgtable(pudp_get(pud));
	int i;

	pud_clear(pud);

	flush_tlb_kernel_range(addr, addr + PUD_SIZE);

	for (i = 0; i < PTRS_PER_PMD; i++) {
		if (!pmd_none(pmd[i])) {
			pte_t *pte = (pte_t *)pmd_page_vaddr(pmd[i]);

			pte_free_kernel(NULL, pte);
		}
	}

	pmd_free(NULL, pmd);

	return 1;
}

int pmd_set_huge(pmd_t *pmd, phys_addr_t phys, pgprot_t prot)
{
	pmd_t new_pmd = pfn_pmd(__phys_to_pfn(phys), prot);

	set_pmd(pmd, new_pmd);
	return 1;
}

int pmd_clear_huge(pmd_t *pmd)
{
	if (!pmd_leaf(pmdp_get(pmd)))
		return 0;
	pmd_clear(pmd);
	return 1;
}

int pmd_free_pte_page(pmd_t *pmd, unsigned long addr)
{
	pte_t *pte = (pte_t *)pmd_page_vaddr(pmdp_get(pmd));

	pmd_clear(pmd);

	flush_tlb_kernel_range(addr, addr + PMD_SIZE);
	pte_free_kernel(NULL, pte);
	return 1;
}

#endif /* CONFIG_HAVE_ARCH_HUGE_VMAP */
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
pmd_t pmdp_collapse_flush(struct vm_area_struct *vma,
					unsigned long address, pmd_t *pmdp)
{
	pmd_t pmd = pmdp_huge_get_and_clear(vma->vm_mm, address, pmdp);

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	VM_BUG_ON(pmd_trans_huge(pmdp_get(pmdp)));
	/*
	 * When leaf PTE entries (regular pages) are collapsed into a leaf
	 * PMD entry (huge page), a valid non-leaf PTE is converted into a
	 * valid leaf PTE at the level 1 page table.  Since the sfence.vma
	 * forms that specify an address only apply to leaf PTEs, we need a
	 * global flush here.  collapse_huge_page() assumes these flushes are
	 * eager, so just do the fence here.
	 */
	flush_tlb_mm(vma->vm_mm);
	return pmd;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
