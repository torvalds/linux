#ifndef _ASM_POWERPC_PTE_WALK_H
#define _ASM_POWERPC_PTE_WALK_H

#include <linux/sched.h>

/* Don't use this directly */
extern pte_t *__find_linux_pte(pgd_t *pgdir, unsigned long ea,
			       bool *is_thp, unsigned *hshift);

static inline pte_t *find_linux_pte(pgd_t *pgdir, unsigned long ea,
				    bool *is_thp, unsigned *hshift)
{
	pte_t *pte;

	VM_WARN(!arch_irqs_disabled(), "%s called with irq enabled\n", __func__);
	pte = __find_linux_pte(pgdir, ea, is_thp, hshift);

#if defined(CONFIG_DEBUG_VM) &&						\
	!(defined(CONFIG_HUGETLB_PAGE) || defined(CONFIG_TRANSPARENT_HUGEPAGE))
	/*
	 * We should not find huge page if these configs are not enabled.
	 */
	if (hshift)
		WARN_ON(*hshift);
#endif
	return pte;
}

static inline pte_t *find_init_mm_pte(unsigned long ea, unsigned *hshift)
{
	pgd_t *pgdir = init_mm.pgd;
	return __find_linux_pte(pgdir, ea, NULL, hshift);
}

/*
 * Convert a kernel vmap virtual address (vmalloc or ioremap space) to a
 * physical address, without taking locks. This can be used in real-mode.
 */
static inline phys_addr_t ppc_find_vmap_phys(unsigned long addr)
{
	pte_t *ptep;
	phys_addr_t pa;
	int hugepage_shift;

	/*
	 * init_mm does not free page tables, and does not do THP. It may
	 * have huge pages from huge vmalloc / ioremap etc.
	 */
	ptep = find_init_mm_pte(addr, &hugepage_shift);
	if (WARN_ON(!ptep))
		return 0;

	pa = PFN_PHYS(pte_pfn(*ptep));

	if (!hugepage_shift)
		hugepage_shift = PAGE_SHIFT;

	pa |= addr & ((1ul << hugepage_shift) - 1);

	return pa;
}

#endif /* _ASM_POWERPC_PTE_WALK_H */
